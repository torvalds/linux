/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-09-23
 */
	.cpu generic+fp+simd
	.file	"rk_ftl_arm_v8.S"
	.text
	.align	2
	.type	ftl_set_blk_mode.part.9, %function
ftl_set_blk_mode.part.9:
	adrp	x1, .LANCHOR0
	uxth	w0, w0
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	orr	w0, w1, w0
	str	w0, [x3,x2]
	ret
	.size	ftl_set_blk_mode.part.9, .-ftl_set_blk_mode.part.9
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	adrp	x2, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	stp	x29, x30, [sp, -32]!
	add	x2, x2, :lo12:.LANCHOR0
	add	x2, x2, 8
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
	adrp	x0, .LANCHOR0+72
	ldrb	w0, [x0,#:lo12:.LANCHOR0+72]
	cbz	w0, .L8
	ldrb	w4, [x5,1]
	mov	w0, 0
	ldrb	w3, [x1,1]
	cmp	w4, w3
	beq	.L5
.L8:
	mov	x3, 0
.L6:
	cmp	w3, w2
	mov	w4, w3
	bcs	.L12
	ldrb	w0, [x5,x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6,-1]
	cmp	w6, w0
	beq	.L6
	add	w0, w4, 1
	b	.L5
.L12:
	mov	w0, 0
.L5:
	ret
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	2
	.global	FlashRsvdBlkChk
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	adrp	x2, .LANCHOR0
	uxtb	w0, w0
	add	x2, x2, :lo12:.LANCHOR0
	ldrb	w3, [x2,73]
	ldr	w2, [x2,76]
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
	adrp	x2, .LANCHOR0+80
	ldrb	w2, [x2,#:lo12:.LANCHOR0+80]
	cbz	w2, .L15
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w19, -1073741824
	csel	w19, w1, w19, ne
.L15:
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
	ldrb	w0, [x0,80]
	cbz	w0, .L22
	mov	w0, w21
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w20, -1073741824
	csel	w20, w1, w20, ne
.L22:
	add	x2, x19, :lo12:.LANCHOR0
	add	x2, x2, x21, sxtw 4
	ldr	x0, [x2,8]
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
	bls	.L29
	mov	w0, 1024
	b	.L33
.L29:
	cmp	w0, 256
	bls	.L31
	mov	w0, 512
	b	.L33
.L31:
	cmp	w0, 128
	bls	.L33
	mov	w0, 256
.L33:
	str	w0, [x1,76]
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
	add	x3, x4, 8
	add	x29, sp, 0
	ldr	x4, [x4,88]
	add	x5, x3, x2
	ldr	x2, [x3,x2]
	ldrb	w4, [x4,7]
	ldrb	w3, [x5,8]
	cmp	w4, 1
	bne	.L35
	sxtw	x4, w3
	mov	w5, 38
	add	x4, x4, 8
	add	x4, x2, x4, lsl 8
	str	w5, [x4,8]
.L35:
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
	add	x4, x3, 8
	stp	x29, x30, [sp, -16]!
	add	x5, x4, x2
	add	x29, sp, 0
	ldr	x6, [x4,x2]
	lsr	w4, w1, 8
	ldrb	w2, [x3,112]
	lsr	w3, w1, 16
	cmp	w2, 1
	ldrb	w2, [x5,8]
	and	w5, w1, 255
	add	x2, x6, x2, lsl 8
	bne	.L37
	mov	w6, 6
	str	w6, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w5, [x2,2052]
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	b	.L39
.L37:
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
.L39:
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
	add	x3, x3, 8
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
	add	x4, x3, 8
	add	x5, x4, x0
	ldr	x4, [x4,x0]
	ldrb	w0, [x5,8]
	cbz	w2, .L42
	add	x2, x4, x0, lsl 8
	mov	w5, 96
	str	w5, [x2,2056]
	and	w5, w1, 255
	str	w5, [x2,2052]
	lsr	w5, w1, 8
	str	w5, [x2,2052]
	lsr	w5, w1, 16
	str	w5, [x2,2052]
	ldr	w2, [x3,76]
	add	w1, w1, w2
.L42:
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
	add	x5, x2, 8
	lsr	w3, w1, 16
	add	x7, x5, x4
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w6, [x2,107]
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
	add	x1, x1, 8
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
	add	x2, x1, 8
	add	x3, x2, x0
	ldrb	w1, [x1,106]
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
	add	x1, x1, 8
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
.L51:
	cmp	w1, w2
	bls	.L53
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L51
.L53:
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
	stp	x29, x30, [sp, -96]!
	smull	x3, w2, w3
	adrp	x2, .LANCHOR1
	add	x29, sp, 0
	add	x2, x2, :lo12:.LANCHOR1
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	add	x23, x2, x1, sxtw
	add	x24, x2, 256
	add	x21, x2, 304
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
.L57:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,129]
	cmp	w0, w20
	bls	.L63
	add	x0, x19, 256
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	x0, 1000
	bl	__const_udelay
	ldrb	w0, [x25,128]
	cmp	w0, 34
	bne	.L58
	ldrsb	w0, [x24,x20]
	b	.L62
.L58:
	cmp	w0, 35
	bne	.L60
	ldrsb	w0, [x21,x20]
	b	.L62
.L60:
	ldrsb	w0, [x23,400]
.L62:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L57
.L63:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	2
	.global	SamsungSetRRPara
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	stp	x29, x30, [sp, -64]!
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	add	x29, sp, 0
	add	x1, x2, x1, uxtb 2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x21, x0
	add	x22, x1, 412
	mov	x20, 0
	adrp	x23, .LANCHOR0
	mov	w24, 161
	mov	x19, x2
.L65:
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,129]
	cmp	w0, w20
	bls	.L67
	str	w24, [x21,8]
	add	x0, x19, 408
	str	wzr, [x21]
	ldrsb	w0, [x20,x0]
	str	w0, [x21]
	ldrsb	w0, [x22,x20]
	add	x20, x20, 1
	str	w0, [x21]
	mov	x0, 1500
	bl	__const_udelay
	b	.L65
.L67:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	2
	.global	ftl_flash_suspend
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0,136]
	ldr	w2, [x1]
	str	w2, [x0,144]
	ldr	w2, [x1,4]
	str	w2, [x0,148]
	ldr	w2, [x1,8]
	str	w2, [x0,152]
	ldr	w2, [x1,12]
	str	w2, [x0,156]
	ldr	w2, [x1,304]
	str	w2, [x0,160]
	ldr	w2, [x1,308]
	str	w2, [x0,164]
	ldr	w2, [x1,336]
	ldr	w1, [x1,344]
	str	w2, [x0,168]
	str	w1, [x0,172]
	ret
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	adrp	x7, .LANCHOR0
	uxtb	w4, w4
	add	x6, x7, :lo12:.LANCHOR0
	ldrh	w8, [x6,190]
	ldrh	w5, [x6,188]
	ldrh	w9, [x6,76]
	ldrb	w6, [x6,72]
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
	bne	.L71
	add	x1, x7, :lo12:.LANCHOR0
	ldrb	w5, [x1,204]
	cbnz	w5, .L71
	add	x1, x1, 208
	ldrh	w8, [x1,w8,sxtw 1]
.L71:
	add	x7, x7, :lo12:.LANCHOR0
	cmp	w4, 1
	add	x7, x7, 1232
	mov	w1, 0
	ldr	w5, [x7,w10,uxtw 2]
	madd	w5, w6, w9, w5
	add	w5, w5, w8
	str	w5, [x2]
	str	w10, [x3]
	bls	.L72
	ldr	w1, [x0,4]
	ldr	w0, [x0,60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w1, eq
.L72:
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
	add	x5, x4, 8
	stp	x19, x20, [sp,16]
	add	x6, x5, x0
	uxtb	w2, w2
	ldr	x20, [x5,x0]
	ldr	x0, [x4,88]
	ldrb	w19, [x6,8]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	bne	.L75
	add	x4, x4, 96
	cbnz	w2, .L76
	ldrb	w2, [x4,13]
	b	.L84
.L76:
	ldrb	w2, [x4,14]
.L84:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,111]
	cbz	w4, .L79
	add	x3, x19, 8
	lsl	x3, x3, 8
.L83:
	cmp	w2, w4
	bcs	.L79
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L83
.L75:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L79:
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
.L86:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L86
	tbnz	x3, 6, .L87
	mov	x0, 1
	mov	x1, 3
	bl	usleep_range
	b	.L86
.L87:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
	.align	2
	.global	FlashScheduleEnSet
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w2, [x1,1264]
	str	w0, [x1,1264]
	mov	w0, w2
	ret
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	2
	.global	FlashGetPageSize
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
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
	add	x1, x1, 8
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
	adrp	x1, .LANCHOR0+136
	uxtb	w0, w0
	lsl	w2, w0, 8
	orr	w0, w2, w0, lsl 16
	ldr	x1, [x1,#:lo12:.LANCHOR0+136]
	orr	w0, w0, 1
	str	w0, [x1,304]
	ret
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrDiv
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	adrp	x1, .LANCHOR0+136
	uxtb	w0, w0
	ldr	x2, [x1,#:lo12:.LANCHOR0+136]
	mov	w1, 16640
	orr	w0, w0, w1
	str	w0, [x2,344]
	ret
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	2
	.global	NandcSetDdrMode
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	adrp	x1, .LANCHOR0+136
	cmp	w0, wzr
	ldr	x2, [x1,#:lo12:.LANCHOR0+136]
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
	uxtb	w0, w0
	tst	w0, 6
	ldr	x2, [x1,#:lo12:.LANCHOR0+136]
	ldr	w1, [x2]
	beq	.L101
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
	b	.L103
.L101:
	and	w1, w1, -8193
.L103:
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
	add	x1, x1, 8
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
	add	x1, x1, 8
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
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x27, [sp,80]
	add	x19, x20, :lo12:.LANCHOR0
	uxtb	w27, w1
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxtb	x24, w0
	mov	x21, x2
	ldr	x0, [x19,88]
	mov	x26, x24
	uxtb	w25, w3
	ldrb	w0, [x0,19]
	cmp	w0, 6
	bne	.L109
	add	x19, x19, x24, lsl 6
	lsl	w4, w25, 2
	add	x19, x19, 1292
	b	.L115
.L109:
	cmp	w0, 7
	bne	.L111
	mov	x2, 160
	mov	w4, 10
	madd	x19, x24, x2, x19
	mul	w4, w25, w4
	add	x19, x19, 1300
.L115:
	add	x19, x19, x4, sxtw
	b	.L110
.L111:
	cmp	w0, 8
	bne	.L112
	add	x19, x19, 1300
	add	w4, w25, w25, lsl 2
	b	.L115
.L112:
	lsl	x4, x24, 3
	add	x4, x4, x25, sxtw
	add	x19, x19, x4, lsl 3
	add	x19, x19, 1292
.L110:
	add	x2, x20, :lo12:.LANCHOR0
	lsl	x0, x24, 4
	add	x2, x2, 8
	uxtb	x27, w27
	add	x1, x2, x0
	ldr	x22, [x2,x0]
	mov	w0, w26
	ldrb	w23, [x1,8]
	bl	NandcFlashCs
	ubfiz	x23, x23, 8, 8
	mov	w0, 54
	add	x22, x22, x23
	mov	x23, 0
	str	w0, [x22,2056]
.L113:
	cmp	x23, x27
	beq	.L116
	ldrb	w0, [x21,x23]
	str	w0, [x22,2052]
	mov	x0, 1000
	bl	__const_udelay
	ldrsb	w0, [x19,x23]
	add	x23, x23, 1
	str	w0, [x22,2048]
	b	.L113
.L116:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, 22
	add	x24, x20, x24
	str	w0, [x22,2056]
	mov	w0, w26
	bl	NandcFlashDeCs
	strb	w25, [x24,2128]
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
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	mov	x20, 0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	ldr	x0, [x19,88]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L117
	add	x21, x19, 2132
	add	x22, x19, 1276
.L122:
	lsl	x1, x20, 3
	uxtb	w0, w20
	ldrb	w1, [x1,x21]
	cmp	w1, 173
	bne	.L119
	ldrb	w1, [x19,1273]
	mov	x2, x22
	mov	w3, 0
	bl	HynixSetRRPara
.L119:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L122
.L117:
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
	add	x0, x20, 2164
	add	x19, x0, x1
	ldrb	w21, [x0,x1]
	ldr	x0, [x19,8]
	cbz	x0, .L126
	mov	w0, w21
	add	x20, x20, 1232
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
	cbz	x1, .L126
	str	w0, [x1]
	str	xzr, [x19,16]
.L126:
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
	add	x1, x1, 8
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	ldr	x20, [x1,x0]
	movk	w19, 0x1, lsl 16
.L134:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L135
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L134
	mov	w0, -1
	b	.L133
.L135:
	mov	w0, 0
.L133:
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
	add	x1, x1, 8
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	ldr	x20, [x1,x0]
	movk	w19, 0x1, lsl 16
	mov	x0, 650
	bl	__const_udelay
.L139:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L140
	mov	x0, 1
	mov	x1, 2
	bl	usleep_range
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
	add	x1, x1, 8
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
	adrp	x21, .LANCHOR0
	str	x23, [sp,48]
	stp	x19, x20, [sp,16]
	add	x20, x21, :lo12:.LANCHOR0
	uxtb	w22, w0
	ldrb	w0, [x20,204]
	cbz	w0, .L143
	mov	w0, w22
	bl	NandcFlashCs
	sxtw	x1, w22
	add	x2, x20, 8
	lsl	x0, x1, 4
	add	x1, x20, x1, lsl 3
	add	x3, x2, x0
	ldr	x23, [x2,x0]
	ldrb	w0, [x1,2132]
	ldrb	w19, [x3,8]
	cmp	w0, 44
	bne	.L145
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
.L145:
	add	x19, x19, 8
	mov	w0, w22
	add	x21, x21, :lo12:.LANCHOR0
	add	x19, x23, x19, lsl 8
	bl	NandcWaitFlashReadyNoDelay
	mov	w0, 218
	str	w0, [x19,8]
	mov	w0, w22
	bl	NandcWaitFlashReady
	mov	w0, 2
	strb	w0, [x21,2356]
.L143:
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
	adrp	x21, .LANCHOR0
	str	x23, [sp,48]
	stp	x19, x20, [sp,16]
	add	x20, x21, :lo12:.LANCHOR0
	uxtb	w22, w0
	ldrb	w0, [x20,204]
	cbz	w0, .L149
	mov	w0, w22
	bl	NandcFlashCs
	sxtw	x1, w22
	add	x2, x20, 8
	lsl	x0, x1, 4
	add	x1, x20, x1, lsl 3
	add	x3, x2, x0
	ldr	x23, [x2,x0]
	ldrb	w0, [x1,2132]
	ldrb	w19, [x3,8]
	cmp	w0, 44
	bne	.L151
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
.L151:
	add	x19, x19, 8
	mov	w0, w22
	add	x21, x21, :lo12:.LANCHOR0
	add	x19, x23, x19, lsl 8
	bl	NandcWaitFlashReadyNoDelay
	mov	w0, 223
	str	w0, [x19,8]
	mov	w0, w22
	bl	NandcWaitFlashReady
	strb	wzr, [x21,2356]
.L149:
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
	adrp	x1, .LANCHOR0
	mov	x6, 0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w8, 239
	stp	x29, x30, [sp, -16]!
	add	x7, x1, 8
	add	x4, x1, 2132
	add	x29, sp, 0
	ldrb	w2, [x1,2357]
	mov	w9, 128
	mov	w10, 1
	mov	w12, 35
	and	w11, w2, 1
	and	w2, w2, 4
	mov	w13, 32
	mov	w14, 5
	uxtb	w5, w2
.L167:
	ldrb	w3, [x6,x4]
	ldr	x1, [x7]
	cmp	w3, 152
	ldrb	w2, [x7,8]
	beq	.L157
	cmp	w3, 69
	beq	.L157
	cmp	w3, 173
	beq	.L157
	cmp	w3, 44
	bne	.L158
.L157:
	cmp	w0, 1
	bne	.L160
	cbz	w11, .L158
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L161
	str	w0, [x1,2052]
	b	.L178
.L161:
	cmp	w3, 44
	bne	.L163
	str	w0, [x1,2052]
	str	w14, [x1,2048]
	b	.L165
.L163:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L165
.L160:
	cbz	w5, .L158
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L164
	str	w10, [x1,2052]
	str	w13, [x1,2048]
	b	.L165
.L164:
	cmp	w3, 44
	bne	.L166
	str	w10, [x1,2052]
	str	w12, [x1,2048]
	b	.L165
.L166:
	str	w9, [x1,2052]
.L178:
	str	wzr, [x1,2048]
.L165:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L158:
	add	x6, x6, 8
	add	x7, x7, 16
	cmp	x6, 32
	bne	.L167
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
	add	x5, x5, 8
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
	add	x3, x3, 8
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
	adrp	x5, .LANCHOR0
	smull	x1, w19, w1
	add	x0, x2, 256
	add	x2, x2, 304
	add	x0, x0, x1
	add	x1, x2, x1
	mov	x2, 0
.L182:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,129]
	cmp	w4, w2
	bls	.L187
	ldrb	w3, [x3,128]
	cmp	w3, 67
	bne	.L183
	ldrsb	w3, [x0,x2]
	b	.L186
.L183:
	ldrsb	w3, [x1,x2]
.L186:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L182
.L187:
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
	add	x3, x3, 8
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
	adrp	x23, .LC0
	stp	x19, x20, [sp,16]
	mov	w24, w1
	mov	x20, x0
	mov	w22, 0
	add	x25, x21, 2164
	add	x23, x23, :lo12:.LC0
.L190:
	cmp	w22, w24
	beq	.L199
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,2358]
	cmp	w2, w0
	bcc	.L191
	mov	w0, -1
	str	w0, [x20]
	b	.L192
.L191:
	uxtw	x2, w2
	add	x0, x21, x2
	ldrb	w19, [x0,2360]
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
	ldr	w2, [x21,76]
	add	w1, w2, w1
	mov	w2, 0
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29,88]
	mov	w0, w19
	bl	FlashReadStatus
	tbz	x0, 0, .L193
	mov	w0, -1
	str	w0, [x20]
.L193:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L194
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L194:
	mov	w0, w19
	bl	NandcFlashDeCs
.L192:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L190
.L199:
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
	stp	x27, x28, [sp,80]
	mov	w27, w1
	adrp	x1, .LANCHOR0
	stp	x19, x20, [sp,16]
	add	x19, x1, :lo12:.LANCHOR0
	str	x1, [x29,104]
	stp	x23, x24, [sp,48]
	mov	w23, w2
	mov	x24, x0
	ldrb	w2, [x19,72]
	mov	w20, 0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	cbz	w2, .L201
	mov	w1, w23
	bl	FlashEraseSLc2KBlocks
	b	.L202
.L201:
	mov	w28, 56
	add	x25, x19, 2164
	mov	x26, 24
.L232:
	cmp	w20, w23
	bcs	.L233
	umull	x5, w20, w28
	mov	w1, 0
	sub	w4, w23, w20
	add	x21, x24, x5
	add	x2, x29, 120
	mov	x0, x21
	add	x3, x29, 124
	str	x5, [x29,96]
	bl	LogAddr2PhyAddr
	mov	w22, w0
	ldrb	w1, [x19,2358]
	ldr	w0, [x29,124]
	ldr	x5, [x29,96]
	cmp	w0, w1
	bcc	.L203
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L204
.L203:
	ldrb	w1, [x19,2368]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w22, w22, wzr, ne
	madd	x1, x1, x26, x25
	ldr	x1, [x1,8]
	cbz	x1, .L206
	bl	FlashWaitCmdDone
.L206:
	ldr	w0, [x29,124]
	ldr	w1, [x29,120]
	madd	x2, x0, x26, x25
	str	x21, [x2,8]
	str	xzr, [x2,16]
	str	w1, [x2,4]
	cbz	w22, .L207
	add	w1, w20, 1
	umull	x1, w1, w28
	add	x1, x24, x1
	str	x1, [x2,16]
.L207:
	add	x1, x19, x0
	mul	x0, x0, x26
	ldrb	w21, [x1,2360]
	strb	w21, [x25,x0]
	mov	w0, w21
	bl	NandcFlashCs
	cmp	w27, 1
	bne	.L208
	ldrb	w0, [x19,204]
	cbz	w0, .L208
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L209
.L208:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L209:
	ldr	w1, [x29,124]
	add	x0, x19, 1232
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
.L204:
	add	w20, w20, 1
	b	.L232
.L233:
	ldr	x0, [x29,104]
	mov	x20, 0
	mov	x22, 24
	add	x19, x0, :lo12:.LANCHOR0
	add	x21, x19, 2164
.L211:
	ldrb	w0, [x19,2358]
	cmp	w0, w20
	bls	.L234
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w27, 1
	bne	.L212
	ldrb	w0, [x19,204]
	cbz	w0, .L212
	mul	x0, x20, x22
	ldrb	w0, [x0,x21]
	bl	flash_exit_slc_mode
.L212:
	add	x20, x20, 1
	b	.L211
.L234:
	ldr	w0, [x19,2372]
	cbnz	w0, .L214
.L216:
	mov	w0, 0
	b	.L202
.L214:
	ldrb	w0, [x19,2132]
	cmp	w0, 69
	bne	.L216
	mov	x0, 0
	mov	x1, 56
.L215:
	cmp	w23, w0
	bls	.L216
	mul	x2, x0, x1
	add	x0, x0, 1
	str	wzr, [x24,x2]
	b	.L215
.L202:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FlashEraseBlocks, .-FlashEraseBlocks
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
	add	x4, x6, 8
	stp	x19, x20, [sp,16]
	str	x23, [sp,48]
	add	x7, x4, x0
	and	w23, w2, 255
	lsr	w22, w2, 8
	ldr	x5, [x4,x0]
	lsr	w20, w2, 16
	ldrb	w0, [x6,112]
	ldr	x2, [x6,88]
	cmp	w0, 1
	ldrb	w19, [x7,8]
	lsr	w0, w1, 16
	lsr	w7, w1, 8
	ldrb	w2, [x2,7]
	bne	.L236
	cmp	w2, 1
	bne	.L237
	sxtw	x4, w19
	mov	w2, 38
	add	x4, x4, 8
	add	x4, x5, x4, lsl 8
	str	w2, [x4,8]
.L237:
	add	x3, x3, :lo12:.LANCHOR0
	add	x19, x5, x19, lsl 8
	str	x1, [x29,72]
	ldrb	w2, [x3,104]
	str	w2, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w8, [x19,2052]
	str	w7, [x19,2052]
	str	w0, [x19,2052]
	ldrb	w0, [x3,105]
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
	b	.L238
.L236:
	cmp	w2, 1
	bne	.L239
	sxtw	x4, w19
	mov	w2, 38
	add	x4, x4, 8
	add	x4, x5, x4, lsl 8
	str	w2, [x4,8]
.L239:
	add	x3, x3, :lo12:.LANCHOR0
	add	x19, x5, x19, lsl 8
	ldrb	w2, [x3,104]
	str	w2, [x19,2056]
	str	w8, [x19,2052]
	str	w7, [x19,2052]
	str	w0, [x19,2052]
	ldrb	w0, [x3,105]
	str	w0, [x19,2056]
	mov	w0, 48
	str	w23, [x19,2052]
	str	w22, [x19,2052]
	str	w20, [x19,2052]
	str	w0, [x19,2056]
.L238:
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
	stp	x19, x20, [sp,16]
	bl	NandcWaitFlashReady
	adrp	x19, .LANCHOR0
	bl	FlashSetReadRetryDefault
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w0, [x0,2376]
	cbz	w0, .L241
	mov	w0, 0
	bl	flash_enter_slc_mode
	b	.L242
.L241:
	bl	flash_exit_slc_mode
.L242:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,2380]
	cbz	w0, .L243
	ldrb	w0, [x20,2357]
	tbz	x0, 0, .L243
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,2380]
.L243:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,8]
	str	wzr, [x0,336]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
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
	add	x2, x2, 8
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
	ble	.L252
	ldr	x0, [x1,136]
	mov	w1, 8354
	b	.L259
.L252:
	cmp	w0, 220
	ble	.L254
	ldr	x0, [x1,136]
	b	.L260
.L254:
	cmp	w0, 185
	ble	.L255
	ldr	x0, [x1,136]
	mov	w1, 4226
	b	.L259
.L255:
	cmp	w0, 160
	ldr	x0, [x1,136]
	ble	.L256
	mov	w1, 4194
	b	.L259
.L256:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L259
	cmp	w19, 99
	mov	w1, 4225
	bls	.L259
.L260:
	mov	w1, 8322
.L259:
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
	bls	.L262
	mov	w1, 8322
	cmp	w0, w1
	bne	.L263
.L262:
	adrp	x1, .LANCHOR0+136
	ldr	x1, [x1,#:lo12:.LANCHOR0+136]
	str	w0, [x1,4]
.L263:
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
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	str	w3, [x1,32]
	mov	w3, 2
	str	wzr, [x1,16]
	str	w3, [x1,48]
	mov	w3, 3
	str	x0, [x1,8]
	str	w3, [x1,64]
	str	x0, [x1,24]
	str	x0, [x1,40]
	str	x0, [x1,56]
	str	x0, [x1,136]
	ldr	w2, [x0]
	ubfx	x3, x2, 13, 1
	str	w3, [x1,2384]
	ldr	w3, [x0,352]
	and	w2, w2, 245760
	orr	w2, w2, 256
	ubfx	x3, x3, 16, 4
	str	w3, [x1,2388]
	ldr	w3, [x0,352]
	str	w3, [x1,2392]
	cmp	w3, 2049
	bne	.L265
	mov	w3, 8
	str	w3, [x1,2388]
.L265:
	add	x19, x19, :lo12:.LANCHOR0
	str	w2, [x0]
	ldr	x0, [x19,136]
	str	wzr, [x0,336]
	mov	w0, 40
	bl	NandcTimeCfg
	ldr	x0, [x19,136]
	mov	w1, 8322
	str	w1, [x0,344]
	mov	w1, 6145
	movk	w1, 0x18, lsl 16
	str	w1, [x0,304]
	mov	w0, 36864
	bl	ftl_malloc
	str	wzr, [x19,2448]
	str	x0, [x19,2400]
	str	x0, [x19,2408]
	add	x0, x0, 32768
	str	wzr, [x19,2456]
	str	x0, [x19,2416]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	NandcInit, .-NandcInit
	.align	2
	.global	NandcGetTimeCfg
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	ldr	x5, [x4,136]
	ldr	w5, [x5,4]
	str	w5, [x0]
	ldr	x0, [x4,136]
	ldr	w0, [x0]
	str	w0, [x1]
	ldr	x0, [x4,136]
	ldr	w0, [x0,304]
	str	w0, [x2]
	ldr	x1, [x4,136]
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
	adrp	x1, .LANCHOR0
	uxtb	w0, w0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w3, 1
	cmp	w0, 16
	ldr	x2, [x1,136]
	str	w0, [x1,2460]
	mov	w1, 4096
	str	w3, [x2,8]
	bne	.L268
.L271:
	and	w1, w1, -17
	b	.L269
.L268:
	cmp	w0, 24
	bne	.L270
	orr	w1, w1, 16
	b	.L269
.L270:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L271
.L269:
	orr	w1, w1, 1
	str	w1, [x2,12]
	ret
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0+2464
	uxtb	w0, w0
	add	x29, sp, 0
	strb	w0, [x1,#:lo12:.LANCHOR0+2464]
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
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x0, x20, :lo12:.LANCHOR0
	mov	x21, 0
	add	x19, x0, 2132
	ldr	x1, [x0,136]
	ldr	w2, [x0,144]
	str	w2, [x1]
	ldr	w2, [x0,148]
	ldr	x1, [x0,136]
	str	w2, [x1,4]
	ldr	w2, [x0,152]
	ldr	x1, [x0,136]
	str	w2, [x1,8]
	ldr	w2, [x0,156]
	str	w2, [x1,12]
	ldr	w2, [x0,160]
	str	w2, [x1,304]
	ldr	w2, [x0,164]
	str	w2, [x1,308]
	ldr	w2, [x0,168]
	str	w2, [x1,336]
	ldr	w2, [x0,172]
	str	w2, [x1,344]
.L278:
	lsl	x0, x21, 3
	ldrb	w0, [x0,x19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L277
	mov	w0, w21
	bl	FlashReset
.L277:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L278
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,2380]
	cbz	w0, .L279
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,2357]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,2357]
	bl	NandcSetMode
	ldr	w0, [x19,160]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L279:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,88]
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
	.global	rk_nandc_flash_ready
	.type	rk_nandc_flash_ready, %function
rk_nandc_flash_ready:
	ldr	w1, [x0,368]
	orr	w1, w1, 2
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	and	w1, w1, -3
	str	w1, [x0,364]
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
	ldr	w1, [x0,368]
	orr	w1, w1, 1
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	and	w1, w1, -2
	str	w1, [x0,364]
	ret
	.size	rk_nandc_flash_xfer_completed, .-rk_nandc_flash_xfer_completed
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
.L290:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L290
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	uxtb	w2, w2
	stp	x23, x24, [sp,48]
	uxtb	w23, w1
	add	x1, x19, :lo12:.LANCHOR0
	add	x6, x1, 8
	ubfiz	w20, w23, 1, 1
	stp	x21, x22, [sp,32]
	add	x7, x6, x0
	orr	w20, w20, 8
	ldr	x21, [x6,x0]
	mov	w6, 16
	ldrb	w0, [x7,8]
	ldr	w22, [x21,12]
	bfi	w22, w6, 8, 8
	and	w22, w22, -9
	bfi	w22, w0, 5, 3
	mov	w0, 1
	bfi	w20, w0, 5, 2
	ldr	w0, [x1,2388]
	orr	w20, w20, 536870912
	cmp	w0, 3
	orr	w20, w20, 1024
	bfi	w20, w3, 4, 1
	bls	.L294
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w9, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w9, .L308
	cbz	x4, .L295
.L308:
	cbnz	w23, .L297
.L305:
	add	w2, w2, 1
	mov	x0, x4
	asr	w2, w2, 1
	bfi	w20, w2, 22, 6
	cbnz	x4, .L299
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2408]
	b	.L299
.L297:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w7, 128
	mov	w3, 0
	lsr	w10, w2, 1
	mov	w6, w3
	mov	w11, -1
	ldr	w0, [x1,2460]
	cmp	w0, 25
	mov	w0, 64
	csel	w7, w0, w7, cc
.L301:
	cmp	w6, w10
	bcs	.L305
	lsr	w0, w3, 2
	ubfiz	x0, x0, 2, 30
	cbz	w9, .L302
	ldrh	w8, [x5,2]
	ldr	x12, [x1,2416]
	ldrh	w13, [x5],4
	orr	w8, w13, w8, lsl 16
	str	w8, [x12,x0]
	b	.L303
.L302:
	ldr	x8, [x1,2416]
	str	w11, [x8,x0]
.L303:
	add	w6, w6, 1
	add	w3, w3, w7
	b	.L301
.L299:
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x24, x20, 22, 5
	mov	w2, w23
	str	x4, [x29,72]
	ldr	x1, [x19,2416]
	str	x1, [x19,2432]
	lsl	w1, w24, 10
	str	x0, [x19,2424]
	bl	rknand_dma_map_single
	str	w0, [x19,2440]
	ldr	x0, [x19,2432]
	lsl	w1, w24, 7
	mov	w2, w23
	bl	rknand_dma_map_single
	str	w0, [x19,2444]
	mov	w0, 1
	str	w0, [x19,2448]
	ldr	w0, [x19,2440]
	mov	w1, 16
	str	w0, [x21,20]
	ldr	x4, [x29,72]
	ldr	w0, [x19,2444]
	str	w0, [x21,24]
	and	x4, x4, 3
	str	wzr, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 9, 5
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 448
	str	w0, [x29,88]
	cbnz	x4, .L306
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L306:
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
.L295:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L294:
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
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	mov	w2, 1
	mov	w1, 0
.L313:
	cmp	w2, w0
	bhi	.L315
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L313
.L315:
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
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w2, [x1,2472]
	ldrh	w3, [x1,2482]
	str	w0, [x1,2468]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,2480]
	ldr	w0, [x1,2488]
	str	w2, [x1,2476]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,2484]
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
	adrp	x20, .LANCHOR0
	mov	x23, x0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w4, [x23,14]
	ldrh	w6, [x0,8]
	ldrh	w2, [x0,10]
	ldrh	w0, [x0,12]
	strh	w6, [x1,2492]
	strh	w2, [x1,2494]
	strh	w0, [x1,2496]
	strh	w4, [x1,2482]
.L320:
	add	x5, x1, 2504
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L320
	ldrh	w3, [x23,20]
	ldrb	w1, [x23,15]
	cmp	w3, w1
	bcs	.L321
	uxtb	w8, w0
	mov	w3, 0
	mul	w11, w0, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x20, :lo12:.LANCHOR0
.L322:
	cmp	w3, w0
	bcs	.L324
	uxtb	w1, w3
	mov	w5, w3
	mov	w7, 0
.L325:
	cmp	w7, w2
	bcs	.L347
	add	x9, x12, 2504
	add	w13, w5, w11
	add	w14, w1, w8
	add	w7, w7, 1
	strb	w1, [x9,w5,uxtw]
	add	w1, w1, w10
	strb	w14, [x9,x13]
	add	w5, w5, w0
	uxtb	w1, w1
	b	.L325
.L347:
	add	w3, w3, 1
	b	.L322
.L324:
	add	x1, x20, :lo12:.LANCHOR0
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x1,2494]
	strh	w4, [x1,2482]
.L321:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x1,2536]
	strh	wzr, [x1,2538]
	bne	.L326
	strh	w6, [x1,2536]
.L326:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w2, 4352
	ldrb	w26, [x1,72]
	strh	w2, [x1,2540]
	cbz	w26, .L327
	mov	w2, 384
	strh	w2, [x1,2540]
.L327:
	add	x21, x20, :lo12:.LANCHOR0
	ldrh	w25, [x23,16]
	ldrh	w24, [x23,20]
	ldrh	w27, [x23,18]
	ldrh	w22, [x21,2494]
	ldrh	w19, [x21,2482]
	strh	w25, [x21,2544]
	mul	w22, w0, w22
	mul	w0, w19, w0
	strh	w0, [x21,2542]
	uxth	w22, w22
	strh	w27, [x21,2546]
	strh	w22, [x21,2472]
	mul	w0, w22, w25
	strh	w0, [x21,2548]
	mov	w0, w24
	strh	w24, [x21,2550]
	bl	Ftl_log2
	uxth	w3, w0
	lsl	w2, w24, 9
	strh	w0, [x21,2552]
	ldrh	w0, [x23,26]
	cmp	w19, 1024
	strh	w2, [x21,2554]
	ubfx	x2, x2, 9, 7
	strh	w0, [x21,2558]
	lsl	w2, w2, 1
	mul	w0, w22, w19
	strh	w2, [x21,2556]
	str	w0, [x21,2488]
	bls	.L328
	and	w0, w19, 255
	strh	w0, [x21,2538]
.L328:
	add	x2, x20, :lo12:.LANCHOR0
	mul	w0, w24, w27
	ldrh	w1, [x2,2538]
	sub	w1, w19, w1
	mul	w1, w22, w1
	mul	w1, w1, w24
	mul	w1, w1, w25
	asr	w1, w1, 11
	str	w1, [x2,2560]
	ldrh	w1, [x2,2540]
	lsl	w1, w1, 3
	sdiv	w0, w1, w0
	uxth	w0, w0
	cmp	w0, 4
	bhi	.L344
	mov	w0, 4
.L344:
	strh	w0, [x2,2564]
	cbz	w26, .L331
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 640
	strh	w1, [x0,2540]
.L331:
	add	x1, x20, :lo12:.LANCHOR0
	lsl	w19, w19, 6
	cmp	w22, 1
	ldrh	w0, [x1,2540]
	asr	w0, w0, w3
	add	w3, w3, 9
	asr	w19, w19, w3
	strh	w19, [x1,2568]
	and	w19, w19, 65535
	add	w0, w0, 2
	strh	w0, [x1,2566]
	mul	w0, w19, w22
	str	w0, [x1,2572]
	add	w19, w19, 8
	ldrh	w0, [x1,2564]
	udiv	w0, w0, w22
	add	w19, w19, w0
	bne	.L345
	add	w19, w19, 4
.L345:
	str	w19, [x1,2468]
	add	x19, x20, :lo12:.LANCHOR0
	ldrh	w0, [x19,2468]
	bl	FtlSysBlkNumInit
	str	wzr, [x19,2584]
	ldr	w0, [x19,2468]
	str	w0, [x19,2576]
	ldr	w0, [x19,2484]
	ldrh	w1, [x19,2544]
	lsl	w0, w0, 2
	ldrh	w3, [x19,2550]
	ldrh	w2, [x19,2564]
	mul	w1, w0, w1
	ldrh	w0, [x19,2552]
	ldrb	w5, [x19,204]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	strh	w1, [x19,2582]
	uxth	w0, w0
	strh	w0, [x19,2580]
	add	w1, w2, 3
	strh	w1, [x19,2564]
	ldr	w1, [x19,2572]
	add	w4, w1, 3
	str	w4, [x19,2572]
	cbz	w5, .L334
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x19,2564]
	b	.L346
.L334:
	cmp	w4, 7
	bhi	.L335
	mov	w1, 8
.L346:
	str	w1, [x19,2572]
.L335:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,2480]
	strh	wzr, [x20,2588]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L336
	mov	w0, 1
	strh	w0, [x20,2588]
.L336:
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
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	adrp	x1, .LANCHOR0
	uxth	w4, w0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w0, 0
	ldrh	w2, [x1,2590]
	cbz	w2, .L349
	ldrh	w2, [x1,2564]
	ldr	x3, [x1,2592]
	mov	x1, 0
.L350:
	cmp	w2, w1, uxth
	bls	.L355
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L350
	mov	w0, 1
	b	.L349
.L355:
	mov	w0, 0
.L349:
	ret
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheMetchLpa
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	adrp	x2, .LANCHOR0
	mov	w6, w0
	add	x2, x2, :lo12:.LANCHOR0
	mov	w0, 0
	ldr	w3, [x2,2600]
	cbz	w3, .L357
	ldr	x5, [x2,2608]
	mov	x0, 56
	mov	x2, 0
.L358:
	cmp	w3, w2
	bls	.L367
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L358
	cmp	w4, w1
	bhi	.L358
	mov	w0, 1
	b	.L357
.L367:
	mov	w0, 0
.L357:
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR0+2584
	ldr	w0, [x0,#:lo12:.LANCHOR0+2584]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	FtlGetCapacity
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	adrp	x0, .LANCHOR0+2584
	ldr	w0, [x0,#:lo12:.LANCHOR0+2584]
	ret
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	adrp	x0, .LANCHOR0+2584
	ldr	w0, [x0,#:lo12:.LANCHOR0+2584]
	ret
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR0+2616
	ldr	w0, [x0,#:lo12:.LANCHOR0+2616]
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
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 2624
	ldrh	w3, [x19,-82]
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
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
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
	adrp	x2, .LANCHOR0
	uxth	w0, w0
	add	x3, x2, :lo12:.LANCHOR0
	ldrh	w1, [x3,2542]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	ldr	x1, [x2,2656]
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
	.global	FtlBbtCalcTotleCnt
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	w19, 0
	mov	w20, w19
	ldrh	w1, [x0,2542]
	ldrh	w21, [x0,2494]
	mul	w21, w1, w21
.L376:
	cmp	w20, w21
	bge	.L382
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L377
	add	w19, w19, 1
	uxth	w19, w19
.L377:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L376
.L382:
	mov	w0, w19
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.type	V2P_block, %function
V2P_block:
	adrp	x3, .LANCHOR0
	uxth	w0, w0
	add	x3, x3, :lo12:.LANCHOR0
	uxth	w1, w1
	ldrh	w4, [x3,2496]
	udiv	w2, w0, w4
	msub	w0, w2, w4, w0
	madd	w0, w1, w4, w0
	ldrh	w1, [x3,2542]
	madd	w0, w2, w1, w0
	ret
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.type	P2V_plane, %function
P2V_plane:
	adrp	x2, .LANCHOR0
	uxth	w0, w0
	add	x2, x2, :lo12:.LANCHOR0
	ldrh	w1, [x2,2496]
	ldrh	w3, [x2,2542]
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
	adrp	x2, .LANCHOR0
	uxth	w0, w0
	add	x2, x2, :lo12:.LANCHOR0
	ldrh	w3, [x2,2542]
	udiv	w1, w0, w3
	msub	w0, w1, w3, w0
	ldrh	w1, [x2,2496]
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
	bls	.L387
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L388
.L387:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L388:
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	adrp	x0, .LANCHOR0+2726
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2726]
	cmp	w0, wzr
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR0+2726
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2726]
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
	bcc	.L391
	adrp	x2, .LANCHOR0
	add	x0, x2, :lo12:.LANCHOR0
	mov	x19, x2
	ldrh	w0, [x0,2726]
	cmp	w0, 1024
	beq	.L391
	cbz	w1, .L393
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w0, [x20,424]
	cbnz	w0, .L393
	mov	w0, w21
	bl	P2V_block_in_plane
	uxth	w22, w0
	ldr	x0, [x20,432]
	lsl	w1, w21, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,432]
	bl	FlashEraseBlocks
	ubfiz	x0, x22, 1, 16
	ldr	x2, [x20,440]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x20,448]
	add	w0, w0, 1
	str	w0, [x20,448]
.L393:
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 2720
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w21, [x0,8]
	strh	w2, [x1,4]
.L391:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	adrp	x0, .LANCHOR2+484
	ldrh	w6, [x0,#:lo12:.LANCHOR2+484]
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x1, x1, 2720
	ldrh	w2, [x1,6]
	cbz	w2, .L400
	mov	w5, 0
	ldrh	w3, [x1,2]
	ldrh	w2, [x1,4]
	mov	w4, w5
	and	w6, w6, 31
.L402:
	cmp	w4, w6
	bge	.L410
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
	b	.L402
.L410:
	cbz	w5, .L400
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0,2722]
	strh	w2, [x0,2724]
.L400:
	ret
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp,48]
	add	x21, x21, :lo12:.LANCHOR0
	adrp	x23, .LC3
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	add	x22, x21, 2720
	add	x23, x23, :lo12:.LC3
	mov	x24, x21
.L412:
	ldrh	w1, [x22,6]
	cbz	w1, .L413
	adrp	x19, .LANCHOR2
	ldrh	w2, [x22,2]
	add	x25, x19, :lo12:.LANCHOR2
	sub	w1, w1, #1
	add	x0, x22, x2, sxtw 1
	strh	w1, [x22,6]
	add	w2, w2, 1
	ldrh	w20, [x0,8]
	ldr	w1, [x25,424]
	and	w2, w2, 1023
	strh	w2, [x22,2]
	cbnz	w1, .L414
	mov	w0, w20
	str	x1, [x29,88]
	bl	P2V_block_in_plane
	uxth	w26, w0
	ldr	x0, [x25,432]
	lsl	w2, w20, 10
	ldr	x1, [x29,88]
	str	w2, [x0,4]
	ldrb	w0, [x21,204]
	cbz	w0, .L415
	ldr	x0, [x25,432]
	mov	w2, 1
	bl	FlashEraseBlocks
.L415:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x19,432]
	bl	FlashEraseBlocks
	ubfiz	x0, x26, 1, 16
	ldr	x2, [x19,440]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x19,448]
	add	w0, w0, 1
	str	w0, [x19,448]
	b	.L414
.L413:
	adrp	x0, .LC2
	add	x0, x0, :lo12:.LC2
	bl	printk
.L416:
	b	.L416
.L414:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L417
	ldrh	w2, [x24,2726]
	mov	x0, x23
	mov	w1, w20
	bl	printk
	b	.L412
.L417:
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
	adrp	x0, .LANCHOR2+504
	mov	x4, -6148914691236517206
	uxth	w1, w1
	mov	w5, 65535
	ldr	x3, [x0,#:lo12:.LANCHOR2+504]
	sub	x0, x2, x3
	asr	x0, x0, 1
	madd	x0, x4, x0, x0
	mov	w4, 6
	uxth	w0, w0
.L423:
	cmp	w1, w0
	beq	.L424
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L425
	umull	x2, w0, w4
	add	x2, x3, x2
	b	.L423
.L424:
	mov	w0, 1
	b	.L422
.L425:
	mov	w0, 0
.L422:
	ret
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x9, .LANCHOR0
	uxth	w0, w0
	add	x9, x9, :lo12:.LANCHOR0
	ldrh	w1, [x9,2480]
	cmp	w1, w0
	bls	.L428
	adrp	x2, .LANCHOR2
	mov	w7, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x10, w0, w7
	mov	x3, x2
	ldr	x11, [x5,504]
	add	x4, x11, x10
	strh	w1, [x4,2]
	strh	w1, [x11,x10]
	ldr	x1, [x5,512]
	cbnz	x1, .L429
	str	x4, [x5,512]
	b	.L428
.L429:
	ubfiz	x8, x0, 1, 16
	ldr	x13, [x5,520]
	ldrh	w2, [x4,4]
	mov	x14, -6148914691236517206
	ldr	x12, [x5,504]
	mov	w18, 65535
	ldrh	w6, [x13,x8]
	cmp	w2, wzr
	ldrh	w17, [x9,2480]
	mul	w6, w6, w2
	sub	x2, x1, x12
	asr	x2, x2, 1
	csinv	w6, w6, wzr, ne
	madd	x2, x14, x2, x2
	ldr	x14, [x5,440]
	mov	w5, 0
	add	x9, x14, x8
	uxth	w2, w2
	mov	w8, w7
.L437:
	add	w5, w5, 1
	cmp	w0, w2
	uxth	w5, w5
	beq	.L428
	cmp	w5, w17
	bhi	.L428
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w7, [x13,x15]
	mul	w7, w7, w16
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L433
	ldrh	w15, [x14,x15]
	ldrh	w7, [x9]
	cmp	w15, w7
	bcc	.L435
	b	.L434
.L433:
	bhi	.L434
.L435:
	ldrh	w7, [x1]
	cmp	w7, w18
	bne	.L436
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x4, [x2,528]
	b	.L428
.L436:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x12, x1
	b	.L437
.L434:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,512]
	cmp	x1, x3
	bne	.L438
	strh	w0, [x1,2]
	str	x4, [x2,512]
	b	.L428
.L438:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,504]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L428:
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
	ldrh	w1, [x0,536]
	add	w1, w1, 1
	strh	w1, [x0,536]
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
	beq	.L443
	adrp	x2, .LANCHOR2
	mov	w6, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x8, w0, w6
	mov	x3, x2
	ldr	x9, [x5,504]
	add	x4, x9, x8
	strh	w1, [x4,2]
	strh	w1, [x9,x8]
	ldr	x1, [x5,544]
	cbnz	x1, .L444
	str	x4, [x5,544]
	b	.L443
.L444:
	ldr	x11, [x5,440]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,504]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L447:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L445
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L446
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L443
.L446:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L447
.L445:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,544]
	cmp	x1, x3
	bne	.L448
	strh	w0, [x1,2]
	str	x4, [x2,544]
	b	.L443
.L448:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,504]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L443:
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
	ldrh	w1, [x0,552]
	add	w1, w1, 1
	strh	w1, [x0,552]
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
	ldr	x2, [x6,504]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L451
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L452
	str	xzr, [x0]
	b	.L453
.L452:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L453
.L451:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L454
	cmp	w3, w0
	beq	.L453
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L453
.L454:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,504]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L453:
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
	cbz	x2, .L456
	adrp	x3, .LANCHOR2+504
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2+504]
.L457:
	cbnz	w1, .L458
.L460:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L456
.L458:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L460
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L457
.L456:
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
	ldr	x1, [x2,512]
	cbz	x1, .L468
	ldr	x4, [x2,504]
	mov	w5, 6
.L464:
	cbz	w3, .L465
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L468
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L464
.L465:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L468:
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
	ldrh	w1, [x0,560]
	cmp	w1, w20
	beq	.L470
	ldrh	w1, [x0,608]
	cmp	w1, w20
	beq	.L470
	ldrh	w1, [x0,656]
	cmp	w1, w20
	beq	.L470
	mov	w3, 6
	ldr	x4, [x0,504]
	ldr	x2, [x0,512]
	umull	x3, w20, w3
	add	x1, x4, x3
	cmp	x1, x2
	beq	.L470
	ldr	x5, [x0,520]
	ubfiz	x0, x20, 1, 16
	ldrh	w2, [x5,x0]
	ldrh	w0, [x1,4]
	ldrh	w1, [x1,2]
	cmp	w0, wzr
	mul	w2, w2, w0
	mov	w0, 65535
	csinv	w2, w2, wzr, ne
	cmp	w1, w0
	bne	.L472
	ldrh	w0, [x4,x3]
	cmp	w0, w1
	beq	.L470
.L472:
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
	bcs	.L470
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, w20
	add	x0, x19, 512
	bl	List_remove_node
	ldrh	w0, [x19,536]
	sub	w0, w0, #1
	strh	w0, [x19,536]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L470:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	List_update_data_list, .-List_update_data_list
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
.L477:
	cmp	w20, w2
	beq	.L481
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L478
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	uxth	w1, w0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L479
	adrp	x2, .LANCHOR0+2726
	adrp	x0, .LC4
	add	x0, x0, :lo12:.LC4
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2726]
	bl	printk
.L480:
	b	.L480
.L479:
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L481
.L478:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L477
.L481:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	adrp	x0, .LANCHOR0+2582
	mov	x4, 0
	mov	w5, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR0+2582]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	mov	x3, x0
	ldr	x1, [x1,704]
.L484:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L500
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L484
	b	.L485
.L500:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L487:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L501
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L496
	cmp	w5, w7
	bcc	.L488
.L496:
	mov	w5, w7
	mov	w6, w0
.L488:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L487
.L501:
	cmp	w0, w2
	bcc	.L485
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,712]
	mov	w0, w2
.L491:
	cmp	w3, w2
	beq	.L485
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L492
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L492:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L491
.L485:
	ret
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3,714]
	cbnz	w0, .L503
	cmp	w2, 4
	bhi	.L503
	add	w2, w2, 1
	strh	w2, [x3,714]
	b	.L502
.L503:
	adrp	x2, .LANCHOR0+2480
	add	x0, x1, :lo12:.LANCHOR2
	mov	w7, 65535
	ldrh	w5, [x2,#:lo12:.LANCHOR0+2480]
	strh	wzr, [x0,714]
	str	wzr, [x0,716]
	ldr	x6, [x0,520]
	mov	x0, 0
.L504:
	cmp	w5, w0, uxth
	bls	.L502
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L505
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,716]
	add	w2, w4, w2
	str	w2, [x3,716]
.L505:
	add	x0, x0, 1
	b	.L504
.L502:
	ret
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	stp	x29, x30, [sp, -16]!
	uxth	w0, w0
	add	x29, sp, 0
	cbz	w1, .L509
	bl	ftl_set_blk_mode.part.9
	b	.L508
.L509:
	adrp	x1, .LANCHOR0
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L508:
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	adrp	x1, .LANCHOR0
	uxth	w0, w0
	ubfx	x2, x0, 5, 11
	ldr	x1, [x1,#:lo12:.LANCHOR0]
	ldr	w1, [x1,x2,lsl 2]
	lsr	w0, w1, w0
	and	w0, w0, 1
	ret
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	adrp	x3, .LANCHOR0
	uxth	w1, w1
	add	x4, x3, :lo12:.LANCHOR0
	uxth	w2, w2
	strh	wzr, [x0,4]
	mov	w6, 65535
	ldrh	w4, [x4,2472]
.L513:
	cmp	w2, w4
	bcs	.L519
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L514
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L514:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L513
.L519:
	add	x3, x3, :lo12:.LANCHOR0
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,2544]
	mov	x3, x0
.L516:
	cmp	x3, x4
	beq	.L520
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L517
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L517:
	add	x3, x3, 2
	b	.L516
.L520:
	ret
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
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
	adrp	x20, .LANCHOR0
	mov	x19, x0
	strh	wzr, [x0,4]
	add	x22, x19, 16
	strb	wzr, [x0,7]
	add	x0, x20, :lo12:.LANCHOR0
	mov	x21, 0
	mov	x23, x0
	mov	w25, -1
	ldrh	w24, [x0,2472]
.L522:
	cmp	w24, w21, uxth
	bls	.L533
	add	x0, x23, 2504
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L523
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L523:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L522
.L533:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w2, [x1,2544]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldr	w0, [x1,2372]
	cbz	w0, .L525
	adrp	x0, .LANCHOR2+440
	ldrh	w1, [x19]
	ldr	x0, [x0,#:lo12:.LANCHOR2+440]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 79
	bhi	.L525
	mov	w0, 1
	strb	w0, [x19,9]
.L525:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,72]
	cbz	w0, .L526
	mov	w0, 1
	strb	w0, [x19,9]
.L526:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
	stp	x29, x30, [sp, -64]!
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x22, 0
	uxth	w21, w0
	ldrh	w23, [x1,2472]
	mov	w19, w22
	ldrh	w24, [x1,2544]
	mov	x20, x1
.L535:
	cmp	w23, w22, uxth
	bls	.L542
	add	x0, x20, 2504
	mov	w1, w21
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L536
	add	w19, w19, w24
	uxth	w19, w19
.L536:
	add	x22, x22, 1
	b	.L535
.L542:
	cbz	w19, .L538
	mov	w0, 32768
	sdiv	w19, w0, w19
.L538:
	mov	w0, 6
	umull	x21, w21, w0
	adrp	x0, .LANCHOR2+504
	ldr	x0, [x0,#:lo12:.LANCHOR2+504]
	add	x21, x0, x21
	mov	w0, 0
	strh	w19, [x21,4]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x1, [x2,544]
	mov	w0, w1
	cbz	x1, .L544
	ldr	x0, [x2,504]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,440]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L544:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR2
	uxth	w4, w0
	add	x5, x2, :lo12:.LANCHOR2
	ldr	x1, [x5,544]
	mov	w0, w1
	cbz	x1, .L547
	ldrh	w3, [x5,552]
	mov	w0, 7
	mov	w6, 6
	mov	w7, 65535
	mul	w3, w3, w0
	ldr	x0, [x5,504]
	asr	w3, w3, 3
	cmp	w4, w3
	sub	x1, x1, x0
	csel	w4, w3, w4, gt
	asr	x1, x1, 1
	mov	x3, -6148914691236517206
	madd	x1, x3, x1, x1
	mov	w3, 0
	uxth	w1, w1
.L549:
	cmp	w3, w4
	beq	.L552
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L550
.L552:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,440]
	ldrh	w0, [x0,x1]
	b	.L547
.L550:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L549
.L547:
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC5
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR0
	add	x1, x1, :lo12:.LC5
	add	x24, x24, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	x22, x0
	bl	strcpy
	add	x20, x22, 12
	ldr	w2, [x24,176]
	adrp	x1, .LC6
	mov	x0, x20
	add	x1, x1, :lo12:.LC6
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC7
	ldr	w2, [x24,2560]
	mov	x0, x20
	add	x1, x1, :lo12:.LC7
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0,#:lo12:.LANCHOR1+504]
	cmp	w0, 1
	beq	.L555
	sub	w0, w20, w22
	b	.L556
.L555:
	add	x0, x29, 96
	add	x1, x29, 100
	add	x2, x29, 104
	add	x3, x29, 108
	adrp	x25, .LANCHOR2
	bl	NandcGetTimeCfg
	add	x23, x25, :lo12:.LANCHOR2
	ldr	w4, [x29,104]
	adrp	x1, .LC8
	ldr	w5, [x29,108]
	add	x1, x1, :lo12:.LC8
	ldr	w3, [x29,100]
	mov	x0, x20
	ldr	w2, [x29,96]
	bl	sprintf
	add	x21, x20, x0, sxtw
	adrp	x1, .LC9
	mov	x0, x21
	add	x1, x1, :lo12:.LC9
	add	x21, x21, 10
	bl	strcpy
	ldr	w2, [x24,2616]
	adrp	x1, .LC10
	mov	x0, x21
	add	x1, x1, :lo12:.LC10
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,716]
	adrp	x1, .LC11
	mov	x0, x21
	add	x1, x1, :lo12:.LC11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,720]
	adrp	x1, .LC12
	mov	x0, x21
	add	x1, x1, :lo12:.LC12
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,724]
	adrp	x1, .LC13
	mov	x0, x21
	add	x1, x1, :lo12:.LC13
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,728]
	adrp	x1, .LC14
	mov	x0, x21
	add	x1, x1, :lo12:.LC14
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,732]
	adrp	x1, .LC15
	mov	x0, x21
	add	x1, x1, :lo12:.LC15
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,736]
	adrp	x1, .LC16
	mov	x0, x21
	add	x1, x1, :lo12:.LC16
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,740]
	adrp	x1, .LC17
	mov	x0, x21
	add	x1, x1, :lo12:.LC17
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,744]
	adrp	x1, .LC18
	mov	x0, x21
	add	x1, x1, :lo12:.LC18
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,748]
	adrp	x1, .LC19
	mov	x0, x21
	add	x1, x1, :lo12:.LC19
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,752]
	adrp	x1, .LC20
	mov	x0, x21
	add	x1, x1, :lo12:.LC20
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,756]
	adrp	x1, .LC21
	add	x1, x1, :lo12:.LC21
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x24,2630]
	adrp	x1, .LC22
	mov	x0, x21
	add	x1, x1, :lo12:.LC22
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,552]
	adrp	x1, .LC23
	mov	x0, x21
	add	x1, x1, :lo12:.LC23
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,760]
	adrp	x1, .LC24
	mov	x0, x21
	add	x1, x1, :lo12:.LC24
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,764]
	adrp	x1, .LC25
	mov	x0, x21
	add	x1, x1, :lo12:.LC25
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,768]
	adrp	x1, .LC26
	mov	x0, x21
	add	x1, x1, :lo12:.LC26
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,448]
	adrp	x1, .LC27
	mov	x0, x21
	add	x1, x1, :lo12:.LC27
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,772]
	adrp	x1, .LC28
	mov	x0, x21
	add	x1, x1, :lo12:.LC28
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,776]
	adrp	x1, .LC29
	mov	x0, x21
	add	x1, x1, :lo12:.LC29
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,486]
	adrp	x1, .LC30
	mov	x0, x21
	add	x1, x1, :lo12:.LC30
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,484]
	adrp	x1, .LC31
	mov	x0, x21
	add	x1, x1, :lo12:.LC31
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2584]
	adrp	x1, .LC32
	mov	x0, x21
	add	x1, x1, :lo12:.LC32
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2576]
	adrp	x1, .LC33
	mov	x0, x21
	add	x1, x1, :lo12:.LC33
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2468]
	adrp	x1, .LC34
	mov	x0, x21
	add	x1, x1, :lo12:.LC34
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,2726]
	adrp	x1, .LC35
	mov	x0, x21
	add	x1, x1, :lo12:.LC35
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,2480]
	adrp	x1, .LC36
	mov	x0, x21
	add	x1, x1, :lo12:.LC36
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,780]
	adrp	x1, .LC37
	mov	x0, x21
	add	x1, x1, :lo12:.LC37
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2484]
	adrp	x1, .LC38
	mov	x0, x21
	add	x1, x1, :lo12:.LC38
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,784]
	adrp	x1, .LC39
	mov	x0, x21
	add	x1, x1, :lo12:.LC39
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,2624]
	adrp	x1, .LC40
	mov	x0, x21
	add	x1, x1, :lo12:.LC40
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,562]
	adrp	x1, .LC41
	mov	x0, x21
	add	x1, x1, :lo12:.LC41
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,566]
	adrp	x1, .LC42
	mov	x0, x21
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,560]
	adrp	x1, .LC43
	mov	x0, x21
	add	x1, x1, :lo12:.LC43
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,568]
	adrp	x1, .LC44
	mov	x0, x21
	add	x1, x1, :lo12:.LC44
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,564]
	adrp	x1, .LC45
	mov	x0, x21
	add	x1, x1, :lo12:.LC45
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x23,560]
	adrp	x1, .LC46
	ldr	x2, [x23,520]
	mov	x0, x21
	add	x1, x1, :lo12:.LC46
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,610]
	adrp	x1, .LC47
	mov	x0, x21
	add	x1, x1, :lo12:.LC47
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,614]
	adrp	x1, .LC48
	mov	x0, x21
	add	x1, x1, :lo12:.LC48
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,608]
	adrp	x1, .LC49
	mov	x0, x21
	add	x1, x1, :lo12:.LC49
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,616]
	adrp	x1, .LC50
	mov	x0, x21
	add	x1, x1, :lo12:.LC50
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,612]
	adrp	x1, .LC51
	mov	x0, x21
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x23,608]
	adrp	x1, .LC52
	ldr	x2, [x23,520]
	mov	x0, x21
	add	x1, x1, :lo12:.LC52
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,658]
	adrp	x1, .LC53
	mov	x0, x21
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,662]
	adrp	x1, .LC54
	mov	x0, x21
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,656]
	adrp	x1, .LC55
	mov	x0, x21
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,664]
	adrp	x1, .LC56
	mov	x0, x21
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,660]
	adrp	x1, .LC57
	mov	x0, x21
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,802]
	adrp	x1, .LC58
	mov	x0, x21
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,806]
	adrp	x1, .LC59
	mov	x0, x21
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,800]
	adrp	x1, .LC60
	mov	x0, x21
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,808]
	adrp	x1, .LC61
	mov	x0, x21
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,804]
	adrp	x1, .LC62
	mov	x0, x21
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x23,848]
	adrp	x1, .LC63
	ldr	w2, [x24,2372]
	add	x1, x1, :lo12:.LC63
	ldr	w4, [x23,940]
	mov	x0, x21
	ldr	w5, [x23,932]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x23,936]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x23,928]
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x23,952]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,1368]
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,1370]
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x23,1372]
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,1376]
	adrp	x1, .LC69
	add	x1, x1, :lo12:.LC69
	mov	x0, x19
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	uxth	w2, w0
	adrp	x1, .LC70
	mov	x0, x19
	add	x1, x1, :lo12:.LC70
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x23,552]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x23,800]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L557
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x23,520]
	adrp	x1, .LC72
	mov	x0, x19
	add	x1, x1, :lo12:.LC72
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L557:
	mov	w0, 0
	adrp	x23, .LC73
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x23, x23, :lo12:.LC73
.L559:
	cmp	w3, w27
	beq	.L558
	add	x21, x25, :lo12:.LANCHOR2
	ubfiz	x6, x3, 1, 16
	umull	x24, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x5, [x21,504]
	mov	x1, x23
	ldr	x4, [x21,520]
	add	w20, w20, 1
	ldr	x7, [x21,440]
	add	x5, x5, x24
	ldrh	w4, [x4,x6]
	ldrh	w5, [x5,4]
	ldrh	w6, [x7,x6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,504]
	cmp	w20, 16
	ldrh	w3, [x0,x24]
	bne	.L559
.L558:
	add	x1, x25, :lo12:.LANCHOR2
	adrp	x23, .LC74
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
	add	x23, x23, :lo12:.LC74
	ldr	x0, [x1,544]
	ldr	x3, [x1,504]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L561:
	cmp	w3, w27
	beq	.L560
	add	x21, x25, :lo12:.LANCHOR2
	ubfiz	x5, x3, 1, 16
	umull	x24, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x4, [x21,504]
	mov	x1, x23
	ldr	x6, [x21,440]
	add	w20, w20, 1
	add	x4, x4, x24
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,504]
	cmp	w20, 4
	ldrh	w3, [x0,x24]
	bne	.L561
.L560:
	sub	w0, w19, w22
.L556:
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
	adrp	x1, .LC75
	adrp	x2, .LC76
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC75
	add	x2, x2, :lo12:.LC76
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
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w2, [x0,768]
	ldr	w1, [x0,776]
	cmp	w2, w1
	bcs	.L566
	adrp	x1, .LANCHOR0
	str	wzr, [x0,760]
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x5, [x0,440]
	mov	x0, 0
	ldrh	w3, [x2,2480]
.L567:
	cmp	w3, w0
	bls	.L599
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x0,lsl 1]
	add	x0, x0, 1
	ldr	w2, [x4,760]
	add	w2, w6, w2
	str	w2, [x4,760]
	b	.L567
.L599:
	add	x2, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x2,760]
	ldrh	w1, [x1,2536]
	udiv	w3, w0, w3
	str	w3, [x2,768]
	ldr	w3, [x2,764]
	sub	w0, w0, w3
	udiv	w0, w0, w1
	str	w0, [x2,760]
	b	.L569
.L566:
	ldr	w1, [x0,772]
	cmp	w2, w1
	bls	.L569
	add	w1, w1, 1
	adrp	x4, .LANCHOR0
	str	w1, [x0,772]
	mov	w0, 0
.L571:
	add	x1, x4, :lo12:.LANCHOR0
	ldrh	w1, [x1,2480]
	cmp	w0, w1
	bcs	.L569
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 1, 32
	add	w0, w0, 1
	ldr	x3, [x1,440]
	ldrh	w1, [x3,x2]
	add	w1, w1, 1
	strh	w1, [x3,x2]
	b	.L571
.L569:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w21, [x1,776]
	ldr	w24, [x1,768]
	add	w0, w21, 256
	cmp	w0, w24
	bls	.L574
	ldr	w1, [x1,772]
	add	w0, w21, 768
	cmp	w0, w1
	bls	.L574
	adrp	x0, .LANCHOR0+2372
	ldr	w0, [x0,#:lo12:.LANCHOR0+2372]
	cbz	w0, .L577
	cmp	w21, 40
	bls	.L574
.L577:
	mov	w0, 65535
	b	.L576
.L574:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,552]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L586
	cmp	w21, 40
	bhi	.L577
.L586:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x3, [x0,512]
	cbz	x3, .L577
	adrp	x1, .LANCHOR0+2480
	mov	w23, 65535
	ldr	x7, [x0,504]
	mov	w20, w23
	ldr	x26, [x0,440]
	mov	x0, -6148914691236517206
	ldrh	w9, [x1,#:lo12:.LANCHOR0+2480]
	mov	w8, w23
	mov	w1, 0
	add	x0, x0, 1
	mov	w10, 6
.L579:
	ldrh	w2, [x3]
	cmp	w2, w8
	beq	.L581
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L577
	ldrh	w4, [x3,4]
	cbz	w4, .L580
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w5, w4
	and	x4, x4, 65535
	ldrh	w3, [x26,x4,lsl 1]
	cmp	w3, w21
	bls	.L585
	cmp	w3, w23
	bcs	.L580
	mov	w23, w3
	mov	w20, w5
.L580:
	umull	x3, w2, w10
	add	x3, x7, x3
	b	.L579
.L585:
	mov	w20, w5
.L581:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L577
	ubfiz	x25, x20, 1, 16
	ldrh	w22, [x26,x25]
	cmp	w22, w21
	bls	.L583
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L583
	add	x0, x19, :lo12:.LANCHOR2
	str	w23, [x0,776]
.L583:
	cmp	w22, w24
	bcs	.L577
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L577
	add	w0, w22, 256
	cmp	w0, w24
	bcc	.L584
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,772]
	cmp	w22, w0
	bcs	.L577
.L584:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC77
	ldrh	w5, [x26,x25]
	add	x0, x0, :lo12:.LC77
	mov	w1, w20
	mov	w2, w24
	ldr	x4, [x19,520]
	ldr	w3, [x19,772]
	ldrh	w4, [x4,x25]
	bl	printk
	mov	w0, 1
	str	w0, [x19,1380]
	mov	w0, w20
.L576:
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
	beq	.L601
	adrp	x2, .LANCHOR2+520
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2+520]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L601:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	adrp	x3, .LANCHOR2
	mov	w1, 0
	add	x0, x3, :lo12:.LANCHOR2
	adrp	x10, .LANCHOR0
	mov	w11, 24
	mov	w12, 1
	mov	w6, 4
	mov	w13, 56
	str	wzr, [x0,1384]
.L603:
	add	x5, x10, :lo12:.LANCHOR0
	adrp	x9, .LANCHOR0
	ldrh	w0, [x5,2472]
	cmp	w1, w0
	bcs	.L607
	add	x2, x3, :lo12:.LANCHOR2
	umull	x4, w1, w11
	ldr	x7, [x2,1392]
	ldr	x8, [x2,1400]
	add	x0, x7, x4
	str	w12, [x0,16]
	ldrh	w0, [x5,2554]
	mul	w0, w1, w0
	sdiv	w0, w0, w6
	add	x0, x8, x0, sxtw 2
	str	x0, [x7,x4]
	ldrh	w0, [x5,2556]
	ldr	x8, [x2,1392]
	ldr	x5, [x2,1408]
	mul	w0, w1, w0
	add	x7, x8, x4
	ldr	x2, [x2,1416]
	sdiv	w0, w0, w6
	add	x0, x5, x0, sxtw 2
	str	x0, [x7,8]
	umull	x0, w1, w13
	add	w1, w1, 1
	add	x0, x2, x0
	ldr	x2, [x8,x4]
	uxth	w1, w1
	str	x2, [x0,8]
	ldr	x2, [x7,8]
	str	x2, [x0,16]
	b	.L603
.L607:
	mov	w8, 24
	mov	w5, 4
.L605:
	add	x2, x3, :lo12:.LANCHOR2
	ldr	w1, [x2,1424]
	cmp	w0, w1
	bcs	.L608
	umull	x4, w0, w8
	ldr	x7, [x2,1392]
	add	x6, x9, :lo12:.LANCHOR0
	ldr	x10, [x2,1400]
	add	x1, x7, x4
	str	wzr, [x1,16]
	ldrh	w1, [x6,2554]
	mul	w1, w0, w1
	sdiv	w1, w1, w5
	add	x1, x10, x1, sxtw 2
	str	x1, [x7,x4]
	ldrh	w1, [x6,2556]
	ldr	x6, [x2,1392]
	ldr	x2, [x2,1408]
	mul	w1, w0, w1
	add	x4, x6, x4
	add	w0, w0, 1
	sdiv	w1, w1, w5
	uxth	w0, w0
	add	x1, x2, x1, sxtw 2
	str	x1, [x4,8]
	b	.L605
.L608:
	ret
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	adrp	x2, .LANCHOR2
	mov	w3, 0
	add	x2, x2, :lo12:.LANCHOR2
	mov	w7, 56
	mov	w9, 24
	ldr	w8, [x2,1424]
	ldr	x5, [x2,1392]
.L610:
	cmp	w3, w1
	bcs	.L609
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L615:
	cmp	w2, w8
	bcs	.L612
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L611
	str	wzr, [x10,16]
	b	.L612
.L611:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L615
.L612:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L610
.L609:
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
	ldr	w5, [x2,1424]
	ldr	x6, [x2,1392]
	mov	w2, 0
.L617:
	cmp	w2, w1
	bcs	.L623
	mov	w3, 0
.L621:
	cmp	w3, w5
	bcs	.L619
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L618
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L619
.L618:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L621
.L619:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L617
.L623:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR2
	uxth	w0, w0
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w2, [x1,1428]
	ldr	x3, [x1,1432]
	mov	x1, 0
.L625:
	cmp	w2, w1, uxth
	bls	.L629
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L625
	mov	w0, 1
	b	.L626
.L629:
	mov	w0, 0
.L626:
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
	adrp	x3, .LANCHOR2
	ldr	x2, [x29,40]
	add	x4, x3, :lo12:.LANCHOR2
	ldr	x1, [x29,32]
	uxth	w8, w0
	mov	x5, 0
	ldrh	w7, [x4,1428]
	ldr	x6, [x4,1432]
.L631:
	uxth	w4, w5
	cmp	w4, w7
	bcs	.L635
	add	x5, x5, 1
	add	x9, x6, x5, lsl 1
	ldrh	w9, [x9,-2]
	cmp	w9, w8
	bne	.L631
.L635:
	cmp	w4, w7
	bne	.L633
	ubfiz	x4, x4, 1, 16
	strh	w0, [x6,x4]
	add	x0, x3, :lo12:.LANCHOR2
	ldrh	w4, [x0,1428]
	add	w4, w4, 1
	strh	w4, [x0,1428]
.L633:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w4, [x3,1440]
	ldr	x5, [x3,1448]
	umull	x4, w4, w0
	add	x5, x5, x4
	str	w1, [x5,4]
	ldr	x1, [x3,1448]
	add	x0, x1, x4
	str	w2, [x0,8]
	str	w19, [x1,x4]
	ldrh	w0, [x3,1440]
	add	w0, w0, 1
	strh	w0, [x3,1440]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
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
	ldrh	w0, [x19,1456]
	cmp	w0, w20
	beq	.L639
	ldrh	w0, [x19,1458]
	cmp	w0, w20
	beq	.L639
	ldrh	w0, [x19,1460]
	cmp	w0, w20
	beq	.L639
	ldrh	w0, [x19,1462]
	cmp	w0, w20
	beq	.L639
	adrp	x0, .LC78
	mov	w1, w20
	add	x0, x0, :lo12:.LC78
	bl	printk
	ldrh	w1, [x19,1456]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L641
	strh	w20, [x19,1456]
	b	.L639
.L641:
	ldrh	w1, [x19,1458]
	cmp	w1, w0
	bne	.L642
	strh	w20, [x19,1458]
	b	.L639
.L642:
	ldrh	w1, [x19,1460]
	cmp	w1, w0
	bne	.L643
	strh	w20, [x19,1460]
	b	.L639
.L643:
	ldrh	w1, [x19,1462]
	cmp	w1, w0
	bne	.L639
	strh	w20, [x19,1462]
.L639:
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
	ldrh	w0, [x19,1456]
	cmp	w0, w20
	beq	.L645
	ldrh	w0, [x19,1458]
	cmp	w0, w20
	beq	.L645
	ldrh	w0, [x19,1460]
	cmp	w0, w20
	beq	.L645
	ldrh	w0, [x19,1462]
	cmp	w0, w20
	beq	.L645
	adrp	x0, .LC78
	mov	w1, w20
	add	x0, x0, :lo12:.LC78
	bl	printk
	ldrh	w1, [x19,1456]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L646
	strh	w20, [x19,1456]
	b	.L645
.L646:
	ldrh	w1, [x19,1458]
	cmp	w1, w0
	bne	.L647
	strh	w20, [x19,1458]
	b	.L645
.L647:
	ldrh	w1, [x19,1460]
	cmp	w1, w0
	bne	.L648
	strh	w20, [x19,1460]
	b	.L645
.L648:
	ldrh	w1, [x19,1462]
	cmp	w1, w0
	bne	.L653
	strh	w20, [x19,1462]
	b	.L645
.L653:
	mov	w21, -1
.L645:
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
	ldrh	w1, [x22,1464]
	adrp	x0, .LC79
	mov	w2, w20
	add	x0, x0, :lo12:.LC79
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	adrp	x0, .LANCHOR0+2372
	ldr	w0, [x0,#:lo12:.LANCHOR0+2372]
	cbz	w0, .L655
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,440]
	ldrh	w1, [x2,x0]
	cmp	w1, 39
	bls	.L655
	sub	w1, w1, #40
	strh	w1, [x2,x0]
.L655:
	add	x2, x19, :lo12:.LANCHOR2
	mov	x0, 0
	add	x2, x2, 1472
	ldrh	w1, [x2,-8]
.L656:
	cmp	w1, w0, uxth
	bls	.L662
	add	x0, x0, 1
	add	x3, x2, x0, lsl 1
	ldrh	w3, [x3,-2]
	cmp	w3, w20
	bne	.L656
	b	.L657
.L662:
	cmp	w1, 15
	bhi	.L657
	add	x19, x19, :lo12:.LANCHOR2
	add	w0, w1, 1
	add	x19, x19, 1472
	strh	w0, [x19,-8]
	strh	w20, [x19,w1,sxtw 1]
.L657:
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
	ldrh	w1, [x0,1464]
	cbz	w1, .L664
	ldrh	w3, [x0,1456]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L664
	ldrh	w2, [x0,1506]
	cmp	w2, w1
	bcc	.L665
	strh	wzr, [x0,1506]
.L665:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x19, 1472
	ldrh	w1, [x19,1506]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,1506]
	add	w0, w0, 1
	strh	w0, [x19,1506]
.L664:
	mov	w0, 0
	ldr	x19, [sp,16]
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
	stp	x19, x20, [sp,16]
	mov	w20, w1
	cbnz	w0, .L671
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
.L672:
	add	x2, x1, 208
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L672
.L678:
	adrp	x19, .LANCHOR2
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR2
	mov	w2, 2048
	add	x19, x19, 1508
	uxth	w20, w20
	mov	x0, x19
	bl	ftl_memset
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
	b	.L673
.L671:
	cmp	w0, 1
	bne	.L674
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x3, :lo12:.LANCHOR0
.L677:
	cmp	x0, 3
	uxth	w2, w0
	mov	w1, w2
	bls	.L675
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L675:
	add	x2, x3, 208
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L677
	b	.L678
.L674:
	cmp	w0, 2
	bne	.L679
	adrp	x3, .LANCHOR0
	mov	w1, 65535
	mov	x0, 0
	add	x3, x3, :lo12:.LANCHOR0
.L681:
	add	x4, x3, 208
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	uxth	w1, w1
	bne	.L681
	b	.L678
.L679:
	cmp	w0, 3
	bne	.L682
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x3, :lo12:.LANCHOR0
.L685:
	cmp	x0, 5
	uxth	w2, w0
	mov	w1, w2
	bls	.L683
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L683:
	add	x2, x3, 208
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L685
	b	.L678
.L682:
	cmp	w0, 4
	bne	.L686
	adrp	x1, .LANCHOR0
	mov	w5, 7
	add	x1, x1, :lo12:.LANCHOR0
	mov	w4, 6
	add	x2, x1, 208
	strh	w0, [x2,8]
	mov	w0, 5
	strh	wzr, [x1,208]
	mov	w1, 1
	strh	w0, [x2,10]
	mov	w0, 7
	strh	w1, [x2,2]
	mov	w1, 2
	strh	w0, [x2,12]
	mov	w0, 8
	strh	w1, [x2,4]
	mov	w1, 3
	strh	w0, [x2,14]
	mov	w0, 8
	strh	w1, [x2,6]
.L688:
	and	w3, w0, 1
	ubfiz	w1, w0, 1, 15
	add	w0, w0, 1
	cmp	w3, wzr
	csel	w3, w5, w4, ne
	add	x2, x2, 2
	uxth	w0, w0
	sub	w1, w1, w3
	cmp	w0, 512
	strh	w1, [x2,14]
	bne	.L688
	b	.L678
.L686:
	cmp	w0, 5
	bne	.L689
	adrp	x2, .LANCHOR0
	mov	x0, 0
	add	x2, x2, :lo12:.LANCHOR0
.L690:
	add	x1, x2, 208
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L690
	mov	x0, 0
.L691:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 992
	strh	w3, [x2,32]
	bne	.L691
	b	.L678
.L689:
	cmp	w0, 6
	bne	.L692
	adrp	x1, .LANCHOR0
	mov	x2, 0
	mov	w4, 12
	mov	w5, 10
	add	x1, x1, :lo12:.LANCHOR0
.L695:
	cmp	x2, 5
	uxth	w3, w2
	mov	w0, w3
	bls	.L693
	add	w0, w3, w3, lsl 1
	and	w3, w3, 1
	cmp	w3, wzr
	uxth	w0, w0
	csel	w3, w4, w5, ne
	sub	w0, w0, w3
	uxth	w0, w0
.L693:
	add	x3, x1, 208
	strh	w0, [x3,x2,lsl 1]
	add	x2, x2, 1
	cmp	x2, 512
	bne	.L695
	b	.L678
.L692:
	cmp	w0, 9
	bne	.L696
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x0, 208
	strh	wzr, [x0,208]
	mov	w0, 1
	strh	w0, [x1,2]
	mov	w0, 2
	strh	w0, [x1,4]
	mov	x0, 0
.L697:
	add	x2, x1, x0
	add	w3, w0, 3
	add	x0, x0, 2
	cmp	x0, 1018
	strh	w3, [x2,6]
	bne	.L697
	b	.L678
.L696:
	cmp	w0, 10
	bne	.L698
	adrp	x2, .LANCHOR0
	mov	x0, 0
	add	x2, x2, :lo12:.LANCHOR0
.L699:
	add	x1, x2, 208
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L699
	mov	x0, 0
.L700:
	add	x2, x1, x0
	add	w3, w0, 63
	add	x0, x0, 2
	cmp	x0, 898
	strh	w3, [x2,126]
	bne	.L700
	b	.L678
.L698:
	cmp	w0, 11
	bne	.L701
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
.L702:
	add	x2, x1, 208
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L702
	mov	w5, 7
	mov	w4, 6
.L704:
	and	w3, w0, 1
	ubfiz	w1, w0, 1, 15
	add	w0, w0, 1
	cmp	w3, wzr
	csel	w3, w5, w4, ne
	add	x2, x2, 2
	uxth	w0, w0
	sub	w1, w1, w3
	cmp	w0, 512
	strh	w1, [x2,14]
	bne	.L704
	b	.L678
.L701:
	cmp	w0, 12
	bne	.L678
	adrp	x1, .LANCHOR0
	mov	w2, 4
	add	x1, x1, :lo12:.LANCHOR0
	add	x0, x1, 208
	strh	wzr, [x1,208]
	mov	w1, 1
	strh	w1, [x0,2]
	mov	w1, 2
	strh	w1, [x0,4]
	mov	w1, 3
	strh	w1, [x0,6]
.L705:
	sub	w1, w2, #1
	add	x0, x0, 2
	add	w1, w1, w2, lsr 1
	add	w2, w2, 1
	strh	w1, [x0,6]
	uxth	w2, w2
	cmp	w2, 512
	bne	.L705
	b	.L678
.L673:
	cmp	w20, w0, uxth
	bls	.L730
	add	x2, x1, 208
	ldrh	w2, [x2,x0,lsl 1]
	add	x0, x0, 1
	strh	w2, [x19,w2,sxtw 1]
	b	.L673
.L730:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	adrp	x21, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	strb	wzr, [x0,3556]
	adrp	x0, .LANCHOR1+482
	add	x19, x21, :lo12:.LANCHOR0
	mov	x24, 0
	add	x25, x19, 2132
	ldrh	w0, [x0,#:lo12:.LANCHOR1+482]
	strb	wzr, [x19,2358]
	bl	FlashBlockAlignInit
	mov	w1, 0
	mov	w2, 8
	add	x0, x19, 2360
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 32
	add	x0, x19, 1232
	bl	ftl_memset
	add	x0, x19, 2164
	mov	w1, 0
	mov	w2, 192
	bl	ftl_memset
	ldr	x20, [x19,88]
	add	x23, x20, 1
.L733:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x25, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L732
	ldrb	w1, [x19,2358]
	add	x2, x19, 1232
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x19, x1, sxtw
	strb	w0, [x19,2358]
	strb	w24, [x1,2360]
.L732:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L733
	add	x19, x21, :lo12:.LANCHOR0
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w1, [x19,2358]
	strb	w1, [x0,3556]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L734
.L738:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x20,13]
	add	x22, x22, :lo12:.LANCHOR2
	ldp	x23, x24, [sp,48]
	ldrb	w0, [x21,2358]
	ldp	x25, x26, [sp,64]
	mul	w0, w0, w1
	ldrh	w1, [x20,14]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,3558]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
.L734:
	ldr	w25, [x19,76]
	mov	x24, 0
	add	x26, x19, 2132
.L737:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x26, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L735
	ldrb	w1, [x20,13]
	add	x4, x19, 1232
	ldrb	w3, [x19,2358]
	mul	w0, w1, w25
	ldrh	w1, [x20,14]
	mov	x2, x3
	and	w1, w1, 65280
	mul	w1, w0, w1
	str	w1, [x4,w3,sxtw 2]
	ldrb	w0, [x20,23]
	cbz	w0, .L736
	lsl	w1, w1, 1
	str	w1, [x4,w3,sxtw 2]
.L736:
	add	x0, x19, x3
	add	w2, w2, 1
	strb	w2, [x19,2358]
	strb	w24, [x0,2360]
.L735:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L737
	b	.L738
	.size	FlashDieInfoInit, .-FlashDieInfoInit
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
	ldr	x0, [x1,88]
	ldrb	w2, [x0,9]
	ldr	w0, [x1,76]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldrb	w0, [x1,2464]
	strb	w0, [x19,7]
	ldr	w0, [x1,2584]
	str	w0, [x19]
	ldr	x0, [x1,88]
	ldrb	w4, [x1,2358]
	ldrb	w0, [x0,9]
	strb	w0, [x19,6]
	mov	w0, 32
	strb	w0, [x19,8]
	ldr	x0, [x1,88]
	ldrb	w0, [x0,7]
	strb	w0, [x19,9]
	mov	x0, 0
	strb	wzr, [x19,10]
.L746:
	cmp	w4, w0, uxtb
	bls	.L748
	add	x2, x1, 2360
	ldrb	w3, [x19,10]
	ldrb	w2, [x0,x2]
	add	x0, x0, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L746
.L748:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	stp	x29, x30, [sp, -80]!
	mov	w1, 65535
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	adrp	x22, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR2
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	add	x3, x22, :lo12:.LANCHOR0
	str	w1, [x0,3572]
	strh	wzr, [x0,3560]
	mov	w1, -1
	str	wzr, [x0,3564]
	strh	w1, [x0,1456]
	str	wzr, [x0,848]
	str	wzr, [x0,752]
	str	wzr, [x0,756]
	str	wzr, [x0,740]
	str	wzr, [x0,728]
	str	wzr, [x0,724]
	str	wzr, [x0,732]
	str	wzr, [x0,736]
	str	wzr, [x0,720]
	str	wzr, [x0,760]
	str	wzr, [x0,764]
	str	wzr, [x0,448]
	str	wzr, [x0,772]
	str	wzr, [x0,776]
	str	wzr, [x0,3568]
	str	wzr, [x0,1380]
	str	wzr, [x0,3576]
	str	wzr, [x0,1372]
	str	wzr, [x0,3580]
	strh	w1, [x0,1458]
	strh	w1, [x0,1460]
	strh	w1, [x0,1462]
	mov	w1, 32
	ldrh	w2, [x3,2550]
	strh	w1, [x0,1368]
	mov	w1, 128
	strh	w1, [x0,1370]
	mov	w1, 1024
	strh	wzr, [x0,1376]
	sdiv	w2, w1, w2
	ldrh	w1, [x3,2472]
	strh	wzr, [x0,1464]
	lsl	w1, w1, 2
	strh	wzr, [x0,3584]
	strh	wzr, [x0,1506]
	cmp	w2, w1
	str	w2, [x0,3588]
	bls	.L750
	str	w1, [x0,3588]
.L750:
	add	x20, x22, :lo12:.LANCHOR0
	add	x19, x21, :lo12:.LANCHOR2
	mov	w24, 56
	ldrh	w0, [x20,2548]
	str	wzr, [x20,2600]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,1432]
	ldrh	w1, [x20,2548]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,2472]
	str	x0, [x19,1448]
	mul	w23, w23, w24
	lsl	w25, w23, 3
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,3592]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3600]
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,3608]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,432]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,1416]
	ldr	w0, [x19,3588]
	mul	w0, w0, w24
	bl	ftl_malloc
	str	x0, [x20,2608]
	ldrh	w0, [x20,2472]
	ldrh	w23, [x20,2554]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,1424]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3616]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3624]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3632]
	ldr	w0, [x19,1424]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,1400]
	ldr	w0, [x19,3588]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,3640]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3648]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3656]
	ldr	w1, [x19,1424]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,2472]
	str	x0, [x19,1392]
	ldrh	w0, [x20,2556]
	mul	w23, w0, w23
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3664]
	lsl	w0, w23, 3
	bl	ftl_malloc
	str	x0, [x19,3672]
	ldrh	w1, [x20,2556]
	ldr	w0, [x19,1424]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1408]
	ldrh	w1, [x20,2556]
	ldr	w0, [x19,3588]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,3680]
	ldrh	w0, [x20,2482]
	lsl	w0, w0, 1
	strh	w0, [x19,3688]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19,3696]
	ldrh	w0, [x19,3688]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19,3688]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w23, [x20,2482]
	str	x0, [x19,3704]
	add	x0, x0, 32
	lsl	w23, w23, 1
	str	x0, [x19,440]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3712]
	mov	w0, w23
	bl	ftl_malloc
	ldr	w23, [x20,2572]
	str	x0, [x19,520]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3720]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3728]
	ldrh	w0, [x20,2482]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x22,#:lo12:.LANCHOR0]
	ldrh	w0, [x20,2564]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x20,2592]
	ldrh	w0, [x20,2564]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,3736]
	ldrh	w0, [x20,2564]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3744]
	ldrh	w0, [x20,2566]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3752]
	ldrh	w2, [x20,2566]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w23, [x20,2580]
	lsl	w23, w23, 2
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3760]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3768]
	ldr	w0, [x20,2572]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3776]
	ldrh	w0, [x20,2582]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,704]
	ldrh	w1, [x20,2582]
	ldrh	w0, [x20,2554]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,3784]
	ldrh	w1, [x20,2482]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,504]
	ldrh	w0, [x20,2542]
	ldrh	w1, [x20,2494]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,3792]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,2656]
	add	x1, x20, 2624
	ldrh	w4, [x19,3792]
	ldrh	w5, [x20,2494]
	mov	w0, 1
	mov	w2, w4
.L751:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L851
	ldr	x3, [x20,2656]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L751
.L851:
	add	x1, x22, :lo12:.LANCHOR0
.L753:
	cmp	w0, 8
	beq	.L852
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,2656]
	b	.L753
.L852:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,3720]
	cbnz	x1, .L755
.L757:
	adrp	x0, .LC80
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC80
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L756
.L755:
	ldr	x1, [x0,3728]
	cbz	x1, .L757
	ldr	x1, [x0,3760]
	cbz	x1, .L757
	ldr	x1, [x0,3776]
	cbz	x1, .L757
	ldr	x1, [x0,704]
	cbz	x1, .L757
	ldr	x1, [x0,3784]
	cbz	x1, .L757
	ldr	x1, [x0,504]
	cbz	x1, .L757
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x1, [x22,2656]
	cbz	x1, .L757
	ldr	x0, [x0,520]
	cbz	x0, .L757
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,1432]
	cbz	x1, .L757
	ldr	x1, [x0,1448]
	cbz	x1, .L757
	ldr	x1, [x0,3592]
	cbz	x1, .L757
	ldr	x1, [x0,3608]
	cbz	x1, .L757
	ldr	x1, [x0,432]
	cbz	x1, .L757
	ldr	x1, [x0,1416]
	cbz	x1, .L757
	ldr	x1, [x0,3600]
	cbz	x1, .L757
	ldr	x1, [x0,3616]
	cbz	x1, .L757
	ldr	x1, [x0,3624]
	cbz	x1, .L757
	ldr	x0, [x0,3632]
	cbz	x0, .L757
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,1400]
	cbz	x1, .L757
	ldr	x1, [x0,3648]
	cbz	x1, .L757
	ldr	x1, [x0,3656]
	cbz	x1, .L757
	ldr	x1, [x0,1392]
	cbz	x1, .L757
	ldr	x1, [x0,3664]
	cbz	x1, .L757
	ldr	x1, [x0,3672]
	cbz	x1, .L757
	ldr	x1, [x0,1408]
	cbz	x1, .L757
	ldr	x1, [x0,440]
	cbz	x1, .L757
	ldr	x0, [x0,3696]
	cbz	x0, .L757
	adrp	x0, .LANCHOR0+2592
	ldr	x0, [x0,#:lo12:.LANCHOR0+2592]
	cbz	x0, .L757
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,3736]
	cbz	x1, .L757
	ldr	x1, [x0,3744]
	cbz	x1, .L757
	ldr	x0, [x0,3752]
	cbz	x0, .L757
	mov	w0, 0
.L756:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x20, x0
	adrp	x0, .LANCHOR2+3792
	mov	x19, x1
	ldrh	w2, [x0,#:lo12:.LANCHOR2+3792]
	mov	x0, x1
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	adrp	x5, .LANCHOR0
	mov	x3, 0
	mov	w0, 65535
	mov	w6, 1
	add	x5, x5, :lo12:.LANCHOR0
.L855:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L853
	ubfx	x4, x2, 5, 11
	lsl	w2, w6, w2
	lsl	x4, x4, 2
	add	x3, x3, 2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	ldrh	w2, [x5,2630]
	add	w2, w2, 1
	strh	w2, [x5,2630]
	bne	.L855
.L853:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	adrp	x0, .LANCHOR0
	mov	w1, -1
	stp	x29, x30, [sp, -16]!
	add	x0, x0, :lo12:.LANCHOR0
	mov	w2, 16
	add	x29, sp, 0
	add	x0, x0, 2636
	strh	w1, [x0,-12]
	mov	w1, 255
	strh	wzr, [x0,-6]
	bl	ftl_memset
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	adrp	x1, .LANCHOR0
	mov	w2, 2048
	stp	x29, x30, [sp, -16]!
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	strh	w0, [x1,2720]
	add	x0, x1, 2728
	strh	wzr, [x1,2722]
	strh	wzr, [x1,2724]
	strh	wzr, [x1,2726]
	mov	w1, 0
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
.L861:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L883
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L862:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L884
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22,x3]
	cmp	w2, w4
	bne	.L863
	cbz	w4, .L863
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L863:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L862
.L884:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L861
.L883:
	adrp	x0, .LANCHOR0+2546
	mov	w23, 0
	mov	w20, w23
	ldrh	w1, [x0,#:lo12:.LANCHOR0+2546]
	ldrh	w0, [x19]
	strh	w1, [x21,x0,lsl 1]
	ldrh	w24, [x21]
.L866:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L885
	ubfiz	x2, x20, 1, 16
	ldrh	w1, [x21,x2]
	cmp	w24, w1
	bls	.L867
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbnz	w0, .L868
	b	.L869
.L867:
	cbnz	w1, .L869
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbz	w0, .L869
.L871:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L869:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L866
.L868:
	mov	w23, w20
	mov	w24, w1
	cbz	w1, .L871
	b	.L869
.L885:
	mov	w0, w23
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	stp	x29, x30, [sp, -48]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp,32]
	add	x21, x20, :lo12:.LANCHOR0
	add	x22, x19, :lo12:.LANCHOR2
	ldr	w2, [x21,2572]
	ldr	x0, [x22,3728]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x21,2554]
	mov	w1, 255
	ldrh	w2, [x21,2582]
	ldr	x0, [x22,3784]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w1, 0
	mov	w5, -1
.L887:
	add	x3, x20, :lo12:.LANCHOR0
	ldrh	w0, [x3,2582]
	cmp	w0, w1
	bls	.L889
	add	x4, x19, :lo12:.LANCHOR2
	ubfiz	x0, x1, 4, 16
	ldr	x2, [x4,704]
	add	x2, x2, x0
	str	wzr, [x2,4]
	ldr	x2, [x4,704]
	strh	w5, [x2,x0]
	ldr	x2, [x4,704]
	add	x2, x2, x0
	ldrh	w0, [x3,2554]
	ldr	x3, [x4,3784]
	mul	w0, w0, w1
	add	w1, w1, 1
	sxtw	x0, w0
	uxth	w1, w1
	and	x0, x0, -4
	add	x0, x3, x0
	str	x0, [x2,8]
	b	.L887
.L889:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, -1
	add	x1, x0, 3800
	strh	w2, [x0,3802]
	strh	w2, [x0,3800]
	ldr	w2, [x3,2572]
	strh	w2, [x0,3810]
	mov	w2, -3902
	strh	w2, [x0,3804]
	ldrh	w2, [x0,3864]
	strh	w2, [x0,3808]
	ldrh	w2, [x3,2580]
	strh	w2, [x0,3806]
	ldr	x2, [x0,3720]
	str	x2, [x0,3816]
	ldr	x2, [x0,3776]
	str	x2, [x0,3824]
	ldr	x2, [x0,3728]
	str	x2, [x0,3832]
	ldr	x0, [x0,3760]
	str	x0, [x1,40]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	stp	x29, x30, [sp, -32]!
	mov	w0, -1
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	adrp	x20, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	strh	w0, [x19,3880]
	mov	w0, -1
	ldrh	w2, [x20,2564]
	str	w0, [x19,3892]
	ldr	x0, [x20,2592]
	lsl	w2, w2, 1
	str	xzr, [x19,3872]
	str	wzr, [x19,3884]
	str	wzr, [x19,3888]
	str	wzr, [x20,2372]
	strh	wzr, [x20,2590]
	bl	ftl_memset
	ldrh	w2, [x20,2482]
	mov	w1, 0
	ldr	x0, [x19,440]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2482]
	mov	w1, 0
	ldr	x0, [x19,3696]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x19, 456
	mov	w1, 0
	mov	w2, 48
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 512
	add	x0, x19, 856
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	stp	x29, x30, [sp, -112]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	mov	w24, 6
	add	x0, x23, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR2
	mov	w25, 0
	add	x19, x21, :lo12:.LANCHOR2
	ldrh	w2, [x0,2482]
	mov	w22, w25
	mov	w26, 32768
	ldr	x0, [x19,504]
	mul	w2, w2, w24
	bl	ftl_memset
	str	xzr, [x19,544]
	str	xzr, [x19,512]
	str	xzr, [x19,528]
	strh	wzr, [x19,536]
	strh	wzr, [x19,552]
	strh	wzr, [x19,3560]
	mov	w19, w25
.L892:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2480]
	cmp	w19, w1
	bge	.L899
	mov	w20, 0
	ldrh	w3, [x0,2472]
	ldrh	w2, [x0,2544]
	mov	w28, w20
	mov	x27, x0
.L900:
	cmp	w28, w3
	bge	.L901
	add	x0, x27, 2504
	mov	w1, w19
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L893
	add	w20, w2, w20
	sxth	w20, w20
.L893:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L900
.L901:
	cbz	w20, .L895
	sdiv	w20, w26, w20
	sxth	w20, w20
	b	.L896
.L895:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, -1
	ldr	x0, [x0,520]
	strh	w1, [x0,w19,sxtw 1]
.L896:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,504]
	smaddl	x1, w19, w24, x1
	strh	w20, [x1,4]
	ldrh	w1, [x0,560]
	cmp	w19, w1
	beq	.L897
	ldrh	w1, [x0,608]
	cmp	w19, w1
	beq	.L897
	ldrh	w1, [x0,656]
	cmp	w19, w1
	beq	.L897
	ldr	x0, [x0,520]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L898
	add	w4, w25, 1
	mov	w0, w19
	uxth	w25, w4
	bl	INSERT_FREE_LIST
	b	.L897
.L898:
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_DATA_LIST
.L897:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L892
.L899:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x23, x24, [sp,48]
	strh	w25, [x21,552]
	strh	w22, [x21,536]
	ldp	x25, x26, [sp,64]
	ldp	x21, x22, [sp,32]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	stp	x29, x30, [sp, -32]!
	mov	w1, 255
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w2, [x20,2548]
	ldr	x0, [x19,1432]
	lsl	w2, w2, 1
	strh	wzr, [x19,1428]
	strh	wzr, [x19,1440]
	bl	ftl_memset
	ldrh	w3, [x20,2548]
	mov	w2, 12
	ldr	x0, [x19,1448]
	mov	w1, 255
	mul	w2, w3, w2
	bl	ftl_memset
	bl	FtlGcBufInit
	ldp	x19, x20, [sp,16]
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
	mov	w2, 2048
	add	x1, x1, :lo12:.LANCHOR2
	add	x29, sp, 0
	add	x1, x1, 3896
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
	stp	x21, x22, [sp,32]
	adrp	x19, .LANCHOR1
	adrp	x22, .LANCHOR0
	add	x21, x19, :lo12:.LANCHOR1
	add	x22, x22, :lo12:.LANCHOR0
	str	x23, [sp,48]
	add	x21, x21, 512
	mov	w20, 0
	add	x22, x22, 2132
.L909:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x22
	bl	FlashMemCmp8
	mov	w23, w0
	cbnz	w0, .L906
	add	x0, x19, :lo12:.LANCHOR1
	ubfiz	x20, x20, 5, 32
	add	x1, x0, 512
	adds	x20, x1, x20
	beq	.L912
	ldrb	w3, [x20,22]
	mov	x2, 0
	mov	x1, x0
	b	.L911
.L906:
	add	w20, w20, 1
	add	x21, x21, 32
	cmp	w20, 86
	bne	.L909
	b	.L912
.L911:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,3264]
	cmp	w4, w3
	beq	.L910
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L911
	mov	w0, w2
.L910:
	add	x19, x19, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x19, 3264
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x2, x1
	add	x0, x0, 96
	mov	w2, 32
	add	x19, x19, 472
	bl	ftl_memcpy
	mov	x0, x19
	mov	x1, x20
	mov	w2, 32
	bl	ftl_memcpy
	ldrh	w0, [x19,10]
	bl	FlashBlockAlignInit
	b	.L907
.L912:
	mov	w23, -1
.L907:
	mov	w0, w23
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
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
	bne	.L917
	cbz	x3, .L918
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L918:
	cbz	x20, .L916
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
	b	.L916
.L917:
	cbz	x3, .L921
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L921:
	cbz	x20, .L916
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
.L916:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	NandcCopy1KB, .-NandcCopy1KB
	.align	2
	.global	ftl_memcpy32
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	mov	x3, 0
.L933:
	cmp	w2, w3
	bls	.L935
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L933
.L935:
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
	.global	timer_get_time
	.type	timer_get_time, %function
timer_get_time:
	adrp	x0, jiffies
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:jiffies]
	bl	jiffies_to_msecs
	ldp	x29, x30, [sp], 16
	ret
	.size	timer_get_time, .-timer_get_time
	.align	2
	.global	FlashSramLoadStore
	.type	FlashSramLoadStore, %function
FlashSramLoadStore:
	adrp	x4, .LANCHOR4+1592
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1592]
	add	x4, x4, 4096
	cbnz	w2, .L939
	add	x1, x4, x1
	b	.L941
.L939:
	add	x0, x4, x1
	mov	x1, x6
.L941:
	mov	w2, w3
	bl	ftl_memcpy
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashSramLoadStore, .-FlashSramLoadStore
	.align	2
	.global	FlashCs123Init
	.type	FlashCs123Init, %function
FlashCs123Init:
	ret
	.size	FlashCs123Init, .-FlashCs123Init
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
	ldr	w0, [x0,#:lo12:.LANCHOR0+2584]
	ret
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	2
	.global	rk_nandc_get_irq_status
	.type	rk_nandc_get_irq_status, %function
rk_nandc_get_irq_status:
	ldr	w0, [x0,372]
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
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x19, 0
	adrp	x23, .LC81
	adrp	x21, .LC82
	adrp	x24, .LC75
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x22, x1
	mov	x27, x0
	mov	w25, w2
	uxtw	x26, w3
	mov	w20, w19
	add	x23, x23, :lo12:.LC81
	add	x21, x21, :lo12:.LC82
	add	x24, x24, :lo12:.LC75
.L950:
	cmp	x19, x26
	beq	.L958
	cbnz	w20, .L951
	mov	x0, x23
	mov	x1, x27
	mov	x2, x22
	mov	w3, w19
	bl	printk
.L951:
	cmp	w25, 4
	mov	x0, x21
	bne	.L952
	ldr	w1, [x22,x19,lsl 2]
	b	.L957
.L952:
	cmp	w25, 2
	bne	.L954
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L957
.L954:
	ldrb	w1, [x22,x19]
.L957:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L955
	adrp	x1, .LC83
	mov	x0, x24
	add	x1, x1, :lo12:.LC83
	mov	w20, 0
	bl	printk
.L955:
	add	x19, x19, 1
	b	.L950
.L958:
	adrp	x0, .LC75
	adrp	x1, .LC83
	add	x1, x1, :lo12:.LC83
	add	x0, x0, :lo12:.LC75
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
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	stp	x29, x30, [sp, -144]!
	mov	w4, -83
	mov	w2, -82
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	stp	x27, x28, [sp,80]
	mov	w27, w0
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 1272
	cmp	w27, 2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	strb	w27, [x0,1272]
	mov	w0, -84
	strb	w4, [x1,5]
	strb	w0, [x1,4]
	mov	w0, -81
	strb	w2, [x1,6]
	strb	w0, [x1,7]
	bne	.L960
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+3409
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+3409]
	b	.L1023
.L960:
	cmp	w27, 3
	bne	.L962
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
	b	.L1063
.L962:
	cmp	w27, 4
	bne	.L963
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
	strb	w2, [x1,10]
	strb	w5, [x1,8]
.L1063:
	mov	w26, 8
	strb	w0, [x1,11]
	mov	w28, w26
	b	.L961
.L963:
	cmp	w27, 5
	bne	.L964
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w26, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L1064
.L964:
	cmp	w27, 6
	bne	.L965
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w26, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L1064
.L965:
	cmp	w27, 7
	bne	.L966
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
	mov	w26, 12
	strb	w0, [x1,12]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x1,13]
	b	.L961
.L966:
	cmp	w27, 8
	bne	.L1023
	mov	w0, 6
	strb	w0, [x1,4]
	mov	w0, 7
	strb	w0, [x1,5]
	mov	w0, 9
	strb	w27, [x1,6]
	strb	w0, [x1,7]
	mov	w26, 50
	mov	w0, 10
	mov	w28, 5
	strb	w0, [x1,8]
	b	.L961
.L1023:
	mov	w26, 7
.L1064:
	mov	w28, 4
.L961:
	sub	w0, w27, #1
	cmp	w0, 1
	bhi	.L1060
	adrp	x22, .LANCHOR1
	add	x25, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	add	x4, x25, 1272
	add	x27, x25, 8
	add	x22, x22, 3392
.L967:
	ldrb	w0, [x25,2358]
	cmp	w0, w21
	bls	.L974
	add	x0, x25, x21, sxtw
	ldrb	w0, [x0,2360]
	mov	x23, 0
	add	x20, x4, x0, lsl 6
	lsl	x0, x0, 4
	add	x1, x27, x0
	add	x20, x20, 20
	ldr	x24, [x27,x0]
	ldrb	w0, [x1,8]
	mov	w1, 55
	add	x24, x24, x0, lsl 8
.L969:
	add	x0, x4, x23
	str	w1, [x24,2056]
	str	x1, [x29,128]
	str	x4, [x29,136]
	ldrb	w0, [x0,4]
	str	w0, [x24,2052]
	mov	x0, 400
	bl	__const_udelay
	ldr	w0, [x24,2048]
	strb	w0, [x20,x23]
	add	x23, x23, 1
	ldr	x4, [x29,136]
	cmp	w28, w23, uxtb
	ldr	x1, [x29,128]
	bhi	.L969
	mov	x0, 0
.L970:
	add	w2, w0, 8
	mov	x1, 0
.L971:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x22, x3
	cmp	x1, 24
	ldrb	w6, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w6, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L971
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L970
	add	w21, w21, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w21, w21
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L967
.L1060:
	sub	w0, w27, #3
	cmp	w0, 5
	bhi	.L974
	mul	w25, w28, w26
	sub	w24, w28, #1
	add	x20, x19, :lo12:.LANCHOR0
	lsl	w0, w25, 4
	asr	w23, w25, 1
	str	w0, [x29,128]
	lsl	w0, w23, 1
	uxtb	x24, w24
	str	w0, [x29,124]
	sub	w0, w27, #5
	mov	w21, 0
	str	w0, [x29,136]
	add	x25, x20, 8
	add	x0, x24, 1
	str	x0, [x29,112]
.L975:
	ldrb	w0, [x20,2358]
	cmp	w0, w21
	bhi	.L1021
.L974:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	strb	w28, [x19,1273]
	strb	w26, [x19,1274]
	ldp	x27, x28, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 144
	ret
.L1021:
	add	x0, x20, x21, sxtw
	ldrb	w6, [x0,2360]
	str	x6, [x29,104]
	lsl	x0, x6, 4
	mov	x22, x6
	add	x1, x25, x0
	ldr	x0, [x25,x0]
	ldrb	w2, [x1,8]
	add	x24, x0, x2, lsl 8
	mov	w0, 255
	str	w0, [x24,2056]
	mov	w0, w6
	bl	NandcWaitFlashReady
	cmp	w27, 7
	ldr	x6, [x29,104]
	bne	.L976
	mov	x0, 160
	madd	x6, x6, x0, x20
	add	x6, x6, 1300
	b	.L977
.L976:
	cmp	w27, 8
	beq	.L978
	add	x6, x20, x6, lsl 6
	add	x6, x6, 1292
.L977:
	mov	w0, 54
	cmp	w27, 4
	str	w0, [x24,2056]
	bne	.L979
	mov	w0, 255
	str	w0, [x24,2052]
	mov	w0, 64
	str	w0, [x24,2048]
	mov	w0, 204
	b	.L1065
.L979:
	ldr	w0, [x29,136]
	cmp	w0, 1
	bhi	.L981
	ldrb	w0, [x20,1276]
	str	w0, [x24,2052]
	mov	w0, 82
	b	.L1066
.L981:
	cmp	w27, 7
	bne	.L980
	mov	w0, 174
	str	w0, [x24,2052]
	str	wzr, [x24,2048]
	mov	w0, 176
.L1065:
	str	w0, [x24,2052]
	mov	w0, 77
.L1066:
	str	w0, [x24,2048]
.L980:
	mov	w0, 22
	str	w0, [x24,2056]
	mov	w0, 23
	str	w0, [x24,2056]
	mov	w0, 4
	str	w0, [x24,2056]
	mov	w0, 25
	str	w0, [x24,2056]
	str	wzr, [x24,2056]
	cmp	w27, 6
	str	wzr, [x24,2052]
	str	wzr, [x24,2052]
	bne	.L982
	mov	w0, 31
	str	w0, [x24,2052]
	b	.L983
.L982:
	str	wzr, [x24,2052]
.L983:
	mov	w0, 2
	str	w0, [x24,2052]
	str	wzr, [x24,2052]
.L1022:
	mov	w0, 48
	str	w0, [x24,2056]
	mov	w0, w22
	str	x6, [x29,104]
	bl	NandcWaitFlashReady
	ldr	w0, [x29,136]
	ldr	x6, [x29,104]
	cmp	w0, 1
	cset	w9, ls
	cmp	w27, 8
	cset	w8, eq
	cbnz	w9, .L1025
	cbnz	w8, .L1025
	cmp	w27, 7
	mov	w7, 32
	mov	w0, 2
	csel	w7, w0, w7, ne
	b	.L984
.L1025:
	mov	w7, 16
.L984:
	adrp	x1, .LANCHOR4
	mov	x10, 0
	add	x0, x1, :lo12:.LANCHOR4
	ldr	x0, [x0,1600]
.L985:
	ldr	w11, [x24,2048]
	strb	w11, [x0,x10]
	add	x10, x10, 1
	cmp	w7, w10, uxtb
	bhi	.L985
	cbz	w8, .L986
	mov	w7, 0
.L988:
	ldrb	w10, [x0]
	cmp	w10, 50
	beq	.L987
	ldrb	w10, [x0,1]
	cmp	w10, 5
	beq	.L987
	add	w7, w7, 1
	add	x0, x0, 4
	uxtb	w7, w7
	cmp	w7, 8
	bne	.L988
	b	.L989
.L987:
	cmp	w7, 6
	bls	.L990
.L989:
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L991:
	b	.L991
.L986:
	cmp	w27, 7
	bne	.L992
	mov	w7, w8
.L994:
	ldrb	w10, [x0]
	cmp	w10, 12
	beq	.L993
	ldrb	w10, [x0,1]
	cmp	w10, 10
	beq	.L993
	add	w7, w7, 1
	add	x0, x0, 4
	uxtb	w7, w7
	cmp	w7, 8
	bne	.L994
	b	.L995
.L993:
	cmp	w7, 7
	bne	.L990
.L995:
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L996:
	b	.L996
.L992:
	cmp	w27, 6
	bne	.L990
	mov	x7, 0
.L997:
	ldrb	w10, [x0,x7]
	cmp	w10, 12
	beq	.L990
	add	x10, x0, x7
	ldrb	w10, [x10,8]
	cmp	w10, 4
	beq	.L990
	add	x7, x7, 1
	cmp	x7, 8
	bne	.L997
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L999:
	b	.L999
.L990:
	add	x0, x1, :lo12:.LANCHOR4
	ldr	x11, [x0,1600]
	mov	x0, 0
.L1000:
	ldr	w2, [x29,128]
	cmp	w2, w0
	ble	.L1068
	ldr	w7, [x24,2048]
	strb	w7, [x11,x0]
	add	x0, x0, 1
	b	.L1000
.L1068:
	add	x0, x1, :lo12:.LANCHOR4
	mov	w12, w23
	mov	w10, 8
	ldr	x13, [x0,1600]
.L1003:
	mov	w0, 0
.L1002:
	add	w7, w0, w12
	add	w0, w0, 1
	sbfiz	x7, x7, 1, 32
	cmp	w0, w23
	ldrh	w14, [x13,x7]
	mvn	w14, w14
	strh	w14, [x13,x7]
	bne	.L1002
	ldr	w0, [x29,124]
	subs	w10, w10, #1
	add	w12, w12, w0
	bne	.L1003
	mov	x10, 0
	mov	w18, 1
.L1004:
	mov	w7, 0
	mov	w12, w7
.L1008:
	lsl	w14, w18, w12
	mov	w16, w10
	mov	w0, 16
	mov	w15, 0
.L1006:
	ldrh	w17, [x13,w16,sxtw 1]
	add	w16, w16, w23
	and	w17, w17, w14
	cmp	w17, w14
	csinc	w15, w15, w15, ne
	subs	w0, w0, #1
	bne	.L1006
	cmp	w15, 8
	bls	.L1007
	orr	w7, w7, w14
	uxth	w7, w7
.L1007:
	add	w12, w12, 1
	cmp	w12, 16
	bne	.L1008
	strh	w7, [x13,x10,lsl 1]
	add	x10, x10, 1
	cmp	w23, w10
	bgt	.L1004
	add	x1, x1, :lo12:.LANCHOR4
	mov	x7, 0
	mov	w10, w7
	ldr	x1, [x1,1600]
.L1011:
	ldr	w12, [x1,x7]
	add	x7, x7, 4
	cmp	w12, wzr
	csinc	w10, w10, w10, ne
	cmp	x7, 32
	bne	.L1011
	cmp	w10, 7
	ble	.L1012
	adrp	x0, .LC85
	mov	w2, 1
	add	x0, x0, :lo12:.LC85
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L1013:
	b	.L1013
.L1012:
	cmp	w27, 6
	mov	w7, 4
	beq	.L1014
	cmp	w27, 7
	mov	w7, 10
	beq	.L1014
	cmp	w8, wzr
	mov	w7, 5
	mov	w1, 8
	csel	w7, w1, w7, eq
.L1014:
	mov	w10, 0
.L1015:
	mov	x1, 0
.L1016:
	add	w12, w0, w1
	ldrb	w13, [x11,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w13, [x6,w12,sxtw]
	bhi	.L1016
	add	w10, w10, 1
	ldr	x1, [x29,112]
	cmp	w10, w26
	add	w0, w0, w7
	add	x11, x11, x1
	blt	.L1015
	mov	w0, 255
	str	w0, [x24,2056]
	mov	w0, w22
	str	x8, [x29,96]
	str	x9, [x29,104]
	bl	NandcWaitFlashReady
	ldr	x9, [x29,104]
	ldr	x8, [x29,96]
	cbz	w9, .L1018
	mov	w0, 54
	str	w0, [x24,2056]
	adrp	x0, .LANCHOR0+1276
	mov	w1, -1
	ldrb	w0, [x0,#:lo12:.LANCHOR0+1276]
	str	w0, [x24,2052]
	mov	w0, 22
	str	wzr, [x24,2048]
	str	w0, [x24,2056]
	mov	w0, w21
	bl	FlashReadCmd
	b	.L1019
.L1018:
	mov	w0, 190
	cbnz	w8, .L1067
	mov	w0, 56
.L1067:
	str	w0, [x24,2056]
.L1019:
	mov	w0, w22
	add	w21, w21, 1
	bl	NandcWaitFlashReady
	uxtb	w21, w21
	b	.L975
.L978:
	mov	w0, 120
	str	w0, [x24,2056]
	str	wzr, [x24,2052]
	mov	w0, 23
	str	wzr, [x24,2052]
	mov	w1, 25
	str	wzr, [x24,2052]
	add	x6, x20, 1300
	str	w0, [x24,2056]
	mov	w0, 4
	str	w0, [x24,2056]
	str	w1, [x24,2056]
	mov	w1, 218
	str	w1, [x24,2056]
	mov	w1, 21
	str	wzr, [x24,2056]
	str	wzr, [x24,2052]
	str	wzr, [x24,2052]
	str	w1, [x24,2052]
	str	w0, [x24,2052]
	str	wzr, [x24,2052]
	b	.L1022
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L1069
	sub	w2, w0, #1
	cmp	w2, 7
	bhi	.L1071
	bl	HynixGetReadRetryDefault
	b	.L1069
.L1071:
	cmp	w0, 49
	bne	.L1072
	adrp	x0, .LANCHOR0
	mov	w2, 64
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 1276
	strb	w1, [x0,-4]
	mov	w1, 4
	strb	w1, [x0,-3]
	mov	w1, 15
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 408
	b	.L1087
.L1072:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L1078
	cmp	w1, 33
	bne	.L1073
.L1078:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,1272]
	mov	w1, 4
	b	.L1088
.L1073:
	cmp	w1, 67
	beq	.L1079
	cmp	w1, 34
	bne	.L1075
.L1079:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,1272]
	mov	w1, 5
.L1088:
	strb	w1, [x0,1273]
	mov	w1, 7
	strb	w1, [x0,1274]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 1276
	add	x1, x1, 256
	mov	w2, 45
	b	.L1087
.L1075:
	cmp	w1, 68
	beq	.L1080
	cmp	w1, 35
	bne	.L1069
.L1080:
	adrp	x0, .LANCHOR0
	mov	w2, 95
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 1276
	strb	w1, [x0,-4]
	mov	w1, 5
	strb	w1, [x0,-3]
	mov	w1, 17
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 304
.L1087:
	bl	ftl_memcpy
.L1069:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	NandcXferComp
	.type	NandcXferComp, %function
NandcXferComp:
	stp	x29, x30, [sp, -80]!
	ubfiz	x0, x0, 4, 8
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x23, [sp,48]
	add	x1, x20, :lo12:.LANCHOR0
	add	x2, x1, 8
	stp	x21, x22, [sp,32]
	ldr	x19, [x2,x0]
	ldr	w0, [x1,2388]
	cmp	w0, 3
	bls	.L1120
	ldr	w0, [x19,16]
	tbz	x0, 2, .L1120
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L1091
	adrp	x22, .LC86
	adrp	x23, .LC87
	mov	w21, 0
	add	x22, x22, :lo12:.LC86
	add	x23, x23, :lo12:.LC87
.L1092:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1100
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,2388]
	cmp	w0, 5
	bhi	.L1093
.L1096:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L1095
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
	b	.L1095
.L1093:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1096
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1096
.L1100:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 2408
	ldr	w0, [x19,40]
	cbz	w0, .L1101
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
	b	.L1101
.L1095:
	mov	x0, 1
	mov	x1, 5
	bl	usleep_range
	b	.L1092
.L1091:
	adrp	x22, .LC88
	adrp	x23, .LC87
	add	x22, x22, :lo12:.LC88
	add	x23, x23, :lo12:.LC87
.L1102:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L1128
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L1103
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
.L1103:
	mov	x0, 1
	mov	x1, 5
	bl	usleep_range
	b	.L1102
.L1128:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,2456]
	cbz	w0, .L1105
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L1105:
	add	x21, x20, :lo12:.LANCHOR0
	add	x21, x21, 2408
	ldr	w0, [x21,40]
	cbz	w0, .L1106
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
.L1106:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,2456]
	cbz	w0, .L1101
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L1101:
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x20,2448]
	b	.L1089
.L1120:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1120
.L1089:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	NandcXferComp, .-NandcXferComp
	.align	2
	.global	NandcXferData
	.type	NandcXferData, %function
NandcXferData:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	adrp	x19, .LANCHOR0
	uxtb	w25, w0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, x25, sxtw 4
	ldr	x20, [x0,8]
	and	x0, x3, 63
	uxtb	w21, w1
	uxtb	w24, w2
	mov	x26, x3
	mov	x22, x4
	cbnz	x0, .L1130
	cbnz	x4, .L1131
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x22, x29, 112
	bl	ftl_memset
.L1131:
	mov	w0, w25
	mov	w1, w21
	mov	w2, w24
	mov	w3, 0
	mov	x4, x26
	mov	x5, x22
	mov	w23, 0
	bl	NandcXferStart
	mov	w0, w25
	mov	w1, w21
	bl	NandcXferComp
	cbnz	w21, .L1132
	add	x1, x19, :lo12:.LANCHOR0
	ubfx	x2, x24, 1, 7
	mov	w3, 128
	add	x2, x22, x2, lsl 2
	ldr	w0, [x1,2460]
	cmp	w0, 25
	mov	w0, 64
	csel	w3, w0, w3, cc
	mov	w0, w21
.L1134:
	cmp	x22, x2
	add	w4, w0, w3
	beq	.L1176
	ldr	x5, [x1,2416]
	and	x0, x0, 4294967292
	add	x22, x22, 4
	ldr	w0, [x5,x0]
	strb	w0, [x22,-4]
	lsr	w5, w0, 8
	strb	w5, [x22,-3]
	lsr	w5, w0, 16
	strb	w5, [x22,-2]
	lsr	w0, w0, 24
	strb	w0, [x22,-1]
	mov	w0, w4
	b	.L1134
.L1176:
	add	x0, x19, :lo12:.LANCHOR0
	lsr	w24, w24, 2
	ldr	w5, [x0,2460]
	ldr	w4, [x0,2388]
	mov	w0, 0
	mov	w23, w0
.L1136:
	cmp	w0, w24
	bcs	.L1132
	cbz	w5, .L1132
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x20,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L1160
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L1160
	cmp	w4, 5
	bls	.L1138
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
	bls	.L1139
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L1140
.L1139:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L1175
.L1138:
	cmp	w4, 3
	bls	.L1140
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
	bls	.L1141
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L1175
.L1141:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L1175:
	orr	w3, w1, w3, lsl 5
.L1140:
	cmp	w23, w3
	csel	w23, w23, w3, cs
	b	.L1137
.L1160:
	mov	w23, -1
.L1137:
	add	w0, w0, 1
	b	.L1136
.L1132:
	str	wzr, [x20,16]
	b	.L1143
.L1130:
	cmp	w21, 1
	bne	.L1174
	mov	w23, 0
	mov	w28, 2
.L1144:
	cmp	w23, w24
	bcs	.L1177
	and	w27, w23, 3
	mov	x3, x26
	cbz	x26, .L1146
	ubfiz	x0, x23, 9, 23
	add	x3, x26, x0
.L1146:
	cmp	x22, xzr
	mov	x0, x20
	csel	w4, w28, wzr, ne
	mov	w1, 1
	mov	w2, w27
	mul	w4, w4, w23
	add	w23, w23, 2
	add	x4, x22, x4
	bl	NandcCopy1KB
	mov	x4, 0
	mov	w0, w25
	mov	w1, 1
	mov	w2, 2
	mov	w3, w27
	mov	x5, x4
	bl	NandcXferStart
	mov	w0, w25
	mov	w1, 1
	bl	NandcXferComp
	b	.L1144
.L1177:
	mov	w23, 0
	b	.L1143
.L1174:
	mov	w1, 0
	mov	x4, 0
	mov	w27, 0
	mov	w0, w25
	mov	w2, 2
	mov	w3, w1
	mov	x5, x4
	mov	w23, w27
	bl	NandcXferStart
.L1149:
	cmp	w27, w24
	bcs	.L1143
	mov	w0, w25
	mov	w1, w21
	add	w28, w27, 2
	bl	NandcXferComp
	ldr	w0, [x20,32]
	cmp	w28, w24
	str	w0, [x29,104]
	bcs	.L1150
	mov	x4, 0
	mov	w0, w25
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L1150:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L1165
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w23, w0
	csel	w23, w23, w0, cs
	b	.L1151
.L1165:
	mov	w23, -1
.L1151:
	and	w2, w27, 3
	mov	x3, x26
	cbz	x26, .L1152
	ubfiz	x3, x27, 9, 23
	add	x3, x26, x3
.L1152:
	cmp	x22, xzr
	mov	w0, 2
	csel	w4, w0, wzr, ne
	mov	w1, 0
	mov	x0, x20
	mul	w4, w4, w27
	mov	w27, w28
	add	x4, x22, x4
	bl	NandcCopy1KB
	b	.L1149
.L1143:
	cbnz	w21, .L1155
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19,2388]
	cmp	w0, 5
	bls	.L1155
	ldr	w0, [x20]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L1155
	orr	w0, w0, 131072
	mov	w23, -1
	str	w0, [x20]
.L1155:
	mov	w0, w23
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
	cbnz	w19, .L1179
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,73]
	ldr	w0, [x0,76]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L1179:
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
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	uxtb	w23, w0
	mov	w22, 6
	ldr	x0, [x19,136]
	mov	w24, w1
	mov	x28, x2
	mov	x27, x3
	mov	w25, w4
	mov	w21, 1024
	ldr	w0, [x0,304]
	str	w0, [x29,124]
	ldr	w0, [x19,2388]
	cmp	w0, 8
	mov	w0, 12
	csel	w22, w22, w0, cc
	cbz	w4, .L1182
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	mov	w0, w23
	bl	FlashReset
	mov	w1, w24
	mov	x2, x28
	mov	x3, x27
	mov	w0, w23
	bl	FlashReadRawPage
	mov	w21, w0
	ldrb	w0, [x19,2357]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,2357]
	bl	NandcSetMode
	cmn	w21, #1
	bne	.L1183
.L1192:
	mov	w21, -1
	b	.L1184
.L1183:
	adrp	x0, .LC89
	mov	w1, w24
	add	x0, x0, :lo12:.LC89
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L1185
	add	x19, x19, x23, sxtw 4
	ldr	x0, [x19,8]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L1185:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,1608]
	add	w1, w1, 1
	str	w1, [x0,1608]
	cmp	w1, 2047
	bls	.L1184
	mov	x27, 0
	str	wzr, [x0,1608]
	mov	x28, x27
.L1182:
	mov	w19, 0
	mov	w26, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L1190:
	mov	w0, w22
	str	x5, [x29,104]
	str	x6, [x29,112]
	bl	NandcSetDdrPara
	mov	w1, w24
	mov	w0, w23
	mov	x2, x28
	mov	x3, x27
	bl	FlashReadRawPage
	add	w1, w21, 1
	ldr	x6, [x29,112]
	cmp	w0, w1
	ldr	x5, [x29,104]
	bhi	.L1186
	cmp	w0, 2
	bhi	.L1196
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L1196
	sub	w19, w22, w20
	mov	w21, w0
	mov	w26, 0
	b	.L1188
.L1186:
	cmp	w6, w20
	bcs	.L1197
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L1198
	mov	w6, w20
	b	.L1197
.L1196:
	mov	x27, 0
	mov	w19, w22
	mov	w21, w0
	mov	w26, 0
	mov	x28, x27
	b	.L1187
.L1197:
	mov	w20, 0
.L1187:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L1190
.L1188:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L1189
.L1198:
	mov	w19, w5
.L1189:
	cbz	w19, .L1191
	adrp	x0, .LC90
	mov	w1, w19
	add	x0, x0, :lo12:.LC90
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L1191:
	cbz	w26, .L1184
	adrp	x0, .LC91
	mov	w1, w23
	add	x0, x0, :lo12:.LC91
	mov	w2, w24
	bl	printk
	cbz	w25, .L1192
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L1184:
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
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w22, w1
	mov	x24, x2
	mov	x23, x3
	bl	FlashReadRawPage
	cmn	w0, #1
	mov	w19, w0
	bne	.L1208
	adrp	x21, .LANCHOR0
	add	x25, x21, :lo12:.LANCHOR0
	ldrb	w26, [x25,80]
	cbnz	w26, .L1209
.L1211:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,2380]
	cbz	w0, .L1208
	b	.L1229
.L1209:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	strb	wzr, [x25,80]
	bl	FlashReadRawPage
	strb	w26, [x25,80]
	cmn	w0, #1
	beq	.L1211
	mov	w19, w0
	b	.L1208
.L1229:
	ldr	x0, [x21,136]
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w4, 1
	ldr	w25, [x0,304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w19, w0
	beq	.L1212
	ldrb	w0, [x21,2464]
	cmp	w19, w0, lsr 1
	bls	.L1208
.L1212:
	lsr	w0, w25, 8
	bl	NandcSetDdrPara
.L1208:
	adrp	x21, .LANCHOR4
	cmn	w19, #1
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x4, [x21,1616]
	bne	.L1213
	cbz	x4, .L1213
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w0, w20
	blr	x4
	mov	w19, w0
	adrp	x0, .LC92
	mov	w1, w19
	add	x0, x0, :lo12:.LC92
	mov	w2, w20
	mov	w3, w22
	bl	printk
	cmn	w19, #1
	bne	.L1213
	adrp	x0, .LANCHOR0+204
	ldrb	w0, [x0,#:lo12:.LANCHOR0+204]
	cbz	w0, .L1213
	mov	w0, w20
	bl	flash_enter_slc_mode
	ldr	x4, [x21,1616]
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	blr	x4
	mov	w19, w0
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1213:
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
	adrp	x19, .LANCHOR0
	mov	w20, w1
	stp	x21, x22, [sp,32]
	add	x22, x19, :lo12:.LANCHOR0
	uxtb	w21, w0
	ldrb	w0, [x22,2357]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,2357]
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
	beq	.L1234
	cmn	w22, #1
	bne	.L1231
.L1234:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,2357]
	tbz	x0, 0, .L1231
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,2380]
	b	.L1233
.L1231:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,2380]
.L1233:
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
	stp	x29, x30, [sp, -128]!
	mov	w0, 60
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR4
	adrp	x24, .LANCHOR1
	strb	w0, [x29,120]
	mov	w0, 40
	strb	w0, [x29,121]
	mov	w0, 24
	strb	w0, [x29,122]
	mov	w0, 16
	strb	w0, [x29,123]
	add	x0, x23, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	adrp	x22, .LANCHOR0
	add	x24, x24, :lo12:.LANCHOR1
	ldr	x1, [x0,1600]
	add	x21, x22, :lo12:.LANCHOR0
	str	x1, [x0,1624]
	mov	w20, 0
	str	wzr, [x0,1632]
	mov	w25, 4
	mov	w0, 0
	mov	w28, -1
	add	x24, x24, 472
	add	x27, x21, 96
	ldr	w26, [x21,76]
	bl	flash_enter_slc_mode
.L1242:
	add	w0, w20, 1
	mov	x19, 0
	str	w0, [x29,104]
.L1244:
	add	x0, x29, 120
	ldrb	w0, [x19,x0]
	bl	FlashBchSel
	add	x5, x23, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w1, w20
	mov	x3, 0
	str	x5, [x29,96]
	ldr	x2, [x5,1600]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1243
	ldr	x5, [x29,96]
	mov	w0, 0
	ldr	w1, [x29,104]
	mov	x3, 0
	ldr	x2, [x5,1600]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1243
	add	x19, x19, 1
	cmp	x19, 4
	beq	.L1245
	b	.L1244
.L1246:
	add	x0, x2, 12
	mov	w1, 2036
	str	x2, [x29,104]
	mov	w28, -1
	bl	js_hash
	ldr	x2, [x29,104]
	ldr	w1, [x2,8]
	cmp	w1, w0
	beq	.L1256
.L1245:
	subs	w25, w25, #1
	add	w20, w20, w26
	bne	.L1242
	mov	w0, w25
.L1255:
	bl	flash_exit_slc_mode
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
.L1243:
	add	x19, x23, :lo12:.LANCHOR4
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x2, [x19,1624]
	ldr	w1, [x2]
	cmp	w1, w0
	bne	.L1245
	cbnz	w28, .L1246
	add	x22, x22, :lo12:.LANCHOR0
	ldr	w0, [x22,76]
	udiv	w20, w20, w0
	mov	w0, w28
	add	w20, w20, 1
	str	w20, [x19,1636]
	b	.L1255
.L1256:
	add	x1, x2, 160
	mov	x0, x24
	mov	w2, 32
	bl	ftl_memcpy
	ldr	x1, [x19,1624]
	mov	w2, 32
	mov	x0, x27
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x19,1624]
	mov	w2, 852
	add	x0, x21, 1272
	add	x1, x1, 224
	bl	ftl_memcpy
	ldrh	w0, [x24,10]
	bl	FlashBlockAlignInit
	str	w20, [x19,1632]
	ldr	w1, [x21,76]
	ldr	x2, [x19,1624]
	udiv	w1, w20, w1
	ldr	w0, [x2,1076]
	add	w1, w1, 1
	strb	w0, [x21,2380]
	cmp	w1, 1
	bls	.L1248
	str	w1, [x19,1636]
	b	.L1249
.L1248:
	mov	w0, 2
	str	w0, [x19,1636]
.L1249:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w1, [x2,14]
	mov	w28, 0
	strb	w1, [x0,1640]
	b	.L1245
	.size	FlashLoadPhyInfo, .-FlashLoadPhyInfo
	.align	2
	.global	ToshibaReadRetrial
	.type	ToshibaReadRetrial, %function
ToshibaReadRetrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	mov	w0, w23
	str	w1, [x29,116]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	x27, x2
	mov	x26, x3
	adrp	x19, .LANCHOR0
	bl	NandcWaitFlashReady
	add	x2, x19, :lo12:.LANCHOR0
	sbfiz	x0, x23, 4, 32
	add	x1, x2, 8
	mov	w22, 0
	add	x3, x1, x0
	ldr	x24, [x1,x0]
	ldrb	w0, [x3,8]
	str	x0, [x29,120]
	add	x20, x0, 8
	mov	x28, x0
	ldrb	w0, [x2,128]
	add	x20, x24, x20, lsl 8
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1258
	ldrb	w0, [x2,2380]
	cbz	w0, .L1259
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L1259:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L1258:
	ldr	x0, [x29,120]
	mov	w21, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x28, 8, 8
	str	x0, [x29,96]
.L1260:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1641]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L1285
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w21
	ldrb	w0, [x0,128]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1261
	bl	SandiskSetRRPara
	b	.L1262
.L1261:
	bl	ToshibaSetRRPara
.L1262:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,128]
	cmp	w0, 34
	bne	.L1263
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1641]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L1263
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L1263:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L1264
	mov	w0, 4
	bl	NandcSetDdrMode
	ldr	w1, [x29,116]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
	mov	w0, 0
	bl	NandcSetDdrMode
	b	.L1265
.L1264:
	ldr	w1, [x29,116]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L1265:
	cmn	w28, #1
	beq	.L1266
	add	x0, x19, :lo12:.LANCHOR0
	cmn	w25, #1
	csel	w25, w25, w28, ne
	ldrb	w0, [x0,2464]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1268
	mov	x26, 0
	mov	x27, x26
.L1266:
	add	w21, w21, 1
	b	.L1260
.L1285:
	mov	w28, w25
.L1268:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,128]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1270
	bl	SandiskSetRRPara
	b	.L1271
.L1270:
	bl	ToshibaSetRRPara
.L1271:
	ldr	x0, [x29,120]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	ldrb	w0, [x19,2464]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1272
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1272:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L1273
	mov	w0, 4
	bl	NandcSetDdrMode
.L1273:
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
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
	adrp	x27, .LANCHOR4
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
	add	x0, x0, 8
	mov	w19, -1
	add	x2, x0, x1
	ldr	x0, [x0,x1]
	ldrb	w21, [x2,8]
	add	x21, x21, 8
	add	x21, x0, x21, lsl 8
.L1287:
	add	x0, x27, :lo12:.LANCHOR4
	ldrb	w0, [x0,1641]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L1290
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1288
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,2464]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1293
	mov	x23, 0
	mov	x24, x23
.L1288:
	add	w26, w26, 1
	b	.L1287
.L1293:
	mov	w19, w0
.L1290:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,2464]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1292
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1292:
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
	ldrb	w19, [x0,2464]
	mov	x22, x3
	ldrb	w0, [x0,204]
	cbnz	w0, .L1300
	add	w19, w19, w19, lsl 1
	ubfx	x19, x19, 2, 8
	b	.L1301
.L1300:
	mov	w0, 3
	sdiv	w19, w19, w0
.L1301:
	adrp	x27, .LC93
	mov	w25, 0
	add	x0, x27, :lo12:.LC93
	str	x0, [x29,152]
	add	x20, x20, :lo12:.LANCHOR0
	add	x23, x20, 8
	add	x23, x23, x21, sxtw 4
.L1312:
	mov	w0, w21
	mov	w24, 0
	mov	w27, -1
	bl	NandcWaitFlashReady
	ldrb	w4, [x23,8]
	mov	w9, 239
	ldr	x6, [x23]
	mov	w10, 137
	lsl	x8, x4, 8
.L1302:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1641]
	cmp	w24, w0
	bcs	.L1305
	add	x2, x6, x8
	mov	x0, 1000
	str	x4, [x29,104]
	str	x8, [x29,112]
	str	w9, [x2,2056]
	str	w10, [x2,2052]
	str	x6, [x29,120]
	str	x9, [x29,128]
	str	x10, [x29,136]
	str	x2, [x29,144]
	bl	__const_udelay
	ldr	x2, [x29,144]
	add	w7, w24, 1
	mov	w0, w21
	mov	w1, w28
	mov	x3, x22
	str	x7, [x29,144]
	str	w7, [x2,2048]
	str	wzr, [x2,2048]
	str	wzr, [x2,2048]
	str	wzr, [x2,2048]
	mov	x2, x26
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x7, [x29,144]
	ldr	x10, [x29,136]
	ldr	x9, [x29,128]
	ldr	x6, [x29,120]
	ldr	x8, [x29,112]
	ldr	x4, [x29,104]
	beq	.L1303
	cmn	w27, #1
	csel	w27, w27, w0, ne
	cmp	w0, w19
	bcc	.L1314
	mov	x22, 0
	mov	x26, x22
.L1303:
	mov	w24, w7
	b	.L1302
.L1314:
	mov	x22, 0
	mov	w27, w0
	mov	x26, x22
.L1305:
	add	x1, x6, x4, lsl 8
	mov	w0, 239
	str	x1, [x29,144]
	str	w0, [x1,2056]
	mov	w0, 137
	str	w0, [x1,2052]
	mov	x0, 1000
	bl	__const_udelay
	ldr	x1, [x29,144]
	cmp	w27, w19
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	bcc	.L1307
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
.L1307:
	cmn	w27, #1
	cset	w6, eq
	cbnz	w6, .L1316
	cmp	w27, 256
	cset	w1, eq
	cbz	w1, .L1308
.L1316:
	ldr	x0, [x29,152]
	mov	w1, w24
	mov	w2, w28
	mov	w3, w24
	mov	w4, w27
	str	x6, [x29,144]
	bl	printk
	cbnz	w25, .L1310
	ldr	x6, [x29,144]
	cbz	w6, .L1321
	ldrb	w0, [x20,204]
	cbz	w0, .L1321
	mov	w0, w21
	mov	w1, 3
	mov	w25, 1
	bl	micron_auto_read_calibration_config
	b	.L1312
.L1310:
	mov	w0, w21
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
	b	.L1321
.L1308:
	cbz	w25, .L1321
	mov	w0, w21
	mov	w27, 256
	bl	micron_auto_read_calibration_config
.L1321:
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
	add	x2, x0, 1272
	ldrb	w25, [x0,1274]
	mov	w27, w1
	ldr	x0, [x0,88]
	add	x1, x2, x22
	mov	x23, x22
	mov	x24, x3
	ldrb	w0, [x0,19]
	ldrb	w19, [x1,12]
	sub	w0, w0, #7
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L1330
	ldrb	w19, [x1,20]
.L1330:
	mov	w0, w23
	mov	w28, 0
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	add	x5, x4, 1276
.L1331:
	cmp	w28, w25
	bcs	.L1335
	add	w19, w19, 1
	ldrb	w1, [x4,1273]
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
	beq	.L1333
	ldrb	w1, [x4,2464]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1340
	mov	x24, 0
	mov	x26, x24
.L1333:
	add	w28, w28, 1
	b	.L1331
.L1340:
	mov	w20, w0
.L1335:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,88]
	add	x0, x0, 1272
	add	x0, x0, x22
	ldrb	w1, [x1,19]
	sub	w1, w1, #7
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L1337
	strb	w19, [x0,20]
	b	.L1338
.L1337:
	strb	w19, [x0,12]
.L1338:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,2464]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1339
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1339:
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
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	adrp	x21, .LANCHOR3
	mov	w0, w22
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w23, w1
	mov	x24, x2
	mov	x27, x3
	adrp	x25, .LANCHOR0
	bl	NandcWaitFlashReady
	add	x0, x25, :lo12:.LANCHOR0
	sbfiz	x1, x22, 4, 32
	add	x2, x0, 8
	add	x3, x2, x1
	ldrb	w0, [x0,2356]
	ldr	x5, [x2,x1]
	ldrb	w19, [x3,8]
	cbnz	w0, .L1347
	lsl	x19, x19, 8
	add	x21, x21, :lo12:.LANCHOR3
	mov	x4, 0
	mov	w20, -1
	add	x28, x5, x19
	mov	w6, 239
	mov	w7, 141
	add	x21, x21, 16
.L1351:
	str	w6, [x28,2056]
	add	x0, x21, x4
	str	w7, [x28,2052]
	mov	w1, w23
	mov	x2, x24
	mov	x3, x27
	ldrsb	w0, [x0,1]
	add	w26, w4, 1
	str	w0, [x28,2048]
	mov	w0, w22
	str	wzr, [x28,2048]
	str	wzr, [x28,2048]
	str	wzr, [x28,2048]
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
	beq	.L1348
	add	x1, x25, :lo12:.LANCHOR0
	cmn	w20, #1
	csel	w20, w20, w0, ne
	ldrb	w1, [x1,2464]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1360
	mov	x27, 0
	mov	x24, x27
.L1348:
	add	x4, x4, 1
	cmp	x4, 25
	bne	.L1351
	mov	w26, 26
	b	.L1350
.L1360:
	mov	w20, w0
.L1350:
	add	x3, x5, x19
	mov	w0, 239
	str	w0, [x3,2056]
	mov	w0, 141
	b	.L1376
.L1347:
	add	x21, x21, :lo12:.LANCHOR3
	lsl	x19, x19, 8
	add	x21, x21, 48
	mov	w20, -1
	mov	w26, 1
	add	x28, x5, x19
	mov	w4, 239
	mov	w6, 137
.L1356:
	str	w4, [x28,2056]
	mov	w1, w23
	str	w6, [x28,2052]
	mov	x2, x24
	mov	x3, x27
	str	x5, [x29,104]
	ldrb	w0, [x21,4]
	str	w0, [x28,2048]
	str	x4, [x29,112]
	ldrb	w0, [x21,5]
	str	w0, [x28,2048]
	str	x6, [x29,120]
	ldrb	w0, [x21,6]
	str	w0, [x28,2048]
	ldrb	w0, [x21,7]
	str	w0, [x28,2048]
	mov	w0, w22
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x6, [x29,120]
	ldr	x4, [x29,112]
	ldr	x5, [x29,104]
	beq	.L1353
	add	x1, x25, :lo12:.LANCHOR0
	cmn	w20, #1
	csel	w20, w20, w0, ne
	ldrb	w1, [x1,2464]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1361
	mov	x27, 0
	mov	x24, x27
.L1353:
	add	w26, w26, 1
	add	x21, x21, 4
	cmp	w26, 26
	bne	.L1356
	b	.L1355
.L1361:
	mov	w20, w0
.L1355:
	add	x3, x5, x19
	mov	w0, 239
	str	w0, [x3,2056]
	mov	w0, 137
.L1376:
	str	w0, [x3,2052]
	add	x25, x25, :lo12:.LANCHOR0
	str	wzr, [x3,2048]
	str	wzr, [x3,2048]
	str	wzr, [x3,2048]
	str	wzr, [x3,2048]
	ldrb	w0, [x25,2464]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1357
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1357:
	cmn	w20, #1
	beq	.L1362
	cmp	w20, 256
	bne	.L1358
.L1362:
	adrp	x0, .LC94
	mov	w1, w26
	add	x0, x0, :lo12:.LC94
	mov	w2, w23
	mov	w3, w26
	mov	w4, w20
	bl	printk
.L1358:
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
	cbnz	w19, .L1378
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,73]
	ldr	w2, [x0,76]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L1378
	ldrb	w0, [x0,72]
	cmp	w0, wzr
	mov	w0, 4
	csel	w21, w21, w0, eq
.L1378:
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
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR4
	stp	x19, x20, [sp,16]
	add	x19, x23, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	adrp	x21, .LANCHOR0
	ldr	x0, [x19,1600]
	add	x20, x21, :lo12:.LANCHOR0
	str	x0, [x19,1624]
	ldrb	w0, [x19,1642]
	bl	FlashBchSel
	ldr	x0, [x19,1600]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19,1624]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	mov	w2, 32
	str	w0, [x1]
	ldr	x0, [x19,1624]
	ldrb	w1, [x20,2358]
	add	x0, x0, 16
	strh	w1, [x0,-4]
	ldrb	w1, [x20,73]
	strh	w1, [x0,-2]
	ldrb	w1, [x20,2380]
	str	w1, [x0,1060]
	add	x1, x20, 2132
	bl	ftl_memcpy
	ldr	x0, [x19,1624]
	add	x1, x20, 2360
	mov	w2, 8
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x19,1624]
	add	x1, x20, 1232
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x19,1624]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 472
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x19,1624]
	add	x1, x20, 96
	mov	w2, 32
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x19,1624]
	add	x1, x20, 1272
	mov	w2, 852
	mov	x20, x23
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x22, [x19,1624]
	mov	w1, 2036
	add	x0, x22, 12
	bl	js_hash
	str	w0, [x22,8]
	mov	w0, 1592
	str	w0, [x22,4]
	mov	w22, 0
	ldr	x0, [x19,1648]
	str	x0, [x19,1624]
	mov	w0, 0
	mov	w19, w22
	bl	flash_enter_slc_mode
.L1386:
	add	x24, x21, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w2, 0
	ldr	w1, [x24,76]
	mul	w1, w19, w1
	bl	FlashEraseBlock
	ldrb	w25, [x24,204]
	cbz	w25, .L1381
	mov	w23, 0
.L1382:
	add	x0, x21, :lo12:.LANCHOR0
	add	x2, x20, :lo12:.LANCHOR4
	mov	x3, 0
	ldr	w1, [x0,76]
	mov	w0, 0
	ldr	x2, [x2,1600]
	madd	w1, w19, w1, w23
	add	w23, w23, 1
	bl	FlashProgPage
	cmp	w23, 10
	bne	.L1382
	b	.L1383
.L1381:
	ldr	w1, [x24,76]
	add	x23, x20, :lo12:.LANCHOR4
	mov	x3, 0
	mov	w0, w25
	mul	w1, w19, w1
	ldr	x2, [x23,1600]
	bl	FlashProgPage
	ldr	w1, [x24,76]
	mov	w0, w25
	ldr	x2, [x23,1600]
	mov	x3, 0
	mul	w1, w19, w1
	add	w1, w1, 1
	bl	FlashProgPage
.L1383:
	add	x26, x21, :lo12:.LANCHOR0
	add	x23, x20, :lo12:.LANCHOR4
	mov	w0, 0
	mov	x3, 0
	add	w25, w19, 1
	ldr	w1, [x26,76]
	ldr	x2, [x23,1648]
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1384
	ldr	x24, [x23,1624]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x24]
	cmp	w1, w0
	bne	.L1384
	mov	w1, 2036
	add	x0, x24, 12
	bl	js_hash
	ldr	w1, [x24,8]
	cmp	w1, w0
	bne	.L1384
	ldr	w0, [x26,76]
	cmp	w22, 1
	str	w25, [x23,1636]
	mul	w19, w19, w0
	str	w19, [x23,1632]
	beq	.L1387
	mov	w22, 1
.L1384:
	cmp	w25, 4
	mov	w19, w25
	bne	.L1386
	b	.L1385
.L1387:
	mov	w22, 2
.L1385:
	mov	w0, 0
	bl	flash_exit_slc_mode
	cmp	w22, wzr
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
	mov	x26, x0
	mov	w0, 60
	strb	w0, [x29,120]
	mov	w0, 40
	strb	w0, [x29,121]
	mov	w0, 24
	stp	x19, x20, [sp,16]
	strb	w0, [x29,122]
	adrp	x19, .LANCHOR0
	mov	w0, 16
	strb	w0, [x29,123]
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	ldrb	w27, [x0,2464]
	ldr	w0, [x0,2376]
	cbz	w0, .L1394
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1394:
	mov	x0, x26
	mov	w1, 0
	mov	w2, 2048
	adrp	x24, .LC95
	mov	w23, -1
	mov	w20, 2
	bl	ftl_memset
	adrp	x28, .LANCHOR4
	add	x0, x24, :lo12:.LC95
	str	x0, [x29,104]
.L1395:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,73]
	cmp	w20, w0
	bcs	.L1399
	mov	x22, 0
.L1397:
	add	x0, x29, 120
	add	x24, x19, :lo12:.LANCHOR0
	add	x21, x28, :lo12:.LANCHOR4
	ldrb	w25, [x22,x0]
	mov	w0, w25
	bl	FlashBchSel
	ldr	w1, [x24,76]
	mov	w0, 0
	ldr	x2, [x21,1600]
	mov	x3, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1396
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L1397
	b	.L1398
.L1396:
	ldr	x0, [x21,1600]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L1398
	ldr	x0, [x29,104]
	mov	w1, w25
	bl	printk
	ldr	x1, [x21,1600]
	mov	x0, x26
	mov	w2, 2048
	bl	ftl_memcpy
	ldr	x0, [x21,1600]
	ldr	w0, [x0,512]
	strb	w0, [x24,73]
	ldr	w0, [x21,1636]
	cmp	w0, w20
	bls	.L1402
	mov	w23, 0
	str	w20, [x21,1636]
	bl	FlashSavePhyInfo
.L1398:
	add	w20, w20, 1
	b	.L1395
.L1402:
	mov	w23, 0
.L1399:
	mov	w0, w27
	add	x19, x19, :lo12:.LANCHOR0
	bl	FlashBchSel
	ldr	w0, [x19,2376]
	cbz	w0, .L1406
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1406:
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
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR4
	add	x22, x21, :lo12:.LANCHOR4
	stp	x23, x24, [sp,48]
	mov	x23, x0
	mov	w0, 32768
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	bl	ftl_malloc
	str	x0, [x22,1600]
	mov	w0, 32768
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x24, .LC96
	bl	ftl_malloc
	str	x0, [x22,1648]
	mov	w0, 4096
	mov	w27, 144
	mov	w28, 239
	bl	ftl_malloc
	str	x0, [x22,1656]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x22,1664]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x22,1672]
	mov	w0, 128
	str	w0, [x20,76]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x22,1642]
	mov	x0, x23
	strb	wzr, [x20,2380]
	add	x23, x20, 8
	strb	w1, [x20,73]
	strb	w1, [x22,1640]
	str	wzr, [x22,1636]
	str	wzr, [x22,1608]
	strb	wzr, [x20,72]
	add	x20, x20, 2132
	strb	wzr, [x22,1680]
	bl	NandcInit
	add	x0, x24, :lo12:.LC96
	mov	w22, 0
	str	x0, [x29,104]
	mov	x26, x20
.L1418:
	ldrb	w2, [x23,8]
	uxtb	w25, w22
	mov	w0, w25
	str	x2, [x29,96]
	ldr	x24, [x23]
	bl	FlashReset
	mov	w0, w25
	bl	NandcFlashCs
	ldr	x2, [x29,96]
	mov	x0, 1000
	ubfiz	x2, x2, 8, 8
	add	x24, x24, x2
	str	w27, [x24,2056]
	str	wzr, [x24,2052]
	bl	__const_udelay
	ldr	w0, [x24,2048]
	uxtb	w0, w0
	strb	w0, [x20]
	cmp	w0, 44
	ldr	w2, [x24,2048]
	strb	w2, [x20,1]
	ldr	w2, [x24,2048]
	strb	w2, [x20,2]
	ldr	w2, [x24,2048]
	strb	w2, [x20,3]
	ldr	w2, [x24,2048]
	strb	w2, [x20,4]
	ldr	w2, [x24,2048]
	strb	w2, [x20,5]
	bne	.L1413
	str	w28, [x24,2056]
	mov	w0, 1
	str	w0, [x24,2052]
	mov	x0, 1000
	bl	__const_udelay
	mov	w0, 4
	str	w0, [x24,2048]
	str	wzr, [x24,2048]
	str	wzr, [x24,2048]
	str	wzr, [x24,2048]
.L1413:
	mov	w0, w25
	bl	NandcFlashDeCs
	ldrb	w2, [x20]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1414
	ldr	x0, [x29,104]
	add	w1, w22, 1
	ldrb	w3, [x20,1]
	ldrb	w4, [x20,2]
	ldrb	w5, [x20,3]
	ldrb	w6, [x20,4]
	ldrb	w7, [x20,5]
	bl	printk
.L1414:
	cbnz	w22, .L1415
	ldrb	w0, [x26]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1470
	ldrb	w0, [x26,1]
	cmp	w0, 255
	beq	.L1470
.L1415:
	ldrb	w0, [x20]
	cmp	w0, 181
	bne	.L1417
	mov	w0, 44
	strb	w0, [x20]
.L1417:
	add	w22, w22, 1
	add	x23, x23, 16
	cmp	w22, 4
	add	x20, x20, 8
	bne	.L1418
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,2132]
	cmp	w1, 173
	beq	.L1419
	ldr	w0, [x0,2384]
	bl	NandcSetDdrMode
.L1419:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w2, 852
	add	x0, x20, 1272
	bl	ftl_memset
	strb	wzr, [x20,80]
	adrp	x1, .LANCHOR1
	ldr	w4, [x20,2392]
	mov	w2, 12336
	add	x0, x1, :lo12:.LANCHOR1
	movk	w2, 0x5638, lsl 16
	add	x0, x0, 472
	cmp	w4, w2
	str	x0, [x20,88]
	bne	.L1420
	ldrb	w0, [x0,19]
	cmp	w0, 50
	beq	.L1420
	mov	w0, 1
	str	w0, [x20,2376]
.L1420:
	add	x3, x19, :lo12:.LANCHOR0
	add	x3, x3, 2132
	ldrb	w0, [x3,1]
	cmp	w0, 218
	cset	w6, eq
	cmp	w0, 241
	cset	w5, eq
	cmp	w0, 161
	cset	w2, eq
	orr	w2, w5, w2
	orr	w2, w6, w2
	cbnz	w2, .L1421
	and	w2, w0, -3
	cmp	w2, 209
	beq	.L1421
	cmp	w0, 220
	bne	.L1422
	ldrb	w2, [x3,3]
	cmp	w2, 149
	bne	.L1422
.L1421:
	add	x2, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR4
	mov	w7, 16
	mov	w5, 1
	add	x8, x2, 2132
	strb	w7, [x2,73]
	strb	w5, [x2,72]
	strb	w7, [x3,1642]
	ldrb	w7, [x2,2132]
	add	x2, x1, :lo12:.LANCHOR1
	cmp	w7, 152
	strb	w7, [x2,3425]
	strb	w0, [x2,3426]
	bne	.L1424
	ldrsb	w2, [x8,4]
	tbnz	w2, #31, .L1425
	mov	w2, 24
	strb	w2, [x3,1642]
.L1424:
	cmp	w4, 2049
	beq	.L1473
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L1427
.L1473:
	add	x2, x21, :lo12:.LANCHOR4
	mov	w3, 16
	strb	w3, [x2,1642]
.L1427:
	cbz	w6, .L1429
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,3438]
	mov	w2, -38
	b	.L1517
.L1429:
	cmp	w0, 220
	bne	.L1431
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3438]
	mov	w2, -36
.L1517:
	strb	w2, [x0,3426]
	b	.L1430
.L1431:
	cmp	w0, 211
	bne	.L1430
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3438]
	mov	w2, 2
	strb	w2, [x0,3437]
.L1430:
	add	x20, x1, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x20, 3296
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	add	x0, x20, 472
	add	x1, x20, 3424
	mov	w2, 32
	bl	ftl_memcpy
.L1422:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,72]
	cbnz	w0, .L1432
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L1434
	ldr	x0, [x20,88]
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x20,2357]
	tbnz	x1, 0, .L1434
	mov	w1, 1
	strb	w1, [x20,2380]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,2357]
	bl	NandcSetMode
.L1434:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,88]
	ldrb	w0, [x0,26]
	strb	w0, [x20,204]
	bl	FlashLoadPhyInfo
	cbz	w0, .L1432
	ldr	w0, [x20,2384]
	cbz	w0, .L1437
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L1514
.L1437:
	ldrb	w0, [x20,2357]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,2357]
.L1514:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L1432
	mov	w0, 1
	add	x20, x19, :lo12:.LANCHOR0
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x1, [x20,88]
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L1488
	bl	FlashDieInfoInit
	ldr	x0, [x20,88]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	adrp	x1, .LANCHOR2+3558
	ldr	x0, [x20,88]
	ldrh	w1, [x1,#:lo12:.LANCHOR2+3558]
	ldrb	w2, [x0,9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L1439
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L1440
.L1439:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L1440:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2357]
	tst	w0, 6
	beq	.L1441
	bl	FlashSavePhyInfo
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x1, x21, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x1,1632]
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1441:
	bl	FlashSavePhyInfo
.L1432:
	add	x20, x19, :lo12:.LANCHOR0
	add	x22, x21, :lo12:.LANCHOR4
	ldr	x2, [x20,88]
	str	xzr, [x22,1616]
	ldrb	w0, [x2,26]
	strb	w0, [x20,204]
	ldrh	w0, [x2,16]
	ldrh	w3, [x2,10]
	ubfx	x1, x0, 7, 1
	strb	w1, [x20,80]
	ubfx	x1, x0, 3, 1
	strb	w1, [x22,1681]
	ubfx	x1, x0, 4, 1
	strb	w1, [x20,2368]
	ldrb	w1, [x2,12]
	ubfx	x0, x0, 8, 3
	strb	w0, [x20,2357]
	ldrb	w0, [x2,18]
	sdiv	w1, w3, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x20,88]
	ldrh	w1, [x0,16]
	tbz	x1, 6, .L1443
	ldrb	w0, [x0,19]
	ldrb	w1, [x20,1274]
	strb	w1, [x22,1641]
	sub	w1, w0, #1
	ldrb	w2, [x20,1273]
	uxtb	w1, w1
	strb	w0, [x20,128]
	strb	w2, [x20,129]
	cmp	w1, 7
	bhi	.L1444
	adrp	x1, HynixReadRetrial
	cmp	w0, 8
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,1616]
	sub	w1, w0, #5
	cset	w2, eq
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1474
	cbz	w2, .L1445
.L1474:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 1
	str	w3, [x1,2456]
.L1445:
	cmp	w0, 7
	add	x1, x19, :lo12:.LANCHOR0
	beq	.L1472
	cbnz	w2, .L1472
	add	x1, x1, 1292
	b	.L1448
.L1472:
	add	x1, x1, 1300
.L1448:
	mov	x2, 0
	mov	w3, w2
.L1450:
	ldrsb	w4, [x1,x2]
	add	x2, x2, 1
	cmp	w4, wzr
	csinc	w3, w3, w3, ne
	cmp	x2, 32
	bne	.L1450
	cmp	w3, 27
	bls	.L1443
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1443
.L1444:
	sub	w1, w0, #17
	uxtb	w1, w1
	cmp	w1, 2
	bhi	.L1452
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x22,1616]
	beq	.L1453
	mov	w0, 7
	b	.L1515
.L1453:
	mov	w0, 15
.L1515:
	strb	w0, [x22,1641]
	b	.L1443
.L1452:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1475
	cmp	w0, 33
	bne	.L1454
.L1475:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	mov	w2, 4
	str	x1, [x0,1616]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,129]
	mov	w1, 7
	strb	w1, [x0,1641]
	b	.L1443
.L1454:
	sub	w1, w0, #67
	sub	w2, w0, #34
	uxtb	w1, w1
	uxtb	w2, w2
	cmp	w1, 1
	cset	w1, ls
	cmp	w2, 1
	bls	.L1476
	cbz	w1, .L1456
.L1476:
	add	x3, x21, :lo12:.LANCHOR4
	adrp	x2, ToshibaReadRetrial
	add	x2, x2, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x2, [x3,1616]
	beq	.L1458
	cmp	w0, 68
	beq	.L1458
	mov	w0, 7
	strb	w0, [x3,1641]
	b	.L1459
.L1458:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w2, 17
	strb	w2, [x0,1641]
.L1459:
	add	x0, x19, :lo12:.LANCHOR0
	cbz	w1, .L1460
	mov	w1, 4
	b	.L1516
.L1460:
	mov	w1, 5
.L1516:
	strb	w1, [x0,129]
	b	.L1443
.L1456:
	cmp	w0, 49
	bne	.L1461
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,1616]
	b	.L1443
.L1461:
	cmp	w0, 50
	bne	.L1443
	adrp	x0, samsung_read_retrial
	str	w1, [x20,2376]
	add	x0, x0, :lo12:samsung_read_retrial
	str	x0, [x22,1616]
.L1443:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ldr	w2, [x0,2392]
	cmp	w2, w1
	bne	.L1462
	ldrb	w1, [x0,204]
	cbz	w1, .L1462
	ldr	x0, [x0,88]
	strb	wzr, [x0,18]
.L1462:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,2132]
	cmp	w1, 44
	bne	.L1463
	ldrb	w1, [x0,2380]
	cbz	w1, .L1463
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L1464
	ldrb	w0, [x0,204]
	cbnz	w0, .L1463
.L1464:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,2380]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L1463:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w0, [x1,2357]
	tst	w0, 6
	beq	.L1465
	ldrb	w1, [x1,2380]
	cbnz	w1, .L1466
	tbnz	x0, 0, .L1465
.L1466:
	mov	w0, 0
	add	x21, x21, :lo12:.LANCHOR4
	bl	flash_enter_slc_mode
	ldr	w1, [x21,1632]
	mov	w0, 0
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1465:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 16
	ldr	x0, [x19,88]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 3896
	bl	FlashReadIdbDataRaw
	strb	w20, [x19,73]
	mov	w0, 18928
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	ldr	x1, [x19,88]
	ldrb	w2, [x1,12]
	strh	w2, [x19,184]
	ldrh	w4, [x1,14]
	ldrb	w2, [x1,7]
	str	w2, [x19,180]
	ldrb	w2, [x19,2133]
	lsl	w3, w2, w20
	orr	w2, w3, w2, lsl 8
	ldrb	w3, [x19,2132]
	orr	w2, w2, w3
	ldrb	w3, [x19,2135]
	orr	w2, w2, w3, lsl 24
	str	w2, [x19,176]
	ldrb	w2, [x19,2358]
	strh	w2, [x19,186]
	ldrb	w2, [x1,13]
	strh	w2, [x19,188]
	strh	w4, [x19,190]
	ldrh	w2, [x1,10]
	strh	w2, [x19,192]
	ldrb	w3, [x1,12]
	ldrh	w2, [x1,10]
	sdiv	w2, w2, w3
	strh	w2, [x19,194]
	ldrb	w3, [x1,9]
	strh	w3, [x19,196]
	ldrh	w5, [x1,10]
	ldrb	w2, [x1,9]
	mul	w2, w2, w5
	mov	w5, 512
	strh	w5, [x19,200]
	ldrb	w5, [x19,73]
	strh	w5, [x19,202]
	uxth	w2, w2
	ldrb	w5, [x19,72]
	strh	w2, [x19,198]
	cmp	w5, 1
	bne	.L1468
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,198]
	strb	w20, [x19,73]
	mov	w2, 8
	strh	w4, [x19,190]
	strh	w3, [x19,196]
	strh	w2, [x19,202]
.L1468:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
	b	.L1488
.L1470:
	mov	w0, -2
	b	.L1488
.L1425:
	strb	w5, [x3,1680]
	b	.L1424
.L1488:
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
	stp	x25, x26, [sp,64]
	mov	x23, x2
	ldr	x3, [x0,88]
	ldrb	w4, [x0,204]
	ldrb	w3, [x3,19]
	cbz	w4, .L1519
	ldr	w0, [x0,2376]
	cbnz	w0, .L1518
.L1519:
	sub	w2, w3, #5
	uxtb	w2, w2
	cmp	w2, 2
	bls	.L1520
	cmp	w3, 50
	beq	.L1520
	sub	w2, w3, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbz	w2, .L1520
	cmp	w3, 68
	bne	.L1518
.L1520:
	adrp	x20, .LANCHOR2
	mov	w24, w1
	add	x20, x20, :lo12:.LANCHOR2
	mov	w25, 65535
	add	x20, x20, 1508
	adrp	x26, .LANCHOR4
.L1523:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,88]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L1518
	ldrh	w0, [x20,w19,sxtw 1]
	cmp	w0, w25
	bne	.L1518
	add	x22, x26, :lo12:.LANCHOR4
	mov	w1, 255
	mov	w2, 32768
	ldr	x0, [x22,1648]
	bl	ftl_memset
	ldr	x2, [x22,1648]
	add	w1, w19, w24
	mov	w0, w21
	add	w19, w19, 1
	mov	x3, x2
	uxth	w19, w19
	bl	FlashProgPage
	b	.L1523
.L1518:
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
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	mov	w24, w1
	stp	x19, x20, [sp,16]
	adrp	x1, .LANCHOR1+481
	mov	x19, x0
	add	x0, x23, :lo12:.LANCHOR0
	str	x0, [x29,120]
	adrp	x0, .LC98
	str	w2, [x29,116]
	add	x0, x0, :lo12:.LC98
	str	x0, [x29,104]
	stp	x25, x26, [sp,64]
	adrp	x0, .LC100
	ldrb	w25, [x1,#:lo12:.LANCHOR1+481]
	stp	x21, x22, [sp,32]
	add	x0, x0, :lo12:.LC100
	mov	w22, 0
	stp	x27, x28, [sp,80]
	str	x0, [x29,96]
.L1535:
	cmp	w22, w24
	beq	.L1599
	ldr	w1, [x29,116]
	sub	w4, w24, w22
	mov	x0, x19
	add	x2, x29, 140
	add	x3, x29, 136
	adrp	x21, .LANCHOR0
	bl	LogAddr2PhyAddr
	ldr	x1, [x29,120]
	ldr	w0, [x29,136]
	ldrb	w1, [x1,2358]
	cmp	w0, w1
	bcc	.L1536
	mov	w0, -1
	str	w0, [x19]
	b	.L1537
.L1536:
	ldr	x1, [x29,120]
	add	x0, x1, x0, uxtw
	ldrb	w20, [x0,2360]
	mov	w23, 0
	adrp	x27, .LANCHOR4
	mov	w28, 256
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashCs
.L1538:
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
	add	x0, x27, :lo12:.LANCHOR4
	ldrb	w0, [x0,1680]
	cbz	w0, .L1539
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w26, w26, w28, ls
.L1539:
	cmn	w26, #1
	bne	.L1540
	cmp	w23, 9
	bhi	.L1540
	add	w23, w23, 1
	b	.L1538
.L1540:
	cmp	w23, wzr
	mov	w27, 256
	csel	w26, w26, w27, eq
	mov	w23, 0
.L1542:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29,140]
	ldr	w2, [x0,76]
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
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1680]
	cbz	w0, .L1545
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w28, w28, w27, ls
.L1545:
	cmn	w28, #1
	bne	.L1546
	cmp	w23, 9
	bhi	.L1546
	add	w23, w23, 1
	b	.L1542
.L1546:
	cmp	w23, wzr
	mov	w0, w20
	mov	w27, 256
	csel	w28, w28, w27, eq
	bl	NandcFlashDeCs
	add	x0, x21, :lo12:.LANCHOR0
	cmp	w28, w26
	csel	w28, w28, w26, cs
	ldrb	w0, [x0,2464]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1548
	cmn	w28, #1
	csel	w28, w28, w27, eq
.L1548:
	cmp	w28, 256
	beq	.L1559
	cmn	w28, #1
	bne	.L1549
.L1559:
	str	w28, [x19]
	b	.L1551
.L1549:
	str	wzr, [x19]
.L1551:
	ldr	x0, [x19,16]
	cbz	x0, .L1552
	ldr	w1, [x0,12]
	cmn	w1, #1
	bne	.L1552
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L1552
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1552
	str	w1, [x19]
.L1552:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1537
	add	x21, x21, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	ldr	x0, [x29,104]
	ldrb	w2, [x21,2464]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L1554
	adrp	x0, .LC99
	mov	w2, 4
	add	x0, x0, :lo12:.LC99
	mov	w3, 8
	bl	rknand_print_hex
.L1554:
	ldr	x1, [x19,16]
	cbz	x1, .L1537
	mov	w2, 4
	ldr	x0, [x29,96]
	mov	w3, w2
	bl	rknand_print_hex
.L1537:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L1535
.L1599:
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
	stp	x29, x30, [sp, -176]!
	adrp	x3, .LANCHOR1+481
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	ldrb	w3, [x3,#:lo12:.LANCHOR1+481]
	adrp	x22, .LANCHOR0
	str	w3, [x29,148]
	add	x3, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	str	w1, [x29,144]
	mov	x26, x0
	ldrb	w4, [x3,80]
	ldrb	w23, [x3,72]
	str	w2, [x29,140]
	str	w4, [x29,136]
	cbz	w23, .L1640
	bl	FlashReadSlc2KPages
	b	.L1689
.L1640:
	adrp	x0, .LC98
	mov	w24, w23
	add	x0, x0, :lo12:.LC98
	str	x0, [x29,128]
	adrp	x0, .LC100
	add	x0, x0, :lo12:.LC100
	str	x0, [x29,120]
	adrp	x0, .LC101
	add	x0, x0, :lo12:.LC101
	str	x0, [x29,112]
.L1601:
	ldr	w0, [x29,144]
	cmp	w24, w0
	bcs	.L1691
	mov	w27, 56
	ldr	w0, [x29,144]
	ldr	w1, [x29,140]
	add	x2, x29, 172
	umull	x27, w24, w27
	sub	w4, w0, w24
	add	x3, x29, 168
	add	x21, x26, x27
	mov	x0, x21
	ldr	w28, [x21,4]
	bl	LogAddr2PhyAddr
	add	x1, x22, :lo12:.LANCHOR0
	mov	w20, w0
	ldr	w0, [x29,168]
	ldrb	w2, [x1,2358]
	cmp	w0, w2
	bcc	.L1603
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L1604
.L1603:
	add	x0, x1, x0, uxtw
	adrp	x25, .LANCHOR4
	ldrb	w19, [x0,2360]
	add	x0, x25, :lo12:.LANCHOR4
	str	x1, [x29,152]
	ldrb	w0, [x0,1681]
	cmp	w0, wzr
	mov	w0, w19
	csel	w20, w20, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x1, [x29,152]
	ldr	x0, [x1,88]
	ldrb	w0, [x0,19]
	sub	w2, w0, #1
	uxtb	w2, w2
	cmp	w2, 7
	bhi	.L1606
	sub	w0, w0, #7
	add	x1, x1, 1272
	sxtw	x4, w19
	add	x1, x1, x4
	uxtb	w0, w0
	cmp	w0, 1
	ldrb	w3, [x1,12]
	bhi	.L1607
	ldrb	w3, [x1,20]
.L1607:
	add	x2, x22, :lo12:.LANCHOR0
	add	x4, x2, x4
	ldrb	w0, [x4,2128]
	cmp	w0, w3
	beq	.L1606
	ldrb	w1, [x2,1273]
	mov	w0, w19
	add	x2, x2, 1276
	bl	HynixSetRRPara
.L1606:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,140]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,152]
	cbz	w0, .L1608
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbz	w0, .L1608
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1609
.L1608:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1609:
	cmp	w19, 255
	ldr	w1, [x29,172]
	bne	.L1647
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1641
.L1647:
	cbz	w20, .L1613
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x0,76]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1614
.L1613:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1614
.L1641:
	mov	w20, w0
.L1611:
	ldrb	w2, [x29,148]
	mov	w0, w19
	ldr	x3, [x21,8]
	mov	w1, 0
	ldr	x4, [x21,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1615
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0,80]
	cbz	w1, .L1615
	strb	wzr, [x0,80]
	mov	w20, 0
	b	.L1609
.L1615:
	cbz	w20, .L1616
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x29,172]
	ldr	w2, [x0,76]
	mov	w0, w19
	add	w1, w2, w1
	bl	FlashReadDpDataOutCmd
	add	w0, w24, 1
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
.L1616:
	mov	w0, w19
	bl	NandcFlashDeCs
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w0, [x29,136]
	cmn	w28, #1
	strb	w0, [x6,80]
	bne	.L1623
	ldrb	w0, [x6,2380]
	cbnz	w0, .L1618
.L1622:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x20, [x0,1616]
	cbnz	x20, .L1619
	b	.L1692
.L1618:
	ldr	x0, [x6,136]
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
	beq	.L1621
	ldr	x6, [x29,104]
	ldrb	w0, [x6,2464]
	cmp	w28, w0, lsr 1
	bls	.L1643
.L1621:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1622
	b	.L1643
.L1619:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L1624
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,88]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L1625
	ldrb	w1, [x2,1273]
	mov	w0, w19
	add	x2, x2, 1276
	mov	w3, w20
	bl	HynixSetRRPara
.L1625:
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
	ldrb	w2, [x4,2464]
	bl	printk
	cmn	w28, #1
	ldr	x4, [x29,104]
	bne	.L1624
	ldrb	w0, [x4,204]
	cbz	w0, .L1624
	ldr	w0, [x29,152]
	cbz	w0, .L1626
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1627
.L1626:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1627:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	mov	w20, 0
	ldr	x3, [x21,16]
	ldr	x4, [x0,1616]
	mov	w0, w19
	blr	x4
	mov	w28, w0
	b	.L1624
.L1692:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1624
.L1643:
	mov	w20, 0
.L1623:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,2464]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1624
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x0, [x0,1616]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1624:
	cmp	w28, 256
	beq	.L1648
	cmn	w28, #1
	bne	.L1628
.L1648:
	str	w28, [x26,x27]
	b	.L1630
.L1628:
	str	wzr, [x26,x27]
.L1630:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L1632
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x21,4]
	ldr	x0, [x29,128]
	ldrb	w2, [x2,2464]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L1632
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	bl	rknand_print_hex
.L1632:
	cbz	w20, .L1634
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,2464]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L1635
	add	x25, x25, :lo12:.LANCHOR4
	ldr	x0, [x25,1616]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L1635:
	add	w0, w24, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L1649
	cmn	w23, #1
	bne	.L1636
.L1649:
	str	w23, [x26,x0]
	b	.L1634
.L1636:
	str	wzr, [x26,x0]
.L1634:
	ldr	w0, [x29,152]
	add	w24, w24, w20
	cbz	w0, .L1604
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbz	w0, .L1604
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1604:
	add	w24, w24, 1
	b	.L1601
.L1691:
	mov	w0, 0
	b	.L1689
.L1614:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L1611
	ldr	w1, [x29,172]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L1611
.L1689:
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
	adrp	x23, .LANCHOR4
	mov	w27, -1
	ldrh	w1, [x0,188]
	add	x19, x23, :lo12:.LANCHOR4
	ldrh	w21, [x0,190]
	add	x0, x19, 1688
	mov	w28, 0
	adrp	x26, .LC102
	add	x26, x26, :lo12:.LC102
	mul	w21, w1, w21
	mov	w1, 0
	uxth	w21, w21
	bl	ftl_memset
	add	w25, w21, w27
	ldr	x0, [x19,1656]
	str	x0, [x29,152]
	mov	w19, w28
	sub	w0, w21, #12
	str	xzr, [x29,144]
	uxth	w25, w25
	str	w0, [x29,124]
.L1694:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,2358]
	cmp	w1, w19
	bls	.L1704
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1695:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1697
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
	beq	.L1696
	add	x24, x23, :lo12:.LANCHOR4
	ldr	x0, [x24,1656]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1696
	add	x24, x24, 1688
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	bl	printk
	strh	w20, [x24,w19,sxtw 1]
	uxth	w28, w28
	b	.L1697
.L1696:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1695
.L1697:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,2358]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1694
.L1704:
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
	.global	FlashProgSlc2KPages
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	mov	w23, w1
	adrp	x1, .LANCHOR1+481
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	adrp	x22, .LANCHOR0
	ldrb	w26, [x1,#:lo12:.LANCHOR1+481]
	stp	x19, x20, [sp,16]
	str	x27, [sp,80]
	mov	w24, w2
	mov	x19, x0
	mov	x21, x0
	mov	w25, 0
	add	x22, x22, :lo12:.LANCHOR0
	mov	w27, -1
.L1706:
	cmp	w25, w23
	beq	.L1732
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,2358]
	cmp	w0, w1
	bcc	.L1707
	str	w27, [x21]
	b	.L1708
.L1707:
	add	x0, x22, x0, uxtw
	ldrb	w20, [x0,2360]
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
	ldr	w2, [x22,76]
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
	ldr	w2, [x0,76]
	mov	w0, w20
	add	w1, w2, w1
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	w1, [x29,96]
	mov	w0, w20
	bl	FlashReadStatus
	tbz	x0, 0, .L1711
	str	w27, [x21]
.L1711:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1708:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1706
.L1732:
	adrp	x25, .LC107
	adrp	x26, .LC106
	adrp	x27, .LC105
	mov	w21, 0
	add	x25, x25, :lo12:.LC107
	add	x26, x26, :lo12:.LC106
	add	x27, x27, :lo12:.LC105
.L1713:
	cmp	w21, w23
	beq	.L1733
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1714
	adrp	x0, .LC103
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC103
	bl	printk
	b	.L1715
.L1714:
	adrp	x20, .LANCHOR4
	sub	w4, w23, w21
	add	x22, x20, :lo12:.LANCHOR4
	add	x3, x29, 100
	mov	w1, w24
	add	x2, x29, 96
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,1664]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,1672]
	str	wzr, [x0]
	add	x0, x29, 104
	bl	memcpy
	ldr	x0, [x22,1664]
	mov	w1, 1
	str	x0, [x29,112]
	mov	w2, w24
	ldr	x0, [x22,1672]
	str	x0, [x29,120]
	add	x0, x29, 104
	bl	FlashReadPages
	ldr	w22, [x29,104]
	cmn	w22, #1
	bne	.L1716
	adrp	x0, .LC104
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC104
	bl	printk
	str	w22, [x19]
.L1716:
	ldr	w22, [x29,104]
	cmp	w22, 256
	bne	.L1717
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1717:
	ldr	x0, [x19,16]
	cbz	x0, .L1718
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,1672]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1718
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1718:
	ldr	x0, [x19,8]
	cbz	x0, .L1715
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,1664]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1715
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1715:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1713
.L1733:
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
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	stp	x29, x30, [sp, -80]!
	adrp	x0, .LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	add	x19, x19, 1704
	adrp	x22, .LANCHOR0
	ldr	x1, [x0,3616]
	add	x21, x22, :lo12:.LANCHOR0
	ldr	x24, [x0,3664]
	add	x21, x21, 2624
	str	x1, [x19,8]
	mov	w23, 0
	str	x24, [x19,16]
	mov	w25, -1
	mov	w26, 61664
.L1735:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,2494]
	cmp	w23, w1
	bcs	.L1744
	ldrh	w20, [x0,2542]
	strh	w25, [x21,12]
.L1737:
	add	x0, x22, :lo12:.LANCHOR0
	sub	w20, w20, #1
	uxth	w20, w20
	ldrh	w0, [x0,2542]
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1738
	madd	w0, w23, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1737
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1737
	strh	w20, [x21,12]
.L1738:
	add	w23, w23, 1
	add	x21, x21, 2
	b	.L1735
.L1744:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	stp	x29, x30, [sp, -208]!
	cmp	w1, 1
	adrp	x3, .LANCHOR0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x25, [sp,64]
	mov	w23, w1
	add	x3, x3, :lo12:.LANCHOR0
	bne	.L1746
	ldrh	w19, [x3,2546]
	b	.L1747
.L1746:
	ldrh	w19, [x3,2544]
.L1747:
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
	bne	.L1748
	mov	w22, 0
	mov	w25, 2
.L1749:
	cmp	w22, w19
	bgt	.L1748
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
	bne	.L1750
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1750
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1750
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1749
.L1750:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1749
.L1748:
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR2
	adrp	x23, .LANCHOR4
	add	x0, x24, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	add	x19, x23, :lo12:.LANCHOR4
	adrp	x20, .LANCHOR0
	ldr	x1, [x0,3616]
	add	x19, x19, 1704
	ldr	x22, [x0,3664]
	mov	w25, 61649
	str	x1, [x19,8]
	str	x22, [x19,16]
	bl	FtlBbtMemInit
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w21, [x0,2542]
	sub	w21, w21, #1
	uxth	w21, w21
.L1757:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2542]
	sub	w0, w0, #48
	cmp	w21, w0
	ble	.L1760
	lsl	w0, w21, 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1758
	ldr	w0, [x19,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
.L1758:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1759
	ldrh	w0, [x22]
	cmp	w0, w25
	bne	.L1759
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2624
	strh	w21, [x1,2624]
	ldr	w1, [x22,4]
	str	w1, [x0,8]
	ldrh	w1, [x22,8]
	strh	w1, [x0,4]
	b	.L1760
.L1759:
	sub	w21, w21, #1
	uxth	w21, w21
	b	.L1757
.L1760:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,2624]
	cmp	w1, w2
	beq	.L1762
	ldrh	w1, [x21,2628]
	cmp	w1, w2
	beq	.L1764
	add	x25, x23, :lo12:.LANCHOR4
	lsl	w1, w1, 10
	add	x0, x25, 1704
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x25,1704]
	cmn	w0, #1
	beq	.L1764
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1764
	ldr	w0, [x22,4]
	ldr	w1, [x21,2632]
	cmp	w0, w1
	bls	.L1764
	str	w0, [x21,2632]
	ldrh	w1, [x21,2628]
	ldrh	w0, [x22,8]
	strh	w1, [x21,2624]
	strh	w0, [x21,2628]
.L1764:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 1
	add	x21, x23, :lo12:.LANCHOR4
	mov	w26, 61649
	add	x21, x21, 1704
	ldrh	w0, [x25,2624]
	bl	FtlGetLastWrittenPage
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x25,2626]
.L1766:
	tbnz	w19, #31, .L1771
	ldrh	w0, [x25,2624]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w19, w0, lsl 10
	str	w0, [x21,4]
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,3616]
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1767
	ldrh	w0, [x22]
	cmp	w0, w26
	bne	.L1767
.L1771:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,2630]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1768
	b	.L1769
.L1767:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1766
.L1768:
	ldr	w2, [x1,2468]
	cmp	w0, w2
	beq	.L1769
	ldrh	w1, [x1,2482]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1769
	cmp	w2, w1
	bcs	.L1769
	bl	FtlSysBlkNumInit
.L1769:
	add	x20, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x21, x20, 2624
	add	x23, x23, :lo12:.LANCHOR4
.L1772:
	ldrh	w0, [x20,2494]
	mov	w1, w19
	add	x19, x19, 1
	cmp	w1, w0
	bcs	.L1790
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x3, [x23,1712]
	ldrh	w2, [x0,3792]
	add	x0, x21, x19, lsl 3
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1772
.L1790:
	mov	w0, 0
.L1762:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp,32]
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR2
	add	x21, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	strh	wzr, [x21,3864]
	mov	x24, x22
	ldr	w2, [x20,2572]
	mov	w22, 65535
	ldr	x0, [x21,3776]
	lsl	w2, w2, 2
	strh	wzr, [x20,2590]
	bl	ftl_memset
	ldr	w2, [x20,2572]
	mov	w1, 0
	ldr	x0, [x21,3720]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2564]
	mov	w1, 0
	ldr	x0, [x21,3744]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2564]
	mov	w1, 0
	ldr	x0, [x20,2592]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x21, 784
	mov	w1, 255
	mov	w2, 16
	bl	ftl_memset
	ldrh	w21, [x20,2480]
.L1792:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,2482]
	cmp	w0, w21
	bls	.L1833
	mov	x27, 0
	ldrh	w5, [x1,2472]
	ldrh	w4, [x1,2556]
	mov	w20, w27
	mov	x23, x1
.L1834:
	cmp	w5, w27, uxth
	bls	.L1867
	add	x0, x23, 2504
	mov	w1, w21
	str	x4, [x29,112]
	str	x5, [x29,120]
	ldrb	w0, [x0,x27]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x5, [x29,120]
	ldr	x4, [x29,112]
	cbnz	w0, .L1793
	add	x2, x24, :lo12:.LANCHOR2
	mov	w0, 56
	lsl	w28, w28, 10
	mov	w3, 4
	umull	x0, w20, w0
	ldr	x1, [x2,3592]
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x2,3592]
	add	x0, x1, x0
	ldr	x1, [x2,1400]
	ldr	x2, [x2,1408]
	str	x1, [x0,8]
	mul	w1, w20, w4
	add	w20, w20, 1
	sdiv	w1, w1, w3
	uxth	w20, w20
	add	x1, x2, x1, sxtw 2
	str	x1, [x0,16]
.L1793:
	add	x27, x27, 1
	b	.L1834
.L1867:
	cbz	w20, .L1796
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	mov	w28, 61604
	ldr	x0, [x0,3592]
	bl	FlashReadPages
.L1797:
	cmp	w20, w23, uxth
	bls	.L1796
	mov	x0, 56
	mul	x6, x23, x0
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,3592]
	add	x1, x0, x6
	ldr	w0, [x0,x6]
	ldr	w5, [x1,4]
	cmn	w0, #1
	ldr	x26, [x1,16]
	ubfx	x25, x5, 10, 16
	bne	.L1799
	mov	w7, 16
.L1798:
	add	x8, x24, :lo12:.LANCHOR2
	str	x7, [x29,104]
	str	x8, [x29,112]
	str	x6, [x29,120]
	ldr	x0, [x8,3592]
	add	x0, x0, x6
	ldr	w1, [x0,4]
	add	w1, w1, 1
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x0, [x8,3592]
	mov	w2, w1
	add	x0, x0, x6
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	x8, [x29,112]
	cmp	w0, w22
	ldr	x6, [x29,120]
	ldr	x7, [x29,104]
	ldr	x0, [x8,3592]
	bne	.L1800
	str	w27, [x0,x6]
	b	.L1799
.L1800:
	ldr	w0, [x0,x6]
	cmn	w0, #1
	bne	.L1799
	sub	w7, w7, #1
	uxth	w7, w7
	cbnz	w7, .L1798
.L1799:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x1, [x0,3592]
	ldr	w1, [x1,x6]
	cmn	w1, #1
	beq	.L1802
	ldr	w0, [x0,752]
	cmn	w0, #1
	beq	.L1803
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1804
.L1803:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1804
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,752]
.L1804:
	ldrh	w0, [x26]
	cmp	w0, w28
	beq	.L1806
	bhi	.L1807
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1805
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldrh	w6, [x0,2564]
	ldrh	w0, [x0,2590]
	sub	w2, w6, #1
	ldr	x7, [x7,3744]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1820
.L1807:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1809
	cmp	w0, w22
	bne	.L1805
	mov	w0, w25
	b	.L1866
.L1809:
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldr	w6, [x0,2572]
	ldrh	w0, [x7,3864]
	ldr	x7, [x7,3776]
	uxth	w2, w6
	sub	w1, w2, #1
	sub	w2, w2, w0
	sub	w2, w2, #1
	sxth	x1, w1
	sxth	w2, w2
.L1811:
	cmp	w1, w2
	ble	.L1868
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bls	.L1812
	ldr	w2, [x7]
	cbnz	w2, .L1813
	cmp	w0, w6
	beq	.L1813
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,3864]
.L1813:
	mov	w0, 0
.L1814:
	cmp	w0, w1
	beq	.L1869
	add	x6, x24, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x7, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x10, [x6,3776]
	sxth	w0, w0
	add	x11, x10, x7
	ldr	w11, [x11,4]
	str	w11, [x10,x7]
	ldr	x6, [x6,3720]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1814
.L1869:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x2, [x0,3776]
	str	w6, [x2,x8]
	ldr	x2, [x0,3720]
	strh	w25, [x2,x9,lsl 1]
	tbz	w1, #31, .L1816
	b	.L1805
.L1812:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1811
.L1868:
	tbz	w1, #31, .L1849
	b	.L1805
.L1816:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3864]
	ldr	w2, [x2,2572]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1805
.L1849:
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldr	w4, [x26,4]
	strh	w0, [x2,3864]
	ldr	x0, [x2,3776]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,3720]
	b	.L1865
.L1826:
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bhi	.L1870
	sub	w1, w1, #1
	sxth	x1, w1
.L1820:
	cmp	w1, w2
	bgt	.L1826
	b	.L1825
.L1870:
	ldr	w2, [x7]
	cbnz	w2, .L1822
	cmp	w0, w6
	beq	.L1822
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	strh	w0, [x2,2590]
.L1822:
	mov	w0, 0
.L1823:
	cmp	w0, w1
	beq	.L1871
	add	x2, x24, :lo12:.LANCHOR2
	ldr	x7, [x2,3744]
	sxtw	x2, w0
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	x10, x7, x6
	add	w0, w0, 1
	sxth	w0, w0
	ldr	w10, [x10,4]
	str	w10, [x7,x6]
	add	x6, x19, :lo12:.LANCHOR0
	ldr	x6, [x6,2592]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1823
.L1871:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	ldr	x0, [x0,3744]
	str	w2, [x0,x8]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2592]
	strh	w25, [x0,x9,lsl 1]
.L1825:
	tbnz	w1, #31, .L1805
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2564]
	ldrh	w6, [x2,2590]
	sub	w0, w0, #1
	sub	w0, w0, w6
	cmp	w1, w0, sxth
	bgt	.L1805
	add	x0, x24, :lo12:.LANCHOR2
	add	w6, w6, 1
	ldr	w4, [x26,4]
	strh	w6, [x2,2590]
	ldr	x0, [x0,3744]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2592]
.L1865:
	strh	w25, [x0,x1,lsl 1]
	b	.L1805
.L1806:
	add	x0, x24, :lo12:.LANCHOR2
	add	x1, x0, 784
	ldrh	w2, [x0,784]
	cmp	w2, w22
	bne	.L1827
	strh	w25, [x0,784]
	ldr	w0, [x26,4]
	str	w0, [x1,8]
	b	.L1805
.L1827:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1828
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1828:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	add	x0, x1, 784
	ldr	w6, [x0,8]
	cmp	w6, w2
	bcs	.L1829
	ldrh	w2, [x1,784]
	strh	w2, [x0,4]
	strh	w25, [x1,784]
	ldr	w1, [x26,4]
	str	w1, [x0,8]
	b	.L1805
.L1829:
	strh	w25, [x0,4]
	b	.L1805
.L1802:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,204]
	mov	w0, w25
	cbz	w1, .L1830
.L1866:
	mov	w1, 0
.L1830:
	bl	FtlFreeSysBlkQueueIn
.L1805:
	add	x23, x23, 1
	b	.L1797
.L1796:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1792
.L1833:
	add	x4, x24, :lo12:.LANCHOR2
	ldr	x2, [x4,3720]
	ldrh	w0, [x2]
	cbz	w0, .L1835
.L1838:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,2592]
	ldrh	w0, [x2]
	cbz	w0, .L1836
	b	.L1859
.L1835:
	ldrh	w4, [x4,3864]
	cbz	w4, .L1838
	ldr	w1, [x1,2572]
.L1839:
	cmp	w0, w1
	bcs	.L1838
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1840
	mov	w7, w0
.L1841:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w1, [x1,2572]
	cmp	w0, w1
	bcs	.L1838
	add	x2, x24, :lo12:.LANCHOR2
	sxtw	x6, w0
	sub	w1, w0, w7
	lsl	x4, x6, 1
	add	w0, w0, 1
	ldr	x5, [x2,3720]
	sxtw	x1, w1
	sxth	w0, w0
	ldrh	w8, [x5,x4]
	strh	w8, [x5,x1,lsl 1]
	ldr	x5, [x2,3776]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,3720]
	strh	wzr, [x1,x4]
	b	.L1841
.L1840:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1839
.L1836:
	ldrh	w4, [x1,2590]
	cbz	w4, .L1859
	ldrh	w1, [x1,2564]
.L1844:
	cmp	w0, w1
	mov	w7, w0
	bge	.L1859
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1845
.L1846:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2,2564]
	cmp	w0, w1
	bge	.L1859
	sxtw	x6, w0
	sub	w1, w0, w7
	ldr	x5, [x2,2592]
	lsl	x4, x6, 1
	sxtw	x1, w1
	add	w0, w0, 1
	ldrh	w8, [x5,x4]
	sxth	w0, w0
	strh	w8, [x5,x1,lsl 1]
	add	x5, x24, :lo12:.LANCHOR2
	ldr	x5, [x5,3744]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,2592]
	strh	wzr, [x1,x4]
	b	.L1846
.L1845:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1844
.L1859:
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
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	sub	sp, sp, #144
	mov	w1, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x23, x24, [sp,64]
	stp	x21, x22, [sp,48]
	adrp	x23, .LANCHOR4
	adrp	x21, .LANCHOR2
	stp	x19, x20, [sp,32]
	add	x24, x21, :lo12:.LANCHOR2
	add	x19, x23, :lo12:.LANCHOR4
	add	x19, x19, 1704
	adrp	x20, .LANCHOR0
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	ldr	x0, [x24,3616]
	add	x25, x24, 784
	str	x0, [x19,8]
	ldr	x0, [x24,3664]
	str	x0, [x19,16]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,2480]
	ldr	x0, [x24,520]
	lsl	w2, w2, 1
	bl	ftl_memset
	str	x23, [x29,120]
	ldrh	w0, [x24,784]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1873
.L1884:
	mov	w19, -1
	b	.L1874
.L1873:
	mov	w1, 1
	adrp	x26, .LC108
	add	x26, x26, :lo12:.LC108
	bl	FtlGetLastWrittenPage
	ldrsh	w23, [x24,784]
	sxth	w22, w0
	add	w0, w0, 1
	strh	w0, [x25,2]
.L1875:
	tbnz	w22, #31, .L1883
	orr	w0, w22, w23, lsl 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	ldr	x0, [x24,3616]
	add	x27, x20, :lo12:.LANCHOR0
	str	x0, [x19,8]
	mov	x0, x19
	bl	FlashReadPages
	ldrb	w0, [x27,72]
	cbz	w0, .L1876
	ldr	x25, [x19,16]
	ldr	w7, [x25,12]
	cbz	w7, .L1876
	ldr	x2, [x19,8]
	ldrh	w1, [x27,2554]
	mov	x0, x2
	str	x7, [x29,104]
	str	x2, [x29,112]
	bl	js_hash
	ldr	x7, [x29,104]
	cmp	w7, w0
	beq	.L1876
	ldr	x2, [x29,112]
	cbnz	w22, .L1877
	ldrh	w0, [x24,788]
	cmp	w23, w0
	beq	.L1877
	ldr	w0, [x2]
	str	w0, [sp]
	mov	x0, x26
	ldrh	w1, [x24,784]
	ldr	w2, [x19]
	ldr	w3, [x19,4]
	ldr	w4, [x25]
	ldr	w5, [x25,4]
	ldr	w6, [x25,8]
	bl	printk
	ldrsh	w23, [x24,788]
	ldrh	w22, [x27,2546]
	b	.L1879
.L1877:
	mov	w0, -1
	str	w0, [x19]
.L1876:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1879
	ldr	x0, [x24,3616]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1879
	ldr	x0, [x24,3664]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L1879
.L1883:
	ldr	x0, [x29,120]
	add	x24, x21, :lo12:.LANCHOR2
	add	x22, x20, :lo12:.LANCHOR0
	mov	w2, 48
	add	x19, x0, :lo12:.LANCHOR4
	add	x0, x24, 456
	add	x19, x19, 1704
	ldr	x1, [x19,8]
	bl	ftl_memcpy
	ldr	x1, [x19,8]
	ldrh	w2, [x22,2480]
	ldr	x0, [x24,520]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x22,2480]
	ldr	x3, [x19,8]
	ubfiz	x1, x2, 1, 16
	ldr	x0, [x20,#:lo12:.LANCHOR0]
	add	x1, x1, 51
	lsr	w2, w2, 3
	and	x1, x1, 262140
	add	w2, w2, 4
	add	x1, x3, x1
	bl	ftl_memcpy
	ldrh	w0, [x22,2588]
	cbz	w0, .L1881
	ldrh	w0, [x22,2480]
	ldr	x3, [x19,8]
	lsr	w1, w0, 3
	ldrh	w2, [x22,2580]
	add	w1, w1, w0, lsl 1
	ldr	x0, [x24,3768]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x3, x1
	bl	ftl_memcpy
	b	.L1881
.L1879:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1875
.L1881:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w2, [x0,456]
	cmp	w2, w1
	bne	.L1884
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w3, [x0,466]
	ldrh	w5, [x0,464]
	strh	w5, [x0,790]
	ldrh	w2, [x1,2494]
	cmp	w3, w2
	bne	.L1884
	ldrh	w3, [x1,2544]
	ldr	x2, [x29,120]
	strh	wzr, [x0,802]
	mul	w3, w3, w5
	add	x23, x2, :lo12:.LANCHOR4
	ldrh	w2, [x1,2550]
	str	w3, [x1,2616]
	strb	wzr, [x0,806]
	mul	w3, w3, w2
	str	w3, [x1,2584]
	ldrh	w2, [x1,2630]
	ldr	w3, [x1,2484]
	ldrh	w1, [x1,2472]
	sub	w2, w3, w2
	str	w5, [x23,1760]
	sub	w2, w2, w5
	ldrh	w3, [x0,470]
	strh	w3, [x0,560]
	udiv	w1, w2, w1
	ldrh	w2, [x0,472]
	strh	w1, [x0,780]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0,566]
	ldrb	w2, [x0,467]
	strb	w2, [x0,568]
	mov	w2, -1
	strh	w2, [x0,800]
	ldrh	w2, [x0,474]
	strh	w2, [x0,608]
	ldrh	w2, [x0,476]
	strh	w5, [x0,562]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0,614]
	ldrb	w2, [x0,468]
	strb	w2, [x0,616]
	ldrh	w2, [x0,478]
	strh	w2, [x0,656]
	ldrh	w2, [x0,480]
	strh	w5, [x0,610]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	wzr, [x0,808]
	strh	w5, [x0,658]
	strb	w2, [x0,662]
	ldrb	w2, [x0,469]
	ldr	w1, [x0,488]
	strb	w2, [x0,664]
	str	w1, [x0,760]
	ldr	w2, [x0,752]
	ldr	w1, [x0,496]
	str	wzr, [x0,740]
	str	wzr, [x0,728]
	cmp	w1, w2
	str	wzr, [x0,720]
	str	wzr, [x0,736]
	str	wzr, [x0,764]
	str	wzr, [x0,772]
	str	wzr, [x0,732]
	bls	.L1885
	str	w1, [x0,752]
.L1885:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,492]
	ldr	w2, [x0,756]
	cmp	w1, w2
	bls	.L1886
	str	w1, [x0,756]
.L1886:
	mov	w0, 65535
	cmp	w3, w0
	beq	.L1887
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, 560
	bl	make_superblock
.L1887:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 608
	ldrh	w2, [x1,608]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1888
	bl	make_superblock
.L1888:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 656
	ldrh	w2, [x1,656]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1889
	bl	make_superblock
.L1889:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x21, 800
	mov	w19, 0
	ldrh	w2, [x21,800]
	cmp	w2, w1
	beq	.L1874
	bl	make_superblock
.L1874:
	sub	sp, x29, #16
	mov	w0, w19
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 144
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
	stp	x21, x22, [sp,48]
	stp	x19, x20, [sp,32]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	uxtb	w25, w1
	bl	P2V_block_in_plane
	adrp	x2, .LANCHOR3
	adrp	x22, .LANCHOR0
	uxth	w23, w0
	add	x1, x2, :lo12:.LANCHOR3
	add	x19, x22, :lo12:.LANCHOR0
	adrp	x0, .LC109
	add	x1, x1, 152
	add	x0, x0, :lo12:.LC109
	adrp	x21, .LANCHOR2
	ldrh	w24, [x19,2544]
	bl	printk
	add	x0, x21, :lo12:.LANCHOR2
	ubfiz	x2, x23, 1, 16
	mov	w1, w23
	ldr	x3, [x0,520]
	adrp	x0, .LC110
	add	x0, x0, :lo12:.LC110
	ldrh	w2, [x3,x2]
	bl	printk
	strh	w23, [x29,112]
	add	x0, x29, 112
	bl	make_superblock
	cbnz	w25, .L1909
	ldrb	w0, [x19,204]
	cbz	w0, .L1909
	mov	w0, w23
	bl	ftl_get_blk_mode
	cmp	w0, 1
	mov	w23, w0
	bne	.L1901
	ldrh	w24, [x19,2546]
	b	.L1901
.L1909:
	mov	w23, 0
.L1901:
	add	x3, x22, :lo12:.LANCHOR0
	adrp	x0, .LC111
	adrp	x25, .LC112
	add	x0, x0, :lo12:.LC111
	mov	w1, w23
	mov	w2, w24
	ldrh	w3, [x3,2544]
	mov	w20, 0
	mov	w27, 65535
	mov	w28, 56
	mov	w26, 4
	add	x25, x25, :lo12:.LC112
	bl	printk
.L1902:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w5, [x0,2472]
	ldrh	w6, [x0,2554]
	ldrh	w7, [x0,2556]
	mov	x0, 0
	mov	w19, w0
.L1903:
	cmp	w5, w0, uxth
	bls	.L1917
	add	x1, x29, 112
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w27
	beq	.L1904
	add	x3, x21, :lo12:.LANCHOR2
	orr	w2, w20, w2, lsl 10
	umull	x1, w19, w28
	ldr	x4, [x3,3592]
	add	x4, x4, x1
	str	w2, [x4,4]
	ldr	x2, [x3,3592]
	ldr	x4, [x3,1400]
	add	x1, x2, x1
	ldr	x3, [x3,1408]
	mul	w2, w19, w6
	sdiv	w2, w2, w26
	add	x2, x4, x2, sxtw 2
	str	x2, [x1,8]
	mul	w2, w19, w7
	add	w19, w19, 1
	uxth	w19, w19
	sdiv	w2, w2, w26
	add	x2, x3, x2, sxtw 2
	str	x2, [x1,16]
.L1904:
	add	x0, x0, 1
	b	.L1903
.L1917:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, w23
	ldr	x0, [x0,3592]
	bl	FlashReadPages
	mov	x8, 0
	mov	x9, 56
.L1906:
	cmp	w19, w8, uxth
	bls	.L1918
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,112]
	mul	x2, x8, x9
	str	x9, [x29,96]
	ldr	x4, [x0,3592]
	str	x8, [x29,104]
	add	x3, x4, x2
	ldr	x0, [x3,8]
	ldr	x7, [x3,16]
	ldr	w5, [x0]
	str	w5, [sp]
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	mov	x0, x25
	ldr	w2, [x4,x2]
	ldr	w5, [x7,4]
	ldr	w4, [x7]
	ldr	w6, [x7,8]
	ldr	w3, [x3,4]
	ldr	w7, [x7,12]
	bl	printk
	ldr	x8, [x29,104]
	ldr	x9, [x29,96]
	add	x8, x8, 1
	b	.L1906
.L1918:
	add	w20, w20, 1
	uxth	w20, w20
	cmp	w20, w24
	bne	.L1902
.L1908:
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
	adrp	x0, .LC109
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC109
	add	x1, x1, 176
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x23, x24, [sp,64]
	stp	x21, x22, [sp,48]
	adrp	x23, .LC114
	adrp	x22, .LC113
	adrp	x24, .LC115
	add	x22, x22, :lo12:.LC113
	stp	x19, x20, [sp,32]
	add	x23, x23, :lo12:.LC114
	mov	w20, 0
	add	x24, x24, :lo12:.LC115
	stp	x25, x26, [sp,80]
	bl	printk
.L1920:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,2482]
	cmp	w0, w20
	bls	.L1929
	mov	w0, w20
	strh	w20, [x29,80]
	adrp	x21, .LANCHOR2
	bl	ftl_get_blk_mode
	mov	w3, w0
	add	x0, x21, :lo12:.LANCHOR2
	ubfiz	x2, x20, 1, 16
	mov	w1, w20
	ldr	x4, [x0,520]
	mov	x0, x22
	ldrh	w2, [x4,x2]
	bl	printk
	add	x0, x29, 80
	bl	make_superblock
	mov	x1, 0
	ldrh	w6, [x19,2472]
	ldrh	w7, [x19,2554]
	mov	w9, 65535
	ldrh	w8, [x19,2556]
	mov	w10, 56
	mov	w19, w1
	mov	w5, 4
.L1921:
	cmp	w6, w1, uxth
	bls	.L1930
	add	x0, x29, 80
	add	x0, x0, x1, lsl 1
	ldrh	w2, [x0,16]
	cmp	w2, w9
	beq	.L1922
	add	x3, x21, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x0, w19, w10
	ldr	x4, [x3,3592]
	add	x4, x4, x0
	str	w2, [x4,4]
	ldr	x2, [x3,3592]
	ldr	x4, [x3,1400]
	add	x2, x2, x0
	ldr	x3, [x3,1408]
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
.L1922:
	add	x1, x1, 1
	b	.L1921
.L1930:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x25, 0
	mov	x26, 56
	ldr	x0, [x0,3592]
	bl	FlashReadPages
.L1924:
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w19, w25, uxth
	bls	.L1931
	ldr	x4, [x0,3592]
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
	b	.L1924
.L1931:
	ldr	x0, [x0,3592]
	mov	w1, w19
	mov	w2, 1
	mov	x25, 0
	mov	x26, 56
	bl	FlashReadPages
.L1926:
	cmp	w19, w25, uxth
	bls	.L1932
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,80]
	mul	x2, x25, x26
	add	x25, x25, 1
	ldr	x4, [x0,3592]
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
	b	.L1926
.L1932:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1920
.L1929:
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
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	sub	sp, sp, #96
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR4
	stp	x23, x24, [sp,64]
	stp	x21, x22, [sp,48]
	stp	x25, x26, [sp,80]
	adrp	x23, .LC116
	add	x19, x19, 1704
	ldrh	w26, [x0,2542]
	add	x23, x23, :lo12:.LC116
	sub	w26, w26, #16
.L1934:
	add	x21, x20, :lo12:.LANCHOR0
	ldrh	w0, [x21,2542]
	cmp	w26, w0
	bge	.L1949
	uxth	w22, w26
	mov	w0, w22
	bl	ftl_get_blk_mode
	ldrb	w1, [x21,204]
	cbz	w1, .L1935
	ldrh	w1, [x21,2480]
	cmp	w26, w1
	bge	.L1936
	ldrh	w1, [x21,2558]
	cmp	w26, w1
	blt	.L1936
.L1935:
	cmp	w0, 1
	bne	.L1937
.L1936:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w21, -2147483648
	ldrh	w24, [x0,2546]
	b	.L1938
.L1937:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w21, 0
	ldrh	w24, [x0,2544]
.L1938:
	mov	x0, x23
	mov	w1, w26
	mov	w2, w24
	mov	w3, w21
	bl	printk
	mov	w0, w22
	bl	FtlBbmIsBadBlock
	cbz	w0, .L1939
	adrp	x0, .LC117
	mov	w1, w26
	add	x0, x0, :lo12:.LC117
	mov	w2, w24
	mov	w3, w21
	bl	printk
.L1939:
	adrp	x25, .LC118
	mov	w22, 0
	add	w21, w21, w26, lsl 10
	add	x25, x25, :lo12:.LC118
.L1940:
	cmp	w22, w24
	beq	.L1950
	adrp	x3, .LANCHOR2
	add	w0, w21, w22
	add	x3, x3, :lo12:.LANCHOR2
	str	w0, [x19,4]
	mov	w1, 1
	mov	w2, 0
	str	wzr, [x19]
	add	w22, w22, 1
	ldr	x0, [x3,3616]
	str	x0, [x19,8]
	ldr	x0, [x3,3664]
	str	x0, [x19,16]
	mov	x0, x19
	bl	FlashReadPages
	ldr	x7, [x19,8]
	ldr	x6, [x19,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	mov	x0, x25
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x19,4]
	ldr	w2, [x19]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L1940
.L1950:
	add	w26, w26, 1
	b	.L1934
.L1949:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC119
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC119
	add	x1, x1, 192
	bl	printk
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 96
	ret
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	stp	x29, x30, [sp, -192]!
	adrp	x6, .LANCHOR1+481
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	ldrb	w6, [x6,#:lo12:.LANCHOR1+481]
	add	x4, x21, :lo12:.LANCHOR0
	str	w3, [x29,120]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	w6, [x29,124]
	mov	x19, x0
	ldr	x5, [x4,88]
	mov	w25, w1
	ldrb	w4, [x4,72]
	mov	w23, w2
	mov	w22, 0
	ldrb	w5, [x5,19]
	cbz	w4, .L1952
	bl	FlashProgSlc2KPages
	b	.L1953
.L1952:
	sub	w0, w5, #1
	mov	w27, 56
	mov	x28, 24
	str	w0, [x29,116]
.L2003:
	cmp	w22, w25
	bcs	.L2004
	umull	x20, w22, w27
	mov	w1, w23
	add	x2, x29, 128
	add	x26, x19, x20
	sub	w4, w25, w22
	mov	x0, x26
	add	x3, x29, 132
	bl	LogAddr2PhyAddr
	mov	w24, w0
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w0, [x29,132]
	ldrb	w2, [x1,2358]
	cmp	w0, w2
	bcc	.L1954
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1955
.L1954:
	ldrb	w2, [x1,2368]
	add	x1, x1, 2164
	cmp	w2, wzr
	uxtw	x2, w0
	csel	w24, w24, wzr, ne
	madd	x1, x2, x28, x1
	ldr	x1, [x1,8]
	cbz	x1, .L1957
	bl	FlashWaitCmdDone
.L1957:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	add	x0, x0, 2164
	ldr	w1, [x29,128]
	madd	x0, x2, x28, x0
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1958
	add	w1, w22, 1
	umull	x1, w1, w27
	add	x1, x19, x1
	str	x1, [x0,16]
.L1958:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, x2
	madd	x2, x2, x28, x1
	ldrb	w20, [x0,2360]
	ldrb	w0, [x1,2358]
	strb	w20, [x2,2164]
	cmp	w0, 1
	bne	.L1959
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1960
.L1959:
	mov	w0, w20
	str	x1, [x29,104]
	bl	NandcFlashCs
	ldr	x1, [x29,104]
	ldr	w0, [x29,132]
	add	x1, x1, 1232
	ldr	w0, [x1,x0,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
.L1960:
	ldr	w0, [x29,116]
	cmp	w0, 7
	bhi	.L1961
	add	x2, x21, :lo12:.LANCHOR0
	add	x0, x2, x20, sxtw
	ldrb	w0, [x0,2128]
	cbz	w0, .L1961
	ldrb	w1, [x2,1273]
	mov	w0, w20
	add	x2, x2, 1276
	mov	w3, 0
	bl	HynixSetRRPara
.L1961:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1962
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbz	w0, .L1962
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1963
.L1962:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1963:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldrb	w2, [x29,124]
	mov	w0, w20
	ldr	x3, [x26,8]
	mov	w1, 1
	ldr	x4, [x26,16]
	bl	NandcXferData
	cbz	w24, .L1964
	ldr	w1, [x29,128]
	mov	w0, w20
	add	x26, x21, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29,132]
	add	x0, x26, 1232
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w2, [x26,76]
	mov	w0, w20
	ldr	w1, [x29,128]
	add	w1, w2, w1
	bl	FlashProgDpSecondCmd
	add	w4, w22, 1
	ldrb	w2, [x29,124]
	mov	w0, w20
	mov	w1, 1
	umull	x4, w4, w27
	add	x4, x19, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
.L1964:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1955:
	add	w22, w22, 1
	b	.L2003
.L2004:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	add	x22, x21, 2164
	mov	x24, 24
.L1966:
	ldrb	w0, [x21,2358]
	cmp	w0, w20
	bls	.L2005
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1967
	ldrb	w0, [x21,204]
	cbz	w0, .L1967
	mul	x0, x20, x24
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L1967:
	add	x20, x20, 1
	b	.L1966
.L2005:
	ldr	w0, [x29,120]
	cbnz	w0, .L1969
.L1977:
	mov	w0, 0
	b	.L1953
.L1969:
	adrp	x24, .LC107
	adrp	x26, .LC106
	adrp	x27, .LC104
	mov	w21, 0
	add	x24, x24, :lo12:.LC107
	add	x26, x26, :lo12:.LC106
	add	x27, x27, :lo12:.LC104
.L1970:
	cmp	w21, w25
	beq	.L1977
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1971
	adrp	x0, .LC103
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC103
	bl	printk
	b	.L1972
.L1971:
	adrp	x20, .LANCHOR4
	sub	w4, w25, w21
	add	x22, x20, :lo12:.LANCHOR4
	add	x3, x29, 132
	mov	w1, w23
	add	x2, x29, 128
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,1664]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,1672]
	str	wzr, [x0]
	add	x0, x29, 136
	bl	memcpy
	ldr	x0, [x22,1664]
	mov	w1, 1
	str	x0, [x29,144]
	mov	w2, w23
	ldr	x0, [x22,1672]
	str	x0, [x29,152]
	add	x0, x29, 136
	bl	FlashReadPages
	ldr	w22, [x29,136]
	cmn	w22, #1
	bne	.L1973
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1973:
	ldr	x0, [x19,16]
	cbz	x0, .L1974
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,1672]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1974
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1974:
	ldr	x0, [x19,8]
	cbz	x0, .L1972
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,1664]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1972
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1972:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1970
.L1953:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FlashTestBlk.part.18, %function
FlashTestBlk.part.18:
	stp	x29, x30, [sp, -160]!
	mov	w1, 165
	mov	w2, 32
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	uxth	w20, w0
	add	x19, x19, :lo12:.LANCHOR4
	lsl	w20, w20, 10
	ldr	x0, [x19,1648]
	str	x0, [x29,48]
	add	x0, x29, 96
	str	x0, [x29,56]
	bl	ftl_memset
	ldr	x0, [x19,1648]
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
	.size	FlashTestBlk.part.18, .-FlashTestBlk.part.18
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	uxth	w1, w0
	adrp	x0, .LANCHOR4+1636
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x0,#:lo12:.LANCHOR4+1636]
	mov	w0, 0
	cmp	w1, w2
	bcc	.L2008
	mov	w0, w1
	bl	FlashTestBlk.part.18
.L2008:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	stp	x29, x30, [sp, -224]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	mov	w19, 0
	add	x0, x20, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR0
	ldr	x0, [x0,1656]
	str	x0, [x29,136]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,188]
	ldrh	w23, [x0,190]
	ldrh	w26, [x0,76]
	mul	w23, w1, w23
	ldr	x1, [x0,88]
	ldrb	w0, [x0,72]
	uxth	w23, w23
	ldrb	w1, [x1,24]
	cmp	w0, 1
	str	w1, [x29,132]
	adrp	x0, .LC120
	ubfiz	w1, w26, 1, 15
	add	x0, x0, :lo12:.LC120
	csel	w26, w1, w26, eq
	mov	w1, 1
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w2, 4096
	ldr	x0, [x0,1656]
	bl	ftl_memset
	lsr	w0, w23, 4
	str	w0, [x29,128]
	ldr	w0, [x29,132]
	and	w0, w0, 1
	str	w0, [x29,116]
	ldr	w0, [x29,132]
	and	w0, w0, 2
	uxtb	w0, w0
	str	w0, [x29,112]
	sub	w0, w23, #1
	uxth	w0, w0
	str	w0, [x29,124]
.L2012:
	add	x24, x21, :lo12:.LANCHOR0
	ldrb	w0, [x24,2358]
	cmp	w0, w19
	bls	.L2062
	add	x0, x20, :lo12:.LANCHOR4
	sxtw	x25, w19
	add	x1, x0, 1688
	ldrh	w22, [x1,w19,sxtw 1]
	cbnz	w22, .L2042
	ldrh	w2, [x24,196]
	mov	w1, w22
	ldr	x0, [x0,1600]
	add	x24, x24, x25
	lsl	w2, w2, 9
	mov	w27, w22
	bl	ftl_memset
	ldrb	w28, [x24,2360]
	ldr	w0, [x29,132]
	mov	w24, w22
	and	w0, w0, 4
	uxtb	w0, w0
	str	w0, [x29,120]
.L2014:
	uxth	w0, w27
	str	w0, [x29,144]
	cmp	w0, w23
	bcs	.L2024
	mov	w0, -1
	strb	w0, [x29,166]
	strb	w0, [x29,167]
	ldr	w0, [x29,116]
	cbz	w0, .L2016
	add	x4, x21, :lo12:.LANCHOR0
	add	x2, x29, 166
	add	x0, x4, 1232
	str	x4, [x29,96]
	ldr	w3, [x0,x25,lsl 2]
	mov	w0, w28
	add	w3, w22, w3
	str	x3, [x29,104]
	mov	w1, w3
	bl	FlashReadSpare
	ldr	x4, [x29,96]
	ldr	x3, [x29,104]
	ldrb	w0, [x4,72]
	cmp	w0, 1
	bne	.L2016
	ldr	w1, [x4,76]
	mov	w0, w28
	add	x2, x29, 167
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,166]
	ldrb	w1, [x29,167]
	and	w0, w1, w0
	strb	w0, [x29,166]
.L2016:
	ldr	w0, [x29,112]
	cbz	w0, .L2018
	add	x0, x21, :lo12:.LANCHOR0
	add	x2, x29, 167
	ldr	x1, [x0,88]
	add	x0, x0, 1232
	ldrh	w1, [x1,10]
	ldr	w0, [x0,x25,lsl 2]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w28
	add	w1, w1, w22
	bl	FlashReadSpare
.L2018:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x29,166]
	ldr	x0, [x0,88]
	ldrb	w2, [x0,7]
	cmp	w2, 8
	beq	.L2019
	cmp	w2, 1
	beq	.L2019
	ldrb	w0, [x0,18]
	cmp	w0, 12
	bne	.L2020
.L2019:
	mov	w0, 1
	cbz	w1, .L2021
	ldrb	w0, [x29,167]
	cmp	w0, wzr
	cset	w0, eq
	b	.L2021
.L2020:
	cmp	w1, 255
	mov	w0, 1
	bne	.L2021
	ldrb	w0, [x29,167]
	cmp	w0, 255
	cset	w0, ne
.L2021:
	ldr	w1, [x29,120]
	cbz	w1, .L2022
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 1232
	ldr	w1, [x0,x25,lsl 2]
	mov	w0, w28
	add	w1, w22, w1
	bl	SandiskProgTestBadBlock
.L2022:
	cbz	w0, .L2023
	adrp	x0, .LC121
	mov	w1, w19
	mov	w2, w27
	add	x0, x0, :lo12:.LC121
	add	w24, w24, 1
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x1, [x29,144]
	ldrb	w3, [x29,144]
	uxth	w24, w24
	ubfx	x1, x1, 5, 11
	lsl	x1, x1, 2
	ldr	x2, [x0,1600]
	mov	w0, 1
	lsl	w0, w0, w3
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29,128]
	ldrb	w0, [x0,2358]
	mul	w0, w1, w0
	cmp	w24, w0
	bgt	.L2024
.L2023:
	add	w27, w27, 1
	add	w22, w22, w26
	b	.L2014
.L2024:
	adrp	x0, .LC122
	mov	w1, w19
	mov	w2, w24
	add	x0, x0, :lo12:.LC122
	bl	printk
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,128]
	ldrb	w0, [x1,2358]
	mul	w0, w2, w0
	cmp	w24, w0
	blt	.L2026
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w2, [x1,196]
	mov	w1, 0
	lsl	w2, w2, 9
	ldr	x0, [x0,1600]
	bl	ftl_memset
.L2026:
	cbnz	w19, .L2028
	add	x0, x20, :lo12:.LANCHOR4
	adrp	x27, .LC123
	mov	w22, w19
	add	x27, x27, :lo12:.LC123
	mov	w24, 1
	ldrh	w28, [x0,1636]
.L2029:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,73]
	cmp	w0, w28
	bls	.L2063
	mov	w0, w28
	bl	FlashTestBlk
	cbz	w0, .L2030
	mov	w1, w28
	mov	x0, x27
	add	w22, w22, 1
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ubfx	x1, x28, 5, 11
	lsl	x1, x1, 2
	uxth	w22, w22
	ldr	x2, [x0,1600]
	lsl	w0, w24, w28
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L2030:
	add	w28, w28, 1
	uxth	w28, w28
	b	.L2029
.L2063:
	adrp	x27, .LC123
	ldr	w24, [x29,124]
	sub	w28, w23, #50
	add	x27, x27, :lo12:.LC123
.L2032:
	cmp	w24, w28
	ble	.L2064
	mov	w0, w24
	bl	FlashTestBlk
	cbz	w0, .L2033
	mov	w1, w24
	mov	x0, x27
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ubfx	x1, x24, 5, 11
	lsl	x1, x1, 2
	ldr	x3, [x0,1600]
	mov	w0, 1
	lsl	w0, w0, w24
	ldr	w4, [x3,x1]
	orr	w0, w4, w0
	str	w0, [x3,x1]
.L2033:
	sub	w2, w24, #1
	uxth	w24, w2
	b	.L2032
.L2064:
	add	x2, x21, :lo12:.LANCHOR0
	add	x1, x20, :lo12:.LANCHOR4
	ldrb	w0, [x2,73]
	ldr	w3, [x1,1636]
	sub	w0, w0, w3
	cmp	w22, w0
	bcc	.L2028
	ldrh	w2, [x2,196]
	ldr	x0, [x1,1600]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L2028:
	adrp	x24, .LC124
	ldr	w28, [x29,124]
	mul	w27, w19, w23
	add	x24, x24, :lo12:.LC124
	mov	w22, -3872
.L2036:
	mov	w2, w28
	mov	x0, x24
	mov	w1, w19
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x2, [x0,1600]
.L2037:
	ubfx	x0, x28, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w28
	and	w1, w0, 1
	tbz	x0, 0, .L2065
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L2037
.L2065:
	add	x0, x20, :lo12:.LANCHOR4
	add	x2, x0, 1688
	strh	w28, [x2,x25,lsl 1]
	ldr	x2, [x29,136]
	strh	w22, [x2]
	strh	w28, [x2,2]
	strh	w1, [x2,8]
	ldr	x1, [x0,1600]
	ldr	x0, [x0,1656]
	str	x0, [x29,184]
	add	w0, w28, w27
	str	x1, [x29,176]
	lsl	w0, w0, 10
	mov	w1, 1
	str	w0, [x29,172]
	mov	w2, w1
	add	x0, x29, 168
	bl	FlashEraseBlocks
	mov	w1, 1
	add	x0, x29, 168
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x29,168]
	cbz	w0, .L2042
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L2036
.L2042:
	add	w19, w19, 1
	uxtb	w19, w19
	b	.L2012
.L2062:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 224
	ret
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR2
	adrp	x22, .LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	uxtb	w25, w1
	add	x24, x22, :lo12:.LANCHOR0
	add	x1, x20, :lo12:.LANCHOR2
	uxth	w26, w0
	stp	x27, x28, [sp,80]
	ldrb	w0, [x24,2368]
	ldr	w2, [x1,424]
	str	w0, [x29,120]
	mov	w0, 0
	cbnz	w2, .L2067
	ldrb	w0, [x24,204]
	mov	w28, w2
	str	w0, [x29,124]
	mov	w21, w2
	str	w26, [x1,3580]
	mov	w19, w2
	mov	w27, 56
.L2068:
	ldrh	w0, [x24,2472]
	cmp	w0, w28
	bls	.L2112
	add	x1, x20, :lo12:.LANCHOR2
	umull	x0, w28, w27
	ldr	x1, [x1,432]
	str	wzr, [x1,x0]
	add	x0, x24, 2504
	mov	w1, w26
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	uxth	w23, w0
	mov	w1, w23
	cbz	w25, .L2069
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L2070
.L2069:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2071
	add	x3, x20, :lo12:.LANCHOR2
	lsl	w23, w23, 10
	umull	x0, w21, w27
	mov	w2, 4
	ldr	x1, [x3,432]
	add	x1, x1, x0
	str	w23, [x1,4]
	ldr	x1, [x3,432]
	ldr	x3, [x3,3672]
	add	x1, x1, x0
	ldrh	w0, [x24,2556]
	mul	w0, w21, w0
	add	w21, w21, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L2070
.L2071:
	add	w19, w19, 1
	uxth	w19, w19
.L2070:
	add	w2, w28, 1
	uxth	w28, w2
	b	.L2068
.L2112:
	cbz	w21, .L2091
	ldr	w0, [x29,124]
	mov	w2, w21
	strb	wzr, [x24,2368]
	mov	x27, 56
	cmp	w0, wzr
	add	x0, x20, :lo12:.LANCHOR2
	cset	w23, ne
	mov	w1, w23
	ldr	x0, [x0,432]
	bl	FlashEraseBlocks
	ldrb	w0, [x29,120]
	strb	w0, [x24,2368]
	mov	x24, 0
.L2074:
	cmp	w21, w24, uxth
	bls	.L2113
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x24, x27
	ldr	x1, [x1,432]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L2075
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L2075:
	add	x24, x24, 1
	b	.L2074
.L2113:
	cbnz	w25, .L2077
	mov	w0, 1
	uxth	w23, w23
	mov	w28, 6
	str	w0, [x29,124]
	b	.L2078
.L2077:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,2546]
	ldrb	w0, [x0,204]
	str	w1, [x29,124]
	cbnz	w0, .L2094
	uxtw	x0, w1
	mov	w23, 1
	lsr	w28, w0, 2
	b	.L2078
.L2094:
	mov	w23, 1
	mov	w28, w23
.L2078:
	mov	w24, 0
	add	x22, x22, :lo12:.LANCHOR0
.L2087:
	mov	w27, 0
	mov	w21, w27
.L2079:
	ldrh	w0, [x22,2472]
	cmp	w0, w27
	bls	.L2114
	add	x1, x20, :lo12:.LANCHOR2
	mov	w0, 56
	umull	x0, w27, w0
	ldr	x1, [x1,432]
	str	wzr, [x1,x0]
	add	x0, x22, 2504
	mov	w1, w26
	ldrb	w0, [x0,w27,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w25, .L2080
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L2081
.L2080:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2081
	add	x2, x20, :lo12:.LANCHOR2
	mov	w0, 56
	ldr	w3, [x29,112]
	umull	x0, w21, w0
	ldr	x1, [x2,432]
	add	w3, w24, w3, lsl 10
	add	x1, x1, x0
	str	w3, [x1,4]
	mov	w3, 4
	ldr	x1, [x2,432]
	add	x1, x1, x0
	ldr	x0, [x2,3648]
	ldr	x2, [x2,3656]
	str	x0, [x1,8]
	ldrh	w0, [x22,2556]
	mul	w0, w21, w0
	add	w21, w21, 1
	sdiv	w0, w0, w3
	uxth	w21, w21
	add	x0, x2, x0, sxtw 2
	str	x0, [x1,16]
.L2081:
	add	w4, w27, 1
	uxth	w27, w4
	b	.L2079
.L2114:
	cbz	w21, .L2091
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w21
	mov	w2, w23
	mov	w3, 1
	strb	wzr, [x22,2368]
	ldr	x0, [x0,432]
	bl	FlashProgPages
	ldrb	w0, [x29,120]
	mov	x1, 0
	strb	w0, [x22,2368]
.L2084:
	cmp	w21, w1, uxth
	bls	.L2115
	add	x2, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x1, x0
	ldr	x2, [x2,432]
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L2085
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L2085:
	add	x1, x1, 1
	b	.L2084
.L2115:
	add	w24, w24, w28
	ldr	w0, [x29,124]
	uxth	w24, w24
	cmp	w24, w0
	bcc	.L2087
	mov	x22, 0
	mov	x24, 56
.L2088:
	cmp	w21, w22, uxth
	bls	.L2116
	cbz	w25, .L2089
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x22, x24
	ldr	x1, [x1,432]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L2089
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L2089:
	add	x22, x22, 1
	b	.L2088
.L2116:
	cmp	w26, 63
	bls	.L2095
	cbz	w25, .L2091
.L2095:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, w21
	ldr	x0, [x20,432]
	bl	FlashEraseBlocks
.L2091:
	mov	w0, w19
.L2067:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	add	x2, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w1, [x2,424]
	cbnz	w1, .L2119
	adrp	x19, .LANCHOR4
	adrp	x20, .LANCHOR0
	add	x24, x19, :lo12:.LANCHOR4
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x2,3616]
	mov	x21, 0
	ldr	x2, [x2,3664]
	add	x23, x20, 2624
	str	x2, [x24,1720]
	ldrh	w2, [x20,2554]
	str	x0, [x24,1712]
	bl	ftl_memset
.L2120:
	ldrh	w1, [x20,2494]
	mov	w0, w21
	add	x21, x21, 1
	cmp	w0, w1
	bge	.L2133
	add	x1, x22, :lo12:.LANCHOR2
	ldr	x3, [x24,1712]
	ldrh	w2, [x1,3792]
	add	x1, x23, x21, lsl 3
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L2120
.L2133:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 255
	add	x19, x19, 1704
	mov	w2, 16
	mov	w24, 0
	adrp	x26, .LC125
	ldr	x21, [x19,16]
	adrp	x27, .LC126
	mov	w25, w24
	add	x26, x26, :lo12:.LC125
	mov	x0, x21
	add	x27, x27, :lo12:.LC126
	bl	ftl_memset
	mov	w0, -3887
	strh	w0, [x21]
	ldr	w0, [x23,8]
	str	w0, [x21,4]
	ldrh	w0, [x20,2624]
	strh	w0, [x21,2]
	ldrh	w0, [x23,4]
	strh	w0, [x21,8]
	ldrh	w0, [x23,6]
	strh	w0, [x21,10]
	ldr	w0, [x20,2468]
	strh	w0, [x21,12]
.L2122:
	add	x28, x22, :lo12:.LANCHOR2
	ldrh	w1, [x20,2624]
	ldrh	w2, [x20,2626]
	ldrh	w3, [x20,2628]
	ldr	x0, [x28,3616]
	str	x0, [x19,8]
	ldr	x0, [x28,3664]
	str	x0, [x19,16]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19,4]
	mov	x0, x26
	ldrh	w4, [x21,10]
	str	wzr, [x19]
	bl	printk
	ldrh	w0, [x20,2546]
	ldrh	w1, [x20,2626]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2123
	ldr	w0, [x20,2632]
	strh	wzr, [x20,2626]
	add	w0, w0, 1
	str	w0, [x20,2632]
	str	w0, [x21,4]
	ldrh	w0, [x20,2624]
	ldrh	w1, [x20,2628]
	strh	w0, [x21,8]
	strh	w0, [x20,2628]
	ldr	x0, [x28,432]
	strh	w1, [x20,2624]
	lsl	w1, w1, 10
	str	w1, [x19,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x28,432]
	bl	FlashEraseBlocks
.L2123:
	mov	w1, 1
	mov	x0, x19
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w0, [x20,2626]
	add	w0, w0, 1
	strh	w0, [x20,2626]
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L2124
	ldr	w1, [x19,4]
	mov	x0, x27
	add	w24, w24, 1
	bl	printk
	uxth	w24, w24
	cmp	w24, 3
	bls	.L2122
	adrp	x0, .LC127
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC127
	mov	w2, w24
	add	x22, x22, :lo12:.LANCHOR2
	bl	printk
	mov	w0, 1
	str	w0, [x22,424]
	b	.L2119
.L2124:
	add	w25, w25, 1
	cmp	w25, 1
	beq	.L2122
	cmp	w0, 256
	beq	.L2122
.L2119:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
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
	ldr	w1, [x19,424]
	stp	x27, x28, [sp,80]
	cbnz	w1, .L2135
	mov	x20, x0
	add	x23, x19, 656
	adrp	x22, .LANCHOR0
	mov	w24, 7
	add	x25, x19, 544
.L2136:
	cmp	x20, x23
	bne	.L2137
	ldrh	w2, [x19,552]
	ldr	w4, [x19,1380]
	lsr	w3, w2, 1
	add	w1, w3, 1
	mul	w0, w2, w4
	add	w1, w1, w0, lsr 2
	add	x0, x22, :lo12:.LANCHOR0
	uxth	w1, w1
	ldr	w0, [x0,2372]
	cbz	w0, .L2138
	ldr	w0, [x19,776]
	cmp	w0, 39
	bhi	.L2138
	cmp	w0, 2
	mov	w1, 0
	bls	.L2139
	tbz	x2, 0, .L2166
	cbz	w4, .L2139
.L2166:
	mov	w1, w3
	b	.L2138
.L2137:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L2139
	add	x2, x22, :lo12:.LANCHOR0
	ldrh	w0, [x2,2492]
	cmp	w0, 1
	beq	.L2139
	ldrb	w0, [x2,204]
	cbnz	w0, .L2139
	ldr	w2, [x2,2372]
	ldrh	w0, [x19,552]
	lsr	w1, w0, 3
	cbz	w2, .L2138
	ldr	w2, [x19,776]
	cmp	w2, 1
	bhi	.L2138
	mul	w1, w0, w24
	lsr	w1, w1, 3
.L2138:
	cbz	w1, .L2139
	sub	w1, w1, #1
	uxth	w1, w1
.L2139:
	ldrb	w2, [x20,8]
	mov	x0, x25
	add	x27, x22, :lo12:.LANCHOR0
	bl	List_pop_index_node
	uxth	w28, w0
	ldrh	w0, [x19,552]
	sub	w0, w0, #1
	strh	w0, [x19,552]
	ldrh	w0, [x27,2480]
	cmp	w0, w28
	bls	.L2136
	ubfiz	x26, x28, 1, 16
	ldr	x0, [x19,520]
	ldrh	w0, [x0,x26]
	cbnz	w0, .L2136
	strh	w28, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L2191
	mov	x0, 0
	ldrh	w5, [x27,2472]
	mov	x3, 56
	mov	w27, w0
	mov	w6, 65535
.L2143:
	cmp	w5, w0, uxth
	bls	.L2194
	ldr	x4, [x19,432]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L2144
	umull	x2, w27, w3
	add	w27, w27, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w27, w27
	str	w1, [x2,4]
.L2144:
	add	x0, x0, 1
	b	.L2143
.L2194:
	add	x0, x19, 560
	cmp	x20, x0
	bne	.L2146
	add	x1, x22, :lo12:.LANCHOR0
	ldr	w1, [x1,2372]
	cbz	w1, .L2146
	ldr	x1, [x19,440]
	ldrh	w1, [x1,x26]
	cmp	w1, 40
	bls	.L2146
	strb	wzr, [x19,568]
.L2146:
	ldrb	w0, [x20,8]
	cbnz	w0, .L2147
	ldr	x0, [x19,440]
	mov	w1, 2
	ldrh	w2, [x0,x26]
	cbz	w2, .L2192
	add	x1, x22, :lo12:.LANCHOR0
	ldrh	w1, [x1,2536]
	add	w1, w2, w1
.L2192:
	strh	w1, [x0,x26]
	mov	w1, 0
	ldr	w0, [x19,760]
	add	w0, w0, 1
	str	w0, [x19,760]
	mov	w0, w28
	bl	ftl_set_blk_mode
	b	.L2150
.L2147:
	ldr	x1, [x19,440]
	ldrh	w0, [x1,x26]
	add	w0, w0, 1
	strh	w0, [x1,x26]
	ldr	w0, [x19,764]
	add	w0, w0, 1
	str	w0, [x19,764]
	mov	w0, w28
	bl	ftl_set_blk_mode.part.9
.L2150:
	ldr	x0, [x19,440]
	ldr	w1, [x19,772]
	ldrh	w0, [x0,x26]
	cmp	w0, w1
	bls	.L2151
	str	w0, [x19,772]
.L2151:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w3, [x19,760]
	ldr	w0, [x19,764]
	ldrh	w1, [x2,2536]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,2480]
	mov	x3, 56
	udiv	w0, w0, w1
	ldr	x1, [x19,3704]
	str	w0, [x19,768]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L2152:
	cmp	w27, w0, uxth
	bls	.L2195
	ldr	x1, [x19,432]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L2152
.L2195:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbz	w0, .L2154
	ldrb	w0, [x20,8]
	cmp	w0, 1
	ldr	x0, [x19,432]
	bne	.L2155
	mov	w1, 0
	b	.L2193
.L2155:
	mov	w1, 1
.L2193:
	mov	w2, w27
	bl	FlashEraseBlocks
.L2154:
	ldrb	w1, [x20,8]
	mov	w2, w27
	ldr	x0, [x19,432]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	x4, 56
	mov	w2, w1
.L2156:
	cmp	w27, w1, uxth
	bls	.L2196
	mul	x0, x1, x4
	ldr	x3, [x19,432]
	add	x5, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	bne	.L2157
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
.L2157:
	add	x1, x1, 1
	b	.L2156
.L2196:
	cbz	w2, .L2159
	mov	w0, w28
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2159:
	ldrb	w0, [x20,7]
	cbnz	w0, .L2160
.L2191:
	ldr	x0, [x19,520]
	mov	w1, -1
	strh	w1, [x0,x26]
	b	.L2136
.L2160:
	adrp	x1, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x1,2544]
	strh	w28, [x20]
	mul	w0, w0, w1
	ldr	w1, [x21,752]
	str	w1, [x20,12]
	uxth	w0, w0
	add	w1, w1, 1
	strh	w0, [x20,4]
	str	w1, [x21,752]
	ldr	x1, [x21,520]
	ldrh	w2, [x20]
	strh	w0, [x1,x2,lsl 1]
.L2135:
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
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x25, [sp,64]
	add	x19, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	uxth	w23, w0
	ldrh	w0, [x19,1464]
	stp	x21, x22, [sp,32]
	mov	w21, 0
	cbz	w0, .L2199
	adrp	x22, .LANCHOR0
	adrp	x24, .LC128
	add	x22, x22, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LC128
.L2207:
	ldrh	w0, [x22,2472]
	cmp	w0, w21
	bls	.L2208
	add	x0, x22, 2504
	mov	w1, w23
	mov	w20, 0
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w25, w0
.L2200:
	ldrh	w0, [x19,1464]
	cmp	w0, w20
	bls	.L2209
	add	x0, x19, 1472
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w25
	bne	.L2201
	mov	w1, w25
	mov	x0, x24
	bl	printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,1464]
	mov	w3, w20
.L2202:
	cmp	w3, w2
	bcs	.L2210
	add	x4, x19, 1472
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2202
.L2210:
	sub	w2, w2, #1
	strh	w2, [x19,1464]
.L2201:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2200
.L2209:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2207
.L2208:
	bl	FtlGcReFreshBadBlk
.L2199:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
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
	ldr	x2, [x1,520]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2212
	ldrh	w0, [x1,800]
	cmp	w0, w19
	bne	.L2213
	mov	w0, -1
	strh	w0, [x1,800]
	b	.L2214
.L2213:
	ldrh	w3, [x1,560]
	mov	w0, w2
	cmp	w3, w19
	beq	.L2215
	ldrh	w3, [x1,608]
	cmp	w3, w19
	beq	.L2215
	ldrh	w1, [x1,656]
	cmp	w1, w19
	beq	.L2215
.L2214:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w19
	add	x0, x20, 512
	bl	List_remove_node
	ldrh	w0, [x20,536]
	sub	w0, w0, #1
	strh	w0, [x20,536]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L2215
.L2212:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2215:
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
	adrp	x20, .LANCHOR2
	beq	.L2220
	add	x21, x20, :lo12:.LANCHOR2
	ubfiz	x22, x19, 1, 16
	ldr	x0, [x21,520]
	ldrh	w2, [x0,x22]
	cbnz	w2, .L2221
	adrp	x0, .LC129
	mov	w1, w19
	add	x0, x0, :lo12:.LC129
	add	x20, x21, 544
	bl	printk
	ldr	x0, [x21,520]
	mov	w1, 32
	strh	w1, [x0,x22]
	mov	x0, x20
	mov	w1, w19
	bl	test_node_in_list
	cbz	w0, .L2222
	mov	w1, w19
	mov	x0, x20
	bl	List_remove_node
	ldrh	w0, [x21,552]
	sub	w0, w0, #1
	strh	w0, [x21,552]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x2, [x21,520]
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
	mov	w1, w19
	ldrh	w2, [x2,x22]
	bl	printk
.L2222:
	mov	w0, w19
	bl	FtlGcRefreshBlock
	b	.L2229
.L2221:
	sub	w2, w2, #1
	strh	w2, [x0,x22]
.L2220:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w1, [x20,3880]
	cmp	w1, w0
	bne	.L2224
	strh	w19, [x20,3880]
.L2229:
	mov	w0, 0
	b	.L2223
.L2224:
	cmp	w1, w19
	mov	w0, 0
	beq	.L2223
	mov	w0, w1
	bl	update_vpc_list
	cmp	w0, wzr
	cset	w0, ne
	strh	w19, [x20,3880]
.L2223:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	ldrh	w1, [x0,4]
	cbz	w1, .L2230
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2230
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR2
	adrp	x22, .LANCHOR0
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR2
	add	x21, x21, 1508
	ldrh	w0, [x19,x0,lsl 1]
.L2234:
	cmp	w0, w20
	bne	.L2241
.L2236:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,2472]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2235
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2235:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2234
.L2241:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2237
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbnz	w0, .L2237
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w20
	bne	.L2237
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2236
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2230
.L2237:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,204]
	cbz	w2, .L2230
	cmp	w1, 1
	bne	.L2230
	ldrh	w2, [x19,2]
	ldrh	w1, [x0,2546]
	cmp	w2, w1
	bcc	.L2230
	adrp	x2, .LANCHOR2+520
	ldrh	w1, [x19]
	ldrh	w4, [x19,4]
	lsl	x1, x1, 1
	ldr	x3, [x2,#:lo12:.LANCHOR2+520]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,2544]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2230:
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
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	mov	w22, 65535
	add	x0, x0, 8
	adrp	x23, .LANCHOR0
	add	x21, x21, 1508
	ldrh	w0, [x19,x0,lsl 1]
.L2243:
	cmp	w0, w22
	adrp	x20, .LANCHOR0
	bne	.L2258
.L2244:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,2472]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2245
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2245:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2243
.L2258:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2246
	add	x1, x23, :lo12:.LANCHOR0
	ldrb	w1, [x1,204]
	cbnz	w1, .L2246
	ldrh	w1, [x19,2]
	ldrh	w1, [x21,w1,sxtw 1]
	cmp	w1, w22
	bne	.L2246
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2244
.L2246:
	ldrh	w1, [x19,4]
	adrp	x21, .LANCHOR2
	ldrh	w22, [x19,2]
	mov	w23, 65535
	add	x21, x21, :lo12:.LANCHOR2
	sub	w1, w1, #1
	orr	w22, w22, w0, lsl 10
	strh	w1, [x19,4]
	mov	w24, w23
	add	x21, x21, 1508
.L2247:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,2472]
.L2249:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2248
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2248:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2249
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2254
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbnz	w0, .L2251
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w24
	bne	.L2251
	ldrh	w1, [x19,4]
	cbz	w1, .L2251
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2247
.L2251:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,204]
	cbz	w0, .L2254
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,2546]
	cmp	w1, w0
	bcc	.L2254
	adrp	x0, .LANCHOR2+520
	ldrh	w1, [x19]
	ldrh	w3, [x19,4]
	lsl	x1, x1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR2+520]
	ldrh	w2, [x0,x1]
	sub	w2, w2, w3
	strh	w2, [x0,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x20,2544]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2254:
	mov	w0, w22
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x23, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	ldr	w0, [x19,424]
	stp	x27, x28, [sp,80]
	cbnz	w0, .L2261
	adrp	x26, .LANCHOR4
	ldr	x21, [x19,3664]
	add	x22, x26, :lo12:.LANCHOR4
	ldr	x1, [x19,3616]
	ldrh	w2, [x19,784]
	add	x20, x19, 456
	adrp	x24, .LANCHOR0
	add	x22, x22, 1704
	str	x1, [x22,8]
	add	x25, x24, :lo12:.LANCHOR0
	str	x21, [x22,16]
	strh	w2, [x21,2]
	mov	w2, -3932
	strh	w2, [x21]
	str	w0, [x21,12]
	ldr	w2, [x19,792]
	str	w2, [x21,4]
	str	w0, [x21,8]
	mov	w0, 19539
	ldrh	w1, [x19,560]
	movk	w0, 0x4654, lsl 16
	strh	w1, [x20,14]
	ldrh	w1, [x19,562]
	ldrb	w2, [x19,566]
	str	w0, [x19,456]
	mov	w0, 99
	orr	w1, w2, w1, lsl 6
	strh	w1, [x20,16]
	ldrh	w1, [x19,608]
	movk	w0, 0x5000, lsl 16
	strh	w1, [x20,18]
	ldrh	w1, [x19,610]
	str	w0, [x20,4]
	ldrb	w2, [x19,614]
	ldrh	w0, [x19,790]
	strh	w0, [x20,8]
	orr	w1, w2, w1, lsl 6
	ldrh	w0, [x25,2494]
	strb	w0, [x20,10]
	ldrb	w0, [x19,568]
	strb	w0, [x20,11]
	strh	w1, [x20,20]
	ldrb	w0, [x19,616]
	ldrh	w1, [x19,656]
	strb	w0, [x20,12]
	strh	w1, [x20,22]
	ldrh	w1, [x19,658]
	ldrb	w0, [x19,664]
	strb	w0, [x20,13]
	ldr	w0, [x19,760]
	str	w0, [x20,32]
	ldr	w0, [x19,752]
	str	w0, [x20,40]
	ldr	w0, [x19,756]
	ldrb	w2, [x19,662]
	str	w0, [x20,36]
	ldrh	w0, [x19,1456]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x20,44]
	ldrh	w0, [x19,1458]
	strh	w0, [x20,46]
	ldrh	w2, [x25,2554]
	ldr	x0, [x22,8]
	strh	w1, [x20,24]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x22,8]
	mov	x1, x20
	mov	w2, 48
	mov	x20, x26
	bl	ftl_memcpy
	ldr	x0, [x22,8]
	ldrh	w2, [x25,2480]
	ldr	x1, [x19,520]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x25,2480]
	ldr	x1, [x22,8]
	ubfiz	x0, x2, 1, 16
	lsr	w2, w2, 3
	add	x0, x0, 51
	add	w2, w2, 4
	and	x0, x0, 262140
	add	x0, x1, x0
	ldr	x1, [x24,#:lo12:.LANCHOR0]
	bl	ftl_memcpy
	ldrh	w0, [x25,2588]
	cbz	w0, .L2262
	ldrh	w0, [x25,2480]
	ldrh	w2, [x25,2580]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x22,8]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,3760]
	bl	ftl_memcpy
.L2262:
	mov	w0, 0
	mov	w22, 0
	mov	w25, 65535
	add	x26, x20, :lo12:.LANCHOR4
	add	x19, x23, :lo12:.LANCHOR2
	bl	FtlUpdateVaildLpn
.L2263:
	ldrh	w1, [x19,784]
	ldr	x0, [x19,3616]
	ldrh	w2, [x19,786]
	str	x0, [x26,1712]
	ldr	x0, [x19,3664]
	str	x0, [x26,1720]
	orr	w0, w2, w1, lsl 10
	str	w0, [x26,1708]
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w0, [x0,2546]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L2264
	ldrh	w25, [x19,788]
	strh	wzr, [x19,786]
	strh	w1, [x19,788]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,752]
	str	w1, [x19,792]
	add	w2, w1, 1
	str	w2, [x19,752]
	ubfiz	w2, w0, 10, 16
	str	w2, [x26,1708]
	strh	w0, [x19,784]
	str	w1, [x21,4]
	strh	w0, [x21,2]
.L2264:
	add	x1, x24, :lo12:.LANCHOR0
	ldrb	w0, [x1,72]
	cbz	w0, .L2265
	ldr	x0, [x19,3616]
	ldrh	w1, [x1,2554]
	bl	js_hash
	str	w0, [x21,12]
.L2265:
	mov	w1, 1
	add	x0, x26, 1704
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,786]
	ldr	w2, [x26,1704]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x19,786]
	bne	.L2266
	cmp	w1, 1
	bne	.L2267
	add	x1, x24, :lo12:.LANCHOR0
	ldrh	w1, [x1,2546]
	sub	w1, w1, #1
	strh	w1, [x19,786]
.L2267:
	add	w22, w22, 1
	uxth	w22, w22
	cmp	w22, 3
	bls	.L2263
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	mov	w2, w22
	add	x23, x23, :lo12:.LANCHOR2
	ldr	w1, [x20,1708]
	bl	printk
	mov	w0, 1
	str	w0, [x23,424]
	b	.L2261
.L2266:
	cmp	w1, 1
	beq	.L2263
	cmp	w2, 256
	beq	.L2263
	mov	w0, 65535
	cmp	w25, w0
	beq	.L2261
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L2261:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	add	x1, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	w1, [x1,424]
	cbnz	w1, .L2276
	adrp	x21, .LANCHOR0
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w22, [x2,204]
	cbz	w22, .L2291
	ldrb	w22, [x0,8]
	cmp	w22, 1
	bne	.L2287
	ldrh	w24, [x0,4]
	b	.L2278
.L2287:
	mov	w22, w1
.L2291:
	mov	w24, 12
.L2278:
	mov	x19, x0
	mov	w0, -1
	str	w0, [x29,96]
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 61589
	ldr	x23, [x0,3664]
	ldr	x1, [x0,3616]
	str	x1, [x29,80]
	mov	w1, -3
	str	x23, [x29,88]
	str	w1, [x23,8]
	mov	w1, -2
	str	w1, [x23,12]
	ldrh	w1, [x19]
	strh	w1, [x23,2]
	strh	wzr, [x23]
	ldr	x1, [x0,3616]
	str	w2, [x1]
	ldr	x1, [x0,3616]
	mov	w0, 22136
	movk	w0, 0x1234, lsl 16
	str	w0, [x1,4]
.L2279:
	sub	w24, w24, #1
	cmn	w24, #1
	beq	.L2282
	ldrh	w0, [x19,4]
	cbnz	w0, .L2280
.L2282:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19]
	ldrh	w3, [x19,4]
	add	x21, x21, :lo12:.LANCHOR0
	lsl	x0, x0, 1
	ldr	x2, [x20,520]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x21,2544]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	b	.L2276
.L2280:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2282
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, w22
	mov	w3, 0
	ldr	w0, [x1,756]
	str	w0, [x23,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,756]
	add	x0, x29, 72
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2279
.L2276:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 128
	ret
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	adrp	x22, .LANCHOR0
	ldr	x20, [x0,16]
	ldr	x25, [x0,40]
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19,10]
	ldrh	w2, [x19,8]
	sub	w1, w1, #4
	cmp	w2, w1
	blt	.L2293
	ubfiz	x0, x0, 1, 16
	ldrh	w24, [x20,x0]
	cbz	w24, .L2293
	ldr	w1, [x19,52]
	cbnz	w1, .L2293
	mov	w2, 1
	str	w2, [x19,52]
	strh	w1, [x20,x0]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2546]
	cmp	w1, w0
	bcc	.L2294
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2294:
	adrp	x20, .LANCHOR4
	mov	w21, 0
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x26, .LANCHOR2
	add	x20, x20, 1704
.L2295:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L2304
	ubfiz	x0, x21, 2, 16
	add	x27, x25, x0
	ldr	w1, [x25,x0]
	cmp	w24, w1, lsr 10
	bne	.L2296
	add	x1, x26, :lo12:.LANCHOR2
	ldr	x2, [x1,3624]
	ldr	x23, [x1,3664]
	mov	w1, 1
	str	x2, [x20,8]
	mov	w2, w1
	str	x23, [x20,16]
	ldr	w0, [x25,x0]
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L2297
.L2299:
	adrp	x1, .LANCHOR4
	str	wzr, [x27]
	add	x1, x1, :lo12:.LANCHOR4
	adrp	x0, .LC132
	ldrh	w2, [x23,8]
	add	x0, x0, :lo12:.LC132
	ldr	w1, [x1,1708]
	bl	printk
	adrp	x0, .LANCHOR2
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR2
	str	w1, [x0,424]
	b	.L2298
.L2297:
	ldrh	w0, [x23,8]
	cmp	w0, w21
	bne	.L2299
	ldrh	w1, [x23]
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L2299
	ldr	x2, [x20,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L2296:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2295
.L2304:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L2293:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x22,2546]
	cmp	w1, w0
	bcc	.L2298
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2298:
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR2
	add	x1, x24, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	ldr	w20, [x1,424]
	ldr	x21, [x0,16]
	ldr	x26, [x0,40]
	cbnz	w20, .L2306
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L2307
	ldrh	w0, [x19,8]
	add	w0, w0, 1
	strh	w0, [x19,8]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x21]
	strh	w20, [x19,2]
	ldr	w0, [x19,48]
	strh	w20, [x19]
	add	w0, w0, 1
	str	w0, [x19,48]
	b	.L2306
.L2307:
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x19,2]
	ldr	x23, [x1,3664]
	adrp	x22, .LANCHOR0
	ldrh	w25, [x21,x0]
	adrp	x21, .LANCHOR4
	add	x0, x21, :lo12:.LANCHOR4
	orr	w2, w2, w25, lsl 10
	str	w2, [x0,1708]
	ldr	x2, [x1,3616]
	str	x2, [x0,1712]
	str	x23, [x0,1720]
	ldr	w0, [x19,48]
	str	w0, [x23,4]
	mov	w0, -1291
	strh	w0, [x23,8]
	ldrh	w0, [x19,4]
	strh	w0, [x23]
	add	x0, x22, :lo12:.LANCHOR0
	strh	w25, [x23,2]
	ldrh	w2, [x0,2546]
	ldr	x0, [x1,3616]
	mov	w1, 255
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, w20
.L2308:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L2315
	ubfiz	x3, x0, 2, 32
	ldr	w1, [x26,x3]
	cmp	w25, w1, lsr 10
	bne	.L2309
	add	x2, x24, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x4, [x2,3616]
	ubfiz	x1, x20, 3, 16
	str	w0, [x4,x1]
	ldr	x2, [x2,3616]
	ldr	w3, [x26,x3]
	add	x1, x2, x1
	str	w3, [x1,4]
.L2309:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L2308
.L2315:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w0, [x22,72]
	cbz	w0, .L2311
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w1, [x22,2554]
	ldr	x0, [x0,1712]
	bl	js_hash
	str	w0, [x23,12]
.L2311:
	mov	w1, 1
	add	x0, x21, :lo12:.LANCHOR4
	add	x0, x0, 1704
	mov	w2, w1
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	strh	w0, [x19,2]
	mov	x0, x19
	bl	ftl_map_blk_gc
.L2306:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x27, x28, [sp,80]
	adrp	x27, .LANCHOR4
	mov	x28, x0
	stp	x23, x24, [sp,48]
	add	x23, x27, :lo12:.LANCHOR4
	adrp	x24, .LC133
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w26, w1
	str	x2, [x29,104]
	mov	w22, 0
	add	x23, x23, 1704
	add	x24, x24, :lo12:.LC133
.L2317:
	adrp	x19, .LANCHOR2
	adrp	x20, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,736]
	add	w0, w0, 1
	str	w0, [x1,736]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x28,2]
	ldrh	w0, [x0,2546]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L2318
	ldrh	w1, [x28]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2319
.L2318:
	mov	x0, x28
	bl	Ftl_write_map_blk_to_last_page
.L2319:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	w1, [x2,424]
	cbnz	w1, .L2320
	ldrh	w3, [x28]
	adrp	x21, .LANCHOR4
	ldr	x0, [x28,16]
	ldrh	w25, [x0,x3,lsl 1]
	add	x3, x27, :lo12:.LANCHOR4
	ldrh	w0, [x28,2]
	add	x3, x3, 1704
	orr	w0, w0, w25, lsl 10
	str	w0, [x3,4]
	ldr	x0, [x29,104]
	str	x0, [x3,8]
	ldr	x0, [x2,3664]
	mov	w2, 16
	str	x0, [x3,16]
	str	x3, [x29,96]
	bl	ftl_memset
	ldr	x3, [x29,96]
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x28,48]
	ldr	x2, [x3,16]
	str	w0, [x2,4]
	strh	w26, [x2,8]
	ldrh	w0, [x28,4]
	strh	w0, [x2]
	strh	w25, [x2,2]
	ldrb	w0, [x1,72]
	cbz	w0, .L2321
	ldr	x0, [x3,8]
	ldrh	w1, [x1,2554]
	str	x2, [x29,96]
	bl	js_hash
	ldr	x2, [x29,96]
	str	w0, [x2,12]
.L2321:
	mov	w1, 1
	mov	x0, x23
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w0, [x28,2]
	add	w0, w0, 1
	uxth	w0, w0
	strh	w0, [x28,2]
	ldr	w1, [x23]
	cmn	w1, #1
	bne	.L2322
	ldr	w1, [x23,4]
	mov	x0, x24
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	ldrh	w0, [x28,2]
	cmp	w0, 2
	bhi	.L2323
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20,2546]
	sub	w0, w0, #1
	strh	w0, [x28,2]
.L2323:
	cmp	w22, 3
	bls	.L2317
	add	x21, x21, :lo12:.LANCHOR4
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
	mov	w2, w22
	add	x19, x19, :lo12:.LANCHOR2
	ldr	w1, [x21,1708]
	bl	printk
	mov	w0, 1
	str	w0, [x19,424]
	b	.L2320
.L2322:
	cbz	w1, .L2325
	strh	w25, [x28,60]
.L2325:
	cmp	w0, 1
	beq	.L2326
	cmp	w1, 256
	beq	.L2326
	ldr	w0, [x28,56]
	cbz	w0, .L2327
.L2326:
	str	wzr, [x28,56]
	b	.L2317
.L2327:
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x0, [x28,40]
	ldr	w1, [x21,1708]
	str	w1, [x0,w26,uxtw 2]
.L2320:
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
	.global	flush_l2p_region
	.type	flush_l2p_region, %function
flush_l2p_region:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ubfiz	x19, x0, 4, 16
	add	x20, x20, :lo12:.LANCHOR2
	add	x0, x20, 3800
	ldr	x1, [x20,704]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20,704]
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
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	stp	x29, x30, [sp, -144]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	mov	x23, x0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	ldrh	w25, [x23,6]
	adrp	x21, .LANCHOR2
	ldr	x0, [x0,40]
	mov	w19, 0
	lsl	w2, w25, 2
	ldrh	w27, [x23,8]
	str	x0, [x29,136]
	adrp	x26, .LANCHOR0
	ldr	x24, [x23,16]
	ldr	x28, [x23,24]
	bl	ftl_memset
	add	x1, x21, :lo12:.LANCHOR2
	adrp	x0, .LANCHOR4
	add	x4, x0, :lo12:.LANCHOR4
	str	x0, [x29,112]
	add	x4, x4, 1704
	str	x21, [x29,128]
	ldr	x2, [x1,3616]
	sub	w0, w27, #1
	ldr	x20, [x1,3664]
	mov	w1, -1
	str	x2, [x4,8]
	str	x20, [x4,16]
	mov	x21, x4
	strh	w1, [x23]
	strh	w1, [x23,2]
	mov	w1, 1
	str	wzr, [x23,52]
	str	wzr, [x23,48]
	str	w1, [x23,56]
	str	w0, [x29,108]
.L2336:
	cmp	w19, w27
	adrp	x22, .LANCHOR0
	bge	.L2354
	ldr	w0, [x29,108]
	sxtw	x2, w19
	cmp	w19, w0
	bne	.L2337
	lsl	x0, x2, 1
	mov	w1, 1
	add	x21, x24, x0
	str	x2, [x29,128]
	mov	w26, 0
	mov	w27, -1
	ldrh	w0, [x24,x0]
	bl	FtlGetLastWrittenPage
	strh	w19, [x23]
	ldr	x2, [x29,128]
	sxth	w24, w0
	add	w0, w0, 1
	strh	w0, [x23,2]
	add	w24, w24, 1
	ldr	w0, [x28,x2,lsl 2]
	str	w0, [x23,48]
	ldr	x0, [x29,112]
	add	x19, x0, :lo12:.LANCHOR4
	add	x19, x19, 1704
.L2338:
	cmp	w26, w24
	bge	.L2354
	ldrh	w0, [x21]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w26, w0, lsl 10
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w0, [x1,72]
	cbz	w0, .L2339
	ldr	x0, [x19,16]
	ldr	w28, [x0,12]
	cbz	w28, .L2339
	ldr	x0, [x19,8]
	ldrh	w1, [x1,2554]
	bl	js_hash
	cmp	w28, w0
	beq	.L2339
	str	w27, [x19]
.L2339:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2340
	ldrh	w0, [x20,8]
	cmp	w0, w25
	bcs	.L2340
	ldrh	w2, [x20]
	ldrh	w1, [x23,4]
	cmp	w2, w1
	bne	.L2340
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x19,4]
	str	w1, [x2,x0]
.L2340:
	add	w26, w26, 1
	sxth	w26, w26
	b	.L2338
.L2354:
	mov	x0, x23
	add	x22, x22, :lo12:.LANCHOR0
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x23,2]
	ldrh	w0, [x22,2546]
	cmp	w1, w0
	bne	.L2343
	mov	x0, x23
	bl	ftl_map_blk_alloc_new_blk
	b	.L2343
.L2337:
	ldr	x0, [x29,128]
	lsl	x2, x2, 1
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,3616]
	str	x0, [x21,8]
	add	x0, x24, x2
	str	x0, [x29,120]
	add	x0, x26, :lo12:.LANCHOR0
	ldrh	w1, [x24,x2]
	ldrh	w0, [x0,2546]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x21,4]
	mov	w2, w1
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L2355
	ldrh	w1, [x20]
	mov	w22, 0
	ldrh	w0, [x23,4]
	cmp	w1, w0
	bne	.L2376
	ldrh	w1, [x20,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2376
	mov	w5, w22
.L2345:
	add	x0, x26, :lo12:.LANCHOR0
	ldrh	w0, [x0,2546]
	sub	w0, w0, #1
	cmp	w5, w0
	bge	.L2348
	ldr	x0, [x29,128]
	sbfiz	x1, x5, 3, 32
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x2, [x0,3616]
	ldrh	w0, [x2,x1]
	cmp	w0, w25
	bcs	.L2346
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x1,4]
	str	w1, [x2,x0]
.L2346:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L2345
.L2355:
	mov	w22, 0
.L2376:
	add	x6, x26, :lo12:.LANCHOR0
	ldrh	w0, [x6,2546]
	cmp	w22, w0
	bge	.L2348
	ldr	x0, [x29,120]
	mov	w1, 1
	mov	w2, w1
	str	x6, [x29,96]
	ldrh	w0, [x0]
	orr	w0, w22, w0, lsl 10
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	x6, [x29,96]
	ldrb	w0, [x6,72]
	cbz	w0, .L2349
	ldr	x0, [x21,16]
	ldr	w2, [x0,12]
	cbz	w2, .L2349
	ldr	x0, [x21,8]
	ldrh	w1, [x6,2554]
	str	x2, [x29,96]
	bl	js_hash
	ldr	x2, [x29,96]
	cmp	w2, w0
	beq	.L2349
	mov	w0, -1
	str	w0, [x21]
.L2349:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L2350
	ldrh	w0, [x20,8]
	cmp	w0, w25
	bcs	.L2350
	ldrh	w2, [x20]
	ldrh	w1, [x23,4]
	cmp	w2, w1
	bne	.L2350
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x21,4]
	str	w1, [x2,x0]
.L2350:
	add	w5, w22, 1
	sxth	w22, w5
	b	.L2376
.L2348:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2336
.L2343:
	mov	x0, x23
	bl	ftl_map_blk_gc
	mov	x0, x23
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
	adrp	x1, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR4
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x0, x0, 1768
	add	x29, sp, 0
	ldrh	w2, [x1,2564]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,2590]
	strh	w2, [x0,8]
	ldrh	w2, [x1,2566]
	ldr	x1, [x1,2592]
	str	x1, [x0,16]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	strh	w2, [x0,6]
	ldr	x2, [x1,3744]
	str	x2, [x0,24]
	ldr	x2, [x1,3736]
	ldr	x1, [x1,3752]
	str	x2, [x0,32]
	str	x1, [x0,40]
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
	add	x0, x0, 3800
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
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	stp	x25, x26, [sp,64]
	mov	w26, w0
	add	x0, x23, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	mov	w28, w1
	add	w1, w26, w1
	ldrh	w21, [x0,2552]
	mov	x25, x2
	ldrh	w0, [x0,2540]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L2380
	adrp	x24, .LANCHOR4
	lsr	w21, w26, w21
	add	x24, x24, :lo12:.LANCHOR4
	mov	w20, 0
	adrp	x27, .LANCHOR2
	add	x24, x24, 1768
.L2381:
	cbz	w28, .L2380
	adrp	x0, .LANCHOR2
	add	x2, x0, :lo12:.LANCHOR2
	ldr	x0, [x2,3752]
	ldr	w4, [x0,w21,uxtw 2]
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2550]
	uxth	w0, w28
	udiv	w22, w26, w1
	msub	w22, w22, w1, w26
	sub	w19, w1, w22
	uxth	w19, w19
	cmp	w19, w28
	csel	w19, w0, w19, hi
	cmp	w19, w1
	beq	.L2383
	cbz	w4, .L2383
	ldr	x0, [x2,3632]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w4, [x29,124]
	str	xzr, [x29,136]
	bl	FlashReadPages
	b	.L2384
.L2383:
	add	x0, x27, :lo12:.LANCHOR2
	add	x2, x23, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x0,3632]
	ldrh	w2, [x2,2554]
	bl	ftl_memset
.L2384:
	add	x5, x27, :lo12:.LANCHOR2
	lsl	w4, w19, 9
	ubfiz	x22, x22, 9, 16
	mov	w2, w4
	mov	x1, x25
	str	x4, [x29,104]
	ldr	x0, [x5,3632]
	sub	w28, w28, w19
	str	x5, [x29,96]
	add	w26, w26, w19
	add	x0, x0, x22
	bl	ftl_memcpy
	ldr	x5, [x29,96]
	mov	w1, w21
	mov	x0, x24
	add	w21, w21, 1
	ldr	x2, [x5,3632]
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	x4, [x29,104]
	csinv	w20, w20, wzr, ne
	add	x25, x25, x4, sxtw
	b	.L2381
.L2380:
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
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	adrp	x0, .LANCHOR2
	mov	w1, 19539
	add	x0, x0, :lo12:.LANCHOR2
	movk	w1, 0x4654, lsl 16
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 856
	add	x29, sp, 0
	ldr	w3, [x0,856]
	cmp	w3, w1
	bne	.L2394
	mov	w1, 99
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	ldr	w1, [x0,744]
	str	w1, [x2,88]
	ldr	w1, [x0,748]
	str	w1, [x2,92]
	ldr	w1, [x0,740]
	str	w1, [x2,8]
	ldr	w1, [x0,728]
	str	w1, [x2,12]
	ldr	w1, [x0,720]
	str	w1, [x2,16]
	ldr	w1, [x0,736]
	str	w1, [x2,20]
	ldr	w1, [x0,764]
	str	w1, [x2,28]
	ldr	w1, [x0,448]
	str	w1, [x2,32]
	ldr	w1, [x0,724]
	str	w1, [x2,36]
	ldr	w1, [x0,732]
	str	w1, [x2,40]
	ldr	w1, [x0,772]
	str	w1, [x2,44]
	ldr	w1, [x0,776]
	str	w1, [x2,48]
	ldr	w1, [x0,848]
	ldr	w0, [x0,3564]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2394:
	ldp	x29, x30, [sp], 16
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x1, .LANCHOR0+2372
	adrp	x2, .LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w3, [x1,#:lo12:.LANCHOR0+2372]
	mov	w1, 32
	cbz	w3, .L2397
	add	x3, x2, :lo12:.LANCHOR2
	ldr	w3, [x3,776]
	cmp	w3, 39
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2397:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,1832]
	cmp	w4, 31
	bhi	.L2398
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,1832]
.L2398:
	cbnz	w0, .L2399
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,3704]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2400
.L2399:
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x0, [x2,3704]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	ldr	x0, [x2,3704]
	ldrh	w1, [x2,3688]
	str	wzr, [x0,4]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	ldr	x2, [x2,3704]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2400:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	stp	x29, x30, [sp, -208]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	mov	w22, w1
	adrp	x1, .LANCHOR0
	stp	x23, x24, [sp,48]
	mov	w23, w0
	add	x0, x1, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	x26, x2
	add	w3, w23, w22
	ldrh	w2, [x0,2540]
	str	x1, [x29,136]
	cmp	w3, w2
	ldrh	w20, [x0,2552]
	mov	w0, -1
	bhi	.L2405
	adrp	x27, .LANCHOR4
	adrp	x0, .LC135
	add	x0, x0, :lo12:.LC135
	add	x27, x27, :lo12:.LANCHOR4
	str	x0, [x29,128]
	lsr	w20, w23, w20
	add	x0, x27, 1768
	mov	w21, 0
	str	x0, [x29,120]
.L2406:
	cbz	w22, .L2414
	adrp	x25, .LANCHOR2
	add	x4, x25, :lo12:.LANCHOR2
	ldr	x0, [x4,3752]
	ldr	w3, [x0,w20,uxtw 2]
	ldr	x0, [x29,136]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w19, [x0,2550]
	uxth	w0, w22
	udiv	w24, w23, w19
	msub	w24, w24, w19, w23
	sub	w19, w19, w24
	uxth	w19, w19
	cmp	w19, w22
	csel	w19, w0, w19, hi
	lsl	w28, w19, 9
	cbz	w3, .L2408
	ldr	x0, [x4,3632]
	mov	w1, 1
	str	x0, [x29,160]
	mov	w2, w1
	add	x0, x29, 152
	str	w3, [x29,156]
	str	x3, [x29,104]
	str	x4, [x29,112]
	str	xzr, [x29,168]
	bl	FlashReadPages
	ldr	w0, [x29,152]
	ldr	x3, [x29,104]
	cmn	w0, #1
	ldr	w0, [x27,1704]
	csinv	w21, w21, wzr, ne
	cmp	w0, 256
	bne	.L2410
	ldr	x0, [x29,128]
	mov	w1, w20
	mov	w2, w3
	bl	printk
	ldr	x4, [x29,112]
	mov	w1, w20
	ldr	x0, [x29,120]
	ldr	x2, [x4,3632]
	bl	FtlMapWritePage
.L2410:
	add	x25, x25, :lo12:.LANCHOR2
	ubfiz	x24, x24, 9, 16
	mov	x0, x26
	mov	w2, w28
	ldr	x1, [x25,3632]
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L2411
.L2408:
	mov	x0, x26
	mov	w1, w3
	mov	w2, w28
	bl	ftl_memset
.L2411:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x26, x26, x28, sxtw
	b	.L2406
.L2414:
	mov	w0, w21
.L2405:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 208
	ret
	.size	FtlVendorPartRead, .-FtlVendorPartRead
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
	ldrh	w1, [x19,3688]
	ldr	x2, [x19,3704]
	bl	FtlVendorPartRead
	ldr	x0, [x19,3704]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L2416
	adrp	x0, .LC75
	adrp	x1, .LC136
	add	x1, x1, :lo12:.LC136
	add	x0, x0, :lo12:.LC75
	bl	printk
	ldrh	w2, [x19,3688]
	mov	w1, 0
	ldr	x0, [x19,3704]
	lsl	w2, w2, 9
	bl	ftl_memset
.L2416:
	mov	w0, 0
	ldr	x19, [sp,16]
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x21, [sp,32]
	add	x20, x19, :lo12:.LANCHOR2
	add	x21, x20, 856
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,856]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L2418
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,856]
.L2418:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,856]
	cmp	w3, w2
	bne	.L2419
	ldr	w2, [x0,944]
	str	w2, [x0,744]
	ldr	w2, [x0,948]
	str	w2, [x0,748]
	ldr	w2, [x0,864]
	str	w2, [x0,740]
	ldr	w2, [x0,868]
	str	w2, [x0,728]
	ldr	w2, [x0,872]
	str	w2, [x0,720]
	ldr	w2, [x0,876]
	str	w2, [x0,736]
	ldr	w2, [x0,884]
	str	w2, [x0,764]
	ldr	w2, [x0,888]
	str	w2, [x0,448]
	ldr	w2, [x0,892]
	str	w2, [x0,724]
	ldr	w2, [x0,896]
	str	w2, [x0,732]
	ldr	w2, [x0,900]
	str	w2, [x0,772]
	ldr	w1, [x0,916]
	ldr	w2, [x0,904]
	str	w2, [x0,776]
	str	w1, [x0,848]
.L2419:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 34661
	movk	w1, 0x1234, lsl 16
	add	x0, x0, 856
	adrp	x20, .LANCHOR0
	ldr	w2, [x0,68]
	str	wzr, [x0,2708]
	cmp	w2, w1
	bne	.L2420
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w2, [x1,204]
	cbz	w2, .L2421
	str	wzr, [x0,68]
	bl	Ftl_save_ext_data
	b	.L2420
.L2421:
	mov	w0, 1
	str	w0, [x1,2372]
	adrp	x0, .LC75
	adrp	x1, .LC137
	add	x0, x0, :lo12:.LC75
	add	x1, x1, :lo12:.LC137
	bl	printk
.L2420:
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x21, [sp,32]
	ldrh	w1, [x20,2536]
	ldr	w2, [x19,760]
	ldr	w0, [x19,764]
	madd	w0, w1, w2, w0
	ldrh	w1, [x20,2480]
	udiv	w0, w0, w1
	str	w0, [x19,768]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	FtlMapBlkWriteDumpData
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	ldr	w1, [x0,56]
	ldrh	w20, [x0,6]
	ldr	x4, [x0,40]
	cbz	w1, .L2422
	adrp	x3, .LANCHOR2
	str	wzr, [x0,56]
	add	x3, x3, :lo12:.LANCHOR2
	ldr	w1, [x3,424]
	cbnz	w1, .L2422
	adrp	x2, .LANCHOR4
	sub	w20, w20, #1
	add	x1, x2, :lo12:.LANCHOR4
	mov	x21, x0
	uxth	w20, w20
	ldr	x0, [x3,3624]
	ldr	x3, [x3,3664]
	add	x1, x1, 1704
	str	x3, [x1,16]
	ubfiz	x3, x20, 2, 16
	str	x0, [x1,8]
	mov	x19, x2
	ldr	w3, [x4,x3]
	str	w3, [x1,4]
	cbz	w3, .L2426
	mov	x0, x1
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L2427
.L2426:
	adrp	x2, .LANCHOR0+2554
	mov	w1, 255
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2554]
	bl	ftl_memset
.L2427:
	add	x2, x19, :lo12:.LANCHOR4
	mov	x0, x21
	mov	w1, w20
	ldr	x2, [x2,1712]
	bl	FtlMapWritePage
.L2422:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.type	FlashReadFacBbtData.part.17, %function
FlashReadFacBbtData.part.17:
	stp	x29, x30, [sp, -32]!
	mov	w3, 0
	adrp	x4, .LANCHOR4
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w7, 1
	mov	x19, x0
	cbnz	w1, .L2431
.L2433:
	add	x0, x4, :lo12:.LANCHOR4
	ldr	w1, [x0,1636]
	cmp	w3, w1
	bcs	.L2431
	ubfx	x5, x3, 5, 11
	ldr	x0, [x0,1600]
	lsl	x5, x5, 2
	lsl	w6, w7, w3
	add	w3, w3, 1
	ldr	w1, [x0,x5]
	uxth	w3, w3
	orr	w6, w1, w6
	str	w6, [x0,x5]
	b	.L2433
.L2431:
	add	x4, x4, :lo12:.LANCHOR4
	mov	x0, x19
	ldr	x1, [x4,1600]
	bl	ftl_memcpy
	mov	w2, 4
	adrp	x0, .LC138
	mov	x1, x19
	add	x0, x0, :lo12:.LC138
	mov	w3, w2
	bl	rknand_print_hex
	ldr	x19, [sp,16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadFacBbtData.part.17, .-FlashReadFacBbtData.part.17
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	stp	x29, x30, [sp, -144]!
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	add	x4, x4, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x22, x0
	mov	w23, w1
	ldrh	w0, [x4,190]
	mov	w25, w2
	ldrh	w3, [x4,188]
	adrp	x4, .LANCHOR4
	mov	x21, x4
	mov	w26, 61664
	mul	w3, w3, w0
	add	x0, x4, :lo12:.LANCHOR4
	uxth	w3, w3
	sub	w20, w3, #1
	ldr	x1, [x0,1600]
	mul	w24, w23, w3
	ldr	x0, [x0,1656]
	uxth	w20, w20
	str	x1, [x29,96]
	sub	w19, w3, #16
	str	x0, [x29,104]
.L2436:
	cmp	w20, w19
	ble	.L2444
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L2437
	add	x0, x21, :lo12:.LANCHOR4
	ldr	x0, [x0,1656]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L2437
	mov	w0, w22
	cbz	x22, .L2438
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.17
	b	.L2438
.L2437:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2436
.L2444:
	mov	w0, -1
.L2438:
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
	adrp	x0, .LANCHOR0+88
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrb	w2, [x0,13]
	ldrh	w19, [x0,14]
	add	x0, x20, :lo12:.LANCHOR4
	mul	w19, w2, w19
	ldr	x0, [x0,1648]
	uxth	w19, w19
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L2446
.L2450:
	mov	w1, 0
	b	.L2447
.L2446:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L2448:
	cmp	w2, w19
	bge	.L2447
	add	x3, x20, :lo12:.LANCHOR4
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,1648]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L2449
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L2449:
	cmp	w1, w0
	bcs	.L2450
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2448
.L2447:
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
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR2
	add	x0, x23, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w21, [x0,424]
	cbnz	w21, .L2456
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR4
	add	x24, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	add	x24, x24, 2624
	add	x20, x20, 1704
	mov	x25, x24
	mov	x26, x20
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L2457:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22,2494]
	cmp	w21, w0
	bcs	.L2475
	add	x1, x23, :lo12:.LANCHOR2
	ldrh	w28, [x25,12]
	ldr	x0, [x1,3616]
	ldr	x27, [x1,3664]
	mov	w1, 65535
	str	x0, [x20,8]
	cmp	w28, w1
	str	x27, [x20,16]
	beq	.L2458
	ldrh	w0, [x22,2542]
	mov	w1, 1
	mov	w2, w1
	madd	w28, w21, w0, w28
	lsl	w0, w28, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x22,2542]
	ldr	x0, [x24,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L2459
.L2458:
	mov	w1, w21
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x22,2542]
.L2461:
	sub	w22, w22, #1
	uxth	w22, w22
.L2460:
	add	x28, x19, :lo12:.LANCHOR0
	ldrh	w0, [x28,2542]
	madd	w0, w21, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2461
	add	x3, x23, :lo12:.LANCHOR2
	mov	w1, 0
	mov	w2, 16
	strh	w22, [x25,12]
	str	x3, [x29,104]
	ldr	x0, [x3,3664]
	bl	ftl_memset
	str	wzr, [x27,4]
	mov	w0, -3872
	strh	w0, [x27]
	ldr	x3, [x29,104]
	ldrh	w0, [x25,12]
	strh	w0, [x27,2]
	ldrh	w0, [x28,2542]
	ldrh	w1, [x25,12]
	ldrh	w2, [x3,3792]
	madd	w28, w21, w0, w1
	ldr	x1, [x24,32]
	lsl	w2, w2, 2
	lsl	w0, w28, 10
	str	w0, [x26,4]
	ldr	x0, [x26,8]
	bl	ftl_memcpy
	mov	w1, 1
	mov	x0, x26
	mov	w2, w1
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x26
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x26]
	cmn	w0, #1
	bne	.L2459
	mov	w0, w28
	bl	FtlBbmMapBadBlock
	b	.L2460
.L2459:
	mov	w0, w28
	add	w21, w21, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L2457
.L2475:
	mov	w20, 0
.L2464:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,2558]
	cmp	w0, w20
	bls	.L2476
	mov	w0, w20
	add	w20, w20, 1
	bl	FtlBbmMapBadBlock
	uxth	w20, w20
	b	.L2464
.L2476:
	ldrh	w20, [x21,2636]
	mov	w24, 65535
	sub	w20, w20, #1
	uxth	w20, w20
.L2466:
	add	x22, x21, 2624
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L2470
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2467
	mov	w0, w20
	bl	FlashTestBlk
	cbz	w0, .L2468
	mov	w0, w20
	bl	FtlBbmMapBadBlock
	b	.L2467
.L2468:
	ldrh	w0, [x21,2624]
	cmp	w0, w24
	bne	.L2469
	strh	w20, [x21,2624]
	b	.L2467
.L2469:
	strh	w20, [x22,4]
	b	.L2470
.L2467:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2466
.L2470:
	add	x23, x23, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 2
	ldr	x1, [x23,432]
	ldrh	w0, [x19,2624]
	str	wzr, [x19,2632]
	lsl	w0, w0, 10
	str	w0, [x1,4]
	strh	wzr, [x19,2626]
	ldr	x0, [x23,432]
	ldrh	w1, [x19,2628]
	lsl	w1, w1, 10
	str	w1, [x0,60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x19,2624]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19,2628]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19,2626]
	ldr	w0, [x19,2632]
	ldrh	w1, [x19,2628]
	add	w0, w0, 1
	str	w0, [x19,2632]
	ldrh	w0, [x19,2624]
	strh	w0, [x19,2628]
	strh	w1, [x19,2624]
	bl	FtlBbmTblFlush
.L2456:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	add	x3, x22, :lo12:.LANCHOR0
	str	x22, [x29,120]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w4, [x3,2616]
	ldrh	w27, [x3,2552]
	cmp	w0, w4
	bcs	.L2478
	add	w27, w27, 7
	adrp	x20, .LANCHOR2
	lsr	w24, w0, w27
	str	w0, [x29,116]
	add	x0, x20, :lo12:.LANCHOR2
	str	w2, [x29,112]
	mov	x26, x1
	uxth	w23, w24
	ldrh	w2, [x3,2582]
	ldr	x22, [x0,704]
	mov	x0, 0
	b	.L2479
.L2478:
	mov	w0, -1
	cbnz	w2, .L2480
	str	w0, [x1]
	b	.L2480
.L2484:
	add	x0, x0, 1
	add	x3, x22, x0, lsl 4
	ldrh	w3, [x3,-16]
	cmp	w3, w23
	beq	.L2481
.L2479:
	uxth	x19, w0
	cmp	w19, w2
	bcc	.L2484
	bl	select_l2p_ram_region
	uxth	x19, w0
	ubfiz	x21, x19, 4, 16
	mov	w1, 65535
	add	x2, x22, x21
	ldrh	w3, [x22,x21]
	cmp	w3, w1
	bne	.L2492
.L2485:
	add	x28, x20, :lo12:.LANCHOR2
	ubfiz	x0, x23, 2, 16
	ldr	x1, [x28,3760]
	ldr	w25, [x1,x0]
	cbnz	w25, .L2486
	ldr	x1, [x29,120]
	ldr	x0, [x28,704]
	add	x22, x1, :lo12:.LANCHOR0
	mov	w1, 255
	add	x0, x0, x21
	ldrh	w2, [x22,2554]
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x28,704]
	strh	w23, [x0,x21]
	ldr	x0, [x28,704]
	add	x21, x0, x21
	str	w25, [x21,4]
.L2481:
	mov	x0, 1
	lsl	x27, x0, x27
	ldr	w0, [x29,116]
	sub	w27, w27, #1
	and	w25, w0, w27
	ldr	w0, [x29,112]
	uxth	x25, w25
	cbnz	w0, .L2482
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,704]
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0,8]
	ldr	w0, [x0,x25,lsl 2]
	str	w0, [x26]
	b	.L2483
.L2482:
	add	x1, x20, :lo12:.LANCHOR2
	lsl	x0, x19, 4
	ldr	w3, [x26]
	ldr	x2, [x1,704]
	add	x2, x2, x0
	ldr	x2, [x2,8]
	str	w3, [x2,x25,lsl 2]
	strh	w23, [x1,712]
	ldr	x2, [x1,704]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2483:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x20,704]
	add	x19, x0, x19, lsl 4
	mov	w0, 0
	ldr	w1, [x19,4]
	cmn	w1, #1
	beq	.L2480
	add	w1, w1, 1
	str	w1, [x19,4]
	b	.L2480
.L2492:
	ldr	w1, [x2,4]
	tbz	w1, #31, .L2485
	bl	flush_l2p_region
	b	.L2485
.L2486:
	ldr	x0, [x28,704]
	adrp	x6, .LANCHOR4
	add	x6, x6, :lo12:.LANCHOR4
	mov	w1, 1
	add	x0, x0, x21
	add	x22, x6, 1704
	mov	w2, w1
	str	x6, [x29,104]
	str	w25, [x6,1708]
	ldr	x0, [x0,8]
	str	x0, [x6,1712]
	ldr	x0, [x28,3664]
	str	x0, [x6,1720]
	mov	x0, x22
	bl	FlashReadPages
	ldr	x0, [x22,16]
	ldr	x6, [x29,104]
	ldrh	w0, [x0,8]
	cmp	w0, w23
	beq	.L2487
	adrp	x0, .LC139
	uxth	w1, w24
	mov	w2, w25
	add	x0, x0, :lo12:.LC139
	bl	printk
	ldr	x1, [x22,16]
	mov	w2, 4
	adrp	x0, .LC100
	mov	w3, w2
	add	x0, x0, :lo12:.LC100
	bl	rknand_print_hex
	ldr	x0, [x29,120]
	mov	w2, 4
	ldr	x1, [x28,3760]
	add	x22, x0, :lo12:.LANCHOR0
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	ldrh	w3, [x22,2580]
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x28,424]
	b	.L2488
.L2487:
	ldr	w0, [x6,1704]
	cmp	w0, 256
	bne	.L2488
	and	w24, w24, 65535
	adrp	x0, .LC141
	mov	w1, w24
	mov	w2, w25
	add	x0, x0, :lo12:.LC141
	bl	printk
	ldr	x2, [x28,704]
	add	x0, x28, 3800
	mov	w1, w24
	add	x2, x2, x21
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L2488:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x0, [x1,704]
	add	x0, x0, x21
	str	wzr, [x0,4]
	ldr	x0, [x1,704]
	strh	w23, [x0,x21]
	b	.L2481
.L2480:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	add	x23, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	ldr	w0, [x23,424]
	cbnz	w0, .L2493
	ldrh	w2, [x23,564]
	cbz	w2, .L2495
	ldrb	w1, [x23,568]
	cbnz	w1, .L2495
	adrp	x21, .LANCHOR0
	ldrb	w1, [x23,567]
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w3, [x21,2544]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L2495
	ldrb	w24, [x23,570]
	ldr	w22, [x21,2616]
	ldrh	w25, [x21,2472]
	cbnz	w24, .L2493
	sub	w22, w22, #1
	add	x1, x29, 84
	mov	w0, w22
	mov	w2, w24
	bl	log2phys
	ldr	x20, [x23,3664]
	ldr	w0, [x29,84]
	ldr	x1, [x23,3616]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w22, [x29,112]
	str	x1, [x29,96]
	str	x20, [x29,104]
	str	w24, [x20,4]
	beq	.L2497
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w24
	bl	FlashReadPages
	b	.L2498
.L2497:
	ldr	x0, [x23,3616]
	mov	w1, 255
	ldrh	w2, [x21,2554]
	bl	ftl_memset
.L2498:
	add	x24, x19, :lo12:.LANCHOR2
	mov	w0, -3947
	lsl	w25, w25, 2
	strh	w0, [x20]
	mov	w23, 0
	add	x21, x24, 560
.L2499:
	cmp	w23, w25
	beq	.L2500
	ldrh	w0, [x21,4]
	cbz	w0, .L2500
	ldr	w0, [x29,92]
	add	w23, w23, 1
	str	w0, [x20,12]
	ldrh	w0, [x21]
	strh	w0, [x20,2]
	mov	x0, x21
	str	w22, [x20,8]
	bl	get_new_active_ppa
	str	w0, [x29,92]
	ldr	w1, [x24,756]
	mov	w2, 0
	str	w1, [x20,4]
	mov	w3, w2
	add	w1, w1, 1
	add	x0, x29, 88
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x24,756]
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x21]
	bl	decrement_vpc_count
	b	.L2499
.L2500:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,570]
	b	.L2493
.L2495:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,570]
.L2493:
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
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	w19, 0
	adrp	x20, .LANCHOR0
	adrp	x21, .LANCHOR2
	bl	FtlWriteDumpData
.L2511:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2582]
	cmp	w0, w19
	bls	.L2514
	add	x1, x21, :lo12:.LANCHOR2
	ubfiz	x0, x19, 4, 16
	ldr	x1, [x1,704]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2512
	mov	w0, w19
	bl	flush_l2p_region
.L2512:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2511
.L2514:
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x21, [sp,32]
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w20, [x0]
	ldr	w1, [x2,424]
	cbnz	w1, .L2516
	mov	x21, x0
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2517
	ubfiz	x1, x20, 1, 16
	ldr	x0, [x2,520]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2518
	mov	w0, w20
	bl	INSERT_DATA_LIST
	b	.L2517
.L2518:
	mov	w0, w20
	bl	INSERT_FREE_LIST
.L2517:
	add	x2, x19, :lo12:.LANCHOR2
	strb	wzr, [x21,8]
	add	x0, x2, 608
	cmp	x21, x0
	beq	.L2519
	adrp	x1, .LANCHOR0
	add	x4, x1, :lo12:.LANCHOR0
	ldrh	w3, [x4,2492]
	cmp	w3, 1
	beq	.L2519
	ldrb	w0, [x4,204]
	cbz	w0, .L2520
.L2519:
	mov	w0, 1
	strb	w0, [x21,8]
	b	.L2521
.L2520:
	add	x0, x2, 560
	cmp	x21, x0
	bne	.L2521
	cmp	w3, 3
	beq	.L2523
	ldr	w0, [x2,848]
	cmp	w0, 1
	bne	.L2524
.L2523:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x2,568]
.L2524:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x1,2372]
	cbz	w0, .L2521
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,776]
	cmp	w0, 39
	bhi	.L2521
	mov	w0, 1
	strb	w0, [x1,568]
.L2521:
	add	x3, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w1, [x3,3880]
	cmp	w1, w0
	beq	.L2526
	cmp	w20, w1
	bne	.L2527
	ubfiz	x2, x1, 1, 16
	ldr	x0, [x3,520]
	ldrh	w0, [x0,x2]
	cbz	w0, .L2528
.L2527:
	mov	w0, w1
	bl	update_vpc_list
.L2528:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,3880]
.L2526:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2516:
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlCheckVpc
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	stp	x29, x30, [sp, -128]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC109
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 216
	add	x0, x0, :lo12:.LC109
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	mov	w19, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	add	x21, x20, :lo12:.LANCHOR4
	bl	printk
	add	x0, x21, 1840
	mov	w1, 0
	mov	x2, 8192
	adrp	x22, .LANCHOR0
	bl	memset
.L2542:
	add	x0, x22, :lo12:.LANCHOR0
	adrp	x26, .LANCHOR0
	ldr	w0, [x0,2616]
	cmp	w19, w0
	bcs	.L2561
	mov	w0, w19
	add	x1, x29, 124
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,124]
	cmn	w0, #1
	beq	.L2543
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x2, x21, 1840
	ldrh	w1, [x2,w0,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x2,w0,sxtw 1]
.L2543:
	add	w19, w19, 1
	b	.L2542
.L2561:
	mov	w21, 0
	adrp	x22, .LC142
	mov	w19, w21
	adrp	x27, .LANCHOR2
	add	x28, x20, :lo12:.LANCHOR4
	add	x22, x22, :lo12:.LC142
	mov	w4, 65535
.L2545:
	add	x0, x26, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR2
	ldrh	w0, [x0,2480]
	cmp	w0, w19
	bls	.L2562
	add	x25, x27, :lo12:.LANCHOR2
	ubfiz	x24, x19, 1, 16
	add	x23, x28, 1840
	sxtw	x5, w19
	ldr	x0, [x25,520]
	ldrh	w3, [x23,w19,sxtw 1]
	ldrh	w2, [x0,x24]
	cmp	w2, w3
	beq	.L2546
	mov	x0, x22
	mov	w1, w19
	str	x4, [x29,96]
	str	x5, [x29,104]
	bl	printk
	ldr	x0, [x25,520]
	ldr	x4, [x29,96]
	ldrh	w0, [x0,x24]
	cmp	w0, w4
	beq	.L2546
	ldr	x5, [x29,104]
	ldrh	w1, [x23,x5,lsl 1]
	cmp	w1, w0
	csinc	w21, w21, wzr, ls
.L2546:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2545
.L2562:
	add	x1, x23, :lo12:.LANCHOR2
	ldr	x0, [x1,544]
	cbz	x0, .L2548
	ldr	x19, [x1,504]
	adrp	x24, .LC143
	ldrh	w25, [x1,552]
	mov	w22, 0
	sub	x19, x0, x19
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	add	x24, x24, :lo12:.LC143
	add	x20, x20, :lo12:.LANCHOR4
	mov	w26, 6
	madd	x19, x0, x19, x19
	mov	w27, 65535
	uxth	w19, w19
.L2549:
	cmp	w22, w25
	beq	.L2548
	add	x1, x23, :lo12:.LANCHOR2
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x1,520]
	ldrh	w2, [x1,x0]
	cbz	w2, .L2550
	add	x3, x20, 1840
	mov	x0, x24
	mov	w1, w19
	mov	w21, 1
	ldrh	w3, [x3,w19,sxtw 1]
	bl	printk
.L2550:
	add	x0, x23, :lo12:.LANCHOR2
	umull	x19, w19, w26
	ldr	x0, [x0,504]
	ldrh	w19, [x0,x19]
	cmp	w19, w27
	beq	.L2548
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2549
.L2548:
	adrp	x0, .LC144
	mov	w1, w21
	add	x0, x0, :lo12:.LC144
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
	.global	Ftlscanalldata
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	sub	sp, sp, #80
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	mov	w1, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR4
	mov	w20, 0
	stp	x21, x22, [sp,48]
	add	x19, x19, :lo12:.LANCHOR4
	adrp	x21, .LC146
	add	x21, x21, :lo12:.LC146
	add	x19, x19, 1704
	bl	printk
.L2564:
	adrp	x0, .LANCHOR0+2616
	ldr	w0, [x0,#:lo12:.LANCHOR0+2616]
	cmp	w20, w0
	bcs	.L2571
	mov	w0, w20
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	and	w0, w20, 2047
	cbnz	w0, .L2565
	ldr	w2, [x29,60]
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L2565:
	ldr	w0, [x29,60]
	cmn	w0, #1
	beq	.L2567
	adrp	x1, .LANCHOR2
	str	w0, [x19,4]
	add	x1, x1, :lo12:.LANCHOR2
	mov	w2, 0
	str	w20, [x19,24]
	str	wzr, [x19]
	ldr	x0, [x1,3616]
	ldr	x22, [x1,3664]
	mov	w1, 1
	str	x0, [x19,8]
	mov	x0, x19
	str	x22, [x19,16]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2568
	cmp	w0, 256
	beq	.L2568
	ldr	w0, [x22,8]
	cmp	w0, w20
	beq	.L2567
.L2568:
	adrp	x2, .LANCHOR4
	mov	w1, w20
	add	x2, x2, :lo12:.LANCHOR4
	add	x2, x2, 1704
	ldr	x7, [x2,8]
	ldr	x6, [x2,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x2,4]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
.L2567:
	add	w20, w20, 1
	b	.L2564
.L2571:
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 80
	ret
	.size	Ftlscanalldata, .-Ftlscanalldata
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
	ldr	x3, [x2,520]
	ldrh	w1, [x3,x22]
	cbnz	w1, .L2573
	ldr	x19, [x2,544]
	cbz	x19, .L2574
	ldrh	w3, [x2,552]
	mov	x4, -6148914691236517206
	ldr	x2, [x2,504]
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L2575:
	cmp	w1, w3
	beq	.L2574
	cmp	w19, w0
	bne	.L2576
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w19
	add	x0, x21, 544
	bl	List_remove_node
	ldrh	w0, [x21,552]
	sub	w0, w0, #1
	strh	w0, [x21,552]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x1, [x21,520]
	ldrh	w0, [x1,x22]
	add	w0, w0, 1
	strh	w0, [x1,x22]
	b	.L2574
.L2576:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2574
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2575
.L2573:
	add	w1, w1, 1
	strh	w1, [x3,x22]
.L2574:
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
	beq	.L2742
	ldrh	w0, [x19,2]
	adrp	x23, .LANCHOR0
	str	w0, [x29,188]
	ldrb	w0, [x19,6]
	str	w0, [x29,176]
	add	x0, x23, :lo12:.LANCHOR0
	ldr	w1, [x29,188]
	ldrh	w0, [x0,2544]
	cmp	w0, w1
	bne	.L2584
	strh	wzr, [x19,4]
	b	.L2748
.L2584:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2585:
	cmp	w0, w2
	bne	.L2751
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2585
.L2751:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2587
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L2588
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, w20
	ldrb	w1, [x0,204]
	cbnz	w1, .L2662
	add	x0, x0, 208
	ldrh	w24, [x0,w20,sxtw 1]
	b	.L2662
.L2587:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L2588
	mov	w24, w0
.L2662:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w26, 0
	mov	w8, 65535
	mov	w9, 56
	adrp	x10, .LANCHOR2
	ldrh	w4, [x0,2472]
	mov	w11, 4
	ldrh	w7, [x0,2556]
	add	x4, x19, x4, lsl 1
	b	.L2590
.L2588:
	strh	wzr, [x19,2]
.L2748:
	strb	wzr, [x19,6]
	b	.L2742
.L2592:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2591
	add	x5, x10, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x0, w26, w9
	add	w3, w26, 1
	ldr	x6, [x5,3592]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x5,3592]
	ldr	x5, [x5,1408]
	add	x0, x1, x0
	mul	w1, w26, w7
	uxth	w26, w3
	str	xzr, [x0,8]
	sdiv	w1, w1, w11
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L2591:
	add	x2, x2, 2
.L2590:
	cmp	x2, x4
	bne	.L2592
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2593
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2593:
	adrp	x21, .LANCHOR2
	ldr	w2, [x29,184]
	add	x22, x21, :lo12:.LANCHOR2
	mov	w1, w26
	mov	x27, 0
	mov	w25, 65535
	mov	x28, 56
	ldr	x0, [x22,3592]
	bl	FlashReadPages
	ldr	w0, [x22,756]
	adrp	x22, .LC148
	str	w0, [x29,168]
	add	x22, x22, :lo12:.LC148
	uxth	w0, w24
	str	w0, [x29,160]
.L2594:
	uxth	w2, w27
	cmp	w2, w26
	bcs	.L2601
	add	x6, x21, :lo12:.LANCHOR2
	mul	x0, x27, x28
	ldr	x4, [x6,3592]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2595
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2596
	ldr	w1, [x6,756]
	mov	w0, w4
	str	x7, [x29,128]
	str	x2, [x29,136]
	str	x4, [x29,144]
	str	x6, [x29,152]
	bl	ftl_cmp_data_ver
	ldr	x6, [x29,152]
	ldr	x4, [x29,144]
	ldr	x2, [x29,136]
	ldr	x7, [x29,128]
	cbz	w0, .L2596
	add	w4, w4, 1
	str	w4, [x6,756]
.L2596:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2597
.L2601:
	cmp	w2, w26
	bne	.L2743
	add	x0, x21, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x0, [x0,3592]
	ldr	w0, [x0,4]
	b	.L2744
.L2595:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w25, [x29,160]
	adrp	x0, .LANCHOR5
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LANCHOR5
	strh	w1, [x0,1328]
.L2597:
	add	x27, x27, 1
	b	.L2594
.L2743:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w20, w20
	umull	x2, w2, w1
	ldr	x0, [x0,3592]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2744:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2603
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w2, [x0,204]
	cbnz	w2, .L2603
	add	x0, x0, 208
	ldrh	w20, [x0,w20,sxtw 1]
.L2603:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w0, [x0,2544]
	cmp	w0, w20
	bne	.L2604
	strh	w20, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2604:
	ldrh	w0, [x29,176]
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w20, w0
	bne	.L2605
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2749
.L2605:
	ldr	w0, [x29,168]
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w25, w0
	bne	.L2606
	cbnz	w1, .L2607
.L2606:
	add	x0, x21, :lo12:.LANCHOR2
	uxth	w5, w24
	ldr	w1, [x0,3892]
	cmn	w1, #1
	bne	.L2608
	str	w22, [x0,3892]
.L2608:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w25, [x29,188]
	ldr	w6, [x0,3892]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w24, uxth
	bge	.L2609
	sub	w25, w5, #7
	uxth	w25, w25
.L2609:
	mov	w3, -1
	add	x28, x21, :lo12:.LANCHOR2
	mov	w27, w3
	mov	w4, 65535
	mov	w7, 56
	add	x8, x28, 1508
.L2610:
	cmp	w25, w5
	bhi	.L2623
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, 0
	ldrh	w2, [x0,2472]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2624:
	cmp	x0, x2
	beq	.L2752
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2611
	umull	x9, w24, w7
	ldr	x10, [x28,3592]
	add	w24, w24, 1
	orr	w1, w25, w1, lsl 10
	add	x9, x10, x9
	uxth	w24, w24
	str	w1, [x9,4]
.L2611:
	add	x0, x0, 2
	b	.L2624
.L2752:
	ldr	x0, [x28,3592]
	mov	w1, w24
	ldr	w2, [x29,184]
	str	x8, [x29,128]
	str	x7, [x29,136]
	str	x4, [x29,144]
	str	x6, [x29,160]
	str	x5, [x29,168]
	str	x3, [x29,176]
	bl	FlashReadPages
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x3, [x29,176]
	ldr	x5, [x29,168]
	mov	w1, 0
	ldr	x6, [x29,160]
	sxtw	x10, w25
	ldrb	w9, [x0,204]
	ldr	x0, [x28,3592]
	ldr	x4, [x29,144]
	ldr	x7, [x29,136]
	add	x0, x0, 16
	ldr	x8, [x29,128]
.L2613:
	cmp	w1, w24
	beq	.L2753
	ldr	w2, [x0,-16]
	cbnz	w2, .L2614
	ldr	x2, [x0]
	ldrh	w11, [x2]
	cmp	w11, w4
	beq	.L2615
	ldr	w2, [x2,4]
	cmn	w2, #1
	beq	.L2615
	ldr	w27, [x28,3892]
	cmn	w3, #1
	str	w2, [x28,3892]
	bne	.L2615
	ldrh	w2, [x8,x10,lsl 1]
	cmp	w2, w4
	bne	.L2616
	cbz	w9, .L2615
.L2616:
	cmp	w27, w22
	csinv	w3, w27, wzr, ne
	b	.L2615
.L2614:
	adrp	x0, .LANCHOR5+1328
	ldrh	w1, [x19]
	strh	w1, [x0,#:lo12:.LANCHOR5+1328]
	ldrb	w0, [x19,8]
	cbnz	w0, .L2607
	add	x0, x21, :lo12:.LANCHOR2
	add	x1, x0, 1508
	ldrh	w2, [x1,w25,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2618
	cmn	w3, #1
	beq	.L2619
	str	w3, [x0,3892]
	b	.L2607
.L2619:
	cmp	w6, w22
	beq	.L2620
	str	w6, [x0,3892]
	b	.L2607
.L2620:
	ldr	w1, [x0,3892]
	b	.L2750
.L2618:
	cmp	w27, w22
	beq	.L2621
	cmn	w27, #1
	beq	.L2607
	str	w27, [x0,3892]
	b	.L2607
.L2621:
	ldr	w1, [x0,3892]
	cmp	w1, w22
	beq	.L2607
.L2750:
	sub	w1, w1, #1
	b	.L2745
.L2615:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2613
.L2753:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2610
.L2623:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, -1
.L2745:
	str	w1, [x0,3892]
.L2607:
	adrp	x0, .LANCHOR5
	str	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR5
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	ldr	w24, [x29,188]
	str	x0, [x29,128]
	adrp	x0, .LC149
	mov	w2, 1
	add	x0, x0, :lo12:.LC149
	strh	w2, [x1,1330]
	str	x0, [x29,120]
.L2625:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x1, x19
	str	wzr, [x29,176]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,2472]
	ldrb	w6, [x0,204]
	add	x3, x19, x3, lsl 1
.L2626:
	cmp	x1, x3
	beq	.L2754
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2627
	ldr	w0, [x29,176]
	add	x5, x21, :lo12:.LANCHOR2
	orr	w2, w24, w2, lsl 10
	ldr	x4, [x5,3592]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2628
	cbz	w6, .L2628
	ldr	x2, [x5,3592]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2628:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2627:
	add	x1, x1, 2
	b	.L2626
.L2754:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,3592]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2630:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2755
	ldr	x0, [x29,160]
	mov	x25, 56
	mul	x25, x0, x25
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x28, [x0,3592]
	add	x27, x28, x25
	ldr	w1, [x27,4]
	str	w1, [x29,204]
	lsr	x0, x1, 10
	str	x1, [x29,168]
	bl	P2V_plane
	uxth	w0, w0
	ldr	w1, [x29,188]
	cmp	w24, w1
	ldr	x1, [x29,168]
	bcc	.L2631
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2666
	ldr	w2, [x29,188]
	cmp	w24, w2
	beq	.L2631
.L2666:
	cmp	w0, w26
	bne	.L2667
	cmp	w24, w20
	beq	.L2633
.L2667:
	ldr	w0, [x28,x25]
	cmn	w0, #1
	beq	.L2635
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	beq	.L2636
	ldrh	w0, [x19]
	b	.L2746
.L2636:
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2637
	add	x27, x21, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,756]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2637
	add	w0, w22, 1
	str	w0, [x27,756]
.L2637:
	ldr	w27, [x28,8]
	add	x1, x29, 200
	ldr	w0, [x28,12]
	mov	w2, 0
	str	w0, [x29,196]
	mov	w0, w27
	bl	log2phys
	ldr	w28, [x29,196]
	add	x3, x21, :lo12:.LANCHOR2
	str	x3, [x29,168]
	ldr	w1, [x3,3892]
	cmn	w1, #1
	beq	.L2638
	mov	w0, w22
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,168]
	cbz	w0, .L2638
	cmn	w28, #1
	beq	.L2639
	ldr	x0, [x3,3592]
	mov	w2, 0
	str	x3, [x29,112]
	add	x0, x0, x25
	str	w28, [x0,4]
	ldr	x1, [x0,16]
	ldr	x0, [x3,3592]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x3, [x29,112]
	ldr	x0, [x29,168]
	ldr	x2, [x3,3592]
	ldr	w0, [x0,4]
	str	w0, [x29,144]
	add	x4, x2, x25
	ldr	w0, [x2,x25]
	cmn	w0, #1
	bne	.L2640
	b	.L2641
.L2639:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2631
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
	b	.L2631
.L2640:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2641
	ldr	w0, [x3,3892]
	ldr	w1, [x29,144]
	str	x2, [x29,96]
	str	x4, [x29,104]
	str	x3, [x29,112]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	ldr	x2, [x29,96]
	cbz	w0, .L2641
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2646
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2641
	cmn	w0, #1
	beq	.L2644
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,3592]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	b	.L2645
.L2644:
	str	w0, [x2,x25]
.L2645:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,3592]
	ldr	w1, [x1,x25]
	cmn	w1, #1
	beq	.L2646
	ldr	x1, [x29,168]
	ldr	w0, [x0,3892]
	ldr	w25, [x1,4]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2646
	ldr	w0, [x29,144]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2641
.L2646:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2641:
	mov	w0, -1
	str	w0, [x29,196]
	b	.L2648
.L2638:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2648
	cmn	w28, #1
	beq	.L2650
	add	x0, x23, :lo12:.LANCHOR0
	ubfx	x28, x28, 10, 21
	ldr	w0, [x0,2488]
	cmp	w28, w0
	bcs	.L2631
.L2650:
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w25, [x29,200]
	cmn	w25, #1
	beq	.L2648
	ldr	w0, [x29,196]
	cmp	w25, w0
	beq	.L2648
	lsr	x0, x25, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x1, x21, :lo12:.LANCHOR2
	ldrh	w2, [x1,560]
	cmp	w2, w0
	beq	.L2652
	ldrh	w2, [x1,608]
	cmp	w2, w0
	beq	.L2652
	ldrh	w1, [x1,656]
	cmp	w1, w0
	bne	.L2648
.L2652:
	add	x28, x21, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, 0
	ldr	x0, [x28,3592]
	str	w25, [x0,4]
	ldr	x3, [x0,16]
	ldr	x0, [x28,3592]
	str	x3, [x29,168]
	bl	FlashReadPages
	ldr	x0, [x28,3592]
	ldr	x3, [x29,168]
	ldr	w0, [x0]
	ldr	w1, [x3,4]
	cmn	w0, #1
	beq	.L2648
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2648
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2648:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2631
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x21, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,520]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2653
.L2746:
	bl	decrement_vpc_count
	b	.L2631
.L2653:
	ldr	x0, [x29,120]
	bl	printk
	b	.L2631
.L2635:
	ldr	x2, [x29,136]
	ldrh	w0, [x19]
	add	x25, x2, :lo12:.LANCHOR5
	mov	w2, w22
	strh	w0, [x25,1328]
	ldr	x0, [x29,128]
	bl	printk
	ldr	w0, [x25,1332]
	cmp	w0, 31
	bhi	.L2654
	add	x1, x25, 1336
	ldr	w2, [x29,204]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x25,1332]
.L2654:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,3892]
	cmn	w1, #1
	beq	.L2747
	cmp	w1, w22
	bls	.L2631
.L2747:
	str	w22, [x0,3892]
.L2631:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2630
.L2755:
	ldrb	w0, [x19,8]
	add	w24, w24, 1
	cmp	w0, 1
	uxth	w24, w24
	bne	.L2657
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,204]
	cbz	w1, .L2657
	cmp	w20, w24
	bne	.L2657
	ldrh	w0, [x0,2546]
	cmp	w0, w24
	beq	.L2633
.L2657:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2544]
	cmp	w24, w1
	bne	.L2625
	ldrh	w2, [x0,2472]
	mov	w3, 65535
	strh	w24, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2659:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2742
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2659
	strb	w1, [x19,6]
	b	.L2742
.L2633:
	strb	w26, [x19,6]
	strh	w20, [x19,2]
.L2749:
	mov	x0, x19
	mov	w1, w20
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2742:
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
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC109
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 232
	add	x0, x0, :lo12:.LC109
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x22, .LANCHOR0
	bl	printk
	add	x1, x22, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	mov	w19, 0
	ldrh	w2, [x1,2482]
	mov	w1, 0
	ldr	x0, [x0,3712]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2757:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2616]
	cmp	w19, w0
	bcs	.L2773
	mov	w0, w19
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2758
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x2, [x1,3712]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2758:
	add	w19, w19, 1
	b	.L2757
.L2773:
	adrp	x23, .LC151
	mov	w19, 0
	add	x20, x20, :lo12:.LANCHOR2
	mov	w24, 65535
	add	x23, x23, :lo12:.LC151
.L2760:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2480]
	cmp	w0, w19
	bls	.L2774
	ubfiz	x21, x19, 1, 16
	ldr	x0, [x20,520]
	ldrh	w2, [x0,x21]
	ldr	x0, [x20,3712]
	cmp	w2, w24
	ldrh	w3, [x0,x21]
	beq	.L2761
	cmp	w2, w3
	beq	.L2761
	ldrh	w0, [x20,560]
	cmp	w0, w19
	beq	.L2761
	ldrh	w0, [x20,656]
	cmp	w0, w19
	beq	.L2761
	ldrh	w0, [x20,608]
	cmp	w0, w19
	beq	.L2761
	mov	w1, w19
	mov	x0, x23
	bl	printk
	ldr	x0, [x20,520]
	ldrh	w1, [x0,x21]
	cbnz	w1, .L2762
	ldr	x1, [x20,3712]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	b	.L2761
.L2762:
	ldr	x1, [x20,3712]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	mov	w0, w19
	bl	update_vpc_list
.L2761:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2760
.L2774:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	stp	x29, x30, [sp, -224]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x20, x0
	adrp	x0, .LANCHOR1
	str	w1, [x29,148]
	add	x1, x0, :lo12:.LANCHOR1
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	x0, [x29,136]
	ldrh	w27, [x1,3456]
	mov	w1, 65535
	cmp	w27, w1
	beq	.L2808
	cbnz	w27, .L2776
	b	.L2777
.L2808:
	mov	w27, 0
.L2776:
	adrp	x0, .LANCHOR0+2544
	ldr	w1, [x29,148]
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2544]
	cmp	w1, w0
	bne	.L2778
.L2777:
	bl	FtlGcPageVarInit
.L2778:
	mov	w0, -1
	str	w0, [x29,152]
	adrp	x0, .LC153
	mov	w23, 0
	add	x0, x0, :lo12:.LC153
	adrp	x28, .LANCHOR0
	str	x0, [x29,128]
.L2779:
	ldrh	w1, [x20]
	mov	w0, 65535
	strb	wzr, [x20,8]
	cmp	w1, w0
	beq	.L2809
.L2781:
	str	w0, [x29,156]
.L2805:
	adrp	x0, .LANCHOR0
	mov	x2, x20
	add	x0, x0, :lo12:.LANCHOR0
	mov	w21, 0
	mov	w10, 56
	adrp	x11, .LANCHOR2
	mov	w7, 4
	ldrh	w5, [x0,2472]
	ldrh	w8, [x0,2554]
	ldrh	w9, [x0,2556]
	add	x5, x20, x5, lsl 1
.L2782:
	cmp	x2, x5
	beq	.L2828
	ldrh	w1, [x2,16]
	ldr	w0, [x29,156]
	cmp	w1, w0
	beq	.L2783
	add	x4, x11, :lo12:.LANCHOR2
	orr	w1, w27, w1, lsl 10
	umull	x0, w21, w10
	ldr	x6, [x4,3592]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x4,3592]
	ldr	x6, [x4,1400]
	add	x0, x1, x0
	ldr	x4, [x4,1408]
	mul	w1, w21, w8
	sdiv	w1, w1, w7
	add	x1, x6, x1, sxtw 2
	str	x1, [x0,8]
	mul	w1, w21, w9
	add	w21, w21, 1
	uxth	w21, w21
	sdiv	w1, w1, w7
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L2783:
	add	x2, x2, 2
	b	.L2782
.L2828:
	adrp	x19, .LANCHOR2
	mov	w1, w21
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 0
	mov	x24, 0
	ldr	x0, [x0,3592]
	bl	FlashReadPages
.L2785:
	cmp	w21, w24, uxth
	bls	.L2829
	add	x25, x19, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x22, x24, x0
	ldr	x1, [x25,3592]
	str	x1, [x29,112]
	add	x2, x1, x22
	str	x2, [x29,120]
	ldr	w26, [x2,4]
	lsr	x0, x26, 10
	bl	P2V_plane
	uxth	w5, w0
	ldr	x1, [x29,112]
	ldr	x2, [x29,120]
	ldr	w8, [x1,x22]
	ldr	x4, [x2,16]
	cbnz	w8, .L2786
	ldrh	w0, [x4]
	ldr	w1, [x29,156]
	cmp	w0, w1
	bne	.L2787
.L2790:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbz	w0, .L2824
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	str	w0, [x19,3888]
	b	.L2780
.L2787:
	add	x7, x28, :lo12:.LANCHOR0
	ldr	w0, [x4,8]
	ldr	w1, [x7,2616]
	cmp	w0, w1
	bls	.L2825
	b	.L2790
.L2824:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w2, [x20]
	ldr	x1, [x19,520]
	strh	w0, [x1,x2,lsl 1]
	b	.L2827
.L2825:
	ldrb	w1, [x7,72]
	cbz	w1, .L2794
	add	x1, x29, 164
	mov	w2, w8
	str	x4, [x29,120]
	str	x7, [x29,104]
	str	x8, [x29,112]
	bl	log2phys
	ldr	x4, [x29,120]
	ldr	w1, [x29,164]
	ldr	w0, [x4,12]
	cmp	w1, w0
	bne	.L2794
	cmn	w1, #1
	beq	.L2794
	str	w0, [x29,172]
	mov	w1, 1
	ldr	x0, [x25,3656]
	ldr	x8, [x29,112]
	str	x0, [x29,176]
	ldr	x0, [x25,3672]
	mov	w2, w8
	str	x0, [x29,184]
	add	x0, x29, 168
	bl	FlashReadPages
	ldr	x7, [x29,104]
	ldr	x0, [x25,3592]
	ldr	x5, [x29,176]
	ldrh	w2, [x7,2550]
	add	x22, x0, x22
	ldr	x4, [x29,120]
	mov	x0, 0
	lsl	w2, w2, 7
.L2796:
	cmp	w2, w0
	bls	.L2794
	lsl	x1, x0, 2
	ldr	x6, [x22,8]
	add	x0, x0, 1
	ldr	w6, [x6,x1]
	ldr	w1, [x5,x1]
	cmp	w6, w1
	beq	.L2796
	ldrh	w1, [x20]
	adrp	x0, .LC152
	ldr	w2, [x29,172]
	add	x0, x0, :lo12:.LC152
	add	x19, x19, :lo12:.LANCHOR2
	bl	printk
	ldrh	w1, [x20]
	ldr	x0, [x19,520]
	strh	wzr, [x0,x1,lsl 1]
.L2827:
	ldrh	w0, [x20]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x20]
	strh	w0, [x19,800]
	b	.L2826
.L2794:
	ldr	w0, [x4,12]
	mov	w1, w26
	ldr	w2, [x4,8]
	add	x24, x24, 1
	bl	FtlGcUpdatePage
	b	.L2785
.L2786:
	ldrh	w1, [x20]
	mov	w2, w26
	ldr	x0, [x29,128]
	bl	printk
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	w1, [x0,2372]
	cbnz	w1, .L2799
	ldrb	w0, [x0,204]
	cbz	w0, .L2800
.L2799:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x20]
	ldr	x0, [x0,440]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 159
	bls	.L2801
.L2800:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,3592]
	ldr	w0, [x0,x22]
	cmn	w0, #1
	bne	.L2802
.L2801:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,3592]
	add	x22, x0, x22
	ldr	w0, [x22,4]
	str	w0, [x29,152]
.L2802:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x20]
	ldr	x0, [x19,520]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x20]
.L2826:
	bl	FtlGcPageVarInit
	mov	w27, 0
	b	.L2779
.L2829:
	ldr	w0, [x29,148]
	add	w23, w23, 1
	add	w3, w27, 1
	cmp	w23, w0
	uxth	w27, w3
	bcs	.L2804
.L2806:
	add	x0, x28, :lo12:.LANCHOR0
	ldrh	w0, [x0,2544]
	cmp	w0, w27
	bhi	.L2805
	b	.L2809
.L2804:
	ldr	x0, [x29,136]
	ldr	w2, [x29,156]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3456]
	cmp	w0, w2
	beq	.L2806
	add	w0, w0, w23
	strh	w0, [x1,3456]
	add	x0, x28, :lo12:.LANCHOR0
	ldrh	w0, [x0,2544]
	cmp	w0, w27
	bls	.L2806
	b	.L2807
.L2809:
	mov	w5, 0
.L2780:
	ldr	x0, [x29,136]
	mov	w1, -1
	strh	w27, [x20,2]
	mov	w2, w5
	add	x0, x0, :lo12:.LANCHOR1
	strb	w5, [x20,6]
	strh	w1, [x0,3456]
	mov	x0, x20
	mov	w1, w27
	bl	ftl_sb_update_avl_pages
.L2807:
	ldr	w0, [x29,152]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 224
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	stp	x29, x30, [sp, -112]!
	adrp	x2, .LANCHOR2
	add	x1, x2, :lo12:.LANCHOR2
	add	x29, sp, 0
	add	x3, x1, 856
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR0
	ldr	w0, [x3,80]
	cbz	w0, .L2831
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x3,84]
	ldr	w0, [x0,2616]
	cmp	w2, w0
	bcs	.L2832
	mov	w21, 2048
	mov	x19, x1
.L2837:
	add	x22, x19, 856
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x22,84]
	ldr	w1, [x1,2616]
	cmp	w0, w1
	bcs	.L2836
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x22,84]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x22,84]
	cmn	w1, #1
	beq	.L2835
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
	bne	.L2836
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2836:
	mov	w0, -1
	b	.L2839
.L2835:
	subs	w21, w21, #1
	bne	.L2837
	b	.L2836
.L2832:
	ldr	w0, [x1,720]
	str	wzr, [x3,80]
	str	wzr, [x3,84]
	str	w0, [x3,76]
	b	.L2846
.L2831:
	ldr	w4, [x1,772]
	mov	w6, 10000
	mov	w8, 63
	ldr	w7, [x1,720]
	cmp	w4, w6
	mov	w6, 31
	csel	w8, w6, w8, hi
	ldr	w6, [x3,76]
	add	x20, x20, :lo12:.LANCHOR0
	add	w9, w7, 1048576
	cmp	w6, w9
	ldr	w5, [x20,2616]
	bhi	.L2841
	mov	w9, 1000
	lsr	w4, w4, 10
	add	w4, w4, 1
	mul	w5, w5, w9
	udiv	w4, w5, w4
	add	w4, w4, w6
	cmp	w4, w7
	bcc	.L2841
	ldrh	w1, [x1,484]
	tst	w8, w1
	bne	.L2839
	ldr	w3, [x3,100]
	cmp	w3, w1
	beq	.L2839
.L2841:
	add	x0, x2, :lo12:.LANCHOR2
	add	x1, x0, 856
	ldrh	w0, [x0,484]
	str	w0, [x1,100]
	mov	w0, 1
	str	wzr, [x1,84]
	str	w0, [x1,80]
	str	w7, [x1,76]
.L2846:
	mov	w0, 0
.L2839:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	adrp	x21, .LANCHOR0
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w3, [x2,424]
	ldrh	w1, [x1,2544]
	cbnz	w3, .L2883
	ldrh	w20, [x2,656]
	mov	w4, 65535
	cmp	w20, w4
	bne	.L2850
.L2859:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x22, x20, 656
	ldrh	w1, [x20,656]
	str	wzr, [x20,3888]
	cmp	w1, w0
	beq	.L2883
	bl	FtlCacheWriteBack
	mov	w26, 12
	add	x3, x21, :lo12:.LANCHOR0
	ldrb	w0, [x22,7]
	ldr	x1, [x20,520]
	mov	w22, 0
	ldrh	w2, [x20,656]
	ldrh	w3, [x3,2544]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldr	w0, [x20,740]
	ldrh	w1, [x20,1440]
	add	w0, w1, w0
	str	w0, [x20,740]
	b	.L2860
.L2850:
	cbz	w0, .L2853
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3456]
	cmp	w5, w4
	beq	.L2854
.L2855:
	mov	w1, 2
	b	.L2853
.L2854:
	strh	w3, [x0,3456]
	ldrh	w0, [x2,552]
	cmp	w0, 17
	bhi	.L2855
.L2853:
	add	x22, x19, :lo12:.LANCHOR2
	add	x0, x22, 656
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L2856
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x22,440]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L2857
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2857:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,3888]
	cbnz	w1, .L2858
	ldr	w1, [x0,952]
	add	w1, w1, 1
	str	w1, [x0,952]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2858:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	str	wzr, [x19,3888]
	b	.L2849
.L2856:
	adrp	x0, .LANCHOR1+3456
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3456]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2849
	b	.L2859
.L2863:
	ldr	w1, [x23,4]
	cmp	w0, w1
	bne	.L2881
.L2862:
	add	w22, w22, 1
	uxth	w22, w22
.L2860:
	ldrh	w0, [x20,1440]
	cmp	w0, w22
	bls	.L2884
	umull	x24, w22, w26
	ldr	x25, [x20,1448]
	add	x1, x21, :lo12:.LANCHOR0
	add	x23, x25, x24
	ldr	w1, [x1,2616]
	ldr	w0, [x23,8]
	cmp	w0, w1
	bcs	.L2881
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	ldr	w1, [x25,x24]
	cmp	w0, w1
	bne	.L2863
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
	b	.L2882
.L2881:
	ldrh	w0, [x20,656]
.L2882:
	bl	decrement_vpc_count
	b	.L2862
.L2884:
	mov	w0, -1
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbz	w0, .L2865
	adrp	x0, .LC154
	ldrh	w1, [x20,656]
	add	x0, x0, :lo12:.LC154
	bl	printk
.L2865:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,656]
	ldr	x1, [x1,520]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1,x2]
	cbz	w1, .L2866
	bl	INSERT_DATA_LIST
	b	.L2867
.L2866:
	bl	INSERT_FREE_LIST
.L2867:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w22, -1
	strh	wzr, [x20,1440]
	strh	w22, [x20,656]
	strh	wzr, [x20,1428]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w22, [x20,800]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,2372]
	cbz	w0, .L2868
	ldr	w0, [x20,776]
	cmp	w0, 39
	bhi	.L2868
	ldrh	w0, [x20,780]
	ldrh	w1, [x20,552]
	cmp	w1, w0
	bcs	.L2883
	lsl	w0, w0, 1
	strh	w0, [x20,1368]
.L2883:
	mov	w0, 0
	b	.L2849
.L2868:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,780]
	ldrh	w0, [x19,552]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L2883
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,204]
	cbz	w0, .L2869
	sub	w1, w1, #2
	strh	w1, [x19,1368]
	b	.L2883
.L2869:
	mov	w1, 20
	strh	w1, [x19,1368]
.L2849:
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
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR2
	str	x21, [sp,32]
	add	x21, x19, 656
	mov	x0, x21
	ldrh	w1, [x20,2544]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,658]
	ldrh	w0, [x20,2544]
	cmp	w1, w0
	bcc	.L2885
	add	x0, x19, 3800
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19,3888]
.L2885:
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
	adrp	x0, .LANCHOR5+1332
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR5+1332]
	add	x19, x19, :lo12:.LANCHOR2
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
	adrp	x20, .LANCHOR0
	adrp	x21, .LANCHOR5
	stp	x23, x24, [sp,48]
	add	x0, x21, :lo12:.LANCHOR5
	add	x24, x20, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR2
	add	x23, x19, :lo12:.LANCHOR2
	strh	w1, [x0,1328]
	strh	wzr, [x0,1330]
	ldrh	w0, [x24,2476]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x23,784]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2889
.L2891:
	mov	w22, -1
	b	.L2890
.L2889:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L2891
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x24,2582]
	mov	x1, 0
	ldr	x0, [x23,704]
.L2892:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2896
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2892
.L2896:
	add	x0, x19, :lo12:.LANCHOR2
	cmp	w3, w2
	ldrh	w1, [x0,484]
	add	w1, w1, 1
	strh	w1, [x0,484]
	blt	.L2893
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w0, [x21,1330]
	cbz	w0, .L2897
.L2893:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,560]
	ldr	x4, [x1,520]
	lsl	x2, x2, 1
	ldrh	w5, [x1,564]
	ldrh	w3, [x4,x2]
	sub	w3, w3, w5
	strh	w3, [x4,x2]
	add	x4, x20, :lo12:.LANCHOR0
	strb	wzr, [x1,566]
	ldr	x5, [x1,520]
	strh	wzr, [x1,564]
	ldrh	w2, [x4,2544]
	strh	w2, [x1,562]
	ldrh	w2, [x1,608]
	ldrh	w6, [x1,612]
	lsl	x2, x2, 1
	ldrh	w3, [x5,x2]
	sub	w3, w3, w6
	strh	w3, [x5,x2]
	strb	wzr, [x1,614]
	ldrh	w0, [x1,486]
	ldrh	w2, [x4,2544]
	add	w0, w0, 1
	strh	w2, [x1,610]
	strh	wzr, [x1,612]
	strh	w0, [x1,486]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2897:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x24, x21, 560
	ldrh	w0, [x21,560]
	cmp	w0, w1
	beq	.L2898
	ldrh	w1, [x21,564]
	cbnz	w1, .L2898
	ldrh	w1, [x21,612]
	add	x23, x21, 608
	cbnz	w1, .L2898
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x21,608]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x24
	bl	allocate_new_data_superblock
	mov	x0, x23
	bl	allocate_new_data_superblock
.L2898:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,72]
	cbnz	w0, .L2899
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,484]
	and	w0, w0, 31
	cbnz	w0, .L2890
.L2899:
	bl	FtlVpcCheckAndModify
.L2890:
	mov	w0, w22
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	ldr	w21, [x20,424]
	cbnz	w21, .L2907
	adrp	x22, .LANCHOR0
	ldr	x0, [x20,3768]
	add	x23, x22, :lo12:.LANCHOR0
	mov	w1, w21
	ldrh	w2, [x23,2580]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x23,2580]
	mov	w1, w21
	ldr	x0, [x20,3760]
	lsl	w2, w2, 2
	bl	ftl_memset
	str	w21, [x20,752]
	ldrh	w0, [x23,2476]
	str	w21, [x20,756]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2908
	bl	FtlMakeBbt
.L2908:
	mov	w0, 0
.L2909:
	add	x1, x22, :lo12:.LANCHOR0
	ldrh	w2, [x1,2550]
	cmp	w0, w2, lsl 7
	bge	.L2936
	add	x3, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 2, 16
	mvn	w1, w0
	ldr	x4, [x3,3648]
	orr	w1, w0, w1, lsl 16
	add	w0, w0, 1
	str	w1, [x4,x2]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	uxth	w0, w0
	ldr	x3, [x3,3656]
	str	w1, [x3,x2]
	b	.L2909
.L2936:
	ldrh	w21, [x1,2480]
	mov	w20, 0
.L2911:
	add	x23, x22, :lo12:.LANCHOR0
	ldrh	w0, [x23,2482]
	cmp	w0, w21
	bls	.L2937
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L2911
.L2937:
	ldrh	w0, [x23,2472]
	sub	w1, w20, #3
	cmp	w1, w0, lsl 1
	blt	.L2913
	udiv	w0, w20, w0
	ldr	w20, [x23,2576]
	add	w0, w0, w20
	mov	w20, 0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x23,2476]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w21, [x23,2480]
.L2914:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2482]
	cmp	w0, w21
	bls	.L2913
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L2914
.L2913:
	mov	w23, 0
	mov	w21, w23
.L2916:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,2480]
	cmp	w1, w21
	bls	.L2938
	mov	w0, w21
	mov	w1, 0
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w23, w0
	uxth	w21, w21
	uxth	w23, w0
	b	.L2916
.L2938:
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,2482]
	ldrh	w2, [x0,2472]
	str	w1, [x4,3580]
	ldr	w1, [x0,2484]
	udiv	w3, w1, w2
	str	w3, [x0,2616]
	ubfx	x6, x3, 5, 16
	add	w5, w6, 36
	strh	w5, [x4,780]
	mov	w5, 24
	mul	w5, w2, w5
	cmp	w23, w5
	ble	.L2918
	sub	w1, w1, w23
	udiv	w1, w1, w2
	str	w1, [x0,2616]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x4,780]
.L2918:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2372]
	cmp	w0, 1
	bne	.L2919
	add	x1, x19, :lo12:.LANCHOR2
	udiv	w0, w23, w2
	ldrh	w4, [x1,780]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,780]
.L2919:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbz	w0, .L2920
	add	x1, x19, :lo12:.LANCHOR2
	udiv	w0, w23, w2
	ldrh	w4, [x1,780]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,780]
.L2920:
	add	x5, x22, :lo12:.LANCHOR0
	ldrh	w1, [x5,2538]
	cbz	w1, .L2922
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w4, [x0,780]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,780]
	mul	w4, w1, w2
	cmp	w4, w23
	ble	.L2922
	add	w1, w1, 32
	str	w3, [x5,2616]
	add	w1, w6, w1
	strh	w1, [x0,780]
.L2922:
	add	x24, x22, :lo12:.LANCHOR0
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR4
	ldr	w1, [x24,2616]
	ldrh	w0, [x0,780]
	sub	w0, w1, w0
	add	x1, x21, :lo12:.LANCHOR4
	mul	w0, w0, w2
	str	w0, [x1,1760]
	ldrh	w1, [x24,2544]
	mul	w0, w0, w1
	ldrh	w1, [x24,2550]
	str	w0, [x24,2616]
	mul	w0, w0, w1
	str	w0, [x24,2584]
	bl	FtlBbmTblFlush
	ldr	w2, [x24,2488]
	add	w1, w23, w20
	ldrh	w0, [x24,2558]
	add	w0, w0, w2, lsr 3
	cmp	w1, w0
	bls	.L2924
	adrp	x0, .LC155
	lsr	w2, w2, 5
	add	x0, x0, :lo12:.LC155
	bl	printk
.L2924:
	add	x24, x22, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, 0
	add	x20, x19, 560
	mov	w23, -1
	ldrh	w2, [x24,2482]
	ldr	x0, [x19,520]
	lsl	w2, w2, 1
	bl	ftl_memset
	str	wzr, [x19,716]
	mov	w0, 1
	ldrh	w2, [x24,2480]
	strb	w0, [x20,8]
	mov	w1, 255
	ldr	x0, [x22,#:lo12:.LANCHOR0]
	lsr	w2, w2, 3
	strh	w23, [x19,800]
	strh	wzr, [x19,802]
	strb	wzr, [x19,806]
	strb	wzr, [x19,808]
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	strh	wzr, [x19,560]
	bl	ftl_memset
.L2925:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2926
	ldrh	w1, [x20]
	ldr	x0, [x19,520]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2925
.L2926:
	ldr	w0, [x19,752]
	mov	w22, -1
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,752]
	ldr	x0, [x19,520]
	ldrh	w2, [x20,4]
	strh	w2, [x0,x1,lsl 1]
	add	x0, x19, 608
	strh	wzr, [x19,610]
	ldrh	w1, [x20]
	mov	x20, x0
	strb	wzr, [x19,614]
	add	w1, w1, 1
	strh	w1, [x19,608]
	mov	w1, 1
	strb	w1, [x19,616]
.L2927:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2928
	ldrh	w1, [x20]
	ldr	x0, [x19,520]
	strh	w22, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2927
.L2928:
	ldr	w0, [x19,752]
	add	x21, x21, :lo12:.LANCHOR4
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,752]
	ldr	x0, [x19,520]
	ldrh	w2, [x20,4]
	mov	w20, -1
	strh	w2, [x0,x1,lsl 1]
	strh	w20, [x19,656]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19,784]
	ldr	w0, [x21,1760]
	strh	w0, [x19,790]
	ldr	w0, [x19,752]
	str	w0, [x19,792]
	add	w0, w0, 1
	strh	wzr, [x19,786]
	strh	w20, [x19,788]
	str	w0, [x19,752]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L2907
	adrp	x0, .LANCHOR1+504
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+504]
.L2907:
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbz	w0, .L2940
.L2942:
	mov	w0, 0
	b	.L2941
.L2940:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,1592]
	bl	FlashInit
	cbnz	w0, .L2942
	bl	FlashLoadFactorBbt
	cbz	w0, .L2943
	bl	FlashMakeFactorBbt
.L2943:
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20,1648]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L2944
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L2946:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L2946
	cmp	w0, 6
	bhi	.L2947
	add	x0, x19, :lo12:.LANCHOR0
	b	.L2969
.L2947:
	mov	w1, 0
	mov	w4, 1
.L2950:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L2950
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L2969
	mov	w1, 36
.L2969:
	strb	w1, [x0,73]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,73]
	strh	w1, [x0,202]
.L2944:
	adrp	x0, .LC75
	adrp	x1, .LC76
	add	x1, x1, :lo12:.LC76
	add	x0, x0, :lo12:.LC75
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	add	x0, x19, 176
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,2476]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L2952:
	bl	FtlLoadBbt
	cbz	w0, .L2953
.L2971:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L2972
	add	w19, w19, 1
	b	.L2952
.L2972:
	mov	w0, -1
	b	.L2941
.L2953:
	bl	FtlSysBlkInit
	cbnz	w0, .L2971
	adrp	x1, .LANCHOR1+504
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+504]
.L2941:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	ldr	w1, [x0,424]
	cbz	w1, .L2974
.L2977:
	mov	w0, 0
	b	.L2975
.L2974:
	adrp	x1, .LANCHOR0+204
	ldrb	w1, [x1,#:lo12:.LANCHOR0+204]
	cbz	w1, .L2976
	ldr	w1, [x0,1384]
	tbz	x1, 0, .L2976
	ldrh	w0, [x0,660]
	cbnz	w0, .L2977
.L2976:
	add	x1, x21, :lo12:.LANCHOR2
	mov	w2, 0
	mov	w3, w2
	mov	w20, 0
	mov	w22, 56
	ldr	x0, [x1,3600]
	ldr	w1, [x1,1384]
	bl	FlashProgPages
.L2978:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w1, [x19,1384]
	cmp	w20, w1
	bcs	.L2988
	umull	x0, w20, w22
	ldr	x3, [x19,3600]
	add	x1, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	ldr	x2, [x1,16]
	bne	.L2979
	ldrh	w2, [x19,656]
	ldr	x1, [x19,520]
	strh	wzr, [x1,x2,lsl 1]
	strh	w3, [x19,656]
	ldr	w1, [x19,952]
	add	w1, w1, 1
	str	w1, [x19,952]
	ldr	x1, [x19,3600]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2987
.L2979:
	ldr	w0, [x2,12]
	add	w20, w20, 1
	ldr	w1, [x1,4]
	ldr	w2, [x2,8]
	uxth	w20, w20
	bl	FtlGcUpdatePage
	b	.L2978
.L2988:
	ldr	x0, [x19,3600]
	bl	FtlGcBufFree
	str	wzr, [x19,1384]
	ldrh	w0, [x19,660]
	cbnz	w0, .L2977
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L2987:
	mov	w0, 1
.L2975:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 656
	ldrh	w2, [x0,656]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2990
	ldrh	w0, [x1,4]
	cbnz	w0, .L2991
.L2990:
	bl	FtlCacheWriteBack
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	strb	wzr, [x20,664]
	add	x0, x20, 656
	bl	allocate_data_superblock
	strh	wzr, [x20,1428]
	strh	wzr, [x20,1440]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2991:
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 656
	bl	get_new_active_ppa
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
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
	ldr	w4, [x3,424]
	cbnz	w4, .L3145
	adrp	x4, .LANCHOR1
	add	x4, x4, :lo12:.LANCHOR1
	ldr	w20, [x4,504]
	cmp	w20, 1
	bne	.L3145
	ldr	w5, [x3,3568]
	cbnz	w5, .L3145
	ldrh	w5, [x3,536]
	cmp	w5, 47
	bls	.L3145
	mov	w22, w1
	ldrh	w1, [x4,3456]
	str	w0, [x29,124]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2994
.L2997:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0,1458]
	cmp	w4, w2
	bne	.L2995
	b	.L2996
.L2994:
	ldrh	w1, [x3,656]
	cmp	w1, w0
	beq	.L2997
	mov	w0, w20
	bl	FtlGcFreeTempBlock
	mov	w2, w20
	cbz	w0, .L2997
	b	.L3145
.L2995:
	ldrh	w1, [x0,1456]
	cmp	w1, w2
	bne	.L2996
	ldrh	w3, [x0,1460]
	cmp	w3, w1
	beq	.L2996
	ldrh	w2, [x0,1462]
	cmp	w2, w1
	beq	.L2996
	mov	w1, -1
	strh	w4, [x0,1456]
	strh	w3, [x0,1458]
	strh	w2, [x0,1460]
	strh	w1, [x0,1462]
.L2996:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x29,124]
	cmp	w2, 1
	ldr	w0, [x1,1372]
	add	w0, w0, 1
	add	w0, w0, w2, lsl 7
	str	w0, [x1,1372]
	bne	.L3008
	adrp	x2, .LANCHOR0
	add	x1, x2, :lo12:.LANCHOR0
	mov	x26, x2
	ldr	w3, [x1,2372]
	cbnz	w3, .L2999
	ldrb	w1, [x1,204]
	cbz	w1, .L3008
.L2999:
	add	x20, x19, :lo12:.LANCHOR2
	ldr	w1, [x20,776]
	cmp	w1, 39
	bhi	.L3008
	adrp	x21, .LANCHOR5
	add	x1, x21, :lo12:.LANCHOR5
	ldrh	w1, [x1,1464]
	add	w0, w0, w1
	str	w0, [x20,1372]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x20,800]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L3008
	ldrh	w1, [x20,1456]
	cmp	w1, w0
	bne	.L3008
	ldr	w0, [x20,1372]
	cmp	w0, 1024
	bhi	.L3000
	ldrh	w0, [x20,552]
	cmp	w0, 63
	bhi	.L3008
.L3000:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR5
	ldrh	w2, [x0,1370]
	ldrh	w3, [x0,552]
	add	w2, w2, 64
	strh	wzr, [x1,1464]
	cmp	w3, w2
	bgt	.L3008
	str	wzr, [x0,1372]
	ldr	w0, [x0,776]
	cbnz	w0, .L3001
	mov	w0, 6
	b	.L3167
.L3001:
	cmp	w0, 5
	bhi	.L3002
	mov	w0, 18
.L3167:
	strh	w0, [x1,1464]
.L3002:
	mov	w0, 32
	mov	w28, 65535
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w28
	beq	.L3007
	add	x23, x19, :lo12:.LANCHOR2
	ldrh	w0, [x23,1376]
	cbz	w0, .L3004
	add	x2, x26, :lo12:.LANCHOR0
	ldr	x27, [x23,520]
	ubfiz	x25, x25, 1, 16
	ldrh	w1, [x2,2546]
	ldrh	w2, [x2,2472]
	ldrh	w3, [x27,x25]
	mul	w1, w1, w2
	add	w1, w1, 1
	cmp	w3, w1
	bgt	.L3007
	add	w1, w0, 1
	str	wzr, [x23,1380]
	uxth	w1, w1
	strh	w1, [x23,1376]
	str	x1, [x29,112]
	bl	List_get_gc_head_node
	uxth	w20, w0
	cmp	w20, w28
	ldr	x1, [x29,112]
	beq	.L3007
	ubfiz	x26, x20, 1, 16
	adrp	x0, .LC156
	ldrh	w4, [x27,x25]
	add	x0, x0, :lo12:.LC156
	mov	w2, w20
	ldrh	w3, [x27,x26]
	bl	printk
	ldrh	w0, [x23,1376]
	cmp	w0, 40
	bls	.L3005
	ldr	x0, [x23,520]
	ldrh	w0, [x0,x26]
	cmp	w0, 32
	bls	.L3005
	strh	wzr, [x23,1376]
.L3005:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w0, 6
	strh	w0, [x21,1464]
	b	.L3009
.L3004:
	mov	w0, 1
	strh	w0, [x23,1376]
.L3007:
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L3009
	add	x21, x21, :lo12:.LANCHOR5
	strh	wzr, [x21,1464]
.L3008:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w20, [x21,800]
	cmp	w20, w0
	bne	.L3171
	ldrh	w0, [x21,656]
	cmp	w0, w20
	beq	.L3172
.L3009:
	mov	w0, 65535
	cmp	w20, w0
	cset	w2, eq
	cbz	w2, .L3022
	ldr	w0, [x29,124]
	cbnz	w0, .L3022
	add	x0, x19, :lo12:.LANCHOR2
	mov	w21, 1
	ldrh	w1, [x0,552]
	cmp	w1, 24
	bhi	.L3023
	adrp	x0, .LANCHOR0
	cmp	w1, 16
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w21, [x0,2544]
	bls	.L3024
	lsr	w21, w21, 5
	b	.L3023
.L3024:
	cmp	w1, 12
	bls	.L3025
	lsr	w21, w21, 4
	b	.L3023
.L3025:
	cmp	w1, 8
	bls	.L3023
	lsr	w21, w21, 2
.L3023:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,1368]
	cmp	w2, w1
	bcs	.L3027
	ldrh	w1, [x0,656]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L3028
	ldrh	w2, [x0,1456]
	cmp	w2, w1
	bne	.L3028
	adrp	x1, .LANCHOR5+1464
	ldrh	w2, [x1,#:lo12:.LANCHOR5+1464]
	cbnz	w2, .L3029
	adrp	x1, .LANCHOR0+2616
	ldr	w3, [x0,716]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2616]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L3030
.L3029:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,780]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,1368]
	b	.L3031
.L3030:
	mov	w1, 18
	strh	w1, [x0,1368]
.L3031:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,1380]
	b	.L3145
.L3028:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,780]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,1368]
.L3027:
	cmp	w22, 2
	bhi	.L3099
	adrp	x0, .LANCHOR0+2372
	ldr	w0, [x0,#:lo12:.LANCHOR0+2372]
	cbz	w0, .L3099
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3099
.L3022:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w3, 65535
	ldrh	w1, [x0,656]
	cmp	w1, w3
	bne	.L3034
	cbz	w2, .L3034
	ldrh	w2, [x0,1456]
	cmp	w2, w1
	bne	.L3034
	ldrh	w2, [x0,800]
	cmp	w2, w1
	beq	.L3035
.L3040:
	mov	w20, 65535
	b	.L3034
.L3035:
	ldrh	w2, [x0,552]
	adrp	x20, .LANCHOR5
	ldrh	w1, [x0,1368]
	str	wzr, [x0,1380]
	cmp	w2, w1
	bls	.L3036
	add	x1, x20, :lo12:.LANCHOR5
	ldrh	w1, [x1,1464]
	cbnz	w1, .L3037
	adrp	x1, .LANCHOR0+2616
	ldr	w2, [x0,716]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2616]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L3038
.L3037:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,780]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,1368]
	b	.L3039
.L3038:
	mov	w1, 18
	strh	w1, [x0,1368]
.L3039:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x1, [x1,520]
	ldrh	w0, [x1,x0]
	cmp	w0, 4
	bls	.L3036
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w2, [x20,1464]
	b	.L3145
.L3036:
	add	x21, x20, :lo12:.LANCHOR5
	ldrh	w0, [x21,1464]
	cbnz	w0, .L3040
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w20, [x22,780]
	add	w1, w20, w20, lsl 1
	asr	w1, w1, 2
	strh	w1, [x22,1368]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x22,520]
	ldrh	w2, [x1,x0]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1,2546]
	ldrh	w1, [x1,2472]
	mul	w0, w0, w1
	mov	w1, 2
	sdiv	w0, w0, w1
	cmp	w2, w0
	ble	.L3041
	ldrh	w0, [x22,552]
	sub	w20, w20, #1
	cmp	w0, w20
	blt	.L3041
	bl	FtlReadRefresh
	b	.L3170
.L3041:
	cbnz	w2, .L3040
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,552]
	add	w2, w2, 1
	b	.L3145
.L3034:
	adrp	x0, .LANCHOR0+2372
	mov	w21, 2
	ldr	w0, [x0,#:lo12:.LANCHOR0+2372]
	cmp	w0, wzr
	csinc	w21, w21, wzr, ne
	b	.L3033
.L3099:
	mov	w20, 65535
.L3033:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,800]
	cmp	w1, w2
	bne	.L3043
	cmp	w20, w1
	beq	.L3044
	strh	w20, [x0,800]
	b	.L3045
.L3044:
	ldrh	w1, [x0,1456]
	cmp	w1, w20
	beq	.L3045
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x0,520]
	ldrh	w1, [x2,x1]
	cbnz	w1, .L3046
	mov	w1, -1
	strh	w1, [x0,1456]
.L3046:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,1456]
	strh	w1, [x0,800]
	mov	w1, -1
	strh	w1, [x0,1456]
.L3045:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x22,800]
	strb	wzr, [x22,808]
	cmp	w0, w1
	beq	.L3043
	bl	IsBlkInGcList
	cbz	w0, .L3048
	mov	w0, -1
	strh	w0, [x22,800]
.L3048:
	adrp	x0, .LANCHOR0+204
	ldrb	w0, [x0,#:lo12:.LANCHOR0+204]
	cbz	w0, .L3049
	add	x0, x19, :lo12:.LANCHOR2
	add	x22, x0, 800
	ldrh	w0, [x0,800]
	bl	ftl_get_blk_mode
	strb	w0, [x22,8]
.L3049:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x23, x22, 800
	ldrh	w1, [x22,800]
	cmp	w1, w0
	beq	.L3043
	mov	x0, x23
	bl	make_superblock
	strh	wzr, [x22,802]
	adrp	x0, .LANCHOR5
	ldrh	w2, [x22,800]
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x1, [x22,520]
	strb	wzr, [x22,806]
	strh	wzr, [x0,1466]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,1468]
.L3043:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,800]
	ldrh	w2, [x0,560]
	cmp	w2, w1
	beq	.L3050
	ldrh	w2, [x0,608]
	cmp	w2, w1
	beq	.L3050
	ldrh	w0, [x0,656]
	cmp	w0, w1
	bne	.L3051
.L3050:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, -1
	strh	w1, [x0,800]
.L3051:
	add	x24, x19, :lo12:.LANCHOR2
	mov	w25, 65535
.L3084:
	ldrh	w0, [x24,800]
	cmp	w0, w25
	bne	.L3052
	str	wzr, [x24,1380]
.L3053:
	ldrh	w22, [x24,1376]
	mov	w0, w22
	bl	List_get_gc_head_node
	uxth	w23, w0
	cmp	w23, w25
	strh	w23, [x24,800]
	bne	.L3054
	strh	wzr, [x24,1376]
	mov	w2, 8
	b	.L3145
.L3054:
	mov	w0, w23
	add	w22, w22, 1
	bl	IsBlkInGcList
	cbz	w0, .L3055
	strh	w22, [x24,1376]
	b	.L3053
.L3055:
	adrp	x0, .LANCHOR0
	ubfiz	x1, x23, 1, 16
	add	x5, x0, :lo12:.LANCHOR0
	ldr	x2, [x24,520]
	uxth	w22, w22
	mov	w3, 2
	strh	w22, [x24,1376]
	ldrh	w0, [x5,2544]
	ldrh	w5, [x5,2472]
	ldrh	w4, [x2,x1]
	mul	w0, w0, w5
	sdiv	w5, w0, w3
	cmp	w4, w5
	bgt	.L3057
	cmp	w4, 8
	bls	.L3058
	cmp	w22, 48
	bls	.L3058
	ldrh	w4, [x24,1428]
	cmp	w4, 35
	bhi	.L3058
.L3057:
	strh	wzr, [x24,1376]
.L3058:
	cmp	w20, w25
	ldrh	w1, [x2,x1]
	bne	.L3059
	cmp	w1, w0
	blt	.L3059
	ldrh	w0, [x24,1376]
	cmp	w0, 3
	bhi	.L3059
	mov	w0, -1
	strh	w0, [x24,800]
	adrp	x0, .LANCHOR5+1464
	strh	wzr, [x24,1376]
	ldrh	w2, [x0,#:lo12:.LANCHOR5+1464]
	b	.L3145
.L3059:
	cbnz	w1, .L3060
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x24,1376]
	add	w0, w0, 1
	strh	w0, [x24,1376]
	b	.L3053
.L3060:
	adrp	x0, .LANCHOR0
	strb	wzr, [x24,808]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbz	w0, .L3061
	mov	w0, w23
	bl	ftl_get_blk_mode
	strb	w0, [x24,808]
.L3061:
	add	x22, x24, 800
	mov	x0, x22
	bl	make_superblock
	adrp	x1, .LANCHOR5
	ldrh	w2, [x24,800]
	add	x1, x1, :lo12:.LANCHOR5
	ldr	x0, [x24,520]
	strh	wzr, [x1,1466]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,1468]
	strh	wzr, [x24,802]
	strb	wzr, [x24,806]
.L3052:
	ldr	w0, [x29,124]
	cmp	w0, 1
	bne	.L3062
	bl	FtlReadRefresh
.L3062:
	mov	w0, 1
	str	w0, [x24,3568]
	adrp	x0, .LANCHOR0
	adrp	x22, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,204]
	ldrh	w26, [x0,2544]
	cbz	w1, .L3063
	ldrb	w1, [x24,808]
	cmp	w1, 1
	bne	.L3063
	ldrh	w26, [x0,2546]
.L3063:
	ldrh	w0, [x24,802]
	add	w1, w0, w21
	cmp	w1, w26
	ble	.L3064
	sub	w21, w26, w0
	uxth	w21, w21
.L3064:
	mov	w27, 0
.L3065:
	cmp	w21, w27, uxth
	bls	.L3072
	add	x1, x24, 800
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w4, [x1,2]
	ldrh	w6, [x0,2472]
	mov	x0, 0
	add	w4, w4, w27
	mov	w23, w0
.L3073:
	cmp	w6, w0, uxth
	bls	.L3173
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,16]
	cmp	w2, w25
	beq	.L3066
	mov	w3, 56
	ldr	x7, [x24,1416]
	orr	w2, w4, w2, lsl 10
	umull	x5, w23, w3
	add	w23, w23, 1
	add	x5, x7, x5
	uxth	w23, w23
	str	w2, [x5,4]
.L3066:
	add	x0, x0, 1
	b	.L3073
.L3173:
	ldr	x0, [x24,1416]
	mov	w1, w23
	ldrb	w2, [x24,808]
	mov	x28, 0
	bl	FlashReadPages
.L3068:
	cmp	w23, w28, uxth
	bls	.L3174
	mov	x0, 56
	mul	x4, x28, x0
	ldr	x0, [x24,1416]
	add	x1, x0, x4
	ldr	w0, [x0,x4]
	cmn	w0, #1
	ldr	x5, [x1,16]
	beq	.L3102
	ldrh	w0, [x5]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L3102
	ldr	w0, [x5,8]
	add	x1, x29, 136
	mov	w2, 0
	str	x4, [x29,104]
	str	x5, [x29,112]
	bl	log2phys
	ldr	x2, [x24,1416]
	ldr	x4, [x29,104]
	ldr	w0, [x29,136]
	add	x2, x2, x4
	ldr	x5, [x29,112]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L3102
	adrp	x0, .LANCHOR5
	mov	x3, 56
	add	x1, x0, :lo12:.LANCHOR5
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrh	w0, [x1,1466]
	add	w0, w0, 1
	strh	w0, [x1,1466]
	ldr	w0, [x24,1384]
	ldr	x1, [x24,3600]
	madd	x1, x0, x3, x1
	ldr	w0, [x2,24]
	str	x1, [x29,112]
	str	w0, [x1,24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29,112]
	mov	x2, 56
	ldr	x4, [x29,96]
	ldr	x5, [x29,104]
	str	w0, [x1,4]
	ldr	w0, [x24,1384]
	ldr	x1, [x24,3600]
	madd	x0, x0, x2, x1
	ldr	x1, [x24,1416]
	add	x1, x1, x4
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	add	x2, x24, 656
	str	x2, [x29,112]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,136]
	str	w0, [x5,12]
	ldrh	w0, [x24,656]
	strh	w0, [x5,2]
	ldr	w0, [x24,756]
	str	w0, [x5,4]
	ldr	w0, [x24,1384]
	add	w0, w0, 1
	str	w0, [x24,1384]
	ldr	x0, [x24,1416]
	add	x0, x0, x4
	bl	FtlGcBufAlloc
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,204]
	cbnz	w0, .L3070
	ldr	x2, [x29,112]
	ldr	w1, [x24,1384]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L3070
	ldrh	w0, [x2,4]
	cbnz	w0, .L3102
.L3070:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L3102
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,800]
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x19,3568]
	strh	wzr, [x19,802]
	ldrh	w2, [x0,1464]
	b	.L3145
.L3102:
	add	x28, x28, 1
	b	.L3068
.L3174:
	add	w27, w27, 1
	b	.L3065
.L3072:
	ldrh	w0, [x24,802]
	add	w21, w21, w0
	uxth	w21, w21
	strh	w21, [x24,802]
	cmp	w21, w26
	bcc	.L3074
	ldr	w0, [x24,1384]
	adrp	x21, .LANCHOR5
	cbz	w0, .L3075
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L3075
	str	wzr, [x24,3568]
	add	x21, x21, :lo12:.LANCHOR5
.L3170:
	ldrh	w2, [x21,1464]
	b	.L3145
.L3075:
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w21, [x21,1466]
	cbnz	w21, .L3076
	ldrh	w1, [x24,800]
	ldr	x0, [x24,520]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L3076
.L3077:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2616]
	cmp	w21, w0
	bcs	.L3082
	mov	w0, w21
	add	x1, x29, 140
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,140]
	cmn	w0, #1
	beq	.L3078
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x24,800]
	cmp	w1, w0, uxth
	bne	.L3078
.L3082:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2616]
	cmp	w21, w0
	bcc	.L3076
	ldrh	w1, [x24,800]
	ldr	x0, [x24,520]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x24,800]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3076
.L3078:
	add	w21, w21, 1
	b	.L3077
.L3076:
	mov	w0, -1
	strh	w0, [x24,800]
.L3074:
	ldrh	w0, [x24,552]
	cmp	w0, 2
	bhi	.L3083
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w21, [x22,2544]
	b	.L3084
.L3083:
	adrp	x1, .LANCHOR5+1464
	str	wzr, [x24,3568]
	ldrh	w2, [x1,#:lo12:.LANCHOR5+1464]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L3145
.L3172:
	ldrh	w25, [x21,1456]
	cmp	w25, w20
	bne	.L3009
	ldrh	w0, [x21,552]
	mov	w2, 1024
	ldr	w1, [x21,1372]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L3009
	adrp	x0, .LANCHOR5+1464
	str	wzr, [x21,1372]
	mov	x23, x21
	strh	wzr, [x0,#:lo12:.LANCHOR5+1464]
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	cmp	w20, w25
	bne	.L3011
	ldrh	w1, [x21,552]
	ldrh	w0, [x21,1370]
	cmp	w1, w0
	bcs	.L3012
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w20
	beq	.L3021
	ldr	w2, [x21,3564]
	uxtw	x1, w0
	adrp	x21, .LANCHOR0
	cbnz	w2, .L3014
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w2, [x0,2492]
	cmp	w2, 3
	beq	.L3014
	ldr	w2, [x23,848]
	cbnz	w2, .L3014
	ldr	w2, [x0,2372]
	cbnz	w2, .L3014
	ldrb	w0, [x0,204]
	cbz	w0, .L3015
.L3014:
	add	x0, x19, :lo12:.LANCHOR2
	add	x3, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,520]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x3,2546]
	ldrh	w0, [x3,2472]
	ldrh	w3, [x3,2492]
	mul	w2, w1, w0
	cmp	w3, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w2, w0
	cmp	w4, w0
	bgt	.L3017
	mov	w0, 0
	add	x21, x21, :lo12:.LANCHOR0
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x21,2616]
	ldr	w2, [x1,716]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L3018
	mov	w0, 128
	b	.L3168
.L3018:
	mov	w0, 160
.L3168:
	strh	w0, [x1,1370]
	b	.L3019
.L3017:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L3169
.L3015:
	ldr	x2, [x23,520]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L3020
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0,1370]
	b	.L3019
.L3020:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L3169:
	strh	w1, [x0,1370]
	b	.L3021
.L3012:
	mov	w0, 80
	strh	w0, [x21,1370]
	b	.L3021
.L3019:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L3021
.L3011:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x20, 1, 32
	adrp	x0, .LC157
	mov	w1, w20
	add	x0, x0, :lo12:.LC157
	ldr	x3, [x5,520]
	ldr	x6, [x5,440]
	ldrh	w2, [x5,552]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,1368]
	ldrh	w4, [x6,x4]
	bl	printk
.L3021:
	bl	FtlGcReFreshBadBlk
	b	.L3009
.L3171:
	mov	w20, w0
	b	.L3009
.L3145:
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
	stp	x29, x30, [sp, -128]!
	adrp	x0, .LANCHOR5+1472
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	ldr	x20, [x0,#:lo12:.LANCHOR5+1472]
	add	x0, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w23, [x0,424]
	cbnz	w23, .L3177
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2600]
	cbz	w1, .L3177
	ldrb	w0, [x0,204]
	cbz	w0, .L3179
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w23, eq
.L3179:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x20,9]
	adrp	x26, .LC158
	mov	w2, w23
	mov	w24, 0
	mov	w27, 56
	ldr	x0, [x0,2608]
	add	x26, x26, :lo12:.LC158
	bl	FlashProgPages
.L3180:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2600]
	cmp	w24, w1
	bcs	.L3198
	umull	x21, w24, w27
	ldr	x1, [x0,2608]
	add	x0, x1, x21
	ldr	w25, [x1,x21]
	cmn	w25, #1
	beq	.L3201
	ldr	w1, [x0,4]
	cbz	w23, .L3215
	orr	w1, w1, -2147483648
.L3215:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,124]
	add	x1, x29, 124
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2608]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L3184
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x22, :lo12:.LANCHOR2
	ubfiz	x0, x1, 1, 16
	mov	w21, w1
	ldr	x2, [x2,520]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L3185
	mov	x0, x26
	bl	printk
.L3185:
	mov	w0, w21
	bl	decrement_vpc_count
.L3184:
	add	w24, w24, 1
	b	.L3180
.L3217:
	mov	w20, 16386
.L3197:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w0, [x0,1464]
	cbz	w0, .L3198
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w20, w20, #1
	bne	.L3197
.L3198:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19,2600]
	b	.L3177
.L3201:
	adrp	x26, .LC158
	mov	w24, 0
	mov	w27, 56
	str	w25, [x29,108]
	mov	w28, 1
	add	x26, x26, :lo12:.LC158
.L3181:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2600]
	cmp	w24, w1
	bcs	.L3217
	umull	x21, w24, w27
	ldr	x0, [x0,2608]
	ldr	w1, [x29,108]
	str	w1, [x0,x21]
.L3187:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x0, [x2,2608]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L3218
	ldr	w0, [x1,4]
	str	x2, [x29,96]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	ldr	x2, [x29,96]
	bne	.L3188
	add	x0, x22, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldrh	w4, [x20,4]
	ldr	x3, [x0,520]
	ldrh	w0, [x3,x1]
	sub	w0, w0, w4
	strh	w0, [x3,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x2,2544]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L3188:
	ldrh	w0, [x20,4]
	cbnz	w0, .L3189
	mov	x0, x20
	bl	allocate_new_data_superblock
.L3189:
	add	x5, x22, :lo12:.LANCHOR2
	add	x25, x19, :lo12:.LANCHOR0
	str	x5, [x29,96]
	ldr	w0, [x5,952]
	add	w0, w0, 1
	str	w0, [x5,952]
	ldr	x0, [x25,2608]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,124]
	ldr	x1, [x25,2608]
	mov	w2, w23
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x25,2608]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	ldr	x0, [x25,2608]
	ldr	x5, [x29,96]
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L3190
	str	w28, [x5,424]
.L3190:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w0, [x0,424]
	cbz	w0, .L3187
	b	.L3177
.L3218:
	ldr	w0, [x1,4]
	cbz	w23, .L3216
	orr	w0, w0, -2147483648
.L3216:
	str	w0, [x29,124]
	mov	w2, 1
	ldr	w0, [x1,24]
	add	x1, x29, 124
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2608]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L3194
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x22, :lo12:.LANCHOR2
	ubfiz	x0, x1, 1, 16
	mov	w21, w1
	ldr	x2, [x2,520]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L3195
	mov	x0, x26
	bl	printk
.L3195:
	mov	w0, w21
	bl	decrement_vpc_count
.L3194:
	add	w24, w24, 1
	b	.L3181
.L3177:
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
	adrp	x0, .LANCHOR2+424
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2+424]
	str	x19, [sp,16]
	cbnz	w0, .L3220
	adrp	x0, .LANCHOR1+504
	ldr	w19, [x0,#:lo12:.LANCHOR1+504]
	cmp	w19, 1
	bne	.L3220
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, w19
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3220:
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	adrp	x0, .LANCHOR1+504
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR1+504]
	cmp	w0, 1
	bne	.L3222
	bl	FtlSysFlush
.L3222:
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
	.global	rk_ftl_de_init
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LC159
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC159
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
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	str	x23, [sp,48]
	add	x21, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	mov	w19, w1
	ldr	w2, [x21,2584]
	cmp	w1, w2
	bhi	.L3234
	cmp	w0, w2
	cset	w1, cs
	cbnz	w1, .L3234
	add	w3, w0, w19
	mov	w0, -1
	cmp	w3, w2
	bhi	.L3228
	cmp	w19, 31
	mov	w0, w1
	bls	.L3228
	adrp	x23, .LANCHOR2
	add	x0, x23, :lo12:.LANCHOR2
	ldr	w2, [x0,424]
	mov	w0, w1
	cbnz	w2, .L3228
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,2550]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L3229
	sub	w20, w1, w20
	add	w21, w21, 1
	cmp	w20, w19
	csel	w20, w20, w19, ls
	sub	w19, w19, w20, uxth
.L3229:
	mov	w0, -1
	adrp	x20, .LANCHOR5
	str	w0, [x29,76]
.L3230:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2550]
	cmp	w19, w0
	bcc	.L3244
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L3231
	add	x1, x20, :lo12:.LANCHOR5
	mov	w2, 1
	ldr	w0, [x1,1480]
	add	w0, w0, 1
	str	w0, [x1,1480]
	add	x1, x23, :lo12:.LANCHOR2
	ldr	w0, [x1,724]
	add	w0, w0, 1
	str	w0, [x1,724]
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,72]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3231:
	add	x0, x22, :lo12:.LANCHOR0
	add	w21, w21, 1
	ldrh	w0, [x0,2550]
	sub	w19, w19, w0
	b	.L3230
.L3244:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,1480]
	cmp	w1, 32
	bls	.L3233
	str	wzr, [x0,1480]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3233:
	mov	w0, 0
	b	.L3228
.L3234:
	mov	w0, -1
.L3228:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
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
	mov	w4, w0
	adrp	x0, .LANCHOR1+504
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x21, x22, [sp,48]
	ldr	w21, [x0,#:lo12:.LANCHOR1+504]
	mov	w0, -1
	stp	x19, x20, [sp,32]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	cmp	w21, 1
	bne	.L3247
	cmp	w4, 16
	mov	x25, x3
	mov	w26, w2
	mov	w20, w1
	bne	.L3248
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L3247
.L3248:
	adrp	x22, .LANCHOR0
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x2,2584]
	cmp	w20, w1
	bcs	.L3271
	cmp	w26, w1
	bhi	.L3271
	add	w0, w20, w26
	str	w0, [x29,168]
	mov	w0, -1
	ldr	w3, [x29,168]
	cmp	w3, w1
	bhi	.L3247
	ldrh	w0, [x2,2550]
	sub	w27, w3, #1
	adrp	x19, .LANCHOR2
	udiv	w1, w20, w0
	str	w1, [x29,172]
	udiv	w27, w27, w0
	ldr	w0, [x29,172]
	sub	w21, w21, w0
	add	x0, x19, :lo12:.LANCHOR2
	add	w21, w21, w27
	ldr	w1, [x0,748]
	add	w1, w26, w1
	str	w1, [x0,748]
	ldr	w1, [x0,720]
	add	w1, w21, w1
	str	w1, [x0,720]
	ldr	w0, [x29,172]
	mov	w1, w27
	bl	FtlCacheMetchLpa
	cbz	w0, .L3249
	bl	FtlCacheWriteBack
.L3249:
	mov	w28, 0
	adrp	x0, .LC147
	ldr	w23, [x29,172]
	add	x0, x0, :lo12:.LC147
	str	w28, [x29,156]
	mov	w24, w28
	str	w28, [x29,152]
	str	x0, [x29,112]
.L3250:
	cbz	w21, .L3295
	mov	w0, w23
	add	x1, x29, 188
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,188]
	cmn	w3, #1
	bne	.L3292
	mov	w3, 0
.L3251:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2550]
	cmp	w3, w0
	bcs	.L3255
	madd	w0, w23, w0, w3
	cmp	w0, w20
	bcc	.L3253
	ldr	w1, [x29,168]
	cmp	w0, w1
	bcs	.L3253
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x25, x0
	str	x3, [x29,160]
	bl	ftl_memset
	ldr	x3, [x29,160]
.L3253:
	add	w3, w3, 1
	b	.L3251
.L3292:
	mov	w0, 56
	umull	x1, w24, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x0,3592]
	add	x2, x2, x1
	str	w3, [x2,4]
	ldr	w2, [x29,172]
	cmp	w23, w2
	ldr	x2, [x0,3592]
	add	x2, x2, x1
	bne	.L3256
	ldr	x0, [x0,3648]
	str	x0, [x2,8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2550]
	udiv	w3, w20, w0
	msub	w3, w3, w0, w20
	str	w3, [x29,128]
	sub	w3, w0, w3
	cmp	w3, w26
	csel	w3, w3, w26, ls
	str	w3, [x29,156]
	cmp	w3, w0
	bne	.L3257
	str	x25, [x2,8]
	b	.L3257
.L3256:
	cmp	w23, w27
	bne	.L3258
	ldr	x0, [x0,3656]
	str	x0, [x2,8]
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w4, [x29,168]
	ldrh	w3, [x0,2550]
	mul	w0, w23, w3
	sub	w28, w4, w0
	cmp	w28, w3
	bne	.L3257
	b	.L3293
.L3258:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2550]
	mul	w0, w23, w0
.L3293:
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x25, x0
	str	x0, [x2,8]
.L3257:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,3592]
	ldr	x2, [x2,3672]
	add	x1, x0, x1
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2556]
	str	w23, [x1,24]
	mul	w0, w24, w0
	add	w24, w24, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L3255:
	subs	w21, w21, #1
	add	w23, w23, 1
	beq	.L3259
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2472]
	cmp	w24, w0, lsl 3
	bne	.L3250
.L3259:
	cbz	w24, .L3250
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w24
	mov	w2, 0
	ldr	x0, [x0,3592]
	bl	FlashReadPages
	str	xzr, [x29,160]
	ldr	x0, [x29,128]
	ubfiz	x0, x0, 9, 23
	str	x0, [x29,144]
	ldr	w0, [x29,156]
	lsl	w0, w0, 9
	str	w0, [x29,124]
	lsl	w0, w28, 9
	str	w0, [x29,120]
.L3261:
	ldr	w0, [x29,160]
	cmp	w24, w0
	bls	.L3296
	ldr	x0, [x29,160]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	ldr	w3, [x29,172]
	mul	x8, x0, x1
	ldr	x0, [x2,3592]
	add	x0, x0, x8
	ldr	w1, [x0,24]
	cmp	w1, w3
	bne	.L3262
	ldr	x1, [x0,8]
	ldr	x0, [x2,3648]
	cmp	x1, x0
	bne	.L3263
	ldr	x2, [x29,144]
	mov	x0, x25
	str	x8, [x29,104]
	add	x1, x1, x2
	ldr	w2, [x29,124]
	b	.L3294
.L3262:
	cmp	w1, w27
	bne	.L3263
	ldr	x1, [x0,8]
	ldr	x0, [x2,3656]
	cmp	x1, x0
	bne	.L3263
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x29,120]
	str	x8, [x29,104]
	ldrh	w0, [x0,2550]
	mul	w0, w27, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x25, x0
.L3294:
	bl	ftl_memcpy
	ldr	x8, [x29,104]
.L3263:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,3592]
	add	x2, x1, x8
	ldr	w3, [x1,x8]
	cmn	w3, #1
	bne	.L3264
	ldr	w1, [x0,928]
	str	w3, [x29,152]
	add	w1, w1, 1
	str	w1, [x0,928]
.L3264:
	ldr	x0, [x2,16]
	ldr	w1, [x2,24]
	ldr	w0, [x0,8]
	cmp	w1, w0
	beq	.L3265
	add	x0, x19, :lo12:.LANCHOR2
	str	x8, [x29,104]
	ldr	w1, [x0,928]
	add	w1, w1, 1
	str	w1, [x0,928]
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
.L3265:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,3592]
	add	x1, x0, x8
	ldr	w0, [x0,x8]
	cmp	w0, 256
	bne	.L3266
	ldr	w0, [x1,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3266:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L3261
.L3296:
	mov	w24, 0
	b	.L3250
.L3295:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,1464]
	cbz	w0, .L3269
	mov	w0, w21
	mov	w1, 1
	bl	ftl_do_gc
.L3269:
	ldr	w0, [x29,152]
	b	.L3247
.L3271:
	mov	w0, -1
.L3247:
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 208
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
	.global	FlashBootVendorRead
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w19, -1
	str	x0, [x29,40]
	str	x1, [x29,48]
	str	x2, [x29,56]
	bl	rknand_device_lock
	adrp	x3, .LANCHOR1+504
	ldr	x2, [x29,56]
	ldr	x1, [x29,48]
	ldr	x0, [x29,40]
	ldr	w3, [x3,#:lo12:.LANCHOR1+504]
	cmp	w3, 1
	bne	.L3299
	bl	ftl_vendor_read
	mov	w19, w0
.L3299:
	bl	rknand_device_unlock
	mov	w0, w19
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	FlashBootVendorRead, .-FlashBootVendorRead
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
	mov	x2, x19
	mov	w1, 1
	mov	w0, w20
	bl	ftl_sys_read
	str	x0, [x29,40]
	bl	rknand_device_unlock
	ldr	x0, [x29,40]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	stp	x29, x30, [sp, -16]!
	uxtb	w0, w0
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
	adrp	x1, .LC76
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR1
	add	x1, x1, :lo12:.LC76
	add	x22, x22, :lo12:.LANCHOR1
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	str	w0, [x22,504]
	adrp	x0, .LANCHOR5+1484
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR5+1484]
	adrp	x0, .LC75
	add	x0, x0, :lo12:.LC75
	str	wzr, [x19,424]
	bl	printk
	add	x0, x21, 176
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x21,2476]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3305
	adrp	x0, .LC160
	add	x0, x0, :lo12:.LC160
	b	.L3319
.L3305:
	bl	FtlSysBlkInit
	mov	w20, w0
	cbz	w0, .L3307
	adrp	x0, .LC161
	add	x0, x0, :lo12:.LC161
.L3319:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 256
	bl	printk
	b	.L3306
.L3307:
	mov	w1, 1
	str	w1, [x22,504]
	bl	ftl_do_gc
	ldrh	w0, [x19,552]
	cmp	w0, 15
	bhi	.L3308
	mov	w22, 65535
	add	w21, w0, 2
.L3311:
	ldrh	w0, [x19,800]
	cmp	w0, w22
	bne	.L3309
	ldrh	w0, [x19,1456]
	cmp	w0, w22
	bne	.L3309
	and	w0, w20, 63
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3309:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldrh	w0, [x19,552]
	cmp	w0, w21
	bhi	.L3306
	add	w20, w20, 1
	cmp	w20, 4096
	bne	.L3311
	b	.L3306
.L3308:
	ldrb	w0, [x21,204]
	cbz	w0, .L3306
	mov	w19, 128
.L3313:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3313
.L3306:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlInit, .-FtlInit
	.align	2
	.global	rk_ftl_init
	.type	rk_ftl_init, %function
rk_ftl_init:
	stp	x29, x30, [sp, -32]!
	mov	w0, 2048
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR5
	adrp	x19, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR5
	bl	ftl_malloc
	add	x19, x19, :lo12:.LANCHOR4
	add	x1, x20, 1496
	str	x0, [x20,1488]
	add	x0, x19, 1592
	str	xzr, [x19,1592]
	str	xzr, [x20,1496]
	bl	rknand_get_reg_addr
	ldr	x1, [x19,1592]
	mov	w0, -1
	cbz	x1, .L3321
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x20,1488]
	mov	w2, w1
	mov	w3, 2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	x0, [x19,1592]
	bl	FlashInit
	mov	w19, w0
	cbnz	w0, .L3322
	bl	FtlInit
.L3322:
	adrp	x0, .LC162
	mov	w1, w19
	add	x0, x0, :lo12:.LC162
	bl	printk
	mov	w0, w19
.L3321:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x27, [sp,80]
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrb	w0, [x0,204]
	cbz	w0, .L3325
	adrp	x21, .LANCHOR5
	adrp	x19, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR5
	add	x24, x19, :lo12:.LANCHOR2
	adrp	x25, .LC163
	add	x27, x24, 560
	add	x26, x24, 608
	ldrh	w23, [x0,1328]
	add	x0, x25, :lo12:.LC163
	ldr	x2, [x24,520]
	ubfiz	x22, x23, 1, 16
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldrh	w0, [x24,560]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x24,608]
	mov	w24, 4097
	bl	FtlGcRefreshOpenBlock
	mov	x0, x27
	bl	allocate_new_data_superblock
	mov	x0, x26
	bl	allocate_new_data_superblock
.L3327:
	subs	w24, w24, #1
	beq	.L3331
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,520]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3327
.L3331:
	add	x24, x19, :lo12:.LANCHOR2
	add	x0, x25, :lo12:.LC163
	mov	w1, w23
	ldr	x2, [x24,520]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x24,520]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3329
	add	x0, x29, 96
	add	x20, x20, :lo12:.LANCHOR0
	strh	w23, [x29,96]
	bl	make_superblock
	mov	x0, 0
	ldrh	w5, [x20,2472]
	mov	w6, 65535
	mov	w20, w0
	mov	w7, 56
.L3332:
	cmp	w5, w0, uxth
	bls	.L3339
	add	x1, x29, 96
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L3333
	add	x4, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x1, w20, w7
	add	w20, w20, 1
	ldr	x3, [x4,432]
	uxth	w20, w20
	add	x3, x3, x1
	str	w2, [x3,4]
	ldr	x2, [x4,432]
	add	x1, x2, x1
	str	xzr, [x1,8]
	str	xzr, [x1,16]
.L3333:
	add	x0, x0, 1
	b	.L3332
.L3339:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC164
	mov	w1, w23
	add	x0, x0, :lo12:.LC164
	ldr	x2, [x19,520]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x19,432]
	mov	w1, 0
	mov	w2, w20
	bl	FlashEraseBlocks
	ldr	x0, [x19,432]
	mov	w1, 1
	mov	w2, w20
	bl	FlashEraseBlocks
.L3329:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w0, -1
	strh	w0, [x21,1328]
.L3325:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
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
	stp	x29, x30, [sp, -288]!
	mov	w4, w0
	mov	w0, 0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	mov	w23, w1
	adrp	x1, .LANCHOR2
	stp	x25, x26, [sp,64]
	mov	w25, w2
	add	x2, x1, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	x26, x3
	str	x1, [x29,176]
	ldr	w3, [x2,424]
	cbnz	w3, .L3342
	adrp	x1, .LANCHOR1
	str	x1, [x29,152]
	add	x3, x1, :lo12:.LANCHOR1
	ldr	w3, [x3,504]
	cmp	w3, 1
	bne	.L3342
	cmp	w4, 16
	bne	.L3343
	add	w0, w23, 256
	mov	w1, w25
	mov	x2, x26
	bl	FtlVendorPartWrite
	b	.L3342
.L3343:
	adrp	x21, .LANCHOR0
	add	x4, x21, :lo12:.LANCHOR0
	ldr	w1, [x4,2584]
	cmp	w23, w1
	bcs	.L3385
	cmp	w25, w1
	bhi	.L3385
	add	w3, w23, w25
	mov	w0, -1
	cmp	w3, w1
	bhi	.L3342
	adrp	x5, .LANCHOR5
	ldrh	w1, [x4,2550]
	add	x22, x5, :lo12:.LANCHOR5
	sub	w3, w3, #1
	mov	w0, 2048
	str	x5, [x29,144]
	udiv	w27, w23, w1
	cmp	w25, w1, lsl 1
	str	w0, [x22,1504]
	udiv	w0, w3, w1
	ldr	w3, [x4,2600]
	str	w0, [x29,208]
	sub	w24, w0, w27
	add	w20, w24, 1
	ldr	w0, [x2,728]
	add	w0, w20, w0
	str	w0, [x2,728]
	ldr	w0, [x2,744]
	add	w0, w25, w0
	str	w0, [x2,744]
	cset	w0, cs
	str	w0, [x29,212]
	cbz	w3, .L3345
	mov	w0, 56
	sub	w3, w3, #1
	umull	x3, w3, w0
	ldr	x0, [x4,2608]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w27, w0
	bne	.L3346
	ldr	w0, [x2,732]
	add	w0, w0, 1
	str	w0, [x2,732]
	ldr	w0, [x22,1508]
	add	w0, w0, 1
	str	w0, [x22,1508]
	msub	w0, w27, w1, w23
	sub	w1, w1, w0
	ubfiz	x0, x0, 9, 23
	cmp	w1, w25
	csel	w19, w1, w25, ls
	ldr	x1, [x3,8]
	lsl	w20, w19, 9
	add	x0, x1, x0
	mov	w2, w20
	mov	x1, x26
	bl	ftl_memcpy
	cbnz	w24, .L3347
	ldr	w0, [x22,1508]
	cmp	w0, 2
	bgt	.L3347
.L3381:
	mov	w0, 0
	b	.L3342
.L3347:
	add	x26, x26, x20
	sub	w25, w25, w19
	add	w23, w23, w19
	add	w27, w27, 1
	mov	w20, w24
.L3346:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,1508]
.L3345:
	ldr	w1, [x29,208]
	mov	w0, w27
	bl	FtlCacheMetchLpa
	cbz	w0, .L3348
	bl	FtlCacheWriteBack
.L3348:
	ldr	x0, [x29,144]
	ldr	x1, [x29,176]
	add	x0, x0, :lo12:.LANCHOR5
	str	w27, [x29,192]
	add	x24, x1, :lo12:.LANCHOR2
	add	x22, x24, 560
	str	x22, [x29,184]
	str	x22, [x0,1472]
	adrp	x0, .LC165
	add	x0, x0, :lo12:.LC165
	str	x0, [x29,104]
.L3349:
	cbz	w20, .L3413
	ldrh	w0, [x22,4]
	cbnz	w0, .L3350
	ldr	x1, [x29,184]
	cmp	x22, x1
	bne	.L3351
	add	x0, x24, 608
	ldrh	w19, [x0,4]
	cbnz	w19, .L3352
	bl	allocate_new_data_superblock
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR1
	str	w19, [x0,3460]
.L3352:
	ldr	x0, [x29,184]
	add	x22, x24, 608
	bl	allocate_new_data_superblock
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR1
	ldr	w0, [x0,3460]
	cbnz	w0, .L3353
.L3354:
	ldr	x22, [x29,184]
	b	.L3353
.L3351:
	ldr	x1, [x29,152]
	add	x1, x1, :lo12:.LANCHOR1
	str	w0, [x1,3460]
	ldr	x0, [x29,184]
	ldrh	w0, [x0,4]
	cbnz	w0, .L3354
	mov	x0, x22
	bl	allocate_new_data_superblock
.L3353:
	ldrh	w0, [x22,4]
	cbnz	w0, .L3355
	mov	x0, x22
	bl	allocate_new_data_superblock
.L3355:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	x22, [x0,1472]
.L3350:
	add	x2, x21, :lo12:.LANCHOR0
	ldr	w0, [x24,3588]
	ldrh	w1, [x22,4]
	str	xzr, [x29,200]
	ldr	w2, [x2,2600]
	sub	w0, w0, w2
	cmp	w1, w0
	csel	w0, w1, w0, ls
	cmp	w0, w20
	csel	w0, w0, w20, ls
	str	w0, [x29,140]
	ldr	w0, [x29,212]
	and	w0, w0, 1
	str	w0, [x29,116]
	add	w0, w25, w23
	str	w0, [x29,136]
.L3356:
	ldr	w0, [x29,200]
	ldr	w2, [x29,140]
	ldr	w1, [x29,192]
	cmp	w0, w2
	add	w28, w1, w0
	bcs	.L3414
	ldrh	w1, [x22,4]
	cbz	w1, .L3389
	ldr	w1, [x29,208]
	cmp	w28, w1
	cset	w6, eq
	cbz	w0, .L3358
	ldr	w1, [x29,116]
	tst	w6, w1
	beq	.L3358
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,136]
	ldrh	w1, [x1,2550]
	msub	w2, w28, w1, w2
	cmp	w2, w1
	bne	.L3389
.L3358:
	add	x1, x29, 228
	mov	w2, 0
	mov	w0, w28
	str	x6, [x29,96]
	add	x19, x21, :lo12:.LANCHOR0
	bl	log2phys
	mov	x0, x22
	bl	get_new_active_ppa
	ldr	w5, [x19,2600]
	mov	w4, 56
	ldr	x2, [x19,2608]
	str	x4, [x29,160]
	umull	x1, w5, w4
	add	x2, x2, x1
	str	w0, [x2,4]
	ldrh	w2, [x19,2556]
	ldr	x0, [x19,2608]
	add	x1, x0, x1
	mul	w0, w2, w5
	and	x0, x0, 4294967292
	str	x0, [x29,128]
	ldr	x0, [x24,3680]
	ldr	x3, [x29,128]
	str	x0, [x29,120]
	add	x0, x0, x3
	str	x0, [x29,216]
	str	x0, [x1,16]
	ldrh	w0, [x19,2554]
	str	w28, [x1,24]
	mul	w0, w0, w5
	ldr	x5, [x24,3640]
	and	x0, x0, 4294967292
	add	x0, x5, x0
	str	x0, [x1,8]
	ldr	x0, [x29,216]
	mov	w1, 0
	bl	ftl_memset
	cmp	w28, w27
	cset	w0, eq
	str	w0, [x29,196]
	cbnz	w0, .L3393
	ldr	x6, [x29,96]
	ldr	x4, [x29,160]
	cbz	w6, .L3359
	ldr	w0, [x29,136]
	ldrh	w19, [x19,2550]
	msub	w19, w28, w19, w0
	ldr	w0, [x29,196]
	str	w0, [x29,160]
	uxth	w19, w19
	b	.L3362
.L3393:
	ldrh	w19, [x19,2550]
	udiv	w0, w23, w19
	msub	w0, w0, w19, w23
	str	w0, [x29,160]
	sub	w19, w19, w0
	cmp	w19, w25
	csel	w19, w19, w25, ls
.L3362:
	add	x2, x21, :lo12:.LANCHOR0
	ldrh	w0, [x2,2550]
	cmp	w19, w0
	bne	.L3363
	ldr	w0, [x29,196]
	mov	x1, x26
	cbnz	w0, .L3364
	mul	w1, w19, w28
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x26, x1
.L3364:
	ldr	w2, [x29,212]
	mov	w0, 56
	add	x4, x21, :lo12:.LANCHOR0
	cbz	w2, .L3365
	ldr	w2, [x4,2600]
	umull	x0, w2, w0
	ldr	x2, [x4,2608]
	add	x0, x2, x0
	b	.L3411
.L3365:
	ldr	w2, [x4,2600]
	umull	x0, w2, w0
	ldr	x2, [x4,2608]
	add	x0, x2, x0
	ldrh	w2, [x4,2554]
	ldr	x0, [x0,8]
	b	.L3412
.L3363:
	ldr	w0, [x29,228]
	cmn	w0, #1
	beq	.L3367
	str	w0, [x29,236]
	mov	w1, 56
	ldr	w0, [x2,2600]
	str	w28, [x29,256]
	umull	x0, w0, w1
	ldr	x1, [x2,2608]
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
	bne	.L3368
	ldr	w0, [x24,928]
	add	w0, w0, 1
	str	w0, [x24,928]
	b	.L3370
.L3368:
	ldr	x0, [x29,216]
	ldr	w0, [x0,8]
	cmp	w0, w28
	beq	.L3370
	ldr	w0, [x24,928]
	mov	w2, w28
	ldr	x1, [x29,216]
	add	w0, w0, 1
	str	w0, [x24,928]
	ldr	x0, [x29,104]
	ldr	w1, [x1,8]
	bl	printk
	b	.L3370
.L3367:
	ldr	w0, [x2,2600]
	mov	w1, 56
	umull	x0, w0, w1
	ldr	x1, [x2,2608]
	ldrh	w2, [x2,2554]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L3370:
	ldr	w1, [x29,196]
	mov	w0, 56
	lsl	w2, w19, 9
	cbz	w1, .L3371
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w4, [x1,2600]
	umull	x0, w4, w0
	ldr	x4, [x1,2608]
	add	x4, x4, x0
	ldr	x0, [x29,160]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x4,8]
	add	x0, x1, x0
	mov	x1, x26
	b	.L3412
.L3371:
	add	x5, x21, :lo12:.LANCHOR0
	ldr	w4, [x5,2600]
	ldr	x1, [x5,2608]
	umull	x0, w4, w0
	add	x0, x1, x0
	ldrh	w1, [x5,2550]
	mul	w1, w28, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x26, x1
	b	.L3412
.L3359:
	ldr	w0, [x29,212]
	ldrh	w1, [x19,2550]
	ldr	w2, [x19,2600]
	cbz	w0, .L3372
	mul	w1, w28, w1
	umull	x0, w2, w4
	sub	w1, w1, w23
	ldr	x2, [x19,2608]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x26, x1
.L3411:
	str	x1, [x0,8]
	b	.L3366
.L3372:
	umull	x0, w2, w4
	ldr	x2, [x19,2608]
	mul	w1, w28, w1
	add	x0, x2, x0
	ldrh	w2, [x19,2554]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x26, x1
.L3412:
	bl	ftl_memcpy
.L3366:
	ldr	x1, [x29,120]
	mov	w0, -3947
	ldr	x2, [x29,128]
	strh	w0, [x1,x2]
	ldr	x1, [x29,216]
	ldr	w0, [x24,756]
	str	w0, [x1,4]
	add	w0, w0, 1
	cmn	w0, #1
	str	w28, [x1,8]
	csel	w0, w0, wzr, ne
	str	w0, [x24,756]
	ldr	w0, [x29,228]
	str	w0, [x1,12]
	ldrh	w0, [x22]
	strh	w0, [x1,2]
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w0, [x1,2600]
	add	w0, w0, 1
	str	w0, [x1,2600]
	ldr	x0, [x29,200]
	add	x0, x0, 1
	str	x0, [x29,200]
	b	.L3356
.L3414:
	str	w28, [x29,192]
	mov	x0, x2
	b	.L3357
.L3389:
	str	w28, [x29,192]
.L3357:
	sub	w20, w20, w0
	ldr	w0, [x29,212]
	cbnz	w0, .L3376
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x0,2600]
	ldr	w0, [x24,3588]
	cmp	w1, w0
	bcs	.L3376
	ldrh	w0, [x22,4]
	cbz	w0, .L3376
.L3378:
	str	wzr, [x29,212]
	b	.L3349
.L3376:
	bl	FtlCacheWriteBack
	add	x0, x21, :lo12:.LANCHOR0
	cmp	w20, 1
	str	wzr, [x0,2600]
	bhi	.L3349
	b	.L3378
.L3413:
	ldr	w1, [x29,208]
	mov	w0, w20
	sub	w1, w1, w27
	bl	ftl_do_gc
	ldr	x0, [x29,176]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w0, [x0,552]
	cmp	w0, 5
	bls	.L3380
	cmp	w0, 31
	bhi	.L3381
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,72]
	cbnz	w0, .L3381
.L3380:
	ldr	x0, [x29,176]
	mov	w21, 65535
	mov	w22, 128
	add	x19, x0, :lo12:.LANCHOR2
.L3404:
	ldrh	w0, [x19,800]
	cmp	w0, w21
	bne	.L3382
	ldrh	w0, [x19,1456]
	cmp	w0, w21
	bne	.L3382
	ldrh	w0, [x19,1458]
	cmp	w0, w21
	bne	.L3382
	and	w0, w20, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3382:
	mov	w0, 1
	strh	w22, [x19,1370]
	mov	w1, w0
	strh	w22, [x19,1368]
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldr	w0, [x19,424]
	cbnz	w0, .L3381
	ldrh	w0, [x19,552]
	cmp	w0, 2
	bhi	.L3381
	add	w20, w20, 1
	cmp	w20, 256
	bne	.L3404
	b	.L3381
.L3385:
	mov	w0, -1
.L3342:
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
	.global	FlashBootVendorWrite
	.type	FlashBootVendorWrite, %function
FlashBootVendorWrite:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w19, -1
	str	x0, [x29,40]
	str	x1, [x29,48]
	str	x2, [x29,56]
	bl	rknand_device_lock
	adrp	x3, .LANCHOR1+504
	ldr	x2, [x29,56]
	ldr	x1, [x29,48]
	ldr	x0, [x29,40]
	ldr	w3, [x3,#:lo12:.LANCHOR1+504]
	cmp	w3, 1
	bne	.L3417
	bl	ftl_vendor_write
	mov	w19, w0
.L3417:
	bl	rknand_device_unlock
	mov	w0, w19
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
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
	.global	StorageSysDataStore
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x0, [x29,16]
	str	x1, [x29,24]
	bl	rknand_device_lock
	ldr	x2, [x29,24]
	mov	w1, 1
	ldr	x0, [x29,16]
	bl	ftl_sys_write
	str	x0, [x29,24]
	bl	rknand_device_unlock
	ldr	x0, [x29,24]
	ldp	x29, x30, [sp], 32
	ret
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.global	FtlDumpSysBlock
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	sub	sp, sp, #96
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	stp	x21, x22, [sp,48]
	adrp	x19, .LANCHOR4
	adrp	x21, .LANCHOR2
	stp	x23, x24, [sp,64]
	add	x19, x19, :lo12:.LANCHOR4
	uxth	w23, w0
	add	x0, x21, :lo12:.LANCHOR2
	str	x25, [sp,80]
	add	x19, x19, 1704
	adrp	x22, .LC166
	adrp	x24, .LC167
	lsl	w25, w23, 10
	ldr	x1, [x0,3616]
	mov	w20, 0
	ldr	x0, [x0,3664]
	add	x22, x22, :lo12:.LC166
	str	x1, [x19,8]
	add	x24, x24, :lo12:.LC167
	str	x0, [x19,16]
.L3422:
	adrp	x0, .LANCHOR0+2546
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2546]
	cmp	w20, w0
	bge	.L3428
	mov	w1, 1
	orr	w0, w20, w25
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
	beq	.L3423
	add	x1, x21, :lo12:.LANCHOR2
	mov	x0, x24
	mov	w2, 4
	mov	w3, 768
	ldr	x1, [x1,3616]
	bl	rknand_print_hex
.L3423:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L3422
.L3428:
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
	.global	dump_map_info
	.type	dump_map_info, %function
dump_map_info:
	sub	sp, sp, #128
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x21, x22, [sp,48]
	stp	x27, x28, [sp,96]
	adrp	x23, .LC168
	mov	w25, 56
	ldrh	w21, [x0,2480]
	adrp	x24, .LANCHOR2
	mov	w26, 4
	add	x23, x23, :lo12:.LC168
.L3430:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2482]
	cmp	w0, w21
	bls	.L3443
	mov	x22, 0
	ldrh	w5, [x2,2472]
	ldrh	w4, [x2,2556]
	mov	w27, w22
	mov	x20, x2
.L3439:
	cmp	w5, w22, uxth
	bls	.L3444
	add	x0, x20, 2504
	mov	w1, w21
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrb	w0, [x0,x22]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x5, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L3431
	add	x3, x24, :lo12:.LANCHOR2
	lsl	w28, w28, 10
	umull	x0, w27, w25
	mul	w2, w27, w4
	ldr	x1, [x3,3592]
	add	w27, w27, 1
	add	x1, x1, x0
	uxth	w27, w27
	sdiv	w2, w2, w26
	str	w28, [x1,4]
	ldr	x1, [x3,3592]
	add	x0, x1, x0
	ldr	x1, [x3,1400]
	str	x1, [x0,8]
	ldr	x1, [x3,1408]
	add	x2, x1, x2, sxtw 2
	str	x2, [x0,16]
.L3431:
	add	x22, x22, 1
	b	.L3439
.L3444:
	cbz	w27, .L3434
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w27
	mov	w2, 1
	mov	x20, 0
	adrp	x22, .LANCHOR2
	mov	x28, 56
	ldr	x0, [x0,3592]
	bl	FlashReadPages
.L3435:
	cmp	w27, w20, uxth
	bls	.L3434
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x3, [x0,3592]
	madd	x3, x20, x28, x3
	add	x20, x20, 1
	ldr	x7, [x3,8]
	ldr	x6, [x3,16]
	ldr	w2, [x3,4]
	ldr	w0, [x7,4]
	str	w0, [sp]
	ubfx	x1, x2, 10, 16
	mov	x0, x23
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w7, [x7]
	ldr	w6, [x6,12]
	bl	printk
	b	.L3435
.L3434:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3430
.L3443:
	adrp	x20, .LANCHOR4
	adrp	x23, .LC112
	add	x20, x20, :lo12:.LANCHOR4
	mov	w22, 0
	adrp	x27, .LANCHOR2
	add	x20, x20, 1704
	add	x23, x23, :lo12:.LC112
.L3438:
	add	x21, x27, :lo12:.LANCHOR2
	adrp	x26, .LANCHOR2
	ldrh	w0, [x21,3864]
	cmp	w22, w0
	bge	.L3441
	sbfiz	x24, x22, 1, 32
	mov	w21, 0
.L3442:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2546]
	cmp	w21, w0
	bge	.L3445
	add	x25, x26, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x25,3720]
	ldrh	w0, [x0,x24]
	orr	w0, w21, w0, lsl 10
	str	w0, [x20,4]
	mov	x0, x20
	add	w21, w21, 1
	bl	FlashReadPages
	sxth	w21, w21
	ldr	x0, [x20,8]
	ldr	x1, [x25,3720]
	ldr	x7, [x20,16]
	ldr	w2, [x0]
	ldrh	w1, [x1,x24]
	str	w2, [sp]
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	mov	x0, x23
	ldr	w4, [x7]
	ldr	w5, [x7,4]
	ldr	w6, [x7,8]
	ldr	w2, [x20]
	ldr	w3, [x20,4]
	ldr	w7, [x7,12]
	bl	printk
	b	.L3442
.L3445:
	add	w22, w22, 1
	sxth	w22, w22
	b	.L3438
.L3441:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x21,3720]
	adrp	x0, .LC169
	mov	w2, 2
	add	x0, x0, :lo12:.LC169
	ldr	w3, [x19,2572]
	bl	rknand_print_hex
	ldr	x1, [x21,3760]
	adrp	x0, .LC170
	ldrh	w3, [x19,2580]
	add	x0, x0, :lo12:.LC170
	mov	w2, 4
	bl	rknand_print_hex
	ldr	x1, [x21,3768]
	adrp	x0, .LC171
	ldrh	w3, [x19,2580]
	add	x0, x0, :lo12:.LC171
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
	.global	flash_boot_enter_slc_mode
	.type	flash_boot_enter_slc_mode, %function
flash_boot_enter_slc_mode:
	adrp	x1, .LANCHOR0+2392
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+2392]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3446
	bl	flash_enter_slc_mode
.L3446:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	2
	.global	flash_boot_exit_slc_mode
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	adrp	x1, .LANCHOR0+2392
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+2392]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3448
	bl	flash_exit_slc_mode
.L3448:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	stp	x29, x30, [sp, -304]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w19, w0
	add	x0, x25, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	mov	x22, x1
	mov	x23, x2
	ldr	x1, [x0,88]
	ldr	w21, [x0,76]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	ldrb	w20, [x1,9]
	bl	ftl_malloc
	mov	x27, x0
	mov	w0, -1
	cbz	x27, .L3493
	add	w19, w19, 511
	lsr	w19, w19, 9
	cmp	w19, 8
	bls	.L3475
	cmp	w19, 500
	bhi	.L3493
	b	.L3452
.L3475:
	mov	w19, 8
.L3452:
	ldr	w2, [x22]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	mov	w0, -1
	cmp	w2, w1
	bne	.L3493
	mul	w21, w20, w21
	mov	w3, 0
	mov	w2, 63871
	mov	w4, 4097
	uxth	w21, w21
	add	w0, w21, w0
	add	w0, w0, w19
	udiv	w0, w0, w21
	str	w0, [x29,216]
	add	x0, x22, 253952
	add	x0, x0, 1532
.L3456:
	ldr	w1, [x0]
	cbnz	w1, .L3453
	ldr	w1, [x22,w3,uxtw 2]
	add	w3, w3, 1
	cmp	w3, w4
	sub	w2, w2, #1
	str	w1, [x0,512]
	csel	w3, w3, wzr, cc
	cmp	w2, 4096
	sub	x0, x0, #4
	bne	.L3456
	b	.L3455
.L3453:
	adrp	x0, .LC172
	add	x0, x0, :lo12:.LC172
	bl	printk
.L3455:
	adrp	x0, .LC173
	mov	w3, 5
	add	x20, x25, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC173
	mov	x1, x23
	mov	w2, 4
	adrp	x24, .LANCHOR4
	bl	rknand_print_hex
	ldr	w1, [x22,512]
	adrp	x0, .LC174
	ldrb	w2, [x20,73]
	add	x0, x0, :lo12:.LC174
	bl	printk
	add	x4, x24, :lo12:.LANCHOR4
	adrp	x0, .LC175
	ldrh	w3, [x20,202]
	mov	w1, w19
	add	x0, x0, :lo12:.LC175
	mov	w2, w19
	ldr	w4, [x4,1636]
	bl	printk
	str	x24, [x29,224]
	ldrb	w0, [x20,73]
	ldr	w1, [x22,512]
	cmp	w1, w0
	bls	.L3457
	str	w0, [x22,512]
.L3457:
	lsl	w0, w19, 7
	str	w0, [x29,204]
	adrp	x0, .LC176
	mov	x20, 0
	add	x0, x0, :lo12:.LC176
	str	x0, [x29,192]
	adrp	x0, .LC177
	str	w20, [x29,236]
	add	x0, x0, :lo12:.LC177
	str	x0, [x29,184]
	adrp	x0, .LC178
	add	x0, x0, :lo12:.LC178
	str	x0, [x29,176]
.L3473:
	add	x0, x25, :lo12:.LANCHOR0
	ldr	w1, [x23,x20,lsl 2]
	str	w20, [x29,232]
	ldrb	w0, [x0,73]
	cmp	w1, w0
	bcs	.L3458
	ldr	x0, [x29,224]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w0, [x0,1636]
	cmp	w1, w0
	bcc	.L3458
	ldr	w0, [x29,216]
	cmp	w0, 1
	cset	w0, hi
	str	w0, [x29,220]
	uxtw	x0, w20
	cbz	w0, .L3459
	ldr	w0, [x29,220]
	cbz	w0, .L3459
	uxtw	x0, w20
	sub	w0, w0, #1
	ldr	w0, [x23,x0,lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	beq	.L3458
.L3459:
	mov	w1, 0
	mov	x2, 512
	mov	x0, x27
	bl	memset
	ldr	w24, [x23,x20,lsl 2]
	add	x0, x25, :lo12:.LANCHOR0
	mov	w2, w19
	mul	w24, w21, w24
	ldr	x1, [x0,88]
	ldr	w26, [x0,76]
	ldr	x0, [x29,192]
	ldrb	w28, [x1,9]
	mov	w1, w24
	bl	printk
	mov	w0, 0
	mul	w26, w26, w28
	bl	flash_boot_enter_slc_mode
	udiv	w1, w24, w28
	mov	w2, 0
	mov	w0, 0
	uxth	w26, w26
	bl	FlashEraseBlock
	cmp	w19, w26
	mov	w2, 1
	bls	.L3460
	mov	w2, 0
	mov	w0, 0
	add	w1, w24, w26
	bl	FlashEraseBlock
	mov	w2, 2
.L3460:
	mov	w0, 0
	str	x2, [x29,208]
	bl	flash_boot_exit_slc_mode
	add	x4, x25, :lo12:.LANCHOR0
	ldr	x2, [x29,208]
	udiv	w8, w24, w26
	mov	w6, 0
	mov	w10, 1073741823
	mov	w11, 61424
	ldr	x1, [x4,88]
	ldrh	w0, [x1,10]
	ldrb	w5, [x1,12]
	lsl	w0, w0, 2
	msub	w8, w8, w26, w24
	mul	w0, w0, w2
	mov	x26, x22
	sdiv	w5, w0, w5
	sub	w0, w24, w8
	str	w0, [x29,208]
.L3461:
	cmp	w6, w5
	bcs	.L3505
	add	w0, w6, w8
	lsr	w0, w0, 2
	uxth	w1, w0
	and	w0, w0, 65535
	cbz	w1, .L3462
	add	w2, w0, 1
	add	x1, x4, 208
	ldrb	w3, [x4,204]
	ldrh	w1, [x1,w2,sxtw 1]
	cbz	w3, .L3463
	ldr	w7, [x4,2392]
	mov	w3, 12336
	movk	w3, 0x5638, lsl 16
	cmp	w7, w3
	csel	w1, w1, w2, ne
.L3463:
	add	w1, w1, w10
	lsl	w1, w1, 2
	str	w1, [x29,240]
.L3462:
	add	x1, x4, 208
	str	w11, [x29,244]
	ldrh	w7, [x1,w0,sxtw 1]
	ldrb	w1, [x4,204]
	cbz	w1, .L3464
	ldr	w2, [x4,2392]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w7, w7, w0, ne
.L3464:
	ldr	w0, [x29,208]
	ldrb	w12, [x4,2464]
	str	x11, [x29,104]
	madd	w2, w7, w28, w0
	ldr	x0, [x29,224]
	str	x10, [x29,112]
	add	x0, x0, :lo12:.LANCHOR4
	str	x6, [x29,120]
	str	x8, [x29,128]
	str	x5, [x29,136]
	ldrb	w0, [x0,1642]
	str	x7, [x29,144]
	str	x12, [x29,160]
	str	x2, [x29,152]
	str	x4, [x29,168]
	bl	FlashBchSel
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	x4, [x29,168]
	add	x3, x29, 240
	ldr	x2, [x29,152]
	ldr	x0, [x4,88]
	ldrb	w1, [x0,9]
	mov	w0, 0
	udiv	w1, w2, w1
	mov	x2, x26
	add	x26, x26, 2048
	bl	FlashProgPage
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	ldr	x12, [x29,160]
	mov	w0, w12
	bl	FlashBchSel
	ldr	w1, [x29,208]
	mov	w0, 0
	ldr	x7, [x29,144]
	udiv	w1, w1, w28
	add	w2, w7, 1
	bl	FlashPageProgMsbFFData
	ldr	x6, [x29,120]
	ldr	x11, [x29,104]
	add	w6, w6, 4
	ldr	x10, [x29,112]
	ldr	x8, [x29,128]
	uxth	w6, w6
	ldr	x5, [x29,136]
	ldr	x4, [x29,168]
	b	.L3461
.L3505:
	ldr	x0, [x29,184]
	mov	w1, w24
	mov	w2, w19
	mov	w3, 0
	add	x24, x25, :lo12:.LANCHOR0
	bl	printk
	ldr	w5, [x23,x20,lsl 2]
	mov	w2, w19
	ldr	x0, [x24,88]
	ldr	w26, [x24,76]
	mul	w5, w21, w5
	ldrb	w7, [x0,9]
	ldr	x0, [x29,176]
	mov	w1, w5
	str	x7, [x29,168]
	mul	w26, w26, w7
	str	x5, [x29,208]
	bl	printk
	ldr	x5, [x29,208]
	mov	w12, 0
	uxth	w26, w26
	ldr	x7, [x29,168]
	mov	w11, 4
	udiv	w8, w5, w26
	msub	w8, w8, w26, w5
	mov	x26, x27
	sub	w10, w5, w8
	mul	w0, w8, w7
	ubfx	x0, x0, 2, 2
.L3466:
	cmp	w12, w19
	bcs	.L3506
	add	w4, w12, w8
	add	x1, x24, 208
	ubfx	x4, x4, 2, 16
	sub	w6, w11, w0
	uxth	w6, w6
	ldrh	w3, [x1,w4,sxtw 1]
	ldrb	w1, [x24,204]
	cbz	w1, .L3467
	ldr	w2, [x24,2392]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w3, w3, w4, ne
.L3467:
	add	w0, w0, w10
	ldrb	w4, [x24,2464]
	str	x11, [x29,112]
	madd	w28, w3, w7, w0
	ldr	x0, [x24,88]
	str	x12, [x29,120]
	str	x6, [x29,128]
	ldrb	w1, [x0,9]
	ldr	x0, [x29,224]
	str	x8, [x29,136]
	add	x0, x0, :lo12:.LANCHOR4
	str	x5, [x29,144]
	str	x10, [x29,152]
	str	x7, [x29,160]
	ldrb	w0, [x0,1642]
	str	x4, [x29,208]
	str	x1, [x29,168]
	bl	FlashBchSel
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	x1, [x29,168]
	mov	x2, x26
	mov	x3, 0
	mov	w0, 0
	udiv	w1, w28, w1
	bl	FlashReadPage
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	ldr	x4, [x29,208]
	mov	w0, w4
	bl	FlashBchSel
	ldr	x6, [x29,128]
	ldr	x12, [x29,120]
	ubfiz	x0, x6, 9, 16
	ldr	x11, [x29,112]
	add	x26, x26, x0
	add	w0, w6, w12
	ldr	x8, [x29,136]
	uxth	w12, w0
	ldr	x5, [x29,144]
	mov	w0, 0
	ldr	x10, [x29,152]
	ldr	x7, [x29,160]
	b	.L3466
.L3506:
	adrp	x0, .LC179
	mov	w1, w5
	mov	w2, w19
	add	x0, x0, :lo12:.LC179
	mov	w3, 0
	bl	printk
	mov	x2, x27
	mov	x1, x22
	mov	x0, 0
.L3469:
	ldr	w4, [x29,204]
	mov	w3, w0
	cmp	w0, w4
	bcs	.L3507
	mov	x4, x2
	mov	x26, x1
	mov	x24, x0
	add	x2, x2, 4
	add	x0, x0, 1
	add	x1, x1, 4
	ldr	w6, [x4]
	ldr	w5, [x26]
	cmp	w6, w5
	beq	.L3469
	mov	w1, 0
	mov	x2, 512
	mov	x0, x27
	str	x4, [x29,168]
	str	x3, [x29,208]
	and	x24, x24, -256
	bl	memset
	lsl	x24, x24, 2
	ldr	x4, [x29,168]
	adrp	x0, .LC180
	ldr	w5, [x26]
	add	x0, x0, :lo12:.LC180
	ldr	w1, [x29,232]
	ldr	w4, [x4]
	ldr	w2, [x23,x20,lsl 2]
	ldr	x3, [x29,208]
	bl	printk
	adrp	x0, .LC181
	add	x1, x22, x24
	add	x0, x0, :lo12:.LC181
	mov	w2, 4
	mov	w3, 256
	bl	rknand_print_hex
	adrp	x0, .LC182
	add	x1, x27, x24
	mov	w2, 4
	mov	w3, 256
	add	x0, x0, :lo12:.LC182
	bl	rknand_print_hex
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	w1, [x23,x20,lsl 2]
	mov	w0, 0
	mov	w2, 0
	mul	w1, w21, w1
	bl	FlashEraseBlock
	ldr	w0, [x29,220]
	cbz	w0, .L3470
	ldr	w1, [x23,x20,lsl 2]
	mov	w0, 0
	mov	w2, 0
	madd	w1, w1, w21, w21
	bl	FlashEraseBlock
.L3470:
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	adrp	x0, .LC183
	ldr	w1, [x23,x20,lsl 2]
	add	x0, x0, :lo12:.LC183
	bl	printk
	b	.L3458
.L3507:
	ldr	w0, [x29,236]
	add	w0, w0, 1
	str	w0, [x29,236]
.L3458:
	add	x20, x20, 1
	cmp	x20, 5
	bne	.L3473
	mov	x0, x27
	bl	ftl_free
	ldr	w0, [x29,236]
	cmp	w0, wzr
	csetm	w0, eq
.L3493:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 304
	ret
	.size	write_idblock, .-write_idblock
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	stp	x29, x30, [sp, -112]!
	cmp	w0, 64
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w19, w0
	mov	w21, w1
	mov	x24, x2
	adrp	x20, .LANCHOR5
	bne	.L3509
	ldr	w1, [x2]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L3509
	add	x22, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x22,1512]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	str	x0, [x22,1520]
	mov	w2, 59392
	mov	w1, 0
	movk	w2, 0x3, lsl 16
	bl	ftl_memset
	str	w19, [x22,1528]
.L3509:
	add	x23, x20, :lo12:.LANCHOR5
	adrp	x0, .LC184
	ldr	w2, [x24]
	add	x0, x0, :lo12:.LC184
	mov	w3, w19
	mov	w4, w21
	ldr	x1, [x23,1520]
	bl	printk
	ldrb	w0, [x23,1512]
	cbz	w0, .L3508
	sub	w0, w19, #64
	ldr	x22, [x23,1520]
	cmp	w0, 499
	bhi	.L3511
	mov	w3, 564
	ubfiz	x0, x0, 9, 25
	sub	w3, w3, w19
	add	x0, x22, x0
	cmp	w3, w21
	mov	x1, x24
	csel	w2, w3, w21, ls
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L3512
.L3511:
	cmp	w19, 563
	bhi	.L3522
.L3512:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,1528]
	cmp	w1, w19
	beq	.L3520
	strb	wzr, [x0,1512]
	cbz	x22, .L3521
	mov	x0, x22
	bl	ftl_free
.L3521:
	add	x0, x20, :lo12:.LANCHOR5
	str	xzr, [x0,1520]
.L3520:
	add	x20, x20, :lo12:.LANCHOR5
	add	w19, w19, w21
	str	w19, [x20,1528]
	b	.L3508
.L3522:
	ldr	w2, [x23,1528]
	mov	w0, 500
	sub	w2, w2, #64
	cmp	w2, 500
	csel	w2, w2, w0, ls
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrb	w0, [x0,9]
	cmp	w0, 4
	beq	.L3523
	mov	w0, 2
	str	w0, [x29,72]
	mov	w0, 3
	str	w0, [x29,76]
	mov	w0, 4
	str	w0, [x29,80]
	mov	w0, 5
	str	w0, [x29,84]
	mov	w0, 6
	str	w0, [x29,88]
	b	.L3514
.L3523:
	mov	x3, 0
.L3513:
	cmp	w2, 256
	add	x0, x29, 72
	bls	.L3515
	lsl	w1, w3, 1
	str	w1, [x0,x3,lsl 2]
	b	.L3516
.L3515:
	str	w3, [x0,x3,lsl 2]
.L3516:
	add	x3, x3, 1
	cmp	x3, 5
	bne	.L3513
.L3514:
	add	x0, x22, 253952
	mov	w3, 63872
	add	x0, x0, 1536
.L3519:
	ldr	w1, [x0]
	cbz	w1, .L3517
	add	w3, w3, 128
	lsl	w0, w3, 2
	b	.L3518
.L3517:
	sub	w3, w3, #1
	sub	x0, x0, #4
	cmp	w3, 4096
	bne	.L3519
	lsl	w0, w2, 9
.L3518:
	mov	x1, x22
	add	x2, x29, 72
	add	x22, x20, :lo12:.LANCHOR5
	bl	write_idblock
	ldr	x0, [x22,1520]
	strb	wzr, [x22,1512]
	bl	ftl_free
	str	xzr, [x22,1520]
	b	.L3520
.L3508:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 112
	ret
	.size	write_loader_lba, .-write_loader_lba
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	str	x21, [sp,32]
	uxtb	w21, w0
	sub	w0, w1, #64
	stp	x19, x20, [sp,16]
	cmp	w0, 1983
	mov	w19, w1
	mov	w20, w2
	bhi	.L3533
	cbnz	w21, .L3533
	mov	w0, w1
	mov	w1, w2
	mov	x2, x3
	str	x3, [x29,56]
	bl	write_loader_lba
	ldr	x3, [x29,56]
.L3533:
	mov	w1, w19
	mov	w2, w20
	mov	w0, w21
	bl	ftl_write
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	sub	sp, sp, #528
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, w1
	mov	x20, x2
	beq	.L3542
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3543
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3544
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3545
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3546
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3547
	b	.L3578
.L3545:
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3548
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3549
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3578
	b	.L3600
.L3543:
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3551
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3552
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3542
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3542
	b	.L3578
.L3552:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3551
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3553
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3553
	b	.L3578
.L3547:
	adrp	x0, .LC185
	add	x0, x0, :lo12:.LC185
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbz	x0, .L3554
.L3560:
	adrp	x0, .LC186
	add	x0, x0, :lo12:.LC186
	bl	printk
	b	.L3597
.L3554:
	ldr	w1, [x29,40]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3555
.L3557:
	mov	x19, -1
	b	.L3556
.L3555:
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L3557
	adrp	x19, .LANCHOR5
	add	x0, x29, 40
	add	x19, x19, :lo12:.LANCHOR5
	mov	x2, 512
	ldr	x1, [x19,1536]
	bl	memcpy
	ldr	w1, [x19,1544]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3558
	add	x0, x29, 104
	mov	w1, 0
	mov	x2, 128
	str	wzr, [x29,48]
	str	wzr, [x29,52]
	bl	memset
.L3558:
	add	x0, x29, 296
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,56]
	bl	memset
.L3592:
	mov	x0, x20
	add	x1, x29, 40
	mov	x2, 520
	bl	rk_copy_to_user
	cbnz	x0, .L3597
	b	.L3595
.L3544:
	adrp	x0, .LC187
	add	x0, x0, :lo12:.LC187
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3560
	ldr	w1, [x29,40]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3557
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L3557
	adrp	x1, .LANCHOR5
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR5
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,1544]
	cmp	w3, w2
	bne	.L3541
	ldr	w2, [x29,52]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L3541
	ldr	x19, [x1,1536]
	add	x1, x29, 104
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,52]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3596
.L3549:
	adrp	x0, .LC188
	add	x0, x0, :lo12:.LC188
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3560
	ldr	w1, [x29,40]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3557
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L3557
	adrp	x19, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR5
	ldr	w0, [x0,1548]
	cbnz	w0, .L3561
.L3564:
	mov	x0, 0
	b	.L3541
.L3561:
	add	x0, x19, :lo12:.LANCHOR5
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	x1, [x0,1552]
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3562
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,1552]
	str	w2, [x1,4]
	ldr	x0, [x0,1552]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3562:
	add	x20, x19, :lo12:.LANCHOR5
	mov	w0, 0
	ldr	x1, [x20,1552]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,1536]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3563
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1536]
	str	w1, [x0,4]
	ldr	x0, [x20,1536]
	str	wzr, [x0,8]
.L3563:
	add	x19, x19, :lo12:.LANCHOR5
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,1536]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,1548]
	str	wzr, [x19,1544]
	b	.L3595
.L3548:
	adrp	x0, .LC189
	add	x0, x0, :lo12:.LC189
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3560
	ldr	w1, [x29,40]
	mov	w0, 20037
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3557
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L3557
	adrp	x19, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR5
	ldr	w1, [x0,1548]
	cmp	w1, 1
	beq	.L3564
	ldr	x0, [x0,1552]
	mov	w1, 22867
	movk	w1, 0x4453, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3565
	mov	w1, 22867
	mov	w2, 504
	movk	w1, 0x4453, lsl 16
	str	w1, [x0]
	add	x0, x19, :lo12:.LANCHOR5
	ldr	x1, [x0,1552]
	str	w2, [x1,4]
	ldr	x0, [x0,1552]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3565:
	add	x20, x19, :lo12:.LANCHOR5
	mov	w0, 1
	ldr	x1, [x20,1552]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,1536]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3566
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1536]
	str	w1, [x0,4]
	ldr	x0, [x20,1536]
	str	wzr, [x0,8]
.L3566:
	add	x19, x19, :lo12:.LANCHOR5
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,1536]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,1548]
	b	.L3595
.L3600:
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3560
	ldr	w1, [x29,40]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3557
	ldr	w2, [x29,44]
	cmp	w2, 512
	bhi	.L3557
	adrp	x1, .LANCHOR5
	add	x0, x29, 48
	add	x1, x1, :lo12:.LANCHOR5
	uxtw	x2, w2
	add	x1, x1, 1560
	b	.L3598
.L3542:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3568
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
	b	.L3594
.L3568:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3570
	adrp	x0, .LC192
	add	x0, x0, :lo12:.LC192
	b	.L3594
.L3570:
	adrp	x0, .LC193
	add	x0, x0, :lo12:.LC193
.L3594:
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3560
	ldr	w1, [x29,40]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3597
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR5
	bne	.L3571
	add	x0, x0, :lo12:.LANCHOR5
	add	x1, x29, 40
	mov	x2, 16
	ldr	x0, [x0,1536]
	ldr	w0, [x0,20]
	str	w0, [x29,44]
	strb	w0, [x29,48]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3541
	b	.L3597
.L3571:
	add	x20, x0, :lo12:.LANCHOR5
	ldr	w1, [x20,2072]
	cmp	w1, 10
	bhi	.L3597
	ldr	x1, [x20,1536]
	ldr	w2, [x29,44]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3572
	cbz	w3, .L3572
	adrp	x0, .LC194
	mov	w1, w2
	add	x0, x0, :lo12:.LC194
	bl	printk
	ldr	w0, [x20,2072]
	add	w0, w0, 1
	str	w0, [x20,2072]
.L3597:
	mov	x0, -14
	b	.L3541
.L3572:
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,2072]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3573
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3574
.L3573:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3574:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3595
	b	.L3556
.L3551:
	adrp	x0, .LC195
	add	x0, x0, :lo12:.LC195
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3560
	ldr	w1, [x29,40]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3557
	ldr	w2, [x29,44]
	cmp	w2, 504
	bhi	.L3557
	mov	w0, 30224
	adrp	x1, .LANCHOR5
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR5
	add	x0, x29, 48
	bne	.L3575
	ldr	x1, [x1,2080]
	b	.L3599
.L3575:
	ldr	x1, [x1,2088]
.L3599:
	add	x1, x1, 8
.L3598:
	bl	memcpy
	b	.L3592
.L3553:
	adrp	x0, .LC196
	add	x0, x0, :lo12:.LC196
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3560
	ldr	w1, [x29,40]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3557
	ldr	w2, [x29,44]
	cmp	w2, 504
	bhi	.L3557
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	bne	.L3577
	ldr	x0, [x19,2080]
	add	x1, x29, 40
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,2080]
	b	.L3596
.L3577:
	ldr	x0, [x19,2088]
	add	x1, x29, 40
	bl	memcpy
	ldr	x1, [x19,2088]
	mov	w0, 3
.L3596:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3556
.L3546:
	bl	rknand_dev_flush
.L3595:
	mov	x19, 0
.L3556:
	adrp	x0, .LC197
	mov	x1, x19
	add	x0, x0, :lo12:.LC197
	bl	printk
	mov	x0, x19
	b	.L3541
.L3578:
	mov	x0, -22
.L3541:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	add	sp, sp, 528
	ret
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	stp	x29, x30, [sp, -48]!
	mov	w0, -1
	mov	w2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR5
	add	x19, x20, :lo12:.LANCHOR5
	stp	x21, x22, [sp,32]
	ldr	x1, [x19,1488]
	str	w0, [x19,1528]
	add	x0, x1, 512
	str	x0, [x19,1536]
	add	x0, x1, 1024
	str	x1, [x19,1552]
	str	x0, [x19,2080]
	add	x1, x1, 1536
	add	x0, x19, 1560
	strb	wzr, [x19,1512]
	str	xzr, [x19,1520]
	str	xzr, [x19,2096]
	str	x1, [x19,2088]
	bl	ftl_memcpy
	ldr	x21, [x19,1552]
	str	wzr, [x19,1544]
	str	wzr, [x19,2072]
	ldr	w0, [x21,16]
	ldr	w22, [x21,508]
	str	w0, [x19,1548]
	cbz	w22, .L3602
	mov	x0, x21
	mov	w1, 508
	bl	js_hash
	cmp	w22, w0
	beq	.L3602
	adrp	x0, .LC198
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC198
	str	wzr, [x19,1548]
	bl	printk
.L3602:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,1548]
	cbz	w1, .L3603
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,1544]
.L3603:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 2
	ldr	x1, [x20,2080]
	bl	StorageSysDataLoad
	ldr	x1, [x20,2088]
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
	mov	w0, 65536
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR5
	bl	ftl_malloc
	add	x1, x21, :lo12:.LANCHOR5
	mov	w19, -12
	str	x0, [x1,2104]
	cbz	x0, .L3612
	mov	w23, 0
	adrp	x24, .LC199
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC199
.L3616:
	add	x20, x21, :lo12:.LANCHOR5
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,2104]
	bl	FlashBootVendorRead
	cbnz	w0, .L3613
	ldr	x3, [x20,2104]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,2104]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3614
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3614
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L3614:
	cbnz	w22, .L3624
	mov	w22, 1
	b	.L3616
.L3624:
	cbz	w19, .L3617
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L3612
	b	.L3613
.L3617:
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
	b	.L3612
.L3613:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w19, -1
	ldr	x0, [x21,2104]
	bl	kfree
	str	xzr, [x21,2104]
.L3612:
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
	adrp	x3, .LANCHOR5+2104
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR5+2104]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3626
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3627:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3631
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3627
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
	b	.L3626
.L3631:
	mov	w3, -1
.L3626:
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
	adrp	x0, .LANCHOR5+2104
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR5+2104]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3633
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3634:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3649
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3634
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3635
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3633
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3636:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3650
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
	b	.L3636
.L3650:
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
	b	.L3648
.L3635:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3648
.L3649:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3633
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
.L3648:
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
.L3633:
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
	stp	x29, x30, [sp, -64]!
	mov	w0, 4096
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x20, -1
	str	x1, [x29,56]
	mov	x21, x2
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3652
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3654
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3655
	b	.L3666
.L3654:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3666
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3657
.L3658:
	mov	x20, -1
	b	.L3653
.L3657:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3658
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3653
.L3655:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3666
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3658
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3658
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3666
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3653
.L3666:
	mov	x20, -14
.L3653:
	mov	x0, x19
	bl	kfree
.L3652:
	mov	x0, x20
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
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
.LANCHOR3 = . + 0
	.type	__func__.20995, %object
	.size	__func__.20995, 11
__func__.20995:
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
	.type	__func__.21774, %object
	.size	__func__.21774, 17
__func__.21774:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.21793, %object
	.size	__func__.21793, 16
__func__.21793:
	.string	"FtlScanAllBlock"
	.type	__func__.22061, %object
	.size	__func__.22061, 17
__func__.22061:
	.string	"ftl_scan_all_ppa"
	.zero	7
	.type	__func__.21742, %object
	.size	__func__.21742, 12
__func__.21742:
	.string	"FtlCheckVpc"
	.zero	4
	.type	__func__.22041, %object
	.size	__func__.22041, 21
__func__.22041:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.21068, %object
	.size	__func__.21068, 8
__func__.21068:
	.string	"FtlInit"
	.section	.rodata.str1.1,"aMS",%progbits,1
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
	.string	"%s\n"
.LC76:
	.string	"FTL version: 5.0.63 20200923"
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
.LANCHOR0 = . + 0
.LANCHOR2 = . + 4352
.LANCHOR4 = . + 8704
.LANCHOR5 = . + 17408
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
	.type	g_all_blk_used_slc_mode, %object
	.size	g_all_blk_used_slc_mode, 4
g_all_blk_used_slc_mode:
	.zero	4
	.zero	4
	.type	g_sys_ext_data, %object
	.size	g_sys_ext_data, 512
g_sys_ext_data:
	.zero	512
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
	.zero	6
	.type	check_valid_page_count_table, %object
	.size	check_valid_page_count_table, 8192
check_valid_page_count_table:
	.zero	8192
	.type	g_power_lost_ecc_error_blk, %object
	.size	g_power_lost_ecc_error_blk, 2
g_power_lost_ecc_error_blk:
	.zero	2
	.type	g_power_lost_recovery_flag, %object
	.size	g_power_lost_recovery_flag, 2
g_power_lost_recovery_flag:
	.zero	2
	.type	g_recovery_page_num, %object
	.size	g_recovery_page_num, 4
g_recovery_page_num:
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
