/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-16
 * function: rk ftl v5 for rockchip soc base on arm v8 to support MLC NAND.
 */
	.file	"rk_ftlv5_arm64.S"
	.text
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	stp	x29, x30, [sp, -32]!
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x2, x1, x0
	add	x29, sp, 0
	str	x19, [sp, 16]
	ldr	x0, [x1, x0]
	ldrb	w19, [x2, 8]
	add	x19, x0, x19, lsl 8
	mov	w0, 122
	str	w0, [x19, 2056]
	mov	x0, 400
	bl	__const_udelay
	ldr	w1, [x19, 2048]
	ldr	w0, [x19, 2048]
	and	w1, w1, 15
	and	w0, w0, 15
	cmp	w1, w0
	csel	w1, w1, w0, cs
	ldr	w0, [x19, 2048]
	ldr	w2, [x19, 2048]
	and	w0, w0, 15
	ldr	x19, [sp, 16]
	and	w2, w2, 15
	cmp	w0, w2
	csel	w0, w0, w2, cs
	cmp	w0, w1
	csel	w0, w0, w1, cs
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_read_ecc, .-flash_read_ecc
	.align	2
	.type	FlashReadFacBbtData.part.6, %function
FlashReadFacBbtData.part.6:
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LANCHOR0
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x0
	cbnz	w1, .L4
	add	x5, x3, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w7, 1
.L5:
	ldr	w1, [x5, 72]
	cmp	w0, w1
	bcc	.L6
.L4:
	add	x3, x3, :lo12:.LANCHOR0
	mov	x0, x19
	ldr	x1, [x3, 64]
	bl	ftl_memcpy
	mov	w3, 4
	mov	x1, x19
	mov	w2, w3
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	rknand_print_hex
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L6:
	ubfx	x1, x0, 5, 11
	ldr	x6, [x5, 64]
	lsl	x1, x1, 2
	lsl	w8, w7, w0
	add	w0, w0, 1
	and	w0, w0, 65535
	ldr	w4, [x6, x1]
	orr	w4, w4, w8
	str	w4, [x6, x1]
	b	.L5
	.size	FlashReadFacBbtData.part.6, .-FlashReadFacBbtData.part.6
	.align	2
	.type	ftl_set_blk_mode.part.17, %function
ftl_set_blk_mode.part.17:
	and	w0, w0, 65535
	adrp	x2, .LANCHOR0+80
	ubfx	x1, x0, 5, 11
	ldr	x3, [x2, #:lo12:.LANCHOR0+80]
	lsl	x1, x1, 2
	mov	w2, 1
	lsl	w2, w2, w0
	ldr	w0, [x3, x1]
	orr	w0, w0, w2
	str	w0, [x3, x1]
	ret
	.size	ftl_set_blk_mode.part.17, .-ftl_set_blk_mode.part.17
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	adrp	x3, .LANCHOR0+88
	ldrb	w3, [x3, #:lo12:.LANCHOR0+88]
	cbz	w3, .L15
	ldrb	w4, [x0, 1]
	ldrb	w3, [x1, 1]
	cmp	w4, w3
	beq	.L16
.L15:
	mov	x3, 0
.L13:
	mov	w4, w3
	cmp	w3, w2
	bcc	.L14
.L16:
	mov	w0, 0
	ret
.L14:
	ldrb	w5, [x0, x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6, -1]
	cmp	w6, w5
	beq	.L13
	add	w0, w4, 1
	ret
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	2
	.global	FlashRsvdBlkChk
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	and	w0, w0, 255
	ldrb	w3, [x2, 89]
	ldr	w2, [x2, 92]
	mul	w2, w3, w2
	cmp	w2, w1
	bls	.L20
	cmp	w0, 0
	cset	w0, ne
	ret
.L20:
	mov	w0, 1
	ret
	.size	FlashRsvdBlkChk, .-FlashRsvdBlkChk
	.align	2
	.global	FlashGetRandomizer
	.type	FlashGetRandomizer, %function
FlashGetRandomizer:
	and	x3, x1, 127
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	ldrh	w4, [x2, x3, lsl 1]
	adrp	x2, .LANCHOR0+96
	ldrb	w2, [x2, #:lo12:.LANCHOR0+96]
	cbz	w2, .L29
	stp	x29, x30, [sp, -16]!
	and	w0, w0, 255
	add	x29, sp, 0
	bl	FlashRsvdBlkChk
	cmp	w0, 0
	orr	w1, w4, -1073741824
	csel	w4, w1, w4, ne
	mov	w0, w4
	ldp	x29, x30, [sp], 16
	ret
.L29:
	mov	w0, w4
	ret
	.size	FlashGetRandomizer, .-FlashGetRandomizer
	.align	2
	.global	FlashSetRandomizer
	.type	FlashSetRandomizer, %function
FlashSetRandomizer:
	and	x2, x1, 127
	and	w6, w0, 255
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0, x2, lsl 1]
	adrp	x0, .LANCHOR0
	add	x2, x0, :lo12:.LANCHOR0
	mov	x4, x0
	ldrb	w2, [x2, 96]
	cbz	w2, .L40
	stp	x29, x30, [sp, -16]!
	mov	w0, w6
	add	x29, sp, 0
	bl	FlashRsvdBlkChk
	cmp	w0, 0
	sbfiz	x6, x6, 4, 32
	add	x0, x4, :lo12:.LANCHOR0
	orr	w1, w5, -1073741824
	csel	w5, w1, w5, ne
	ldr	x0, [x0, x6]
	str	w5, [x0, 336]
	ldp	x29, x30, [sp], 16
	ret
.L40:
	add	x0, x0, :lo12:.LANCHOR0
	sbfiz	x6, x6, 4, 32
	ldr	x0, [x0, x6]
	str	w5, [x0, 336]
	ret
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	2
	.global	FlashBlockAlignInit
	.type	FlashBlockAlignInit, %function
FlashBlockAlignInit:
	and	w0, w0, 65535
	adrp	x1, .LANCHOR0
	cmp	w0, 512
	add	x1, x1, :lo12:.LANCHOR0
	bls	.L44
	mov	w0, 1024
.L48:
	str	w0, [x1, 92]
	ret
.L44:
	cmp	w0, 256
	bls	.L46
	mov	w0, 512
	b	.L48
.L46:
	cmp	w0, 128
	bls	.L48
	mov	w0, 256
	b	.L48
	.size	FlashBlockAlignInit, .-FlashBlockAlignInit
	.align	2
	.global	FlashReadCmd
	.type	FlashReadCmd, %function
FlashReadCmd:
	and	w0, w0, 255
	adrp	x3, .LANCHOR0
	sbfiz	x2, x0, 4, 32
	add	x3, x3, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x4, x3, x2
	add	x29, sp, 0
	ldr	x2, [x3, x2]
	ldr	x3, [x3, 104]
	ldrb	w4, [x4, 8]
	ldrb	w3, [x3, 7]
	cmp	w3, 1
	bne	.L50
	sxtw	x3, w4
	mov	w5, 38
	add	x3, x3, 8
	add	x3, x2, x3, lsl 8
	str	w5, [x3, 8]
.L50:
	ubfiz	x4, x4, 8, 8
	and	w3, w1, 255
	add	x2, x2, x4
	str	wzr, [x2, 2056]
	str	wzr, [x2, 2052]
	str	wzr, [x2, 2052]
	str	w3, [x2, 2052]
	lsr	w3, w1, 8
	str	w3, [x2, 2052]
	lsr	w3, w1, 16
	str	w3, [x2, 2052]
	mov	w3, 48
	str	w3, [x2, 2056]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashReadCmd, .-FlashReadCmd
	.align	2
	.global	FlashReadDpDataOutCmd
	.type	FlashReadDpDataOutCmd, %function
FlashReadDpDataOutCmd:
	and	w0, w0, 255
	adrp	x3, .LANCHOR0
	sbfiz	x2, x0, 4, 32
	add	x3, x3, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x4, x3, x2
	and	w5, w1, 255
	add	x29, sp, 0
	ldr	x6, [x3, x2]
	ldrb	w2, [x3, 128]
	lsr	w3, w1, 16
	cmp	w2, 1
	ldrb	w2, [x4, 8]
	lsr	w4, w1, 8
	add	x2, x6, x2, lsl 8
	bne	.L53
	mov	w6, 6
	str	w6, [x2, 2056]
	str	wzr, [x2, 2052]
	str	wzr, [x2, 2052]
	str	w5, [x2, 2052]
	str	w4, [x2, 2052]
	str	w3, [x2, 2052]
.L56:
	mov	w3, 224
	str	w3, [x2, 2056]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
.L53:
	str	wzr, [x2, 2056]
	str	wzr, [x2, 2052]
	str	wzr, [x2, 2052]
	str	w5, [x2, 2052]
	str	w4, [x2, 2052]
	str	w3, [x2, 2052]
	mov	w3, 5
	str	w3, [x2, 2056]
	str	wzr, [x2, 2052]
	str	wzr, [x2, 2052]
	b	.L56
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	2
	.global	FlashProgFirstCmd
	.type	FlashProgFirstCmd, %function
FlashProgFirstCmd:
	and	w0, w0, 255
	adrp	x4, .LANCHOR0
	sbfiz	x5, x0, 4, 32
	add	x4, x4, :lo12:.LANCHOR0
	add	x2, x4, x5
	stp	x29, x30, [sp, -16]!
	lsr	w3, w1, 16
	add	x29, sp, 0
	ldr	x4, [x4, x5]
	ldrb	w2, [x2, 8]
	add	x2, x4, x2, lsl 8
	mov	w4, 128
	str	w4, [x2, 2056]
	and	w4, w1, 255
	str	wzr, [x2, 2052]
	str	wzr, [x2, 2052]
	str	w4, [x2, 2052]
	lsr	w4, w1, 8
	str	w4, [x2, 2052]
	str	w3, [x2, 2052]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashProgFirstCmd, .-FlashProgFirstCmd
	.align	2
	.global	FlashEraseCmd
	.type	FlashEraseCmd, %function
FlashEraseCmd:
	ubfiz	x0, x0, 4, 8
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	add	x5, x3, x0
	ldr	x4, [x3, x0]
	ldrb	w0, [x5, 8]
	cbz	w2, .L60
	add	x2, x4, x0, lsl 8
	mov	w5, 96
	str	w5, [x2, 2056]
	and	w5, w1, 255
	str	w5, [x2, 2052]
	lsr	w5, w1, 8
	str	w5, [x2, 2052]
	lsr	w5, w1, 16
	str	w5, [x2, 2052]
	ldr	w2, [x3, 92]
	add	w1, w1, w2
.L60:
	add	x0, x4, x0, lsl 8
	mov	w2, 96
	str	w2, [x0, 2056]
	and	w2, w1, 255
	str	w2, [x0, 2052]
	lsr	w2, w1, 8
	str	w2, [x0, 2052]
	lsr	w1, w1, 16
	str	w1, [x0, 2052]
	mov	w1, 208
	str	w1, [x0, 2056]
	ret
	.size	FlashEraseCmd, .-FlashEraseCmd
	.align	2
	.global	FlashProgDpSecondCmd
	.type	FlashProgDpSecondCmd, %function
FlashProgDpSecondCmd:
	and	w0, w0, 255
	adrp	x4, .LANCHOR0
	sbfiz	x5, x0, 4, 32
	add	x4, x4, :lo12:.LANCHOR0
	add	x2, x4, x5
	stp	x29, x30, [sp, -16]!
	lsr	w3, w1, 16
	add	x29, sp, 0
	ldrb	w6, [x4, 123]
	ldrb	w2, [x2, 8]
	ldr	x4, [x4, x5]
	add	x2, x4, x2, lsl 8
	and	w4, w1, 255
	str	w6, [x2, 2056]
	str	wzr, [x2, 2052]
	str	wzr, [x2, 2052]
	str	w4, [x2, 2052]
	lsr	w4, w1, 8
	str	w4, [x2, 2052]
	str	w3, [x2, 2052]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashProgDpSecondCmd, .-FlashProgDpSecondCmd
	.align	2
	.global	FlashProgSecondCmd
	.type	FlashProgSecondCmd, %function
FlashProgSecondCmd:
	stp	x29, x30, [sp, -32]!
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x2, x1, x0
	stp	x19, x20, [sp, 16]
	ldr	x20, [x1, x0]
	mov	x0, 36284
	ldrb	w19, [x2, 8]
	movk	x0, 0x6, lsl 16
	bl	__const_udelay
	add	x19, x19, 8
	mov	w0, 16
	add	x19, x20, x19, lsl 8
	str	w0, [x19, 8]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashProgSecondCmd, .-FlashProgSecondCmd
	.align	2
	.global	FlashProgDpFirstCmd
	.type	FlashProgDpFirstCmd, %function
FlashProgDpFirstCmd:
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x3, x1, x0
	ldr	x2, [x1, x0]
	ldrb	w0, [x3, 8]
	ldrb	w1, [x1, 122]
	add	x0, x0, 8
	add	x0, x2, x0, lsl 8
	str	w1, [x0, 8]
	ret
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	2
	.global	FlashReadStatus
	.type	FlashReadStatus, %function
FlashReadStatus:
	stp	x29, x30, [sp, -32]!
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x2, x1, x0
	str	x19, [sp, 16]
	ldr	x0, [x1, x0]
	ldrb	w19, [x2, 8]
	add	x19, x0, x19, lsl 8
	mov	w0, 112
	str	w0, [x19, 2056]
	mov	x0, 400
	bl	__const_udelay
	ldr	w0, [x19, 2048]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadStatus, .-FlashReadStatus
	.align	2
	.global	js_hash
	.type	js_hash, %function
js_hash:
	mov	x4, x0
	mov	w0, 42982
	mov	x3, 0
	movk	w0, 0x47c6, lsl 16
.L72:
	cmp	w1, w3
	bhi	.L73
	ret
.L73:
	lsr	w2, w0, 2
	ldrb	w5, [x4, x3]
	add	w2, w2, w0, lsl 5
	add	x3, x3, 1
	add	w2, w2, w5
	eor	w0, w0, w2
	b	.L72
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
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w1
	str	x21, [sp, 32]
	adrp	x1, .LANCHOR0
	cbnz	w0, .L76
	add	x2, x1, :lo12:.LANCHOR0
	mov	x0, 0
	add	x2, x2, 144
.L77:
	strh	w0, [x2, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L77
.L83:
	add	x20, x1, :lo12:.LANCHOR0
	mov	w2, 2048
	add	x21, x20, 1168
	mov	w1, 255
	mov	x0, x21
	bl	ftl_memset
	and	w19, w19, 65535
	add	x1, x20, 144
	mov	x2, 0
.L78:
	cmp	w19, w2, uxth
	bhi	.L111
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L76:
	cmp	w0, 1
	bne	.L79
	add	x3, x1, :lo12:.LANCHOR0
	mov	x2, 0
	add	x3, x3, 144
.L82:
	and	w0, w2, 65535
	cmp	x2, 3
	bls	.L80
	ubfiz	w4, w0, 1, 15
	and	w0, w0, 1
	add	w0, w0, 2
	sub	w0, w4, w0
	and	w0, w0, 65535
.L80:
	strh	w0, [x3, x2, lsl 1]
	add	x2, x2, 1
	cmp	x2, 512
	bne	.L82
	b	.L83
.L79:
	cmp	w0, 2
	bne	.L84
	add	x3, x1, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x3, x3, 144
	mov	x0, 0
.L86:
	cmp	x0, 2
	and	w4, w0, 65535
	csel	w4, w4, w2, cc
	strh	w4, [x3, x0, lsl 1]
	add	w2, w2, 2
	add	x0, x0, 1
	and	w2, w2, 65535
	cmp	x0, 512
	bne	.L86
	b	.L83
.L84:
	cmp	w0, 3
	bne	.L87
	add	x3, x1, :lo12:.LANCHOR0
	mov	x2, 0
	add	x3, x3, 144
.L90:
	and	w0, w2, 65535
	cmp	x2, 5
	bls	.L88
	ubfiz	w4, w0, 1, 15
	and	w0, w0, 1
	add	w0, w0, 4
	sub	w0, w4, w0
	and	w0, w0, 65535
.L88:
	strh	w0, [x3, x2, lsl 1]
	add	x2, x2, 1
	cmp	x2, 512
	bne	.L90
	b	.L83
.L87:
	cmp	w0, 4
	bne	.L91
	add	x3, x1, :lo12:.LANCHOR0
	mov	w4, 1
	add	x3, x3, 160
	strh	w0, [x3, -8]
	mov	w0, 5
	strh	w4, [x3, -14]
	mov	w4, 2
	strh	w0, [x3, -6]
	mov	w0, 7
	strh	w4, [x3, -12]
	mov	w4, 3
	strh	w0, [x3, -4]
	mov	w0, 8
	strh	wzr, [x3, -16]
	strh	w0, [x3, -2]
	mov	w0, 8
	strh	w4, [x3, -10]
.L93:
	and	w4, w0, 1
	ubfiz	w2, w0, 1, 15
	add	w4, w4, 6
	add	w0, w0, 1
	sub	w2, w2, w4
	strh	w2, [x3], 2
	and	w0, w0, 65535
	cmp	w0, 512
	bne	.L93
	b	.L83
.L91:
	cmp	w0, 5
	bne	.L94
	add	x2, x1, :lo12:.LANCHOR0
	mov	x0, 0
	add	x3, x2, 144
.L95:
	strh	w0, [x3, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L95
	add	x2, x2, 176
.L96:
	strh	w0, [x2], 2
	add	w0, w0, 2
	and	w0, w0, 65535
	cmp	w0, 1008
	bne	.L96
	b	.L83
.L94:
	cmp	w0, 6
	bne	.L97
	add	x4, x1, :lo12:.LANCHOR0
	mov	w2, 0
	add	x4, x4, 144
	mov	x3, 0
	mov	w5, 12
	mov	w6, 10
.L100:
	and	w0, w3, 65535
	cmp	x3, 5
	bls	.L98
	tst	x0, 1
	csel	w0, w5, w6, ne
	sub	w0, w2, w0
	and	w0, w0, 65535
.L98:
	strh	w0, [x4, x3, lsl 1]
	add	w2, w2, 3
	and	w2, w2, 65535
	add	x3, x3, 1
	cmp	w2, 1536
	bne	.L100
	b	.L83
.L97:
	cmp	w0, 9
	bne	.L101
	add	x0, x1, :lo12:.LANCHOR0
	mov	w3, 1
	add	x2, x0, 150
	strh	w3, [x0, 146]
	mov	w3, 2
	strh	wzr, [x0, 144]
	strh	w3, [x0, 148]
	mov	w0, 3
.L102:
	strh	w0, [x2], 2
	add	w0, w0, 2
	and	w0, w0, 65535
	cmp	w0, 1021
	bne	.L102
	b	.L83
.L101:
	cmp	w0, 10
	bne	.L103
	add	x2, x1, :lo12:.LANCHOR0
	mov	x0, 0
	add	x3, x2, 144
.L104:
	strh	w0, [x3, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L104
	add	x2, x2, 270
.L105:
	strh	w0, [x2], 2
	add	w0, w0, 2
	and	w0, w0, 65535
	cmp	w0, 961
	bne	.L105
	b	.L83
.L103:
	cmp	w0, 11
	bne	.L106
	add	x2, x1, :lo12:.LANCHOR0
	mov	x0, 0
	add	x3, x2, 144
.L107:
	strh	w0, [x3, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L107
	add	x2, x2, 160
.L109:
	and	w4, w0, 1
	ubfiz	w3, w0, 1, 15
	add	w4, w4, 6
	add	w0, w0, 1
	sub	w3, w3, w4
	strh	w3, [x2], 2
	and	w0, w0, 65535
	cmp	w0, 512
	bne	.L109
	b	.L83
.L106:
	cmp	w0, 12
	bne	.L83
	add	x0, x1, :lo12:.LANCHOR0
	mov	w3, 1
	add	x2, x0, 152
	strh	w3, [x0, 146]
	mov	w3, 2
	strh	wzr, [x0, 144]
	strh	w3, [x0, 148]
	mov	w3, 3
	strh	w3, [x0, 150]
	mov	w0, 4
.L110:
	sub	w3, w0, #1
	add	w3, w3, w0, lsr 1
	add	w0, w0, 1
	strh	w3, [x2], 2
	and	w0, w0, 65535
	cmp	w0, 512
	bne	.L110
	b	.L83
.L111:
	ldrh	w0, [x1, x2, lsl 1]
	add	x2, x2, 1
	strh	w0, [x21, w0, sxtw 1]
	b	.L78
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
	stp	x29, x30, [sp, -80]!
	and	w1, w1, 255
	add	w2, w1, 1
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x21, x0
	mov	w0, 5
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	adrp	x23, g_maxRegNum
	umull	x2, w2, w0
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	stp	x19, x20, [sp, 16]
	add	x25, x0, 256
	add	x24, x0, 352
	add	x25, x25, x2
	add	x24, x24, x2
	add	x23, x23, :lo12:g_maxRegNum
	mov	x19, x0
	add	x22, x0, w1, sxtw
	mov	x20, 0
	mov	w26, 85
.L135:
	ldrb	w0, [x23]
	cmp	w0, w20
	bhi	.L139
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L139:
	add	x0, x19, 352
	str	w26, [x21, 8]
	ldrsb	w0, [x20, x0]
	str	w0, [x21, 4]
	mov	x0, 1000
	bl	__const_udelay
	adrp	x0, g_retryMode
	ldrb	w0, [x0, #:lo12:g_retryMode]
	cmp	w0, 34
	bne	.L136
	ldrsb	w0, [x24, x20]
.L141:
	add	x20, x20, 1
	str	w0, [x21]
	b	.L135
.L136:
	cmp	w0, 35
	bne	.L138
	ldrsb	w0, [x25, x20]
	b	.L141
.L138:
	ldrsb	w0, [x22, 400]
	b	.L141
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	2
	.global	SamsungSetRRPara
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	ubfiz	x21, x1, 2, 8
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x21, x21, 4
	add	x0, x0, 408
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	add	x21, x0, x21
	adrp	x23, g_maxRegNum
	mov	x19, x0
	add	x23, x23, :lo12:g_maxRegNum
	mov	x20, 0
	mov	w24, 161
.L143:
	ldrb	w0, [x23]
	cmp	w0, w20
	bhi	.L144
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L144:
	str	w24, [x22, 8]
	str	wzr, [x22]
	ldrsb	w0, [x20, x19]
	str	w0, [x22]
	ldrsb	w0, [x21, x20]
	add	x20, x20, 1
	str	w0, [x22]
	mov	x0, 1500
	bl	__const_udelay
	b	.L143
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR1+482
	add	x29, sp, 0
	ldrh	w0, [x0, #:lo12:.LANCHOR1+482]
	stp	x19, x20, [sp, 16]
	str	x21, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x19, x21, :lo12:.LANCHOR0
	add	x20, x19, 3228
	strb	wzr, [x19, 3216]
	strb	wzr, [x19, 3217]
	bl	FlashBlockAlignInit
	mov	w2, 8
	mov	w1, 0
	add	x0, x19, 3220
	bl	ftl_memset
	mov	w2, 32
	mov	w1, 0
	mov	x0, x20
	bl	ftl_memset
	mov	w2, 192
	mov	w1, 0
	add	x0, x19, 3260
	bl	ftl_memset
	ldr	x7, [x19, 104]
	adrp	x0, IDByte
	add	x13, x19, 3072
	add	x11, x0, :lo12:IDByte
	mov	x10, x0
	add	x12, x7, 1
	mov	x8, 0
.L148:
	ldrb	w2, [x7]
	add	x1, x11, x8, lsl 3
	mov	x0, x12
	bl	FlashMemCmp8
	cbnz	w0, .L147
	ldrb	w0, [x19, 3216]
	add	w1, w0, 1
	strb	w1, [x19, 3216]
	str	wzr, [x20, x0, lsl 2]
	add	x0, x13, x0
	strb	w8, [x0, 148]
.L147:
	add	x8, x8, 1
	cmp	x8, 4
	bne	.L148
	add	x8, x21, :lo12:.LANCHOR0
	ldrb	w0, [x8, 3216]
	strb	w0, [x8, 3217]
	ldrb	w0, [x7, 8]
	cmp	w0, 2
	beq	.L149
.L153:
	add	x9, x21, :lo12:.LANCHOR0
	ldrh	w1, [x7, 14]
	ldp	x19, x20, [sp, 16]
	ldrb	w0, [x9, 3216]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	mul	w0, w0, w1
	ldrb	w1, [x7, 13]
	mul	w0, w0, w1
	strh	w0, [x9, 3452]
	ret
.L149:
	ldr	w14, [x8, 92]
	add	x10, x10, :lo12:IDByte
	add	x13, x8, 3228
	add	x15, x8, 3072
	mov	x11, 0
.L152:
	ldrb	w2, [x7]
	add	x1, x10, x11, lsl 3
	mov	x0, x12
	bl	FlashMemCmp8
	cbnz	w0, .L150
	ldrb	w1, [x7, 13]
	ldrh	w0, [x7, 14]
	ldrb	w2, [x8, 3216]
	and	w0, w0, 65280
	mul	w1, w1, w14
	mul	w0, w0, w1
	sxtw	x1, w2
	str	w0, [x13, x1, lsl 2]
	ldrb	w3, [x7, 23]
	cbz	w3, .L151
	lsl	w0, w0, 1
	str	w0, [x13, x1, lsl 2]
.L151:
	add	x1, x15, x1
	add	w2, w2, 1
	strb	w2, [x8, 3216]
	strb	w11, [x1, 148]
.L150:
	add	x11, x11, 1
	cmp	x11, 4
	bne	.L152
	b	.L153
	.size	FlashDieInfoInit, .-FlashDieInfoInit
	.align	2
	.global	FlashReadIdbData
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w2, 2048
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
	stp	x29, x30, [sp, -48]!
	adrp	x9, IDByte
	add	x9, x9, :lo12:IDByte
	mov	x8, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR1
	add	x7, x19, :lo12:.LANCHOR1
	str	x21, [sp, 32]
	add	x7, x7, 505
.L166:
	ldrb	w2, [x7, -1]
	mov	w10, w8
	lsl	x21, x8, 5
	mov	x1, x9
	mov	x0, x7
	bl	FlashMemCmp8
	mov	w20, w0
	cbnz	w0, .L164
	add	x2, x19, :lo12:.LANCHOR1
	ubfiz	x10, x10, 5, 32
	add	x0, x2, 504
	add	x1, x2, 3256
	add	x21, x0, x21
	add	x0, x0, x10
	ldrb	w3, [x0, 22]
	mov	x0, 0
.L165:
	lsl	x4, x0, 5
	mov	w2, w0
	ldrb	w4, [x4, x1]
	cmp	w4, w3
	beq	.L168
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L165
	mov	w2, w0
.L168:
	add	x19, x19, :lo12:.LANCHOR1
	ubfiz	x1, x2, 5, 32
	add	x0, x19, 3256
	add	x19, x19, 472
	add	x1, x0, x1
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w2, 32
	add	x0, x0, 112
	bl	ftl_memcpy
	mov	w2, 32
	mov	x1, x21
	mov	x0, x19
	bl	ftl_memcpy
	ldrh	w0, [x19, 10]
	bl	FlashBlockAlignInit
	b	.L163
.L164:
	add	x8, x8, 1
	add	x7, x7, 32
	cmp	x8, 86
	bne	.L166
	mov	w20, -1
.L163:
	mov	w0, w20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
	.align	2
	.global	ftl_flash_suspend
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0, 1152]
	ldr	w2, [x1]
	str	w2, [x0, 1160]
	ldr	w2, [x1, 4]
	str	w2, [x0, 1164]
	ldr	w2, [x1, 8]
	str	w2, [x0, 1168]
	ldr	w2, [x1, 12]
	str	w2, [x0, 1172]
	ldr	w2, [x1, 304]
	str	w2, [x0, 1176]
	ldr	w2, [x1, 308]
	str	w2, [x0, 1180]
	ldr	w2, [x1, 336]
	ldr	w1, [x1, 344]
	str	w2, [x0, 1184]
	str	w1, [x0, 1188]
	ret
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	adrp	x7, .LANCHOR2
	add	x6, x7, :lo12:.LANCHOR2
	mov	x9, x7
	ldr	w7, [x0, 4]
	and	w4, w4, 255
	ldrh	w10, [x6, 1204]
	ldrh	w5, [x6, 1206]
	adrp	x6, .LANCHOR0
	ubfx	x12, x7, 10, 16
	and	w7, w7, 1023
	mul	w5, w5, w10
	and	w10, w5, 65535
	add	x5, x6, :lo12:.LANCHOR0
	ldrh	w8, [x5, 92]
	ldrb	w11, [x5, 88]
	ubfiz	w5, w8, 1, 15
	cmp	w11, 1
	csel	w8, w5, w8, eq
	cmp	w1, 1
	udiv	w5, w12, w10
	and	w11, w5, 65535
	msub	w5, w5, w10, w12
	and	w5, w5, 65535
	bne	.L175
	add	x1, x9, :lo12:.LANCHOR2
	ldrb	w1, [x1, 1220]
	cbnz	w1, .L175
	add	x1, x6, :lo12:.LANCHOR0
	add	x1, x1, 144
	ldrh	w7, [x1, w7, sxtw 1]
.L175:
	add	x6, x6, :lo12:.LANCHOR0
	uxtw	x1, w11
	add	x6, x6, 3228
	cmp	w4, 1
	ldr	w1, [x6, x1, lsl 2]
	madd	w5, w5, w8, w1
	add	w5, w5, w7
	str	w5, [x2]
	str	w11, [x3]
	bls	.L177
	ldr	w1, [x0, 4]
	ldr	w0, [x0, 60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w0, eq
	ret
.L177:
	mov	w0, 0
	ret
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	stp	x29, x30, [sp, -32]!
	ubfiz	x0, x0, 4, 8
	adrp	x4, .LANCHOR0
	add	x3, x4, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	add	x5, x3, x0
	ldr	x20, [x3, x0]
	ldr	x0, [x3, 104]
	ldrb	w19, [x5, 8]
	ldrb	w0, [x0, 8]
	cmp	w0, 2
	bne	.L179
	and	w2, w2, 255
	add	x3, x3, 112
	cbnz	w2, .L180
	ldrb	w2, [x3, 13]
.L190:
	add	x0, x19, 8
	add	x4, x4, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0, 8]
	ldrb	w4, [x4, 127]
	cbz	w4, .L184
	add	x3, x19, 8
	mov	w2, 0
	add	x3, x20, x3, lsl 8
.L183:
	cmp	w2, w4
	bcc	.L185
.L184:
	add	x19, x19, 8
	mov	x0, 400
	lsl	x19, x19, 8
	bl	__const_udelay
	ldr	w0, [x20, x19]
	ldp	x19, x20, [sp, 16]
	and	w0, w0, 255
	ldp	x29, x30, [sp], 32
	ret
.L180:
	ldrb	w2, [x3, 14]
	b	.L190
.L185:
	lsl	w0, w2, 3
	add	w2, w2, 1
	lsr	w0, w1, w0
	and	w0, w0, 255
	str	w0, [x3, 4]
	b	.L183
.L179:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0, 8]
	b	.L184
	.size	FlashReadStatusEN, .-FlashReadStatusEN
	.align	2
	.global	FlashWaitReadyEN
	.type	FlashWaitReadyEN, %function
FlashWaitReadyEN:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 255
	str	x21, [sp, 32]
	mov	w20, w1
	and	w21, w2, 255
.L192:
	mov	w1, w20
	mov	w2, w21
	mov	w0, w19
	bl	FlashReadStatusEN
	mov	w1, w0
	cmp	w0, 255
	beq	.L192
	tbnz	x1, 6, .L191
	mov	x1, 3
	mov	x0, 1
	bl	usleep_range
	b	.L192
.L191:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
	.align	2
	.global	ftl_read_flash_info
	.type	ftl_read_flash_info, %function
ftl_read_flash_info:
	stp	x29, x30, [sp, -32]!
	mov	w2, 11
	mov	w1, 0
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x0
	bl	ftl_memset
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w4, 1
	ldr	x1, [x0, 104]
	ldrb	w2, [x1, 9]
	ldr	w1, [x0, 92]
	mul	w1, w1, w2
	strh	w1, [x19, 4]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	ldrb	w2, [x1, 1221]
	ldr	w1, [x1, 1224]
	strb	w2, [x19, 7]
	mov	x2, 0
	str	w1, [x19]
	ldr	x1, [x0, 104]
	ldrb	w3, [x0, 3216]
	ldrb	w1, [x1, 9]
	strb	w1, [x19, 6]
	mov	w1, 32
	strb	w1, [x19, 8]
	ldr	x1, [x0, 104]
	add	x0, x0, 3220
	ldrb	w1, [x1, 7]
	strb	w1, [x19, 9]
	strb	wzr, [x19, 10]
.L199:
	cmp	w3, w2, uxtb
	bhi	.L200
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L200:
	ldrb	w1, [x2, x0]
	add	x2, x2, 1
	ldrb	w5, [x19, 10]
	lsl	w1, w4, w1
	orr	w1, w1, w5
	strb	w1, [x19, 10]
	b	.L199
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	2
	.global	FlashScheduleEnSet
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	ldr	w2, [x1, 1228]
	str	w0, [x1, 1228]
	mov	w0, w2
	ret
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	2
	.global	FlashGetPageSize
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	adrp	x0, .LANCHOR0+104
	ldr	x0, [x0, #:lo12:.LANCHOR0+104]
	ldrb	w0, [x0, 9]
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
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x2, x1, x0
	ldr	x0, [x1, x0]
	ldrb	w2, [x2, 8]
	add	x2, x2, 8
	add	x0, x0, x2, lsl 8
	ret
	.size	NandcGetChipIf, .-NandcGetChipIf
	.align	2
	.global	NandcSetDdrPara
	.type	NandcSetDdrPara, %function
NandcSetDdrPara:
	adrp	x1, .LANCHOR2+1152
	and	w0, w0, 255
	lsl	w2, w0, 8
	ldr	x1, [x1, #:lo12:.LANCHOR2+1152]
	orr	w0, w2, w0, lsl 16
	orr	w0, w0, 1
	str	w0, [x1, 304]
	ret
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrDiv
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	adrp	x1, .LANCHOR2+1152
	and	w0, w0, 255
	mov	w2, 16640
	orr	w0, w0, w2
	ldr	x1, [x1, #:lo12:.LANCHOR2+1152]
	str	w0, [x1, 344]
	ret
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	2
	.global	NandcSetDdrMode
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	adrp	x1, .LANCHOR2+1152
	cmp	w0, 0
	ldr	x2, [x1, #:lo12:.LANCHOR2+1152]
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
	and	w0, w0, 255
	tst	w0, 6
	ldr	x2, [x1, #:lo12:.LANCHOR2+1152]
	ldr	w1, [x2]
	beq	.L212
	orr	w1, w1, 24576
	tst	x0, 4
	and	w1, w1, -32769
	mov	w0, 8322
	orr	w1, w1, 196608
	str	w0, [x2, 344]
	mov	w0, 4099
	orr	w3, w1, 32768
	movk	w0, 0x10, lsl 16
	str	w0, [x2, 304]
	csel	w1, w3, w1, ne
	mov	w0, 38
	str	w0, [x2, 308]
	mov	w0, 39
	str	w0, [x2, 308]
.L214:
	mov	w0, 0
	str	w1, [x2]
	ret
.L212:
	and	w1, w1, -8193
	b	.L214
	.size	NandcSetMode, .-NandcSetMode
	.align	2
	.global	NandcFlashCs
	.type	NandcFlashCs, %function
NandcFlashCs:
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x3, x1, x0
	ldr	x2, [x1, x0]
	mov	w1, 1
	ldrb	w3, [x3, 8]
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
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x1, [x1, x0]
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
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	and	w23, w0, 255
	str	x27, [sp, 80]
	adrp	x0, .LANCHOR0
	and	w27, w1, 255
	add	x1, x0, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	x20, x2
	stp	x21, x22, [sp, 32]
	mov	x2, x0
	stp	x25, x26, [sp, 64]
	adrp	x21, .LANCHOR2
	and	w22, w3, 255
	add	x19, x21, :lo12:.LANCHOR2
	ldr	x1, [x1, 104]
	ldrb	w1, [x1, 19]
	cmp	w1, 6
	bne	.L220
	ubfiz	x0, x23, 6, 8
	add	x19, x19, 1232
	add	x0, x0, 20
	add	x0, x0, w22, uxtw 2
.L227:
	add	x19, x19, x0
.L221:
	sxtw	x25, w23
	add	x0, x2, :lo12:.LANCHOR0
	lsl	x1, x25, 4
	and	x27, x27, 255
	add	x2, x0, x1
	mov	x26, 0
	ldr	x24, [x0, x1]
	mov	w0, w23
	ldrb	w5, [x2, 8]
	bl	NandcFlashCs
	ubfiz	x5, x5, 8, 8
	add	x24, x24, x5
	mov	w0, 54
	str	w0, [x24, 2056]
.L224:
	cmp	x26, x27
	bne	.L225
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, 22
	add	x21, x21, x25
	str	w0, [x24, 2056]
	mov	w0, w23
	bl	NandcFlashDeCs
	strb	w22, [x21, 2088]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L220:
	cmp	w1, 7
	bne	.L222
	mov	w0, 160
	mov	x1, 28
	add	x19, x19, 1232
	umaddl	x1, w0, w23, x1
	mov	w0, 10
	umaddl	x0, w22, w0, x1
	b	.L227
.L222:
	cmp	w1, 8
	bne	.L223
	add	x0, x19, 1260
	add	w19, w22, w22, lsl 2
	add	x19, x0, w19, sxtw
	b	.L221
.L223:
	and	x0, x22, 255
	add	x0, x0, 2
	add	x0, x0, w23, uxtw 3
	add	x19, x19, x0, lsl 3
	add	x19, x19, 1236
	b	.L221
.L225:
	ldrb	w0, [x20, x26]
	str	w0, [x24, 2052]
	mov	x0, 1000
	bl	__const_udelay
	ldrsb	w0, [x19, x26]
	add	x26, x26, 1
	str	w0, [x24, 2048]
	b	.L224
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	adrp	x0, .LANCHOR0+104
	ldr	x0, [x0, #:lo12:.LANCHOR0+104]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 7
	bhi	.L235
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	adrp	x21, IDByte
	add	x22, x20, 1236
	add	x21, x21, :lo12:IDByte
	mov	x19, 0
.L231:
	lsl	x1, x19, 3
	and	w0, w19, 255
	ldrb	w1, [x1, x21]
	cmp	w1, 173
	bne	.L230
	ldrb	w1, [x20, 1233]
	mov	w3, 0
	mov	x2, x22
	bl	HynixSetRRPara
.L230:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L231
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L235:
	ret
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	FlashWaitCmdDone
	.type	FlashWaitCmdDone, %function
FlashWaitCmdDone:
	and	x5, x0, 255
	mov	x0, 24
	stp	x29, x30, [sp, -32]!
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	mul	x0, x5, x0
	add	x29, sp, 0
	add	x1, x4, 3260
	stp	x19, x20, [sp, 16]
	add	x19, x1, x0
	ldr	x2, [x19, 8]
	cbz	x2, .L240
	ldrb	w20, [x1, x0]
	mov	w0, w20
	bl	NandcFlashCs
	add	x4, x4, 3228
	ldr	w1, [x19, 4]
	ldr	w0, [x4, x5, lsl 2]
	cmp	w0, 0
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w2, w0
	mov	w0, w20
	bl	NandcFlashDeCs
	sbfx	x0, x2, 0, 1
	ldr	x1, [x19, 8]
	str	w0, [x1]
	str	xzr, [x19, 8]
	ldr	x1, [x19, 16]
	cbz	x1, .L240
	str	w0, [x1]
	str	xzr, [x19, 16]
.L240:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
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
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, 34464
	movk	w19, 0x1, lsl 16
	ldr	x20, [x1, x0]
.L250:
	ldr	w0, [x20]
	str	w0, [x29, 40]
	ldr	w0, [x29, 40]
	tbnz	x0, 9, .L251
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L250
	mov	w0, -1
.L248:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L251:
	mov	w0, 0
	b	.L248
	.size	NandcWaitFlashReadyNoDelay, .-NandcWaitFlashReadyNoDelay
	.align	2
	.global	NandcWaitFlashReady
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	stp	x29, x30, [sp, -48]!
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, 34464
	movk	w19, 0x1, lsl 16
	ldr	x20, [x1, x0]
	mov	x0, 650
	bl	__const_udelay
.L256:
	ldr	w0, [x20]
	str	w0, [x29, 40]
	ldr	w0, [x29, 40]
	tbnz	x0, 9, .L257
	mov	x1, 2
	mov	x0, 1
	bl	usleep_range
	subs	w19, w19, #1
	bne	.L256
	mov	w0, -1
.L254:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L257:
	mov	w0, 0
	b	.L254
	.size	NandcWaitFlashReady, .-NandcWaitFlashReady
	.align	2
	.global	FlashReset
	.type	FlashReset, %function
FlashReset:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	and	w19, w0, 255
	sbfiz	x1, x19, 4, 32
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x2, x0, x1
	ldr	x5, [x0, x1]
	mov	w0, w19
	ldrb	w4, [x2, 8]
	bl	NandcFlashCs
	add	x4, x4, 8
	add	x4, x5, x4, lsl 8
	mov	w0, 255
	str	w0, [x4, 8]
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashDeCs
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReset, .-FlashReset
	.align	2
	.global	flash_enter_slc_mode
	.type	flash_enter_slc_mode, %function
flash_enter_slc_mode:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 255
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	str	x23, [sp, 48]
	ldrb	w0, [x0, 1220]
	cbz	w0, .L262
	mov	w0, w22
	bl	NandcFlashCs
	sxtw	x0, w22
	adrp	x1, .LANCHOR0
	lsl	x2, x0, 4
	add	x1, x1, :lo12:.LANCHOR0
	add	x3, x1, x2
	lsl	x0, x0, 3
	ldr	x23, [x1, x2]
	adrp	x1, IDByte
	add	x1, x1, :lo12:IDByte
	ldrb	w19, [x3, 8]
	ldrb	w0, [x1, x0]
	cmp	w0, 44
	bne	.L264
	ubfiz	x20, x19, 8, 8
	mov	w0, 239
	add	x20, x23, x20
	str	w0, [x20, 2056]
	mov	w0, 145
	str	w0, [x20, 2052]
	mov	x0, 250
	bl	__const_udelay
	str	wzr, [x20, 2048]
	mov	w0, 1
	str	w0, [x20, 2048]
	str	wzr, [x20, 2048]
	mov	x0, 500
	str	wzr, [x20, 2048]
	bl	__const_udelay
.L264:
	add	x19, x19, 8
	mov	w0, w22
	add	x19, x23, x19, lsl 8
	bl	NandcWaitFlashReadyNoDelay
	mov	w0, 218
	add	x21, x21, :lo12:.LANCHOR2
	str	w0, [x19, 8]
	mov	w0, w22
	bl	NandcWaitFlashReady
	mov	w0, 2
	strb	w0, [x21, 2092]
.L262:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	2
	.global	flash_exit_slc_mode
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 255
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	str	x23, [sp, 48]
	ldrb	w0, [x0, 1220]
	cbz	w0, .L269
	mov	w0, w22
	bl	NandcFlashCs
	sxtw	x0, w22
	adrp	x1, .LANCHOR0
	lsl	x2, x0, 4
	add	x1, x1, :lo12:.LANCHOR0
	add	x3, x1, x2
	lsl	x0, x0, 3
	ldr	x23, [x1, x2]
	adrp	x1, IDByte
	add	x1, x1, :lo12:IDByte
	ldrb	w19, [x3, 8]
	ldrb	w0, [x1, x0]
	cmp	w0, 44
	bne	.L271
	ubfiz	x20, x19, 8, 8
	mov	w0, 239
	add	x20, x23, x20
	str	w0, [x20, 2056]
	mov	w0, 145
	str	w0, [x20, 2052]
	mov	x0, 250
	bl	__const_udelay
	mov	w0, 2
	str	w0, [x20, 2048]
	mov	w0, 1
	str	w0, [x20, 2048]
	str	wzr, [x20, 2048]
	mov	x0, 500
	str	wzr, [x20, 2048]
	bl	__const_udelay
.L271:
	add	x19, x19, 8
	mov	w0, w22
	add	x19, x23, x19, lsl 8
	bl	NandcWaitFlashReadyNoDelay
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, 223
	str	w0, [x19, 8]
	mov	w0, w22
	bl	NandcWaitFlashReady
	strb	wzr, [x21, 2092]
.L269:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_exit_slc_mode, .-flash_exit_slc_mode
	.align	2
	.global	FlashEraseBlock
	.type	FlashEraseBlock, %function
FlashEraseBlock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 255
	mov	w20, w1
	str	x21, [sp, 32]
	mov	w0, w19
	mov	w21, w2
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	mov	w2, w21
	mov	w1, w20
	mov	w0, w19
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w1, w20
	mov	w0, w19
	bl	FlashReadStatus
	mov	w2, w0
	mov	w0, w19
	bl	NandcFlashDeCs
	and	w0, w2, 1
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashEraseBlock, .-FlashEraseBlock
	.align	2
	.global	FlashSetInterfaceMode
	.type	FlashSetInterfaceMode, %function
FlashSetInterfaceMode:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR2+2093
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	ldrb	w1, [x1, #:lo12:.LANCHOR2+2093]
	adrp	x7, IDByte
	add	x2, x2, 8
	add	x7, x7, :lo12:IDByte
	and	w11, w1, 4
	and	w6, w1, 1
	mov	x5, 0
	mov	w12, 69
	mov	w8, 239
	mov	w9, 128
	mov	w10, 1
	mov	w13, 35
	mov	w14, 32
	mov	w15, 5
	mov	w16, 44
.L288:
	ldrb	w3, [x5, x7]
	ldrb	w4, [x2]
	cmp	w3, 152
	ccmp	w3, w12, 4, ne
	beq	.L279
	cmp	w3, 173
	ccmp	w3, w16, 4, ne
	bne	.L280
.L279:
	cmp	w0, 1
	ldr	x1, [x2, -8]
	bne	.L281
	cbz	w6, .L280
	ubfiz	x4, x4, 8, 8
	cmp	w3, 173
	add	x1, x1, x4
	str	w8, [x1, 2056]
	bne	.L282
	str	w0, [x1, 2052]
.L300:
	str	wzr, [x1, 2048]
	b	.L286
.L282:
	cmp	w3, 44
	bne	.L284
	str	w0, [x1, 2052]
	str	w15, [x1, 2048]
.L286:
	str	wzr, [x1, 2048]
	str	wzr, [x1, 2048]
	str	wzr, [x1, 2048]
.L280:
	add	x5, x5, 8
	add	x2, x2, 16
	cmp	x5, 32
	bne	.L288
	mov	w0, 0
	bl	NandcWaitFlashReady
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L284:
	str	w9, [x1, 2052]
	str	w0, [x1, 2048]
	b	.L286
.L281:
	cbz	w11, .L280
	ubfiz	x4, x4, 8, 8
	cmp	w3, 173
	add	x1, x1, x4
	str	w8, [x1, 2056]
	bne	.L285
	str	w10, [x1, 2052]
	str	w14, [x1, 2048]
	b	.L286
.L285:
	cmp	w3, 44
	bne	.L287
	str	w10, [x1, 2052]
	str	w13, [x1, 2048]
	b	.L286
.L287:
	str	w9, [x1, 2052]
	b	.L300
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	2
	.global	FlashReadSpare
	.type	FlashReadSpare, %function
FlashReadSpare:
	stp	x29, x30, [sp, -32]!
	and	w0, w0, 255
	sbfiz	x5, x0, 4, 32
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x2
	adrp	x2, .LANCHOR1+481
	add	x4, x4, :lo12:.LANCHOR0
	ldrb	w3, [x2, #:lo12:.LANCHOR1+481]
	add	x2, x4, x5
	ldrb	w19, [x2, 8]
	lsl	w3, w3, 9
	ldr	x2, [x4, x5]
	add	x19, x2, x19, lsl 8
	and	w2, w1, 255
	str	wzr, [x19, 2056]
	str	w3, [x19, 2052]
	lsr	w3, w3, 8
	str	w3, [x19, 2052]
	str	w2, [x19, 2052]
	lsr	w2, w1, 8
	str	w2, [x19, 2052]
	lsr	w1, w1, 16
	str	w1, [x19, 2052]
	mov	w1, 48
	str	w1, [x19, 2056]
	bl	NandcWaitFlashReady
	ldr	w0, [x19, 2048]
	strb	w0, [x20]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadSpare, .-FlashReadSpare
	.align	2
	.global	SandiskProgTestBadBlock
	.type	SandiskProgTestBadBlock, %function
SandiskProgTestBadBlock:
	stp	x29, x30, [sp, -32]!
	and	w0, w0, 255
	sbfiz	x3, x0, 4, 32
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x4, x2, x3
	str	x19, [sp, 16]
	ldr	x2, [x2, x3]
	ldrb	w19, [x4, 8]
	add	x19, x2, x19, lsl 8
	mov	w2, 162
	str	w2, [x19, 2056]
	mov	w2, 128
	str	w2, [x19, 2056]
	and	w2, w1, 255
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w2, [x19, 2052]
	lsr	w2, w1, 8
	str	w2, [x19, 2052]
	lsr	w1, w1, 16
	str	w1, [x19, 2052]
	mov	w1, 16
	str	w1, [x19, 2056]
	bl	NandcWaitFlashReady
	mov	w0, 112
	str	w0, [x19, 2056]
	mov	x0, 400
	bl	__const_udelay
	ldr	w0, [x19, 2048]
	ldr	x19, [sp, 16]
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
	adrp	x2, g_maxRegNum
	adrp	x4, g_retryMode
	add	x2, x2, :lo12:g_maxRegNum
	add	x4, x4, :lo12:g_retryMode
	umull	x1, w1, w0
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x3, x0, 256
	add	x0, x0, 352
	add	x3, x3, x1
	add	x0, x0, x1
	mov	x1, 0
.L306:
	ldrb	w5, [x2]
	cmp	w5, w1
	bhi	.L309
	mov	w0, 0
	bl	NandcWaitFlashReady
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L309:
	ldrb	w5, [x4]
	cmp	w5, 67
	bne	.L307
	ldrsb	w5, [x0, x1]
.L311:
	add	x1, x1, 1
	str	w5, [x20]
	b	.L306
.L307:
	ldrsb	w5, [x3, x1]
	b	.L311
	.size	SandiskSetRRPara, .-SandiskSetRRPara
	.align	2
	.global	micron_auto_read_calibration_config
	.type	micron_auto_read_calibration_config, %function
micron_auto_read_calibration_config:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 255
	mov	w20, w1
	mov	w0, w19
	bl	NandcWaitFlashReady
	sbfiz	x0, x19, 4, 32
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	add	x1, x2, x0
	ldr	x0, [x2, x0]
	ldrb	w19, [x1, 8]
	add	x19, x0, x19, lsl 8
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
	.size	micron_auto_read_calibration_config, .-micron_auto_read_calibration_config
	.align	2
	.global	FlashEraseSLc2KBlocks
	.type	FlashEraseSLc2KBlocks, %function
FlashEraseSLc2KBlocks:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, 56
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	umaddl	x23, w1, w23, x0
	add	x21, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	and	w22, w1, 255
	mov	x20, x0
	add	x24, x21, 3072
.L315:
	cmp	x20, x23
	bne	.L320
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L320:
	mov	w1, 0
	mov	w4, w22
	add	x3, x29, 76
	add	x2, x29, 72
	mov	x0, x20
	bl	LogAddr2PhyAddr
	ldrb	w1, [x21, 3216]
	ldr	w0, [x29, 76]
	cmp	w1, w0
	bhi	.L316
	mov	w0, -1
	str	w0, [x20]
.L317:
	sub	w22, w22, #1
	add	x20, x20, 56
	and	w22, w22, 255
	b	.L315
.L316:
	uxtw	x0, w0
	mov	x2, 24
	add	x1, x24, x0
	mul	x0, x0, x2
	ldrb	w19, [x1, 148]
	add	x1, x21, 3260
	strb	w19, [x1, x0]
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w1, [x29, 72]
	mov	w2, 0
	mov	w0, w19
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29, 72]
	mov	w0, w19
	bl	FlashReadStatus
	sbfx	x0, x0, 0, 1
	str	w0, [x20]
	mov	w2, 0
	ldr	w1, [x29, 72]
	ldr	w0, [x21, 92]
	add	w1, w1, w0
	mov	w0, w19
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29, 72]
	mov	w0, w19
	bl	FlashReadStatus
	tbz	x0, 0, .L318
	mov	w0, -1
	str	w0, [x20]
.L318:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L319
	ldr	w1, [x29, 72]
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
.L319:
	mov	w0, w19
	bl	NandcFlashDeCs
	b	.L317
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x25, x26, [sp, 64]
	mov	w25, w1
	add	x1, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x21, x22, [sp, 32]
	mov	w23, w2
	str	x27, [sp, 80]
	ldrb	w1, [x1, 88]
	cbnz	w1, .L326
	adrp	x26, .LANCHOR2
	mov	x20, x0
	add	x27, x26, :lo12:.LANCHOR2
	mov	w21, 0
.L327:
	cmp	w21, w23
	bcc	.L336
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR2
	add	x24, x19, 3260
	add	x22, x22, :lo12:.LANCHOR2
	mov	x21, 0
	mov	x26, 24
.L337:
	ldrb	w0, [x19, 3216]
	cmp	w0, w21
	bhi	.L339
	adrp	x0, .LANCHOR2+2096
	ldr	w0, [x0, #:lo12:.LANCHOR2+2096]
	cbnz	w0, .L340
.L341:
	mov	w0, 0
	b	.L325
.L326:
	mov	w1, w2
	bl	FlashEraseSLc2KBlocks
.L325:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L336:
	mov	w13, 56
	add	x2, x29, 104
	mov	w1, 0
	sub	w4, w23, w21
	umull	x13, w21, w13
	add	x3, x29, 108
	add	x22, x20, x13
	mov	x0, x22
	bl	LogAddr2PhyAddr
	mov	w24, w0
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w0, [x29, 108]
	ldrb	w2, [x1, 3216]
	cmp	w2, w0
	bhi	.L329
	mov	w0, -1
	str	w0, [x20, x13]
.L330:
	add	w21, w21, 1
	b	.L327
.L329:
	add	x2, x26, :lo12:.LANCHOR2
	mov	x3, 24
	ldrb	w2, [x2, 2094]
	cmp	w2, 0
	add	x2, x1, 3260
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x1, x1, x3, x2
	ldr	x1, [x1, 8]
	cbz	x1, .L332
	bl	FlashWaitCmdDone
.L332:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x29, 108]
	add	x2, x0, 3260
	mov	x0, 24
	madd	x0, x1, x0, x2
	ldr	w2, [x29, 104]
	str	w2, [x0, 4]
	stp	x22, xzr, [x0, 8]
	cbz	w24, .L333
	add	w2, w21, 1
	mov	w3, 56
	umaddl	x2, w2, w3, x20
	str	x2, [x0, 16]
.L333:
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x0, x1
	add	x0, x0, 3260
	ldrb	w22, [x2, 3220]
	mov	x2, 24
	mul	x1, x1, x2
	strb	w22, [x0, x1]
	mov	w0, w22
	bl	NandcFlashCs
	cmp	w25, 1
	bne	.L334
	ldrb	w0, [x27, 1220]
	cbz	w0, .L334
	mov	w0, w22
	bl	flash_enter_slc_mode
.L335:
	ldr	w1, [x29, 108]
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 3228
	add	w21, w21, w24
	ldr	w0, [x0, x1, lsl 2]
	ldr	w1, [x29, 104]
	cmp	w0, 0
	mov	w0, w22
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w1, [x29, 104]
	mov	w2, w24
	mov	w0, w22
	bl	FlashEraseCmd
	mov	w0, w22
	bl	NandcFlashDeCs
	b	.L330
.L334:
	mov	w0, w22
	bl	flash_exit_slc_mode
	b	.L335
.L339:
	mov	w0, w21
	bl	FlashWaitCmdDone
	cmp	w25, 1
	bne	.L338
	ldrb	w0, [x22, 1220]
	cbz	w0, .L338
	mul	x0, x21, x26
	ldrb	w0, [x0, x24]
	bl	flash_exit_slc_mode
.L338:
	add	x21, x21, 1
	b	.L337
.L340:
	adrp	x0, IDByte
	ldrb	w0, [x0, #:lo12:IDByte]
	cmp	w0, 69
	bne	.L341
	mov	w0, 56
	umaddl	x23, w23, w0, x20
.L342:
	cmp	x23, x20
	beq	.L341
	str	wzr, [x20], 56
	b	.L342
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	stp	x29, x30, [sp, -128]!
	mov	w3, -83
	mov	w2, -82
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x1, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	mov	w20, w0
	add	x0, x1, 1232
	stp	x23, x24, [sp, 48]
	cmp	w20, 2
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	strb	w20, [x1, 1232]
	mov	w1, -84
	strb	w3, [x0, 5]
	strb	w1, [x0, 4]
	mov	w1, -81
	strb	w2, [x0, 6]
	strb	w1, [x0, 7]
	bne	.L358
	mov	w1, -89
	strb	w1, [x0, 4]
	adrp	x0, .LANCHOR1+3401
	mov	w1, -9
	strb	w1, [x0, #:lo12:.LANCHOR1+3401]
.L423:
	mov	w27, 7
	b	.L467
.L358:
	cmp	w20, 3
	bne	.L360
	mov	w1, -80
	strb	w1, [x0, 4]
	mov	w1, -79
	strb	w1, [x0, 5]
	mov	w1, -78
	strb	w1, [x0, 6]
	mov	w1, -77
	strb	w1, [x0, 7]
	mov	w1, -76
	strb	w1, [x0, 8]
	mov	w1, -75
	strb	w1, [x0, 9]
	mov	w1, -74
	strb	w1, [x0, 10]
	mov	w1, -73
.L466:
	mov	w27, 8
	mov	w28, w27
	strb	w1, [x0, 11]
.L359:
	sub	w0, w20, #1
	cmp	w0, 1
	bhi	.L365
	adrp	x25, .LANCHOR0
	adrp	x26, .LANCHOR1
	add	x25, x25, :lo12:.LANCHOR0
	add	x26, x26, :lo12:.LANCHOR1
	add	x3, x25, 3072
	add	x26, x26, 3384
	mov	w24, 0
.L366:
	ldrb	w0, [x25, 3216]
	cmp	w0, w24
	bhi	.L372
.L373:
	add	x21, x21, :lo12:.LANCHOR2
	ldp	x19, x20, [sp, 16]
	strb	w28, [x21, 1233]
	strb	w27, [x21, 1234]
	ldp	x23, x24, [sp, 48]
	ldp	x21, x22, [sp, 32]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L360:
	cmp	w20, 4
	bne	.L361
	mov	w4, -52
	strb	w4, [x0, 4]
	mov	w4, -65
	strb	w4, [x0, 5]
	mov	w4, -86
	strb	w4, [x0, 6]
	mov	w4, -85
	strb	w3, [x0, 9]
	strb	w4, [x0, 7]
	mov	w4, -51
	strb	w2, [x0, 10]
	strb	w4, [x0, 8]
	b	.L466
.L361:
	cmp	w20, 5
	bne	.L362
	mov	w1, 56
	strb	w1, [x0, 4]
	mov	w1, 57
	strb	w1, [x0, 5]
	mov	w1, 58
	mov	w27, 8
	strb	w1, [x0, 6]
	mov	w1, 59
	strb	w1, [x0, 7]
.L467:
	mov	w28, 4
	b	.L359
.L362:
	cmp	w20, 6
	bne	.L363
	mov	w1, 14
	strb	w1, [x0, 4]
	mov	w1, 15
	strb	w1, [x0, 5]
	mov	w1, 16
	mov	w27, 12
	strb	w1, [x0, 6]
	mov	w1, 17
	strb	w1, [x0, 7]
	b	.L467
.L363:
	cmp	w20, 7
	bne	.L364
	mov	w1, -80
	strb	w1, [x0, 4]
	mov	w1, -79
	strb	w1, [x0, 5]
	mov	w1, -78
	strb	w1, [x0, 6]
	mov	w1, -77
	strb	w1, [x0, 7]
	mov	w1, -76
	strb	w1, [x0, 8]
	mov	w1, -75
	strb	w1, [x0, 9]
	mov	w1, -74
	strb	w1, [x0, 10]
	mov	w1, -73
	strb	w1, [x0, 11]
	mov	w1, -44
	mov	w27, 12
	strb	w1, [x0, 12]
	mov	w28, 10
	mov	w1, -43
	strb	w1, [x0, 13]
	b	.L359
.L364:
	cmp	w20, 8
	bne	.L423
	mov	w1, 6
	strb	w1, [x0, 4]
	mov	w1, 7
	strb	w1, [x0, 5]
	mov	w1, 9
	strb	w20, [x0, 6]
	strb	w1, [x0, 7]
	mov	w27, 50
	mov	w1, 10
	mov	w28, 5
	strb	w1, [x0, 8]
	b	.L359
.L372:
	add	x0, x3, w24, sxtw
	mov	x22, 0
	ldrb	w1, [x0, 148]
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, 1232
	mov	x20, x0
	ubfiz	x19, x1, 6, 8
	sbfiz	x1, x1, 4, 32
	add	x2, x25, x1
	add	x19, x19, 20
	add	x19, x0, x19
	ldr	x1, [x25, x1]
	ldrb	w23, [x2, 8]
	add	x23, x1, x23, lsl 8
	mov	w1, 55
.L367:
	add	x0, x20, x22
	str	w1, [x23, 2056]
	str	x3, [x29, 112]
	str	w1, [x29, 124]
	ldrb	w0, [x0, 4]
	str	w0, [x23, 2052]
	mov	x0, 400
	bl	__const_udelay
	ldr	w0, [x23, 2048]
	strb	w0, [x19, x22]
	add	x22, x22, 1
	cmp	w28, w22, uxtb
	ldr	w1, [x29, 124]
	ldr	x3, [x29, 112]
	bhi	.L367
	mov	x0, 0
.L370:
	add	x1, x0, 4
	add	x2, x0, 28
	add	w5, w0, 8
	add	x1, x26, x1
	add	x2, x26, x2
.L369:
	ldrb	w6, [x19, x0]
	ldrb	w7, [x1], 4
	add	w6, w6, w7
	strb	w6, [x19, w5, sxtw]
	cmp	x2, x1
	add	w5, w5, 8
	bne	.L369
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L370
	add	w24, w24, 1
	strb	wzr, [x19, 16]
	strb	wzr, [x19, 24]
	and	w24, w24, 255
	strb	wzr, [x19, 32]
	strb	wzr, [x19, 40]
	strb	wzr, [x19, 48]
	strb	wzr, [x19, 41]
	strb	wzr, [x19, 49]
	b	.L366
.L365:
	sub	w0, w20, #3
	cmp	w0, 5
	bhi	.L373
	mul	w26, w28, w27
	sub	w25, w28, #1
	adrp	x24, .LANCHOR0
	and	x25, x25, 255
	mov	w22, 0
	lsl	w0, w26, 4
	asr	w23, w26, 1
	str	w0, [x29, 112]
	add	x26, x24, :lo12:.LANCHOR0
	lsl	w0, w23, 1
	str	w0, [x29, 124]
	add	x0, x25, 1
	str	x0, [x29, 104]
.L374:
	ldrb	w0, [x26, 3216]
	cmp	w0, w22
	bls	.L373
	add	x0, x26, w22, sxtw
	ldrb	w25, [x0, 3220]
	sbfiz	x0, x25, 4, 32
	add	x1, x26, x0
	ldr	x0, [x26, x0]
	ldrb	w19, [x1, 8]
	add	x19, x0, x19, lsl 8
	mov	w0, 255
	str	w0, [x19, 2056]
	mov	w0, w25
	bl	NandcWaitFlashReady
	cmp	w20, 7
	sub	w6, w20, #5
	bne	.L375
	mov	x1, 28
	mov	w0, 160
	add	x2, x21, :lo12:.LANCHOR2
	umaddl	x0, w0, w25, x1
	add	x2, x2, 1232
	add	x2, x2, x0
.L376:
	mov	w0, 54
	str	w0, [x19, 2056]
	cmp	w20, 4
	bne	.L378
	mov	w0, 255
	str	w0, [x19, 2052]
	mov	w0, 64
	str	w0, [x19, 2048]
	mov	w0, 204
.L468:
	str	w0, [x19, 2052]
	mov	w0, 77
	b	.L469
.L375:
	cmp	w20, 8
	beq	.L377
	add	x2, x21, :lo12:.LANCHOR2
	ubfiz	x0, x25, 6, 8
	add	x2, x2, x0
	add	x2, x2, 1252
	b	.L376
.L378:
	cmp	w6, 1
	bhi	.L380
	add	x0, x21, :lo12:.LANCHOR2
	ldrb	w0, [x0, 1236]
	str	w0, [x19, 2052]
	mov	w0, 82
.L469:
	str	w0, [x19, 2048]
.L379:
	mov	w0, 22
	str	w0, [x19, 2056]
	mov	w0, 23
	str	w0, [x19, 2056]
	mov	w0, 4
	str	w0, [x19, 2056]
	mov	w0, 25
	str	w0, [x19, 2056]
	str	wzr, [x19, 2056]
	cmp	w20, 6
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	bne	.L381
	mov	w0, 31
	str	w0, [x19, 2052]
.L382:
	mov	w0, 2
	str	w0, [x19, 2052]
	str	wzr, [x19, 2052]
.L422:
	mov	w0, 48
	str	w0, [x19, 2056]
	str	x2, [x29, 96]
	mov	w0, w25
	str	w6, [x29, 120]
	bl	NandcWaitFlashReady
	ldr	w6, [x29, 120]
	ldr	x2, [x29, 96]
	cmp	w6, 1
	ccmp	w20, 8, 4, hi
	beq	.L424
	cmp	w20, 7
	mov	w0, 2
	mov	w1, 32
	csel	w1, w1, w0, eq
.L383:
	add	x0, x24, :lo12:.LANCHOR0
	mov	x7, 0
	ldr	x0, [x0, 64]
.L384:
	ldr	w8, [x19, 2048]
	strb	w8, [x0, x7]
	add	x7, x7, 1
	cmp	w1, w7, uxtb
	bhi	.L384
	cmp	w20, 8
	bne	.L385
	mov	w1, 0
.L387:
	ldrb	w7, [x0]
	cmp	w7, 50
	beq	.L386
	ldrb	w7, [x0, 1]
	cmp	w7, 5
	beq	.L386
	add	w1, w1, 1
	add	x0, x0, 4
	and	w1, w1, 255
	cmp	w1, 8
	bne	.L387
.L388:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L390:
	b	.L390
.L380:
	cmp	w20, 7
	bne	.L379
	mov	w0, 174
	str	w0, [x19, 2052]
	str	wzr, [x19, 2048]
	mov	w0, 176
	b	.L468
.L381:
	str	wzr, [x19, 2052]
	b	.L382
.L424:
	mov	w1, 16
	b	.L383
.L386:
	cmp	w1, 6
	bhi	.L388
.L389:
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x7, [x0, 64]
	mov	x0, 0
.L399:
	ldr	w1, [x29, 112]
	cmp	w1, w0
	bgt	.L400
	add	x0, x24, :lo12:.LANCHOR0
	mov	w9, w23
	mov	w8, 8
	ldr	x11, [x0, 64]
.L402:
	mov	w0, 0
.L401:
	add	w1, w0, w9
	add	w0, w0, 1
	sbfiz	x1, x1, 1, 32
	cmp	w23, w0
	ldrh	w10, [x11, x1]
	mvn	w10, w10
	strh	w10, [x11, x1]
	bgt	.L401
	ldr	w0, [x29, 124]
	subs	w8, w8, #1
	add	w9, w9, w0
	bne	.L402
	mov	x1, 0
	mov	w14, 1
.L408:
	mov	w0, 0
	mov	w8, 0
.L407:
	mov	w10, w1
	lsl	w13, w14, w8
	mov	w12, 16
	mov	w9, 0
.L405:
	ldrh	w15, [x11, w10, sxtw 1]
	add	w10, w10, w23
	bics	wzr, w13, w15
	cinc	w9, w9, eq
	subs	w12, w12, #1
	bne	.L405
	cmp	w9, 8
	bls	.L406
	orr	w0, w0, w13
	and	w0, w0, 65535
.L406:
	add	w8, w8, 1
	cmp	w8, 16
	bne	.L407
	strh	w0, [x11, x1, lsl 1]
	add	x1, x1, 1
	cmp	w23, w1
	bgt	.L408
	add	x0, x24, :lo12:.LANCHOR0
	mov	w8, 0
	ldr	x1, [x0, 64]
	mov	x0, 0
.L411:
	ldr	w9, [x1, x0]
	add	x0, x0, 4
	cmp	w9, 0
	cinc	w8, w8, eq
	cmp	x0, 32
	bne	.L411
	cmp	w8, 7
	ble	.L412
	mov	w3, 1024
	mov	w2, 1
	adrp	x0, .LC3
	add	x0, x0, :lo12:.LC3
	bl	rknand_print_hex
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L413:
	b	.L413
.L385:
	cmp	w20, 7
	bne	.L391
	mov	w1, 0
.L393:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L392
	ldrb	w7, [x0, 1]
	cmp	w7, 10
	beq	.L392
	add	w1, w1, 1
	add	x0, x0, 4
	and	w1, w1, 255
	cmp	w1, 8
	bne	.L393
.L394:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L395:
	b	.L395
.L392:
	cmp	w1, 6
	bls	.L389
	b	.L394
.L391:
	cmp	w20, 6
	bne	.L389
	mov	x1, 0
.L396:
	ldrb	w7, [x0, x1]
	cmp	w7, 12
	beq	.L389
	add	x7, x0, x1
	ldrb	w7, [x7, 8]
	cmp	w7, 4
	beq	.L389
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L396
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L398:
	b	.L398
.L400:
	ldr	w1, [x19, 2048]
	strb	w1, [x7, x0]
	add	x0, x0, 1
	b	.L399
.L412:
	cmp	w20, 6
	beq	.L426
	cmp	w20, 7
	beq	.L427
	cmp	w20, 8
	mov	w0, 8
	mov	w1, 5
	csel	w1, w1, w0, eq
.L414:
	mov	w8, 0
.L415:
	mov	x0, 0
.L416:
	add	w9, w12, w0
	ldrb	w10, [x7, x0]
	add	x0, x0, 1
	cmp	w28, w0, uxtb
	strb	w10, [x2, w9, sxtw]
	bhi	.L416
	ldr	x0, [x29, 104]
	add	w8, w8, 1
	add	w12, w12, w1
	cmp	w27, w8
	add	x7, x7, x0
	bgt	.L415
	mov	w0, 255
	str	w0, [x19, 2056]
	str	w6, [x29, 96]
	mov	w0, w25
	bl	NandcWaitFlashReady
	ldr	w6, [x29, 96]
	cmp	w6, 1
	bhi	.L418
	mov	w0, 54
	str	w0, [x19, 2056]
	adrp	x0, .LANCHOR2+1236
	mov	w1, -1
	ldrb	w0, [x0, #:lo12:.LANCHOR2+1236]
	str	w0, [x19, 2052]
	str	wzr, [x19, 2048]
	mov	w0, 22
	str	w0, [x19, 2056]
	mov	w0, w22
	bl	FlashReadCmd
.L419:
	add	w22, w22, 1
	mov	w0, w25
	and	w22, w22, 255
	bl	NandcWaitFlashReady
	b	.L374
.L426:
	mov	w1, 4
	b	.L414
.L427:
	mov	w1, 10
	b	.L414
.L418:
	cmp	w20, 8
	bne	.L420
	mov	w0, 190
.L470:
	str	w0, [x19, 2056]
	b	.L419
.L420:
	mov	w0, 56
	b	.L470
.L377:
	mov	w0, 120
	str	w0, [x19, 2056]
	str	wzr, [x19, 2052]
	mov	w0, 23
	str	wzr, [x19, 2052]
	mov	w1, 25
	str	wzr, [x19, 2052]
	add	x2, x21, :lo12:.LANCHOR2
	str	w0, [x19, 2056]
	mov	w0, 4
	str	w0, [x19, 2056]
	add	x2, x2, 1260
	str	w1, [x19, 2056]
	mov	w1, 218
	str	w1, [x19, 2056]
	mov	w1, 21
	str	wzr, [x19, 2056]
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w1, [x19, 2052]
	str	w0, [x19, 2052]
	str	wzr, [x19, 2052]
	b	.L422
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	cbz	w0, .L484
	stp	x29, x30, [sp, -16]!
	sub	w2, w0, #1
	mov	w1, w0
	cmp	w2, 7
	add	x29, sp, 0
	bhi	.L473
	bl	HynixGetReadRetryDefault
.L471:
	ldp	x29, x30, [sp], 16
	ret
.L473:
	cmp	w0, 49
	bne	.L474
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	mov	w2, 64
	strb	w1, [x0, 1232]
	mov	w1, 4
	strb	w1, [x0, 1233]
	mov	w1, 15
	strb	w1, [x0, 1234]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 408
.L487:
	add	x0, x0, 1236
	bl	ftl_memcpy
	b	.L471
.L474:
	sub	w0, w0, #65
	cmp	w1, 33
	ccmp	w0, 1, 0, ne
	bhi	.L475
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	strb	w1, [x0, 1232]
	mov	w1, 4
.L488:
	strb	w1, [x0, 1233]
	mov	w1, 7
	strb	w1, [x0, 1234]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 45
	add	x1, x1, 352
	b	.L487
.L475:
	cmp	w1, 34
	mov	w0, 67
	ccmp	w1, w0, 4, ne
	bne	.L476
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	strb	w1, [x0, 1232]
	mov	w1, 5
	b	.L488
.L476:
	cmp	w1, 35
	mov	w0, 68
	ccmp	w1, w0, 4, ne
	bne	.L471
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	mov	w2, 95
	strb	w1, [x0, 1232]
	mov	w1, 5
	strb	w1, [x0, 1233]
	mov	w1, 17
	strb	w1, [x0, 1234]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 256
	b	.L487
.L484:
	ret
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	FlashReadDpCmd
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 255
	adrp	x0, .LANCHOR0
	add	x4, x0, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	w21, w1
	stp	x19, x20, [sp, 16]
	sbfiz	x1, x22, 4, 32
	and	w24, w2, 255
	lsr	w23, w2, 8
	lsr	w20, w2, 16
	ldr	x2, [x4, 104]
	add	x3, x4, x1
	ldr	x5, [x4, x1]
	ldrb	w1, [x4, 128]
	and	w7, w21, 255
	lsr	w6, w21, 8
	cmp	w1, 1
	ldrb	w19, [x3, 8]
	lsr	w1, w21, 16
	ldrb	w2, [x2, 7]
	bne	.L490
	cmp	w2, 1
	bne	.L491
	sxtw	x3, w19
	mov	w2, 38
	add	x3, x3, 8
	add	x3, x5, x3, lsl 8
	str	w2, [x3, 8]
.L491:
	add	x0, x0, :lo12:.LANCHOR0
	add	x19, x5, x19, lsl 8
	add	x0, x0, 112
	ldrb	w2, [x0, 8]
	str	w2, [x19, 2056]
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w7, [x19, 2052]
	str	w6, [x19, 2052]
	ldrb	w0, [x0, 9]
	str	w1, [x19, 2052]
	str	w0, [x19, 2056]
	mov	w0, w22
	bl	NandcWaitFlashReady
	str	wzr, [x19, 2056]
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
.L495:
	str	w24, [x19, 2052]
	mov	w0, 48
	str	w23, [x19, 2052]
	mov	w1, w21
	str	w20, [x19, 2052]
	str	w0, [x19, 2056]
	mov	w0, w22
	bl	FlashSetRandomizer
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L490:
	cmp	w2, 1
	bne	.L493
	sxtw	x3, w19
	mov	w2, 38
	add	x3, x3, 8
	add	x3, x5, x3, lsl 8
	str	w2, [x3, 8]
.L493:
	add	x0, x0, :lo12:.LANCHOR0
	add	x19, x5, x19, lsl 8
	add	x0, x0, 112
	ldrb	w2, [x0, 8]
	str	w2, [x19, 2056]
	str	w7, [x19, 2052]
	str	w6, [x19, 2052]
	ldrb	w0, [x0, 9]
	str	w1, [x19, 2052]
	str	w0, [x19, 2056]
	b	.L495
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	2
	.global	ftl_flash_de_init
	.type	ftl_flash_de_init, %function
ftl_flash_de_init:
	stp	x29, x30, [sp, -32]!
	mov	w0, 0
	add	x29, sp, 0
	str	x19, [sp, 16]
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0, 2100]
	cbz	w0, .L497
	mov	w0, 0
	bl	flash_enter_slc_mode
.L498:
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19, 2104]
	cbz	w0, .L499
	ldrb	w0, [x19, 2093]
	tbz	x0, 0, .L499
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x19, 2104]
.L499:
	adrp	x0, .LANCHOR0
	ldr	x0, [x0, #:lo12:.LANCHOR0]
	str	wzr, [x0, 336]
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L497:
	mov	w0, 0
	bl	flash_exit_slc_mode
	b	.L498
	.size	ftl_flash_de_init, .-ftl_flash_de_init
	.align	2
	.global	NandcRandmzSel
	.type	NandcRandmzSel, %function
NandcRandmzSel:
	ubfiz	x0, x0, 4, 8
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	ldr	x0, [x2, x0]
	str	w1, [x0, 336]
	ret
	.size	NandcRandmzSel, .-NandcRandmzSel
	.align	2
	.global	NandcTimeCfg
	.type	NandcTimeCfg, %function
NandcTimeCfg:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	w19, w0
	mov	w0, 0
	bl	rknand_get_clk_rate
	mov	w1, 16960
	movk	w1, 0xf, lsl 16
	sdiv	w0, w0, w1
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	cmp	w0, 250
	ble	.L509
	ldr	x0, [x1, 1152]
	mov	w1, 8354
.L517:
	str	w1, [x0, 4]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L509:
	cmp	w0, 220
	ble	.L511
	ldr	x0, [x1, 1152]
.L518:
	mov	w1, 8322
	b	.L517
.L511:
	cmp	w0, 185
	ble	.L512
	ldr	x0, [x1, 1152]
	mov	w1, 4226
	b	.L517
.L512:
	cmp	w0, 160
	ldr	x0, [x1, 1152]
	ble	.L513
	mov	w1, 4194
	b	.L517
.L513:
	cmp	w19, 35
	bhi	.L514
	mov	w1, 4193
	b	.L517
.L514:
	cmp	w19, 99
	bhi	.L518
	mov	w1, 4225
	b	.L517
	.size	NandcTimeCfg, .-NandcTimeCfg
	.align	2
	.global	FlashTimingCfg
	.type	FlashTimingCfg, %function
FlashTimingCfg:
	stp	x29, x30, [sp, -16]!
	mov	w1, -4193
	add	w2, w0, w1
	mov	w3, -4225
	add	x29, sp, 0
	add	w1, w0, w3
	cmp	w2, 1
	ccmp	w1, 1, 0, hi
	bls	.L520
	mov	w1, 8322
	cmp	w0, w1
	bne	.L521
.L520:
	adrp	x1, .LANCHOR2+1152
	ldr	x1, [x1, #:lo12:.LANCHOR2+1152]
	str	w0, [x1, 4]
.L521:
	adrp	x0, .LANCHOR1+493
	ldrb	w0, [x0, #:lo12:.LANCHOR1+493]
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
	add	x1, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR2
	str	x0, [x2, #:lo12:.LANCHOR0]
	mov	w2, 1
	str	w2, [x1, 24]
	mov	w2, 2
	str	w2, [x1, 40]
	mov	w2, 3
	str	w2, [x1, 56]
	add	x2, x19, :lo12:.LANCHOR2
	str	wzr, [x1, 8]
	str	x0, [x1, 16]
	str	x0, [x1, 32]
	str	x0, [x1, 48]
	str	x0, [x2, 1152]
	ldr	w1, [x0]
	ubfx	x3, x1, 13, 1
	str	w3, [x2, 2108]
	ldr	w3, [x0, 352]
	and	w1, w1, 245760
	orr	w1, w1, 256
	ubfx	x3, x3, 16, 4
	str	w3, [x2, 2112]
	ldr	w3, [x0, 352]
	str	w3, [x2, 2116]
	cmp	w3, 2049
	bne	.L524
	mov	w3, 8
	str	w3, [x2, 2112]
.L524:
	add	x19, x19, :lo12:.LANCHOR2
	str	w1, [x0]
	ldr	x0, [x19, 1152]
	str	wzr, [x0, 336]
	mov	w0, 40
	bl	NandcTimeCfg
	ldr	x0, [x19, 1152]
	mov	w1, 8322
	str	w1, [x0, 344]
	mov	w1, 6145
	movk	w1, 0x18, lsl 16
	str	w1, [x0, 304]
	mov	w0, 36864
	bl	ftl_dma32_malloc
	str	wzr, [x19, 2168]
	str	x0, [x19, 2120]
	str	x0, [x19, 2128]
	add	x0, x0, 32768
	str	wzr, [x19, 2176]
	str	x0, [x19, 2136]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	NandcInit, .-NandcInit
	.align	2
	.global	NandcGetTimeCfg
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	adrp	x4, .LANCHOR2
	add	x4, x4, :lo12:.LANCHOR2
	ldr	x5, [x4, 1152]
	ldr	w5, [x5, 4]
	str	w5, [x0]
	ldr	x0, [x4, 1152]
	ldr	w0, [x0]
	str	w0, [x1]
	ldr	x0, [x4, 1152]
	ldr	w0, [x0, 304]
	str	w0, [x2]
	ldr	x0, [x4, 1152]
	ldr	w1, [x0, 308]
	ldr	w0, [x0, 344]
	and	w1, w1, 255
	orr	w0, w1, w0, lsl 16
	str	w0, [x3]
	ret
	.size	NandcGetTimeCfg, .-NandcGetTimeCfg
	.align	2
	.global	NandcBchSel
	.type	NandcBchSel, %function
NandcBchSel:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	and	w0, w0, 255
	mov	w3, 1
	cmp	w0, 16
	ldr	x2, [x1, 1152]
	str	w0, [x1, 2180]
	mov	w1, 4096
	str	w3, [x2, 8]
	bne	.L528
.L531:
	and	w1, w1, -17
.L529:
	orr	w1, w1, 1
	str	w1, [x2, 12]
	ret
.L528:
	cmp	w0, 24
	bne	.L530
	orr	w1, w1, 16
	b	.L529
.L530:
	orr	w1, w1, 262144
	cmp	w0, 40
	orr	w1, w1, 16
	bne	.L529
	b	.L531
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR2+1221
	and	w0, w0, 255
	add	x29, sp, 0
	strb	w0, [x1, #:lo12:.LANCHOR2+1221]
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
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	str	x21, [sp, 32]
	adrp	x21, IDByte
	add	x21, x21, :lo12:IDByte
	mov	x20, 0
	ldr	x1, [x0, 1152]
	ldr	w2, [x0, 1160]
	str	w2, [x1]
	ldr	w2, [x0, 1164]
	ldr	x1, [x0, 1152]
	str	w2, [x1, 4]
	ldr	w2, [x0, 1168]
	ldr	x1, [x0, 1152]
	str	w2, [x1, 8]
	ldr	w2, [x0, 1172]
	str	w2, [x1, 12]
	ldr	w2, [x0, 1176]
	str	w2, [x1, 304]
	ldr	w2, [x0, 1180]
	str	w2, [x1, 308]
	ldr	w2, [x0, 1184]
	str	w2, [x1, 336]
	ldr	w0, [x0, 1188]
	str	w0, [x1, 344]
.L539:
	lsl	x0, x20, 3
	ldrb	w0, [x0, x21]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 253
	bhi	.L538
	mov	w0, w20
	bl	FlashReset
.L538:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L539
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19, 2104]
	cbz	w0, .L540
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19, 2093]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19, 2093]
	bl	NandcSetMode
	ldr	w0, [x19, 1176]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L540:
	adrp	x0, .LANCHOR0+104
	ldr	x0, [x0, #:lo12:.LANCHOR0+104]
	ldrb	w0, [x0, 20]
	bl	FlashBchSel
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_flash_resume, .-ftl_flash_resume
	.align	2
	.global	ftl_nandc_get_irq_status
	.type	ftl_nandc_get_irq_status, %function
ftl_nandc_get_irq_status:
	ldr	w0, [x0, 372]
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
	ldr	w2, [x0, 16]
	mov	w1, 1066
	movk	w1, 0x2020, lsl 16
	str	w2, [sp, 8]
	ldr	w2, [sp, 8]
	and	w2, w2, -5
	str	w2, [sp, 8]
	ldr	w2, [sp, 8]
	str	w2, [x0, 16]
	str	w1, [x0, 8]
	orr	w1, w1, 4
	str	w1, [x0, 8]
	add	sp, sp, 16
	ret
	.size	NandcSendDumpDataStart, .-NandcSendDumpDataStart
	.align	2
	.global	NandcSendDumpDataDone
	.type	NandcSendDumpDataDone, %function
NandcSendDumpDataDone:
	sub	sp, sp, #16
.L551:
	ldr	w1, [x0, 8]
	str	w1, [sp, 8]
	ldr	w1, [sp, 8]
	tbz	x1, 20, .L551
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
	stp	x23, x24, [sp, 48]
	and	w24, w1, 255
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	add	x6, x1, x0
	stp	x21, x22, [sp, 32]
	ubfiz	w20, w24, 1, 1
	str	x25, [sp, 64]
	orr	w20, w20, 8
	adrp	x19, .LANCHOR2
	ldr	x21, [x1, x0]
	mov	w1, 16
	ldrb	w0, [x6, 8]
	ldr	w23, [x21, 12]
	bfi	w23, w1, 8, 8
	and	w23, w23, -9
	bfi	w23, w0, 5, 3
	mov	w0, 1
	bfi	w20, w0, 5, 2
	add	x0, x19, :lo12:.LANCHOR2
	orr	w20, w20, 536870912
	orr	w20, w20, 1024
	ldr	w1, [x0, 2112]
	bfi	w20, w3, 4, 1
	cmp	w1, 3
	bls	.L556
	ldr	w1, [x21, 16]
	cmp	x5, 0
	str	w1, [x29, 88]
	ccmp	x4, 0, 0, eq
	ldr	w1, [x29, 88]
	and	w1, w1, -5
	str	w1, [x29, 88]
	beq	.L557
	and	w2, w2, 255
	cbnz	w24, .L558
.L566:
	add	w2, w2, 1
	asr	w2, w2, 1
	bfi	w20, w2, 22, 6
	cbz	x4, .L559
	mov	x0, x4
.L560:
	add	x19, x19, :lo12:.LANCHOR2
	ubfx	x25, x20, 22, 5
	mov	x22, x4
	mov	w2, w24
	ldr	x1, [x19, 2136]
	str	x1, [x19, 2152]
	lsl	w1, w25, 10
	str	x0, [x19, 2144]
	bl	rknand_dma_map_single
	str	w0, [x19, 2160]
	lsl	w1, w25, 7
	ldr	x0, [x19, 2152]
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x19, 2164]
	mov	w0, 1
	str	w0, [x19, 2168]
	ldr	w0, [x19, 2160]
	mov	w1, 16
	str	w0, [x21, 20]
	tst	x22, 3
	ldr	w0, [x19, 2164]
	str	w0, [x21, 24]
	str	wzr, [x29, 88]
	ldr	w0, [x29, 88]
	bfi	w0, w1, 9, 5
	str	w0, [x29, 88]
	ldr	w0, [x29, 88]
	orr	w0, w0, 448
	str	w0, [x29, 88]
	bne	.L567
	ldr	w0, [x29, 88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29, 88]
.L567:
	ldr	w0, [x29, 88]
	cmp	w24, 0
	cset	w1, eq
	orr	w0, w0, 4
	str	w0, [x29, 88]
	ldr	w0, [x29, 88]
	bfi	w0, w1, 1, 1
	str	w0, [x29, 88]
	ldr	w0, [x29, 88]
	orr	w0, w0, 1
	str	w0, [x29, 88]
.L557:
	ldr	w0, [x29, 88]
	str	w0, [x21, 16]
.L556:
	str	w23, [x21, 12]
	str	w20, [x21, 8]
	orr	w20, w20, 4
	str	w20, [x21, 8]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L558:
	ldr	w1, [x0, 2180]
	mov	w6, 64
	lsr	w9, w2, 1
	mov	x8, x5
	cmp	w1, 25
	mov	w1, 128
	csel	w6, w6, w1, cc
	mov	w7, 0
	mov	w3, 0
	mov	w10, -1
.L562:
	cmp	w3, w9
	bcs	.L566
	lsr	w1, w7, 2
	cbz	x5, .L563
	ldr	x11, [x0, 2136]
	lsl	w1, w1, 2
	ldr	w12, [x8], 4
	str	w12, [x11, x1]
.L564:
	add	w3, w3, 1
	add	w7, w7, w6
	b	.L562
.L563:
	ldr	x11, [x0, 2136]
	lsl	w1, w1, 2
	str	w10, [x11, x1]
	b	.L564
.L559:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0, 2128]
	b	.L560
	.size	NandcXferStart, .-NandcXferStart
	.align	2
	.global	NandcXferComp
	.type	NandcXferComp, %function
NandcXferComp:
	stp	x29, x30, [sp, -80]!
	ubfiz	x0, x0, 4, 8
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	ldr	x19, [x1, x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0, 2112]
	cmp	w1, 3
	bls	.L604
	ldr	w1, [x19, 16]
	tbz	x1, 2, .L604
	ldr	w1, [x19, 16]
	tbz	x1, 1, .L575
	adrp	x22, .LC4
	adrp	x23, .LC5
	ldr	w1, [x19, 8]
	mov	x24, x0
	add	x22, x22, :lo12:.LC4
	add	x23, x23, :lo12:.LC5
	mov	w21, 0
	str	w1, [x29, 64]
.L576:
	ldr	w1, [x19, 28]
	ldr	w0, [x29, 64]
	ubfx	x1, x1, 16, 5
	ubfx	x0, x0, 22, 6
	cmp	w1, w0
	bge	.L584
	ldr	w0, [x24, 2112]
	cmp	w0, 5
	bhi	.L577
.L580:
	add	w21, w21, 1
	tst	x21, 16777215
	bne	.L579
	ldr	w2, [x19, 28]
	mov	w1, w21
	ldr	w3, [x29, 64]
	mov	x0, x22
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	bl	printk
	mov	w3, 512
	mov	w2, 4
	mov	x1, x19
	mov	x0, x23
	bl	rknand_print_hex
.L579:
	mov	x1, 5
	mov	x0, 1
	bl	usleep_range
	b	.L576
.L577:
	ldr	w0, [x19]
	str	w0, [x29, 72]
	ldr	w0, [x29, 72]
	tbz	x0, 13, .L580
	ldr	w0, [x29, 72]
	tbz	x0, 17, .L580
.L584:
	add	x19, x20, :lo12:.LANCHOR2
	add	x19, x19, 2128
	ldr	w0, [x19, 40]
	cbz	w0, .L585
	ldr	w0, [x19, 32]
	mov	w2, 0
	ldr	w1, [x29, 64]
	ubfx	x1, x1, 22, 5
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29, 64]
	mov	w2, 0
	ldr	w0, [x19, 36]
	ubfx	x1, x1, 22, 5
	lsl	w1, w1, 7
	bl	rknand_dma_unmap_single
.L585:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x20, 2168]
.L573:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L575:
	adrp	x22, .LC6
	adrp	x23, .LC5
	ldr	w0, [x19, 8]
	add	x22, x22, :lo12:.LC6
	add	x23, x23, :lo12:.LC5
	mov	w21, 0
	str	w0, [x29, 64]
.L586:
	ldr	w0, [x29, 64]
	tbz	x0, 20, .L588
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0, 2176]
	cbz	w0, .L589
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L589:
	add	x21, x20, :lo12:.LANCHOR2
	add	x21, x21, 2128
	ldr	w0, [x21, 40]
	cbz	w0, .L590
	ldr	w0, [x21, 32]
	mov	w2, 1
	ldr	w1, [x29, 64]
	ubfx	x1, x1, 22, 5
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29, 64]
	mov	w2, 1
	ldr	w0, [x21, 36]
	ubfx	x1, x1, 22, 5
	lsl	w1, w1, 7
	bl	rknand_dma_unmap_single
.L590:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0, 2176]
	cbz	w0, .L585
	mov	x0, x19
	bl	NandcSendDumpDataDone
	b	.L585
.L588:
	ldr	w0, [x19, 8]
	add	w21, w21, 1
	str	w0, [x29, 64]
	tst	x21, 16777215
	bne	.L587
	ldr	w2, [x29, 64]
	mov	w1, w21
	ldr	w3, [x19, 28]
	mov	x0, x22
	ubfx	x3, x3, 16, 5
	bl	printk
	mov	w3, 512
	mov	w2, 4
	mov	x1, x19
	mov	x0, x23
	bl	rknand_print_hex
.L587:
	mov	x1, 5
	mov	x0, 1
	bl	usleep_range
	b	.L586
.L604:
	ldr	w0, [x19, 8]
	str	w0, [x29, 64]
	ldr	w0, [x29, 64]
	tbz	x0, 20, .L604
	b	.L573
	.size	NandcXferComp, .-NandcXferComp
	.align	2
	.global	NandcCopy1KB
	.type	NandcCopy1KB, %function
NandcCopy1KB:
	stp	x29, x30, [sp, -48]!
	and	w1, w1, 255
	cmp	w1, 1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w2, 255
	str	x21, [sp, 32]
	add	x2, x0, 4096
	add	x21, x0, 512
	ubfiz	x0, x19, 9, 8
	mov	x20, x4
	add	x0, x2, x0
	bne	.L614
	cbz	x3, .L615
	mov	w2, 1024
	mov	x1, x3
	bl	ftl_memcpy
.L615:
	cbz	x20, .L613
	lsr	w19, w19, 1
	mov	w1, 48
	ldr	w0, [x20]
	mul	w19, w19, w1
	and	x19, x19, 8176
	str	w0, [x21, x19]
.L613:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L614:
	cbz	x3, .L618
	mov	x1, x0
	mov	w2, 1024
	mov	x0, x3
	bl	ftl_memcpy
.L618:
	cbz	x20, .L613
	lsr	w19, w19, 1
	mov	w0, 48
	mul	w19, w19, w0
	and	x19, x19, 8176
	ldr	w0, [x21, x19]
	strb	w0, [x20]
	lsr	w1, w0, 8
	strb	w1, [x20, 1]
	lsr	w1, w0, 16
	lsr	w0, w0, 24
	strb	w1, [x20, 2]
	strb	w0, [x20, 3]
	b	.L613
	.size	NandcCopy1KB, .-NandcCopy1KB
	.align	2
	.global	NandcXferData
	.type	NandcXferData, %function
NandcXferData:
	stp	x29, x30, [sp, -192]!
	tst	x3, 63
	add	x29, sp, 0
	stp	x25, x26, [sp, 64]
	and	w25, w0, 255
	stp	x19, x20, [sp, 16]
	adrp	x0, .LANCHOR0
	and	w20, w1, 255
	add	x0, x0, :lo12:.LANCHOR0
	sbfiz	x1, x25, 4, 32
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	mov	x26, x3
	stp	x27, x28, [sp, 80]
	and	w24, w2, 255
	mov	x21, x4
	ldr	x19, [x0, x1]
	bne	.L631
	cbnz	x4, .L632
	add	x21, x29, 128
	mov	w2, 64
	mov	w1, 255
	add	x0, x29, 128
	bl	ftl_memset
.L632:
	mov	x5, x21
	mov	x4, x26
	mov	w2, w24
	mov	w3, 0
	mov	w1, w20
	mov	w0, w25
	bl	NandcXferStart
	mov	w1, w20
	mov	w0, w25
	bl	NandcXferComp
	cbnz	w20, .L656
	adrp	x0, .LANCHOR2
	add	x2, x0, :lo12:.LANCHOR2
	ubfx	x3, x24, 1, 7
	mov	w4, 64
	add	x3, x21, x3, lsl 2
	ldr	w1, [x2, 2180]
	cmp	w1, 25
	mov	w1, 128
	csel	w4, w4, w1, cc
	mov	w1, 0
.L635:
	add	w5, w4, w1
	cmp	x21, x3
	bne	.L636
	add	x0, x0, :lo12:.LANCHOR2
	lsr	w24, w24, 2
	mov	w2, 0
	mov	w22, 0
	ldr	w4, [x0, 2112]
	ldr	w3, [x0, 2180]
.L637:
	cmp	w2, w24
	bcs	.L633
	cbnz	w3, .L643
.L633:
	str	wzr, [x19, 16]
.L644:
	adrp	x0, .LANCHOR2+2112
	ldr	w0, [x0, #:lo12:.LANCHOR2+2112]
	cmp	w0, 5
	bls	.L630
	cbnz	w20, .L630
	ldr	w0, [x19]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L630
	orr	w0, w0, 131072
	mov	w22, -1
	str	w0, [x19]
.L630:
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 192
	ret
.L636:
	ldr	x6, [x2, 2136]
	and	x1, x1, 4294967292
	add	x21, x21, 4
	ldr	w1, [x6, x1]
	strb	w1, [x21, -4]
	lsr	w6, w1, 8
	strb	w6, [x21, -3]
	lsr	w6, w1, 16
	strb	w6, [x21, -2]
	lsr	w1, w1, 24
	strb	w1, [x21, -1]
	mov	w1, w5
	b	.L635
.L643:
	uxtw	x0, w2
	add	x0, x0, 8
	ldr	w0, [x19, x0, lsl 2]
	str	w0, [x29, 120]
	ldr	w0, [x29, 120]
	tbnz	x0, 2, .L659
	ldr	w0, [x29, 120]
	tbnz	x0, 15, .L659
	cmp	w4, 5
	bls	.L639
	ldr	w1, [x29, 120]
	ubfx	x6, x1, 3, 5
	ldr	w1, [x29, 120]
	ldr	w0, [x29, 120]
	ubfx	x1, x1, 27, 1
	ubfx	x5, x0, 16, 5
	ldr	w0, [x29, 120]
	orr	w1, w6, w1, lsl 5
	ubfx	x0, x0, 29, 1
	orr	w0, w5, w0, lsl 5
	cmp	w1, w0
	ldr	w0, [x29, 120]
	bls	.L640
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 27, 1
.L665:
	orr	w0, w1, w0, lsl 5
.L641:
	cmp	w22, w0
	csel	w22, w22, w0, cs
.L638:
	add	w2, w2, 1
	b	.L637
.L640:
	ubfx	x1, x0, 16, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 29, 1
	b	.L665
.L639:
	cmp	w4, 3
	bls	.L660
	ldr	w1, [x29, 120]
	ubfx	x6, x1, 3, 5
	ldr	w1, [x29, 120]
	ldr	w0, [x29, 120]
	ubfx	x1, x1, 28, 1
	ubfx	x5, x0, 16, 5
	ldr	w0, [x29, 120]
	orr	w1, w6, w1, lsl 5
	ubfx	x0, x0, 30, 1
	orr	w0, w5, w0, lsl 5
	cmp	w1, w0
	ldr	w0, [x29, 120]
	bls	.L642
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 28, 1
	b	.L665
.L642:
	ubfx	x1, x0, 16, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 30, 1
	b	.L665
.L660:
	mov	w0, 0
	b	.L641
.L659:
	mov	w22, -1
	b	.L638
.L656:
	mov	w22, 0
	b	.L633
.L631:
	cmp	w20, 1
	bne	.L645
	cmp	x4, 0
	mov	w23, 2
	csel	w23, w23, wzr, ne
	mov	w27, 0
	lsl	w23, w23, 1
	mov	w22, 0
.L646:
	cmp	w22, w24
	bcc	.L648
	mov	w22, 0
	b	.L644
.L648:
	and	w28, w22, 3
	cbz	x26, .L661
	lsl	w3, w22, 9
	add	x3, x26, x3
.L647:
	add	x4, x21, w27, uxtw
	mov	w2, w28
	mov	w1, 1
	mov	x0, x19
	bl	NandcCopy1KB
	add	w22, w22, 2
	mov	w3, w28
	mov	x5, 0
	mov	x4, 0
	mov	w2, 2
	mov	w1, 1
	mov	w0, w25
	bl	NandcXferStart
	add	w27, w27, w23
	mov	w1, 1
	mov	w0, w25
	bl	NandcXferComp
	b	.L646
.L661:
	mov	x3, 0
	b	.L647
.L645:
	mov	w0, w25
	mov	x5, 0
	mov	x4, 0
	mov	w3, 0
	mov	w2, 2
	mov	w1, 0
	bl	NandcXferStart
	mov	w27, 2
	cmp	x21, 0
	mov	w28, 0
	csel	w27, w27, wzr, ne
	mov	w23, 0
	lsl	w0, w27, 1
	mov	w22, 0
	str	w0, [x29, 108]
.L649:
	cmp	w24, w23
	bls	.L644
	mov	w0, w25
	mov	w1, w20
	bl	NandcXferComp
	add	w27, w23, 2
	ldr	w0, [x19, 32]
	cmp	w24, w27
	str	w0, [x29, 120]
	bls	.L650
	mov	x5, 0
	mov	x4, 0
	and	w3, w27, 3
	mov	w2, 2
	mov	w1, 0
	mov	w0, w25
	bl	NandcXferStart
.L650:
	ldr	w0, [x29, 120]
	tbnz	x0, 2, .L662
	ldr	w0, [x29, 120]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w22, w0
	csel	w22, w22, w0, cs
.L651:
	and	w2, w23, 3
	cbz	x26, .L663
	lsl	w3, w23, 9
	add	x3, x26, x3
.L652:
	add	x4, x21, w28, uxtw
	mov	x0, x19
	mov	w1, 0
	bl	NandcCopy1KB
	ldr	w0, [x29, 108]
	mov	w23, w27
	add	w28, w28, w0
	b	.L649
.L662:
	mov	w22, -1
	b	.L651
.L663:
	mov	x3, 0
	b	.L652
	.size	NandcXferData, .-NandcXferData
	.align	2
	.global	FlashReadRawPage
	.type	FlashReadRawPage, %function
FlashReadRawPage:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	ands	w19, w0, 255
	stp	x21, x22, [sp, 32]
	mov	w21, w1
	str	x23, [sp, 48]
	adrp	x1, .LANCHOR1+481
	mov	x22, x2
	mov	x23, x3
	ldrb	w20, [x1, #:lo12:.LANCHOR1+481]
	bne	.L667
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0, 89]
	ldr	w0, [x0, 92]
	mul	w0, w5, w0
	cmp	w0, w21
	mov	w0, 4
	csel	w20, w20, w0, ls
.L667:
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	mov	w1, w21
	mov	w0, w19
	bl	FlashReadCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w2, w20
	mov	x4, x23
	mov	x3, x22
	mov	w1, 0
	mov	w0, w19
	bl	NandcXferData
	mov	w2, w0
	mov	w0, w19
	bl	NandcFlashDeCs
	mov	w0, w2
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FlashReadRawPage, .-FlashReadRawPage
	.align	2
	.global	FlashDdrTunningRead
	.type	FlashDdrTunningRead, %function
FlashDdrTunningRead:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w22, 6
	stp	x23, x24, [sp, 48]
	and	w23, w0, 255
	adrp	x0, .LANCHOR2
	add	x21, x0, :lo12:.LANCHOR2
	stp	x25, x26, [sp, 64]
	mov	w26, w1
	stp	x19, x20, [sp, 16]
	mov	x24, x2
	stp	x27, x28, [sp, 80]
	mov	x19, x0
	mov	x25, x3
	mov	w0, 12
	ldr	x1, [x21, 1152]
	mov	w27, w4
	ldr	w1, [x1, 304]
	str	w1, [x29, 108]
	ldr	w1, [x21, 2112]
	cmp	w1, 8
	csel	w22, w22, w0, cc
	cbz	w4, .L683
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	mov	w0, w23
	bl	FlashReset
	mov	x3, x25
	mov	x2, x24
	mov	w1, w26
	mov	w0, w23
	bl	FlashReadRawPage
	mov	w20, w0
	ldrb	w0, [x21, 2093]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x21, 2093]
	bl	NandcSetMode
	cmn	w20, #1
	bne	.L672
.L681:
	mov	w20, -1
.L669:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L672:
	mov	w2, w20
	mov	w1, w26
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
	bl	printk
	cmp	w20, 9
	bhi	.L674
	sbfiz	x0, x23, 4, 32
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1, x0]
	ldr	w1, [x0, 3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L674:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0, 2184]
	add	w1, w1, 1
	str	w1, [x0, 2184]
	cmp	w1, 2047
	bls	.L669
	mov	x25, 0
	mov	x24, 0
	str	wzr, [x0, 2184]
.L671:
	mov	w5, 0
	mov	w21, 0
	mov	w6, 0
	mov	w19, 0
	mov	w28, -1
.L679:
	stp	w5, w6, [x29, 100]
	mov	w0, w22
	bl	NandcSetDdrPara
	mov	w1, w26
	mov	x3, x25
	mov	x2, x24
	mov	w0, w23
	bl	FlashReadRawPage
	add	w1, w20, 1
	cmp	w0, w1
	ldp	w5, w6, [x29, 100]
	bhi	.L675
	cmp	w0, 2
	bhi	.L685
	add	w19, w19, 1
	cmp	w19, 9
	bls	.L685
	mov	w1, w21
	mov	w20, w0
	sub	w21, w22, w19
	mov	w28, 0
.L677:
	cmp	w19, w6
	csel	w21, w21, w1, hi
.L678:
	cbz	w21, .L680
	mov	w1, w21
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	printk
	mov	w0, w21
	bl	NandcSetDdrPara
.L680:
	cbz	w28, .L669
	adrp	x0, .LC9
	mov	w2, w26
	mov	w1, w23
	add	x0, x0, :lo12:.LC9
	bl	printk
	cbz	w27, .L681
	ldr	w1, [x29, 108]
	lsr	w0, w1, 8
	bl	NandcSetDdrPara
	b	.L669
.L683:
	mov	w20, 1024
	b	.L671
.L675:
	cmp	w19, w6
	bls	.L686
	sub	w21, w5, w19
	cmp	w19, 7
	bhi	.L678
	mov	w6, w19
.L686:
	mov	w19, 0
	b	.L676
.L685:
	mov	w5, w22
	mov	w20, w0
	mov	w28, 0
	mov	x25, 0
	mov	x24, 0
.L676:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L679
	mov	w1, w21
	mov	w21, w5
	b	.L677
	.size	FlashDdrTunningRead, .-FlashDdrTunningRead
	.align	2
	.global	FlashReadPage
	.type	FlashReadPage, %function
FlashReadPage:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 255
	stp	x21, x22, [sp, 32]
	mov	w0, w20
	stp	x23, x24, [sp, 48]
	mov	w22, w1
	stp	x25, x26, [sp, 64]
	mov	x24, x2
	mov	x25, x3
	adrp	x21, .LANCHOR2
	bl	FlashReadRawPage
	mov	w19, w0
	cmn	w0, #1
	bne	.L696
	adrp	x23, .LANCHOR0
	add	x23, x23, :lo12:.LANCHOR0
	ldrb	w26, [x23, 96]
	cbnz	w26, .L697
.L699:
	add	x23, x21, :lo12:.LANCHOR2
	ldrb	w0, [x23, 2104]
	cbz	w0, .L696
	ldr	x0, [x23, 1152]
	mov	w4, 1
	mov	x3, x25
	mov	x2, x24
	mov	w1, w22
	ldr	w26, [x0, 304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	mov	w19, w0
	cmn	w0, #1
	beq	.L700
	ldrb	w0, [x23, 1221]
	cmp	w19, w0, lsr 1
	bls	.L696
.L700:
	lsr	w0, w26, 8
	bl	NandcSetDdrPara
	b	.L696
.L697:
	strb	wzr, [x23, 96]
	mov	x3, x25
	mov	x2, x24
	mov	w1, w22
	mov	w0, w20
	bl	FlashReadRawPage
	strb	w26, [x23, 96]
	cmn	w0, #1
	beq	.L699
	mov	w19, w0
.L696:
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x4, [x21, 2192]
	cbz	x4, .L695
	cmn	w19, #1
	bne	.L695
	mov	x3, x25
	mov	x2, x24
	mov	w1, w22
	mov	w0, w20
	blr	x4
	mov	w19, w0
	mov	w1, w0
	mov	w3, w22
	mov	w2, w20
	adrp	x0, .LC10
	add	x0, x0, :lo12:.LC10
	bl	printk
	cmn	w19, #1
	bne	.L695
	ldrb	w0, [x21, 1220]
	cbz	w0, .L695
	mov	w0, w20
	bl	flash_enter_slc_mode
	ldr	x4, [x21, 2192]
	mov	x3, x25
	mov	x2, x24
	mov	w1, w22
	mov	w0, w20
	blr	x4
	mov	w19, w0
	mov	w0, w20
	bl	flash_exit_slc_mode
.L695:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashReadPage, .-FlashReadPage
	.align	2
	.global	FlashDdrParaScan
	.type	FlashDdrParaScan, %function
FlashDdrParaScan:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x22, x19, :lo12:.LANCHOR2
	and	w20, w0, 255
	mov	w21, w1
	ldrb	w0, [x22, 2093]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22, 2093]
	bl	NandcSetMode
	mov	w4, 0
	mov	x3, 0
	mov	x2, 0
	mov	w1, w21
	mov	w0, w20
	bl	FlashDdrTunningRead
	mov	x3, 0
	mov	w22, w0
	mov	x2, 0
	mov	w1, w21
	mov	w0, w20
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L716
	cmn	w22, #1
	bne	.L717
.L716:
	add	x20, x19, :lo12:.LANCHOR2
	ldrb	w0, [x20, 2093]
	tbz	x0, 0, .L717
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20, 2104]
.L718:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L717:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19, 2104]
	b	.L718
	.size	FlashDdrParaScan, .-FlashDdrParaScan
	.align	2
	.global	FlashLoadPhyInfo
	.type	FlashLoadPhyInfo, %function
FlashLoadPhyInfo:
	stp	x29, x30, [sp, -128]!
	mov	w0, 60
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	strb	w0, [x29, 120]
	mov	w0, 40
	stp	x23, x24, [sp, 48]
	adrp	x22, .LANCHOR2
	stp	x27, x28, [sp, 80]
	adrp	x21, .LANCHOR1
	stp	x25, x26, [sp, 64]
	mov	w27, 20036
	strb	w0, [x29, 121]
	mov	w0, 24
	strb	w0, [x29, 122]
	mov	w0, 16
	strb	w0, [x29, 123]
	add	x21, x21, :lo12:.LANCHOR1
	ldr	w0, [x1, 92]
	mov	w20, 0
	ldr	x1, [x1, 64]
	mov	w24, 4
	str	w0, [x29, 108]
	add	x0, x22, :lo12:.LANCHOR2
	mov	w23, -1
	movk	w27, 0x4e41, lsl 16
	add	x21, x21, 472
	str	x1, [x0, 2200]
	str	wzr, [x0, 2208]
	mov	w0, 0
	bl	flash_enter_slc_mode
.L727:
	add	w28, w20, 1
	add	x25, x19, :lo12:.LANCHOR0
	mov	x26, 0
.L729:
	add	x0, x29, 120
	ldrb	w0, [x0, x26]
	bl	FlashBchSel
	ldr	x2, [x25, 64]
	mov	x3, 0
	mov	w1, w20
	mov	w0, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L728
	ldr	x2, [x25, 64]
	mov	x3, 0
	mov	w1, w28
	mov	w0, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L728
	add	x26, x26, 1
	cmp	x26, 4
	bne	.L729
.L730:
	ldr	w0, [x29, 108]
	subs	w24, w24, #1
	add	w20, w20, w0
	bne	.L727
	b	.L735
.L731:
	mov	w1, 2036
	add	x0, x6, 12
	bl	js_hash
	ldr	w1, [x6, 8]
	cmp	w1, w0
	bne	.L737
	add	x1, x6, 160
	mov	w2, 32
	mov	x0, x21
	bl	ftl_memcpy
	ldr	x1, [x25, 2200]
	add	x23, x19, :lo12:.LANCHOR0
	mov	w2, 32
	add	x0, x23, 112
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x25, 2200]
	mov	w2, 852
	add	x0, x25, 1232
	add	x1, x1, 224
	bl	ftl_memcpy
	ldrh	w0, [x21, 10]
	bl	FlashBlockAlignInit
	str	w20, [x25, 2208]
	ldr	x1, [x25, 2200]
	ldr	w0, [x1, 1076]
	strb	w0, [x25, 2104]
	ldr	w0, [x23, 92]
	udiv	w0, w20, w0
	add	w0, w0, 1
	cmp	w0, 1
	bls	.L733
.L740:
	str	w0, [x23, 72]
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w1, [x1, 14]
	mov	w23, 0
	strb	w1, [x0, 2212]
	b	.L730
.L733:
	mov	w0, 2
	b	.L740
.L737:
	mov	w23, -1
	b	.L730
.L728:
	add	x25, x22, :lo12:.LANCHOR2
	ldr	x6, [x25, 2200]
	ldr	w0, [x6]
	cmp	w0, w27
	bne	.L730
	cbnz	w23, .L731
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19, 92]
	udiv	w20, w20, w0
	add	w20, w20, 1
	str	w20, [x19, 72]
.L735:
	mov	w0, 0
	bl	flash_exit_slc_mode
	mov	w0, w23
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FlashLoadPhyInfo, .-FlashLoadPhyInfo
	.align	2
	.global	ToshibaReadRetrial
	.type	ToshibaReadRetrial, %function
ToshibaReadRetrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w21, w0, 255
	stp	x25, x26, [sp, 64]
	mov	w0, w21
	stp	x19, x20, [sp, 16]
	mov	x25, x2
	stp	x27, x28, [sp, 80]
	mov	x26, x3
	str	w1, [x29, 120]
	stp	x23, x24, [sp, 48]
	bl	NandcWaitFlashReady
	sbfiz	x1, x21, 4, 32
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x2, x0, x1
	ldr	x22, [x0, x1]
	adrp	x1, g_retryMode
	ldrb	w27, [x2, 8]
	ldrb	w0, [x1, #:lo12:g_retryMode]
	str	x1, [x29, 112]
	add	x19, x27, 8
	sub	w0, w0, #67
	str	w27, [x29, 124]
	and	w0, w0, 255
	add	x19, x22, x19, lsl 8
	cmp	w0, 1
	bls	.L758
	adrp	x0, .LANCHOR2+2104
	ldrb	w0, [x0, #:lo12:.LANCHOR2+2104]
	cbz	w0, .L759
	mov	w23, 1
	mov	w0, 0
	bl	NandcSetDdrMode
.L743:
	add	x0, x22, x27, lsl 8
	mov	w1, 92
	str	w1, [x0, 2056]
	mov	w1, 197
	str	w1, [x0, 2056]
.L742:
	ldrsw	x0, [x29, 124]
	mov	w20, 1
	mov	w24, -1
	add	x0, x0, 8
	add	x0, x22, x0, lsl 8
	str	x0, [x29, 104]
.L744:
	adrp	x0, g_maxRetryCount
	ldrb	w0, [x0, #:lo12:g_maxRetryCount]
	add	w0, w0, 1
	cmp	w20, w0
	bcc	.L753
	mov	w28, w24
.L752:
	ldr	x0, [x29, 112]
	mov	w1, 0
	ldrb	w0, [x0, #:lo12:g_retryMode]
	sub	w0, w0, #67
	and	w0, w0, 255
	cmp	w0, 1
	mov	x0, x19
	bhi	.L754
	bl	SandiskSetRRPara
.L755:
	ldrsw	x0, [x29, 124]
	add	x0, x0, 8
	add	x22, x22, x0, lsl 8
	mov	w0, 255
	str	w0, [x22, 8]
	adrp	x0, .LANCHOR2+1221
	ldrb	w0, [x0, #:lo12:.LANCHOR2+1221]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L756
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L756:
	mov	w0, w21
	bl	NandcWaitFlashReady
	cbz	w23, .L741
	mov	w0, 4
	bl	NandcSetDdrMode
.L741:
	mov	w0, w28
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L759:
	mov	w23, 0
	b	.L743
.L758:
	mov	w23, 0
	b	.L742
.L753:
	ldr	x0, [x29, 112]
	mov	w1, w20
	ldrb	w0, [x0, #:lo12:g_retryMode]
	sub	w0, w0, #67
	and	w0, w0, 255
	cmp	w0, 1
	mov	x0, x19
	bhi	.L745
	bl	SandiskSetRRPara
.L746:
	ldr	x0, [x29, 112]
	ldrb	w0, [x0, #:lo12:g_retryMode]
	cmp	w0, 34
	bne	.L747
	adrp	x0, g_maxRetryCount
	ldrb	w0, [x0, #:lo12:g_maxRetryCount]
	sub	w0, w0, #3
	cmp	w20, w0
	bne	.L747
	ldr	x1, [x29, 104]
	mov	w0, 179
	str	w0, [x1, 8]
.L747:
	add	x0, x22, x27, lsl 8
	mov	w1, 38
	str	w1, [x0, 2056]
	mov	w1, 93
	str	w1, [x0, 2056]
	cbz	w23, .L748
	mov	w0, 4
	bl	NandcSetDdrMode
	ldr	w1, [x29, 120]
	mov	x3, x26
	mov	x2, x25
	mov	w0, w21
	bl	FlashReadRawPage
	mov	w28, w0
	mov	w0, 0
	bl	NandcSetDdrMode
.L749:
	cmn	w28, #1
	beq	.L750
	adrp	x0, .LANCHOR2+1221
	cmn	w24, #1
	csel	w24, w24, w28, ne
	ldrb	w0, [x0, #:lo12:.LANCHOR2+1221]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L752
	mov	x26, 0
	mov	x25, 0
.L750:
	add	w20, w20, 1
	b	.L744
.L745:
	bl	ToshibaSetRRPara
	b	.L746
.L748:
	ldr	w1, [x29, 120]
	mov	x3, x26
	mov	x2, x25
	mov	w0, w21
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L749
.L754:
	bl	ToshibaSetRRPara
	b	.L755
	.size	ToshibaReadRetrial, .-ToshibaReadRetrial
	.align	2
	.global	SamsungReadRetrial
	.type	SamsungReadRetrial, %function
SamsungReadRetrial:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 255
	stp	x19, x20, [sp, 16]
	mov	w0, w22
	stp	x23, x24, [sp, 48]
	mov	w23, w1
	stp	x25, x26, [sp, 64]
	mov	x24, x2
	str	x27, [sp, 80]
	mov	x25, x3
	bl	NandcWaitFlashReady
	adrp	x26, g_maxRetryCount
	sbfiz	x1, x22, 4, 32
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	adrp	x27, .LANCHOR2
	add	x2, x0, x1
	add	x26, x26, :lo12:g_maxRetryCount
	add	x27, x27, :lo12:.LANCHOR2
	mov	w21, 1
	ldr	x0, [x0, x1]
	mov	w19, -1
	ldrb	w20, [x2, 8]
	add	x20, x20, 8
	add	x20, x0, x20, lsl 8
.L770:
	ldrb	w0, [x26]
	add	w0, w0, 1
	cmp	w21, w0
	bcc	.L774
.L773:
	mov	x0, x20
	mov	w1, 0
	bl	SamsungSetRRPara
	adrp	x0, .LANCHOR2+1221
	ldrb	w0, [x0, #:lo12:.LANCHOR2+1221]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L769
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L769:
	mov	w0, w19
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L774:
	mov	w1, w21
	mov	x0, x20
	bl	SamsungSetRRPara
	mov	x3, x25
	mov	x2, x24
	mov	w1, w23
	mov	w0, w22
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L771
	ldrb	w1, [x27, 1221]
	cmn	w19, #1
	csel	w19, w19, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L776
	mov	x25, 0
	mov	x24, 0
.L771:
	add	w21, w21, 1
	b	.L770
.L776:
	mov	w19, w0
	b	.L773
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	2
	.global	MicronReadRetrial
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 255
	adrp	x0, .LANCHOR2
	stp	x23, x24, [sp, 48]
	str	w1, [x29, 140]
	add	x1, x0, :lo12:.LANCHOR2
	stp	x25, x26, [sp, 64]
	mov	x24, x2
	stp	x21, x22, [sp, 32]
	mov	x25, x3
	stp	x27, x28, [sp, 80]
	str	x0, [x29, 128]
	ldrb	w19, [x1, 1221]
	ldrb	w1, [x1, 1220]
	cbnz	w1, .L784
	add	w19, w19, w19, lsl 1
	asr	w19, w19, 2
.L785:
	adrp	x21, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR0
	add	x21, x21, w20, sxtw 4
	mov	w23, 0
	mov	w28, 137
.L795:
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldrb	w4, [x21, 8]
	adrp	x7, g_maxRetryCount
	ldr	x8, [x21]
	add	x7, x7, :lo12:g_maxRetryCount
	mov	w22, 0
	mov	w26, -1
	add	x27, x8, x4, lsl 8
.L786:
	ldrb	w0, [x7]
	cmp	w22, w0
	bcc	.L790
.L789:
	add	x4, x8, x4, lsl 8
	mov	w0, 239
	str	x4, [x29, 120]
	str	w0, [x4, 2056]
	mov	x0, 1000
	str	w28, [x4, 2052]
	bl	__const_udelay
	ldr	x4, [x29, 120]
	cmp	w26, w19
	str	wzr, [x4, 2048]
	str	wzr, [x4, 2048]
	str	wzr, [x4, 2048]
	str	wzr, [x4, 2048]
	bcc	.L791
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L791:
	cmp	w26, 256
	ccmn	w26, #1, 4, ne
	bne	.L792
	ldr	w2, [x29, 140]
	adrp	x0, .LC11
	mov	w4, w26
	mov	w3, w22
	mov	w1, w22
	add	x0, x0, :lo12:.LC11
	bl	printk
	cbnz	w23, .L793
	ldr	x0, [x29, 128]
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w0, [x0, 1220]
	cbz	w0, .L783
	cmn	w26, #1
	bne	.L783
	mov	w1, 3
	mov	w0, w20
	mov	w23, 1
	bl	micron_auto_read_calibration_config
	b	.L795
.L784:
	mov	w0, 3
	sdiv	w19, w19, w0
	b	.L785
.L790:
	mov	w0, 239
	str	w0, [x27, 2056]
	str	w28, [x27, 2052]
	mov	x0, 1000
	stp	x4, x7, [x29, 96]
	str	x8, [x29, 112]
	bl	__const_udelay
	add	w9, w22, 1
	str	w9, [x27, 2048]
	str	wzr, [x27, 2048]
	mov	x3, x25
	ldr	w1, [x29, 140]
	mov	x2, x24
	str	wzr, [x27, 2048]
	mov	w0, w20
	str	wzr, [x27, 2048]
	str	w9, [x29, 120]
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	w9, [x29, 120]
	ldp	x4, x7, [x29, 96]
	ldr	x8, [x29, 112]
	beq	.L787
	cmn	w26, #1
	csel	w26, w26, w0, ne
	cmp	w0, w19
	bcc	.L797
	mov	x25, 0
	mov	x24, 0
.L787:
	mov	w22, w9
	b	.L786
.L797:
	mov	w26, w0
	mov	x25, 0
	mov	x24, 0
	b	.L789
.L793:
	mov	w0, w20
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L783:
	mov	w0, w26
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L792:
	cbz	w23, .L783
	mov	w1, 0
	mov	w0, w20
	mov	w26, 256
	bl	micron_auto_read_calibration_config
	b	.L783
	.size	MicronReadRetrial, .-MicronReadRetrial
	.align	2
	.global	HynixReadRetrial
	.type	HynixReadRetrial, %function
HynixReadRetrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	stp	x27, x28, [sp, 80]
	and	x28, x0, 255
	add	x0, x21, :lo12:.LANCHOR2
	stp	x25, x26, [sp, 64]
	add	x0, x0, 1232
	stp	x23, x24, [sp, 48]
	stp	x19, x20, [sp, 16]
	mov	w27, w1
	adrp	x22, .LANCHOR0
	add	x1, x0, x28
	ldrb	w24, [x0, 2]
	add	x0, x22, :lo12:.LANCHOR0
	mov	x23, x28
	mov	x25, x2
	mov	x26, x3
	ldrb	w19, [x1, 12]
	ldr	x0, [x0, 104]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #7
	and	w0, w0, 255
	cmp	w0, 1
	bhi	.L809
	ldrb	w19, [x1, 20]
.L809:
	mov	w0, w23
	bl	NandcWaitFlashReady
	add	x5, x21, :lo12:.LANCHOR2
	mov	w4, 0
	add	x6, x5, 1236
	mov	w20, -1
.L810:
	cmp	w4, w24
	bcc	.L815
.L814:
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22, 104]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #7
	and	w0, w0, 255
	cmp	w0, 1
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, x28
	bhi	.L816
	strb	w19, [x0, 1252]
.L817:
	add	x21, x21, :lo12:.LANCHOR2
	ldrb	w0, [x21, 1221]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L808
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L808:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L815:
	add	w19, w19, 1
	ldrb	w1, [x5, 1233]
	and	w19, w19, 255
	mov	x2, x6
	cmp	w24, w19
	str	w4, [x29, 108]
	csel	w19, w19, wzr, hi
	stp	x6, x5, [x29, 112]
	mov	w3, w19
	mov	w0, w23
	bl	HynixSetRRPara
	mov	x3, x26
	mov	x2, x25
	mov	w1, w27
	mov	w0, w23
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	w4, [x29, 108]
	ldp	x6, x5, [x29, 112]
	beq	.L812
	ldrb	w1, [x5, 1221]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L819
	mov	x26, 0
	mov	x25, 0
.L812:
	add	w4, w4, 1
	b	.L810
.L819:
	mov	w20, w0
	b	.L814
.L816:
	strb	w19, [x0, 1244]
	b	.L817
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	2
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	and	w23, w0, 255
	stp	x21, x22, [sp, 32]
	mov	x24, x2
	stp	x25, x26, [sp, 64]
	mov	w0, w23
	stp	x19, x20, [sp, 16]
	mov	w26, w1
	mov	x25, x3
	stp	x27, x28, [sp, 80]
	bl	NandcWaitFlashReady
	adrp	x22, .LANCHOR2
	sbfiz	x1, x23, 4, 32
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	add	x3, x0, x1
	ldr	x1, [x0, x1]
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w19, [x3, 8]
	ldrb	w2, [x0, 2092]
	cbnz	w2, .L827
	add	x19, x1, x19, lsl 8
	add	x21, x21, :lo12:.LANCHOR3
	mov	x27, x0
	mov	x4, 0
	mov	w20, -1
	mov	w6, 239
	mov	w5, 141
.L831:
	str	w6, [x19, 2056]
	add	x0, x21, x4
	str	w5, [x19, 2052]
	add	w28, w4, 1
	stp	w6, w5, [x29, 96]
	mov	x3, x25
	ldrsb	w0, [x0, 1]
	mov	x2, x24
	str	w0, [x19, 2048]
	mov	w1, w26
	str	wzr, [x19, 2048]
	mov	w0, w23
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	x4, [x29, 104]
	bl	FlashReadRawPage
	cmn	w0, #1
	ldp	w6, w5, [x29, 96]
	ldr	x4, [x29, 104]
	beq	.L828
	ldrb	w1, [x27, 1221]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L839
	mov	x25, 0
	mov	x24, 0
.L828:
	add	x4, x4, 1
	cmp	x4, 25
	bne	.L831
	mov	w28, 26
.L830:
	mov	w0, 239
	str	w0, [x19, 2056]
	mov	w0, 141
.L855:
	str	w0, [x19, 2052]
	add	x22, x22, :lo12:.LANCHOR2
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	ldrb	w0, [x22, 1221]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L837
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L837:
	cmp	w20, 256
	ccmn	w20, #1, 4, ne
	bne	.L838
	adrp	x0, .LC12
	mov	w4, w20
	mov	w3, w28
	mov	w2, w26
	mov	w1, w28
	add	x0, x0, :lo12:.LC12
	bl	printk
.L838:
	mov	w0, w23
	bl	NandcWaitFlashReady
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L839:
	mov	w20, w0
	b	.L830
.L827:
	add	x21, x21, :lo12:.LANCHOR3
	add	x19, x1, x19, lsl 8
	add	x21, x21, 36
	mov	x27, x0
	mov	w20, -1
	mov	w28, 1
	mov	w5, 239
	mov	w4, 137
.L836:
	str	w5, [x19, 2056]
	mov	x3, x25
	str	w4, [x19, 2052]
	mov	x2, x24
	stp	w5, w4, [x29, 100]
	mov	w1, w26
	ldrb	w0, [x21]
	str	w0, [x19, 2048]
	ldrb	w0, [x21, 1]
	str	w0, [x19, 2048]
	ldrb	w0, [x21, 2]
	str	w0, [x19, 2048]
	ldrb	w0, [x21, 3]
	str	w0, [x19, 2048]
	mov	w0, w23
	bl	FlashReadRawPage
	cmn	w0, #1
	ldp	w5, w4, [x29, 100]
	beq	.L833
	ldrb	w1, [x27, 1221]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L840
	mov	x25, 0
	mov	x24, 0
.L833:
	add	w28, w28, 1
	add	x21, x21, 4
	cmp	w28, 26
	bne	.L836
.L835:
	mov	w0, 239
	str	w0, [x19, 2056]
	mov	w0, 137
	b	.L855
.L840:
	mov	w20, w0
	b	.L835
	.size	samsung_read_retrial, .-samsung_read_retrial
	.align	2
	.global	FlashProgPage
	.type	FlashProgPage, %function
FlashProgPage:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w20, w1
	stp	x21, x22, [sp, 32]
	adrp	x1, .LANCHOR1+481
	str	x23, [sp, 48]
	mov	x22, x2
	ldrb	w21, [x1, #:lo12:.LANCHOR1+481]
	ands	w19, w0, 255
	mov	x23, x3
	bne	.L857
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0, 89]
	ldr	w2, [x0, 92]
	mul	w1, w1, w2
	cmp	w1, w20
	bls	.L857
	ldrb	w0, [x0, 88]
	cmp	w0, 0
	mov	w0, 4
	csel	w21, w21, w0, eq
.L857:
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	mov	w1, w20
	mov	w0, w19
	bl	FlashProgFirstCmd
	mov	x4, x23
	mov	x3, x22
	mov	w2, w21
	mov	w1, 1
	mov	w0, w19
	bl	NandcXferData
	mov	w1, w20
	mov	w0, w19
	bl	FlashProgSecondCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w1, w20
	mov	w0, w19
	bl	FlashReadStatus
	mov	w2, w0
	mov	w0, w19
	bl	NandcFlashDeCs
	and	w0, w2, 1
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FlashProgPage, .-FlashProgPage
	.align	2
	.global	FlashSavePhyInfo
	.type	FlashSavePhyInfo, %function
FlashSavePhyInfo:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	mov	w23, 20036
	ldr	x0, [x19, 64]
	movk	w23, 0x4e41, lsl 16
	str	x0, [x20, 2200]
	mov	w22, 0
	ldrb	w0, [x20, 2213]
	mov	w21, 0
	bl	FlashBchSel
	ldr	x0, [x19, 64]
	mov	w2, 2048
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x20, 2200]
	mov	w2, 32
	str	w23, [x0]
	ldr	x0, [x20, 2200]
	ldrb	w1, [x19, 3216]
	add	x0, x0, 16
	strh	w1, [x0, -4]
	ldrb	w1, [x19, 89]
	strh	w1, [x0, -2]
	ldrb	w1, [x20, 2104]
	str	w1, [x0, 1060]
	adrp	x1, IDByte
	add	x1, x1, :lo12:IDByte
	bl	ftl_memcpy
	ldr	x0, [x20, 2200]
	mov	w2, 8
	add	x1, x19, 3220
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x20, 2200]
	mov	w2, 32
	add	x1, x19, 3228
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x20, 2200]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 472
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x20, 2200]
	mov	w2, 32
	add	x1, x19, 112
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x20, 2200]
	mov	w2, 852
	add	x1, x20, 1232
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x6, [x20, 2200]
	mov	w1, 2036
	add	x0, x6, 12
	bl	js_hash
	str	w0, [x6, 8]
	mov	w0, 1592
	str	w0, [x6, 4]
	ldr	x0, [x20, 2216]
	str	x0, [x20, 2200]
	mov	w0, 0
	bl	flash_enter_slc_mode
.L866:
	ldr	w1, [x19, 92]
	mov	w2, 0
	mov	w0, 0
	mul	w1, w21, w1
	bl	FlashEraseBlock
	ldrb	w0, [x20, 1220]
	cbz	w0, .L861
	mov	w24, 0
.L862:
	ldr	w1, [x19, 92]
	mov	x3, 0
	ldr	x2, [x19, 64]
	mov	w0, 0
	madd	w1, w21, w1, w24
	add	w24, w24, 1
	bl	FlashProgPage
	cmp	w24, 10
	bne	.L862
.L863:
	ldr	w1, [x19, 92]
	mov	x3, 0
	ldr	x2, [x20, 2216]
	mov	w0, 0
	mul	w1, w21, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	add	w7, w21, 1
	beq	.L864
	ldr	x6, [x20, 2200]
	ldr	w0, [x6]
	cmp	w0, w23
	bne	.L864
	mov	w1, 2036
	add	x0, x6, 12
	bl	js_hash
	ldr	w1, [x6, 8]
	cmp	w1, w0
	bne	.L864
	ldr	w0, [x19, 92]
	cmp	w22, 1
	str	w7, [x19, 72]
	mul	w21, w0, w21
	str	w21, [x20, 2208]
	beq	.L867
	mov	w22, 1
.L864:
	mov	w21, w7
	cmp	w7, 4
	bne	.L866
.L865:
	mov	w0, 0
	bl	flash_exit_slc_mode
	cmp	w22, 0
	csetm	w0, eq
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L861:
	ldr	w1, [x19, 92]
	mov	x3, 0
	ldr	x2, [x19, 64]
	mov	w0, 0
	mul	w1, w21, w1
	bl	FlashProgPage
	ldr	w1, [x19, 92]
	mov	x3, 0
	ldr	x2, [x19, 64]
	mov	w0, 0
	mul	w1, w21, w1
	add	w1, w1, 1
	bl	FlashProgPage
	b	.L863
.L867:
	mov	w22, 2
	b	.L865
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	2
	.global	FlashReadIdbDataRaw
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x25, x26, [sp, 64]
	mov	x25, x0
	mov	w0, 60
	stp	x21, x22, [sp, 32]
	strb	w0, [x29, 120]
	mov	w0, 40
	strb	w0, [x29, 121]
	mov	w0, 24
	strb	w0, [x29, 122]
	mov	w0, 16
	adrp	x21, .LANCHOR2
	strb	w0, [x29, 123]
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x27, x28, [sp, 80]
	ldrb	w26, [x0, 1221]
	ldr	w0, [x0, 2100]
	cbz	w0, .L875
	mov	w0, 0
	bl	flash_enter_slc_mode
.L875:
	adrp	x22, .LANCHOR0
	add	x22, x22, :lo12:.LANCHOR0
	mov	w28, 35899
	add	x27, x29, 120
	mov	x19, x22
	mov	w24, -1
	mov	w20, 2
	movk	w28, 0xfcdc, lsl 16
	mov	w2, 2048
	mov	w1, 0
	mov	x0, x25
	bl	ftl_memset
.L876:
	ldrb	w0, [x22, 89]
	cmp	w20, w0
	bcc	.L881
.L880:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, w26
	bl	FlashBchSel
	ldr	w0, [x21, 2100]
	cbz	w0, .L874
	mov	w0, 0
	bl	flash_exit_slc_mode
.L874:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L881:
	mov	x23, 0
.L878:
	ldrb	w4, [x23, x27]
	str	w4, [x29, 108]
	mov	w0, w4
	bl	FlashBchSel
	ldr	w1, [x19, 92]
	mov	x3, 0
	ldr	x2, [x19, 64]
	mov	w0, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	w4, [x29, 108]
	bne	.L877
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L878
.L879:
	add	w20, w20, 1
	b	.L876
.L884:
	mov	w24, 0
	b	.L880
.L877:
	ldr	x0, [x19, 64]
	ldr	w0, [x0]
	cmp	w0, w28
	bne	.L879
	mov	w1, w4
	adrp	x0, .LC13
	add	x0, x0, :lo12:.LC13
	bl	printk
	ldr	x1, [x19, 64]
	mov	w2, 2048
	mov	x0, x25
	bl	ftl_memcpy
	ldr	x0, [x19, 64]
	ldr	w0, [x0, 512]
	strb	w0, [x19, 89]
	ldr	w0, [x19, 72]
	cmp	w20, w0
	bcs	.L884
	mov	w24, 0
	str	w20, [x19, 72]
	bl	FlashSavePhyInfo
	b	.L879
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	2
	.global	FlashInit
	.type	FlashInit, %function
FlashInit:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x23, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	adrp	x19, .LANCHOR2
	add	x21, x19, :lo12:.LANCHOR2
	mov	x22, x0
	stp	x25, x26, [sp, 64]
	mov	w0, 32768
	stp	x27, x28, [sp, 80]
	bl	ftl_dma32_malloc
	str	x0, [x23, 64]
	mov	w0, 32768
	bl	ftl_dma32_malloc
	str	x0, [x21, 2216]
	mov	w0, 4096
	bl	ftl_dma32_malloc
	str	x0, [x21, 2224]
	mov	w0, 32768
	bl	ftl_dma32_malloc
	str	x0, [x21, 2232]
	mov	w0, 4096
	bl	ftl_dma32_malloc
	str	x0, [x21, 2240]
	mov	w0, 50
	strb	w0, [x21, 2212]
	adrp	x25, .LC14
	strb	w0, [x23, 89]
	mov	w0, 128
	str	w0, [x23, 92]
	mov	w0, 60
	strb	wzr, [x21, 2104]
	add	x25, x25, :lo12:.LC14
	strb	w0, [x21, 2213]
	mov	x0, x22
	strb	wzr, [x21, 2248]
	adrp	x22, IDByte
	str	wzr, [x21, 2184]
	add	x21, x22, :lo12:IDByte
	mov	w24, 0
	mov	w28, 239
	strb	wzr, [x23, 88]
	str	wzr, [x23, 72]
	bl	NandcInit
.L899:
	ldrb	w2, [x23, 8]
	and	w26, w24, 255
	str	w2, [x29, 108]
	mov	w0, w26
	ldr	x27, [x23]
	bl	FlashReset
	mov	w0, w26
	bl	NandcFlashCs
	ldr	w2, [x29, 108]
	mov	w0, 144
	ubfiz	x2, x2, 8, 8
	add	x27, x27, x2
	str	w0, [x27, 2056]
	mov	x0, 1000
	str	wzr, [x27, 2052]
	bl	__const_udelay
	ldr	w0, [x27, 2048]
	and	w0, w0, 255
	strb	w0, [x21]
	cmp	w0, 44
	ldr	w2, [x27, 2048]
	strb	w2, [x21, 1]
	ldr	w2, [x27, 2048]
	strb	w2, [x21, 2]
	ldr	w2, [x27, 2048]
	strb	w2, [x21, 3]
	ldr	w2, [x27, 2048]
	strb	w2, [x21, 4]
	ldr	w2, [x27, 2048]
	strb	w2, [x21, 5]
	bne	.L894
	str	w28, [x27, 2056]
	mov	w0, 1
	str	w0, [x27, 2052]
	mov	x0, 1000
	bl	__const_udelay
	mov	w0, 4
	str	w0, [x27, 2048]
	str	wzr, [x27, 2048]
	str	wzr, [x27, 2048]
	str	wzr, [x27, 2048]
.L894:
	mov	w0, w26
	bl	NandcFlashDeCs
	ldrb	w2, [x21]
	sub	w0, w2, #1
	and	w0, w0, 255
	cmp	w0, 253
	bhi	.L895
	ldrb	w7, [x21, 5]
	add	w1, w24, 1
	ldrb	w6, [x21, 4]
	mov	x0, x25
	ldrb	w5, [x21, 3]
	ldrb	w4, [x21, 2]
	ldrb	w3, [x21, 1]
	bl	printk
.L895:
	cbnz	w24, .L896
	ldrb	w0, [x22, #:lo12:IDByte]
	add	x1, x22, :lo12:IDByte
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 253
	bhi	.L947
	ldrb	w0, [x1, 1]
	cmp	w0, 255
	beq	.L947
	bl	FlashCs123Init
.L896:
	ldrb	w0, [x21]
	cmp	w0, 181
	bne	.L898
	mov	w0, 44
	strb	w0, [x21]
.L898:
	add	w24, w24, 1
	add	x23, x23, 16
	add	x21, x21, 8
	cmp	w24, 4
	bne	.L899
	ldrb	w0, [x22, #:lo12:IDByte]
	cmp	w0, 173
	beq	.L900
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0, 2108]
	bl	NandcSetDdrMode
.L900:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w2, 852
	mov	w1, 0
	add	x0, x21, 1232
	bl	ftl_memset
	add	x2, x20, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR1
	add	x1, x0, :lo12:.LANCHOR1
	ldr	w4, [x21, 2116]
	add	x1, x1, 472
	strb	wzr, [x2, 96]
	str	x1, [x2, 104]
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L901
	ldrb	w1, [x1, 19]
	cmp	w1, 50
	beq	.L901
	mov	w1, 1
	str	w1, [x21, 2100]
.L901:
	add	x1, x22, :lo12:IDByte
	ldrb	w2, [x1, 1]
	add	w1, w2, 95
	and	w3, w1, 255
	mov	x1, 1
	cmp	w3, 57
	bhi	.L902
	movk	x1, 0x205, lsl 48
	lsr	x1, x1, x3
	mvn	x1, x1
.L902:
	and	w1, w1, 1
	cmp	w2, 241
	eor	w1, w1, 1
	cset	w3, eq
	orr	w1, w3, w1
	cbnz	w1, .L903
	cmp	w2, 220
	bne	.L904
	add	x1, x22, :lo12:IDByte
	ldrb	w1, [x1, 3]
	cmp	w1, 149
	bne	.L904
.L903:
	add	x1, x20, :lo12:.LANCHOR0
	add	x3, x19, :lo12:.LANCHOR2
	mov	w6, 16
	mov	w5, 1
	add	x7, x22, :lo12:IDByte
	strb	w6, [x1, 89]
	strb	w5, [x1, 88]
	add	x1, x0, :lo12:.LANCHOR1
	strb	w6, [x3, 2213]
	ldrb	w6, [x22, #:lo12:IDByte]
	strb	w6, [x1, 3417]
	strb	w2, [x1, 3418]
	cmp	w6, 152
	bne	.L906
	ldrsb	w1, [x7, 4]
	tbnz	w1, #31, .L907
	mov	w1, 24
	strb	w1, [x3, 2213]
.L906:
	cmp	w4, 2049
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ccmp	w4, w1, 4, ne
	bne	.L909
	add	x1, x19, :lo12:.LANCHOR2
	mov	w3, 16
	strb	w3, [x1, 2213]
.L909:
	cmp	w2, 218
	bne	.L910
	add	x1, x0, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x1, 3430]
	mov	w2, -38
.L993:
	strb	w2, [x1, 3418]
.L911:
	add	x21, x0, :lo12:.LANCHOR1
	add	x0, x20, :lo12:.LANCHOR0
	mov	w2, 32
	add	x1, x21, 3288
	add	x0, x0, 112
	bl	ftl_memcpy
	mov	w2, 32
	add	x1, x21, 3416
	add	x0, x21, 472
	bl	ftl_memcpy
.L904:
	add	x21, x20, :lo12:.LANCHOR0
	ldrb	w0, [x21, 88]
	cbnz	w0, .L914
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L916
	ldr	x0, [x21, 104]
	add	x21, x19, :lo12:.LANCHOR2
	ldrb	w1, [x0, 17]
	and	w0, w1, 7
	strb	w0, [x21, 2093]
	tbnz	x1, 0, .L916
	mov	w1, 1
	strb	w1, [x21, 2104]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x21, 2093]
	bl	NandcSetMode
.L916:
	add	x0, x20, :lo12:.LANCHOR0
	add	x21, x19, :lo12:.LANCHOR2
	ldr	x0, [x0, 104]
	ldrb	w0, [x0, 26]
	strb	w0, [x21, 1220]
	bl	FlashLoadPhyInfo
	cbz	w0, .L914
	ldr	w0, [x21, 2108]
	cbz	w0, .L919
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
.L988:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L914
	add	x21, x20, :lo12:.LANCHOR0
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x0, [x21, 104]
	ldrh	w1, [x0, 14]
	adrp	x0, .LC15
	add	x0, x0, :lo12:.LC15
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L893
	bl	FlashDieInfoInit
	ldr	x0, [x21, 104]
	ldrb	w0, [x0, 19]
	bl	FlashGetReadRetryDefault
	ldr	x0, [x21, 104]
	ldrh	w1, [x21, 3452]
	add	w1, w1, 4095
	ldrb	w2, [x0, 9]
	cmp	w2, w1, lsr 12
	blt	.L921
	ldrh	w1, [x0, 14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L922
.L921:
	ldrh	w1, [x0, 14]
	and	w1, w1, -256
	strh	w1, [x0, 14]
.L922:
	add	x21, x19, :lo12:.LANCHOR2
	ldrb	w0, [x21, 2093]
	tst	w0, 6
	beq	.L923
	bl	FlashSavePhyInfo
	mov	w0, 0
	bl	flash_enter_slc_mode
	ldr	w1, [x21, 2208]
	mov	w0, 0
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L923:
	bl	FlashSavePhyInfo
.L914:
	add	x23, x20, :lo12:.LANCHOR0
	add	x21, x19, :lo12:.LANCHOR2
	ldr	x2, [x23, 104]
	str	xzr, [x21, 2192]
	ldrb	w0, [x2, 26]
	strb	w0, [x21, 1220]
	ldrh	w0, [x2, 16]
	ubfx	x1, x0, 7, 1
	strb	w1, [x23, 96]
	ubfx	x1, x0, 3, 1
	strb	w1, [x21, 2249]
	ubfx	x1, x0, 4, 1
	ubfx	x0, x0, 8, 3
	strb	w1, [x21, 2094]
	strb	w0, [x21, 2093]
	ldrh	w1, [x2, 10]
	ldrb	w0, [x2, 12]
	sdiv	w1, w1, w0
	ldrb	w0, [x2, 18]
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x23, 104]
	ldrh	w1, [x0, 16]
	tbz	x1, 6, .L925
	adrp	x1, g_retryMode
	ldrb	w0, [x0, 19]
	adrp	x2, g_maxRegNum
	ldrb	w3, [x21, 1234]
	strb	w0, [x1, #:lo12:g_retryMode]
	mov	x4, x2
	ldrb	w1, [x21, 1233]
	strb	w1, [x2, #:lo12:g_maxRegNum]
	adrp	x1, g_maxRetryCount
	strb	w3, [x1, #:lo12:g_maxRetryCount]
	sub	w3, w0, #1
	and	w3, w3, 255
	cmp	w3, 7
	bhi	.L926
	adrp	x1, HynixReadRetrial
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x21, 2192]
	sub	w1, w0, #5
	and	w1, w1, 255
	cmp	w1, 1
	ccmp	w0, 8, 4, hi
	bne	.L927
	mov	w1, 1
	str	w1, [x21, 2176]
.L927:
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w0, 7
	beq	.L989
	cmp	w0, 8
	bne	.L950
.L989:
	add	x1, x1, 1260
.L928:
	mov	x2, 0
	mov	w3, 0
.L930:
	ldrsb	w4, [x1, x2]
	add	x2, x2, 1
	cmp	w4, 0
	cinc	w3, w3, eq
	cmp	x2, 32
	bne	.L930
	cmp	w3, 27
	bls	.L925
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
.L925:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	ldr	w1, [x0, 2116]
	cmp	w1, w2
	bne	.L940
	ldrb	w0, [x0, 1220]
	cbz	w0, .L940
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0, 104]
	strb	wzr, [x0, 18]
.L940:
	ldrb	w0, [x22, #:lo12:IDByte]
	cmp	w0, 44
	bne	.L941
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w2, [x0, 2104]
	cbz	w2, .L941
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w1, w2
	bne	.L942
	ldrb	w0, [x0, 1220]
	cbnz	w0, .L941
.L942:
	add	x0, x19, :lo12:.LANCHOR2
	strb	wzr, [x0, 2104]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L941:
	add	x1, x19, :lo12:.LANCHOR2
	ldrb	w0, [x1, 2093]
	tst	w0, 6
	beq	.L943
	ldrb	w1, [x1, 2104]
	cbnz	w1, .L944
	tbnz	x0, 0, .L943
.L944:
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0, 2208]
	mov	w0, 0
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L943:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w21, 16
	add	x19, x19, :lo12:.LANCHOR2
	ldr	x0, [x20, 104]
	ldrb	w0, [x0, 20]
	bl	FlashBchSel
	add	x0, x20, 3456
	bl	FlashReadIdbDataRaw
	mov	w0, 18928
	strb	w21, [x20, 89]
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	ldr	x1, [x20, 104]
	ldrb	w4, [x22, #:lo12:IDByte]
	ldrb	w2, [x1, 12]
	strh	w2, [x19, 1200]
	ldrb	w2, [x1, 7]
	str	w2, [x19, 1196]
	add	x2, x22, :lo12:IDByte
	ldrb	w3, [x2, 1]
	ldrb	w2, [x2, 3]
	orr	w2, w4, w2, lsl 24
	lsl	w4, w3, w21
	orr	w3, w4, w3, lsl 8
	ldrh	w4, [x1, 14]
	orr	w2, w2, w3
	str	w2, [x19, 1192]
	ldrb	w2, [x20, 3216]
	strh	w2, [x19, 1202]
	ldrb	w2, [x1, 13]
	strh	w2, [x19, 1204]
	strh	w4, [x19, 1206]
	ldrh	w2, [x1, 10]
	strh	w2, [x19, 1208]
	ldrb	w3, [x1, 12]
	ldrh	w2, [x1, 10]
	sdiv	w2, w2, w3
	strh	w2, [x19, 1210]
	ldrb	w3, [x1, 9]
	strh	w3, [x19, 1212]
	ldrh	w5, [x1, 10]
	ldrb	w2, [x1, 9]
	mul	w2, w2, w5
	mov	w5, 512
	strh	w5, [x19, 1216]
	ldrb	w5, [x20, 89]
	and	w2, w2, 65535
	strh	w5, [x19, 1218]
	strh	w2, [x19, 1214]
	ldrb	w5, [x20, 88]
	cmp	w5, 1
	bne	.L945
	ubfiz	w2, w2, 1, 15
	lsr	w4, w4, 1
	ubfiz	w3, w3, 1, 15
	strh	w2, [x19, 1214]
	strb	w21, [x20, 89]
	mov	w2, 8
	strh	w4, [x19, 1206]
	strh	w3, [x19, 1212]
	strh	w2, [x19, 1218]
.L945:
	ldrb	w0, [x1, 20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
.L893:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L910:
	cmp	w2, 220
	bne	.L912
	add	x1, x0, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x1, 3430]
	mov	w2, -36
	b	.L993
.L912:
	cmp	w2, 211
	bne	.L911
	add	x1, x0, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x1, 3430]
	mov	w2, 2
	strb	w2, [x1, 3429]
	b	.L911
.L919:
	ldrb	w0, [x21, 2093]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x21, 2093]
	b	.L988
.L950:
	add	x1, x1, 1252
	b	.L928
.L926:
	sub	w3, w0, #17
	and	w3, w3, 255
	cmp	w3, 2
	bhi	.L932
	adrp	x2, MicronReadRetrial
	add	x2, x2, :lo12:MicronReadRetrial
	str	x2, [x21, 2192]
	cmp	w0, 19
	beq	.L933
.L994:
	mov	w0, 7
	b	.L991
.L933:
	mov	w0, 15
.L991:
	strb	w0, [x1, #:lo12:g_maxRetryCount]
	b	.L925
.L932:
	sub	w3, w0, #65
	cmp	w0, 33
	and	w3, w3, 255
	ccmp	w3, 1, 0, ne
	bhi	.L934
	adrp	x0, ToshibaReadRetrial
	add	x0, x0, :lo12:ToshibaReadRetrial
	str	x0, [x21, 2192]
	mov	w0, 4
	strb	w0, [x2, #:lo12:g_maxRegNum]
	b	.L994
.L934:
	sub	w3, w0, #34
	sub	w2, w0, #67
	and	w3, w3, 255
	and	w2, w2, 255
	cmp	w3, 1
	ccmp	w2, 1, 0, hi
	bhi	.L935
	adrp	x3, ToshibaReadRetrial
	add	x3, x3, :lo12:ToshibaReadRetrial
	str	x3, [x21, 2192]
	cmp	w0, 35
	mov	w3, 68
	ccmp	w0, w3, 4, ne
	beq	.L936
	mov	w0, 7
.L990:
	strb	w0, [x1, #:lo12:g_maxRetryCount]
	cmp	w2, 1
	bhi	.L938
	mov	w0, 4
.L992:
	strb	w0, [x4, #:lo12:g_maxRegNum]
	b	.L925
.L936:
	mov	w0, 17
	b	.L990
.L938:
	mov	w0, 5
	b	.L992
.L935:
	cmp	w0, 49
	bne	.L939
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x21, 2192]
	b	.L925
.L939:
	cmp	w0, 50
	bne	.L925
	adrp	x0, samsung_read_retrial
	str	wzr, [x21, 2100]
	add	x0, x0, :lo12:samsung_read_retrial
	str	x0, [x21, 2192]
	b	.L925
.L947:
	mov	w0, -2
	b	.L893
.L907:
	strb	w5, [x3, 2248]
	b	.L906
	.size	FlashInit, .-FlashInit
	.align	2
	.global	FlashPageProgMsbFFData
	.type	FlashPageProgMsbFFData, %function
FlashPageProgMsbFFData:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 255
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	and	w19, w2, 65535
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldrb	w2, [x0, 1220]
	cbz	w2, .L996
	ldr	w0, [x0, 2100]
	cbnz	w0, .L995
.L996:
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20, 104]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #5
	and	w3, w0, 255
	cmp	w3, 63
	bhi	.L995
	mov	x2, 16391
	movk	x2, 0x4000, lsl 16
	movk	x2, 0x2000, lsl 32
	movk	x2, 0x8000, lsl 48
	lsr	x0, x2, x3
	tbz	x0, 0, .L995
	mov	w23, w1
	add	x24, x20, 1168
	add	x21, x21, :lo12:.LANCHOR2
	mov	w25, 65535
.L998:
	ldr	x0, [x20, 104]
	ldrh	w0, [x0, 10]
	cmp	w0, w19
	bhi	.L999
.L995:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L999:
	ldrh	w0, [x24, w19, sxtw 1]
	cmp	w0, w25
	bne	.L995
	ldr	x0, [x21, 2216]
	mov	w2, 32768
	mov	w1, 255
	bl	ftl_memset
	ldr	x3, [x21, 2216]
	add	w1, w19, w23
	add	w19, w19, 1
	mov	w0, w22
	and	w19, w19, 65535
	mov	x2, x3
	bl	FlashProgPage
	b	.L998
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w0, 56
	stp	x21, x22, [sp, 32]
	str	w2, [x29, 120]
	adrp	x2, .LANCHOR1+481
	umaddl	x0, w1, w0, x19
	stp	x25, x26, [sp, 64]
	str	x0, [x29, 112]
	and	w22, w1, 255
	adrp	x0, .LANCHOR0
	ldrb	w2, [x2, #:lo12:.LANCHOR1+481]
	add	x26, x0, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x0, x26, 3072
	stp	x27, x28, [sp, 80]
	str	x0, [x29, 96]
	str	w2, [x29, 124]
.L1005:
	ldr	x0, [x29, 112]
	cmp	x19, x0
	bne	.L1025
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L1025:
	ldr	w1, [x29, 120]
	mov	w4, w22
	add	x3, x29, 136
	add	x2, x29, 140
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldrb	w1, [x26, 3216]
	ldr	w0, [x29, 136]
	cmp	w1, w0
	bhi	.L1006
	mov	w0, -1
	str	w0, [x19]
.L1007:
	sub	w22, w22, #1
	add	x19, x19, 56
	and	w22, w22, 255
	b	.L1005
.L1006:
	ldr	x1, [x29, 96]
	adrp	x21, .LANCHOR2
	mov	w25, 0
	add	x23, x21, :lo12:.LANCHOR2
	mov	w28, 256
	add	x0, x1, w0, uxtw
	ldrb	w20, [x0, 148]
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashCs
.L1008:
	ldr	w1, [x29, 140]
	mov	w0, w20
	bl	FlashReadCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldrb	w2, [x29, 124]
	mov	w1, 0
	ldp	x3, x4, [x19, 8]
	mov	w0, w20
	bl	NandcXferData
	mov	w24, w0
	ldrb	w0, [x23, 2248]
	cbz	w0, .L1009
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w24, w24, w28, ls
.L1009:
	cmp	w25, 9
	ccmn	w24, #1, 0, ls
	bne	.L1010
	add	w25, w25, 1
	b	.L1008
.L1010:
	cmp	w25, 0
	adrp	x0, .LANCHOR0
	mov	w23, 256
	csel	w24, w24, w23, eq
	add	x28, x0, :lo12:.LANCHOR0
	mov	w25, 0
	add	x0, x21, :lo12:.LANCHOR2
	str	x0, [x29, 104]
.L1012:
	ldr	w0, [x28, 92]
	ldr	w1, [x29, 140]
	add	w1, w1, w0
	mov	w0, w20
	bl	FlashReadCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	x0, [x19, 8]
	mov	w1, 0
	ldrb	w2, [x29, 124]
	cmp	x0, 0
	add	x3, x0, 2048
	ldr	x0, [x19, 16]
	csel	x3, x3, xzr, ne
	cmp	x0, 0
	add	x4, x0, 8
	csel	x4, x4, xzr, ne
	mov	w0, w20
	bl	NandcXferData
	mov	w27, w0
	ldr	x0, [x29, 104]
	ldrb	w0, [x0, 2248]
	cbz	w0, .L1015
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w27, w27, w23, ls
.L1015:
	cmp	w25, 9
	ccmn	w27, #1, 0, ls
	bne	.L1016
	add	w25, w25, 1
	b	.L1012
.L1016:
	cmp	w25, 0
	mov	w2, 256
	mov	w0, w20
	csel	w27, w27, w2, eq
	bl	NandcFlashDeCs
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w24, w27
	csel	w5, w24, w27, cs
	ldrb	w0, [x0, 1221]
	add	w0, w0, w0, lsl 1
	cmp	w5, w0, lsr 2
	bls	.L1018
	cmn	w5, #1
	csel	w5, w5, w2, eq
.L1018:
	cmp	w5, 256
	ldr	x0, [x19, 16]
	ccmn	w5, #1, 4, ne
	csel	w5, w5, wzr, eq
	str	w5, [x19]
	cbz	x0, .L1021
	ldr	w1, [x0, 12]
	cmn	w1, #1
	bne	.L1021
	ldr	w1, [x0, 8]
	cmn	w1, #1
	bne	.L1021
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1021
	str	w1, [x19]
.L1021:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1007
	add	x21, x21, :lo12:.LANCHOR2
	ldr	w1, [x19, 4]
	adrp	x0, .LC16
	add	x0, x0, :lo12:.LC16
	ldrb	w2, [x21, 1221]
	bl	printk
	ldr	x1, [x19, 8]
	cbz	x1, .L1023
	adrp	x0, .LC17
	mov	w3, 8
	mov	w2, 4
	add	x0, x0, :lo12:.LC17
	bl	rknand_print_hex
.L1023:
	ldr	x1, [x19, 16]
	cbz	x1, .L1007
	mov	w3, 4
	adrp	x0, .LC18
	mov	w2, w3
	add	x0, x0, :lo12:.LC18
	bl	rknand_print_hex
	b	.L1007
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR0
	add	x3, x25, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x27, x28, [sp, 80]
	stp	w2, w1, [x29, 120]
	ldrb	w4, [x3, 88]
	cbnz	w4, .L1054
	mov	x26, x0
	adrp	x0, .LANCHOR1+481
	mov	w24, 0
	mov	w23, 0
	ldrb	w0, [x0, #:lo12:.LANCHOR1+481]
	str	w0, [x29, 136]
	ldrb	w0, [x3, 96]
	str	w0, [x29, 108]
	adrp	x0, .LANCHOR2
	add	x20, x0, :lo12:.LANCHOR2
	add	x0, x20, 1236
	str	x0, [x29, 112]
.L1055:
	ldr	w0, [x29, 124]
	cmp	w23, w0
	bcc	.L1089
	mov	w0, 0
	b	.L1053
.L1054:
	bl	FlashReadSlc2KPages
.L1053:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1089:
	mov	w27, 56
	ldr	w1, [x29, 120]
	add	x2, x29, 156
	add	x3, x29, 152
	umull	x0, w23, w27
	add	x27, x25, :lo12:.LANCHOR0
	str	x0, [x29, 128]
	add	x22, x26, x0
	ldr	w0, [x29, 124]
	sub	w4, w0, w23
	mov	x0, x22
	ldr	w28, [x22, 4]
	bl	LogAddr2PhyAddr
	ldrb	w2, [x27, 3216]
	mov	w21, w0
	ldr	w0, [x29, 152]
	cmp	w2, w0
	bhi	.L1057
	ldr	x1, [x29, 128]
	mov	w0, -1
	str	w0, [x26, x1]
.L1058:
	add	w23, w23, 1
	b	.L1055
.L1057:
	add	x0, x27, w0, uxtw
	ldrb	w19, [x0, 3220]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w0, [x0, 2249]
	cmp	w0, 0
	mov	w0, w19
	csel	w21, w21, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x0, [x27, 104]
	ldrb	w1, [x0, 19]
	sub	w0, w1, #1
	and	w0, w0, 255
	cmp	w0, 7
	bhi	.L1060
	add	x0, x20, 1232
	sxtw	x2, w19
	add	x0, x0, x2
	sub	w1, w1, #7
	and	w1, w1, 255
	cmp	w1, 1
	ldrb	w3, [x0, 12]
	bhi	.L1061
	ldrb	w3, [x0, 20]
.L1061:
	add	x2, x20, x2
	ldrb	w0, [x2, 2088]
	cmp	w0, w3
	beq	.L1060
	ldrb	w1, [x20, 1233]
	mov	w0, w19
	ldr	x2, [x29, 112]
	bl	HynixSetRRPara
.L1060:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29, 120]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29, 140]
	cbz	w0, .L1062
	ldrb	w0, [x20, 1220]
	cbz	w0, .L1062
	mov	w0, w19
	bl	flash_enter_slc_mode
.L1063:
	add	x28, x25, :lo12:.LANCHOR0
.L1069:
	ldr	w1, [x29, 156]
	cmn	w1, #1
	bne	.L1064
	cmp	w19, 255
	beq	.L1091
.L1064:
	cbz	w21, .L1066
	ldr	w2, [x28, 92]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
.L1067:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w21, .L1065
	ldr	w1, [x29, 156]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
.L1065:
	ldrb	w2, [x29, 136]
	mov	w1, 0
	ldp	x3, x4, [x22, 8]
	mov	w0, w19
	bl	NandcXferData
	mov	w27, w0
	ldrb	w0, [x28, 96]
	cbz	w0, .L1068
	cmn	w27, #1
	bne	.L1068
	strb	wzr, [x28, 96]
	mov	w21, 0
	b	.L1069
.L1062:
	mov	w0, w19
	bl	flash_exit_slc_mode
	b	.L1063
.L1066:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1067
.L1091:
	mov	w21, 0
	b	.L1065
.L1068:
	cbz	w21, .L1070
	add	x0, x25, :lo12:.LANCHOR0
	ldr	w1, [x29, 156]
	ldr	w0, [x0, 92]
	add	w1, w1, w0
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	add	w0, w23, 1
	mov	w1, 56
	ldrb	w2, [x29, 136]
	nop // between mem op and mult-accumulate
	umaddl	x0, w0, w1, x26
	mov	w1, 0
	ldp	x3, x4, [x0, 8]
	mov	w0, w19
	bl	NandcXferData
	cmn	w0, #1
	mov	w24, w0
	csel	w21, w21, wzr, ne
.L1070:
	mov	w0, w19
	bl	NandcFlashDeCs
	add	x0, x25, :lo12:.LANCHOR0
	ldrb	w1, [x29, 108]
	cmn	w27, #1
	strb	w1, [x0, 96]
	bne	.L1071
	ldrb	w0, [x20, 2104]
	cbnz	w0, .L1072
.L1076:
	ldr	x4, [x20, 2192]
	cbnz	x4, .L1073
	ldr	w1, [x29, 156]
	mov	w0, w19
	ldp	x2, x3, [x22, 8]
	bl	FlashReadRawPage
	b	.L1128
.L1072:
	ldr	x0, [x20, 1152]
	mov	w4, 1
	ldr	w1, [x29, 156]
	ldp	x2, x3, [x22, 8]
	ldr	w21, [x0, 304]
	mov	w0, w19
	bl	FlashDdrTunningRead
	mov	w27, w0
	cmn	w0, #1
	beq	.L1075
	ldrb	w0, [x20, 1221]
	cmp	w27, w0, lsr 1
	bls	.L1092
.L1075:
	lsr	w0, w21, 8
	bl	NandcSetDdrPara
	cmn	w27, #1
	beq	.L1076
.L1092:
	mov	w21, 0
.L1071:
	ldrb	w0, [x20, 1221]
	add	w0, w0, w0, lsl 1
	cmp	w27, w0, lsr 2
	bls	.L1077
	ldr	x0, [x20, 2192]
	cmp	x0, 0
	mov	w0, 256
	csel	w27, w27, w0, ne
.L1077:
	ldr	x0, [x29, 128]
	cmp	w27, 256
	ccmn	w27, #1, 4, ne
	csel	w3, w27, wzr, eq
	cmn	w3, #1
	str	w3, [x26, x0]
	bne	.L1084
	ldr	w1, [x22, 4]
	adrp	x0, .LC16
	ldrb	w2, [x20, 1221]
	add	x0, x0, :lo12:.LC16
	bl	printk
	ldr	x1, [x22, 16]
	cbz	x1, .L1084
	mov	w3, 4
	adrp	x0, .LC18
	mov	w2, w3
	add	x0, x0, :lo12:.LC18
	bl	rknand_print_hex
.L1084:
	cbz	w21, .L1086
	ldrb	w0, [x20, 1221]
	add	w0, w0, w0, lsl 1
	cmp	w24, w0, lsr 2
	bls	.L1087
	ldr	x0, [x20, 2192]
	cmp	x0, 0
	mov	w0, 256
	csel	w24, w24, w0, ne
.L1087:
	add	w0, w23, 1
	mov	w1, 56
	cmp	w24, 256
	ccmn	w24, #1, 4, ne
	umull	x0, w0, w1
	csel	w1, w24, wzr, eq
	str	w1, [x26, x0]
.L1086:
	ldr	w0, [x29, 140]
	add	w23, w23, w21
	cbz	w0, .L1058
	ldrb	w0, [x20, 1220]
	cbz	w0, .L1058
	mov	w0, w19
	bl	flash_exit_slc_mode
	b	.L1058
.L1073:
	ldr	w1, [x29, 156]
	mov	w0, w19
	ldp	x2, x3, [x22, 8]
	mov	w21, 0
	blr	x4
	mov	w27, w0
	cmn	w0, #1
	bne	.L1077
	add	x0, x25, :lo12:.LANCHOR0
	ldr	x0, [x0, 104]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 7
	bhi	.L1078
	ldrb	w1, [x20, 1233]
	mov	w3, 0
	ldr	x2, [x29, 112]
	mov	w0, w19
	bl	HynixSetRRPara
.L1078:
	ldp	x2, x3, [x22, 8]
	mov	w0, w19
	ldr	w1, [x29, 156]
	bl	FlashReadRawPage
	mov	w27, w0
	ldrb	w2, [x20, 1221]
	mov	w3, w0
	ldr	w1, [x22, 4]
	adrp	x0, .LC19
	add	x0, x0, :lo12:.LC19
	bl	printk
	cmn	w27, #1
	bne	.L1095
	ldrb	w0, [x20, 1220]
	cbz	w0, .L1095
	ldr	w0, [x29, 140]
	cbz	w0, .L1079
	mov	w0, w19
	bl	flash_enter_slc_mode
.L1080:
	ldr	w1, [x29, 156]
	mov	w0, w19
	ldr	x4, [x20, 2192]
	ldp	x2, x3, [x22, 8]
	blr	x4
.L1128:
	mov	w27, w0
.L1095:
	mov	w21, 0
	b	.L1077
.L1079:
	mov	w0, w19
	bl	flash_exit_slc_mode
	b	.L1080
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FlashLoadFactorBbt
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	stp	x29, x30, [sp, -176]!
	mov	w2, 16
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR2
	add	x19, x23, :lo12:.LANCHOR2
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	mov	w26, -1
	stp	x21, x22, [sp, 32]
	mov	w27, 0
	ldrh	w0, [x19, 1204]
	ldrh	w21, [x19, 1206]
	mul	w21, w21, w0
	add	x0, x19, 2256
	bl	ftl_memset
	and	w21, w21, 65535
	ldr	x0, [x19, 2224]
	add	w25, w21, w26
	stp	xzr, x0, [x29, 128]
	and	w25, w25, 65535
	sub	w0, w21, #12
	mov	w19, 0
	str	w0, [x29, 108]
.L1130:
	adrp	x22, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 3216]
	cmp	w0, w19
	bhi	.L1136
	mov	w0, w26
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1136:
	mul	w28, w21, w19
	mov	w20, w25
	add	x24, x23, :lo12:.LANCHOR2
	mov	w3, 61664
.L1131:
	ldr	w0, [x29, 108]
	cmp	w20, w0
	ble	.L1133
	add	w0, w20, w28
	mov	w2, 1
	lsl	w0, w0, 10
	str	w3, [x29, 104]
	str	w0, [x29, 124]
	mov	w1, w2
	add	x0, x29, 120
	bl	FlashReadPages
	ldr	w0, [x29, 120]
	ldr	w3, [x29, 104]
	cmn	w0, #1
	beq	.L1132
	ldr	x0, [x24, 2224]
	ldrh	w0, [x0]
	cmp	w0, w3
	bne	.L1132
	add	x24, x24, w19, sxtw 1
	add	w27, w27, 1
	and	w27, w27, 65535
	mov	w2, w20
	mov	w1, w19
	adrp	x0, .LC20
	add	x0, x0, :lo12:.LC20
	bl	printk
	strh	w20, [x24, 2256]
.L1133:
	add	x22, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	and	w19, w19, 255
	ldrb	w0, [x22, 3216]
	cmp	w0, w27
	csel	w26, w26, wzr, ne
	b	.L1130
.L1132:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L1131
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, w1
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	stp	x25, x26, [sp, 64]
	mov	w25, w2
	mov	x21, x1
	mov	w26, 61664
	ldrh	w2, [x1, 1204]
	ldrh	w0, [x1, 1206]
	mul	w0, w0, w2
	adrp	x2, .LANCHOR0+64
	and	w0, w0, 65535
	ldr	x2, [x2, #:lo12:.LANCHOR0+64]
	sub	w20, w0, #1
	str	x2, [x29, 96]
	and	w20, w20, 65535
	ldr	x2, [x1, 2224]
	mul	w24, w0, w23
	sub	w19, w0, #16
	str	x2, [x29, 104]
.L1142:
	cmp	w20, w19
	bgt	.L1145
	mov	w0, -1
	b	.L1141
.L1145:
	add	w0, w20, w24
	mov	w2, 1
	lsl	w0, w0, 10
	mov	w1, w2
	str	w0, [x29, 92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29, 88]
	cmn	w0, #1
	beq	.L1143
	ldr	x0, [x21, 2224]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L1143
	cbz	x22, .L1146
	mov	w2, w25
	mov	w1, w23
	mov	x0, x22
	bl	FlashReadFacBbtData.part.6
.L1141:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 144
	ret
.L1143:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L1142
.L1146:
	mov	w0, 0
	b	.L1141
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x21, [sp, 32]
	mov	x21, x0
	adrp	x0, .LANCHOR0+104
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x0, #:lo12:.LANCHOR0+104]
	ldrb	w2, [x0, 13]
	ldrh	w19, [x0, 14]
	ldr	x0, [x20, 2216]
	mul	w19, w19, w2
	and	w19, w19, 65535
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L1152
.L1156:
	mov	w0, 0
.L1153:
	ubfiz	x0, x0, 1, 16
	mov	w1, -1
	strh	w1, [x21, x0]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L1152:
	lsr	w4, w19, 4
	sub	w19, w19, #1
	mov	w1, 0
	mov	w0, 0
	mov	w5, 1
.L1154:
	cmp	w1, w19
	bge	.L1153
	ldr	x6, [x20, 2216]
	ubfx	x3, x1, 5, 11
	lsl	w2, w5, w1
	ldr	w3, [x6, x3, lsl 2]
	tst	w2, w3
	beq	.L1155
	add	w2, w0, 1
	ubfiz	x0, x0, 1, 16
	strh	w1, [x21, x0]
	and	w0, w2, 65535
.L1155:
	cmp	w0, w4
	bcs	.L1156
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L1154
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	FlashProgSlc2KPages
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w24, w2
	adrp	x2, .LANCHOR1+481
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	and	w21, w1, 255
	mov	w23, 56
	ldrb	w26, [x2, #:lo12:.LANCHOR1+481]
	stp	x27, x28, [sp, 80]
	mov	w22, w21
	adrp	x27, .LANCHOR0
	add	x25, x27, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	x28, x0
	umaddl	x23, w1, w23, x0
	mov	x20, x0
	add	x0, x25, 3072
	str	x0, [x29, 104]
.L1163:
	cmp	x20, x23
	bne	.L1169
	adrp	x22, .LANCHOR2
	add	x23, x22, :lo12:.LANCHOR2
.L1170:
	cmp	x28, x20
	bne	.L1177
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1169:
	mov	w1, w24
	mov	w4, w22
	add	x3, x29, 116
	add	x2, x29, 112
	mov	x0, x20
	bl	LogAddr2PhyAddr
	ldrb	w1, [x25, 3216]
	ldr	w0, [x29, 116]
	cmp	w1, w0
	bhi	.L1164
	mov	w0, -1
	str	w0, [x20]
.L1165:
	sub	w22, w22, #1
	add	x20, x20, 56
	and	w22, w22, 255
	b	.L1163
.L1164:
	ldr	x1, [x29, 104]
	add	x0, x1, w0, uxtw
	ldrb	w19, [x0, 148]
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w1, [x29, 112]
	mov	w0, w19
	bl	FlashProgFirstCmd
	ldp	x3, x4, [x20, 8]
	mov	w2, w26
	mov	w1, 1
	mov	w0, w19
	bl	NandcXferData
	ldr	w1, [x29, 112]
	mov	w0, w19
	bl	FlashProgSecondCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29, 112]
	mov	w0, w19
	bl	FlashReadStatus
	sbfx	x0, x0, 0, 1
	str	w0, [x20]
	ldr	w1, [x29, 112]
	ldr	w0, [x25, 92]
	add	w1, w1, w0
	mov	w0, w19
	bl	FlashProgFirstCmd
	ldr	x0, [x20, 8]
	mov	w2, w26
	mov	w1, 1
	cmp	x0, 0
	add	x3, x0, 2048
	ldr	x0, [x20, 16]
	csel	x3, x3, xzr, ne
	cmp	x0, 0
	add	x4, x0, 8
	csel	x4, x4, xzr, ne
	mov	w0, w19
	bl	NandcXferData
	add	x0, x27, :lo12:.LANCHOR0
	ldr	w1, [x29, 112]
	ldr	w0, [x0, 92]
	add	w1, w1, w0
	mov	w0, w19
	bl	FlashProgSecondCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29, 112]
	mov	w0, w19
	bl	FlashReadStatus
	tbz	x0, 0, .L1168
	mov	w0, -1
	str	w0, [x20]
.L1168:
	mov	w0, w19
	bl	NandcFlashDeCs
	b	.L1165
.L1177:
	ldr	w0, [x28]
	cmn	w0, #1
	bne	.L1171
	ldr	w1, [x28, 4]
	adrp	x0, .LC21
	add	x0, x0, :lo12:.LC21
	bl	printk
.L1172:
	sub	w21, w21, #1
	add	x28, x28, 56
	and	w21, w21, 255
	b	.L1170
.L1171:
	add	x19, x22, :lo12:.LANCHOR2
	mov	w4, w21
	add	x3, x29, 116
	add	x2, x29, 112
	mov	w1, w24
	mov	x0, x28
	bl	LogAddr2PhyAddr
	ldr	x0, [x19, 2232]
	mov	x2, 56
	mov	x1, x28
	str	wzr, [x0]
	ldr	x0, [x19, 2240]
	str	wzr, [x0]
	add	x0, x29, 120
	bl	memcpy
	ldr	x0, [x19, 2232]
	mov	w2, w24
	str	x0, [x29, 128]
	mov	w1, 1
	ldr	x0, [x19, 2240]
	str	x0, [x29, 136]
	add	x0, x29, 120
	bl	FlashReadPages
	ldr	w19, [x29, 120]
	cmn	w19, #1
	bne	.L1173
	ldr	w1, [x28, 4]
	adrp	x0, .LC22
	add	x0, x0, :lo12:.LC22
	bl	printk
	str	w19, [x28]
.L1173:
	ldr	w19, [x29, 120]
	cmp	w19, 256
	bne	.L1174
	ldr	w1, [x28, 4]
	adrp	x0, .LC23
	add	x0, x0, :lo12:.LC23
	bl	printk
	str	w19, [x28]
.L1174:
	ldr	x0, [x28, 16]
	cbz	x0, .L1175
	ldr	w2, [x0]
	ldr	x0, [x23, 2240]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1175
	ldr	w1, [x28, 4]
	adrp	x0, .LC24
	add	x0, x0, :lo12:.LC24
	bl	printk
	mov	w0, -1
	str	w0, [x28]
.L1175:
	ldr	x0, [x28, 8]
	cbz	x0, .L1172
	ldr	w2, [x0]
	ldr	x0, [x23, 2232]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1172
	ldr	w1, [x28, 4]
	adrp	x0, .LC25
	add	x0, x0, :lo12:.LC25
	bl	printk
	mov	w0, -1
	str	w0, [x28]
	b	.L1172
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	stp	x29, x30, [sp, -192]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x4, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x5, [x4, 104]
	ldrb	w4, [x4, 88]
	ldrb	w5, [x5, 19]
	stp	w3, w5, [x29, 116]
	cbnz	w4, .L1191
	mov	x19, x0
	adrp	x0, .LANCHOR1+481
	adrp	x28, .LANCHOR2
	mov	w25, w1
	ldrb	w0, [x0, #:lo12:.LANCHOR1+481]
	add	x26, x28, :lo12:.LANCHOR2
	mov	w23, w2
	str	w0, [x29, 124]
	mov	w22, 0
	add	x0, x26, 1236
	str	x0, [x29, 104]
.L1192:
	cmp	w22, w25
	bcc	.L1205
	add	x21, x21, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR2
	add	x24, x21, 3260
	add	x22, x22, :lo12:.LANCHOR2
	mov	x20, 0
	mov	x26, 24
.L1206:
	ldrb	w0, [x21, 3216]
	cmp	w0, w20
	bhi	.L1208
	ldr	w0, [x29, 116]
	cbnz	w0, .L1209
.L1217:
	mov	w0, 0
	b	.L1190
.L1191:
	bl	FlashProgSlc2KPages
.L1190:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 192
	ret
.L1205:
	mov	w13, 56
	add	x2, x29, 128
	mov	w1, w23
	sub	w4, w25, w22
	umull	x13, w22, w13
	add	x3, x29, 132
	add	x27, x19, x13
	mov	x0, x27
	bl	LogAddr2PhyAddr
	mov	w24, w0
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w0, [x29, 132]
	ldrb	w2, [x1, 3216]
	cmp	w2, w0
	bhi	.L1194
	mov	w0, -1
	str	w0, [x19, x13]
.L1195:
	add	w22, w22, 1
	b	.L1192
.L1194:
	add	x2, x28, :lo12:.LANCHOR2
	mov	x3, 24
	ldrb	w2, [x2, 2094]
	cmp	w2, 0
	add	x2, x1, 3260
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x1, x1, x3, x2
	ldr	x1, [x1, 8]
	cbz	x1, .L1197
	bl	FlashWaitCmdDone
.L1197:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29, 132]
	add	x2, x0, 3260
	mov	x0, 24
	madd	x0, x1, x0, x2
	ldr	w2, [x29, 128]
	str	w2, [x0, 4]
	stp	x27, xzr, [x0, 8]
	cbz	w24, .L1198
	add	w2, w22, 1
	mov	w3, 56
	umaddl	x2, w2, w3, x19
	str	x2, [x0, 16]
.L1198:
	add	x4, x21, :lo12:.LANCHOR0
	mov	x2, 24
	add	x0, x4, x1
	mul	x1, x1, x2
	ldrb	w20, [x0, 3220]
	add	x0, x4, 3260
	strb	w20, [x0, x1]
	ldrb	w0, [x4, 3216]
	cmp	w0, 1
	mov	w0, w20
	bne	.L1199
	bl	NandcWaitFlashReady
.L1200:
	ldr	w0, [x29, 120]
	sub	w0, w0, #1
	cmp	w0, 7
	bhi	.L1201
	add	x0, x26, w20, sxtw
	ldrb	w0, [x0, 2088]
	cbz	w0, .L1201
	ldrb	w1, [x26, 1233]
	mov	w3, 0
	ldr	x2, [x29, 104]
	mov	w0, w20
	bl	HynixSetRRPara
.L1201:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1202
	ldrb	w0, [x26, 1220]
	cbz	w0, .L1202
	mov	w0, w20
	bl	flash_enter_slc_mode
.L1203:
	ldr	w1, [x29, 128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldrb	w2, [x29, 124]
	mov	w1, 1
	ldp	x3, x4, [x27, 8]
	mov	w0, w20
	bl	NandcXferData
	cbz	w24, .L1204
	ldr	w1, [x29, 128]
	mov	w0, w20
	add	x27, x21, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29, 132]
	add	x0, x27, 3228
	ldr	w0, [x0, x1, lsl 2]
	ldr	w1, [x29, 128]
	cmp	w0, 0
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w0, [x27, 92]
	ldr	w1, [x29, 128]
	add	w1, w1, w0
	mov	w0, w20
	bl	FlashProgDpSecondCmd
	add	w0, w22, 1
	mov	w1, 56
	ldrb	w2, [x29, 124]
	nop // between mem op and mult-accumulate
	umaddl	x0, w0, w1, x19
	mov	w1, 1
	ldp	x3, x4, [x0, 8]
	mov	w0, w20
	bl	NandcXferData
.L1204:
	ldr	w1, [x29, 128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
	b	.L1195
.L1199:
	bl	NandcFlashCs
	add	x4, x4, 3228
	ldp	w1, w0, [x29, 128]
	ldr	w0, [x4, x0, lsl 2]
	cmp	w0, 0
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
	b	.L1200
.L1202:
	mov	w0, w20
	bl	flash_exit_slc_mode
	b	.L1203
.L1208:
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1207
	ldrb	w0, [x22, 1220]
	cbz	w0, .L1207
	mul	x0, x20, x26
	ldrb	w0, [x0, x24]
	bl	flash_exit_slc_mode
.L1207:
	add	x20, x20, 1
	b	.L1206
.L1209:
	mov	w0, 56
	and	w20, w25, 255
	adrp	x22, .LANCHOR2
	add	x24, x22, :lo12:.LANCHOR2
	umaddl	x25, w25, w0, x19
.L1210:
	cmp	x25, x19
	beq	.L1217
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1211
	ldr	w1, [x19, 4]
	adrp	x0, .LC21
	add	x0, x0, :lo12:.LC21
	bl	printk
.L1212:
	sub	w20, w20, #1
	add	x19, x19, 56
	and	w20, w20, 255
	b	.L1210
.L1211:
	add	x21, x22, :lo12:.LANCHOR2
	mov	w4, w20
	add	x3, x29, 132
	add	x2, x29, 128
	mov	w1, w23
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x21, 2232]
	mov	x2, 56
	mov	x1, x19
	str	wzr, [x0]
	ldr	x0, [x21, 2240]
	str	wzr, [x0]
	add	x0, x29, 136
	bl	memcpy
	ldr	x0, [x21, 2232]
	mov	w2, w23
	str	x0, [x29, 144]
	mov	w1, 1
	ldr	x0, [x21, 2240]
	str	x0, [x29, 152]
	add	x0, x29, 136
	bl	FlashReadPages
	ldr	w21, [x29, 136]
	cmn	w21, #1
	bne	.L1213
	ldr	w1, [x19, 4]
	adrp	x0, .LC22
	add	x0, x0, :lo12:.LC22
	bl	printk
	str	w21, [x19]
.L1213:
	ldr	x0, [x19, 16]
	cbz	x0, .L1214
	ldr	w2, [x0]
	ldr	x0, [x24, 2240]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1214
	ldr	w1, [x19, 4]
	adrp	x0, .LC24
	add	x0, x0, :lo12:.LC24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1214:
	ldr	x0, [x19, 8]
	cbz	x0, .L1212
	ldr	w2, [x0]
	ldr	x0, [x24, 2232]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1212
	ldr	w1, [x19, 4]
	adrp	x0, .LC25
	add	x0, x0, :lo12:.LC25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
	b	.L1212
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FlashTestBlk.part.7, %function
FlashTestBlk.part.7:
	stp	x29, x30, [sp, -160]!
	mov	w2, 32
	mov	w1, 165
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	and	w20, w0, 65535
	lsl	w20, w20, 10
	ldr	x0, [x19, 2216]
	str	x0, [x29, 48]
	add	x0, x29, 96
	str	x0, [x29, 56]
	bl	ftl_memset
	ldr	x0, [x19, 2216]
	mov	w2, 8
	mov	w1, 90
	bl	ftl_memset
	str	w20, [x29, 44]
	mov	w2, 1
	add	x0, x29, 40
	mov	w1, w2
	bl	FlashEraseBlocks
	mov	w3, 1
	add	x0, x29, 40
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldr	w0, [x29, 40]
	mov	w2, 1
	mov	w1, 0
	cmp	w0, 0
	add	x0, x29, 40
	csetm	w19, ne
	bl	FlashEraseBlocks
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 160
	ret
	.size	FlashTestBlk.part.7, .-FlashTestBlk.part.7
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	adrp	x1, .LANCHOR0+72
	and	w0, w0, 65535
	ldr	w1, [x1, #:lo12:.LANCHOR0+72]
	cmp	w0, w1
	bcc	.L1247
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FlashTestBlk.part.7
	ldp	x29, x30, [sp], 16
	ret
.L1247:
	mov	w0, 0
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x0, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	add	x0, x0, 1192
	stp	x25, x26, [sp, 64]
	adrp	x20, .LANCHOR0
	stp	x27, x28, [sp, 80]
	mov	w19, 0
	ldr	x1, [x0, 1032]
	ldrh	w21, [x0, 14]
	ldrh	w0, [x0, 12]
	str	x1, [x29, 136]
	mul	w21, w21, w0
	add	x0, x20, :lo12:.LANCHOR0
	and	w21, w21, 65535
	ldr	x1, [x0, 104]
	ldrh	w2, [x0, 92]
	ldrb	w0, [x0, 88]
	ldrb	w1, [x1, 24]
	cmp	w0, 1
	str	w1, [x29, 112]
	mov	x1, x2
	ubfiz	w2, w2, 1, 15
	csel	w0, w2, w1, eq
	mov	w1, 1
	str	w0, [x29, 168]
	adrp	x0, .LC26
	add	x0, x0, :lo12:.LC26
	bl	printk
	add	x0, x22, :lo12:.LANCHOR2
	mov	w2, 4096
	mov	w1, 0
	ldr	x0, [x0, 2224]
	bl	ftl_memset
	lsr	w0, w21, 4
	str	w0, [x29, 132]
	sub	w0, w21, #1
	and	w0, w0, 65535
	str	w0, [x29, 128]
	adrp	x0, .LC29
	add	x0, x0, :lo12:.LC29
	str	x0, [x29, 104]
.L1254:
	add	x28, x20, :lo12:.LANCHOR0
	ldrb	w0, [x28, 3216]
	cmp	w0, w19
	bhi	.L1281
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 240
	ret
.L1281:
	add	x0, x22, :lo12:.LANCHOR2
	sxtw	x24, w19
	add	x1, x0, x24, lsl 1
	ldrh	w1, [x1, 2256]
	str	w1, [x29, 172]
	cbnz	w1, .L1255
	ldrh	w2, [x0, 1212]
	mov	x26, x28
	ldr	x0, [x28, 64]
	mov	w23, 0
	mov	w25, 0
	lsl	w2, w2, 9
	bl	ftl_memset
	add	x0, x28, x24
	add	x28, x28, 3228
	ldrb	w27, [x0, 3220]
	ldr	w0, [x29, 112]
	and	w0, w0, 1
	stp	w0, wzr, [x29, 160]
.L1256:
	ldrh	w0, [x29, 164]
	str	w0, [x29, 144]
	cmp	w0, w21
	bcc	.L1267
.L1266:
	mov	w1, w19
	mov	w2, w23
	adrp	x0, .LC28
	add	x0, x0, :lo12:.LC28
	bl	printk
	add	x3, x20, :lo12:.LANCHOR0
	ldr	w1, [x29, 132]
	ldrb	w0, [x3, 3216]
	mul	w0, w0, w1
	cmp	w23, w0
	blt	.L1268
	add	x0, x22, :lo12:.LANCHOR2
	mov	w1, 0
	ldrh	w2, [x0, 1212]
	ldr	x0, [x3, 64]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1268:
	cbnz	w19, .L1270
	add	x26, x20, :lo12:.LANCHOR0
	mov	w23, 1
	ldrh	w25, [x26, 72]
.L1271:
	ldrb	w0, [x26, 89]
	cmp	w0, w25
	bhi	.L1273
	ldr	w27, [x29, 128]
	sub	w26, w21, #50
	add	x23, x20, :lo12:.LANCHOR0
	mov	w25, 1
.L1274:
	cmp	w27, w26
	bgt	.L1276
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x0, 72]
	ldrb	w1, [x0, 89]
	sub	w1, w1, w2
	ldr	w2, [x29, 172]
	cmp	w2, w1
	bcc	.L1270
	add	x1, x22, :lo12:.LANCHOR2
	ldr	x0, [x0, 64]
	ldrh	w2, [x1, 1212]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L1270:
	add	x28, x22, :lo12:.LANCHOR2
	mul	w25, w19, w21
	add	x0, x28, 2048
	ldr	w26, [x29, 128]
	adrp	x23, .LC30
	add	x27, x20, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC30
	add	x24, x0, x24, lsl 1
.L1278:
	mov	w1, w19
	mov	w2, w26
	mov	x0, x23
	bl	printk
	ldr	x1, [x27, 64]
.L1279:
	ubfx	x0, x26, 5, 11
	ldr	w0, [x1, x0, lsl 2]
	lsr	w0, w0, w26
	tbnz	x0, 0, .L1280
	ldr	x1, [x29, 136]
	mov	w0, -3872
	strh	w26, [x24, 208]
	mov	w2, 1
	strh	w0, [x1]
	strh	w26, [x1, 2]
	strh	wzr, [x1, 8]
	mov	w1, w2
	ldr	x0, [x27, 64]
	str	x0, [x29, 192]
	ldr	x0, [x28, 2224]
	str	x0, [x29, 200]
	add	w0, w26, w25
	lsl	w0, w0, 10
	str	w0, [x29, 188]
	add	x0, x29, 184
	bl	FlashEraseBlocks
	mov	w3, 1
	add	x0, x29, 184
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldr	w0, [x29, 184]
	cbz	w0, .L1255
	sub	w26, w26, #1
	and	w26, w26, 65535
	b	.L1278
.L1267:
	mov	w0, -1
	strb	w0, [x29, 182]
	strb	w0, [x29, 183]
	ldr	w0, [x29, 160]
	cbz	w0, .L1258
	ldr	w4, [x28, x24, lsl 2]
	mov	w0, w27
	add	x2, x29, 182
	add	w4, w25, w4
	str	w4, [x29, 100]
	mov	w1, w4
	bl	FlashReadSpare
	ldrb	w0, [x26, 88]
	ldr	w4, [x29, 100]
	cmp	w0, 1
	bne	.L1258
	ldr	w1, [x26, 92]
	mov	w0, w27
	add	x2, x29, 183
	add	w1, w4, w1
	bl	FlashReadSpare
	ldrb	w0, [x29, 182]
	ldrb	w1, [x29, 183]
	and	w0, w0, w1
	strb	w0, [x29, 182]
.L1258:
	ldr	x0, [x29, 112]
	tbz	x0, 1, .L1260
	ldr	x0, [x26, 104]
	add	x2, x29, 183
	ldrh	w1, [x0, 10]
	ldr	w0, [x28, x24, lsl 2]
	sub	w1, w1, #1
	add	w0, w25, w0
	add	w1, w1, w0
	mov	w0, w27
	bl	FlashReadSpare
.L1260:
	ldr	x1, [x26, 104]
	ldrb	w0, [x1, 7]
	cmp	w0, 1
	ccmp	w0, 8, 4, ne
	ldrb	w0, [x29, 182]
	beq	.L1261
	ldrb	w1, [x1, 18]
	cmp	w1, 12
	bne	.L1262
.L1261:
	cbz	w0, .L1283
	ldrb	w0, [x29, 183]
	cmp	w0, 0
	cset	w0, eq
.L1263:
	ldr	x1, [x29, 112]
	tbz	x1, 2, .L1264
	ldr	w1, [x28, x24, lsl 2]
	mov	w0, w27
	add	w1, w25, w1
	bl	SandiskProgTestBadBlock
.L1264:
	cbz	w0, .L1265
	ldr	w2, [x29, 164]
	mov	w1, w19
	adrp	x0, .LC27
	add	x0, x0, :lo12:.LC27
	add	w23, w23, 1
	and	w23, w23, 65535
	bl	printk
	ldr	x0, [x29, 144]
	mov	w1, 1
	ldr	x4, [x26, 64]
	ldrb	w2, [x29, 144]
	ubfx	x0, x0, 5, 11
	lsl	x0, x0, 2
	lsl	w2, w1, w2
	ldr	w1, [x4, x0]
	orr	w1, w1, w2
	str	w1, [x4, x0]
	ldr	w1, [x29, 132]
	ldrb	w0, [x26, 3216]
	mul	w0, w0, w1
	cmp	w23, w0
	bgt	.L1266
.L1265:
	ldr	w0, [x29, 164]
	add	w0, w0, 1
	str	w0, [x29, 164]
	ldr	w0, [x29, 168]
	add	w25, w25, w0
	b	.L1256
.L1262:
	cmp	w0, 255
	bne	.L1283
	ldrb	w0, [x29, 183]
	cmp	w0, 255
	cset	w0, ne
	b	.L1263
.L1283:
	mov	w0, 1
	b	.L1263
.L1273:
	mov	w0, w25
	bl	FlashTestBlk
	cbz	w0, .L1272
	ldr	x0, [x29, 104]
	mov	w1, w25
	bl	printk
	ldr	x3, [x26, 64]
	ubfx	x0, x25, 5, 11
	lsl	x0, x0, 2
	lsl	w1, w23, w25
	ldr	w2, [x3, x0]
	orr	w1, w2, w1
	str	w1, [x3, x0]
	ldr	w0, [x29, 172]
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w0, [x29, 172]
.L1272:
	add	w25, w25, 1
	and	w25, w25, 65535
	b	.L1271
.L1276:
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1275
	ldr	x0, [x29, 104]
	mov	w1, w27
	bl	printk
	ldr	x3, [x23, 64]
	ubfx	x0, x27, 5, 11
	lsl	x0, x0, 2
	lsl	w1, w25, w27
	ldr	w2, [x3, x0]
	orr	w1, w2, w1
	str	w1, [x3, x0]
.L1275:
	sub	w27, w27, #1
	and	w27, w27, 65535
	b	.L1274
.L1280:
	sub	w26, w26, #1
	and	w26, w26, 65535
	b	.L1279
.L1255:
	add	w19, w19, 1
	and	w19, w19, 255
	b	.L1254
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	mov	w2, 1
	mov	w1, 0
.L1304:
	cmp	w2, w0
	bls	.L1305
	sub	w0, w1, #1
	ret
.L1305:
	add	w1, w1, 1
	lsl	w2, w2, 1
	and	w1, w1, 65535
	b	.L1304
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
	and	w0, w0, 65535
	mov	w1, 24
	cmp	w0, 24
	csel	w0, w0, w1, cs
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	and	w0, w0, 65535
	ldrh	w2, [x1, 2276]
	ldrh	w3, [x1, 2286]
	str	w0, [x1, 2272]
	mul	w2, w2, w0
	sub	w0, w3, w0
	strh	w0, [x1, 2284]
	ldr	w0, [x1, 2292]
	str	w2, [x1, 2280]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1, 2288]
	ret
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	mov	x7, x0
	stp	x29, x30, [sp, -16]!
	adrp	x5, .LANCHOR2
	add	x1, x5, :lo12:.LANCHOR2
	add	x29, sp, 0
	ldrh	w9, [x0, 8]
	add	x1, x1, 2304
	ldrh	w2, [x0, 10]
	mov	x3, 0
	ldrh	w0, [x0, 12]
	ldrh	w4, [x7, 14]
	strh	w9, [x1, -8]
	strh	w2, [x1, -6]
	strh	w0, [x1, -4]
	strh	w4, [x1, -18]
.L1309:
	strb	w3, [x3, x1]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L1309
	ldrh	w3, [x7, 20]
	ldrb	w1, [x7, 15]
	cmp	w3, w1
	bcs	.L1310
	and	w11, w0, 255
	add	x8, x5, :lo12:.LANCHOR2
	mul	w13, w0, w2
	ubfiz	w12, w11, 1, 7
	add	x8, x8, 2304
	mov	w3, 0
.L1311:
	cmp	w3, w0
	bcs	.L1313
	and	w1, w3, 255
	mov	w6, w3
	mov	w10, 0
	b	.L1314
.L1312:
	add	w14, w13, w6
	strb	w1, [x8, w6, uxtw]
	add	w15, w11, w1
	add	w1, w12, w1
	add	w10, w10, 1
	and	w1, w1, 255
	add	w6, w6, w0
	strb	w15, [x8, x14]
.L1314:
	cmp	w10, w2
	bcc	.L1312
	add	w3, w3, 1
	b	.L1311
.L1313:
	add	x1, x5, :lo12:.LANCHOR2
	ubfiz	w2, w2, 1, 15
	lsr	w4, w4, 1
	strh	w2, [x1, 2298]
	strh	w4, [x1, 2286]
.L1310:
	add	x1, x5, :lo12:.LANCHOR2
	mov	w2, 5
	cmp	w9, 1
	strh	w2, [x1, 2336]
	strh	wzr, [x1, 2338]
	bne	.L1315
	strh	w9, [x1, 2336]
.L1315:
	add	x1, x5, :lo12:.LANCHOR2
	mov	w2, 4352
	strh	w2, [x1, 2340]
	adrp	x2, .LANCHOR0+88
	ldrb	w11, [x2, #:lo12:.LANCHOR0+88]
	cbz	w11, .L1316
	mov	w2, 384
	strh	w2, [x1, 2340]
.L1316:
	add	x4, x5, :lo12:.LANCHOR2
	ldrh	w10, [x7, 16]
	ldrh	w8, [x7, 20]
	ldrh	w9, [x7, 18]
	ldrh	w6, [x4, 2298]
	ldrh	w3, [x4, 2286]
	strh	w10, [x4, 2344]
	strh	w9, [x4, 2346]
	mul	w6, w0, w6
	strh	w8, [x4, 2350]
	mul	w0, w0, w3
	and	w6, w6, 65535
	strh	w0, [x4, 2342]
	strh	w6, [x4, 2276]
	mul	w0, w6, w10
	strh	w0, [x4, 2348]
	mov	w0, w8
	bl	Ftl_log2
	and	w2, w0, 65535
	strh	w0, [x4, 2352]
	ubfiz	w0, w8, 9, 7
	strh	w0, [x4, 2354]
	ubfx	w0, w0, 8, 8
	strh	w0, [x4, 2356]
	cmp	w3, 1024
	ldrh	w0, [x7, 26]
	strh	w0, [x4, 2358]
	mul	w0, w6, w3
	str	w0, [x4, 2292]
	bls	.L1317
	and	w0, w3, 255
	strh	w0, [x4, 2338]
.L1317:
	add	x1, x5, :lo12:.LANCHOR2
	ldrh	w0, [x1, 2338]
	sub	w0, w3, w0
	mul	w0, w0, w6
	mul	w0, w0, w8
	mul	w8, w9, w8
	mul	w0, w0, w10
	asr	w0, w0, 11
	str	w0, [x1, 2360]
	ldrh	w0, [x1, 2340]
	lsl	w0, w0, 3
	sdiv	w0, w0, w8
	and	w0, w0, 65535
	cmp	w0, 4
	bls	.L1318
.L1334:
	strh	w0, [x1, 2364]
	cbz	w11, .L1320
	add	x0, x5, :lo12:.LANCHOR2
	mov	w1, 640
	strh	w1, [x0, 2340]
.L1320:
	add	x1, x5, :lo12:.LANCHOR2
	lsl	w3, w3, 6
	cmp	w6, 1
	ldrh	w0, [x1, 2340]
	asr	w0, w0, w2
	add	w2, w2, 9
	add	w0, w0, 2
	strh	w0, [x1, 2366]
	asr	w3, w3, w2
	strh	w3, [x1, 2368]
	and	w3, w3, 65535
	mul	w0, w6, w3
	add	w3, w3, 8
	str	w0, [x1, 2372]
	ldrh	w0, [x1, 2364]
	udiv	w0, w0, w6
	add	w3, w0, w3
	beq	.L1321
.L1335:
	add	x4, x5, :lo12:.LANCHOR2
	str	w3, [x1, 2272]
	ldrh	w0, [x4, 2272]
	bl	FtlSysBlkNumInit
	ldr	w0, [x4, 2272]
	str	w0, [x4, 2376]
	ldr	w0, [x4, 2288]
	ldrh	w1, [x4, 2344]
	ldrh	w3, [x4, 2350]
	lsl	w0, w0, 2
	ldrh	w2, [x4, 2364]
	ldrb	w7, [x4, 1220]
	str	wzr, [x4, 1224]
	mul	w0, w0, w1
	ldrh	w1, [x4, 2352]
	add	w1, w1, 9
	lsr	w0, w0, w1
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	and	w0, w0, 65535
	strh	w0, [x4, 2380]
	strh	w1, [x4, 2382]
	add	w1, w2, 3
	strh	w1, [x4, 2364]
	ldr	w1, [x4, 2372]
	add	w6, w1, 3
	str	w6, [x4, 2372]
	cbz	w7, .L1323
	add	w1, w1, 5
	add	w2, w2, 4
	strh	w2, [x4, 2364]
.L1336:
	str	w1, [x4, 2372]
.L1324:
	add	x5, x5, :lo12:.LANCHOR2
	ldrh	w1, [x5, 2284]
	strh	wzr, [x5, 2384]
	lsl	w2, w1, 1
	lsr	w1, w1, 3
	add	w2, w2, 48
	add	w1, w1, 4
	add	w0, w2, w0, lsl 2
	add	w0, w0, w1
	cmp	w0, w3, lsl 9
	bcs	.L1325
	mov	w0, 1
	strh	w0, [x5, 2384]
.L1325:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1318:
	mov	w0, 4
	b	.L1334
.L1321:
	add	w3, w3, 4
	b	.L1335
.L1323:
	cmp	w6, 7
	bhi	.L1324
	mov	w1, 8
	b	.L1336
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	stp	x29, x30, [sp, -64]!
	mov	w1, 65535
	mov	w2, 1024
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x3, x0, 2304
	str	x23, [sp, 48]
	add	x4, x0, 2304
	add	x5, x0, 2304
	strh	wzr, [x0, 2386]
	add	x6, x0, 2304
	stp	wzr, wzr, [x3, 84]
	add	x7, x0, 2304
	stp	wzr, wzr, [x3, 92]
	stp	wzr, wzr, [x4, 100]
	stp	wzr, wzr, [x4, 108]
	stp	wzr, wzr, [x5, 116]
	stp	wzr, wzr, [x5, 124]
	stp	wzr, wzr, [x6, 132]
	stp	wzr, wzr, [x6, 140]
	stp	wzr, w1, [x7, 148]
	mov	w1, -1
	stp	wzr, wzr, [x7, 156]
	strh	w1, [x0, 2472]
	strh	w1, [x0, 2474]
	str	wzr, [x0, 2468]
	strh	w1, [x0, 2476]
	strh	w1, [x0, 2478]
	mov	w1, 32
	strh	w1, [x0, 2480]
	mov	w1, 128
	strh	w1, [x0, 2482]
	ldrh	w1, [x0, 2350]
	strh	wzr, [x0, 2484]
	strh	wzr, [x0, 2486]
	strh	wzr, [x0, 2488]
	sdiv	w2, w2, w1
	ldrh	w1, [x0, 2276]
	strh	wzr, [x0, 2490]
	lsl	w1, w1, 2
	str	w2, [x0, 2492]
	cmp	w2, w1
	bls	.L1338
	str	w1, [x0, 2492]
.L1338:
	add	x19, x20, :lo12:.LANCHOR2
	mov	w22, 56
	ldrh	w0, [x19, 2348]
	str	wzr, [x19, 2496]
	lsl	w0, w0, 1
	bl	ftl_malloc
	ldrh	w1, [x19, 2348]
	str	x0, [x19, 2504]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19, 2276]
	str	x0, [x19, 2512]
	mul	w21, w21, w22
	lsl	w23, w21, 3
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 2520]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2528]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 2536]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2544]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2552]
	ldr	w0, [x19, 2492]
	mul	w0, w0, w22
	bl	ftl_malloc
	str	x0, [x19, 2560]
	ldrh	w0, [x19, 2276]
	ldrh	w21, [x19, 2354]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19, 2568]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2576]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2584]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2592]
	ldr	w0, [x19, 2568]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19, 2600]
	ldr	w0, [x19, 2492]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19, 2608]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2616]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2624]
	ldr	w1, [x19, 2568]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19, 2356]
	str	x0, [x19, 2632]
	ldrh	w0, [x19, 2276]
	mul	w21, w21, w0
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2640]
	lsl	w0, w21, 3
	bl	ftl_malloc
	str	x0, [x19, 2648]
	ldrh	w1, [x19, 2356]
	ldr	w0, [x19, 2568]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 2656]
	ldrh	w1, [x19, 2356]
	ldr	w0, [x19, 2492]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 2664]
	ldrh	w0, [x19, 2286]
	ubfiz	w0, w0, 1, 15
	strh	w0, [x19, 2672]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19, 2680]
	ldrh	w0, [x19, 2672]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19, 2672]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w21, [x19, 2286]
	str	x0, [x19, 2688]
	add	x0, x0, 32
	str	x0, [x19, 2696]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2704]
	mov	w0, w21
	bl	ftl_malloc
	ldr	w21, [x19, 2372]
	str	x0, [x19, 2712]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2720]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2728]
	ldrh	w0, [x19, 2286]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	adrp	x1, .LANCHOR0+80
	str	x0, [x1, #:lo12:.LANCHOR0+80]
	ldrh	w0, [x19, 2364]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19, 2736]
	ldrh	w0, [x19, 2364]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19, 2744]
	ldrh	w0, [x19, 2364]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 2752]
	ldrh	w0, [x19, 2366]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w2, [x19, 2366]
	mov	w1, 0
	str	x0, [x19, 2760]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w21, [x19, 2380]
	lsl	w21, w21, 2
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2768]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 2776]
	ldr	w0, [x19, 2372]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 2784]
	ldrh	w0, [x19, 2382]
	lsl	w0, w0, 4
	bl	ftl_malloc
	ldrh	w1, [x19, 2382]
	str	x0, [x19, 2792]
	ldrh	w0, [x19, 2354]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 2800]
	ldrh	w1, [x19, 2286]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 2808]
	ldrh	w0, [x19, 2342]
	ldrh	w1, [x19, 2298]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19, 2816]
	mul	w0, w1, w0
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w3, [x19, 2816]
	add	x4, x19, 2864
	ldrh	w5, [x19, 2298]
	mov	w1, w3
	str	x0, [x19, 2856]
	mov	x0, 1
.L1339:
	cmp	w0, w5
	bcc	.L1340
	mov	w1, 8
	add	x3, x20, :lo12:.LANCHOR2
	sub	w1, w1, w0
	add	x3, x3, 2824
	add	x1, x1, 1
	mov	x2, 0
.L1341:
	add	x2, x2, 1
	cmp	x2, x1
	bne	.L1342
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0, 2720]
	cbnz	x1, .L1343
.L1345:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 136
	adrp	x0, .LC31
	add	x0, x0, :lo12:.LC31
	bl	printk
	mov	w0, -1
.L1337:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1340:
	ldr	x2, [x19, 2856]
	add	w0, w0, 1
	add	x2, x2, w1, uxtw 2
	add	w1, w1, w3
	str	x2, [x4], 8
	b	.L1339
.L1342:
	add	x4, x0, x2
	add	x4, x3, x4, lsl 3
	str	xzr, [x4, 24]
	b	.L1341
.L1343:
	ldr	x1, [x0, 2728]
	cbz	x1, .L1345
	ldr	x1, [x0, 2768]
	cbz	x1, .L1345
	ldr	x1, [x0, 2784]
	cbz	x1, .L1345
	ldr	x1, [x0, 2792]
	cbz	x1, .L1345
	ldr	x1, [x0, 2800]
	cbz	x1, .L1345
	ldr	x1, [x0, 2808]
	cbz	x1, .L1345
	ldr	x1, [x0, 2856]
	cbz	x1, .L1345
	ldr	x0, [x0, 2712]
	cbz	x0, .L1345
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0, 2504]
	cbz	x1, .L1345
	ldr	x1, [x0, 2512]
	cbz	x1, .L1345
	ldr	x1, [x0, 2520]
	cbz	x1, .L1345
	ldr	x1, [x0, 2536]
	cbz	x1, .L1345
	ldr	x1, [x0, 2544]
	cbz	x1, .L1345
	ldr	x1, [x0, 2552]
	cbz	x1, .L1345
	ldr	x1, [x0, 2528]
	cbz	x1, .L1345
	ldr	x1, [x0, 2576]
	cbz	x1, .L1345
	ldr	x1, [x0, 2584]
	cbz	x1, .L1345
	ldr	x0, [x0, 2592]
	cbz	x0, .L1345
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0, 2600]
	cbz	x1, .L1345
	ldr	x1, [x0, 2616]
	cbz	x1, .L1345
	ldr	x1, [x0, 2624]
	cbz	x1, .L1345
	ldr	x1, [x0, 2632]
	cbz	x1, .L1345
	ldr	x1, [x0, 2640]
	cbz	x1, .L1345
	ldr	x1, [x0, 2648]
	cbz	x1, .L1345
	ldr	x1, [x0, 2656]
	cbz	x1, .L1345
	ldr	x1, [x0, 2696]
	cbz	x1, .L1345
	ldr	x1, [x0, 2680]
	cbz	x1, .L1345
	ldr	x0, [x0, 2736]
	cbz	x0, .L1345
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0, 2744]
	cbz	x1, .L1345
	ldr	x1, [x0, 2752]
	cbz	x1, .L1345
	ldr	x0, [x0, 2760]
	cbz	x0, .L1345
	mov	w0, 0
	b	.L1337
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	and	w0, w0, 65535
	ldrh	w2, [x1, 2920]
	cbz	w2, .L1444
	ldrh	w2, [x1, 2364]
	ldr	x3, [x1, 2736]
	mov	x1, 0
.L1442:
	cmp	w2, w1, uxth
	bhi	.L1443
.L1444:
	mov	w0, 0
	ret
.L1443:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L1442
	mov	w0, 1
	ret
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheMetchLpa
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ldr	w4, [x2, 2496]
	cbz	w4, .L1451
	mov	x5, 24
	mov	w6, 56
	ldr	x2, [x2, 2560]
	nop // between mem op and mult-accumulate
	umaddl	x4, w4, w6, x5
	add	x3, x2, 24
	add	x2, x2, x4
.L1448:
	cmp	x3, x2
	bne	.L1450
.L1451:
	mov	w0, 0
	ret
.L1450:
	ldr	w4, [x3]
	cmp	w4, w0
	bcc	.L1449
	cmp	w4, w1
	bls	.L1452
.L1449:
	add	x3, x3, 56
	b	.L1448
.L1452:
	mov	w0, 1
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR2+1224
	ldr	w0, [x0, #:lo12:.LANCHOR2+1224]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	FtlGetCapacity
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	adrp	x0, .LANCHOR2+1224
	ldr	w0, [x0, #:lo12:.LANCHOR2+1224]
	ret
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	adrp	x0, .LANCHOR2+1224
	ldr	w0, [x0, #:lo12:.LANCHOR2+1224]
	ret
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR2+2924
	ldr	w0, [x0, #:lo12:.LANCHOR2+2924]
	ret
	.size	FtlGetLpn, .-FtlGetLpn
	.align	2
	.global	FtlBbmMapBadBlock
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	stp	x29, x30, [sp, -32]!
	and	w1, w0, 65535
	mov	w4, 1
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19, 2342]
	add	x19, x19, 2824
	udiv	w3, w1, w0
	and	w2, w3, 65535
	msub	w3, w3, w0, w1
	add	x0, x19, w2, uxth 3
	and	w3, w3, 65535
	ldr	x0, [x0, 32]
	ubfx	x5, x3, 5, 11
	lsl	x5, x5, 2
	lsl	w4, w4, w3
	ldr	w6, [x0, x5]
	orr	w4, w4, w6
	str	w4, [x0, x5]
	adrp	x0, .LC32
	add	x0, x0, :lo12:.LC32
	bl	printk
	ldrh	w0, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 6]
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	adrp	x2, .LANCHOR2
	add	x3, x2, :lo12:.LANCHOR2
	and	w0, w0, 65535
	ldrh	w1, [x3, 2342]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, w2, uxth 3
	and	w0, w0, 65535
	ldr	x1, [x2, 2856]
	ubfx	x3, x0, 5, 11
	ldr	w1, [x1, x3, lsl 2]
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
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	mov	x20, x1
	str	x21, [sp, 32]
	mov	w1, 0
	mov	x21, x0
	mov	x0, x20
	ldrh	w2, [x19, 2816]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	x1, 0
	mov	w5, 65535
	mov	w4, 1
.L1463:
	ldrh	w0, [x21, x1]
	cmp	w0, w5
	beq	.L1461
	ubfx	x2, x0, 5, 11
	lsl	w0, w4, w0
	lsl	x2, x2, 2
	add	x1, x1, 2
	cmp	x1, 1024
	ldr	w3, [x20, x2]
	orr	w0, w3, w0
	str	w0, [x20, x2]
	ldrh	w0, [x19, 2830]
	add	w0, w0, 1
	strh	w0, [x19, 2830]
	bne	.L1463
.L1461:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR2
	add	x19, x25, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	ldr	w0, [x19, 2928]
	cbnz	w0, .L1469
	ldr	x2, [x19, 2640]
	mov	w1, 0
	ldr	x0, [x19, 2576]
	add	x21, x19, 2856
	str	x2, [x19, 2952]
	mov	w20, 0
	ldrh	w2, [x19, 2354]
	str	x0, [x19, 2944]
	bl	ftl_memset
.L1470:
	ldrh	w0, [x19, 2298]
	add	x1, x19, 2936
	cmp	w20, w0
	blt	.L1471
	ldr	x26, [x1, 16]
	mov	w2, 16
	mov	w1, 255
	adrp	x23, .LC33
	mov	x0, x26
	bl	ftl_memset
	mov	w0, -3887
	add	x25, x25, :lo12:.LANCHOR2
	strh	w0, [x26]
	add	x0, x19, 2824
	add	x23, x23, :lo12:.LC33
	mov	w21, 0
	ldr	w1, [x19, 2832]
	mov	w22, 0
	str	w1, [x26, 4]
	adrp	x24, .LC34
	ldrh	w1, [x19, 2824]
	strh	w1, [x26, 2]
	ldrh	w1, [x19, 2828]
	strh	w1, [x26, 8]
	ldrh	w1, [x19, 2830]
	strh	w1, [x26, 10]
	ldr	w1, [x19, 2272]
	mov	x19, x0
	strh	w1, [x26, 12]
.L1472:
	ldr	x0, [x25, 2576]
	str	x0, [x25, 2944]
	ldr	x0, [x25, 2640]
	str	x0, [x25, 2952]
	ldrh	w1, [x19]
	ldrh	w2, [x19, 2]
	ldrh	w3, [x19, 4]
	ldrh	w4, [x26, 10]
	orr	w0, w2, w1, lsl 10
	str	wzr, [x25, 2936]
	str	w0, [x25, 2940]
	mov	x0, x23
	bl	printk
	ldrh	w0, [x25, 2346]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L1473
	ldr	w0, [x19, 8]
	mov	w2, 1
	ldrh	w1, [x19]
	add	w0, w0, 1
	str	w0, [x19, 8]
	str	w0, [x26, 4]
	strh	w1, [x26, 8]
	ldrh	w0, [x19, 4]
	strh	w1, [x19, 4]
	ldr	x1, [x25, 2544]
	strh	w0, [x19]
	lsl	w0, w0, 10
	str	w0, [x25, 2940]
	strh	wzr, [x19, 2]
	str	w0, [x1, 4]
	mov	w1, w2
	ldr	x0, [x25, 2544]
	bl	FlashEraseBlocks
.L1473:
	add	x20, x25, 2936
	mov	w3, 1
	mov	x0, x20
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	ldr	w0, [x25, 2936]
	cmn	w0, #1
	bne	.L1474
	ldr	w1, [x25, 2940]
	add	w21, w21, 1
	add	x0, x24, :lo12:.LC34
	and	w21, w21, 65535
	bl	printk
	cmp	w21, 3
	bls	.L1472
	ldr	w1, [x25, 2940]
	mov	w2, w21
	adrp	x0, .LC35
	add	x0, x0, :lo12:.LC35
	bl	printk
	mov	w0, 1
	str	w0, [x25, 2928]
.L1469:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1471:
	ldrh	w2, [x19, 2816]
	ldr	x0, [x1, 8]
	ldr	x1, [x21], 8
	mul	w3, w2, w20
	lsl	w2, w2, 2
	add	w20, w20, 1
	add	x0, x0, w3, sxtw 2
	bl	ftl_memcpy
	b	.L1470
.L1477:
	mov	w22, 1
	b	.L1472
.L1474:
	add	w22, w22, 1
	cmp	w22, 1
	ble	.L1477
	cmp	w0, 256
	bne	.L1469
	b	.L1472
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	stp	x29, x30, [sp, -80]!
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	add	x22, x0, 2836
	stp	x25, x26, [sp, 64]
	mov	w21, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x23, x24, [sp, 48]
	add	x23, x0, 2936
	mov	x25, x23
	mov	w26, 61664
	ldr	x1, [x0, 2576]
	ldr	x24, [x0, 2640]
	stp	x1, x24, [x23, 8]
.L1483:
	ldrh	w0, [x19, 2298]
	cmp	w21, w0
	bcc	.L1488
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1488:
	ldrh	w20, [x19, 2342]
	mov	w0, -1
	strh	w0, [x22]
.L1485:
	ldrh	w0, [x19, 2342]
	sub	w20, w20, #1
	and	w20, w20, 65535
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1486
	madd	w0, w0, w21, w20
	mov	w2, 1
	mov	w1, w2
	lsl	w0, w0, 10
	str	w0, [x25, 4]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L1485
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1485
	strh	w20, [x22]
.L1486:
	add	w21, w21, 1
	add	x22, x22, 2
	b	.L1483
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	mov	w1, -1
	add	x29, sp, 0
	mov	w2, 16
	add	x0, x0, 2836
	strh	w1, [x0, -12]
	mov	w1, 255
	strh	wzr, [x0, -6]
	bl	ftl_memset
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlBbtCalcTotleCnt
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	mov	w4, 0
	mov	w5, 0
	ldrh	w6, [x0, 2342]
	ldrh	w0, [x0, 2298]
	mul	w6, w6, w0
	cmp	w4, w6
	blt	.L1505
	mov	w0, w5
	ret
.L1505:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L1498:
	mov	w0, w4
	bl	FtlBbmIsBadBlock
	cbz	w0, .L1497
	add	w5, w5, 1
	and	w5, w5, 65535
.L1497:
	add	w4, w4, 1
	and	w4, w4, 65535
	cmp	w4, w6
	blt	.L1498
	mov	w0, w5
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w24, [x20, 2928]
	cbnz	w24, .L1507
	mov	x21, x19
	add	x26, x20, 2856
	add	x25, x20, 2836
	mov	x19, x20
	add	x20, x20, 2936
	mov	w28, -3872
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L1508:
	ldrh	w0, [x19, 2298]
	cmp	w24, w0
	bcc	.L1514
	add	x20, x21, :lo12:.LANCHOR2
	mov	w19, 0
.L1515:
	ldrh	w0, [x20, 2358]
	cmp	w0, w19
	bhi	.L1516
	add	x20, x20, 2824
	ldrh	w19, [x20, 12]
	mov	w22, 65535
	sub	w19, w19, #1
	and	w19, w19, 65535
.L1517:
	ldrh	w0, [x20, 12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1521
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1518
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L1519
	mov	w0, w19
	bl	FtlBbmMapBadBlock
.L1518:
	sub	w19, w19, #1
	and	w19, w19, 65535
	b	.L1517
.L1514:
	ldr	x0, [x19, 2576]
	mov	w2, 65535
	ldr	x27, [x19, 2640]
	ldrh	w1, [x25]
	stp	x0, x27, [x20, 8]
	cmp	w1, w2
	beq	.L1509
	ldrh	w23, [x19, 2342]
	mov	w2, 1
	madd	w23, w23, w24, w1
	mov	w1, w2
	lsl	w0, w23, 10
	str	w0, [x20, 4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	x1, [x20, 8]
	ldr	x0, [x26]
	ldrh	w2, [x19, 2342]
	add	w2, w2, 7
	lsr	w2, w2, 3
	bl	ftl_memcpy
.L1510:
	mov	w0, w23
	add	w24, w24, 1
	bl	FtlBbmMapBadBlock
	add	x26, x26, 8
	add	x25, x25, 2
	b	.L1508
.L1509:
	mov	w1, w24
	bl	FlashGetBadBlockList
	ldr	x0, [x20, 8]
	ldr	x1, [x26]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x19, 2342]
.L1512:
	sub	w22, w22, #1
	and	w22, w22, 65535
.L1511:
	ldrh	w0, [x19, 2342]
	madd	w0, w24, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1512
	ldr	x0, [x19, 2640]
	mov	w2, 16
	strh	w22, [x25]
	mov	w1, 0
	bl	ftl_memset
	strh	w28, [x27]
	str	wzr, [x27, 4]
	ldrh	w23, [x19, 2342]
	ldrh	w0, [x25]
	strh	w0, [x27, 2]
	ldrh	w2, [x19, 2816]
	ldrh	w0, [x25]
	ldr	x1, [x26]
	lsl	w2, w2, 2
	madd	w23, w23, w24, w0
	lsl	w0, w23, 10
	str	w0, [x20, 4]
	ldr	x0, [x20, 8]
	bl	ftl_memcpy
	mov	w2, 1
	mov	x0, x20
	mov	w1, w2
	bl	FlashEraseBlocks
	mov	w3, 1
	mov	x0, x20
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L1510
	mov	w0, w23
	bl	FtlBbmMapBadBlock
	b	.L1511
.L1516:
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	and	w19, w19, 65535
	b	.L1515
.L1519:
	ldrh	w0, [x20]
	cmp	w0, w22
	bne	.L1520
	strh	w19, [x20]
	b	.L1518
.L1520:
	strh	w19, [x20, 4]
.L1521:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w2, 2
	ldr	x1, [x21, 2544]
	ldrh	w0, [x21, 2824]
	str	wzr, [x21, 2832]
	strh	wzr, [x21, 2826]
	lsl	w0, w0, 10
	str	w0, [x1, 4]
	ldr	x0, [x21, 2544]
	ldrh	w1, [x21, 2828]
	lsl	w1, w1, 10
	str	w1, [x0, 60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x21, 2824]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x21, 2828]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x21, 2826]
	ldr	w0, [x21, 2832]
	ldrh	w1, [x21, 2828]
	add	w0, w0, 1
	str	w0, [x21, 2832]
	ldrh	w0, [x21, 2824]
	strh	w0, [x21, 2828]
	strh	w1, [x21, 2824]
	bl	FtlBbmTblFlush
.L1507:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	V2P_block
	.type	V2P_block, %function
V2P_block:
	adrp	x4, .LANCHOR2
	add	x4, x4, :lo12:.LANCHOR2
	and	w0, w0, 65535
	and	w1, w1, 65535
	ldrh	w2, [x4, 2300]
	ldrh	w4, [x4, 2342]
	udiv	w3, w0, w2
	msub	w0, w3, w2, w0
	madd	w2, w2, w1, w0
	madd	w0, w3, w4, w2
	ret
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.type	P2V_plane, %function
P2V_plane:
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	and	w3, w0, 65535
	ldrh	w1, [x2, 2300]
	ldrh	w2, [x2, 2342]
	udiv	w0, w3, w1
	udiv	w2, w3, w2
	msub	w0, w0, w1, w3
	madd	w0, w1, w2, w0
	ret
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	and	w3, w0, 65535
	ldrh	w2, [x1, 2342]
	ldrh	w1, [x1, 2300]
	udiv	w0, w3, w2
	msub	w0, w0, w2, w3
	and	w0, w0, 65535
	udiv	w0, w0, w1
	ret
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	cmp	w0, w1
	mov	w2, -2147483648
	bls	.L1531
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	ret
.L1531:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	stp	x29, x30, [sp, -192]!
	cmp	w1, 1
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, w1
	stp	x19, x20, [sp, 16]
	adrp	x1, .LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x1, x1, :lo12:.LANCHOR2
	bne	.L1534
	ldrh	w19, [x1, 2346]
.L1535:
	sub	w19, w19, #1
	lsl	w21, w0, 10
	sxth	w19, w19
	add	x1, x29, 128
	orr	w0, w19, w21
	stp	xzr, x1, [x29, 80]
	str	w0, [x29, 76]
	mov	w2, w23
	mov	w1, 1
	add	x0, x29, 72
	bl	FlashReadPages
	ldr	w0, [x29, 128]
	cmn	w0, #1
	bne	.L1536
	mov	w22, 0
	mov	w24, 2
.L1537:
	cmp	w22, w19
	ble	.L1540
.L1536:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 192
	ret
.L1534:
	ldrh	w19, [x1, 2344]
	b	.L1535
.L1540:
	add	w20, w22, w19
	mov	w2, w23
	mov	w1, 1
	sdiv	w20, w20, w24
	sxth	w0, w20
	orr	w0, w0, w21
	str	w0, [x29, 76]
	add	x0, x29, 72
	bl	FlashReadPages
	ldr	w0, [x29, 128]
	cmn	w0, #1
	bne	.L1538
	ldr	w0, [x29, 132]
	cmn	w0, #1
	bne	.L1538
	ldr	w0, [x29, 72]
	cmn	w0, #1
	beq	.L1538
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1537
.L1538:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1537
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	stp	x23, x24, [sp, 48]
	add	x23, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x21, x23, 2936
	mov	w24, 61649
	ldr	x0, [x23, 2576]
	ldr	x22, [x23, 2640]
	stp	x0, x22, [x21, 8]
	bl	FtlBbtMemInit
	ldrh	w20, [x23, 2342]
	sub	w20, w20, #1
	and	w20, w20, 65535
.L1546:
	ldrh	w0, [x23, 2342]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L1549
	lsl	w0, w20, 10
	mov	w2, 1
	str	w0, [x21, 4]
	mov	w1, w2
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1547
	ldr	w0, [x21, 4]
	mov	w2, 1
	mov	w1, w2
	add	w0, w0, 1
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
.L1547:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1548
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1548
	ldr	w1, [x22, 4]
	str	w1, [x23, 2832]
	strh	w20, [x23, 2824]
	ldrh	w1, [x22, 8]
	strh	w1, [x23, 2828]
.L1549:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w1, [x21, 2824]
	cmp	w1, w0
	beq	.L1563
	ldrh	w1, [x21, 2828]
	cmp	w1, w0
	beq	.L1553
	add	x0, x21, 2936
	lsl	w1, w1, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x21, 2936]
	cmn	w0, #1
	beq	.L1553
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1553
	ldr	w1, [x21, 2832]
	ldr	w0, [x22, 4]
	cmp	w0, w1
	bls	.L1553
	str	w0, [x21, 2832]
	ldrh	w1, [x21, 2828]
	ldrh	w0, [x22, 8]
	strh	w1, [x21, 2824]
	strh	w0, [x21, 2828]
.L1553:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w1, 1
	add	x23, x20, 2936
	mov	w24, 61649
	ldrh	w0, [x20, 2824]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20, 2826]
.L1555:
	tbnz	w21, #31, .L1560
	ldrh	w0, [x20, 2824]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w21, w0, lsl 10
	str	w0, [x23, 4]
	ldr	x0, [x20, 2576]
	str	x0, [x23, 8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1556
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1556
.L1560:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22, 10]
	mov	w2, 65535
	strh	w0, [x1, 2830]
	ldrh	w0, [x22, 12]
	cmp	w0, w2
	bne	.L1557
.L1558:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w20, 0
	add	x21, x19, 2856
.L1561:
	ldrh	w0, [x19, 2298]
	cmp	w20, w0
	bcc	.L1562
	mov	w0, 0
.L1545:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1548:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L1546
.L1556:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L1555
.L1557:
	ldr	w2, [x1, 2272]
	cmp	w0, w2
	beq	.L1558
	ldrh	w1, [x1, 2286]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L1558
	cmp	w0, w1
	bcs	.L1558
	bl	FtlSysBlkNumInit
	b	.L1558
.L1562:
	ldrh	w2, [x19, 2816]
	ldr	x0, [x19, 2944]
	mul	w1, w2, w20
	lsl	w2, w2, 2
	add	w20, w20, 1
	add	x1, x0, x1, lsl 2
	ldr	x0, [x21], 8
	bl	ftl_memcpy
	b	.L1561
.L1563:
	mov	w0, -1
	b	.L1545
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	stp	x29, x30, [sp, -16]!
	adrp	x3, .LANCHOR2
	add	x3, x3, :lo12:.LANCHOR2
	mov	w2, 2048
	add	x29, sp, 0
	mov	w1, 0
	strh	w0, [x3, 2992]
	add	x0, x3, 3000
	strh	wzr, [x3, 2994]
	strh	wzr, [x3, 2996]
	strh	wzr, [x3, 2998]
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
	ldrh	w0, [x0, #:lo12:.LANCHOR2+2998]
	cmp	w0, 0
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR2+2998
	ldrh	w0, [x0, #:lo12:.LANCHOR2+2998]
	cmp	w0, 1024
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w21, w0, 65535
	stp	x19, x20, [sp, 16]
	sub	w2, w21, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcc	.L1578
	adrp	x0, .LANCHOR2
	add	x20, x0, :lo12:.LANCHOR2
	mov	x19, x0
	ldrh	w2, [x20, 2998]
	cmp	w2, 1024
	beq	.L1578
	and	w1, w1, 65535
	cbz	w1, .L1580
	ldr	w0, [x20, 2928]
	cbnz	w0, .L1580
	mov	w0, w21
	bl	P2V_block_in_plane
	and	w22, w0, 65535
	ldr	x0, [x20, 2544]
	lsl	w1, w21, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x20, 2544]
	bl	FlashEraseBlocks
	ldr	x2, [x20, 2696]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x20, 2436]
	add	w0, w0, 1
	str	w0, [x20, 2436]
.L1580:
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 2992
	ldrh	w1, [x0, 6]
	add	w1, w1, 1
	strh	w1, [x0, 6]
	ldrh	w1, [x0, 4]
	add	x2, x0, w1, sxtw 1
	add	w1, w1, 1
	and	w1, w1, 1023
	strh	w1, [x0, 4]
	strh	w21, [x2, 8]
.L1578:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x21, x20, :lo12:.LANCHOR2
	stp	x25, x26, [sp, 64]
	and	w25, w0, 65535
	stp	x23, x24, [sp, 48]
	stp	x27, x28, [sp, 80]
	ldr	w0, [x21, 2928]
	cbnz	w0, .L1611
	ldrb	w8, [x21, 1220]
	and	w24, w1, 255
	ldrb	w0, [x21, 2094]
	add	x9, x21, 2304
	mov	w5, 0
	mov	w23, 0
	mov	w19, 0
	mov	w7, 56
	mov	w10, 4
	str	w25, [x21, 2468]
	str	w0, [x29, 132]
.L1590:
	ldrh	w0, [x21, 2276]
	cmp	w0, w5
	bhi	.L1594
	cbz	w23, .L1588
	ldr	x0, [x21, 2544]
	cmp	w8, 0
	cset	w22, ne
	strb	wzr, [x21, 2094]
	mov	w2, w23
	mov	w1, w22
	add	x26, x20, :lo12:.LANCHOR2
	bl	FlashEraseBlocks
	ldrb	w0, [x29, 132]
	strb	w0, [x21, 2094]
	mov	w21, 56
	umull	x21, w23, w21
	mov	x23, 0
.L1597:
	ldr	x0, [x26, 2544]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	cmn	w0, #1
	bne	.L1596
	ldr	w0, [x1, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.L1596:
	add	x23, x23, 56
	cmp	x21, x23
	bne	.L1597
	cbnz	w24, .L1598
	and	w22, w22, 65535
	mov	w0, 1
	mov	w27, 6
	str	w0, [x29, 136]
.L1599:
	add	x26, x20, :lo12:.LANCHOR2
	mov	w23, 0
	add	x0, x26, 2304
	mov	w28, 56
	str	x0, [x29, 120]
.L1607:
	mov	w5, 0
	mov	w21, 0
	mov	w7, 4
.L1600:
	ldrh	w0, [x26, 2276]
	cmp	w0, w5
	bhi	.L1603
	cbz	w21, .L1588
	ldr	x0, [x26, 2544]
	mov	w2, w22
	mov	w1, w21
	strb	wzr, [x26, 2094]
	mov	w3, 1
	bl	FlashProgPages
	umull	x1, w21, w28
	ldrb	w0, [x29, 132]
	strb	w0, [x26, 2094]
	mov	x2, 0
.L1606:
	ldr	x0, [x26, 2544]
	add	x3, x0, x2
	ldr	w0, [x0, x2]
	cbz	w0, .L1605
	ldr	w0, [x3, 4]
	add	w19, w19, 1
	stp	x2, x1, [x29, 104]
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldp	x2, x1, [x29, 104]
.L1605:
	add	x2, x2, 56
	cmp	x1, x2
	bne	.L1606
	add	w23, w23, w27
	ldr	w0, [x29, 136]
	and	w23, w23, 65535
	cmp	w0, w23
	bhi	.L1607
	add	x26, x20, :lo12:.LANCHOR2
	mov	x23, 0
.L1609:
	cbz	w24, .L1608
	ldr	x0, [x26, 2544]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	cbnz	w0, .L1608
	ldr	w0, [x1, 4]
	mov	w1, 1
	str	x2, [x29, 136]
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
	ldr	x2, [x29, 136]
.L1608:
	add	x23, x23, 56
	cmp	x23, x2
	bne	.L1609
	cmp	w25, 63
	ccmp	w24, 0, 0, hi
	beq	.L1588
	add	x20, x20, :lo12:.LANCHOR2
	mov	w2, w21
	mov	w1, w22
	ldr	x0, [x20, 2544]
	bl	FlashEraseBlocks
.L1588:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L1594:
	umull	x0, w5, w7
	ldr	x1, [x21, 2544]
	str	wzr, [x1, x0]
	mov	w1, w25
	ldrb	w0, [x9, w5, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w11, w6
	cbz	w24, .L1591
	bl	IsBlkInVendorPart
	cbnz	w0, .L1592
.L1591:
	mov	w0, w11
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1593
	umull	x2, w23, w7
	ldr	x0, [x21, 2544]
	lsl	w6, w6, 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	ldrh	w0, [x21, 2356]
	ldr	x1, [x21, 2544]
	add	x1, x1, x2
	ldr	x2, [x21, 2648]
	mul	w0, w0, w23
	add	w23, w23, 1
	and	w23, w23, 65535
	sdiv	w0, w0, w10
	add	x0, x2, w0, sxtw 2
	stp	xzr, x0, [x1, 8]
.L1592:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L1590
.L1593:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1592
.L1598:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0, 2346]
	ldrb	w0, [x0, 1220]
	str	w1, [x29, 136]
	cbnz	w0, .L1612
	uxtw	x0, w1
	mov	w22, 1
	lsr	w27, w0, 2
	b	.L1599
.L1612:
	mov	w22, 1
	mov	w27, w22
	b	.L1599
.L1603:
	umull	x0, w5, w28
	ldr	x1, [x26, 2544]
	str	wzr, [x1, x0]
	mov	w1, w25
	ldr	x0, [x29, 120]
	ldrb	w0, [x0, w5, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w8, w6
	cbz	w24, .L1601
	bl	IsBlkInVendorPart
	cbnz	w0, .L1602
.L1601:
	mov	w0, w8
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1602
	umull	x2, w21, w28
	ldr	x0, [x26, 2544]
	add	w6, w23, w6, lsl 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	ldr	x1, [x26, 2544]
	ldr	x0, [x26, 2616]
	add	x1, x1, x2
	ldr	x2, [x26, 2624]
	str	x0, [x1, 8]
	ldrh	w0, [x26, 2356]
	mul	w0, w0, w21
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w0, w0, w7
	add	x0, x2, w0, sxtw 2
	str	x0, [x1, 16]
.L1602:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L1600
.L1611:
	mov	w19, 0
	b	.L1588
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	add	x1, x1, 2992
	ldrh	w2, [x1, 6]
	cbz	w2, .L1633
	adrp	x2, .LANCHOR4+724
	ldrh	w3, [x1, 2]
	mov	w6, 0
	mov	w4, 0
	ldrh	w5, [x2, #:lo12:.LANCHOR4+724]
	ldrh	w2, [x1, 4]
	and	w5, w5, 31
.L1635:
	cmp	w5, w4
	bgt	.L1636
	cbz	w6, .L1633
	add	x0, x0, :lo12:.LANCHOR2
	strh	w3, [x0, 2994]
	strh	w2, [x0, 2996]
.L1633:
	ret
.L1636:
	add	x6, x1, w3, sxtw 1
	add	w4, w4, 1
	add	w3, w3, 1
	and	w4, w4, 65535
	and	w3, w3, 1023
	ldrh	w7, [x6, 8]
	add	x6, x1, w2, sxtw 1
	strh	w7, [x6, 8]
	mov	w6, 1
	add	w2, w2, w6
	and	w2, w2, 1023
	b	.L1635
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	str	x23, [sp, 48]
	mov	x21, x19
	add	x23, x19, 2992
.L1643:
	ldrh	w2, [x19, 2998]
	add	x1, x19, 2992
	cbz	w2, .L1644
	ldrh	w0, [x19, 2994]
	sub	w2, w2, #1
	strh	w2, [x19, 2998]
	add	x3, x1, w0, sxtw 1
	add	w0, w0, 1
	and	w0, w0, 1023
	strh	w0, [x19, 2994]
	ldr	w0, [x19, 2928]
	ldrh	w20, [x3, 8]
	cbnz	w0, .L1645
	mov	w0, w20
	bl	P2V_block_in_plane
	and	w22, w0, 65535
	ldr	x0, [x19, 2544]
	lsl	w1, w20, 10
	str	w1, [x0, 4]
	ldrb	w0, [x19, 1220]
	cbz	w0, .L1646
	ldr	x0, [x19, 2544]
	mov	w2, 1
	mov	w1, 0
	bl	FlashEraseBlocks
.L1646:
	ldr	x0, [x21, 2544]
	mov	w2, 1
	mov	w1, w2
	bl	FlashEraseBlocks
	ldr	x2, [x21, 2696]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x21, 2436]
	add	w0, w0, 1
	str	w0, [x21, 2436]
.L1645:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L1648
	ldrh	w2, [x23, 6]
	mov	w1, w20
	adrp	x0, .LC37
	add	x0, x0, :lo12:.LC37
	bl	printk
	b	.L1643
.L1644:
	adrp	x0, .LC36
	mov	w1, 0
	add	x0, x0, :lo12:.LC36
	bl	printk
.L1647:
	b	.L1647
.L1648:
	mov	w0, w20
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	test_node_in_list
	.type	test_node_in_list, %function
test_node_in_list:
	ldr	x2, [x0]
	adrp	x0, .LANCHOR2+2808
	mov	x4, -6148914691236517206
	and	w1, w1, 65535
	ldr	x3, [x0, #:lo12:.LANCHOR2+2808]
	movk	x4, 0xaaab, lsl 0
	mov	w5, 65535
	sub	x0, x2, x3
	asr	x0, x0, 1
	mul	x0, x0, x4
	mov	w4, 6
	and	w0, w0, 65535
.L1655:
	cmp	w0, w1
	beq	.L1656
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L1657
	umaddl	x2, w0, w4, x3
	b	.L1655
.L1656:
	mov	w0, 1
	ret
.L1657:
	mov	w0, 0
	ret
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x11, .LANCHOR2
	add	x3, x11, :lo12:.LANCHOR2
	and	w0, w0, 65535
	ldrh	w1, [x3, 2284]
	cmp	w1, w0
	bls	.L1674
	mov	w8, 6
	ldr	x13, [x3, 2808]
	mov	w1, -1
	adrp	x2, .LANCHOR4
	umull	x12, w0, w8
	add	x4, x2, :lo12:.LANCHOR4
	mov	x5, x2
	add	x6, x13, x12
	strh	w1, [x6, 2]
	strh	w1, [x13, x12]
	ldr	x1, [x4, 744]
	cbnz	x1, .L1661
	str	x6, [x4, 744]
.L1674:
	mov	w0, 0
	ret
.L1661:
	stp	x29, x30, [sp, -32]!
	ubfiz	x9, x0, 1, 16
	mov	w10, -1
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	w19, 65535
	ldr	x15, [x3, 2712]
	ldrh	w2, [x6, 4]
	ldr	x14, [x3, 2808]
	cmp	w2, 0
	ldr	x16, [x3, 2696]
	ldrh	w7, [x15, x9]
	sub	x4, x1, x14
	asr	x4, x4, 1
	add	x9, x16, x9
	ldrh	w30, [x3, 2284]
	mov	w3, 0
	mul	w7, w7, w2
	mov	x2, -6148914691236517206
	movk	x2, 0xaaab, lsl 0
	csel	w7, w7, w10, ne
	mul	x4, x4, x2
	and	w2, w4, 65535
.L1669:
	add	w3, w3, 1
	and	w3, w3, 65535
	cmp	w3, w30
	bhi	.L1660
	cmp	w0, w2
	beq	.L1660
	ubfiz	x17, x2, 1, 16
	ldrh	w18, [x1, 4]
	cmp	w18, 0
	ldrh	w4, [x15, x17]
	mul	w4, w4, w18
	csel	w4, w4, w10, ne
	cmp	w7, w4
	bne	.L1665
	ldrh	w17, [x16, x17]
	ldrh	w4, [x9]
	cmp	w17, w4
	bcc	.L1667
.L1666:
	strh	w2, [x13, x12]
	ldrh	w2, [x1, 2]
	strh	w2, [x6, 2]
	add	x2, x5, :lo12:.LANCHOR4
	ldr	x3, [x2, 744]
	cmp	x1, x3
	bne	.L1670
	strh	w0, [x1, 2]
	str	x6, [x2, 744]
	b	.L1660
.L1665:
	bcc	.L1666
.L1667:
	ldrh	w4, [x1]
	cmp	w4, w19
	bne	.L1668
	strh	w2, [x6, 2]
	add	x2, x5, :lo12:.LANCHOR4
	strh	w0, [x1]
	str	x6, [x2, 752]
.L1660:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1668:
	umaddl	x1, w4, w8, x14
	mov	w2, w4
	b	.L1669
.L1670:
	ldrh	w2, [x1, 2]
	add	x3, x11, :lo12:.LANCHOR2
	mov	w4, 6
	ldr	x3, [x3, 2808]
	umull	x2, w2, w4
	strh	w0, [x3, x2]
	strh	w0, [x1, 2]
	b	.L1660
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
	ldrh	w1, [x0, 760]
	add	w1, w1, 1
	strh	w1, [x0, 760]
	ldp	x29, x30, [sp], 16
	ret
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.type	insert_free_list, %function
insert_free_list:
	and	w0, w0, 65535
	mov	w7, 65535
	cmp	w0, w7
	beq	.L1679
	adrp	x3, .LANCHOR2
	add	x10, x3, :lo12:.LANCHOR2
	mov	w6, 6
	mov	w1, -1
	adrp	x2, .LANCHOR4
	add	x11, x2, :lo12:.LANCHOR4
	ldr	x9, [x10, 2808]
	umull	x8, w0, w6
	mov	x4, x3
	mov	x3, x2
	add	x5, x9, x8
	strh	w1, [x5, 2]
	strh	w1, [x9, x8]
	ldr	x1, [x11, 768]
	cbnz	x1, .L1680
	str	x5, [x11, 768]
.L1679:
	mov	w0, 0
	ret
.L1680:
	ldr	x12, [x10, 2696]
	ubfiz	x2, x0, 1, 16
	ldr	x11, [x10, 2808]
	mov	x10, -6148914691236517206
	movk	x10, 0xaaab, lsl 0
	ldrh	w13, [x12, x2]
	sub	x2, x1, x11
	asr	x2, x2, 1
	mul	x2, x2, x10
	and	w2, w2, 65535
.L1683:
	ubfiz	x10, x2, 1, 16
	ldrh	w10, [x12, x10]
	cmp	w10, w13
	bcs	.L1681
	ldrh	w10, [x1]
	cmp	w10, w7
	bne	.L1682
	strh	w2, [x5, 2]
	strh	w0, [x1]
	b	.L1679
.L1682:
	umaddl	x1, w10, w6, x11
	mov	w2, w10
	b	.L1683
.L1681:
	ldrh	w6, [x1, 2]
	strh	w6, [x5, 2]
	strh	w2, [x9, x8]
	add	x2, x3, :lo12:.LANCHOR4
	ldr	x3, [x2, 768]
	cmp	x1, x3
	bne	.L1684
	strh	w0, [x1, 2]
	str	x5, [x2, 768]
	b	.L1679
.L1684:
	ldrh	w2, [x1, 2]
	add	x3, x4, :lo12:.LANCHOR2
	mov	w4, 6
	ldr	x3, [x3, 2808]
	umull	x2, w2, w4
	strh	w0, [x3, x2]
	strh	w0, [x1, 2]
	b	.L1679
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
	ldrh	w1, [x0, 776]
	add	w1, w1, 1
	strh	w1, [x0, 776]
	ldp	x29, x30, [sp], 16
	ret
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	and	w1, w1, 65535
	adrp	x6, .LANCHOR2
	add	x6, x6, :lo12:.LANCHOR2
	mov	w4, 6
	ldr	x2, [x0]
	mov	w7, 65535
	umull	x1, w1, w4
	ldr	x3, [x6, 2808]
	add	x5, x3, x1
	cmp	x5, x2
	ldrh	w2, [x3, x1]
	bne	.L1688
	cmp	w2, w7
	bne	.L1689
	str	xzr, [x0]
.L1690:
	mov	w0, -1
	strh	w0, [x3, x1]
	strh	w0, [x5, 2]
	mov	w0, 0
	ret
.L1689:
	umaddl	x2, w2, w4, x3
	str	x2, [x0]
	mov	w0, -1
	strh	w0, [x2, 2]
	b	.L1690
.L1688:
	cmp	w2, w7
	ldrh	w0, [x5, 2]
	bne	.L1691
	cmp	w0, w2
	beq	.L1690
	umull	x2, w0, w4
	mov	w0, -1
	strh	w0, [x3, x2]
	b	.L1690
.L1691:
	umaddl	x2, w2, w4, x3
	strh	w0, [x2, 2]
	ldrh	w2, [x5, 2]
	ldr	x0, [x6, 2808]
	ldrh	w7, [x3, x1]
	umull	x2, w2, w4
	strh	w7, [x0, x2]
	b	.L1690
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
	ldr	x2, [x0]
	cbz	x2, .L1698
	stp	x29, x30, [sp, -16]!
	adrp	x3, .LANCHOR2+2808
	and	w1, w1, 65535
	mov	w4, 65535
	add	x29, sp, 0
	ldr	x8, [x3, #:lo12:.LANCHOR2+2808]
	mov	w5, 6
.L1694:
	cbnz	w1, .L1695
.L1697:
	sub	x8, x2, x8
	mov	x2, -6148914691236517206
	asr	x8, x8, 1
	movk	x2, 0xaaab, lsl 0
	mul	x8, x8, x2
	and	w8, w8, 65535
	mov	w1, w8
	bl	List_remove_node
	mov	w0, w8
	ldp	x29, x30, [sp], 16
	ret
.L1695:
	ldrh	w3, [x2]
	cmp	w3, w4
	beq	.L1697
	sub	w1, w1, #1
	umaddl	x2, w3, w5, x8
	and	w1, w1, 65535
	b	.L1694
.L1698:
	mov	w0, 65535
	ret
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_get_gc_head_node
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	and	w2, w0, 65535
	adrp	x0, .LANCHOR4+744
	ldr	x1, [x0, #:lo12:.LANCHOR4+744]
	cbz	x1, .L1708
	adrp	x0, .LANCHOR2+2808
	mov	w4, 6
	ldr	x3, [x0, #:lo12:.LANCHOR2+2808]
	mov	w0, 65535
.L1705:
	cbz	w2, .L1706
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L1707
	ret
.L1707:
	sub	w2, w2, #1
	umaddl	x1, w1, w4, x3
	and	w2, w2, 65535
	b	.L1705
.L1708:
	mov	w0, 65535
	ret
.L1706:
	sub	x0, x1, x3
	mov	x1, -6148914691236517206
	asr	x0, x0, 1
	movk	x1, 0xaaab, lsl 0
	mul	x0, x0, x1
	and	w0, w0, 65535
	ret
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.type	List_update_data_list, %function
List_update_data_list:
	adrp	x8, .LANCHOR4
	add	x2, x8, :lo12:.LANCHOR4
	and	w9, w0, 65535
	ldrh	w0, [x2, 784]
	cmp	w0, w9
	beq	.L1717
	ldrh	w0, [x2, 832]
	cmp	w0, w9
	beq	.L1717
	ldrh	w0, [x2, 880]
	cmp	w0, w9
	beq	.L1717
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	mov	w1, 6
	ldr	x2, [x2, 744]
	umull	x1, w9, w1
	ldr	x3, [x0, 2808]
	add	x4, x3, x1
	cmp	x4, x2
	beq	.L1717
	ldr	x5, [x0, 2712]
	ubfiz	x0, x9, 1, 16
	ldrh	w2, [x5, x0]
	ldrh	w0, [x4, 4]
	cmp	w0, 0
	mul	w2, w2, w0
	ldrh	w0, [x4, 2]
	mov	w4, 65535
	csinv	w2, w2, wzr, ne
	cmp	w0, w4
	bne	.L1712
	ldrh	w1, [x3, x1]
	cmp	w1, w0
	beq	.L1717
.L1712:
	mov	w1, 6
	mov	x4, -6148914691236517206
	movk	x4, 0xaaab, lsl 0
	umull	x0, w0, w1
	asr	x1, x0, 1
	add	x0, x3, x0
	mul	x1, x1, x4
	ldrh	w3, [x0, 4]
	cmp	w3, 0
	ldrh	w1, [x5, x1, lsl 1]
	mul	w0, w1, w3
	csinv	w0, w0, wzr, ne
	cmp	w2, w0
	bcs	.L1717
	add	x8, x8, :lo12:.LANCHOR4
	stp	x29, x30, [sp, -16]!
	mov	w1, w9
	add	x0, x8, 744
	add	x29, sp, 0
	bl	List_remove_node
	ldrh	w0, [x8, 760]
	sub	w0, w0, #1
	strh	w0, [x8, 760]
	mov	w0, w9
	bl	INSERT_DATA_LIST
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1717:
	mov	w0, 0
	ret
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	ftl_free_no_use_map_blk
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	stp	x29, x30, [sp, -80]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldrh	w2, [x0, 10]
	ldp	x21, x20, [x0, 32]
	ldr	x22, [x0, 16]
	lsl	w2, w2, 1
	mov	x0, x21
	bl	ftl_memset
	mov	w0, 0
.L1721:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L1725
	adrp	x0, .LANCHOR2+2346
	mov	w23, 0
	mov	w20, 0
	ldrh	w1, [x0, #:lo12:.LANCHOR2+2346]
	ldrh	w0, [x19]
	strh	w1, [x21, x0, lsl 1]
	ldrh	w24, [x21]
.L1726:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L1730
	mov	w0, w23
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L1725:
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20, x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L1722:
	ldrh	w3, [x19, 10]
	cmp	w3, w1
	bhi	.L1724
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1721
.L1724:
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22, x3]
	cmp	w4, w2
	bne	.L1723
	cbz	w2, .L1723
	ldrh	w4, [x21, x3]
	add	w4, w4, 1
	strh	w4, [x21, x3]
.L1723:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L1722
.L1730:
	ubfiz	x0, x20, 1, 16
	ldrh	w1, [x21, x0]
	cmp	w24, w1
	bls	.L1727
	add	x25, x22, x0
	ldrh	w0, [x22, x0]
	cbnz	w0, .L1728
.L1729:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1726
.L1727:
	cbnz	w1, .L1729
	add	x25, x22, x0
	ldrh	w0, [x22, x0]
	cbz	w0, .L1729
.L1731:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19, 8]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	b	.L1729
.L1732:
	mov	w24, 0
	b	.L1731
.L1728:
	mov	w23, w20
	cbz	w1, .L1732
	mov	w24, w1
	b	.L1729
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w20, 0
	str	x21, [sp, 32]
	ldrh	w2, [x0, 10]
	ldr	x1, [x0, 16]
.L1742:
	cmp	w20, w2
	beq	.L1746
	mov	x21, x1
	ldrh	w3, [x1], 2
	cbnz	w3, .L1743
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	and	w1, w0, 65535
	strh	w0, [x21]
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L1744
	adrp	x0, .LANCHOR2+2998
	ldrh	w2, [x0, #:lo12:.LANCHOR2+2998]
	adrp	x0, .LC38
	add	x0, x0, :lo12:.LC38
	bl	printk
.L1745:
	b	.L1745
.L1744:
	ldr	w0, [x19, 48]
	strh	wzr, [x19, 2]
	add	w0, w0, 1
	str	w0, [x19, 48]
	ldrh	w0, [x19, 8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19, 8]
.L1746:
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1743:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1742
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	mov	w22, 0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR2
	stp	x25, x26, [sp, 64]
	adrp	x24, .LANCHOR0
	stp	x27, x28, [sp, 80]
	mov	w26, w1
	mov	x27, x2
	mov	x21, x23
	add	x20, x23, :lo12:.LANCHOR2
	add	x24, x24, :lo12:.LANCHOR0
.L1750:
	add	x0, x23, :lo12:.LANCHOR2
	ldr	w1, [x0, 2420]
	add	w1, w1, 1
	str	w1, [x0, 2420]
	ldrh	w0, [x0, 2346]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L1751
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1752
.L1751:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L1752:
	ldr	w0, [x20, 2928]
	cbnz	w0, .L1765
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19, 16]
	ldrh	w25, [x0, x1, lsl 1]
	mov	w1, 0
	ldrh	w0, [x19, 2]
	str	x27, [x20, 2944]
	orr	w0, w0, w25, lsl 10
	str	w0, [x20, 2940]
	ldr	x0, [x20, 2640]
	str	x0, [x20, 2952]
	bl	ftl_memset
	ldr	x6, [x20, 2952]
	ldr	w0, [x19, 48]
	str	w0, [x6, 4]
	strh	w26, [x6, 8]
	ldrh	w0, [x19, 4]
	strh	w0, [x6]
	strh	w25, [x6, 2]
	ldrb	w0, [x24, 88]
	cbz	w0, .L1754
	ldr	x0, [x20, 2944]
	ldrh	w1, [x20, 2354]
	bl	js_hash
	str	w0, [x6, 12]
.L1754:
	add	x28, x20, 2936
	mov	w3, 1
	mov	x0, x28
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2]
	ldr	w1, [x20, 2936]
	cmn	w1, #1
	bne	.L1755
	ldr	w1, [x20, 2940]
	adrp	x0, .LC39
	add	x0, x0, :lo12:.LC39
	add	w22, w22, 1
	and	w22, w22, 65535
	bl	printk
	ldrh	w0, [x19, 2]
	cmp	w0, 2
	bhi	.L1756
	ldrh	w0, [x20, 2346]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L1756:
	cmp	w22, 3
	bls	.L1750
	add	x21, x21, :lo12:.LANCHOR2
	mov	w2, w22
	adrp	x0, .LC40
	add	x0, x0, :lo12:.LC40
	ldr	w1, [x21, 2940]
	bl	printk
	mov	w0, 1
	str	w0, [x21, 2928]
.L1765:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L1755:
	cbz	w1, .L1758
	strh	w25, [x19, 60]
	cmp	w0, 1
	bne	.L1759
.L1760:
	str	wzr, [x19, 56]
	b	.L1750
.L1759:
	cmp	w1, 256
.L1773:
	beq	.L1760
	ldr	w0, [x19, 56]
	cbnz	w0, .L1760
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x0, [x19, 40]
	ldr	w1, [x21, 2940]
	str	w1, [x0, w26, uxtw 2]
	b	.L1765
.L1758:
	cmp	w0, 1
	b	.L1773
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	adrp	x20, .LANCHOR2
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	ldr	x21, [x0, 16]
	ldr	x25, [x0, 40]
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 10]
	ldrh	w2, [x19, 8]
	sub	w1, w1, #4
	cmp	w2, w1
	blt	.L1775
	ubfiz	x0, x0, 1, 16
	ldrh	w24, [x21, x0]
	cbz	w24, .L1775
	ldr	w1, [x19, 52]
	cbnz	w1, .L1775
	mov	w1, 1
	str	w1, [x19, 52]
	strh	wzr, [x21, x0]
	ldrh	w0, [x19, 8]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0, 2346]
	cmp	w1, w0
	bcc	.L1776
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1776:
	add	x26, x20, :lo12:.LANCHOR2
	mov	w21, 0
	add	x22, x26, 2936
.L1777:
	ldrh	w0, [x19, 6]
	cmp	w0, w21
	bhi	.L1782
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19, 52]
.L1775:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w1, [x19, 2]
	ldrh	w0, [x20, 2346]
	cmp	w1, w0
	bcc	.L1780
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
	b	.L1780
.L1782:
	ubfiz	x0, x21, 2, 16
	add	x27, x25, x0
	ldr	w1, [x25, x0]
	cmp	w24, w1, lsr 10
	bne	.L1778
	ldr	x1, [x26, 2584]
	mov	w2, 1
	ldr	x23, [x26, 2640]
	stp	x1, x23, [x22, 8]
	mov	w1, w2
	ldr	w0, [x25, x0]
	str	w0, [x22, 4]
	mov	x0, x22
	bl	FlashReadPages
	ldr	w0, [x22]
	cmn	w0, #1
	bne	.L1779
.L1781:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x27]
	adrp	x0, .LC41
	add	x0, x0, :lo12:.LC41
	ldrh	w2, [x23, 8]
	ldr	w1, [x20, 2940]
	bl	printk
	mov	w0, 1
	str	w0, [x20, 2928]
.L1780:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1779:
	ldrh	w0, [x23, 8]
	cmp	w0, w21
	bne	.L1781
	ldrh	w1, [x23]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L1781
	ldr	x2, [x22, 8]
	mov	w1, w21
	mov	x0, x19
	bl	FtlMapWritePage
.L1778:
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L1777
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x20, x21, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	ldr	w1, [x20, 2928]
	cbnz	w1, .L1788
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w1, 65535
	cmp	w0, w1
	ldr	x22, [x19, 16]
	bne	.L1789
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	strh	w0, [x19, 8]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	strh	wzr, [x19, 2]
	ldr	w0, [x19, 48]
	strh	wzr, [x19]
	add	w0, w0, 1
	str	w0, [x19, 48]
.L1788:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1789:
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x19, 2]
	ldr	x24, [x19, 40]
	ldrh	w23, [x22, x0]
	ldr	x22, [x20, 2640]
	ldr	w0, [x19, 48]
	orr	w1, w1, w23, lsl 10
	str	w1, [x20, 2940]
	ldr	x1, [x20, 2576]
	str	x1, [x20, 2944]
	str	x22, [x20, 2952]
	mov	w1, 255
	str	w0, [x22, 4]
	mov	w0, -1291
	strh	w0, [x22, 8]
	ldrh	w0, [x19, 4]
	strh	w0, [x22]
	strh	w23, [x22, 2]
	ldrh	w2, [x20, 2346]
	ldr	x0, [x20, 2576]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w1, 0
	mov	w0, 0
.L1790:
	ldrh	w2, [x19, 6]
	cmp	w2, w0
	bhi	.L1792
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0, #:lo12:.LANCHOR0+88]
	cbz	w0, .L1793
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x0, 2354]
	ldr	x0, [x0, 2944]
	bl	js_hash
	str	w0, [x22, 12]
.L1793:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w2, 1
	mov	w3, 0
	mov	w1, w2
	add	x0, x0, 2936
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	x0, x19
	bl	ftl_map_blk_gc
	b	.L1788
.L1792:
	ubfiz	x2, x0, 2, 16
	ldr	w3, [x24, x2]
	cmp	w23, w3, lsr 10
	bne	.L1791
	add	w1, w1, 1
	ldr	x4, [x20, 2576]
	and	w1, w1, 65535
	ubfiz	x3, x1, 3, 16
	str	w0, [x4, x3]
	ldr	w4, [x24, x2]
	ldr	x2, [x20, 2576]
	add	x2, x2, x3
	str	w4, [x2, 4]
.L1791:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1790
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	flush_l2p_region
	.type	flush_l2p_region, %function
flush_l2p_region:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	ubfiz	x20, x0, 4, 16
	ldr	x0, [x19, 2792]
	add	x1, x0, x20
	ldr	x2, [x1, 8]
	ldrh	w1, [x0, x20]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 928
	bl	FtlMapWritePage
	ldr	x0, [x19, 2792]
	add	x0, x0, x20
	ldr	w1, [x0, 4]
	and	w1, w1, 2147483647
	str	w1, [x0, 4]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	mov	x3, 0
	mov	w4, 65535
	ldrh	w2, [x0, 2382]
	ldr	x1, [x0, 2792]
.L1801:
	and	w0, w3, 65535
	cmp	w0, w2
	bcc	.L1803
	add	x4, x1, 4
	mov	w0, w2
	mov	w6, -2147483648
	mov	w3, 0
.L1804:
	cmp	w3, w2
	bne	.L1806
	cmp	w0, w2
	bcc	.L1802
	adrp	x0, .LANCHOR4+992
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0, #:lo12:.LANCHOR4+992]
	mov	w0, w2
.L1807:
	cmp	w3, w2
	beq	.L1802
	ldr	w7, [x1, 4]
	cmp	w4, w7
	bls	.L1808
	ldrh	w6, [x1]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	csel	w0, w0, w3, eq
.L1808:
	add	w3, w3, 1
	add	x1, x1, 16
	and	w3, w3, 65535
	b	.L1807
.L1803:
	add	x3, x3, 1
	add	x5, x1, x3, lsl 4
	ldrh	w5, [x5, -16]
	cmp	w5, w4
	bne	.L1801
.L1802:
	ret
.L1806:
	ldr	w5, [x4]
	tbnz	w5, #31, .L1805
	cmp	w6, w5
	bls	.L1805
	mov	w6, w5
	mov	w0, w3
.L1805:
	add	w3, w3, 1
	add	x4, x4, 16
	and	w3, w3, 65535
	b	.L1804
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x3, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w4, [x3, 2924]
	cmp	w0, w4
	bcs	.L1811
	mov	w25, w0
	ldrh	w0, [x3, 2352]
	mov	x23, x1
	ldr	x8, [x3, 2792]
	add	w27, w0, 7
	ldrh	w1, [x3, 2382]
	mov	x0, 0
	str	w2, [x29, 108]
	lsr	w28, w25, w27
	and	w22, w28, 65535
.L1812:
	and	x20, x0, 65535
	cmp	w20, w1
	bcc	.L1817
	bl	select_l2p_ram_region
	and	x20, x0, 65535
	ubfiz	x26, x20, 4, 16
	mov	w2, 65535
	add	x1, x8, x26
	ldrh	w3, [x8, x26]
	cmp	w3, w2
	beq	.L1818
	ldr	w1, [x1, 4]
	tbz	w1, #31, .L1818
	bl	flush_l2p_region
.L1818:
	add	x19, x21, :lo12:.LANCHOR2
	ubfiz	x0, x22, 2, 16
	ldr	x1, [x19, 2768]
	ldr	w3, [x1, x0]
	cbnz	w3, .L1819
	ldr	x0, [x19, 2792]
	mov	w1, 255
	ldrh	w2, [x19, 2354]
	add	x0, x0, x26
	ldr	x0, [x0, 8]
	bl	ftl_memset
	ldr	x0, [x19, 2792]
	strh	w22, [x0, x26]
	ldr	x0, [x19, 2792]
	add	x26, x0, x26
	str	wzr, [x26, 4]
	b	.L1814
.L1811:
	mov	w0, -1
	cbnz	w2, .L1810
	str	w0, [x1]
.L1810:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L1817:
	add	x0, x0, 1
	add	x2, x8, x0, lsl 4
	ldrh	w2, [x2, -16]
	cmp	w2, w22
	bne	.L1812
.L1814:
	ldr	w1, [x29, 108]
	mov	x0, 1
	lsl	x0, x0, x27
	sub	w0, w0, #1
	and	w0, w0, w25
	and	x0, x0, 65535
	cbnz	w1, .L1815
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x1, [x1, 2792]
	add	x1, x1, x20, lsl 4
	ldr	x1, [x1, 8]
	ldr	w0, [x1, x0, lsl 2]
	str	w0, [x23]
.L1816:
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x0, [x21, 2792]
	add	x20, x0, x20, lsl 4
	ldr	w0, [x20, 4]
	cmn	w0, #1
	beq	.L1823
	add	w0, w0, 1
	str	w0, [x20, 4]
.L1823:
	mov	w0, 0
	b	.L1810
.L1815:
	add	x3, x21, :lo12:.LANCHOR2
	lsl	x1, x20, 4
	ldr	w4, [x23]
	ldr	x2, [x3, 2792]
	add	x2, x2, x1
	ldr	x2, [x2, 8]
	str	w4, [x2, x0, lsl 2]
	ldr	x0, [x3, 2792]
	add	x0, x0, x1
	ldr	w1, [x0, 4]
	orr	w1, w1, -2147483648
	str	w1, [x0, 4]
	adrp	x0, .LANCHOR4+992
	strh	w22, [x0, #:lo12:.LANCHOR4+992]
	b	.L1816
.L1819:
	ldr	x0, [x19, 2792]
	add	x24, x19, 2936
	mov	w2, 1
	str	w3, [x19, 2940]
	add	x0, x0, x26
	str	w3, [x29, 104]
	mov	w1, w2
	ldr	x0, [x0, 8]
	str	x0, [x19, 2944]
	ldr	x0, [x19, 2640]
	str	x0, [x19, 2952]
	mov	x0, x24
	bl	FlashReadPages
	ldr	x0, [x19, 2952]
	ldr	w3, [x29, 104]
	ldrh	w0, [x0, 8]
	cmp	w0, w22
	beq	.L1820
	mov	w2, w3
	and	w1, w28, 65535
	adrp	x0, .LC42
	add	x0, x0, :lo12:.LC42
	bl	printk
	ldr	x1, [x19, 2952]
	mov	w3, 4
	adrp	x0, .LC18
	mov	w2, w3
	add	x0, x0, :lo12:.LC18
	bl	rknand_print_hex
	ldrh	w3, [x19, 2380]
	mov	w2, 4
	ldr	x1, [x19, 2768]
	adrp	x0, .LC43
	add	x0, x0, :lo12:.LC43
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x19, 2928]
.L1821:
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x0, [x1, 2792]
	add	x0, x0, x26
	str	wzr, [x0, 4]
	ldr	x0, [x1, 2792]
	strh	w22, [x0, x26]
	b	.L1814
.L1820:
	ldr	w0, [x19, 2936]
	cmp	w0, 256
	bne	.L1821
	and	w28, w28, 65535
	mov	w2, w3
	mov	w1, w28
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	bl	printk
	ldr	x0, [x19, 2792]
	mov	w1, w28
	add	x0, x0, x26
	ldr	x2, [x0, 8]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 928
	bl	FtlMapWritePage
	b	.L1821
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	mov	w22, w1
	mov	w24, w0
	add	w1, w0, w1
	stp	x25, x26, [sp, 64]
	ldrh	w0, [x19, 2340]
	stp	x27, x28, [sp, 80]
	cmp	w1, w0
	bhi	.L1834
	ldrh	w23, [x19, 2352]
	adrp	x27, .LANCHOR4
	add	x27, x27, :lo12:.LANCHOR4
	mov	x25, x2
	add	x27, x27, 1000
	mov	w26, 0
	lsr	w23, w24, w23
.L1828:
	cbnz	w22, .L1833
.L1826:
	mov	w0, w26
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1833:
	ldrh	w1, [x19, 2350]
	ldr	x0, [x19, 2760]
	udiv	w21, w24, w1
	ldr	w2, [x0, w23, uxtw 2]
	and	w0, w22, 65535
	msub	w21, w21, w1, w24
	sub	w20, w1, w21
	and	w20, w20, 65535
	cmp	w22, w20
	csel	w20, w0, w20, cc
	cbz	w2, .L1830
	cmp	w20, w1
	beq	.L1830
	ldr	x0, [x19, 2592]
	str	w2, [x29, 108]
	mov	w2, 1
	stp	x0, xzr, [x29, 112]
	mov	w1, w2
	add	x0, x29, 104
	bl	FlashReadPages
.L1831:
	ldr	x0, [x19, 2592]
	lsl	w21, w21, 9
	lsl	w28, w20, 9
	asr	w21, w21, 2
	mov	x1, x25
	mov	w2, w28
	add	x0, x0, w21, sxtw 2
	bl	ftl_memcpy
	ldr	x2, [x19, 2592]
	mov	w1, w23
	mov	x0, x27
	sub	w22, w22, w20
	add	w24, w24, w20
	add	x25, x25, w28, sxtw
	add	w23, w23, 1
	bl	FtlMapWritePage
	cmn	w0, #1
	csinv	w26, w26, wzr, ne
	b	.L1828
.L1830:
	ldrh	w2, [x19, 2354]
	mov	w1, 0
	ldr	x0, [x19, 2592]
	bl	ftl_memset
	b	.L1831
.L1834:
	mov	w26, -1
	b	.L1826
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	stp	x21, x22, [sp, 32]
	mov	w24, w0
	mov	w23, w1
	stp	x25, x26, [sp, 64]
	add	w1, w0, w1
	stp	x27, x28, [sp, 80]
	ldrh	w0, [x19, 2340]
	cmp	w1, w0
	bhi	.L1848
	ldrh	w22, [x19, 2352]
	adrp	x25, .LANCHOR4
	add	x25, x25, :lo12:.LANCHOR4
	mov	x27, x2
	add	x25, x25, 1000
	mov	w26, 0
	lsr	w22, w24, w22
.L1841:
	cbnz	w23, .L1847
.L1839:
	mov	w0, w26
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1847:
	ldrh	w20, [x19, 2350]
	ldr	x0, [x19, 2760]
	udiv	w21, w24, w20
	ldr	w3, [x0, w22, uxtw 2]
	and	w0, w23, 65535
	msub	w21, w21, w20, w24
	sub	w20, w20, w21
	and	w20, w20, 65535
	cmp	w23, w20
	csel	w20, w0, w20, cc
	lsl	w28, w20, 9
	cbz	w3, .L1843
	ldr	x0, [x19, 2592]
	mov	w2, 1
	str	w3, [x29, 108]
	mov	w1, w2
	str	w3, [x29, 124]
	stp	x0, xzr, [x29, 128]
	add	x0, x29, 120
	bl	FlashReadPages
	ldr	w0, [x29, 120]
	ldr	w3, [x29, 108]
	cmn	w0, #1
	ldr	w0, [x19, 2936]
	csinv	w26, w26, wzr, ne
	cmp	w0, 256
	bne	.L1845
	mov	w2, w3
	mov	w1, w22
	adrp	x0, .LC45
	add	x0, x0, :lo12:.LC45
	bl	printk
	ldr	x2, [x19, 2592]
	mov	w1, w22
	mov	x0, x25
	bl	FtlMapWritePage
.L1845:
	ldr	x1, [x19, 2592]
	lsl	w21, w21, 9
	asr	w21, w21, 2
	mov	w2, w28
	mov	x0, x27
	add	x1, x1, w21, sxtw 2
	bl	ftl_memcpy
.L1846:
	add	w22, w22, 1
	sub	w23, w23, w20
	add	w24, w24, w20
	add	x27, x27, w28, sxtw
	b	.L1841
.L1843:
	mov	w2, w28
	mov	w1, 0
	mov	x0, x27
	bl	ftl_memset
	b	.L1846
.L1848:
	mov	w26, -1
	b	.L1839
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR4
	add	x3, x1, :lo12:.LANCHOR4
	ldrh	w2, [x3, 1064]
	cmp	w2, 4
	bhi	.L1851
	cbnz	w0, .L1851
	add	w2, w2, 1
	strh	w2, [x3, 1064]
	ret
.L1851:
	add	x0, x1, :lo12:.LANCHOR4
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	mov	w6, 65535
	strh	wzr, [x0, 1064]
	ldrh	w4, [x1, 2284]
	ldr	x5, [x1, 2712]
	mov	x1, 0
	str	wzr, [x0, 1068]
.L1852:
	cmp	w4, w1, uxth
	bhi	.L1854
	ret
.L1854:
	ldrh	w3, [x5, x1, lsl 1]
	cmp	w3, w6
	beq	.L1853
	ldr	w2, [x0, 1068]
	add	w2, w2, w3
	str	w2, [x0, 1068]
.L1853:
	add	x1, x1, 1
	b	.L1852
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	FtlMapBlkWriteDumpData
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	ldr	w1, [x0, 56]
	cbz	w1, .L1863
	stp	x29, x30, [sp, -48]!
	adrp	x2, .LANCHOR2
	add	x1, x2, :lo12:.LANCHOR2
	add	x29, sp, 0
	str	x21, [sp, 32]
	mov	x21, x2
	stp	x19, x20, [sp, 16]
	str	wzr, [x0, 56]
	ldr	w3, [x1, 2928]
	ldrh	w19, [x0, 6]
	ldr	x4, [x0, 40]
	cbnz	w3, .L1856
	mov	x20, x0
	add	x0, x1, 2936
	sub	w19, w19, #1
	ldr	x2, [x1, 2640]
	and	w19, w19, 65535
	ldr	x3, [x1, 2584]
	stp	x3, x2, [x0, 8]
	ubfiz	x2, x19, 2, 16
	ldr	w2, [x4, x2]
	str	w2, [x0, 4]
	cbz	w2, .L1860
	mov	w2, 1
	mov	w1, w2
	bl	FlashReadPages
.L1861:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	ldr	x2, [x0, 2944]
	mov	x0, x20
	bl	FtlMapWritePage
.L1856:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L1860:
	ldrh	w2, [x1, 2354]
	mov	x0, x3
	mov	w1, 255
	bl	ftl_memset
	b	.L1861
.L1863:
	ret
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x19, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	ldr	w0, [x19, 2928]
	cbnz	w0, .L1868
	ldr	x0, [x19, 2576]
	adrp	x25, .LANCHOR4
	str	x0, [x19, 2944]
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x21, [x19, 2640]
	add	x20, x0, 696
	str	x21, [x19, 2952]
	adrp	x23, .LANCHOR0
	ldrh	w2, [x0, 1072]
	strh	w2, [x21, 2]
	mov	w2, -3932
	strh	w2, [x21]
	str	wzr, [x21, 12]
	ldr	w2, [x0, 1080]
	stp	w2, wzr, [x21, 4]
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	str	w2, [x0, 696]
	mov	w2, 99
	ldrb	w3, [x0, 790]
	movk	w2, 0x5000, lsl 16
	str	w2, [x20, 4]
	ldrh	w2, [x0, 784]
	strh	w2, [x20, 14]
	ldrh	w2, [x0, 786]
	ldrh	w1, [x0, 1078]
	strh	w1, [x20, 8]
	orr	w2, w3, w2, lsl 6
	ldrh	w1, [x19, 2298]
	strb	w1, [x20, 10]
	strh	w2, [x20, 16]
	ldrb	w1, [x0, 792]
	ldrh	w2, [x0, 832]
	ldrb	w3, [x0, 838]
	strb	w1, [x20, 11]
	strh	w2, [x20, 18]
	ldrb	w1, [x0, 840]
	ldrh	w2, [x0, 834]
	strb	w1, [x20, 12]
	add	x1, x0, 880
	ldrh	w0, [x0, 880]
	orr	w2, w3, w2, lsl 6
	strh	w2, [x20, 20]
	strh	w0, [x20, 22]
	ldrb	w2, [x1, 6]
	ldrh	w0, [x1, 2]
	orr	w0, w2, w0, lsl 6
	strh	w0, [x20, 24]
	ldrb	w0, [x1, 8]
	mov	w1, 255
	strb	w0, [x20, 13]
	ldr	w0, [x19, 2428]
	str	w0, [x20, 32]
	ldr	w0, [x19, 2396]
	str	w0, [x20, 40]
	ldr	w0, [x19, 2400]
	str	w0, [x20, 36]
	ldrh	w0, [x19, 2472]
	strh	w0, [x20, 44]
	ldrh	w0, [x19, 2474]
	strh	w0, [x20, 46]
	ldrh	w2, [x19, 2354]
	ldr	x0, [x19, 2944]
	bl	ftl_memset
	ldr	x0, [x19, 2944]
	mov	x1, x20
	mov	w2, 48
	mov	x20, x25
	bl	ftl_memcpy
	ldr	x1, [x19, 2712]
	ldrh	w2, [x19, 2284]
	ldr	x0, [x19, 2944]
	lsl	w2, w2, 1
	add	x0, x0, 48
	bl	ftl_memcpy
	add	x1, x23, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2284]
	ldr	x3, [x19, 2944]
	ldr	x1, [x1, 80]
	lsr	w2, w0, 3
	ubfiz	x0, x0, 1, 16
	add	w2, w2, 4
	add	x0, x0, 51
	and	x0, x0, -4
	add	x0, x3, x0
	bl	ftl_memcpy
	ldrh	w0, [x19, 2384]
	cbz	w0, .L1869
	ldrh	w0, [x19, 2284]
	ldr	x3, [x19, 2944]
	ldrh	w2, [x19, 2380]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x19, 2768]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x3, x0
	bl	ftl_memcpy
.L1869:
	add	x19, x20, :lo12:.LANCHOR4
	add	x26, x22, :lo12:.LANCHOR2
	add	x19, x19, 1072
	add	x23, x23, :lo12:.LANCHOR0
	mov	w24, 0
	mov	w25, 65535
	mov	w0, 0
	bl	FtlUpdateVaildLpn
.L1870:
	ldrh	w2, [x19, 2]
	ldrh	w1, [x19]
	ldr	x0, [x26, 2576]
	str	x0, [x26, 2944]
	ldr	x0, [x26, 2640]
	str	x0, [x26, 2952]
	orr	w0, w2, w1, lsl 10
	str	w0, [x26, 2940]
	ldrh	w0, [x26, 2346]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L1871
	ldrh	w25, [x19, 4]
	strh	wzr, [x19, 2]
	strh	w1, [x19, 4]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x26, 2396]
	str	w1, [x19, 8]
	add	w2, w1, 1
	str	w2, [x26, 2396]
	ubfiz	w2, w0, 10, 16
	str	w2, [x26, 2940]
	strh	w0, [x19]
	strh	w0, [x21, 2]
	str	w1, [x21, 4]
.L1871:
	ldrb	w0, [x23, 88]
	cbz	w0, .L1872
	ldrh	w1, [x26, 2354]
	ldr	x0, [x26, 2576]
	bl	js_hash
	str	w0, [x21, 12]
.L1872:
	mov	w3, 1
	add	x0, x26, 2936
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	ldr	w1, [x26, 2936]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2]
	cmn	w1, #1
	bne	.L1873
	cmp	w0, 1
	bne	.L1874
	ldrh	w0, [x26, 2346]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L1874:
	add	w24, w24, 1
	and	w24, w24, 65535
	cmp	w24, 3
	bls	.L1870
	add	x22, x22, :lo12:.LANCHOR2
	mov	w2, w24
	adrp	x0, .LC46
	add	x0, x0, :lo12:.LC46
	ldr	w1, [x22, 2940]
	bl	printk
	mov	w0, 1
	str	w0, [x22, 2928]
.L1868:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1873:
	cmp	w0, 1
	beq	.L1870
	cmp	w1, 256
	beq	.L1870
	mov	w0, 65535
	cmp	w25, w0
	beq	.L1868
	mov	w1, 1
	mov	w0, w25
	bl	FtlFreeSysBlkQueueIn
	b	.L1868
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x19, x21, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR4
	add	x20, x22, :lo12:.LANCHOR4
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	mov	w25, 56
	ldr	x0, [x19, 2784]
	ldr	w2, [x19, 2372]
	stp	x27, x28, [sp, 80]
	strh	wzr, [x20, 1088]
	lsl	w2, w2, 2
	strh	wzr, [x19, 2920]
	bl	ftl_memset
	ldr	x0, [x19, 2720]
	mov	w1, 0
	ldr	w2, [x19, 2372]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 2752]
	mov	w1, 0
	ldrh	w2, [x19, 2364]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x19, 2736]
	mov	w1, 0
	ldrh	w2, [x19, 2364]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w2, 16
	mov	w1, 255
	add	x0, x20, 1072
	bl	ftl_memset
	ldrh	w23, [x19, 2284]
	add	x0, x19, 2304
	str	x0, [x29, 112]
.L1891:
	ldrh	w0, [x19, 2286]
	cmp	w0, w23
	bls	.L1930
	ldrh	w8, [x19, 2276]
	mov	x5, 0
	ldrh	w7, [x19, 2356]
	mov	w20, 0
	mov	w6, 4
	b	.L1931
.L1893:
	ldr	x0, [x29, 112]
	mov	w1, w23
	ldrb	w0, [x0, x5]
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1892
	umull	x2, w20, w25
	ldr	x0, [x19, 2520]
	lsl	w4, w4, 10
	add	x0, x0, x2
	str	w4, [x0, 4]
	ldr	x1, [x19, 2520]
	ldr	x0, [x19, 2600]
	add	x1, x1, x2
	ldr	x2, [x19, 2656]
	str	x0, [x1, 8]
	mul	w0, w20, w7
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w0, w0, w6
	add	x0, x2, w0, sxtw 2
	str	x0, [x1, 16]
.L1892:
	add	x5, x5, 1
.L1931:
	cmp	w8, w5, uxth
	bhi	.L1893
	cbnz	w20, .L1894
.L1929:
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L1891
.L1894:
	ldr	x0, [x19, 2520]
	mov	w2, 1
	mov	w1, w20
	add	x24, x22, :lo12:.LANCHOR4
	add	x27, x24, 1072
	mov	x26, 0
	bl	FlashReadPages
	umull	x0, w20, w25
	str	x0, [x29, 120]
.L1928:
	ldr	x0, [x19, 2520]
	add	x1, x0, x26
	ldr	w0, [x0, x26]
	ldr	w28, [x1, 4]
	cmn	w0, #1
	ldr	x20, [x1, 16]
	ubfx	x28, x28, 10, 16
	bne	.L1897
	mov	w6, 16
	mov	w7, 65535
.L1899:
	ldr	x0, [x19, 2520]
	mov	w2, 1
	stp	w7, w6, [x29, 104]
	add	x0, x0, x26
	ldr	w1, [x0, 4]
	add	w1, w1, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 2520]
	add	x0, x0, x26
	bl	FlashReadPages
	ldp	w7, w6, [x29, 104]
	ldrh	w0, [x20]
	cmp	w0, w7
	ldr	x0, [x19, 2520]
	bne	.L1896
	mov	w1, -1
	str	w1, [x0, x26]
	ldr	x0, [x19, 2520]
	ldr	w0, [x0, x26]
	cmp	w0, w1
	beq	.L1965
.L1897:
	ldr	w0, [x19, 2396]
	ldr	w10, [x20, 4]
	cmn	w0, #1
	beq	.L1900
	cmp	w0, w10
	bhi	.L1901
.L1900:
	cmn	w10, #1
	beq	.L1901
	add	w0, w10, 1
	str	w0, [x19, 2396]
.L1901:
	ldrh	w0, [x20]
	mov	w1, 61604
	cmp	w0, w1
	beq	.L1903
	bhi	.L1904
	mov	w1, 61574
	cmp	w0, w1
	beq	.L1905
.L1902:
	ldr	x0, [x29, 120]
	add	x26, x26, 56
	cmp	x26, x0
	bne	.L1928
	b	.L1929
.L1896:
	ldr	w0, [x0, x26]
	cmn	w0, #1
	bne	.L1897
	sub	w6, w6, #1
	ands	w6, w6, 65535
	bne	.L1899
.L1965:
	mov	w1, 0
	mov	w0, w28
	bl	FtlFreeSysBlkQueueIn
	b	.L1902
.L1904:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1906
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1965
	b	.L1902
.L1906:
	ldr	w6, [x19, 2372]
	ldrh	w1, [x24, 1088]
	and	w2, w6, 65535
	ldr	x7, [x19, 2784]
	sub	w0, w2, #1
	sub	w2, w2, w1
	sub	w2, w2, #1
	sxth	x0, w0
	sxth	w2, w2
.L1908:
	cmp	w0, w2
	bgt	.L1914
	tbz	w0, #31, .L1946
	b	.L1902
.L1914:
	sxtw	x8, w0
	lsl	x9, x8, 2
	ldr	w11, [x7, x9]
	cmp	w10, w11
	bls	.L1909
	ldr	w2, [x7]
	cbnz	w2, .L1910
	cmp	w6, w1
	beq	.L1910
	add	w1, w1, 1
	strh	w1, [x24, 1088]
.L1910:
	mov	w1, 0
.L1911:
	cmp	w1, w0
	bne	.L1912
	ldr	x1, [x19, 2784]
	ldr	w2, [x20, 4]
	str	w2, [x1, x9]
	ldr	x1, [x19, 2720]
	strh	w28, [x1, x8, lsl 1]
	tbnz	w0, #31, .L1902
	ldrh	w1, [x24, 1088]
	ldr	w2, [x19, 2372]
	sub	w2, w2, w1
	sub	w2, w2, #1
	cmp	w0, w2, sxth
	bgt	.L1902
.L1946:
	add	w1, w1, 1
	strh	w1, [x24, 1088]
	ldr	x1, [x19, 2784]
	ldr	w2, [x20, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 2720]
.L1964:
	strh	w28, [x1, x0, lsl 1]
	b	.L1902
.L1912:
	ldr	x7, [x19, 2784]
	sxtw	x2, w1
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	x10, x7, x6
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w10, [x10, 4]
	str	w10, [x7, x6]
	ldr	x6, [x19, 2720]
	add	x7, x6, x2
	ldrh	w7, [x7, 2]
	strh	w7, [x6, x2]
	b	.L1911
.L1909:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L1908
.L1905:
	ldrh	w6, [x19, 2364]
	ldrh	w1, [x19, 2920]
	sub	w2, w6, #1
	ldr	x7, [x19, 2752]
	sxth	x0, w2
	sub	w2, w2, w1
.L1917:
	cmp	w0, w2
	ble	.L1922
	sxtw	x8, w0
	lsl	x9, x8, 2
	ldr	w11, [x7, x9]
	cmp	w10, w11
	bls	.L1918
	ldr	w2, [x7]
	cbnz	w2, .L1919
	cmp	w6, w1
	beq	.L1919
	add	w1, w1, 1
	strh	w1, [x19, 2920]
.L1919:
	mov	w1, 0
.L1920:
	cmp	w1, w0
	bne	.L1921
	ldr	x1, [x19, 2752]
	ldr	w2, [x20, 4]
	str	w2, [x1, x9]
	ldr	x1, [x19, 2736]
	strh	w28, [x1, x8, lsl 1]
.L1922:
	tbnz	w0, #31, .L1902
	ldrh	w1, [x19, 2364]
	ldrh	w2, [x19, 2920]
	sub	w1, w1, #1
	sub	w1, w1, w2
	cmp	w0, w1, sxth
	bgt	.L1902
	ldr	x1, [x19, 2752]
	add	w2, w2, 1
	strh	w2, [x19, 2920]
	ldr	w2, [x20, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 2736]
	b	.L1964
.L1921:
	ldr	x7, [x19, 2752]
	sxtw	x2, w1
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	x10, x7, x6
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w10, [x10, 4]
	str	w10, [x7, x6]
	ldr	x6, [x19, 2736]
	add	x7, x6, x2
	ldrh	w7, [x7, 2]
	strh	w7, [x6, x2]
	b	.L1920
.L1918:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L1917
.L1903:
	ldrh	w0, [x27]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1924
	strh	w28, [x27]
	str	w10, [x27, 8]
	b	.L1902
.L1924:
	ldrh	w0, [x27, 4]
	cmp	w0, w1
	beq	.L1925
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1925:
	ldr	w0, [x20, 4]
	ldr	w1, [x27, 8]
	cmp	w1, w0
	bcs	.L1926
	ldrh	w0, [x27]
	strh	w0, [x27, 4]
	strh	w28, [x27]
	ldr	w0, [x20, 4]
	str	w0, [x27, 8]
	b	.L1902
.L1926:
	strh	w28, [x27, 4]
	b	.L1902
.L1930:
	ldr	x1, [x19, 2720]
	ldrh	w0, [x1]
	cbz	w0, .L1932
.L1935:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0, 2736]
	ldrh	w2, [x1]
	cbz	w2, .L1933
.L1951:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1932:
	add	x22, x22, :lo12:.LANCHOR4
	ldrh	w0, [x22, 1088]
	cbz	w0, .L1935
	ldr	w2, [x19, 2372]
	mov	w0, 0
.L1936:
	cmp	w0, w2
	bcs	.L1935
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L1937
	mov	w1, w0
	add	x2, x21, :lo12:.LANCHOR2
.L1938:
	ldr	w3, [x2, 2372]
	cmp	w1, w3
	bcs	.L1935
	ldr	x5, [x2, 2720]
	sxtw	x6, w1
	lsl	x4, x6, 1
	sub	w3, w1, w0
	sxtw	x3, w3
	add	w1, w1, 1
	sxth	w1, w1
	ldrh	w7, [x5, x4]
	strh	w7, [x5, x3, lsl 1]
	ldr	x5, [x2, 2784]
	ldr	w6, [x5, x6, lsl 2]
	str	w6, [x5, x3, lsl 2]
	ldr	x3, [x2, 2720]
	strh	wzr, [x3, x4]
	b	.L1938
.L1937:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1936
.L1933:
	ldrh	w2, [x0, 2920]
	cbz	w2, .L1951
	ldrh	w2, [x0, 2364]
	mov	w0, 0
.L1941:
	mov	w5, w0
	cmp	w0, w2
	bge	.L1951
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L1942
	add	x21, x21, :lo12:.LANCHOR2
.L1943:
	ldrh	w1, [x21, 2364]
	cmp	w0, w1
	bge	.L1951
	ldr	x3, [x21, 2736]
	sxtw	x4, w0
	lsl	x2, x4, 1
	sub	w1, w0, w5
	sxtw	x1, w1
	add	w0, w0, 1
	sxth	w0, w0
	ldrh	w6, [x3, x2]
	strh	w6, [x3, x1, lsl 1]
	ldr	x3, [x21, 2752]
	ldr	w4, [x3, x4, lsl 2]
	str	w4, [x3, x1, lsl 2]
	ldr	x1, [x21, 2736]
	strh	wzr, [x1, x2]
	b	.L1943
.L1942:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1941
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadEctTbl
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	stp	x29, x30, [sp, -32]!
	mov	w0, 64
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19, 2672]
	ldr	x2, [x19, 2688]
	bl	FtlVendorPartRead
	ldr	x0, [x19, 2688]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1967
	adrp	x1, .LC47
	adrp	x0, .LC48
	add	x1, x1, :lo12:.LC47
	add	x0, x0, :lo12:.LC48
	bl	printk
	ldr	x0, [x19, 2688]
	mov	w1, 0
	ldrh	w2, [x19, 2672]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1967:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	and	w0, w0, 65535
	cbz	w1, .L1970
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_set_blk_mode.part.17
	ldp	x29, x30, [sp], 16
	ret
.L1970:
	adrp	x1, .LANCHOR0+80
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1, #:lo12:.LANCHOR0+80]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3, x2]
	bic	w0, w1, w0
	str	w0, [x3, x2]
	ret
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	and	w1, w0, 65535
	adrp	x0, .LANCHOR0+80
	ldr	x0, [x0, #:lo12:.LANCHOR0+80]
	ubfx	x2, x1, 5, 11
	ldr	w0, [x0, x2, lsl 2]
	lsr	w0, w0, w1
	and	w0, w0, 1
	ret
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	FtlCheckVpc
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC49
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	stp	x23, x24, [sp, 48]
	add	x23, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	mov	w22, 0
	add	x1, x1, 152
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	add	x0, x0, :lo12:.LC49
	adrp	x21, check_valid_page_count_table
	bl	printk
	add	x19, x21, :lo12:check_valid_page_count_table
	mov	x2, 8192
	mov	w1, 0
	mov	x0, x19
	bl	memset
.L1978:
	ldr	w0, [x23, 2924]
	cmp	w22, w0
	bcc	.L1980
	adrp	x24, .LC50
	add	x22, x20, :lo12:.LANCHOR2
	add	x25, x21, :lo12:check_valid_page_count_table
	add	x24, x24, :lo12:.LC50
	mov	w23, 0
	mov	w19, 0
	mov	w28, 65535
.L1981:
	ldrh	w0, [x22, 2284]
	cmp	w0, w19
	bhi	.L1983
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x19, [x0, 768]
	cbz	x19, .L1984
	ldrh	w25, [x0, 776]
	adrp	x24, .LC51
	ldr	x0, [x22, 2808]
	add	x20, x20, :lo12:.LANCHOR2
	add	x21, x21, :lo12:check_valid_page_count_table
	add	x24, x24, :lo12:.LC51
	sub	x19, x19, x0
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	movk	x0, 0xaaab, lsl 0
	mov	w26, 6
	mov	w22, 0
	mul	x19, x19, x0
	and	w19, w19, 65535
.L1985:
	cmp	w22, w25
	bne	.L1987
.L1984:
	mov	w1, w23
	adrp	x0, .LC52
	add	x0, x0, :lo12:.LC52
	bl	printk
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L1980:
	mov	w2, 0
	add	x1, x29, 108
	mov	w0, w22
	bl	log2phys
	ldr	w0, [x29, 108]
	cmn	w0, #1
	beq	.L1979
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	and	x0, x0, 65535
	ldrh	w1, [x19, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x19, x0, lsl 1]
.L1979:
	add	w22, w22, 1
	b	.L1978
.L1983:
	ldr	x0, [x22, 2712]
	ubfiz	x27, x19, 1, 16
	sxtw	x26, w19
	ldrh	w2, [x0, x27]
	ldrh	w3, [x25, x26, lsl 1]
	cmp	w2, w3
	beq	.L1982
	mov	w1, w19
	mov	x0, x24
	bl	printk
	ldr	x0, [x22, 2712]
	ldrh	w0, [x0, x27]
	cmp	w0, w28
	beq	.L1982
	ldrh	w1, [x25, x26, lsl 1]
	cmp	w1, w0
	csinc	w23, w23, wzr, ls
.L1982:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1981
.L1987:
	ldr	x1, [x20, 2712]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L1986
	ldrh	w3, [x21, w19, sxtw 1]
	mov	w23, 1
	mov	w1, w19
	mov	x0, x24
	bl	printk
.L1986:
	ldr	x0, [x20, 2808]
	umull	x19, w19, w26
	ldrh	w19, [x0, x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L1984
	add	w22, w22, 1
	and	w22, w22, 65535
	b	.L1985
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	2
	.global	FtlDumpSysBlock
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	sub	sp, sp, #80
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x23, x24, [sp, 64]
	and	w23, w0, 65535
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 32]
	add	x19, x0, 2936
	stp	x21, x22, [sp, 48]
	lsl	w24, w23, 10
	adrp	x22, .LC53
	mov	x21, x0
	ldr	x1, [x0, 2576]
	add	x22, x22, :lo12:.LC53
	str	x1, [x19, 8]
	mov	w20, 0
	ldr	x1, [x0, 2640]
	str	x1, [x19, 16]
.L1999:
	ldrh	w0, [x21, 2346]
	cmp	w20, w0
	blt	.L2001
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 80
	ret
.L2001:
	orr	w0, w20, w24
	mov	w2, 1
	str	w0, [x19, 4]
	mov	w1, w2
	mov	x0, x19
	bl	FlashReadPages
	ldp	x1, x0, [x19, 8]
	ldr	w1, [x1]
	str	w1, [sp]
	mov	w1, w23
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x22
	ldp	w2, w3, [x19]
	bl	printk
	ldr	x0, [x19, 16]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2000
	ldr	x1, [x21, 2576]
	adrp	x0, .LC54
	mov	w3, 768
	mov	w2, 4
	add	x0, x0, :lo12:.LC54
	bl	rknand_print_hex
.L2000:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L1999
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	2
	.global	Ftlscanalldata
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	sub	sp, sp, #96
	adrp	x0, .LC55
	mov	w1, 0
	add	x0, x0, :lo12:.LC55
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x21, x22, [sp, 48]
	adrp	x22, .LANCHOR2
	add	x21, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 32]
	str	x23, [sp, 64]
	mov	w20, 0
	add	x19, x21, 2936
	bl	printk
.L2007:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w0, [x0, 2924]
	cmp	w20, w0
	bcc	.L2013
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x29, x30, [sp, 16]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	ret
.L2013:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w20
	bl	log2phys
	tst	x20, 2047
	bne	.L2008
	ldr	w2, [x29, 76]
	adrp	x0, .LC56
	mov	w1, w20
	add	x0, x0, :lo12:.LC56
	bl	printk
.L2008:
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L2010
	ldr	x23, [x21, 2640]
	mov	w2, 0
	stp	wzr, w0, [x19]
	mov	w1, 1
	ldr	x0, [x21, 2576]
	str	x0, [x19, 8]
	mov	x0, x19
	str	x23, [x19, 16]
	str	w20, [x19, 24]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmp	w0, 256
	ccmn	w0, #1, 4, ne
	beq	.L2011
	ldr	w0, [x23, 8]
	cmp	w20, w0
	beq	.L2010
.L2011:
	ldp	x1, x0, [x19, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC57
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC57
	ldr	w2, [x19, 4]
	mov	w1, w20
	bl	printk
.L2010:
	add	w20, w20, 1
	b	.L2007
	.size	Ftlscanalldata, .-Ftlscanalldata
	.align	2
	.global	dump_map_info
	.type	dump_map_info, %function
dump_map_info:
	sub	sp, sp, #96
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 64]
	stp	x21, x22, [sp, 48]
	add	x24, x0, 2304
	stp	x25, x26, [sp, 80]
	mov	x21, x19
	mov	x19, x0
	mov	w23, 56
	ldrh	w22, [x0, 2284]
.L2016:
	ldrh	w0, [x19, 2286]
	cmp	w0, w22
	bhi	.L2022
	add	x25, x21, :lo12:.LANCHOR2
	adrp	x26, .LANCHOR4
	add	x24, x25, 2936
	add	x26, x26, :lo12:.LANCHOR4
	mov	w20, 0
.L2023:
	ldrh	w0, [x26, 1088]
	cmp	w20, w0
	bge	.L2026
	adrp	x0, .LC59
	sbfiz	x22, x20, 1, 32
	mov	w19, 0
	add	x23, x0, :lo12:.LC59
	b	.L2027
.L2018:
	ldrb	w0, [x24, x5]
	mov	w1, w22
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2017
	umull	x2, w20, w23
	ldr	x0, [x19, 2520]
	lsl	w4, w4, 10
	add	x0, x0, x2
	str	w4, [x0, 4]
	ldr	x1, [x19, 2520]
	ldr	x0, [x19, 2600]
	add	x1, x1, x2
	ldr	x2, [x19, 2656]
	str	x0, [x1, 8]
	mul	w0, w20, w7
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w0, w0, w6
	add	x0, x2, w0, sxtw 2
	str	x0, [x1, 16]
.L2017:
	add	x5, x5, 1
.L2024:
	cmp	w8, w5, uxth
	bhi	.L2018
	cbnz	w20, .L2019
.L2021:
	add	w22, w22, 1
	and	w22, w22, 65535
	b	.L2016
.L2019:
	ldr	x0, [x19, 2520]
	mov	w1, w20
	adrp	x26, .LC58
	mov	w2, 1
	umull	x20, w20, w23
	mov	x25, 0
	add	x26, x26, :lo12:.LC58
	bl	FlashReadPages
.L2020:
	ldr	x0, [x19, 2520]
	add	x0, x0, x25
	add	x25, x25, 56
	ldr	x1, [x0, 16]
	ldr	w2, [x0, 4]
	ldr	x0, [x0, 8]
	ldr	w3, [x0, 4]
	str	w3, [sp]
	ldr	w7, [x0]
	mov	x0, x26
	ldp	w3, w4, [x1]
	ldp	w5, w6, [x1, 8]
	ubfx	x1, x2, 10, 16
	bl	printk
	cmp	x25, x20
	beq	.L2021
	b	.L2020
.L2022:
	ldrh	w8, [x19, 2276]
	mov	x5, 0
	ldrh	w7, [x19, 2356]
	mov	w20, 0
	mov	w6, 4
	b	.L2024
.L2025:
	ldr	x0, [x25, 2720]
	mov	w2, 1
	mov	w1, w2
	ldrh	w0, [x0, x22]
	orr	w0, w19, w0, lsl 10
	str	w0, [x24, 4]
	mov	x0, x24
	bl	FlashReadPages
	ldp	x2, x0, [x24, 8]
	add	w19, w19, 1
	ldr	x1, [x25, 2720]
	sxth	w19, w19
	ldr	w3, [x2, 4]
	ldrh	w1, [x1, x22]
	str	w3, [sp, 8]
	ldr	w2, [x2]
	str	w2, [sp]
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x23
	ldp	w2, w3, [x24]
	bl	printk
.L2027:
	ldrh	w0, [x25, 2346]
	cmp	w19, w0
	blt	.L2025
	add	w20, w20, 1
	sxth	w20, w20
	b	.L2023
.L2026:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w2, 2
	adrp	x0, .LC60
	add	x0, x0, :lo12:.LC60
	ldr	w3, [x21, 2372]
	ldr	x1, [x21, 2720]
	bl	rknand_print_hex
	ldrh	w3, [x21, 2380]
	mov	w2, 4
	ldr	x1, [x21, 2768]
	adrp	x0, .LC61
	add	x0, x0, :lo12:.LC61
	bl	rknand_print_hex
	ldrh	w3, [x21, 2380]
	mov	w2, 4
	ldr	x1, [x21, 2776]
	adrp	x0, .LC62
	add	x0, x0, :lo12:.LC62
	bl	rknand_print_hex
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 96
	ret
	.size	dump_map_info, .-dump_map_info
	.align	2
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	stp	x25, x26, [sp, 64]
	mov	w19, 0
	stp	x23, x24, [sp, 48]
	adrp	x25, .LANCHOR0
	stp	x27, x28, [sp, 80]
	ldrh	w24, [x0, 6]
	ldr	x0, [x0, 40]
	str	x0, [x29, 120]
	ldrh	w0, [x22, 8]
	lsl	w2, w24, 2
	str	w0, [x29, 116]
	ldr	x0, [x29, 120]
	ldp	x23, x27, [x22, 16]
	bl	ftl_memset
	stp	wzr, wzr, [x22, 48]
	add	x4, x20, :lo12:.LANCHOR2
	mov	w0, -1
	mov	x26, x4
	ldr	x1, [x4, 2576]
	ldr	x21, [x4, 2640]
	str	x1, [x4, 2944]
	str	x21, [x4, 2952]
	strh	w0, [x22]
	strh	w0, [x22, 2]
	mov	w0, 1
	str	w0, [x22, 56]
	ldr	w0, [x29, 116]
	sub	w0, w0, #1
	str	w0, [x29, 112]
	add	x0, x25, :lo12:.LANCHOR0
	str	x0, [x29, 96]
.L2031:
	ldr	w0, [x29, 116]
	cmp	w19, w0
	bge	.L2050
	ldr	w0, [x29, 112]
	sxtw	x28, w19
	cmp	w19, w0
	bne	.L2032
	lsl	x0, x28, 1
	mov	w1, 1
	add	x25, x23, x0
	adrp	x26, .LANCHOR0
	add	x26, x26, :lo12:.LANCHOR0
	ldrh	w0, [x23, x0]
	bl	FtlGetLastWrittenPage
	strh	w19, [x22]
	sxth	w23, w0
	add	w0, w0, 1
	strh	w0, [x22, 2]
	add	w23, w23, 1
	ldr	w0, [x27, x28, lsl 2]
	add	x27, x20, :lo12:.LANCHOR2
	add	x19, x27, 2936
	mov	w28, 0
	str	w0, [x22, 48]
.L2033:
	cmp	w28, w23
	blt	.L2036
.L2050:
	add	x20, x20, :lo12:.LANCHOR2
	mov	x0, x22
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x22, 2]
	ldrh	w0, [x20, 2346]
	cmp	w1, w0
	bne	.L2038
	mov	x0, x22
	bl	ftl_map_blk_alloc_new_blk
.L2038:
	mov	x0, x22
	bl	ftl_map_blk_gc
	mov	x0, x22
	bl	ftl_map_blk_gc
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L2036:
	ldrh	w0, [x25]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w28, w0, lsl 10
	str	w0, [x19, 4]
	mov	x0, x19
	bl	FlashReadPages
	ldrb	w0, [x26, 88]
	cbz	w0, .L2034
	ldr	x0, [x19, 16]
	ldr	w6, [x0, 12]
	cbz	w6, .L2034
	ldrh	w1, [x27, 2354]
	ldr	x0, [x19, 8]
	bl	js_hash
	cmp	w6, w0
	beq	.L2034
	mov	w0, -1
	str	w0, [x19]
.L2034:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2035
	ldrh	w0, [x21, 8]
	cmp	w24, w0
	bls	.L2035
	ldrh	w2, [x21]
	ldrh	w1, [x22, 4]
	cmp	w2, w1
	bne	.L2035
	ldr	x2, [x29, 120]
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x19, 4]
	str	w1, [x2, x0]
.L2035:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L2033
.L2032:
	add	x0, x26, 2936
	ldr	x1, [x26, 2576]
	str	x1, [x0, 8]
	lsl	x1, x28, 1
	add	x2, x23, x1
	str	x2, [x29, 104]
	ldrh	w2, [x26, 2346]
	ldrh	w1, [x23, x1]
	sub	w2, w2, #1
	orr	w1, w2, w1, lsl 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x26, 2936]
	cmn	w0, #1
	beq	.L2052
	ldrh	w1, [x21]
	ldrh	w0, [x22, 4]
	cmp	w1, w0
	bne	.L2052
	ldrh	w1, [x21, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L2040
.L2052:
	add	x25, x26, 2936
	mov	w28, 0
.L2041:
	ldrh	w0, [x26, 2346]
	cmp	w28, w0
	bge	.L2048
	ldr	x0, [x29, 104]
	mov	w2, 1
	mov	w1, w2
	ldrh	w0, [x0]
	orr	w0, w28, w0, lsl 10
	str	w0, [x25, 4]
	mov	x0, x25
	bl	FlashReadPages
	ldr	x0, [x29, 96]
	ldrb	w0, [x0, 88]
	cbz	w0, .L2045
	ldr	x0, [x25, 16]
	ldr	w6, [x0, 12]
	cbz	w6, .L2045
	ldrh	w1, [x26, 2354]
	ldr	x0, [x25, 8]
	bl	js_hash
	cmp	w6, w0
	beq	.L2045
	mov	w0, -1
	str	w0, [x25]
.L2045:
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L2046
	ldrh	w0, [x21, 8]
	cmp	w24, w0
	bls	.L2046
	ldrh	w2, [x21]
	ldrh	w1, [x22, 4]
	cmp	w2, w1
	bne	.L2046
	ldr	x2, [x29, 120]
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x25, 4]
	str	w1, [x2, x0]
.L2046:
	add	w5, w28, 1
	sxth	w28, w5
	b	.L2041
.L2040:
	mov	w0, 0
.L2042:
	ldrh	w1, [x26, 2346]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L2044
.L2048:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2031
.L2044:
	ldr	x2, [x26, 2576]
	sbfiz	x5, x0, 3, 32
	ldrh	w1, [x2, x5]
	cmp	w24, w1
	bls	.L2043
	add	x2, x2, x5
	ldr	x3, [x29, 120]
	ubfiz	x1, x1, 2, 16
	ldr	w2, [x2, 4]
	str	w2, [x3, x1]
.L2043:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L2042
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 1000
	add	x29, sp, 0
	ldrh	w2, [x1, 2364]
	strh	w2, [x0, 10]
	mov	w2, -3962
	strh	w2, [x0, 4]
	ldrh	w2, [x1, 2920]
	strh	w2, [x0, 8]
	ldrh	w2, [x1, 2366]
	strh	w2, [x0, 6]
	ldr	x2, [x1, 2736]
	str	x2, [x0, 16]
	ldr	x2, [x1, 2752]
	str	x2, [x0, 24]
	ldr	x2, [x1, 2744]
	ldr	x1, [x1, 2760]
	stp	x2, x1, [x0, 32]
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
	adrp	x0, .LANCHOR2
	mov	w1, 0
	add	x29, sp, 0
	str	x19, [sp, 16]
	add	x19, x0, :lo12:.LANCHOR2
	ldr	x0, [x19, 2728]
	ldr	w2, [x19, 2372]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x19, 2382]
	mov	w1, 255
	ldrh	w2, [x19, 2354]
	mul	w2, w2, w0
	ldr	x0, [x19, 2800]
	bl	ftl_memset
	mov	x0, x19
	mov	w1, 0
	mov	w5, -1
.L2075:
	ldrh	w2, [x0, 2382]
	cmp	w2, w1
	bhi	.L2076
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	mov	w3, -1
	add	x1, x2, 928
	strh	w3, [x2, 930]
	strh	w3, [x2, 928]
	ldr	w3, [x0, 2372]
	strh	w3, [x2, 938]
	mov	w3, -3902
	strh	w3, [x2, 932]
	ldrh	w2, [x2, 1088]
	strh	w2, [x1, 8]
	ldrh	w2, [x0, 2380]
	strh	w2, [x1, 6]
	ldr	x2, [x0, 2720]
	str	x2, [x1, 16]
	ldr	x2, [x0, 2784]
	str	x2, [x1, 24]
	ldr	x2, [x0, 2728]
	ldr	x0, [x0, 2768]
	stp	x2, x0, [x1, 32]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2076:
	ldr	x3, [x0, 2792]
	ubfiz	x2, x1, 4, 16
	add	x3, x3, x2
	str	wzr, [x3, 4]
	ldr	x3, [x0, 2792]
	strh	w5, [x3, x2]
	ldr	x3, [x0, 2792]
	ldr	x4, [x0, 2800]
	add	x3, x3, x2
	ldrh	w2, [x0, 2354]
	mul	w2, w2, w1
	add	w1, w1, 1
	and	w1, w1, 65535
	sxtw	x2, w2
	and	x2, x2, -4
	add	x2, x4, x2
	str	x2, [x3, 8]
	b	.L2075
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
	and	w6, w1, 65535
	adrp	x4, .LANCHOR2
	add	x1, x4, :lo12:.LANCHOR2
	and	w2, w2, 65535
	strh	wzr, [x0, 4]
	ldrh	w3, [x1, 2276]
	mov	w1, 65535
.L2081:
	cmp	w3, w2, uxth
	bhi	.L2083
	add	x4, x4, :lo12:.LANCHOR2
	ubfiz	x3, x3, 1, 16
	add	x3, x3, 16
	add	x2, x0, 16
	add	x3, x0, x3
	mov	w5, 65535
	ldrh	w1, [x4, 2344]
	sub	w1, w1, #1
	and	w1, w1, 65535
	sub	w1, w1, w6
.L2084:
	cmp	x2, x3
	bne	.L2086
	ret
.L2083:
	add	x5, x0, w2, sxtw 1
	ldrh	w5, [x5, 16]
	cmp	w5, w1
	beq	.L2082
	ldrh	w5, [x0, 4]
	add	w5, w5, 1
	strh	w5, [x0, 4]
.L2082:
	add	w2, w2, 1
	b	.L2081
.L2086:
	ldrh	w4, [x2]
	cmp	w4, w5
	beq	.L2085
	ldrh	w4, [x0, 4]
	add	w4, w1, w4
	strh	w4, [x0, 4]
.L2085:
	add	x2, x2, 2
	b	.L2084
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlReUsePrevPpa
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	str	x21, [sp, 32]
	lsr	w0, w1, 10
	str	w1, [x29, 60]
	bl	P2V_block_in_plane
	adrp	x2, .LANCHOR2
	add	x4, x2, :lo12:.LANCHOR2
	and	w0, w0, 65535
	ubfiz	x21, x0, 1, 16
	ldr	x3, [x4, 2712]
	ldrh	w1, [x3, x21]
	cbnz	w1, .L2088
	mov	x20, x2
	adrp	x9, .LANCHOR4
	add	x2, x9, :lo12:.LANCHOR4
	ldr	x8, [x2, 768]
	cbz	x8, .L2089
	ldrh	w3, [x2, 776]
	mov	w5, 65535
	ldr	x2, [x4, 2808]
	mov	x4, -6148914691236517206
	movk	x4, 0xaaab, lsl 0
	sub	x8, x8, x2
	asr	x8, x8, 1
	mul	x8, x8, x4
	mov	w4, 6
	and	w8, w8, 65535
.L2090:
	cmp	w1, w3
	beq	.L2089
	cmp	w8, w0
	bne	.L2091
	add	x9, x9, :lo12:.LANCHOR4
	mov	w1, w8
	add	x0, x9, 768
	bl	List_remove_node
	ldrh	w0, [x9, 776]
	sub	w0, w0, #1
	strh	w0, [x9, 776]
	mov	w0, w8
	bl	INSERT_DATA_LIST
	add	x2, x20, :lo12:.LANCHOR2
	ldr	x1, [x2, 2712]
	ldrh	w0, [x1, x21]
	add	w0, w0, 1
	strh	w0, [x1, x21]
.L2089:
	add	x1, x29, 60
	mov	w2, 1
	mov	w0, w19
	bl	log2phys
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L2091:
	umull	x8, w8, w4
	ldrh	w8, [x2, x8]
	cmp	w8, w5
	beq	.L2089
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L2090
.L2088:
	add	w1, w1, 1
	strh	w1, [x3, x21]
	b	.L2089
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	mov	x5, x0
	strh	wzr, [x0, 4]
	strb	wzr, [x0, 7]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	mov	x7, 0
	add	x8, x5, 16
	mov	x6, x0
	add	x9, x1, 2304
	mov	w11, -1
	ldrh	w10, [x1, 2276]
	cmp	w10, w7, uxth
	bhi	.L2117
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w1, [x5, 7]
	strb	wzr, [x5, 9]
	ldrh	w2, [x0, 2344]
	mul	w1, w1, w2
	strh	w1, [x5, 4]
	ldr	w1, [x0, 2096]
	cbz	w1, .L2113
	ldrh	w1, [x5]
	ldr	x0, [x0, 2696]
	ldrh	w0, [x0, x1, lsl 1]
	cmp	w0, 79
	bhi	.L2113
	mov	w0, 1
	strb	w0, [x5, 9]
.L2113:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0, #:lo12:.LANCHOR0+88]
	cbz	w0, .L2116
	mov	w0, 1
	strb	w0, [x5, 9]
.L2116:
	mov	w0, 0
	ret
.L2117:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L2100:
	ldrh	w1, [x5]
	ldrb	w0, [x9, x7]
	bl	V2P_block
	mov	w4, w0
	strh	w11, [x8]
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2099
	strh	w4, [x8]
	ldrb	w0, [x5, 7]
	add	w0, w0, 1
	strb	w0, [x5, 7]
.L2099:
	add	x7, x7, 1
	add	x8, x8, 2
	cmp	w10, w7, uxth
	bhi	.L2100
	add	x0, x6, :lo12:.LANCHOR2
	ldrb	w1, [x5, 7]
	strb	wzr, [x5, 9]
	ldrh	w2, [x0, 2344]
	mul	w1, w1, w2
	strh	w1, [x5, 4]
	ldr	w1, [x0, 2096]
	cbz	w1, .L2101
	ldrh	w1, [x5]
	ldr	x0, [x0, 2696]
	ldrh	w0, [x0, x1, lsl 1]
	cmp	w0, 79
	bhi	.L2101
	mov	w0, 1
	strb	w0, [x5, 9]
.L2101:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0, #:lo12:.LANCHOR0+88]
	cbz	w0, .L2102
	mov	w0, 1
	strb	w0, [x5, 9]
.L2102:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	make_superblock, .-make_superblock
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	sub	sp, sp, #112
	mov	w1, 0
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x23, x24, [sp, 64]
	stp	x21, x22, [sp, 48]
	adrp	x22, .LANCHOR2
	add	x24, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 32]
	add	x20, x24, 2936
	stp	x25, x26, [sp, 80]
	stp	x27, x28, [sp, 96]
	adrp	x19, .LANCHOR4
	ldr	x0, [x24, 2576]
	add	x23, x19, :lo12:.LANCHOR4
	str	x0, [x20, 8]
	ldr	x0, [x24, 2640]
	str	x0, [x20, 16]
	ldr	x0, [x24, 2712]
	ldrh	w2, [x24, 2284]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x23, 1072]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2119
.L2130:
	mov	w0, -1
.L2118:
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 112
	ret
.L2119:
	add	x25, x23, 1072
	mov	w1, 1
	mov	w26, 19539
	bl	FtlGetLastWrittenPage
	ldrsh	w28, [x23, 1072]
	sxth	w21, w0
	adrp	x23, .LANCHOR0
	add	w0, w0, 1
	strh	w0, [x25, 2]
	add	x25, x23, :lo12:.LANCHOR0
	movk	w26, 0x4654, lsl 16
.L2121:
	tbnz	w21, #31, .L2129
	orr	w0, w21, w28, lsl 10
	str	w0, [x20, 4]
	ldr	x0, [x24, 2576]
	mov	w2, 1
	str	x0, [x20, 8]
	mov	w1, w2
	mov	x0, x20
	bl	FlashReadPages
	ldrb	w0, [x25, 88]
	cbz	w0, .L2122
	ldr	x8, [x20, 16]
	ldr	w7, [x8, 12]
	cbz	w7, .L2122
	ldr	x6, [x20, 8]
	ldrh	w1, [x24, 2354]
	mov	x0, x6
	bl	js_hash
	cmp	w7, w0
	beq	.L2122
	cbnz	w21, .L2123
	add	x0, x19, :lo12:.LANCHOR4
	add	x27, x0, 1072
	ldrh	w1, [x0, 1076]
	cmp	w28, w1
	beq	.L2123
	ldrh	w1, [x0, 1072]
	ldr	w0, [x6]
	str	w0, [sp]
	adrp	x0, .LC63
	add	x0, x0, :lo12:.LC63
	ldp	w4, w5, [x8]
	ldr	w6, [x8, 8]
	ldp	w2, w3, [x20]
	bl	printk
	ldrsh	w28, [x27, 4]
	ldrh	w21, [x24, 2346]
.L2125:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L2121
.L2123:
	mov	w0, -1
	str	w0, [x20]
.L2122:
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L2125
	ldr	x0, [x24, 2576]
	ldr	w0, [x0]
	cmp	w0, w26
	bne	.L2125
	ldr	x0, [x24, 2640]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L2125
.L2129:
	add	x20, x22, :lo12:.LANCHOR2
	add	x0, x19, :lo12:.LANCHOR4
	add	x21, x20, 2936
	mov	w2, 48
	add	x0, x0, 696
	add	x23, x23, :lo12:.LANCHOR0
	ldr	x1, [x21, 8]
	bl	ftl_memcpy
	ldr	x0, [x20, 2712]
	ldrh	w2, [x20, 2284]
	ldr	x1, [x21, 8]
	lsl	w2, w2, 1
	add	x1, x1, 48
	bl	ftl_memcpy
	ldrh	w1, [x20, 2284]
	ldr	x0, [x21, 8]
	lsr	w2, w1, 3
	ubfiz	x1, x1, 1, 16
	add	x1, x1, 51
	add	w2, w2, 4
	and	x1, x1, -4
	add	x1, x0, x1
	ldr	x0, [x23, 80]
	bl	ftl_memcpy
	ldrh	w0, [x20, 2384]
	cbz	w0, .L2127
	ldrh	w1, [x20, 2284]
	ldrh	w2, [x20, 2380]
	lsr	w0, w1, 3
	add	w1, w0, w1, lsl 1
	ldr	x0, [x21, 8]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x0, x1
	ldr	x0, [x20, 2776]
	bl	ftl_memcpy
.L2127:
	add	x2, x19, :lo12:.LANCHOR4
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	add	x1, x2, 696
	ldr	w3, [x2, 696]
	cmp	w3, w0
	bne	.L2130
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w4, [x2, 704]
	ldrb	w5, [x2, 706]
	strh	w4, [x2, 1078]
	ldrh	w3, [x0, 2298]
	cmp	w5, w3
	bne	.L2130
	ldrh	w3, [x0, 2344]
	ldrh	w5, [x0, 2350]
	str	w4, [x2, 1092]
	strh	wzr, [x2, 1106]
	mul	w3, w3, w4
	strb	wzr, [x2, 1110]
	str	w3, [x0, 2924]
	strb	wzr, [x2, 1112]
	mul	w3, w5, w3
	ldrh	w5, [x0, 2830]
	str	w3, [x0, 1224]
	ldr	w3, [x0, 2288]
	sub	w3, w3, w5
	ldrh	w5, [x2, 710]
	sub	w3, w3, w4
	ldrh	w4, [x0, 2276]
	strh	w5, [x2, 784]
	udiv	w3, w3, w4
	mov	w4, -1
	strh	w4, [x2, 1104]
	ldrh	w4, [x2, 718]
	strh	w4, [x2, 880]
	strh	w3, [x2, 1096]
	ldrh	w3, [x2, 712]
	lsr	w6, w3, 6
	and	w3, w3, 63
	strb	w3, [x2, 790]
	ldrb	w3, [x2, 707]
	strb	w3, [x2, 792]
	ldrh	w3, [x2, 714]
	strh	w3, [x2, 832]
	ldrh	w3, [x2, 716]
	strh	w6, [x2, 786]
	lsr	w6, w3, 6
	and	w3, w3, 63
	strb	w3, [x2, 838]
	ldrb	w3, [x2, 708]
	strb	w3, [x2, 840]
	add	x3, x2, 880
	strh	w6, [x2, 834]
	ldrh	w2, [x2, 720]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strh	w4, [x3, 2]
	strb	w2, [x3, 6]
	ldrb	w2, [x1, 13]
	strb	w2, [x3, 8]
	add	x3, x0, 2304
	ldr	w2, [x1, 32]
	str	wzr, [x0, 2404]
	str	wzr, [x0, 2408]
	ldr	w1, [x1, 40]
	stp	wzr, wzr, [x3, 112]
	stp	wzr, w2, [x3, 120]
	str	wzr, [x0, 2432]
	ldr	w2, [x0, 2396]
	str	wzr, [x0, 2440]
	cmp	w1, w2
	bls	.L2131
	str	w1, [x0, 2396]
.L2131:
	add	x0, x19, :lo12:.LANCHOR4
	add	x22, x22, :lo12:.LANCHOR2
	ldr	w0, [x0, 732]
	ldr	w1, [x22, 2400]
	cmp	w0, w1
	bls	.L2132
	str	w0, [x22, 2400]
.L2132:
	mov	w0, 65535
	cmp	w5, w0
	beq	.L2133
	add	x0, x19, :lo12:.LANCHOR4
	add	x0, x0, 784
	bl	make_superblock
.L2133:
	add	x1, x19, :lo12:.LANCHOR4
	add	x0, x1, 832
	ldrh	w2, [x1, 832]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2134
	bl	make_superblock
.L2134:
	add	x1, x19, :lo12:.LANCHOR4
	add	x0, x1, 880
	ldrh	w2, [x1, 880]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2135
	bl	make_superblock
.L2135:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 65535
	add	x0, x19, 1104
	ldrh	w2, [x19, 1104]
	cmp	w2, w1
	beq	.L2136
	bl	make_superblock
.L2136:
	mov	w0, 0
	b	.L2118
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlDumpBlockInfo
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	sub	sp, sp, #160
	lsr	w0, w0, 10
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x19, .LANCHOR2
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 48]
	stp	x23, x24, [sp, 64]
	add	x23, x29, 144
	stp	x25, x26, [sp, 80]
	and	w25, w1, 255
	str	x27, [sp, 96]
	bl	P2V_block_in_plane
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	and	w22, w0, 65535
	add	x1, x1, 168
	ldrh	w24, [x20, 2344]
	adrp	x0, .LC49
	add	x0, x0, :lo12:.LC49
	bl	printk
	ldr	x1, [x20, 2712]
	ubfiz	x0, x22, 1, 16
	ldrh	w2, [x1, x0]
	mov	w1, w22
	adrp	x0, .LC64
	add	x0, x0, :lo12:.LC64
	bl	printk
	strh	w22, [x23, -48]!
	mov	x0, x23
	bl	make_superblock
	ldrb	w0, [x20, 1220]
	cbz	w0, .L2157
	cbnz	w25, .L2157
	mov	w0, w22
	bl	ftl_get_blk_mode
	mov	w22, w0
	cmp	w0, 1
	bne	.L2148
	ldrh	w24, [x20, 2346]
.L2148:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w21, 0
	mov	w26, 56
	mov	w2, w24
	mov	w1, w22
	adrp	x0, .LC65
	ldrh	w3, [x19, 2344]
	add	x0, x0, :lo12:.LC65
	bl	printk
.L2149:
	ldrh	w7, [x19, 2276]
	add	x3, x23, 16
	ldrh	w8, [x19, 2354]
	mov	w20, 0
	ldrh	w9, [x19, 2356]
	mov	w0, 0
	mov	w10, 65535
	mov	w5, 4
.L2150:
	cmp	w0, w7
	bne	.L2152
	ldr	x0, [x19, 2520]
	mov	w1, w20
	adrp	x27, .LC59
	mov	w2, w22
	umull	x20, w20, w26
	mov	x25, 0
	add	x27, x27, :lo12:.LC59
	bl	FlashReadPages
.L2153:
	cmp	x25, x20
	bne	.L2154
	add	w21, w21, 1
	and	w21, w21, 65535
	cmp	w24, w21
	bne	.L2149
.L2155:
	ldp	x19, x20, [sp, 32]
	mov	w0, 0
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x29, x30, [sp, 16]
	ldr	x27, [sp, 96]
	add	sp, sp, 160
	ret
.L2157:
	mov	w22, 0
	b	.L2148
.L2152:
	ldrh	w1, [x3]
	cmp	w1, w10
	beq	.L2151
	umull	x6, w20, w26
	ldr	x4, [x19, 2520]
	orr	w1, w21, w1, lsl 10
	add	x4, x4, x6
	str	w1, [x4, 4]
	mul	w1, w20, w8
	ldr	x2, [x19, 2520]
	ldr	x4, [x19, 2600]
	sdiv	w1, w1, w5
	add	x2, x2, x6
	add	x1, x4, w1, sxtw 2
	str	x1, [x2, 8]
	mul	w1, w20, w9
	ldr	x4, [x19, 2656]
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w1, w1, w5
	add	x1, x4, w1, sxtw 2
	str	x1, [x2, 16]
.L2151:
	add	w0, w0, 1
	add	x3, x3, 2
	and	w0, w0, 65535
	b	.L2150
.L2154:
	ldr	x8, [x19, 2520]
	ldrh	w1, [x29, 96]
	add	x2, x8, x25
	ldp	x3, x0, [x2, 8]
	ldr	w4, [x3, 4]
	str	w4, [sp, 8]
	ldr	w3, [x3]
	str	w3, [sp]
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x27
	ldr	w3, [x2, 4]
	ldr	w2, [x8, x25]
	add	x25, x25, 56
	bl	printk
	b	.L2153
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
	.align	2
	.global	FtlScanAllBlock
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	sub	sp, sp, #144
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC49
	add	x1, x1, 192
	add	x0, x0, :lo12:.LC49
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	mov	w20, 0
	stp	x21, x22, [sp, 48]
	stp	x23, x24, [sp, 64]
	adrp	x23, .LANCHOR2
	str	x25, [sp, 80]
	add	x21, x23, :lo12:.LANCHOR2
	bl	printk
.L2161:
	add	x19, x23, :lo12:.LANCHOR2
	ldrh	w0, [x19, 2286]
	cmp	w0, w20
	bhi	.L2169
	ldp	x19, x20, [sp, 32]
	mov	w0, 0
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x29, x30, [sp, 16]
	ldr	x25, [sp, 80]
	add	sp, sp, 144
	ret
.L2169:
	strh	w20, [x29, 80]
	mov	w0, w20
	bl	ftl_get_blk_mode
	mov	w3, w0
	ldr	x2, [x19, 2712]
	ubfiz	x1, x20, 1, 16
	adrp	x0, .LC66
	add	x0, x0, :lo12:.LC66
	ldrh	w2, [x2, x1]
	mov	w1, w20
	bl	printk
	add	x0, x29, 80
	bl	make_superblock
	add	x3, x29, 96
	ldrh	w7, [x19, 2276]
	ldrh	w8, [x19, 2354]
	mov	w0, 0
	ldrh	w9, [x19, 2356]
	mov	w10, 65535
	mov	w19, 0
	mov	w11, 56
	mov	w5, 4
.L2162:
	cmp	w0, w7
	bne	.L2164
	ldr	x0, [x21, 2520]
	mov	w24, 56
	adrp	x25, .LC67
	mov	w2, 0
	mov	w1, w19
	umull	x24, w19, w24
	mov	x22, 0
	add	x25, x25, :lo12:.LC67
	bl	FlashReadPages
.L2165:
	cmp	x22, x24
	bne	.L2166
	ldr	x0, [x21, 2520]
	mov	w1, w19
	adrp	x24, .LC68
	mov	w2, 1
	mov	x19, 0
	add	x24, x24, :lo12:.LC68
	bl	FlashReadPages
.L2167:
	cmp	x22, x19
	bne	.L2168
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L2161
.L2164:
	ldrh	w1, [x3]
	cmp	w1, w10
	beq	.L2163
	umull	x6, w19, w11
	ldr	x4, [x21, 2520]
	lsl	w1, w1, 10
	add	x4, x4, x6
	str	w1, [x4, 4]
	mul	w1, w19, w8
	ldr	x2, [x21, 2520]
	ldr	x4, [x21, 2600]
	sdiv	w1, w1, w5
	add	x2, x2, x6
	add	x1, x4, w1, sxtw 2
	str	x1, [x2, 8]
	mul	w1, w19, w9
	ldr	x4, [x21, 2656]
	add	w19, w19, 1
	and	w19, w19, 65535
	sdiv	w1, w1, w5
	add	x1, x4, w1, sxtw 2
	str	x1, [x2, 16]
.L2163:
	add	w0, w0, 1
	add	x3, x3, 2
	and	w0, w0, 65535
	b	.L2162
.L2166:
	ldr	x8, [x21, 2520]
	ldrh	w1, [x29, 80]
	add	x2, x8, x22
	ldp	x3, x0, [x2, 8]
	ldr	w4, [x3, 4]
	str	w4, [sp, 8]
	ldr	w3, [x3]
	str	w3, [sp]
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x25
	ldr	w3, [x2, 4]
	ldr	w2, [x8, x22]
	add	x22, x22, 56
	bl	printk
	b	.L2165
.L2168:
	ldr	x8, [x21, 2520]
	ldrh	w1, [x29, 80]
	add	x2, x8, x19
	ldp	x3, x0, [x2, 8]
	ldr	w4, [x3, 4]
	str	w4, [sp, 8]
	ldr	w3, [x3]
	str	w3, [sp]
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x24
	ldr	w3, [x2, 4]
	ldr	w2, [x8, x19]
	add	x19, x19, 56
	bl	printk
	b	.L2167
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	stp	x29, x30, [sp, -64]!
	mov	w0, 6
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	adrp	x20, .LANCHOR4
	add	x23, x19, 2304
	mov	w21, 0
	ldrh	w2, [x19, 2286]
	mov	w22, 0
	mov	w24, 0
	mul	w2, w2, w0
	ldr	x0, [x19, 2808]
	bl	ftl_memset
	strh	wzr, [x19, 2386]
	add	x0, x20, :lo12:.LANCHOR4
	strh	wzr, [x0, 760]
	strh	wzr, [x0, 776]
	str	xzr, [x0, 744]
	str	xzr, [x0, 752]
	str	xzr, [x0, 768]
.L2172:
	ldrh	w0, [x19, 2284]
	cmp	w24, w0
	bge	.L2179
	ldrh	w8, [x19, 2276]
	mov	w5, 0
	ldrh	w7, [x19, 2344]
	mov	w6, 0
	b	.L2180
.L2174:
	ldrb	w0, [x23, w6, sxtw]
	mov	w1, w24
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2173
	add	w5, w7, w5
	sxth	w5, w5
.L2173:
	add	w6, w6, 1
	sxth	w6, w6
.L2180:
	cmp	w6, w8
	blt	.L2174
	cbz	w5, .L2175
	mov	w0, 32768
	sdiv	w5, w0, w5
	sxth	w5, w5
.L2176:
	ldr	x1, [x19, 2808]
	mov	w0, 6
	smaddl	x0, w24, w0, x1
	strh	w5, [x0, 4]
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w1, [x0, 784]
	cmp	w24, w1
	beq	.L2177
	ldrh	w1, [x0, 832]
	cmp	w24, w1
	beq	.L2177
	ldrh	w0, [x0, 880]
	cmp	w24, w0
	beq	.L2177
	ldr	x0, [x19, 2712]
	ldrh	w0, [x0, w24, sxtw 1]
	cbnz	w0, .L2178
	add	w21, w21, 1
	mov	w0, w24
	and	w21, w21, 65535
	bl	INSERT_FREE_LIST
.L2177:
	add	w24, w24, 1
	sxth	w24, w24
	b	.L2172
.L2175:
	ldr	x0, [x19, 2712]
	mov	w1, -1
	strh	w1, [x0, w24, sxtw 1]
	b	.L2176
.L2178:
	add	w22, w22, 1
	mov	w0, w24
	and	w22, w22, 65535
	bl	INSERT_DATA_LIST
	b	.L2177
.L2179:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 0
	ldp	x23, x24, [sp, 48]
	strh	w22, [x20, 760]
	strh	w21, [x20, 776]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w3, [x0, 1152]
	cmp	w3, w1
	bne	.L2185
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 1152
	mov	w1, 99
	add	x29, sp, 0
	movk	w1, 0x5000, lsl 16
	str	w1, [x2, 4]
	ldr	w1, [x0, 1664]
	ldr	w0, [x0, 1668]
	stp	w1, w0, [x2, 88]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w1, [x0, 2404]
	str	w1, [x2, 8]
	ldr	w1, [x0, 2408]
	str	w1, [x2, 12]
	ldr	w1, [x0, 2424]
	str	w1, [x2, 16]
	ldr	w1, [x0, 2420]
	str	w1, [x2, 20]
	ldr	w1, [x0, 2432]
	str	w1, [x2, 28]
	ldr	w1, [x0, 2436]
	str	w1, [x2, 32]
	ldr	w1, [x0, 2412]
	str	w1, [x2, 36]
	ldr	w1, [x0, 2416]
	str	w1, [x2, 40]
	ldr	w1, [x0, 2440]
	str	w1, [x2, 44]
	ldr	w1, [x0, 2444]
	str	w1, [x2, 48]
	ldr	w1, [x0, 2392]
	ldr	w0, [x0, 2388]
	stp	w1, w0, [x2, 60]
	mov	w1, 1
	mov	w0, 0
	bl	FtlVendorPartWrite
	ldp	x29, x30, [sp], 16
	ret
.L2185:
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x1, .LANCHOR2
	add	x2, x1, :lo12:.LANCHOR2
	ldr	w3, [x2, 2096]
	cbz	w3, .L2193
	ldr	w2, [x2, 2444]
	mov	w3, 4
	cmp	w2, 39
	mov	w2, 32
	csel	w2, w2, w3, hi
.L2189:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3, 1672]
	cmp	w4, 31
	bhi	.L2190
	add	w4, w4, 1
	mov	w2, 1
	strh	w4, [x3, 1672]
.L2190:
	cbnz	w0, .L2191
	add	x0, x1, :lo12:.LANCHOR2
	ldr	x0, [x0, 2688]
	ldr	w3, [x0, 20]
	ldr	w0, [x0, 16]
	add	w2, w2, w3
	cmp	w0, w2
	bcc	.L2196
.L2191:
	add	x0, x1, :lo12:.LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x0, 2688]
	ldr	w2, [x1, 16]
	str	w2, [x1, 20]
	mov	w2, 17221
	movk	w2, 0x4254, lsl 16
	str	w2, [x1]
	ldr	x2, [x0, 2688]
	ldrh	w1, [x0, 2672]
	lsl	w3, w1, 9
	str	wzr, [x2, 4]
	str	w3, [x2, 12]
	ldr	w3, [x2, 8]
	add	w3, w3, 1
	str	w3, [x2, 8]
	ldr	x2, [x0, 2688]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L2193:
	mov	w2, 32
	b	.L2189
.L2196:
	mov	w0, 0
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
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR4
	add	x20, x19, :lo12:.LANCHOR4
	stp	x21, x22, [sp, 32]
	add	x22, x20, 1152
	mov	w21, 19539
	mov	x2, x22
	bl	FtlVendorPartRead
	ldr	w0, [x20, 1152]
	movk	w21, 0x4654, lsl 16
	cmp	w0, w21
	beq	.L2199
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	str	w21, [x20, 1152]
.L2199:
	add	x1, x19, :lo12:.LANCHOR4
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	add	x0, x1, 1152
	adrp	x20, .LANCHOR2
	ldr	w3, [x1, 1152]
	cmp	w3, w2
	bne	.L2200
	ldr	w2, [x1, 1240]
	str	w2, [x1, 1664]
	ldr	w2, [x1, 1244]
	str	w2, [x1, 1668]
	add	x1, x20, :lo12:.LANCHOR2
	ldr	w2, [x0, 8]
	str	w2, [x1, 2404]
	ldr	w2, [x0, 12]
	str	w2, [x1, 2408]
	ldr	w2, [x0, 16]
	str	w2, [x1, 2424]
	ldr	w2, [x0, 20]
	str	w2, [x1, 2420]
	ldr	w2, [x0, 28]
	str	w2, [x1, 2432]
	ldr	w2, [x0, 32]
	str	w2, [x1, 2436]
	ldr	w2, [x0, 36]
	str	w2, [x1, 2412]
	ldr	w2, [x0, 40]
	str	w2, [x1, 2416]
	ldr	w2, [x0, 44]
	str	w2, [x1, 2440]
	ldr	w2, [x0, 48]
	ldr	w0, [x0, 60]
	str	w0, [x1, 2392]
	str	w2, [x1, 2444]
.L2200:
	add	x0, x19, :lo12:.LANCHOR4
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, 34661
	add	x0, x0, 1152
	movk	w2, 0x1234, lsl 16
	ldr	w3, [x0, 68]
	str	wzr, [x1, 2388]
	cmp	w3, w2
	bne	.L2201
	ldrb	w2, [x1, 1220]
	cbz	w2, .L2202
	str	wzr, [x0, 68]
	bl	Ftl_save_ext_data
.L2201:
	add	x0, x20, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR4
	ldp	x21, x22, [sp, 32]
	ldrh	w1, [x0, 2336]
	ldr	w3, [x0, 2428]
	ldr	w2, [x0, 2432]
	ldrh	w0, [x0, 2284]
	madd	w1, w1, w3, w2
	udiv	w0, w1, w0
	str	w0, [x19, 1676]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L2202:
	mov	w0, 1
	str	w0, [x1, 2096]
	adrp	x1, .LC69
	adrp	x0, .LC48
	add	x1, x1, :lo12:.LC69
	add	x0, x0, :lo12:.LC48
	bl	printk
	b	.L2201
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	sub	sp, sp, #112
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 64]
	stp	x25, x26, [sp, 80]
	add	x20, x19, 2936
	adrp	x26, .LC70
	add	x26, x26, :lo12:.LC70
	ldrh	w23, [x19, 2342]
	stp	x21, x22, [sp, 48]
	sub	w23, w23, #16
	str	x27, [sp, 96]
	lsl	w25, w23, 10
.L2205:
	ldrh	w0, [x19, 2342]
	cmp	w23, w0
	blt	.L2213
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 208
	adrp	x0, .LC73
	add	x0, x0, :lo12:.LC73
	bl	printk
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x29, x30, [sp, 16]
	ldr	x27, [sp, 96]
	add	sp, sp, 112
	ret
.L2213:
	and	w22, w23, 65535
	mov	w0, w22
	bl	ftl_get_blk_mode
	ldrb	w1, [x19, 1220]
	cbz	w1, .L2206
	ldrh	w1, [x19, 2284]
	cmp	w23, w1
	bge	.L2207
	ldrh	w1, [x19, 2358]
	cmp	w23, w1
	blt	.L2207
.L2206:
	cmp	w0, 1
	bne	.L2208
.L2207:
	ldrh	w24, [x19, 2346]
	mov	w21, -2147483648
.L2209:
	mov	w3, w21
	mov	w2, w24
	mov	w1, w23
	mov	x0, x26
	bl	printk
	mov	w0, w22
	bl	FtlBbmIsBadBlock
	cbz	w0, .L2210
	adrp	x0, .LC71
	mov	w3, w21
	mov	w2, w24
	mov	w1, w23
	add	x0, x0, :lo12:.LC71
	bl	printk
.L2210:
	adrp	x27, .LC72
	add	w21, w21, w25
	add	x27, x27, :lo12:.LC72
	mov	w22, 0
.L2211:
	cmp	w22, w24
	bne	.L2212
	add	w23, w23, 1
	add	w25, w25, 1024
	b	.L2205
.L2208:
	ldrh	w24, [x19, 2344]
	mov	w21, 0
	b	.L2209
.L2212:
	add	w0, w21, w22
	stp	wzr, w0, [x20]
	ldr	x0, [x19, 2576]
	mov	w2, 0
	str	x0, [x20, 8]
	mov	w1, 1
	ldr	x0, [x19, 2640]
	add	w22, w22, 1
	str	x0, [x20, 16]
	mov	x0, x20
	bl	FlashReadPages
	ldp	x1, x0, [x20, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	mov	x0, x27
	ldr	w7, [x1]
	ldp	w2, w1, [x20]
	bl	printk
	b	.L2211
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
	and	w8, w0, 65535
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	mov	x9, 0
	mov	w6, 0
	mov	x5, x0
	add	x7, x1, 2304
	ldrh	w10, [x1, 2276]
	ldrh	w11, [x1, 2344]
	cmp	w10, w9, uxth
	bhi	.L2233
	cbz	w6, .L2231
	mov	w0, 32768
	sdiv	w6, w0, w6
.L2232:
	add	x0, x5, :lo12:.LANCHOR2
	mov	w1, 6
	ldr	x0, [x0, 2808]
	umaddl	x8, w8, w1, x0
	mov	w0, 0
	strh	w6, [x8, 4]
	ret
.L2226:
	mov	w6, 0
	b	.L2225
.L2231:
	mov	w6, 0
	b	.L2232
.L2233:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L2224:
	ldrb	w0, [x7, x9]
	mov	w1, w8
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2223
	add	w6, w6, w11
	and	w6, w6, 65535
.L2223:
	add	x9, x9, 1
	cmp	w10, w9, uxth
	bhi	.L2224
	cbz	w6, .L2226
	mov	w0, 32768
	sdiv	w6, w0, w6
.L2225:
	add	x0, x5, :lo12:.LANCHOR2
	mov	w1, 6
	ldr	x0, [x0, 2808]
	umaddl	x8, w8, w1, x0
	mov	w0, 0
	strh	w6, [x8, 4]
	ldp	x29, x30, [sp], 16
	ret
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	adrp	x0, .LANCHOR4+768
	ldr	x0, [x0, #:lo12:.LANCHOR4+768]
	cbz	x0, .L2236
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	ldr	x2, [x1, 2808]
	ldr	x1, [x1, 2696]
	sub	x0, x0, x2
	mov	x2, -6148914691236517206
	asr	x0, x0, 1
	movk	x2, 0xaaab, lsl 0
	mul	x0, x0, x2
	and	x0, x0, 65535
	ldrh	w0, [x1, x0, lsl 1]
	ret
.L2236:
	mov	w0, 0
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	and	w0, w0, 65535
	ldr	x1, [x2, 768]
	cbz	x1, .L2244
	ldrh	w2, [x2, 776]
	mov	w3, 7
	mov	w6, 6
	mov	w7, 65535
	mul	w2, w2, w3
	asr	w2, w2, 3
	cmp	w0, w2
	csel	w0, w2, w0, gt
	adrp	x2, .LANCHOR2
	add	x3, x2, :lo12:.LANCHOR2
	ldr	x4, [x3, 2808]
	mov	x3, -6148914691236517206
	movk	x3, 0xaaab, lsl 0
	sub	x1, x1, x4
	asr	x1, x1, 1
	mul	x1, x1, x3
	mov	w3, 0
	and	w1, w1, 65535
.L2240:
	cmp	w0, w3
	beq	.L2243
	umull	x5, w1, w6
	ldrh	w5, [x4, x5]
	cmp	w5, w7
	bne	.L2241
.L2243:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2, 2696]
	ldrh	w0, [x0, x1]
	ret
.L2241:
	add	w3, w3, 1
	mov	w1, w5
	and	w3, w3, 65535
	b	.L2240
.L2244:
	mov	w0, 0
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC74
	add	x1, x1, :lo12:.LC74
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x22, x23, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	mov	x24, x0
	stp	x25, x26, [sp, 64]
	add	x20, x24, 12
	stp	x27, x28, [sp, 80]
	bl	strcpy
	ldr	w2, [x22, 1192]
	mov	x0, x20
	adrp	x1, .LC75
	add	x1, x1, :lo12:.LC75
	bl	sprintf
	add	x20, x20, w0, sxtw
	ldr	w2, [x22, 2360]
	mov	x0, x20
	adrp	x1, .LC76
	add	x1, x1, :lo12:.LC76
	bl	sprintf
	add	x20, x20, w0, sxtw
	adrp	x0, .LANCHOR1+3448
	ldr	w0, [x0, #:lo12:.LANCHOR1+3448]
	cmp	w0, 1
	beq	.L2246
	sub	w0, w20, w24
.L2245:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2246:
	add	x3, x29, 108
	add	x2, x29, 104
	add	x1, x29, 100
	add	x0, x29, 96
	bl	NandcGetTimeCfg
	adrp	x26, .LANCHOR4
	ldp	w4, w5, [x29, 104]
	adrp	x1, .LC77
	ldp	w2, w3, [x29, 96]
	add	x1, x1, :lo12:.LC77
	mov	x0, x20
	add	x25, x26, :lo12:.LANCHOR4
	bl	sprintf
	add	x21, x20, w0, sxtw
	mov	x0, x21
	adrp	x1, .LC78
	add	x1, x1, :lo12:.LC78
	bl	strcpy
	ldr	w2, [x22, 2924]
	add	x21, x21, 10
	mov	x0, x21
	adrp	x1, .LC79
	add	x1, x1, :lo12:.LC79
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x25, 1068]
	mov	x0, x21
	adrp	x1, .LC80
	add	x1, x1, :lo12:.LC80
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2424]
	mov	x0, x21
	adrp	x1, .LC81
	add	x1, x1, :lo12:.LC81
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2412]
	mov	x0, x21
	adrp	x1, .LC82
	add	x1, x1, :lo12:.LC82
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2408]
	mov	x0, x21
	adrp	x1, .LC83
	add	x1, x1, :lo12:.LC83
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2416]
	mov	x0, x21
	adrp	x1, .LC84
	add	x1, x1, :lo12:.LC84
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2420]
	mov	x0, x21
	adrp	x1, .LC85
	add	x1, x1, :lo12:.LC85
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2404]
	mov	x0, x21
	adrp	x1, .LC86
	add	x1, x1, :lo12:.LC86
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x25, 1664]
	mov	x0, x21
	adrp	x1, .LC87
	add	x1, x1, :lo12:.LC87
	lsr	w2, w2, 11
	bl	sprintf
	ldr	w2, [x25, 1668]
	add	x21, x21, w0, sxtw
	mov	x0, x21
	adrp	x1, .LC88
	add	x1, x1, :lo12:.LC88
	lsr	w2, w2, 11
	bl	sprintf
	ldr	w2, [x22, 2396]
	add	x21, x21, w0, sxtw
	mov	x0, x21
	adrp	x1, .LC89
	add	x1, x1, :lo12:.LC89
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2400]
	adrp	x1, .LC90
	add	x1, x1, :lo12:.LC90
	mov	x0, x21
	bl	sprintf
	add	x21, x21, w0, sxtw
	bl	FtlBbtCalcTotleCnt
	and	w3, w0, 65535
	ldrh	w2, [x22, 2830]
	mov	x0, x21
	adrp	x1, .LC91
	add	x1, x1, :lo12:.LC91
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 776]
	mov	x0, x21
	adrp	x1, .LC92
	add	x1, x1, :lo12:.LC92
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2428]
	mov	x0, x21
	adrp	x1, .LC93
	add	x1, x1, :lo12:.LC93
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2432]
	mov	x0, x21
	adrp	x1, .LC94
	add	x1, x1, :lo12:.LC94
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x25, 1676]
	mov	x0, x21
	adrp	x1, .LC95
	add	x1, x1, :lo12:.LC95
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2436]
	mov	x0, x21
	adrp	x1, .LC96
	add	x1, x1, :lo12:.LC96
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2440]
	mov	x0, x21
	adrp	x1, .LC97
	add	x1, x1, :lo12:.LC97
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2444]
	mov	x0, x21
	adrp	x1, .LC98
	add	x1, x1, :lo12:.LC98
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 726]
	mov	x0, x21
	adrp	x1, .LC99
	add	x1, x1, :lo12:.LC99
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 724]
	mov	x0, x21
	adrp	x1, .LC100
	add	x1, x1, :lo12:.LC100
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 1224]
	mov	x0, x21
	adrp	x1, .LC101
	add	x1, x1, :lo12:.LC101
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2376]
	mov	x0, x21
	adrp	x1, .LC102
	add	x1, x1, :lo12:.LC102
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2272]
	mov	x0, x21
	adrp	x1, .LC103
	add	x1, x1, :lo12:.LC103
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x22, 2998]
	mov	x0, x21
	adrp	x1, .LC104
	add	x1, x1, :lo12:.LC104
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x22, 2284]
	mov	x0, x21
	adrp	x1, .LC105
	add	x1, x1, :lo12:.LC105
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 1096]
	mov	x0, x21
	adrp	x1, .LC106
	add	x1, x1, :lo12:.LC106
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x22, 2288]
	mov	x0, x21
	adrp	x1, .LC107
	add	x1, x1, :lo12:.LC107
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 1072]
	mov	x0, x21
	adrp	x1, .LC108
	add	x1, x1, :lo12:.LC108
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x22, 2824]
	mov	x0, x21
	adrp	x1, .LC109
	add	x1, x1, :lo12:.LC109
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 786]
	mov	x0, x21
	adrp	x1, .LC110
	add	x1, x1, :lo12:.LC110
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x25, 790]
	mov	x0, x21
	adrp	x1, .LC111
	add	x1, x1, :lo12:.LC111
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 784]
	mov	x0, x21
	adrp	x1, .LC112
	add	x1, x1, :lo12:.LC112
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x25, 792]
	mov	x0, x21
	adrp	x1, .LC113
	add	x1, x1, :lo12:.LC113
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 788]
	mov	x0, x21
	adrp	x1, .LC114
	add	x1, x1, :lo12:.LC114
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w1, [x25, 784]
	ldr	x0, [x22, 2712]
	ldrh	w2, [x0, x1, lsl 1]
	mov	x0, x21
	adrp	x1, .LC115
	add	x1, x1, :lo12:.LC115
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 834]
	mov	x0, x21
	adrp	x1, .LC116
	add	x1, x1, :lo12:.LC116
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x25, 838]
	mov	x0, x21
	adrp	x1, .LC117
	add	x1, x1, :lo12:.LC117
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 832]
	mov	x0, x21
	adrp	x1, .LC118
	add	x1, x1, :lo12:.LC118
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x25, 840]
	mov	x0, x21
	adrp	x1, .LC119
	add	x1, x1, :lo12:.LC119
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 836]
	mov	x0, x21
	adrp	x1, .LC120
	add	x1, x1, :lo12:.LC120
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w1, [x25, 832]
	ldr	x0, [x22, 2712]
	ldrh	w2, [x0, x1, lsl 1]
	mov	x0, x21
	adrp	x1, .LC121
	add	x1, x1, :lo12:.LC121
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 882]
	mov	x0, x21
	adrp	x1, .LC122
	add	x1, x1, :lo12:.LC122
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x25, 886]
	mov	x0, x21
	adrp	x1, .LC123
	add	x1, x1, :lo12:.LC123
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 880]
	mov	x0, x21
	adrp	x1, .LC124
	add	x1, x1, :lo12:.LC124
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x25, 888]
	mov	x0, x21
	adrp	x1, .LC125
	add	x1, x1, :lo12:.LC125
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 884]
	mov	x0, x21
	adrp	x1, .LC126
	add	x1, x1, :lo12:.LC126
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 1106]
	mov	x0, x21
	adrp	x1, .LC127
	add	x1, x1, :lo12:.LC127
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x25, 1110]
	mov	x0, x21
	adrp	x1, .LC128
	add	x1, x1, :lo12:.LC128
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 1104]
	mov	x0, x21
	adrp	x1, .LC129
	add	x1, x1, :lo12:.LC129
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x25, 1112]
	mov	x0, x21
	adrp	x1, .LC130
	add	x1, x1, :lo12:.LC130
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x25, 1108]
	mov	x0, x21
	adrp	x1, .LC131
	add	x1, x1, :lo12:.LC131
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w5, [x25, 1228]
	adrp	x1, .LC132
	ldr	w3, [x25, 1232]
	add	x1, x1, :lo12:.LC132
	ldr	w4, [x25, 1236]
	ldr	w0, [x22, 2096]
	ldr	w2, [x22, 2392]
	orr	w2, w0, w2, lsl 8
	mov	x0, x21
	bl	sprintf
	add	x19, x21, w0, sxtw
	ldr	w2, [x25, 1224]
	mov	x0, x19
	adrp	x1, .LC133
	add	x1, x1, :lo12:.LC133
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w2, [x25, 1248]
	mov	x0, x19
	adrp	x1, .LC134
	add	x1, x1, :lo12:.LC134
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x22, 2480]
	mov	x0, x19
	adrp	x1, .LC135
	add	x1, x1, :lo12:.LC135
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x22, 2482]
	mov	x0, x19
	adrp	x1, .LC136
	add	x1, x1, :lo12:.LC136
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w2, [x22, 2464]
	mov	x0, x19
	adrp	x1, .LC137
	add	x1, x1, :lo12:.LC137
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x22, 2484]
	adrp	x1, .LC138
	add	x1, x1, :lo12:.LC138
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	bl	GetFreeBlockMinEraseCount
	and	w2, w0, 65535
	adrp	x1, .LC139
	mov	x0, x19
	add	x1, x1, :lo12:.LC139
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w0, [x25, 776]
	bl	GetFreeBlockMaxEraseCount
	and	w2, w0, 65535
	adrp	x1, .LC140
	mov	x0, x19
	add	x1, x1, :lo12:.LC140
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w0, [x25, 1104]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2248
	ldr	x1, [x22, 2712]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x1, x0]
	mov	x0, x19
	adrp	x1, .LC141
	add	x1, x1, :lo12:.LC141
	bl	sprintf
	add	x19, x19, w0, sxtw
.L2248:
	mov	w0, 0
	adrp	x22, .LC142
	bl	List_get_gc_head_node
	add	x21, x23, :lo12:.LANCHOR2
	and	w3, w0, 65535
	add	x22, x22, :lo12:.LC142
	mov	w20, 0
	mov	w28, 65535
	mov	w27, 6
.L2250:
	cmp	w3, w28
	beq	.L2249
	umull	x25, w3, w27
	ldr	x0, [x21, 2808]
	ldr	x4, [x21, 2696]
	ubfiz	x1, x3, 1, 16
	ldr	x2, [x21, 2712]
	add	x0, x0, x25
	ldrh	w6, [x4, x1]
	ldrh	w5, [x0, 4]
	mov	x0, x19
	ldrh	w4, [x2, x1]
	mov	w2, w20
	mov	x1, x22
	add	w20, w20, 1
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 2808]
	cmp	w20, 16
	ldrh	w3, [x0, x25]
	bne	.L2250
.L2249:
	add	x26, x26, :lo12:.LANCHOR4
	add	x23, x23, :lo12:.LANCHOR2
	adrp	x21, .LC143
	add	x21, x21, :lo12:.LC143
	mov	w20, 0
	mov	w25, 65535
	ldr	x0, [x23, 2808]
	ldr	x3, [x26, 768]
	mov	w26, 6
	sub	x3, x3, x0
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	movk	x0, 0xaaab, lsl 0
	mul	x3, x3, x0
	and	w3, w3, 65535
.L2252:
	cmp	w3, w25
	beq	.L2251
	umull	x22, w3, w26
	ldr	x0, [x23, 2808]
	ldr	x2, [x23, 2696]
	ubfiz	x1, x3, 1, 16
	add	x0, x0, x22
	ldrh	w5, [x2, x1]
	mov	w2, w20
	ldrh	w4, [x0, 4]
	mov	x1, x21
	mov	x0, x19
	add	w20, w20, 1
	bl	sprintf
	add	x19, x19, w0, sxtw
	cmp	w20, 4
	beq	.L2251
	ldr	x0, [x23, 2808]
	ldrh	w3, [x0, x22]
	b	.L2252
.L2251:
	sub	w0, w19, w24
	b	.L2245
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	ftl_proc_ftl_read
	.type	ftl_proc_ftl_read, %function
ftl_proc_ftl_read:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LC144
	adrp	x1, .LC48
	add	x2, x2, :lo12:.LC144
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC48
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	bl	sprintf
	add	x19, x20, w0, sxtw
	mov	x0, x19
	bl	FtlPrintInfo2buf
	add	x0, x19, w0, sxtw
	sub	w0, w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_proc_ftl_read, .-ftl_proc_ftl_read
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	stp	x29, x30, [sp, -32]!
	adrp	x9, .LANCHOR4
	add	x0, x9, :lo12:.LANCHOR4
	adrp	x8, .LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	ldr	w2, [x0, 1676]
	add	x0, x8, :lo12:.LANCHOR2
	ldr	w1, [x0, 2444]
	cmp	w2, w1
	bcs	.L2261
	ldrh	w2, [x0, 2284]
	mov	x1, 0
	ldr	x4, [x0, 2696]
	str	wzr, [x0, 2428]
.L2262:
	cmp	w2, w1
	bhi	.L2263
	add	x1, x8, :lo12:.LANCHOR2
	add	x3, x9, :lo12:.LANCHOR4
	ldr	w0, [x1, 2428]
	udiv	w2, w0, w2
	str	w2, [x3, 1676]
	ldr	w2, [x1, 2432]
	sub	w0, w0, w2
	ldrh	w2, [x1, 2336]
	udiv	w0, w0, w2
	str	w0, [x1, 2428]
.L2264:
	add	x0, x8, :lo12:.LANCHOR2
	add	x1, x9, :lo12:.LANCHOR4
	ldr	w10, [x0, 2444]
	ldr	w11, [x1, 1676]
	add	w1, w10, 256
	cmp	w1, w11
	bls	.L2269
	ldr	w2, [x0, 2440]
	add	w1, w10, 768
	cmp	w1, w2
	bls	.L2269
	ldr	w0, [x0, 2096]
	cbnz	w0, .L2270
.L2272:
	mov	w20, 65535
.L2271:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2263:
	ldrh	w3, [x4, x1, lsl 1]
	add	x1, x1, 1
	ldr	w5, [x0, 2428]
	add	w3, w3, w5
	str	w3, [x0, 2428]
	b	.L2262
.L2261:
	ldr	w1, [x0, 2440]
	cmp	w2, w1
	bls	.L2264
	add	w1, w1, 1
	str	w1, [x0, 2440]
	mov	w1, 0
.L2266:
	ldrh	w2, [x0, 2284]
	cmp	w1, w2
	bcs	.L2264
	ldr	x4, [x0, 2696]
	ubfiz	x3, x1, 1, 32
	add	w1, w1, 1
	ldrh	w2, [x4, x3]
	add	w2, w2, 1
	strh	w2, [x4, x3]
	b	.L2266
.L2270:
	cmp	w10, 40
	bhi	.L2272
.L2269:
	add	x0, x9, :lo12:.LANCHOR4
	ldrh	w0, [x0, 776]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	and	w6, w0, 65535
	add	w0, w10, 64
	cmp	w6, w0
	bcs	.L2273
	cmp	w10, 40
	bhi	.L2272
.L2273:
	add	x9, x9, :lo12:.LANCHOR4
	ldr	x0, [x9, 744]
	cbz	x0, .L2272
	add	x1, x8, :lo12:.LANCHOR2
	mov	w4, 65535
	mov	x13, -6148914691236517206
	mov	w2, w4
	mov	w9, w4
	movk	x13, 0xaaab, lsl 0
	ldrh	w12, [x1, 2284]
	mov	w14, 6
	ldr	x5, [x1, 2696]
	ldr	x7, [x1, 2808]
	mov	w1, 0
.L2274:
	ldrh	w3, [x0]
	cmp	w3, w9
	bne	.L2277
	mov	w20, w2
.L2276:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2272
	ubfiz	x7, x20, 1, 32
	ldrh	w3, [x5, x7]
	cmp	w10, w3
	bcs	.L2278
	bl	GetFreeBlockMinEraseCount
	cmp	w10, w0, uxth
	bcs	.L2278
	add	x0, x8, :lo12:.LANCHOR2
	str	w4, [x0, 2444]
.L2278:
	cmp	w11, w3
	bls	.L2272
	add	w0, w3, 128
	cmp	w6, w0
	ble	.L2272
	add	w0, w3, 256
	cmp	w11, w0
	bhi	.L2279
	add	x0, x8, :lo12:.LANCHOR2
	add	w3, w3, 768
	ldr	w0, [x0, 2440]
	cmp	w3, w0
	bcs	.L2272
.L2279:
	add	x19, x8, :lo12:.LANCHOR2
	ldrh	w5, [x5, x7]
	mov	w2, w11
	mov	w1, w20
	ldr	x0, [x19, 2712]
	ldr	w3, [x19, 2440]
	ldrh	w4, [x0, x7]
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	bl	printk
	mov	w0, 1
	str	w0, [x19, 2452]
	b	.L2271
.L2277:
	add	w1, w1, 1
	and	w1, w1, 65535
	cmp	w1, w12
	bhi	.L2272
	ldrh	w15, [x0, 4]
	cbz	w15, .L2275
	sub	x0, x0, x7
	asr	x0, x0, 1
	mul	x0, x0, x13
	and	w20, w0, 65535
	and	x0, x0, 65535
	ldrh	w0, [x5, x0, lsl 1]
	cmp	w10, w0
	bcs	.L2276
	cmp	w4, w0
	bls	.L2275
	mov	w4, w0
	mov	w2, w20
.L2275:
	umaddl	x0, w3, w14, x7
	b	.L2274
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
	and	w0, w0, 65535
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2290
	stp	x29, x30, [sp, -16]!
	adrp	x2, .LANCHOR2+2712
	ubfiz	x1, x0, 1, 16
	add	x29, sp, 0
	ldr	x2, [x2, #:lo12:.LANCHOR2+2712]
	strh	wzr, [x2, x1]
	bl	INSERT_FREE_LIST
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L2290:
	mov	w0, 0
	ret
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x19, x21, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w1, [x19, 2928]
	cbnz	w1, .L2294
	adrp	x23, .LANCHOR4
	add	x22, x23, :lo12:.LANCHOR4
	mov	x20, x0
	add	x24, x22, 768
.L2295:
	add	x0, x23, :lo12:.LANCHOR4
	add	x1, x0, 880
	cmp	x20, x1
	bne	.L2296
	ldrh	w2, [x0, 776]
	ldr	w3, [x19, 2452]
	lsr	w0, w2, 1
	add	w4, w0, 1
	mul	w1, w2, w3
	add	w1, w4, w1, lsr 2
	ldr	w4, [x19, 2096]
	and	w1, w1, 65535
	cbz	w4, .L2297
	ldr	w4, [x19, 2444]
	cmp	w4, 39
	bhi	.L2297
	cmp	w4, 2
	bls	.L2324
	tbz	x2, 0, .L2320
	cbz	w3, .L2324
.L2320:
	mov	w1, w0
	b	.L2297
.L2296:
	ldrb	w1, [x20, 8]
	cmp	w1, 1
	bne	.L2324
	ldrh	w1, [x19, 2296]
	cmp	w1, 1
	beq	.L2324
	ldrb	w1, [x19, 1220]
	cbnz	w1, .L2324
	ldr	w2, [x19, 2096]
	ldrh	w0, [x0, 776]
	lsr	w1, w0, 3
	cbz	w2, .L2297
	ldr	w2, [x19, 2444]
	cmp	w2, 1
	bhi	.L2297
	mov	w1, 7
	mul	w1, w0, w1
	lsr	w1, w1, 3
.L2297:
	cbz	w1, .L2298
	sub	w1, w1, #1
	and	w1, w1, 65535
.L2298:
	ldrb	w2, [x20, 8]
	mov	x0, x24
	bl	List_pop_index_node
	and	w27, w0, 65535
	ldrh	w0, [x22, 776]
	sub	w0, w0, #1
	strh	w0, [x22, 776]
	ldrh	w0, [x19, 2284]
	cmp	w0, w27
	bls	.L2295
	ldr	x0, [x19, 2712]
	ubfiz	x26, x27, 1, 16
	ldrh	w25, [x0, x26]
	cbnz	w25, .L2295
	strh	w27, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20, 7]
	cbz	w0, .L2341
	add	x28, x20, 16
	ldrh	w5, [x19, 2276]
	mov	x3, x28
	mov	x0, 0
	mov	x4, 56
	mov	w6, 65535
.L2301:
	cmp	w5, w0, uxth
	bhi	.L2303
	ldr	w0, [x19, 2096]
	cbz	w0, .L2304
	add	x0, x22, 784
	cmp	x20, x0
	bne	.L2304
	ldr	x0, [x19, 2696]
	ldrh	w0, [x0, x26]
	cmp	w0, 40
	bls	.L2304
	strb	wzr, [x20, 8]
.L2304:
	ldrb	w0, [x20, 8]
	ldr	x1, [x19, 2696]
	cbnz	w0, .L2305
	ldrh	w0, [x1, x26]
	cbz	w0, .L2306
	ldrh	w2, [x19, 2336]
	add	w0, w0, w2
.L2342:
	strh	w0, [x1, x26]
	mov	w1, 0
	ldr	w0, [x19, 2428]
	add	w0, w0, 1
	str	w0, [x19, 2428]
	mov	w0, w27
	bl	ftl_set_blk_mode
.L2308:
	ldr	x0, [x19, 2696]
	ldr	w1, [x19, 2440]
	ldrh	w0, [x0, x26]
	cmp	w0, w1
	bls	.L2309
	str	w0, [x19, 2440]
.L2309:
	ldr	w2, [x19, 2428]
	ldr	w1, [x19, 2432]
	ldrh	w0, [x19, 2336]
	madd	w0, w0, w2, w1
	ldrh	w1, [x19, 2284]
	mov	w2, 56
	umull	x2, w25, w2
	udiv	w0, w0, w1
	ldr	x1, [x19, 2688]
	str	w0, [x22, 1676]
	ldr	w0, [x1, 16]
	add	w0, w0, 1
	str	w0, [x1, 16]
	mov	x0, 0
.L2310:
	cmp	x2, x0
	bne	.L2311
	ldrb	w0, [x19, 1220]
	cbz	w0, .L2312
	ldrb	w0, [x20, 8]
	mov	w2, w25
	cmp	w0, 1
	bne	.L2313
	mov	w1, 0
.L2343:
	ldr	x0, [x19, 2544]
	bl	FlashEraseBlocks
.L2312:
	ldrb	w1, [x20, 8]
	mov	w2, w25
	ldr	x0, [x19, 2544]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	w2, 0
	mov	x4, 56
.L2314:
	cmp	w25, w1, uxth
	bhi	.L2316
	cmp	w2, 0
	ble	.L2317
	mov	w0, w27
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2317:
	ldrb	w0, [x20, 7]
	cbnz	w0, .L2318
.L2341:
	ldr	x0, [x19, 2712]
	mov	w1, -1
	strh	w1, [x0, x26]
	b	.L2295
.L2324:
	mov	w1, 0
	b	.L2298
.L2303:
	ldr	x1, [x19, 2544]
	madd	x2, x0, x4, x1
	stp	xzr, xzr, [x2, 8]
	ldrh	w2, [x3]
	cmp	w2, w6
	beq	.L2302
	umull	x7, w25, w4
	add	w25, w25, 1
	and	w25, w25, 65535
	lsl	w2, w2, 10
	add	x1, x1, x7
	str	w2, [x1, 4]
.L2302:
	add	x0, x0, 1
	add	x3, x3, 2
	b	.L2301
.L2306:
	mov	w0, 2
	b	.L2342
.L2305:
	ldrh	w0, [x1, x26]
	add	w0, w0, 1
	strh	w0, [x1, x26]
	ldr	w0, [x19, 2432]
	add	w0, w0, 1
	str	w0, [x19, 2432]
	mov	w0, w27
	bl	ftl_set_blk_mode.part.17
	b	.L2308
.L2311:
	ldr	x1, [x19, 2544]
	add	x1, x1, x0
	add	x0, x0, 56
	ldr	w3, [x1, 4]
	and	w3, w3, -1024
	str	w3, [x1, 4]
	b	.L2310
.L2313:
	mov	w1, 1
	b	.L2343
.L2316:
	mul	x0, x1, x4
	ldr	x3, [x19, 2544]
	add	x5, x3, x0
	ldr	w3, [x3, x0]
	cmn	w3, #1
	bne	.L2315
	add	w2, w2, 1
	ldr	w0, [x5, 4]
	stp	x4, x1, [x29, 104]
	stp	w3, w2, [x29, 120]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldp	w3, w2, [x29, 120]
	strh	w3, [x28]
	ldp	x4, x1, [x29, 104]
	ldrb	w0, [x20, 7]
	sub	w0, w0, #1
	strb	w0, [x20, 7]
.L2315:
	add	x1, x1, 1
	add	x28, x28, 2
	b	.L2314
.L2318:
	add	x21, x21, :lo12:.LANCHOR2
	strh	wzr, [x20, 2]
	strb	wzr, [x20, 6]
	ldrh	w1, [x21, 2344]
	strh	w27, [x20]
	mul	w0, w0, w1
	ldr	w1, [x21, 2396]
	str	w1, [x20, 12]
	and	w0, w0, 65535
	add	w1, w1, 1
	strh	w0, [x20, 4]
	str	w1, [x21, 2396]
	ldr	x1, [x21, 2712]
	ldrh	w2, [x20]
	strh	w0, [x1, x2, lsl 1]
.L2294:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	adrp	x0, .LANCHOR4+1680
	mov	w2, 0
	mov	w8, 24
	mov	w9, 1
	str	wzr, [x0, #:lo12:.LANCHOR4+1680]
	adrp	x0, .LANCHOR2
	add	x3, x0, :lo12:.LANCHOR2
	mov	w5, 4
	mov	w10, 56
.L2345:
	ldrh	w1, [x3, 2276]
	cmp	w2, w1
	bcc	.L2346
	add	x0, x0, :lo12:.LANCHOR2
	mov	w6, 24
	mov	w5, 4
.L2347:
	ldr	w2, [x0, 2568]
	cmp	w1, w2
	bcc	.L2348
	ret
.L2346:
	umull	x4, w2, w8
	ldr	x6, [x3, 2632]
	ldr	x7, [x3, 2600]
	add	x1, x6, x4
	str	w9, [x1, 16]
	ldrh	w1, [x3, 2354]
	mul	w1, w1, w2
	sdiv	w1, w1, w5
	add	x1, x7, w1, sxtw 2
	str	x1, [x6, x4]
	ldrh	w1, [x3, 2356]
	ldr	x7, [x3, 2632]
	ldr	x11, [x3, 2656]
	add	x6, x7, x4
	mul	w1, w1, w2
	sdiv	w1, w1, w5
	add	x1, x11, w1, sxtw 2
	str	x1, [x6, 8]
	ldr	x1, [x3, 2552]
	ldr	x4, [x7, x4]
	nop // between mem op and mult-accumulate
	umaddl	x1, w2, w10, x1
	add	w2, w2, 1
	and	w2, w2, 65535
	str	x4, [x1, 8]
	ldr	x4, [x6, 8]
	str	x4, [x1, 16]
	b	.L2345
.L2348:
	umull	x4, w1, w6
	ldr	x3, [x0, 2632]
	ldr	x7, [x0, 2600]
	add	x2, x3, x4
	str	wzr, [x2, 16]
	ldrh	w2, [x0, 2354]
	mul	w2, w2, w1
	sdiv	w2, w2, w5
	add	x2, x7, w2, sxtw 2
	str	x2, [x3, x4]
	ldrh	w2, [x0, 2356]
	ldr	x3, [x0, 2632]
	add	x3, x3, x4
	ldr	x4, [x0, 2656]
	mul	w2, w2, w1
	add	w1, w1, 1
	and	w1, w1, 65535
	sdiv	w2, w2, w5
	add	x2, x4, w2, sxtw 2
	str	x2, [x3, 8]
	b	.L2347
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	stp	x29, x30, [sp, -32]!
	mov	w0, -1
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	strh	w0, [x20, 1696]
	mov	w0, -1
	str	w0, [x20, 1708]
	ldr	x0, [x19, 2736]
	ldrh	w2, [x19, 2364]
	strh	wzr, [x19, 2920]
	str	wzr, [x19, 2096]
	lsl	w2, w2, 1
	str	xzr, [x20, 1688]
	str	wzr, [x20, 1700]
	str	wzr, [x20, 1704]
	bl	ftl_memset
	ldr	x0, [x19, 2696]
	mov	w1, 0
	ldrh	w2, [x19, 2286]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 2680]
	mov	w1, 0
	ldrh	w2, [x19, 2286]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w2, 48
	mov	w1, 0
	add	x0, x20, 696
	bl	ftl_memset
	mov	w2, 512
	mov	w1, 0
	add	x0, x20, 1152
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	mov	w3, 0
	mov	w7, 56
	mov	w9, 24
	ldr	w8, [x2, 2568]
	ldr	x4, [x2, 2632]
.L2352:
	cmp	w3, w1
	bcs	.L2351
	umaddl	x6, w3, w7, x0
	mov	w2, 0
	b	.L2357
.L2353:
	add	w2, w2, 1
	and	w2, w2, 65535
.L2357:
	cmp	w2, w8
	bcs	.L2354
	umull	x5, w2, w9
	add	x10, x4, x5
	ldr	x11, [x4, x5]
	ldr	x5, [x6, 8]
	cmp	x11, x5
	bne	.L2353
	str	wzr, [x10, 16]
.L2354:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L2352
.L2351:
	ret
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	mov	w3, 0
	mov	w7, 24
	mov	w8, 1
	mov	w9, 56
	ldr	w5, [x2, 2568]
	ldr	x6, [x2, 2632]
.L2359:
	cmp	w3, w1
	bcs	.L2358
	mov	w2, 0
	b	.L2364
.L2360:
	add	w2, w2, 1
	and	w2, w2, 65535
.L2364:
	cmp	w2, w5
	bcs	.L2361
	umaddl	x4, w2, w7, x6
	ldr	w10, [x4, 16]
	cbnz	w10, .L2360
	umaddl	x2, w3, w9, x0
	ldr	x10, [x4]
	str	w8, [x4, 16]
	str	x10, [x2, 8]
	ldr	x4, [x4, 8]
	str	x4, [x2, 16]
.L2361:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L2359
.L2358:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR4+1712
	and	w0, w0, 65535
	ldrh	w2, [x1, #:lo12:.LANCHOR4+1712]
	adrp	x1, .LANCHOR2+2504
	ldr	x3, [x1, #:lo12:.LANCHOR2+2504]
	mov	x1, 0
.L2366:
	cmp	w2, w1, uxth
	bhi	.L2368
	mov	w0, 0
	ret
.L2368:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L2366
	mov	w0, 1
	ret
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	mov	w6, w0
	mov	w9, w1
	mov	w8, w2
	stp	x29, x30, [sp, -16]!
	lsr	w0, w0, 10
	add	x29, sp, 0
	bl	P2V_block_in_plane
	and	w10, w0, 65535
	adrp	x3, .LANCHOR4
	adrp	x4, .LANCHOR2
	add	x1, x3, :lo12:.LANCHOR4
	add	x2, x4, :lo12:.LANCHOR2
	mov	x5, 0
	ldrh	w1, [x1, 1712]
	ldr	x7, [x2, 2504]
.L2371:
	and	w2, w5, 65535
	cmp	w2, w1
	bcc	.L2373
	bne	.L2372
	and	x5, x5, 65535
	strh	w0, [x7, x5, lsl 1]
	add	x5, x3, :lo12:.LANCHOR4
	ldrh	w0, [x5, 1712]
	add	w0, w0, 1
	strh	w0, [x5, 1712]
	b	.L2372
.L2373:
	add	x5, x5, 1
	add	x2, x7, x5, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w10
	bne	.L2371
.L2372:
	add	x0, x3, :lo12:.LANCHOR4
	add	x3, x4, :lo12:.LANCHOR2
	mov	w1, 12
	ldrh	w5, [x0, 1714]
	ldr	x4, [x3, 2512]
	umull	x5, w5, w1
	add	x4, x4, x5
	str	w9, [x4, 4]
	ldr	x1, [x3, 2512]
	add	x3, x1, x5
	str	w8, [x3, 8]
	str	w6, [x1, x5]
	ldrh	w1, [x0, 1714]
	add	w1, w1, 1
	strh	w1, [x0, 1714]
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	mov	w1, 255
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	strh	wzr, [x0, 1712]
	strh	wzr, [x0, 1714]
	ldr	x0, [x19, 2504]
	ldrh	w2, [x19, 2348]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x19, 2348]
	mov	w0, 12
	mov	w1, 255
	mul	w2, w2, w0
	ldr	x0, [x19, 2512]
	bl	ftl_memset
	bl	FtlGcBufInit
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR1
	add	x0, x22, :lo12:.LANCHOR1
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	str	w1, [x29, 104]
	ldrh	w26, [x0, 3452]
	mov	w0, 65535
	cmp	w26, w0
	beq	.L2408
	cbnz	w26, .L2378
.L2379:
	bl	FtlGcPageVarInit
	b	.L2380
.L2408:
	mov	w26, 0
.L2378:
	adrp	x0, .LANCHOR2+2344
	ldr	w1, [x29, 104]
	ldrh	w0, [x0, #:lo12:.LANCHOR2+2344]
	cmp	w0, w1
	beq	.L2379
.L2380:
	adrp	x28, .LANCHOR2
	add	x27, x28, :lo12:.LANCHOR2
	mov	w0, -1
	mov	w23, 0
	str	w0, [x29, 108]
.L2381:
	ldrh	w1, [x19]
	mov	w0, 65535
	strb	wzr, [x19, 8]
	cmp	w1, w0
	beq	.L2409
.L2405:
	add	x0, x28, :lo12:.LANCHOR2
	add	x6, x19, 16
	mov	w20, 0
	mov	w11, 65535
	mov	w12, 56
	mov	w7, 4
	ldrh	w9, [x0, 2354]
	ldrh	w10, [x0, 2356]
	ldrh	w0, [x0, 2276]
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
.L2383:
	cmp	x6, x0
	bne	.L2385
	ldr	x0, [x27, 2520]
	mov	w2, 0
	mov	w1, w20
	adrp	x24, .LANCHOR0
	add	x24, x24, :lo12:.LANCHOR0
	mov	x21, 0
	bl	FlashReadPages
	mov	w0, 56
	umull	x0, w20, w0
	str	x0, [x29, 96]
.L2386:
	ldr	x0, [x29, 96]
	cmp	x0, x21
	bne	.L2403
	ldr	w0, [x29, 104]
	add	w4, w26, 1
	add	w23, w23, 1
	and	w26, w4, 65535
	cmp	w0, w23
	bls	.L2404
.L2406:
	ldrh	w0, [x27, 2344]
	cmp	w0, w26
	bhi	.L2405
.L2409:
	mov	w2, 0
	b	.L2382
.L2385:
	ldrh	w1, [x6]
	cmp	w1, w11
	beq	.L2384
	umull	x8, w20, w12
	ldr	x5, [x27, 2520]
	orr	w1, w26, w1, lsl 10
	add	x5, x5, x8
	str	w1, [x5, 4]
	mul	w1, w20, w9
	ldr	x2, [x27, 2520]
	ldr	x5, [x27, 2600]
	sdiv	w1, w1, w7
	add	x2, x2, x8
	add	x1, x5, w1, sxtw 2
	str	x1, [x2, 8]
	mul	w1, w20, w10
	ldr	x5, [x27, 2656]
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w1, w1, w7
	add	x1, x5, w1, sxtw 2
	str	x1, [x2, 16]
.L2384:
	add	x6, x6, 2
	b	.L2383
.L2403:
	ldr	x6, [x27, 2520]
	add	x5, x6, x21
	ldr	w25, [x5, 4]
	lsr	w0, w25, 10
	bl	P2V_plane
	and	w2, w0, 65535
	ldr	w0, [x6, x21]
	ldr	x20, [x5, 16]
	cbnz	w0, .L2387
	ldrh	w1, [x20]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2388
.L2391:
	ldrb	w0, [x27, 1220]
	adrp	x14, .LANCHOR4
	cbz	w0, .L2421
	add	x14, x14, :lo12:.LANCHOR4
	mov	w0, 1
	str	w0, [x14, 1704]
.L2382:
	add	x22, x22, :lo12:.LANCHOR1
	strh	w26, [x19, 2]
	strb	w2, [x19, 6]
	mov	w0, -1
	mov	w1, w26
	strh	w0, [x22, 3452]
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
	b	.L2377
.L2388:
	ldr	w0, [x20, 8]
	ldr	w1, [x27, 2924]
	cmp	w0, w1
	bhi	.L2391
	ldrb	w1, [x24, 88]
	cbnz	w1, .L2394
.L2395:
	ldp	w2, w0, [x20, 8]
	mov	w1, w25
	add	x21, x21, 56
	bl	FtlGcUpdatePage
	b	.L2386
.L2421:
	ldr	x0, [x27, 2712]
	ldrh	w1, [x19]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	add	x14, x14, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x14, 1104]
.L2424:
	bl	FtlGcPageVarInit
	mov	w26, 0
	b	.L2381
.L2394:
	add	x1, x29, 116
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x20, 12]
	ldr	w1, [x29, 116]
	cmp	w0, w1
	bne	.L2395
	cmn	w0, #1
	beq	.L2395
	str	w0, [x29, 124]
	mov	w2, 0
	ldr	x0, [x27, 2624]
	mov	w1, 1
	str	x0, [x29, 128]
	ldr	x0, [x27, 2648]
	str	x0, [x29, 136]
	add	x0, x29, 120
	bl	FlashReadPages
	ldrh	w1, [x27, 2350]
	mov	x0, 0
	ldr	x2, [x27, 2520]
	ldr	x6, [x29, 128]
	ubfiz	x1, x1, 9, 16
	add	x2, x2, x21
.L2396:
	cmp	x0, x1
	beq	.L2395
	ldr	x7, [x2, 8]
	ldr	w8, [x7, x0]
	add	x0, x0, 4
	add	x7, x6, x0
	ldr	w7, [x7, -4]
	cmp	w8, w7
	beq	.L2396
	ldrh	w1, [x19]
	adrp	x0, .LC146
	ldr	w2, [x29, 124]
	add	x0, x0, :lo12:.LC146
	bl	printk
	ldrh	w1, [x19]
	ldr	x0, [x27, 2712]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	adrp	x1, .LANCHOR4+1104
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x1, #:lo12:.LANCHOR4+1104]
	b	.L2424
.L2387:
	ldrh	w1, [x19]
	mov	w2, w25
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	bl	printk
	ldr	w1, [x27, 2096]
	ldrh	w0, [x19]
	cbnz	w1, .L2399
	ldrb	w1, [x27, 1220]
	cbz	w1, .L2400
.L2399:
	ldr	x2, [x27, 2696]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cmp	w1, 159
	bls	.L2401
.L2400:
	ldr	x1, [x27, 2520]
	ldr	w1, [x1, x21]
	cmn	w1, #1
	bne	.L2402
.L2401:
	ldr	x1, [x27, 2520]
	add	x21, x1, x21
	ldr	w1, [x21, 4]
	str	w1, [x29, 108]
.L2402:
	ldr	x1, [x27, 2712]
	ubfiz	x0, x0, 1, 16
	strh	wzr, [x1, x0]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	b	.L2424
.L2404:
	add	x1, x22, :lo12:.LANCHOR1
	mov	w2, 65535
	ldrh	w0, [x1, 3452]
	cmp	w0, w2
	beq	.L2406
	add	w0, w0, w23
	strh	w0, [x1, 3452]
	ldrh	w0, [x27, 2344]
	cmp	w0, w26
	bls	.L2406
.L2377:
	ldr	w0, [x29, 108]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlGcRefreshOpenBlock
	.type	FtlGcRefreshOpenBlock, %function
FtlGcRefreshOpenBlock:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	and	w20, w0, 65535
	ldrh	w0, [x19, 2472]
	cmp	w0, w20
	beq	.L2427
	ldrh	w0, [x19, 2474]
	cmp	w0, w20
	beq	.L2427
	ldrh	w0, [x19, 2476]
	cmp	w0, w20
	beq	.L2427
	ldrh	w0, [x19, 2478]
	cmp	w0, w20
	beq	.L2427
	mov	w1, w20
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	bl	printk
	ldrh	w1, [x19, 2472]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2429
	strh	w20, [x19, 2472]
.L2427:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2429:
	ldrh	w1, [x19, 2474]
	cmp	w1, w0
	bne	.L2430
	strh	w20, [x19, 2474]
	b	.L2427
.L2430:
	ldrh	w1, [x19, 2476]
	cmp	w1, w0
	bne	.L2431
	strh	w20, [x19, 2476]
	b	.L2427
.L2431:
	ldrh	w1, [x19, 2478]
	cmp	w1, w0
	bne	.L2427
	strh	w20, [x19, 2478]
	b	.L2427
	.size	FtlGcRefreshOpenBlock, .-FtlGcRefreshOpenBlock
	.align	2
	.global	FtlGcRefreshBlock
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	and	w20, w0, 65535
	str	x21, [sp, 32]
	ldrh	w0, [x19, 2472]
	cmp	w0, w20
	beq	.L2440
	ldrh	w0, [x19, 2474]
	cmp	w0, w20
	beq	.L2440
	ldrh	w0, [x19, 2476]
	cmp	w0, w20
	beq	.L2440
	ldrh	w0, [x19, 2478]
	mov	w21, 0
	cmp	w0, w20
	beq	.L2433
	mov	w1, w20
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	bl	printk
	ldrh	w1, [x19, 2472]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2435
	strh	w20, [x19, 2472]
.L2433:
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L2435:
	ldrh	w1, [x19, 2474]
	cmp	w1, w0
	bne	.L2436
	strh	w20, [x19, 2474]
	b	.L2433
.L2436:
	ldrh	w1, [x19, 2476]
	cmp	w1, w0
	bne	.L2437
	strh	w20, [x19, 2476]
	b	.L2433
.L2437:
	ldrh	w1, [x19, 2478]
	cmp	w1, w0
	bne	.L2442
	strh	w20, [x19, 2478]
	b	.L2433
.L2440:
	mov	w21, 0
	b	.L2433
.L2442:
	mov	w21, -1
	b	.L2433
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x22, x19, :lo12:.LANCHOR2
	and	w20, w0, 65535
	str	x23, [sp, 48]
	mov	w0, w20
	bl	P2V_block_in_plane
	ldrh	w1, [x22, 2486]
	mov	w2, w20
	and	w21, w0, 65535
	adrp	x0, .LC149
	add	x0, x0, :lo12:.LC149
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	ldr	w0, [x22, 2096]
	cbz	w0, .L2445
	ldr	x2, [x22, 2696]
	ubfiz	x0, x21, 1, 16
	ldrh	w1, [x2, x0]
	cmp	w1, 39
	bls	.L2445
	sub	w1, w1, #40
	strh	w1, [x2, x0]
.L2445:
	add	x0, x19, :lo12:.LANCHOR2
	mov	x2, 0
	ldrh	w1, [x0, 2486]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 1720
.L2446:
	cmp	w1, w2, uxth
	bhi	.L2448
	cmp	w1, 15
	bhi	.L2447
	add	x19, x19, :lo12:.LANCHOR2
	add	w0, w1, 1
	strh	w0, [x19, 2486]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 1720
	strh	w20, [x0, w1, sxtw 1]
	b	.L2447
.L2448:
	add	x2, x2, 1
	add	x3, x0, x2, lsl 1
	ldrh	w3, [x3, -2]
	cmp	w3, w20
	bne	.L2446
.L2447:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0, 2486]
	cbz	w1, .L2454
	ldrh	w3, [x0, 2472]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2454
	ldrh	w2, [x0, 2490]
	cmp	w2, w1
	bcc	.L2455
	strh	wzr, [x0, 2490]
.L2455:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 1720
	ldrh	w1, [x19, 2490]
	ldrh	w0, [x0, x1, lsl 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 2490]
	add	w0, w0, 1
	strh	w0, [x19, 2490]
.L2454:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	and	w23, w0, 65535
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	ldrh	w1, [x1, 2486]
	cbz	w1, .L2461
	adrp	x22, .LANCHOR4
	add	x21, x22, :lo12:.LANCHOR4
	mov	x27, x0
	add	x21, x21, 1720
	mov	w20, 0
.L2462:
	add	x0, x27, :lo12:.LANCHOR2
	ldrh	w1, [x0, 2276]
	cmp	w1, w20
	bhi	.L2468
	bl	FtlGcReFreshBadBlk
.L2461:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L2468:
	add	x0, x0, 2304
	mov	w1, w23
	mov	w19, 0
	ldrb	w0, [x0, w20, sxtw]
	bl	V2P_block
	and	w24, w0, 65535
.L2463:
	add	x25, x27, :lo12:.LANCHOR2
	ldrh	w0, [x25, 2486]
	cmp	w0, w19
	bhi	.L2467
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L2462
.L2467:
	add	x0, x22, :lo12:.LANCHOR4
	add	w26, w19, 1
	add	x0, x0, 1720
	ldrh	w0, [x0, w19, sxtw 1]
	cmp	w0, w24
	bne	.L2464
	mov	w1, w24
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	bl	printk
	mov	w0, w24
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w0, [x25, 2486]
	mov	w1, w26
.L2465:
	cmp	w19, w0
	bcc	.L2466
	add	x1, x27, :lo12:.LANCHOR2
	sub	w0, w0, #1
	strh	w0, [x1, 2486]
.L2464:
	and	w19, w26, 65535
	b	.L2463
.L2466:
	sub	w2, w1, #1
	ldrh	w3, [x21, w1, sxtw 1]
	add	w19, w19, 1
	add	w1, w1, 1
	and	w19, w19, 65535
	strh	w3, [x21, w2, sxtw 1]
	b	.L2465
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR2+2712
	and	w14, w0, 65535
	add	x29, sp, 0
	ldr	x1, [x1, #:lo12:.LANCHOR2+2712]
	ubfiz	x0, x14, 1, 16
	ldrh	w0, [x1, x0]
	cbnz	w0, .L2474
	adrp	x8, .LANCHOR4
	add	x0, x8, :lo12:.LANCHOR4
	ldrh	w1, [x0, 1104]
	cmp	w1, w14
	bne	.L2475
	mov	w1, -1
	strh	w1, [x0, 1104]
.L2476:
	add	x8, x8, :lo12:.LANCHOR4
	mov	w1, w14
	add	x0, x8, 744
	bl	List_remove_node
	ldrh	w0, [x8, 760]
	sub	w0, w0, #1
	strh	w0, [x8, 760]
	mov	w0, w14
	bl	free_data_superblock
	mov	w0, w14
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
.L2473:
	ldp	x29, x30, [sp], 16
	ret
.L2475:
	ldrh	w1, [x0, 784]
	cmp	w1, w14
	beq	.L2480
	ldrh	w1, [x0, 832]
	cmp	w1, w14
	beq	.L2480
	ldrh	w0, [x0, 880]
	cmp	w0, w14
	bne	.L2476
.L2480:
	mov	w0, 0
	b	.L2473
.L2474:
	mov	w0, w14
	bl	List_update_data_list
	b	.L2480
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 65535
	stp	x21, x22, [sp, 32]
	mov	w0, 65535
	adrp	x20, .LANCHOR4
	cmp	w19, w0
	beq	.L2483
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	ubfiz	x22, x19, 1, 16
	ldr	x1, [x21, 2712]
	ldrh	w0, [x1, x22]
	cbnz	w0, .L2484
	mov	w1, w19
	mov	w2, 0
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	bl	printk
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x21, 2712]
	add	x6, x20, 768
	mov	w1, 32
	strh	w1, [x0, x22]
	mov	w1, w19
	mov	x0, x6
	bl	test_node_in_list
	cbz	w0, .L2485
	mov	w1, w19
	mov	x0, x6
	bl	List_remove_node
	ldrh	w0, [x20, 776]
	sub	w0, w0, #1
	strh	w0, [x20, 776]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x0, [x21, 2712]
	mov	w1, w19
	ldrh	w2, [x0, x22]
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
	bl	printk
.L2485:
	mov	w0, w19
	bl	FtlGcRefreshBlock
.L2488:
	mov	w0, 0
	b	.L2482
.L2484:
	sub	w0, w0, #1
	strh	w0, [x1, x22]
.L2483:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w1, 65535
	ldrh	w0, [x20, 1696]
	cmp	w0, w1
	bne	.L2487
	strh	w19, [x20, 1696]
	b	.L2488
.L2487:
	cmp	w19, w0
	beq	.L2488
	bl	update_vpc_list
	cmp	w0, 0
	cset	w0, ne
	strh	w19, [x20, 1696]
.L2482:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	stp	x29, x30, [sp, -176]!
	mov	w2, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w0, [x0]
	cmp	w0, w2
	beq	.L2639
	ldrh	w0, [x19, 2]
	adrp	x20, .LANCHOR2
	str	w0, [x29, 156]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x29, 156]
	ldrh	w0, [x0, 2344]
	cmp	w0, w1
	bne	.L2496
	strh	wzr, [x19, 4]
.L2646:
	strb	wzr, [x19, 6]
.L2639:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L2496:
	ldrh	w0, [x19, 16]
	mov	w1, 0
.L2497:
	cmp	w0, w2
	beq	.L2498
	ldrb	w1, [x19, 6]
	str	w1, [x29, 140]
	ldrb	w1, [x19, 8]
	cmp	w1, 1
	bne	.L2499
	bl	FtlGetLastWrittenPage
	mov	w21, w0
	cmn	w0, #1
	beq	.L2500
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0, 1220]
	cbnz	w0, .L2572
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 144
	ldrh	w23, [x0, w21, sxtw 1]
.L2501:
	add	x0, x20, :lo12:.LANCHOR2
	add	x1, x19, 16
	str	x1, [x29, 144]
	add	x3, x19, 16
	mov	w28, 0
	mov	w8, 65535
	ldrh	w1, [x0, 2276]
	mov	w9, 56
	ldrh	w7, [x0, 2356]
	mov	w10, 4
	add	x1, x1, 8
	add	x1, x19, x1, lsl 1
.L2502:
	cmp	x3, x1
	bne	.L2504
	ldrb	w0, [x19, 8]
	cmp	w0, 1
	bne	.L2573
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0, 1220]
	cmp	w0, 0
	cset	w0, ne
	str	w0, [x29, 152]
.L2505:
	add	x24, x20, :lo12:.LANCHOR2
	ldr	w2, [x29, 152]
	mov	w1, w28
	adrp	x25, .LC153
	adrp	x26, .LANCHOR4
	add	x25, x25, :lo12:.LC153
	ldr	x0, [x24, 2520]
	add	x26, x26, :lo12:.LANCHOR4
	mov	x27, 0
	bl	FlashReadPages
	ldr	w22, [x24, 2400]
	mov	w4, 65535
	sub	w22, w22, #1
.L2506:
	and	w0, w27, 65535
	cmp	w28, w0
	bhi	.L2511
	bne	.L2509
	add	x0, x20, :lo12:.LANCHOR2
	add	w21, w21, 1
	and	w21, w21, 65535
	ldr	x0, [x0, 2520]
	ldr	w0, [x0, 4]
.L2641:
	lsr	w0, w0, 10
	bl	P2V_plane
	ldrb	w1, [x19, 8]
	and	w28, w0, 65535
	cmp	w1, 1
	bne	.L2513
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0, 1220]
	cbnz	w0, .L2513
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 144
	ldrh	w21, [x0, w21, sxtw 1]
.L2513:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0, 2344]
	cmp	w0, w21
	bne	.L2514
	strh	w21, [x19, 2]
	strb	wzr, [x19, 6]
	strh	wzr, [x19, 4]
.L2514:
	ldrh	w0, [x29, 140]
	str	w0, [x29, 140]
	ldr	w0, [x29, 156]
	cmp	w21, w0
	ldr	w0, [x29, 140]
	ccmp	w28, w0, 0, eq
	bne	.L2515
.L2647:
	mov	w2, w28
	mov	w1, w21
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
	b	.L2639
.L2498:
	add	w1, w1, 1
	and	w1, w1, 65535
	add	x0, x19, w1, sxtw 1
	ldrh	w0, [x0, 16]
	b	.L2497
.L2499:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	mov	w21, w0
	cmn	w0, #1
	beq	.L2500
.L2572:
	mov	w23, w21
	b	.L2501
.L2500:
	strh	wzr, [x19, 2]
	b	.L2646
.L2504:
	ldrh	w2, [x3]
	cmp	w2, w8
	beq	.L2503
	umull	x6, w28, w9
	ldr	x5, [x0, 2520]
	orr	w2, w23, w2, lsl 10
	add	x5, x5, x6
	str	w2, [x5, 4]
	mul	w2, w28, w7
	add	w28, w28, 1
	ldr	x4, [x0, 2520]
	and	w28, w28, 65535
	ldr	x5, [x0, 2656]
	sdiv	w2, w2, w10
	add	x4, x4, x6
	add	x2, x5, w2, sxtw 2
	stp	xzr, x2, [x4, 8]
.L2503:
	add	x3, x3, 2
	b	.L2502
.L2573:
	str	wzr, [x29, 152]
	b	.L2505
.L2511:
	mov	x0, 56
	ldr	x3, [x24, 2520]
	mul	x0, x27, x0
	add	x1, x3, x0
	ldr	w0, [x3, x0]
	cbnz	w0, .L2507
	ldr	x5, [x1, 16]
	ldr	w3, [x5, 4]
	cmn	w3, #1
	beq	.L2508
	ldr	w1, [x24, 2400]
	mov	w0, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L2508
	add	w3, w3, 1
	str	w3, [x24, 2400]
.L2508:
	ldr	w0, [x5]
	cmn	w0, #1
	bne	.L2510
.L2509:
	add	x0, x20, :lo12:.LANCHOR2
	and	x27, x27, 65535
	mov	x1, 56
	and	w21, w21, 65535
	ldr	x0, [x0, 2520]
	madd	x27, x27, x1, x0
	ldr	w0, [x27, 4]
	b	.L2641
.L2507:
	ldr	w1, [x1, 4]
	mov	x0, x25
	bl	printk
	ldrh	w0, [x19]
	and	w4, w23, 65535
	strh	w0, [x26, 1754]
.L2510:
	add	x27, x27, 1
	b	.L2506
.L2515:
	mov	w0, 65535
	adrp	x27, .LANCHOR4
	cmp	w4, w0
	bne	.L2516
	cbnz	w1, .L2517
.L2516:
	add	x0, x27, :lo12:.LANCHOR4
	and	w6, w23, 65535
	ldr	w1, [x0, 1708]
	cmn	w1, #1
	bne	.L2518
	str	w22, [x0, 1708]
.L2518:
	add	x0, x27, :lo12:.LANCHOR4
	ldr	w5, [x0, 1708]
	ldr	w0, [x29, 156]
	add	w0, w0, 7
	cmp	w0, w23, uxth
	bge	.L2574
	sub	w23, w6, #7
	and	w23, w23, 65535
.L2519:
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	mov	w26, -1
	add	x3, x20, :lo12:.LANCHOR2
	mov	w25, w26
	add	x4, x4, 1168
.L2520:
	cmp	w23, w6
	bhi	.L2533
	ldrh	w0, [x3, 2276]
	mov	w24, 0
	mov	w9, 65535
	mov	w8, 56
	add	x0, x0, 8
	ldr	x1, [x29, 144]
	add	x0, x19, x0, lsl 1
	b	.L2534
.L2574:
	ldr	w23, [x29, 156]
	b	.L2519
.L2522:
	ldrh	w2, [x1]
	cmp	w2, w9
	beq	.L2521
	ldr	x7, [x3, 2520]
	orr	w2, w23, w2, lsl 10
	umaddl	x7, w24, w8, x7
	add	w24, w24, 1
	and	w24, w24, 65535
	str	w2, [x7, 4]
.L2521:
	add	x1, x1, 2
.L2534:
	cmp	x0, x1
	bne	.L2522
	ldr	w2, [x29, 152]
	mov	w1, w24
	ldr	x0, [x3, 2520]
	str	x4, [x29, 104]
	str	w5, [x29, 112]
	str	w6, [x29, 120]
	str	x3, [x29, 128]
	bl	FlashReadPages
	ldr	x3, [x29, 128]
	mov	w1, 56
	ldr	w5, [x29, 112]
	add	x2, x27, :lo12:.LANCHOR4
	ldr	w6, [x29, 120]
	sxtw	x9, w23
	ldr	x4, [x29, 104]
	ldr	x0, [x3, 2520]
	ldrb	w8, [x3, 1220]
	nop // between mem op and mult-accumulate
	umaddl	x24, w24, w1, x0
	mov	w1, 65535
.L2523:
	cmp	x24, x0
	bne	.L2532
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L2520
.L2532:
	ldr	w7, [x0]
	cbnz	w7, .L2524
	ldr	x7, [x0, 16]
	ldrh	w10, [x7]
	cmp	w10, w1
	beq	.L2525
	ldr	w7, [x7, 4]
	cmn	w7, #1
	beq	.L2525
	ldr	w25, [x2, 1708]
	cmn	w26, #1
	str	w7, [x2, 1708]
	bne	.L2525
	ldrh	w7, [x4, x9, lsl 1]
	cmp	w7, w1
	bne	.L2526
	cbz	w8, .L2525
.L2526:
	cmp	w22, w25
	csel	w26, w26, w25, eq
.L2525:
	add	x0, x0, 56
	b	.L2523
.L2524:
	add	x0, x27, :lo12:.LANCHOR4
	ldrh	w1, [x19]
	strh	w1, [x0, 1754]
	ldrb	w1, [x19, 8]
	cbnz	w1, .L2517
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 1168
	ldrh	w2, [x1, w23, sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2528
	cmn	w26, #1
	beq	.L2529
	str	w26, [x0, 1708]
.L2517:
	add	x0, x27, :lo12:.LANCHOR4
	ldr	w24, [x29, 156]
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, 1
	strh	w1, [x0, 1756]
.L2535:
	ldrh	w0, [x20, 2276]
	mov	w23, 0
	ldrb	w6, [x20, 1220]
	mov	w7, 65535
	add	x0, x0, 8
	ldr	x1, [x29, 144]
	add	x0, x19, x0, lsl 1
.L2536:
	cmp	x0, x1
	bne	.L2539
	ldr	w2, [x29, 152]
	mov	w1, w23
	ldr	x0, [x20, 2520]
	mov	x25, 0
	bl	FlashReadPages
	mov	w0, 56
	umull	x0, w23, w0
	add	x23, x27, :lo12:.LANCHOR4
	str	x0, [x29, 120]
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
	str	x0, [x29, 112]
	add	x0, x23, 1768
	str	x0, [x29, 104]
.L2540:
	ldr	x0, [x29, 120]
	cmp	x0, x25
	bne	.L2565
	ldrb	w0, [x19, 8]
	add	w24, w24, 1
	and	w24, w24, 65535
	cmp	w0, 1
	bne	.L2566
	ldrb	w0, [x20, 1220]
	cbz	w0, .L2566
	ldrh	w0, [x20, 2346]
	cmp	w0, w24
	bne	.L2566
	cmp	w21, w24
	beq	.L2542
.L2566:
	ldrh	w0, [x20, 2344]
	cmp	w0, w24
	bne	.L2535
	ldrh	w1, [x20, 2276]
	mov	w0, 0
	strh	w24, [x19, 2]
	mov	w2, 65535
	strh	wzr, [x19, 4]
.L2567:
	cmp	w0, w1
	beq	.L2639
	ldr	x4, [x29, 144]
	ldrh	w3, [x4], 2
	str	x4, [x29, 144]
	cmp	w3, w2
	beq	.L2568
	strb	w0, [x19, 6]
	b	.L2639
.L2529:
	cmp	w22, w5
	beq	.L2530
	str	w5, [x0, 1708]
	b	.L2517
.L2530:
	ldr	w1, [x0, 1708]
.L2648:
	sub	w1, w1, #1
.L2642:
	str	w1, [x0, 1708]
	b	.L2517
.L2528:
	cmp	w25, w22
	beq	.L2531
	cmn	w25, #1
	beq	.L2517
	str	w25, [x0, 1708]
	b	.L2517
.L2531:
	ldr	w1, [x0, 1708]
	cmp	w22, w1
	bne	.L2648
	b	.L2517
.L2533:
	add	x0, x27, :lo12:.LANCHOR4
	mov	w1, -1
	b	.L2642
.L2539:
	ldrh	w3, [x1]
	cmp	w3, w7
	beq	.L2537
	mov	w2, 56
	ldr	x4, [x20, 2520]
	orr	w3, w24, w3, lsl 10
	umull	x5, w23, w2
	add	x4, x4, x5
	str	w3, [x4, 4]
	ldrb	w2, [x19, 8]
	cmp	w2, 1
	bne	.L2538
	cbz	w6, .L2538
	ldr	x2, [x20, 2520]
	add	x2, x2, x5
	ldr	w3, [x2, 4]
	orr	w3, w3, -2147483648
	str	w3, [x2, 4]
.L2538:
	add	w23, w23, 1
	and	w23, w23, 65535
.L2537:
	add	x1, x1, 2
	b	.L2536
.L2565:
	ldr	x4, [x20, 2520]
	add	x4, x4, x25
	ldr	w5, [x4, 4]
	str	w5, [x29, 172]
	lsr	w0, w5, 10
	bl	P2V_plane
	and	w0, w0, 65535
	ldr	w1, [x29, 156]
	cmp	w24, w1
	bcc	.L2541
	ldr	w1, [x29, 140]
	ccmp	w1, w0, 0, eq
	bhi	.L2541
	cmp	w24, w21
	ccmp	w28, w0, 0, eq
	beq	.L2542
	ldr	w0, [x4]
	cmn	w0, #1
	beq	.L2543
	ldr	x3, [x4, 16]
	mov	w0, 61589
	ldrh	w1, [x3]
	cmp	w1, w0
	beq	.L2544
	ldrh	w0, [x19]
.L2644:
	bl	decrement_vpc_count
	b	.L2541
.L2544:
	ldr	w22, [x3, 4]
	cmn	w22, #1
	beq	.L2545
	ldr	w1, [x20, 2400]
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2545
	add	w0, w22, 1
	str	w0, [x20, 2400]
.L2545:
	ldp	w26, w0, [x3, 8]
	add	x1, x29, 168
	str	w0, [x29, 164]
	mov	w2, 0
	mov	w0, w26
	bl	log2phys
	ldr	w1, [x23, 1708]
	ldr	w3, [x29, 164]
	cmn	w1, #1
	beq	.L2546
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2546
	cmn	w3, #1
	beq	.L2547
	ldr	x0, [x20, 2520]
	mov	w2, 0
	mov	w1, 1
	add	x0, x0, x25
	ldr	x4, [x0, 16]
	str	w3, [x0, 4]
	str	x4, [x29, 128]
	ldr	x0, [x20, 2520]
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x0, [x20, 2520]
	ldr	x4, [x29, 128]
	add	x3, x0, x25
	ldr	w0, [x0, x25]
	cmn	w0, #1
	bne	.L2548
.L2549:
	mov	w0, -1
	str	w0, [x29, 164]
.L2556:
	ldr	w4, [x29, 164]
	cmn	w4, #1
	beq	.L2541
.L2571:
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldr	x3, [x20, 2712]
	and	w1, w0, 65535
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3, x2]
	cbnz	w2, .L2644
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	bl	printk
	b	.L2541
.L2547:
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	bne	.L2541
	mov	w2, 1
	add	x1, x29, 164
	mov	w0, w26
	bl	log2phys
.L2541:
	add	x25, x25, 56
	b	.L2540
.L2548:
	ldr	w0, [x4, 8]
	cmp	w26, w0
	bne	.L2549
	ldr	w0, [x4, 4]
	str	w0, [x29, 128]
	str	x4, [x29, 96]
	uxtw	x1, w0
	ldr	w0, [x23, 1708]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2549
	ldp	w0, w1, [x29, 168]
	ldr	x4, [x29, 96]
	cmp	w0, w1
	ldr	w1, [x29, 164]
	bne	.L2551
.L2643:
	mov	w0, w26
	bl	FtlReUsePrevPpa
	b	.L2549
.L2551:
	cmp	w0, w1
	beq	.L2549
	cmn	w0, #1
	beq	.L2552
	ldr	x4, [x3, 16]
	mov	w2, 0
	str	w0, [x3, 4]
	mov	w1, 1
	str	x4, [x29, 96]
	ldr	x0, [x20, 2520]
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x4, [x29, 96]
.L2553:
	adrp	x0, .LANCHOR2+2520
	ldr	x0, [x0, #:lo12:.LANCHOR2+2520]
	ldr	w0, [x0, x25]
	cmn	w0, #1
	beq	.L2554
	ldr	w3, [x4, 4]
	ldr	w0, [x23, 1708]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L2554
	ldr	w0, [x29, 128]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L2549
.L2554:
	ldr	w1, [x29, 164]
	b	.L2643
.L2552:
	str	w0, [x3]
	b	.L2553
.L2546:
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	beq	.L2556
	cmn	w3, #1
	beq	.L2558
	ldr	w0, [x20, 2292]
	ubfx	x3, x3, 10, 21
	cmp	w3, w0
	bcs	.L2541
.L2558:
	mov	w2, 1
	add	x1, x29, 172
	mov	w0, w26
	bl	log2phys
	ldr	w4, [x29, 168]
	cmn	w4, #1
	beq	.L2556
	ldr	w0, [x29, 164]
	cmp	w4, w0
	beq	.L2571
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x23, 784]
	and	w0, w0, 65535
	cmp	w1, w0
	beq	.L2561
	ldrh	w1, [x23, 832]
	cmp	w1, w0
	beq	.L2561
	ldrh	w1, [x23, 880]
	cmp	w1, w0
	bne	.L2556
.L2561:
	ldr	x0, [x20, 2520]
	mov	w2, 0
	mov	w1, 1
	str	w4, [x0, 4]
	ldr	x3, [x0, 16]
	ldr	x0, [x20, 2520]
	str	x3, [x29, 128]
	bl	FlashReadPages
	ldr	x0, [x20, 2520]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2556
	ldr	x3, [x29, 128]
	mov	w0, w22
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2556
	mov	w2, 1
	add	x1, x29, 168
	mov	w0, w26
	bl	log2phys
	b	.L2556
.L2543:
	ldrh	w0, [x19]
	mov	w2, w22
	strh	w0, [x23, 1754]
	mov	w1, w5
	ldr	x0, [x29, 112]
	bl	printk
	ldr	w0, [x23, 1760]
	cmp	w0, 31
	bhi	.L2563
	ldr	x2, [x29, 104]
	ldr	w1, [x29, 172]
	str	w1, [x2, w0, uxtw 2]
	add	w0, w0, 1
	str	w0, [x23, 1760]
.L2563:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldr	w0, [x23, 1708]
	cmn	w0, #1
	bne	.L2564
.L2645:
	str	w22, [x23, 1708]
	b	.L2541
.L2564:
	cmp	w22, w0
	bcs	.L2541
	b	.L2645
.L2568:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L2567
.L2542:
	strb	w28, [x19, 6]
	strh	w21, [x19, 2]
	b	.L2647
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	ldrh	w1, [x0, 4]
	cbz	w1, .L2661
	ldrh	w2, [x0]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2661
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR2
	ldrb	w0, [x0, 6]
	add	x22, x20, :lo12:.LANCHOR2
	add	x21, x21, 1168
	add	x0, x0, 8
	ldrh	w1, [x19, x0, lsl 1]
.L2653:
	mov	w0, 65535
	cmp	w1, w0
	beq	.L2655
	ldrb	w1, [x19, 8]
	cmp	w1, 1
	bne	.L2656
	ldrb	w2, [x22, 1220]
	cbnz	w2, .L2656
	ldrh	w2, [x19, 2]
	ldrh	w2, [x21, x2, lsl 1]
	cmp	w2, w0
	bne	.L2656
	ldrh	w0, [x19, 4]
	sub	w0, w0, #1
	strh	w0, [x19, 4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w0, [x19, 4]
	cbnz	w0, .L2655
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
.L2664:
	strh	w0, [x19, 2]
	strb	wzr, [x19, 6]
.L2649:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L2655:
	ldrb	w0, [x19, 6]
	ldrh	w1, [x22, 2276]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	cmp	w1, w0
	bne	.L2654
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L2654:
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w1, [x19, x0, lsl 1]
	b	.L2653
.L2656:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w2, [x0, 1220]
	cbz	w2, .L2649
	cmp	w1, 1
	bne	.L2649
	ldrh	w2, [x19, 2]
	ldrh	w1, [x0, 2346]
	cmp	w2, w1
	bcc	.L2649
	ldrh	w1, [x19]
	ldr	x3, [x0, 2712]
	ldrh	w4, [x19, 4]
	lsl	x1, x1, 1
	ldrh	w2, [x3, x1]
	sub	w2, w2, w4
	strh	w2, [x3, x1]
	strh	wzr, [x19, 4]
	ldrh	w0, [x0, 2344]
	b	.L2664
.L2661:
	ret
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	get_new_active_ppa
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	str	x23, [sp, 48]
	add	x21, x21, :lo12:.LANCHOR0
	strb	wzr, [x0, 10]
	adrp	x20, .LANCHOR2
	ldrb	w0, [x0, 6]
	add	x21, x21, 1168
	add	x23, x20, :lo12:.LANCHOR2
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
.L2666:
	mov	w2, 65535
	cmp	w0, w2
	beq	.L2667
	ldrb	w1, [x19, 8]
	ldrh	w22, [x19, 2]
	cmp	w1, 1
	ldrh	w1, [x19, 4]
	bne	.L2669
	ldrb	w3, [x23, 1220]
	cbnz	w3, .L2669
	ldrh	w3, [x21, w22, sxtw 1]
	cmp	w3, w2
	bne	.L2669
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19, 4]
	bl	decrement_vpc_count
.L2667:
	ldrb	w0, [x19, 6]
	ldrh	w1, [x23, 2276]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	cmp	w1, w0
	bne	.L2668
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L2668:
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
	b	.L2666
.L2669:
	adrp	x21, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR0
	orr	w22, w22, w0, lsl 10
	add	x20, x20, :lo12:.LANCHOR2
	add	x21, x21, 1168
	sub	w1, w1, #1
	strh	w1, [x19, 4]
.L2670:
	ldrb	w0, [x19, 6]
	mov	w1, 65535
	ldrh	w3, [x20, 2276]
.L2672:
	add	w0, w0, 1
	and	w0, w0, 255
	cmp	w0, w3
	bne	.L2671
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	w0, 0
.L2671:
	add	x2, x19, w0, sxtw 1
	ldrh	w2, [x2, 16]
	cmp	w2, w1
	beq	.L2672
	strb	w0, [x19, 6]
	ldrb	w0, [x19, 8]
	cmp	w0, 1
	bne	.L2665
	ldrb	w2, [x20, 1220]
	ldrh	w0, [x19, 2]
	cbnz	w2, .L2674
	ldrh	w0, [x21, w0, sxtw 1]
	cmp	w0, w1
	bne	.L2665
	ldrh	w0, [x19, 4]
	cbz	w0, .L2665
	sub	w0, w0, #1
	strh	w0, [x19, 4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2670
.L2674:
	ldrh	w1, [x20, 2346]
	cmp	w0, w1
	bcc	.L2665
	ldrh	w0, [x19]
	ldr	x2, [x20, 2712]
	ldrh	w3, [x19, 4]
	lsl	x0, x0, 1
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strh	wzr, [x19, 4]
	ldrh	w0, [x20, 2344]
	strh	w0, [x19, 2]
	strb	wzr, [x19, 6]
.L2665:
	mov	w0, w22
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x20, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldr	w0, [x20, 2928]
	cbnz	w0, .L2681
	adrp	x19, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR4
	add	x0, x0, 784
	ldrh	w2, [x0, 4]
	cbz	w2, .L2683
	ldrb	w1, [x0, 8]
	cbnz	w1, .L2683
	ldrb	w1, [x0, 7]
	ldrh	w3, [x20, 2344]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L2683
	ldrb	w0, [x0, 10]
	cbnz	w0, .L2681
	ldr	w23, [x20, 2924]
	add	x1, x29, 84
	ldrh	w25, [x20, 2276]
	mov	w2, 0
	sub	w23, w23, #1
	mov	w0, w23
	bl	log2phys
	ldr	x21, [x20, 2640]
	ldr	w0, [x29, 84]
	ldr	x1, [x20, 2576]
	str	w0, [x29, 92]
	cmn	w0, #1
	stp	x1, x21, [x29, 96]
	str	w23, [x29, 112]
	str	wzr, [x21, 4]
	beq	.L2685
	mov	w2, 0
	mov	w1, 1
	add	x0, x29, 88
	bl	FlashReadPages
.L2686:
	add	x20, x19, :lo12:.LANCHOR4
	lsl	w25, w25, 2
	add	x20, x20, 784
	add	x22, x22, :lo12:.LANCHOR2
	mov	w0, -3947
	mov	w24, 0
	strh	w0, [x21]
.L2687:
	cmp	w25, w24
	bne	.L2691
.L2688:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x19, 794]
.L2681:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 144
	ret
.L2685:
	ldrh	w2, [x20, 2354]
	mov	w1, 255
	ldr	x0, [x20, 2576]
	bl	ftl_memset
	b	.L2686
.L2691:
	ldrh	w0, [x20, 4]
	cbz	w0, .L2688
	ldr	w0, [x29, 92]
	add	w24, w24, 1
	stp	w23, w0, [x21, 8]
	ldrh	w0, [x20]
	strh	w0, [x21, 2]
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29, 92]
	ldr	w0, [x22, 2400]
	mov	w3, 0
	str	w0, [x21, 4]
	mov	w2, 0
	add	w0, w0, 1
	mov	w1, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x22, 2400]
	add	x0, x29, 88
	bl	FlashProgPages
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	b	.L2687
.L2683:
	add	x19, x19, :lo12:.LANCHOR4
	strb	wzr, [x19, 794]
	b	.L2681
	.size	FtlWriteDumpData, .-FtlWriteDumpData
	.align	2
	.global	l2p_flush
	.type	l2p_flush, %function
l2p_flush:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	mov	w19, 0
	bl	FtlWriteDumpData
.L2700:
	ldrh	w0, [x20, 2382]
	cmp	w0, w19
	bhi	.L2702
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2702:
	ldr	x1, [x20, 2792]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L2701
	mov	w0, w19
	bl	flush_l2p_region
.L2701:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L2700
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x1, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	ldr	w24, [x1, 2928]
	cbnz	w24, .L2704
	ldrb	w1, [x1, 1220]
	cbz	w1, .L2715
	ldrb	w1, [x0, 8]
	cmp	w1, 1
	bne	.L2715
	ldrh	w23, [x0, 4]
	mov	w24, w1
.L2706:
	mov	x19, x0
	mov	w0, -1
	str	w0, [x29, 96]
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 61589
	mov	x21, x0
	ldr	x22, [x0, 2640]
	ldr	x1, [x0, 2576]
	stp	x1, x22, [x29, 80]
	mov	w1, -3
	str	w1, [x22, 8]
	mov	w1, -2
	str	w1, [x22, 12]
	ldrh	w1, [x19]
	strh	w1, [x22, 2]
	strh	wzr, [x22]
	ldr	x1, [x0, 2576]
	str	w2, [x1]
	mov	w2, 22136
	movk	w2, 0x1234, lsl 16
	ldr	x1, [x0, 2576]
	str	w2, [x1, 4]
.L2707:
	sub	w23, w23, #1
	cmn	w23, #1
	beq	.L2710
	ldrh	w0, [x19, 4]
	cbnz	w0, .L2708
.L2710:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19]
	ldrh	w3, [x19, 4]
	ldr	x2, [x20, 2712]
	lsl	x0, x0, 1
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strb	wzr, [x19, 6]
	ldrh	w0, [x20, 2344]
	strh	w0, [x19, 2]
	strh	wzr, [x19, 4]
.L2704:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 128
	ret
.L2715:
	mov	w23, 12
	b	.L2706
.L2708:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29, 76]
	cmn	w0, #1
	beq	.L2710
	ldr	w0, [x21, 2400]
	mov	w3, 0
	str	w0, [x22, 4]
	mov	w2, w24
	add	w0, w0, 1
	mov	w1, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x21, 2400]
	add	x0, x29, 72
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2707
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC49
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x19, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x1, x1, 232
	add	x0, x0, :lo12:.LC49
	str	x23, [sp, 48]
	bl	printk
	ldr	x0, [x19, 2704]
	mov	w21, 0
	ldrh	w2, [x19, 2286]
	mov	w1, 0
	lsl	w2, w2, 1
	bl	ftl_memset
.L2721:
	ldr	w0, [x19, 2924]
	cmp	w21, w0
	bcc	.L2723
	adrp	x22, .LANCHOR4
	adrp	x23, .LC156
	add	x20, x20, :lo12:.LANCHOR2
	add	x22, x22, :lo12:.LANCHOR4
	add	x23, x23, :lo12:.LC156
	mov	w19, 0
.L2724:
	ldrh	w0, [x20, 2284]
	cmp	w0, w19
	bhi	.L2727
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 80
	ret
.L2723:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L2722
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x19, 2704]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L2722:
	add	w21, w21, 1
	b	.L2721
.L2727:
	ldr	x0, [x20, 2712]
	ubfiz	x21, x19, 1, 16
	ldrh	w2, [x0, x21]
	ldr	x0, [x20, 2704]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L2725
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2725
	ldrh	w0, [x22, 784]
	cmp	w0, w19
	beq	.L2725
	ldrh	w0, [x22, 880]
	cmp	w0, w19
	beq	.L2725
	ldrh	w0, [x22, 832]
	cmp	w0, w19
	beq	.L2725
	mov	w1, w19
	mov	x0, x23
	bl	printk
	ldr	x0, [x20, 2712]
	ldrh	w1, [x0, x21]
	cbnz	w1, .L2726
	ldr	x1, [x20, 2704]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
.L2725:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L2724
.L2726:
	ldr	x1, [x20, 2704]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
	mov	w0, w19
	bl	update_vpc_list
	b	.L2725
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x2, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	ldr	w1, [x2, 2928]
	cbnz	w1, .L2733
	ldrh	w21, [x0]
	mov	x22, x0
	mov	w0, 65535
	cmp	w21, w0
	beq	.L2734
	ldr	x0, [x2, 2712]
	ubfiz	x1, x21, 1, 16
	ldrh	w0, [x0, x1]
	cbz	w0, .L2735
	mov	w0, w21
	bl	INSERT_DATA_LIST
.L2734:
	adrp	x19, .LANCHOR4
	strb	wzr, [x22, 8]
	add	x1, x19, :lo12:.LANCHOR4
	add	x0, x1, 832
	cmp	x22, x0
	beq	.L2736
	add	x2, x20, :lo12:.LANCHOR2
	ldrh	w3, [x2, 2296]
	cmp	w3, 1
	beq	.L2736
	ldrb	w0, [x2, 1220]
	cbz	w0, .L2737
.L2736:
	mov	w0, 1
	strb	w0, [x22, 8]
.L2738:
	add	x0, x19, :lo12:.LANCHOR4
	mov	w1, 65535
	ldrh	w0, [x0, 1696]
	cmp	w0, w1
	beq	.L2743
	cmp	w21, w0
	bne	.L2744
	add	x20, x20, :lo12:.LANCHOR2
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x20, 2712]
	ldrh	w1, [x2, x1]
	cbz	w1, .L2745
.L2744:
	bl	update_vpc_list
.L2745:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x19, 1696]
.L2743:
	mov	x0, x22
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2733:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L2735:
	mov	w0, w21
	bl	INSERT_FREE_LIST
	b	.L2734
.L2737:
	add	x1, x1, 784
	cmp	x22, x1
	bne	.L2738
	cmp	w3, 3
	beq	.L2740
	ldr	w0, [x2, 2392]
	cmp	w0, 1
	bne	.L2741
.L2740:
	add	x1, x19, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x1, 792]
.L2741:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	w0, [x1, 2096]
	cbz	w0, .L2738
	ldr	w0, [x1, 2444]
	cmp	w0, 39
	bhi	.L2738
	add	x1, x19, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x1, 792]
	b	.L2738
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	adrp	x0, .LANCHOR4
	add	x4, x0, :lo12:.LANCHOR4
	add	x2, x4, 1152
	mov	x1, x0
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w3, [x2, 80]
	cbz	w3, .L2760
	ldr	w1, [x0, 2924]
	ldr	w3, [x2, 84]
	cmp	w3, w1
	bcs	.L2761
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x4
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	mov	w21, 2048
.L2766:
	add	x22, x20, 1152
	ldr	w1, [x19, 2924]
	ldr	w0, [x22, 84]
	cmp	w0, w1
	bcc	.L2762
.L2765:
	ldp	x19, x20, [sp, 16]
	mov	w0, -1
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 112
	ret
.L2762:
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x22, 84]
	ldr	w1, [x29, 52]
	add	w0, w0, 1
	str	w0, [x22, 84]
	cmn	w1, #1
	beq	.L2764
	str	w0, [x29, 80]
	add	x0, x29, 112
	str	w1, [x29, 60]
	mov	w2, 0
	stp	xzr, xzr, [x29, 64]
	mov	w1, 1
	str	wzr, [x0, -56]!
	bl	FlashReadPages
	ldr	w0, [x29, 56]
	cmp	w0, 256
	bne	.L2765
	ldr	w0, [x29, 52]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2765
.L2764:
	subs	w21, w21, #1
	bne	.L2766
	b	.L2765
.L2761:
	ldr	w0, [x0, 2424]
	stp	w0, wzr, [x2, 76]
	str	wzr, [x2, 84]
.L2773:
	mov	w0, 0
	ret
.L2760:
	ldr	w3, [x0, 2440]
	mov	w5, 10000
	mov	w6, 31
	ldr	w7, [x2, 76]
	cmp	w3, w5
	mov	w5, 63
	csel	w6, w6, w5, hi
	ldr	w5, [x0, 2424]
	add	w8, w5, 1048576
	cmp	w7, w8
	bhi	.L2770
	ldr	w0, [x0, 2924]
	mov	w8, 1000
	lsr	w3, w3, 10
	add	w3, w3, 1
	mul	w0, w0, w8
	udiv	w0, w0, w3
	add	w0, w0, w7
	cmp	w5, w0
	bhi	.L2770
	ldrh	w0, [x4, 724]
	tst	w6, w0
	bne	.L2773
	ldr	w2, [x2, 100]
	cmp	w0, w2
	beq	.L2773
.L2770:
	add	x0, x1, :lo12:.LANCHOR4
	add	x1, x0, 1152
	ldrh	w0, [x0, 724]
	str	w0, [x1, 100]
	str	w5, [x1, 76]
	mov	w0, 1
	str	wzr, [x1, 84]
	str	w0, [x1, 80]
	b	.L2773
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	ftl_do_gc
	.type	ftl_do_gc, %function
ftl_do_gc:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x3, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w2, [x3, 2928]
	cbnz	w2, .L2878
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	ldr	w21, [x2, 3448]
	cmp	w21, 1
	bne	.L2878
	ldr	w3, [x3, 2448]
	cbnz	w3, .L2878
	adrp	x20, .LANCHOR4
	add	x3, x20, :lo12:.LANCHOR4
	ldrh	w4, [x3, 760]
	cmp	w4, 47
	bls	.L2878
	mov	w23, w1
	str	w0, [x29, 124]
	ldrh	w1, [x2, 3452]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2781
.L2784:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0, 2474]
	cmp	w4, w2
	bne	.L2782
.L2783:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x29, 124]
	cmp	w2, 1
	ldr	w0, [x1, 2464]
	add	w0, w0, 1
	add	w0, w0, w2, lsl 7
	str	w0, [x1, 2464]
	bne	.L2785
	ldr	w2, [x1, 2096]
	cbnz	w2, .L2786
	ldrb	w1, [x1, 1220]
	cbz	w1, .L2785
.L2786:
	add	x22, x19, :lo12:.LANCHOR2
	ldr	w1, [x22, 2444]
	cmp	w1, 39
	bhi	.L2785
	add	x25, x20, :lo12:.LANCHOR4
	mov	w21, 65535
	ldrh	w1, [x25, 1896]
	add	w0, w1, w0
	str	w0, [x22, 2464]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x25, 1104]
	cmp	w0, w21
	bne	.L2787
	ldrh	w1, [x22, 2472]
	cmp	w1, w0
	bne	.L2874
	ldr	w0, [x22, 2464]
	cmp	w0, 1024
	bhi	.L2789
	ldrh	w0, [x25, 776]
	cmp	w0, 63
	bhi	.L2874
.L2789:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w2, [x1, 2482]
	ldrh	w3, [x0, 776]
	strh	wzr, [x0, 1896]
	add	w2, w2, 64
	cmp	w3, w2
	bgt	.L2874
	str	wzr, [x1, 2464]
	ldr	w1, [x1, 2444]
	cbnz	w1, .L2790
	mov	w1, 6
.L2927:
	strh	w1, [x0, 1896]
.L2791:
	mov	w0, 32
	bl	List_get_gc_head_node
	and	w5, w0, 65535
	mov	w8, 65535
	cmp	w5, w8
	beq	.L2795
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22, 2484]
	cbz	w0, .L2793
	ldrh	w1, [x22, 2346]
	ubfiz	x5, x5, 1, 16
	ldrh	w3, [x22, 2276]
	ldr	x7, [x22, 2712]
	mul	w1, w1, w3
	ldrh	w2, [x7, x5]
	add	w1, w1, 1
	cmp	w2, w1
	bgt	.L2795
	add	w6, w0, 1
	str	wzr, [x22, 2452]
	and	w6, w6, 65535
	strh	w6, [x22, 2484]
	bl	List_get_gc_head_node
	and	w21, w0, 65535
	cmp	w21, w8
	beq	.L2795
	ubfiz	x25, x21, 1, 16
	ldrh	w4, [x7, x5]
	mov	w2, w21
	mov	w1, w6
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	ldrh	w3, [x7, x25]
	bl	printk
	ldrh	w0, [x22, 2484]
	cmp	w0, 40
	bls	.L2794
	ldr	x0, [x22, 2712]
	ldrh	w0, [x0, x25]
	cmp	w0, 32
	bls	.L2794
	strh	wzr, [x22, 2484]
.L2794:
	add	x0, x20, :lo12:.LANCHOR4
	mov	w1, 6
	strh	w1, [x0, 1896]
.L2787:
	ldr	w1, [x29, 124]
	mov	w0, 65535
	cmp	w1, 0
	ccmp	w21, w0, 0, eq
	bne	.L2809
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0, 776]
	cmp	w0, 24
	bhi	.L2886
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w0, 16
	ldrh	w22, [x1, 2344]
	bls	.L2811
	lsr	w22, w22, 5
.L2810:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1, 2480]
	cmp	w2, w0
	bcs	.L2814
	add	x3, x20, :lo12:.LANCHOR4
	mov	w2, 65535
	ldrh	w0, [x3, 880]
	cmp	w0, w2
	bne	.L2815
	ldrh	w2, [x1, 2472]
	cmp	w2, w0
	bne	.L2815
	ldrh	w0, [x3, 1896]
	cbnz	w0, .L2816
	ldr	w2, [x1, 2924]
	ldr	w3, [x3, 1068]
	add	w2, w2, w2, lsl 1
	cmp	w3, w2, lsr 2
	bcs	.L2817
.L2816:
	add	x20, x20, :lo12:.LANCHOR4
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w1, [x20, 1096]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2, 2480]
.L2818:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19, 2452]
.L2779:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L2781:
	ldrh	w1, [x3, 880]
	cmp	w1, w0
	beq	.L2784
	mov	w0, w21
	bl	FtlGcFreeTempBlock
	cbz	w0, .L2784
	mov	w0, w21
	b	.L2779
.L2782:
	ldrh	w1, [x0, 2472]
	cmp	w1, w2
	bne	.L2783
	ldrh	w3, [x0, 2476]
	cmp	w3, w1
	beq	.L2783
	ldrh	w2, [x0, 2478]
	cmp	w2, w1
	beq	.L2783
	mov	w1, -1
	strh	w4, [x0, 2472]
	strh	w3, [x0, 2474]
	strh	w2, [x0, 2476]
	strh	w1, [x0, 2478]
	b	.L2783
.L2790:
	cmp	w1, 5
	bhi	.L2791
	mov	w1, 18
	b	.L2927
.L2793:
	mov	w0, 1
	strh	w0, [x22, 2484]
.L2795:
	bl	GetSwlReplaceBlock
	and	w21, w0, 65535
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2787
	add	x0, x20, :lo12:.LANCHOR4
	strh	wzr, [x0, 1896]
.L2785:
	add	x0, x20, :lo12:.LANCHOR4
	mov	w21, 65535
	ldrh	w0, [x0, 1104]
	cmp	w0, w21
	bne	.L2787
.L2874:
	add	x25, x20, :lo12:.LANCHOR4
	mov	w0, 65535
	ldrh	w21, [x25, 880]
	cmp	w21, w0
	bne	.L2881
	add	x26, x19, :lo12:.LANCHOR2
	ldrh	w27, [x26, 2472]
	cmp	w27, w21
	bne	.L2787
	ldrh	w0, [x25, 776]
	mov	w1, 1024
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w1, cc
	ldr	w1, [x26, 2464]
	cmp	w1, w0
	bls	.L2787
	strh	wzr, [x25, 1896]
	str	wzr, [x26, 2464]
	bl	GetSwlReplaceBlock
	and	w21, w0, 65535
	cmp	w21, w27
	bne	.L2884
	ldrh	w1, [x25, 776]
	ldrh	w0, [x26, 2482]
	cmp	w1, w0
	bcs	.L2798
	mov	w0, 64
	bl	List_get_gc_head_node
	and	x0, x0, 65535
	cmp	w0, w21
	beq	.L2800
	ldr	w1, [x26, 2388]
	cbnz	w1, .L2801
	ldrh	w1, [x26, 2296]
	cmp	w1, 3
	beq	.L2801
	ldr	w1, [x26, 2392]
	cbnz	w1, .L2801
	ldr	w1, [x26, 2096]
	cbnz	w1, .L2801
	ldrb	w1, [x26, 1220]
	cbz	w1, .L2802
.L2801:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x2, [x1, 2712]
	ldrh	w3, [x2, x0, lsl 1]
	ldrh	w0, [x1, 2346]
	ldrh	w2, [x1, 2276]
	ldrh	w1, [x1, 2296]
	cmp	w1, 3
	mul	w2, w2, w0
	lsr	w0, w0, 1
	csel	w0, w0, wzr, eq
	add	w0, w0, w2
	cmp	w3, w0
	bgt	.L2804
	mov	w0, 0
	bl	List_get_gc_head_node
	add	x1, x19, :lo12:.LANCHOR2
	add	x2, x20, :lo12:.LANCHOR4
	and	w22, w0, 65535
	ldr	w0, [x1, 2924]
	ldr	w2, [x2, 1068]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2805
	mov	w0, 128
.L2928:
	strh	w0, [x1, 2482]
.L2806:
	mov	w0, 65535
	cmp	w22, w0
	beq	.L2800
.L2797:
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x2, x22, 1, 32
	add	x0, x20, :lo12:.LANCHOR4
	mov	w21, w22
	ldr	x4, [x1, 2696]
	ldr	x3, [x1, 2712]
	ldrh	w5, [x1, 2480]
	mov	w1, w22
	ldrh	w4, [x4, x2]
	ldrh	w3, [x3, x2]
	ldrh	w2, [x0, 776]
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
	bl	printk
	b	.L2800
.L2805:
	mov	w0, 160
	b	.L2928
.L2804:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
.L2929:
	strh	w1, [x0, 2482]
.L2800:
	bl	FtlGcReFreshBadBlk
	b	.L2787
.L2802:
	ldr	x1, [x26, 2712]
	ldrh	w0, [x1, x0, lsl 1]
	cmp	w0, 7
	bhi	.L2807
	mov	w0, 0
	bl	List_get_gc_head_node
	and	w22, w0, 65535
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0, 2482]
	b	.L2806
.L2807:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
	b	.L2929
.L2798:
	mov	w0, 80
	strh	w0, [x26, 2482]
	b	.L2800
.L2884:
	mov	w22, w21
	b	.L2797
.L2881:
	mov	w21, w0
	b	.L2787
.L2811:
	cmp	w0, 12
	bls	.L2812
	lsr	w22, w22, 4
	b	.L2810
.L2812:
	cmp	w0, 8
	bls	.L2810
	lsr	w22, w22, 2
	b	.L2810
.L2886:
	mov	w22, 1
	b	.L2810
.L2817:
	mov	w2, 18
	strh	w2, [x1, 2480]
	b	.L2818
.L2815:
	add	x0, x20, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0, 1096]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 2480]
.L2814:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0, 2096]
	cbz	w0, .L2888
	cmp	w23, 2
	bhi	.L2888
	add	w22, w22, 1
	and	w22, w22, 65535
.L2888:
	mov	w21, 65535
	b	.L2820
.L2809:
	add	x1, x20, :lo12:.LANCHOR4
	ldrh	w3, [x1, 880]
	cmp	w3, w0
	bne	.L2821
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0, 2472]
	cmp	w2, w3
	bne	.L2821
	cmp	w21, w2
	bne	.L2821
	ldrh	w2, [x1, 1104]
	cmp	w2, w21
	bne	.L2821
	ldrh	w3, [x1, 776]
	ldrh	w2, [x0, 2480]
	str	wzr, [x0, 2452]
	cmp	w3, w2
	bls	.L2823
	ldrh	w2, [x1, 1896]
	cbnz	w2, .L2824
	ldr	w2, [x0, 2924]
	ldr	w1, [x1, 1068]
	add	w2, w2, w2, lsl 1
	cmp	w1, w2, lsr 2
	bcs	.L2825
.L2824:
	add	x0, x20, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0, 1096]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 2480]
.L2826:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1, 2712]
	ldrh	w0, [x1, x0]
	cmp	w0, 4
	bls	.L2823
	add	x20, x20, :lo12:.LANCHOR4
.L2930:
	ldrh	w0, [x20, 1896]
	b	.L2779
.L2825:
	mov	w1, 18
	strh	w1, [x0, 2480]
	b	.L2826
.L2823:
	add	x22, x20, :lo12:.LANCHOR4
	ldrh	w0, [x22, 1896]
	cbnz	w0, .L2821
	ldrh	w5, [x22, 1096]
	add	x6, x19, :lo12:.LANCHOR2
	add	w0, w5, w5, lsl 1
	asr	w0, w0, 2
	strh	w0, [x6, 2480]
	mov	w0, 0
	bl	List_get_gc_head_node
	ldr	x1, [x6, 2712]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x6, 2276]
	ldrh	w1, [x1, x0]
	ldrh	w0, [x6, 2346]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L2828
	ldrh	w0, [x22, 776]
	sub	w5, w5, #1
	cmp	w0, w5
	blt	.L2828
	bl	FtlReadRefresh
	ldrh	w0, [x22, 1896]
	b	.L2779
.L2828:
	cbnz	w1, .L2821
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x20, 776]
	add	w0, w0, 1
	b	.L2779
.L2821:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0, 2096]
	cmp	w0, 0
	cset	w22, ne
	add	w22, w22, 1
.L2820:
	add	x0, x20, :lo12:.LANCHOR4
	mov	w2, 65535
	ldrh	w1, [x0, 1104]
	cmp	w1, w2
	bne	.L2830
	cmp	w21, w1
	beq	.L2831
	strh	w21, [x0, 1104]
.L2832:
	add	x5, x20, :lo12:.LANCHOR4
	mov	w1, 65535
	ldrh	w0, [x5, 1104]
	strb	wzr, [x5, 1112]
	cmp	w0, w1
	beq	.L2830
	bl	IsBlkInGcList
	cbz	w0, .L2835
	mov	w0, -1
	strh	w0, [x5, 1104]
.L2835:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0, 1220]
	cbz	w0, .L2836
	add	x0, x20, :lo12:.LANCHOR4
	add	x3, x0, 1104
	ldrh	w0, [x0, 1104]
	bl	ftl_get_blk_mode
	strb	w0, [x3, 8]
.L2836:
	add	x12, x20, :lo12:.LANCHOR4
	mov	w0, 65535
	add	x13, x12, 1104
	ldrh	w1, [x12, 1104]
	cmp	w1, w0
	beq	.L2830
	mov	x0, x13
	bl	make_superblock
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x12, 1104]
	strh	wzr, [x12, 1106]
	strb	wzr, [x12, 1110]
	ldr	x0, [x0, 2712]
	strh	wzr, [x12, 1898]
	ldrh	w0, [x0, x1, lsl 1]
	strh	w0, [x12, 1900]
.L2830:
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w1, [x0, 1104]
	ldrh	w2, [x0, 784]
	cmp	w2, w1
	beq	.L2837
	ldrh	w2, [x0, 832]
	cmp	w2, w1
	beq	.L2837
	ldrh	w0, [x0, 880]
	cmp	w0, w1
	bne	.L2871
.L2837:
	add	x0, x20, :lo12:.LANCHOR4
	mov	w1, -1
	strh	w1, [x0, 1104]
.L2871:
	add	x25, x20, :lo12:.LANCHOR4
	mov	w0, 65535
	ldrh	w26, [x25, 1104]
	cmp	w26, w0
	bne	.L2839
	add	x23, x19, :lo12:.LANCHOR2
	mov	w27, 2
	str	wzr, [x23, 2452]
.L2840:
	ldrh	w5, [x23, 2484]
	mov	w0, w5
	bl	List_get_gc_head_node
	and	w6, w0, 65535
	strh	w6, [x25, 1104]
	cmp	w6, w26
	bne	.L2841
	strh	wzr, [x23, 2484]
	mov	w0, 8
	b	.L2779
.L2831:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1, 2472]
	cmp	w0, w21
	beq	.L2832
	ldr	x2, [x1, 2712]
	ubfiz	x0, x0, 1, 16
	ldrh	w0, [x2, x0]
	cbnz	w0, .L2833
	mov	w0, -1
	strh	w0, [x1, 2472]
.L2833:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w2, [x1, 2472]
	strh	w2, [x0, 1104]
	mov	w0, -1
	strh	w0, [x1, 2472]
	b	.L2832
.L2841:
	mov	w0, w6
	bl	IsBlkInGcList
	add	w5, w5, 1
	cbz	w0, .L2842
	strh	w5, [x23, 2484]
	b	.L2840
.L2842:
	ldrh	w4, [x23, 2276]
	ubfiz	x1, x6, 1, 16
	ldrh	w0, [x23, 2344]
	and	w5, w5, 65535
	ldr	x2, [x23, 2712]
	strh	w5, [x23, 2484]
	mul	w0, w0, w4
	ldrh	w3, [x2, x1]
	sdiv	w4, w0, w27
	cmp	w3, w4
	bgt	.L2844
	cmp	w5, 48
	bls	.L2845
	cmp	w3, 8
	bls	.L2845
	ldrh	w3, [x25, 1712]
	cmp	w3, 35
	bhi	.L2845
.L2844:
	strh	wzr, [x23, 2484]
.L2845:
	ldrh	w1, [x2, x1]
	cmp	w0, w1
	bgt	.L2846
	cmp	w21, w26
	bne	.L2846
	ldrh	w0, [x23, 2484]
	cmp	w0, 3
	bhi	.L2846
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, -1
	strh	wzr, [x23, 2484]
	strh	w0, [x20, 1104]
	b	.L2930
.L2846:
	cbnz	w1, .L2847
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x23, 2484]
	add	w0, w0, 1
	strh	w0, [x23, 2484]
	b	.L2840
.L2847:
	add	x0, x19, :lo12:.LANCHOR2
	add	x3, x20, :lo12:.LANCHOR4
	add	x3, x3, 1104
	ldrb	w0, [x0, 1220]
	strb	wzr, [x3, 8]
	cbz	w0, .L2848
	mov	w0, w6
	bl	ftl_get_blk_mode
	strb	w0, [x3, 8]
.L2848:
	add	x12, x20, :lo12:.LANCHOR4
	add	x13, x12, 1104
	mov	x0, x13
	bl	make_superblock
	ldrh	w1, [x12, 1104]
	add	x0, x19, :lo12:.LANCHOR2
	strh	wzr, [x12, 1898]
	ldr	x0, [x0, 2712]
	ldrh	w0, [x0, x1, lsl 1]
	strh	w0, [x12, 1900]
	strh	wzr, [x12, 1106]
	strb	wzr, [x12, 1110]
.L2839:
	ldr	w0, [x29, 124]
	cmp	w0, 1
	bne	.L2849
	bl	FtlReadRefresh
.L2849:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 1
	str	w1, [x0, 2448]
	ldrb	w1, [x0, 1220]
	ldrh	w25, [x0, 2344]
	cbz	w1, .L2850
	add	x1, x20, :lo12:.LANCHOR4
	ldrb	w1, [x1, 1112]
	cmp	w1, 1
	bne	.L2850
	ldrh	w25, [x0, 2346]
.L2850:
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0, 1106]
	add	w1, w0, w22
	cmp	w1, w25
	ble	.L2851
	sub	w22, w25, w0
	and	w22, w22, 65535
.L2851:
	add	x24, x20, :lo12:.LANCHOR4
	mov	w26, 0
	add	x27, x24, 880
.L2852:
	cmp	w22, w26, uxth
	bls	.L2859
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w5, [x24, 1106]
	add	x3, x24, 1120
	mov	w23, 0
	add	w5, w5, w26
	mov	w0, 0
	ldrh	w9, [x1, 2276]
	mov	w8, 65535
	mov	w7, 56
	b	.L2860
.L2854:
	ldrh	w2, [x3]
	cmp	w2, w8
	beq	.L2853
	ldr	x6, [x1, 2552]
	orr	w2, w5, w2, lsl 10
	umaddl	x6, w23, w7, x6
	add	w23, w23, 1
	and	w23, w23, 65535
	str	w2, [x6, 4]
.L2853:
	add	w0, w0, 1
	add	x3, x3, 2
	and	w0, w0, 65535
.L2860:
	cmp	w0, w9
	bne	.L2854
	add	x3, x19, :lo12:.LANCHOR2
	ldrb	w2, [x24, 1112]
	mov	w1, w23
	mov	x28, x3
	ldr	x0, [x3, 2552]
	bl	FlashReadPages
	mov	w0, 56
	umull	x0, w23, w0
	mov	x23, 0
	str	x0, [x29, 112]
.L2855:
	ldr	x0, [x29, 112]
	cmp	x0, x23
	bne	.L2858
	add	w26, w26, 1
	b	.L2852
.L2858:
	ldr	x0, [x28, 2552]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	cmn	w0, #1
	beq	.L2856
	ldr	x5, [x1, 16]
	mov	w1, 61589
	ldrh	w0, [x5]
	cmp	w0, w1
	bne	.L2856
	ldr	w0, [x5, 8]
	mov	w2, 0
	add	x1, x29, 136
	str	x5, [x29, 104]
	bl	log2phys
	ldr	x0, [x28, 2552]
	ldr	w1, [x29, 136]
	add	x0, x0, x23
	ldr	x5, [x29, 104]
	and	w1, w1, 2147483647
	ldr	w2, [x0, 4]
	cmp	w1, w2
	bne	.L2856
	ldrh	w1, [x24, 1898]
	mov	w2, 56
	ldr	x7, [x28, 2528]
	add	w1, w1, 1
	strh	w1, [x24, 1898]
	ldr	w1, [x24, 1680]
	ldr	w0, [x0, 24]
	str	w2, [x29, 120]
	nop // between mem op and mult-accumulate
	umaddl	x1, w1, w2, x7
	stp	x5, x1, [x29, 96]
	str	w0, [x1, 24]
	bl	Ftl_get_new_temp_ppa
	ldp	x5, x1, [x29, 96]
	ldr	w2, [x29, 120]
	str	w0, [x1, 4]
	ldr	w0, [x24, 1680]
	ldr	x1, [x28, 2528]
	umaddl	x0, w0, w2, x1
	ldr	x1, [x28, 2552]
	add	x1, x1, x23
	ldr	x2, [x1, 8]
	str	x2, [x0, 8]
	ldr	x1, [x1, 16]
	str	x1, [x0, 16]
	ldr	w0, [x29, 136]
	mov	w1, 1
	str	w0, [x5, 12]
	ldrh	w0, [x27]
	strh	w0, [x5, 2]
	ldr	w0, [x28, 2400]
	str	w0, [x5, 4]
	ldr	w0, [x24, 1680]
	add	w0, w0, 1
	str	w0, [x24, 1680]
	ldr	x0, [x28, 2552]
	add	x0, x0, x23
	bl	FtlGcBufAlloc
	ldrb	w0, [x28, 1220]
	cbnz	w0, .L2857
	ldrb	w1, [x27, 7]
	ldr	w0, [x24, 1680]
	cmp	w1, w0
	beq	.L2857
	ldrh	w0, [x27, 4]
	cbnz	w0, .L2856
.L2857:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2856
	add	x20, x20, :lo12:.LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x20, 1106]
	strh	w0, [x20, 1104]
	str	wzr, [x19, 2448]
	b	.L2930
.L2856:
	add	x23, x23, 56
	b	.L2855
.L2859:
	add	x23, x20, :lo12:.LANCHOR4
	ldrh	w0, [x23, 1106]
	add	w22, w22, w0
	and	w22, w22, 65535
	strh	w22, [x23, 1106]
	cmp	w25, w22
	bhi	.L2861
	ldr	w0, [x23, 1680]
	cbz	w0, .L2862
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2862
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x23, 1896]
	str	wzr, [x19, 2448]
	b	.L2779
.L2862:
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w1, [x0, 1898]
	cbnz	w1, .L2863
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w3, [x0, 1104]
	ldr	x2, [x1, 2712]
	ldrh	w2, [x2, x3, lsl 1]
	cbz	w2, .L2863
	mov	x25, x1
	mov	x23, x0
	mov	w22, 0
.L2864:
	ldr	w0, [x25, 2924]
	cmp	w22, w0
	bcs	.L2869
	mov	w2, 0
	add	x1, x29, 140
	mov	w0, w22
	bl	log2phys
	ldr	w0, [x29, 140]
	cmn	w0, #1
	beq	.L2865
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x23, 1104]
	cmp	w1, w0, uxth
	bne	.L2865
.L2869:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1, 2924]
	cmp	w22, w0
	bcc	.L2863
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x1, [x1, 2712]
	ldrh	w2, [x0, 1104]
	strh	wzr, [x1, x2, lsl 1]
	ldrh	w0, [x0, 1104]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2863:
	add	x0, x20, :lo12:.LANCHOR4
	mov	w1, -1
	strh	w1, [x0, 1104]
.L2861:
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w1, [x0, 776]
	cmp	w1, 2
	bhi	.L2870
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w22, [x0, 2344]
	b	.L2871
.L2865:
	add	w22, w22, 1
	b	.L2864
.L2870:
	ldrh	w0, [x0, 1896]
	add	x19, x19, :lo12:.LANCHOR2
	cmp	w0, 0
	csinc	w0, w0, w1, ne
	str	wzr, [x19, 2448]
	b	.L2779
.L2878:
	mov	w0, 0
	b	.L2779
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x0, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w24, [x0, 2928]
	cbnz	w24, .L2933
	ldr	w1, [x0, 2496]
	cbz	w1, .L2933
	ldrb	w0, [x0, 1220]
	adrp	x23, .LANCHOR4
	add	x2, x23, :lo12:.LANCHOR4
	ldr	x20, [x2, 1904]
	cbz	w0, .L2958
	ldrb	w0, [x20, 8]
	cmp	w0, 1
	cset	w25, eq
.L2935:
	add	x19, x22, :lo12:.LANCHOR2
	ldrb	w3, [x20, 9]
	adrp	x26, .LC159
	mov	w2, w25
	mov	w21, 0
	mov	w27, 56
	ldr	x0, [x19, 2560]
	add	x26, x26, :lo12:.LC159
	bl	FlashProgPages
.L2936:
	ldr	w0, [x19, 2496]
	cmp	w21, w0
	bcc	.L2943
.L2955:
	add	x22, x22, :lo12:.LANCHOR2
	str	wzr, [x22, 2496]
.L2933:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2958:
	mov	w25, 0
	b	.L2935
.L2943:
	umull	x28, w21, w27
	ldr	x0, [x19, 2560]
	add	x3, x0, x28
	ldr	w0, [x0, x28]
	cmn	w0, #1
	bne	.L2937
	add	x19, x22, :lo12:.LANCHOR2
	add	x23, x23, :lo12:.LANCHOR4
.L2938:
	ldr	w0, [x19, 2496]
	cmp	w24, w0
	bcc	.L2953
	add	x20, x22, :lo12:.LANCHOR2
	mov	w19, 16386
.L2956:
	ldrh	w0, [x20, 2486]
	cbz	w0, .L2955
	mov	w1, 1
	mov	w0, w1
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L2956
	b	.L2955
.L2937:
	ldr	w0, [x3, 4]
	cbnz	w25, .L2939
.L2972:
	str	w0, [x29, 108]
	mov	w2, 1
	ldr	w0, [x3, 24]
	add	x1, x29, 108
	bl	log2phys
	ldr	x0, [x19, 2560]
	add	x0, x0, x28
	ldr	x0, [x0, 16]
	ldr	w0, [x0, 12]
	cmn	w0, #1
	beq	.L2941
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x19, 2712]
	and	w1, w0, 65535
	ubfiz	x0, x1, 1, 16
	mov	w28, w1
	ldrh	w0, [x2, x0]
	cbnz	w0, .L2942
	mov	w2, 0
	mov	x0, x26
	bl	printk
.L2942:
	mov	w0, w28
	bl	decrement_vpc_count
.L2941:
	add	w21, w21, 1
	b	.L2936
.L2939:
	orr	w0, w0, -2147483648
	b	.L2972
.L2953:
	mov	w21, 56
	ldr	x0, [x19, 2560]
	mov	w1, -1
	mov	w26, 1
	umull	x21, w24, w21
	str	w1, [x0, x21]
.L2944:
	ldr	x0, [x19, 2560]
	add	x3, x0, x21
	ldr	w0, [x0, x21]
	cmn	w0, #1
	ldr	w0, [x3, 4]
	beq	.L2948
	cbnz	w25, .L2949
.L2973:
	str	w0, [x29, 108]
	mov	w2, 1
	ldr	w0, [x3, 24]
	add	x1, x29, 108
	bl	log2phys
	ldr	x0, [x19, 2560]
	add	x21, x0, x21
	ldr	x0, [x21, 16]
	ldr	w0, [x0, 12]
	cmn	w0, #1
	beq	.L2951
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x19, 2712]
	and	w1, w0, 65535
	ubfiz	x0, x1, 1, 16
	mov	w21, w1
	ldrh	w0, [x2, x0]
	cbnz	w0, .L2952
	adrp	x0, .LC159
	mov	w2, 0
	add	x0, x0, :lo12:.LC159
	bl	printk
.L2952:
	mov	w0, w21
	bl	decrement_vpc_count
.L2951:
	add	w24, w24, 1
	b	.L2938
.L2948:
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	bne	.L2945
	ldr	x2, [x19, 2712]
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x20, 4]
	ldrh	w0, [x2, x1]
	sub	w0, w0, w3
	strh	w0, [x2, x1]
	strb	wzr, [x20, 6]
	ldrh	w0, [x19, 2344]
	strh	w0, [x20, 2]
	strh	wzr, [x20, 4]
.L2945:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L2946
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2946:
	ldr	w0, [x23, 1248]
	add	w0, w0, 1
	str	w0, [x23, 1248]
	ldr	x0, [x19, 2560]
	add	x0, x0, x21
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	ldr	x1, [x19, 2560]
	mov	w2, w25
	str	w0, [x29, 108]
	add	x1, x1, x21
	str	w0, [x1, 4]
	mov	w1, 1
	ldrb	w3, [x20, 9]
	ldr	x0, [x19, 2560]
	add	x0, x0, x21
	bl	FlashProgPages
	ldr	x0, [x19, 2560]
	ldr	w0, [x0, x21]
	cmn	w0, #1
	bne	.L2947
	str	w26, [x19, 2928]
.L2947:
	ldr	w0, [x19, 2928]
	cbz	w0, .L2944
	b	.L2933
.L2949:
	orr	w0, w0, -2147483648
	b	.L2973
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	adrp	x0, .LANCHOR2+2928
	ldr	w0, [x0, #:lo12:.LANCHOR2+2928]
	cbnz	w0, .L2977
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR1+3448
	add	x29, sp, 0
	str	x19, [sp, 16]
	ldr	w19, [x0, #:lo12:.LANCHOR1+3448]
	cmp	w19, 1
	bne	.L2975
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, w19
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2975:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2977:
	mov	w0, 0
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	adrp	x0, .LANCHOR1+3448
	ldr	w0, [x0, #:lo12:.LANCHOR1+3448]
	cmp	w0, 1
	bne	.L2983
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlSysFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L2983:
	mov	w0, 0
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w21, w0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x22, x20, :lo12:.LANCHOR2
	ldr	w0, [x22, 1224]
	cmp	w0, w21
	bls	.L3000
	mov	w19, w1
	cmp	w0, w1
	bcc	.L3000
	add	w1, w21, w1
	cmp	w0, w1
	bcc	.L3000
	cmp	w19, 31
	bhi	.L2992
.L3008:
	mov	w0, 0
.L2990:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L2992:
	ldr	w0, [x22, 2928]
	cbnz	w0, .L3008
	bl	FtlCacheWriteBack
	ldrh	w0, [x22, 2350]
	udiv	w22, w21, w0
	msub	w21, w0, w22, w21
	ands	w21, w21, 65535
	beq	.L2994
	sub	w21, w0, w21
	add	w22, w22, 1
	cmp	w21, w19
	csel	w21, w21, w19, ls
	sub	w19, w19, w21, uxth
.L2994:
	adrp	x21, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	str	w0, [x29, 60]
.L2995:
	ldrh	w0, [x20, 2350]
	cmp	w19, w0
	bcs	.L2997
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0, 1912]
	cmp	w1, 32
	bls	.L3008
	str	wzr, [x0, 1912]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3008
.L2997:
	mov	w2, 0
	add	x1, x29, 56
	mov	w0, w22
	bl	log2phys
	ldr	w0, [x29, 56]
	cmn	w0, #1
	beq	.L2996
	ldr	w0, [x21, 1912]
	mov	w2, 1
	add	x1, x29, 60
	add	w0, w0, 1
	str	w0, [x21, 1912]
	ldr	w0, [x20, 2412]
	add	w0, w0, 1
	str	w0, [x20, 2412]
	mov	w0, w22
	bl	log2phys
	ldr	w0, [x29, 56]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2996:
	ldrh	w0, [x20, 2350]
	add	w22, w22, 1
	sub	w19, w19, w0
	b	.L2995
.L3000:
	mov	w0, -1
	b	.L2990
	.size	ftl_discard, .-ftl_discard
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x2, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	ldrh	w1, [x2, 2344]
	ldr	w2, [x2, 2928]
	cbz	w2, .L3010
.L3047:
	mov	w0, 0
.L3009:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L3010:
	adrp	x20, .LANCHOR4
	add	x2, x20, :lo12:.LANCHOR4
	mov	w3, 65535
	ldrh	w21, [x2, 880]
	cmp	w21, w3
	bne	.L3012
.L3021:
	add	x21, x20, :lo12:.LANCHOR4
	mov	w0, 65535
	add	x22, x21, 880
	ldrh	w1, [x21, 880]
	str	wzr, [x21, 1704]
	cmp	w1, w0
	beq	.L3047
	bl	FtlCacheWriteBack
	mov	w23, 0
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x22, 7]
	ldrh	w3, [x21, 880]
	mov	x22, x0
	mov	w26, 12
	ldrh	w4, [x0, 2344]
	ldr	x2, [x0, 2712]
	mul	w1, w1, w4
	strh	w1, [x2, x3, lsl 1]
	ldr	w2, [x0, 2404]
	ldrh	w1, [x21, 1714]
	add	w1, w1, w2
	str	w1, [x0, 2404]
.L3022:
	ldrh	w0, [x21, 1714]
	cmp	w0, w23
	bhi	.L3026
	mov	w0, -1
	bl	decrement_vpc_count
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0, 1220]
	cbz	w0, .L3027
	ldrh	w1, [x21, 880]
	adrp	x0, .LC160
	add	x0, x0, :lo12:.LC160
	bl	printk
.L3027:
	add	x0, x20, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0, 880]
	ldr	x1, [x1, 2712]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1, x2]
	cbz	w1, .L3028
	bl	INSERT_DATA_LIST
.L3029:
	add	x21, x20, :lo12:.LANCHOR4
	mov	w22, -1
	strh	wzr, [x21, 1714]
	strh	w22, [x21, 880]
	strh	wzr, [x21, 1712]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w22, [x21, 1104]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1, 2096]
	cbz	w0, .L3030
	ldr	w0, [x1, 2444]
	cmp	w0, 39
	bhi	.L3030
	ldrh	w0, [x21, 1096]
	ldrh	w2, [x21, 776]
	cmp	w2, w0
	bcs	.L3047
	ubfiz	w0, w0, 1, 15
	strh	w0, [x1, 2480]
	b	.L3047
.L3012:
	cbz	w0, .L3015
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w4, [x0, 3452]
	cmp	w4, w3
	beq	.L3016
.L3017:
	mov	w1, 2
.L3015:
	add	x0, x20, :lo12:.LANCHOR4
	add	x0, x0, 880
	bl	FtlGcScanTempBlk
	str	w0, [x29, 108]
	cmn	w0, #1
	beq	.L3018
	add	x19, x19, :lo12:.LANCHOR2
	ubfiz	x21, x21, 1, 16
	ldr	x1, [x19, 2696]
	ldrh	w0, [x1, x21]
	cmp	w0, 4
	bls	.L3019
	sub	w0, w0, #5
	strh	w0, [x1, x21]
	mov	w0, 1
	bl	FtlEctTblFlush
.L3019:
	add	x0, x20, :lo12:.LANCHOR4
	ldr	w1, [x0, 1704]
	cbnz	w1, .L3020
	ldr	w1, [x0, 1248]
	add	w1, w1, 1
	str	w1, [x0, 1248]
	ldr	w0, [x29, 108]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3020:
	add	x20, x20, :lo12:.LANCHOR4
	str	wzr, [x20, 1704]
.L3032:
	mov	w0, 1
	b	.L3009
.L3016:
	strh	wzr, [x0, 3452]
	ldrh	w0, [x2, 776]
	cmp	w0, 17
	bhi	.L3017
	b	.L3015
.L3018:
	adrp	x0, .LANCHOR1+3452
	ldrh	w1, [x0, #:lo12:.LANCHOR1+3452]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3032
	b	.L3021
.L3026:
	umull	x25, w23, w26
	ldr	x27, [x22, 2512]
	ldr	w1, [x22, 2924]
	add	x24, x27, x25
	ldr	w0, [x24, 8]
	cmp	w0, w1
	bcc	.L3023
.L3044:
	ldrh	w0, [x21, 880]
	b	.L3045
.L3023:
	add	x1, x29, 108
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x27, x25]
	ldr	w1, [x29, 108]
	cmp	w0, w1
	bne	.L3025
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	mov	w25, w0
	ldr	w0, [x24, 8]
	mov	w2, 1
	add	x1, x24, 4
	bl	log2phys
	mov	w0, w25
.L3045:
	bl	decrement_vpc_count
	b	.L3024
.L3025:
	ldr	w0, [x24, 4]
	cmp	w1, w0
	bne	.L3044
.L3024:
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L3022
.L3028:
	bl	INSERT_FREE_LIST
	b	.L3029
.L3030:
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w0, [x20, 1096]
	ldrh	w1, [x20, 776]
	add	w2, w0, w0, lsl 1
	cmp	w1, w2, lsr 2
	ble	.L3047
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w1, [x19, 1220]
	cbz	w1, .L3031
	sub	w0, w0, #2
.L3046:
	strh	w0, [x19, 2480]
	b	.L3047
.L3031:
	mov	w0, 20
	b	.L3046
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	str	x21, [sp, 32]
	add	x21, x19, 880
	ldrh	w1, [x20, 2344]
	mov	x0, x21
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19, 882]
	ldrh	w0, [x20, 2344]
	cmp	w1, w0
	bcc	.L3048
	add	x0, x19, 928
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19, 1704]
.L3048:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	add	x20, x19, 784
	add	x19, x19, 832
	mov	x0, x20
	str	wzr, [x19, 928]
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
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	stp	x29, x30, [sp, -64]!
	mov	w0, -1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR4
	stp	x21, x22, [sp, 32]
	adrp	x20, .LANCHOR2
	add	x21, x19, :lo12:.LANCHOR4
	add	x22, x20, :lo12:.LANCHOR2
	str	x23, [sp, 48]
	strh	w0, [x21, 1754]
	ldrh	w0, [x22, 2280]
	strh	wzr, [x21, 1756]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x21, 1072]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3054
.L3056:
	mov	w21, -1
.L3053:
	mov	w0, w21
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L3054:
	bl	FtlLoadSysInfo
	mov	w21, w0
	cbnz	w0, .L3056
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldr	x1, [x22, 2792]
	mov	w0, 0
	ldrh	w3, [x22, 2382]
	add	x1, x1, 4
.L3057:
	cmp	w0, w3
	bge	.L3062
	ldr	w2, [x1], 16
	tbz	w2, #31, .L3058
.L3062:
	add	x2, x19, :lo12:.LANCHOR4
	cmp	w0, w3
	ldrh	w1, [x2, 724]
	add	w1, w1, 1
	strh	w1, [x2, 724]
	bge	.L3069
.L3059:
	add	x0, x19, :lo12:.LANCHOR4
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w3, [x0, 784]
	ldr	x5, [x1, 2712]
	ldrh	w6, [x0, 788]
	lsl	x3, x3, 1
	ldrh	w4, [x5, x3]
	sub	w4, w4, w6
	strh	w4, [x5, x3]
	strb	wzr, [x0, 790]
	ldrh	w3, [x1, 2344]
	strh	w3, [x0, 786]
	ldrh	w3, [x0, 832]
	ldr	x5, [x1, 2712]
	strh	wzr, [x0, 788]
	lsl	x3, x3, 1
	ldrh	w6, [x0, 836]
	ldrh	w4, [x5, x3]
	sub	w4, w4, w6
	strh	w4, [x5, x3]
	strb	wzr, [x0, 838]
	ldrh	w1, [x1, 2344]
	strh	w1, [x0, 834]
	ldrh	w1, [x0, 726]
	strh	wzr, [x0, 836]
	add	w1, w1, 1
	strh	w1, [x0, 726]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L3063
.L3058:
	add	w0, w0, 1
	b	.L3057
.L3069:
	ldrh	w0, [x2, 1756]
	cbnz	w0, .L3059
.L3063:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w1, 65535
	add	x23, x20, 784
	ldrh	w0, [x20, 784]
	cmp	w0, w1
	beq	.L3064
	ldrh	w1, [x20, 788]
	cbnz	w1, .L3064
	ldrh	w1, [x20, 836]
	add	x22, x20, 832
	cbnz	w1, .L3064
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x20, 832]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x23
	bl	allocate_new_data_superblock
	mov	x0, x22
	bl	allocate_new_data_superblock
.L3064:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0, #:lo12:.LANCHOR0+88]
	cbnz	w0, .L3065
	add	x19, x19, :lo12:.LANCHOR4
	ldrh	w0, [x19, 724]
	tst	x0, 31
	bne	.L3053
.L3065:
	bl	FtlVpcCheckAndModify
	b	.L3053
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	ldr	w0, [x20, 2928]
	cbnz	w0, .L3073
	ldr	x0, [x20, 2776]
	mov	w1, 0
	ldrh	w2, [x20, 2380]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x20, 2768]
	mov	w1, 0
	ldrh	w2, [x20, 2380]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x20, 2280]
	str	wzr, [x20, 2396]
	str	wzr, [x20, 2400]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3074
	bl	FtlMakeBbt
.L3074:
	mov	w4, 23752
	add	x1, x19, :lo12:.LANCHOR2
	mov	w0, 0
	movk	w4, 0xa0f, lsl 16
.L3075:
	ldrh	w2, [x1, 2350]
	cmp	w0, w2, lsl 7
	blt	.L3076
	ldrh	w21, [x1, 2284]
	add	x22, x19, :lo12:.LANCHOR2
	mov	w20, 0
.L3077:
	ldrh	w0, [x22, 2286]
	cmp	w0, w21
	bhi	.L3078
	ldrh	w0, [x22, 2276]
	sub	w1, w20, #3
	cmp	w1, w0, lsl 1
	blt	.L3079
	udiv	w0, w20, w0
	ldr	w20, [x22, 2376]
	add	w0, w0, w20
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22, 2280]
	mov	w20, 0
	bl	FtlFreeSysBlkQueueInit
	ldrh	w21, [x22, 2284]
	add	x22, x19, :lo12:.LANCHOR2
.L3080:
	ldrh	w0, [x22, 2286]
	cmp	w0, w21
	bhi	.L3081
.L3079:
	add	x23, x19, :lo12:.LANCHOR2
	mov	w22, 0
	mov	w21, 0
.L3082:
	ldrh	w0, [x23, 2284]
	cmp	w0, w21
	bhi	.L3083
	ldrh	w0, [x23, 2286]
	adrp	x21, .LANCHOR4
	ldr	w2, [x23, 2288]
	add	x5, x21, :lo12:.LANCHOR4
	str	w0, [x23, 2468]
	ldrh	w0, [x23, 2276]
	udiv	w4, w2, w0
	ubfx	x3, x4, 5, 16
	str	w4, [x23, 2924]
	add	w1, w3, 36
	strh	w1, [x5, 1096]
	mov	w1, 24
	mul	w1, w0, w1
	cmp	w22, w1
	ble	.L3084
	sub	w2, w2, w22
	udiv	w2, w2, w0
	str	w2, [x23, 2924]
	lsr	w2, w2, 5
	add	w2, w2, 24
	strh	w2, [x5, 1096]
.L3084:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w1, [x1, 2096]
	cmp	w1, 1
	bne	.L3085
	udiv	w2, w22, w0
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w5, [x1, 1096]
	add	w2, w2, w5
	add	w2, w5, w2, asr 2
	strh	w2, [x1, 1096]
.L3085:
	add	x1, x19, :lo12:.LANCHOR2
	ldrb	w1, [x1, 1220]
	cbz	w1, .L3086
	udiv	w2, w22, w0
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w5, [x1, 1096]
	add	w2, w2, w5
	add	w2, w5, w2, asr 2
	strh	w2, [x1, 1096]
.L3086:
	add	x6, x19, :lo12:.LANCHOR2
	ldrh	w1, [x6, 2338]
	cbz	w1, .L3088
	add	x2, x21, :lo12:.LANCHOR4
	ldrh	w5, [x2, 1096]
	add	w5, w5, w1, lsr 1
	strh	w5, [x2, 1096]
	mul	w5, w1, w0
	cmp	w22, w5
	bge	.L3088
	add	w1, w1, 32
	str	w4, [x6, 2924]
	add	w1, w3, w1
	strh	w1, [x2, 1096]
.L3088:
	add	x23, x19, :lo12:.LANCHOR2
	add	x2, x21, :lo12:.LANCHOR4
	ldr	w1, [x23, 2924]
	ldrh	w3, [x2, 1096]
	sub	w1, w1, w3
	mul	w0, w1, w0
	ldrh	w1, [x23, 2344]
	str	w0, [x2, 1092]
	mul	w0, w1, w0
	ldrh	w1, [x23, 2350]
	str	w0, [x23, 2924]
	mul	w0, w1, w0
	str	w0, [x23, 1224]
	bl	FtlBbmTblFlush
	ldrh	w0, [x23, 2358]
	add	w1, w20, w22
	ldr	w2, [x23, 2292]
	add	w0, w0, w2, lsr 3
	cmp	w1, w0
	bls	.L3090
	adrp	x0, .LC161
	lsr	w2, w2, 5
	add	x0, x0, :lo12:.LC161
	bl	printk
.L3090:
	add	x23, x19, :lo12:.LANCHOR2
	add	x22, x21, :lo12:.LANCHOR4
	add	x20, x22, 784
	mov	w1, 0
	mov	w24, -1
	ldr	x0, [x23, 2712]
	ldrh	w2, [x23, 2286]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w0, 1
	strb	w0, [x20, 8]
	adrp	x0, .LANCHOR0+80
	ldrh	w2, [x23, 2284]
	strh	w24, [x22, 1104]
	mov	w1, 255
	ldr	x0, [x0, #:lo12:.LANCHOR0+80]
	strh	wzr, [x22, 1106]
	lsr	w2, w2, 3
	strb	wzr, [x22, 1110]
	strb	wzr, [x22, 1112]
	strh	wzr, [x20, 2]
	strb	wzr, [x20, 6]
	strh	wzr, [x22, 784]
	str	wzr, [x22, 1068]
	bl	ftl_memset
.L3091:
	mov	x0, x20
	bl	make_superblock
	ldrb	w1, [x20, 7]
	ldrh	w0, [x20]
	cbnz	w1, .L3092
	ldr	x1, [x23, 2712]
	ubfiz	x0, x0, 1, 16
	strh	w24, [x1, x0]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L3091
.L3076:
	ldr	x5, [x1, 2616]
	ubfiz	x3, x0, 2, 16
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w2, [x5, x3]
	ldr	x2, [x1, 2624]
	str	w4, [x2, x3]
	b	.L3075
.L3078:
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w20, w20, w0
	and	w21, w21, 65535
	and	w20, w20, 65535
	b	.L3077
.L3081:
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w20, w20, w0
	and	w21, w21, 65535
	and	w20, w20, 65535
	b	.L3080
.L3083:
	mov	w0, w21
	mov	w1, 0
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w22, w22, w0
	and	w21, w21, 65535
	and	w22, w22, 65535
	b	.L3082
.L3092:
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x0, x0, 1, 16
	ldrh	w3, [x20, 4]
	mov	x13, x1
	mov	w14, -1
	ldr	w2, [x1, 2396]
	str	w2, [x20, 12]
	add	w2, w2, 1
	str	w2, [x1, 2396]
	ldr	x2, [x1, 2712]
	strh	w3, [x2, x0]
	add	x0, x22, 832
	mov	x12, x0
	strh	wzr, [x22, 834]
	ldrh	w2, [x20]
	add	x20, x21, :lo12:.LANCHOR4
	strb	wzr, [x22, 838]
	add	w2, w2, 1
	strh	w2, [x22, 832]
	mov	w2, 1
	strb	w2, [x22, 840]
.L3093:
	mov	x0, x12
	bl	make_superblock
	ldrb	w1, [x12, 7]
	ldrh	w0, [x12]
	cbnz	w1, .L3094
	ldr	x1, [x13, 2712]
	ubfiz	x0, x0, 1, 16
	strh	w14, [x1, x0]
	ldrh	w0, [x12]
	add	w0, w0, 1
	strh	w0, [x12]
	b	.L3093
.L3094:
	add	x19, x19, :lo12:.LANCHOR2
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x12, 4]
	mov	w21, -1
	ldr	w1, [x19, 2396]
	str	w1, [x12, 12]
	add	w1, w1, 1
	str	w1, [x19, 2396]
	ldr	x1, [x19, 2712]
	strh	w2, [x1, x0]
	strh	w21, [x20, 880]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x20, 1072]
	ldr	w0, [x20, 1092]
	strh	w0, [x20, 1078]
	ldr	w0, [x19, 2396]
	str	w0, [x20, 1080]
	add	w0, w0, 1
	strh	wzr, [x20, 1074]
	strh	w21, [x20, 1076]
	str	w0, [x19, 2396]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L3073
	adrp	x0, .LANCHOR1+3448
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR1+3448]
.L3073:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0, 1220]
	cbz	w0, .L3104
.L3106:
	mov	w20, 0
.L3103:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L3104:
	adrp	x0, RK29_NANDC_REG_BASE
	ldr	x0, [x0, #:lo12:RK29_NANDC_REG_BASE]
	bl	FlashInit
	mov	w20, w0
	cbnz	w0, .L3106
	bl	FlashLoadFactorBbt
	cbz	w0, .L3107
	bl	FlashMakeFactorBbt
.L3107:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0, 2216]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L3108
	mov	w2, 16
	mov	w1, 0
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	ldr	w2, [x29, 32]
	mov	w0, 0
	mov	w1, 0
	mov	w4, 1
.L3110:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cinc	w0, w0, ne
	cmp	w1, 16
	bne	.L3110
	cmp	w0, 6
	bhi	.L3111
	adrp	x0, .LANCHOR0+89
	strb	w1, [x0, #:lo12:.LANCHOR0+89]
.L3112:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR0+89
	ldrb	w1, [x1, #:lo12:.LANCHOR0+89]
	strh	w1, [x0, 1218]
.L3108:
	adrp	x1, .LC144
	add	x1, x1, :lo12:.LC144
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC48
	add	x0, x0, :lo12:.LC48
	bl	printk
	add	x0, x19, 1192
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19, 2280]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L3116:
	bl	FtlLoadBbt
	cbz	w0, .L3117
.L3136:
	bl	FtlLowFormat
	cmp	w19, 3
	bls	.L3118
	mov	w20, -1
	b	.L3103
.L3111:
	mov	w1, 0
	mov	w4, 1
.L3114:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cinc	w0, w0, ne
	cmp	w1, 24
	bne	.L3114
	cmp	w0, 17
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	bhi	.L3115
.L3134:
	strb	w1, [x0, 89]
	b	.L3112
.L3115:
	mov	w1, 36
	b	.L3134
.L3118:
	add	w19, w19, 1
	b	.L3116
.L3117:
	bl	FtlSysBlkInit
	cbnz	w0, .L3136
	adrp	x0, .LANCHOR1+3448
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR1+3448]
	b	.L3103
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x21, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	ldr	w1, [x0, 2928]
	cbz	w1, .L3138
.L3141:
	mov	w0, 0
.L3137:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L3138:
	ldrb	w0, [x0, 1220]
	adrp	x12, .LANCHOR4
	cbz	w0, .L3140
	add	x0, x12, :lo12:.LANCHOR4
	ldr	w1, [x0, 1680]
	tbz	x1, 0, .L3140
	ldrh	w0, [x0, 884]
	cbnz	w0, .L3141
.L3140:
	add	x11, x21, :lo12:.LANCHOR2
	add	x19, x12, :lo12:.LANCHOR4
	mov	w3, 0
	mov	w2, 0
	mov	x20, x11
	ldr	w1, [x19, 1680]
	ldr	x0, [x11, 2528]
	bl	FlashProgPages
	mov	w13, 0
	mov	w14, 56
.L3142:
	ldr	w1, [x19, 1680]
	cmp	w13, w1
	bcc	.L3144
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x0, [x21, 2528]
	bl	FtlGcBufFree
	str	wzr, [x19, 1680]
	ldrh	w0, [x19, 884]
	cbnz	w0, .L3141
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	b	.L3152
.L3144:
	umull	x1, w13, w14
	ldr	x2, [x20, 2528]
	add	x3, x2, x1
	ldr	w2, [x2, x1]
	ldr	x0, [x3, 16]
	cmn	w2, #1
	bne	.L3143
	ldrh	w3, [x19, 880]
	ldr	x0, [x20, 2712]
	strh	wzr, [x0, x3, lsl 1]
	strh	w2, [x19, 880]
	ldr	w0, [x19, 1248]
	add	w0, w0, 1
	str	w0, [x19, 1248]
	ldr	x0, [x20, 2528]
	add	x0, x0, x1
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L3152:
	mov	w0, 1
	b	.L3137
.L3143:
	ldp	w2, w0, [x0, 8]
	ldr	w1, [x3, 4]
	bl	FtlGcUpdatePage
	add	w13, w13, 1
	and	w13, w13, 65535
	b	.L3142
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR4
	add	x1, x0, 880
	ldrh	w2, [x0, 880]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L3154
	ldrh	w0, [x1, 4]
	cbnz	w0, .L3155
.L3154:
	bl	FtlCacheWriteBack
	add	x20, x19, :lo12:.LANCHOR4
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x20, 880
	strb	wzr, [x0, 8]
	bl	allocate_data_superblock
	strh	wzr, [x20, 1712]
	strh	wzr, [x20, 1714]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3155:
	add	x0, x19, :lo12:.LANCHOR4
	add	x0, x0, 880
	bl	get_new_active_ppa
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	ftl_read
	.type	ftl_read, %function
ftl_read:
	sub	sp, sp, #208
	adrp	x4, .LANCHOR1+3448
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x21, x22, [sp, 48]
	stp	x19, x20, [sp, 32]
	ldr	w21, [x4, #:lo12:.LANCHOR1+3448]
	stp	x23, x24, [sp, 64]
	stp	x25, x26, [sp, 80]
	cmp	w21, 1
	stp	x27, x28, [sp, 96]
	bne	.L3182
	mov	x23, x3
	mov	w24, w2
	mov	w19, w1
	cmp	w0, 16
	bne	.L3159
	mov	x2, x3
	mov	w1, w24
	add	w0, w19, 256
	bl	FtlVendorPartRead
	mov	w25, w0
.L3157:
	mov	w0, w25
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 208
	ret
.L3159:
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0, 1224]
	cmp	w19, w1
	bcs	.L3182
	cmp	w2, w1
	bhi	.L3182
	add	w2, w19, w2
	str	w2, [x29, 156]
	cmp	w1, w2
	bcc	.L3182
	ldrh	w1, [x0, 2350]
	sub	w26, w2, #1
	adrp	x7, .LANCHOR4
	add	x2, x7, :lo12:.LANCHOR4
	udiv	w27, w19, w1
	udiv	w26, w26, w1
	sub	w21, w21, w27
	add	w1, w21, w26
	str	w1, [x29, 172]
	ldr	w1, [x2, 1668]
	add	w1, w1, w24
	str	w1, [x2, 1668]
	ldr	w2, [x29, 172]
	ldr	w1, [x0, 2424]
	add	w1, w1, w2
	str	w1, [x0, 2424]
	mov	w1, w26
	mov	w0, w27
	bl	FtlCacheMetchLpa
	str	x7, [x29, 112]
	cbz	w0, .L3160
	bl	FtlCacheWriteBack
.L3160:
	mov	w22, w27
	adrp	x0, .LC57
	mov	w28, 0
	add	x0, x0, :lo12:.LC57
	mov	w25, 0
	str	x0, [x29, 104]
	stp	wzr, wzr, [x29, 164]
.L3161:
	ldr	w0, [x29, 172]
	cbnz	w0, .L3178
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x20, 2486]
	cbz	w0, .L3157
	mov	w1, 1
	mov	w0, 0
	bl	ftl_do_gc
	b	.L3157
.L3178:
	mov	w2, 0
	add	x1, x29, 188
	mov	w0, w22
	bl	log2phys
	ldr	w4, [x29, 188]
	cmn	w4, #1
	bne	.L3162
	add	x5, x20, :lo12:.LANCHOR2
	mov	w21, 0
.L3163:
	ldrh	w0, [x5, 2350]
	cmp	w21, w0
	bcc	.L3165
.L3166:
	ldr	w0, [x29, 172]
	add	w22, w22, 1
	subs	w0, w0, #1
	str	w0, [x29, 172]
	beq	.L3170
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0, 2276]
	cmp	w28, w0, lsl 3
	bne	.L3161
.L3170:
	cbz	w28, .L3161
	add	x8, x20, :lo12:.LANCHOR2
	mov	w1, w28
	mov	w2, 0
	mov	x21, x8
	ldr	x0, [x8, 2520]
	bl	FlashReadPages
	ldr	w0, [x29, 164]
	lsl	w0, w0, 9
	str	w0, [x29, 132]
	ldr	w0, [x29, 160]
	lsl	w0, w0, 9
	str	x0, [x29, 136]
	ldr	w0, [x29, 168]
	lsl	w0, w0, 9
	str	w0, [x29, 152]
	mov	w0, 56
	umull	x0, w28, w0
	mov	x28, 0
	str	x0, [x29, 120]
	ldr	x0, [x29, 112]
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29, 144]
.L3177:
	ldr	x0, [x21, 2520]
	add	x0, x0, x28
	ldr	w1, [x0, 24]
	cmp	w27, w1
	bne	.L3172
	ldr	x1, [x0, 8]
	ldr	x0, [x21, 2616]
	cmp	x1, x0
	bne	.L3173
	ldr	x0, [x29, 136]
	ldr	w2, [x29, 152]
	add	x1, x1, x0
	mov	x0, x23
.L3197:
	bl	ftl_memcpy
.L3173:
	ldr	x1, [x21, 2520]
	add	x0, x1, x28
	ldr	w2, [x1, x28]
	cmn	w2, #1
	bne	.L3174
	ldr	x1, [x29, 144]
	mov	w25, w2
	add	x3, x1, 1152
	ldr	w1, [x1, 1224]
	add	w1, w1, 1
	str	w1, [x3, 72]
.L3174:
	ldr	x1, [x0, 16]
	ldr	w2, [x0, 24]
	ldr	w1, [x1, 8]
	cmp	w2, w1
	beq	.L3175
	ldr	x1, [x29, 144]
	add	x2, x1, 1152
	ldr	w1, [x1, 1224]
	add	w1, w1, 1
	str	w1, [x2, 72]
	ldp	x2, x1, [x0, 8]
	ldr	w3, [x2, 4]
	str	w3, [sp]
	ldp	w3, w4, [x1]
	ldp	w5, w6, [x1, 8]
	ldr	w7, [x2]
	ldr	w1, [x0, 24]
	ldr	w2, [x0, 4]
	ldr	x0, [x29, 104]
	bl	printk
.L3175:
	ldr	x0, [x21, 2520]
	add	x1, x0, x28
	ldr	w0, [x0, x28]
	cmp	w0, 256
	bne	.L3176
	ldr	w0, [x1, 4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3176:
	ldr	x0, [x29, 120]
	add	x28, x28, 56
	cmp	x0, x28
	bne	.L3177
	mov	w28, 0
	b	.L3161
.L3165:
	madd	w0, w22, w0, w21
	cmp	w19, w0
	bhi	.L3164
	ldr	w1, [x29, 156]
	cmp	w1, w0
	bls	.L3164
	sub	w0, w0, w19
	str	x5, [x29, 144]
	lsl	w0, w0, 9
	mov	w2, 512
	mov	w1, 0
	add	x0, x23, x0
	bl	ftl_memset
	ldr	x5, [x29, 144]
.L3164:
	add	w21, w21, 1
	b	.L3163
.L3162:
	add	x1, x20, :lo12:.LANCHOR2
	mov	w0, 56
	cmp	w22, w27
	umull	x2, w28, w0
	ldr	x0, [x1, 2520]
	add	x0, x0, x2
	str	w4, [x0, 4]
	ldrh	w0, [x1, 2350]
	bne	.L3167
	ldr	x4, [x1, 2520]
	ldr	x1, [x1, 2616]
	add	x4, x4, x2
	str	x1, [x4, 8]
	udiv	w1, w19, w0
	msub	w1, w1, w0, w19
	str	w1, [x29, 160]
	sub	w1, w0, w1
	cmp	w24, w1
	csel	w1, w24, w1, ls
	str	w1, [x29, 168]
	cmp	w1, w0
	bne	.L3168
	str	x23, [x4, 8]
.L3168:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x0, [x1, 2520]
	add	x2, x0, x2
	ldrh	w0, [x1, 2356]
	ldr	x1, [x1, 2648]
	str	w22, [x2, 24]
	mul	w0, w0, w28
	add	w28, w28, 1
	and	x0, x0, 4294967292
	add	x0, x1, x0
	str	x0, [x2, 16]
	b	.L3166
.L3167:
	cmp	w22, w26
	bne	.L3169
	ldr	x4, [x1, 2520]
	ldr	x1, [x1, 2624]
	add	x4, x4, x2
	ldr	w3, [x29, 156]
	str	x1, [x4, 8]
	mul	w1, w22, w0
	sub	w3, w3, w1
	str	w3, [x29, 164]
	cmp	w0, w3
	bne	.L3168
	sub	w1, w1, w19
	lsl	w1, w1, 9
	add	x1, x23, x1
	str	x1, [x4, 8]
	b	.L3168
.L3169:
	ldr	x1, [x1, 2520]
	mul	w0, w0, w22
	add	x1, x1, x2
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	str	x0, [x1, 8]
	b	.L3168
.L3172:
	cmp	w26, w1
	bne	.L3173
	ldr	x1, [x0, 8]
	ldr	x0, [x21, 2624]
	cmp	x1, x0
	bne	.L3173
	ldrh	w0, [x21, 2350]
	ldr	w2, [x29, 132]
	mul	w0, w0, w26
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	b	.L3197
.L3182:
	mov	w25, -1
	b	.L3157
	.size	ftl_read, .-ftl_read
	.align	2
	.global	ftl_vendor_read
	.type	ftl_vendor_read, %function
ftl_vendor_read:
	stp	x29, x30, [sp, -16]!
	mov	x3, x2
	mov	w2, w1
	mov	w1, w0
	add	x29, sp, 0
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
	mov	x3, x2
	mov	w2, w1
	add	w1, w0, 256
	add	x29, sp, 0
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
	adrp	x1, .LC144
	add	x1, x1, :lo12:.LC144
	mov	w0, -1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR4
	stp	x21, x22, [sp, 32]
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x21, .LANCHOR1
	adrp	x19, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR1
	add	x19, x19, :lo12:.LANCHOR2
	str	x23, [sp, 48]
	str	wzr, [x20, 1916]
	str	w0, [x21, 3448]
	adrp	x0, .LC48
	str	wzr, [x19, 2928]
	add	x0, x0, :lo12:.LC48
	bl	printk
	add	x0, x19, 1192
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x19, 2280]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3203
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC162
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC162
.L3218:
	bl	printk
.L3204:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L3203:
	bl	FtlSysBlkInit
	cbz	w0, .L3205
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC163
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC163
	b	.L3218
.L3205:
	mov	w1, 1
	str	w1, [x21, 3448]
	bl	ftl_do_gc
	ldrh	w0, [x20, 776]
	cmp	w0, 15
	bhi	.L3206
	add	w22, w0, 2
	mov	w21, 0
	mov	w23, 65535
.L3209:
	ldrh	w0, [x20, 1104]
	cmp	w0, w23
	bne	.L3207
	ldrh	w0, [x19, 2472]
	cmp	w0, w23
	bne	.L3207
	and	w0, w21, 63
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3207:
	mov	w1, 1
	mov	w0, w1
	bl	ftl_do_gc
	mov	w1, 1
	mov	w0, 0
	bl	ftl_do_gc
	ldrh	w0, [x20, 776]
	cmp	w0, w22
	bhi	.L3204
	add	w21, w21, 1
	cmp	w21, 4096
	bne	.L3209
	b	.L3204
.L3206:
	ldrb	w0, [x19, 1220]
	cbz	w0, .L3204
	mov	w19, 128
.L3211:
	mov	w1, 1
	mov	w0, w1
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3211
	b	.L3204
	.size	FtlInit, .-FtlInit
	.align	2
	.global	ftl_write
	.type	ftl_write, %function
ftl_write:
	stp	x29, x30, [sp, -256]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	stp	x25, x26, [sp, 64]
	mov	w25, w2
	add	x2, x20, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	stp	x21, x22, [sp, 32]
	mov	w23, w1
	stp	x27, x28, [sp, 80]
	ldr	w1, [x2, 2928]
	cbnz	w1, .L3261
	adrp	x1, .LANCHOR1
	mov	x24, x3
	add	x3, x1, :lo12:.LANCHOR1
	str	x1, [x29, 144]
	ldr	w3, [x3, 3448]
	cmp	w3, 1
	bne	.L3261
	cmp	w0, 16
	bne	.L3221
	mov	x2, x24
	mov	w1, w25
	add	w0, w23, 256
	bl	FtlVendorPartWrite
.L3219:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 256
	ret
.L3221:
	ldr	w1, [x2, 1224]
	cmp	w23, w1
	bcs	.L3264
	cmp	w25, w1
	bhi	.L3264
	add	w0, w23, w25
	cmp	w1, w0
	bcc	.L3264
	adrp	x3, .LANCHOR4
	add	x21, x3, :lo12:.LANCHOR4
	mov	w1, 2048
	sub	w0, w0, #1
	ldr	w4, [x2, 2496]
	str	w1, [x21, 1920]
	ldrh	w1, [x2, 2350]
	str	x3, [x29, 152]
	cmp	w25, w1, lsl 1
	udiv	w0, w0, w1
	udiv	w27, w23, w1
	str	w0, [x29, 172]
	sub	w28, w0, w27
	ldr	w0, [x2, 2408]
	add	w26, w28, 1
	add	w0, w0, w26
	str	w0, [x2, 2408]
	ldr	w0, [x21, 1664]
	add	w0, w0, w25
	str	w0, [x21, 1664]
	cset	w0, cs
	str	w0, [x29, 188]
	cbz	w4, .L3223
	ldr	x0, [x2, 2560]
	sub	w4, w4, #1
	mov	w3, 56
	umaddl	x4, w4, w3, x0
	ldr	w0, [x4, 24]
	cmp	w27, w0
	bne	.L3224
	ldr	w0, [x2, 2416]
	ldr	x3, [x4, 8]
	add	w0, w0, 1
	str	w0, [x2, 2416]
	ldr	w0, [x21, 1924]
	add	w0, w0, 1
	str	w0, [x21, 1924]
	msub	w0, w27, w1, w23
	sub	w1, w1, w0
	cmp	w25, w1
	lsl	w0, w0, 9
	csel	w19, w25, w1, ls
	add	x0, x3, x0
	lsl	w22, w19, 9
	mov	x1, x24
	mov	w2, w22
	bl	ftl_memcpy
	cbnz	w28, .L3225
	ldr	w0, [x21, 1924]
	cmp	w0, 2
	bgt	.L3225
.L3261:
	mov	w0, 0
	b	.L3219
.L3225:
	sub	w25, w25, w19
	add	w23, w23, w19
	add	x24, x24, x22
	add	w27, w27, 1
	mov	w26, w28
.L3224:
	ldr	x0, [x29, 152]
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x0, 1924]
.L3223:
	ldr	w1, [x29, 172]
	mov	w0, w27
	bl	FtlCacheMetchLpa
	cbz	w0, .L3226
	bl	FtlCacheWriteBack
.L3226:
	ldr	x0, [x29, 152]
	mov	w21, w27
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29, 176]
	add	x1, x0, 784
	str	x1, [x29, 160]
	mov	x22, x1
	str	x1, [x29, 120]
	str	x1, [x0, 1904]
.L3227:
	cbnz	w26, .L3255
	ldr	w0, [x29, 172]
	sub	w1, w0, w27
	mov	w0, 0
	bl	ftl_do_gc
	ldr	x0, [x29, 152]
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w0, [x0, 776]
	cmp	w0, 5
	bls	.L3256
	cmp	w0, 31
	bhi	.L3261
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0, #:lo12:.LANCHOR0+88]
	cbnz	w0, .L3261
.L3256:
	ldr	x0, [x29, 152]
	add	x20, x20, :lo12:.LANCHOR2
	mov	w19, 65535
	mov	w22, 128
	add	x21, x0, :lo12:.LANCHOR4
.L3259:
	ldrh	w0, [x21, 1104]
	cmp	w0, w19
	bne	.L3258
	ldrh	w0, [x20, 2472]
	cmp	w0, w19
	bne	.L3258
	ldrh	w0, [x20, 2474]
	cmp	w0, w19
	bne	.L3258
	and	w0, w26, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3258:
	mov	w1, 1
	strh	w22, [x20, 2482]
	mov	w0, w1
	strh	w22, [x20, 2480]
	bl	ftl_do_gc
	mov	w1, 1
	mov	w0, 0
	bl	ftl_do_gc
	ldr	w0, [x20, 2928]
	cbnz	w0, .L3261
	ldrh	w0, [x21, 776]
	cmp	w0, 2
	bhi	.L3261
	add	w26, w26, 1
	cmp	w26, 256
	bne	.L3259
	b	.L3261
.L3255:
	ldrh	w0, [x22, 4]
	cbnz	w0, .L3228
	ldr	x0, [x29, 120]
	cmp	x22, x0
	bne	.L3229
	ldr	x0, [x29, 176]
	add	x0, x0, 832
	ldrh	w1, [x0, 4]
	cbnz	w1, .L3230
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 144]
	add	x0, x0, :lo12:.LANCHOR1
	str	wzr, [x0, 3456]
.L3230:
	ldr	x0, [x29, 160]
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 144]
	ldr	x1, [x29, 176]
	add	x0, x0, :lo12:.LANCHOR1
	add	x22, x1, 832
	ldr	w0, [x0, 3456]
	cmp	w0, 0
	ldr	x0, [x29, 160]
	csel	x22, x22, x0, ne
.L3231:
	ldrh	w0, [x22, 4]
	cbnz	w0, .L3232
	mov	x0, x22
	bl	allocate_new_data_superblock
.L3232:
	ldr	x0, [x29, 176]
	str	x22, [x0, 1904]
.L3228:
	add	x1, x20, :lo12:.LANCHOR2
	str	wzr, [x29, 184]
	ldr	w0, [x1, 2492]
	ldr	w1, [x1, 2496]
	sub	w0, w0, w1
	ldrh	w1, [x22, 4]
	cmp	w0, w26
	csel	w0, w0, w26, ls
	cmp	w1, w0
	csel	w0, w1, w0, ls
	str	w0, [x29, 116]
	ldr	w0, [x29, 188]
	and	w0, w0, 1
	str	w0, [x29, 112]
	adrp	x0, .LC164
	add	x0, x0, :lo12:.LC164
	str	x0, [x29, 104]
.L3233:
	ldr	w1, [x29, 116]
	ldr	w0, [x29, 184]
	cmp	w0, w1
	bne	.L3251
.L3234:
	ldr	w0, [x29, 184]
	sub	w26, w26, w0
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0, 2496]
	ldr	w0, [x0, 2492]
	cmp	w1, w0
	bcs	.L3252
	ldr	w0, [x29, 188]
	cbnz	w0, .L3252
	ldrh	w0, [x22, 4]
	cbz	w0, .L3252
.L3254:
	str	wzr, [x29, 188]
	b	.L3227
.L3229:
	ldr	x0, [x29, 144]
	add	x0, x0, :lo12:.LANCHOR1
	str	wzr, [x0, 3456]
	ldr	x0, [x29, 120]
	ldrh	w0, [x0, 4]
	cbnz	w0, .L3268
	mov	x0, x22
	bl	allocate_new_data_superblock
	b	.L3231
.L3268:
	ldr	x22, [x29, 160]
	b	.L3232
.L3251:
	ldrh	w0, [x22, 4]
	cbz	w0, .L3234
	ldr	w0, [x29, 172]
	cmp	w0, w21
	ldr	w0, [x29, 112]
	cset	w3, eq
	tst	w3, w0
	beq	.L3235
	ldr	w0, [x29, 184]
	cbz	w0, .L3235
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0, 2350]
	add	w0, w23, w25
	msub	w0, w21, w1, w0
	cmp	w1, w0
	bne	.L3234
.L3235:
	add	x1, x29, 196
	add	x19, x20, :lo12:.LANCHOR2
	mov	w2, 0
	str	w3, [x29, 100]
	mov	w0, w21
	bl	log2phys
	mov	x0, x22
	bl	get_new_active_ppa
	ldr	w5, [x19, 2496]
	mov	w4, 56
	ldr	x1, [x19, 2560]
	str	w4, [x29, 168]
	umull	x2, w5, w4
	add	x1, x1, x2
	str	w0, [x1, 4]
	ldr	x0, [x19, 2560]
	add	x0, x0, x2
	ldrh	w2, [x19, 2356]
	str	w21, [x0, 24]
	mul	w1, w5, w2
	and	x1, x1, 4294967292
	str	x1, [x29, 136]
	ldr	x1, [x19, 2664]
	ldr	x3, [x29, 136]
	str	x1, [x29, 128]
	add	x28, x1, x3
	ldrh	w1, [x19, 2354]
	str	x28, [x0, 16]
	mul	w1, w1, w5
	ldr	x5, [x19, 2608]
	and	x1, x1, 4294967292
	add	x1, x5, x1
	str	x1, [x0, 8]
	mov	w1, 0
	mov	x0, x28
	bl	ftl_memset
	cmp	w27, w21
	ldr	w3, [x29, 100]
	cset	w0, eq
	ldr	w4, [x29, 168]
	orr	w3, w3, w0
	cbz	w3, .L3236
	bne	.L3237
	ldrh	w19, [x19, 2350]
	udiv	w0, w23, w19
	msub	w0, w0, w19, w23
	str	w0, [x29, 168]
	sub	w19, w19, w0
	cmp	w19, w25
	csel	w19, w19, w25, ls
.L3238:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0, 2350]
	cmp	w1, w19
	bne	.L3239
	cmp	w27, w21
	beq	.L3269
	mul	w1, w21, w19
	sub	w1, w1, w23
	lsl	w1, w1, 9
	add	x1, x24, x1
.L3240:
	ldr	w0, [x29, 188]
	add	x2, x20, :lo12:.LANCHOR2
	cbz	w0, .L3241
	ldr	w0, [x2, 2496]
	mov	w3, 56
	ldr	x2, [x2, 2560]
	umaddl	x0, w0, w3, x2
.L3287:
	str	x1, [x0, 8]
.L3242:
	ldp	x1, x2, [x29, 128]
	mov	w0, -3947
	strh	w0, [x1, x2]
	add	x1, x20, :lo12:.LANCHOR2
	ldr	w0, [x1, 2400]
	stp	w0, w21, [x28, 4]
	add	w21, w21, 1
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1, 2400]
	ldr	w0, [x29, 196]
	str	w0, [x28, 12]
	ldrh	w0, [x22]
	strh	w0, [x28, 2]
	ldr	w0, [x1, 2496]
	add	w0, w0, 1
	str	w0, [x1, 2496]
	ldr	w0, [x29, 184]
	add	w0, w0, 1
	str	w0, [x29, 184]
	b	.L3233
.L3237:
	ldrh	w0, [x19, 2350]
	add	w2, w23, w25
	str	wzr, [x29, 168]
	msub	w19, w21, w0, w2
	and	w19, w19, 65535
	b	.L3238
.L3269:
	mov	x1, x24
	b	.L3240
.L3241:
	ldr	w0, [x2, 2496]
	mov	w4, 56
	ldr	x3, [x2, 2560]
	ldrh	w2, [x2, 2354]
	nop // between mem op and mult-accumulate
	umaddl	x0, w0, w4, x3
.L3288:
	ldr	x0, [x0, 8]
	b	.L3290
.L3239:
	ldr	w1, [x29, 196]
	mov	w2, 56
	cmn	w1, #1
	beq	.L3243
	str	w1, [x29, 204]
	ldr	w1, [x0, 2496]
	ldr	x0, [x0, 2560]
	str	w21, [x29, 224]
	nop // between mem op and mult-accumulate
	umaddl	x0, w1, w2, x0
	mov	w2, 0
	ldp	x1, x0, [x0, 8]
	stp	x1, x0, [x29, 208]
	mov	w1, 1
	add	x0, x29, 200
	bl	FlashReadPages
	ldr	w0, [x29, 200]
	cmn	w0, #1
	bne	.L3244
	ldr	x0, [x29, 176]
	add	x1, x0, 1152
	ldr	w0, [x0, 1224]
	add	w0, w0, 1
	str	w0, [x1, 72]
.L3246:
	cmp	w27, w21
	lsl	w2, w19, 9
	bne	.L3247
	add	x0, x20, :lo12:.LANCHOR2
	mov	w3, 56
	ldr	w1, [x0, 2496]
	ldr	x0, [x0, 2560]
	umaddl	x1, w1, w3, x0
	ldr	w0, [x29, 168]
	lsl	w0, w0, 9
	ldr	x3, [x1, 8]
	mov	x1, x24
	add	x0, x3, x0
.L3290:
	bl	ftl_memcpy
	b	.L3242
.L3244:
	ldr	w0, [x28, 8]
	cmp	w21, w0
	beq	.L3246
	ldr	x0, [x29, 176]
	mov	w2, w21
	add	x1, x0, 1152
	ldr	w0, [x0, 1224]
	add	w0, w0, 1
	str	w0, [x1, 72]
	ldr	x0, [x29, 104]
	ldr	w1, [x28, 8]
	bl	printk
	b	.L3246
.L3243:
	ldr	x1, [x0, 2560]
	ldr	w3, [x0, 2496]
	umaddl	x3, w3, w2, x1
	ldrh	w2, [x0, 2354]
	mov	w1, 0
	ldr	x0, [x3, 8]
	bl	ftl_memset
	b	.L3246
.L3247:
	add	x3, x20, :lo12:.LANCHOR2
	mov	w4, 56
	ldrh	w1, [x3, 2350]
	ldr	w0, [x3, 2496]
	ldr	x3, [x3, 2560]
	mul	w1, w1, w21
	umaddl	x0, w0, w4, x3
	sub	w1, w1, w23
	lsl	w1, w1, 9
.L3289:
	add	x1, x24, x1
	b	.L3288
.L3236:
	ldr	w0, [x29, 188]
	ldrh	w1, [x19, 2350]
	cbz	w0, .L3248
	mul	w1, w1, w21
	ldr	w0, [x19, 2496]
	ldr	x2, [x19, 2560]
	sub	w1, w1, w23
	lsl	w1, w1, 9
	add	x1, x24, x1
	umaddl	x0, w0, w4, x2
	b	.L3287
.L3248:
	ldr	x2, [x19, 2560]
	mul	w1, w1, w21
	ldr	w0, [x19, 2496]
	sub	w1, w1, w23
	lsl	w1, w1, 9
	umaddl	x0, w0, w4, x2
	ldrh	w2, [x19, 2354]
	b	.L3289
.L3252:
	bl	FtlCacheWriteBack
	add	x0, x20, :lo12:.LANCHOR2
	cmp	w26, 1
	str	wzr, [x0, 2496]
	bhi	.L3227
	b	.L3254
.L3264:
	mov	w0, -1
	b	.L3219
	.size	ftl_write, .-ftl_write
	.align	2
	.global	ftl_vendor_write
	.type	ftl_vendor_write, %function
ftl_vendor_write:
	stp	x29, x30, [sp, -16]!
	mov	x3, x2
	mov	w2, w1
	mov	w1, w0
	add	x29, sp, 0
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
	mov	x3, x2
	mov	w2, w1
	add	w1, w0, 256
	add	x29, sp, 0
	mov	w0, 16
	bl	ftl_write
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_sys_write, .-ftl_sys_write
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x19, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	ldrb	w0, [x19, 1220]
	cbz	w0, .L3295
	mov	x24, x20
	adrp	x21, .LANCHOR4
	add	x20, x21, :lo12:.LANCHOR4
	ldr	x0, [x19, 2712]
	adrp	x25, .LC165
	add	x27, x20, 784
	add	x26, x20, 832
	ldrh	w22, [x20, 1754]
	mov	w1, w22
	ubfiz	x23, x22, 1, 16
	ldrh	w2, [x0, x23]
	add	x0, x25, :lo12:.LC165
	bl	printk
	ldrh	w0, [x20, 784]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x20, 832]
	mov	w20, 4097
	bl	FtlGcRefreshOpenBlock
	mov	x0, x27
	bl	allocate_new_data_superblock
	mov	x0, x26
	bl	allocate_new_data_superblock
.L3297:
	subs	w20, w20, #1
	beq	.L3301
	mov	w1, 1
	mov	w0, w1
	bl	ftl_do_gc
	ldr	x0, [x19, 2712]
	ldrh	w0, [x0, x23]
	cbnz	w0, .L3297
.L3301:
	add	x20, x24, :lo12:.LANCHOR2
	mov	w1, w22
	ldr	x0, [x20, 2712]
	ldrh	w2, [x0, x23]
	add	x0, x25, :lo12:.LC165
	bl	printk
	ldr	x0, [x20, 2712]
	ldrh	w19, [x0, x23]
	cbnz	w19, .L3299
	add	x12, x29, 144
	strh	w22, [x12, -48]!
	mov	x0, x12
	bl	make_superblock
	ldrh	w5, [x20, 2276]
	add	x12, x12, 16
	mov	w0, 0
	mov	w6, 65535
	mov	w7, 56
.L3302:
	cmp	w0, w5
	bne	.L3304
	add	x24, x24, :lo12:.LANCHOR2
	mov	w1, w22
	ldr	x0, [x24, 2712]
	ldrh	w2, [x0, x23]
	adrp	x0, .LC166
	add	x0, x0, :lo12:.LC166
	bl	printk
	ldr	x0, [x24, 2544]
	mov	w2, w19
	mov	w1, 0
	bl	FlashEraseBlocks
	ldr	x0, [x24, 2544]
	mov	w2, w19
	mov	w1, 1
	bl	FlashEraseBlocks
.L3299:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x21, 1754]
.L3295:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L3304:
	ldrh	w2, [x12]
	cmp	w2, w6
	beq	.L3303
	umull	x4, w19, w7
	ldr	x3, [x20, 2544]
	lsl	w2, w2, 10
	add	w19, w19, 1
	add	x3, x3, x4
	and	w19, w19, 65535
	str	w2, [x3, 4]
	ldr	x1, [x20, 2544]
	add	x1, x1, x4
	stp	xzr, xzr, [x1, 8]
.L3303:
	add	w0, w0, 1
	add	x12, x12, 2
	and	w0, w0, 65535
	b	.L3302
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
	.set	.LANCHOR3,. + 0
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
	.type	__func__.27051, %object
	.size	__func__.27051, 11
__func__.27051:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.27798, %object
	.size	__func__.27798, 12
__func__.27798:
	.string	"FtlCheckVpc"
	.zero	4
	.type	__func__.27830, %object
	.size	__func__.27830, 17
__func__.27830:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.27849, %object
	.size	__func__.27849, 16
__func__.27849:
	.string	"FtlScanAllBlock"
	.type	__func__.28117, %object
	.size	__func__.28117, 17
__func__.28117:
	.string	"ftl_scan_all_ppa"
	.zero	7
	.type	__func__.28097, %object
	.size	__func__.28097, 21
__func__.28097:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.27124, %object
	.size	__func__.27124, 8
__func__.27124:
	.string	"FtlInit"
	.data
	.align	3
	.set	.LANCHOR1,. + 0
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
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR2,. + 4352
	.set	.LANCHOR4,. + 8704
	.type	gNandChipMap, %object
	.size	gNandChipMap, 64
gNandChipMap:
	.zero	64
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
	.zero	2
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.zero	4
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.zero	2
	.zero	6
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
	.type	check_valid_page_count_table, %object
	.size	check_valid_page_count_table, 8192
check_valid_page_count_table:
	.zero	8192
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
	.section	.rodata.str1.1,"aMS",@progbits,1
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
	.string	"no ect"
.LC48:
	.string	"%s\n"
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
	.string	"FTL version: 5.0.63 20210616"
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
