/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-16
 */
	.file	"rk_ftl_arm_v8.S"
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
	.type	ftl_set_blk_mode.part.9, %function
ftl_set_blk_mode.part.9:
	and	w0, w0, 65535
	adrp	x2, .LANCHOR0+64
	ubfx	x1, x0, 5, 11
	ldr	x3, [x2, #:lo12:.LANCHOR0+64]
	lsl	x1, x1, 2
	mov	w2, 1
	lsl	w2, w2, w0
	ldr	w0, [x3, x1]
	orr	w0, w0, w2
	str	w0, [x3, x1]
	ret
	.size	ftl_set_blk_mode.part.9, .-ftl_set_blk_mode.part.9
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	adrp	x3, .LANCHOR0+72
	ldrb	w3, [x3, #:lo12:.LANCHOR0+72]
	cbz	w3, .L9
	ldrb	w4, [x0, 1]
	ldrb	w3, [x1, 1]
	cmp	w4, w3
	beq	.L10
.L9:
	mov	x3, 0
.L7:
	mov	w4, w3
	cmp	w3, w2
	bcc	.L8
.L10:
	mov	w0, 0
	ret
.L8:
	ldrb	w5, [x0, x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6, -1]
	cmp	w6, w5
	beq	.L7
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
	ldrb	w3, [x2, 73]
	ldr	w2, [x2, 76]
	mul	w2, w3, w2
	cmp	w2, w1
	bls	.L14
	cmp	w0, 0
	cset	w0, ne
	ret
.L14:
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
	adrp	x2, .LANCHOR0+80
	ldrb	w2, [x2, #:lo12:.LANCHOR0+80]
	cbz	w2, .L23
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
.L23:
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
	ldrb	w2, [x2, 80]
	cbz	w2, .L34
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
.L34:
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
	bls	.L38
	mov	w0, 1024
.L42:
	str	w0, [x1, 76]
	ret
.L38:
	cmp	w0, 256
	bls	.L40
	mov	w0, 512
	b	.L42
.L40:
	cmp	w0, 128
	bls	.L42
	mov	w0, 256
	b	.L42
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
	ldr	x3, [x3, 88]
	ldrb	w4, [x4, 8]
	ldrb	w3, [x3, 7]
	cmp	w3, 1
	bne	.L44
	sxtw	x3, w4
	mov	w5, 38
	add	x3, x3, 8
	add	x3, x2, x3, lsl 8
	str	w5, [x3, 8]
.L44:
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
	ldrb	w2, [x3, 112]
	lsr	w3, w1, 16
	cmp	w2, 1
	ldrb	w2, [x4, 8]
	lsr	w4, w1, 8
	add	x2, x6, x2, lsl 8
	bne	.L47
	mov	w6, 6
	str	w6, [x2, 2056]
	str	wzr, [x2, 2052]
	str	wzr, [x2, 2052]
	str	w5, [x2, 2052]
	str	w4, [x2, 2052]
	str	w3, [x2, 2052]
.L50:
	mov	w3, 224
	str	w3, [x2, 2056]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
.L47:
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
	b	.L50
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
	cbz	w2, .L54
	add	x2, x4, x0, lsl 8
	mov	w5, 96
	str	w5, [x2, 2056]
	and	w5, w1, 255
	str	w5, [x2, 2052]
	lsr	w5, w1, 8
	str	w5, [x2, 2052]
	lsr	w5, w1, 16
	str	w5, [x2, 2052]
	ldr	w2, [x3, 76]
	add	w1, w1, w2
.L54:
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
	ldrb	w6, [x4, 107]
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
	ldrb	w1, [x1, 106]
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
.L66:
	cmp	w1, w3
	bhi	.L67
	ret
.L67:
	lsr	w2, w0, 2
	ldrb	w5, [x4, x3]
	add	w2, w2, w0, lsl 5
	add	x3, x3, 1
	add	w2, w2, w5
	eor	w0, w0, w2
	b	.L66
	.size	js_hash, .-js_hash
	.align	2
	.global	FlashLoadIdbInfo
	.type	FlashLoadIdbInfo, %function
FlashLoadIdbInfo:
	mov	w0, 0
	ret
	.size	FlashLoadIdbInfo, .-FlashLoadIdbInfo
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
	adrp	x22, .LANCHOR0
	umull	x2, w2, w0
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	stp	x19, x20, [sp, 16]
	add	x25, x0, 256
	add	x24, x0, 352
	add	x25, x25, x2
	add	x24, x24, x2
	add	x22, x22, :lo12:.LANCHOR0
	mov	x19, x0
	add	x23, x0, w1, sxtw
	mov	x20, 0
	mov	w26, 85
.L71:
	ldrb	w0, [x22, 129]
	cmp	w0, w20
	bhi	.L75
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L75:
	add	x0, x19, 352
	str	w26, [x21, 8]
	ldrsb	w0, [x20, x0]
	str	w0, [x21, 4]
	mov	x0, 1000
	bl	__const_udelay
	ldrb	w0, [x22, 128]
	cmp	w0, 34
	bne	.L72
	ldrsb	w0, [x24, x20]
.L77:
	add	x20, x20, 1
	str	w0, [x21]
	b	.L71
.L72:
	cmp	w0, 35
	bne	.L74
	ldrsb	w0, [x25, x20]
	b	.L77
.L74:
	ldrsb	w0, [x23, 400]
	b	.L77
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
	adrp	x23, .LANCHOR0
	mov	x19, x0
	add	x23, x23, :lo12:.LANCHOR0
	mov	x20, 0
	mov	w24, 161
.L79:
	ldrb	w0, [x23, 129]
	cmp	w0, w20
	bhi	.L80
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L80:
	str	w24, [x22, 8]
	str	wzr, [x22]
	ldrsb	w0, [x20, x19]
	str	w0, [x22]
	ldrsb	w0, [x21, x20]
	add	x20, x20, 1
	str	w0, [x22]
	mov	x0, 1500
	bl	__const_udelay
	b	.L79
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	2
	.global	ftl_flash_suspend
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 136]
	ldr	w2, [x1]
	str	w2, [x0, 144]
	ldr	w2, [x1, 4]
	str	w2, [x0, 148]
	ldr	w2, [x1, 8]
	str	w2, [x0, 152]
	ldr	w2, [x1, 12]
	str	w2, [x0, 156]
	ldr	w2, [x1, 304]
	str	w2, [x0, 160]
	ldr	w2, [x1, 308]
	str	w2, [x0, 164]
	ldr	w2, [x1, 336]
	ldr	w1, [x1, 344]
	stp	w2, w1, [x0, 168]
	ret
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	adrp	x6, .LANCHOR0
	add	x7, x6, :lo12:.LANCHOR0
	and	w4, w4, 255
	ldrh	w9, [x7, 188]
	ldrh	w5, [x7, 190]
	ldrh	w8, [x7, 76]
	ldrb	w7, [x7, 72]
	cmp	w7, 1
	ldr	w7, [x0, 4]
	mul	w5, w5, w9
	ubfx	x11, x7, 10, 16
	and	w9, w5, 65535
	ubfiz	w5, w8, 1, 15
	and	w7, w7, 1023
	csel	w8, w5, w8, eq
	cmp	w1, 1
	udiv	w5, w11, w9
	and	w10, w5, 65535
	msub	w5, w5, w9, w11
	and	w5, w5, 65535
	bne	.L85
	add	x1, x6, :lo12:.LANCHOR0
	ldrb	w9, [x1, 204]
	cbnz	w9, .L85
	add	x1, x1, 208
	ldrh	w7, [x1, w7, sxtw 1]
.L85:
	add	x6, x6, :lo12:.LANCHOR0
	uxtw	x1, w10
	add	x6, x6, 1232
	cmp	w4, 1
	ldr	w1, [x6, x1, lsl 2]
	madd	w5, w5, w8, w1
	add	w5, w5, w7
	str	w5, [x2]
	str	w10, [x3]
	bls	.L87
	ldr	w1, [x0, 4]
	ldr	w0, [x0, 60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w0, eq
	ret
.L87:
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
	ldr	x0, [x3, 88]
	ldrb	w19, [x5, 8]
	ldrb	w0, [x0, 8]
	cmp	w0, 2
	bne	.L89
	and	w2, w2, 255
	add	x3, x3, 96
	cbnz	w2, .L90
	ldrb	w2, [x3, 13]
.L100:
	add	x0, x19, 8
	add	x4, x4, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0, 8]
	ldrb	w4, [x4, 111]
	cbz	w4, .L94
	add	x3, x19, 8
	mov	w2, 0
	add	x3, x20, x3, lsl 8
.L93:
	cmp	w2, w4
	bcc	.L95
.L94:
	add	x19, x19, 8
	mov	x0, 400
	lsl	x19, x19, 8
	bl	__const_udelay
	ldr	w0, [x20, x19]
	ldp	x19, x20, [sp, 16]
	and	w0, w0, 255
	ldp	x29, x30, [sp], 32
	ret
.L90:
	ldrb	w2, [x3, 14]
	b	.L100
.L95:
	lsl	w0, w2, 3
	add	w2, w2, 1
	lsr	w0, w1, w0
	and	w0, w0, 255
	str	w0, [x3, 4]
	b	.L93
.L89:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0, 8]
	b	.L94
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
.L102:
	mov	w1, w20
	mov	w2, w21
	mov	w0, w19
	bl	FlashReadStatusEN
	mov	w1, w0
	cmp	w0, 255
	beq	.L102
	tbnz	x1, 6, .L101
	mov	x1, 3
	mov	x0, 1
	bl	usleep_range
	b	.L102
.L101:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
	.align	2
	.global	FlashScheduleEnSet
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w2, [x1, 1264]
	str	w0, [x1, 1264]
	mov	w0, w2
	ret
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	2
	.global	FlashGetPageSize
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0, #:lo12:.LANCHOR0+88]
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
	adrp	x1, .LANCHOR0+136
	and	w0, w0, 255
	lsl	w2, w0, 8
	ldr	x1, [x1, #:lo12:.LANCHOR0+136]
	orr	w0, w2, w0, lsl 16
	orr	w0, w0, 1
	str	w0, [x1, 304]
	ret
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrDiv
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	adrp	x1, .LANCHOR0+136
	and	w0, w0, 255
	mov	w2, 16640
	orr	w0, w0, w2
	ldr	x1, [x1, #:lo12:.LANCHOR0+136]
	str	w0, [x1, 344]
	ret
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	2
	.global	NandcSetDdrMode
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	adrp	x1, .LANCHOR0+136
	cmp	w0, 0
	ldr	x2, [x1, #:lo12:.LANCHOR0+136]
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
	adrp	x1, .LANCHOR0+136
	and	w0, w0, 255
	tst	w0, 6
	ldr	x2, [x1, #:lo12:.LANCHOR0+136]
	ldr	w1, [x2]
	beq	.L118
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
.L120:
	mov	w0, 0
	str	w1, [x2]
	ret
.L118:
	and	w1, w1, -8193
	b	.L120
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
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 48]
	and	w23, w0, 255
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	str	x27, [sp, 80]
	and	w27, w1, 255
	stp	x25, x26, [sp, 64]
	mov	x21, x2
	and	w22, w3, 255
	ldr	x1, [x0, 88]
	ldrb	w1, [x1, 19]
	cmp	w1, 6
	bne	.L126
	ubfiz	x19, x23, 6, 8
	add	x0, x0, 1272
	add	x19, x19, 20
	add	x19, x19, w22, uxtw 2
.L133:
	add	x19, x0, x19
.L127:
	sxtw	x26, w23
	add	x0, x20, :lo12:.LANCHOR0
	lsl	x1, x26, 4
	and	x27, x27, 255
	add	x2, x0, x1
	mov	x25, 0
	ldr	x24, [x0, x1]
	mov	w0, w23
	ldrb	w5, [x2, 8]
	bl	NandcFlashCs
	ubfiz	x5, x5, 8, 8
	add	x24, x24, x5
	mov	w0, 54
	str	w0, [x24, 2056]
.L130:
	cmp	x25, x27
	bne	.L131
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, 22
	add	x20, x20, x26
	str	w0, [x24, 2056]
	mov	w0, w23
	bl	NandcFlashDeCs
	strb	w22, [x20, 2128]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L126:
	cmp	w1, 7
	bne	.L128
	mov	w19, 160
	mov	x1, 28
	add	x0, x0, 1272
	umaddl	x1, w19, w23, x1
	mov	w19, 10
	umaddl	x19, w22, w19, x1
	b	.L133
.L128:
	cmp	w1, 8
	bne	.L129
	add	x0, x0, 1300
	add	w19, w22, w22, lsl 2
	add	x19, x0, w19, sxtw
	b	.L127
.L129:
	and	x19, x22, 255
	add	x19, x19, 2
	add	x19, x19, w23, uxtw 3
	add	x19, x0, x19, lsl 3
	add	x19, x19, 1276
	b	.L127
.L131:
	ldrb	w0, [x21, x25]
	str	w0, [x24, 2052]
	mov	x0, 1000
	bl	__const_udelay
	ldrsb	w0, [x19, x25]
	add	x25, x25, 1
	str	w0, [x24, 2048]
	b	.L130
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ldr	x0, [x19, 88]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 7
	bhi	.L134
	add	x21, x19, 2132
	add	x22, x19, 1276
	mov	x20, 0
.L137:
	lsl	x1, x20, 3
	and	w0, w20, 255
	ldrb	w1, [x1, x21]
	cmp	w1, 173
	bne	.L136
	ldrb	w1, [x19, 1273]
	mov	w3, 0
	mov	x2, x22
	bl	HynixSetRRPara
.L136:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L137
.L134:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
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
	add	x1, x4, 2164
	stp	x19, x20, [sp, 16]
	add	x19, x1, x0
	ldr	x2, [x19, 8]
	cbz	x2, .L143
	ldrb	w20, [x1, x0]
	mov	w0, w20
	bl	NandcFlashCs
	add	x4, x4, 1232
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
	cbz	x1, .L143
	str	w0, [x1]
	str	xzr, [x19, 16]
.L143:
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
.L153:
	ldr	w0, [x20]
	str	w0, [x29, 40]
	ldr	w0, [x29, 40]
	tbnz	x0, 9, .L154
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L153
	mov	w0, -1
.L151:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L154:
	mov	w0, 0
	b	.L151
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
.L159:
	ldr	w0, [x20]
	str	w0, [x29, 40]
	ldr	w0, [x29, 40]
	tbnz	x0, 9, .L160
	mov	x1, 2
	mov	x0, 1
	bl	usleep_range
	subs	w19, w19, #1
	bne	.L159
	mov	w0, -1
.L157:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L160:
	mov	w0, 0
	b	.L157
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
	adrp	x21, .LANCHOR0
	add	x4, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	str	x23, [sp, 48]
	and	w22, w0, 255
	ldrb	w0, [x4, 204]
	cbz	w0, .L165
	mov	w0, w22
	bl	NandcFlashCs
	sxtw	x0, w22
	lsl	x1, x0, 4
	add	x0, x4, x0, lsl 3
	add	x2, x4, x1
	ldrb	w0, [x0, 2132]
	ldr	x23, [x4, x1]
	ldrb	w19, [x2, 8]
	cmp	w0, 44
	bne	.L167
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
.L167:
	add	x19, x19, 8
	mov	w0, w22
	add	x19, x23, x19, lsl 8
	bl	NandcWaitFlashReadyNoDelay
	mov	w0, 218
	add	x21, x21, :lo12:.LANCHOR0
	str	w0, [x19, 8]
	mov	w0, w22
	bl	NandcWaitFlashReady
	mov	w0, 2
	strb	w0, [x21, 2356]
.L165:
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
	adrp	x21, .LANCHOR0
	add	x4, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	str	x23, [sp, 48]
	and	w22, w0, 255
	ldrb	w0, [x4, 204]
	cbz	w0, .L172
	mov	w0, w22
	bl	NandcFlashCs
	sxtw	x0, w22
	lsl	x1, x0, 4
	add	x0, x4, x0, lsl 3
	add	x2, x4, x1
	ldrb	w0, [x0, 2132]
	ldr	x23, [x4, x1]
	ldrb	w19, [x2, 8]
	cmp	w0, 44
	bne	.L174
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
.L174:
	add	x19, x19, 8
	mov	w0, w22
	add	x19, x23, x19, lsl 8
	bl	NandcWaitFlashReadyNoDelay
	add	x21, x21, :lo12:.LANCHOR0
	mov	w0, 223
	str	w0, [x19, 8]
	mov	w0, w22
	bl	NandcWaitFlashReady
	strb	wzr, [x21, 2356]
.L172:
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
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x6, x1, 8
	add	x3, x1, 2132
	mov	x5, 0
	add	x29, sp, 0
	ldrb	w2, [x1, 2357]
	mov	w12, 69
	mov	w8, 239
	mov	w9, 128
	and	w11, w2, 4
	and	w7, w2, 1
	mov	w10, 1
	mov	w13, 35
	mov	w14, 32
	mov	w15, 5
	mov	w16, 44
.L191:
	ldrb	w2, [x5, x3]
	ldrb	w4, [x6]
	cmp	w2, 152
	ccmp	w2, w12, 4, ne
	beq	.L182
	cmp	w2, 173
	ccmp	w2, w16, 4, ne
	bne	.L183
.L182:
	cmp	w0, 1
	ldr	x1, [x6, -8]
	bne	.L184
	cbz	w7, .L183
	ubfiz	x4, x4, 8, 8
	cmp	w2, 173
	add	x1, x1, x4
	str	w8, [x1, 2056]
	bne	.L185
	str	w0, [x1, 2052]
.L203:
	str	wzr, [x1, 2048]
	b	.L189
.L185:
	cmp	w2, 44
	bne	.L187
	str	w0, [x1, 2052]
	str	w15, [x1, 2048]
.L189:
	str	wzr, [x1, 2048]
	str	wzr, [x1, 2048]
	str	wzr, [x1, 2048]
.L183:
	add	x5, x5, 8
	add	x6, x6, 16
	cmp	x5, 32
	bne	.L191
	mov	w0, 0
	bl	NandcWaitFlashReady
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L187:
	str	w9, [x1, 2052]
	str	w0, [x1, 2048]
	b	.L189
.L184:
	cbz	w11, .L183
	ubfiz	x4, x4, 8, 8
	cmp	w2, 173
	add	x1, x1, x4
	str	w8, [x1, 2056]
	bne	.L188
	str	w10, [x1, 2052]
	str	w14, [x1, 2048]
	b	.L189
.L188:
	cmp	w2, 44
	bne	.L190
	str	w10, [x1, 2052]
	str	w13, [x1, 2048]
	b	.L189
.L190:
	str	w9, [x1, 2052]
	b	.L203
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
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	umull	x1, w1, w0
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x3, x0, 256
	add	x0, x0, 352
	add	x3, x3, x1
	add	x0, x0, x1
	mov	x1, 0
.L209:
	ldrb	w4, [x2, 129]
	cmp	w4, w1
	bhi	.L212
	mov	w0, 0
	bl	NandcWaitFlashReady
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L212:
	ldrb	w4, [x2, 128]
	cmp	w4, 67
	bne	.L210
	ldrsb	w4, [x0, x1]
.L214:
	add	x1, x1, 1
	str	w4, [x20]
	b	.L209
.L210:
	ldrsb	w4, [x3, x1]
	b	.L214
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
	add	x24, x21, 2164
.L218:
	cmp	x20, x23
	bne	.L223
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L223:
	mov	w1, 0
	mov	w4, w22
	add	x3, x29, 76
	add	x2, x29, 72
	mov	x0, x20
	bl	LogAddr2PhyAddr
	ldrb	w1, [x21, 2358]
	ldr	w0, [x29, 76]
	cmp	w1, w0
	bhi	.L219
	mov	w0, -1
	str	w0, [x20]
.L220:
	sub	w22, w22, #1
	add	x20, x20, 56
	and	w22, w22, 255
	b	.L218
.L219:
	uxtw	x0, w0
	add	x1, x21, x0
	ldrb	w19, [x1, 2360]
	mov	x1, 24
	mul	x0, x0, x1
	strb	w19, [x24, x0]
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
	ldr	w0, [x21, 76]
	add	w1, w1, w0
	mov	w0, w19
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29, 72]
	mov	w0, w19
	bl	FlashReadStatus
	tbz	x0, 0, .L221
	mov	w0, -1
	str	w0, [x20]
.L221:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L222
	ldr	w1, [x29, 72]
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
.L222:
	mov	w0, w19
	bl	NandcFlashDeCs
	b	.L220
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x21, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	w24, w2
	stp	x27, x28, [sp, 80]
	mov	w27, w1
	stp	x25, x26, [sp, 64]
	ldrb	w1, [x21, 72]
	cbnz	w1, .L229
	mov	x20, x0
	add	x26, x21, 2164
	mov	w22, 0
	mov	w28, 56
.L230:
	cmp	w22, w24
	bcc	.L239
	add	x19, x19, :lo12:.LANCHOR0
	mov	x21, 0
	add	x22, x19, 2164
	mov	x23, 24
.L240:
	ldrb	w0, [x19, 2358]
	cmp	w0, w21
	bhi	.L242
	ldr	w0, [x19, 2372]
	cbnz	w0, .L243
.L244:
	mov	w0, 0
	b	.L228
.L229:
	mov	w1, w2
	bl	FlashEraseSLc2KBlocks
.L228:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L239:
	umull	x12, w22, w28
	mov	w1, 0
	sub	w4, w24, w22
	add	x3, x29, 108
	add	x23, x20, x12
	add	x2, x29, 104
	mov	x0, x23
	bl	LogAddr2PhyAddr
	mov	w25, w0
	ldrb	w1, [x21, 2358]
	ldr	w0, [x29, 108]
	cmp	w1, w0
	bhi	.L232
	mov	w0, -1
	str	w0, [x20, x12]
.L233:
	add	w22, w22, 1
	b	.L230
.L232:
	ldrb	w1, [x21, 2368]
	mov	x2, 24
	cmp	w1, 0
	uxtw	x1, w0
	csel	w25, w25, wzr, ne
	madd	x1, x1, x2, x26
	ldr	x1, [x1, 8]
	cbz	x1, .L235
	bl	FlashWaitCmdDone
.L235:
	ldp	w2, w1, [x29, 104]
	mov	x0, 24
	madd	x0, x1, x0, x26
	str	w2, [x0, 4]
	stp	x23, xzr, [x0, 8]
	cbz	w25, .L236
	add	w2, w22, 1
	umaddl	x2, w2, w28, x20
	str	x2, [x0, 16]
.L236:
	add	x0, x21, x1
	ldrb	w23, [x0, 2360]
	mov	x0, 24
	mul	x1, x1, x0
	mov	w0, w23
	strb	w23, [x26, x1]
	bl	NandcFlashCs
	cmp	w27, 1
	bne	.L237
	ldrb	w0, [x21, 204]
	cbz	w0, .L237
	mov	w0, w23
	bl	flash_enter_slc_mode
.L238:
	ldr	w1, [x29, 108]
	add	x0, x21, 1232
	add	w22, w22, w25
	ldr	w0, [x0, x1, lsl 2]
	ldr	w1, [x29, 104]
	cmp	w0, 0
	mov	w0, w23
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w1, [x29, 104]
	mov	w2, w25
	mov	w0, w23
	bl	FlashEraseCmd
	mov	w0, w23
	bl	NandcFlashDeCs
	b	.L233
.L237:
	mov	w0, w23
	bl	flash_exit_slc_mode
	b	.L238
.L242:
	mov	w0, w21
	bl	FlashWaitCmdDone
	cmp	w27, 1
	bne	.L241
	ldrb	w0, [x19, 204]
	cbz	w0, .L241
	mul	x0, x21, x23
	ldrb	w0, [x0, x22]
	bl	flash_exit_slc_mode
.L241:
	add	x21, x21, 1
	b	.L240
.L243:
	ldrb	w0, [x19, 2132]
	cmp	w0, 69
	bne	.L244
	mov	w1, 56
	mov	x0, x20
	umaddl	x24, w24, w1, x20
.L245:
	cmp	x24, x0
	beq	.L244
	str	wzr, [x0], 56
	b	.L245
	.size	FlashEraseBlocks, .-FlashEraseBlocks
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
	ldr	x2, [x4, 88]
	add	x3, x4, x1
	ldr	x5, [x4, x1]
	ldrb	w1, [x4, 112]
	and	w7, w21, 255
	lsr	w6, w21, 8
	cmp	w1, 1
	ldrb	w19, [x3, 8]
	lsr	w1, w21, 16
	ldrb	w2, [x2, 7]
	bne	.L261
	cmp	w2, 1
	bne	.L262
	sxtw	x3, w19
	mov	w2, 38
	add	x3, x3, 8
	add	x3, x5, x3, lsl 8
	str	w2, [x3, 8]
.L262:
	add	x0, x0, :lo12:.LANCHOR0
	add	x19, x5, x19, lsl 8
	add	x0, x0, 96
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
.L266:
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
.L261:
	cmp	w2, 1
	bne	.L264
	sxtw	x3, w19
	mov	w2, 38
	add	x3, x3, 8
	add	x3, x5, x3, lsl 8
	str	w2, [x3, 8]
.L264:
	add	x0, x0, :lo12:.LANCHOR0
	add	x19, x5, x19, lsl 8
	add	x0, x0, 96
	ldrb	w2, [x0, 8]
	str	w2, [x19, 2056]
	str	w7, [x19, 2052]
	str	w6, [x19, 2052]
	ldrb	w0, [x0, 9]
	str	w1, [x19, 2052]
	str	w0, [x19, 2056]
	b	.L266
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	2
	.global	ftl_flash_de_init
	.type	ftl_flash_de_init, %function
ftl_flash_de_init:
	stp	x29, x30, [sp, -32]!
	mov	w0, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w0, [x0, 2376]
	cbz	w0, .L268
	mov	w0, 0
	bl	flash_enter_slc_mode
.L269:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20, 2380]
	cbz	w0, .L270
	ldrb	w0, [x20, 2357]
	tbz	x0, 0, .L270
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20, 2380]
.L270:
	ldr	x0, [x19, #:lo12:.LANCHOR0]
	str	wzr, [x0, 336]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L268:
	mov	w0, 0
	bl	flash_exit_slc_mode
	b	.L269
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
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	cmp	w0, 250
	ble	.L280
	ldr	x0, [x1, 136]
	mov	w1, 8354
.L288:
	str	w1, [x0, 4]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L280:
	cmp	w0, 220
	ble	.L282
	ldr	x0, [x1, 136]
.L289:
	mov	w1, 8322
	b	.L288
.L282:
	cmp	w0, 185
	ble	.L283
	ldr	x0, [x1, 136]
	mov	w1, 4226
	b	.L288
.L283:
	cmp	w0, 160
	ldr	x0, [x1, 136]
	ble	.L284
	mov	w1, 4194
	b	.L288
.L284:
	cmp	w19, 35
	bhi	.L285
	mov	w1, 4193
	b	.L288
.L285:
	cmp	w19, 99
	bhi	.L289
	mov	w1, 4225
	b	.L288
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
	bls	.L291
	mov	w1, 8322
	cmp	w0, w1
	bne	.L292
.L291:
	adrp	x1, .LANCHOR0+136
	ldr	x1, [x1, #:lo12:.LANCHOR0+136]
	str	w0, [x1, 4]
.L292:
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
	mov	w2, 1
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	str	x0, [x19, #:lo12:.LANCHOR0]
	str	w2, [x1, 24]
	mov	w2, 2
	str	wzr, [x1, 8]
	str	w2, [x1, 40]
	mov	w2, 3
	str	x0, [x1, 16]
	str	x0, [x1, 32]
	str	x0, [x1, 48]
	str	x0, [x1, 136]
	str	w2, [x1, 56]
	ldr	w2, [x0]
	ubfx	x3, x2, 13, 1
	str	w3, [x1, 2384]
	ldr	w3, [x0, 352]
	and	w2, w2, 245760
	orr	w2, w2, 256
	ubfx	x3, x3, 16, 4
	str	w3, [x1, 2388]
	ldr	w3, [x0, 352]
	str	w3, [x1, 2392]
	cmp	w3, 2049
	bne	.L295
	mov	w3, 8
	str	w3, [x1, 2388]
.L295:
	add	x19, x19, :lo12:.LANCHOR0
	str	w2, [x0]
	ldr	x0, [x19, 136]
	str	wzr, [x0, 336]
	mov	w0, 40
	bl	NandcTimeCfg
	ldr	x0, [x19, 136]
	mov	w1, 8322
	str	w1, [x0, 344]
	mov	w1, 6145
	movk	w1, 0x18, lsl 16
	str	w1, [x0, 304]
	mov	w0, 36864
	bl	ftl_malloc
	str	wzr, [x19, 2448]
	str	x0, [x19, 2400]
	str	x0, [x19, 2408]
	add	x0, x0, 32768
	str	wzr, [x19, 2456]
	str	x0, [x19, 2416]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	NandcInit, .-NandcInit
	.align	2
	.global	NandcGetTimeCfg
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	ldr	x5, [x4, 136]
	ldr	w5, [x5, 4]
	str	w5, [x0]
	ldr	x0, [x4, 136]
	ldr	w0, [x0]
	str	w0, [x1]
	ldr	x0, [x4, 136]
	ldr	w0, [x0, 304]
	str	w0, [x2]
	ldr	x0, [x4, 136]
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
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w0, w0, 255
	mov	w3, 1
	cmp	w0, 16
	ldr	x2, [x1, 136]
	str	w0, [x1, 2460]
	mov	w1, 4096
	str	w3, [x2, 8]
	bne	.L299
.L302:
	and	w1, w1, -17
.L300:
	orr	w1, w1, 1
	str	w1, [x2, 12]
	ret
.L299:
	cmp	w0, 24
	bne	.L301
	orr	w1, w1, 16
	b	.L300
.L301:
	orr	w1, w1, 262144
	cmp	w0, 40
	orr	w1, w1, 16
	bne	.L300
	b	.L302
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0+2464
	and	w0, w0, 255
	add	x29, sp, 0
	strb	w0, [x1, #:lo12:.LANCHOR0+2464]
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
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	add	x19, x0, 2132
	mov	x21, 0
	ldr	x1, [x0, 136]
	ldr	w2, [x0, 144]
	str	w2, [x1]
	ldr	w2, [x0, 148]
	ldr	x1, [x0, 136]
	str	w2, [x1, 4]
	ldr	w2, [x0, 152]
	ldr	x1, [x0, 136]
	str	w2, [x1, 8]
	ldr	w2, [x0, 156]
	str	w2, [x1, 12]
	ldr	w2, [x0, 160]
	str	w2, [x1, 304]
	ldr	w2, [x0, 164]
	str	w2, [x1, 308]
	ldr	w2, [x0, 168]
	str	w2, [x1, 336]
	ldr	w2, [x0, 172]
	str	w2, [x1, 344]
.L310:
	lsl	x0, x21, 3
	ldrb	w0, [x0, x19]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 253
	bhi	.L309
	mov	w0, w21
	bl	FlashReset
.L309:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L310
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19, 2380]
	cbz	w0, .L311
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19, 2357]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19, 2357]
	bl	NandcSetMode
	ldr	w0, [x19, 160]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L311:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20, 88]
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
	.global	rk_nandc_flash_ready
	.type	rk_nandc_flash_ready, %function
rk_nandc_flash_ready:
	ldr	w1, [x0, 368]
	orr	w1, w1, 2
	str	w1, [x0, 368]
	ldr	w1, [x0, 364]
	and	w1, w1, -3
	str	w1, [x0, 364]
	ret
	.size	rk_nandc_flash_ready, .-rk_nandc_flash_ready
	.align	2
	.global	NandcIqrWaitFlashReady
	.type	NandcIqrWaitFlashReady, %function
NandcIqrWaitFlashReady:
	ret
	.size	NandcIqrWaitFlashReady, .-NandcIqrWaitFlashReady
	.align	2
	.global	rk_nandc_flash_xfer_completed
	.type	rk_nandc_flash_xfer_completed, %function
rk_nandc_flash_xfer_completed:
	ldr	w1, [x0, 368]
	orr	w1, w1, 1
	str	w1, [x0, 368]
	ldr	w1, [x0, 364]
	and	w1, w1, -2
	str	w1, [x0, 364]
	ret
	.size	rk_nandc_flash_xfer_completed, .-rk_nandc_flash_xfer_completed
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
.L324:
	ldr	w1, [x0, 8]
	str	w1, [sp, 8]
	ldr	w1, [sp, 8]
	tbz	x1, 20, .L324
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
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x23, x24, [sp, 48]
	and	w24, w1, 255
	add	x1, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	str	x25, [sp, 64]
	add	x6, x1, x0
	ubfiz	w20, w24, 1, 1
	ldr	x21, [x1, x0]
	orr	w20, w20, 8
	ldrb	w0, [x6, 8]
	mov	w6, 16
	ldr	w23, [x21, 12]
	bfi	w23, w6, 8, 8
	and	w23, w23, -9
	bfi	w23, w0, 5, 3
	mov	w0, 1
	bfi	w20, w0, 5, 2
	ldr	w0, [x1, 2388]
	orr	w20, w20, 536870912
	orr	w20, w20, 1024
	cmp	w0, 3
	bfi	w20, w3, 4, 1
	bls	.L329
	ldr	w0, [x21, 16]
	cmp	x5, 0
	str	w0, [x29, 88]
	ccmp	x4, 0, 0, eq
	ldr	w0, [x29, 88]
	and	w0, w0, -5
	str	w0, [x29, 88]
	beq	.L330
	and	w2, w2, 255
	cbnz	w24, .L331
.L339:
	add	w2, w2, 1
	asr	w2, w2, 1
	bfi	w20, w2, 22, 6
	cbz	x4, .L332
	mov	x0, x4
.L333:
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x25, x20, 22, 5
	mov	x22, x4
	mov	w2, w24
	ldr	x1, [x19, 2416]
	str	x1, [x19, 2432]
	lsl	w1, w25, 10
	str	x0, [x19, 2424]
	bl	rknand_dma_map_single
	str	w0, [x19, 2440]
	lsl	w1, w25, 7
	ldr	x0, [x19, 2432]
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x19, 2444]
	mov	w0, 1
	str	w0, [x19, 2448]
	ldr	w0, [x19, 2440]
	mov	w1, 16
	str	w0, [x21, 20]
	tst	x22, 3
	ldr	w0, [x19, 2444]
	str	w0, [x21, 24]
	str	wzr, [x29, 88]
	ldr	w0, [x29, 88]
	bfi	w0, w1, 9, 5
	str	w0, [x29, 88]
	ldr	w0, [x29, 88]
	orr	w0, w0, 448
	str	w0, [x29, 88]
	bne	.L340
	ldr	w0, [x29, 88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29, 88]
.L340:
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
.L330:
	ldr	w0, [x29, 88]
	str	w0, [x21, 16]
.L329:
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
.L331:
	ldr	w0, [x1, 2460]
	mov	w6, 64
	lsr	w9, w2, 1
	mov	x8, x5
	cmp	w0, 25
	mov	w0, 128
	csel	w6, w6, w0, cc
	mov	w7, 0
	mov	w3, 0
	mov	w10, -1
.L335:
	cmp	w3, w9
	bcs	.L339
	lsr	w0, w7, 2
	cbz	x5, .L336
	ldr	x11, [x1, 2416]
	lsl	w0, w0, 2
	ldr	w12, [x8], 4
	str	w12, [x11, x0]
.L337:
	add	w3, w3, 1
	add	w7, w7, w6
	b	.L335
.L336:
	ldr	x11, [x1, 2416]
	lsl	w0, w0, 2
	str	w10, [x11, x0]
	b	.L337
.L332:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 2408]
	b	.L333
	.size	NandcXferStart, .-NandcXferStart
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	mov	w2, 1
	mov	w1, 0
.L347:
	cmp	w2, w0
	bls	.L348
	sub	w0, w1, #1
	ret
.L348:
	add	w1, w1, 1
	lsl	w2, w2, 1
	and	w1, w1, 65535
	b	.L347
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
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w2, [x1, 2472]
	ldrh	w3, [x1, 2482]
	str	w0, [x1, 2468]
	mul	w2, w2, w0
	sub	w0, w3, w0
	strh	w0, [x1, 2480]
	ldr	w0, [x1, 2488]
	str	w2, [x1, 2476]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1, 2484]
	ret
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	mov	x7, x0
	stp	x29, x30, [sp, -16]!
	adrp	x5, .LANCHOR0
	add	x1, x5, :lo12:.LANCHOR0
	add	x29, sp, 0
	ldrh	w9, [x0, 8]
	add	x1, x1, 2504
	ldrh	w2, [x0, 10]
	mov	x3, 0
	ldrh	w0, [x0, 12]
	ldrh	w4, [x7, 14]
	strh	w9, [x1, -12]
	strh	w2, [x1, -10]
	strh	w0, [x1, -8]
	strh	w4, [x1, -22]
.L352:
	strb	w3, [x3, x1]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L352
	ldrh	w3, [x7, 20]
	ldrb	w1, [x7, 15]
	cmp	w3, w1
	bcs	.L353
	and	w11, w0, 255
	add	x8, x5, :lo12:.LANCHOR0
	mul	w13, w0, w2
	ubfiz	w12, w11, 1, 7
	add	x8, x8, 2504
	mov	w3, 0
.L354:
	cmp	w3, w0
	bcs	.L356
	and	w1, w3, 255
	mov	w6, w3
	mov	w10, 0
	b	.L357
.L355:
	add	w14, w13, w6
	strb	w1, [x8, w6, uxtw]
	add	w15, w11, w1
	add	w1, w12, w1
	add	w10, w10, 1
	and	w1, w1, 255
	add	w6, w6, w0
	strb	w15, [x8, x14]
.L357:
	cmp	w10, w2
	bcc	.L355
	add	w3, w3, 1
	b	.L354
.L356:
	add	x1, x5, :lo12:.LANCHOR0
	ubfiz	w2, w2, 1, 15
	lsr	w4, w4, 1
	strh	w2, [x1, 2494]
	strh	w4, [x1, 2482]
.L353:
	add	x1, x5, :lo12:.LANCHOR0
	mov	w2, 5
	cmp	w9, 1
	strh	w2, [x1, 2536]
	strh	wzr, [x1, 2538]
	bne	.L358
	strh	w9, [x1, 2536]
.L358:
	add	x1, x5, :lo12:.LANCHOR0
	mov	w2, 4352
	ldrb	w11, [x1, 72]
	strh	w2, [x1, 2540]
	cbz	w11, .L359
	mov	w2, 384
	strh	w2, [x1, 2540]
.L359:
	add	x4, x5, :lo12:.LANCHOR0
	ldrh	w10, [x7, 16]
	ldrh	w8, [x7, 20]
	ldrh	w9, [x7, 18]
	ldrh	w6, [x4, 2494]
	ldrh	w3, [x4, 2482]
	strh	w10, [x4, 2544]
	strh	w9, [x4, 2546]
	mul	w6, w0, w6
	strh	w8, [x4, 2550]
	mul	w0, w0, w3
	and	w6, w6, 65535
	strh	w0, [x4, 2542]
	strh	w6, [x4, 2472]
	mul	w0, w6, w10
	strh	w0, [x4, 2548]
	mov	w0, w8
	bl	Ftl_log2
	and	w2, w0, 65535
	strh	w0, [x4, 2552]
	ubfiz	w0, w8, 9, 7
	strh	w0, [x4, 2554]
	ubfx	w0, w0, 8, 8
	strh	w0, [x4, 2556]
	cmp	w3, 1024
	ldrh	w0, [x7, 26]
	strh	w0, [x4, 2558]
	mul	w0, w6, w3
	str	w0, [x4, 2488]
	bls	.L360
	and	w0, w3, 255
	strh	w0, [x4, 2538]
.L360:
	add	x1, x5, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2538]
	sub	w0, w3, w0
	mul	w0, w0, w6
	mul	w0, w0, w8
	mul	w8, w9, w8
	mul	w0, w0, w10
	asr	w0, w0, 11
	str	w0, [x1, 2560]
	ldrh	w0, [x1, 2540]
	lsl	w0, w0, 3
	sdiv	w0, w0, w8
	and	w0, w0, 65535
	cmp	w0, 4
	bls	.L361
.L377:
	strh	w0, [x1, 2564]
	cbz	w11, .L363
	add	x0, x5, :lo12:.LANCHOR0
	mov	w1, 640
	strh	w1, [x0, 2540]
.L363:
	add	x1, x5, :lo12:.LANCHOR0
	lsl	w3, w3, 6
	cmp	w6, 1
	ldrh	w0, [x1, 2540]
	asr	w0, w0, w2
	add	w2, w2, 9
	add	w0, w0, 2
	strh	w0, [x1, 2566]
	asr	w3, w3, w2
	strh	w3, [x1, 2568]
	and	w3, w3, 65535
	mul	w0, w6, w3
	add	w3, w3, 8
	str	w0, [x1, 2572]
	ldrh	w0, [x1, 2564]
	udiv	w0, w0, w6
	add	w3, w0, w3
	beq	.L364
.L378:
	add	x4, x5, :lo12:.LANCHOR0
	str	w3, [x1, 2468]
	ldrh	w0, [x4, 2468]
	bl	FtlSysBlkNumInit
	ldr	w0, [x4, 2468]
	str	w0, [x4, 2576]
	ldr	w0, [x4, 2484]
	ldrh	w1, [x4, 2544]
	ldrh	w3, [x4, 2550]
	lsl	w0, w0, 2
	ldrh	w2, [x4, 2564]
	ldrb	w7, [x4, 204]
	str	wzr, [x4, 2584]
	mul	w0, w0, w1
	ldrh	w1, [x4, 2552]
	add	w1, w1, 9
	lsr	w0, w0, w1
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	and	w0, w0, 65535
	strh	w0, [x4, 2580]
	strh	w1, [x4, 2582]
	add	w1, w2, 3
	strh	w1, [x4, 2564]
	ldr	w1, [x4, 2572]
	add	w6, w1, 3
	str	w6, [x4, 2572]
	cbz	w7, .L366
	add	w1, w1, 5
	add	w2, w2, 4
	strh	w2, [x4, 2564]
.L379:
	str	w1, [x4, 2572]
.L367:
	add	x5, x5, :lo12:.LANCHOR0
	ldrh	w1, [x5, 2480]
	strh	wzr, [x5, 2588]
	lsl	w2, w1, 1
	lsr	w1, w1, 3
	add	w2, w2, 48
	add	w1, w1, 4
	add	w0, w2, w0, lsl 2
	add	w0, w0, w1
	cmp	w0, w3, lsl 9
	bcs	.L368
	mov	w0, 1
	strh	w0, [x5, 2588]
.L368:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L361:
	mov	w0, 4
	b	.L377
.L364:
	add	w3, w3, 4
	b	.L378
.L366:
	cmp	w6, 7
	bhi	.L367
	mov	w1, 8
	b	.L379
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w2, [x1, 2590]
	cbz	w2, .L384
	ldrh	w2, [x1, 2564]
	ldr	x3, [x1, 2592]
	mov	x1, 0
.L382:
	cmp	w2, w1, uxth
	bhi	.L383
.L384:
	mov	w0, 0
	ret
.L383:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L382
	mov	w0, 1
	ret
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheMetchLpa
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	ldr	w4, [x2, 2600]
	cbz	w4, .L391
	mov	x5, 24
	mov	w6, 56
	ldr	x2, [x2, 2608]
	nop // between mem op and mult-accumulate
	umaddl	x4, w4, w6, x5
	add	x3, x2, 24
	add	x2, x2, x4
.L388:
	cmp	x3, x2
	bne	.L390
.L391:
	mov	w0, 0
	ret
.L390:
	ldr	w4, [x3]
	cmp	w4, w0
	bcc	.L389
	cmp	w4, w1
	bls	.L392
.L389:
	add	x3, x3, 56
	b	.L388
.L392:
	mov	w0, 1
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR0+2584
	ldr	w0, [x0, #:lo12:.LANCHOR0+2584]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	FtlGetCapacity
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	adrp	x0, .LANCHOR0+2584
	ldr	w0, [x0, #:lo12:.LANCHOR0+2584]
	ret
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	adrp	x0, .LANCHOR0+2584
	ldr	w0, [x0, #:lo12:.LANCHOR0+2584]
	ret
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR0+2616
	ldr	w0, [x0, #:lo12:.LANCHOR0+2616]
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
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 2624
	ldrh	w0, [x19, -82]
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
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
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
	adrp	x2, .LANCHOR0
	add	x3, x2, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w1, [x3, 2542]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, w2, uxth 3
	and	w0, w0, 65535
	ldr	x1, [x2, 2656]
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
	.global	FtlBbtCalcTotleCnt
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w4, 0
	mov	w5, 0
	ldrh	w6, [x0, 2542]
	ldrh	w0, [x0, 2494]
	mul	w6, w6, w0
	cmp	w4, w6
	blt	.L411
	mov	w0, w5
	ret
.L411:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L404:
	mov	w0, w4
	bl	FtlBbmIsBadBlock
	cbz	w0, .L403
	add	w5, w5, 1
	and	w5, w5, 65535
.L403:
	add	w4, w4, 1
	and	w4, w4, 65535
	cmp	w4, w6
	blt	.L404
	mov	w0, w5
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.type	V2P_block, %function
V2P_block:
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	and	w0, w0, 65535
	and	w1, w1, 65535
	ldrh	w2, [x4, 2496]
	ldrh	w4, [x4, 2542]
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
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	and	w3, w0, 65535
	ldrh	w1, [x2, 2496]
	ldrh	w2, [x2, 2542]
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
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w3, w0, 65535
	ldrh	w2, [x1, 2542]
	ldrh	w1, [x1, 2496]
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
	bls	.L416
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	ret
.L416:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	adrp	x0, .LANCHOR0+2726
	ldrh	w0, [x0, #:lo12:.LANCHOR0+2726]
	cmp	w0, 0
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR0+2726
	ldrh	w0, [x0, #:lo12:.LANCHOR0+2726]
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
	bcc	.L420
	adrp	x0, .LANCHOR0
	add	x2, x0, :lo12:.LANCHOR0
	mov	x19, x0
	ldrh	w2, [x2, 2726]
	cmp	w2, 1024
	beq	.L420
	and	w1, w1, 65535
	cbz	w1, .L422
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w0, [x20, 424]
	cbnz	w0, .L422
	mov	w0, w21
	bl	P2V_block_in_plane
	and	w22, w0, 65535
	ldr	x0, [x20, 432]
	lsl	w1, w21, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x20, 432]
	bl	FlashEraseBlocks
	ldr	x2, [x20, 440]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x20, 448]
	add	w0, w0, 1
	str	w0, [x20, 448]
.L422:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2720
	ldrh	w1, [x0, 6]
	add	w1, w1, 1
	strh	w1, [x0, 6]
	ldrh	w1, [x0, 4]
	add	x2, x0, w1, sxtw 1
	add	w1, w1, 1
	and	w1, w1, 1023
	strh	w1, [x0, 4]
	strh	w21, [x2, 8]
.L420:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x1, x1, 2720
	ldrh	w2, [x1, 6]
	cbz	w2, .L430
	adrp	x2, .LANCHOR2+484
	ldrh	w3, [x1, 2]
	mov	w6, 0
	mov	w4, 0
	ldrh	w5, [x2, #:lo12:.LANCHOR2+484]
	ldrh	w2, [x1, 4]
	and	w5, w5, 31
.L432:
	cmp	w5, w4
	bgt	.L433
	cbz	w6, .L430
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0, 2722]
	strh	w2, [x0, 2724]
.L430:
	ret
.L433:
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
	b	.L432
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	add	x20, x21, 2720
	stp	x23, x24, [sp, 48]
	mov	x22, x20
.L440:
	ldrh	w1, [x20, 6]
	cbz	w1, .L441
	ldrh	w0, [x20, 2]
	adrp	x19, .LANCHOR2
	add	x4, x19, :lo12:.LANCHOR2
	sub	w1, w1, #1
	strh	w1, [x20, 6]
	add	x2, x20, w0, sxtw 1
	add	w0, w0, 1
	and	w0, w0, 1023
	strh	w0, [x20, 2]
	ldr	w0, [x4, 424]
	ldrh	w23, [x2, 8]
	cbnz	w0, .L442
	mov	w0, w23
	bl	P2V_block_in_plane
	and	w24, w0, 65535
	ldr	x0, [x4, 432]
	lsl	w1, w23, 10
	str	w1, [x0, 4]
	ldrb	w0, [x21, 204]
	cbz	w0, .L443
	ldr	x0, [x4, 432]
	mov	w2, 1
	mov	w1, 0
	bl	FlashEraseBlocks
.L443:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w2, 1
	mov	w1, w2
	ldr	x0, [x19, 432]
	bl	FlashEraseBlocks
	ldr	x2, [x19, 440]
	ubfiz	x0, x24, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x19, 448]
	add	w0, w0, 1
	str	w0, [x19, 448]
.L442:
	sub	w0, w23, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L445
	ldrh	w2, [x22, 6]
	mov	w1, w23
	adrp	x0, .LC3
	add	x0, x0, :lo12:.LC3
	bl	printk
	b	.L440
.L441:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L444:
	b	.L444
.L445:
	mov	w0, w23
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	test_node_in_list
	.type	test_node_in_list, %function
test_node_in_list:
	ldr	x2, [x0]
	adrp	x0, .LANCHOR2+504
	mov	x4, -6148914691236517206
	and	w1, w1, 65535
	ldr	x3, [x0, #:lo12:.LANCHOR2+504]
	movk	x4, 0xaaab, lsl 0
	mov	w5, 65535
	sub	x0, x2, x3
	asr	x0, x0, 1
	mul	x0, x0, x4
	mov	w4, 6
	and	w0, w0, 65535
.L452:
	cmp	w0, w1
	beq	.L453
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L454
	umaddl	x2, w0, w4, x3
	b	.L452
.L453:
	mov	w0, 1
	ret
.L454:
	mov	w0, 0
	ret
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x9, .LANCHOR0
	add	x9, x9, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w1, [x9, 2480]
	cmp	w1, w0
	bls	.L471
	adrp	x2, .LANCHOR2
	add	x4, x2, :lo12:.LANCHOR2
	mov	w5, 6
	mov	w1, -1
	mov	x3, x2
	ldr	x12, [x4, 504]
	umull	x11, w0, w5
	add	x6, x12, x11
	strh	w1, [x6, 2]
	strh	w1, [x12, x11]
	ldr	x1, [x4, 512]
	cbnz	x1, .L458
	str	x6, [x4, 512]
.L471:
	mov	w0, 0
	ret
.L458:
	stp	x29, x30, [sp, -16]!
	ubfiz	x8, x0, 1, 16
	mov	x15, -6148914691236517206
	mov	w10, -1
	add	x29, sp, 0
	ldr	x14, [x4, 520]
	movk	x15, 0xaaab, lsl 0
	ldrh	w2, [x6, 4]
	mov	w30, 65535
	ldr	x13, [x4, 504]
	cmp	w2, 0
	ldrh	w18, [x9, 2480]
	ldrh	w7, [x14, x8]
	mul	w7, w7, w2
	sub	x2, x1, x13
	asr	x2, x2, 1
	csel	w7, w7, w10, ne
	mul	x2, x2, x15
	ldr	x15, [x4, 440]
	mov	w4, 0
	add	x9, x15, x8
	and	w2, w2, 65535
	mov	w8, w5
.L466:
	add	w4, w4, 1
	and	w4, w4, 65535
	cmp	w4, w18
	bhi	.L457
	cmp	w0, w2
	beq	.L457
	ubfiz	x16, x2, 1, 16
	ldrh	w17, [x1, 4]
	cmp	w17, 0
	ldrh	w5, [x14, x16]
	mul	w5, w5, w17
	csel	w5, w5, w10, ne
	cmp	w7, w5
	bne	.L462
	ldrh	w16, [x15, x16]
	ldrh	w5, [x9]
	cmp	w16, w5
	bcc	.L464
.L463:
	strh	w2, [x12, x11]
	ldrh	w2, [x1, 2]
	strh	w2, [x6, 2]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2, 512]
	cmp	x1, x3
	bne	.L467
	strh	w0, [x1, 2]
	str	x6, [x2, 512]
	b	.L457
.L462:
	bcc	.L463
.L464:
	ldrh	w5, [x1]
	cmp	w5, w30
	bne	.L465
	strh	w2, [x6, 2]
	add	x2, x3, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x6, [x2, 528]
.L457:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L465:
	umaddl	x1, w5, w8, x13
	mov	w2, w5
	b	.L466
.L467:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 504]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L457
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
	ldrh	w1, [x0, 536]
	add	w1, w1, 1
	strh	w1, [x0, 536]
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
	beq	.L476
	adrp	x2, .LANCHOR2
	add	x5, x2, :lo12:.LANCHOR2
	mov	w6, 6
	mov	w1, -1
	mov	x3, x2
	ldr	x9, [x5, 504]
	umull	x8, w0, w6
	add	x4, x9, x8
	strh	w1, [x4, 2]
	strh	w1, [x9, x8]
	ldr	x1, [x5, 544]
	cbnz	x1, .L477
	str	x4, [x5, 544]
.L476:
	mov	w0, 0
	ret
.L477:
	ldr	x11, [x5, 440]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5, 504]
	mov	x5, -6148914691236517206
	movk	x5, 0xaaab, lsl 0
	ldrh	w12, [x11, x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	mul	x2, x2, x5
	and	w2, w2, 65535
.L480:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11, x5]
	cmp	w5, w12
	bcs	.L478
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L479
	strh	w2, [x4, 2]
	strh	w0, [x1]
	b	.L476
.L479:
	umaddl	x1, w5, w6, x10
	mov	w2, w5
	b	.L480
.L478:
	ldrh	w5, [x1, 2]
	strh	w5, [x4, 2]
	strh	w2, [x9, x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2, 544]
	cmp	x1, x3
	bne	.L481
	strh	w0, [x1, 2]
	str	x4, [x2, 544]
	b	.L476
.L481:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 504]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L476
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
	ldrh	w1, [x0, 552]
	add	w1, w1, 1
	strh	w1, [x0, 552]
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
	ldr	x3, [x6, 504]
	add	x5, x3, x1
	cmp	x5, x2
	ldrh	w2, [x3, x1]
	bne	.L485
	cmp	w2, w7
	bne	.L486
	str	xzr, [x0]
.L487:
	mov	w0, -1
	strh	w0, [x3, x1]
	strh	w0, [x5, 2]
	mov	w0, 0
	ret
.L486:
	umaddl	x2, w2, w4, x3
	str	x2, [x0]
	mov	w0, -1
	strh	w0, [x2, 2]
	b	.L487
.L485:
	cmp	w2, w7
	ldrh	w0, [x5, 2]
	bne	.L488
	cmp	w0, w2
	beq	.L487
	umull	x2, w0, w4
	mov	w0, -1
	strh	w0, [x3, x2]
	b	.L487
.L488:
	umaddl	x2, w2, w4, x3
	strh	w0, [x2, 2]
	ldrh	w2, [x5, 2]
	ldr	x0, [x6, 504]
	ldrh	w7, [x3, x1]
	umull	x2, w2, w4
	strh	w7, [x0, x2]
	b	.L487
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
	ldr	x2, [x0]
	cbz	x2, .L495
	stp	x29, x30, [sp, -16]!
	adrp	x3, .LANCHOR2+504
	and	w1, w1, 65535
	mov	w4, 65535
	add	x29, sp, 0
	ldr	x8, [x3, #:lo12:.LANCHOR2+504]
	mov	w5, 6
.L491:
	cbnz	w1, .L492
.L494:
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
.L492:
	ldrh	w3, [x2]
	cmp	w3, w4
	beq	.L494
	sub	w1, w1, #1
	umaddl	x2, w3, w5, x8
	and	w1, w1, 65535
	b	.L491
.L495:
	mov	w0, 65535
	ret
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_get_gc_head_node
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	and	w2, w0, 65535
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0, 512]
	cbz	x1, .L505
	ldr	x3, [x0, 504]
	mov	w4, 6
	mov	w0, 65535
.L502:
	cbz	w2, .L503
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L504
	ret
.L504:
	sub	w2, w2, #1
	umaddl	x1, w1, w4, x3
	and	w2, w2, 65535
	b	.L502
.L505:
	mov	w0, 65535
	ret
.L503:
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
	adrp	x8, .LANCHOR2
	add	x1, x8, :lo12:.LANCHOR2
	and	w9, w0, 65535
	ldrh	w0, [x1, 560]
	cmp	w0, w9
	beq	.L514
	ldrh	w0, [x1, 608]
	cmp	w0, w9
	beq	.L514
	ldrh	w0, [x1, 656]
	cmp	w0, w9
	beq	.L514
	mov	w4, 6
	ldp	x3, x2, [x1, 504]
	umull	x4, w9, w4
	add	x0, x3, x4
	cmp	x0, x2
	beq	.L514
	ldr	x5, [x1, 520]
	ubfiz	x1, x9, 1, 16
	ldrh	w2, [x5, x1]
	ldrh	w1, [x0, 4]
	ldrh	w0, [x0, 2]
	cmp	w1, 0
	mul	w2, w2, w1
	mov	w1, 65535
	csinv	w2, w2, wzr, ne
	cmp	w0, w1
	bne	.L509
	ldrh	w1, [x3, x4]
	cmp	w1, w0
	beq	.L514
.L509:
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
	bcs	.L514
	add	x8, x8, :lo12:.LANCHOR2
	stp	x29, x30, [sp, -16]!
	mov	w1, w9
	add	x0, x8, 512
	add	x29, sp, 0
	bl	List_remove_node
	ldrh	w0, [x8, 536]
	sub	w0, w0, #1
	strh	w0, [x8, 536]
	mov	w0, w9
	bl	INSERT_DATA_LIST
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L514:
	mov	w0, 0
	ret
	.size	List_update_data_list, .-List_update_data_list
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
.L518:
	cmp	w20, w2
	beq	.L522
	mov	x21, x1
	ldrh	w3, [x1], 2
	cbnz	w3, .L519
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	and	w1, w0, 65535
	strh	w0, [x21]
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L520
	adrp	x0, .LANCHOR0+2726
	ldrh	w2, [x0, #:lo12:.LANCHOR0+2726]
	adrp	x0, .LC4
	add	x0, x0, :lo12:.LC4
	bl	printk
.L521:
	b	.L521
.L520:
	ldr	w0, [x19, 48]
	strh	wzr, [x19, 2]
	add	w0, w0, 1
	str	w0, [x19, 48]
	ldrh	w0, [x19, 8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19, 8]
.L522:
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L519:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L518
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	adrp	x0, .LANCHOR0+2582
	mov	x4, 0
	mov	w5, 65535
	ldrh	w2, [x0, #:lo12:.LANCHOR0+2582]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	mov	x3, x0
	ldr	x1, [x1, 704]
.L526:
	and	w0, w4, 65535
	cmp	w0, w2
	bcc	.L528
	add	x5, x1, 4
	mov	w0, w2
	mov	w7, -2147483648
	mov	w4, 0
.L529:
	cmp	w4, w2
	bne	.L531
	cmp	w0, w2
	bcc	.L527
	add	x0, x3, :lo12:.LANCHOR2
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0, 712]
	mov	w0, w2
.L532:
	cmp	w3, w2
	beq	.L527
	ldr	w7, [x1, 4]
	cmp	w4, w7
	bls	.L533
	ldrh	w6, [x1]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	csel	w0, w0, w3, eq
.L533:
	add	w3, w3, 1
	add	x1, x1, 16
	and	w3, w3, 65535
	b	.L532
.L528:
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6, -16]
	cmp	w6, w5
	bne	.L526
.L527:
	ret
.L531:
	ldr	w6, [x5]
	tbnz	w6, #31, .L530
	cmp	w7, w6
	bls	.L530
	mov	w7, w6
	mov	w0, w4
.L530:
	add	w4, w4, 1
	add	x5, x5, 16
	and	w4, w4, 65535
	b	.L529
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3, 714]
	cmp	w2, 4
	bhi	.L536
	cbnz	w0, .L536
	add	w2, w2, 1
	strh	w2, [x3, 714]
	ret
.L536:
	add	x0, x1, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR0+2480
	mov	w6, 65535
	ldrh	w4, [x1, #:lo12:.LANCHOR0+2480]
	mov	x1, 0
	ldr	x5, [x0, 520]
	strh	wzr, [x0, 714]
	str	wzr, [x0, 716]
.L537:
	cmp	w4, w1, uxth
	bhi	.L539
	ret
.L539:
	ldrh	w3, [x5, x1, lsl 1]
	cmp	w3, w6
	beq	.L538
	ldr	w2, [x0, 716]
	add	w2, w2, w3
	str	w2, [x0, 716]
.L538:
	add	x1, x1, 1
	b	.L537
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	and	w0, w0, 65535
	cbz	w1, .L542
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_set_blk_mode.part.9
	ldp	x29, x30, [sp], 16
	ret
.L542:
	adrp	x1, .LANCHOR0+64
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1, #:lo12:.LANCHOR0+64]
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
	adrp	x0, .LANCHOR0+64
	ldr	x0, [x0, #:lo12:.LANCHOR0+64]
	ubfx	x2, x1, 5, 11
	ldr	w0, [x0, x2, lsl 2]
	lsr	w0, w0, w1
	and	w0, w0, 1
	ret
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	and	w6, w1, 65535
	adrp	x4, .LANCHOR0
	add	x1, x4, :lo12:.LANCHOR0
	and	w2, w2, 65535
	strh	wzr, [x0, 4]
	ldrh	w3, [x1, 2472]
	mov	w1, 65535
.L550:
	cmp	w3, w2, uxth
	bhi	.L552
	add	x4, x4, :lo12:.LANCHOR0
	ubfiz	x3, x3, 1, 16
	add	x3, x3, 16
	add	x2, x0, 16
	add	x3, x0, x3
	mov	w5, 65535
	ldrh	w1, [x4, 2544]
	sub	w1, w1, #1
	and	w1, w1, 65535
	sub	w1, w1, w6
.L553:
	cmp	x2, x3
	bne	.L555
	ret
.L552:
	add	x5, x0, w2, sxtw 1
	ldrh	w5, [x5, 16]
	cmp	w5, w1
	beq	.L551
	ldrh	w5, [x0, 4]
	add	w5, w5, 1
	strh	w5, [x0, 4]
.L551:
	add	w2, w2, 1
	b	.L550
.L555:
	ldrh	w4, [x2]
	cmp	w4, w5
	beq	.L554
	ldrh	w4, [x0, 4]
	add	w4, w1, w4
	strh	w4, [x0, 4]
.L554:
	add	x2, x2, 2
	b	.L553
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	mov	x5, x0
	strh	wzr, [x0, 4]
	strb	wzr, [x0, 7]
	adrp	x6, .LANCHOR0
	add	x0, x6, :lo12:.LANCHOR0
	mov	x7, 0
	add	x8, x5, 16
	add	x9, x0, 2504
	mov	w11, -1
	ldrh	w10, [x0, 2472]
	cmp	w10, w7, uxth
	bhi	.L576
	add	x1, x6, :lo12:.LANCHOR0
	ldrb	w0, [x5, 7]
	strb	wzr, [x5, 9]
	ldrh	w2, [x1, 2544]
	mul	w0, w0, w2
	strh	w0, [x5, 4]
	ldr	w0, [x1, 2372]
	cbz	w0, .L572
	adrp	x0, .LANCHOR2+440
	ldrh	w1, [x5]
	ldr	x0, [x0, #:lo12:.LANCHOR2+440]
	ldrh	w0, [x0, x1, lsl 1]
	cmp	w0, 79
	bhi	.L572
	mov	w0, 1
	strb	w0, [x5, 9]
.L572:
	add	x6, x6, :lo12:.LANCHOR0
	ldrb	w0, [x6, 72]
	cbz	w0, .L575
	mov	w0, 1
	strb	w0, [x5, 9]
.L575:
	mov	w0, 0
	ret
.L576:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L559:
	ldrh	w1, [x5]
	ldrb	w0, [x9, x7]
	bl	V2P_block
	mov	w4, w0
	strh	w11, [x8]
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L558
	strh	w4, [x8]
	ldrb	w0, [x5, 7]
	add	w0, w0, 1
	strb	w0, [x5, 7]
.L558:
	add	x7, x7, 1
	add	x8, x8, 2
	cmp	w10, w7, uxth
	bhi	.L559
	add	x1, x6, :lo12:.LANCHOR0
	ldrb	w0, [x5, 7]
	strb	wzr, [x5, 9]
	ldrh	w2, [x1, 2544]
	mul	w0, w0, w2
	strh	w0, [x5, 4]
	ldr	w0, [x1, 2372]
	cbz	w0, .L560
	adrp	x0, .LANCHOR2+440
	ldrh	w1, [x5]
	ldr	x0, [x0, #:lo12:.LANCHOR2+440]
	ldrh	w0, [x0, x1, lsl 1]
	cmp	w0, 79
	bhi	.L560
	mov	w0, 1
	strb	w0, [x5, 9]
.L560:
	add	x6, x6, :lo12:.LANCHOR0
	ldrb	w0, [x6, 72]
	cbz	w0, .L561
	mov	w0, 1
	strb	w0, [x5, 9]
.L561:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
	and	w7, w0, 65535
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	x8, 0
	mov	w6, 0
	add	x5, x0, 2504
	ldrh	w9, [x0, 2472]
	ldrh	w10, [x0, 2544]
	cmp	w9, w8, uxth
	bhi	.L589
	cbz	w6, .L587
	mov	w0, 32768
	sdiv	w6, w0, w6
.L588:
	adrp	x0, .LANCHOR2+504
	mov	w1, 6
	ldr	x0, [x0, #:lo12:.LANCHOR2+504]
	umaddl	x7, w7, w1, x0
	mov	w0, 0
	strh	w6, [x7, 4]
	ret
.L582:
	mov	w6, 0
	b	.L581
.L587:
	mov	w6, 0
	b	.L588
.L589:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L580:
	ldrb	w0, [x5, x8]
	mov	w1, w7
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L579
	add	w6, w6, w10
	and	w6, w6, 65535
.L579:
	add	x8, x8, 1
	cmp	w9, w8, uxth
	bhi	.L580
	cbz	w6, .L582
	mov	w0, 32768
	sdiv	w6, w0, w6
.L581:
	adrp	x0, .LANCHOR2+504
	mov	w1, 6
	ldr	x0, [x0, #:lo12:.LANCHOR2+504]
	umaddl	x7, w7, w1, x0
	mov	w0, 0
	strh	w6, [x7, 4]
	ldp	x29, x30, [sp], 16
	ret
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	ldr	x0, [x1, 544]
	cbz	x0, .L592
	ldr	x2, [x1, 504]
	ldr	x1, [x1, 440]
	sub	x0, x0, x2
	mov	x2, -6148914691236517206
	asr	x0, x0, 1
	movk	x2, 0xaaab, lsl 0
	mul	x0, x0, x2
	and	x0, x0, 65535
	ldrh	w0, [x1, x0, lsl 1]
	ret
.L592:
	mov	w0, 0
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR2
	add	x4, x2, :lo12:.LANCHOR2
	and	w0, w0, 65535
	ldr	x1, [x4, 544]
	cbz	x1, .L600
	ldrh	w3, [x4, 552]
	mov	w5, 7
	ldr	x4, [x4, 504]
	mov	w6, 6
	mov	w7, 65535
	sub	x1, x1, x4
	mul	w3, w3, w5
	asr	x1, x1, 1
	asr	w3, w3, 3
	cmp	w0, w3
	csel	w0, w3, w0, gt
	mov	x3, -6148914691236517206
	movk	x3, 0xaaab, lsl 0
	mul	x1, x1, x3
	mov	w3, 0
	and	w1, w1, 65535
.L596:
	cmp	w0, w3
	beq	.L599
	umull	x5, w1, w6
	ldrh	w5, [x4, x5]
	cmp	w5, w7
	bne	.L597
.L599:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2, 440]
	ldrh	w0, [x0, x1]
	ret
.L597:
	add	w3, w3, 1
	mov	w1, w5
	and	w3, w3, 65535
	b	.L596
.L600:
	mov	w0, 0
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC5
	add	x1, x1, :lo12:.LC5
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x24, .LANCHOR0
	add	x24, x24, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	add	x20, x0, 12
	stp	x25, x26, [sp, 64]
	mov	x22, x0
	str	x27, [sp, 80]
	bl	strcpy
	ldr	w2, [x24, 176]
	mov	x0, x20
	adrp	x1, .LC6
	add	x1, x1, :lo12:.LC6
	bl	sprintf
	add	x20, x20, w0, sxtw
	ldr	w2, [x24, 2560]
	mov	x0, x20
	adrp	x1, .LC7
	add	x1, x1, :lo12:.LC7
	bl	sprintf
	add	x20, x20, w0, sxtw
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0, #:lo12:.LANCHOR1+504]
	cmp	w0, 1
	beq	.L602
	sub	w0, w20, w22
.L601:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L602:
	add	x3, x29, 108
	add	x2, x29, 104
	add	x1, x29, 100
	add	x0, x29, 96
	bl	NandcGetTimeCfg
	adrp	x25, .LANCHOR2
	ldp	w4, w5, [x29, 104]
	adrp	x1, .LC8
	ldp	w2, w3, [x29, 96]
	add	x1, x1, :lo12:.LC8
	mov	x0, x20
	add	x23, x25, :lo12:.LANCHOR2
	bl	sprintf
	add	x21, x20, w0, sxtw
	mov	x0, x21
	adrp	x1, .LC9
	add	x1, x1, :lo12:.LC9
	bl	strcpy
	ldr	w2, [x24, 2616]
	add	x21, x21, 10
	mov	x0, x21
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 716]
	mov	x0, x21
	adrp	x1, .LC11
	add	x1, x1, :lo12:.LC11
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 720]
	mov	x0, x21
	adrp	x1, .LC12
	add	x1, x1, :lo12:.LC12
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 724]
	mov	x0, x21
	adrp	x1, .LC13
	add	x1, x1, :lo12:.LC13
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 728]
	mov	x0, x21
	adrp	x1, .LC14
	add	x1, x1, :lo12:.LC14
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 732]
	mov	x0, x21
	adrp	x1, .LC15
	add	x1, x1, :lo12:.LC15
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 736]
	mov	x0, x21
	adrp	x1, .LC16
	add	x1, x1, :lo12:.LC16
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 740]
	mov	x0, x21
	adrp	x1, .LC17
	add	x1, x1, :lo12:.LC17
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 744]
	mov	x0, x21
	adrp	x1, .LC18
	add	x1, x1, :lo12:.LC18
	lsr	w2, w2, 11
	bl	sprintf
	ldr	w2, [x23, 748]
	add	x21, x21, w0, sxtw
	mov	x0, x21
	adrp	x1, .LC19
	add	x1, x1, :lo12:.LC19
	lsr	w2, w2, 11
	bl	sprintf
	ldr	w2, [x23, 752]
	add	x21, x21, w0, sxtw
	mov	x0, x21
	adrp	x1, .LC20
	add	x1, x1, :lo12:.LC20
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 756]
	adrp	x1, .LC21
	add	x1, x1, :lo12:.LC21
	mov	x0, x21
	bl	sprintf
	add	x21, x21, w0, sxtw
	bl	FtlBbtCalcTotleCnt
	and	w3, w0, 65535
	ldrh	w2, [x24, 2630]
	mov	x0, x21
	adrp	x1, .LC22
	add	x1, x1, :lo12:.LC22
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 552]
	mov	x0, x21
	adrp	x1, .LC23
	add	x1, x1, :lo12:.LC23
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 760]
	mov	x0, x21
	adrp	x1, .LC24
	add	x1, x1, :lo12:.LC24
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 764]
	mov	x0, x21
	adrp	x1, .LC25
	add	x1, x1, :lo12:.LC25
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 768]
	mov	x0, x21
	adrp	x1, .LC26
	add	x1, x1, :lo12:.LC26
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 448]
	mov	x0, x21
	adrp	x1, .LC27
	add	x1, x1, :lo12:.LC27
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 772]
	mov	x0, x21
	adrp	x1, .LC28
	add	x1, x1, :lo12:.LC28
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x23, 776]
	mov	x0, x21
	adrp	x1, .LC29
	add	x1, x1, :lo12:.LC29
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 486]
	mov	x0, x21
	adrp	x1, .LC30
	add	x1, x1, :lo12:.LC30
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 484]
	mov	x0, x21
	adrp	x1, .LC31
	add	x1, x1, :lo12:.LC31
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x24, 2584]
	mov	x0, x21
	adrp	x1, .LC32
	add	x1, x1, :lo12:.LC32
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x24, 2576]
	mov	x0, x21
	adrp	x1, .LC33
	add	x1, x1, :lo12:.LC33
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x24, 2468]
	mov	x0, x21
	adrp	x1, .LC34
	add	x1, x1, :lo12:.LC34
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x24, 2726]
	mov	x0, x21
	adrp	x1, .LC35
	add	x1, x1, :lo12:.LC35
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x24, 2480]
	mov	x0, x21
	adrp	x1, .LC36
	add	x1, x1, :lo12:.LC36
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 780]
	mov	x0, x21
	adrp	x1, .LC37
	add	x1, x1, :lo12:.LC37
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w2, [x24, 2484]
	mov	x0, x21
	adrp	x1, .LC38
	add	x1, x1, :lo12:.LC38
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 784]
	mov	x0, x21
	adrp	x1, .LC39
	add	x1, x1, :lo12:.LC39
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x24, 2624]
	mov	x0, x21
	adrp	x1, .LC40
	add	x1, x1, :lo12:.LC40
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 562]
	mov	x0, x21
	adrp	x1, .LC41
	add	x1, x1, :lo12:.LC41
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x23, 566]
	mov	x0, x21
	adrp	x1, .LC42
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 560]
	mov	x0, x21
	adrp	x1, .LC43
	add	x1, x1, :lo12:.LC43
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x23, 568]
	mov	x0, x21
	adrp	x1, .LC44
	add	x1, x1, :lo12:.LC44
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 564]
	mov	x0, x21
	adrp	x1, .LC45
	add	x1, x1, :lo12:.LC45
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w1, [x23, 560]
	ldr	x0, [x23, 520]
	ldrh	w2, [x0, x1, lsl 1]
	mov	x0, x21
	adrp	x1, .LC46
	add	x1, x1, :lo12:.LC46
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 610]
	mov	x0, x21
	adrp	x1, .LC47
	add	x1, x1, :lo12:.LC47
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x23, 614]
	mov	x0, x21
	adrp	x1, .LC48
	add	x1, x1, :lo12:.LC48
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 608]
	mov	x0, x21
	adrp	x1, .LC49
	add	x1, x1, :lo12:.LC49
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x23, 616]
	mov	x0, x21
	adrp	x1, .LC50
	add	x1, x1, :lo12:.LC50
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 612]
	mov	x0, x21
	adrp	x1, .LC51
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w1, [x23, 608]
	ldr	x0, [x23, 520]
	ldrh	w2, [x0, x1, lsl 1]
	mov	x0, x21
	adrp	x1, .LC52
	add	x1, x1, :lo12:.LC52
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 658]
	mov	x0, x21
	adrp	x1, .LC53
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x23, 662]
	mov	x0, x21
	adrp	x1, .LC54
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 656]
	mov	x0, x21
	adrp	x1, .LC55
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x23, 664]
	mov	x0, x21
	adrp	x1, .LC56
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 660]
	mov	x0, x21
	adrp	x1, .LC57
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 802]
	mov	x0, x21
	adrp	x1, .LC58
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x23, 806]
	mov	x0, x21
	adrp	x1, .LC59
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 800]
	mov	x0, x21
	adrp	x1, .LC60
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrb	w2, [x23, 808]
	mov	x0, x21
	adrp	x1, .LC61
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldrh	w2, [x23, 804]
	mov	x0, x21
	adrp	x1, .LC62
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x21, x21, w0, sxtw
	ldr	w5, [x23, 924]
	adrp	x1, .LC63
	ldr	w3, [x23, 928]
	add	x1, x1, :lo12:.LC63
	ldr	w4, [x23, 932]
	ldr	w0, [x24, 2372]
	ldr	w2, [x23, 1360]
	orr	w2, w0, w2, lsl 8
	mov	x0, x21
	bl	sprintf
	add	x19, x21, w0, sxtw
	ldr	w2, [x23, 920]
	mov	x0, x19
	adrp	x1, .LC64
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w2, [x23, 944]
	mov	x0, x19
	adrp	x1, .LC65
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x23, 1364]
	mov	x0, x19
	adrp	x1, .LC66
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x23, 1366]
	mov	x0, x19
	adrp	x1, .LC67
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w2, [x23, 1368]
	mov	x0, x19
	adrp	x1, .LC68
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x23, 1372]
	adrp	x1, .LC69
	add	x1, x1, :lo12:.LC69
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	bl	GetFreeBlockMinEraseCount
	and	w2, w0, 65535
	adrp	x1, .LC70
	mov	x0, x19
	add	x1, x1, :lo12:.LC70
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w0, [x23, 552]
	bl	GetFreeBlockMaxEraseCount
	and	w2, w0, 65535
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w0, [x23, 800]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L604
	ldr	x1, [x23, 520]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x1, x0]
	mov	x0, x19
	adrp	x1, .LC72
	add	x1, x1, :lo12:.LC72
	bl	sprintf
	add	x19, x19, w0, sxtw
.L604:
	mov	w0, 0
	adrp	x23, .LC73
	bl	List_get_gc_head_node
	add	x21, x25, :lo12:.LANCHOR2
	and	w3, w0, 65535
	add	x23, x23, :lo12:.LC73
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
.L606:
	cmp	w3, w27
	beq	.L605
	umull	x24, w3, w26
	ldr	x0, [x21, 504]
	ldr	x4, [x21, 440]
	ubfiz	x1, x3, 1, 16
	ldr	x2, [x21, 520]
	add	x0, x0, x24
	ldrh	w6, [x4, x1]
	ldrh	w5, [x0, 4]
	mov	x0, x19
	ldrh	w4, [x2, x1]
	mov	w2, w20
	mov	x1, x23
	add	w20, w20, 1
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 504]
	cmp	w20, 16
	ldrh	w3, [x0, x24]
	bne	.L606
.L605:
	add	x25, x25, :lo12:.LANCHOR2
	adrp	x21, .LC74
	add	x21, x21, :lo12:.LC74
	mov	w20, 0
	mov	w24, 65535
	mov	w26, 6
	ldr	x0, [x25, 504]
	ldr	x3, [x25, 544]
	sub	x3, x3, x0
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	movk	x0, 0xaaab, lsl 0
	mul	x3, x3, x0
	and	w3, w3, 65535
.L608:
	cmp	w3, w24
	beq	.L607
	umull	x23, w3, w26
	ldr	x0, [x25, 504]
	ldr	x2, [x25, 440]
	ubfiz	x1, x3, 1, 16
	add	x0, x0, x23
	ldrh	w5, [x2, x1]
	mov	w2, w20
	ldrh	w4, [x0, 4]
	mov	x1, x21
	mov	x0, x19
	add	w20, w20, 1
	bl	sprintf
	add	x19, x19, w0, sxtw
	cmp	w20, 4
	beq	.L607
	ldr	x0, [x25, 504]
	ldrh	w3, [x0, x23]
	b	.L608
.L607:
	sub	w0, w19, w22
	b	.L601
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	ftl_proc_ftl_read
	.type	ftl_proc_ftl_read, %function
ftl_proc_ftl_read:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LC75
	adrp	x1, .LC76
	add	x2, x2, :lo12:.LC75
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC76
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
	adrp	x8, .LANCHOR2
	add	x0, x8, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	ldr	w2, [x0, 768]
	ldr	w1, [x0, 776]
	cmp	w2, w1
	bcs	.L617
	adrp	x1, .LANCHOR0
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x5, [x0, 440]
	str	wzr, [x0, 760]
	ldrh	w3, [x2, 2480]
	mov	x2, 0
.L618:
	cmp	w3, w2
	bhi	.L619
	add	x2, x8, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x2, 760]
	ldrh	w1, [x1, 2536]
	udiv	w3, w0, w3
	str	w3, [x2, 768]
	ldr	w3, [x2, 764]
	sub	w0, w0, w3
	udiv	w0, w0, w1
	str	w0, [x2, 760]
.L620:
	add	x1, x8, :lo12:.LANCHOR2
	ldr	w9, [x1, 776]
	ldr	w10, [x1, 768]
	add	w0, w9, 256
	cmp	w0, w10
	bls	.L625
	ldr	w1, [x1, 772]
	add	w0, w9, 768
	cmp	w0, w1
	bls	.L625
	adrp	x0, .LANCHOR0+2372
	ldr	w0, [x0, #:lo12:.LANCHOR0+2372]
	cbnz	w0, .L626
.L628:
	mov	w20, 65535
.L627:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L619:
	ldrh	w4, [x5, x2, lsl 1]
	add	x2, x2, 1
	ldr	w6, [x0, 760]
	add	w4, w4, w6
	str	w4, [x0, 760]
	b	.L618
.L617:
	ldr	w1, [x0, 772]
	cmp	w2, w1
	bls	.L620
	add	w1, w1, 1
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	str	w1, [x0, 772]
	mov	w1, 0
.L622:
	ldrh	w3, [x2, 2480]
	cmp	w1, w3
	bcs	.L620
	ldr	x5, [x0, 440]
	ubfiz	x4, x1, 1, 32
	add	w1, w1, 1
	ldrh	w3, [x5, x4]
	add	w3, w3, 1
	strh	w3, [x5, x4]
	b	.L622
.L626:
	cmp	w9, 40
	bhi	.L628
.L625:
	add	x0, x8, :lo12:.LANCHOR2
	ldrh	w0, [x0, 552]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	and	w6, w0, 65535
	add	w0, w9, 64
	cmp	w6, w0
	bcs	.L629
	cmp	w9, 40
	bhi	.L628
.L629:
	add	x1, x8, :lo12:.LANCHOR2
	ldr	x0, [x1, 512]
	cbz	x0, .L628
	adrp	x2, .LANCHOR0+2480
	mov	w4, 65535
	mov	x13, -6148914691236517206
	mov	w11, w4
	ldrh	w12, [x2, #:lo12:.LANCHOR0+2480]
	mov	w2, w4
	ldr	x5, [x1, 440]
	movk	x13, 0xaaab, lsl 0
	ldr	x7, [x1, 504]
	mov	w14, 6
	mov	w1, 0
.L630:
	ldrh	w3, [x0]
	cmp	w3, w11
	bne	.L633
	mov	w20, w2
.L632:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L628
	ubfiz	x7, x20, 1, 32
	ldrh	w3, [x5, x7]
	cmp	w9, w3
	bcs	.L634
	bl	GetFreeBlockMinEraseCount
	cmp	w9, w0, uxth
	bcs	.L634
	add	x0, x8, :lo12:.LANCHOR2
	str	w4, [x0, 776]
.L634:
	cmp	w10, w3
	bls	.L628
	add	w0, w3, 128
	cmp	w6, w0
	ble	.L628
	add	w0, w3, 256
	cmp	w10, w0
	bhi	.L635
	add	x0, x8, :lo12:.LANCHOR2
	add	w3, w3, 768
	ldr	w0, [x0, 772]
	cmp	w3, w0
	bcs	.L628
.L635:
	add	x19, x8, :lo12:.LANCHOR2
	ldrh	w5, [x5, x7]
	mov	w2, w10
	mov	w1, w20
	ldr	x0, [x19, 520]
	ldr	w3, [x19, 772]
	ldrh	w4, [x0, x7]
	adrp	x0, .LC77
	add	x0, x0, :lo12:.LC77
	bl	printk
	mov	w0, 1
	str	w0, [x19, 1376]
	b	.L627
.L633:
	add	w1, w1, 1
	and	w1, w1, 65535
	cmp	w1, w12
	bhi	.L628
	ldrh	w15, [x0, 4]
	cbz	w15, .L631
	sub	x0, x0, x7
	asr	x0, x0, 1
	mul	x0, x0, x13
	and	w20, w0, 65535
	and	x0, x0, 65535
	ldrh	w0, [x5, x0, lsl 1]
	cmp	w9, w0
	bcs	.L632
	cmp	w4, w0
	bls	.L631
	mov	w4, w0
	mov	w2, w20
.L631:
	umaddl	x0, w3, w14, x7
	b	.L630
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
	and	w0, w0, 65535
	mov	w1, 65535
	cmp	w0, w1
	beq	.L646
	stp	x29, x30, [sp, -16]!
	adrp	x2, .LANCHOR2+520
	ubfiz	x1, x0, 1, 16
	add	x29, sp, 0
	ldr	x2, [x2, #:lo12:.LANCHOR2+520]
	strh	wzr, [x2, x1]
	bl	INSERT_FREE_LIST
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L646:
	mov	w0, 0
	ret
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	adrp	x3, .LANCHOR0
	mov	x7, x0
	add	x9, x3, :lo12:.LANCHOR0
	mov	w8, 0
	mov	w10, 24
	mov	w11, 1
	mov	w4, 4
	mov	w12, 56
	str	wzr, [x1, 1380]
.L650:
	ldrh	w0, [x9, 2472]
	cmp	w8, w0
	bcc	.L651
	add	x3, x3, :lo12:.LANCHOR0
	add	x1, x7, :lo12:.LANCHOR2
	mov	w6, 4
	mov	w7, 24
.L652:
	ldr	w2, [x1, 1416]
	cmp	w0, w2
	bcc	.L653
	ret
.L651:
	umull	x2, w8, w10
	ldr	x5, [x1, 1384]
	ldr	x6, [x1, 1392]
	add	x0, x5, x2
	str	w11, [x0, 16]
	ldrh	w0, [x9, 2554]
	mul	w0, w0, w8
	sdiv	w0, w0, w4
	add	x0, x6, w0, sxtw 2
	str	x0, [x5, x2]
	ldrh	w0, [x9, 2556]
	ldr	x6, [x1, 1384]
	ldr	x13, [x1, 1400]
	add	x5, x6, x2
	mul	w0, w0, w8
	sdiv	w0, w0, w4
	add	x0, x13, w0, sxtw 2
	str	x0, [x5, 8]
	ldr	x0, [x1, 1408]
	ldr	x2, [x6, x2]
	nop // between mem op and mult-accumulate
	umaddl	x0, w8, w12, x0
	add	w8, w8, 1
	and	w8, w8, 65535
	str	x2, [x0, 8]
	ldr	x2, [x5, 8]
	str	x2, [x0, 16]
	b	.L650
.L653:
	umull	x5, w0, w7
	ldr	x4, [x1, 1384]
	ldr	x8, [x1, 1392]
	add	x2, x4, x5
	str	wzr, [x2, 16]
	ldrh	w2, [x3, 2554]
	mul	w2, w2, w0
	sdiv	w2, w2, w6
	add	x2, x8, w2, sxtw 2
	str	x2, [x4, x5]
	ldrh	w2, [x3, 2556]
	ldr	x4, [x1, 1384]
	add	x4, x4, x5
	ldr	x5, [x1, 1400]
	mul	w2, w2, w0
	add	w0, w0, 1
	and	w0, w0, 65535
	sdiv	w2, w2, w6
	add	x2, x5, w2, sxtw 2
	str	x2, [x4, 8]
	b	.L652
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	mov	w3, 0
	mov	w7, 56
	mov	w9, 24
	ldr	w8, [x2, 1416]
	ldr	x4, [x2, 1384]
.L655:
	cmp	w3, w1
	bcs	.L654
	umaddl	x6, w3, w7, x0
	mov	w2, 0
	b	.L660
.L656:
	add	w2, w2, 1
	and	w2, w2, 65535
.L660:
	cmp	w2, w8
	bcs	.L657
	umull	x5, w2, w9
	add	x10, x4, x5
	ldr	x11, [x4, x5]
	ldr	x5, [x6, 8]
	cmp	x11, x5
	bne	.L656
	str	wzr, [x10, 16]
.L657:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L655
.L654:
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
	ldr	w5, [x2, 1416]
	ldr	x6, [x2, 1384]
.L662:
	cmp	w3, w1
	bcs	.L661
	mov	w2, 0
	b	.L667
.L663:
	add	w2, w2, 1
	and	w2, w2, 65535
.L667:
	cmp	w2, w5
	bcs	.L664
	umaddl	x4, w2, w7, x6
	ldr	w10, [x4, 16]
	cbnz	w10, .L663
	umaddl	x2, w3, w9, x0
	ldr	x10, [x4]
	str	w8, [x4, 16]
	str	x10, [x2, 8]
	ldr	x4, [x4, 8]
	str	x4, [x2, 16]
.L664:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L662
.L661:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	and	w0, w0, 65535
	ldrh	w2, [x1, 1420]
	ldr	x3, [x1, 1424]
	mov	x1, 0
.L669:
	cmp	w2, w1, uxth
	bhi	.L671
	mov	w0, 0
	ret
.L671:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L669
	mov	w0, 1
	ret
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	mov	w5, w0
	mov	w8, w1
	mov	w7, w2
	stp	x29, x30, [sp, -16]!
	lsr	w0, w0, 10
	add	x29, sp, 0
	bl	P2V_block_in_plane
	and	w9, w0, 65535
	adrp	x3, .LANCHOR2
	add	x4, x3, :lo12:.LANCHOR2
	ldrh	w1, [x4, 1420]
	ldr	x6, [x4, 1424]
	mov	x4, 0
.L674:
	and	w2, w4, 65535
	cmp	w2, w1
	bcc	.L676
	bne	.L675
	and	x4, x4, 65535
	strh	w0, [x6, x4, lsl 1]
	add	x4, x3, :lo12:.LANCHOR2
	ldrh	w0, [x4, 1420]
	add	w0, w0, 1
	strh	w0, [x4, 1420]
	b	.L675
.L676:
	add	x4, x4, 1
	add	x2, x6, x4, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w9
	bne	.L674
.L675:
	add	x0, x3, :lo12:.LANCHOR2
	mov	w1, 12
	ldrh	w3, [x0, 1432]
	ldr	x4, [x0, 1440]
	umull	x3, w3, w1
	add	x4, x4, x3
	str	w8, [x4, 4]
	ldr	x1, [x0, 1440]
	add	x4, x1, x3
	str	w7, [x4, 8]
	str	w5, [x1, x3]
	ldrh	w1, [x0, 1432]
	add	w1, w1, 1
	strh	w1, [x0, 1432]
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
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
	ldrh	w0, [x19, 1448]
	cmp	w0, w20
	beq	.L680
	ldrh	w0, [x19, 1450]
	cmp	w0, w20
	beq	.L680
	ldrh	w0, [x19, 1452]
	cmp	w0, w20
	beq	.L680
	ldrh	w0, [x19, 1454]
	cmp	w0, w20
	beq	.L680
	mov	w1, w20
	adrp	x0, .LC78
	add	x0, x0, :lo12:.LC78
	bl	printk
	ldrh	w1, [x19, 1448]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L682
	strh	w20, [x19, 1448]
.L680:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L682:
	ldrh	w1, [x19, 1450]
	cmp	w1, w0
	bne	.L683
	strh	w20, [x19, 1450]
	b	.L680
.L683:
	ldrh	w1, [x19, 1452]
	cmp	w1, w0
	bne	.L684
	strh	w20, [x19, 1452]
	b	.L680
.L684:
	ldrh	w1, [x19, 1454]
	cmp	w1, w0
	bne	.L680
	strh	w20, [x19, 1454]
	b	.L680
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
	ldrh	w0, [x19, 1448]
	cmp	w0, w20
	beq	.L693
	ldrh	w0, [x19, 1450]
	cmp	w0, w20
	beq	.L693
	ldrh	w0, [x19, 1452]
	cmp	w0, w20
	beq	.L693
	ldrh	w0, [x19, 1454]
	mov	w21, 0
	cmp	w0, w20
	beq	.L686
	mov	w1, w20
	adrp	x0, .LC78
	add	x0, x0, :lo12:.LC78
	bl	printk
	ldrh	w1, [x19, 1448]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L688
	strh	w20, [x19, 1448]
.L686:
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L688:
	ldrh	w1, [x19, 1450]
	cmp	w1, w0
	bne	.L689
	strh	w20, [x19, 1450]
	b	.L686
.L689:
	ldrh	w1, [x19, 1452]
	cmp	w1, w0
	bne	.L690
	strh	w20, [x19, 1452]
	b	.L686
.L690:
	ldrh	w1, [x19, 1454]
	cmp	w1, w0
	bne	.L695
	strh	w20, [x19, 1454]
	b	.L686
.L693:
	mov	w21, 0
	b	.L686
.L695:
	mov	w21, -1
	b	.L686
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
	ldrh	w1, [x22, 1456]
	mov	w2, w20
	and	w21, w0, 65535
	adrp	x0, .LC79
	add	x0, x0, :lo12:.LC79
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	adrp	x0, .LANCHOR0+2372
	ldr	w0, [x0, #:lo12:.LANCHOR0+2372]
	cbz	w0, .L698
	ldr	x2, [x22, 440]
	ubfiz	x0, x21, 1, 16
	ldrh	w1, [x2, x0]
	cmp	w1, 39
	bls	.L698
	sub	w1, w1, #40
	strh	w1, [x2, x0]
.L698:
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 0
	add	x2, x2, 1464
	ldrh	w0, [x2, -8]
.L699:
	cmp	w0, w1, uxth
	bhi	.L701
	cmp	w0, 15
	bhi	.L700
	add	x19, x19, :lo12:.LANCHOR2
	add	w1, w0, 1
	add	x19, x19, 1464
	strh	w1, [x19, -8]
	strh	w20, [x19, w0, sxtw 1]
	b	.L700
.L701:
	add	x1, x1, 1
	add	x3, x2, x1, lsl 1
	ldrh	w3, [x3, -2]
	cmp	w3, w20
	bne	.L699
.L700:
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
	ldrh	w1, [x0, 1456]
	cbz	w1, .L707
	ldrh	w3, [x0, 1448]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L707
	ldrh	w2, [x0, 1498]
	cmp	w2, w1
	bcc	.L708
	strh	wzr, [x0, 1498]
.L708:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x19, 1464
	ldrh	w1, [x19, 1498]
	ldrh	w0, [x0, x1, lsl 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 1498]
	add	w0, w0, 1
	strh	w0, [x19, 1498]
.L707:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
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
	.global	BuildFlashLsbPageTable
	.type	BuildFlashLsbPageTable, %function
BuildFlashLsbPageTable:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w20, w1
	cbnz	w0, .L716
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 208
	mov	x0, 0
.L717:
	strh	w0, [x1, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L717
.L723:
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	add	x19, x19, 1500
	mov	w1, 255
	mov	w2, 2048
	mov	x0, x19
	bl	ftl_memset
	and	w20, w20, 65535
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 208
	mov	x1, 0
.L718:
	cmp	w20, w1, uxth
	bhi	.L751
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L716:
	cmp	w0, 1
	bne	.L719
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	add	x2, x2, 208
	mov	x1, 0
.L722:
	and	w0, w1, 65535
	cmp	x1, 3
	bls	.L720
	ubfiz	w3, w0, 1, 15
	and	w0, w0, 1
	add	w0, w0, 2
	sub	w0, w3, w0
	and	w0, w0, 65535
.L720:
	strh	w0, [x2, x1, lsl 1]
	add	x1, x1, 1
	cmp	x1, 512
	bne	.L722
	b	.L723
.L719:
	cmp	w0, 2
	bne	.L724
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	add	x2, x2, 208
	mov	w1, 65535
	mov	x0, 0
.L726:
	cmp	x0, 2
	and	w3, w0, 65535
	csel	w3, w3, w1, cc
	strh	w3, [x2, x0, lsl 1]
	add	w1, w1, 2
	add	x0, x0, 1
	and	w1, w1, 65535
	cmp	x0, 512
	bne	.L726
	b	.L723
.L724:
	cmp	w0, 3
	bne	.L727
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	add	x2, x2, 208
	mov	x1, 0
.L730:
	and	w0, w1, 65535
	cmp	x1, 5
	bls	.L728
	ubfiz	w3, w0, 1, 15
	and	w0, w0, 1
	add	w0, w0, 4
	sub	w0, w3, w0
	and	w0, w0, 65535
.L728:
	strh	w0, [x2, x1, lsl 1]
	add	x1, x1, 1
	cmp	x1, 512
	bne	.L730
	b	.L723
.L727:
	cmp	w0, 4
	bne	.L731
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w3, 1
	add	x1, x1, 224
	strh	w0, [x1, -8]
	mov	w0, 5
	strh	w3, [x1, -14]
	mov	w3, 2
	strh	w0, [x1, -6]
	mov	w0, 7
	strh	w3, [x1, -12]
	mov	w3, 3
	strh	w0, [x1, -4]
	mov	w0, 8
	strh	wzr, [x1, -16]
	strh	w0, [x1, -2]
	mov	w0, 8
	strh	w3, [x1, -10]
.L733:
	and	w3, w0, 1
	ubfiz	w2, w0, 1, 15
	add	w3, w3, 6
	add	w0, w0, 1
	sub	w2, w2, w3
	strh	w2, [x1], 2
	and	w0, w0, 65535
	cmp	w0, 512
	bne	.L733
	b	.L723
.L731:
	cmp	w0, 5
	bne	.L734
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x2, x1, 208
	mov	x0, 0
.L735:
	strh	w0, [x2, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L735
	add	x1, x1, 240
.L736:
	strh	w0, [x1], 2
	add	w0, w0, 2
	and	w0, w0, 65535
	cmp	w0, 1008
	bne	.L736
	b	.L723
.L734:
	cmp	w0, 6
	bne	.L737
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	add	x3, x3, 208
	mov	w1, 0
	mov	x2, 0
	mov	w4, 12
	mov	w5, 10
.L740:
	and	w0, w2, 65535
	cmp	x2, 5
	bls	.L738
	tst	x0, 1
	csel	w0, w4, w5, ne
	sub	w0, w1, w0
	and	w0, w0, 65535
.L738:
	strh	w0, [x3, x2, lsl 1]
	add	w1, w1, 3
	and	w1, w1, 65535
	add	x2, x2, 1
	cmp	w1, 1536
	bne	.L740
	b	.L723
.L737:
	cmp	w0, 9
	bne	.L741
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w2, 1
	add	x0, x0, 214
	mov	w1, 3
	strh	w2, [x0, -4]
	mov	w2, 2
	strh	wzr, [x0, -6]
	strh	w2, [x0, -2]
.L742:
	strh	w1, [x0], 2
	add	w1, w1, 2
	and	w1, w1, 65535
	cmp	w1, 1021
	bne	.L742
	b	.L723
.L741:
	cmp	w0, 10
	bne	.L743
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x2, x1, 208
	mov	x0, 0
.L744:
	strh	w0, [x2, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L744
	add	x1, x1, 334
.L745:
	strh	w0, [x1], 2
	add	w0, w0, 2
	and	w0, w0, 65535
	cmp	w0, 961
	bne	.L745
	b	.L723
.L743:
	cmp	w0, 11
	bne	.L746
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x2, x1, 208
	mov	x0, 0
.L747:
	strh	w0, [x2, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L747
	add	x1, x1, 224
.L749:
	and	w3, w0, 1
	ubfiz	w2, w0, 1, 15
	add	w3, w3, 6
	add	w0, w0, 1
	sub	w2, w2, w3
	strh	w2, [x1], 2
	and	w0, w0, 65535
	cmp	w0, 512
	bne	.L749
	b	.L723
.L746:
	cmp	w0, 12
	bne	.L723
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w2, 1
	add	x0, x0, 216
	mov	w1, 4
	strh	w2, [x0, -6]
	mov	w2, 2
	strh	wzr, [x0, -8]
	strh	w2, [x0, -4]
	mov	w2, 3
	strh	w2, [x0, -2]
.L750:
	sub	w2, w1, #1
	add	w2, w2, w1, lsr 1
	add	w1, w1, 1
	strh	w2, [x0], 2
	and	w1, w1, 65535
	cmp	w1, 512
	bne	.L750
	b	.L723
.L751:
	ldrh	w2, [x0, x1, lsl 1]
	add	x1, x1, 1
	strh	w2, [x19, w2, sxtw 1]
	b	.L718
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	adrp	x22, .LANCHOR0
	add	x19, x22, :lo12:.LANCHOR0
	add	x20, x19, 1232
	strb	wzr, [x0, 3548]
	adrp	x0, .LANCHOR1+482
	strb	wzr, [x19, 2358]
	ldrh	w0, [x0, #:lo12:.LANCHOR1+482]
	bl	FlashBlockAlignInit
	mov	w2, 8
	mov	w1, 0
	add	x0, x19, 2360
	bl	ftl_memset
	mov	w2, 32
	mov	w1, 0
	mov	x0, x20
	bl	ftl_memset
	mov	w2, 192
	mov	w1, 0
	add	x0, x19, 2164
	bl	ftl_memset
	ldr	x7, [x19, 88]
	add	x11, x19, 2132
	mov	x8, 0
	add	x12, x7, 1
.L775:
	ldrb	w2, [x7]
	add	x1, x11, x8, lsl 3
	mov	x0, x12
	bl	FlashMemCmp8
	cbnz	w0, .L774
	ldrb	w0, [x19, 2358]
	add	w1, w0, 1
	strb	w1, [x19, 2358]
	str	wzr, [x20, x0, lsl 2]
	add	x0, x19, x0
	strb	w8, [x0, 2360]
.L774:
	add	x8, x8, 1
	cmp	x8, 4
	bne	.L775
	add	x8, x22, :lo12:.LANCHOR0
	add	x0, x21, :lo12:.LANCHOR2
	ldrb	w1, [x8, 2358]
	strb	w1, [x0, 3548]
	ldrb	w0, [x7, 8]
	cmp	w0, 2
	beq	.L776
.L780:
	add	x9, x22, :lo12:.LANCHOR0
	ldrh	w1, [x7, 14]
	add	x10, x21, :lo12:.LANCHOR2
	ldp	x19, x20, [sp, 16]
	ldrb	w0, [x9, 2358]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	mul	w0, w0, w1
	ldrb	w1, [x7, 13]
	mul	w0, w0, w1
	strh	w0, [x10, 3550]
	ret
.L776:
	ldr	w14, [x8, 76]
	add	x15, x8, 2132
	add	x13, x8, 1232
	mov	x11, 0
.L779:
	ldrb	w2, [x7]
	add	x1, x15, x11, lsl 3
	mov	x0, x12
	bl	FlashMemCmp8
	cbnz	w0, .L777
	ldrb	w1, [x7, 13]
	ldrh	w0, [x7, 14]
	ldrb	w2, [x8, 2358]
	and	w0, w0, 65280
	mul	w1, w1, w14
	mul	w0, w0, w1
	sxtw	x1, w2
	str	w0, [x13, x1, lsl 2]
	ldrb	w3, [x7, 23]
	cbz	w3, .L778
	lsl	w0, w0, 1
	str	w0, [x13, x1, lsl 2]
.L778:
	add	x1, x8, x1
	add	w2, w2, 1
	strb	w2, [x8, 2358]
	strb	w11, [x1, 2360]
.L777:
	add	x11, x11, 1
	cmp	x11, 4
	bne	.L779
	b	.L780
	.size	FlashDieInfoInit, .-FlashDieInfoInit
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
	ldr	x1, [x0, 88]
	ldrb	w2, [x1, 9]
	ldr	w1, [x0, 76]
	mul	w1, w1, w2
	mov	x2, 0
	strh	w1, [x19, 4]
	ldrb	w1, [x0, 2464]
	strb	w1, [x19, 7]
	ldr	w1, [x0, 2584]
	str	w1, [x19]
	ldr	x1, [x0, 88]
	ldrb	w3, [x0, 2358]
	ldrb	w1, [x1, 9]
	strb	w1, [x19, 6]
	mov	w1, 32
	strb	w1, [x19, 8]
	ldr	x1, [x0, 88]
	add	x0, x0, 2360
	ldrb	w1, [x1, 7]
	strb	w1, [x19, 9]
	strb	wzr, [x19, 10]
.L789:
	cmp	w3, w2, uxtb
	bhi	.L790
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L790:
	ldrb	w1, [x2, x0]
	add	x2, x2, 1
	ldrb	w5, [x19, 10]
	lsl	w1, w4, w1
	orr	w1, w1, w5
	strb	w1, [x19, 10]
	b	.L789
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	stp	x29, x30, [sp, -80]!
	mov	w1, 65535
	mov	w2, 1024
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	add	x3, x0, 512
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	add	x4, x0, 512
	strh	wzr, [x0, 3552]
	add	x5, x0, 3328
	str	wzr, [x0, 448]
	adrp	x22, .LANCHOR0
	stp	wzr, wzr, [x3, 208]
	stp	wzr, wzr, [x3, 216]
	str	wzr, [x0, 736]
	str	wzr, [x0, 740]
	stp	wzr, wzr, [x4, 240]
	stp	wzr, wzr, [x4, 248]
	str	wzr, [x0, 772]
	str	wzr, [x0, 776]
	str	wzr, [x0, 1360]
	str	wzr, [x0, 1368]
	str	wzr, [x0, 1376]
	stp	w1, wzr, [x5, 236]
	mov	w1, -1
	stp	wzr, wzr, [x5, 228]
	str	wzr, [x0, 3572]
	strh	w1, [x0, 1448]
	strh	w1, [x0, 1450]
	strh	w1, [x0, 1452]
	strh	w1, [x0, 1454]
	mov	w1, 32
	strh	w1, [x0, 1364]
	mov	w1, 128
	strh	w1, [x0, 1366]
	add	x1, x22, :lo12:.LANCHOR0
	strh	wzr, [x0, 1372]
	strh	wzr, [x0, 1456]
	ldrh	w3, [x1, 2550]
	ldrh	w1, [x1, 2472]
	strh	wzr, [x0, 3576]
	strh	wzr, [x0, 1498]
	sdiv	w2, w2, w3
	lsl	w1, w1, 2
	str	w2, [x0, 3580]
	cmp	w2, w1
	bls	.L793
	str	w1, [x0, 3580]
.L793:
	add	x20, x22, :lo12:.LANCHOR0
	add	x19, x21, :lo12:.LANCHOR2
	mov	w24, 56
	ldrh	w0, [x20, 2548]
	str	wzr, [x20, 2600]
	lsl	w0, w0, 1
	bl	ftl_malloc
	ldrh	w1, [x20, 2548]
	str	x0, [x19, 1424]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20, 2472]
	str	x0, [x19, 1440]
	mul	w23, w23, w24
	lsl	w25, w23, 3
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19, 3584]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3592]
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19, 3600]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 432]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 1408]
	ldr	w0, [x19, 3580]
	mul	w0, w0, w24
	bl	ftl_malloc
	str	x0, [x20, 2608]
	ldrh	w0, [x20, 2472]
	ldrh	w23, [x20, 2554]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19, 1416]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3608]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3616]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3624]
	ldr	w0, [x19, 1416]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19, 1392]
	ldr	w0, [x19, 3580]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19, 3632]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3640]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3648]
	ldr	w1, [x19, 1416]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20, 2556]
	str	x0, [x19, 1384]
	ldrh	w0, [x20, 2472]
	mul	w23, w23, w0
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3656]
	lsl	w0, w23, 3
	bl	ftl_malloc
	str	x0, [x19, 3664]
	ldrh	w1, [x20, 2556]
	ldr	w0, [x19, 1416]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 1400]
	ldrh	w1, [x20, 2556]
	ldr	w0, [x19, 3580]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3672]
	ldrh	w0, [x20, 2482]
	ubfiz	w0, w0, 1, 15
	strh	w0, [x19, 3680]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19, 3688]
	ldrh	w0, [x19, 3680]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19, 3680]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w23, [x20, 2482]
	str	x0, [x19, 3696]
	add	x0, x0, 32
	str	x0, [x19, 440]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3704]
	mov	w0, w23
	bl	ftl_malloc
	ldr	w23, [x20, 2572]
	str	x0, [x19, 520]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3712]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3720]
	ldrh	w0, [x20, 2482]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x20, 64]
	ldrh	w0, [x20, 2564]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x20, 2592]
	ldrh	w0, [x20, 2564]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19, 3728]
	ldrh	w0, [x20, 2564]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3736]
	ldrh	w0, [x20, 2566]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w2, [x20, 2566]
	mov	w1, 0
	str	x0, [x19, 3744]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w23, [x20, 2580]
	lsl	w23, w23, 2
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3752]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19, 3760]
	ldr	w0, [x20, 2572]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3768]
	ldrh	w0, [x20, 2582]
	lsl	w0, w0, 4
	bl	ftl_malloc
	ldrh	w1, [x20, 2582]
	str	x0, [x19, 704]
	ldrh	w0, [x20, 2554]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3776]
	ldrh	w1, [x20, 2482]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 504]
	ldrh	w0, [x20, 2542]
	ldrh	w1, [x20, 2494]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19, 3784]
	mul	w0, w1, w0
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w3, [x19, 3784]
	add	x4, x20, 2664
	ldrh	w5, [x20, 2494]
	mov	w1, w3
	str	x0, [x20, 2656]
	mov	x0, 1
.L794:
	cmp	w0, w5
	bcc	.L795
	mov	w1, 8
	add	x3, x22, :lo12:.LANCHOR0
	sub	w1, w1, w0
	add	x3, x3, 2624
	add	x1, x1, 1
	mov	x2, 0
.L796:
	add	x2, x2, 1
	cmp	x2, x1
	bne	.L797
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0, 3712]
	cbnz	x1, .L798
.L800:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC80
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC80
	bl	printk
	mov	w0, -1
.L792:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L795:
	ldr	x2, [x20, 2656]
	add	w0, w0, 1
	add	x2, x2, w1, uxtw 2
	add	w1, w1, w3
	str	x2, [x4], 8
	b	.L794
.L797:
	add	x4, x0, x2
	add	x4, x3, x4, lsl 3
	str	xzr, [x4, 24]
	b	.L796
.L798:
	ldr	x1, [x0, 3720]
	cbz	x1, .L800
	ldr	x1, [x0, 3752]
	cbz	x1, .L800
	ldr	x1, [x0, 3768]
	cbz	x1, .L800
	ldr	x1, [x0, 704]
	cbz	x1, .L800
	ldr	x1, [x0, 3776]
	cbz	x1, .L800
	ldr	x1, [x0, 504]
	cbz	x1, .L800
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x1, [x22, 2656]
	cbz	x1, .L800
	ldr	x0, [x0, 520]
	cbz	x0, .L800
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0, 1424]
	cbz	x1, .L800
	ldr	x1, [x0, 1440]
	cbz	x1, .L800
	ldr	x1, [x0, 3584]
	cbz	x1, .L800
	ldr	x1, [x0, 3600]
	cbz	x1, .L800
	ldr	x1, [x0, 432]
	cbz	x1, .L800
	ldr	x1, [x0, 1408]
	cbz	x1, .L800
	ldr	x1, [x0, 3592]
	cbz	x1, .L800
	ldr	x1, [x0, 3608]
	cbz	x1, .L800
	ldr	x1, [x0, 3616]
	cbz	x1, .L800
	ldr	x0, [x0, 3624]
	cbz	x0, .L800
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0, 1392]
	cbz	x1, .L800
	ldr	x1, [x0, 3640]
	cbz	x1, .L800
	ldr	x1, [x0, 3648]
	cbz	x1, .L800
	ldr	x1, [x0, 1384]
	cbz	x1, .L800
	ldr	x1, [x0, 3656]
	cbz	x1, .L800
	ldr	x1, [x0, 3664]
	cbz	x1, .L800
	ldr	x1, [x0, 1400]
	cbz	x1, .L800
	ldr	x1, [x0, 440]
	cbz	x1, .L800
	ldr	x0, [x0, 3688]
	cbz	x0, .L800
	adrp	x0, .LANCHOR0+2592
	ldr	x0, [x0, #:lo12:.LANCHOR0+2592]
	cbz	x0, .L800
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0, 3728]
	cbz	x1, .L800
	ldr	x1, [x0, 3736]
	cbz	x1, .L800
	ldr	x0, [x0, 3744]
	cbz	x0, .L800
	mov	w0, 0
	b	.L792
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	adrp	x0, .LANCHOR2+3784
	mov	x19, x1
	mov	w1, 0
	ldrh	w2, [x0, #:lo12:.LANCHOR2+3784]
	mov	x0, x19
	lsl	w2, w2, 2
	bl	ftl_memset
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	mov	x1, 0
	mov	w6, 65535
	mov	w5, 1
.L897:
	ldrh	w0, [x20, x1]
	cmp	w0, w6
	beq	.L895
	ubfx	x2, x0, 5, 11
	lsl	w0, w5, w0
	lsl	x2, x2, 2
	add	x1, x1, 2
	cmp	x1, 1024
	ldr	w4, [x19, x2]
	orr	w0, w4, w0
	str	w0, [x19, x2]
	ldrh	w0, [x3, 2630]
	add	w0, w0, 1
	strh	w0, [x3, 2630]
	bne	.L897
.L895:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	add	x29, sp, 0
	mov	w2, 16
	add	x0, x0, 2636
	strh	w1, [x0, -12]
	mov	w1, 255
	strh	wzr, [x0, -6]
	bl	ftl_memset
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	stp	x29, x30, [sp, -16]!
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	mov	w2, 2048
	add	x29, sp, 0
	mov	w1, 0
	strh	w0, [x3, 2720]
	add	x0, x3, 2728
	strh	wzr, [x3, 2722]
	strh	wzr, [x3, 2724]
	strh	wzr, [x3, 2726]
	bl	ftl_memset
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
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
.L906:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L910
	adrp	x0, .LANCHOR0+2546
	mov	w23, 0
	mov	w20, 0
	ldrh	w1, [x0, #:lo12:.LANCHOR0+2546]
	ldrh	w0, [x19]
	strh	w1, [x21, x0, lsl 1]
	ldrh	w24, [x21]
.L911:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L915
	mov	w0, w23
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L910:
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20, x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L907:
	ldrh	w3, [x19, 10]
	cmp	w3, w1
	bhi	.L909
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L906
.L909:
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22, x3]
	cmp	w4, w2
	bne	.L908
	cbz	w2, .L908
	ldrh	w4, [x21, x3]
	add	w4, w4, 1
	strh	w4, [x21, x3]
.L908:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L907
.L915:
	ubfiz	x0, x20, 1, 16
	ldrh	w1, [x21, x0]
	cmp	w24, w1
	bls	.L912
	add	x25, x22, x0
	ldrh	w0, [x22, x0]
	cbnz	w0, .L913
.L914:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L911
.L912:
	cbnz	w1, .L914
	add	x25, x22, x0
	ldrh	w0, [x22, x0]
	cbz	w0, .L914
.L916:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19, 8]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	b	.L914
.L917:
	mov	w24, 0
	b	.L916
.L913:
	mov	w23, w20
	cbz	w1, .L917
	mov	w24, w1
	b	.L914
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	stp	x29, x30, [sp, -48]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	str	x21, [sp, 32]
	add	x21, x19, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x21, 3720]
	ldr	w2, [x20, 2572]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x20, 2582]
	mov	w1, 255
	ldrh	w2, [x20, 2554]
	mul	w2, w2, w0
	ldr	x0, [x21, 3776]
	bl	ftl_memset
	mov	x0, x21
	mov	w1, 0
	mov	w5, -1
.L927:
	ldrh	w2, [x20, 2582]
	cmp	w2, w1
	bhi	.L928
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, -1
	add	x1, x0, 3792
	strh	w2, [x0, 3794]
	strh	w2, [x0, 3792]
	ldr	w2, [x20, 2572]
	strh	w2, [x0, 3802]
	mov	w2, -3902
	strh	w2, [x0, 3796]
	ldrh	w2, [x0, 3856]
	strh	w2, [x0, 3800]
	ldrh	w2, [x20, 2580]
	strh	w2, [x0, 3798]
	ldr	x2, [x0, 3712]
	str	x2, [x0, 3808]
	ldr	x2, [x0, 3768]
	str	x2, [x0, 3816]
	ldr	x2, [x0, 3720]
	str	x2, [x0, 3824]
	ldr	x0, [x0, 3752]
	str	x0, [x1, 40]
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L928:
	ldr	x3, [x0, 704]
	ubfiz	x2, x1, 4, 16
	add	x3, x3, x2
	str	wzr, [x3, 4]
	ldr	x3, [x0, 704]
	strh	w5, [x3, x2]
	ldr	x3, [x0, 704]
	ldr	x4, [x0, 3776]
	add	x3, x3, x2
	ldrh	w2, [x20, 2554]
	mul	w2, w2, w1
	add	w1, w1, 1
	and	w1, w1, 65535
	sxtw	x2, w2
	and	x2, x2, -4
	add	x2, x4, x2
	str	x2, [x3, 8]
	b	.L927
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	stp	x29, x30, [sp, -32]!
	mov	w0, -1
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	strh	w0, [x19, 3872]
	mov	w0, -1
	str	w0, [x19, 3884]
	ldr	x0, [x20, 2592]
	ldrh	w2, [x20, 2564]
	strh	wzr, [x20, 2590]
	str	xzr, [x19, 3864]
	lsl	w2, w2, 1
	str	wzr, [x19, 3876]
	str	wzr, [x19, 3880]
	str	wzr, [x20, 2372]
	bl	ftl_memset
	ldr	x0, [x19, 440]
	mov	w1, 0
	ldrh	w2, [x20, 2482]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 3688]
	mov	w1, 0
	ldrh	w2, [x20, 2482]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w2, 48
	mov	w1, 0
	add	x0, x19, 456
	bl	ftl_memset
	mov	w2, 512
	mov	w1, 0
	add	x0, x19, 848
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	stp	x29, x30, [sp, -64]!
	mov	w0, 6
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x19, x20, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	add	x24, x21, 2504
	ldrh	w2, [x21, 2482]
	mov	w22, 0
	mov	w23, 0
	mul	w2, w2, w0
	ldr	x0, [x19, 504]
	bl	ftl_memset
	strh	wzr, [x19, 536]
	strh	wzr, [x19, 552]
	strh	wzr, [x19, 3552]
	str	xzr, [x19, 512]
	str	xzr, [x19, 528]
	str	xzr, [x19, 544]
	mov	w19, 0
.L933:
	ldrh	w0, [x21, 2480]
	cmp	w19, w0
	bge	.L940
	ldrh	w8, [x21, 2472]
	mov	w5, 0
	ldrh	w7, [x21, 2544]
	mov	w6, 0
	b	.L941
.L935:
	ldrb	w0, [x24, w6, sxtw]
	mov	w1, w19
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L934
	add	w5, w7, w5
	sxth	w5, w5
.L934:
	add	w6, w6, 1
	sxth	w6, w6
.L941:
	cmp	w6, w8
	blt	.L935
	cbz	w5, .L936
	mov	w0, 32768
	sdiv	w5, w0, w5
	sxth	w5, w5
.L937:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 6
	ldr	x2, [x0, 504]
	smaddl	x1, w19, w1, x2
	strh	w5, [x1, 4]
	ldrh	w1, [x0, 560]
	cmp	w19, w1
	beq	.L938
	ldrh	w1, [x0, 608]
	cmp	w19, w1
	beq	.L938
	ldrh	w1, [x0, 656]
	cmp	w19, w1
	beq	.L938
	ldr	x0, [x0, 520]
	ldrh	w0, [x0, w19, sxtw 1]
	cbnz	w0, .L939
	add	w22, w22, 1
	mov	w0, w19
	and	w22, w22, 65535
	bl	INSERT_FREE_LIST
.L938:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L933
.L936:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, -1
	ldr	x0, [x0, 520]
	strh	w1, [x0, w19, sxtw 1]
	b	.L937
.L939:
	add	w23, w23, 1
	mov	w0, w19
	and	w23, w23, 65535
	bl	INSERT_DATA_LIST
	b	.L938
.L940:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, 0
	strh	w23, [x20, 536]
	strh	w22, [x20, 552]
	ldp	x23, x24, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	stp	x29, x30, [sp, -32]!
	mov	w1, 255
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x19, 1424]
	ldrh	w2, [x20, 2548]
	strh	wzr, [x19, 1420]
	strh	wzr, [x19, 1432]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20, 2548]
	mov	w0, 12
	mov	w1, 255
	mul	w2, w2, w0
	ldr	x0, [x19, 1440]
	bl	ftl_memset
	bl	FtlGcBufInit
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
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
	.global	FlashReadIdbData
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	mov	w2, 2048
	add	x29, sp, 0
	add	x1, x1, 3888
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
	adrp	x0, .LANCHOR0
	add	x10, x0, :lo12:.LANCHOR0
	mov	x7, x0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR1
	add	x8, x19, :lo12:.LANCHOR1
	add	x10, x10, 2132
	add	x8, x8, 513
	mov	x9, 0
	str	x21, [sp, 32]
.L952:
	ldrb	w2, [x8, -1]
	mov	w11, w9
	lsl	x21, x9, 5
	mov	x1, x10
	mov	x0, x8
	bl	FlashMemCmp8
	mov	w20, w0
	cbnz	w0, .L950
	add	x2, x19, :lo12:.LANCHOR1
	ubfiz	x11, x11, 5, 32
	add	x0, x2, 512
	add	x1, x2, 3264
	add	x21, x0, x21
	add	x0, x0, x11
	ldrb	w3, [x0, 22]
	mov	x0, 0
.L951:
	lsl	x4, x0, 5
	mov	w2, w0
	ldrb	w4, [x4, x1]
	cmp	w4, w3
	beq	.L954
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L951
	mov	w2, w0
.L954:
	add	x19, x19, :lo12:.LANCHOR1
	add	x0, x7, :lo12:.LANCHOR0
	add	x3, x19, 3264
	ubfiz	x1, x2, 5, 32
	add	x19, x19, 472
	add	x1, x3, x1
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	mov	w2, 32
	mov	x1, x21
	mov	x0, x19
	bl	ftl_memcpy
	ldrh	w0, [x19, 10]
	bl	FlashBlockAlignInit
	b	.L949
.L950:
	add	x9, x9, 1
	add	x8, x8, 32
	cmp	x9, 86
	bne	.L952
	mov	w20, -1
.L949:
	mov	w0, w20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
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
	bne	.L959
	cbz	x3, .L960
	mov	w2, 1024
	mov	x1, x3
	bl	ftl_memcpy
.L960:
	cbz	x20, .L958
	lsr	w19, w19, 1
	mov	w1, 48
	ldr	w0, [x20]
	mul	w19, w19, w1
	and	x19, x19, 8176
	str	w0, [x21, x19]
.L958:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L959:
	cbz	x3, .L963
	mov	x1, x0
	mov	w2, 1024
	mov	x0, x3
	bl	ftl_memcpy
.L963:
	cbz	x20, .L958
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
	b	.L958
	.size	NandcCopy1KB, .-NandcCopy1KB
	.align	2
	.global	ftl_memcpy32
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	mov	x3, 0
.L976:
	cmp	w2, w3
	bhi	.L977
	ret
.L977:
	ldr	w4, [x1, x3, lsl 2]
	str	w4, [x0, x3, lsl 2]
	add	x3, x3, 1
	b	.L976
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
	ldr	x0, [x0, #:lo12:jiffies]
	bl	jiffies_to_msecs
	ldp	x29, x30, [sp], 16
	ret
	.size	timer_get_time, .-timer_get_time
	.align	2
	.global	FlashSramLoadStore
	.type	FlashSramLoadStore, %function
FlashSramLoadStore:
	stp	x29, x30, [sp, -16]!
	adrp	x4, .LANCHOR4+1584
	uxtw	x5, w1
	add	x29, sp, 0
	ldr	x4, [x4, #:lo12:.LANCHOR4+1584]
	add	x4, x4, 4096
	cbnz	w2, .L983
	mov	w2, w3
	add	x1, x4, x5
.L986:
	bl	ftl_memcpy
	ldp	x29, x30, [sp], 16
	ret
.L983:
	mov	x1, x0
	mov	w2, w3
	add	x0, x4, x5
	b	.L986
	.size	FlashSramLoadStore, .-FlashSramLoadStore
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
	ble	.L989
	bl	ftl_malloc
.L988:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L989:
	adrp	x1, .LANCHOR4
	add	x21, x1, :lo12:.LANCHOR4
	add	w19, w0, 63
	mov	x20, x1
	and	w19, w19, -64
	ldr	w0, [x21, 1592]
	cmp	w19, w0
	ble	.L991
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x21, 1600]
	mov	w0, 16384
	str	w0, [x21, 1592]
.L991:
	add	x1, x20, :lo12:.LANCHOR4
	ldr	w0, [x1, 1592]
	sub	w0, w0, w19
	str	w0, [x1, 1592]
	ldr	x0, [x1, 1600]
	add	x19, x0, w19, sxtw
	str	x19, [x1, 1600]
	b	.L988
	.size	ftl_dma32_malloc, .-ftl_dma32_malloc
	.align	2
	.global	rk_nand_suspend
	.type	rk_nand_suspend, %function
rk_nand_suspend:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_flash_suspend
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_flash_resume
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	adrp	x0, .LANCHOR0+2584
	ldr	w0, [x0, #:lo12:.LANCHOR0+2584]
	ret
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	2
	.global	rk_nandc_get_irq_status
	.type	rk_nandc_get_irq_status, %function
rk_nandc_get_irq_status:
	ldr	w0, [x0, 372]
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_proc_ftl_read
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_read_flash_info
	ldp	x29, x30, [sp], 16
	ret
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	2
	.global	rknand_print_hex
	.type	rknand_print_hex, %function
rknand_print_hex:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LC82
	stp	x23, x24, [sp, 48]
	mov	x22, x1
	stp	x25, x26, [sp, 64]
	adrp	x23, .LC81
	mov	x26, x0
	mov	w24, w2
	uxtw	x25, w3
	add	x23, x23, :lo12:.LC81
	add	x21, x21, :lo12:.LC82
	stp	x19, x20, [sp, 16]
	str	x27, [sp, 80]
	mov	x19, 0
	mov	w20, 0
	adrp	x27, .LC83
.L1004:
	cmp	x25, x19
	bne	.L1010
	adrp	x1, .LC83
	adrp	x0, .LC76
	add	x1, x1, :lo12:.LC83
	add	x0, x0, :lo12:.LC76
	bl	printk
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1010:
	cbnz	w20, .L1005
	mov	w3, w19
	mov	x2, x22
	mov	x1, x26
	mov	x0, x23
	bl	printk
.L1005:
	cmp	w24, 4
	bne	.L1006
	ldr	w1, [x22, x19, lsl 2]
.L1012:
	mov	x0, x21
	add	w20, w20, 1
	bl	printk
	cmp	w20, 15
	bls	.L1009
	mov	w20, 0
	add	x1, x27, :lo12:.LC83
	adrp	x0, .LC76
	add	x0, x0, :lo12:.LC76
	bl	printk
.L1009:
	add	x19, x19, 1
	b	.L1004
.L1006:
	cmp	w24, 2
	bne	.L1008
	ldrsh	w1, [x22, x19, lsl 1]
	b	.L1012
.L1008:
	ldrb	w1, [x22, x19]
	b	.L1012
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	stp	x29, x30, [sp, -128]!
	mov	w3, -83
	mov	w2, -82
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x1, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	w20, w0
	add	x0, x1, 1272
	stp	x23, x24, [sp, 48]
	cmp	w20, 2
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	strb	w20, [x1, 1272]
	mov	w1, -84
	strb	w3, [x0, 5]
	strb	w1, [x0, 4]
	mov	w1, -81
	strb	w2, [x0, 6]
	strb	w1, [x0, 7]
	bne	.L1014
	mov	w1, -89
	strb	w1, [x0, 4]
	adrp	x0, .LANCHOR1+3409
	mov	w1, -9
	strb	w1, [x0, #:lo12:.LANCHOR1+3409]
.L1079:
	mov	w27, 7
	b	.L1123
.L1014:
	cmp	w20, 3
	bne	.L1016
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
.L1122:
	mov	w27, 8
	mov	w28, w27
	strb	w1, [x0, 11]
.L1015:
	sub	w0, w20, #1
	cmp	w0, 1
	bhi	.L1021
	add	x25, x21, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR1
	add	x24, x24, :lo12:.LANCHOR1
	add	x26, x25, 1272
	add	x24, x24, 3392
	mov	w23, 0
.L1022:
	ldrb	w0, [x25, 2358]
	cmp	w0, w23
	bhi	.L1028
.L1029:
	add	x21, x21, :lo12:.LANCHOR0
	ldp	x19, x20, [sp, 16]
	strb	w28, [x21, 1273]
	strb	w27, [x21, 1274]
	ldp	x23, x24, [sp, 48]
	ldp	x21, x22, [sp, 32]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1016:
	cmp	w20, 4
	bne	.L1017
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
	b	.L1122
.L1017:
	cmp	w20, 5
	bne	.L1018
	mov	w1, 56
	strb	w1, [x0, 4]
	mov	w1, 57
	strb	w1, [x0, 5]
	mov	w1, 58
	mov	w27, 8
	strb	w1, [x0, 6]
	mov	w1, 59
	strb	w1, [x0, 7]
.L1123:
	mov	w28, 4
	b	.L1015
.L1018:
	cmp	w20, 6
	bne	.L1019
	mov	w1, 14
	strb	w1, [x0, 4]
	mov	w1, 15
	strb	w1, [x0, 5]
	mov	w1, 16
	mov	w27, 12
	strb	w1, [x0, 6]
	mov	w1, 17
	strb	w1, [x0, 7]
	b	.L1123
.L1019:
	cmp	w20, 7
	bne	.L1020
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
	b	.L1015
.L1020:
	cmp	w20, 8
	bne	.L1079
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
	b	.L1015
.L1028:
	add	x0, x25, w23, sxtw
	mov	x20, 0
	ldrb	w0, [x0, 2360]
	ubfiz	x19, x0, 6, 8
	sbfiz	x0, x0, 4, 32
	add	x1, x25, x0
	add	x19, x19, 20
	add	x19, x26, x19
	ldr	x0, [x25, x0]
	ldrb	w22, [x1, 8]
	mov	w1, 55
	add	x22, x0, x22, lsl 8
.L1023:
	add	x0, x26, x20
	str	w1, [x22, 2056]
	str	w1, [x29, 124]
	ldrb	w0, [x0, 4]
	str	w0, [x22, 2052]
	mov	x0, 400
	bl	__const_udelay
	ldr	w0, [x22, 2048]
	strb	w0, [x19, x20]
	add	x20, x20, 1
	cmp	w28, w20, uxtb
	ldr	w1, [x29, 124]
	bhi	.L1023
	mov	x0, 0
.L1026:
	add	x1, x0, 4
	add	x4, x0, 28
	add	w5, w0, 8
	add	x1, x24, x1
	add	x4, x24, x4
.L1025:
	ldrb	w6, [x19, x0]
	ldrb	w7, [x1], 4
	add	w6, w6, w7
	strb	w6, [x19, w5, sxtw]
	cmp	x4, x1
	add	w5, w5, 8
	bne	.L1025
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1026
	add	w23, w23, 1
	strb	wzr, [x19, 16]
	strb	wzr, [x19, 24]
	and	w23, w23, 255
	strb	wzr, [x19, 32]
	strb	wzr, [x19, 40]
	strb	wzr, [x19, 48]
	strb	wzr, [x19, 41]
	strb	wzr, [x19, 49]
	b	.L1022
.L1021:
	sub	w0, w20, #3
	cmp	w0, 5
	bhi	.L1029
	mul	w26, w28, w27
	sub	w25, w28, #1
	and	x25, x25, 255
	add	x22, x21, :lo12:.LANCHOR0
	mov	w23, 0
	lsl	w0, w26, 4
	asr	w24, w26, 1
	str	w0, [x29, 120]
	sub	w26, w20, #5
	lsl	w0, w24, 1
	str	w0, [x29, 124]
	add	x0, x25, 1
	str	x0, [x29, 112]
.L1030:
	ldrb	w0, [x22, 2358]
	cmp	w0, w23
	bls	.L1029
	add	x0, x22, w23, sxtw
	ldrb	w25, [x0, 2360]
	sbfiz	x0, x25, 4, 32
	add	x1, x22, x0
	ldr	x0, [x22, x0]
	ldrb	w19, [x1, 8]
	add	x19, x0, x19, lsl 8
	mov	w0, 255
	str	w0, [x19, 2056]
	mov	w0, w25
	bl	NandcWaitFlashReady
	cmp	w20, 7
	bne	.L1031
	mov	x1, 28
	mov	w0, 160
	add	x4, x22, 1272
	umaddl	x0, w0, w25, x1
	add	x4, x4, x0
.L1032:
	mov	w0, 54
	str	w0, [x19, 2056]
	cmp	w20, 4
	bne	.L1034
	mov	w0, 255
	str	w0, [x19, 2052]
	mov	w0, 64
	str	w0, [x19, 2048]
	mov	w0, 204
.L1124:
	str	w0, [x19, 2052]
	mov	w0, 77
	b	.L1125
.L1031:
	cmp	w20, 8
	beq	.L1033
	ubfiz	x4, x25, 6, 8
	add	x4, x22, x4
	add	x4, x4, 1292
	b	.L1032
.L1034:
	cmp	w26, 1
	bhi	.L1036
	ldrb	w0, [x22, 1276]
	str	w0, [x19, 2052]
	mov	w0, 82
.L1125:
	str	w0, [x19, 2048]
.L1035:
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
	bne	.L1037
	mov	w0, 31
	str	w0, [x19, 2052]
.L1038:
	mov	w0, 2
	str	w0, [x19, 2052]
	str	wzr, [x19, 2052]
.L1078:
	mov	w0, 48
	str	w0, [x19, 2056]
	str	x4, [x29, 104]
	mov	w0, w25
	bl	NandcWaitFlashReady
	cmp	w26, 1
	ldr	x4, [x29, 104]
	ccmp	w20, 8, 4, hi
	beq	.L1080
	cmp	w20, 7
	mov	w0, 2
	mov	w6, 32
	csel	w6, w6, w0, eq
.L1039:
	adrp	x1, .LANCHOR4
	add	x0, x1, :lo12:.LANCHOR4
	mov	x7, 0
	ldr	x0, [x0, 1608]
.L1040:
	ldr	w8, [x19, 2048]
	strb	w8, [x0, x7]
	add	x7, x7, 1
	cmp	w6, w7, uxtb
	bhi	.L1040
	cmp	w20, 8
	bne	.L1041
	mov	w6, 0
.L1043:
	ldrb	w7, [x0]
	cmp	w7, 50
	beq	.L1042
	ldrb	w7, [x0, 1]
	cmp	w7, 5
	beq	.L1042
	add	w6, w6, 1
	add	x0, x0, 4
	and	w6, w6, 255
	cmp	w6, 8
	bne	.L1043
.L1044:
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L1046:
	b	.L1046
.L1036:
	cmp	w20, 7
	bne	.L1035
	mov	w0, 174
	str	w0, [x19, 2052]
	str	wzr, [x19, 2048]
	mov	w0, 176
	b	.L1124
.L1037:
	str	wzr, [x19, 2052]
	b	.L1038
.L1080:
	mov	w6, 16
	b	.L1039
.L1042:
	cmp	w6, 6
	bhi	.L1044
.L1045:
	add	x0, x1, :lo12:.LANCHOR4
	ldr	x8, [x0, 1608]
	mov	x0, 0
.L1055:
	ldr	w2, [x29, 120]
	cmp	w2, w0
	bgt	.L1056
	add	x0, x1, :lo12:.LANCHOR4
	mov	w9, w24
	mov	w7, 8
	ldr	x10, [x0, 1608]
.L1058:
	mov	w0, 0
.L1057:
	add	w6, w0, w9
	add	w0, w0, 1
	sbfiz	x6, x6, 1, 32
	cmp	w24, w0
	ldrh	w11, [x10, x6]
	mvn	w11, w11
	strh	w11, [x10, x6]
	bgt	.L1057
	ldr	w0, [x29, 124]
	subs	w7, w7, #1
	add	w9, w9, w0
	bne	.L1058
	mov	x7, 0
	mov	w14, 1
.L1064:
	mov	w6, 0
	mov	w9, 0
.L1063:
	mov	w12, w7
	lsl	w13, w14, w9
	mov	w0, 16
	mov	w11, 0
.L1061:
	ldrh	w15, [x10, w12, sxtw 1]
	add	w12, w12, w24
	bics	wzr, w13, w15
	cinc	w11, w11, eq
	subs	w0, w0, #1
	bne	.L1061
	cmp	w11, 8
	bls	.L1062
	orr	w6, w6, w13
	and	w6, w6, 65535
.L1062:
	add	w9, w9, 1
	cmp	w9, 16
	bne	.L1063
	strh	w6, [x10, x7, lsl 1]
	add	x7, x7, 1
	cmp	w24, w7
	bgt	.L1064
	add	x1, x1, :lo12:.LANCHOR4
	mov	x6, 0
	mov	w7, 0
	ldr	x1, [x1, 1608]
.L1067:
	ldr	w9, [x1, x6]
	add	x6, x6, 4
	cmp	w9, 0
	cinc	w7, w7, eq
	cmp	x6, 32
	bne	.L1067
	cmp	w7, 7
	ble	.L1068
	mov	w3, 1024
	mov	w2, 1
	adrp	x0, .LC85
	add	x0, x0, :lo12:.LC85
	bl	rknand_print_hex
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L1069:
	b	.L1069
.L1041:
	cmp	w20, 7
	bne	.L1047
	mov	w6, 0
.L1049:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L1048
	ldrb	w7, [x0, 1]
	cmp	w7, 10
	beq	.L1048
	add	w6, w6, 1
	add	x0, x0, 4
	and	w6, w6, 255
	cmp	w6, 8
	bne	.L1049
.L1050:
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L1051:
	b	.L1051
.L1048:
	cmp	w6, 6
	bls	.L1045
	b	.L1050
.L1047:
	cmp	w20, 6
	bne	.L1045
	mov	x6, 0
.L1052:
	ldrb	w7, [x0, x6]
	cmp	w7, 12
	beq	.L1045
	add	x7, x0, x6
	ldrb	w7, [x7, 8]
	cmp	w7, 4
	beq	.L1045
	add	x6, x6, 1
	cmp	x6, 8
	bne	.L1052
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L1054:
	b	.L1054
.L1056:
	ldr	w6, [x19, 2048]
	strb	w6, [x8, x0]
	add	x0, x0, 1
	b	.L1055
.L1068:
	cmp	w20, 6
	beq	.L1082
	cmp	w20, 7
	beq	.L1083
	cmp	w20, 8
	mov	w1, 8
	mov	w6, 5
	csel	w6, w6, w1, eq
.L1070:
	mov	w7, 0
.L1071:
	mov	x1, 0
.L1072:
	add	w9, w0, w1
	ldrb	w10, [x8, x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w10, [x4, w9, sxtw]
	bhi	.L1072
	ldr	x1, [x29, 112]
	add	w7, w7, 1
	add	w0, w0, w6
	cmp	w27, w7
	add	x8, x8, x1
	bgt	.L1071
	mov	w0, 255
	str	w0, [x19, 2056]
	mov	w0, w25
	bl	NandcWaitFlashReady
	cmp	w26, 1
	bhi	.L1074
	mov	w0, 54
	str	w0, [x19, 2056]
	adrp	x0, .LANCHOR0+1276
	mov	w1, -1
	ldrb	w0, [x0, #:lo12:.LANCHOR0+1276]
	str	w0, [x19, 2052]
	str	wzr, [x19, 2048]
	mov	w0, 22
	str	w0, [x19, 2056]
	mov	w0, w23
	bl	FlashReadCmd
.L1075:
	add	w23, w23, 1
	mov	w0, w25
	and	w23, w23, 255
	bl	NandcWaitFlashReady
	b	.L1030
.L1082:
	mov	w6, 4
	b	.L1070
.L1083:
	mov	w6, 10
	b	.L1070
.L1074:
	cmp	w20, 8
	bne	.L1076
	mov	w0, 190
.L1126:
	str	w0, [x19, 2056]
	b	.L1075
.L1076:
	mov	w0, 56
	b	.L1126
.L1033:
	mov	w0, 120
	str	w0, [x19, 2056]
	str	wzr, [x19, 2052]
	mov	w0, 23
	str	wzr, [x19, 2052]
	mov	w1, 25
	str	wzr, [x19, 2052]
	add	x4, x22, 1300
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
	str	w0, [x19, 2052]
	str	wzr, [x19, 2052]
	b	.L1078
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	cbz	w0, .L1140
	stp	x29, x30, [sp, -16]!
	sub	w2, w0, #1
	mov	w1, w0
	cmp	w2, 7
	add	x29, sp, 0
	bhi	.L1129
	bl	HynixGetReadRetryDefault
.L1127:
	ldp	x29, x30, [sp], 16
	ret
.L1129:
	cmp	w0, 49
	bne	.L1130
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w2, 64
	strb	w1, [x0, 1272]
	mov	w1, 4
	strb	w1, [x0, 1273]
	mov	w1, 15
	strb	w1, [x0, 1274]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 408
.L1143:
	add	x0, x0, 1276
	bl	ftl_memcpy
	b	.L1127
.L1130:
	sub	w0, w0, #65
	cmp	w1, 33
	ccmp	w0, 1, 0, ne
	bhi	.L1131
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0, 1272]
	mov	w1, 4
.L1144:
	strb	w1, [x0, 1273]
	mov	w1, 7
	strb	w1, [x0, 1274]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 45
	add	x1, x1, 352
	b	.L1143
.L1131:
	cmp	w1, 34
	mov	w0, 67
	ccmp	w1, w0, 4, ne
	bne	.L1132
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0, 1272]
	mov	w1, 5
	b	.L1144
.L1132:
	cmp	w1, 35
	mov	w0, 68
	ccmp	w1, w0, 4, ne
	bne	.L1127
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w2, 95
	strb	w1, [x0, 1272]
	mov	w1, 5
	strb	w1, [x0, 1273]
	mov	w1, 17
	strb	w1, [x0, 1274]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 256
	b	.L1143
.L1140:
	ret
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	NandcXferComp
	.type	NandcXferComp, %function
NandcXferComp:
	stp	x29, x30, [sp, -80]!
	ubfiz	x0, x0, 4, 8
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x1, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	ldr	x19, [x1, x0]
	ldr	w0, [x1, 2388]
	cmp	w0, 3
	bls	.L1176
	ldr	w0, [x19, 16]
	tbz	x0, 2, .L1176
	ldr	w0, [x19, 16]
	tbz	x0, 1, .L1147
	adrp	x22, .LC86
	adrp	x23, .LC87
	ldr	w0, [x19, 8]
	mov	x24, x1
	add	x22, x22, :lo12:.LC86
	add	x23, x23, :lo12:.LC87
	mov	w21, 0
	str	w0, [x29, 64]
.L1148:
	ldr	w1, [x19, 28]
	ldr	w0, [x29, 64]
	ubfx	x1, x1, 16, 5
	ubfx	x0, x0, 22, 6
	cmp	w1, w0
	bge	.L1156
	ldr	w0, [x24, 2388]
	cmp	w0, 5
	bhi	.L1149
.L1152:
	add	w21, w21, 1
	tst	x21, 16777215
	bne	.L1151
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
.L1151:
	mov	x1, 5
	mov	x0, 1
	bl	usleep_range
	b	.L1148
.L1149:
	ldr	w0, [x19]
	str	w0, [x29, 72]
	ldr	w0, [x29, 72]
	tbz	x0, 13, .L1152
	ldr	w0, [x29, 72]
	tbz	x0, 17, .L1152
.L1156:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 2408
	ldr	w0, [x19, 40]
	cbz	w0, .L1157
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
.L1157:
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x20, 2448]
.L1145:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L1147:
	adrp	x22, .LC88
	adrp	x23, .LC87
	ldr	w0, [x19, 8]
	add	x22, x22, :lo12:.LC88
	add	x23, x23, :lo12:.LC87
	mov	w21, 0
	str	w0, [x29, 64]
.L1158:
	ldr	w0, [x29, 64]
	tbz	x0, 20, .L1160
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0, 2456]
	cbz	w0, .L1161
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L1161:
	add	x21, x20, :lo12:.LANCHOR0
	add	x21, x21, 2408
	ldr	w0, [x21, 40]
	cbz	w0, .L1162
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
.L1162:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0, 2456]
	cbz	w0, .L1157
	mov	x0, x19
	bl	NandcSendDumpDataDone
	b	.L1157
.L1160:
	ldr	w0, [x19, 8]
	add	w21, w21, 1
	str	w0, [x29, 64]
	tst	x21, 16777215
	bne	.L1159
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
.L1159:
	mov	x1, 5
	mov	x0, 1
	bl	usleep_range
	b	.L1158
.L1176:
	ldr	w0, [x19, 8]
	str	w0, [x29, 64]
	ldr	w0, [x29, 64]
	tbz	x0, 20, .L1176
	b	.L1145
	.size	NandcXferComp, .-NandcXferComp
	.align	2
	.global	NandcXferData
	.type	NandcXferData, %function
NandcXferData:
	stp	x29, x30, [sp, -192]!
	tst	x3, 63
	add	x29, sp, 0
	stp	x25, x26, [sp, 64]
	and	w25, w0, 255
	stp	x21, x22, [sp, 32]
	sbfiz	x0, x25, 4, 32
	stp	x19, x20, [sp, 16]
	and	w21, w1, 255
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	x26, x3
	stp	x27, x28, [sp, 80]
	and	w24, w2, 255
	mov	x22, x4
	ldr	x20, [x1, x0]
	bne	.L1186
	cbnz	x4, .L1187
	add	x22, x29, 128
	mov	w2, 64
	mov	w1, 255
	add	x0, x29, 128
	bl	ftl_memset
.L1187:
	mov	x5, x22
	mov	x4, x26
	mov	w2, w24
	mov	w3, 0
	mov	w1, w21
	mov	w0, w25
	bl	NandcXferStart
	mov	w1, w21
	mov	w0, w25
	bl	NandcXferComp
	cbnz	w21, .L1211
	add	x1, x19, :lo12:.LANCHOR0
	ubfx	x2, x24, 1, 7
	add	x2, x22, x2, lsl 2
	mov	w3, 64
	ldr	w0, [x1, 2460]
	cmp	w0, 25
	mov	w0, 128
	csel	w3, w3, w0, cc
	mov	w0, 0
.L1190:
	add	w4, w3, w0
	cmp	x22, x2
	bne	.L1191
	add	x0, x19, :lo12:.LANCHOR0
	lsr	w24, w24, 2
	mov	w2, 0
	mov	w23, 0
	ldr	w3, [x0, 2388]
	ldr	w4, [x0, 2460]
.L1192:
	cmp	w2, w24
	bcs	.L1188
	cbnz	w4, .L1198
.L1188:
	str	wzr, [x20, 16]
.L1199:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19, 2388]
	cmp	w0, 5
	bls	.L1185
	cbnz	w21, .L1185
	ldr	w0, [x20]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L1185
	orr	w0, w0, 131072
	mov	w23, -1
	str	w0, [x20]
.L1185:
	mov	w0, w23
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 192
	ret
.L1191:
	ldr	x5, [x1, 2416]
	and	x0, x0, 4294967292
	add	x22, x22, 4
	ldr	w0, [x5, x0]
	strb	w0, [x22, -4]
	lsr	w5, w0, 8
	strb	w5, [x22, -3]
	lsr	w5, w0, 16
	strb	w5, [x22, -2]
	lsr	w0, w0, 24
	strb	w0, [x22, -1]
	mov	w0, w4
	b	.L1190
.L1198:
	uxtw	x0, w2
	add	x0, x0, 8
	ldr	w0, [x20, x0, lsl 2]
	str	w0, [x29, 120]
	ldr	w0, [x29, 120]
	tbnz	x0, 2, .L1214
	ldr	w0, [x29, 120]
	tbnz	x0, 15, .L1214
	cmp	w3, 5
	bls	.L1194
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
	bls	.L1195
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 27, 1
.L1220:
	orr	w0, w1, w0, lsl 5
.L1196:
	cmp	w23, w0
	csel	w23, w23, w0, cs
.L1193:
	add	w2, w2, 1
	b	.L1192
.L1195:
	ubfx	x1, x0, 16, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 29, 1
	b	.L1220
.L1194:
	cmp	w3, 3
	bls	.L1215
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
	bls	.L1197
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 28, 1
	b	.L1220
.L1197:
	ubfx	x1, x0, 16, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 30, 1
	b	.L1220
.L1215:
	mov	w0, 0
	b	.L1196
.L1214:
	mov	w23, -1
	b	.L1193
.L1211:
	mov	w23, 0
	b	.L1188
.L1186:
	cmp	w21, 1
	bne	.L1200
	cmp	x4, 0
	mov	w27, 2
	csel	w27, w27, wzr, ne
	mov	w28, 0
	lsl	w27, w27, 1
	mov	w23, 0
.L1201:
	cmp	w23, w24
	bcc	.L1203
	mov	w23, 0
	b	.L1199
.L1203:
	and	w6, w23, 3
	cbz	x26, .L1216
	lsl	w3, w23, 9
	add	x3, x26, x3
.L1202:
	add	x4, x22, w28, uxtw
	mov	w2, w6
	mov	x0, x20
	str	w6, [x29, 108]
	mov	w1, 1
	bl	NandcCopy1KB
	ldr	w6, [x29, 108]
	mov	x5, 0
	mov	x4, 0
	mov	w2, 2
	mov	w3, w6
	mov	w0, w25
	mov	w1, 1
	add	w23, w23, 2
	bl	NandcXferStart
	add	w28, w28, w27
	mov	w1, 1
	mov	w0, w25
	bl	NandcXferComp
	b	.L1201
.L1216:
	mov	x3, 0
	b	.L1202
.L1200:
	mov	w0, w25
	mov	x5, 0
	mov	x4, 0
	mov	w3, 0
	mov	w2, 2
	mov	w1, 0
	bl	NandcXferStart
	mov	w27, 2
	cmp	x22, 0
	mov	w28, 0
	csel	w27, w27, wzr, ne
	mov	w23, 0
	lsl	w0, w27, 1
	mov	w27, 0
	str	w0, [x29, 108]
.L1204:
	cmp	w24, w27
	bls	.L1199
	mov	w0, w25
	mov	w1, w21
	bl	NandcXferComp
	ldr	w0, [x20, 32]
	add	w7, w27, 2
	str	w0, [x29, 120]
	cmp	w24, w7
	bls	.L1205
	and	w3, w7, 3
	str	w7, [x29, 104]
	mov	x5, 0
	mov	x4, 0
	mov	w2, 2
	mov	w1, 0
	mov	w0, w25
	bl	NandcXferStart
	ldr	w7, [x29, 104]
.L1205:
	ldr	w0, [x29, 120]
	tbnz	x0, 2, .L1217
	ldr	w0, [x29, 120]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29, 120]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w23, w0
	csel	w23, w23, w0, cs
.L1206:
	and	w2, w27, 3
	cbz	x26, .L1218
	lsl	w3, w27, 9
	add	x3, x26, x3
.L1207:
	add	x4, x22, w28, uxtw
	mov	x0, x20
	str	w7, [x29, 104]
	mov	w1, 0
	bl	NandcCopy1KB
	ldp	w7, w0, [x29, 104]
	add	w28, w28, w0
	mov	w27, w7
	b	.L1204
.L1217:
	mov	w23, -1
	b	.L1206
.L1218:
	mov	x3, 0
	b	.L1207
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
	bne	.L1222
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0, 73]
	ldr	w0, [x0, 76]
	mul	w0, w5, w0
	cmp	w0, w21
	mov	w0, 4
	csel	w20, w20, w0, ls
.L1222:
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
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	and	w23, w0, 255
	stp	x21, x22, [sp, 32]
	mov	w25, w1
	stp	x27, x28, [sp, 80]
	mov	w22, 6
	mov	x24, x2
	mov	x28, x3
	ldr	x0, [x19, 136]
	mov	w26, w4
	ldr	w0, [x0, 304]
	str	w0, [x29, 108]
	ldr	w0, [x19, 2388]
	cmp	w0, 8
	mov	w0, 12
	csel	w22, w22, w0, cc
	cbz	w4, .L1238
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	mov	w0, w23
	bl	FlashReset
	mov	x3, x28
	mov	x2, x24
	mov	w1, w25
	mov	w0, w23
	bl	FlashReadRawPage
	mov	w20, w0
	ldrb	w0, [x19, 2357]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19, 2357]
	bl	NandcSetMode
	cmn	w20, #1
	bne	.L1227
.L1236:
	mov	w20, -1
.L1224:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L1227:
	mov	w2, w20
	mov	w1, w25
	adrp	x0, .LC89
	add	x0, x0, :lo12:.LC89
	bl	printk
	cmp	w20, 9
	bhi	.L1229
	sbfiz	x0, x23, 4, 32
	ldr	x0, [x19, x0]
	ldr	w1, [x0, 3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L1229:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0, 1616]
	add	w1, w1, 1
	str	w1, [x0, 1616]
	cmp	w1, 2047
	bls	.L1224
	mov	x28, 0
	mov	x24, 0
	str	wzr, [x0, 1616]
.L1226:
	mov	w5, 0
	mov	w21, 0
	mov	w6, 0
	mov	w19, 0
	mov	w27, -1
.L1234:
	stp	w5, w6, [x29, 100]
	mov	w0, w22
	bl	NandcSetDdrPara
	mov	w1, w25
	mov	x3, x28
	mov	x2, x24
	mov	w0, w23
	bl	FlashReadRawPage
	add	w1, w20, 1
	cmp	w0, w1
	ldp	w5, w6, [x29, 100]
	bhi	.L1230
	cmp	w0, 2
	bhi	.L1240
	add	w19, w19, 1
	cmp	w19, 9
	bls	.L1240
	mov	w1, w21
	mov	w20, w0
	sub	w21, w22, w19
	mov	w27, 0
.L1232:
	cmp	w19, w6
	csel	w21, w21, w1, hi
.L1233:
	cbz	w21, .L1235
	mov	w1, w21
	adrp	x0, .LC90
	add	x0, x0, :lo12:.LC90
	bl	printk
	mov	w0, w21
	bl	NandcSetDdrPara
.L1235:
	cbz	w27, .L1224
	adrp	x0, .LC91
	mov	w2, w25
	mov	w1, w23
	add	x0, x0, :lo12:.LC91
	bl	printk
	cbz	w26, .L1236
	ldr	w1, [x29, 108]
	lsr	w0, w1, 8
	bl	NandcSetDdrPara
	b	.L1224
.L1238:
	mov	w20, 1024
	b	.L1226
.L1230:
	cmp	w19, w6
	bls	.L1241
	sub	w21, w5, w19
	cmp	w19, 7
	bhi	.L1233
	mov	w6, w19
.L1241:
	mov	w19, 0
	b	.L1231
.L1240:
	mov	w5, w22
	mov	w20, w0
	mov	w27, 0
	mov	x28, 0
	mov	x24, 0
.L1231:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L1234
	mov	w1, w21
	mov	w21, w5
	b	.L1232
	.size	FlashDdrTunningRead, .-FlashDdrTunningRead
	.align	2
	.global	FlashReadPage
	.type	FlashReadPage, %function
FlashReadPage:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 255
	stp	x23, x24, [sp, 48]
	mov	w0, w20
	stp	x21, x22, [sp, 32]
	mov	x23, x2
	stp	x25, x26, [sp, 64]
	mov	w22, w1
	mov	x24, x3
	bl	FlashReadRawPage
	mov	w19, w0
	cmn	w0, #1
	bne	.L1251
	adrp	x21, .LANCHOR0
	add	x25, x21, :lo12:.LANCHOR0
	ldrb	w26, [x25, 80]
	cbnz	w26, .L1252
.L1254:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21, 2380]
	cbz	w0, .L1251
	ldr	x0, [x21, 136]
	mov	w4, 1
	mov	x3, x24
	mov	x2, x23
	mov	w1, w22
	ldr	w25, [x0, 304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	mov	w19, w0
	cmn	w0, #1
	beq	.L1255
	ldrb	w0, [x21, 2464]
	cmp	w19, w0, lsr 1
	bls	.L1251
.L1255:
	lsr	w0, w25, 8
	bl	NandcSetDdrPara
	b	.L1251
.L1252:
	strb	wzr, [x25, 80]
	mov	x3, x24
	mov	x2, x23
	mov	w1, w22
	mov	w0, w20
	bl	FlashReadRawPage
	strb	w26, [x25, 80]
	cmn	w0, #1
	beq	.L1254
	mov	w19, w0
.L1251:
	adrp	x21, .LANCHOR4
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x4, [x21, 1624]
	cbz	x4, .L1250
	cmn	w19, #1
	bne	.L1250
	mov	x3, x24
	mov	x2, x23
	mov	w1, w22
	mov	w0, w20
	blr	x4
	mov	w19, w0
	mov	w1, w0
	mov	w3, w22
	mov	w2, w20
	adrp	x0, .LC92
	add	x0, x0, :lo12:.LC92
	bl	printk
	cmn	w19, #1
	bne	.L1250
	adrp	x0, .LANCHOR0+204
	ldrb	w0, [x0, #:lo12:.LANCHOR0+204]
	cbz	w0, .L1250
	mov	w0, w20
	bl	flash_enter_slc_mode
	ldr	x4, [x21, 1624]
	mov	x3, x24
	mov	x2, x23
	mov	w1, w22
	mov	w0, w20
	blr	x4
	mov	w19, w0
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1250:
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
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x22, x19, :lo12:.LANCHOR0
	and	w20, w0, 255
	mov	w21, w1
	ldrb	w0, [x22, 2357]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22, 2357]
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
	beq	.L1271
	cmn	w22, #1
	bne	.L1272
.L1271:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20, 2357]
	tbz	x0, 0, .L1272
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20, 2380]
.L1273:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L1272:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19, 2380]
	b	.L1273
	.size	FlashDdrParaScan, .-FlashDdrParaScan
	.align	2
	.global	FlashLoadPhyInfo
	.type	FlashLoadPhyInfo, %function
FlashLoadPhyInfo:
	stp	x29, x30, [sp, -128]!
	mov	w0, 60
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR4
	strb	w0, [x29, 120]
	mov	w0, 40
	strb	w0, [x29, 121]
	mov	w0, 24
	strb	w0, [x29, 122]
	mov	w0, 16
	strb	w0, [x29, 123]
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	adrp	x22, .LANCHOR1
	stp	x27, x28, [sp, 80]
	add	x22, x22, :lo12:.LANCHOR1
	stp	x25, x26, [sp, 64]
	mov	w27, 20036
	mov	w19, 0
	mov	w24, 4
	ldr	w0, [x0, 76]
	mov	w23, -1
	str	w0, [x29, 108]
	add	x0, x21, :lo12:.LANCHOR4
	movk	w27, 0x4e41, lsl 16
	add	x22, x22, 472
	ldr	x1, [x0, 1608]
	str	x1, [x0, 1632]
	str	wzr, [x0, 1640]
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1282:
	add	w28, w19, 1
	add	x25, x21, :lo12:.LANCHOR4
	mov	x26, 0
.L1284:
	add	x0, x29, 120
	ldrb	w0, [x0, x26]
	bl	FlashBchSel
	ldr	x2, [x25, 1608]
	mov	x3, 0
	mov	w1, w19
	mov	w0, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1283
	ldr	x2, [x25, 1608]
	mov	x3, 0
	mov	w1, w28
	mov	w0, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1283
	add	x26, x26, 1
	cmp	x26, 4
	bne	.L1284
.L1285:
	ldr	w0, [x29, 108]
	subs	w24, w24, #1
	add	w19, w19, w0
	bne	.L1282
	b	.L1290
.L1286:
	mov	w1, 2036
	add	x0, x6, 12
	bl	js_hash
	ldr	w1, [x6, 8]
	cmp	w1, w0
	bne	.L1292
	add	x1, x6, 160
	mov	w2, 32
	mov	x0, x22
	bl	ftl_memcpy
	ldr	x1, [x25, 1632]
	add	x23, x20, :lo12:.LANCHOR0
	mov	w2, 32
	add	x0, x23, 96
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x25, 1632]
	mov	w2, 852
	add	x0, x23, 1272
	add	x1, x1, 224
	bl	ftl_memcpy
	ldrh	w0, [x22, 10]
	bl	FlashBlockAlignInit
	str	w19, [x25, 1640]
	ldr	x1, [x25, 1632]
	ldr	w0, [x1, 1076]
	strb	w0, [x23, 2380]
	ldr	w0, [x23, 76]
	udiv	w0, w19, w0
	add	w0, w0, 1
	cmp	w0, 1
	bls	.L1288
.L1295:
	str	w0, [x25, 1644]
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w1, [x1, 14]
	mov	w23, 0
	strb	w1, [x0, 1648]
	b	.L1285
.L1288:
	mov	w0, 2
	b	.L1295
.L1292:
	mov	w23, -1
	b	.L1285
.L1283:
	add	x25, x21, :lo12:.LANCHOR4
	ldr	x6, [x25, 1632]
	ldr	w0, [x6]
	cmp	w0, w27
	bne	.L1285
	cbnz	w23, .L1286
	add	x20, x20, :lo12:.LANCHOR0
	ldr	w0, [x20, 76]
	udiv	w19, w19, w0
	add	w19, w19, 1
	str	w19, [x25, 1644]
.L1290:
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
	and	w22, w0, 255
	stp	x19, x20, [sp, 16]
	mov	w0, w22
	stp	x27, x28, [sp, 80]
	mov	x27, x3
	stp	x23, x24, [sp, 48]
	adrp	x19, .LANCHOR0
	str	x2, [x29, 112]
	str	w1, [x29, 120]
	stp	x25, x26, [sp, 64]
	bl	NandcWaitFlashReady
	add	x1, x19, :lo12:.LANCHOR0
	sbfiz	x0, x22, 4, 32
	add	x2, x1, x0
	ldr	x23, [x1, x0]
	ldrb	w0, [x1, 128]
	ldrb	w28, [x2, 8]
	sub	w0, w0, #67
	str	w28, [x29, 124]
	add	x20, x28, 8
	and	w0, w0, 255
	add	x20, x23, x20, lsl 8
	cmp	w0, 1
	bls	.L1313
	ldrb	w0, [x1, 2380]
	cbz	w0, .L1314
	mov	w24, 1
	mov	w0, 0
	bl	NandcSetDdrMode
.L1298:
	add	x0, x23, x28, lsl 8
	mov	w1, 92
	str	w1, [x0, 2056]
	mov	w1, 197
	str	w1, [x0, 2056]
.L1297:
	ldrsw	x0, [x29, 124]
	mov	w21, 1
	mov	w25, -1
	add	x0, x0, 8
	add	x0, x23, x0, lsl 8
	str	x0, [x29, 104]
.L1299:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0, 1649]
	add	w0, w0, 1
	cmp	w21, w0
	bcc	.L1308
	mov	w26, w25
.L1307:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0, 128]
	sub	w0, w0, #67
	and	w0, w0, 255
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1309
	bl	SandiskSetRRPara
.L1310:
	ldrsw	x0, [x29, 124]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x23, x23, x0, lsl 8
	mov	w0, 255
	str	w0, [x23, 8]
	ldrb	w0, [x19, 2464]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L1311
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L1311:
	mov	w0, w22
	bl	NandcWaitFlashReady
	cbz	w24, .L1296
	mov	w0, 4
	bl	NandcSetDdrMode
.L1296:
	mov	w0, w26
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1314:
	mov	w24, 0
	b	.L1298
.L1313:
	mov	w24, 0
	b	.L1297
.L1308:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w21
	ldrb	w0, [x0, 128]
	sub	w0, w0, #67
	and	w0, w0, 255
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1300
	bl	SandiskSetRRPara
.L1301:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0, 128]
	cmp	w0, 34
	bne	.L1302
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0, 1649]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L1302
	ldr	x1, [x29, 104]
	mov	w0, 179
	str	w0, [x1, 8]
.L1302:
	add	x0, x23, x28, lsl 8
	mov	w1, 38
	str	w1, [x0, 2056]
	mov	w1, 93
	str	w1, [x0, 2056]
	cbz	w24, .L1303
	mov	w0, 4
	bl	NandcSetDdrMode
	ldr	w1, [x29, 120]
	mov	x3, x27
	ldr	x2, [x29, 112]
	mov	w0, w22
	bl	FlashReadRawPage
	mov	w26, w0
	mov	w0, 0
	bl	NandcSetDdrMode
.L1304:
	cmn	w26, #1
	beq	.L1305
	add	x0, x19, :lo12:.LANCHOR0
	cmn	w25, #1
	csel	w25, w25, w26, ne
	ldrb	w0, [x0, 2464]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L1307
	mov	x27, 0
	str	xzr, [x29, 112]
.L1305:
	add	w21, w21, 1
	b	.L1299
.L1300:
	bl	ToshibaSetRRPara
	b	.L1301
.L1303:
	ldr	w1, [x29, 120]
	mov	x3, x27
	ldr	x2, [x29, 112]
	mov	w0, w22
	bl	FlashReadRawPage
	mov	w26, w0
	b	.L1304
.L1309:
	bl	ToshibaSetRRPara
	b	.L1310
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
	mov	x23, x2
	stp	x25, x26, [sp, 64]
	mov	x24, x3
	stp	x27, x28, [sp, 80]
	mov	w25, w1
	bl	NandcWaitFlashReady
	adrp	x20, .LANCHOR0
	sbfiz	x1, x22, 4, 32
	add	x0, x20, :lo12:.LANCHOR0
	add	x2, x0, x1
	adrp	x28, .LANCHOR4
	mov	x26, x0
	add	x28, x28, :lo12:.LANCHOR4
	ldr	x1, [x0, x1]
	mov	w27, 1
	ldrb	w21, [x2, 8]
	mov	w19, -1
	add	x21, x21, 8
	add	x21, x1, x21, lsl 8
.L1325:
	ldrb	w0, [x28, 1649]
	add	w0, w0, 1
	cmp	w27, w0
	bcc	.L1329
.L1328:
	add	x20, x20, :lo12:.LANCHOR0
	mov	x0, x21
	mov	w1, 0
	bl	SamsungSetRRPara
	ldrb	w0, [x20, 2464]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1324
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1324:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L1329:
	mov	w1, w27
	mov	x0, x21
	bl	SamsungSetRRPara
	mov	x3, x24
	mov	x2, x23
	mov	w1, w25
	mov	w0, w22
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1326
	ldrb	w1, [x26, 2464]
	cmn	w19, #1
	csel	w19, w19, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1331
	mov	x24, 0
	mov	x23, 0
.L1326:
	add	w27, w27, 1
	b	.L1325
.L1331:
	mov	w19, w0
	b	.L1328
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	2
	.global	MicronReadRetrial
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 255
	adrp	x0, .LANCHOR0
	stp	x23, x24, [sp, 48]
	str	w1, [x29, 156]
	add	x1, x0, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	x23, x2
	stp	x25, x26, [sp, 64]
	mov	x24, x3
	stp	x27, x28, [sp, 80]
	ldrb	w19, [x1, 2464]
	ldrb	w1, [x1, 204]
	cbnz	w1, .L1339
	add	w19, w19, w19, lsl 1
	asr	w19, w19, 2
.L1340:
	add	x0, x0, :lo12:.LANCHOR0
	mov	w22, 0
	str	x0, [x29, 144]
	mov	w27, 239
	add	x0, x0, w20, sxtw 4
	mov	w28, 137
	str	x0, [x29, 136]
.L1350:
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	x0, [x29, 136]
	adrp	x7, .LANCHOR4
	add	x7, x7, :lo12:.LANCHOR4
	mov	w21, 0
	mov	w25, -1
	ldrb	w4, [x0, 8]
	ldr	x8, [x0]
	add	x26, x8, x4, lsl 8
.L1341:
	ldrb	w0, [x7, 1649]
	cmp	w21, w0
	bcc	.L1345
.L1344:
	add	x4, x8, x4, lsl 8
	mov	x0, 1000
	str	x4, [x29, 128]
	str	w27, [x4, 2056]
	str	w28, [x4, 2052]
	bl	__const_udelay
	ldr	x4, [x29, 128]
	cmp	w25, w19
	str	wzr, [x4, 2048]
	str	wzr, [x4, 2048]
	str	wzr, [x4, 2048]
	str	wzr, [x4, 2048]
	bcc	.L1346
	cmn	w25, #1
	mov	w0, 256
	csel	w25, w25, w0, eq
.L1346:
	cmp	w25, 256
	ccmn	w25, #1, 4, ne
	bne	.L1347
	ldr	w2, [x29, 156]
	adrp	x0, .LC93
	mov	w4, w25
	mov	w3, w21
	mov	w1, w21
	add	x0, x0, :lo12:.LC93
	bl	printk
	cbnz	w22, .L1348
	ldr	x0, [x29, 144]
	ldrb	w0, [x0, 204]
	cbz	w0, .L1338
	cmn	w25, #1
	bne	.L1338
	mov	w1, 3
	mov	w0, w20
	mov	w22, 1
	bl	micron_auto_read_calibration_config
	b	.L1350
.L1339:
	mov	w1, 3
	sdiv	w19, w19, w1
	b	.L1340
.L1345:
	str	w27, [x26, 2056]
	mov	x0, 1000
	str	w28, [x26, 2052]
	stp	x4, x7, [x29, 104]
	str	x8, [x29, 120]
	bl	__const_udelay
	add	w9, w21, 1
	str	w9, [x26, 2048]
	str	wzr, [x26, 2048]
	mov	x3, x24
	ldr	w1, [x29, 156]
	mov	x2, x23
	str	wzr, [x26, 2048]
	mov	w0, w20
	str	wzr, [x26, 2048]
	str	w9, [x29, 128]
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	w9, [x29, 128]
	ldp	x4, x7, [x29, 104]
	ldr	x8, [x29, 120]
	beq	.L1342
	cmn	w25, #1
	csel	w25, w25, w0, ne
	cmp	w0, w19
	bcc	.L1352
	mov	x24, 0
	mov	x23, 0
.L1342:
	mov	w21, w9
	b	.L1341
.L1352:
	mov	w25, w0
	mov	x24, 0
	mov	x23, 0
	b	.L1344
.L1348:
	mov	w0, w20
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w25, #1
	mov	w0, 256
	csel	w25, w25, w0, eq
.L1338:
	mov	w0, w25
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1347:
	cbz	w22, .L1338
	mov	w1, 0
	mov	w0, w20
	mov	w25, 256
	bl	micron_auto_read_calibration_config
	b	.L1338
	.size	MicronReadRetrial, .-MicronReadRetrial
	.align	2
	.global	HynixReadRetrial
	.type	HynixReadRetrial, %function
HynixReadRetrial:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x27, x28, [sp, 80]
	and	x27, x0, 255
	add	x0, x21, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	stp	x23, x24, [sp, 48]
	mov	x24, x2
	stp	x19, x20, [sp, 16]
	add	x2, x0, 1272
	mov	w26, w1
	add	x1, x2, x27
	ldrb	w23, [x0, 1274]
	mov	x22, x27
	ldr	x0, [x0, 88]
	mov	x25, x3
	ldrb	w19, [x1, 12]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #7
	and	w0, w0, 255
	cmp	w0, 1
	bhi	.L1364
	ldrb	w19, [x1, 20]
.L1364:
	mov	w0, w22
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	mov	w28, 0
	add	x5, x4, 1276
	mov	w20, -1
.L1365:
	cmp	w28, w23
	bcc	.L1370
.L1369:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0, 88]
	add	x0, x0, 1272
	add	x0, x0, x27
	ldrb	w1, [x1, 19]
	sub	w1, w1, #7
	and	w1, w1, 255
	cmp	w1, 1
	bhi	.L1371
	strb	w19, [x0, 20]
.L1372:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21, 2464]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1363
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1363:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L1370:
	add	w19, w19, 1
	ldrb	w1, [x4, 1273]
	and	w19, w19, 255
	mov	x2, x5
	cmp	w23, w19
	stp	x5, x4, [x29, 96]
	csel	w19, w19, wzr, hi
	mov	w0, w22
	mov	w3, w19
	bl	HynixSetRRPara
	mov	x3, x25
	mov	x2, x24
	mov	w1, w26
	mov	w0, w22
	bl	FlashReadRawPage
	cmn	w0, #1
	ldp	x5, x4, [x29, 96]
	beq	.L1367
	ldrb	w1, [x4, 2464]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1374
	mov	x25, 0
	mov	x24, 0
.L1367:
	add	w28, w28, 1
	b	.L1365
.L1374:
	mov	w20, w0
	b	.L1369
.L1371:
	strb	w19, [x0, 12]
	b	.L1372
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
	adrp	x22, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR0
	sbfiz	x1, x23, 4, 32
	add	x3, x0, x1
	adrp	x21, .LANCHOR3
	ldrb	w2, [x0, 2356]
	ldr	x1, [x0, x1]
	ldrb	w19, [x3, 8]
	cbnz	w2, .L1382
	add	x21, x21, :lo12:.LANCHOR3
	add	x19, x1, x19, lsl 8
	add	x21, x21, 16
	mov	x27, x0
	mov	x4, 0
	mov	w20, -1
	mov	w6, 239
	mov	w5, 141
.L1386:
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
	beq	.L1383
	ldrb	w1, [x27, 2464]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1394
	mov	x25, 0
	mov	x24, 0
.L1383:
	add	x4, x4, 1
	cmp	x4, 25
	bne	.L1386
	mov	w28, 26
.L1385:
	mov	w0, 239
	str	w0, [x19, 2056]
	mov	w0, 141
.L1410:
	str	w0, [x19, 2052]
	add	x22, x22, :lo12:.LANCHOR0
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	ldrb	w0, [x22, 2464]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1392
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1392:
	cmp	w20, 256
	ccmn	w20, #1, 4, ne
	bne	.L1393
	adrp	x0, .LC94
	mov	w4, w20
	mov	w3, w28
	mov	w2, w26
	mov	w1, w28
	add	x0, x0, :lo12:.LC94
	bl	printk
.L1393:
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
.L1394:
	mov	w20, w0
	b	.L1385
.L1382:
	add	x21, x21, :lo12:.LANCHOR3
	add	x19, x1, x19, lsl 8
	add	x21, x21, 52
	mov	x27, x0
	mov	w20, -1
	mov	w28, 1
	mov	w5, 239
	mov	w4, 137
.L1391:
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
	beq	.L1388
	ldrb	w1, [x27, 2464]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1395
	mov	x25, 0
	mov	x24, 0
.L1388:
	add	w28, w28, 1
	add	x21, x21, 4
	cmp	w28, 26
	bne	.L1391
.L1390:
	mov	w0, 239
	str	w0, [x19, 2056]
	mov	w0, 137
	b	.L1410
.L1395:
	mov	w20, w0
	b	.L1390
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
	bne	.L1412
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0, 73]
	ldr	w2, [x0, 76]
	mul	w1, w1, w2
	cmp	w1, w20
	bls	.L1412
	ldrb	w0, [x0, 72]
	cmp	w0, 0
	mov	w0, 4
	csel	w21, w21, w0, eq
.L1412:
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
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	mov	w23, 20036
	movk	w23, 0x4e41, lsl 16
	adrp	x19, .LANCHOR0
	ldr	x0, [x20, 1608]
	add	x19, x19, :lo12:.LANCHOR0
	str	x0, [x20, 1632]
	mov	w22, 0
	ldrb	w0, [x20, 1650]
	mov	w21, 0
	bl	FlashBchSel
	ldr	x0, [x20, 1608]
	mov	w2, 2048
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x20, 1632]
	mov	w2, 32
	str	w23, [x0]
	ldr	x0, [x20, 1632]
	ldrb	w1, [x19, 2358]
	add	x0, x0, 16
	strh	w1, [x0, -4]
	ldrb	w1, [x19, 73]
	strh	w1, [x0, -2]
	ldrb	w1, [x19, 2380]
	str	w1, [x0, 1060]
	add	x1, x19, 2132
	bl	ftl_memcpy
	ldr	x0, [x20, 1632]
	mov	w2, 8
	add	x1, x19, 2360
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x20, 1632]
	mov	w2, 32
	add	x1, x19, 1232
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x20, 1632]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 472
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x20, 1632]
	mov	w2, 32
	add	x1, x19, 96
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x20, 1632]
	mov	w2, 852
	add	x1, x19, 1272
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x6, [x20, 1632]
	mov	w1, 2036
	add	x0, x6, 12
	bl	js_hash
	str	w0, [x6, 8]
	mov	w0, 1592
	str	w0, [x6, 4]
	ldr	x0, [x20, 1656]
	str	x0, [x20, 1632]
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1421:
	ldr	w1, [x19, 76]
	mov	w2, 0
	mov	w0, 0
	mul	w1, w21, w1
	bl	FlashEraseBlock
	ldrb	w0, [x19, 204]
	cbz	w0, .L1416
	mov	w24, 0
.L1417:
	ldr	w1, [x19, 76]
	mov	x3, 0
	ldr	x2, [x20, 1608]
	mov	w0, 0
	madd	w1, w21, w1, w24
	add	w24, w24, 1
	bl	FlashProgPage
	cmp	w24, 10
	bne	.L1417
.L1418:
	ldr	w1, [x19, 76]
	mov	x3, 0
	ldr	x2, [x20, 1656]
	mov	w0, 0
	mul	w1, w21, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	add	w7, w21, 1
	beq	.L1419
	ldr	x6, [x20, 1632]
	ldr	w0, [x6]
	cmp	w0, w23
	bne	.L1419
	mov	w1, 2036
	add	x0, x6, 12
	bl	js_hash
	ldr	w1, [x6, 8]
	cmp	w1, w0
	bne	.L1419
	ldr	w0, [x19, 76]
	cmp	w22, 1
	str	w7, [x20, 1644]
	mul	w21, w0, w21
	str	w21, [x20, 1640]
	beq	.L1422
	mov	w22, 1
.L1419:
	mov	w21, w7
	cmp	w7, 4
	bne	.L1421
.L1420:
	mov	w0, 0
	bl	flash_exit_slc_mode
	cmp	w22, 0
	csetm	w0, eq
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1416:
	ldr	w1, [x19, 76]
	mov	x3, 0
	ldr	x2, [x20, 1608]
	mov	w0, 0
	mul	w1, w21, w1
	bl	FlashProgPage
	ldr	w1, [x19, 76]
	mov	x3, 0
	ldr	x2, [x20, 1608]
	mov	w0, 0
	mul	w1, w21, w1
	add	w1, w1, 1
	bl	FlashProgPage
	b	.L1418
.L1422:
	mov	w22, 2
	b	.L1420
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
	stp	x19, x20, [sp, 16]
	strb	w0, [x29, 120]
	mov	w0, 40
	strb	w0, [x29, 121]
	mov	w0, 24
	strb	w0, [x29, 122]
	mov	w0, 16
	adrp	x20, .LANCHOR0
	strb	w0, [x29, 123]
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x27, x28, [sp, 80]
	ldrb	w26, [x0, 2464]
	ldr	w0, [x0, 2376]
	cbz	w0, .L1430
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1430:
	adrp	x19, .LANCHOR4
	mov	w28, 35899
	add	x24, x20, :lo12:.LANCHOR0
	add	x27, x29, 120
	add	x19, x19, :lo12:.LANCHOR4
	mov	w23, -1
	mov	w21, 2
	movk	w28, 0xfcdc, lsl 16
	mov	w2, 2048
	mov	w1, 0
	mov	x0, x25
	bl	ftl_memset
.L1431:
	ldrb	w0, [x24, 73]
	cmp	w21, w0
	bcc	.L1436
.L1435:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, w26
	bl	FlashBchSel
	ldr	w0, [x20, 2376]
	cbz	w0, .L1429
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1429:
	mov	w0, w23
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1436:
	mov	x22, 0
.L1433:
	ldrb	w4, [x22, x27]
	str	w4, [x29, 108]
	mov	w0, w4
	bl	FlashBchSel
	ldr	w1, [x24, 76]
	mov	x3, 0
	ldr	x2, [x19, 1608]
	mov	w0, 0
	mul	w1, w21, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	w4, [x29, 108]
	bne	.L1432
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L1433
.L1434:
	add	w21, w21, 1
	b	.L1431
.L1439:
	mov	w23, 0
	b	.L1435
.L1432:
	ldr	x0, [x19, 1608]
	ldr	w0, [x0]
	cmp	w0, w28
	bne	.L1434
	mov	w1, w4
	adrp	x0, .LC95
	add	x0, x0, :lo12:.LC95
	bl	printk
	ldr	x1, [x19, 1608]
	mov	w2, 2048
	mov	x0, x25
	bl	ftl_memcpy
	ldr	x0, [x19, 1608]
	ldr	w0, [x0, 512]
	strb	w0, [x24, 73]
	ldr	w0, [x19, 1644]
	cmp	w21, w0
	bcs	.L1439
	mov	w23, 0
	str	w21, [x19, 1644]
	bl	FlashSavePhyInfo
	b	.L1434
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	2
	.global	FlashInit
	.type	FlashInit, %function
FlashInit:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR4
	add	x20, x23, :lo12:.LANCHOR4
	stp	x21, x22, [sp, 32]
	stp	x27, x28, [sp, 80]
	mov	x21, x0
	stp	x25, x26, [sp, 64]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x20, 1608]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x20, 1656]
	mov	w0, 4096
	bl	ftl_dma32_malloc
	add	x22, x19, :lo12:.LANCHOR0
	str	x0, [x20, 1664]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x20, 1672]
	mov	w0, 4096
	bl	ftl_dma32_malloc
	str	x0, [x20, 1680]
	mov	w0, 50
	strb	w0, [x20, 1648]
	adrp	x25, .LC96
	strb	w0, [x22, 73]
	mov	w0, 128
	strb	wzr, [x20, 1688]
	add	x25, x25, :lo12:.LC96
	str	wzr, [x20, 1616]
	mov	w24, 0
	str	wzr, [x20, 1644]
	mov	w28, 144
	str	w0, [x22, 76]
	mov	w0, 60
	strb	w0, [x20, 1650]
	add	x20, x22, 2132
	mov	x27, x20
	strb	wzr, [x22, 2380]
	strb	wzr, [x22, 72]
	mov	x0, x21
	bl	NandcInit
.L1454:
	ldrb	w1, [x22, 8]
	and	w26, w24, 255
	str	w1, [x29, 108]
	mov	w0, w26
	ldr	x21, [x22]
	bl	FlashReset
	mov	w0, w26
	bl	NandcFlashCs
	ldr	w1, [x29, 108]
	mov	x0, 1000
	ubfiz	x1, x1, 8, 8
	add	x21, x21, x1
	str	w28, [x21, 2056]
	str	wzr, [x21, 2052]
	bl	__const_udelay
	ldr	w0, [x21, 2048]
	and	w0, w0, 255
	strb	w0, [x20]
	cmp	w0, 44
	ldr	w1, [x21, 2048]
	strb	w1, [x20, 1]
	ldr	w1, [x21, 2048]
	strb	w1, [x20, 2]
	ldr	w1, [x21, 2048]
	strb	w1, [x20, 3]
	ldr	w1, [x21, 2048]
	strb	w1, [x20, 4]
	ldr	w1, [x21, 2048]
	strb	w1, [x20, 5]
	bne	.L1449
	mov	w0, 239
	str	w0, [x21, 2056]
	mov	w0, 1
	str	w0, [x21, 2052]
	mov	x0, 1000
	bl	__const_udelay
	mov	w0, 4
	str	w0, [x21, 2048]
	str	wzr, [x21, 2048]
	str	wzr, [x21, 2048]
	str	wzr, [x21, 2048]
.L1449:
	mov	w0, w26
	bl	NandcFlashDeCs
	ldrb	w2, [x20]
	sub	w0, w2, #1
	and	w0, w0, 255
	cmp	w0, 253
	bhi	.L1450
	ldrb	w7, [x20, 5]
	add	w1, w24, 1
	ldrb	w6, [x20, 4]
	mov	x0, x25
	ldrb	w5, [x20, 3]
	ldrb	w4, [x20, 2]
	ldrb	w3, [x20, 1]
	bl	printk
.L1450:
	cbnz	w24, .L1451
	ldrb	w0, [x27]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 253
	bhi	.L1502
	ldrb	w0, [x27, 1]
	cmp	w0, 255
	beq	.L1502
.L1451:
	ldrb	w0, [x20]
	cmp	w0, 181
	bne	.L1453
	mov	w0, 44
	strb	w0, [x20]
.L1453:
	add	w24, w24, 1
	add	x22, x22, 16
	add	x20, x20, 8
	cmp	w24, 4
	bne	.L1454
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0, 2132]
	cmp	w1, 173
	beq	.L1455
	ldr	w0, [x0, 2384]
	bl	NandcSetDdrMode
.L1455:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w2, 852
	mov	w1, 0
	add	x0, x20, 1272
	bl	ftl_memset
	adrp	x0, .LANCHOR1
	add	x1, x0, :lo12:.LANCHOR1
	add	x1, x1, 472
	ldr	w4, [x20, 2392]
	strb	wzr, [x20, 80]
	mov	w2, 12336
	str	x1, [x20, 88]
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L1456
	ldrb	w1, [x1, 19]
	cmp	w1, 50
	beq	.L1456
	mov	w1, 1
	str	w1, [x20, 2376]
.L1456:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1, 2133]
	add	w1, w2, 95
	and	w3, w1, 255
	mov	x1, 1
	cmp	w3, 57
	bhi	.L1457
	movk	x1, 0x205, lsl 48
	lsr	x1, x1, x3
	mvn	x1, x1
.L1457:
	and	w1, w1, 1
	cmp	w2, 241
	eor	w1, w1, 1
	cset	w3, eq
	orr	w1, w3, w1
	cbnz	w1, .L1458
	cmp	w2, 220
	bne	.L1459
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w1, [x1, 2135]
	cmp	w1, 149
	bne	.L1459
.L1458:
	add	x1, x19, :lo12:.LANCHOR0
	add	x3, x23, :lo12:.LANCHOR4
	mov	w6, 16
	mov	w5, 1
	add	x7, x1, 2132
	strb	w6, [x1, 73]
	strb	w5, [x1, 72]
	strb	w6, [x3, 1650]
	ldrb	w6, [x1, 2132]
	add	x1, x0, :lo12:.LANCHOR1
	cmp	w6, 152
	strb	w6, [x1, 3425]
	strb	w2, [x1, 3426]
	bne	.L1461
	ldrsb	w1, [x7, 4]
	tbnz	w1, #31, .L1462
	mov	w1, 24
	strb	w1, [x3, 1650]
.L1461:
	cmp	w4, 2049
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ccmp	w4, w1, 4, ne
	bne	.L1464
	add	x1, x23, :lo12:.LANCHOR4
	mov	w3, 16
	strb	w3, [x1, 1650]
.L1464:
	cmp	w2, 218
	bne	.L1465
	add	x1, x0, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x1, 3438]
	mov	w2, -38
.L1548:
	strb	w2, [x1, 3426]
.L1466:
	add	x20, x0, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 32
	add	x1, x20, 3296
	add	x0, x0, 96
	bl	ftl_memcpy
	mov	w2, 32
	add	x1, x20, 3424
	add	x0, x20, 472
	bl	ftl_memcpy
.L1459:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20, 72]
	cbnz	w0, .L1469
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L1471
	ldr	x0, [x20, 88]
	ldrb	w1, [x0, 17]
	and	w0, w1, 7
	strb	w0, [x20, 2357]
	tbnz	x1, 0, .L1471
	mov	w1, 1
	strb	w1, [x20, 2380]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20, 2357]
	bl	NandcSetMode
.L1471:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20, 88]
	ldrb	w0, [x0, 26]
	strb	w0, [x20, 204]
	bl	FlashLoadPhyInfo
	cbz	w0, .L1469
	ldr	w0, [x20, 2384]
	cbz	w0, .L1474
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
.L1543:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L1469
	add	x20, x19, :lo12:.LANCHOR0
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x0, [x20, 88]
	ldrh	w1, [x0, 14]
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L1448
	bl	FlashDieInfoInit
	ldr	x0, [x20, 88]
	ldrb	w0, [x0, 19]
	bl	FlashGetReadRetryDefault
	ldr	x0, [x20, 88]
	adrp	x1, .LANCHOR2+3550
	ldrh	w1, [x1, #:lo12:.LANCHOR2+3550]
	ldrb	w2, [x0, 9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L1476
	ldrh	w1, [x0, 14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L1477
.L1476:
	ldrh	w1, [x0, 14]
	and	w1, w1, -256
	strh	w1, [x0, 14]
.L1477:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0, 2357]
	tst	w0, 6
	beq	.L1478
	bl	FlashSavePhyInfo
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x0, x23, :lo12:.LANCHOR4
	ldr	w1, [x0, 1640]
	mov	w0, 0
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1478:
	bl	FlashSavePhyInfo
.L1469:
	add	x21, x19, :lo12:.LANCHOR0
	add	x20, x23, :lo12:.LANCHOR4
	ldr	x2, [x21, 88]
	str	xzr, [x20, 1624]
	ldrb	w0, [x2, 26]
	strb	w0, [x21, 204]
	ldrh	w0, [x2, 16]
	ubfx	x1, x0, 7, 1
	strb	w1, [x21, 80]
	ubfx	x1, x0, 3, 1
	strb	w1, [x20, 1689]
	ubfx	x1, x0, 4, 1
	ubfx	x0, x0, 8, 3
	strb	w1, [x21, 2368]
	strb	w0, [x21, 2357]
	ldrh	w1, [x2, 10]
	ldrb	w0, [x2, 12]
	sdiv	w1, w1, w0
	ldrb	w0, [x2, 18]
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x21, 88]
	ldrh	w1, [x0, 16]
	tbz	x1, 6, .L1480
	ldrb	w0, [x0, 19]
	ldrb	w1, [x21, 1274]
	ldrb	w2, [x21, 1273]
	strb	w1, [x20, 1649]
	sub	w1, w0, #1
	strb	w0, [x21, 128]
	and	w1, w1, 255
	strb	w2, [x21, 129]
	cmp	w1, 7
	bhi	.L1481
	adrp	x1, HynixReadRetrial
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x20, 1624]
	sub	w1, w0, #5
	and	w1, w1, 255
	cmp	w1, 1
	ccmp	w0, 8, 4, hi
	bne	.L1482
	mov	w1, 1
	str	w1, [x21, 2456]
.L1482:
	add	x1, x19, :lo12:.LANCHOR0
	cmp	w0, 7
	beq	.L1544
	cmp	w0, 8
	bne	.L1505
.L1544:
	add	x1, x1, 1300
.L1483:
	mov	x2, 0
	mov	w3, 0
.L1485:
	ldrsb	w4, [x1, x2]
	add	x2, x2, 1
	cmp	w4, 0
	cinc	w3, w3, eq
	cmp	x2, 32
	bne	.L1485
	cmp	w3, 27
	bls	.L1480
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
.L1480:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	ldr	w1, [x0, 2392]
	cmp	w1, w2
	bne	.L1495
	ldrb	w2, [x0, 204]
	cbz	w2, .L1495
	ldr	x0, [x0, 88]
	strb	wzr, [x0, 18]
.L1495:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0, 2132]
	cmp	w2, 44
	bne	.L1496
	ldrb	w2, [x0, 2380]
	cbz	w2, .L1496
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w1, w2
	bne	.L1497
	ldrb	w0, [x0, 204]
	cbnz	w0, .L1496
.L1497:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0, 2380]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L1496:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w0, [x1, 2357]
	tst	w0, 6
	beq	.L1498
	ldrb	w1, [x1, 2380]
	cbnz	w1, .L1499
	tbnz	x0, 0, .L1498
.L1499:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 0
	bl	flash_enter_slc_mode
	mov	w0, 0
	ldr	w1, [x23, 1640]
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1498:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 16
	ldr	x0, [x19, 88]
	ldrb	w0, [x0, 20]
	bl	FlashBchSel
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 3888
	bl	FlashReadIdbDataRaw
	mov	w0, 18928
	strb	w20, [x19, 73]
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	ldr	x1, [x19, 88]
	ldrb	w3, [x19, 2133]
	ldrb	w4, [x19, 2132]
	ldrb	w2, [x1, 12]
	strh	w2, [x19, 184]
	ldrb	w2, [x1, 7]
	str	w2, [x19, 180]
	ldrb	w2, [x19, 2135]
	orr	w2, w4, w2, lsl 24
	lsl	w4, w3, w20
	orr	w3, w4, w3, lsl 8
	ldrh	w4, [x1, 14]
	orr	w2, w2, w3
	str	w2, [x19, 176]
	ldrb	w2, [x19, 2358]
	strh	w2, [x19, 186]
	ldrb	w2, [x1, 13]
	strh	w2, [x19, 188]
	strh	w4, [x19, 190]
	ldrh	w2, [x1, 10]
	strh	w2, [x19, 192]
	ldrb	w3, [x1, 12]
	ldrh	w2, [x1, 10]
	sdiv	w2, w2, w3
	strh	w2, [x19, 194]
	ldrb	w3, [x1, 9]
	strh	w3, [x19, 196]
	ldrh	w5, [x1, 10]
	ldrb	w2, [x1, 9]
	mul	w2, w2, w5
	mov	w5, 512
	strh	w5, [x19, 200]
	ldrb	w5, [x19, 73]
	and	w2, w2, 65535
	strh	w5, [x19, 202]
	strh	w2, [x19, 198]
	ldrb	w5, [x19, 72]
	cmp	w5, 1
	bne	.L1500
	ubfiz	w2, w2, 1, 15
	lsr	w4, w4, 1
	ubfiz	w3, w3, 1, 15
	strh	w2, [x19, 198]
	strb	w20, [x19, 73]
	mov	w2, 8
	strh	w4, [x19, 190]
	strh	w3, [x19, 196]
	strh	w2, [x19, 202]
.L1500:
	ldrb	w0, [x1, 20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
.L1448:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L1465:
	cmp	w2, 220
	bne	.L1467
	add	x1, x0, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x1, 3438]
	mov	w2, -36
	b	.L1548
.L1467:
	cmp	w2, 211
	bne	.L1466
	add	x1, x0, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x1, 3438]
	mov	w2, 2
	strb	w2, [x1, 3437]
	b	.L1466
.L1474:
	ldrb	w0, [x20, 2357]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20, 2357]
	b	.L1543
.L1505:
	add	x1, x1, 1292
	b	.L1483
.L1481:
	sub	w1, w0, #17
	and	w1, w1, 255
	cmp	w1, 2
	bhi	.L1487
	adrp	x1, MicronReadRetrial
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x20, 1624]
	cmp	w0, 19
	beq	.L1488
.L1549:
	mov	w0, 7
	b	.L1546
.L1488:
	mov	w0, 15
.L1546:
	strb	w0, [x20, 1649]
	b	.L1480
.L1487:
	sub	w1, w0, #65
	cmp	w0, 33
	and	w1, w1, 255
	ccmp	w1, 1, 0, ne
	bhi	.L1489
	adrp	x0, ToshibaReadRetrial
	add	x0, x0, :lo12:ToshibaReadRetrial
	str	x0, [x20, 1624]
	mov	w0, 4
	strb	w0, [x21, 129]
	b	.L1549
.L1489:
	sub	w2, w0, #34
	sub	w1, w0, #67
	and	w2, w2, 255
	and	w1, w1, 255
	cmp	w2, 1
	ccmp	w1, 1, 0, hi
	bhi	.L1490
	adrp	x2, ToshibaReadRetrial
	add	x2, x2, :lo12:ToshibaReadRetrial
	str	x2, [x20, 1624]
	cmp	w0, 35
	mov	w2, 68
	ccmp	w0, w2, 4, ne
	beq	.L1491
	mov	w0, 7
.L1545:
	strb	w0, [x20, 1649]
	cmp	w1, 1
	add	x0, x19, :lo12:.LANCHOR0
	bhi	.L1493
	mov	w1, 4
.L1547:
	strb	w1, [x0, 129]
	b	.L1480
.L1491:
	mov	w0, 17
	b	.L1545
.L1493:
	mov	w1, 5
	b	.L1547
.L1490:
	cmp	w0, 49
	bne	.L1494
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x20, 1624]
	b	.L1480
.L1494:
	cmp	w0, 50
	bne	.L1480
	adrp	x0, samsung_read_retrial
	str	wzr, [x21, 2376]
	add	x0, x0, :lo12:samsung_read_retrial
	str	x0, [x20, 1624]
	b	.L1480
.L1502:
	mov	w0, -2
	b	.L1448
.L1462:
	strb	w5, [x3, 1688]
	b	.L1461
	.size	FlashInit, .-FlashInit
	.align	2
	.global	FlashPageProgMsbFFData
	.type	FlashPageProgMsbFFData, %function
FlashPageProgMsbFFData:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 48]
	and	w23, w0, 255
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	and	w19, w2, 65535
	ldrb	w2, [x0, 204]
	cbz	w2, .L1551
	ldr	w0, [x0, 2376]
	cbnz	w0, .L1550
.L1551:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0, 88]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #5
	and	w3, w0, 255
	cmp	w3, 63
	bhi	.L1550
	mov	x2, 16391
	movk	x2, 0x4000, lsl 16
	movk	x2, 0x2000, lsl 32
	movk	x2, 0x8000, lsl 48
	lsr	x0, x2, x3
	tbz	x0, 0, .L1550
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR4
	mov	w24, w1
	add	x21, x21, 1500
	add	x22, x22, :lo12:.LANCHOR4
.L1553:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0, 88]
	ldrh	w0, [x0, 10]
	cmp	w0, w19
	bhi	.L1554
.L1550:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1554:
	ldrh	w1, [x21, w19, sxtw 1]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1550
	ldr	x0, [x22, 1656]
	mov	w2, 32768
	mov	w1, 255
	bl	ftl_memset
	ldr	x3, [x22, 1656]
	add	w1, w19, w24
	add	w19, w19, 1
	mov	w0, w23
	and	w19, w19, 65535
	mov	x2, x3
	bl	FlashProgPage
	b	.L1553
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w0, 56
	stp	x21, x22, [sp, 32]
	str	w2, [x29, 104]
	adrp	x2, .LANCHOR1+481
	stp	x23, x24, [sp, 48]
	nop // between mem op and mult-accumulate
	umaddl	x0, w1, w0, x19
	stp	x25, x26, [sp, 64]
	and	w22, w1, 255
	adrp	x25, .LANCHOR0
	ldrb	w2, [x2, #:lo12:.LANCHOR1+481]
	add	x23, x25, :lo12:.LANCHOR0
	stp	x27, x28, [sp, 80]
	str	x0, [x29, 96]
	str	w2, [x29, 108]
.L1560:
	ldr	x0, [x29, 96]
	cmp	x19, x0
	bne	.L1580
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1580:
	ldr	w1, [x29, 104]
	add	x2, x29, 124
	mov	w4, w22
	add	x3, x29, 120
	mov	x0, x19
	bl	LogAddr2PhyAddr
	add	x1, x25, :lo12:.LANCHOR0
	ldr	w0, [x29, 120]
	ldrb	w2, [x1, 2358]
	cmp	w2, w0
	bhi	.L1561
	mov	w0, -1
	str	w0, [x19]
.L1562:
	sub	w22, w22, #1
	add	x19, x19, 56
	and	w22, w22, 255
	b	.L1560
.L1561:
	add	x0, x1, w0, uxtw
	adrp	x21, .LANCHOR4
	add	x27, x21, :lo12:.LANCHOR4
	mov	w24, 0
	mov	w28, 256
	ldrb	w20, [x0, 2360]
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashCs
.L1563:
	ldr	w1, [x29, 124]
	mov	w0, w20
	bl	FlashReadCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldrb	w2, [x29, 108]
	mov	w1, 0
	ldp	x3, x4, [x19, 8]
	mov	w0, w20
	bl	NandcXferData
	mov	w26, w0
	ldrb	w0, [x27, 1688]
	cbz	w0, .L1564
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w26, w26, w28, ls
.L1564:
	cmp	w24, 9
	ccmn	w26, #1, 0, ls
	bne	.L1565
	add	w24, w24, 1
	b	.L1563
.L1565:
	cmp	w24, 0
	mov	w27, 256
	add	x21, x21, :lo12:.LANCHOR4
	csel	w26, w26, w27, eq
	mov	w24, 0
.L1567:
	ldr	w0, [x23, 76]
	ldr	w1, [x29, 124]
	add	w1, w1, w0
	mov	w0, w20
	bl	FlashReadCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	x0, [x19, 8]
	mov	w1, 0
	ldrb	w2, [x29, 108]
	cmp	x0, 0
	add	x3, x0, 2048
	ldr	x0, [x19, 16]
	csel	x3, x3, xzr, ne
	cmp	x0, 0
	add	x4, x0, 8
	csel	x4, x4, xzr, ne
	mov	w0, w20
	bl	NandcXferData
	mov	w28, w0
	ldrb	w0, [x21, 1688]
	cbz	w0, .L1570
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w28, w28, w27, ls
.L1570:
	cmp	w24, 9
	ccmn	w28, #1, 0, ls
	bne	.L1571
	add	w24, w24, 1
	b	.L1567
.L1571:
	cmp	w24, 0
	mov	w2, 256
	mov	w0, w20
	csel	w28, w28, w2, eq
	bl	NandcFlashDeCs
	ldrb	w0, [x23, 2464]
	cmp	w26, w28
	csel	w5, w26, w28, cs
	add	w0, w0, w0, lsl 1
	cmp	w5, w0, lsr 2
	bls	.L1573
	cmn	w5, #1
	csel	w5, w5, w2, eq
.L1573:
	cmp	w5, 256
	ldr	x0, [x19, 16]
	ccmn	w5, #1, 4, ne
	csel	w5, w5, wzr, eq
	str	w5, [x19]
	cbz	x0, .L1576
	ldr	w1, [x0, 12]
	cmn	w1, #1
	bne	.L1576
	ldr	w1, [x0, 8]
	cmn	w1, #1
	bne	.L1576
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1576
	str	w1, [x19]
.L1576:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1562
	ldr	w1, [x19, 4]
	adrp	x0, .LC98
	ldrb	w2, [x23, 2464]
	add	x0, x0, :lo12:.LC98
	bl	printk
	ldr	x1, [x19, 8]
	cbz	x1, .L1578
	adrp	x0, .LC99
	mov	w3, 8
	mov	w2, 4
	add	x0, x0, :lo12:.LC99
	bl	rknand_print_hex
.L1578:
	ldr	x1, [x19, 16]
	cbz	x1, .L1562
	mov	w3, 4
	adrp	x0, .LC100
	mov	w2, w3
	add	x0, x0, :lo12:.LC100
	bl	rknand_print_hex
	b	.L1562
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	add	x3, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	stp	w2, w1, [x29, 104]
	ldrb	w4, [x3, 72]
	cbnz	w4, .L1609
	mov	x25, x0
	adrp	x0, .LANCHOR1+481
	mov	w24, 0
	mov	w23, 0
	ldrb	w0, [x0, #:lo12:.LANCHOR1+481]
	str	w0, [x29, 120]
	ldrb	w0, [x3, 80]
	str	w0, [x29, 100]
	adrp	x0, .LANCHOR4
	add	x27, x0, :lo12:.LANCHOR4
.L1610:
	ldr	w0, [x29, 108]
	cmp	w23, w0
	bcc	.L1644
	mov	w0, 0
	b	.L1608
.L1609:
	bl	FlashReadSlc2KPages
.L1608:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L1644:
	mov	w26, 56
	ldr	w1, [x29, 104]
	add	x2, x29, 140
	add	x3, x29, 136
	umull	x0, w23, w26
	add	x26, x22, :lo12:.LANCHOR0
	str	x0, [x29, 112]
	add	x21, x25, x0
	ldr	w0, [x29, 108]
	sub	w4, w0, w23
	mov	x0, x21
	ldr	w28, [x21, 4]
	bl	LogAddr2PhyAddr
	ldrb	w2, [x26, 2358]
	mov	w20, w0
	ldr	w0, [x29, 136]
	cmp	w2, w0
	bhi	.L1612
	ldr	x1, [x29, 112]
	mov	w0, -1
	str	w0, [x25, x1]
.L1613:
	add	w23, w23, 1
	b	.L1610
.L1612:
	add	x0, x26, w0, uxtw
	ldrb	w19, [x0, 2360]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0, 1689]
	cmp	w0, 0
	mov	w0, w19
	csel	w20, w20, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x0, [x26, 88]
	ldrb	w0, [x0, 19]
	sub	w2, w0, #1
	and	w2, w2, 255
	cmp	w2, 7
	bhi	.L1615
	add	x1, x26, 1272
	sxtw	x2, w19
	add	x1, x1, x2
	sub	w0, w0, #7
	and	w0, w0, 255
	cmp	w0, 1
	ldrb	w3, [x1, 12]
	bhi	.L1616
	ldrb	w3, [x1, 20]
.L1616:
	add	x0, x22, :lo12:.LANCHOR0
	add	x2, x0, x2
	ldrb	w1, [x2, 2128]
	cmp	w1, w3
	beq	.L1615
	ldrb	w1, [x0, 1273]
	add	x2, x0, 1276
	mov	w0, w19
	bl	HynixSetRRPara
.L1615:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29, 104]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29, 124]
	cbz	w0, .L1617
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 204]
	cbz	w0, .L1617
	mov	w0, w19
	bl	flash_enter_slc_mode
.L1618:
	add	x28, x22, :lo12:.LANCHOR0
.L1624:
	ldr	w1, [x29, 140]
	cmn	w1, #1
	bne	.L1619
	cmp	w19, 255
	beq	.L1646
.L1619:
	cbz	w20, .L1621
	ldr	w2, [x28, 76]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
.L1622:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L1620
	ldr	w1, [x29, 140]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
.L1620:
	ldrb	w2, [x29, 120]
	mov	w1, 0
	ldp	x3, x4, [x21, 8]
	mov	w0, w19
	bl	NandcXferData
	mov	w26, w0
	ldrb	w0, [x28, 80]
	cbz	w0, .L1623
	cmn	w26, #1
	bne	.L1623
	strb	wzr, [x28, 80]
	mov	w20, 0
	b	.L1624
.L1617:
	mov	w0, w19
	bl	flash_exit_slc_mode
	b	.L1618
.L1621:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1622
.L1646:
	mov	w20, 0
	b	.L1620
.L1623:
	cbz	w20, .L1625
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x29, 140]
	ldr	w0, [x0, 76]
	add	w1, w1, w0
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	add	w0, w23, 1
	mov	w1, 56
	ldrb	w2, [x29, 120]
	nop // between mem op and mult-accumulate
	umaddl	x0, w0, w1, x25
	mov	w1, 0
	ldp	x3, x4, [x0, 8]
	mov	w0, w19
	bl	NandcXferData
	cmn	w0, #1
	mov	w24, w0
	csel	w20, w20, wzr, ne
.L1625:
	add	x28, x22, :lo12:.LANCHOR0
	mov	w0, w19
	bl	NandcFlashDeCs
	ldrb	w0, [x29, 100]
	cmn	w26, #1
	strb	w0, [x28, 80]
	bne	.L1626
	ldrb	w0, [x28, 2380]
	cbnz	w0, .L1627
.L1631:
	ldr	x4, [x27, 1624]
	cbnz	x4, .L1628
	ldr	w1, [x29, 140]
	mov	w0, w19
	ldp	x2, x3, [x21, 8]
	bl	FlashReadRawPage
	b	.L1683
.L1627:
	ldr	x0, [x28, 136]
	mov	w4, 1
	ldr	w1, [x29, 140]
	ldp	x2, x3, [x21, 8]
	ldr	w20, [x0, 304]
	mov	w0, w19
	bl	FlashDdrTunningRead
	mov	w26, w0
	cmn	w0, #1
	beq	.L1630
	ldrb	w0, [x28, 2464]
	cmp	w26, w0, lsr 1
	bls	.L1647
.L1630:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w26, #1
	beq	.L1631
.L1647:
	mov	w20, 0
.L1626:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 2464]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bls	.L1632
	ldr	x0, [x27, 1624]
	cmp	x0, 0
	mov	w0, 256
	csel	w26, w26, w0, ne
.L1632:
	ldr	x0, [x29, 112]
	cmp	w26, 256
	ccmn	w26, #1, 4, ne
	csel	w3, w26, wzr, eq
	cmn	w3, #1
	str	w3, [x25, x0]
	bne	.L1639
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x21, 4]
	ldrb	w2, [x0, 2464]
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	bl	printk
	ldr	x1, [x21, 16]
	cbz	x1, .L1639
	mov	w3, 4
	adrp	x0, .LC100
	mov	w2, w3
	add	x0, x0, :lo12:.LC100
	bl	rknand_print_hex
.L1639:
	cbz	w20, .L1641
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 2464]
	add	w0, w0, w0, lsl 1
	cmp	w24, w0, lsr 2
	bls	.L1642
	ldr	x0, [x27, 1624]
	cmp	x0, 0
	mov	w0, 256
	csel	w24, w24, w0, ne
.L1642:
	add	w0, w23, 1
	mov	w1, 56
	cmp	w24, 256
	ccmn	w24, #1, 4, ne
	umull	x0, w0, w1
	csel	w1, w24, wzr, eq
	str	w1, [x25, x0]
.L1641:
	ldr	w0, [x29, 124]
	add	w23, w23, w20
	cbz	w0, .L1613
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 204]
	cbz	w0, .L1613
	mov	w0, w19
	bl	flash_exit_slc_mode
	b	.L1613
.L1628:
	ldr	w1, [x29, 140]
	mov	w0, w19
	ldp	x2, x3, [x21, 8]
	mov	w20, 0
	blr	x4
	mov	w26, w0
	cmn	w0, #1
	bne	.L1632
	add	x1, x22, :lo12:.LANCHOR0
	ldr	x0, [x1, 88]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 7
	bhi	.L1633
	add	x2, x1, 1276
	ldrb	w1, [x1, 1273]
	mov	w3, 0
	mov	w0, w19
	bl	HynixSetRRPara
.L1633:
	ldp	x2, x3, [x21, 8]
	add	x20, x22, :lo12:.LANCHOR0
	ldr	w1, [x29, 140]
	mov	w0, w19
	bl	FlashReadRawPage
	mov	w26, w0
	ldrb	w2, [x20, 2464]
	mov	w3, w0
	ldr	w1, [x21, 4]
	adrp	x0, .LC101
	add	x0, x0, :lo12:.LC101
	bl	printk
	cmn	w26, #1
	bne	.L1650
	ldrb	w0, [x20, 204]
	cbz	w0, .L1650
	ldr	w0, [x29, 124]
	cbz	w0, .L1634
	mov	w0, w19
	bl	flash_enter_slc_mode
.L1635:
	ldr	w1, [x29, 140]
	mov	w0, w19
	ldp	x2, x3, [x21, 8]
	ldr	x4, [x27, 1624]
	blr	x4
.L1683:
	mov	w26, w0
.L1650:
	mov	w20, 0
	b	.L1632
.L1634:
	mov	w0, w19
	bl	flash_exit_slc_mode
	b	.L1635
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FlashLoadFactorBbt
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	stp	x29, x30, [sp, -176]!
	mov	w2, 16
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	stp	x19, x20, [sp, 16]
	add	x0, x0, 176
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR4
	stp	x27, x28, [sp, 80]
	add	x19, x23, :lo12:.LANCHOR4
	mov	w26, -1
	mov	w27, 0
	ldrh	w21, [x0, 14]
	ldrh	w0, [x0, 12]
	mul	w21, w21, w0
	add	x0, x19, 1696
	bl	ftl_memset
	and	w21, w21, 65535
	ldr	x0, [x19, 1664]
	add	w25, w21, w26
	stp	xzr, x0, [x29, 128]
	and	w25, w25, 65535
	sub	w0, w21, #12
	mov	w19, 0
	str	w0, [x29, 108]
.L1685:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 2358]
	cmp	w0, w19
	bhi	.L1691
	mov	w0, w26
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1691:
	mul	w28, w21, w19
	mov	w20, w25
	add	x24, x23, :lo12:.LANCHOR4
	mov	w3, 61664
.L1686:
	ldr	w0, [x29, 108]
	cmp	w20, w0
	ble	.L1688
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
	beq	.L1687
	ldr	x0, [x24, 1664]
	ldrh	w0, [x0]
	cmp	w0, w3
	bne	.L1687
	add	x24, x24, 1696
	add	w27, w27, 1
	and	w27, w27, 65535
	mov	w2, w20
	mov	w1, w19
	adrp	x0, .LC102
	add	x0, x0, :lo12:.LC102
	bl	printk
	strh	w20, [x24, w19, sxtw 1]
.L1688:
	add	x0, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	and	w19, w19, 255
	ldrb	w0, [x0, 2358]
	cmp	w0, w27
	csel	w26, w26, wzr, ne
	b	.L1685
.L1687:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L1686
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashProgSlc2KPages
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w24, 56
	stp	x25, x26, [sp, 64]
	mov	w25, w2
	adrp	x2, .LANCHOR1+481
	stp	x21, x22, [sp, 32]
	nop // between mem op and mult-accumulate
	umaddl	x24, w1, w24, x0
	and	w22, w1, 255
	ldrb	w26, [x2, #:lo12:.LANCHOR1+481]
	mov	w23, w22
	stp	x27, x28, [sp, 80]
	mov	x21, x0
	stp	x19, x20, [sp, 16]
	adrp	x27, .LANCHOR0
	mov	x19, x0
	add	x28, x27, :lo12:.LANCHOR0
.L1697:
	cmp	x21, x24
	bne	.L1703
	adrp	x23, .LANCHOR4
	add	x24, x23, :lo12:.LANCHOR4
.L1704:
	cmp	x19, x21
	bne	.L1711
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1703:
	mov	w1, w25
	mov	w4, w23
	add	x3, x29, 116
	add	x2, x29, 112
	mov	x0, x21
	bl	LogAddr2PhyAddr
	add	x5, x27, :lo12:.LANCHOR0
	ldr	w0, [x29, 116]
	ldrb	w1, [x5, 2358]
	cmp	w1, w0
	bhi	.L1698
	mov	w0, -1
	str	w0, [x21]
.L1699:
	sub	w23, w23, #1
	add	x21, x21, 56
	and	w23, w23, 255
	b	.L1697
.L1698:
	add	x0, x5, w0, uxtw
	str	x5, [x29, 104]
	ldrb	w20, [x0, 2360]
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashCs
	ldr	w1, [x29, 112]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldp	x3, x4, [x21, 8]
	mov	w2, w26
	mov	w1, 1
	mov	w0, w20
	bl	NandcXferData
	ldr	w1, [x29, 112]
	mov	w0, w20
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	w1, [x29, 112]
	mov	w0, w20
	bl	FlashReadStatus
	sbfx	x0, x0, 0, 1
	ldr	x5, [x29, 104]
	str	w0, [x21]
	ldr	w1, [x29, 112]
	ldr	w0, [x5, 76]
	add	w1, w1, w0
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldr	x0, [x21, 8]
	mov	w2, w26
	mov	w1, 1
	cmp	x0, 0
	add	x3, x0, 2048
	ldr	x0, [x21, 16]
	csel	x3, x3, xzr, ne
	cmp	x0, 0
	add	x4, x0, 8
	csel	x4, x4, xzr, ne
	mov	w0, w20
	bl	NandcXferData
	ldr	w0, [x28, 76]
	ldr	w1, [x29, 112]
	add	w1, w1, w0
	mov	w0, w20
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	w1, [x29, 112]
	mov	w0, w20
	bl	FlashReadStatus
	tbz	x0, 0, .L1702
	mov	w0, -1
	str	w0, [x21]
.L1702:
	mov	w0, w20
	bl	NandcFlashDeCs
	b	.L1699
.L1711:
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1705
	ldr	w1, [x19, 4]
	adrp	x0, .LC103
	add	x0, x0, :lo12:.LC103
	bl	printk
.L1706:
	sub	w22, w22, #1
	add	x19, x19, 56
	and	w22, w22, 255
	b	.L1704
.L1705:
	add	x20, x23, :lo12:.LANCHOR4
	mov	w4, w22
	add	x3, x29, 116
	add	x2, x29, 112
	mov	w1, w25
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x20, 1672]
	mov	x2, 56
	mov	x1, x19
	str	wzr, [x0]
	ldr	x0, [x20, 1680]
	str	wzr, [x0]
	add	x0, x29, 120
	bl	memcpy
	ldr	x0, [x20, 1672]
	mov	w2, w25
	str	x0, [x29, 128]
	mov	w1, 1
	ldr	x0, [x20, 1680]
	str	x0, [x29, 136]
	add	x0, x29, 120
	bl	FlashReadPages
	ldr	w20, [x29, 120]
	cmn	w20, #1
	bne	.L1707
	ldr	w1, [x19, 4]
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	bl	printk
	str	w20, [x19]
.L1707:
	ldr	w20, [x29, 120]
	cmp	w20, 256
	bne	.L1708
	ldr	w1, [x19, 4]
	adrp	x0, .LC105
	add	x0, x0, :lo12:.LC105
	bl	printk
	str	w20, [x19]
.L1708:
	ldr	x0, [x19, 16]
	cbz	x0, .L1709
	ldr	w2, [x0]
	ldr	x0, [x24, 1680]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1709
	ldr	w1, [x19, 4]
	adrp	x0, .LC106
	add	x0, x0, :lo12:.LC106
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1709:
	ldr	x0, [x19, 8]
	cbz	x0, .L1706
	ldr	w2, [x0]
	ldr	x0, [x24, 1672]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1706
	ldr	w1, [x19, 4]
	adrp	x0, .LC107
	add	x0, x0, :lo12:.LC107
	bl	printk
	mov	w0, -1
	str	w0, [x19]
	b	.L1706
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	stp	x29, x30, [sp, -96]!
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR4
	stp	x21, x22, [sp, 32]
	add	x20, x20, :lo12:.LANCHOR4
	stp	x25, x26, [sp, 64]
	add	x20, x20, 1712
	str	x27, [sp, 80]
	mov	x26, x20
	mov	w27, 61664
	ldr	x1, [x0, 3608]
	ldr	x25, [x0, 3656]
	adrp	x0, .LANCHOR0
	add	x21, x0, :lo12:.LANCHOR0
	mov	x24, x0
	add	x21, x21, 2636
	stp	x1, x25, [x20, 8]
.L1725:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2494]
	cmp	w23, w1
	bcc	.L1730
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1730:
	ldrh	w19, [x0, 2542]
	mov	x22, x0
	mov	w1, -1
	strh	w1, [x21]
	sub	w19, w19, #1
	and	w19, w19, 65535
.L1726:
	ldrh	w0, [x22, 2542]
	sub	w1, w0, #16
	cmp	w19, w1
	ble	.L1728
	madd	w0, w0, w23, w19
	mov	w2, 1
	mov	w1, w2
	lsl	w0, w0, 10
	str	w0, [x26, 4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x26]
	cmn	w0, #1
	beq	.L1727
	ldrh	w0, [x25]
	cmp	w0, w27
	bne	.L1727
	strh	w19, [x21]
.L1728:
	add	w23, w23, 1
	add	x21, x21, 2
	b	.L1725
.L1727:
	sub	w19, w19, #1
	and	w19, w19, 65535
	b	.L1726
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
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
	adrp	x1, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x1, :lo12:.LANCHOR0
	bne	.L1736
	ldrh	w19, [x1, 2546]
.L1737:
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
	bne	.L1738
	mov	w22, 0
	mov	w24, 2
.L1739:
	cmp	w22, w19
	ble	.L1742
.L1738:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 192
	ret
.L1736:
	ldrh	w19, [x1, 2544]
	b	.L1737
.L1742:
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
	bne	.L1740
	ldr	w0, [x29, 132]
	cmn	w0, #1
	bne	.L1740
	ldr	w0, [x29, 72]
	cmn	w0, #1
	beq	.L1740
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1739
.L1740:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1739
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x24, .LANCHOR2
	add	x0, x24, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	adrp	x23, .LANCHOR4
	stp	x21, x22, [sp, 32]
	add	x20, x23, :lo12:.LANCHOR4
	add	x20, x20, 1712
	adrp	x19, .LANCHOR0
	ldr	x1, [x0, 3608]
	add	x25, x19, :lo12:.LANCHOR0
	ldr	x22, [x0, 3656]
	mov	w26, 61649
	stp	x1, x22, [x20, 8]
	bl	FtlBbtMemInit
	ldrh	w21, [x25, 2542]
	sub	w21, w21, #1
	and	w21, w21, 65535
.L1748:
	ldrh	w0, [x25, 2542]
	sub	w0, w0, #48
	cmp	w21, w0
	ble	.L1751
	lsl	w0, w21, 10
	mov	w2, 1
	str	w0, [x20, 4]
	mov	w1, w2
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L1749
	ldr	w0, [x20, 4]
	mov	w2, 1
	mov	w1, w2
	add	w0, w0, 1
	str	w0, [x20, 4]
	mov	x0, x20
	bl	FlashReadPages
.L1749:
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L1750
	ldrh	w0, [x22]
	cmp	w0, w26
	bne	.L1750
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 2624
	strh	w21, [x1, 2624]
	ldr	w1, [x22, 4]
	str	w1, [x0, 8]
	ldrh	w1, [x22, 8]
	strh	w1, [x0, 4]
.L1751:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21, 2624]
	cmp	w1, w0
	beq	.L1765
	ldrh	w1, [x21, 2628]
	cmp	w1, w0
	beq	.L1755
	add	x25, x23, :lo12:.LANCHOR4
	lsl	w1, w1, 10
	add	x0, x25, 1712
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x25, 1712]
	cmn	w0, #1
	beq	.L1755
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1755
	ldr	w1, [x21, 2632]
	ldr	w0, [x22, 4]
	cmp	w0, w1
	bls	.L1755
	str	w0, [x21, 2632]
	ldrh	w1, [x21, 2628]
	ldrh	w0, [x22, 8]
	strh	w1, [x21, 2624]
	strh	w0, [x21, 2628]
.L1755:
	add	x25, x19, :lo12:.LANCHOR0
	add	x21, x23, :lo12:.LANCHOR4
	add	x21, x21, 1712
	add	x26, x24, :lo12:.LANCHOR2
	mov	w1, 1
	ldrh	w0, [x25, 2624]
	bl	FtlGetLastWrittenPage
	sxth	w20, w0
	add	w0, w0, 1
	strh	w0, [x25, 2626]
.L1757:
	tbnz	w20, #31, .L1762
	ldrh	w0, [x25, 2624]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w20, w0, lsl 10
	str	w0, [x21, 4]
	ldr	x0, [x26, 3608]
	str	x0, [x21, 8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1758
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1758
.L1762:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 10]
	mov	w2, 65535
	strh	w0, [x1, 2630]
	ldrh	w0, [x22, 12]
	cmp	w0, w2
	bne	.L1759
.L1760:
	add	x19, x19, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LANCHOR2
	add	x21, x19, 2656
	add	x23, x23, :lo12:.LANCHOR4
	mov	w20, 0
.L1763:
	ldrh	w0, [x19, 2494]
	cmp	w20, w0
	bcc	.L1764
	mov	w0, 0
.L1747:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1750:
	sub	w21, w21, #1
	and	w21, w21, 65535
	b	.L1748
.L1758:
	sub	w20, w20, #1
	sxth	w20, w20
	b	.L1757
.L1759:
	ldr	w2, [x1, 2468]
	cmp	w0, w2
	beq	.L1760
	ldrh	w1, [x1, 2482]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L1760
	cmp	w0, w1
	bcs	.L1760
	bl	FtlSysBlkNumInit
	b	.L1760
.L1764:
	ldrh	w2, [x24, 3784]
	ldr	x0, [x23, 1720]
	mul	w1, w2, w20
	lsl	w2, w2, 2
	add	w20, w20, 1
	add	x1, x0, x1, lsl 2
	ldr	x0, [x21], 8
	bl	ftl_memcpy
	b	.L1763
.L1765:
	mov	w0, -1
	b	.L1747
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x19, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR0
	add	x20, x23, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	mov	w25, 56
	ldr	x0, [x19, 3768]
	ldr	w2, [x20, 2572]
	stp	x27, x28, [sp, 80]
	strh	wzr, [x19, 3856]
	lsl	w2, w2, 2
	strh	wzr, [x20, 2590]
	bl	ftl_memset
	ldr	x0, [x19, 3712]
	mov	w1, 0
	ldr	w2, [x20, 2572]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 3736]
	mov	w1, 0
	ldrh	w2, [x20, 2564]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x20, 2592]
	mov	w1, 0
	ldrh	w2, [x20, 2564]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w2, 16
	mov	w1, 255
	add	x0, x19, 784
	bl	ftl_memset
	ldrh	w24, [x20, 2480]
	add	x0, x20, 2504
	str	x0, [x29, 112]
.L1777:
	ldrh	w0, [x20, 2482]
	cmp	w0, w24
	bls	.L1816
	ldrh	w9, [x20, 2472]
	mov	x6, 0
	ldrh	w8, [x20, 2556]
	mov	w21, 0
	add	x5, x22, :lo12:.LANCHOR2
	mov	w7, 4
	b	.L1817
.L1779:
	ldr	x0, [x29, 112]
	mov	w1, w24
	ldrb	w0, [x0, x6]
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1778
	umull	x2, w21, w25
	ldr	x0, [x5, 3584]
	lsl	w4, w4, 10
	add	x0, x0, x2
	str	w4, [x0, 4]
	ldr	x1, [x5, 3584]
	ldr	x0, [x5, 1392]
	add	x1, x1, x2
	ldr	x2, [x5, 1400]
	str	x0, [x1, 8]
	mul	w0, w21, w8
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w0, w0, w7
	add	x0, x2, w0, sxtw 2
	str	x0, [x1, 16]
.L1778:
	add	x6, x6, 1
.L1817:
	cmp	w9, w6, uxth
	bhi	.L1779
	cbnz	w21, .L1780
.L1815:
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L1777
.L1780:
	add	x19, x22, :lo12:.LANCHOR2
	mov	w2, 1
	mov	w1, w21
	add	x27, x19, 784
	mov	x26, 0
	ldr	x0, [x19, 3584]
	bl	FlashReadPages
	umull	x0, w21, w25
	str	x0, [x29, 120]
.L1814:
	ldr	x0, [x19, 3584]
	add	x1, x0, x26
	ldr	w0, [x0, x26]
	ldr	w28, [x1, 4]
	cmn	w0, #1
	ldr	x21, [x1, 16]
	ubfx	x28, x28, 10, 16
	bne	.L1783
	mov	w6, 16
	mov	w7, 65535
.L1785:
	ldr	x0, [x19, 3584]
	mov	w2, 1
	stp	w7, w6, [x29, 104]
	add	x0, x0, x26
	ldr	w1, [x0, 4]
	add	w1, w1, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 3584]
	add	x0, x0, x26
	bl	FlashReadPages
	ldp	w7, w6, [x29, 104]
	ldrh	w0, [x21]
	cmp	w0, w7
	ldr	x0, [x19, 3584]
	bne	.L1782
	mov	w1, -1
	str	w1, [x0, x26]
	ldr	x0, [x19, 3584]
	ldr	w0, [x0, x26]
	cmp	w0, w1
	beq	.L1851
.L1783:
	ldr	w0, [x19, 752]
	ldr	w10, [x21, 4]
	cmn	w0, #1
	beq	.L1786
	cmp	w0, w10
	bhi	.L1787
.L1786:
	cmn	w10, #1
	beq	.L1787
	add	w0, w10, 1
	str	w0, [x19, 752]
.L1787:
	ldrh	w0, [x21]
	mov	w1, 61604
	cmp	w0, w1
	beq	.L1789
	bhi	.L1790
	mov	w1, 61574
	cmp	w0, w1
	beq	.L1791
.L1788:
	ldr	x0, [x29, 120]
	add	x26, x26, 56
	cmp	x26, x0
	bne	.L1814
	b	.L1815
.L1782:
	ldr	w0, [x0, x26]
	cmn	w0, #1
	bne	.L1783
	sub	w6, w6, #1
	ands	w6, w6, 65535
	bne	.L1785
.L1851:
	mov	w1, 0
	mov	w0, w28
	bl	FtlFreeSysBlkQueueIn
	b	.L1788
.L1790:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1792
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1851
	b	.L1788
.L1792:
	ldr	w6, [x20, 2572]
	ldrh	w1, [x19, 3856]
	and	w2, w6, 65535
	ldr	x7, [x19, 3768]
	sub	w0, w2, #1
	sub	w2, w2, w1
	sub	w2, w2, #1
	sxth	x0, w0
	sxth	w2, w2
.L1794:
	cmp	w0, w2
	bgt	.L1800
	tbz	w0, #31, .L1832
	b	.L1788
.L1800:
	sxtw	x8, w0
	lsl	x9, x8, 2
	ldr	w11, [x7, x9]
	cmp	w10, w11
	bls	.L1795
	ldr	w2, [x7]
	cbnz	w2, .L1796
	cmp	w6, w1
	beq	.L1796
	add	w1, w1, 1
	strh	w1, [x19, 3856]
.L1796:
	mov	w1, 0
.L1797:
	cmp	w1, w0
	bne	.L1798
	ldr	x1, [x19, 3768]
	ldr	w2, [x21, 4]
	str	w2, [x1, x9]
	ldr	x1, [x19, 3712]
	strh	w28, [x1, x8, lsl 1]
	tbnz	w0, #31, .L1788
	ldrh	w1, [x19, 3856]
	ldr	w2, [x20, 2572]
	sub	w2, w2, w1
	sub	w2, w2, #1
	cmp	w0, w2, sxth
	bgt	.L1788
.L1832:
	add	w1, w1, 1
	strh	w1, [x19, 3856]
	ldr	x1, [x19, 3768]
	ldr	w2, [x21, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 3712]
.L1850:
	strh	w28, [x1, x0, lsl 1]
	b	.L1788
.L1798:
	ldr	x7, [x19, 3768]
	sxtw	x2, w1
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	x10, x7, x6
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w10, [x10, 4]
	str	w10, [x7, x6]
	ldr	x6, [x19, 3712]
	add	x7, x6, x2
	ldrh	w7, [x7, 2]
	strh	w7, [x6, x2]
	b	.L1797
.L1795:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L1794
.L1791:
	ldrh	w6, [x20, 2564]
	ldrh	w1, [x20, 2590]
	sub	w2, w6, #1
	ldr	x7, [x19, 3736]
	sxth	x0, w2
	sub	w2, w2, w1
.L1803:
	cmp	w0, w2
	ble	.L1808
	sxtw	x8, w0
	lsl	x9, x8, 2
	ldr	w11, [x7, x9]
	cmp	w10, w11
	bls	.L1804
	ldr	w2, [x7]
	cbnz	w2, .L1805
	cmp	w6, w1
	beq	.L1805
	add	w1, w1, 1
	strh	w1, [x20, 2590]
.L1805:
	mov	w1, 0
.L1806:
	cmp	w1, w0
	bne	.L1807
	ldr	x1, [x19, 3736]
	ldr	w2, [x21, 4]
	str	w2, [x1, x9]
	ldr	x1, [x20, 2592]
	strh	w28, [x1, x8, lsl 1]
.L1808:
	tbnz	w0, #31, .L1788
	ldrh	w1, [x20, 2564]
	ldrh	w2, [x20, 2590]
	sub	w1, w1, #1
	sub	w1, w1, w2
	cmp	w0, w1, sxth
	bgt	.L1788
	ldr	x1, [x19, 3736]
	add	w2, w2, 1
	strh	w2, [x20, 2590]
	ldr	w2, [x21, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x20, 2592]
	b	.L1850
.L1807:
	ldr	x7, [x19, 3736]
	sxtw	x2, w1
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	x10, x7, x6
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w10, [x10, 4]
	str	w10, [x7, x6]
	ldr	x6, [x20, 2592]
	add	x7, x6, x2
	ldrh	w7, [x7, 2]
	strh	w7, [x6, x2]
	b	.L1806
.L1804:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L1803
.L1789:
	ldrh	w0, [x27]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1810
	strh	w28, [x27]
	str	w10, [x27, 8]
	b	.L1788
.L1810:
	ldrh	w0, [x27, 4]
	cmp	w0, w1
	beq	.L1811
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1811:
	ldr	w0, [x21, 4]
	ldr	w1, [x27, 8]
	cmp	w1, w0
	bcs	.L1812
	ldrh	w0, [x27]
	strh	w0, [x27, 4]
	strh	w28, [x27]
	ldr	w0, [x21, 4]
	str	w0, [x27, 8]
	b	.L1788
.L1812:
	strh	w28, [x27, 4]
	b	.L1788
.L1816:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x1, [x0, 3712]
	ldrh	w2, [x1]
	cbz	w2, .L1818
.L1821:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x1, [x0, 2592]
	ldrh	w2, [x1]
	cbz	w2, .L1819
.L1837:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1818:
	ldrh	w0, [x0, 3856]
	cbz	w0, .L1821
	ldr	w2, [x20, 2572]
	mov	w0, 0
.L1822:
	cmp	w0, w2
	bcs	.L1821
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L1823
	mov	w1, w0
	add	x7, x23, :lo12:.LANCHOR0
	add	x3, x22, :lo12:.LANCHOR2
.L1824:
	ldr	w2, [x7, 2572]
	cmp	w1, w2
	bcs	.L1821
	ldr	x5, [x3, 3712]
	sxtw	x6, w1
	lsl	x4, x6, 1
	sub	w2, w1, w0
	sxtw	x2, w2
	add	w1, w1, 1
	sxth	w1, w1
	ldrh	w8, [x5, x4]
	strh	w8, [x5, x2, lsl 1]
	ldr	x5, [x3, 3768]
	ldr	w6, [x5, x6, lsl 2]
	str	w6, [x5, x2, lsl 2]
	ldr	x2, [x3, 3712]
	strh	wzr, [x2, x4]
	b	.L1824
.L1823:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1822
.L1819:
	ldrh	w2, [x0, 2590]
	cbz	w2, .L1837
	ldrh	w2, [x0, 2564]
	mov	w0, 0
.L1827:
	mov	w5, w0
	cmp	w0, w2
	bge	.L1837
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L1828
	add	x23, x23, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR2
.L1829:
	ldrh	w1, [x23, 2564]
	cmp	w0, w1
	bge	.L1837
	ldr	x3, [x23, 2592]
	sxtw	x4, w0
	lsl	x2, x4, 1
	sub	w1, w0, w5
	sxtw	x1, w1
	add	w0, w0, 1
	sxth	w0, w0
	ldrh	w6, [x3, x2]
	strh	w6, [x3, x1, lsl 1]
	ldr	x3, [x22, 3736]
	ldr	w4, [x3, x4, lsl 2]
	str	w4, [x3, x1, lsl 2]
	ldr	x1, [x23, 2592]
	strh	wzr, [x1, x2]
	b	.L1829
.L1828:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1827
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	sub	sp, sp, #112
	mov	w1, 0
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 64]
	adrp	x23, .LANCHOR4
	stp	x21, x22, [sp, 48]
	add	x19, x23, :lo12:.LANCHOR4
	adrp	x21, .LANCHOR2
	add	x24, x21, :lo12:.LANCHOR2
	add	x19, x19, 1712
	stp	x25, x26, [sp, 80]
	stp	x27, x28, [sp, 96]
	ldr	x0, [x24, 3608]
	str	x0, [x19, 8]
	ldr	x0, [x24, 3656]
	str	x0, [x19, 16]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0, 2480]
	ldr	x0, [x24, 520]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x24, 784]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1853
.L1864:
	mov	w0, -1
.L1852:
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 112
	ret
.L1853:
	add	x25, x24, 784
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	ldrsh	w28, [x24, 784]
	mov	w26, 19539
	sxth	w22, w0
	movk	w26, 0x4654, lsl 16
	add	w0, w0, 1
	strh	w0, [x25, 2]
.L1855:
	tbnz	w22, #31, .L1863
	orr	w0, w22, w28, lsl 10
	add	x27, x20, :lo12:.LANCHOR0
	str	w0, [x19, 4]
	mov	w2, 1
	ldr	x0, [x24, 3608]
	mov	w1, w2
	str	x0, [x19, 8]
	mov	x0, x19
	bl	FlashReadPages
	ldrb	w0, [x27, 72]
	cbz	w0, .L1856
	ldr	x8, [x19, 16]
	ldr	w7, [x8, 12]
	cbz	w7, .L1856
	ldr	x6, [x19, 8]
	ldrh	w1, [x27, 2554]
	mov	x0, x6
	bl	js_hash
	cmp	w7, w0
	beq	.L1856
	cbnz	w22, .L1857
	ldrh	w0, [x25, 4]
	cmp	w28, w0
	beq	.L1857
	ldr	w0, [x6]
	ldrh	w1, [x25]
	str	w0, [sp]
	adrp	x0, .LC108
	add	x0, x0, :lo12:.LC108
	ldp	w4, w5, [x8]
	ldr	w6, [x8, 8]
	ldp	w2, w3, [x19]
	bl	printk
	ldrsh	w28, [x25, 4]
	ldrh	w22, [x27, 2546]
.L1859:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1855
.L1857:
	mov	w0, -1
	str	w0, [x19]
.L1856:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1859
	ldr	x0, [x24, 3608]
	ldr	w0, [x0]
	cmp	w0, w26
	bne	.L1859
	ldr	x0, [x24, 3656]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L1859
.L1863:
	add	x19, x23, :lo12:.LANCHOR4
	add	x24, x21, :lo12:.LANCHOR2
	add	x19, x19, 1712
	add	x22, x20, :lo12:.LANCHOR0
	mov	w2, 48
	add	x0, x24, 456
	ldr	x1, [x19, 8]
	bl	ftl_memcpy
	ldr	x0, [x24, 520]
	ldrh	w2, [x22, 2480]
	ldr	x1, [x19, 8]
	lsl	w2, w2, 1
	add	x1, x1, 48
	bl	ftl_memcpy
	ldrh	w1, [x22, 2480]
	ldr	x0, [x19, 8]
	lsr	w2, w1, 3
	ubfiz	x1, x1, 1, 16
	add	x1, x1, 51
	add	w2, w2, 4
	and	x1, x1, -4
	add	x1, x0, x1
	ldr	x0, [x22, 64]
	bl	ftl_memcpy
	ldrh	w0, [x22, 2588]
	cbz	w0, .L1861
	ldrh	w1, [x22, 2480]
	ldrh	w2, [x22, 2580]
	lsr	w0, w1, 3
	add	w1, w0, w1, lsl 1
	ldr	x0, [x19, 8]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x0, x1
	ldr	x0, [x24, 3760]
	bl	ftl_memcpy
.L1861:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w2, [x0, 456]
	cmp	w2, w1
	bne	.L1864
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w4, [x0, 464]
	ldrb	w5, [x0, 466]
	strh	w4, [x0, 790]
	ldrh	w2, [x1, 2494]
	cmp	w5, w2
	bne	.L1864
	ldrh	w2, [x1, 2544]
	add	x23, x23, :lo12:.LANCHOR4
	ldrh	w5, [x1, 2550]
	add	x3, x0, 512
	strh	wzr, [x0, 802]
	str	w4, [x23, 1768]
	mul	w2, w2, w4
	strb	wzr, [x0, 806]
	str	w2, [x1, 2616]
	strb	wzr, [x0, 808]
	mul	w2, w5, w2
	ldrh	w5, [x1, 2630]
	str	w2, [x1, 2584]
	ldr	w2, [x1, 2484]
	ldrh	w1, [x1, 2472]
	sub	w2, w2, w5
	sub	w2, w2, w4
	ldrh	w4, [x0, 470]
	strh	w4, [x0, 560]
	udiv	w1, w2, w1
	ldrh	w2, [x0, 472]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0, 566]
	ldrb	w2, [x0, 467]
	strb	w2, [x0, 568]
	mov	w2, -1
	strh	w2, [x0, 800]
	ldrh	w2, [x0, 474]
	strh	w2, [x0, 608]
	ldrh	w2, [x0, 476]
	strh	w5, [x0, 562]
	strh	w1, [x0, 780]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0, 614]
	ldrb	w2, [x0, 468]
	strb	w2, [x0, 616]
	ldrh	w2, [x0, 478]
	strh	w2, [x0, 656]
	ldrh	w2, [x0, 480]
	strh	w5, [x0, 610]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strh	w5, [x0, 658]
	strb	w2, [x0, 662]
	ldrb	w2, [x0, 469]
	strb	w2, [x0, 664]
	str	wzr, [x0, 720]
	ldr	w1, [x0, 488]
	ldr	w2, [x0, 752]
	stp	wzr, wzr, [x3, 216]
	stp	wzr, wzr, [x3, 224]
	str	w1, [x0, 760]
	str	wzr, [x0, 764]
	ldr	w1, [x0, 496]
	str	wzr, [x0, 772]
	cmp	w1, w2
	bls	.L1865
	str	w1, [x0, 752]
.L1865:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0, 492]
	ldr	w2, [x0, 756]
	cmp	w1, w2
	bls	.L1866
	str	w1, [x0, 756]
.L1866:
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1867
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, 560
	bl	make_superblock
.L1867:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 608
	ldrh	w2, [x1, 608]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1868
	bl	make_superblock
.L1868:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 656
	ldrh	w2, [x1, 656]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1869
	bl	make_superblock
.L1869:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x21, 800
	ldrh	w2, [x21, 800]
	cmp	w2, w1
	beq	.L1870
	bl	make_superblock
.L1870:
	mov	w0, 0
	b	.L1852
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
	stp	x23, x24, [sp, 64]
	adrp	x23, .LANCHOR0
	add	x20, x23, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 48]
	stp	x25, x26, [sp, 80]
	and	w24, w1, 255
	stp	x27, x28, [sp, 96]
	bl	P2V_block_in_plane
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	and	w22, w0, 65535
	add	x1, x1, 152
	ldrh	w26, [x20, 2544]
	adrp	x0, .LC109
	add	x0, x0, :lo12:.LC109
	bl	printk
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x0, x22, 1, 16
	add	x25, x29, 144
	ldr	x1, [x1, 520]
	ldrh	w2, [x1, x0]
	mov	w1, w22
	adrp	x0, .LC110
	add	x0, x0, :lo12:.LC110
	bl	printk
	strh	w22, [x25, -48]!
	mov	x0, x25
	bl	make_superblock
	ldrb	w0, [x20, 204]
	cbz	w0, .L1891
	cbnz	w24, .L1891
	mov	w0, w22
	bl	ftl_get_blk_mode
	mov	w22, w0
	cmp	w0, 1
	bne	.L1882
	ldrh	w26, [x20, 2546]
.L1882:
	add	x0, x23, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR2
	mov	w21, 0
	mov	w27, 56
	mov	w2, w26
	mov	w1, w22
	ldrh	w3, [x0, 2544]
	adrp	x0, .LC111
	add	x0, x0, :lo12:.LC111
	bl	printk
.L1883:
	add	x0, x23, :lo12:.LANCHOR0
	add	x5, x25, 16
	mov	w20, 0
	mov	w10, 65535
	mov	w6, 4
	ldrh	w7, [x0, 2472]
	ldrh	w8, [x0, 2554]
	ldrh	w9, [x0, 2556]
	mov	w0, 0
.L1884:
	cmp	w0, w7
	bne	.L1886
	ldr	x0, [x19, 3584]
	mov	w1, w20
	adrp	x28, .LC112
	mov	w2, w22
	umull	x20, w20, w27
	mov	x24, 0
	add	x28, x28, :lo12:.LC112
	bl	FlashReadPages
.L1887:
	cmp	x24, x20
	bne	.L1888
	add	w21, w21, 1
	and	w21, w21, 65535
	cmp	w26, w21
	bne	.L1883
.L1889:
	ldp	x19, x20, [sp, 32]
	mov	w0, 0
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 160
	ret
.L1891:
	mov	w22, 0
	b	.L1882
.L1886:
	ldrh	w1, [x5]
	cmp	w1, w10
	beq	.L1885
	umull	x4, w20, w27
	ldr	x3, [x19, 3584]
	orr	w1, w21, w1, lsl 10
	add	x3, x3, x4
	str	w1, [x3, 4]
	mul	w1, w20, w8
	ldr	x2, [x19, 3584]
	ldr	x3, [x19, 1392]
	sdiv	w1, w1, w6
	add	x2, x2, x4
	add	x1, x3, w1, sxtw 2
	str	x1, [x2, 8]
	mul	w1, w20, w9
	ldr	x3, [x19, 1400]
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w1, w1, w6
	add	x1, x3, w1, sxtw 2
	str	x1, [x2, 16]
.L1885:
	add	w0, w0, 1
	add	x5, x5, 2
	and	w0, w0, 65535
	b	.L1884
.L1888:
	ldr	x8, [x19, 3584]
	ldrh	w1, [x29, 96]
	add	x2, x8, x24
	ldp	x3, x0, [x2, 8]
	ldr	w4, [x3, 4]
	str	w4, [sp, 8]
	ldr	w3, [x3]
	str	w3, [sp]
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x28
	ldr	w3, [x2, 4]
	ldr	w2, [x8, x24]
	add	x24, x24, 56
	bl	printk
	b	.L1887
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
	.align	2
	.global	FtlScanAllBlock
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	sub	sp, sp, #144
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC109
	add	x1, x1, 176
	add	x0, x0, :lo12:.LC109
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x21, x22, [sp, 48]
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 64]
	add	x22, x22, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR2
	add	x21, x24, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 32]
	mov	w20, 0
	stp	x25, x26, [sp, 80]
	bl	printk
.L1895:
	ldrh	w0, [x22, 2482]
	cmp	w0, w20
	bhi	.L1903
	ldp	x19, x20, [sp, 32]
	mov	w0, 0
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 144
	ret
.L1903:
	add	x19, x29, 128
	mov	w0, w20
	strh	w20, [x19, -48]!
	bl	ftl_get_blk_mode
	add	x2, x24, :lo12:.LANCHOR2
	ubfiz	x1, x20, 1, 16
	mov	w3, w0
	adrp	x0, .LC113
	add	x0, x0, :lo12:.LC113
	ldr	x2, [x2, 520]
	ldrh	w2, [x2, x1]
	mov	w1, w20
	bl	printk
	mov	x0, x19
	bl	make_superblock
	ldrh	w7, [x22, 2472]
	add	x0, x19, 16
	ldrh	w8, [x22, 2554]
	mov	w19, 0
	ldrh	w9, [x22, 2556]
	mov	w1, 0
	mov	w10, 65535
	mov	w11, 56
	mov	w5, 4
.L1896:
	cmp	w1, w7
	bne	.L1898
	ldr	x0, [x21, 3584]
	mov	w25, 56
	adrp	x26, .LC114
	mov	w2, 0
	mov	w1, w19
	umull	x25, w19, w25
	mov	x23, 0
	add	x26, x26, :lo12:.LC114
	bl	FlashReadPages
.L1899:
	cmp	x23, x25
	bne	.L1900
	ldr	x0, [x21, 3584]
	mov	w1, w19
	adrp	x25, .LC115
	mov	w2, 1
	mov	x19, 0
	add	x25, x25, :lo12:.LC115
	bl	FlashReadPages
.L1901:
	cmp	x23, x19
	bne	.L1902
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1895
.L1898:
	ldrh	w2, [x0]
	cmp	w2, w10
	beq	.L1897
	umull	x6, w19, w11
	ldr	x4, [x21, 3584]
	lsl	w2, w2, 10
	add	x4, x4, x6
	str	w2, [x4, 4]
	mul	w2, w19, w8
	ldr	x3, [x21, 3584]
	ldr	x4, [x21, 1392]
	sdiv	w2, w2, w5
	add	x3, x3, x6
	add	x2, x4, w2, sxtw 2
	str	x2, [x3, 8]
	mul	w2, w19, w9
	ldr	x4, [x21, 1400]
	add	w19, w19, 1
	and	w19, w19, 65535
	sdiv	w2, w2, w5
	add	x2, x4, w2, sxtw 2
	str	x2, [x3, 16]
.L1897:
	add	w1, w1, 1
	add	x0, x0, 2
	and	w1, w1, 65535
	b	.L1896
.L1900:
	ldr	x8, [x21, 3584]
	ldrh	w1, [x29, 80]
	add	x2, x8, x23
	ldp	x3, x0, [x2, 8]
	ldr	w4, [x3, 4]
	str	w4, [sp, 8]
	ldr	w3, [x3]
	str	w3, [sp]
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x26
	ldr	w3, [x2, 4]
	ldr	w2, [x8, x23]
	add	x23, x23, 56
	bl	printk
	b	.L1899
.L1902:
	ldr	x8, [x21, 3584]
	ldrh	w1, [x29, 80]
	add	x2, x8, x19
	ldp	x3, x0, [x2, 8]
	ldr	w4, [x3, 4]
	str	w4, [sp, 8]
	ldr	w3, [x3]
	str	w3, [sp]
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x25
	ldr	w3, [x2, 4]
	ldr	w2, [x8, x19]
	add	x19, x19, 56
	bl	printk
	b	.L1901
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	2
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	sub	sp, sp, #96
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 80]
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	add	x19, x19, 1712
	stp	x21, x22, [sp, 48]
	ldrh	w26, [x20, 2542]
	stp	x23, x24, [sp, 64]
	sub	w26, w26, #16
	lsl	w25, w26, 10
.L1906:
	ldrh	w0, [x20, 2542]
	cmp	w26, w0
	blt	.L1914
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 192
	adrp	x0, .LC119
	add	x0, x0, :lo12:.LC119
	bl	printk
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 96
	ret
.L1914:
	and	w21, w26, 65535
	mov	w0, w21
	bl	ftl_get_blk_mode
	ldrb	w1, [x20, 204]
	cbz	w1, .L1907
	ldrh	w1, [x20, 2480]
	cmp	w26, w1
	bge	.L1908
	ldrh	w1, [x20, 2558]
	cmp	w26, w1
	blt	.L1908
.L1907:
	cmp	w0, 1
	bne	.L1909
.L1908:
	ldrh	w23, [x20, 2546]
	mov	w24, -2147483648
.L1910:
	mov	w3, w24
	mov	w2, w23
	mov	w1, w26
	adrp	x0, .LC116
	add	x0, x0, :lo12:.LC116
	bl	printk
	mov	w0, w21
	bl	FtlBbmIsBadBlock
	cbz	w0, .L1911
	adrp	x0, .LC117
	mov	w3, w24
	mov	w2, w23
	mov	w1, w26
	add	x0, x0, :lo12:.LC117
	bl	printk
.L1911:
	adrp	x22, .LANCHOR2
	add	x22, x22, :lo12:.LANCHOR2
	mov	w21, 0
.L1912:
	cmp	w21, w23
	bne	.L1913
	add	w26, w26, 1
	add	w25, w25, 1024
	b	.L1906
.L1909:
	ldrh	w23, [x20, 2544]
	mov	w24, 0
	b	.L1910
.L1913:
	add	w0, w24, w25
	mov	w2, 0
	add	w0, w0, w21
	stp	wzr, w0, [x19]
	ldr	x0, [x22, 3608]
	mov	w1, 1
	str	x0, [x19, 8]
	add	w21, w21, 1
	ldr	x0, [x22, 3656]
	str	x0, [x19, 16]
	mov	x0, x19
	bl	FlashReadPages
	ldp	x1, x0, [x19, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC118
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC118
	ldp	w2, w1, [x19]
	bl	printk
	b	.L1912
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	stp	x29, x30, [sp, -176]!
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	add	x21, x4, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	str	x4, [x29, 96]
	ldr	x5, [x21, 88]
	ldrb	w6, [x21, 72]
	str	w3, [x29, 108]
	ldrb	w5, [x5, 19]
	cbnz	w6, .L1923
	mov	x19, x0
	adrp	x0, .LANCHOR1+481
	mov	w25, w1
	mov	w23, w2
	add	x27, x21, 2164
	ldrb	w28, [x0, #:lo12:.LANCHOR1+481]
	mov	w22, 0
	sub	w0, w5, #1
	str	w0, [x29, 104]
.L1924:
	cmp	w22, w25
	bcc	.L1937
	ldr	x0, [x29, 96]
	mov	x20, 0
	mov	x24, 24
	add	x21, x0, :lo12:.LANCHOR0
	add	x22, x21, 2164
.L1938:
	ldrb	w0, [x21, 2358]
	cmp	w0, w20
	bhi	.L1940
	ldr	w0, [x29, 108]
	cbnz	w0, .L1941
.L1949:
	mov	w0, 0
	b	.L1922
.L1923:
	bl	FlashProgSlc2KPages
.L1922:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1937:
	mov	w12, 56
	mov	w1, w23
	sub	w4, w25, w22
	add	x3, x29, 116
	umull	x12, w22, w12
	add	x2, x29, 112
	add	x26, x19, x12
	mov	x0, x26
	bl	LogAddr2PhyAddr
	mov	w24, w0
	ldrb	w1, [x21, 2358]
	ldr	w0, [x29, 116]
	cmp	w1, w0
	bhi	.L1926
	mov	w0, -1
	str	w0, [x19, x12]
.L1927:
	add	w22, w22, 1
	b	.L1924
.L1926:
	ldrb	w1, [x21, 2368]
	mov	x2, 24
	cmp	w1, 0
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x1, x1, x2, x27
	ldr	x1, [x1, 8]
	cbz	x1, .L1929
	bl	FlashWaitCmdDone
.L1929:
	ldp	w2, w1, [x29, 112]
	mov	x0, 24
	madd	x0, x1, x0, x27
	str	w2, [x0, 4]
	stp	x26, xzr, [x0, 8]
	cbz	w24, .L1930
	add	w2, w22, 1
	mov	w3, 56
	umaddl	x2, w2, w3, x19
	str	x2, [x0, 16]
.L1930:
	add	x0, x21, x1
	ldrb	w20, [x0, 2360]
	mov	x0, 24
	mul	x1, x1, x0
	ldrb	w0, [x21, 2358]
	cmp	w0, 1
	mov	w0, w20
	strb	w20, [x27, x1]
	bne	.L1931
	bl	NandcWaitFlashReady
.L1932:
	ldr	w0, [x29, 104]
	cmp	w0, 7
	bhi	.L1933
	add	x0, x21, w20, sxtw
	ldrb	w0, [x0, 2128]
	cbz	w0, .L1933
	ldrb	w1, [x21, 1273]
	mov	w3, 0
	add	x2, x21, 1276
	mov	w0, w20
	bl	HynixSetRRPara
.L1933:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1934
	ldrb	w0, [x21, 204]
	cbz	w0, .L1934
	mov	w0, w20
	bl	flash_enter_slc_mode
.L1935:
	ldr	w1, [x29, 112]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldp	x3, x4, [x26, 8]
	mov	w2, w28
	mov	w1, 1
	mov	w0, w20
	bl	NandcXferData
	cbz	w24, .L1936
	ldr	w1, [x29, 112]
	mov	w0, w20
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29, 116]
	add	x0, x21, 1232
	ldr	w0, [x0, x1, lsl 2]
	ldr	w1, [x29, 112]
	cmp	w0, 0
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w0, [x21, 76]
	ldr	w1, [x29, 112]
	add	w1, w1, w0
	mov	w0, w20
	bl	FlashProgDpSecondCmd
	add	w0, w22, 1
	mov	w1, 56
	mov	w2, w28
	umaddl	x0, w0, w1, x19
	mov	w1, 1
	ldp	x3, x4, [x0, 8]
	mov	w0, w20
	bl	NandcXferData
.L1936:
	ldr	w1, [x29, 112]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
	b	.L1927
.L1931:
	bl	NandcFlashCs
	ldr	w1, [x29, 116]
	add	x0, x21, 1232
	ldr	w0, [x0, x1, lsl 2]
	ldr	w1, [x29, 112]
	cmp	w0, 0
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
	b	.L1932
.L1934:
	mov	w0, w20
	bl	flash_exit_slc_mode
	b	.L1935
.L1940:
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1939
	ldrb	w0, [x21, 204]
	cbz	w0, .L1939
	mul	x0, x20, x24
	ldrb	w0, [x0, x22]
	bl	flash_exit_slc_mode
.L1939:
	add	x20, x20, 1
	b	.L1938
.L1941:
	mov	w0, 56
	and	w20, w25, 255
	adrp	x22, .LANCHOR4
	add	x24, x22, :lo12:.LANCHOR4
	umaddl	x25, w25, w0, x19
.L1942:
	cmp	x25, x19
	beq	.L1949
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1943
	ldr	w1, [x19, 4]
	adrp	x0, .LC103
	add	x0, x0, :lo12:.LC103
	bl	printk
.L1944:
	sub	w20, w20, #1
	add	x19, x19, 56
	and	w20, w20, 255
	b	.L1942
.L1943:
	add	x21, x22, :lo12:.LANCHOR4
	mov	w4, w20
	add	x3, x29, 116
	add	x2, x29, 112
	mov	w1, w23
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x21, 1672]
	mov	x2, 56
	mov	x1, x19
	str	wzr, [x0]
	ldr	x0, [x21, 1680]
	str	wzr, [x0]
	add	x0, x29, 120
	bl	memcpy
	ldr	x0, [x21, 1672]
	mov	w2, w23
	str	x0, [x29, 128]
	mov	w1, 1
	ldr	x0, [x21, 1680]
	str	x0, [x29, 136]
	add	x0, x29, 120
	bl	FlashReadPages
	ldr	w21, [x29, 120]
	cmn	w21, #1
	bne	.L1945
	ldr	w1, [x19, 4]
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	bl	printk
	str	w21, [x19]
.L1945:
	ldr	x0, [x19, 16]
	cbz	x0, .L1946
	ldr	w2, [x0]
	ldr	x0, [x24, 1680]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1946
	ldr	w1, [x19, 4]
	adrp	x0, .LC106
	add	x0, x0, :lo12:.LC106
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1946:
	ldr	x0, [x19, 8]
	cbz	x0, .L1944
	ldr	w2, [x0]
	ldr	x0, [x24, 1672]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1944
	ldr	w1, [x19, 4]
	adrp	x0, .LC107
	add	x0, x0, :lo12:.LC107
	bl	printk
	mov	w0, -1
	str	w0, [x19]
	b	.L1944
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FlashTestBlk.part.18, %function
FlashTestBlk.part.18:
	stp	x29, x30, [sp, -160]!
	mov	w2, 32
	mov	w1, 165
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	and	w20, w0, 65535
	lsl	w20, w20, 10
	ldr	x0, [x19, 1656]
	str	x0, [x29, 48]
	add	x0, x29, 96
	str	x0, [x29, 56]
	bl	ftl_memset
	ldr	x0, [x19, 1656]
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
	.size	FlashTestBlk.part.18, .-FlashTestBlk.part.18
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	adrp	x1, .LANCHOR4+1644
	and	w0, w0, 65535
	ldr	w1, [x1, #:lo12:.LANCHOR4+1644]
	cmp	w0, w1
	bcc	.L1979
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FlashTestBlk.part.18
	ldp	x29, x30, [sp], 16
	ret
.L1979:
	mov	w0, 0
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	stp	x29, x30, [sp, -240]!
	adrp	x1, .LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR4
	add	x0, x21, :lo12:.LANCHOR4
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	mov	w19, 0
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x0, [x0, 1664]
	stp	x0, x1, [x29, 152]
	add	x0, x1, :lo12:.LANCHOR0
	ldrh	w2, [x0, 188]
	ldrh	w22, [x0, 190]
	mul	w22, w22, w2
	ldr	x2, [x0, 88]
	and	w22, w22, 65535
	ldrb	w2, [x2, 24]
	str	w2, [x29, 128]
	ldrh	w2, [x0, 76]
	ldrb	w0, [x0, 72]
	uxtw	x1, w2
	cmp	w0, 1
	ubfiz	w2, w2, 1, 15
	csel	w0, w2, w1, eq
	mov	w1, 1
	str	w0, [x29, 168]
	adrp	x0, .LC120
	add	x0, x0, :lo12:.LC120
	bl	printk
	add	x0, x21, :lo12:.LANCHOR4
	mov	w2, 4096
	mov	w1, 0
	ldr	x0, [x0, 1664]
	bl	ftl_memset
	lsr	w0, w22, 4
	str	w0, [x29, 148]
	sub	w0, w22, #1
	and	w0, w0, 65535
	str	w0, [x29, 144]
	adrp	x0, .LC123
	add	x0, x0, :lo12:.LC123
	str	x0, [x29, 104]
.L1986:
	ldr	x0, [x29, 160]
	add	x20, x0, :lo12:.LANCHOR0
	ldrb	w0, [x20, 2358]
	cmp	w0, w19
	bhi	.L2013
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 240
	ret
.L2013:
	add	x0, x21, :lo12:.LANCHOR4
	sxtw	x24, w19
	add	x1, x0, 1696
	ldrh	w1, [x1, x24, lsl 1]
	str	w1, [x29, 172]
	cbnz	w1, .L1987
	ldr	x0, [x0, 1608]
	add	x28, x20, 1232
	ldrh	w2, [x20, 196]
	mov	w23, 0
	mov	w25, 0
	mov	w26, 0
	lsl	w2, w2, 9
	bl	ftl_memset
	add	x0, x20, x24
	ldrb	w27, [x0, 2360]
	ldr	w0, [x29, 128]
	and	w0, w0, 1
	str	w0, [x29, 100]
.L1988:
	and	w0, w26, 65535
	str	w0, [x29, 112]
	cmp	w0, w22
	bcc	.L1999
.L1998:
	mov	w2, w23
	mov	w1, w19
	adrp	x0, .LC122
	add	x0, x0, :lo12:.LC122
	bl	printk
	ldr	x0, [x29, 160]
	ldr	w2, [x29, 148]
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w0, [x1, 2358]
	mul	w0, w0, w2
	cmp	w23, w0
	blt	.L2000
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w2, [x1, 196]
	mov	w1, 0
	ldr	x0, [x0, 1608]
	lsl	w2, w2, 9
	bl	ftl_memset
.L2000:
	cbnz	w19, .L2002
	add	x20, x21, :lo12:.LANCHOR4
	ldr	x0, [x29, 160]
	mov	w23, 1
	add	x25, x0, :lo12:.LANCHOR0
	ldrh	w26, [x20, 1644]
.L2003:
	ldrb	w0, [x25, 73]
	cmp	w0, w26
	bhi	.L2005
	ldr	w26, [x29, 144]
	sub	w25, w22, #50
	add	x20, x21, :lo12:.LANCHOR4
	mov	w23, 1
.L2006:
	cmp	w26, w25
	bgt	.L2008
	ldr	x0, [x29, 160]
	add	x3, x21, :lo12:.LANCHOR4
	add	x1, x0, :lo12:.LANCHOR0
	ldr	w2, [x3, 1644]
	ldrb	w0, [x1, 73]
	sub	w0, w0, w2
	ldr	w2, [x29, 172]
	cmp	w2, w0
	bcc	.L2002
	ldr	x0, [x3, 1608]
	ldrh	w2, [x1, 196]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L2002:
	add	x27, x21, :lo12:.LANCHOR4
	mul	w25, w19, w22
	ldr	w26, [x29, 144]
	adrp	x23, .LC124
	add	x20, x27, 1696
	add	x23, x23, :lo12:.LC124
.L2010:
	mov	w1, w19
	mov	w2, w26
	mov	x0, x23
	bl	printk
	ldr	x1, [x27, 1608]
.L2011:
	ubfx	x0, x26, 5, 11
	ldr	w0, [x1, x0, lsl 2]
	lsr	w0, w0, w26
	tbnz	x0, 0, .L2012
	ldr	x1, [x29, 152]
	mov	w0, -3872
	strh	w26, [x20, x24, lsl 1]
	mov	w2, 1
	strh	w0, [x1]
	strh	w26, [x1, 2]
	strh	wzr, [x1, 8]
	mov	w1, w2
	ldr	x0, [x27, 1608]
	str	x0, [x29, 192]
	ldr	x0, [x27, 1664]
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
	cbz	w0, .L1987
	sub	w26, w26, #1
	and	w26, w26, 65535
	b	.L2010
.L1999:
	mov	w0, -1
	strb	w0, [x29, 182]
	strb	w0, [x29, 183]
	ldr	w0, [x29, 100]
	cbz	w0, .L1990
	ldr	w3, [x28, x24, lsl 2]
	mov	w0, w27
	add	x2, x29, 182
	add	w3, w25, w3
	str	w3, [x29, 96]
	mov	w1, w3
	bl	FlashReadSpare
	ldrb	w0, [x20, 72]
	ldr	w3, [x29, 96]
	cmp	w0, 1
	bne	.L1990
	ldr	w1, [x20, 76]
	mov	w0, w27
	add	x2, x29, 183
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29, 182]
	ldrb	w1, [x29, 183]
	and	w0, w0, w1
	strb	w0, [x29, 182]
.L1990:
	ldr	x0, [x29, 128]
	tbz	x0, 1, .L1992
	ldr	x0, [x20, 88]
	add	x2, x29, 183
	ldrh	w1, [x0, 10]
	ldr	w0, [x28, x24, lsl 2]
	sub	w1, w1, #1
	add	w0, w25, w0
	add	w1, w1, w0
	mov	w0, w27
	bl	FlashReadSpare
.L1992:
	ldr	x1, [x20, 88]
	ldrb	w0, [x1, 7]
	cmp	w0, 1
	ccmp	w0, 8, 4, ne
	ldrb	w0, [x29, 182]
	beq	.L1993
	ldrb	w1, [x1, 18]
	cmp	w1, 12
	bne	.L1994
.L1993:
	cbz	w0, .L2015
	ldrb	w0, [x29, 183]
	cmp	w0, 0
	cset	w0, eq
.L1995:
	ldr	x1, [x29, 128]
	tbz	x1, 2, .L1996
	ldr	w1, [x28, x24, lsl 2]
	mov	w0, w27
	add	w1, w25, w1
	bl	SandiskProgTestBadBlock
.L1996:
	cbz	w0, .L1997
	mov	w2, w26
	mov	w1, w19
	adrp	x0, .LC121
	add	x0, x0, :lo12:.LC121
	bl	printk
	add	w23, w23, 1
	add	x1, x21, :lo12:.LANCHOR4
	ldr	x0, [x29, 112]
	ldrb	w2, [x29, 112]
	and	w23, w23, 65535
	ldr	x3, [x1, 1608]
	ubfx	x0, x0, 5, 11
	lsl	x0, x0, 2
	mov	w1, 1
	lsl	w2, w1, w2
	ldr	w1, [x3, x0]
	orr	w1, w1, w2
	str	w1, [x3, x0]
	ldr	w1, [x29, 148]
	ldrb	w0, [x20, 2358]
	mul	w0, w0, w1
	cmp	w23, w0
	bgt	.L1998
.L1997:
	ldr	w0, [x29, 168]
	add	w26, w26, 1
	add	w25, w25, w0
	b	.L1988
.L1994:
	cmp	w0, 255
	bne	.L2015
	ldrb	w0, [x29, 183]
	cmp	w0, 255
	cset	w0, ne
	b	.L1995
.L2015:
	mov	w0, 1
	b	.L1995
.L2005:
	mov	w0, w26
	bl	FlashTestBlk
	cbz	w0, .L2004
	ldr	x0, [x29, 104]
	mov	w1, w26
	bl	printk
	ldr	x3, [x20, 1608]
	ubfx	x0, x26, 5, 11
	lsl	x0, x0, 2
	lsl	w1, w23, w26
	ldr	w2, [x3, x0]
	orr	w1, w2, w1
	str	w1, [x3, x0]
	ldr	w0, [x29, 172]
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w0, [x29, 172]
.L2004:
	add	w26, w26, 1
	and	w26, w26, 65535
	b	.L2003
.L2008:
	mov	w0, w26
	bl	FlashTestBlk
	cbz	w0, .L2007
	ldr	x0, [x29, 104]
	mov	w1, w26
	bl	printk
	ldr	x3, [x20, 1608]
	ubfx	x0, x26, 5, 11
	lsl	x0, x0, 2
	lsl	w1, w23, w26
	ldr	w2, [x3, x0]
	orr	w1, w2, w1
	str	w1, [x3, x0]
.L2007:
	sub	w26, w26, #1
	and	w26, w26, 65535
	b	.L2006
.L2012:
	sub	w26, w26, #1
	and	w26, w26, 65535
	b	.L2011
.L1987:
	add	w19, w19, 1
	and	w19, w19, 255
	b	.L1986
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x5, x22, :lo12:.LANCHOR2
	stp	x25, x26, [sp, 64]
	stp	x19, x20, [sp, 16]
	and	w26, w0, 65535
	stp	x23, x24, [sp, 48]
	stp	x27, x28, [sp, 80]
	ldr	w0, [x5, 424]
	cbnz	w0, .L2058
	adrp	x20, .LANCHOR0
	add	x24, x20, :lo12:.LANCHOR0
	and	w25, w1, 255
	add	x9, x24, 2504
	mov	w6, 0
	mov	w21, 0
	ldrb	w8, [x24, 204]
	mov	w19, 0
	ldrb	w0, [x24, 2368]
	mov	w7, 56
	mov	w10, 4
	str	w26, [x5, 3572]
	str	w0, [x29, 132]
.L2037:
	ldrh	w0, [x24, 2472]
	cmp	w0, w6
	bhi	.L2041
	cbz	w21, .L2035
	add	x27, x22, :lo12:.LANCHOR2
	cmp	w8, 0
	cset	w23, ne
	strb	wzr, [x24, 2368]
	mov	w2, w21
	mov	w1, w23
	ldr	x0, [x27, 432]
	bl	FlashEraseBlocks
	ldrb	w0, [x29, 132]
	strb	w0, [x24, 2368]
	mov	x24, 0
	mov	w0, 56
	umull	x21, w21, w0
.L2044:
	ldr	x0, [x27, 432]
	add	x1, x0, x24
	ldr	w0, [x0, x24]
	cmn	w0, #1
	bne	.L2043
	ldr	w0, [x1, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.L2043:
	add	x24, x24, 56
	cmp	x21, x24
	bne	.L2044
	cbnz	w25, .L2045
	and	w23, w23, 65535
	mov	w0, 1
	mov	w28, 6
	str	w0, [x29, 136]
.L2046:
	add	x20, x20, :lo12:.LANCHOR0
	add	x27, x22, :lo12:.LANCHOR2
	add	x0, x20, 2504
	mov	w24, 0
	str	x0, [x29, 120]
.L2054:
	mov	w5, 0
	mov	w21, 0
	mov	w6, 56
	mov	w7, 4
.L2047:
	ldrh	w0, [x20, 2472]
	cmp	w0, w5
	bhi	.L2050
	cbz	w21, .L2035
	ldr	x0, [x27, 432]
	mov	w2, w23
	mov	w1, w21
	strb	wzr, [x20, 2368]
	mov	w3, 1
	bl	FlashProgPages
	mov	w1, 56
	ldrb	w0, [x29, 132]
	strb	w0, [x20, 2368]
	mov	x2, 0
	umull	x1, w21, w1
.L2053:
	ldr	x0, [x27, 432]
	add	x3, x0, x2
	ldr	w0, [x0, x2]
	cbz	w0, .L2052
	ldr	w0, [x3, 4]
	add	w19, w19, 1
	stp	x2, x1, [x29, 104]
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldp	x2, x1, [x29, 104]
.L2052:
	add	x2, x2, 56
	cmp	x1, x2
	bne	.L2053
	add	w24, w24, w28
	ldr	w0, [x29, 136]
	and	w24, w24, 65535
	cmp	w0, w24
	bhi	.L2054
	add	x24, x22, :lo12:.LANCHOR2
	mov	x20, 0
.L2056:
	cbz	w25, .L2055
	ldr	x0, [x24, 432]
	add	x1, x0, x20
	ldr	w0, [x0, x20]
	cbnz	w0, .L2055
	ldr	w0, [x1, 4]
	mov	w1, 1
	str	x2, [x29, 136]
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
	ldr	x2, [x29, 136]
.L2055:
	add	x20, x20, 56
	cmp	x20, x2
	bne	.L2056
	cmp	w26, 63
	ccmp	w25, 0, 0, hi
	beq	.L2035
	add	x22, x22, :lo12:.LANCHOR2
	mov	w2, w21
	mov	w1, w23
	ldr	x0, [x22, 432]
	bl	FlashEraseBlocks
.L2035:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L2041:
	umull	x0, w6, w7
	ldr	x1, [x5, 432]
	str	wzr, [x1, x0]
	mov	w1, w26
	ldrb	w0, [x9, w6, sxtw]
	bl	V2P_block
	and	w11, w0, 65535
	mov	w12, w11
	cbz	w25, .L2038
	bl	IsBlkInVendorPart
	cbnz	w0, .L2039
.L2038:
	mov	w0, w12
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2040
	umull	x2, w21, w7
	ldr	x0, [x5, 432]
	lsl	w11, w11, 10
	add	x0, x0, x2
	str	w11, [x0, 4]
	ldrh	w0, [x24, 2556]
	ldr	x1, [x5, 432]
	add	x1, x1, x2
	ldr	x2, [x5, 3664]
	mul	w0, w0, w21
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w0, w0, w10
	add	x0, x2, w0, sxtw 2
	stp	xzr, x0, [x1, 8]
.L2039:
	add	w6, w6, 1
	and	w6, w6, 65535
	b	.L2037
.L2040:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L2039
.L2045:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2546]
	ldrb	w0, [x0, 204]
	str	w1, [x29, 136]
	cbnz	w0, .L2059
	uxtw	x0, w1
	mov	w23, 1
	lsr	w28, w0, 2
	b	.L2046
.L2059:
	mov	w23, 1
	mov	w28, w23
	b	.L2046
.L2050:
	umull	x0, w5, w6
	ldr	x1, [x27, 432]
	str	wzr, [x1, x0]
	mov	w1, w26
	ldr	x0, [x29, 120]
	ldrb	w0, [x0, w5, sxtw]
	bl	V2P_block
	and	w8, w0, 65535
	mov	w9, w8
	cbz	w25, .L2048
	bl	IsBlkInVendorPart
	cbnz	w0, .L2049
.L2048:
	mov	w0, w9
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2049
	umull	x2, w21, w6
	ldr	x0, [x27, 432]
	add	w8, w24, w8, lsl 10
	add	x0, x0, x2
	str	w8, [x0, 4]
	ldr	x1, [x27, 432]
	ldr	x0, [x27, 3640]
	add	x1, x1, x2
	ldr	x2, [x27, 3648]
	str	x0, [x1, 8]
	ldrh	w0, [x20, 2556]
	mul	w0, w0, w21
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w0, w0, w7
	add	x0, x2, w0, sxtw 2
	str	x0, [x1, 16]
.L2049:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L2047
.L2058:
	mov	w19, 0
	b	.L2035
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x19, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w0, [x19, 424]
	cbnz	w0, .L2082
	adrp	x20, .LANCHOR4
	adrp	x23, .LANCHOR0
	add	x25, x20, :lo12:.LANCHOR4
	add	x21, x23, :lo12:.LANCHOR0
	ldr	x2, [x19, 3656]
	mov	w1, 0
	ldr	x0, [x19, 3608]
	add	x26, x21, 2656
	str	x2, [x25, 1728]
	mov	w24, 0
	ldrh	w2, [x21, 2554]
	str	x0, [x25, 1720]
	bl	ftl_memset
.L2083:
	ldrh	w0, [x21, 2494]
	cmp	w24, w0
	blt	.L2084
	add	x20, x20, :lo12:.LANCHOR4
	add	x19, x21, 2624
	mov	w2, 16
	mov	w1, 255
	adrp	x26, .LC125
	add	x23, x23, :lo12:.LANCHOR0
	ldr	x28, [x20, 1728]
	add	x26, x26, :lo12:.LC125
	mov	w24, 0
	mov	x0, x28
	bl	ftl_memset
	mov	w0, -3887
	strh	w0, [x28]
	ldr	w0, [x19, 8]
	str	w0, [x28, 4]
	ldrh	w0, [x21, 2624]
	strh	w0, [x28, 2]
	ldrh	w0, [x19, 4]
	strh	w0, [x28, 8]
	ldrh	w0, [x19, 6]
	strh	w0, [x28, 10]
	ldr	w0, [x21, 2468]
	mov	w21, 0
	strh	w0, [x28, 12]
.L2085:
	add	x25, x22, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldrh	w2, [x19, 2]
	ldrh	w3, [x19, 4]
	ldr	x0, [x25, 3608]
	str	x0, [x20, 1720]
	ldr	x0, [x25, 3656]
	str	x0, [x20, 1728]
	orr	w0, w2, w1, lsl 10
	str	wzr, [x20, 1712]
	ldrh	w4, [x28, 10]
	str	w0, [x20, 1716]
	mov	x0, x26
	bl	printk
	ldrh	w0, [x23, 2546]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2086
	ldr	w0, [x19, 8]
	mov	w2, 1
	ldrh	w1, [x19]
	add	w0, w0, 1
	str	w0, [x19, 8]
	str	w0, [x28, 4]
	strh	w1, [x28, 8]
	ldrh	w0, [x19, 4]
	strh	w1, [x19, 4]
	ldr	x1, [x25, 432]
	strh	w0, [x19]
	lsl	w0, w0, 10
	str	w0, [x20, 1716]
	strh	wzr, [x19, 2]
	str	w0, [x1, 4]
	mov	w1, w2
	ldr	x0, [x25, 432]
	bl	FlashEraseBlocks
.L2086:
	add	x25, x20, 1712
	mov	w3, 1
	mov	x0, x25
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	ldr	w0, [x20, 1712]
	cmn	w0, #1
	bne	.L2087
	ldr	w1, [x20, 1716]
	add	w21, w21, 1
	adrp	x0, .LC126
	and	w21, w21, 65535
	add	x0, x0, :lo12:.LC126
	bl	printk
	cmp	w21, 3
	bls	.L2085
	ldr	w1, [x20, 1716]
	add	x22, x22, :lo12:.LANCHOR2
	mov	w2, w21
	adrp	x0, .LC127
	add	x0, x0, :lo12:.LC127
	bl	printk
	mov	w0, 1
	str	w0, [x22, 424]
.L2082:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L2084:
	ldrh	w2, [x19, 3784]
	ldr	x1, [x26], 8
	ldr	x0, [x25, 1720]
	mul	w3, w2, w24
	lsl	w2, w2, 2
	add	w24, w24, 1
	add	x0, x0, w3, sxtw 2
	bl	ftl_memcpy
	b	.L2083
.L2090:
	mov	w24, 1
	b	.L2085
.L2087:
	add	w24, w24, 1
	cmp	w24, 1
	ble	.L2090
	cmp	w0, 256
	bne	.L2082
	b	.L2085
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x19, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w1, [x19, 424]
	cbnz	w1, .L2096
	adrp	x23, .LANCHOR0
	mov	x20, x0
	add	x24, x19, 656
	add	x21, x23, :lo12:.LANCHOR0
.L2097:
	cmp	x20, x24
	bne	.L2098
	ldrh	w2, [x19, 552]
	ldr	w3, [x19, 1376]
	lsr	w0, w2, 1
	add	w4, w0, 1
	mul	w1, w2, w3
	add	w1, w4, w1, lsr 2
	ldr	w4, [x21, 2372]
	and	w1, w1, 65535
	cbz	w4, .L2099
	ldr	w4, [x19, 776]
	cmp	w4, 39
	bhi	.L2099
	cmp	w4, 2
	bls	.L2126
	tbz	x2, 0, .L2122
	cbz	w3, .L2126
.L2122:
	mov	w1, w0
	b	.L2099
.L2098:
	ldrb	w0, [x20, 8]
	cmp	w0, 1
	bne	.L2126
	ldrh	w0, [x21, 2492]
	cmp	w0, 1
	beq	.L2126
	ldrb	w0, [x21, 204]
	cbnz	w0, .L2126
	ldr	w2, [x21, 2372]
	ldrh	w0, [x19, 552]
	lsr	w1, w0, 3
	cbz	w2, .L2099
	ldr	w2, [x19, 776]
	cmp	w2, 1
	bhi	.L2099
	mov	w1, 7
	mul	w1, w0, w1
	lsr	w1, w1, 3
.L2099:
	cbz	w1, .L2100
	sub	w1, w1, #1
	and	w1, w1, 65535
.L2100:
	ldrb	w2, [x20, 8]
	add	x0, x19, 544
	bl	List_pop_index_node
	and	w27, w0, 65535
	ldrh	w0, [x19, 552]
	sub	w0, w0, #1
	strh	w0, [x19, 552]
	ldrh	w0, [x21, 2480]
	cmp	w0, w27
	bls	.L2097
	ldr	x0, [x19, 520]
	ubfiz	x26, x27, 1, 16
	ldrh	w25, [x0, x26]
	cbnz	w25, .L2097
	strh	w27, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20, 7]
	cbz	w0, .L2143
	add	x28, x20, 16
	ldrh	w5, [x21, 2472]
	mov	x3, x28
	mov	x0, 0
	mov	x4, 56
	mov	w6, 65535
.L2103:
	cmp	w5, w0, uxth
	bhi	.L2105
	ldr	w0, [x21, 2372]
	cbz	w0, .L2106
	add	x0, x19, 560
	cmp	x20, x0
	bne	.L2106
	ldr	x0, [x19, 440]
	ldrh	w0, [x0, x26]
	cmp	w0, 40
	bls	.L2106
	strb	wzr, [x20, 8]
.L2106:
	ldrb	w0, [x20, 8]
	ldr	x1, [x19, 440]
	cbnz	w0, .L2107
	ldrh	w0, [x1, x26]
	cbz	w0, .L2108
	ldrh	w2, [x21, 2536]
	add	w0, w0, w2
.L2144:
	strh	w0, [x1, x26]
	mov	w1, 0
	ldr	w0, [x19, 760]
	add	w0, w0, 1
	str	w0, [x19, 760]
	mov	w0, w27
	bl	ftl_set_blk_mode
.L2110:
	ldr	x0, [x19, 440]
	ldr	w1, [x19, 772]
	ldrh	w0, [x0, x26]
	cmp	w0, w1
	bls	.L2111
	str	w0, [x19, 772]
.L2111:
	ldr	w2, [x19, 760]
	ldr	w1, [x19, 764]
	ldrh	w0, [x21, 2536]
	madd	w0, w0, w2, w1
	ldrh	w1, [x21, 2480]
	mov	w2, 56
	umull	x2, w25, w2
	udiv	w0, w0, w1
	ldr	x1, [x19, 3696]
	str	w0, [x19, 768]
	ldr	w0, [x1, 16]
	add	w0, w0, 1
	str	w0, [x1, 16]
	mov	x0, 0
.L2112:
	cmp	x2, x0
	bne	.L2113
	ldrb	w0, [x21, 204]
	cbz	w0, .L2114
	ldrb	w0, [x20, 8]
	mov	w2, w25
	cmp	w0, 1
	bne	.L2115
	mov	w1, 0
.L2145:
	ldr	x0, [x19, 432]
	bl	FlashEraseBlocks
.L2114:
	ldrb	w1, [x20, 8]
	mov	w2, w25
	ldr	x0, [x19, 432]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	w2, 0
	mov	x4, 56
.L2116:
	cmp	w25, w1, uxth
	bhi	.L2118
	cmp	w2, 0
	ble	.L2119
	mov	w0, w27
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2119:
	ldrb	w0, [x20, 7]
	cbnz	w0, .L2120
.L2143:
	ldr	x0, [x19, 520]
	mov	w1, -1
	strh	w1, [x0, x26]
	b	.L2097
.L2126:
	mov	w1, 0
	b	.L2100
.L2105:
	ldr	x1, [x19, 432]
	madd	x2, x0, x4, x1
	stp	xzr, xzr, [x2, 8]
	ldrh	w2, [x3]
	cmp	w2, w6
	beq	.L2104
	umull	x7, w25, w4
	add	w25, w25, 1
	and	w25, w25, 65535
	lsl	w2, w2, 10
	add	x1, x1, x7
	str	w2, [x1, 4]
.L2104:
	add	x0, x0, 1
	add	x3, x3, 2
	b	.L2103
.L2108:
	mov	w0, 2
	b	.L2144
.L2107:
	ldrh	w0, [x1, x26]
	add	w0, w0, 1
	strh	w0, [x1, x26]
	ldr	w0, [x19, 764]
	add	w0, w0, 1
	str	w0, [x19, 764]
	mov	w0, w27
	bl	ftl_set_blk_mode.part.9
	b	.L2110
.L2113:
	ldr	x1, [x19, 432]
	add	x1, x1, x0
	add	x0, x0, 56
	ldr	w3, [x1, 4]
	and	w3, w3, -1024
	str	w3, [x1, 4]
	b	.L2112
.L2115:
	mov	w1, 1
	b	.L2145
.L2118:
	mul	x0, x1, x4
	ldr	x3, [x19, 432]
	add	x5, x3, x0
	ldr	w3, [x3, x0]
	cmn	w3, #1
	bne	.L2117
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
.L2117:
	add	x1, x1, 1
	add	x28, x28, 2
	b	.L2116
.L2120:
	add	x23, x23, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR2
	strh	wzr, [x20, 2]
	strb	wzr, [x20, 6]
	ldrh	w1, [x23, 2544]
	strh	w27, [x20]
	mul	w0, w0, w1
	ldr	w1, [x22, 752]
	str	w1, [x20, 12]
	and	w0, w0, 65535
	add	w1, w1, 1
	strh	w0, [x20, 4]
	str	w1, [x22, 752]
	ldr	x1, [x22, 520]
	ldrh	w2, [x20]
	strh	w0, [x1, x2, lsl 1]
.L2096:
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
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	and	w23, w0, 65535
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	ldrh	w0, [x20, 1456]
	str	x27, [sp, 80]
	cbz	w0, .L2147
	adrp	x22, .LANCHOR0
	add	x24, x20, 1464
	add	x22, x22, :lo12:.LANCHOR0
	mov	w21, 0
.L2148:
	ldrh	w0, [x22, 2472]
	cmp	w0, w21
	bhi	.L2154
	bl	FtlGcReFreshBadBlk
.L2147:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L2154:
	add	x0, x22, 2504
	mov	w1, w23
	adrp	x26, .LC128
	add	x26, x26, :lo12:.LC128
	mov	w19, 0
	ldrb	w0, [x0, w21, sxtw]
	bl	V2P_block
	and	w25, w0, 65535
.L2149:
	ldrh	w0, [x20, 1456]
	cmp	w0, w19
	bhi	.L2153
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L2148
.L2153:
	ldrh	w0, [x24, w19, sxtw 1]
	add	w27, w19, 1
	cmp	w0, w25
	bne	.L2150
	mov	w1, w25
	mov	x0, x26
	bl	printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w0, [x20, 1456]
	mov	w1, w27
.L2151:
	cmp	w19, w0
	bcc	.L2152
	sub	w0, w0, #1
	strh	w0, [x20, 1456]
.L2150:
	and	w19, w27, 65535
	b	.L2149
.L2152:
	sub	w2, w1, #1
	ldrh	w3, [x24, w1, sxtw 1]
	add	w19, w19, 1
	add	w1, w1, 1
	and	w19, w19, 65535
	strh	w3, [x24, w2, sxtw 1]
	b	.L2151
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	and	w13, w0, 65535
	adrp	x8, .LANCHOR2
	add	x0, x8, :lo12:.LANCHOR2
	stp	x29, x30, [sp, -16]!
	ubfiz	x1, x13, 1, 16
	add	x29, sp, 0
	ldr	x2, [x0, 520]
	ldrh	w1, [x2, x1]
	cbnz	w1, .L2160
	ldrh	w1, [x0, 800]
	cmp	w1, w13
	bne	.L2161
	mov	w1, -1
	strh	w1, [x0, 800]
.L2162:
	add	x8, x8, :lo12:.LANCHOR2
	mov	w1, w13
	add	x0, x8, 512
	bl	List_remove_node
	ldrh	w0, [x8, 536]
	sub	w0, w0, #1
	strh	w0, [x8, 536]
	mov	w0, w13
	bl	free_data_superblock
	mov	w0, w13
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
.L2159:
	ldp	x29, x30, [sp], 16
	ret
.L2161:
	ldrh	w1, [x0, 560]
	cmp	w1, w13
	beq	.L2166
	ldrh	w1, [x0, 608]
	cmp	w1, w13
	beq	.L2166
	ldrh	w0, [x0, 656]
	cmp	w0, w13
	bne	.L2162
.L2166:
	mov	w0, 0
	b	.L2159
.L2160:
	mov	w0, w13
	bl	List_update_data_list
	b	.L2166
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
	adrp	x20, .LANCHOR2
	cmp	w19, w0
	beq	.L2169
	add	x21, x20, :lo12:.LANCHOR2
	ubfiz	x22, x19, 1, 16
	ldr	x1, [x21, 520]
	ldrh	w0, [x1, x22]
	cbnz	w0, .L2170
	mov	w1, w19
	mov	w2, 0
	adrp	x0, .LC129
	add	x0, x0, :lo12:.LC129
	bl	printk
	ldr	x0, [x21, 520]
	add	x6, x21, 544
	mov	w1, 32
	strh	w1, [x0, x22]
	mov	w1, w19
	mov	x0, x6
	bl	test_node_in_list
	cbz	w0, .L2171
	mov	w1, w19
	mov	x0, x6
	bl	List_remove_node
	ldrh	w0, [x21, 552]
	sub	w0, w0, #1
	strh	w0, [x21, 552]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x0, [x21, 520]
	mov	w1, w19
	ldrh	w2, [x0, x22]
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
	bl	printk
.L2171:
	mov	w0, w19
	bl	FtlGcRefreshBlock
.L2174:
	mov	w0, 0
	b	.L2168
.L2170:
	sub	w0, w0, #1
	strh	w0, [x1, x22]
.L2169:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x20, 3872]
	cmp	w0, w1
	bne	.L2173
	strh	w19, [x20, 3872]
	b	.L2174
.L2173:
	cmp	w19, w0
	beq	.L2174
	bl	update_vpc_list
	cmp	w0, 0
	cset	w0, ne
	strh	w19, [x20, 3872]
.L2168:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	ldrh	w1, [x0, 4]
	cbz	w1, .L2191
	ldrh	w2, [x0]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2191
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR0
	ldrb	w0, [x0, 6]
	add	x22, x20, :lo12:.LANCHOR0
	add	x21, x21, 1500
	add	x0, x0, 8
	ldrh	w1, [x19, x0, lsl 1]
.L2183:
	mov	w0, 65535
	cmp	w1, w0
	beq	.L2185
	ldrb	w1, [x19, 8]
	cmp	w1, 1
	bne	.L2186
	ldrb	w2, [x22, 204]
	cbnz	w2, .L2186
	ldrh	w2, [x19, 2]
	ldrh	w2, [x21, x2, lsl 1]
	cmp	w2, w0
	bne	.L2186
	ldrh	w0, [x19, 4]
	sub	w0, w0, #1
	strh	w0, [x19, 4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w0, [x19, 4]
	cbnz	w0, .L2185
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
.L2194:
	strh	w0, [x19, 2]
	strb	wzr, [x19, 6]
.L2179:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L2185:
	ldrb	w0, [x19, 6]
	ldrh	w1, [x22, 2472]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	cmp	w1, w0
	bne	.L2184
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L2184:
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w1, [x19, x0, lsl 1]
	b	.L2183
.L2186:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w2, [x0, 204]
	cbz	w2, .L2179
	cmp	w1, 1
	bne	.L2179
	ldrh	w2, [x19, 2]
	ldrh	w1, [x0, 2546]
	cmp	w2, w1
	bcc	.L2179
	ldrh	w1, [x19]
	adrp	x2, .LANCHOR2+520
	ldrh	w4, [x19, 4]
	ldr	x3, [x2, #:lo12:.LANCHOR2+520]
	lsl	x1, x1, 1
	ldrh	w2, [x3, x1]
	sub	w2, w2, w4
	strh	w2, [x3, x1]
	strh	wzr, [x19, 4]
	ldrh	w0, [x0, 2544]
	b	.L2194
.L2191:
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
	adrp	x21, .LANCHOR2
	str	x23, [sp, 48]
	add	x21, x21, :lo12:.LANCHOR2
	strb	wzr, [x0, 10]
	adrp	x20, .LANCHOR0
	ldrb	w0, [x0, 6]
	add	x21, x21, 1500
	add	x23, x20, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
.L2196:
	mov	w2, 65535
	cmp	w0, w2
	beq	.L2197
	ldrb	w1, [x19, 8]
	ldrh	w22, [x19, 2]
	cmp	w1, 1
	ldrh	w1, [x19, 4]
	bne	.L2199
	ldrb	w3, [x23, 204]
	cbnz	w3, .L2199
	ldrh	w3, [x21, w22, sxtw 1]
	cmp	w3, w2
	bne	.L2199
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19, 4]
	bl	decrement_vpc_count
.L2197:
	ldrb	w0, [x19, 6]
	ldrh	w1, [x23, 2472]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	cmp	w1, w0
	bne	.L2198
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L2198:
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
	b	.L2196
.L2199:
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	orr	w22, w22, w0, lsl 10
	add	x20, x20, :lo12:.LANCHOR0
	add	x21, x21, 1500
	sub	w1, w1, #1
	strh	w1, [x19, 4]
.L2200:
	ldrb	w0, [x19, 6]
	mov	w1, 65535
	ldrh	w3, [x20, 2472]
.L2202:
	add	w0, w0, 1
	and	w0, w0, 255
	cmp	w0, w3
	bne	.L2201
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	w0, 0
.L2201:
	add	x2, x19, w0, sxtw 1
	ldrh	w2, [x2, 16]
	cmp	w2, w1
	beq	.L2202
	strb	w0, [x19, 6]
	ldrb	w0, [x19, 8]
	cmp	w0, 1
	bne	.L2195
	ldrb	w2, [x20, 204]
	ldrh	w0, [x19, 2]
	cbnz	w2, .L2204
	ldrh	w0, [x21, w0, sxtw 1]
	cmp	w0, w1
	bne	.L2195
	ldrh	w0, [x19, 4]
	cbz	w0, .L2195
	sub	w0, w0, #1
	strh	w0, [x19, 4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2200
.L2204:
	ldrh	w1, [x20, 2546]
	cmp	w0, w1
	bcc	.L2195
	ldrh	w0, [x19]
	adrp	x1, .LANCHOR2+520
	ldrh	w3, [x19, 4]
	ldr	x2, [x1, #:lo12:.LANCHOR2+520]
	lsl	x0, x0, 1
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strh	wzr, [x19, 4]
	ldrh	w0, [x20, 2544]
	strh	w0, [x19, 2]
	strb	wzr, [x19, 6]
.L2195:
	mov	w0, w22
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR2
	add	x19, x23, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w0, [x19, 424]
	cbnz	w0, .L2213
	ldr	x21, [x19, 3656]
	adrp	x24, .LANCHOR4
	add	x22, x24, :lo12:.LANCHOR4
	ldr	x0, [x19, 3608]
	ldrh	w1, [x19, 784]
	add	x20, x19, 456
	adrp	x26, .LANCHOR0
	add	x25, x26, :lo12:.LANCHOR0
	str	x0, [x22, 1720]
	add	x22, x22, 1712
	str	x21, [x22, 16]
	strh	w1, [x21, 2]
	mov	w1, -3932
	strh	w1, [x21]
	str	wzr, [x21, 12]
	ldr	w1, [x19, 792]
	stp	w1, wzr, [x21, 4]
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	str	w1, [x19, 456]
	mov	w1, 99
	ldrb	w2, [x19, 566]
	movk	w1, 0x5000, lsl 16
	str	w1, [x20, 4]
	ldrh	w1, [x19, 560]
	strh	w1, [x20, 14]
	ldrh	w1, [x19, 562]
	ldrh	w0, [x19, 790]
	strh	w0, [x20, 8]
	orr	w1, w2, w1, lsl 6
	strh	w1, [x20, 16]
	ldrh	w1, [x19, 608]
	strh	w1, [x20, 18]
	ldrb	w2, [x19, 614]
	ldrh	w1, [x19, 610]
	ldrh	w0, [x25, 2494]
	strb	w0, [x20, 10]
	ldrb	w0, [x19, 568]
	orr	w1, w2, w1, lsl 6
	strb	w0, [x20, 11]
	ldrb	w0, [x19, 616]
	strb	w0, [x20, 12]
	strh	w1, [x20, 20]
	ldrh	w1, [x19, 656]
	strh	w1, [x20, 22]
	ldrb	w0, [x19, 664]
	strb	w0, [x20, 13]
	ldr	w0, [x19, 760]
	str	w0, [x20, 32]
	ldr	w0, [x19, 752]
	ldrb	w2, [x19, 662]
	str	w0, [x20, 40]
	ldrh	w1, [x19, 658]
	ldr	w0, [x19, 756]
	str	w0, [x20, 36]
	ldrh	w0, [x19, 1448]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x20, 44]
	ldrh	w0, [x19, 1450]
	strh	w0, [x20, 46]
	ldrh	w2, [x25, 2554]
	ldr	x0, [x22, 8]
	strh	w1, [x20, 24]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x22, 8]
	mov	x1, x20
	mov	w2, 48
	mov	x20, x26
	bl	ftl_memcpy
	ldr	x1, [x19, 520]
	ldrh	w2, [x25, 2480]
	ldr	x0, [x22, 8]
	lsl	w2, w2, 1
	add	x0, x0, 48
	bl	ftl_memcpy
	ldrh	w0, [x25, 2480]
	ldr	x3, [x22, 8]
	ldr	x1, [x25, 64]
	lsr	w2, w0, 3
	ubfiz	x0, x0, 1, 16
	add	x0, x0, 51
	add	w2, w2, 4
	and	x0, x0, -4
	add	x0, x3, x0
	bl	ftl_memcpy
	ldrh	w0, [x25, 2588]
	cbz	w0, .L2214
	ldrh	w0, [x25, 2480]
	ldr	x3, [x22, 8]
	ldrh	w2, [x25, 2580]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x19, 3752]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x3, x0
	bl	ftl_memcpy
.L2214:
	add	x27, x23, :lo12:.LANCHOR2
	add	x26, x24, :lo12:.LANCHOR4
	add	x19, x27, 784
	add	x20, x20, :lo12:.LANCHOR0
	mov	w22, 0
	mov	w25, 65535
	mov	w0, 0
	bl	FtlUpdateVaildLpn
.L2215:
	ldrh	w2, [x19, 2]
	ldrh	w1, [x19]
	ldr	x0, [x27, 3608]
	str	x0, [x26, 1720]
	ldr	x0, [x27, 3656]
	str	x0, [x26, 1728]
	orr	w0, w2, w1, lsl 10
	str	w0, [x26, 1716]
	ldrh	w0, [x20, 2546]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L2216
	ldrh	w25, [x19, 4]
	strh	wzr, [x19, 2]
	strh	w1, [x19, 4]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x27, 752]
	str	w1, [x19, 8]
	add	w2, w1, 1
	str	w2, [x27, 752]
	ubfiz	w2, w0, 10, 16
	str	w2, [x26, 1716]
	strh	w0, [x19]
	strh	w0, [x21, 2]
	str	w1, [x21, 4]
.L2216:
	ldrb	w0, [x20, 72]
	cbz	w0, .L2217
	ldrh	w1, [x20, 2554]
	ldr	x0, [x27, 3608]
	bl	js_hash
	str	w0, [x21, 12]
.L2217:
	mov	w3, 1
	add	x0, x26, 1712
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	ldr	w1, [x26, 1712]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2]
	cmn	w1, #1
	bne	.L2218
	cmp	w0, 1
	bne	.L2219
	ldrh	w0, [x20, 2546]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L2219:
	add	w22, w22, 1
	and	w22, w22, 65535
	cmp	w22, 3
	bls	.L2215
	add	x24, x24, :lo12:.LANCHOR4
	add	x23, x23, :lo12:.LANCHOR2
	mov	w2, w22
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	ldr	w1, [x24, 1716]
	bl	printk
	mov	w0, 1
	str	w0, [x23, 424]
.L2213:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L2218:
	cmp	w0, 1
	beq	.L2215
	cmp	w1, 256
	beq	.L2215
	mov	w0, 65535
	cmp	w25, w0
	beq	.L2213
	mov	w1, 1
	mov	w0, w25
	bl	FtlFreeSysBlkQueueIn
	b	.L2213
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	add	x1, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldr	w25, [x1, 424]
	cbnz	w25, .L2235
	adrp	x22, .LANCHOR0
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1, 204]
	cbz	w1, .L2246
	ldrb	w1, [x0, 8]
	cmp	w1, 1
	bne	.L2246
	ldrh	w24, [x0, 4]
	mov	w25, w1
.L2237:
	mov	x19, x0
	mov	w0, -1
	str	w0, [x29, 112]
	add	x0, x21, :lo12:.LANCHOR2
	mov	w2, 61589
	mov	x20, x0
	ldr	x23, [x0, 3656]
	ldr	x1, [x0, 3608]
	stp	x1, x23, [x29, 96]
	mov	w1, -3
	str	w1, [x23, 8]
	mov	w1, -2
	str	w1, [x23, 12]
	ldrh	w1, [x19]
	strh	w1, [x23, 2]
	strh	wzr, [x23]
	ldr	x1, [x0, 3608]
	str	w2, [x1]
	mov	w2, 22136
	movk	w2, 0x1234, lsl 16
	ldr	x1, [x0, 3608]
	str	w2, [x1, 4]
.L2238:
	sub	w24, w24, #1
	cmn	w24, #1
	beq	.L2241
	ldrh	w0, [x19, 4]
	cbnz	w0, .L2239
.L2241:
	add	x21, x21, :lo12:.LANCHOR2
	ldrh	w0, [x19]
	ldrh	w3, [x19, 4]
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x2, [x21, 520]
	lsl	x0, x0, 1
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strb	wzr, [x19, 6]
	ldrh	w0, [x22, 2544]
	strh	w0, [x19, 2]
	strh	wzr, [x19, 4]
.L2235:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 144
	ret
.L2246:
	mov	w24, 12
	b	.L2237
.L2239:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29, 92]
	cmn	w0, #1
	beq	.L2241
	ldr	w0, [x20, 756]
	mov	w3, 0
	str	w0, [x23, 4]
	mov	w2, w25
	add	w0, w0, 1
	mov	w1, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x20, 756]
	add	x0, x29, 88
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2238
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x20, [x0, 16]
	ldr	x26, [x0, 40]
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 10]
	ldrh	w2, [x19, 8]
	sub	w1, w1, #4
	cmp	w2, w1
	blt	.L2252
	ubfiz	x0, x0, 1, 16
	ldrh	w25, [x20, x0]
	cbz	w25, .L2252
	ldr	w1, [x19, 52]
	cbnz	w1, .L2252
	mov	w1, 1
	str	w1, [x19, 52]
	strh	wzr, [x20, x0]
	ldrh	w0, [x19, 8]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2546]
	cmp	w1, w0
	bcc	.L2253
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2253:
	adrp	x23, .LANCHOR4
	add	x21, x23, :lo12:.LANCHOR4
	adrp	x24, .LANCHOR2
	add	x21, x21, 1712
	add	x28, x24, :lo12:.LANCHOR2
	mov	w20, 0
.L2254:
	ldrh	w0, [x19, 6]
	cmp	w0, w20
	bhi	.L2259
	mov	w1, 1
	mov	w0, w25
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19, 52]
.L2252:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x22, 2546]
	cmp	w1, w0
	bcc	.L2257
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
	b	.L2257
.L2259:
	ubfiz	x0, x20, 2, 16
	add	x3, x26, x0
	ldr	w1, [x26, x0]
	cmp	w25, w1, lsr 10
	bne	.L2255
	ldr	x1, [x28, 3616]
	mov	w2, 1
	ldr	x27, [x28, 3656]
	stp	x1, x27, [x21, 8]
	mov	w1, w2
	str	x3, [x29, 104]
	ldr	w0, [x26, x0]
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	ldr	x3, [x29, 104]
	cmn	w0, #1
	bne	.L2256
.L2258:
	add	x23, x23, :lo12:.LANCHOR4
	str	wzr, [x3]
	add	x24, x24, :lo12:.LANCHOR2
	adrp	x0, .LC132
	ldrh	w2, [x27, 8]
	add	x0, x0, :lo12:.LC132
	ldr	w1, [x23, 1716]
	bl	printk
	mov	w0, 1
	str	w0, [x24, 424]
.L2257:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2256:
	ldrh	w0, [x27, 8]
	cmp	w0, w20
	bne	.L2258
	ldrh	w1, [x27]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L2258
	ldr	x2, [x21, 8]
	mov	w1, w20
	mov	x0, x19
	bl	FtlMapWritePage
.L2255:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L2254
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldr	w1, [x20, 424]
	cbnz	w1, .L2265
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w1, 65535
	cmp	w0, w1
	ldr	x21, [x19, 16]
	bne	.L2266
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	strh	w0, [x19, 8]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x21]
	strh	wzr, [x19, 2]
	ldr	w0, [x19, 48]
	strh	wzr, [x19]
	add	w0, w0, 1
	str	w0, [x19, 48]
.L2265:
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L2266:
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x19, 2]
	ldr	x23, [x20, 3656]
	adrp	x22, .LANCHOR0
	ldr	x25, [x19, 40]
	ldrh	w24, [x21, x0]
	adrp	x21, .LANCHOR4
	add	x0, x21, :lo12:.LANCHOR4
	orr	w1, w1, w24, lsl 10
	str	w1, [x0, 1716]
	ldr	x1, [x20, 3608]
	str	x1, [x0, 1720]
	str	x23, [x0, 1728]
	mov	w1, 255
	ldr	w0, [x19, 48]
	str	w0, [x23, 4]
	mov	w0, -1291
	strh	w0, [x23, 8]
	ldrh	w0, [x19, 4]
	strh	w0, [x23]
	add	x0, x22, :lo12:.LANCHOR0
	strh	w24, [x23, 2]
	ldrh	w2, [x0, 2546]
	ldr	x0, [x20, 3608]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w1, 0
	mov	w0, 0
.L2267:
	ldrh	w2, [x19, 6]
	cmp	w2, w0
	bhi	.L2269
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w0, [x22, 72]
	cbz	w0, .L2270
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w1, [x22, 2554]
	ldr	x0, [x0, 1720]
	bl	js_hash
	str	w0, [x23, 12]
.L2270:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w2, 1
	mov	w3, 0
	mov	w1, w2
	add	x0, x21, 1712
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	x0, x19
	bl	ftl_map_blk_gc
	b	.L2265
.L2269:
	ubfiz	x2, x0, 2, 16
	ldr	w3, [x25, x2]
	cmp	w24, w3, lsr 10
	bne	.L2268
	add	w1, w1, 1
	ldr	x4, [x20, 3608]
	and	w1, w1, 65535
	ubfiz	x3, x1, 3, 16
	str	w0, [x4, x3]
	ldr	w4, [x25, x2]
	ldr	x2, [x20, 3608]
	add	x2, x2, x3
	str	w4, [x2, 4]
.L2268:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L2267
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR2
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR4
	add	x24, x25, :lo12:.LANCHOR4
	stp	x21, x22, [sp, 32]
	stp	x27, x28, [sp, 80]
	mov	x22, x25
	stp	x19, x20, [sp, 16]
	mov	w27, w1
	mov	x19, x0
	mov	x28, x2
	add	x24, x24, 1712
	mov	w21, 0
.L2276:
	add	x1, x23, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR0
	ldr	w0, [x1, 736]
	add	w0, w0, 1
	str	w0, [x1, 736]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x0, 2546]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L2277
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2278
.L2277:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L2278:
	add	x1, x23, :lo12:.LANCHOR2
	ldr	w0, [x1, 424]
	cbnz	w0, .L2291
	ldrh	w2, [x19]
	add	x3, x25, :lo12:.LANCHOR4
	ldr	x0, [x19, 16]
	add	x3, x3, 1712
	ldrh	w26, [x0, x2, lsl 1]
	mov	w2, 16
	ldrh	w0, [x19, 2]
	str	x28, [x3, 8]
	orr	w0, w0, w26, lsl 10
	str	w0, [x3, 4]
	ldr	x0, [x1, 3656]
	mov	w1, 0
	str	x0, [x3, 16]
	str	x3, [x29, 104]
	bl	ftl_memset
	ldr	x3, [x29, 104]
	ldr	w0, [x19, 48]
	ldr	x6, [x3, 16]
	strh	w27, [x6, 8]
	str	w0, [x6, 4]
	ldrh	w0, [x19, 4]
	strh	w0, [x6]
	add	x0, x20, :lo12:.LANCHOR0
	strh	w26, [x6, 2]
	ldrb	w1, [x0, 72]
	cbz	w1, .L2280
	ldrh	w1, [x0, 2554]
	ldr	x0, [x3, 8]
	bl	js_hash
	str	w0, [x6, 12]
.L2280:
	mov	w3, 1
	mov	x0, x24
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2]
	ldr	w1, [x24]
	cmn	w1, #1
	bne	.L2281
	ldr	w1, [x24, 4]
	adrp	x0, .LC133
	add	x0, x0, :lo12:.LC133
	add	w21, w21, 1
	and	w21, w21, 65535
	bl	printk
	ldrh	w0, [x19, 2]
	cmp	w0, 2
	bhi	.L2282
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20, 2546]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L2282:
	cmp	w21, 3
	bls	.L2276
	add	x22, x22, :lo12:.LANCHOR4
	add	x23, x23, :lo12:.LANCHOR2
	mov	w2, w21
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
	ldr	w1, [x22, 1716]
	bl	printk
	mov	w0, 1
	str	w0, [x23, 424]
.L2291:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2281:
	cbz	w1, .L2284
	strh	w26, [x19, 60]
	cmp	w0, 1
	bne	.L2285
.L2286:
	str	wzr, [x19, 56]
	b	.L2276
.L2285:
	cmp	w1, 256
.L2299:
	beq	.L2286
	ldr	w0, [x19, 56]
	cbnz	w0, .L2286
	add	x22, x22, :lo12:.LANCHOR4
	ldr	x0, [x19, 40]
	ldr	w1, [x22, 1716]
	str	w1, [x0, w27, uxtw 2]
	b	.L2291
.L2284:
	cmp	w0, 1
	b	.L2299
	.size	FtlMapWritePage, .-FtlMapWritePage
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
	ldr	x0, [x19, 704]
	add	x1, x0, x20
	ldr	x2, [x1, 8]
	ldrh	w1, [x0, x20]
	add	x0, x19, 3792
	bl	FtlMapWritePage
	ldr	x0, [x19, 704]
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
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	stp	x29, x30, [sp, -144]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, 0
	stp	x25, x26, [sp, 64]
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	stp	x23, x24, [sp, 48]
	stp	x27, x28, [sp, 80]
	ldrh	w24, [x0, 6]
	ldr	x21, [x0, 40]
	ldr	x23, [x0, 16]
	ldr	x0, [x0, 24]
	lsl	w2, w24, 2
	str	x0, [x29, 128]
	ldrh	w0, [x22, 8]
	str	w0, [x29, 140]
	mov	x0, x21
	bl	ftl_memset
	stp	wzr, wzr, [x22, 48]
	adrp	x0, .LANCHOR2
	add	x2, x0, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR4
	add	x4, x1, :lo12:.LANCHOR4
	stp	x0, x1, [x29, 104]
	add	x4, x4, 1712
	ldr	w0, [x29, 140]
	ldr	x5, [x2, 3608]
	ldr	x20, [x2, 3656]
	sub	w0, w0, #1
	str	x5, [x4, 8]
	mov	w2, -1
	str	x20, [x4, 16]
	mov	x25, x4
	str	w0, [x29, 136]
	adrp	x0, .LANCHOR0
	add	x26, x0, :lo12:.LANCHOR0
	strh	w2, [x22]
	strh	w2, [x22, 2]
	mov	w2, 1
	str	w2, [x22, 56]
.L2303:
	ldr	w0, [x29, 140]
	cmp	w19, w0
	bge	.L2322
	ldr	w0, [x29, 136]
	sxtw	x27, w19
	cmp	w19, w0
	bne	.L2304
	lsl	x0, x27, 1
	mov	w1, 1
	add	x25, x23, x0
	mov	w26, 0
	ldrh	w0, [x23, x0]
	bl	FtlGetLastWrittenPage
	sxth	w23, w0
	add	w0, w0, 1
	strh	w0, [x22, 2]
	ldr	x0, [x29, 128]
	add	w23, w23, 1
	strh	w19, [x22]
	ldr	w0, [x0, x27, lsl 2]
	str	w0, [x22, 48]
	ldr	x0, [x29, 112]
	add	x19, x0, :lo12:.LANCHOR4
	adrp	x0, .LANCHOR0
	add	x19, x19, 1712
	add	x27, x0, :lo12:.LANCHOR0
.L2305:
	cmp	w26, w23
	blt	.L2308
.L2322:
	mov	x0, x22
	bl	ftl_free_no_use_map_blk
	adrp	x0, .LANCHOR0
	add	x21, x0, :lo12:.LANCHOR0
	ldrh	w1, [x22, 2]
	ldrh	w0, [x21, 2546]
	cmp	w1, w0
	bne	.L2310
	mov	x0, x22
	bl	ftl_map_blk_alloc_new_blk
.L2310:
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
	ldp	x29, x30, [sp], 144
	ret
.L2308:
	ldrh	w0, [x25]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w26, w0, lsl 10
	str	w0, [x19, 4]
	mov	x0, x19
	bl	FlashReadPages
	ldrb	w0, [x27, 72]
	cbz	w0, .L2306
	ldr	x0, [x19, 16]
	ldr	w6, [x0, 12]
	cbz	w6, .L2306
	ldrh	w1, [x27, 2554]
	ldr	x0, [x19, 8]
	bl	js_hash
	cmp	w6, w0
	beq	.L2306
	mov	w0, -1
	str	w0, [x19]
.L2306:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2307
	ldrh	w0, [x20, 8]
	cmp	w24, w0
	bls	.L2307
	ldrh	w2, [x20]
	ldrh	w1, [x22, 4]
	cmp	w2, w1
	bne	.L2307
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x19, 4]
	str	w1, [x21, x0]
.L2307:
	add	w26, w26, 1
	sxth	w26, w26
	b	.L2305
.L2304:
	ldr	x0, [x29, 104]
	mov	w2, 1
	add	x28, x0, :lo12:.LANCHOR2
	ldr	x0, [x28, 3608]
	str	x0, [x25, 8]
	lsl	x0, x27, 1
	add	x1, x23, x0
	str	x1, [x29, 120]
	ldrh	w1, [x26, 2546]
	ldrh	w0, [x23, x0]
	sub	w1, w1, #1
	orr	w0, w1, w0, lsl 10
	mov	w1, w2
	str	w0, [x25, 4]
	mov	x0, x25
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L2324
	ldrh	w1, [x20]
	ldrh	w0, [x22, 4]
	cmp	w1, w0
	bne	.L2324
	ldrh	w1, [x20, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L2312
.L2324:
	mov	w27, 0
	mov	w28, -1
.L2313:
	ldrh	w0, [x26, 2546]
	cmp	w27, w0
	bge	.L2320
	ldr	x0, [x29, 120]
	mov	w2, 1
	mov	w1, w2
	ldrh	w0, [x0]
	orr	w0, w27, w0, lsl 10
	str	w0, [x25, 4]
	mov	x0, x25
	bl	FlashReadPages
	ldrb	w0, [x26, 72]
	cbz	w0, .L2317
	ldr	x0, [x25, 16]
	ldr	w7, [x0, 12]
	cbz	w7, .L2317
	ldrh	w1, [x26, 2554]
	ldr	x0, [x25, 8]
	bl	js_hash
	cmp	w7, w0
	beq	.L2317
	str	w28, [x25]
.L2317:
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L2318
	ldrh	w0, [x20, 8]
	cmp	w24, w0
	bls	.L2318
	ldrh	w2, [x20]
	ldrh	w1, [x22, 4]
	cmp	w2, w1
	bne	.L2318
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x25, 4]
	str	w1, [x21, x0]
.L2318:
	add	w5, w27, 1
	sxth	w27, w5
	b	.L2313
.L2312:
	mov	w0, 0
.L2314:
	ldrh	w1, [x26, 2546]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L2316
.L2320:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2303
.L2316:
	ldr	x2, [x28, 3608]
	sbfiz	x5, x0, 3, 32
	ldrh	w1, [x2, x5]
	cmp	w24, w1
	bls	.L2315
	add	x2, x2, x5
	ubfiz	x1, x1, 2, 16
	ldr	w2, [x2, 4]
	str	w2, [x21, x1]
.L2315:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L2314
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 1776
	add	x29, sp, 0
	ldrh	w2, [x1, 2564]
	strh	w2, [x0, 10]
	mov	w2, -3962
	strh	w2, [x0, 4]
	ldrh	w2, [x1, 2590]
	strh	w2, [x0, 8]
	ldrh	w2, [x1, 2566]
	ldr	x1, [x1, 2592]
	str	x1, [x0, 16]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	strh	w2, [x0, 6]
	ldr	x2, [x1, 3736]
	str	x2, [x0, 24]
	ldr	x2, [x1, 3728]
	ldr	x1, [x1, 3744]
	stp	x2, x1, [x0, 32]
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlLoadMapInfo
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlL2PDataInit
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 3792
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x27, x28, [sp, 80]
	mov	w28, w0
	stp	x23, x24, [sp, 48]
	mov	x27, x2
	adrp	x23, .LANCHOR0
	add	w2, w0, w1
	add	x0, x23, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x19, x20, [sp, 16]
	mov	w21, w1
	stp	x25, x26, [sp, 64]
	ldrh	w1, [x0, 2540]
	cmp	w2, w1
	bhi	.L2356
	ldrh	w22, [x0, 2552]
	adrp	x25, .LANCHOR2
	add	x25, x25, :lo12:.LANCHOR2
	mov	w24, 0
	mov	x26, x25
	lsr	w22, w28, w22
.L2350:
	cbnz	w21, .L2355
.L2348:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L2355:
	ldr	x0, [x25, 3744]
	ldr	w2, [x0, w22, uxtw 2]
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2550]
	and	w0, w21, 65535
	udiv	w20, w28, w1
	msub	w20, w20, w1, w28
	sub	w19, w1, w20
	and	w19, w19, 65535
	cmp	w21, w19
	csel	w19, w0, w19, cc
	cbz	w2, .L2352
	cmp	w19, w1
	beq	.L2352
	ldr	x0, [x26, 3624]
	str	w2, [x29, 124]
	mov	w2, 1
	stp	x0, xzr, [x29, 128]
	mov	w1, w2
	add	x0, x29, 120
	bl	FlashReadPages
.L2353:
	lsl	w3, w19, 9
	ldr	x0, [x26, 3624]
	lsl	w20, w20, 9
	mov	w2, w3
	asr	w20, w20, 2
	mov	x1, x27
	str	w3, [x29, 108]
	add	x0, x0, w20, sxtw 2
	bl	ftl_memcpy
	sub	w21, w21, w19
	ldr	x2, [x26, 3624]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	mov	w1, w22
	add	x0, x0, 1776
	add	w28, w28, w19
	add	w22, w22, 1
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	w3, [x29, 108]
	csinv	w24, w24, wzr, ne
	add	x27, x27, w3, sxtw
	b	.L2350
.L2352:
	add	x0, x23, :lo12:.LANCHOR0
	mov	w1, 0
	ldrh	w2, [x0, 2554]
	ldr	x0, [x26, 3624]
	bl	ftl_memset
	b	.L2353
.L2356:
	mov	w24, -1
	b	.L2348
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w3, [x0, 848]
	cmp	w3, w1
	bne	.L2364
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 848
	mov	w1, 99
	add	x29, sp, 0
	movk	w1, 0x5000, lsl 16
	str	w1, [x2, 4]
	ldr	w1, [x0, 744]
	str	w1, [x2, 88]
	ldr	w1, [x0, 748]
	str	w1, [x2, 92]
	ldr	w1, [x0, 740]
	str	w1, [x2, 8]
	ldr	w1, [x0, 728]
	str	w1, [x2, 12]
	ldr	w1, [x0, 720]
	str	w1, [x2, 16]
	ldr	w1, [x0, 736]
	str	w1, [x2, 20]
	ldr	w1, [x0, 764]
	str	w1, [x2, 28]
	ldr	w1, [x0, 448]
	str	w1, [x2, 32]
	ldr	w1, [x0, 724]
	str	w1, [x2, 36]
	ldr	w1, [x0, 732]
	str	w1, [x2, 40]
	ldr	w1, [x0, 772]
	str	w1, [x2, 44]
	ldr	w1, [x0, 776]
	str	w1, [x2, 48]
	ldr	w1, [x0, 1360]
	ldr	w0, [x0, 3556]
	stp	w1, w0, [x2, 60]
	mov	w1, 1
	mov	w0, 0
	bl	FtlVendorPartWrite
	ldp	x29, x30, [sp], 16
	ret
.L2364:
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x1, .LANCHOR0+2372
	ldr	w2, [x1, #:lo12:.LANCHOR0+2372]
	adrp	x1, .LANCHOR2
	cbz	w2, .L2372
	add	x2, x1, :lo12:.LANCHOR2
	mov	w3, 4
	ldr	w2, [x2, 776]
	cmp	w2, 39
	mov	w2, 32
	csel	w2, w2, w3, hi
.L2368:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3, 1840]
	cmp	w4, 31
	bhi	.L2369
	add	w4, w4, 1
	mov	w2, 1
	strh	w4, [x3, 1840]
.L2369:
	cbnz	w0, .L2370
	add	x0, x1, :lo12:.LANCHOR2
	ldr	x0, [x0, 3696]
	ldr	w3, [x0, 20]
	ldr	w0, [x0, 16]
	add	w2, w2, w3
	cmp	w0, w2
	bcc	.L2375
.L2370:
	add	x0, x1, :lo12:.LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x0, 3696]
	ldr	w2, [x1, 16]
	str	w2, [x1, 20]
	mov	w2, 17221
	movk	w2, 0x4254, lsl 16
	str	w2, [x1]
	ldr	x2, [x0, 3696]
	ldrh	w1, [x0, 3680]
	lsl	w3, w1, 9
	str	wzr, [x2, 4]
	str	w3, [x2, 12]
	ldr	w3, [x2, 8]
	add	w3, w3, 1
	str	w3, [x2, 8]
	ldr	x2, [x0, 3696]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L2372:
	mov	w2, 32
	b	.L2368
.L2375:
	mov	w0, 0
	ret
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	stp	x29, x30, [sp, -176]!
	add	w3, w0, w1
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w22, w1
	stp	x23, x24, [sp, 48]
	adrp	x1, .LANCHOR0
	mov	w23, w0
	add	x0, x1, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	mov	x25, x2
	stp	x19, x20, [sp, 16]
	stp	x27, x28, [sp, 80]
	str	x1, [x29, 104]
	ldrh	w2, [x0, 2540]
	cmp	w3, w2
	bhi	.L2386
	ldrh	w21, [x0, 2552]
	adrp	x26, .LANCHOR2
	add	x26, x26, :lo12:.LANCHOR2
	mov	w24, 0
	mov	x27, x26
	lsr	w21, w23, w21
.L2379:
	cbnz	w22, .L2385
.L2377:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L2385:
	ldr	x0, [x26, 3744]
	ldr	w4, [x0, w21, uxtw 2]
	ldr	x0, [x29, 104]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w19, [x0, 2550]
	and	w0, w22, 65535
	udiv	w20, w23, w19
	msub	w20, w20, w19, w23
	sub	w19, w19, w20
	and	w19, w19, 65535
	cmp	w22, w19
	csel	w19, w0, w19, cc
	lsl	w28, w19, 9
	cbz	w4, .L2381
	ldr	x0, [x27, 3624]
	mov	w2, 1
	str	w4, [x29, 96]
	mov	w1, w2
	str	w4, [x29, 124]
	stp	x0, xzr, [x29, 128]
	add	x0, x29, 120
	bl	FlashReadPages
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldr	w0, [x29, 120]
	ldr	w4, [x29, 96]
	cmn	w0, #1
	ldr	w0, [x3, 1712]
	csinv	w24, w24, wzr, ne
	cmp	w0, 256
	bne	.L2383
	mov	w2, w4
	mov	w1, w21
	str	x3, [x29, 96]
	adrp	x0, .LC135
	add	x0, x0, :lo12:.LC135
	bl	printk
	ldr	x3, [x29, 96]
	mov	w1, w21
	ldr	x2, [x27, 3624]
	add	x0, x3, 1776
	bl	FtlMapWritePage
.L2383:
	ldr	x1, [x27, 3624]
	lsl	w20, w20, 9
	asr	w20, w20, 2
	mov	w2, w28
	mov	x0, x25
	add	x1, x1, w20, sxtw 2
	bl	ftl_memcpy
.L2384:
	add	w21, w21, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x25, x25, w28, sxtw
	b	.L2379
.L2381:
	mov	w2, w28
	mov	w1, 0
	mov	x0, x25
	bl	ftl_memset
	b	.L2384
.L2386:
	mov	w24, -1
	b	.L2377
	.size	FtlVendorPartRead, .-FtlVendorPartRead
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
	ldrh	w1, [x19, 3680]
	ldr	x2, [x19, 3696]
	bl	FtlVendorPartRead
	ldr	x0, [x19, 3696]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L2389
	adrp	x1, .LC136
	adrp	x0, .LC76
	add	x1, x1, :lo12:.LC136
	add	x0, x0, :lo12:.LC76
	bl	printk
	ldr	x0, [x19, 3696]
	mov	w1, 0
	ldrh	w2, [x19, 3680]
	lsl	w2, w2, 9
	bl	ftl_memset
.L2389:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	stp	x29, x30, [sp, -48]!
	mov	w1, 1
	mov	w0, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x22, x20, 848
	mov	w21, 19539
	mov	x2, x22
	bl	FtlVendorPartRead
	ldr	w0, [x20, 848]
	movk	w21, 0x4654, lsl 16
	cmp	w0, w21
	beq	.L2392
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	str	w21, [x20, 848]
.L2392:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0, 848]
	cmp	w3, w2
	bne	.L2393
	ldr	w2, [x0, 936]
	str	w2, [x0, 744]
	ldr	w2, [x0, 940]
	str	w2, [x0, 748]
	ldr	w2, [x0, 856]
	str	w2, [x0, 740]
	ldr	w2, [x0, 860]
	str	w2, [x0, 728]
	ldr	w2, [x0, 864]
	str	w2, [x0, 720]
	ldr	w2, [x0, 868]
	str	w2, [x0, 736]
	ldr	w2, [x0, 876]
	str	w2, [x0, 764]
	ldr	w2, [x0, 880]
	str	w2, [x0, 448]
	ldr	w2, [x0, 884]
	str	w2, [x0, 724]
	ldr	w2, [x0, 888]
	str	w2, [x0, 732]
	ldr	w2, [x0, 892]
	ldr	w1, [x0, 908]
	str	w2, [x0, 772]
	ldr	w2, [x0, 896]
	str	w1, [x0, 1360]
	str	w2, [x0, 776]
.L2393:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 34661
	movk	w1, 0x1234, lsl 16
	adrp	x20, .LANCHOR0
	add	x0, x0, 848
	ldr	w2, [x0, 68]
	str	wzr, [x0, 2708]
	cmp	w2, w1
	bne	.L2394
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w2, [x1, 204]
	cbz	w2, .L2395
	str	wzr, [x0, 68]
	bl	Ftl_save_ext_data
.L2394:
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	ldp	x21, x22, [sp, 32]
	ldr	w1, [x19, 764]
	ldrh	w0, [x20, 2536]
	ldr	w2, [x19, 760]
	madd	w0, w0, w2, w1
	ldrh	w1, [x20, 2480]
	udiv	w0, w0, w1
	str	w0, [x19, 768]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L2395:
	mov	w0, 1
	str	w0, [x1, 2372]
	adrp	x1, .LC137
	adrp	x0, .LC76
	add	x1, x1, :lo12:.LC137
	add	x0, x0, :lo12:.LC76
	bl	printk
	b	.L2394
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	FtlMapBlkWriteDumpData
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	ldr	w1, [x0, 56]
	cbz	w1, .L2404
	stp	x29, x30, [sp, -48]!
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	str	x21, [sp, 32]
	str	wzr, [x0, 56]
	ldr	w1, [x2, 424]
	ldrh	w19, [x0, 6]
	ldr	x4, [x0, 40]
	cbnz	w1, .L2397
	adrp	x1, .LANCHOR4
	add	x3, x1, :lo12:.LANCHOR4
	sub	w19, w19, #1
	mov	x21, x0
	and	w19, w19, 65535
	ldr	x0, [x2, 3616]
	ldr	x2, [x2, 3656]
	mov	x20, x1
	str	x2, [x3, 1728]
	ubfiz	x2, x19, 2, 16
	str	x0, [x3, 1720]
	add	x3, x3, 1712
	ldr	w2, [x4, x2]
	str	w2, [x3, 4]
	cbz	w2, .L2401
	mov	w2, 1
	mov	x0, x3
	mov	w1, w2
	bl	FlashReadPages
.L2402:
	add	x1, x20, :lo12:.LANCHOR4
	mov	x0, x21
	ldr	x2, [x1, 1720]
	mov	w1, w19
	bl	FtlMapWritePage
.L2397:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L2401:
	adrp	x1, .LANCHOR0+2554
	ldrh	w2, [x1, #:lo12:.LANCHOR0+2554]
	mov	w1, 255
	bl	ftl_memset
	b	.L2402
.L2404:
	ret
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.type	FlashReadFacBbtData.part.17, %function
FlashReadFacBbtData.part.17:
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LANCHOR4
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x0
	cbnz	w1, .L2408
	add	x5, x3, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w7, 1
.L2409:
	ldr	w1, [x5, 1644]
	cmp	w0, w1
	bcc	.L2410
.L2408:
	add	x3, x3, :lo12:.LANCHOR4
	mov	x0, x19
	ldr	x1, [x3, 1608]
	bl	ftl_memcpy
	mov	w3, 4
	mov	x1, x19
	mov	w2, w3
	adrp	x0, .LC138
	add	x0, x0, :lo12:.LC138
	bl	rknand_print_hex
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2410:
	ubfx	x1, x0, 5, 11
	ldr	x6, [x5, 1608]
	lsl	x1, x1, 2
	lsl	w8, w7, w0
	add	w0, w0, 1
	and	w0, w0, 65535
	ldr	w4, [x6, x1]
	orr	w4, w4, w8
	str	w4, [x6, x1]
	b	.L2409
	.size	FlashReadFacBbtData.part.17, .-FlashReadFacBbtData.part.17
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, w1
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	add	x1, x1, 176
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	stp	x25, x26, [sp, 64]
	mov	w25, w2
	mov	w26, 61664
	ldrh	w0, [x1, 14]
	ldrh	w1, [x1, 12]
	mul	w0, w0, w1
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	and	w0, w0, 65535
	mov	x21, x1
	sub	w20, w0, #1
	sub	w19, w0, #16
	ldr	x2, [x1, 1608]
	and	w20, w20, 65535
	mul	w24, w0, w23
	str	x2, [x29, 96]
	ldr	x2, [x1, 1664]
	str	x2, [x29, 104]
.L2414:
	cmp	w20, w19
	bgt	.L2417
	mov	w0, -1
	b	.L2413
.L2417:
	add	w0, w20, w24
	mov	w2, 1
	lsl	w0, w0, 10
	mov	w1, w2
	str	w0, [x29, 92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29, 88]
	cmn	w0, #1
	beq	.L2415
	ldr	x0, [x21, 1664]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L2415
	cbz	x22, .L2418
	mov	w2, w25
	mov	w1, w23
	mov	x0, x22
	bl	FlashReadFacBbtData.part.17
.L2413:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 144
	ret
.L2415:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L2414
.L2418:
	mov	w0, 0
	b	.L2413
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x21, [sp, 32]
	mov	x21, x0
	adrp	x0, .LANCHOR0+88
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x0, #:lo12:.LANCHOR0+88]
	ldrb	w2, [x0, 13]
	ldrh	w19, [x0, 14]
	ldr	x0, [x20, 1656]
	mul	w19, w19, w2
	and	w19, w19, 65535
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L2424
.L2428:
	mov	w0, 0
.L2425:
	ubfiz	x0, x0, 1, 16
	mov	w1, -1
	strh	w1, [x21, x0]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L2424:
	lsr	w4, w19, 4
	sub	w19, w19, #1
	mov	w1, 0
	mov	w0, 0
	mov	w5, 1
.L2426:
	cmp	w1, w19
	bge	.L2425
	ldr	x6, [x20, 1656]
	ubfx	x3, x1, 5, 11
	lsl	w2, w5, w1
	ldr	w3, [x6, x3, lsl 2]
	tst	w2, w3
	beq	.L2427
	add	w2, w0, 1
	ubfiz	x0, x0, 1, 16
	strh	w1, [x21, x0]
	and	w0, w2, 65535
.L2427:
	cmp	w0, w4
	bcs	.L2428
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L2426
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	stp	x29, x30, [sp, -112]!
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	str	x0, [x29, 104]
	ldr	w23, [x1, 424]
	cbnz	w23, .L2435
	bl	FtlBbtMemInit
	adrp	x20, .LANCHOR0
	bl	FtlLoadFactoryBbt
	add	x22, x20, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR4
	add	x24, x0, :lo12:.LANCHOR4
	add	x26, x22, 2656
	add	x24, x24, 1712
	add	x22, x22, 2636
.L2436:
	add	x19, x20, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2494]
	cmp	w23, w0
	bcc	.L2442
	mov	w21, 0
.L2443:
	ldrh	w0, [x19, 2558]
	cmp	w0, w21
	bhi	.L2444
	add	x19, x19, 2624
	ldrh	w21, [x19, 12]
	mov	w22, 65535
	sub	w21, w21, #1
	and	w21, w21, 65535
.L2445:
	ldrh	w0, [x19, 12]
	sub	w0, w0, #48
	cmp	w21, w0
	ble	.L2449
	mov	w0, w21
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2446
	mov	w0, w21
	bl	FlashTestBlk
	cbz	w0, .L2447
	mov	w0, w21
	bl	FtlBbmMapBadBlock
.L2446:
	sub	w21, w21, #1
	and	w21, w21, 65535
	b	.L2445
.L2442:
	ldr	x0, [x29, 104]
	adrp	x1, .LANCHOR4
	add	x21, x1, :lo12:.LANCHOR4
	ldrh	w1, [x22]
	add	x25, x0, :lo12:.LANCHOR2
	add	x21, x21, 1712
	mov	w2, 65535
	cmp	w1, w2
	ldr	x0, [x25, 3608]
	ldr	x27, [x25, 3656]
	stp	x0, x27, [x21, 8]
	beq	.L2437
	ldrh	w4, [x19, 2542]
	mov	w2, 1
	madd	w28, w4, w23, w1
	mov	w1, w2
	lsl	w0, w28, 10
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	x1, [x21, 8]
	ldr	x0, [x26]
	ldrh	w2, [x19, 2542]
	add	w2, w2, 7
	lsr	w2, w2, 3
	bl	ftl_memcpy
.L2438:
	mov	w0, w28
	add	w23, w23, 1
	bl	FtlBbmMapBadBlock
	add	x26, x26, 8
	add	x22, x22, 2
	b	.L2436
.L2437:
	mov	w1, w23
	bl	FlashGetBadBlockList
	ldr	x0, [x21, 8]
	ldr	x1, [x26]
	bl	FtlBbt2Bitmap
	ldrh	w21, [x19, 2542]
.L2440:
	sub	w21, w21, #1
	and	w21, w21, 65535
.L2439:
	ldrh	w0, [x19, 2542]
	madd	w0, w23, w0, w21
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2440
	ldr	x0, [x25, 3656]
	mov	w2, 16
	strh	w21, [x22]
	mov	w1, 0
	bl	ftl_memset
	str	wzr, [x27, 4]
	mov	w0, -3872
	strh	w0, [x27]
	ldrh	w0, [x22]
	strh	w0, [x27, 2]
	ldrh	w4, [x19, 2542]
	ldrh	w0, [x22]
	ldr	x1, [x26]
	ldrh	w2, [x25, 3784]
	madd	w28, w4, w23, w0
	lsl	w2, w2, 2
	lsl	w0, w28, 10
	str	w0, [x24, 4]
	ldr	x0, [x24, 8]
	bl	ftl_memcpy
	mov	w2, 1
	mov	x0, x24
	mov	w1, w2
	bl	FlashEraseBlocks
	mov	w3, 1
	mov	x0, x24
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldr	w0, [x24]
	cmn	w0, #1
	bne	.L2438
	mov	w0, w28
	bl	FtlBbmMapBadBlock
	b	.L2439
.L2444:
	mov	w0, w21
	add	w21, w21, 1
	bl	FtlBbmMapBadBlock
	and	w21, w21, 65535
	b	.L2443
.L2447:
	ldrh	w0, [x19]
	cmp	w0, w22
	bne	.L2448
	strh	w21, [x19]
	b	.L2446
.L2448:
	strh	w21, [x19, 4]
.L2449:
	ldr	x0, [x29, 104]
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x20, 2624]
	str	wzr, [x20, 2632]
	ldr	x2, [x0, 432]
	lsl	w1, w1, 10
	strh	wzr, [x20, 2626]
	str	w1, [x2, 4]
	mov	w2, 2
	ldr	x0, [x0, 432]
	ldrh	w1, [x20, 2628]
	lsl	w1, w1, 10
	str	w1, [x0, 60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x20, 2624]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x20, 2628]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x20, 2626]
	ldr	w0, [x20, 2632]
	ldrh	w1, [x20, 2628]
	add	w0, w0, 1
	str	w0, [x20, 2632]
	ldrh	w0, [x20, 2624]
	strh	w0, [x20, 2628]
	strh	w1, [x20, 2624]
	bl	FtlBbmTblFlush
.L2435:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	add	x3, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w4, [x3, 2616]
	cmp	w0, w4
	bcs	.L2456
	ldrh	w27, [x3, 2552]
	mov	w25, w0
	adrp	x20, .LANCHOR2
	str	x1, [x29, 120]
	add	w27, w27, 7
	ldrh	w1, [x3, 2582]
	str	w2, [x29, 116]
	lsr	w24, w0, w27
	add	x0, x20, :lo12:.LANCHOR2
	and	w23, w24, 65535
	ldr	x8, [x0, 704]
	mov	x0, 0
.L2457:
	and	x19, x0, 65535
	cmp	w19, w1
	bcc	.L2462
	bl	select_l2p_ram_region
	and	x19, x0, 65535
	ubfiz	x21, x19, 4, 16
	mov	w2, 65535
	add	x1, x8, x21
	ldrh	w3, [x8, x21]
	cmp	w3, w2
	beq	.L2463
	ldr	w1, [x1, 4]
	tbz	w1, #31, .L2463
	bl	flush_l2p_region
.L2463:
	add	x28, x20, :lo12:.LANCHOR2
	ubfiz	x0, x23, 2, 16
	ldr	x1, [x28, 3752]
	ldr	w3, [x1, x0]
	cbnz	w3, .L2464
	ldr	x0, [x28, 704]
	add	x22, x22, :lo12:.LANCHOR0
	mov	w1, 255
	add	x0, x0, x21
	ldrh	w2, [x22, 2554]
	ldr	x0, [x0, 8]
	bl	ftl_memset
	ldr	x0, [x28, 704]
	strh	w23, [x0, x21]
	ldr	x0, [x28, 704]
	add	x21, x0, x21
	str	wzr, [x21, 4]
	b	.L2459
.L2456:
	mov	w0, -1
	cbnz	w2, .L2455
	str	w0, [x1]
.L2455:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L2462:
	add	x0, x0, 1
	add	x2, x8, x0, lsl 4
	ldrh	w2, [x2, -16]
	cmp	w2, w23
	bne	.L2457
.L2459:
	mov	x0, 1
	lsl	x27, x0, x27
	ldr	w0, [x29, 116]
	sub	w27, w27, #1
	and	w25, w27, w25
	and	x25, x25, 65535
	cbnz	w0, .L2460
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x29, 120]
	ldr	x0, [x0, 704]
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0, 8]
	ldr	w0, [x0, x25, lsl 2]
	str	w0, [x1]
.L2461:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x20, 704]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19, 4]
	cmn	w0, #1
	beq	.L2468
	add	w0, w0, 1
	str	w0, [x19, 4]
.L2468:
	mov	w0, 0
	b	.L2455
.L2460:
	add	x2, x20, :lo12:.LANCHOR2
	ldr	x1, [x29, 120]
	lsl	x0, x19, 4
	ldr	x3, [x2, 704]
	ldr	w4, [x1]
	mov	x1, x0
	add	x0, x3, x0
	ldr	x0, [x0, 8]
	str	w4, [x0, x25, lsl 2]
	strh	w23, [x2, 712]
	ldr	x0, [x2, 704]
	add	x0, x0, x1
	ldr	w1, [x0, 4]
	orr	w1, w1, -2147483648
	str	w1, [x0, 4]
	b	.L2461
.L2464:
	ldr	x0, [x28, 704]
	adrp	x6, .LANCHOR4
	add	x6, x6, :lo12:.LANCHOR4
	mov	w2, 1
	add	x0, x0, x21
	add	x26, x6, 1712
	str	x6, [x29, 104]
	mov	w1, w2
	str	w3, [x6, 1716]
	ldr	x0, [x0, 8]
	str	x0, [x6, 1720]
	ldr	x0, [x28, 3656]
	str	x0, [x6, 1728]
	mov	x0, x26
	str	w3, [x29, 112]
	bl	FlashReadPages
	ldr	x0, [x26, 16]
	ldr	w3, [x29, 112]
	ldr	x6, [x29, 104]
	ldrh	w0, [x0, 8]
	cmp	w0, w23
	beq	.L2465
	mov	w2, w3
	and	w1, w24, 65535
	adrp	x0, .LC139
	add	x0, x0, :lo12:.LC139
	bl	printk
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x1, [x26, 16]
	mov	w3, 4
	adrp	x0, .LC100
	mov	w2, w3
	add	x0, x0, :lo12:.LC100
	bl	rknand_print_hex
	ldrh	w3, [x22, 2580]
	mov	w2, 4
	ldr	x1, [x28, 3752]
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x28, 424]
.L2466:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x0, [x1, 704]
	add	x0, x0, x21
	str	wzr, [x0, 4]
	ldr	x0, [x1, 704]
	strh	w23, [x0, x21]
	b	.L2459
.L2465:
	ldr	w0, [x6, 1712]
	cmp	w0, 256
	bne	.L2466
	and	w24, w24, 65535
	mov	w2, w3
	mov	w1, w24
	adrp	x0, .LC141
	add	x0, x0, :lo12:.LC141
	bl	printk
	ldr	x0, [x28, 704]
	mov	w1, w24
	add	x0, x0, x21
	ldr	x2, [x0, 8]
	add	x0, x28, 3792
	bl	FtlMapWritePage
	b	.L2466
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	stp	x23, x24, [sp, 48]
	add	x23, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	str	x25, [sp, 64]
	ldr	w0, [x23, 424]
	cbnz	w0, .L2471
	ldrh	w2, [x23, 564]
	cbz	w2, .L2473
	ldrb	w1, [x23, 568]
	cbnz	w1, .L2473
	adrp	x21, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x23, 567]
	ldrh	w3, [x21, 2544]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L2473
	ldrb	w0, [x23, 570]
	cbnz	w0, .L2471
	ldr	w22, [x21, 2616]
	add	x1, x29, 84
	ldrh	w24, [x21, 2472]
	mov	w2, 0
	sub	w22, w22, #1
	mov	w0, w22
	bl	log2phys
	ldr	x20, [x23, 3656]
	ldr	w0, [x29, 84]
	ldr	x1, [x23, 3608]
	str	w0, [x29, 92]
	cmn	w0, #1
	stp	x1, x20, [x29, 96]
	str	w22, [x29, 112]
	str	wzr, [x20, 4]
	beq	.L2475
	mov	w2, 0
	mov	w1, 1
	add	x0, x29, 88
	bl	FlashReadPages
.L2476:
	add	x25, x19, :lo12:.LANCHOR2
	lsl	w24, w24, 2
	add	x21, x25, 560
	mov	w0, -3947
	mov	w23, 0
	strh	w0, [x20]
.L2477:
	cmp	w24, w23
	bne	.L2481
.L2478:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19, 570]
.L2471:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 144
	ret
.L2475:
	ldrh	w2, [x21, 2554]
	mov	w1, 255
	ldr	x0, [x23, 3608]
	bl	ftl_memset
	b	.L2476
.L2481:
	ldrh	w0, [x21, 4]
	cbz	w0, .L2478
	ldr	w0, [x29, 92]
	add	w23, w23, 1
	stp	w22, w0, [x20, 8]
	ldrh	w0, [x21]
	strh	w0, [x20, 2]
	mov	x0, x21
	bl	get_new_active_ppa
	str	w0, [x29, 92]
	ldr	w0, [x25, 756]
	mov	w3, 0
	str	w0, [x20, 4]
	mov	w2, 0
	add	w0, w0, 1
	mov	w1, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x25, 756]
	add	x0, x29, 88
	bl	FlashProgPages
	ldrh	w0, [x21]
	bl	decrement_vpc_count
	b	.L2477
.L2473:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19, 570]
	b	.L2471
	.size	FtlWriteDumpData, .-FtlWriteDumpData
	.align	2
	.global	l2p_flush
	.type	l2p_flush, %function
l2p_flush:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	str	x21, [sp, 32]
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	mov	w19, 0
	bl	FtlWriteDumpData
.L2490:
	ldrh	w0, [x20, 2582]
	cmp	w0, w19
	bhi	.L2492
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L2492:
	ldr	x1, [x21, 704]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L2491
	mov	w0, w19
	bl	flush_l2p_region
.L2491:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L2490
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x2, x19, :lo12:.LANCHOR2
	str	x21, [sp, 32]
	ldr	w1, [x2, 424]
	cbnz	w1, .L2495
	ldrh	w20, [x0]
	mov	x21, x0
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2496
	ldr	x0, [x2, 520]
	ubfiz	x1, x20, 1, 16
	ldrh	w0, [x0, x1]
	cbz	w0, .L2497
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L2496:
	add	x2, x19, :lo12:.LANCHOR2
	strb	wzr, [x21, 8]
	add	x0, x2, 608
	cmp	x21, x0
	beq	.L2498
	adrp	x1, .LANCHOR0
	add	x4, x1, :lo12:.LANCHOR0
	ldrh	w3, [x4, 2492]
	cmp	w3, 1
	beq	.L2498
	ldrb	w0, [x4, 204]
	cbz	w0, .L2499
.L2498:
	mov	w0, 1
	strb	w0, [x21, 8]
.L2500:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x2, 3872]
	cmp	w0, w1
	beq	.L2505
	cmp	w20, w0
	bne	.L2506
	ldr	x2, [x2, 520]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L2507
.L2506:
	bl	update_vpc_list
.L2507:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19, 3872]
.L2505:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2495:
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L2497:
	mov	w0, w20
	bl	INSERT_FREE_LIST
	b	.L2496
.L2499:
	add	x0, x2, 560
	cmp	x21, x0
	bne	.L2500
	cmp	w3, 3
	beq	.L2502
	ldr	w0, [x2, 1360]
	cmp	w0, 1
	bne	.L2503
.L2502:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x2, 568]
.L2503:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x1, 2372]
	cbz	w0, .L2500
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1, 776]
	cmp	w0, 39
	bhi	.L2500
	mov	w0, 1
	strb	w0, [x1, 568]
	b	.L2500
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlCheckVpc
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC109
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x23, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	w22, 0
	add	x1, x1, 216
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	add	x0, x0, :lo12:.LC109
	adrp	x21, check_valid_page_count_table
	bl	printk
	add	x20, x21, :lo12:check_valid_page_count_table
	mov	x2, 8192
	mov	w1, 0
	mov	x0, x20
	bl	memset
.L2522:
	ldr	w0, [x23, 2616]
	cmp	w22, w0
	bcc	.L2524
	add	x22, x19, :lo12:.LANCHOR0
	adrp	x23, .LC142
	adrp	x19, .LANCHOR2
	add	x26, x21, :lo12:check_valid_page_count_table
	add	x25, x19, :lo12:.LANCHOR2
	add	x23, x23, :lo12:.LC142
	mov	w24, 0
	mov	w20, 0
.L2525:
	ldrh	w0, [x22, 2480]
	cmp	w0, w20
	bhi	.L2527
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x19, [x0, 544]
	cbz	x19, .L2528
	ldr	x1, [x0, 504]
	adrp	x23, .LC143
	ldrh	w25, [x0, 552]
	mov	x20, x0
	sub	x19, x19, x1
	mov	x1, -6148914691236517206
	asr	x19, x19, 1
	movk	x1, 0xaaab, lsl 0
	add	x21, x21, :lo12:check_valid_page_count_table
	add	x23, x23, :lo12:.LC143
	mov	w22, 0
	mov	w26, 6
	mul	x19, x19, x1
	and	w19, w19, 65535
.L2529:
	cmp	w22, w25
	bne	.L2531
.L2528:
	mov	w1, w24
	adrp	x0, .LC144
	add	x0, x0, :lo12:.LC144
	bl	printk
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2524:
	mov	w2, 0
	add	x1, x29, 108
	mov	w0, w22
	bl	log2phys
	ldr	w0, [x29, 108]
	cmn	w0, #1
	beq	.L2523
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	and	x0, x0, 65535
	ldrh	w1, [x20, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x20, x0, lsl 1]
.L2523:
	add	w22, w22, 1
	b	.L2522
.L2527:
	ldr	x0, [x25, 520]
	ubfiz	x28, x20, 1, 16
	sxtw	x27, w20
	ldrh	w2, [x0, x28]
	ldrh	w3, [x26, x27, lsl 1]
	cmp	w2, w3
	beq	.L2526
	mov	w1, w20
	mov	x0, x23
	bl	printk
	ldr	x0, [x25, 520]
	mov	w1, 65535
	ldrh	w0, [x0, x28]
	cmp	w0, w1
	beq	.L2526
	ldrh	w1, [x26, x27, lsl 1]
	cmp	w1, w0
	csinc	w24, w24, wzr, ls
.L2526:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L2525
.L2531:
	ldr	x1, [x20, 520]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L2530
	ldrh	w3, [x21, w19, sxtw 1]
	mov	w24, 1
	mov	w1, w19
	mov	x0, x23
	bl	printk
.L2530:
	ldr	x0, [x20, 504]
	umull	x19, w19, w26
	ldrh	w19, [x0, x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L2528
	add	w22, w22, 1
	and	w22, w22, 65535
	b	.L2529
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	2
	.global	Ftlscanalldata
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	sub	sp, sp, #96
	adrp	x0, .LC145
	mov	w1, 0
	add	x0, x0, :lo12:.LC145
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	mov	w19, 0
	stp	x21, x22, [sp, 48]
	adrp	x22, .LANCHOR4
	add	x20, x22, :lo12:.LANCHOR4
	adrp	x21, .LANCHOR0
	add	x20, x20, 1712
	add	x21, x21, :lo12:.LANCHOR0
	str	x23, [sp, 64]
	bl	printk
.L2543:
	ldr	w0, [x21, 2616]
	cmp	w19, w0
	bcc	.L2549
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x29, x30, [sp, 16]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	ret
.L2549:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w19
	bl	log2phys
	tst	x19, 2047
	bne	.L2544
	ldr	w2, [x29, 76]
	adrp	x0, .LC146
	mov	w1, w19
	add	x0, x0, :lo12:.LC146
	bl	printk
.L2544:
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L2546
	stp	wzr, w0, [x20]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	mov	w2, 0
	str	w19, [x20, 24]
	ldr	x1, [x0, 3608]
	ldr	x23, [x0, 3656]
	mov	x0, x20
	stp	x1, x23, [x20, 8]
	mov	w1, 1
	bl	FlashReadPages
	ldr	w0, [x20]
	cmp	w0, 256
	ccmn	w0, #1, 4, ne
	beq	.L2547
	ldr	w0, [x23, 8]
	cmp	w19, w0
	beq	.L2546
.L2547:
	add	x0, x22, :lo12:.LANCHOR4
	ldr	x2, [x0, 1720]
	ldr	x1, [x0, 1728]
	ldr	w3, [x2, 4]
	str	w3, [sp]
	ldp	w3, w4, [x1]
	ldp	w5, w6, [x1, 8]
	mov	w1, w19
	ldr	w7, [x2]
	ldr	w2, [x0, 1716]
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	bl	printk
.L2546:
	add	w19, w19, 1
	b	.L2543
	.size	Ftlscanalldata, .-Ftlscanalldata
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
	adrp	x20, .LANCHOR2
	add	x2, x20, :lo12:.LANCHOR2
	and	w0, w0, 65535
	ubfiz	x21, x0, 1, 16
	ldr	x3, [x2, 520]
	ldrh	w1, [x3, x21]
	cbnz	w1, .L2552
	ldr	x8, [x2, 544]
	cbz	x8, .L2553
	ldrh	w3, [x2, 552]
	mov	x4, -6148914691236517206
	ldr	x2, [x2, 504]
	movk	x4, 0xaaab, lsl 0
	mov	w5, 65535
	sub	x8, x8, x2
	asr	x8, x8, 1
	mul	x8, x8, x4
	mov	w4, 6
	and	w8, w8, 65535
.L2554:
	cmp	w1, w3
	beq	.L2553
	cmp	w8, w0
	bne	.L2555
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w8
	add	x0, x20, 544
	bl	List_remove_node
	ldrh	w0, [x20, 552]
	sub	w0, w0, #1
	strh	w0, [x20, 552]
	mov	w0, w8
	bl	INSERT_DATA_LIST
	ldr	x1, [x20, 520]
	ldrh	w0, [x1, x21]
	add	w0, w0, 1
	strh	w0, [x1, x21]
.L2553:
	add	x1, x29, 60
	mov	w2, 1
	mov	w0, w19
	bl	log2phys
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L2555:
	umull	x8, w8, w4
	ldrh	w8, [x2, x8]
	cmp	w8, w5
	beq	.L2553
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L2554
.L2552:
	add	w1, w1, 1
	strh	w1, [x3, x21]
	b	.L2553
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	stp	x29, x30, [sp, -192]!
	mov	w2, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w0, [x0]
	cmp	w0, w2
	beq	.L2707
	adrp	x23, .LANCHOR0
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w28, [x20, 2]
	ldrh	w0, [x0, 2544]
	cmp	w0, w28
	bne	.L2564
	strh	wzr, [x20, 4]
.L2714:
	strb	wzr, [x20, 6]
.L2707:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 192
	ret
.L2564:
	ldrh	w0, [x20, 16]
	mov	w1, 0
.L2565:
	cmp	w0, w2
	beq	.L2566
	ldrb	w1, [x20, 6]
	str	w1, [x29, 152]
	ldrb	w1, [x20, 8]
	cmp	w1, 1
	bne	.L2567
	bl	FtlGetLastWrittenPage
	mov	w21, w0
	cmn	w0, #1
	beq	.L2568
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0, 204]
	cbnz	w1, .L2640
	add	x0, x0, 208
	ldrh	w24, [x0, w21, sxtw 1]
.L2569:
	add	x0, x23, :lo12:.LANCHOR0
	add	x1, x20, 16
	mov	x4, x1
	str	x1, [x29, 168]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w8, [x0, 2556]
	mov	w26, 0
	ldrh	w0, [x0, 2472]
	mov	w9, 65535
	mov	w10, 56
	mov	w11, 4
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
.L2570:
	cmp	x4, x0
	bne	.L2572
	ldrb	w0, [x20, 8]
	cmp	w0, 1
	bne	.L2641
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0, 204]
	cmp	w0, 0
	cset	w0, ne
	str	w0, [x29, 164]
.L2573:
	adrp	x19, .LANCHOR2
	add	x25, x19, :lo12:.LANCHOR2
	ldr	w2, [x29, 164]
	mov	w1, w26
	mov	x27, 0
	ldr	x0, [x25, 3584]
	bl	FlashReadPages
	ldr	w22, [x25, 756]
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	sub	w22, w22, #1
	str	x0, [x29, 144]
	mov	w5, 65535
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29, 136]
.L2574:
	and	w0, w27, 65535
	cmp	w26, w0
	bhi	.L2579
	bne	.L2577
	add	x0, x19, :lo12:.LANCHOR2
	add	w21, w21, 1
	and	w21, w21, 65535
	ldr	x0, [x0, 3584]
	ldr	w0, [x0, 4]
.L2709:
	lsr	w0, w0, 10
	bl	P2V_plane
	ldrb	w1, [x20, 8]
	and	w27, w0, 65535
	cmp	w1, 1
	bne	.L2581
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w2, [x0, 204]
	cbnz	w2, .L2581
	add	x0, x0, 208
	ldrh	w21, [x0, w21, sxtw 1]
.L2581:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2544]
	cmp	w0, w21
	bne	.L2582
	strh	w21, [x20, 2]
	strb	wzr, [x20, 6]
	strh	wzr, [x20, 4]
.L2582:
	ldrh	w0, [x29, 152]
	cmp	w21, w28
	str	w0, [x29, 144]
	ccmp	w27, w0, 0, eq
	bne	.L2583
.L2715:
	mov	w2, w27
	mov	w1, w21
	mov	x0, x20
	bl	ftl_sb_update_avl_pages
	b	.L2707
.L2566:
	add	w1, w1, 1
	and	w1, w1, 65535
	add	x0, x20, w1, sxtw 1
	ldrh	w0, [x0, 16]
	b	.L2565
.L2567:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	mov	w21, w0
	cmn	w0, #1
	beq	.L2568
.L2640:
	mov	w24, w21
	b	.L2569
.L2568:
	strh	wzr, [x20, 2]
	b	.L2714
.L2572:
	ldrh	w2, [x4]
	cmp	w2, w9
	beq	.L2571
	umull	x7, w26, w10
	ldr	x6, [x1, 3584]
	orr	w2, w24, w2, lsl 10
	add	w3, w26, 1
	add	x6, x6, x7
	str	w2, [x6, 4]
	mul	w2, w26, w8
	and	w26, w3, 65535
	ldr	x5, [x1, 3584]
	ldr	x6, [x1, 1400]
	sdiv	w2, w2, w11
	add	x5, x5, x7
	add	x2, x6, w2, sxtw 2
	stp	xzr, x2, [x5, 8]
.L2571:
	add	x4, x4, 2
	b	.L2570
.L2641:
	str	wzr, [x29, 164]
	b	.L2573
.L2579:
	mov	x0, 56
	ldr	x4, [x25, 3584]
	mul	x0, x27, x0
	add	x1, x4, x0
	ldr	w0, [x4, x0]
	cbnz	w0, .L2575
	ldr	x6, [x1, 16]
	ldr	w4, [x6, 4]
	cmn	w4, #1
	beq	.L2576
	ldr	w1, [x25, 756]
	mov	w0, w4
	bl	ftl_cmp_data_ver
	cbz	w0, .L2576
	add	w4, w4, 1
	str	w4, [x25, 756]
.L2576:
	ldr	w0, [x6]
	cmn	w0, #1
	bne	.L2578
.L2577:
	add	x0, x19, :lo12:.LANCHOR2
	and	x27, x27, 65535
	mov	x1, 56
	and	w21, w21, 65535
	ldr	x0, [x0, 3584]
	madd	x27, x27, x1, x0
	ldr	w0, [x27, 4]
	b	.L2709
.L2575:
	ldr	w1, [x1, 4]
	ldr	x0, [x29, 144]
	bl	printk
	ldr	x1, [x29, 136]
	and	w5, w24, 65535
	ldrh	w0, [x20]
	strh	w0, [x1, 1842]
.L2578:
	add	x27, x27, 1
	b	.L2574
.L2583:
	mov	w0, 65535
	cmp	w5, w0
	bne	.L2584
	cbnz	w1, .L2585
.L2584:
	add	x0, x19, :lo12:.LANCHOR2
	and	w6, w24, 65535
	ldr	w1, [x0, 3884]
	cmn	w1, #1
	bne	.L2586
	str	w22, [x0, 3884]
.L2586:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w7, [x0, 3884]
	add	w0, w28, 7
	cmp	w0, w24, uxth
	bge	.L2642
	sub	w24, w6, #7
	and	w24, w24, 65535
.L2587:
	add	x4, x19, :lo12:.LANCHOR2
	mov	w3, -1
	add	x8, x4, 1500
	mov	w26, w3
	mov	w5, 65535
.L2588:
	cmp	w24, w6
	bhi	.L2601
	add	x0, x23, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w10, 56
	ldr	x1, [x29, 168]
	ldrh	w0, [x0, 2472]
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
	b	.L2602
.L2642:
	mov	w24, w28
	b	.L2587
.L2590:
	ldrh	w2, [x1]
	cmp	w2, w5
	beq	.L2589
	ldr	x9, [x4, 3584]
	orr	w2, w24, w2, lsl 10
	umaddl	x9, w25, w10, x9
	add	w25, w25, 1
	and	w25, w25, 65535
	str	w2, [x9, 4]
.L2589:
	add	x1, x1, 2
.L2602:
	cmp	x0, x1
	bne	.L2590
	ldr	w2, [x29, 164]
	mov	w1, w25
	ldr	x0, [x4, 3584]
	str	x8, [x29, 104]
	str	w5, [x29, 112]
	str	w7, [x29, 120]
	str	w6, [x29, 128]
	str	w3, [x29, 136]
	str	x4, [x29, 152]
	bl	FlashReadPages
	ldr	x4, [x29, 152]
	add	x0, x23, :lo12:.LANCHOR0
	mov	w1, 56
	ldr	w5, [x29, 112]
	ldr	w7, [x29, 120]
	sxtw	x9, w24
	ldrb	w2, [x0, 204]
	ldr	x0, [x4, 3584]
	ldr	w6, [x29, 128]
	ldr	w3, [x29, 136]
	ldr	x8, [x29, 104]
	nop // between mem op and mult-accumulate
	umaddl	x25, w25, w1, x0
.L2591:
	cmp	x25, x0
	bne	.L2600
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L2588
.L2600:
	ldr	w1, [x0]
	cbnz	w1, .L2592
	ldr	x1, [x0, 16]
	ldrh	w10, [x1]
	cmp	w10, w5
	beq	.L2593
	ldr	w1, [x1, 4]
	cmn	w1, #1
	beq	.L2593
	ldr	w26, [x4, 3884]
	cmn	w3, #1
	str	w1, [x4, 3884]
	bne	.L2593
	ldrh	w1, [x8, x9, lsl 1]
	cmp	w1, w5
	bne	.L2594
	cbz	w2, .L2593
.L2594:
	cmp	w22, w26
	csel	w3, w3, w26, eq
.L2593:
	add	x0, x0, 56
	b	.L2591
.L2592:
	adrp	x0, .LANCHOR4+1842
	ldrh	w1, [x20]
	strh	w1, [x0, #:lo12:.LANCHOR4+1842]
	ldrb	w0, [x20, 8]
	cbnz	w0, .L2585
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 1500
	ldrh	w2, [x1, w24, sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2596
	cmn	w3, #1
	beq	.L2597
	str	w3, [x0, 3884]
.L2585:
	adrp	x0, .LANCHOR4
	add	x1, x0, :lo12:.LANCHOR4
	mov	w24, w28
	add	x19, x19, :lo12:.LANCHOR2
	mov	w2, 1
	str	x0, [x29, 120]
	strh	w2, [x1, 1844]
.L2603:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x1, [x29, 168]
	mov	w25, 0
	mov	w7, 65535
	ldrb	w6, [x0, 204]
	ldrh	w0, [x0, 2472]
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
.L2604:
	cmp	x0, x1
	bne	.L2607
	ldr	w2, [x29, 164]
	mov	w1, w25
	ldr	x0, [x19, 3584]
	bl	FlashReadPages
	mov	w0, 56
	umull	x0, w25, w0
	mov	x25, 0
	str	x0, [x29, 128]
	ldr	x0, [x29, 120]
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29, 152]
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	str	x0, [x29, 112]
	ldr	x0, [x29, 152]
	add	x0, x0, 1856
	str	x0, [x29, 104]
.L2608:
	ldr	x0, [x29, 128]
	cmp	x0, x25
	bne	.L2633
	ldrb	w0, [x20, 8]
	add	w24, w24, 1
	and	w24, w24, 65535
	cmp	w0, 1
	bne	.L2634
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0, 204]
	cbz	w1, .L2634
	ldrh	w0, [x0, 2546]
	cmp	w0, w24
	bne	.L2634
	cmp	w21, w24
	beq	.L2610
.L2634:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2544]
	cmp	w1, w24
	bne	.L2603
	ldrh	w1, [x0, 2472]
	mov	w2, 65535
	strh	w24, [x20, 2]
	mov	w0, 0
	strh	wzr, [x20, 4]
.L2635:
	cmp	w0, w1
	beq	.L2707
	ldr	x4, [x29, 168]
	ldrh	w3, [x4], 2
	str	x4, [x29, 168]
	cmp	w3, w2
	beq	.L2636
	strb	w0, [x20, 6]
	b	.L2707
.L2597:
	cmp	w22, w7
	beq	.L2598
	str	w7, [x0, 3884]
	b	.L2585
.L2598:
	ldr	w1, [x0, 3884]
.L2716:
	sub	w1, w1, #1
.L2710:
	str	w1, [x0, 3884]
	b	.L2585
.L2596:
	cmp	w26, w22
	beq	.L2599
	cmn	w26, #1
	beq	.L2585
	str	w26, [x0, 3884]
	b	.L2585
.L2599:
	ldr	w1, [x0, 3884]
	cmp	w22, w1
	bne	.L2716
	b	.L2585
.L2601:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, -1
	b	.L2710
.L2607:
	ldrh	w3, [x1]
	cmp	w3, w7
	beq	.L2605
	mov	w2, 56
	ldr	x4, [x19, 3584]
	orr	w3, w24, w3, lsl 10
	umull	x5, w25, w2
	add	x4, x4, x5
	str	w3, [x4, 4]
	ldrb	w2, [x20, 8]
	cmp	w2, 1
	bne	.L2606
	cbz	w6, .L2606
	ldr	x2, [x19, 3584]
	add	x2, x2, x5
	ldr	w3, [x2, 4]
	orr	w3, w3, -2147483648
	str	w3, [x2, 4]
.L2606:
	add	w25, w25, 1
	and	w25, w25, 65535
.L2605:
	add	x1, x1, 2
	b	.L2604
.L2633:
	ldr	x4, [x19, 3584]
	add	x4, x4, x25
	ldr	w5, [x4, 4]
	str	w5, [x29, 188]
	lsr	w0, w5, 10
	bl	P2V_plane
	and	w0, w0, 65535
	cmp	w24, w28
	bcc	.L2609
	ldr	w1, [x29, 144]
	ccmp	w1, w0, 0, eq
	bhi	.L2609
	cmp	w24, w21
	ccmp	w27, w0, 0, eq
	beq	.L2610
	ldr	w0, [x4]
	cmn	w0, #1
	beq	.L2611
	ldr	x3, [x4, 16]
	mov	w0, 61589
	ldrh	w1, [x3]
	cmp	w1, w0
	beq	.L2612
	ldrh	w0, [x20]
.L2712:
	bl	decrement_vpc_count
	b	.L2609
.L2612:
	ldr	w22, [x3, 4]
	cmn	w22, #1
	beq	.L2613
	ldr	w1, [x19, 756]
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2613
	add	w0, w22, 1
	str	w0, [x19, 756]
.L2613:
	ldp	w26, w0, [x3, 8]
	add	x1, x29, 184
	str	w0, [x29, 180]
	mov	w2, 0
	mov	w0, w26
	bl	log2phys
	ldr	w1, [x19, 3884]
	ldr	w3, [x29, 180]
	cmn	w1, #1
	beq	.L2614
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2614
	cmn	w3, #1
	beq	.L2615
	ldr	x0, [x19, 3584]
	mov	w2, 0
	mov	w1, 1
	add	x0, x0, x25
	ldr	x4, [x0, 16]
	str	w3, [x0, 4]
	str	x4, [x29, 136]
	ldr	x0, [x19, 3584]
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x0, [x19, 3584]
	ldr	x4, [x29, 136]
	add	x3, x0, x25
	ldr	w0, [x0, x25]
	cmn	w0, #1
	bne	.L2616
.L2617:
	mov	w0, -1
	str	w0, [x29, 180]
.L2624:
	ldr	w4, [x29, 180]
	cmn	w4, #1
	beq	.L2609
.L2639:
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldr	x3, [x19, 520]
	and	w1, w0, 65535
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3, x2]
	cbnz	w2, .L2712
	adrp	x0, .LC149
	add	x0, x0, :lo12:.LC149
	bl	printk
	b	.L2609
.L2615:
	ldp	w1, w0, [x29, 184]
	cmp	w1, w0
	bne	.L2609
	mov	w2, 1
	add	x1, x29, 180
	mov	w0, w26
	bl	log2phys
.L2609:
	add	x25, x25, 56
	b	.L2608
.L2616:
	ldr	w0, [x4, 8]
	cmp	w26, w0
	bne	.L2617
	ldr	w0, [x4, 4]
	str	w0, [x29, 136]
	str	x4, [x29, 96]
	uxtw	x1, w0
	ldr	w0, [x19, 3884]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2617
	ldp	w0, w1, [x29, 184]
	ldr	x4, [x29, 96]
	cmp	w0, w1
	ldr	w1, [x29, 180]
	bne	.L2619
.L2711:
	mov	w0, w26
	bl	FtlReUsePrevPpa
	b	.L2617
.L2619:
	cmp	w0, w1
	beq	.L2617
	cmn	w0, #1
	beq	.L2620
	ldr	x4, [x3, 16]
	mov	w2, 0
	str	w0, [x3, 4]
	mov	w1, 1
	str	x4, [x29, 96]
	ldr	x0, [x19, 3584]
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x4, [x29, 96]
.L2621:
	ldr	x0, [x19, 3584]
	ldr	w0, [x0, x25]
	cmn	w0, #1
	beq	.L2622
	ldr	w3, [x4, 4]
	ldr	w0, [x19, 3884]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L2622
	ldr	w0, [x29, 136]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L2617
.L2622:
	ldr	w1, [x29, 180]
	b	.L2711
.L2620:
	str	w0, [x3]
	b	.L2621
.L2614:
	ldp	w1, w0, [x29, 184]
	cmp	w1, w0
	beq	.L2624
	cmn	w3, #1
	beq	.L2626
	add	x0, x23, :lo12:.LANCHOR0
	ubfx	x3, x3, 10, 21
	ldr	w0, [x0, 2488]
	cmp	w3, w0
	bcs	.L2609
.L2626:
	mov	w2, 1
	add	x1, x29, 188
	mov	w0, w26
	bl	log2phys
	ldr	w4, [x29, 184]
	cmn	w4, #1
	beq	.L2624
	ldr	w0, [x29, 180]
	cmp	w4, w0
	beq	.L2639
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19, 560]
	and	w0, w0, 65535
	cmp	w1, w0
	beq	.L2629
	ldrh	w1, [x19, 608]
	cmp	w1, w0
	beq	.L2629
	ldrh	w1, [x19, 656]
	cmp	w1, w0
	bne	.L2624
.L2629:
	ldr	x0, [x19, 3584]
	mov	w2, 0
	mov	w1, 1
	str	w4, [x0, 4]
	ldr	x3, [x0, 16]
	ldr	x0, [x19, 3584]
	str	x3, [x29, 136]
	bl	FlashReadPages
	ldr	x0, [x19, 3584]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2624
	ldr	x3, [x29, 136]
	mov	w0, w22
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2624
	mov	w2, 1
	add	x1, x29, 184
	mov	w0, w26
	bl	log2phys
	b	.L2624
.L2611:
	ldr	x1, [x29, 152]
	mov	w2, w22
	ldrh	w0, [x20]
	strh	w0, [x1, 1842]
	mov	w1, w5
	ldr	x0, [x29, 112]
	bl	printk
	ldr	x0, [x29, 152]
	ldr	w0, [x0, 1848]
	cmp	w0, 31
	bhi	.L2631
	ldr	x2, [x29, 104]
	ldr	w1, [x29, 188]
	str	w1, [x2, w0, uxtw 2]
	add	w0, w0, 1
	ldr	x1, [x29, 152]
	str	w0, [x1, 1848]
.L2631:
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	ldr	w0, [x19, 3884]
	cmn	w0, #1
	bne	.L2632
.L2713:
	str	w22, [x19, 3884]
	b	.L2609
.L2632:
	cmp	w22, w0
	bcs	.L2609
	b	.L2713
.L2636:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L2635
.L2610:
	strb	w27, [x20, 6]
	strh	w21, [x20, 2]
	b	.L2715
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC109
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	stp	x23, x24, [sp, 48]
	add	x23, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x22, x21, :lo12:.LANCHOR0
	add	x1, x1, 232
	add	x0, x0, :lo12:.LC109
	bl	printk
	ldr	x0, [x23, 3704]
	mov	w1, 0
	ldrh	w2, [x22, 2482]
	mov	w19, 0
	lsl	w2, w2, 1
	bl	ftl_memset
.L2718:
	ldr	w0, [x22, 2616]
	cmp	w19, w0
	bcc	.L2720
	adrp	x22, .LC151
	add	x23, x21, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR2
	add	x22, x22, :lo12:.LC151
	mov	w19, 0
	mov	w24, 65535
.L2721:
	ldrh	w0, [x23, 2480]
	cmp	w0, w19
	bhi	.L2724
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L2720:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w19
	bl	log2phys
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L2719
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x23, 3704]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L2719:
	add	w19, w19, 1
	b	.L2718
.L2724:
	ldr	x0, [x20, 520]
	ubfiz	x21, x19, 1, 16
	ldrh	w2, [x0, x21]
	ldr	x0, [x20, 3704]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L2722
	cmp	w2, w24
	beq	.L2722
	ldrh	w0, [x20, 560]
	cmp	w0, w19
	beq	.L2722
	ldrh	w0, [x20, 656]
	cmp	w0, w19
	beq	.L2722
	ldrh	w0, [x20, 608]
	cmp	w0, w19
	beq	.L2722
	mov	w1, w19
	mov	x0, x22
	bl	printk
	ldr	x0, [x20, 520]
	ldrh	w1, [x0, x21]
	cbnz	w1, .L2723
	ldr	x1, [x20, 3704]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
.L2722:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L2721
.L2723:
	ldr	x1, [x20, 3704]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
	mov	w0, w19
	bl	update_vpc_list
	b	.L2722
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	stp	x29, x30, [sp, -208]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	adrp	x0, .LANCHOR1
	stp	x21, x22, [sp, 32]
	str	w1, [x29, 132]
	add	x1, x0, :lo12:.LANCHOR1
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	str	x0, [x29, 120]
	ldrh	w28, [x1, 3456]
	mov	w1, 65535
	cmp	w28, w1
	beq	.L2760
	cbnz	w28, .L2730
.L2731:
	bl	FtlGcPageVarInit
	b	.L2732
.L2760:
	mov	w28, 0
.L2730:
	adrp	x0, .LANCHOR0+2544
	ldr	w1, [x29, 132]
	ldrh	w0, [x0, #:lo12:.LANCHOR0+2544]
	cmp	w0, w1
	beq	.L2731
.L2732:
	adrp	x25, .LANCHOR2
	mov	x21, x25
	add	x26, x25, :lo12:.LANCHOR2
	mov	w0, -1
	mov	w24, 0
	str	w0, [x29, 136]
.L2733:
	ldrh	w0, [x19]
	mov	w23, 65535
	strb	wzr, [x19, 8]
	cmp	w0, w23
	beq	.L2761
.L2757:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	add	x2, x19, 16
	add	x4, x25, :lo12:.LANCHOR2
	mov	w22, 0
	mov	w10, 56
	ldrh	w8, [x0, 2554]
	mov	w7, 4
	ldrh	w9, [x0, 2556]
	ldrh	w0, [x0, 2472]
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
.L2735:
	cmp	x2, x0
	bne	.L2737
	add	x0, x25, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, 0
	add	x27, x20, :lo12:.LANCHOR0
	ldr	x0, [x0, 3584]
	bl	FlashReadPages
	mov	w0, 56
	umull	x0, w22, w0
	mov	x22, 0
	str	x0, [x29, 112]
.L2738:
	ldr	x0, [x29, 112]
	cmp	x0, x22
	bne	.L2755
	ldr	w0, [x29, 132]
	add	w3, w28, 1
	add	w24, w24, 1
	and	w28, w3, 65535
	cmp	w0, w24
	bls	.L2756
.L2758:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20, 2544]
	cmp	w0, w28
	bhi	.L2757
.L2761:
	mov	w2, 0
	b	.L2734
.L2737:
	ldrh	w1, [x2]
	cmp	w1, w23
	beq	.L2736
	umull	x11, w22, w10
	ldr	x6, [x4, 3584]
	orr	w1, w28, w1, lsl 10
	add	x6, x6, x11
	str	w1, [x6, 4]
	mul	w1, w22, w8
	ldr	x5, [x4, 3584]
	ldr	x6, [x4, 1392]
	sdiv	w1, w1, w7
	add	x5, x5, x11
	add	x1, x6, w1, sxtw 2
	str	x1, [x5, 8]
	mul	w1, w22, w9
	ldr	x6, [x4, 1400]
	add	w22, w22, 1
	and	w22, w22, 65535
	sdiv	w1, w1, w7
	add	x1, x6, w1, sxtw 2
	str	x1, [x5, 16]
.L2736:
	add	x2, x2, 2
	b	.L2735
.L2755:
	ldr	x5, [x26, 3584]
	add	x4, x5, x22
	ldr	w0, [x4, 4]
	str	w0, [x29, 140]
	uxtw	x1, w0
	lsr	w0, w1, 10
	bl	P2V_plane
	and	w2, w0, 65535
	ldr	w0, [x5, x22]
	ldr	x4, [x4, 16]
	cbnz	w0, .L2739
	ldrh	w0, [x4]
	cmp	w0, w23
	bne	.L2740
.L2743:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20, 204]
	cbz	w0, .L2777
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, 1
	str	w0, [x21, 3880]
.L2734:
	ldr	x0, [x29, 120]
	mov	w1, -1
	strh	w28, [x19, 2]
	add	x0, x0, :lo12:.LANCHOR1
	strb	w2, [x19, 6]
	strh	w1, [x0, 3456]
	mov	w1, w28
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
	b	.L2729
.L2740:
	ldr	w0, [x4, 8]
	ldr	w1, [x27, 2616]
	cmp	w0, w1
	bhi	.L2743
	ldrb	w1, [x27, 72]
	cbnz	w1, .L2746
.L2747:
	ldp	w2, w0, [x4, 8]
	add	x22, x22, 56
	ldr	w1, [x29, 140]
	bl	FtlGcUpdatePage
	b	.L2738
.L2746:
	add	x1, x29, 148
	str	x4, [x29, 104]
	mov	w2, 0
	bl	log2phys
	ldr	x4, [x29, 104]
	ldr	w1, [x29, 148]
	ldr	w0, [x4, 12]
	cmp	w0, w1
	bne	.L2747
	cmn	w0, #1
	beq	.L2747
	str	w0, [x29, 156]
	mov	w2, 0
	ldr	x0, [x26, 3648]
	mov	w1, 1
	str	x0, [x29, 160]
	ldr	x0, [x26, 3664]
	str	x0, [x29, 168]
	add	x0, x29, 152
	bl	FlashReadPages
	ldrh	w1, [x27, 2550]
	mov	x0, 0
	ldr	x2, [x26, 3584]
	ldr	x4, [x29, 104]
	ldr	x5, [x29, 160]
	ubfiz	x1, x1, 9, 16
	add	x2, x2, x22
.L2748:
	cmp	x0, x1
	beq	.L2747
	ldr	x6, [x2, 8]
	ldr	w7, [x6, x0]
	add	x0, x0, 4
	add	x6, x5, x0
	ldr	w6, [x6, -4]
	cmp	w7, w6
	beq	.L2748
	ldrh	w1, [x19]
	adrp	x0, .LC152
	ldr	w2, [x29, 156]
	add	x0, x0, :lo12:.LC152
	bl	printk
.L2777:
	add	x13, x21, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x13, 520]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x13, 800]
.L2776:
	bl	FtlGcPageVarInit
	mov	w28, 0
	b	.L2733
.L2739:
	ldrh	w1, [x19]
	add	x20, x20, :lo12:.LANCHOR0
	ldr	w2, [x29, 140]
	adrp	x0, .LC153
	add	x0, x0, :lo12:.LC153
	bl	printk
	ldr	w1, [x20, 2372]
	ldrh	w0, [x19]
	cbnz	w1, .L2751
	ldrb	w1, [x20, 204]
	cbz	w1, .L2752
.L2751:
	add	x2, x21, :lo12:.LANCHOR2
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2, 440]
	ldrh	w1, [x2, x1]
	cmp	w1, 159
	bls	.L2753
.L2752:
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x1, [x1, 3584]
	ldr	w1, [x1, x22]
	cmn	w1, #1
	bne	.L2754
.L2753:
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x1, [x1, 3584]
	add	x22, x1, x22
	ldr	w1, [x22, 4]
	str	w1, [x29, 136]
.L2754:
	add	x1, x21, :lo12:.LANCHOR2
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1, 520]
	strh	wzr, [x1, x0]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	b	.L2776
.L2756:
	ldr	x0, [x29, 120]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1, 3456]
	cmp	w0, w23
	beq	.L2758
	add	w0, w0, w24
	strh	w0, [x1, 3456]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2544]
	cmp	w0, w28
	bls	.L2758
.L2729:
	ldr	w0, [x29, 136]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	adrp	x1, .LANCHOR2
	add	x0, x1, :lo12:.LANCHOR2
	add	x2, x0, 848
	ldr	w3, [x2, 80]
	cbz	w3, .L2779
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w4, [x2, 84]
	ldr	w3, [x1, 2616]
	cmp	w4, w3
	bcs	.L2780
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	x20, x1
	stp	x21, x22, [sp, 32]
	mov	w21, 2048
.L2785:
	add	x22, x19, 848
	ldr	w1, [x20, 2616]
	ldr	w0, [x22, 84]
	cmp	w0, w1
	bcc	.L2781
.L2784:
	ldp	x19, x20, [sp, 16]
	mov	w0, -1
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 112
	ret
.L2781:
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x22, 84]
	ldr	w1, [x29, 52]
	add	w0, w0, 1
	str	w0, [x22, 84]
	cmn	w1, #1
	beq	.L2783
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
	bne	.L2784
	ldr	w0, [x29, 52]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2784
.L2783:
	subs	w21, w21, #1
	bne	.L2785
	b	.L2784
.L2780:
	ldr	w0, [x0, 720]
	stp	w0, wzr, [x2, 76]
	str	wzr, [x2, 84]
.L2792:
	mov	w0, 0
	ret
.L2779:
	ldr	w4, [x0, 772]
	mov	w3, 10000
	ldr	w5, [x0, 720]
	mov	w6, 31
	cmp	w4, w3
	ldr	w7, [x2, 76]
	mov	w3, 63
	csel	w6, w6, w3, hi
	add	w3, w5, 1048576
	cmp	w7, w3
	bhi	.L2789
	adrp	x3, .LANCHOR0+2616
	mov	w8, 1000
	lsr	w4, w4, 10
	ldr	w3, [x3, #:lo12:.LANCHOR0+2616]
	add	w4, w4, 1
	mul	w3, w3, w8
	udiv	w3, w3, w4
	add	w3, w3, w7
	cmp	w5, w3
	bhi	.L2789
	ldrh	w0, [x0, 484]
	tst	w6, w0
	bne	.L2792
	ldr	w2, [x2, 100]
	cmp	w0, w2
	beq	.L2792
.L2789:
	add	x0, x1, :lo12:.LANCHOR2
	add	x1, x0, 848
	ldrh	w0, [x0, 484]
	str	w0, [x1, 100]
	str	w5, [x1, 76]
	mov	w0, 1
	str	wzr, [x1, 84]
	str	w0, [x1, 80]
	b	.L2792
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	adrp	x21, .LANCHOR0
	stp	x25, x26, [sp, 64]
	add	x1, x21, :lo12:.LANCHOR0
	str	x27, [sp, 80]
	ldr	w3, [x2, 424]
	ldrh	w1, [x1, 2544]
	cbz	w3, .L2799
.L2836:
	mov	w0, 0
.L2798:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2799:
	ldrh	w20, [x2, 656]
	mov	w3, 65535
	cmp	w20, w3
	bne	.L2801
.L2810:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x22, x20, 656
	ldrh	w1, [x20, 656]
	str	wzr, [x20, 3880]
	cmp	w1, w0
	beq	.L2836
	add	x23, x21, :lo12:.LANCHOR0
	bl	FtlCacheWriteBack
	ldrb	w0, [x22, 7]
	mov	w26, 12
	ldr	x1, [x20, 520]
	mov	w22, 0
	ldrh	w3, [x23, 2544]
	ldrh	w2, [x20, 656]
	mul	w0, w0, w3
	strh	w0, [x1, x2, lsl 1]
	ldr	w1, [x20, 740]
	ldrh	w0, [x20, 1432]
	add	w0, w0, w1
	str	w0, [x20, 740]
.L2811:
	ldrh	w0, [x20, 1432]
	cmp	w0, w22
	bhi	.L2815
	mov	w0, -1
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0, 204]
	cbz	w0, .L2816
	ldrh	w1, [x20, 656]
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	bl	printk
.L2816:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1, 656]
	ldr	x1, [x1, 520]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1, x2]
	cbz	w1, .L2817
	bl	INSERT_DATA_LIST
.L2818:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w22, -1
	strh	wzr, [x20, 1432]
	strh	w22, [x20, 656]
	strh	wzr, [x20, 1420]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w22, [x20, 800]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0, 2372]
	cbz	w0, .L2819
	ldr	w0, [x20, 776]
	cmp	w0, 39
	bhi	.L2819
	ldrh	w0, [x20, 780]
	ldrh	w1, [x20, 552]
	cmp	w1, w0
	bcs	.L2836
	ubfiz	w0, w0, 1, 15
	strh	w0, [x20, 1364]
	b	.L2836
.L2801:
	cbz	w0, .L2804
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w4, [x0, 3456]
	cmp	w4, w3
	beq	.L2805
.L2806:
	mov	w1, 2
.L2804:
	add	x22, x19, :lo12:.LANCHOR2
	add	x0, x22, 656
	bl	FtlGcScanTempBlk
	str	w0, [x29, 108]
	cmn	w0, #1
	beq	.L2807
	ldr	x1, [x22, 440]
	ubfiz	x20, x20, 1, 16
	ldrh	w0, [x1, x20]
	cmp	w0, 4
	bls	.L2808
	sub	w0, w0, #5
	strh	w0, [x1, x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2808:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0, 3880]
	cbnz	w1, .L2809
	ldr	w1, [x0, 944]
	add	w1, w1, 1
	str	w1, [x0, 944]
	ldr	w0, [x29, 108]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2809:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19, 3880]
.L2821:
	mov	w0, 1
	b	.L2798
.L2805:
	strh	wzr, [x0, 3456]
	ldrh	w0, [x2, 552]
	cmp	w0, 17
	bhi	.L2806
	b	.L2804
.L2807:
	adrp	x0, .LANCHOR1+3456
	ldrh	w1, [x0, #:lo12:.LANCHOR1+3456]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2821
	b	.L2810
.L2815:
	umull	x25, w22, w26
	ldr	x27, [x20, 1440]
	ldr	w1, [x23, 2616]
	add	x24, x27, x25
	ldr	w0, [x24, 8]
	cmp	w0, w1
	bcc	.L2812
.L2833:
	ldrh	w0, [x20, 656]
	b	.L2834
.L2812:
	add	x1, x29, 108
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x27, x25]
	ldr	w1, [x29, 108]
	cmp	w0, w1
	bne	.L2814
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	mov	w25, w0
	ldr	w0, [x24, 8]
	mov	w2, 1
	add	x1, x24, 4
	bl	log2phys
	mov	w0, w25
.L2834:
	bl	decrement_vpc_count
	b	.L2813
.L2814:
	ldr	w0, [x24, 4]
	cmp	w1, w0
	bne	.L2833
.L2813:
	add	w22, w22, 1
	and	w22, w22, 65535
	b	.L2811
.L2817:
	bl	INSERT_FREE_LIST
	b	.L2818
.L2819:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19, 780]
	ldrh	w1, [x19, 552]
	add	w2, w0, w0, lsl 1
	cmp	w1, w2, lsr 2
	ble	.L2836
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x21, 204]
	cbz	w1, .L2820
	sub	w0, w0, #2
.L2835:
	strh	w0, [x19, 1364]
	b	.L2836
.L2820:
	mov	w0, 20
	b	.L2835
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	str	x21, [sp, 32]
	add	x21, x19, 656
	ldrh	w1, [x20, 2544]
	mov	x0, x21
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19, 658]
	ldrh	w0, [x20, 2544]
	cmp	w1, w0
	bcc	.L2837
	add	x0, x19, 3792
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19, 3880]
.L2837:
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
	adrp	x0, .LANCHOR4+1848
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x0, #:lo12:.LANCHOR4+1848]
	add	x20, x19, 560
	add	x19, x19, 608
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
	ldp	x19, x20, [sp, 16]
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
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x24, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR4
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x19, .LANCHOR2
	add	x23, x19, :lo12:.LANCHOR2
	strh	w1, [x0, 1842]
	strh	wzr, [x0, 1844]
	ldrh	w0, [x24, 2476]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x23, 784]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2843
.L2845:
	mov	w22, -1
.L2842:
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L2843:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L2845
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldr	x1, [x23, 704]
	mov	w0, 0
	ldrh	w3, [x24, 2582]
	add	x1, x1, 4
.L2846:
	cmp	w0, w3
	bge	.L2851
	ldr	w2, [x1], 16
	tbz	w2, #31, .L2847
.L2851:
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w0, w3
	ldrh	w2, [x1, 484]
	add	w2, w2, 1
	strh	w2, [x1, 484]
	bge	.L2858
.L2848:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0, 560]
	ldr	x4, [x0, 520]
	ldrh	w5, [x0, 564]
	lsl	x2, x2, 1
	ldrh	w3, [x4, x2]
	sub	w3, w3, w5
	strh	w3, [x4, x2]
	add	x4, x20, :lo12:.LANCHOR0
	strb	wzr, [x0, 566]
	ldr	x5, [x0, 520]
	strh	wzr, [x0, 564]
	ldrh	w2, [x4, 2544]
	strh	w2, [x0, 562]
	ldrh	w2, [x0, 608]
	ldrh	w6, [x0, 612]
	lsl	x2, x2, 1
	ldrh	w3, [x5, x2]
	sub	w3, w3, w6
	strh	w3, [x5, x2]
	strb	wzr, [x0, 614]
	ldrh	w1, [x0, 486]
	ldrh	w2, [x4, 2544]
	add	w1, w1, 1
	strh	w2, [x0, 610]
	strh	w1, [x0, 486]
	strh	wzr, [x0, 612]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L2852
.L2847:
	add	w0, w0, 1
	b	.L2846
.L2858:
	add	x21, x21, :lo12:.LANCHOR4
	ldrh	w0, [x21, 1844]
	cbnz	w0, .L2848
.L2852:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x24, x21, 560
	ldrh	w0, [x21, 560]
	cmp	w0, w1
	beq	.L2853
	ldrh	w1, [x21, 564]
	cbnz	w1, .L2853
	ldrh	w1, [x21, 612]
	add	x23, x21, 608
	cbnz	w1, .L2853
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x21, 608]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x24
	bl	allocate_new_data_superblock
	mov	x0, x23
	bl	allocate_new_data_superblock
.L2853:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20, 72]
	cbnz	w0, .L2854
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19, 484]
	tst	x0, 31
	bne	.L2842
.L2854:
	bl	FtlVpcCheckAndModify
	b	.L2842
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp, 32]
	add	x21, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	ldr	w0, [x21, 424]
	cbnz	w0, .L2862
	adrp	x20, .LANCHOR0
	add	x22, x20, :lo12:.LANCHOR0
	ldr	x0, [x21, 3760]
	mov	w1, 0
	ldrh	w2, [x22, 2580]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x21, 3752]
	mov	w1, 0
	ldrh	w2, [x22, 2580]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x22, 2476]
	str	wzr, [x21, 752]
	str	wzr, [x21, 756]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2863
	bl	FtlMakeBbt
.L2863:
	mov	w5, 23752
	add	x2, x20, :lo12:.LANCHOR0
	add	x3, x19, :lo12:.LANCHOR2
	mov	w0, 0
	movk	w5, 0xa0f, lsl 16
.L2864:
	ldrh	w1, [x2, 2550]
	cmp	w0, w1, lsl 7
	blt	.L2865
	ldrh	w22, [x2, 2480]
	add	x23, x20, :lo12:.LANCHOR0
	mov	w21, 0
.L2866:
	ldrh	w0, [x23, 2482]
	cmp	w0, w22
	bhi	.L2867
	ldrh	w0, [x23, 2472]
	sub	w1, w21, #3
	cmp	w1, w0, lsl 1
	blt	.L2868
	udiv	w0, w21, w0
	ldr	w21, [x23, 2576]
	add	w0, w0, w21
	bl	FtlSysBlkNumInit
	ldrh	w0, [x23, 2476]
	mov	w21, 0
	bl	FtlFreeSysBlkQueueInit
	ldrh	w22, [x23, 2480]
	add	x23, x20, :lo12:.LANCHOR0
.L2869:
	ldrh	w0, [x23, 2482]
	cmp	w0, w22
	bhi	.L2870
.L2868:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w22, 0
	mov	w24, 0
.L2871:
	ldrh	w0, [x23, 2480]
	cmp	w0, w24
	bhi	.L2872
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x23, 2482]
	ldr	w2, [x23, 2484]
	str	w0, [x1, 3572]
	ldrh	w0, [x23, 2472]
	udiv	w4, w2, w0
	ubfx	x3, x4, 5, 16
	str	w4, [x23, 2616]
	add	w5, w3, 36
	strh	w5, [x1, 780]
	mov	w5, 24
	mul	w5, w0, w5
	cmp	w22, w5
	ble	.L2873
	sub	w2, w2, w22
	udiv	w2, w2, w0
	str	w2, [x23, 2616]
	lsr	w2, w2, 5
	add	w2, w2, 24
	strh	w2, [x1, 780]
.L2873:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w1, [x1, 2372]
	cmp	w1, 1
	bne	.L2874
	udiv	w2, w22, w0
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w5, [x1, 780]
	add	w2, w2, w5
	add	w2, w5, w2, asr 2
	strh	w2, [x1, 780]
.L2874:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w1, [x1, 204]
	cbz	w1, .L2875
	udiv	w2, w22, w0
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w5, [x1, 780]
	add	w2, w2, w5
	add	w2, w5, w2, asr 2
	strh	w2, [x1, 780]
.L2875:
	add	x6, x20, :lo12:.LANCHOR0
	ldrh	w1, [x6, 2538]
	cbz	w1, .L2877
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w5, [x2, 780]
	add	w5, w5, w1, lsr 1
	strh	w5, [x2, 780]
	mul	w5, w1, w0
	cmp	w22, w5
	bge	.L2877
	add	w1, w1, 32
	str	w4, [x6, 2616]
	add	w1, w3, w1
	strh	w1, [x2, 780]
.L2877:
	add	x24, x20, :lo12:.LANCHOR0
	add	x1, x19, :lo12:.LANCHOR2
	adrp	x23, .LANCHOR4
	ldrh	w2, [x1, 780]
	ldr	w1, [x24, 2616]
	sub	w1, w1, w2
	mul	w0, w1, w0
	add	x1, x23, :lo12:.LANCHOR4
	str	w0, [x1, 1768]
	ldrh	w1, [x24, 2544]
	mul	w0, w1, w0
	ldrh	w1, [x24, 2550]
	str	w0, [x24, 2616]
	mul	w0, w1, w0
	str	w0, [x24, 2584]
	bl	FtlBbmTblFlush
	ldrh	w0, [x24, 2558]
	add	w1, w21, w22
	ldr	w2, [x24, 2488]
	add	w0, w0, w2, lsr 3
	cmp	w1, w0
	bls	.L2879
	adrp	x0, .LC155
	lsr	w2, w2, 5
	add	x0, x0, :lo12:.LC155
	bl	printk
.L2879:
	add	x22, x20, :lo12:.LANCHOR0
	add	x20, x19, :lo12:.LANCHOR2
	add	x21, x20, 560
	mov	w1, 0
	mov	w24, -1
	ldr	x0, [x20, 520]
	ldrh	w2, [x22, 2482]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w0, 1
	strb	w0, [x21, 8]
	ldr	x0, [x22, 64]
	mov	w1, 255
	ldrh	w2, [x22, 2480]
	strh	w24, [x20, 800]
	strh	wzr, [x20, 802]
	strb	wzr, [x20, 806]
	lsr	w2, w2, 3
	strb	wzr, [x20, 808]
	strh	wzr, [x21, 2]
	strb	wzr, [x21, 6]
	strh	wzr, [x20, 560]
	str	wzr, [x20, 716]
	bl	ftl_memset
.L2880:
	mov	x0, x21
	bl	make_superblock
	ldrb	w1, [x21, 7]
	ldrh	w0, [x21]
	cbnz	w1, .L2881
	ldr	x1, [x20, 520]
	ubfiz	x0, x0, 1, 16
	strh	w24, [x1, x0]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L2880
.L2865:
	ldr	x6, [x3, 3640]
	ubfiz	x4, x0, 2, 16
	mvn	w1, w0
	orr	w1, w0, w1, lsl 16
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w1, [x6, x4]
	ldr	x1, [x3, 3648]
	str	w5, [x1, x4]
	b	.L2864
.L2867:
	mov	w0, w22
	mov	w1, 1
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w21, w21, w0
	and	w22, w22, 65535
	and	w21, w21, 65535
	b	.L2866
.L2870:
	mov	w0, w22
	mov	w1, 1
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w21, w21, w0
	and	w22, w22, 65535
	and	w21, w21, 65535
	b	.L2869
.L2872:
	mov	w0, w24
	mov	w1, 0
	add	w24, w24, 1
	bl	FtlLowFormatEraseBlock
	add	w22, w22, w0
	and	w24, w24, 65535
	and	w22, w22, 65535
	b	.L2871
.L2881:
	ldr	w1, [x20, 752]
	ubfiz	x0, x0, 1, 16
	str	w1, [x21, 12]
	add	x19, x19, :lo12:.LANCHOR2
	add	w1, w1, 1
	str	w1, [x20, 752]
	ldr	x1, [x20, 520]
	mov	w13, -1
	ldrh	w2, [x21, 4]
	strh	w2, [x1, x0]
	add	x0, x20, 608
	mov	x12, x0
	strh	wzr, [x20, 610]
	ldrh	w1, [x21]
	strb	wzr, [x20, 614]
	add	w1, w1, 1
	strh	w1, [x20, 608]
	mov	w1, 1
	strb	w1, [x20, 616]
.L2882:
	mov	x0, x12
	bl	make_superblock
	ldrb	w1, [x12, 7]
	ldrh	w0, [x12]
	cbnz	w1, .L2883
	ldr	x1, [x19, 520]
	ubfiz	x0, x0, 1, 16
	strh	w13, [x1, x0]
	ldrh	w0, [x12]
	add	w0, w0, 1
	strh	w0, [x12]
	b	.L2882
.L2883:
	ldr	w1, [x19, 752]
	ubfiz	x0, x0, 1, 16
	str	w1, [x12, 12]
	add	x23, x23, :lo12:.LANCHOR4
	add	w1, w1, 1
	str	w1, [x19, 752]
	ldr	x1, [x19, 520]
	mov	w20, -1
	ldrh	w2, [x12, 4]
	strh	w2, [x1, x0]
	strh	w20, [x19, 656]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19, 784]
	ldr	w0, [x23, 1768]
	strh	w0, [x19, 790]
	ldr	w0, [x19, 752]
	str	w0, [x19, 792]
	add	w0, w0, 1
	strh	wzr, [x19, 786]
	strh	w20, [x19, 788]
	str	w0, [x19, 752]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L2862
	adrp	x0, .LANCHOR1+504
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR1+504]
.L2862:
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	ldrb	w0, [x0, 204]
	cbz	w0, .L2893
.L2895:
	mov	w21, 0
.L2892:
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 64
	ret
.L2893:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0, 1584]
	bl	FlashInit
	mov	w21, w0
	cbnz	w0, .L2895
	bl	FlashLoadFactorBbt
	cbz	w0, .L2896
	bl	FlashMakeFactorBbt
.L2896:
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20, 1656]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L2897
	mov	w2, 16
	mov	w1, 0
	add	x0, x29, 48
	bl	FlashReadFacBbtData
	ldr	w2, [x29, 48]
	mov	w0, 0
	mov	w1, 0
	mov	w4, 1
.L2899:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cinc	w0, w0, ne
	cmp	w1, 16
	bne	.L2899
	cmp	w0, 6
	bhi	.L2900
	add	x0, x19, :lo12:.LANCHOR0
.L2923:
	strb	w1, [x0, 73]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0, 73]
	strh	w1, [x0, 202]
.L2897:
	adrp	x1, .LC75
	add	x1, x1, :lo12:.LC75
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC76
	add	x0, x0, :lo12:.LC76
	bl	printk
	add	x0, x19, 176
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19, 2476]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L2905:
	bl	FtlLoadBbt
	cbz	w0, .L2906
.L2925:
	bl	FtlLowFormat
	cmp	w19, 3
	bls	.L2907
	mov	w21, -1
	b	.L2892
.L2900:
	mov	w1, 0
	mov	w4, 1
.L2903:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cinc	w0, w0, ne
	cmp	w1, 24
	bne	.L2903
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L2923
	mov	w1, 36
	b	.L2923
.L2907:
	add	w19, w19, 1
	b	.L2905
.L2906:
	bl	FtlSysBlkInit
	cbnz	w0, .L2925
	adrp	x0, .LANCHOR1+504
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR1+504]
	b	.L2892
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	adrp	x12, .LANCHOR2
	add	x0, x12, :lo12:.LANCHOR2
	ldr	w1, [x0, 424]
	cbz	w1, .L2927
.L2941:
	mov	w0, 0
	ret
.L2930:
	mov	w0, 0
.L2926:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2927:
	adrp	x1, .LANCHOR0+204
	ldrb	w1, [x1, #:lo12:.LANCHOR0+204]
	cbz	w1, .L2929
	ldr	w1, [x0, 1380]
	tbz	x1, 0, .L2929
	ldrh	w0, [x0, 660]
	cbnz	w0, .L2941
.L2929:
	stp	x29, x30, [sp, -32]!
	mov	w3, 0
	mov	w2, 0
	add	x29, sp, 0
	str	x19, [sp, 16]
	add	x19, x12, :lo12:.LANCHOR2
	ldr	w1, [x19, 1380]
	ldr	x0, [x19, 3592]
	bl	FlashProgPages
	mov	w10, 0
	mov	w11, 56
.L2931:
	ldr	w1, [x19, 1380]
	cmp	w10, w1
	bcc	.L2933
	ldr	x0, [x19, 3592]
	bl	FtlGcBufFree
	str	wzr, [x19, 1380]
	ldrh	w0, [x19, 660]
	cbnz	w0, .L2930
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	b	.L2943
.L2933:
	umull	x1, w10, w11
	ldr	x2, [x19, 3592]
	add	x3, x2, x1
	ldr	w2, [x2, x1]
	ldr	x0, [x3, 16]
	cmn	w2, #1
	bne	.L2932
	ldrh	w3, [x19, 656]
	ldr	x0, [x19, 520]
	strh	wzr, [x0, x3, lsl 1]
	strh	w2, [x19, 656]
	ldr	w0, [x19, 944]
	add	w0, w0, 1
	str	w0, [x19, 944]
	ldr	x0, [x19, 3592]
	add	x0, x0, x1
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L2943:
	mov	w0, 1
	b	.L2926
.L2932:
	ldp	w2, w0, [x0, 8]
	ldr	w1, [x3, 4]
	bl	FtlGcUpdatePage
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L2931
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 656
	ldrh	w2, [x0, 656]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2945
	ldrh	w0, [x1, 4]
	cbnz	w0, .L2946
.L2945:
	bl	FtlCacheWriteBack
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x20, 656
	strb	wzr, [x0, 8]
	bl	allocate_data_superblock
	strh	wzr, [x20, 1420]
	strh	wzr, [x20, 1432]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2946:
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 656
	bl	get_new_active_ppa
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	ftl_do_gc
	.type	ftl_do_gc, %function
ftl_do_gc:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x2, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w3, [x2, 424]
	cbnz	w3, .L3047
	adrp	x3, .LANCHOR1
	add	x3, x3, :lo12:.LANCHOR1
	ldr	w20, [x3, 504]
	cmp	w20, 1
	bne	.L3047
	ldr	w4, [x2, 3560]
	cbnz	w4, .L3047
	ldrh	w4, [x2, 536]
	cmp	w4, 47
	bls	.L3047
	mov	w22, w1
	ldrh	w1, [x3, 3456]
	mov	w23, w0
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2950
.L2953:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0, 1450]
	cmp	w4, w2
	bne	.L2951
.L2952:
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w23, 1
	ldr	w0, [x1, 1368]
	add	w0, w0, 1
	add	w0, w0, w23, lsl 7
	str	w0, [x1, 1368]
	bne	.L2954
	adrp	x2, .LANCHOR0
	add	x1, x2, :lo12:.LANCHOR0
	mov	x25, x2
	ldr	w3, [x1, 2372]
	cbnz	w3, .L2955
	ldrb	w1, [x1, 204]
	cbz	w1, .L2954
.L2955:
	add	x24, x19, :lo12:.LANCHOR2
	ldr	w1, [x24, 776]
	cmp	w1, 39
	bhi	.L2954
	adrp	x21, .LANCHOR4
	add	x1, x21, :lo12:.LANCHOR4
	mov	w20, 65535
	ldrh	w1, [x1, 1984]
	add	w0, w1, w0
	str	w0, [x24, 1368]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x24, 800]
	cmp	w0, w20
	bne	.L2956
	ldrh	w1, [x24, 1448]
	cmp	w1, w0
	bne	.L3043
	ldr	w0, [x24, 1368]
	cmp	w0, 1024
	bhi	.L2958
	ldrh	w0, [x24, 552]
	cmp	w0, 63
	bhi	.L3043
.L2958:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w2, [x0, 1366]
	ldrh	w3, [x0, 552]
	strh	wzr, [x1, 1984]
	add	w2, w2, 64
	cmp	w3, w2
	bgt	.L3043
	str	wzr, [x0, 1368]
	ldr	w0, [x0, 776]
	cbnz	w0, .L2959
	mov	w0, 6
.L3096:
	strh	w0, [x1, 1984]
.L2960:
	mov	w0, 32
	bl	List_get_gc_head_node
	and	w5, w0, 65535
	mov	w8, 65535
	cmp	w5, w8
	beq	.L2964
	add	x24, x19, :lo12:.LANCHOR2
	ldrh	w0, [x24, 1372]
	cbz	w0, .L2962
	add	x2, x25, :lo12:.LANCHOR0
	ldr	x7, [x24, 520]
	ubfiz	x5, x5, 1, 16
	ldrh	w1, [x2, 2546]
	ldrh	w2, [x2, 2472]
	ldrh	w3, [x7, x5]
	mul	w1, w1, w2
	add	w1, w1, 1
	cmp	w3, w1
	bgt	.L2964
	add	w6, w0, 1
	str	wzr, [x24, 1376]
	and	w6, w6, 65535
	strh	w6, [x24, 1372]
	bl	List_get_gc_head_node
	and	w20, w0, 65535
	cmp	w20, w8
	beq	.L2964
	ubfiz	x25, x20, 1, 16
	ldrh	w4, [x7, x5]
	mov	w2, w20
	mov	w1, w6
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
	ldrh	w3, [x7, x25]
	bl	printk
	ldrh	w0, [x24, 1372]
	cmp	w0, 40
	bls	.L2963
	ldr	x0, [x24, 520]
	ldrh	w0, [x0, x25]
	cmp	w0, 32
	bls	.L2963
	strh	wzr, [x24, 1372]
.L2963:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, 6
	strh	w0, [x21, 1984]
.L2956:
	cmp	w23, 0
	mov	w1, 65535
	add	x0, x19, :lo12:.LANCHOR2
	ccmp	w20, w1, 0, eq
	bne	.L2978
	ldrh	w2, [x0, 552]
	cmp	w2, 24
	bhi	.L3055
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	cmp	w2, 16
	ldrh	w21, [x0, 2544]
	bls	.L2980
	lsr	w21, w21, 5
.L2979:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1, 1364]
	cmp	w0, w2
	bcs	.L2983
	ldrh	w0, [x1, 656]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L2984
	ldrh	w2, [x1, 1448]
	cmp	w2, w0
	bne	.L2984
	adrp	x0, .LANCHOR4+1984
	ldrh	w0, [x0, #:lo12:.LANCHOR4+1984]
	cbnz	w0, .L2985
	adrp	x2, .LANCHOR0+2616
	ldr	w3, [x1, 716]
	ldr	w2, [x2, #:lo12:.LANCHOR0+2616]
	add	w2, w2, w2, lsl 1
	cmp	w3, w2, lsr 2
	bcs	.L2986
.L2985:
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w1, [x2, 780]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2, 1364]
.L2987:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19, 1376]
.L2948:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L2950:
	ldrh	w1, [x2, 656]
	cmp	w1, w0
	beq	.L2953
	mov	w0, w20
	bl	FtlGcFreeTempBlock
	cbz	w0, .L2953
	mov	w0, w20
	b	.L2948
.L2951:
	ldrh	w1, [x0, 1448]
	cmp	w1, w2
	bne	.L2952
	ldrh	w3, [x0, 1452]
	cmp	w3, w1
	beq	.L2952
	ldrh	w2, [x0, 1454]
	cmp	w2, w1
	beq	.L2952
	mov	w1, -1
	strh	w4, [x0, 1448]
	strh	w3, [x0, 1450]
	strh	w2, [x0, 1452]
	strh	w1, [x0, 1454]
	b	.L2952
.L2959:
	cmp	w0, 5
	bhi	.L2960
	mov	w0, 18
	b	.L3096
.L2962:
	mov	w0, 1
	strh	w0, [x24, 1372]
.L2964:
	bl	GetSwlReplaceBlock
	and	w20, w0, 65535
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2956
	add	x21, x21, :lo12:.LANCHOR4
	strh	wzr, [x21, 1984]
.L2954:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w20, 65535
	ldrh	w0, [x0, 800]
	cmp	w0, w20
	bne	.L2956
.L3043:
	add	x24, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w20, [x24, 656]
	cmp	w20, w0
	bne	.L3050
	ldrh	w25, [x24, 1448]
	cmp	w25, w20
	bne	.L2956
	ldrh	w0, [x24, 552]
	mov	w1, 1024
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w1, cc
	ldr	w1, [x24, 1368]
	cmp	w1, w0
	bls	.L2956
	adrp	x0, .LANCHOR4+1984
	str	wzr, [x24, 1368]
	strh	wzr, [x0, #:lo12:.LANCHOR4+1984]
	bl	GetSwlReplaceBlock
	and	w20, w0, 65535
	cmp	w20, w25
	bne	.L3053
	ldrh	w1, [x24, 552]
	ldrh	w0, [x24, 1366]
	cmp	w1, w0
	bcs	.L2967
	mov	w0, 64
	bl	List_get_gc_head_node
	and	x0, x0, 65535
	cmp	w0, w20
	beq	.L2969
	ldr	w1, [x24, 3556]
	adrp	x5, .LANCHOR0
	cbnz	w1, .L2970
	add	x1, x5, :lo12:.LANCHOR0
	ldrh	w2, [x1, 2492]
	cmp	w2, 3
	beq	.L2970
	ldr	w2, [x24, 1360]
	cbnz	w2, .L2970
	ldr	w2, [x1, 2372]
	cbnz	w2, .L2970
	ldrb	w1, [x1, 204]
	cbz	w1, .L2971
.L2970:
	add	x1, x19, :lo12:.LANCHOR2
	add	x2, x5, :lo12:.LANCHOR0
	ldr	x1, [x1, 520]
	ldrh	w3, [x1, x0, lsl 1]
	ldrh	w0, [x2, 2546]
	ldrh	w1, [x2, 2472]
	ldrh	w2, [x2, 2492]
	cmp	w2, 3
	mul	w1, w1, w0
	lsr	w0, w0, 1
	csel	w0, w0, wzr, eq
	add	w0, w0, w1
	cmp	w3, w0
	bgt	.L2973
	mov	w0, 0
	bl	List_get_gc_head_node
	add	x5, x5, :lo12:.LANCHOR0
	add	x1, x19, :lo12:.LANCHOR2
	and	w21, w0, 65535
	ldr	w0, [x5, 2616]
	ldr	w2, [x1, 716]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2974
	mov	w0, 128
.L3097:
	strh	w0, [x1, 1366]
.L2975:
	mov	w0, 65535
	cmp	w21, w0
	beq	.L2969
.L2966:
	add	x0, x19, :lo12:.LANCHOR2
	ubfiz	x1, x21, 1, 32
	mov	w20, w21
	ldr	x3, [x0, 440]
	ldr	x2, [x0, 520]
	ldrh	w5, [x0, 1364]
	ldrh	w4, [x3, x1]
	ldrh	w3, [x2, x1]
	mov	w1, w21
	ldrh	w2, [x0, 552]
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	bl	printk
	b	.L2969
.L2974:
	mov	w0, 160
	b	.L3097
.L2973:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
.L3098:
	strh	w1, [x0, 1366]
.L2969:
	bl	FtlGcReFreshBadBlk
	b	.L2956
.L2971:
	ldr	x1, [x24, 520]
	ldrh	w0, [x1, x0, lsl 1]
	cmp	w0, 7
	bhi	.L2976
	mov	w0, 0
	bl	List_get_gc_head_node
	and	w21, w0, 65535
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0, 1366]
	b	.L2975
.L2976:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
	b	.L3098
.L2967:
	mov	w0, 80
	strh	w0, [x24, 1366]
	b	.L2969
.L3053:
	mov	w21, w20
	b	.L2966
.L3050:
	mov	w20, w0
	b	.L2956
.L2980:
	cmp	w2, 12
	bls	.L2981
	lsr	w21, w21, 4
	b	.L2979
.L2981:
	cmp	w2, 8
	bls	.L2979
	lsr	w21, w21, 2
	b	.L2979
.L3055:
	mov	w21, 1
	b	.L2979
.L2986:
	mov	w2, 18
	strh	w2, [x1, 1364]
	b	.L2987
.L2984:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1, 780]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 1364]
.L2983:
	adrp	x0, .LANCHOR0+2372
	ldr	w0, [x0, #:lo12:.LANCHOR0+2372]
	cbz	w0, .L3057
	cmp	w22, 2
	bhi	.L3057
	add	w21, w21, 1
	and	w21, w21, 65535
.L3057:
	mov	w20, 65535
	b	.L2989
.L2978:
	ldrh	w2, [x0, 656]
	cmp	w2, w1
	bne	.L2990
	ldrh	w1, [x0, 1448]
	cmp	w1, w2
	bne	.L2990
	cmp	w20, w1
	bne	.L2990
	ldrh	w1, [x0, 800]
	cmp	w1, w20
	bne	.L2990
	ldrh	w2, [x0, 552]
	adrp	x21, .LANCHOR4
	ldrh	w1, [x0, 1364]
	str	wzr, [x0, 1376]
	cmp	w2, w1
	bls	.L2992
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w1, [x1, 1984]
	cbnz	w1, .L2993
	adrp	x1, .LANCHOR0+2616
	ldr	w2, [x0, 716]
	ldr	w1, [x1, #:lo12:.LANCHOR0+2616]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L2994
.L2993:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1, 780]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 1364]
.L2995:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1, 520]
	ldrh	w0, [x1, x0]
	cmp	w0, 4
	bls	.L2992
.L3099:
	add	x21, x21, :lo12:.LANCHOR4
	b	.L3100
.L2994:
	mov	w1, 18
	strh	w1, [x0, 1364]
	b	.L2995
.L2992:
	add	x21, x21, :lo12:.LANCHOR4
	ldrh	w0, [x21, 1984]
	cbnz	w0, .L2990
	add	x6, x19, :lo12:.LANCHOR2
	ldrh	w5, [x6, 780]
	add	w0, w5, w5, lsl 1
	asr	w0, w0, 2
	strh	w0, [x6, 1364]
	mov	w0, 0
	bl	List_get_gc_head_node
	ldr	x1, [x6, 520]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x1, x0]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2546]
	ldrh	w1, [x1, 2472]
	mul	w0, w0, w1
	mov	w1, 2
	sdiv	w0, w0, w1
	cmp	w2, w0
	ble	.L2997
	ldrh	w0, [x6, 552]
	sub	w5, w5, #1
	cmp	w0, w5
	blt	.L2997
	bl	FtlReadRefresh
.L3100:
	ldrh	w0, [x21, 1984]
	b	.L2948
.L2997:
	cbnz	w2, .L2990
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x19, 552]
	add	w0, w0, 1
	b	.L2948
.L2990:
	adrp	x0, .LANCHOR0+2372
	ldr	w0, [x0, #:lo12:.LANCHOR0+2372]
	cmp	w0, 0
	cset	w21, ne
	add	w21, w21, 1
.L2989:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0, 800]
	cmp	w1, w2
	bne	.L2999
	cmp	w20, w1
	beq	.L3000
	strh	w20, [x0, 800]
.L3001:
	add	x5, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x5, 800]
	strb	wzr, [x5, 808]
	cmp	w0, w1
	beq	.L2999
	bl	IsBlkInGcList
	cbz	w0, .L3004
	mov	w0, -1
	strh	w0, [x5, 800]
.L3004:
	adrp	x0, .LANCHOR0+204
	ldrb	w0, [x0, #:lo12:.LANCHOR0+204]
	cbz	w0, .L3005
	add	x0, x19, :lo12:.LANCHOR2
	add	x3, x0, 800
	ldrh	w0, [x0, 800]
	bl	ftl_get_blk_mode
	strb	w0, [x3, 8]
.L3005:
	add	x12, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x13, x12, 800
	ldrh	w1, [x12, 800]
	cmp	w1, w0
	beq	.L2999
	mov	x0, x13
	bl	make_superblock
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w2, [x12, 800]
	ldr	x1, [x12, 520]
	strh	wzr, [x12, 802]
	strb	wzr, [x12, 806]
	strh	wzr, [x0, 1986]
	ldrh	w1, [x1, x2, lsl 1]
	strh	w1, [x0, 1988]
.L2999:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0, 800]
	ldrh	w2, [x0, 560]
	cmp	w2, w1
	beq	.L3006
	ldrh	w2, [x0, 608]
	cmp	w2, w1
	beq	.L3006
	ldrh	w0, [x0, 656]
	cmp	w0, w1
	bne	.L3040
.L3006:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, -1
	strh	w1, [x0, 800]
.L3040:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w24, [x22, 800]
	cmp	w24, w0
	bne	.L3008
	adrp	x25, .LANCHOR0
	add	x26, x25, :lo12:.LANCHOR0
	mov	w27, 2
	str	wzr, [x22, 1376]
.L3009:
	ldrh	w5, [x22, 1372]
	mov	w0, w5
	bl	List_get_gc_head_node
	and	w6, w0, 65535
	strh	w6, [x22, 800]
	cmp	w6, w24
	bne	.L3010
	strh	wzr, [x22, 1372]
	mov	w0, 8
	b	.L2948
.L3000:
	ldrh	w1, [x0, 1448]
	cmp	w1, w20
	beq	.L3001
	ldr	x2, [x0, 520]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2, x1]
	cbnz	w1, .L3002
	mov	w1, -1
	strh	w1, [x0, 1448]
.L3002:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0, 1448]
	strh	w1, [x0, 800]
	mov	w1, -1
	strh	w1, [x0, 1448]
	b	.L3001
.L3010:
	mov	w0, w6
	bl	IsBlkInGcList
	add	w5, w5, 1
	cbz	w0, .L3011
	strh	w5, [x22, 1372]
	b	.L3009
.L3011:
	ldrh	w4, [x26, 2472]
	ubfiz	x1, x6, 1, 16
	ldrh	w0, [x26, 2544]
	and	w5, w5, 65535
	ldr	x2, [x22, 520]
	strh	w5, [x22, 1372]
	mul	w0, w0, w4
	ldrh	w3, [x2, x1]
	sdiv	w4, w0, w27
	cmp	w3, w4
	bgt	.L3013
	cmp	w5, 48
	bls	.L3014
	cmp	w3, 8
	bls	.L3014
	ldrh	w3, [x22, 1420]
	cmp	w3, 35
	bhi	.L3014
.L3013:
	strh	wzr, [x22, 1372]
.L3014:
	ldrh	w1, [x2, x1]
	cmp	w0, w1
	bgt	.L3015
	cmp	w20, w24
	bne	.L3015
	ldrh	w0, [x22, 1372]
	cmp	w0, 3
	bhi	.L3015
	mov	w0, -1
	strh	w0, [x22, 800]
	adrp	x0, .LANCHOR4+1984
	strh	wzr, [x22, 1372]
	ldrh	w0, [x0, #:lo12:.LANCHOR4+1984]
	b	.L2948
.L3015:
	cbnz	w1, .L3016
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x22, 1372]
	add	w0, w0, 1
	strh	w0, [x22, 1372]
	b	.L3009
.L3016:
	add	x25, x25, :lo12:.LANCHOR0
	add	x3, x19, :lo12:.LANCHOR2
	add	x3, x3, 800
	ldrb	w0, [x25, 204]
	strb	wzr, [x3, 8]
	cbz	w0, .L3017
	mov	w0, w6
	bl	ftl_get_blk_mode
	strb	w0, [x3, 8]
.L3017:
	add	x13, x19, :lo12:.LANCHOR2
	add	x12, x13, 800
	mov	x0, x12
	bl	make_superblock
	ldrh	w2, [x13, 800]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x1, [x13, 520]
	strh	wzr, [x0, 1986]
	ldrh	w1, [x1, x2, lsl 1]
	strh	w1, [x0, 1988]
	strh	wzr, [x13, 802]
	strb	wzr, [x13, 806]
.L3008:
	cmp	w23, 1
	bne	.L3018
	bl	FtlReadRefresh
.L3018:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w0, 1
	adrp	x22, .LANCHOR0
	str	w0, [x1, 3560]
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w2, [x0, 204]
	ldrh	w24, [x0, 2544]
	cbz	w2, .L3019
	ldrb	w1, [x1, 808]
	cmp	w1, 1
	bne	.L3019
	ldrh	w24, [x0, 2546]
.L3019:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0, 802]
	add	w1, w0, w21
	cmp	w1, w24
	ble	.L3020
	sub	w21, w24, w0
	and	w21, w21, 65535
.L3020:
	adrp	x0, .LANCHOR4
	mov	w25, 0
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29, 136]
.L3021:
	cmp	w21, w25, uxth
	bls	.L3028
	add	x0, x22, :lo12:.LANCHOR0
	mov	w6, 0
	mov	w2, 0
	mov	w8, 65535
	mov	w7, 56
	ldrh	w9, [x0, 2472]
	add	x0, x19, :lo12:.LANCHOR2
	add	x3, x0, 816
	ldrh	w4, [x0, 802]
	add	w4, w4, w25
	b	.L3029
.L3023:
	ldrh	w1, [x3]
	cmp	w1, w8
	beq	.L3022
	ldr	x5, [x0, 1408]
	orr	w1, w4, w1, lsl 10
	umaddl	x5, w6, w7, x5
	add	w6, w6, 1
	and	w6, w6, 65535
	str	w1, [x5, 4]
.L3022:
	add	w2, w2, 1
	add	x3, x3, 2
	and	w2, w2, 65535
.L3029:
	cmp	w2, w9
	bne	.L3023
	add	x26, x19, :lo12:.LANCHOR2
	mov	w1, w6
	str	w6, [x29, 128]
	add	x27, x26, 656
	mov	x28, 0
	ldrb	w2, [x26, 808]
	ldr	x0, [x26, 1408]
	bl	FlashReadPages
	ldr	w6, [x29, 128]
	mov	w0, 56
	umull	x0, w6, w0
	str	x0, [x29, 128]
.L3024:
	ldr	x0, [x29, 128]
	cmp	x0, x28
	bne	.L3027
	add	w25, w25, 1
	b	.L3021
.L3027:
	ldr	x0, [x26, 1408]
	add	x1, x0, x28
	ldr	w0, [x0, x28]
	cmn	w0, #1
	beq	.L3025
	ldr	x5, [x1, 16]
	mov	w0, 61589
	ldrh	w1, [x5]
	cmp	w1, w0
	bne	.L3025
	ldr	w0, [x5, 8]
	mov	w2, 0
	add	x1, x29, 152
	str	x5, [x29, 120]
	bl	log2phys
	ldr	x0, [x26, 1408]
	ldr	w1, [x29, 152]
	add	x0, x0, x28
	ldr	x5, [x29, 120]
	and	w1, w1, 2147483647
	ldr	w2, [x0, 4]
	cmp	w1, w2
	bne	.L3025
	ldr	x1, [x29, 136]
	ldr	x2, [x29, 136]
	ldr	x6, [x26, 3592]
	ldr	w0, [x0, 24]
	ldrh	w1, [x1, 1986]
	str	x5, [x29, 104]
	add	w1, w1, 1
	strh	w1, [x2, 1986]
	ldr	w1, [x26, 1380]
	mov	w2, 56
	str	w2, [x29, 116]
	nop // between mem op and mult-accumulate
	umaddl	x1, w1, w2, x6
	str	x1, [x29, 120]
	str	w0, [x1, 24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29, 120]
	ldr	w2, [x29, 116]
	ldr	x5, [x29, 104]
	str	w0, [x1, 4]
	ldr	w0, [x26, 1380]
	ldr	x1, [x26, 3592]
	umaddl	x0, w0, w2, x1
	ldr	x1, [x26, 1408]
	add	x1, x1, x28
	ldr	x2, [x1, 8]
	str	x2, [x0, 8]
	ldr	x1, [x1, 16]
	str	x1, [x0, 16]
	ldr	w0, [x29, 152]
	mov	w1, 1
	str	w0, [x5, 12]
	ldrh	w0, [x27]
	strh	w0, [x5, 2]
	ldr	w0, [x26, 756]
	str	w0, [x5, 4]
	ldr	w0, [x26, 1380]
	add	w0, w0, 1
	str	w0, [x26, 1380]
	ldr	x0, [x26, 1408]
	add	x0, x0, x28
	bl	FtlGcBufAlloc
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 204]
	cbnz	w0, .L3026
	ldrb	w1, [x27, 7]
	ldr	w0, [x26, 1380]
	cmp	w1, w0
	beq	.L3026
	ldrh	w0, [x27, 4]
	cbnz	w0, .L3025
.L3026:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L3025
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19, 800]
	adrp	x0, .LANCHOR4
	add	x26, x0, :lo12:.LANCHOR4
	strh	wzr, [x19, 802]
	str	wzr, [x19, 3560]
	ldrh	w0, [x26, 1984]
	b	.L2948
.L3025:
	add	x28, x28, 56
	b	.L3024
.L3028:
	add	x25, x19, :lo12:.LANCHOR2
	ldrh	w0, [x25, 802]
	add	w21, w21, w0
	and	w21, w21, 65535
	strh	w21, [x25, 802]
	cmp	w24, w21
	bhi	.L3030
	ldr	w0, [x25, 1380]
	adrp	x21, .LANCHOR4
	cbz	w0, .L3031
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L3031
	str	wzr, [x25, 3560]
	b	.L3099
.L3031:
	add	x21, x21, :lo12:.LANCHOR4
	ldrh	w0, [x21, 1986]
	cbnz	w0, .L3032
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0, 800]
	ldr	x1, [x0, 520]
	ldrh	w1, [x1, x2, lsl 1]
	cbz	w1, .L3032
	add	x25, x22, :lo12:.LANCHOR0
	mov	x24, x0
	mov	w21, 0
.L3033:
	ldr	w0, [x25, 2616]
	cmp	w21, w0
	bcs	.L3038
	mov	w2, 0
	add	x1, x29, 156
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 156]
	cmn	w0, #1
	beq	.L3034
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x24, 800]
	cmp	w1, w0, uxth
	bne	.L3034
.L3038:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0, 2616]
	cmp	w21, w0
	bcc	.L3032
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0, 800]
	ldr	x1, [x0, 520]
	strh	wzr, [x1, x2, lsl 1]
	ldrh	w0, [x0, 800]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3032:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, -1
	strh	w1, [x0, 800]
.L3030:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1, 552]
	cmp	w0, 2
	bhi	.L3039
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w21, [x22, 2544]
	b	.L3040
.L3034:
	add	w21, w21, 1
	b	.L3033
.L3039:
	str	wzr, [x1, 3560]
	adrp	x1, .LANCHOR4+1984
	ldrh	w1, [x1, #:lo12:.LANCHOR4+1984]
	cmp	w1, 0
	csinc	w0, w1, w0, ne
	b	.L2948
.L3047:
	mov	w0, 0
	b	.L2948
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR2
	add	x0, x23, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w24, [x0, 424]
	cbnz	w24, .L3103
	adrp	x22, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x0, 2600]
	cbz	w1, .L3103
	ldrb	w0, [x0, 204]
	adrp	x2, .LANCHOR4+1992
	ldr	x19, [x2, #:lo12:.LANCHOR4+1992]
	cbz	w0, .L3128
	ldrb	w0, [x19, 8]
	cmp	w0, 1
	cset	w25, eq
.L3105:
	add	x20, x22, :lo12:.LANCHOR0
	ldrb	w3, [x19, 9]
	adrp	x26, .LC158
	mov	w2, w25
	mov	w21, 0
	mov	w27, 56
	ldr	x0, [x20, 2608]
	add	x26, x26, :lo12:.LC158
	bl	FlashProgPages
.L3106:
	ldr	w0, [x20, 2600]
	cmp	w21, w0
	bcc	.L3113
.L3125:
	add	x22, x22, :lo12:.LANCHOR0
	str	wzr, [x22, 2600]
.L3103:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L3128:
	mov	w25, 0
	b	.L3105
.L3113:
	umull	x28, w21, w27
	ldr	x0, [x20, 2608]
	add	x3, x0, x28
	ldr	w0, [x0, x28]
	cmn	w0, #1
	bne	.L3107
	add	x21, x22, :lo12:.LANCHOR0
	add	x26, x23, :lo12:.LANCHOR2
.L3108:
	ldr	w0, [x21, 2600]
	cmp	w24, w0
	bcc	.L3123
	add	x23, x23, :lo12:.LANCHOR2
	mov	w19, 16386
.L3126:
	ldrh	w0, [x23, 1456]
	cbz	w0, .L3125
	mov	w1, 1
	mov	w0, w1
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3126
	b	.L3125
.L3107:
	ldr	w0, [x3, 4]
	cbnz	w25, .L3109
.L3142:
	str	w0, [x29, 108]
	mov	w2, 1
	ldr	w0, [x3, 24]
	add	x1, x29, 108
	bl	log2phys
	ldr	x0, [x20, 2608]
	add	x0, x0, x28
	ldr	x0, [x0, 16]
	ldr	w0, [x0, 12]
	cmn	w0, #1
	beq	.L3111
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	and	w1, w0, 65535
	add	x0, x23, :lo12:.LANCHOR2
	ubfiz	x2, x1, 1, 16
	mov	w28, w1
	ldr	x0, [x0, 520]
	ldrh	w0, [x0, x2]
	cbnz	w0, .L3112
	mov	w2, 0
	mov	x0, x26
	bl	printk
.L3112:
	mov	w0, w28
	bl	decrement_vpc_count
.L3111:
	add	w21, w21, 1
	b	.L3106
.L3109:
	orr	w0, w0, -2147483648
	b	.L3142
.L3123:
	mov	w20, 56
	ldr	x0, [x21, 2608]
	mov	w1, -1
	mov	w27, 1
	umull	x20, w24, w20
	str	w1, [x0, x20]
.L3114:
	ldr	x0, [x21, 2608]
	add	x3, x0, x20
	ldr	w0, [x0, x20]
	cmn	w0, #1
	ldr	w0, [x3, 4]
	beq	.L3118
	cbnz	w25, .L3119
.L3143:
	str	w0, [x29, 108]
	mov	w2, 1
	ldr	w0, [x3, 24]
	add	x1, x29, 108
	bl	log2phys
	ldr	x0, [x21, 2608]
	add	x20, x0, x20
	ldr	x0, [x20, 16]
	ldr	w0, [x0, 12]
	cmn	w0, #1
	beq	.L3121
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x26, 520]
	and	w1, w0, 65535
	ubfiz	x0, x1, 1, 16
	mov	w20, w1
	ldrh	w0, [x2, x0]
	cbnz	w0, .L3122
	adrp	x0, .LC158
	mov	w2, 0
	add	x0, x0, :lo12:.LC158
	bl	printk
.L3122:
	mov	w0, w20
	bl	decrement_vpc_count
.L3121:
	add	w24, w24, 1
	b	.L3108
.L3118:
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19]
	cmp	w1, w0, uxth
	bne	.L3115
	ldr	x2, [x26, 520]
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x19, 4]
	ldrh	w0, [x2, x1]
	sub	w0, w0, w3
	strh	w0, [x2, x1]
	strb	wzr, [x19, 6]
	ldrh	w0, [x21, 2544]
	strh	w0, [x19, 2]
	strh	wzr, [x19, 4]
.L3115:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L3116
	mov	x0, x19
	bl	allocate_new_data_superblock
.L3116:
	ldr	w0, [x26, 944]
	add	w0, w0, 1
	str	w0, [x26, 944]
	ldr	x0, [x21, 2608]
	add	x0, x0, x20
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x19
	bl	get_new_active_ppa
	ldr	x1, [x21, 2608]
	mov	w2, w25
	str	w0, [x29, 108]
	add	x1, x1, x20
	str	w0, [x1, 4]
	mov	w1, 1
	ldrb	w3, [x19, 9]
	ldr	x0, [x21, 2608]
	add	x0, x0, x20
	bl	FlashProgPages
	ldr	x0, [x21, 2608]
	ldr	w0, [x0, x20]
	cmn	w0, #1
	bne	.L3117
	str	w27, [x26, 424]
.L3117:
	ldr	w0, [x26, 424]
	cbz	w0, .L3114
	b	.L3103
.L3119:
	orr	w0, w0, -2147483648
	b	.L3143
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	adrp	x0, .LANCHOR2+424
	ldr	w0, [x0, #:lo12:.LANCHOR2+424]
	cbnz	w0, .L3147
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR1+504
	add	x29, sp, 0
	str	x19, [sp, 16]
	ldr	w19, [x0, #:lo12:.LANCHOR1+504]
	cmp	w19, 1
	bne	.L3145
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, w19
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3145:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L3147:
	mov	w0, 0
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0, #:lo12:.LANCHOR1+504]
	cmp	w0, 1
	bne	.L3153
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlSysFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L3153:
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
	.global	rk_ftl_de_init
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	stp	x29, x30, [sp, -16]!
	mov	w1, 0
	adrp	x0, .LC159
	add	x0, x0, :lo12:.LC159
	add	x29, sp, 0
	bl	printk
	bl	ftl_deinit
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
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
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlCacheWriteBack
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_cache_write_back, .-rk_ftl_cache_write_back
	.align	2
	.global	ftl_discard
	.type	ftl_discard, %function
ftl_discard:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w20, w0
	stp	x21, x22, [sp, 32]
	adrp	x0, .LANCHOR0
	add	x21, x0, :lo12:.LANCHOR0
	str	x23, [sp, 48]
	mov	w19, w1
	ldr	w1, [x21, 2584]
	cmp	w1, w20
	bls	.L3173
	cmp	w1, w19
	bcc	.L3173
	mov	x23, x0
	add	w0, w20, w19
	cmp	w1, w0
	bcc	.L3173
	cmp	w19, 31
	bls	.L3175
	adrp	x22, .LANCHOR2
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w0, [x0, 424]
	cbnz	w0, .L3175
	bl	FtlCacheWriteBack
	ldrh	w0, [x21, 2550]
	udiv	w21, w20, w0
	msub	w20, w0, w21, w20
	ands	w20, w20, 65535
	beq	.L3166
	sub	w20, w0, w20
	add	w21, w21, 1
	cmp	w20, w19
	csel	w20, w20, w19, ls
	sub	w19, w19, w20, uxth
.L3166:
	add	x20, x23, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR4
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, -1
	str	w0, [x29, 76]
.L3167:
	ldrh	w0, [x20, 2550]
	cmp	w19, w0
	bcs	.L3169
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0, 2000]
	cmp	w1, 32
	bls	.L3175
	str	wzr, [x0, 2000]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3175:
	mov	w0, 0
	b	.L3164
.L3169:
	mov	w2, 0
	add	x1, x29, 72
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 72]
	cmn	w0, #1
	beq	.L3168
	add	x1, x22, :lo12:.LANCHOR2
	ldr	w0, [x23, 2000]
	mov	w2, 1
	add	w0, w0, 1
	str	w0, [x23, 2000]
	ldr	w0, [x1, 724]
	add	w0, w0, 1
	str	w0, [x1, 724]
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 72]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3168:
	ldrh	w0, [x20, 2550]
	add	w21, w21, 1
	sub	w19, w19, w0
	b	.L3167
.L3173:
	mov	w0, -1
.L3164:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_discard, .-ftl_discard
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_discard
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	ftl_read
	.type	ftl_read, %function
ftl_read:
	sub	sp, sp, #208
	adrp	x4, .LANCHOR1+504
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	stp	x21, x22, [sp, 48]
	ldr	w20, [x4, #:lo12:.LANCHOR1+504]
	stp	x23, x24, [sp, 64]
	stp	x25, x26, [sp, 80]
	cmp	w20, 1
	stp	x27, x28, [sp, 96]
	bne	.L3210
	mov	x23, x3
	mov	w24, w2
	mov	w19, w1
	cmp	w0, 16
	bne	.L3187
	mov	x2, x3
	mov	w1, w24
	add	w0, w19, 256
	bl	FtlVendorPartRead
	mov	w25, w0
.L3185:
	mov	w0, w25
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 208
	ret
.L3187:
	adrp	x21, .LANCHOR0
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w0, [x1, 2584]
	cmp	w19, w0
	bcs	.L3210
	cmp	w2, w0
	bhi	.L3210
	add	w2, w19, w2
	str	w2, [x29, 148]
	cmp	w0, w2
	bcc	.L3210
	ldrh	w0, [x1, 2550]
	sub	w26, w2, #1
	adrp	x7, .LANCHOR2
	udiv	w27, w19, w0
	udiv	w26, w26, w0
	sub	w20, w20, w27
	add	w0, w20, w26
	str	w0, [x29, 172]
	add	x0, x7, :lo12:.LANCHOR2
	ldr	w2, [x29, 172]
	ldr	w1, [x0, 748]
	add	w1, w1, w24
	str	w1, [x0, 748]
	ldr	w1, [x0, 720]
	add	w1, w1, w2
	str	w1, [x0, 720]
	mov	w1, w26
	mov	w0, w27
	bl	FtlCacheMetchLpa
	str	x7, [x29, 160]
	cbz	w0, .L3188
	bl	FtlCacheWriteBack
.L3188:
	mov	w22, w27
	adrp	x0, .LC147
	mov	w28, 0
	add	x0, x0, :lo12:.LC147
	mov	w25, 0
	str	x0, [x29, 104]
	str	wzr, [x29, 156]
	str	wzr, [x29, 168]
.L3189:
	ldr	w0, [x29, 172]
	cbnz	w0, .L3206
	ldr	x0, [x29, 160]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w0, [x0, 1456]
	cbz	w0, .L3185
	mov	w1, 1
	mov	w0, 0
	bl	ftl_do_gc
	b	.L3185
.L3206:
	mov	w2, 0
	add	x1, x29, 188
	mov	w0, w22
	bl	log2phys
	ldr	w4, [x29, 188]
	cmn	w4, #1
	bne	.L3190
	add	x5, x21, :lo12:.LANCHOR0
	mov	w20, 0
.L3191:
	ldrh	w0, [x5, 2550]
	cmp	w20, w0
	bcc	.L3193
.L3194:
	ldr	w0, [x29, 172]
	add	w22, w22, 1
	subs	w0, w0, #1
	str	w0, [x29, 172]
	beq	.L3198
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2472]
	cmp	w28, w0, lsl 3
	bne	.L3189
.L3198:
	cbz	w28, .L3189
	ldr	x0, [x29, 160]
	mov	w1, w28
	mov	w2, 0
	add	x8, x0, :lo12:.LANCHOR2
	mov	x20, x8
	ldr	x0, [x8, 3584]
	bl	FlashReadPages
	ldr	w0, [x29, 156]
	lsl	w0, w0, 9
	str	w0, [x29, 132]
	ldr	w0, [x29, 152]
	lsl	w0, w0, 9
	str	x0, [x29, 136]
	ldr	w0, [x29, 168]
	lsl	w0, w0, 9
	str	w0, [x29, 144]
	mov	w0, 56
	umull	x0, w28, w0
	mov	x28, 0
	str	x0, [x29, 120]
	add	x0, x21, :lo12:.LANCHOR0
	str	x0, [x29, 112]
.L3205:
	ldr	x0, [x20, 3584]
	add	x0, x0, x28
	ldr	w1, [x0, 24]
	cmp	w27, w1
	bne	.L3200
	ldr	x1, [x0, 8]
	ldr	x0, [x20, 3640]
	cmp	x1, x0
	bne	.L3201
	ldr	x0, [x29, 136]
	ldr	w2, [x29, 144]
	add	x1, x1, x0
	mov	x0, x23
.L3226:
	bl	ftl_memcpy
.L3201:
	ldr	x1, [x20, 3584]
	add	x0, x1, x28
	ldr	w2, [x1, x28]
	cmn	w2, #1
	bne	.L3202
	ldr	w1, [x20, 920]
	mov	w25, w2
	add	w1, w1, 1
	str	w1, [x20, 920]
.L3202:
	ldr	x1, [x0, 16]
	ldr	w2, [x0, 24]
	ldr	w1, [x1, 8]
	cmp	w2, w1
	beq	.L3203
	ldr	w1, [x20, 920]
	add	w1, w1, 1
	str	w1, [x20, 920]
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
.L3203:
	ldr	x0, [x20, 3584]
	add	x1, x0, x28
	ldr	w0, [x0, x28]
	cmp	w0, 256
	bne	.L3204
	ldr	w0, [x1, 4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3204:
	ldr	x0, [x29, 120]
	add	x28, x28, 56
	cmp	x0, x28
	bne	.L3205
	mov	w28, 0
	b	.L3189
.L3193:
	madd	w0, w22, w0, w20
	cmp	w19, w0
	bhi	.L3192
	ldr	w1, [x29, 148]
	cmp	w1, w0
	bls	.L3192
	sub	w0, w0, w19
	str	x5, [x29, 136]
	lsl	w0, w0, 9
	mov	w2, 512
	mov	w1, 0
	add	x0, x23, x0
	bl	ftl_memset
	ldr	x5, [x29, 136]
.L3192:
	add	w20, w20, 1
	b	.L3191
.L3190:
	mov	w0, 56
	cmp	w22, w27
	umull	x1, w28, w0
	ldr	x0, [x29, 160]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x2, [x0, 3584]
	add	x2, x2, x1
	str	w4, [x2, 4]
	ldr	x2, [x0, 3584]
	add	x2, x2, x1
	bne	.L3195
	ldr	x0, [x0, 3640]
	str	x0, [x2, 8]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2550]
	udiv	w4, w19, w0
	msub	w3, w4, w0, w19
	str	w3, [x29, 152]
	sub	w4, w0, w3
	cmp	w24, w4
	csel	w3, w24, w4, ls
	str	w3, [x29, 168]
	cmp	w3, w0
	bne	.L3196
	str	x23, [x2, 8]
.L3196:
	ldr	x0, [x29, 160]
	add	x2, x0, :lo12:.LANCHOR2
	ldr	x0, [x2, 3584]
	ldr	x2, [x2, 3664]
	add	x1, x0, x1
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2556]
	str	w22, [x1, 24]
	mul	w0, w0, w28
	add	w28, w28, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1, 16]
	b	.L3194
.L3195:
	cmp	w22, w26
	bne	.L3197
	ldr	x0, [x0, 3648]
	str	x0, [x2, 8]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w3, [x29, 148]
	ldrh	w4, [x0, 2550]
	mul	w0, w22, w4
	sub	w3, w3, w0
	str	w3, [x29, 156]
	cmp	w4, w3
	bne	.L3196
.L3225:
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	str	x0, [x2, 8]
	b	.L3196
.L3197:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2550]
	mul	w0, w0, w22
	b	.L3225
.L3200:
	cmp	w26, w1
	bne	.L3201
	ldr	x1, [x0, 8]
	ldr	x0, [x20, 3648]
	cmp	x1, x0
	bne	.L3201
	ldr	x0, [x29, 112]
	ldr	w2, [x29, 132]
	ldrh	w0, [x0, 2550]
	mul	w0, w0, w26
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	b	.L3226
.L3210:
	mov	w25, -1
	b	.L3185
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
	.global	FlashBootVendorRead
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	str	x21, [sp, 32]
	mov	w20, w1
	mov	x21, x2
	bl	rknand_device_lock
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0, #:lo12:.LANCHOR1+504]
	cmp	w0, 1
	bne	.L3231
	mov	w0, w19
	mov	x2, x21
	mov	w1, w20
	bl	ftl_vendor_read
	mov	w19, w0
.L3230:
	bl	rknand_device_unlock
	ldr	x21, [sp, 32]
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L3231:
	mov	w19, -1
	b	.L3230
	.size	FlashBootVendorRead, .-FlashBootVendorRead
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
	mov	x2, x19
	mov	w1, 1
	mov	w0, w20
	bl	ftl_sys_read
	mov	w19, w0
	bl	rknand_device_unlock
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	stp	x29, x30, [sp, -16]!
	and	w0, w0, 255
	add	x29, sp, 0
	bl	ftl_read
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	stp	x29, x30, [sp, -48]!
	mov	w0, -1
	adrp	x1, .LC75
	add	x1, x1, :lo12:.LC75
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR1
	add	x21, x21, :lo12:.LANCHOR1
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	str	w0, [x21, 504]
	adrp	x0, .LANCHOR4+2004
	str	wzr, [x19, 424]
	str	wzr, [x0, #:lo12:.LANCHOR4+2004]
	adrp	x0, .LC76
	add	x0, x0, :lo12:.LC76
	bl	printk
	add	x0, x20, 176
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x20, 2476]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3240
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC160
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC160
.L3255:
	bl	printk
.L3241:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L3240:
	bl	FtlSysBlkInit
	cbz	w0, .L3242
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LC161
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC161
	b	.L3255
.L3242:
	mov	w1, 1
	str	w1, [x21, 504]
	bl	ftl_do_gc
	ldrh	w0, [x19, 552]
	cmp	w0, 15
	bhi	.L3243
	add	w21, w0, 2
	mov	w20, 0
	mov	w22, 65535
.L3246:
	ldrh	w0, [x19, 800]
	cmp	w0, w22
	bne	.L3244
	ldrh	w0, [x19, 1448]
	cmp	w0, w22
	bne	.L3244
	and	w0, w20, 63
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3244:
	mov	w1, 1
	mov	w0, w1
	bl	ftl_do_gc
	mov	w1, 1
	mov	w0, 0
	bl	ftl_do_gc
	ldrh	w0, [x19, 552]
	cmp	w0, w21
	bhi	.L3241
	add	w20, w20, 1
	cmp	w20, 4096
	bne	.L3246
	b	.L3241
.L3243:
	ldrb	w0, [x20, 204]
	cbz	w0, .L3241
	mov	w19, 128
.L3248:
	mov	w1, 1
	mov	w0, w1
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3248
	b	.L3241
	.size	FtlInit, .-FtlInit
	.align	2
	.global	rk_ftl_init
	.type	rk_ftl_init, %function
rk_ftl_init:
	stp	x29, x30, [sp, -32]!
	mov	w0, 2048
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	bl	ftl_dma32_malloc
	add	x1, x19, 2016
	str	x0, [x19, 2008]
	add	x0, x19, 1584
	str	xzr, [x19, 1584]
	str	wzr, [x19, 1592]
	str	xzr, [x19, 2016]
	bl	rknand_get_reg_addr
	ldr	x0, [x19, 1584]
	cbz	x0, .L3261
	bl	rk_nandc_irq_init
	ldr	x0, [x19, 2008]
	mov	w3, 2048
	mov	w2, 0
	mov	w1, 0
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	x0, [x19, 1584]
	bl	FlashInit
	mov	w20, w0
	cbnz	w0, .L3258
	bl	FtlInit
.L3258:
	adrp	x0, .LC162
	mov	w1, w20
	add	x0, x0, :lo12:.LC162
	bl	printk
.L3256:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L3261:
	mov	w20, -1
	b	.L3256
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR0
	add	x0, x25, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x27, [sp, 80]
	ldrb	w0, [x0, 204]
	cbz	w0, .L3263
	adrp	x21, .LANCHOR4
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x20, .LANCHOR2
	add	x19, x20, :lo12:.LANCHOR2
	adrp	x24, .LC163
	add	x27, x19, 560
	ldrh	w22, [x0, 1842]
	add	x26, x19, 608
	ldr	x0, [x19, 520]
	mov	w1, w22
	ubfiz	x23, x22, 1, 16
	ldrh	w2, [x0, x23]
	add	x0, x24, :lo12:.LC163
	bl	printk
	ldrh	w0, [x19, 560]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x19, 608]
	bl	FtlGcRefreshOpenBlock
	mov	x0, x27
	bl	allocate_new_data_superblock
	mov	x0, x26
	mov	x26, x24
	mov	w27, 4097
	bl	allocate_new_data_superblock
.L3265:
	subs	w27, w27, #1
	beq	.L3269
	mov	w1, 1
	mov	w0, w1
	bl	ftl_do_gc
	ldr	x0, [x19, 520]
	ldrh	w0, [x0, x23]
	cbnz	w0, .L3265
.L3269:
	add	x24, x20, :lo12:.LANCHOR2
	mov	w1, w22
	ldr	x0, [x24, 520]
	ldrh	w2, [x0, x23]
	add	x0, x26, :lo12:.LC163
	bl	printk
	ldr	x0, [x24, 520]
	ldrh	w19, [x0, x23]
	cbnz	w19, .L3267
	add	x12, x29, 144
	add	x25, x25, :lo12:.LANCHOR0
	strh	w22, [x12, -48]!
	mov	x0, x12
	bl	make_superblock
	ldrh	w5, [x25, 2472]
	add	x12, x12, 16
	mov	w0, 0
	mov	w6, 65535
	mov	w7, 56
.L3270:
	cmp	w0, w5
	bne	.L3272
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w22
	ldr	x0, [x20, 520]
	ldrh	w2, [x0, x23]
	adrp	x0, .LC164
	add	x0, x0, :lo12:.LC164
	bl	printk
	ldr	x0, [x20, 432]
	mov	w2, w19
	mov	w1, 0
	bl	FlashEraseBlocks
	ldr	x0, [x20, 432]
	mov	w2, w19
	mov	w1, 1
	bl	FlashEraseBlocks
.L3267:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x21, 1842]
.L3263:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L3272:
	ldrh	w2, [x12]
	cmp	w2, w6
	beq	.L3271
	umull	x4, w19, w7
	ldr	x3, [x24, 432]
	lsl	w2, w2, 10
	add	w19, w19, 1
	add	x3, x3, x4
	and	w19, w19, 65535
	str	w2, [x3, 4]
	ldr	x1, [x24, 432]
	add	x1, x1, x4
	stp	xzr, xzr, [x1, 8]
.L3271:
	add	w0, w0, 1
	add	x12, x12, 2
	and	w0, w0, 65535
	b	.L3270
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_do_gc
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ftl_write
	.type	ftl_write, %function
ftl_write:
	stp	x29, x30, [sp, -272]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w22, w1
	adrp	x1, .LANCHOR2
	add	x4, x1, :lo12:.LANCHOR2
	stp	x23, x24, [sp, 48]
	mov	w24, w2
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	str	x1, [x29, 168]
	ldr	w2, [x4, 424]
	cbnz	w2, .L3322
	adrp	x1, .LANCHOR1
	add	x2, x1, :lo12:.LANCHOR1
	str	x1, [x29, 160]
	ldr	w2, [x2, 504]
	cmp	w2, 1
	bne	.L3322
	mov	x23, x3
	cmp	w0, 16
	bne	.L3282
	mov	x2, x3
	mov	w1, w24
	add	w0, w22, 256
	bl	FtlVendorPartWrite
.L3280:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 272
	ret
.L3282:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0, 2584]
	cmp	w22, w1
	bcs	.L3325
	cmp	w24, w1
	bhi	.L3325
	add	w2, w22, w24
	cmp	w1, w2
	bcc	.L3325
	adrp	x3, .LANCHOR4
	add	x25, x3, :lo12:.LANCHOR4
	mov	w1, 2048
	sub	w2, w2, #1
	str	x3, [x29, 152]
	str	w1, [x25, 2024]
	ldrh	w1, [x0, 2550]
	cmp	w24, w1, lsl 1
	udiv	w2, w2, w1
	udiv	w27, w22, w1
	str	w2, [x29, 196]
	sub	w28, w2, w27
	ldr	w2, [x4, 728]
	add	w26, w28, 1
	add	w2, w2, w26
	str	w2, [x4, 728]
	ldr	w2, [x4, 744]
	add	w2, w2, w24
	str	w2, [x4, 744]
	cset	w2, cs
	str	w2, [x29, 204]
	ldr	w2, [x0, 2600]
	cbz	w2, .L3284
	ldr	x0, [x0, 2608]
	sub	w2, w2, #1
	mov	w3, 56
	umaddl	x2, w2, w3, x0
	ldr	w0, [x2, 24]
	cmp	w27, w0
	bne	.L3285
	ldr	w0, [x4, 732]
	ldr	x3, [x2, 8]
	add	w0, w0, 1
	str	w0, [x4, 732]
	ldr	w0, [x25, 2028]
	add	w0, w0, 1
	str	w0, [x25, 2028]
	msub	w0, w27, w1, w22
	sub	w1, w1, w0
	cmp	w24, w1
	lsl	w0, w0, 9
	csel	w19, w24, w1, ls
	add	x0, x3, x0
	lsl	w21, w19, 9
	mov	x1, x23
	mov	w2, w21
	bl	ftl_memcpy
	cbnz	w28, .L3286
	ldr	w0, [x25, 2028]
	cmp	w0, 2
	bgt	.L3286
.L3322:
	mov	w0, 0
	b	.L3280
.L3286:
	sub	w24, w24, w19
	add	w22, w22, w19
	add	x23, x23, x21
	add	w27, w27, 1
	mov	w26, w28
.L3285:
	ldr	x0, [x29, 152]
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x0, 2028]
.L3284:
	ldr	w1, [x29, 196]
	mov	w0, w27
	bl	FtlCacheMetchLpa
	cbz	w0, .L3287
	bl	FtlCacheWriteBack
.L3287:
	ldr	x1, [x29, 168]
	mov	w19, w27
	ldr	x0, [x29, 152]
	add	x25, x1, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	add	x1, x25, 560
	mov	x21, x1
	str	x1, [x29, 128]
	str	x1, [x29, 176]
	str	x1, [x0, 1992]
.L3288:
	cbnz	w26, .L3316
	ldr	w0, [x29, 196]
	sub	w1, w0, w27
	mov	w0, 0
	bl	ftl_do_gc
	ldr	x0, [x29, 168]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w0, [x0, 552]
	cmp	w0, 5
	bls	.L3317
	cmp	w0, 31
	bhi	.L3322
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20, 72]
	cbnz	w0, .L3322
.L3317:
	ldr	x0, [x29, 168]
	mov	w20, 65535
	mov	w21, 128
	add	x19, x0, :lo12:.LANCHOR2
.L3320:
	ldrh	w0, [x19, 800]
	cmp	w0, w20
	bne	.L3319
	ldrh	w0, [x19, 1448]
	cmp	w0, w20
	bne	.L3319
	ldrh	w0, [x19, 1450]
	cmp	w0, w20
	bne	.L3319
	and	w0, w26, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3319:
	mov	w1, 1
	strh	w21, [x19, 1366]
	mov	w0, w1
	strh	w21, [x19, 1364]
	bl	ftl_do_gc
	mov	w1, 1
	mov	w0, 0
	bl	ftl_do_gc
	ldr	w0, [x19, 424]
	cbnz	w0, .L3322
	ldrh	w0, [x19, 552]
	cmp	w0, 2
	bhi	.L3322
	add	w26, w26, 1
	cmp	w26, 256
	bne	.L3320
	b	.L3322
.L3316:
	ldrh	w0, [x21, 4]
	cbnz	w0, .L3289
	ldr	x0, [x29, 128]
	cmp	x21, x0
	bne	.L3290
	add	x0, x25, 608
	ldrh	w1, [x0, 4]
	cbnz	w1, .L3291
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 160]
	add	x0, x0, :lo12:.LANCHOR1
	str	wzr, [x0, 3460]
.L3291:
	ldr	x0, [x29, 176]
	add	x21, x25, 608
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 160]
	add	x0, x0, :lo12:.LANCHOR1
	ldr	w0, [x0, 3460]
	cmp	w0, 0
	ldr	x0, [x29, 176]
	csel	x21, x21, x0, ne
.L3292:
	ldrh	w0, [x21, 4]
	cbnz	w0, .L3293
	mov	x0, x21
	bl	allocate_new_data_superblock
.L3293:
	ldr	x0, [x29, 152]
	add	x0, x0, :lo12:.LANCHOR4
	str	x21, [x0, 1992]
.L3289:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x25, 3580]
	str	wzr, [x29, 200]
	ldr	w1, [x1, 2600]
	sub	w0, w0, w1
	ldrh	w1, [x21, 4]
	cmp	w0, w26
	csel	w0, w0, w26, ls
	cmp	w1, w0
	csel	w0, w1, w0, ls
	str	w0, [x29, 124]
	ldr	w0, [x29, 204]
	and	w0, w0, 1
	str	w0, [x29, 120]
	adrp	x0, .LC165
	add	x0, x0, :lo12:.LC165
	str	x0, [x29, 112]
.L3294:
	ldr	w1, [x29, 124]
	ldr	w0, [x29, 200]
	cmp	w0, w1
	bne	.L3312
.L3295:
	ldr	w0, [x29, 200]
	sub	w26, w26, w0
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0, 2600]
	ldr	w0, [x25, 3580]
	cmp	w1, w0
	bcs	.L3313
	ldr	w0, [x29, 204]
	cbnz	w0, .L3313
	ldrh	w0, [x21, 4]
	cbz	w0, .L3313
.L3315:
	str	wzr, [x29, 204]
	b	.L3288
.L3290:
	ldr	x0, [x29, 160]
	add	x0, x0, :lo12:.LANCHOR1
	str	wzr, [x0, 3460]
	ldr	x0, [x29, 128]
	ldrh	w0, [x0, 4]
	cbnz	w0, .L3329
	mov	x0, x21
	bl	allocate_new_data_superblock
	b	.L3292
.L3329:
	ldr	x21, [x29, 176]
	b	.L3293
.L3312:
	ldrh	w0, [x21, 4]
	cbz	w0, .L3295
	ldr	w0, [x29, 196]
	cmp	w0, w19
	ldr	w0, [x29, 120]
	cset	w4, eq
	tst	w4, w0
	beq	.L3296
	ldr	w0, [x29, 200]
	cbz	w0, .L3296
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2550]
	add	w0, w22, w24
	msub	w0, w19, w1, w0
	cmp	w1, w0
	bne	.L3295
.L3296:
	add	x1, x29, 212
	mov	w2, 0
	str	w4, [x29, 108]
	mov	w0, w19
	bl	log2phys
	mov	x0, x21
	bl	get_new_active_ppa
	add	x3, x20, :lo12:.LANCHOR0
	mov	w5, 56
	str	x3, [x29, 184]
	str	w5, [x29, 192]
	ldr	w6, [x3, 2600]
	ldr	x1, [x3, 2608]
	umull	x2, w6, w5
	add	x1, x1, x2
	str	w0, [x1, 4]
	ldr	x0, [x3, 2608]
	add	x0, x0, x2
	ldrh	w2, [x3, 2556]
	str	w19, [x0, 24]
	mul	w1, w6, w2
	and	x1, x1, 4294967292
	str	x1, [x29, 144]
	ldr	x1, [x25, 3672]
	ldr	x4, [x29, 144]
	str	x1, [x29, 136]
	add	x28, x1, x4
	ldrh	w1, [x3, 2554]
	str	x28, [x0, 16]
	mul	w1, w1, w6
	ldr	x6, [x25, 3632]
	and	x1, x1, 4294967292
	add	x1, x6, x1
	str	x1, [x0, 8]
	mov	w1, 0
	mov	x0, x28
	bl	ftl_memset
	cmp	w27, w19
	ldr	w4, [x29, 108]
	cset	w0, eq
	ldr	w5, [x29, 192]
	orr	w4, w4, w0
	ldr	x3, [x29, 184]
	cbz	w4, .L3297
	bne	.L3298
	ldrh	w2, [x3, 2550]
	udiv	w0, w22, w2
	msub	w0, w0, w2, w22
	str	w0, [x29, 192]
	sub	w2, w2, w0
	cmp	w2, w24
	csel	w0, w2, w24, ls
	str	w0, [x29, 184]
.L3299:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x29, 184]
	ldrh	w1, [x0, 2550]
	cmp	w1, w2
	bne	.L3300
	cmp	w27, w19
	beq	.L3330
	mul	w1, w19, w2
	sub	w1, w1, w22
	lsl	w1, w1, 9
	add	x1, x23, x1
.L3301:
	ldr	w0, [x29, 204]
	add	x2, x20, :lo12:.LANCHOR0
	cbz	w0, .L3302
	ldr	w0, [x2, 2600]
	mov	w3, 56
	ldr	x2, [x2, 2608]
	umaddl	x0, w0, w3, x2
.L3348:
	str	x1, [x0, 8]
.L3303:
	ldp	x1, x2, [x29, 136]
	mov	w0, -3947
	strh	w0, [x1, x2]
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x25, 756]
	stp	w0, w19, [x28, 4]
	add	w19, w19, 1
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x25, 756]
	ldr	w0, [x29, 212]
	str	w0, [x28, 12]
	ldrh	w0, [x21]
	strh	w0, [x28, 2]
	ldr	w0, [x1, 2600]
	add	w0, w0, 1
	str	w0, [x1, 2600]
	ldr	w0, [x29, 200]
	add	w0, w0, 1
	str	w0, [x29, 200]
	b	.L3294
.L3298:
	ldrh	w0, [x3, 2550]
	add	w2, w22, w24
	str	wzr, [x29, 192]
	msub	w2, w19, w0, w2
	and	w0, w2, 65535
	str	w0, [x29, 184]
	b	.L3299
.L3330:
	mov	x1, x23
	b	.L3301
.L3302:
	ldr	w0, [x2, 2600]
	mov	w4, 56
	ldr	x3, [x2, 2608]
	ldrh	w2, [x2, 2554]
	nop // between mem op and mult-accumulate
	umaddl	x0, w0, w4, x3
.L3349:
	ldr	x0, [x0, 8]
	b	.L3351
.L3300:
	ldr	w1, [x29, 212]
	mov	w2, 56
	cmn	w1, #1
	beq	.L3304
	str	w1, [x29, 220]
	ldr	w1, [x0, 2600]
	ldr	x0, [x0, 2608]
	str	w19, [x29, 240]
	nop // between mem op and mult-accumulate
	umaddl	x0, w1, w2, x0
	mov	w2, 0
	ldp	x1, x0, [x0, 8]
	stp	x1, x0, [x29, 224]
	mov	w1, 1
	add	x0, x29, 216
	bl	FlashReadPages
	ldr	w0, [x29, 216]
	cmn	w0, #1
	bne	.L3305
	ldr	w0, [x25, 920]
	add	w0, w0, 1
	str	w0, [x25, 920]
.L3307:
	ldr	w0, [x29, 184]
	cmp	w27, w19
	lsl	w2, w0, 9
	bne	.L3308
	add	x0, x20, :lo12:.LANCHOR0
	mov	w3, 56
	ldr	w1, [x0, 2600]
	ldr	x0, [x0, 2608]
	umaddl	x1, w1, w3, x0
	ldr	w0, [x29, 192]
	lsl	w0, w0, 9
	ldr	x3, [x1, 8]
	mov	x1, x23
	add	x0, x3, x0
.L3351:
	bl	ftl_memcpy
	b	.L3303
.L3305:
	ldr	w0, [x28, 8]
	cmp	w19, w0
	beq	.L3307
	ldr	w0, [x25, 920]
	mov	w2, w19
	add	w0, w0, 1
	str	w0, [x25, 920]
	ldr	x0, [x29, 112]
	ldr	w1, [x28, 8]
	bl	printk
	b	.L3307
.L3304:
	ldr	x1, [x0, 2608]
	ldr	w3, [x0, 2600]
	umaddl	x3, w3, w2, x1
	ldrh	w2, [x0, 2554]
	mov	w1, 0
	ldr	x0, [x3, 8]
	bl	ftl_memset
	b	.L3307
.L3308:
	add	x3, x20, :lo12:.LANCHOR0
	mov	w4, 56
	ldrh	w1, [x3, 2550]
	ldr	w0, [x3, 2600]
	ldr	x3, [x3, 2608]
	mul	w1, w1, w19
	umaddl	x0, w0, w4, x3
	sub	w1, w1, w22
	lsl	w1, w1, 9
.L3350:
	add	x1, x23, x1
	b	.L3349
.L3297:
	ldr	w0, [x29, 204]
	ldrh	w1, [x3, 2550]
	cbz	w0, .L3309
	mul	w1, w1, w19
	ldr	w0, [x3, 2600]
	ldr	x2, [x3, 2608]
	sub	w1, w1, w22
	lsl	w1, w1, 9
	add	x1, x23, x1
	umaddl	x0, w0, w5, x2
	b	.L3348
.L3309:
	ldr	x2, [x3, 2608]
	mul	w1, w1, w19
	ldr	w0, [x3, 2600]
	sub	w1, w1, w22
	lsl	w1, w1, 9
	umaddl	x0, w0, w5, x2
	ldrh	w2, [x3, 2554]
	b	.L3350
.L3313:
	bl	FtlCacheWriteBack
	add	x0, x20, :lo12:.LANCHOR0
	cmp	w26, 1
	str	wzr, [x0, 2600]
	bhi	.L3288
	b	.L3315
.L3325:
	mov	w0, -1
	b	.L3280
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
	.global	FlashBootVendorWrite
	.type	FlashBootVendorWrite, %function
FlashBootVendorWrite:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	str	x21, [sp, 32]
	mov	w20, w1
	mov	x21, x2
	bl	rknand_device_lock
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0, #:lo12:.LANCHOR1+504]
	cmp	w0, 1
	bne	.L3356
	mov	w0, w19
	mov	x2, x21
	mov	w1, w20
	bl	ftl_vendor_write
	mov	w19, w0
.L3355:
	bl	rknand_device_unlock
	ldr	x21, [sp, 32]
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L3356:
	mov	w19, -1
	b	.L3355
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
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
	.global	StorageSysDataStore
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x1
	mov	w19, w0
	bl	rknand_device_lock
	mov	x2, x20
	mov	w1, 1
	mov	w0, w19
	bl	ftl_sys_write
	mov	w19, w0
	bl	rknand_device_unlock
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.global	FtlDumpSysBlock
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	sub	sp, sp, #96
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x19, .LANCHOR4
	stp	x21, x22, [sp, 48]
	add	x19, x19, :lo12:.LANCHOR4
	stp	x23, x24, [sp, 64]
	adrp	x21, .LANCHOR2
	and	w24, w0, 65535
	add	x0, x21, :lo12:.LANCHOR2
	str	x25, [sp, 80]
	add	x19, x19, 1712
	adrp	x22, .LANCHOR0
	adrp	x23, .LC166
	ldr	x1, [x0, 3608]
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x0, 3656]
	add	x23, x23, :lo12:.LC166
	lsl	w25, w24, 10
	mov	w20, 0
	stp	x1, x0, [x19, 8]
.L3363:
	ldrh	w0, [x22, 2546]
	cmp	w20, w0
	blt	.L3365
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x29, x30, [sp, 16]
	ldr	x25, [sp, 80]
	add	sp, sp, 96
	ret
.L3365:
	orr	w0, w20, w25
	mov	w2, 1
	str	w0, [x19, 4]
	mov	w1, w2
	mov	x0, x19
	bl	FlashReadPages
	ldp	x1, x0, [x19, 8]
	ldr	w1, [x1]
	str	w1, [sp]
	mov	w1, w24
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x23
	ldp	w2, w3, [x19]
	bl	printk
	ldr	x0, [x19, 16]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L3364
	add	x0, x21, :lo12:.LANCHOR2
	mov	w3, 768
	mov	w2, 4
	ldr	x1, [x0, 3608]
	adrp	x0, .LC167
	add	x0, x0, :lo12:.LC167
	bl	rknand_print_hex
.L3364:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L3363
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	2
	.global	dump_map_info
	.type	dump_map_info, %function
dump_map_info:
	sub	sp, sp, #112
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 64]
	stp	x21, x22, [sp, 48]
	adrp	x23, .LANCHOR2
	stp	x25, x26, [sp, 80]
	mov	x21, x19
	stp	x27, x28, [sp, 96]
	add	x24, x20, 2504
	add	x26, x23, :lo12:.LANCHOR2
	ldrh	w22, [x20, 2480]
.L3371:
	ldrh	w0, [x20, 2482]
	cmp	w0, w22
	bhi	.L3377
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	add	x19, x19, 1712
	mov	w22, 0
	adrp	x26, .LANCHOR2
.L3378:
	add	x20, x26, :lo12:.LANCHOR2
	ldrh	w0, [x20, 3856]
	cmp	w22, w0
	bge	.L3381
	adrp	x23, .LC112
	sbfiz	x24, x22, 1, 32
	mov	w20, 0
	add	x23, x23, :lo12:.LC112
	b	.L3382
.L3373:
	ldrb	w0, [x24, x5]
	mov	w1, w22
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L3372
	umull	x2, w19, w7
	ldr	x0, [x26, 3584]
	lsl	w4, w4, 10
	add	x0, x0, x2
	str	w4, [x0, 4]
	ldr	x1, [x26, 3584]
	ldr	x0, [x26, 1392]
	add	x1, x1, x2
	ldr	x2, [x26, 1400]
	str	x0, [x1, 8]
	mul	w0, w19, w8
	add	w19, w19, 1
	and	w19, w19, 65535
	sdiv	w0, w0, w6
	add	x0, x2, w0, sxtw 2
	str	x0, [x1, 16]
.L3372:
	add	x5, x5, 1
.L3379:
	cmp	w9, w5, uxth
	bhi	.L3373
	cbnz	w19, .L3374
.L3376:
	add	w22, w22, 1
	and	w22, w22, 65535
	b	.L3371
.L3374:
	ldr	x0, [x26, 3584]
	mov	w1, w19
	mov	w2, 1
	adrp	x27, .LC168
	add	x28, x23, :lo12:.LANCHOR2
	add	x27, x27, :lo12:.LC168
	mov	x25, 0
	bl	FlashReadPages
	mov	w0, 56
	umull	x19, w19, w0
.L3375:
	ldr	x0, [x28, 3584]
	add	x0, x0, x25
	add	x25, x25, 56
	ldr	x1, [x0, 16]
	ldr	w2, [x0, 4]
	ldr	x0, [x0, 8]
	ldr	w3, [x0, 4]
	str	w3, [sp]
	ldr	w7, [x0]
	mov	x0, x27
	ldp	w3, w4, [x1]
	ldp	w5, w6, [x1, 8]
	ubfx	x1, x2, 10, 16
	bl	printk
	cmp	x25, x19
	beq	.L3376
	b	.L3375
.L3377:
	ldrh	w9, [x20, 2472]
	mov	x5, 0
	ldrh	w8, [x20, 2556]
	mov	w19, 0
	mov	w7, 56
	mov	w6, 4
	b	.L3379
.L3380:
	add	x25, x26, :lo12:.LANCHOR2
	mov	w2, 1
	mov	w1, w2
	ldr	x0, [x25, 3712]
	ldrh	w0, [x0, x24]
	orr	w0, w20, w0, lsl 10
	str	w0, [x19, 4]
	mov	x0, x19
	bl	FlashReadPages
	ldp	x2, x0, [x19, 8]
	add	w20, w20, 1
	ldr	x1, [x25, 3712]
	sxth	w20, w20
	ldr	w3, [x2, 4]
	ldrh	w1, [x1, x24]
	str	w3, [sp, 8]
	ldr	w2, [x2]
	str	w2, [sp]
	ldp	w4, w5, [x0]
	ldp	w6, w7, [x0, 8]
	mov	x0, x23
	ldp	w2, w3, [x19]
	bl	printk
.L3382:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2546]
	cmp	w20, w0
	blt	.L3380
	add	w22, w22, 1
	sxth	w22, w22
	b	.L3378
.L3381:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x1, [x20, 3712]
	mov	w2, 2
	adrp	x0, .LC169
	add	x0, x0, :lo12:.LC169
	ldr	w3, [x21, 2572]
	bl	rknand_print_hex
	ldrh	w3, [x21, 2580]
	mov	w2, 4
	ldr	x1, [x20, 3752]
	adrp	x0, .LC170
	add	x0, x0, :lo12:.LC170
	bl	rknand_print_hex
	ldrh	w3, [x21, 2580]
	mov	w2, 4
	ldr	x1, [x20, 3760]
	adrp	x0, .LC171
	add	x0, x0, :lo12:.LC171
	bl	rknand_print_hex
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 112
	ret
	.size	dump_map_info, .-dump_map_info
	.align	2
	.global	flash_boot_enter_slc_mode
	.type	flash_boot_enter_slc_mode, %function
flash_boot_enter_slc_mode:
	adrp	x1, .LANCHOR0+2392
	ldr	w2, [x1, #:lo12:.LANCHOR0+2392]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3388
	stp	x29, x30, [sp, -16]!
	and	w0, w0, 255
	add	x29, sp, 0
	bl	flash_enter_slc_mode
	ldp	x29, x30, [sp], 16
	ret
.L3388:
	ret
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	2
	.global	flash_boot_exit_slc_mode
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	adrp	x1, .LANCHOR0+2392
	ldr	w2, [x1, #:lo12:.LANCHOR0+2392]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3394
	stp	x29, x30, [sp, -16]!
	and	w0, w0, 255
	add	x29, sp, 0
	bl	flash_exit_slc_mode
	ldp	x29, x30, [sp], 16
	ret
.L3394:
	ret
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x24, .LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	add	x0, x24, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	mov	x22, x1
	stp	x27, x28, [sp, 80]
	mov	x23, x2
	ldr	x1, [x0, 88]
	ldr	w20, [x0, 76]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	ldrb	w21, [x1, 9]
	bl	ftl_malloc
	cbz	x0, .L3421
	add	w19, w19, 511
	mov	x26, x0
	lsr	w19, w19, 9
	cmp	w19, 8
	bls	.L3422
	mov	w0, -1
	cmp	w19, 500
	bhi	.L3397
.L3399:
	ldr	w2, [x22]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	mov	w0, -1
	cmp	w2, w1
	bne	.L3397
	mul	w21, w21, w20
	add	x3, x22, 253952
	add	x3, x3, 1532
	mov	w2, 63871
	and	w21, w21, 65535
	mov	w4, 4097
	add	w0, w21, w0
	add	w0, w0, w19
	udiv	w0, w0, w21
	str	w0, [x29, 168]
	mov	w0, 0
.L3403:
	ldr	w1, [x3]
	cbnz	w1, .L3400
	ldr	w1, [x22, w0, uxtw 2]
	add	w0, w0, 1
	str	w1, [x3, 512]
	cmp	w0, w4
	sub	w2, w2, #1
	csel	w0, w0, wzr, cc
	sub	x3, x3, #4
	cmp	w2, 4096
	bne	.L3403
.L3402:
	add	x20, x24, :lo12:.LANCHOR0
	mov	w3, 5
	mov	w2, 4
	mov	x1, x23
	adrp	x0, .LC173
	add	x0, x0, :lo12:.LC173
	bl	rknand_print_hex
	adrp	x25, .LANCHOR4
	ldrb	w2, [x20, 73]
	adrp	x0, .LC174
	ldr	w1, [x22, 512]
	add	x0, x0, :lo12:.LC174
	bl	printk
	add	x0, x25, :lo12:.LANCHOR4
	ldrh	w3, [x20, 202]
	mov	w1, w19
	mov	w2, w19
	ldr	w4, [x0, 1644]
	adrp	x0, .LC175
	add	x0, x0, :lo12:.LC175
	bl	printk
	str	x25, [x29, 160]
	ldrb	w0, [x20, 73]
	ldr	w1, [x22, 512]
	cmp	w1, w0
	bls	.L3404
	str	w0, [x22, 512]
.L3404:
	lsl	w0, w19, 7
	add	x28, x24, :lo12:.LANCHOR0
	str	w0, [x29, 140]
	mov	x20, 0
	add	x0, x28, 208
	str	wzr, [x29, 172]
	str	x0, [x29, 152]
.L3420:
	ldr	w1, [x23, x20, lsl 2]
	ldrb	w0, [x28, 73]
	str	w20, [x29, 136]
	cmp	w1, w0
	bcs	.L3405
	ldr	x0, [x29, 160]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w0, [x0, 1644]
	cmp	w1, w0
	bcc	.L3405
	ldr	w0, [x29, 168]
	cmp	w20, 0
	ccmp	w0, 1, 0, ne
	bls	.L3406
	sub	w0, w20, #1
	ldr	w0, [x23, x0, lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	beq	.L3405
.L3406:
	mov	x2, 512
	mov	w1, 0
	mov	x0, x26
	bl	memset
	ldr	w24, [x23, x20, lsl 2]
	mov	w2, w19
	ldr	x0, [x28, 88]
	ldr	w25, [x28, 76]
	mul	w24, w21, w24
	ldrb	w27, [x0, 9]
	adrp	x0, .LC176
	mov	w1, w24
	add	x0, x0, :lo12:.LC176
	bl	printk
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	mul	w25, w25, w27
	udiv	w1, w24, w27
	mov	w2, 0
	and	w25, w25, 65535
	mov	w0, 0
	bl	FlashEraseBlock
	cmp	w25, w19
	bcs	.L3425
	add	w1, w24, w25
	mov	w2, 0
	mov	w0, 0
	bl	FlashEraseBlock
	mov	w1, 2
.L3407:
	mov	w0, 0
	str	w1, [x29, 144]
	bl	flash_boot_exit_slc_mode
	ldr	x0, [x28, 88]
	mov	w5, 0
	ldr	w1, [x29, 144]
	mov	w10, 1073741823
	udiv	w8, w24, w25
	ldrh	w4, [x0, 10]
	ldrb	w0, [x0, 12]
	msub	w8, w8, w25, w24
	mov	x25, x22
	lsl	w4, w4, 2
	mul	w4, w4, w1
	sdiv	w4, w4, w0
	sub	w0, w24, w8
	str	w0, [x29, 144]
.L3408:
	cmp	w4, w5
	bhi	.L3412
	mov	w1, w24
	mov	w3, 0
	mov	w2, w19
	adrp	x0, .LC177
	add	x0, x0, :lo12:.LC177
	bl	printk
	ldr	x0, [x28, 88]
	mov	w2, w19
	ldr	w25, [x28, 76]
	ldr	w24, [x23, x20, lsl 2]
	ldrb	w5, [x0, 9]
	adrp	x0, .LC178
	str	w5, [x29, 144]
	add	x0, x0, :lo12:.LC178
	mul	w24, w21, w24
	mul	w25, w25, w5
	mov	w1, w24
	bl	printk
	and	w25, w25, 65535
	ldr	w5, [x29, 144]
	ldr	x1, [x29, 160]
	mov	x7, x26
	mov	w4, 0
	mov	w9, 4
	udiv	w27, w24, w25
	add	x10, x1, :lo12:.LANCHOR4
	msub	w27, w27, w25, w24
	sub	w8, w24, w27
	mul	w0, w27, w5
	ubfx	x0, x0, 2, 2
.L3413:
	cmp	w4, w19
	bcc	.L3415
	mov	w1, w24
	mov	w3, 0
	mov	w2, w19
	adrp	x0, .LC179
	add	x0, x0, :lo12:.LC179
	bl	printk
	mov	x1, x26
	mov	x0, x22
	mov	x24, 0
.L3416:
	ldr	w2, [x29, 140]
	cmp	w24, w2
	bcc	.L3418
	ldr	w0, [x29, 172]
	add	w0, w0, 1
	str	w0, [x29, 172]
	b	.L3405
.L3422:
	mov	w19, 8
	b	.L3399
.L3400:
	adrp	x0, .LC172
	add	x0, x0, :lo12:.LC172
	bl	printk
	b	.L3402
.L3425:
	mov	w1, 1
	b	.L3407
.L3412:
	add	w1, w8, w5
	lsr	w1, w1, 2
	cbz	w1, .L3409
	ldr	x0, [x29, 152]
	add	w2, w1, 1
	ldrb	w3, [x28, 204]
	ldrh	w0, [x0, w2, sxtw 1]
	cbz	w3, .L3410
	ldr	w3, [x28, 2392]
	mov	w6, 12336
	movk	w6, 0x5638, lsl 16
	cmp	w3, w6
	csel	w0, w0, w2, ne
.L3410:
	add	w0, w0, w10
	lsl	w0, w0, 2
	str	w0, [x29, 176]
.L3409:
	mov	w0, 61424
	str	w0, [x29, 180]
	ldr	x0, [x29, 152]
	ldrh	w7, [x0, w1, sxtw 1]
	ldrb	w0, [x28, 204]
	cbz	w0, .L3411
	ldr	w0, [x28, 2392]
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w0, w2
	csel	w7, w7, w1, ne
.L3411:
	ldr	w0, [x29, 144]
	ldrb	w11, [x28, 2464]
	stp	w10, w5, [x29, 104]
	stp	w8, w4, [x29, 112]
	madd	w1, w27, w7, w0
	ldr	x0, [x29, 160]
	stp	w7, w1, [x29, 120]
	add	x0, x0, :lo12:.LANCHOR4
	str	w11, [x29, 128]
	ldrb	w0, [x0, 1650]
	bl	FlashBchSel
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	x0, [x28, 88]
	mov	x2, x25
	ldr	w1, [x29, 124]
	add	x3, x29, 176
	add	x25, x25, 2048
	ldrb	w0, [x0, 9]
	udiv	w1, w1, w0
	mov	w0, 0
	bl	FlashProgPage
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	ldr	w11, [x29, 128]
	mov	w0, w11
	bl	FlashBchSel
	ldr	w0, [x29, 144]
	ldr	w7, [x29, 120]
	add	w2, w7, 1
	udiv	w1, w0, w27
	mov	w0, 0
	bl	FlashPageProgMsbFFData
	ldp	w10, w5, [x29, 104]
	ldp	w8, w4, [x29, 112]
	add	w5, w5, 4
	and	w5, w5, 65535
	b	.L3408
.L3415:
	add	w2, w27, w4
	ldr	x1, [x29, 152]
	ldrb	w3, [x28, 204]
	lsr	w2, w2, 2
	sub	w25, w9, w0
	and	w25, w25, 65535
	ldrh	w1, [x1, w2, sxtw 1]
	cbz	w3, .L3414
	ldr	w3, [x28, 2392]
	mov	w6, 12336
	movk	w6, 0x5638, lsl 16
	cmp	w3, w6
	csel	w1, w1, w2, ne
.L3414:
	add	w0, w0, w8
	ldrb	w11, [x28, 2464]
	stp	w9, w4, [x29, 100]
	stp	w8, w5, [x29, 108]
	madd	w1, w5, w1, w0
	ldr	x0, [x28, 88]
	str	x10, [x29, 128]
	str	w11, [x29, 124]
	str	x7, [x29, 144]
	ldrb	w12, [x0, 9]
	ldrb	w0, [x10, 1650]
	stp	w1, w12, [x29, 116]
	bl	FlashBchSel
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldp	w1, w12, [x29, 116]
	mov	x3, 0
	ldr	x7, [x29, 144]
	mov	w0, 0
	udiv	w1, w1, w12
	mov	x2, x7
	bl	FlashReadPage
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	ldr	w11, [x29, 124]
	mov	w0, w11
	bl	FlashBchSel
	ldp	w9, w4, [x29, 100]
	ubfiz	x0, x25, 9, 16
	ldr	x7, [x29, 144]
	add	w4, w25, w4
	ldr	x10, [x29, 128]
	add	x7, x7, x0
	and	w4, w4, 65535
	ldp	w8, w5, [x29, 108]
	mov	w0, 0
	b	.L3413
.L3418:
	mov	x25, x1
	mov	x27, x0
	add	x2, x24, 1
	add	x1, x1, 4
	add	x0, x0, 4
	ldr	w4, [x25]
	ldr	w3, [x27]
	cmp	w4, w3
	beq	.L3426
	mov	x2, 512
	mov	w1, 0
	mov	x0, x26
	bl	memset
	ldr	w4, [x25]
	mov	w3, w24
	ldr	w5, [x27]
	and	x24, x24, -256
	ldr	w2, [x23, x20, lsl 2]
	lsl	x24, x24, 2
	ldr	w1, [x29, 136]
	adrp	x0, .LC180
	add	x0, x0, :lo12:.LC180
	bl	printk
	mov	w3, 256
	mov	w2, 4
	add	x1, x22, x24
	adrp	x0, .LC181
	add	x0, x0, :lo12:.LC181
	bl	rknand_print_hex
	add	x1, x26, x24
	mov	w3, 256
	mov	w2, 4
	adrp	x0, .LC182
	add	x0, x0, :lo12:.LC182
	bl	rknand_print_hex
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	w1, [x23, x20, lsl 2]
	mov	w2, 0
	mov	w0, 0
	mul	w1, w21, w1
	bl	FlashEraseBlock
	ldr	w0, [x29, 168]
	cmp	w0, 1
	bls	.L3417
	ldr	w1, [x23, x20, lsl 2]
	mov	w2, 0
	mov	w0, 0
	madd	w1, w1, w21, w21
	bl	FlashEraseBlock
.L3417:
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	ldr	w1, [x23, x20, lsl 2]
	adrp	x0, .LC183
	add	x0, x0, :lo12:.LC183
	bl	printk
.L3405:
	add	x20, x20, 1
	cmp	x20, 5
	bne	.L3420
	mov	x0, x26
	bl	ftl_free
	ldr	w0, [x29, 172]
	cmp	w0, 0
	csetm	w0, eq
.L3397:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 240
	ret
.L3426:
	mov	x24, x2
	b	.L3416
.L3421:
	mov	w0, -1
	b	.L3397
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
	adrp	x20, .LANCHOR4
	stp	x23, x24, [sp, 48]
	mov	w21, w1
	mov	x24, x2
	bne	.L3447
	ldr	w1, [x2]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L3447
	add	x22, x20, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x22, 2032]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	mov	w2, 59392
	mov	w1, 0
	movk	w2, 0x3, lsl 16
	str	x0, [x22, 2040]
	bl	ftl_memset
	str	w19, [x22, 2048]
.L3447:
	add	x23, x20, :lo12:.LANCHOR4
	ldr	w2, [x24]
	mov	w4, w21
	mov	w3, w19
	adrp	x0, .LC184
	add	x0, x0, :lo12:.LC184
	ldr	x1, [x23, 2040]
	bl	printk
	ldrb	w0, [x23, 2032]
	cbz	w0, .L3446
	sub	w0, w19, #64
	ldr	x22, [x23, 2040]
	cmp	w0, 499
	bhi	.L3449
	mov	w2, 564
	sub	w2, w2, w19
	cmp	w21, w2
	ubfiz	x0, x0, 9, 25
	csel	w2, w21, w2, ls
	mov	x1, x24
	lsl	w2, w2, 9
	add	x0, x22, x0
	bl	ftl_memcpy
.L3450:
	add	x0, x20, :lo12:.LANCHOR4
	ldr	w1, [x0, 2048]
	cmp	w19, w1
	beq	.L3459
	strb	wzr, [x0, 2032]
	cbz	x22, .L3460
	mov	x0, x22
	bl	ftl_free
.L3460:
	add	x0, x20, :lo12:.LANCHOR4
	str	xzr, [x0, 2040]
	b	.L3459
.L3449:
	cmp	w19, 563
	bls	.L3450
	ldr	w0, [x23, 2048]
	mov	w1, 500
	sub	w0, w0, #64
	cmp	w0, 500
	csel	w0, w0, w1, ls
	adrp	x1, .LANCHOR0+88
	ldr	x1, [x1, #:lo12:.LANCHOR0+88]
	ldrb	w1, [x1, 9]
	cmp	w1, 4
	beq	.L3451
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
.L3452:
	add	x2, x22, 253952
	mov	w1, 63872
	add	x2, x2, 1536
.L3458:
	ldr	w3, [x2]
	cbz	w3, .L3456
	add	w0, w1, 128
	lsl	w0, w0, 2
.L3457:
	mov	x1, x22
	add	x22, x20, :lo12:.LANCHOR4
	add	x2, x29, 72
	bl	write_idblock
	ldr	x0, [x22, 2040]
	strb	wzr, [x22, 2032]
	bl	ftl_free
	str	xzr, [x22, 2040]
.L3459:
	add	x20, x20, :lo12:.LANCHOR4
	add	w19, w19, w21
	str	w19, [x20, 2048]
.L3446:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 112
	ret
.L3451:
	add	x2, x29, 72
	mov	x1, 0
.L3455:
	cmp	w0, 256
	bls	.L3453
	lsl	w3, w1, 1
	str	w3, [x2, x1, lsl 2]
.L3454:
	add	x1, x1, 1
	cmp	x1, 5
	bne	.L3455
	b	.L3452
.L3453:
	str	w1, [x2, x1, lsl 2]
	b	.L3454
.L3456:
	sub	w1, w1, #1
	sub	x2, x2, #4
	cmp	w1, 4096
	bne	.L3458
	lsl	w0, w0, 9
	b	.L3457
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
	bhi	.L3471
	cbnz	w20, .L3471
	mov	x2, x3
	mov	w1, w21
	mov	w0, w19
	bl	write_loader_lba
.L3471:
	mov	x3, x22
	mov	w2, w21
	mov	w1, w19
	mov	w0, w20
	bl	ftl_write
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	sub	sp, sp, #560
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	stp	x29, x30, [sp]
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w1
	mov	x20, x2
	beq	.L3475
	bhi	.L3476
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3477
	bhi	.L3478
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3479
	add	w0, w0, 237
	cmp	w1, w0
	beq	.L3480
.L3510:
	mov	x19, -22
	b	.L3473
.L3478:
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3481
	add	w0, w0, 1
	cmp	w1, w0
	beq	.L3482
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3510
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
	bl	printk
	mov	x2, 520
	mov	x1, x20
	add	x0, x29, 40
	bl	rk_copy_from_user
	cbnz	x0, .L3493
	ldr	w1, [x29, 40]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3490
	ldr	w2, [x29, 44]
	cmp	w2, 512
	bhi	.L3490
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	uxtw	x2, w2
	add	x1, x1, 2080
	add	x0, x29, 48
.L3531:
	bl	memcpy
	b	.L3525
.L3476:
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3484
	bhi	.L3485
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3475
	add	w0, w0, 10
	cmp	w1, w0
	bne	.L3510
.L3475:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3500
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
.L3527:
	bl	printk
	mov	x2, 520
	mov	x1, x20
	add	x0, x29, 40
	bl	rk_copy_from_user
	cbnz	x0, .L3493
	ldr	w1, [x29, 40]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3530
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR4
	bne	.L3503
	add	x0, x0, :lo12:.LANCHOR4
	mov	x2, 16
	add	x1, x29, 40
	ldr	x0, [x0, 2056]
	ldr	w0, [x0, 20]
	strb	w0, [x29, 48]
	str	w0, [x29, 44]
	mov	x0, x20
	bl	rk_copy_to_user
	cbnz	x0, .L3530
.L3494:
	mov	x19, 0
.L3473:
	mov	x0, x19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp]
	add	sp, sp, 560
	ret
.L3485:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3484
	bcc	.L3486
	add	w0, w0, 1
	cmp	w1, w0
	bne	.L3510
.L3486:
	adrp	x0, .LC196
	add	x0, x0, :lo12:.LC196
	bl	printk
	mov	x2, 520
	mov	x1, x20
	add	x0, x29, 40
	bl	rk_copy_from_user
	cbnz	x0, .L3493
	ldr	w1, [x29, 40]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3490
	ldr	w2, [x29, 44]
	cmp	w2, 504
	bhi	.L3490
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	add	x1, x29, 40
	cmp	w19, w0
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	bne	.L3509
	ldr	x0, [x19, 2600]
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19, 2600]
	b	.L3529
.L3480:
	adrp	x0, .LC185
	add	x0, x0, :lo12:.LC185
	bl	printk
	mov	x2, 520
	mov	x1, x20
	add	x0, x29, 40
	bl	rk_copy_from_user
	cbz	x0, .L3487
.L3493:
	adrp	x0, .LC186
	add	x0, x0, :lo12:.LC186
	bl	printk
.L3530:
	mov	x19, -14
	b	.L3473
.L3487:
	ldr	w1, [x29, 40]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3488
.L3490:
	mov	x19, -1
.L3489:
	mov	x1, x19
	adrp	x0, .LC197
	add	x0, x0, :lo12:.LC197
	bl	printk
	b	.L3473
.L3488:
	ldr	w0, [x29, 44]
	cmp	w0, 512
	bhi	.L3490
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	mov	x2, 512
	add	x0, x29, 40
	ldr	x1, [x19, 2056]
	bl	memcpy
	ldr	w1, [x19, 2064]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3491
	mov	x2, 128
	mov	w1, 0
	add	x0, x29, 104
	stp	wzr, wzr, [x29, 48]
	bl	memset
.L3491:
	add	x0, x29, 40
	mov	x2, 256
	mov	w1, 0
	add	x0, x0, x2
	str	wzr, [x29, 56]
	bl	memset
.L3525:
	mov	x2, 520
	add	x1, x29, 40
	mov	x0, x20
	bl	rk_copy_to_user
	cbnz	x0, .L3530
.L3528:
	mov	x19, 0
	b	.L3489
.L3477:
	adrp	x0, .LC187
	add	x0, x0, :lo12:.LC187
	bl	printk
	mov	x2, 520
	mov	x1, x20
	add	x0, x29, 40
	bl	rk_copy_from_user
	cbnz	x0, .L3493
	ldr	w1, [x29, 40]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3490
	ldr	w0, [x29, 44]
	cmp	w0, 512
	bhi	.L3490
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	ldr	w2, [x0, 2064]
	cmp	w2, w1
	bne	.L3511
	ldr	w1, [x29, 52]
	mov	x19, -3
	sub	w2, w1, #1
	cmp	w2, 127
	bhi	.L3473
	ldr	x19, [x0, 2056]
	add	x0, x19, 64
	str	w1, [x19, 12]
	add	x1, x29, 104
	ldr	w2, [x29, 52]
	bl	memcpy
	mov	x1, x19
	mov	w0, 1
.L3529:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3489
.L3482:
	adrp	x0, .LC188
	add	x0, x0, :lo12:.LC188
	bl	printk
	mov	x2, 520
	mov	x1, x20
	add	x0, x29, 40
	bl	rk_copy_from_user
	cbnz	x0, .L3493
	ldr	w1, [x29, 40]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3490
	ldr	w0, [x29, 44]
	cmp	w0, 512
	bhi	.L3490
	adrp	x19, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR4
	ldr	w0, [x0, 2068]
	cbz	w0, .L3494
	add	x0, x19, :lo12:.LANCHOR4
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	x1, [x0, 2072]
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3495
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0, 2072]
	str	w2, [x1, 4]
	ldr	x0, [x0, 2072]
	stp	wzr, wzr, [x0, 8]
.L3495:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	x1, [x20, 2072]
	str	wzr, [x1, 16]
	bl	StorageSysDataStore
	ldr	x0, [x20, 2056]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3496
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20, 2056]
	str	w1, [x0, 4]
	ldr	x0, [x20, 2056]
	str	wzr, [x0, 8]
.L3496:
	add	x19, x19, :lo12:.LANCHOR4
	mov	x2, 128
	mov	w1, 0
	ldr	x20, [x19, 2056]
	add	x0, x20, 64
	str	wzr, [x20, 12]
	bl	memset
	mov	x1, x20
	mov	w0, 1
	bl	StorageSysDataStore
	str	wzr, [x19, 2064]
	str	wzr, [x19, 2068]
	b	.L3528
.L3481:
	adrp	x0, .LC189
	add	x0, x0, :lo12:.LC189
	bl	printk
	mov	x2, 520
	mov	x1, x20
	add	x0, x29, 40
	bl	rk_copy_from_user
	cbnz	x0, .L3493
	ldr	w1, [x29, 40]
	mov	w0, 20037
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3490
	ldr	w0, [x29, 44]
	cmp	w0, 512
	bhi	.L3490
	adrp	x19, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR4
	ldr	w1, [x0, 2068]
	cmp	w1, 1
	beq	.L3494
	ldr	x1, [x0, 2072]
	mov	w0, 22867
	movk	w0, 0x4453, lsl 16
	ldr	w2, [x1]
	cmp	w2, w0
	beq	.L3497
	str	w0, [x1]
	add	x0, x19, :lo12:.LANCHOR4
	mov	w2, 504
	ldr	x1, [x0, 2072]
	str	w2, [x1, 4]
	ldr	x0, [x0, 2072]
	stp	wzr, wzr, [x0, 8]
.L3497:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w0, 1
	ldr	x1, [x20, 2072]
	str	w0, [x1, 16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20, 2056]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3498
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20, 2056]
	str	w1, [x0, 4]
	ldr	x0, [x20, 2056]
	str	wzr, [x0, 8]
.L3498:
	add	x19, x19, :lo12:.LANCHOR4
	mov	x2, 128
	mov	w1, 0
	ldr	x20, [x19, 2056]
	add	x0, x20, 64
	str	wzr, [x20, 12]
	bl	memset
	mov	x1, x20
	mov	w0, 1
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19, 2068]
	b	.L3528
.L3500:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3502
	adrp	x0, .LC192
	add	x0, x0, :lo12:.LC192
	b	.L3527
.L3502:
	adrp	x0, .LC193
	add	x0, x0, :lo12:.LC193
	b	.L3527
.L3503:
	add	x20, x0, :lo12:.LANCHOR4
	ldr	w1, [x20, 2592]
	cmp	w1, 10
	bhi	.L3530
	ldr	x1, [x20, 2056]
	ldr	w2, [x29, 44]
	ldr	w3, [x1, 24]
	cmp	w3, w2
	beq	.L3504
	cbz	w3, .L3504
	mov	w1, w2
	adrp	x0, .LC194
	add	x0, x0, :lo12:.LC194
	bl	printk
	ldr	w0, [x20, 2592]
	add	w0, w0, 1
	str	w0, [x20, 2592]
	b	.L3530
.L3504:
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x0, 2592]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3505
	stp	wzr, wzr, [x1, 20]
.L3506:
	mov	w0, 1
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3528
	mov	x19, -2
	b	.L3489
.L3505:
	mov	w0, 1
	stp	w0, w2, [x1, 20]
	b	.L3506
.L3484:
	adrp	x0, .LC195
	add	x0, x0, :lo12:.LC195
	bl	printk
	mov	x2, 520
	mov	x1, x20
	add	x0, x29, 40
	bl	rk_copy_from_user
	cbnz	x0, .L3493
	ldr	w1, [x29, 40]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3490
	ldr	w2, [x29, 44]
	cmp	w2, 504
	bhi	.L3490
	mov	w0, 30224
	adrp	x1, .LANCHOR4
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x29, 48
	bne	.L3507
	ldr	x1, [x1, 2600]
.L3532:
	add	x1, x1, 8
	b	.L3531
.L3507:
	ldr	x1, [x1, 2608]
	b	.L3532
.L3509:
	ldr	x0, [x19, 2608]
	bl	memcpy
	mov	w0, 3
	ldr	x1, [x19, 2608]
	b	.L3529
.L3479:
	bl	rknand_dev_flush
	b	.L3528
.L3511:
	mov	x19, -2
	b	.L3473
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	stp	x29, x30, [sp, -32]!
	mov	w0, -1
	mov	w2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR4
	add	x19, x20, :lo12:.LANCHOR4
	ldr	x1, [x19, 2008]
	str	w0, [x19, 2048]
	add	x0, x1, 512
	str	x0, [x19, 2056]
	add	x0, x1, 1024
	str	x1, [x19, 2072]
	str	x0, [x19, 2600]
	add	x1, x1, 1536
	strb	wzr, [x19, 2032]
	add	x0, x19, 2080
	str	xzr, [x19, 2040]
	str	xzr, [x19, 2616]
	str	x1, [x19, 2608]
	bl	ftl_memcpy
	ldr	x6, [x19, 2072]
	str	wzr, [x19, 2064]
	str	wzr, [x19, 2592]
	ldr	w7, [x6, 508]
	ldr	w0, [x6, 16]
	str	w0, [x19, 2068]
	cbz	w7, .L3534
	mov	w1, 508
	mov	x0, x6
	bl	js_hash
	cmp	w7, w0
	beq	.L3534
	str	wzr, [x6, 16]
	adrp	x0, .LC198
	str	wzr, [x19, 2068]
	add	x0, x0, :lo12:.LC198
	bl	printk
.L3534:
	add	x0, x20, :lo12:.LANCHOR4
	ldr	w1, [x0, 2068]
	cbz	w1, .L3535
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0, 2064]
.L3535:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 2
	ldr	x1, [x20, 2600]
	bl	StorageSysDataLoad
	ldr	x1, [x20, 2608]
	mov	w0, 3
	bl	StorageSysDataLoad
	bl	rknand_sys_storage_init
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
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
	mov	w0, 65536
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR4
	add	x20, x19, :lo12:.LANCHOR4
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	bl	ftl_malloc
	str	x0, [x20, 2624]
	cbz	x0, .L3550
	adrp	x24, .LC199
	mov	w26, 22084
	mov	x22, x19
	add	x24, x24, :lo12:.LC199
	mov	w25, 0
	mov	w23, 0
	mov	w21, 0
	movk	w26, 0x524b, lsl 16
.L3548:
	ldr	x2, [x20, 2624]
	mov	w1, 128
	lsl	w0, w21, 7
	bl	FlashBootVendorRead
	cbnz	w0, .L3546
	ldr	x0, [x20, 2624]
	add	x1, x0, 61440
	ldr	w3, [x0, 4]
	ldr	w2, [x1, 4092]
	ldr	w1, [x0]
	mov	x0, x24
	bl	printk
	ldr	x19, [x20, 2624]
	ldr	w0, [x19]
	cmp	w0, w26
	bne	.L3547
	add	x0, x19, 61440
	ldr	w1, [x19, 4]
	ldr	w0, [x0, 4092]
	cmp	w0, w1
	bne	.L3547
	cmp	w0, w23
	bls	.L3547
	mov	w25, w21
	mov	w23, w0
.L3547:
	add	w21, w21, 1
	cmp	w21, 2
	bne	.L3548
	cbz	w23, .L3549
	mov	x2, x19
	mov	w1, 128
	lsl	w0, w25, 7
	bl	FlashBootVendorRead
	cbnz	w0, .L3546
.L3544:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L3549:
	mov	w1, 0
	mov	x2, 65536
	mov	x0, x19
	bl	memset
	mov	w1, 22084
	mov	w0, 1
	movk	w1, 0x524b, lsl 16
	stp	w1, w0, [x19]
	add	x1, x19, 61440
	str	w0, [x1, 4092]
	mov	w0, -1032
	strh	w0, [x19, 14]
	mov	w0, 0
	b	.L3544
.L3546:
	add	x22, x22, :lo12:.LANCHOR4
	ldr	x0, [x22, 2624]
	bl	kfree
	str	xzr, [x22, 2624]
	mov	w0, -1
	b	.L3544
.L3550:
	mov	w0, -12
	b	.L3544
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	2
	.global	rk_ftl_vendor_read
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	adrp	x3, .LANCHOR4+2624
	ldr	x4, [x3, #:lo12:.LANCHOR4+2624]
	cbz	x4, .L3558
	ldrh	w6, [x4, 10]
	add	x5, x4, 16
	mov	w3, 0
.L3555:
	cmp	w3, w6
	bcc	.L3557
.L3558:
	mov	w0, -1
	ret
.L3557:
	ldrh	w7, [x5], 8
	cmp	w7, w0
	bne	.L3556
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
.L3556:
	add	w3, w3, 1
	b	.L3555
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	2
	.global	rk_ftl_vendor_write
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	stp	x29, x30, [sp, -112]!
	adrp	x3, .LANCHOR4+2624
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	ldr	x19, [x3, #:lo12:.LANCHOR4+2624]
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	cbz	x19, .L3578
	add	w4, w2, 63
	ldrh	w3, [x19, 10]
	ldrh	w24, [x19, 8]
	mov	x28, x1
	mov	w26, w2
	and	w22, w4, -64
	add	x1, x19, 16
	mov	w20, 0
.L3565:
	cmp	w20, w3
	bcc	.L3573
	ldrh	w1, [x19, 14]
	cmp	w22, w1
	bhi	.L3578
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
	b	.L3580
.L3573:
	ldrh	w6, [x1], 8
	cmp	w6, w0
	bne	.L3566
	uxtw	x23, w20
	add	x5, x19, 1024
	add	x21, x19, x23, lsl 3
	ldrh	w25, [x21, 20]
	add	w25, w25, 63
	and	w25, w25, -64
	cmp	w26, w25
	bls	.L3567
	ldrh	w0, [x19, 14]
	cmp	w22, w0
	bhi	.L3578
	add	x23, x23, 2
	ldrh	w21, [x21, 18]
	add	x23, x19, x23, lsl 3
	sub	w3, w3, #1
.L3568:
	cmp	w20, w3
	bcc	.L3569
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
.L3580:
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
.L3563:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L3569:
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
	b	.L3568
.L3567:
	ldrh	w0, [x21, 18]
	uxtw	x2, w26
	mov	x1, x28
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21, 20]
	b	.L3580
.L3566:
	add	w20, w20, 1
	b	.L3565
.L3578:
	mov	w0, -1
	b	.L3563
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	2
	.global	rk_ftl_vendor_storage_ioctl
	.type	rk_ftl_vendor_storage_ioctl, %function
rk_ftl_vendor_storage_ioctl:
	stp	x29, x30, [sp, -48]!
	mov	w0, 4096
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x2
	str	x21, [sp, 32]
	mov	w21, w1
	bl	ftl_malloc
	cbz	x0, .L3589
	mov	w1, 30209
	mov	x19, x0
	movk	w1, 0x4004, lsl 16
	cmp	w21, w1
	beq	.L3584
	add	w1, w1, 1
	cmp	w21, w1
	beq	.L3585
.L3595:
	mov	x20, -14
	b	.L3583
.L3584:
	mov	x2, 8
	mov	x1, x20
	bl	rk_copy_from_user
	cbnz	x0, .L3595
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3587
.L3588:
	mov	x20, -1
.L3583:
	mov	x0, x19
	bl	kfree
.L3581:
	mov	x0, x20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L3587:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3588
	strh	w0, [x19, 6]
	and	x0, x0, 65535
	add	x2, x0, 8
	mov	x1, x19
	mov	x0, x20
	bl	rk_copy_to_user
	cbnz	x0, .L3595
	mov	x20, 0
	b	.L3583
.L3585:
	mov	x2, 8
	mov	x1, x20
	bl	rk_copy_from_user
	cbnz	x0, .L3595
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3588
	ldrh	w2, [x19, 6]
	cmp	w2, 4087
	bhi	.L3588
	add	w2, w2, 8
	mov	x1, x20
	sxtw	x2, w2
	mov	x0, x19
	bl	rk_copy_from_user
	cbnz	x0, .L3595
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3583
.L3589:
	mov	x20, -1
	b	.L3581
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
	.global	g_maxRetryCount
	.global	g_maxRegNum
	.global	g_retryMode
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
	.global	IDByte
	.global	read_retry_cur_offset
	.section	.rodata
	.align	3
	.set	.LANCHOR3,. + 0
	.type	__func__.27063, %object
	.size	__func__.27063, 11
__func__.27063:
	.string	"FtlMemInit"
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
	.type	__func__.27842, %object
	.size	__func__.27842, 17
__func__.27842:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.27861, %object
	.size	__func__.27861, 16
__func__.27861:
	.string	"FtlScanAllBlock"
	.type	__func__.28129, %object
	.size	__func__.28129, 17
__func__.28129:
	.string	"ftl_scan_all_ppa"
	.zero	7
	.type	__func__.27810, %object
	.size	__func__.27810, 12
__func__.27810:
	.string	"FtlCheckVpc"
	.zero	4
	.type	__func__.28109, %object
	.size	__func__.28109, 21
__func__.28109:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.27136, %object
	.size	__func__.27136, 8
__func__.27136:
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
	.type	gFtlInitStatus, %object
	.size	gFtlInitStatus, 4
gFtlInitStatus:
	.word	-1
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
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
	.zero	1
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.zero	1
	.zero	6
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
	.zero	3
	.type	slcPageToMlcPageTbl, %object
	.size	slcPageToMlcPageTbl, 1024
slcPageToMlcPageTbl:
	.zero	1024
	.type	DieAddrs, %object
	.size	DieAddrs, 32
DieAddrs:
	.zero	32
	.type	FlashWaitBusyScheduleEn, %object
	.size	FlashWaitBusyScheduleEn, 4
FlashWaitBusyScheduleEn:
	.zero	4
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
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
	.zero	32
	.type	gDieOp, %object
	.size	gDieOp, 192
gDieOp:
	.zero	192
	.type	gFlashCurMode, %object
	.size	gFlashCurMode, 1
gFlashCurMode:
	.zero	1
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.zero	1
	.type	gNandMaxDie, %object
	.size	gNandMaxDie, 1
gNandMaxDie:
	.zero	1
	.zero	1
	.type	DieCsIndex, %object
	.size	DieCsIndex, 8
DieCsIndex:
	.zero	8
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.zero	1
	.zero	3
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
	.type	gNandFlashEccBits, %object
	.size	gNandFlashEccBits, 1
gNandFlashEccBits:
	.zero	1
	.zero	3
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
	.zero	6
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
	.type	g_MaxLbaSector, %object
	.size	g_MaxLbaSector, 4
g_MaxLbaSector:
	.zero	4
	.type	g_page_map_check_enable, %object
	.size	g_page_map_check_enable, 2
g_page_map_check_enable:
	.zero	2
	.type	g_totle_vendor_block, %object
	.size	g_totle_vendor_block, 2
g_totle_vendor_block:
	.zero	2
	.type	p_vendor_block_table, %object
	.size	p_vendor_block_table, 8
p_vendor_block_table:
	.zero	8
	.type	g_wr_page_num, %object
	.size	g_wr_page_num, 4
g_wr_page_num:
	.zero	4
	.zero	4
	.type	req_wr_io, %object
	.size	req_wr_io, 8
req_wr_io:
	.zero	8
	.type	g_MaxLpn, %object
	.size	g_MaxLpn, 4
g_MaxLpn:
	.zero	4
	.zero	4
	.type	gBbtInfo, %object
	.size	gBbtInfo, 96
gBbtInfo:
	.zero	96
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.zero	2056
	.type	g_flash_read_only_en, %object
	.size	g_flash_read_only_en, 4
g_flash_read_only_en:
	.zero	4
	.zero	4
	.type	req_erase, %object
	.size	req_erase, 8
req_erase:
	.zero	8
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 8
p_erase_count_table:
	.zero	8
	.type	g_totle_sys_slc_erase_count, %object
	.size	g_totle_sys_slc_erase_count, 4
g_totle_sys_slc_erase_count:
	.zero	4
	.zero	4
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.zero	48
	.type	p_data_block_list_table, %object
	.size	p_data_block_list_table, 8
p_data_block_list_table:
	.zero	8
	.type	p_data_block_list_head, %object
	.size	p_data_block_list_head, 8
p_data_block_list_head:
	.zero	8
	.type	p_valid_page_count_table, %object
	.size	p_valid_page_count_table, 8
p_valid_page_count_table:
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
	.type	p_l2p_ram_map, %object
	.size	p_l2p_ram_map, 8
p_l2p_ram_map:
	.zero	8
	.type	g_l2p_last_update_region_id, %object
	.size	g_l2p_last_update_region_id, 2
g_l2p_last_update_region_id:
	.zero	2
	.type	FtlUpdateVaildLpnCount, %object
	.size	FtlUpdateVaildLpnCount, 2
FtlUpdateVaildLpnCount:
	.zero	2
	.type	g_VaildLpn, %object
	.size	g_VaildLpn, 4
g_VaildLpn:
	.zero	4
	.type	g_totle_read_page_count, %object
	.size	g_totle_read_page_count, 4
g_totle_read_page_count:
	.zero	4
	.type	g_totle_discard_page_count, %object
	.size	g_totle_discard_page_count, 4
g_totle_discard_page_count:
	.zero	4
	.type	g_totle_write_page_count, %object
	.size	g_totle_write_page_count, 4
g_totle_write_page_count:
	.zero	4
	.type	g_totle_cache_write_count, %object
	.size	g_totle_cache_write_count, 4
g_totle_cache_write_count:
	.zero	4
	.type	g_totle_l2p_write_count, %object
	.size	g_totle_l2p_write_count, 4
g_totle_l2p_write_count:
	.zero	4
	.type	g_totle_gc_page_count, %object
	.size	g_totle_gc_page_count, 4
g_totle_gc_page_count:
	.zero	4
	.type	g_totle_write_sector, %object
	.size	g_totle_write_sector, 4
g_totle_write_sector:
	.zero	4
	.type	g_totle_read_sector, %object
	.size	g_totle_read_sector, 4
g_totle_read_sector:
	.zero	4
	.type	g_GlobalSysVersion, %object
	.size	g_GlobalSysVersion, 4
g_GlobalSysVersion:
	.zero	4
	.type	g_GlobalDataVersion, %object
	.size	g_GlobalDataVersion, 4
g_GlobalDataVersion:
	.zero	4
	.type	g_totle_mlc_erase_count, %object
	.size	g_totle_mlc_erase_count, 4
g_totle_mlc_erase_count:
	.zero	4
	.type	g_totle_slc_erase_count, %object
	.size	g_totle_slc_erase_count, 4
g_totle_slc_erase_count:
	.zero	4
	.type	g_totle_avg_erase_count, %object
	.size	g_totle_avg_erase_count, 4
g_totle_avg_erase_count:
	.zero	4
	.type	g_max_erase_count, %object
	.size	g_max_erase_count, 4
g_max_erase_count:
	.zero	4
	.type	g_min_erase_count, %object
	.size	g_min_erase_count, 4
g_min_erase_count:
	.zero	4
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.zero	2
	.zero	2
	.type	gSysInfo, %object
	.size	gSysInfo, 16
gSysInfo:
	.zero	16
	.type	g_gc_superblock, %object
	.size	g_gc_superblock, 48
g_gc_superblock:
	.zero	48
	.type	g_sys_ext_data, %object
	.size	g_sys_ext_data, 512
g_sys_ext_data:
	.zero	512
	.type	g_all_blk_used_slc_mode, %object
	.size	g_all_blk_used_slc_mode, 4
g_all_blk_used_slc_mode:
	.zero	4
	.type	g_gc_free_blk_threshold, %object
	.size	g_gc_free_blk_threshold, 2
g_gc_free_blk_threshold:
	.zero	2
	.type	g_gc_merge_free_blk_threshold, %object
	.size	g_gc_merge_free_blk_threshold, 2
g_gc_merge_free_blk_threshold:
	.zero	2
	.type	g_gc_skip_write_count, %object
	.size	g_gc_skip_write_count, 4
g_gc_skip_write_count:
	.zero	4
	.type	g_gc_blk_index, %object
	.size	g_gc_blk_index, 2
g_gc_blk_index:
	.zero	2
	.zero	2
	.type	g_in_swl_replace, %object
	.size	g_in_swl_replace, 4
g_in_swl_replace:
	.zero	4
	.type	g_gc_num_req, %object
	.size	g_gc_num_req, 4
g_gc_num_req:
	.zero	4
	.type	gp_gc_page_buf_info, %object
	.size	gp_gc_page_buf_info, 8
gp_gc_page_buf_info:
	.zero	8
	.type	p_gc_data_buf, %object
	.size	p_gc_data_buf, 8
p_gc_data_buf:
	.zero	8
	.type	p_gc_spare_buf, %object
	.size	p_gc_spare_buf, 8
p_gc_spare_buf:
	.zero	8
	.type	req_gc, %object
	.size	req_gc, 8
req_gc:
	.zero	8
	.type	c_gc_page_buf_num, %object
	.size	c_gc_page_buf_num, 4
c_gc_page_buf_num:
	.zero	4
	.type	g_gc_blk_num, %object
	.size	g_gc_blk_num, 2
g_gc_blk_num:
	.zero	2
	.zero	2
	.type	p_gc_blk_tbl, %object
	.size	p_gc_blk_tbl, 8
p_gc_blk_tbl:
	.zero	8
	.type	g_gc_page_offset, %object
	.size	g_gc_page_offset, 2
g_gc_page_offset:
	.zero	2
	.zero	6
	.type	p_gc_page_info, %object
	.size	p_gc_page_info, 8
p_gc_page_info:
	.zero	8
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
	.type	g_gc_bad_block_temp_num, %object
	.size	g_gc_bad_block_temp_num, 2
g_gc_bad_block_temp_num:
	.zero	2
	.zero	6
	.type	g_gc_bad_block_temp_tbl, %object
	.size	g_gc_bad_block_temp_tbl, 34
g_gc_bad_block_temp_tbl:
	.zero	34
	.type	g_gc_bad_block_gc_index, %object
	.size	g_gc_bad_block_gc_index, 2
g_gc_bad_block_gc_index:
	.zero	2
	.type	mlcPageToSlcPageTbl, %object
	.size	mlcPageToSlcPageTbl, 2048
mlcPageToSlcPageTbl:
	.zero	2048
	.type	gNandMaxChip, %object
	.size	gNandMaxChip, 1
gNandMaxChip:
	.zero	1
	.zero	1
	.type	gTotleBlock, %object
	.size	gTotleBlock, 2
gTotleBlock:
	.zero	2
	.type	g_free_slc_blk_num, %object
	.size	g_free_slc_blk_num, 2
g_free_slc_blk_num:
	.zero	2
	.zero	2
	.type	g_SlcPartLbaEndSector, %object
	.size	g_SlcPartLbaEndSector, 4
g_SlcPartLbaEndSector:
	.zero	4
	.type	g_in_gc_progress, %object
	.size	g_in_gc_progress, 4
g_in_gc_progress:
	.zero	4
	.type	g_gc_head_data_block, %object
	.size	g_gc_head_data_block, 4
g_gc_head_data_block:
	.zero	4
	.type	g_gc_head_data_block_count, %object
	.size	g_gc_head_data_block_count, 4
g_gc_head_data_block_count:
	.zero	4
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
	.zero	4
	.type	g_gc_refresh_block_temp_num, %object
	.size	g_gc_refresh_block_temp_num, 2
g_gc_refresh_block_temp_num:
	.zero	2
	.zero	2
	.type	c_wr_page_buf_num, %object
	.size	c_wr_page_buf_num, 4
c_wr_page_buf_num:
	.zero	4
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
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 8
p_sys_spare_buf:
	.zero	8
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 8
p_io_spare_buf:
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
	.type	p_valid_page_count_check_table, %object
	.size	p_valid_page_count_check_table, 8
p_valid_page_count_check_table:
	.zero	8
	.type	p_map_block_table, %object
	.size	p_map_block_table, 8
p_map_block_table:
	.zero	8
	.type	p_map_block_valid_page_count, %object
	.size	p_map_block_valid_page_count, 8
p_map_block_valid_page_count:
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
	.type	p_l2p_map_buf, %object
	.size	p_l2p_map_buf, 8
p_l2p_map_buf:
	.zero	8
	.type	c_ftl_nand_bbm_buf_size, %object
	.size	c_ftl_nand_bbm_buf_size, 2
c_ftl_nand_bbm_buf_size:
	.zero	2
	.zero	6
	.type	gL2pMapInfo, %object
	.size	gL2pMapInfo, 64
gL2pMapInfo:
	.zero	64
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.zero	2
	.zero	6
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
	.type	gNandIDataBuf, %object
	.size	gNandIDataBuf, 2048
gNandIDataBuf:
	.zero	2048
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
	.type	gFlashPageBuffer0, %object
	.size	gFlashPageBuffer0, 8
gFlashPageBuffer0:
	.zero	8
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
	.type	gNandFlashIdbBlockAddr, %object
	.size	gNandFlashIdbBlockAddr, 4
gNandFlashIdbBlockAddr:
	.zero	4
	.type	gNandIDBResBlkNumSaveInFlash, %object
	.size	gNandIDBResBlkNumSaveInFlash, 1
gNandIDBResBlkNumSaveInFlash:
	.zero	1
	.type	g_maxRetryCount, %object
	.size	g_maxRetryCount, 1
g_maxRetryCount:
	.zero	1
	.type	gNandFlashIDBEccBits, %object
	.size	gNandFlashIDBEccBits, 1
gNandFlashIDBEccBits:
	.zero	1
	.zero	5
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
	.type	req_sys, %object
	.size	req_sys, 56
req_sys:
	.zero	56
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.zero	4
	.zero	4
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 64
gVendorBlkInfo:
	.zero	64
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.zero	2
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
	.type	gLoaderBootInfo, %object
	.size	gLoaderBootInfo, 8
gLoaderBootInfo:
	.zero	8
	.type	RK29_NANDC1_REG_BASE, %object
	.size	RK29_NANDC1_REG_BASE, 8
RK29_NANDC1_REG_BASE:
	.zero	8
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.zero	4
	.type	last_cache_match_count, %object
	.size	last_cache_match_count, 4
last_cache_match_count:
	.zero	4
	.type	idb_write_enable, %object
	.size	idb_write_enable, 1
idb_write_enable:
	.zero	1
	.zero	7
	.type	idb_buf, %object
	.size	idb_buf, 8
idb_buf:
	.zero	8
	.type	idb_last_lba, %object
	.size	idb_last_lba, 4
idb_last_lba:
	.zero	4
	.zero	4
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
	.type	g_idb_buffer, %object
	.size	g_idb_buffer, 8
g_idb_buffer:
	.zero	8
	.type	g_vendor, %object
	.size	g_vendor, 8
g_vendor:
	.zero	8
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
	.string	"FlashEraseBlocks pageAddr error %x\n"
.LC1:
	.string	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\n"
.LC2:
	.string	"FtlFreeSysBlkQueueOut free count = %d\n"
.LC3:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
.LC4:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
.LC5:
	.string	"FLASH INFO:\n"
.LC6:
	.string	"FLASH ID: %x\n"
.LC7:
	.string	"Device Capacity: %d MB\n"
.LC8:
	.string	"FMWAIT: %x %x %x %x\n"
.LC9:
	.string	"FTL INFO:\n"
.LC10:
	.string	"g_MaxLpn = 0x%x\n"
.LC11:
	.string	"g_VaildLpn = 0x%x\n"
.LC12:
	.string	"read_page_count = 0x%x\n"
.LC13:
	.string	"discard_page_count = 0x%x\n"
.LC14:
	.string	"write_page_count = 0x%x\n"
.LC15:
	.string	"cache_write_count = 0x%x\n"
.LC16:
	.string	"l2p_write_count = 0x%x\n"
.LC17:
	.string	"gc_page_count = 0x%x\n"
.LC18:
	.string	"totle_write = %d MB\n"
.LC19:
	.string	"totle_read = %d MB\n"
.LC20:
	.string	"GSV = 0x%x\n"
.LC21:
	.string	"GDV = 0x%x\n"
.LC22:
	.string	"bad blk num = %d %d\n"
.LC23:
	.string	"free_superblocks = 0x%x\n"
.LC24:
	.string	"mlc_EC = 0x%x\n"
.LC25:
	.string	"slc_EC = 0x%x\n"
.LC26:
	.string	"avg_EC = 0x%x\n"
.LC27:
	.string	"sys_EC = 0x%x\n"
.LC28:
	.string	"max_EC = 0x%x\n"
.LC29:
	.string	"min_EC = 0x%x\n"
.LC30:
	.string	"PLT = 0x%x\n"
.LC31:
	.string	"POT = 0x%x\n"
.LC32:
	.string	"MaxSector = 0x%x\n"
.LC33:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC34:
	.string	"sys_blks_pp = 0x%x\n"
.LC35:
	.string	"free sysblock = 0x%x\n"
.LC36:
	.string	"data_blks_pp = 0x%x\n"
.LC37:
	.string	"data_op_blks_pp = 0x%x\n"
.LC38:
	.string	"max_data_blks = 0x%x\n"
.LC39:
	.string	"Sys.id = 0x%x\n"
.LC40:
	.string	"Bbt.id = 0x%x\n"
.LC41:
	.string	"ACT.page = 0x%x\n"
.LC42:
	.string	"ACT.plane = 0x%x\n"
.LC43:
	.string	"ACT.id = 0x%x\n"
.LC44:
	.string	"ACT.mode = 0x%x\n"
.LC45:
	.string	"ACT.a_pages = 0x%x\n"
.LC46:
	.string	"ACT VPC = 0x%x\n"
.LC47:
	.string	"BUF.page = 0x%x\n"
.LC48:
	.string	"BUF.plane = 0x%x\n"
.LC49:
	.string	"BUF.id = 0x%x\n"
.LC50:
	.string	"BUF.mode = 0x%x\n"
.LC51:
	.string	"BUF.a_pages = 0x%x\n"
.LC52:
	.string	"BUF VPC = 0x%x\n"
.LC53:
	.string	"TMP.page = 0x%x\n"
.LC54:
	.string	"TMP.plane = 0x%x\n"
.LC55:
	.string	"TMP.id = 0x%x\n"
.LC56:
	.string	"TMP.mode = 0x%x\n"
.LC57:
	.string	"TMP.a_pages = 0x%x\n"
.LC58:
	.string	"GC.page = 0x%x\n"
.LC59:
	.string	"GC.plane = 0x%x\n"
.LC60:
	.string	"GC.id = 0x%x\n"
.LC61:
	.string	"GC.mode = 0x%x\n"
.LC62:
	.string	"GC.a_pages = 0x%x\n"
.LC63:
	.string	"WR_CHK = 0x%x %x %x %x\n"
.LC64:
	.string	"Read Err = 0x%x\n"
.LC65:
	.string	"Prog Err = 0x%x\n"
.LC66:
	.string	"gc_free_blk_th= 0x%x\n"
.LC67:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC68:
	.string	"gc_skip_write_count= 0x%x\n"
.LC69:
	.string	"gc_blk_index= 0x%x\n"
.LC70:
	.string	"free min EC= 0x%x\n"
.LC71:
	.string	"free max EC= 0x%x\n"
.LC72:
	.string	"GC__SB VPC = 0x%x\n"
.LC73:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC74:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC75:
	.string	"FTL version: 5.0.63 20210616"
.LC76:
	.string	"%s\n"
.LC77:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC78:
	.string	"FtlGcRefreshBlock  0x%x\n"
.LC79:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
.LC80:
	.string	"%s error allocating memory. return -1\n"
.LC81:
	.string	"%s %p:0x%x:"
.LC82:
	.string	"%x "
.LC83:
	.string	""
.LC84:
	.string	"otp error! %d"
.LC85:
	.string	"rr"
.LC86:
	.string	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\n"
.LC87:
	.string	"nandc:"
.LC88:
	.string	"%d flReg.d32=%x %x\n"
.LC89:
	.string	"sdr read ok %x ecc=%d\n"
.LC90:
	.string	"sync para %d\n"
.LC91:
	.string	"TOG mode Read error %x %x\n"
.LC92:
	.string	"read retry status %x %x %x\n"
.LC93:
	.string	"micron RR %d row=%x,count %d,status=%d\n"
.LC94:
	.string	"samsung RR %d row=%x,count %d,status=%d\n"
.LC95:
	.string	"ECC:%d\n"
.LC96:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC97:
	.string	"FlashLoadPhyInfo fail %x!!\n"
.LC98:
	.string	"Read pageadd=%x  ecc=%x err=%x\n"
.LC99:
	.string	"data:"
.LC100:
	.string	"spare:"
.LC101:
	.string	"ReadRetry pageadd=%x ecc=%x err=%x\n"
.LC102:
	.string	"FLFB:%d %d\n"
.LC103:
	.string	"prog error: = %x\n"
.LC104:
	.string	"prog read error: = %x\n"
.LC105:
	.string	"prog read REFRESH: = %x\n"
.LC106:
	.string	"prog read s error: = %x %x %x\n"
.LC107:
	.string	"prog read d error: = %x %x %x\n"
.LC108:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data= %x\n"
.LC109:
	.string	"...%s enter...\n"
.LC110:
	.string	"superBlkID = %x vpc=%x\n"
.LC111:
	.string	"flashmode = %x pagenum = %x %x\n"
.LC112:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC113:
	.string	"blk = %x vpc=%x mode = %x\n"
.LC114:
	.string	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC115:
	.string	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC116:
	.string	"ftl_scan_all_ppa blk %x page %x flag: %x\n"
.LC117:
	.string	"ftl_scan_all_ppa blk %x page %x flag: %x ............... is bad block\n"
.LC118:
	.string	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\n"
.LC119:
	.string	"%s finished\n"
.LC120:
	.string	"FlashMakeFactorBbt %d\n"
.LC121:
	.string	"bad block:%d %d\n"
.LC122:
	.string	"FMFB:%d %d\n"
.LC123:
	.string	"E:bad block:%d\n"
.LC124:
	.string	"FMFB:Save %d %d\n"
.LC125:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC126:
	.string	"FtlBbmTblFlush error:%x\n"
.LC127:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC128:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC129:
	.string	"decrement_vpc_count %x = %d\n"
.LC130:
	.string	"decrement_vpc_count %x = %d in free list\n"
.LC131:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC132:
	.string	"page map lost: %x %x\n"
.LC133:
	.string	"FtlMapWritePage error = %x\n"
.LC134:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC135:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC136:
	.string	"no ect"
.LC137:
	.string	"slc mode"
.LC138:
	.string	"BBT:"
.LC139:
	.string	"region_id = %x phyAddr = %x\n"
.LC140:
	.string	"map_ppn:"
.LC141:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC142:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC143:
	.string	"free blk vpc error %x = %x  %x\n"
.LC144:
	.string	"error_flag %x\n"
.LC145:
	.string	"Ftlscanalldata = %x\n"
.LC146:
	.string	"scan lpa = %x ppa= %x\n"
.LC147:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC148:
	.string	"RSB refresh addr %x\n"
.LC149:
	.string	"spuer block %x vpn is 0\n "
.LC150:
	.string	"g_recovery_ppa %x ver %x\n "
.LC151:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC152:
	.string	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
.LC153:
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC154:
	.string	"GC des block %x done\n"
.LC155:
	.string	"too many bad block  = %d %d\n"
.LC156:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC157:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC158:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC159:
	.string	"rk_ftl_de_init %x\n"
.LC160:
	.string	"...%s: no bad block mapping table, format device\n"
.LC161:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
.LC162:
	.string	"FtlInit %x\n"
.LC163:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC164:
	.string	"erase power lost blk = %x vpc=%x\n"
.LC165:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC166:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\n"
.LC167:
	.string	":"
.LC168:
	.string	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC169:
	.string	"Mblk:"
.LC170:
	.string	"L2P:"
.LC171:
	.string	"L2PC:"
.LC172:
	.string	"write_idblock fix data %x %x\n"
.LC173:
	.string	"idblk:"
.LC174:
	.string	"idb reverse %x %x\n"
.LC175:
	.string	"write_idblock totle_sec %x %x %x %x\n"
.LC176:
	.string	"IDBlockWriteData %x %x\n"
.LC177:
	.string	"IDBlockWriteData %x %x ret= %x\n"
.LC178:
	.string	"IdBlockReadData %x %x\n"
.LC179:
	.string	"IdBlockReadData %x %x ret= %x\n"
.LC180:
	.string	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x\n"
.LC181:
	.string	"write"
.LC182:
	.string	"read"
.LC183:
	.string	"write_idblock error %d\n"
.LC184:
	.string	"wl_lba %p %x %x %x\n"
.LC185:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC186:
	.string	"rk_copy_from_user error\n"
.LC187:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC188:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC189:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC190:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC191:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC192:
	.string	"RKNAND_LOADER_STATUS\n"
.LC193:
	.string	"RKNAND_LOADER_LOCK\n"
.LC194:
	.string	"LockKey not match %d\n"
.LC195:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC196:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC197:
	.string	"return ret = %lx\n"
.LC198:
	.string	"secureBootEn check error\n"
.LC199:
	.string	"\0013vendor storage %x,%x,%x\n"
