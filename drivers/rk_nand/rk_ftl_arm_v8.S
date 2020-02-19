/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-02-18
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
	add	x4, x3, 8
	stp	x29, x30, [sp, -16]!
	add	x5, x4, x2
	add	x29, sp, 0
	ldr	x6, [x4,x2]
	lsr	w4, w1, 8
	ldrb	w2, [x3,104]
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
	cbz	w2, .L41
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
.L41:
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
	ldrb	w6, [x2,99]
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
	ldrb	w1, [x1,98]
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
.L50:
	cmp	w1, w2
	bls	.L52
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L50
.L52:
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
.L56:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,121]
	cmp	w0, w20
	bls	.L62
	add	x0, x19, 256
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	x0, 1000
	bl	__const_udelay
	ldrb	w0, [x25,120]
	cmp	w0, 34
	bne	.L57
	ldrsb	w0, [x24,x20]
	b	.L61
.L57:
	cmp	w0, 35
	bne	.L59
	ldrsb	w0, [x21,x20]
	b	.L61
.L59:
	ldrsb	w0, [x23,400]
.L61:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L56
.L62:
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
.L64:
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,121]
	cmp	w0, w20
	bls	.L66
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
	b	.L64
.L66:
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
	ldr	x1, [x0,128]
	ldr	w2, [x1]
	str	w2, [x0,136]
	ldr	w2, [x1,4]
	str	w2, [x0,140]
	ldr	w2, [x1,8]
	str	w2, [x0,144]
	ldr	w2, [x1,12]
	str	w2, [x0,148]
	ldr	w2, [x1,304]
	str	w2, [x0,152]
	ldr	w2, [x1,308]
	str	w2, [x0,156]
	ldr	w2, [x1,336]
	ldr	w1, [x1,344]
	str	w2, [x0,160]
	str	w1, [x0,164]
	ret
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	adrp	x7, .LANCHOR0
	uxtb	w4, w4
	add	x6, x7, :lo12:.LANCHOR0
	ldrh	w8, [x6,182]
	ldrh	w5, [x6,180]
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
	bne	.L70
	add	x1, x7, :lo12:.LANCHOR0
	ldrb	w5, [x1,196]
	cbnz	w5, .L70
	add	x1, x1, 200
	ldrh	w8, [x1,w8,sxtw 1]
.L70:
	add	x7, x7, :lo12:.LANCHOR0
	cmp	w4, 1
	add	x7, x7, 1224
	mov	w1, 0
	ldr	w5, [x7,w10,uxtw 2]
	madd	w5, w6, w9, w5
	add	w5, w5, w8
	str	w5, [x2]
	str	w10, [x3]
	bls	.L71
	ldr	w1, [x0,4]
	ldr	w0, [x0,60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w1, eq
.L71:
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
	ldr	x0, [x4,1256]
	ldrb	w19, [x6,8]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	bne	.L74
	add	x4, x4, 88
	cbnz	w2, .L75
	ldrb	w2, [x4,13]
	b	.L83
.L75:
	ldrb	w2, [x4,14]
.L83:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,103]
	cbz	w4, .L78
	add	x3, x19, 8
	lsl	x3, x3, 8
.L82:
	cmp	w2, w4
	bcs	.L78
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L82
.L74:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L78:
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
.L88:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L88
	tbz	x3, 6, .L88
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
	adrp	x0, .LANCHOR0+1256
	ldr	x0, [x0,#:lo12:.LANCHOR0+1256]
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
	adrp	x1, .LANCHOR0+128
	uxtb	w0, w0
	lsl	w2, w0, 8
	orr	w0, w2, w0, lsl 16
	ldr	x1, [x1,#:lo12:.LANCHOR0+128]
	orr	w0, w0, 1
	str	w0, [x1,304]
	ret
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrDiv
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	adrp	x1, .LANCHOR0+128
	uxtb	w0, w0
	ldr	x2, [x1,#:lo12:.LANCHOR0+128]
	mov	w1, 16640
	orr	w0, w0, w1
	str	w0, [x2,344]
	ret
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	2
	.global	NandcSetDdrMode
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	adrp	x1, .LANCHOR0+128
	cmp	w0, wzr
	ldr	x2, [x1,#:lo12:.LANCHOR0+128]
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
	adrp	x1, .LANCHOR0+128
	uxtb	w0, w0
	tst	w0, 6
	ldr	x2, [x1,#:lo12:.LANCHOR0+128]
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
	ldr	x0, [x19,1256]
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
	ldr	x0, [x19,1256]
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
	add	x20, x20, 1224
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
	mov	x0, 750
	bl	__const_udelay
.L139:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L140
	mov	x0, 50
	bl	__const_udelay
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
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	uxtb	w20, w0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,196]
	cbz	w0, .L143
	mov	w0, w20
	add	x19, x19, 8
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
.L143:
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
	ldrb	w0, [x19,196]
	cbz	w0, .L148
	mov	w0, w20
	add	x19, x19, 8
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
.L148:
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
	adrp	x1, .LANCHOR0
	mov	x6, 0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w8, 239
	stp	x29, x30, [sp, -16]!
	add	x7, x1, 8
	add	x4, x1, 2132
	add	x29, sp, 0
	ldrb	w2, [x1,2356]
	mov	w9, 128
	mov	w10, 1
	mov	w12, 35
	and	w11, w2, 1
	and	w2, w2, 4
	mov	w13, 32
	mov	w14, 5
	uxtb	w5, w2
.L165:
	ldrb	w3, [x6,x4]
	ldr	x1, [x7]
	cmp	w3, 152
	ldrb	w2, [x7,8]
	beq	.L155
	cmp	w3, 69
	beq	.L155
	cmp	w3, 173
	beq	.L155
	cmp	w3, 44
	bne	.L156
.L155:
	cmp	w0, 1
	bne	.L158
	cbz	w11, .L156
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L159
	str	w0, [x1,2052]
	b	.L176
.L159:
	cmp	w3, 44
	bne	.L161
	str	w0, [x1,2052]
	str	w14, [x1,2048]
	b	.L163
.L161:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L163
.L158:
	cbz	w5, .L156
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L162
	str	w10, [x1,2052]
	str	w13, [x1,2048]
	b	.L163
.L162:
	cmp	w3, 44
	bne	.L164
	str	w10, [x1,2052]
	str	w12, [x1,2048]
	b	.L163
.L164:
	str	w9, [x1,2052]
.L176:
	str	wzr, [x1,2048]
.L163:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L156:
	add	x6, x6, 8
	add	x7, x7, 16
	cmp	x6, 32
	bne	.L165
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
.L180:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,121]
	cmp	w4, w2
	bls	.L185
	ldrb	w3, [x3,120]
	cmp	w3, 67
	bne	.L181
	ldrsb	w3, [x0,x2]
	b	.L184
.L181:
	ldrsb	w3, [x1,x2]
.L184:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L180
.L185:
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
.L188:
	cmp	w22, w24
	beq	.L197
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,2357]
	cmp	w2, w0
	bcc	.L189
	mov	w0, -1
	str	w0, [x20]
	b	.L190
.L189:
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
	tbz	x0, 0, .L191
	mov	w0, -1
	str	w0, [x20]
.L191:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L192
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L192:
	mov	w0, w19
	bl	NandcFlashDeCs
.L190:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L188
.L197:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 96
	ret
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
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
	add	x4, x3, 8
	sbfiz	x0, x21, 4, 32
	add	x3, x3, 88
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
	bne	.L199
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
	b	.L200
.L199:
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
.L200:
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
	ldr	w0, [x0,2368]
	cbz	w0, .L202
	mov	w0, 0
	bl	flash_enter_slc_mode
	b	.L203
.L202:
	bl	flash_exit_slc_mode
.L203:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,2372]
	cbz	w0, .L204
	ldrb	w0, [x20,2356]
	tbz	x0, 0, .L204
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,2372]
.L204:
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
	ble	.L213
	ldr	x0, [x1,128]
	mov	w1, 8354
	b	.L220
.L213:
	cmp	w0, 220
	ble	.L215
	ldr	x0, [x1,128]
	b	.L221
.L215:
	cmp	w0, 185
	ble	.L216
	ldr	x0, [x1,128]
	mov	w1, 4226
	b	.L220
.L216:
	cmp	w0, 160
	ldr	x0, [x1,128]
	ble	.L217
	mov	w1, 4194
	b	.L220
.L217:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L220
	cmp	w19, 99
	mov	w1, 4225
	bls	.L220
.L221:
	mov	w1, 8322
.L220:
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
	bls	.L223
	mov	w1, 8322
	cmp	w0, w1
	bne	.L224
.L223:
	adrp	x1, .LANCHOR0+128
	ldr	x1, [x1,#:lo12:.LANCHOR0+128]
	str	w0, [x1,4]
.L224:
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
	str	x0, [x1,128]
	ldr	w2, [x0]
	ubfx	x3, x2, 13, 1
	str	w3, [x1,2376]
	ldr	w3, [x0,352]
	and	w2, w2, 245760
	orr	w2, w2, 256
	ubfx	x3, x3, 16, 4
	str	w3, [x1,2380]
	ldr	w3, [x0,352]
	str	w3, [x1,2384]
	cmp	w3, 2049
	bne	.L226
	mov	w3, 8
	str	w3, [x1,2380]
.L226:
	add	x19, x19, :lo12:.LANCHOR0
	str	w2, [x0]
	ldr	x0, [x19,128]
	str	wzr, [x0,336]
	mov	w0, 40
	bl	NandcTimeCfg
	ldr	x0, [x19,128]
	mov	w1, 8322
	str	w1, [x0,344]
	mov	w1, 6145
	movk	w1, 0x18, lsl 16
	str	w1, [x0,304]
	mov	w0, 36864
	bl	ftl_malloc
	str	wzr, [x19,2440]
	str	x0, [x19,2392]
	str	x0, [x19,2400]
	add	x0, x0, 32768
	str	wzr, [x19,2448]
	str	x0, [x19,2408]
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
	ldr	x5, [x4,128]
	ldr	w5, [x5,4]
	str	w5, [x0]
	ldr	x0, [x4,128]
	ldr	w0, [x0]
	str	w0, [x1]
	ldr	x0, [x4,128]
	ldr	w0, [x0,304]
	str	w0, [x2]
	ldr	x1, [x4,128]
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
	ldr	x2, [x1,128]
	str	w0, [x1,2452]
	mov	w1, 4096
	str	w3, [x2,8]
	bne	.L229
.L232:
	and	w1, w1, -17
	b	.L230
.L229:
	cmp	w0, 24
	bne	.L231
	orr	w1, w1, 16
	b	.L230
.L231:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L232
.L230:
	orr	w1, w1, 1
	str	w1, [x2,12]
	ret
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0+2456
	uxtb	w0, w0
	add	x29, sp, 0
	strb	w0, [x1,#:lo12:.LANCHOR0+2456]
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
	ldr	x1, [x0,128]
	ldr	w2, [x0,136]
	str	w2, [x1]
	ldr	w2, [x0,140]
	ldr	x1, [x0,128]
	str	w2, [x1,4]
	ldr	w2, [x0,144]
	ldr	x1, [x0,128]
	str	w2, [x1,8]
	ldr	w2, [x0,148]
	str	w2, [x1,12]
	ldr	w2, [x0,152]
	str	w2, [x1,304]
	ldr	w2, [x0,156]
	str	w2, [x1,308]
	ldr	w2, [x0,160]
	str	w2, [x1,336]
	ldr	w2, [x0,164]
	str	w2, [x1,344]
.L239:
	lsl	x0, x21, 3
	ldrb	w0, [x0,x19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L238
	mov	w0, w21
	bl	FlashReset
.L238:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L239
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,2372]
	cbz	w0, .L240
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,2356]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,2356]
	bl	NandcSetMode
	ldr	w0, [x19,152]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L240:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,1256]
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
	tbnz	x0, 9, .L248
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L247
.L248:
	ldr	w0, [x19,368]
	orr	w0, w0, 2
	str	w0, [x19,368]
	ldr	w0, [x19,364]
	and	w0, w0, -3
	str	w0, [x19,364]
.L247:
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
	cbz	w2, .L251
	mov	w1, w23
	bl	FlashEraseSLc2KBlocks
	b	.L252
.L251:
	mov	w28, 56
	add	x25, x19, 2164
	mov	x26, 24
.L282:
	cmp	w20, w23
	bcs	.L283
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
	ldrb	w1, [x19,2357]
	ldr	w0, [x29,124]
	ldr	x5, [x29,96]
	cmp	w0, w1
	bcc	.L253
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L254
.L253:
	ldrb	w1, [x19,2457]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w22, w22, wzr, ne
	madd	x1, x1, x26, x25
	ldr	x1, [x1,8]
	cbz	x1, .L256
	bl	FlashWaitCmdDone
.L256:
	ldr	w0, [x29,124]
	ldr	w1, [x29,120]
	madd	x2, x0, x26, x25
	str	x21, [x2,8]
	str	xzr, [x2,16]
	str	w1, [x2,4]
	cbz	w22, .L257
	add	w1, w20, 1
	umull	x1, w1, w28
	add	x1, x24, x1
	str	x1, [x2,16]
.L257:
	add	x1, x19, x0
	mul	x0, x0, x26
	ldrb	w21, [x1,2360]
	strb	w21, [x25,x0]
	mov	w0, w21
	bl	NandcFlashCs
	cmp	w27, 1
	bne	.L258
	ldrb	w0, [x19,196]
	cbz	w0, .L258
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L259
.L258:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L259:
	ldr	w1, [x29,124]
	add	x0, x19, 1224
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
.L254:
	add	w20, w20, 1
	b	.L282
.L283:
	ldr	x0, [x29,104]
	mov	x20, 0
	mov	x22, 24
	add	x19, x0, :lo12:.LANCHOR0
	add	x21, x19, 2164
	ldr	x0, [x19,128]
	bl	NandcIqrWaitFlashReady
.L261:
	ldrb	w0, [x19,2357]
	cmp	w0, w20
	bls	.L284
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w27, 1
	bne	.L262
	ldrb	w0, [x19,196]
	cbz	w0, .L262
	mul	x0, x20, x22
	ldrb	w0, [x0,x21]
	bl	flash_exit_slc_mode
.L262:
	add	x20, x20, 1
	b	.L261
.L284:
	ldr	w0, [x19,2460]
	cbnz	w0, .L264
.L266:
	mov	w0, 0
	b	.L252
.L264:
	ldrb	w0, [x19,2132]
	cmp	w0, 69
	bne	.L266
	mov	x0, 0
	mov	x1, 56
.L265:
	cmp	w23, w0
	bls	.L266
	mul	x2, x0, x1
	add	x0, x0, 1
	str	wzr, [x24,x2]
	b	.L265
.L252:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FlashEraseBlocks, .-FlashEraseBlocks
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
.L288:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L288
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
	ldr	w0, [x1,2380]
	orr	w20, w20, 536870912
	cmp	w0, 3
	orr	w20, w20, 1024
	bfi	w20, w3, 4, 1
	bls	.L292
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w9, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w9, .L306
	cbz	x4, .L293
.L306:
	cbnz	w23, .L295
.L303:
	add	w2, w2, 1
	mov	x0, x4
	asr	w2, w2, 1
	bfi	w20, w2, 22, 6
	cbnz	x4, .L297
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2400]
	b	.L297
.L295:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w7, 128
	mov	w3, 0
	lsr	w10, w2, 1
	mov	w6, w3
	mov	w11, -1
	ldr	w0, [x1,2452]
	cmp	w0, 25
	mov	w0, 64
	csel	w7, w0, w7, cc
.L299:
	cmp	w6, w10
	bcs	.L303
	lsr	w0, w3, 2
	ubfiz	x0, x0, 2, 30
	cbz	w9, .L300
	ldrh	w8, [x5,2]
	ldr	x12, [x1,2408]
	ldrh	w13, [x5],4
	orr	w8, w13, w8, lsl 16
	str	w8, [x12,x0]
	b	.L301
.L300:
	ldr	x8, [x1,2408]
	str	w11, [x8,x0]
.L301:
	add	w6, w6, 1
	add	w3, w3, w7
	b	.L299
.L297:
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x24, x20, 22, 5
	mov	w2, w23
	str	x4, [x29,72]
	ldr	x1, [x19,2408]
	str	x1, [x19,2424]
	lsl	w1, w24, 10
	str	x0, [x19,2416]
	bl	rknand_dma_map_single
	str	w0, [x19,2432]
	ldr	x0, [x19,2424]
	lsl	w1, w24, 7
	mov	w2, w23
	bl	rknand_dma_map_single
	str	w0, [x19,2436]
	mov	w0, 1
	str	w0, [x19,2440]
	ldr	w0, [x19,2432]
	mov	w1, 16
	str	w0, [x21,20]
	ldr	x4, [x29,72]
	ldr	w0, [x19,2436]
	str	w0, [x21,24]
	and	x4, x4, 3
	str	wzr, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 9, 5
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 448
	str	w0, [x29,88]
	cbnz	x4, .L304
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L304:
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
.L293:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L292:
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
.L311:
	cmp	w2, w0
	bhi	.L313
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L311
.L313:
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
	ldrh	w2, [x1,2468]
	ldrh	w3, [x1,2478]
	str	w0, [x1,2464]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,2476]
	ldr	w0, [x1,2484]
	str	w2, [x1,2472]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,2480]
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
	strh	w6, [x1,2488]
	strh	w2, [x1,2490]
	strh	w0, [x1,2492]
	strh	w4, [x1,2478]
.L318:
	add	x5, x1, 2496
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L318
	ldrh	w3, [x23,20]
	ldrb	w1, [x23,15]
	cmp	w3, w1
	bcs	.L319
	uxtb	w8, w0
	mov	w3, 0
	mul	w11, w0, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x20, :lo12:.LANCHOR0
.L320:
	cmp	w3, w0
	bcs	.L322
	uxtb	w1, w3
	mov	w5, w3
	mov	w7, 0
.L323:
	cmp	w7, w2
	bcs	.L345
	add	x9, x12, 2496
	add	w13, w5, w11
	add	w14, w1, w8
	add	w7, w7, 1
	strb	w1, [x9,w5,uxtw]
	add	w1, w1, w10
	strb	w14, [x9,x13]
	add	w5, w5, w0
	uxtb	w1, w1
	b	.L323
.L345:
	add	w3, w3, 1
	b	.L320
.L322:
	add	x1, x20, :lo12:.LANCHOR0
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x1,2490]
	strh	w4, [x1,2478]
.L319:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x1,2528]
	strh	wzr, [x1,2530]
	bne	.L324
	strh	w6, [x1,2528]
.L324:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w2, 4352
	ldrb	w26, [x1,72]
	strh	w2, [x1,2532]
	cbz	w26, .L325
	mov	w2, 384
	strh	w2, [x1,2532]
.L325:
	add	x21, x20, :lo12:.LANCHOR0
	ldrh	w25, [x23,16]
	ldrh	w24, [x23,20]
	ldrh	w27, [x23,18]
	ldrh	w22, [x21,2490]
	ldrh	w19, [x21,2478]
	strh	w25, [x21,2536]
	mul	w22, w0, w22
	mul	w0, w19, w0
	strh	w0, [x21,2534]
	uxth	w22, w22
	strh	w27, [x21,2538]
	strh	w22, [x21,2468]
	mul	w0, w22, w25
	strh	w0, [x21,2540]
	mov	w0, w24
	strh	w24, [x21,2542]
	bl	Ftl_log2
	uxth	w3, w0
	lsl	w2, w24, 9
	strh	w0, [x21,2544]
	ldrh	w0, [x23,26]
	cmp	w19, 1024
	strh	w2, [x21,2546]
	ubfx	x2, x2, 9, 7
	strh	w0, [x21,2550]
	lsl	w2, w2, 1
	mul	w0, w22, w19
	strh	w2, [x21,2548]
	str	w0, [x21,2484]
	bls	.L326
	and	w0, w19, 255
	strh	w0, [x21,2530]
.L326:
	add	x2, x20, :lo12:.LANCHOR0
	mul	w0, w24, w27
	ldrh	w1, [x2,2530]
	sub	w1, w19, w1
	mul	w1, w22, w1
	mul	w1, w1, w24
	mul	w1, w1, w25
	asr	w1, w1, 11
	str	w1, [x2,2552]
	ldrh	w1, [x2,2532]
	lsl	w1, w1, 3
	sdiv	w0, w1, w0
	uxth	w0, w0
	cmp	w0, 4
	bhi	.L342
	mov	w0, 4
.L342:
	strh	w0, [x2,2556]
	cbz	w26, .L329
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 640
	strh	w1, [x0,2532]
.L329:
	add	x1, x20, :lo12:.LANCHOR0
	lsl	w19, w19, 6
	cmp	w22, 1
	ldrh	w0, [x1,2532]
	asr	w0, w0, w3
	add	w3, w3, 9
	asr	w19, w19, w3
	strh	w19, [x1,2560]
	and	w19, w19, 65535
	add	w0, w0, 2
	strh	w0, [x1,2558]
	mul	w0, w19, w22
	str	w0, [x1,2564]
	add	w19, w19, 8
	ldrh	w0, [x1,2556]
	udiv	w0, w0, w22
	add	w19, w19, w0
	bne	.L343
	add	w19, w19, 4
.L343:
	str	w19, [x1,2464]
	add	x19, x20, :lo12:.LANCHOR0
	ldrh	w0, [x19,2464]
	bl	FtlSysBlkNumInit
	str	wzr, [x19,2576]
	ldr	w0, [x19,2464]
	str	w0, [x19,2568]
	ldr	w0, [x19,2480]
	ldrh	w1, [x19,2536]
	lsl	w0, w0, 2
	ldrh	w3, [x19,2542]
	ldrh	w2, [x19,2556]
	mul	w1, w0, w1
	ldrh	w0, [x19,2544]
	ldrb	w5, [x19,196]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	strh	w1, [x19,2574]
	uxth	w0, w0
	strh	w0, [x19,2572]
	add	w1, w2, 3
	strh	w1, [x19,2556]
	ldr	w1, [x19,2564]
	add	w4, w1, 3
	str	w4, [x19,2564]
	cbz	w5, .L332
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x19,2556]
	b	.L344
.L332:
	cmp	w4, 7
	bhi	.L333
	mov	w1, 8
.L344:
	str	w1, [x19,2564]
.L333:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,2476]
	strh	wzr, [x20,2580]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L334
	mov	w0, 1
	strh	w0, [x20,2580]
.L334:
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
	ldrh	w2, [x1,2582]
	cbz	w2, .L347
	ldrh	w2, [x1,2556]
	ldr	x3, [x1,2584]
	mov	x1, 0
.L348:
	cmp	w2, w1, uxth
	bls	.L353
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L348
	mov	w0, 1
	b	.L347
.L353:
	mov	w0, 0
.L347:
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
	ldr	w3, [x2,2592]
	cbz	w3, .L355
	ldr	x5, [x2,2600]
	mov	x0, 56
	mov	x2, 0
.L356:
	cmp	w3, w2
	bls	.L365
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L356
	cmp	w4, w1
	bhi	.L356
	mov	w0, 1
	b	.L355
.L365:
	mov	w0, 0
.L355:
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR0+2576
	ldr	w0, [x0,#:lo12:.LANCHOR0+2576]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	adrp	x0, .LANCHOR0+2576
	ldr	w0, [x0,#:lo12:.LANCHOR0+2576]
	ret
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR0+2608
	ldr	w0, [x0,#:lo12:.LANCHOR0+2608]
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
	add	x19, x19, 2616
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
	ldrh	w1, [x3,2534]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	ldr	x1, [x2,2648]
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
	ldrh	w1, [x0,2534]
	ldrh	w21, [x0,2490]
	mul	w21, w1, w21
.L373:
	cmp	w20, w21
	bge	.L379
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L374
	add	w19, w19, 1
	uxth	w19, w19
.L374:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L373
.L379:
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
	ldrh	w4, [x3,2492]
	udiv	w2, w0, w4
	msub	w0, w2, w4, w0
	madd	w0, w1, w4, w0
	ldrh	w1, [x3,2534]
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
	ldrh	w1, [x2,2492]
	ldrh	w3, [x2,2534]
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
	ldrh	w3, [x2,2534]
	udiv	w1, w0, w3
	msub	w0, w1, w3, w0
	ldrh	w1, [x2,2492]
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
	bls	.L384
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L385
.L384:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L385:
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	adrp	x0, .LANCHOR0+2718
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2718]
	cmp	w0, wzr
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR0+2718
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2718]
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
	bcc	.L388
	adrp	x2, .LANCHOR0
	add	x0, x2, :lo12:.LANCHOR0
	mov	x19, x2
	ldrh	w0, [x0,2718]
	cmp	w0, 1024
	beq	.L388
	cbz	w1, .L390
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w0, [x20,416]
	cbnz	w0, .L390
	mov	w0, w21
	bl	P2V_block_in_plane
	uxth	w22, w0
	ldr	x0, [x20,424]
	lsl	w1, w21, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,424]
	bl	FlashEraseBlocks
	ubfiz	x0, x22, 1, 16
	ldr	x2, [x20,432]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x20,440]
	add	w0, w0, 1
	str	w0, [x20,440]
.L390:
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 2712
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w21, [x0,8]
	strh	w2, [x1,4]
.L388:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	adrp	x0, .LANCHOR2+476
	ldrh	w6, [x0,#:lo12:.LANCHOR2+476]
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x1, x1, 2712
	ldrh	w2, [x1,6]
	cbz	w2, .L397
	mov	w5, 0
	ldrh	w3, [x1,2]
	ldrh	w2, [x1,4]
	mov	w4, w5
	and	w6, w6, 31
.L399:
	cmp	w4, w6
	bge	.L407
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
	b	.L399
.L407:
	cbz	w5, .L397
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0,2714]
	strh	w2, [x0,2716]
.L397:
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
	add	x22, x21, 2712
	add	x23, x23, :lo12:.LC3
	mov	x24, x21
.L409:
	ldrh	w1, [x22,6]
	cbz	w1, .L410
	adrp	x19, .LANCHOR2
	ldrh	w2, [x22,2]
	add	x25, x19, :lo12:.LANCHOR2
	sub	w1, w1, #1
	add	x0, x22, x2, sxtw 1
	strh	w1, [x22,6]
	add	w2, w2, 1
	ldrh	w20, [x0,8]
	ldr	w1, [x25,416]
	and	w2, w2, 1023
	strh	w2, [x22,2]
	cbnz	w1, .L411
	mov	w0, w20
	str	x1, [x29,88]
	bl	P2V_block_in_plane
	uxth	w26, w0
	ldr	x0, [x25,424]
	lsl	w2, w20, 10
	ldr	x1, [x29,88]
	str	w2, [x0,4]
	ldrb	w0, [x21,196]
	cbz	w0, .L412
	ldr	x0, [x25,424]
	mov	w2, 1
	bl	FlashEraseBlocks
.L412:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x19,424]
	bl	FlashEraseBlocks
	ubfiz	x0, x26, 1, 16
	ldr	x2, [x19,432]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x19,440]
	add	w0, w0, 1
	str	w0, [x19,440]
	b	.L411
.L410:
	adrp	x0, .LC2
	add	x0, x0, :lo12:.LC2
	bl	printk
.L413:
	b	.L413
.L411:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L414
	ldrh	w2, [x24,2718]
	mov	x0, x23
	mov	w1, w20
	bl	printk
	b	.L409
.L414:
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
	adrp	x0, .LANCHOR2+496
	mov	x4, -6148914691236517206
	uxth	w1, w1
	mov	w5, 65535
	ldr	x3, [x0,#:lo12:.LANCHOR2+496]
	sub	x0, x2, x3
	asr	x0, x0, 1
	madd	x0, x4, x0, x0
	mov	w4, 6
	uxth	w0, w0
.L420:
	cmp	w1, w0
	beq	.L421
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L422
	umull	x2, w0, w4
	add	x2, x3, x2
	b	.L420
.L421:
	mov	w0, 1
	b	.L419
.L422:
	mov	w0, 0
.L419:
	ret
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x9, .LANCHOR0
	uxth	w0, w0
	add	x9, x9, :lo12:.LANCHOR0
	ldrh	w1, [x9,2476]
	cmp	w1, w0
	bls	.L425
	adrp	x2, .LANCHOR2
	mov	w7, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x10, w0, w7
	mov	x3, x2
	ldr	x11, [x5,496]
	add	x4, x11, x10
	strh	w1, [x4,2]
	strh	w1, [x11,x10]
	ldr	x1, [x5,504]
	cbnz	x1, .L426
	str	x4, [x5,504]
	b	.L425
.L426:
	ubfiz	x8, x0, 1, 16
	ldr	x13, [x5,512]
	ldrh	w2, [x4,4]
	mov	x14, -6148914691236517206
	ldr	x12, [x5,496]
	mov	w18, 65535
	ldrh	w6, [x13,x8]
	cmp	w2, wzr
	ldrh	w17, [x9,2476]
	mul	w6, w6, w2
	sub	x2, x1, x12
	asr	x2, x2, 1
	csinv	w6, w6, wzr, ne
	madd	x2, x14, x2, x2
	ldr	x14, [x5,432]
	mov	w5, 0
	add	x9, x14, x8
	uxth	w2, w2
	mov	w8, w7
.L434:
	add	w5, w5, 1
	cmp	w0, w2
	uxth	w5, w5
	beq	.L425
	cmp	w5, w17
	bhi	.L425
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w7, [x13,x15]
	mul	w7, w7, w16
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L430
	ldrh	w15, [x14,x15]
	ldrh	w7, [x9]
	cmp	w15, w7
	bcc	.L432
	b	.L431
.L430:
	bhi	.L431
.L432:
	ldrh	w7, [x1]
	cmp	w7, w18
	bne	.L433
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x4, [x2,520]
	b	.L425
.L433:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x12, x1
	b	.L434
.L431:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,504]
	cmp	x1, x3
	bne	.L435
	strh	w0, [x1,2]
	str	x4, [x2,504]
	b	.L425
.L435:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,496]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L425:
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
	ldrh	w1, [x0,528]
	add	w1, w1, 1
	strh	w1, [x0,528]
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
	beq	.L440
	adrp	x2, .LANCHOR2
	mov	w6, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x8, w0, w6
	mov	x3, x2
	ldr	x9, [x5,496]
	add	x4, x9, x8
	strh	w1, [x4,2]
	strh	w1, [x9,x8]
	ldr	x1, [x5,536]
	cbnz	x1, .L441
	str	x4, [x5,536]
	b	.L440
.L441:
	ldr	x11, [x5,432]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,496]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L444:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L442
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L443
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L440
.L443:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L444
.L442:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,536]
	cmp	x1, x3
	bne	.L445
	strh	w0, [x1,2]
	str	x4, [x2,536]
	b	.L440
.L445:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,496]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L440:
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
	ldrh	w1, [x0,544]
	add	w1, w1, 1
	strh	w1, [x0,544]
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
	ldr	x2, [x6,496]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L448
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L449
	str	xzr, [x0]
	b	.L450
.L449:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L450
.L448:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L451
	cmp	w3, w0
	beq	.L450
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L450
.L451:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,496]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L450:
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
	cbz	x2, .L453
	adrp	x3, .LANCHOR2+496
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2+496]
.L454:
	cbnz	w1, .L455
.L457:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L453
.L455:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L457
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L454
.L453:
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
	ldr	x1, [x2,504]
	cbz	x1, .L465
	ldr	x4, [x2,496]
	mov	w5, 6
.L461:
	cbz	w3, .L462
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L465
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L461
.L462:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L465:
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
	ldrh	w1, [x0,552]
	cmp	w1, w20
	beq	.L467
	ldrh	w1, [x0,600]
	cmp	w1, w20
	beq	.L467
	ldrh	w1, [x0,648]
	cmp	w1, w20
	beq	.L467
	mov	w3, 6
	ldr	x4, [x0,496]
	ldr	x2, [x0,504]
	umull	x3, w20, w3
	add	x1, x4, x3
	cmp	x1, x2
	beq	.L467
	ldr	x5, [x0,512]
	ubfiz	x0, x20, 1, 16
	ldrh	w2, [x5,x0]
	ldrh	w0, [x1,4]
	ldrh	w1, [x1,2]
	cmp	w0, wzr
	mul	w2, w2, w0
	mov	w0, 65535
	csinv	w2, w2, wzr, ne
	cmp	w1, w0
	bne	.L469
	ldrh	w0, [x4,x3]
	cmp	w0, w1
	beq	.L467
.L469:
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
	bcs	.L467
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, w20
	add	x0, x19, 504
	bl	List_remove_node
	ldrh	w0, [x19,528]
	sub	w0, w0, #1
	strh	w0, [x19,528]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L467:
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
.L474:
	cmp	w20, w2
	beq	.L478
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L475
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	uxth	w1, w0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L476
	adrp	x2, .LANCHOR0+2718
	adrp	x0, .LC4
	add	x0, x0, :lo12:.LC4
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2718]
	bl	printk
.L477:
	b	.L477
.L476:
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L478
.L475:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L474
.L478:
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
	adrp	x0, .LANCHOR0+2574
	mov	x4, 0
	mov	w5, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR0+2574]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	mov	x3, x0
	ldr	x1, [x1,696]
.L481:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L497
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L481
	b	.L482
.L497:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L484:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L498
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L493
	cmp	w5, w7
	bcc	.L485
.L493:
	mov	w5, w7
	mov	w6, w0
.L485:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L484
.L498:
	cmp	w0, w2
	bcc	.L482
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,704]
	mov	w0, w2
.L488:
	cmp	w3, w2
	beq	.L482
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L489
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L489:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L488
.L482:
	ret
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3,706]
	cbnz	w0, .L500
	cmp	w2, 4
	bhi	.L500
	add	w2, w2, 1
	strh	w2, [x3,706]
	b	.L499
.L500:
	adrp	x2, .LANCHOR0+2476
	add	x0, x1, :lo12:.LANCHOR2
	mov	w7, 65535
	ldrh	w5, [x2,#:lo12:.LANCHOR0+2476]
	strh	wzr, [x0,706]
	str	wzr, [x0,708]
	ldr	x6, [x0,512]
	mov	x0, 0
.L501:
	cmp	w5, w0, uxth
	bls	.L499
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L502
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,708]
	add	w2, w4, w2
	str	w2, [x3,708]
.L502:
	add	x0, x0, 1
	b	.L501
.L499:
	ret
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	stp	x29, x30, [sp, -16]!
	uxth	w0, w0
	add	x29, sp, 0
	cbz	w1, .L506
	bl	ftl_set_blk_mode.part.9
	b	.L505
.L506:
	adrp	x1, .LANCHOR0
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L505:
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
	ldrh	w4, [x4,2468]
.L510:
	cmp	w2, w4
	bcs	.L516
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L511
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L511:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L510
.L516:
	add	x3, x3, :lo12:.LANCHOR0
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,2536]
	mov	x3, x0
.L513:
	cmp	x3, x4
	beq	.L517
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L514
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L514:
	add	x3, x3, 2
	b	.L513
.L517:
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
	ldrh	w24, [x0,2468]
.L519:
	cmp	w24, w21, uxth
	bls	.L530
	add	x0, x23, 2496
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L520
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L520:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L519
.L530:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w2, [x1,2536]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldr	w0, [x1,2460]
	cbz	w0, .L522
	adrp	x0, .LANCHOR2+432
	ldrh	w1, [x19]
	ldr	x0, [x0,#:lo12:.LANCHOR2+432]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 79
	bhi	.L522
	mov	w0, 1
	strb	w0, [x19,9]
.L522:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,72]
	cbz	w0, .L523
	mov	w0, 1
	strb	w0, [x19,9]
.L523:
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
	ldrh	w23, [x1,2468]
	mov	w19, w22
	ldrh	w24, [x1,2536]
	mov	x20, x1
.L532:
	cmp	w23, w22, uxth
	bls	.L539
	add	x0, x20, 2496
	mov	w1, w21
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L533
	add	w19, w19, w24
	uxth	w19, w19
.L533:
	add	x22, x22, 1
	b	.L532
.L539:
	cbz	w19, .L535
	mov	w0, 32768
	sdiv	w19, w0, w19
.L535:
	mov	w0, 6
	umull	x21, w21, w0
	adrp	x0, .LANCHOR2+496
	ldr	x0, [x0,#:lo12:.LANCHOR2+496]
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
	ldr	x1, [x2,536]
	mov	w0, w1
	cbz	x1, .L541
	ldr	x0, [x2,496]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,432]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L541:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR2
	uxth	w4, w0
	add	x5, x2, :lo12:.LANCHOR2
	ldr	x1, [x5,536]
	mov	w0, w1
	cbz	x1, .L544
	ldrh	w3, [x5,544]
	mov	w0, 7
	mov	w6, 6
	mov	w7, 65535
	mul	w3, w3, w0
	ldr	x0, [x5,496]
	asr	w3, w3, 3
	cmp	w4, w3
	sub	x1, x1, x0
	csel	w4, w3, w4, gt
	asr	x1, x1, 1
	mov	x3, -6148914691236517206
	madd	x1, x3, x1, x1
	mov	w3, 0
	uxth	w1, w1
.L546:
	cmp	w3, w4
	beq	.L549
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L547
.L549:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,432]
	ldrh	w0, [x0,x1]
	b	.L544
.L547:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L546
.L544:
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
	ldr	w2, [x24,168]
	adrp	x1, .LC6
	mov	x0, x20
	add	x1, x1, :lo12:.LC6
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC7
	ldr	w2, [x24,2552]
	mov	x0, x20
	add	x1, x1, :lo12:.LC7
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0,#:lo12:.LANCHOR1+504]
	cmp	w0, 1
	beq	.L552
	sub	w0, w20, w22
	b	.L553
.L552:
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
	ldr	w2, [x24,2608]
	adrp	x1, .LC10
	mov	x0, x21
	add	x1, x1, :lo12:.LC10
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,708]
	adrp	x1, .LC11
	mov	x0, x21
	add	x1, x1, :lo12:.LC11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,712]
	adrp	x1, .LC12
	mov	x0, x21
	add	x1, x1, :lo12:.LC12
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,716]
	adrp	x1, .LC13
	mov	x0, x21
	add	x1, x1, :lo12:.LC13
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,720]
	adrp	x1, .LC14
	mov	x0, x21
	add	x1, x1, :lo12:.LC14
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,724]
	adrp	x1, .LC15
	mov	x0, x21
	add	x1, x1, :lo12:.LC15
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,728]
	adrp	x1, .LC16
	mov	x0, x21
	add	x1, x1, :lo12:.LC16
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,732]
	adrp	x1, .LC17
	mov	x0, x21
	add	x1, x1, :lo12:.LC17
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,736]
	adrp	x1, .LC18
	mov	x0, x21
	add	x1, x1, :lo12:.LC18
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,740]
	adrp	x1, .LC19
	mov	x0, x21
	add	x1, x1, :lo12:.LC19
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,744]
	adrp	x1, .LC20
	mov	x0, x21
	add	x1, x1, :lo12:.LC20
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,748]
	adrp	x1, .LC21
	add	x1, x1, :lo12:.LC21
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x24,2622]
	adrp	x1, .LC22
	mov	x0, x21
	add	x1, x1, :lo12:.LC22
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,544]
	adrp	x1, .LC23
	mov	x0, x21
	add	x1, x1, :lo12:.LC23
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,752]
	adrp	x1, .LC24
	mov	x0, x21
	add	x1, x1, :lo12:.LC24
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,756]
	adrp	x1, .LC25
	mov	x0, x21
	add	x1, x1, :lo12:.LC25
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,760]
	adrp	x1, .LC26
	mov	x0, x21
	add	x1, x1, :lo12:.LC26
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,440]
	adrp	x1, .LC27
	mov	x0, x21
	add	x1, x1, :lo12:.LC27
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,764]
	adrp	x1, .LC28
	mov	x0, x21
	add	x1, x1, :lo12:.LC28
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,768]
	adrp	x1, .LC29
	mov	x0, x21
	add	x1, x1, :lo12:.LC29
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,478]
	adrp	x1, .LC30
	mov	x0, x21
	add	x1, x1, :lo12:.LC30
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,476]
	adrp	x1, .LC31
	mov	x0, x21
	add	x1, x1, :lo12:.LC31
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2576]
	adrp	x1, .LC32
	mov	x0, x21
	add	x1, x1, :lo12:.LC32
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2568]
	adrp	x1, .LC33
	mov	x0, x21
	add	x1, x1, :lo12:.LC33
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2464]
	adrp	x1, .LC34
	mov	x0, x21
	add	x1, x1, :lo12:.LC34
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,2718]
	adrp	x1, .LC35
	mov	x0, x21
	add	x1, x1, :lo12:.LC35
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,2476]
	adrp	x1, .LC36
	mov	x0, x21
	add	x1, x1, :lo12:.LC36
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,772]
	adrp	x1, .LC37
	mov	x0, x21
	add	x1, x1, :lo12:.LC37
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2480]
	adrp	x1, .LC38
	mov	x0, x21
	add	x1, x1, :lo12:.LC38
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,776]
	adrp	x1, .LC39
	mov	x0, x21
	add	x1, x1, :lo12:.LC39
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,2616]
	adrp	x1, .LC40
	mov	x0, x21
	add	x1, x1, :lo12:.LC40
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,554]
	adrp	x1, .LC41
	mov	x0, x21
	add	x1, x1, :lo12:.LC41
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,558]
	adrp	x1, .LC42
	mov	x0, x21
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,552]
	adrp	x1, .LC43
	mov	x0, x21
	add	x1, x1, :lo12:.LC43
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,560]
	adrp	x1, .LC44
	mov	x0, x21
	add	x1, x1, :lo12:.LC44
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,556]
	adrp	x1, .LC45
	mov	x0, x21
	add	x1, x1, :lo12:.LC45
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x23,552]
	adrp	x1, .LC46
	ldr	x2, [x23,512]
	mov	x0, x21
	add	x1, x1, :lo12:.LC46
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,602]
	adrp	x1, .LC47
	mov	x0, x21
	add	x1, x1, :lo12:.LC47
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,606]
	adrp	x1, .LC48
	mov	x0, x21
	add	x1, x1, :lo12:.LC48
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,600]
	adrp	x1, .LC49
	mov	x0, x21
	add	x1, x1, :lo12:.LC49
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,608]
	adrp	x1, .LC50
	mov	x0, x21
	add	x1, x1, :lo12:.LC50
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,604]
	adrp	x1, .LC51
	mov	x0, x21
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x23,600]
	adrp	x1, .LC52
	ldr	x2, [x23,512]
	mov	x0, x21
	add	x1, x1, :lo12:.LC52
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,650]
	adrp	x1, .LC53
	mov	x0, x21
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,654]
	adrp	x1, .LC54
	mov	x0, x21
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,648]
	adrp	x1, .LC55
	mov	x0, x21
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,656]
	adrp	x1, .LC56
	mov	x0, x21
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,652]
	adrp	x1, .LC57
	mov	x0, x21
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,794]
	adrp	x1, .LC58
	mov	x0, x21
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,798]
	adrp	x1, .LC59
	mov	x0, x21
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,792]
	adrp	x1, .LC60
	mov	x0, x21
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,800]
	adrp	x1, .LC61
	mov	x0, x21
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,796]
	adrp	x1, .LC62
	mov	x0, x21
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x23,840]
	adrp	x1, .LC63
	ldr	w2, [x24,2460]
	add	x1, x1, :lo12:.LC63
	ldr	w4, [x23,932]
	mov	x0, x21
	ldr	w5, [x23,924]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x23,928]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x23,920]
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x23,944]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,1360]
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,1362]
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x23,1364]
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,1368]
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
	ldrh	w0, [x23,544]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x23,792]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L554
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x23,512]
	adrp	x1, .LC72
	mov	x0, x19
	add	x1, x1, :lo12:.LC72
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L554:
	mov	w0, 0
	adrp	x23, .LC73
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x23, x23, :lo12:.LC73
.L556:
	cmp	w3, w27
	beq	.L555
	add	x21, x25, :lo12:.LANCHOR2
	ubfiz	x6, x3, 1, 16
	umull	x24, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x5, [x21,496]
	mov	x1, x23
	ldr	x4, [x21,512]
	add	w20, w20, 1
	ldr	x7, [x21,432]
	add	x5, x5, x24
	ldrh	w4, [x4,x6]
	ldrh	w5, [x5,4]
	ldrh	w6, [x7,x6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,496]
	cmp	w20, 16
	ldrh	w3, [x0,x24]
	bne	.L556
.L555:
	add	x1, x25, :lo12:.LANCHOR2
	adrp	x23, .LC74
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
	add	x23, x23, :lo12:.LC74
	ldr	x0, [x1,536]
	ldr	x3, [x1,496]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L558:
	cmp	w3, w27
	beq	.L557
	add	x21, x25, :lo12:.LANCHOR2
	ubfiz	x5, x3, 1, 16
	umull	x24, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x4, [x21,496]
	mov	x1, x23
	ldr	x6, [x21,432]
	add	w20, w20, 1
	add	x4, x4, x24
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,496]
	cmp	w20, 4
	ldrh	w3, [x0,x24]
	bne	.L558
.L557:
	sub	w0, w19, w22
.L553:
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
	ldr	w2, [x0,760]
	ldr	w1, [x0,768]
	cmp	w2, w1
	bcs	.L563
	adrp	x1, .LANCHOR0
	str	wzr, [x0,752]
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x5, [x0,432]
	mov	x0, 0
	ldrh	w3, [x2,2476]
.L564:
	cmp	w3, w0
	bls	.L596
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x0,lsl 1]
	add	x0, x0, 1
	ldr	w2, [x4,752]
	add	w2, w6, w2
	str	w2, [x4,752]
	b	.L564
.L596:
	add	x2, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x2,752]
	ldrh	w1, [x1,2528]
	udiv	w3, w0, w3
	str	w3, [x2,760]
	ldr	w3, [x2,756]
	sub	w0, w0, w3
	udiv	w0, w0, w1
	str	w0, [x2,752]
	b	.L566
.L563:
	ldr	w1, [x0,764]
	cmp	w2, w1
	bls	.L566
	add	w1, w1, 1
	adrp	x4, .LANCHOR0
	str	w1, [x0,764]
	mov	w0, 0
.L568:
	add	x1, x4, :lo12:.LANCHOR0
	ldrh	w1, [x1,2476]
	cmp	w0, w1
	bcs	.L566
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 1, 32
	add	w0, w0, 1
	ldr	x3, [x1,432]
	ldrh	w1, [x3,x2]
	add	w1, w1, 1
	strh	w1, [x3,x2]
	b	.L568
.L566:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w21, [x1,768]
	ldr	w24, [x1,760]
	add	w0, w21, 256
	cmp	w0, w24
	bls	.L571
	ldr	w1, [x1,764]
	add	w0, w21, 768
	cmp	w0, w1
	bls	.L571
	adrp	x0, .LANCHOR0+2460
	ldr	w0, [x0,#:lo12:.LANCHOR0+2460]
	cbz	w0, .L574
	cmp	w21, 40
	bls	.L571
.L574:
	mov	w0, 65535
	b	.L573
.L571:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,544]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L583
	cmp	w21, 40
	bhi	.L574
.L583:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x3, [x0,504]
	cbz	x3, .L574
	adrp	x1, .LANCHOR0+2476
	mov	w23, 65535
	ldr	x7, [x0,496]
	mov	w20, w23
	ldr	x26, [x0,432]
	mov	x0, -6148914691236517206
	ldrh	w9, [x1,#:lo12:.LANCHOR0+2476]
	mov	w8, w23
	mov	w1, 0
	add	x0, x0, 1
	mov	w10, 6
.L576:
	ldrh	w2, [x3]
	cmp	w2, w8
	beq	.L578
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L574
	ldrh	w4, [x3,4]
	cbz	w4, .L577
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w5, w4
	and	x4, x4, 65535
	ldrh	w3, [x26,x4,lsl 1]
	cmp	w3, w21
	bls	.L582
	cmp	w3, w23
	bcs	.L577
	mov	w23, w3
	mov	w20, w5
.L577:
	umull	x3, w2, w10
	add	x3, x7, x3
	b	.L576
.L582:
	mov	w20, w5
.L578:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L574
	ubfiz	x25, x20, 1, 16
	ldrh	w22, [x26,x25]
	cmp	w22, w21
	bls	.L580
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L580
	add	x0, x19, :lo12:.LANCHOR2
	str	w23, [x0,768]
.L580:
	cmp	w22, w24
	bcs	.L574
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L574
	add	w0, w22, 256
	cmp	w0, w24
	bcc	.L581
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,764]
	cmp	w22, w0
	bcs	.L574
.L581:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC77
	ldrh	w5, [x26,x25]
	add	x0, x0, :lo12:.LC77
	mov	w1, w20
	mov	w2, w24
	ldr	x4, [x19,512]
	ldr	w3, [x19,764]
	ldrh	w4, [x4,x25]
	bl	printk
	mov	w0, 1
	str	w0, [x19,1372]
	mov	w0, w20
.L573:
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
	beq	.L598
	adrp	x2, .LANCHOR2+512
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2+512]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L598:
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
	str	wzr, [x0,1376]
.L600:
	add	x5, x10, :lo12:.LANCHOR0
	adrp	x9, .LANCHOR0
	ldrh	w0, [x5,2468]
	cmp	w1, w0
	bcs	.L604
	add	x2, x3, :lo12:.LANCHOR2
	umull	x4, w1, w11
	ldr	x7, [x2,1384]
	ldr	x8, [x2,1392]
	add	x0, x7, x4
	str	w12, [x0,16]
	ldrh	w0, [x5,2546]
	mul	w0, w1, w0
	sdiv	w0, w0, w6
	add	x0, x8, x0, sxtw 2
	str	x0, [x7,x4]
	ldrh	w0, [x5,2548]
	ldr	x8, [x2,1384]
	ldr	x5, [x2,1400]
	mul	w0, w1, w0
	add	x7, x8, x4
	ldr	x2, [x2,1408]
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
	b	.L600
.L604:
	mov	w8, 24
	mov	w5, 4
.L602:
	add	x2, x3, :lo12:.LANCHOR2
	ldr	w1, [x2,1416]
	cmp	w0, w1
	bcs	.L605
	umull	x4, w0, w8
	ldr	x7, [x2,1384]
	add	x6, x9, :lo12:.LANCHOR0
	ldr	x10, [x2,1392]
	add	x1, x7, x4
	str	wzr, [x1,16]
	ldrh	w1, [x6,2546]
	mul	w1, w0, w1
	sdiv	w1, w1, w5
	add	x1, x10, x1, sxtw 2
	str	x1, [x7,x4]
	ldrh	w1, [x6,2548]
	ldr	x6, [x2,1384]
	ldr	x2, [x2,1400]
	mul	w1, w0, w1
	add	x4, x6, x4
	add	w0, w0, 1
	sdiv	w1, w1, w5
	uxth	w0, w0
	add	x1, x2, x1, sxtw 2
	str	x1, [x4,8]
	b	.L602
.L605:
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
	ldr	w8, [x2,1416]
	ldr	x5, [x2,1384]
.L607:
	cmp	w3, w1
	bcs	.L606
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L612:
	cmp	w2, w8
	bcs	.L609
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L608
	str	wzr, [x10,16]
	b	.L609
.L608:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L612
.L609:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L607
.L606:
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
	ldr	w5, [x2,1416]
	ldr	x6, [x2,1384]
	mov	w2, 0
.L614:
	cmp	w2, w1
	bcs	.L620
	mov	w3, 0
.L618:
	cmp	w3, w5
	bcs	.L616
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L615
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L616
.L615:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L618
.L616:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L614
.L620:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR2
	uxth	w0, w0
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w2, [x1,1420]
	ldr	x3, [x1,1424]
	mov	x1, 0
.L622:
	cmp	w2, w1, uxth
	bls	.L626
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L622
	mov	w0, 1
	b	.L623
.L626:
	mov	w0, 0
.L623:
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
	ldrh	w7, [x4,1420]
	ldr	x6, [x4,1424]
.L628:
	uxth	w4, w5
	cmp	w4, w7
	bcs	.L632
	add	x5, x5, 1
	add	x9, x6, x5, lsl 1
	ldrh	w9, [x9,-2]
	cmp	w9, w8
	bne	.L628
.L632:
	cmp	w4, w7
	bne	.L630
	ubfiz	x4, x4, 1, 16
	strh	w0, [x6,x4]
	add	x0, x3, :lo12:.LANCHOR2
	ldrh	w4, [x0,1420]
	add	w4, w4, 1
	strh	w4, [x0,1420]
.L630:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w4, [x3,1432]
	ldr	x5, [x3,1440]
	umull	x4, w4, w0
	add	x5, x5, x4
	str	w1, [x5,4]
	ldr	x1, [x3,1440]
	add	x0, x1, x4
	str	w2, [x0,8]
	str	w19, [x1,x4]
	ldrh	w0, [x3,1432]
	add	w0, w0, 1
	strh	w0, [x3,1432]
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
	ldrh	w0, [x19,1448]
	cmp	w0, w20
	beq	.L636
	ldrh	w0, [x19,1450]
	cmp	w0, w20
	beq	.L636
	ldrh	w0, [x19,1452]
	cmp	w0, w20
	beq	.L636
	ldrh	w0, [x19,1454]
	cmp	w0, w20
	beq	.L636
	adrp	x0, .LC78
	mov	w1, w20
	add	x0, x0, :lo12:.LC78
	bl	printk
	ldrh	w1, [x19,1448]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L638
	strh	w20, [x19,1448]
	b	.L636
.L638:
	ldrh	w1, [x19,1450]
	cmp	w1, w0
	bne	.L639
	strh	w20, [x19,1450]
	b	.L636
.L639:
	ldrh	w1, [x19,1452]
	cmp	w1, w0
	bne	.L640
	strh	w20, [x19,1452]
	b	.L636
.L640:
	ldrh	w1, [x19,1454]
	cmp	w1, w0
	bne	.L636
	strh	w20, [x19,1454]
.L636:
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
	ldrh	w0, [x19,1448]
	cmp	w0, w20
	beq	.L642
	ldrh	w0, [x19,1450]
	cmp	w0, w20
	beq	.L642
	ldrh	w0, [x19,1452]
	cmp	w0, w20
	beq	.L642
	ldrh	w0, [x19,1454]
	cmp	w0, w20
	beq	.L642
	adrp	x0, .LC78
	mov	w1, w20
	add	x0, x0, :lo12:.LC78
	bl	printk
	ldrh	w1, [x19,1448]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L643
	strh	w20, [x19,1448]
	b	.L642
.L643:
	ldrh	w1, [x19,1450]
	cmp	w1, w0
	bne	.L644
	strh	w20, [x19,1450]
	b	.L642
.L644:
	ldrh	w1, [x19,1452]
	cmp	w1, w0
	bne	.L645
	strh	w20, [x19,1452]
	b	.L642
.L645:
	ldrh	w1, [x19,1454]
	cmp	w1, w0
	bne	.L650
	strh	w20, [x19,1454]
	b	.L642
.L650:
	mov	w21, -1
.L642:
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
	ldrh	w1, [x22,1456]
	adrp	x0, .LC79
	mov	w2, w20
	add	x0, x0, :lo12:.LC79
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	adrp	x0, .LANCHOR0+2460
	ldr	w0, [x0,#:lo12:.LANCHOR0+2460]
	cbz	w0, .L652
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,432]
	ldrh	w1, [x2,x0]
	cmp	w1, 39
	bls	.L652
	sub	w1, w1, #40
	strh	w1, [x2,x0]
.L652:
	add	x2, x19, :lo12:.LANCHOR2
	mov	x0, 0
	add	x2, x2, 1464
	ldrh	w1, [x2,-8]
.L653:
	cmp	w1, w0, uxth
	bls	.L659
	add	x0, x0, 1
	add	x3, x2, x0, lsl 1
	ldrh	w3, [x3,-2]
	cmp	w3, w20
	bne	.L653
	b	.L654
.L659:
	cmp	w1, 15
	bhi	.L654
	add	x19, x19, :lo12:.LANCHOR2
	add	w0, w1, 1
	add	x19, x19, 1464
	strh	w0, [x19,-8]
	strh	w20, [x19,w1,sxtw 1]
.L654:
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
	ldrh	w1, [x0,1456]
	cbz	w1, .L661
	ldrh	w3, [x0,1448]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L661
	ldrh	w2, [x0,1498]
	cmp	w2, w1
	bcc	.L662
	strh	wzr, [x0,1498]
.L662:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x19, 1464
	ldrh	w1, [x19,1498]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,1498]
	add	w0, w0, 1
	strh	w0, [x19,1498]
.L661:
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
	cbnz	w0, .L668
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
.L669:
	add	x2, x1, 200
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L669
.L675:
	adrp	x19, .LANCHOR2
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR2
	mov	w2, 2048
	add	x19, x19, 1500
	uxth	w20, w20
	mov	x0, x19
	bl	ftl_memset
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
	b	.L670
.L668:
	cmp	w0, 1
	bne	.L671
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x3, :lo12:.LANCHOR0
.L674:
	cmp	x0, 3
	uxth	w1, w0
	mov	w2, w1
	bls	.L672
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L672:
	add	x1, x3, 200
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L674
	b	.L675
.L671:
	cmp	w0, 2
	bne	.L676
	adrp	x3, .LANCHOR0
	mov	w1, 65535
	mov	x0, 0
	add	x3, x3, :lo12:.LANCHOR0
.L678:
	add	x4, x3, 200
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L678
	b	.L675
.L676:
	cmp	w0, 3
	bne	.L679
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x3, :lo12:.LANCHOR0
.L682:
	cmp	x0, 5
	uxth	w1, w0
	mov	w2, w1
	bls	.L680
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L680:
	add	x1, x3, 200
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L682
	b	.L675
.L679:
	cmp	w0, 4
	bne	.L683
	adrp	x1, .LANCHOR0
	mov	w5, 7
	add	x1, x1, :lo12:.LANCHOR0
	mov	w4, 6
	add	x2, x1, 200
	strh	w0, [x2,8]
	mov	w0, 5
	strh	wzr, [x1,200]
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
.L685:
	and	w3, w0, 1
	ubfiz	w1, w0, 1, 15
	add	w0, w0, 1
	cmp	w3, wzr
	csel	w3, w5, w4, ne
	add	x2, x2, 2
	uxth	w0, w0
	sub	w1, w1, w3
	cmp	w0, 256
	strh	w1, [x2,14]
	bne	.L685
	b	.L675
.L683:
	cmp	w0, 5
	bne	.L686
	adrp	x2, .LANCHOR0
	mov	x0, 0
	add	x2, x2, :lo12:.LANCHOR0
.L687:
	add	x1, x2, 200
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L687
	mov	x0, 0
.L688:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L688
	b	.L675
.L686:
	cmp	w0, 6
	bne	.L689
	adrp	x1, .LANCHOR0
	mov	x2, 0
	mov	w4, 12
	mov	w5, 10
	add	x1, x1, :lo12:.LANCHOR0
.L692:
	cmp	x2, 5
	uxth	w3, w2
	mov	w0, w3
	bls	.L690
	add	w0, w3, w3, lsl 1
	and	w3, w3, 1
	cmp	w3, wzr
	uxth	w0, w0
	csel	w3, w4, w5, ne
	sub	w0, w0, w3
	uxth	w0, w0
.L690:
	add	x3, x1, 200
	strh	w0, [x3,x2,lsl 1]
	add	x2, x2, 1
	cmp	x2, 256
	bne	.L692
	b	.L675
.L689:
	cmp	w0, 9
	bne	.L693
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x0, 200
	strh	wzr, [x0,200]
	mov	w0, 1
	strh	w0, [x1,2]
	mov	w0, 2
	strh	w0, [x1,4]
	mov	x0, 0
.L694:
	add	x2, x1, x0
	add	w3, w0, 3
	add	x0, x0, 2
	cmp	x0, 506
	strh	w3, [x2,6]
	bne	.L694
	b	.L675
.L693:
	cmp	w0, 10
	bne	.L675
	adrp	x2, .LANCHOR0
	mov	x0, 0
	add	x2, x2, :lo12:.LANCHOR0
.L695:
	add	x1, x2, 200
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L695
	mov	x0, 0
.L696:
	add	x2, x1, x0
	add	w3, w0, 63
	add	x0, x0, 2
	cmp	x0, 898
	strh	w3, [x2,126]
	bne	.L696
	b	.L675
.L670:
	cmp	w20, w0, uxth
	bls	.L717
	add	x2, x1, 200
	ldrh	w2, [x2,x0,lsl 1]
	add	x0, x0, 1
	strh	w2, [x19,w2,sxtw 1]
	b	.L670
.L717:
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
	strb	wzr, [x0,3548]
	adrp	x0, .LANCHOR1+482
	add	x19, x21, :lo12:.LANCHOR0
	mov	x24, 0
	add	x25, x19, 2132
	ldrh	w0, [x0,#:lo12:.LANCHOR1+482]
	strb	wzr, [x19,2357]
	bl	FlashBlockAlignInit
	mov	w1, 0
	mov	w2, 8
	add	x0, x19, 2360
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 32
	add	x0, x19, 1224
	bl	ftl_memset
	add	x0, x19, 2164
	mov	w1, 0
	mov	w2, 192
	bl	ftl_memset
	ldr	x20, [x19,1256]
	add	x23, x20, 1
.L720:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x25, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L719
	ldrb	w1, [x19,2357]
	add	x2, x19, 1224
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x19, x1, sxtw
	strb	w0, [x19,2357]
	strb	w24, [x1,2360]
.L719:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L720
	add	x19, x21, :lo12:.LANCHOR0
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w1, [x19,2357]
	strb	w1, [x0,3548]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L721
.L725:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x20,13]
	add	x22, x22, :lo12:.LANCHOR2
	ldp	x23, x24, [sp,48]
	ldrb	w0, [x21,2357]
	ldp	x25, x26, [sp,64]
	mul	w0, w0, w1
	ldrh	w1, [x20,14]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,3550]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
.L721:
	ldr	w25, [x19,76]
	mov	x24, 0
	add	x26, x19, 2132
.L724:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x26, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L722
	ldrb	w1, [x20,13]
	add	x4, x19, 1224
	ldrb	w3, [x19,2357]
	mul	w0, w1, w25
	ldrh	w1, [x20,14]
	mov	x2, x3
	and	w1, w1, 65280
	mul	w1, w0, w1
	str	w1, [x4,w3,sxtw 2]
	ldrb	w0, [x20,23]
	cbz	w0, .L723
	lsl	w1, w1, 1
	str	w1, [x4,w3,sxtw 2]
.L723:
	add	x0, x19, x3
	add	w2, w2, 1
	strb	w2, [x19,2357]
	strb	w24, [x0,2360]
.L722:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L724
	b	.L725
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
	ldr	x0, [x1,1256]
	ldrb	w2, [x0,9]
	ldr	w0, [x1,76]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldrb	w0, [x1,2456]
	strb	w0, [x19,7]
	ldr	w0, [x1,2576]
	str	w0, [x19]
	ldr	x0, [x1,1256]
	ldrb	w4, [x1,2357]
	ldrb	w0, [x0,9]
	strb	w0, [x19,6]
	mov	w0, 32
	strb	w0, [x19,8]
	ldr	x0, [x1,1256]
	ldrb	w0, [x0,7]
	strb	w0, [x19,9]
	mov	x0, 0
	strb	wzr, [x19,10]
.L733:
	cmp	w4, w0, uxtb
	bls	.L735
	add	x2, x1, 2360
	ldrb	w3, [x19,10]
	ldrb	w2, [x0,x2]
	add	x0, x0, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L733
.L735:
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
	str	w1, [x0,3564]
	strh	wzr, [x0,3552]
	mov	w1, -1
	str	wzr, [x0,3556]
	strh	w1, [x0,1448]
	str	wzr, [x0,840]
	str	wzr, [x0,744]
	str	wzr, [x0,748]
	str	wzr, [x0,732]
	str	wzr, [x0,720]
	str	wzr, [x0,716]
	str	wzr, [x0,724]
	str	wzr, [x0,728]
	str	wzr, [x0,712]
	str	wzr, [x0,752]
	str	wzr, [x0,756]
	str	wzr, [x0,440]
	str	wzr, [x0,764]
	str	wzr, [x0,768]
	str	wzr, [x0,3560]
	str	wzr, [x0,1372]
	str	wzr, [x0,3568]
	str	wzr, [x0,1364]
	str	wzr, [x0,3572]
	strh	w1, [x0,1450]
	strh	w1, [x0,1452]
	strh	w1, [x0,1454]
	mov	w1, 32
	ldrh	w2, [x3,2542]
	strh	w1, [x0,1360]
	mov	w1, 128
	strh	w1, [x0,1362]
	mov	w1, 1024
	strh	wzr, [x0,1368]
	sdiv	w2, w1, w2
	ldrh	w1, [x3,2468]
	strh	wzr, [x0,1456]
	lsl	w1, w1, 2
	strh	wzr, [x0,3576]
	strh	wzr, [x0,1498]
	cmp	w2, w1
	str	w2, [x0,3580]
	bls	.L737
	str	w1, [x0,3580]
.L737:
	add	x20, x22, :lo12:.LANCHOR0
	add	x19, x21, :lo12:.LANCHOR2
	mov	w24, 56
	ldrh	w0, [x20,2540]
	str	wzr, [x20,2592]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,1424]
	ldrh	w1, [x20,2540]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,2468]
	str	x0, [x19,1440]
	mul	w23, w23, w24
	lsl	w25, w23, 3
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,3584]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3592]
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,3600]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,424]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,1408]
	ldr	w0, [x19,3580]
	mul	w0, w0, w24
	bl	ftl_malloc
	str	x0, [x20,2600]
	ldrh	w0, [x20,2468]
	ldrh	w23, [x20,2546]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,1416]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3608]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3616]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3624]
	ldr	w0, [x19,1416]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,1392]
	ldr	w0, [x19,3580]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,3632]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3640]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3648]
	ldr	w1, [x19,1416]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,2468]
	str	x0, [x19,1384]
	ldrh	w0, [x20,2548]
	mul	w23, w0, w23
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3656]
	lsl	w0, w23, 3
	bl	ftl_malloc
	str	x0, [x19,3664]
	ldrh	w1, [x20,2548]
	ldr	w0, [x19,1416]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1400]
	ldrh	w1, [x20,2548]
	ldr	w0, [x19,3580]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,3672]
	ldrh	w0, [x20,2478]
	lsl	w0, w0, 1
	strh	w0, [x19,3680]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19,3688]
	ldrh	w0, [x19,3680]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19,3680]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w23, [x20,2478]
	str	x0, [x19,3696]
	add	x0, x0, 32
	lsl	w23, w23, 1
	str	x0, [x19,432]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3704]
	mov	w0, w23
	bl	ftl_malloc
	ldr	w23, [x20,2564]
	str	x0, [x19,512]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3712]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3720]
	ldrh	w0, [x20,2478]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x22,#:lo12:.LANCHOR0]
	ldrh	w0, [x20,2556]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x20,2584]
	ldrh	w0, [x20,2556]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,3728]
	ldrh	w0, [x20,2556]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3736]
	ldrh	w0, [x20,2558]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3744]
	ldrh	w2, [x20,2558]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w23, [x20,2572]
	lsl	w23, w23, 2
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3752]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,3760]
	ldr	w0, [x20,2564]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3768]
	ldrh	w0, [x20,2574]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,696]
	ldrh	w1, [x20,2574]
	ldrh	w0, [x20,2546]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,3776]
	ldrh	w1, [x20,2478]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,496]
	ldrh	w0, [x20,2534]
	ldrh	w1, [x20,2490]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,3784]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,2648]
	add	x1, x20, 2616
	ldrh	w4, [x19,3784]
	ldrh	w5, [x20,2490]
	mov	w0, 1
	mov	w2, w4
.L738:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L838
	ldr	x3, [x20,2648]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L738
.L838:
	add	x1, x22, :lo12:.LANCHOR0
.L740:
	cmp	w0, 8
	beq	.L839
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,2648]
	b	.L740
.L839:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,3712]
	cbnz	x1, .L742
.L744:
	adrp	x0, .LC80
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC80
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L743
.L742:
	ldr	x1, [x0,3720]
	cbz	x1, .L744
	ldr	x1, [x0,3752]
	cbz	x1, .L744
	ldr	x1, [x0,3768]
	cbz	x1, .L744
	ldr	x1, [x0,696]
	cbz	x1, .L744
	ldr	x1, [x0,3776]
	cbz	x1, .L744
	ldr	x1, [x0,496]
	cbz	x1, .L744
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x1, [x22,2648]
	cbz	x1, .L744
	ldr	x0, [x0,512]
	cbz	x0, .L744
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,1424]
	cbz	x1, .L744
	ldr	x1, [x0,1440]
	cbz	x1, .L744
	ldr	x1, [x0,3584]
	cbz	x1, .L744
	ldr	x1, [x0,3600]
	cbz	x1, .L744
	ldr	x1, [x0,424]
	cbz	x1, .L744
	ldr	x1, [x0,1408]
	cbz	x1, .L744
	ldr	x1, [x0,3592]
	cbz	x1, .L744
	ldr	x1, [x0,3608]
	cbz	x1, .L744
	ldr	x1, [x0,3616]
	cbz	x1, .L744
	ldr	x0, [x0,3624]
	cbz	x0, .L744
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,1392]
	cbz	x1, .L744
	ldr	x1, [x0,3640]
	cbz	x1, .L744
	ldr	x1, [x0,3648]
	cbz	x1, .L744
	ldr	x1, [x0,1384]
	cbz	x1, .L744
	ldr	x1, [x0,3656]
	cbz	x1, .L744
	ldr	x1, [x0,3664]
	cbz	x1, .L744
	ldr	x1, [x0,1400]
	cbz	x1, .L744
	ldr	x1, [x0,432]
	cbz	x1, .L744
	ldr	x0, [x0,3688]
	cbz	x0, .L744
	adrp	x0, .LANCHOR0+2584
	ldr	x0, [x0,#:lo12:.LANCHOR0+2584]
	cbz	x0, .L744
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,3728]
	cbz	x1, .L744
	ldr	x1, [x0,3736]
	cbz	x1, .L744
	ldr	x0, [x0,3744]
	cbz	x0, .L744
	mov	w0, 0
.L743:
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
	adrp	x0, .LANCHOR2+3784
	mov	x19, x1
	ldrh	w2, [x0,#:lo12:.LANCHOR2+3784]
	mov	x0, x1
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	adrp	x5, .LANCHOR0
	mov	x3, 0
	mov	w0, 65535
	mov	w6, 1
	add	x5, x5, :lo12:.LANCHOR0
.L842:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L840
	ubfx	x4, x2, 5, 11
	lsl	w2, w6, w2
	lsl	x4, x4, 2
	add	x3, x3, 2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	ldrh	w2, [x5,2622]
	add	w2, w2, 1
	strh	w2, [x5,2622]
	bne	.L842
.L840:
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
	add	x0, x0, 2628
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
	strh	w0, [x1,2712]
	add	x0, x1, 2720
	strh	wzr, [x1,2714]
	strh	wzr, [x1,2716]
	strh	wzr, [x1,2718]
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
.L848:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L870
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L849:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L871
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22,x3]
	cmp	w2, w4
	bne	.L850
	cbz	w4, .L850
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L850:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L849
.L871:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L848
.L870:
	adrp	x0, .LANCHOR0+2538
	mov	w23, 0
	mov	w20, w23
	ldrh	w1, [x0,#:lo12:.LANCHOR0+2538]
	ldrh	w0, [x19]
	strh	w1, [x21,x0,lsl 1]
	ldrh	w24, [x21]
.L853:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L872
	ubfiz	x2, x20, 1, 16
	ldrh	w1, [x21,x2]
	cmp	w24, w1
	bls	.L854
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbnz	w0, .L855
	b	.L856
.L854:
	cbnz	w1, .L856
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbz	w0, .L856
.L858:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L856:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L853
.L855:
	mov	w23, w20
	mov	w24, w1
	cbz	w1, .L858
	b	.L856
.L872:
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
	ldr	w2, [x21,2564]
	ldr	x0, [x22,3720]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x21,2546]
	mov	w1, 255
	ldrh	w2, [x21,2574]
	ldr	x0, [x22,3776]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w1, 0
	mov	w5, -1
.L874:
	add	x3, x20, :lo12:.LANCHOR0
	ldrh	w0, [x3,2574]
	cmp	w0, w1
	bls	.L876
	add	x4, x19, :lo12:.LANCHOR2
	ubfiz	x0, x1, 4, 16
	ldr	x2, [x4,696]
	add	x2, x2, x0
	str	wzr, [x2,4]
	ldr	x2, [x4,696]
	strh	w5, [x2,x0]
	ldr	x2, [x4,696]
	add	x2, x2, x0
	ldrh	w0, [x3,2546]
	ldr	x3, [x4,3776]
	mul	w0, w0, w1
	add	w1, w1, 1
	sxtw	x0, w0
	uxth	w1, w1
	and	x0, x0, -4
	add	x0, x3, x0
	str	x0, [x2,8]
	b	.L874
.L876:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, -1
	add	x1, x0, 3792
	strh	w2, [x0,3794]
	strh	w2, [x0,3792]
	ldr	w2, [x3,2564]
	strh	w2, [x0,3802]
	mov	w2, -3902
	strh	w2, [x0,3796]
	ldrh	w2, [x0,3856]
	strh	w2, [x0,3800]
	ldrh	w2, [x3,2572]
	strh	w2, [x0,3798]
	ldr	x2, [x0,3712]
	str	x2, [x0,3808]
	ldr	x2, [x0,3768]
	str	x2, [x0,3816]
	ldr	x2, [x0,3720]
	str	x2, [x0,3824]
	ldr	x0, [x0,3752]
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
	strh	w0, [x19,3872]
	mov	w0, -1
	ldrh	w2, [x20,2556]
	str	w0, [x19,3884]
	ldr	x0, [x20,2584]
	lsl	w2, w2, 1
	str	xzr, [x19,3864]
	str	wzr, [x19,3876]
	str	wzr, [x19,3880]
	str	wzr, [x20,2460]
	strh	wzr, [x20,2582]
	bl	ftl_memset
	ldrh	w2, [x20,2478]
	mov	w1, 0
	ldr	x0, [x19,432]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2478]
	mov	w1, 0
	ldr	x0, [x19,3688]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x19, 448
	mov	w1, 0
	mov	w2, 48
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 512
	add	x0, x19, 848
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
	ldrh	w2, [x0,2478]
	mov	w22, w25
	mov	w26, 32768
	ldr	x0, [x19,496]
	mul	w2, w2, w24
	bl	ftl_memset
	str	xzr, [x19,536]
	str	xzr, [x19,504]
	str	xzr, [x19,520]
	strh	wzr, [x19,528]
	strh	wzr, [x19,544]
	strh	wzr, [x19,3552]
	mov	w19, w25
.L879:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2476]
	cmp	w19, w1
	bge	.L886
	mov	w20, 0
	ldrh	w3, [x0,2468]
	ldrh	w2, [x0,2536]
	mov	w28, w20
	mov	x27, x0
.L887:
	cmp	w28, w3
	bge	.L888
	add	x0, x27, 2496
	mov	w1, w19
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L880
	add	w20, w2, w20
	sxth	w20, w20
.L880:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L887
.L888:
	cbz	w20, .L882
	sdiv	w20, w26, w20
	sxth	w20, w20
	b	.L883
.L882:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, -1
	ldr	x0, [x0,512]
	strh	w1, [x0,w19,sxtw 1]
.L883:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,496]
	smaddl	x1, w19, w24, x1
	strh	w20, [x1,4]
	ldrh	w1, [x0,552]
	cmp	w19, w1
	beq	.L884
	ldrh	w1, [x0,600]
	cmp	w19, w1
	beq	.L884
	ldrh	w1, [x0,648]
	cmp	w19, w1
	beq	.L884
	ldr	x0, [x0,512]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L885
	add	w4, w25, 1
	mov	w0, w19
	uxth	w25, w4
	bl	INSERT_FREE_LIST
	b	.L884
.L885:
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_DATA_LIST
.L884:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L879
.L886:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x23, x24, [sp,48]
	strh	w25, [x21,544]
	strh	w22, [x21,528]
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
	ldrh	w2, [x20,2540]
	ldr	x0, [x19,1424]
	lsl	w2, w2, 1
	strh	wzr, [x19,1420]
	strh	wzr, [x19,1432]
	bl	ftl_memset
	ldrh	w3, [x20,2540]
	mov	w2, 12
	ldr	x0, [x19,1440]
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
.L896:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x22
	bl	FlashMemCmp8
	mov	w23, w0
	cbnz	w0, .L893
	add	x0, x19, :lo12:.LANCHOR1
	ubfiz	x20, x20, 5, 32
	add	x1, x0, 512
	adds	x20, x1, x20
	beq	.L899
	ldrb	w3, [x20,22]
	mov	x2, 0
	mov	x1, x0
	b	.L898
.L893:
	add	w20, w20, 1
	add	x21, x21, 32
	cmp	w20, 83
	bne	.L896
	b	.L899
.L898:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,3168]
	cmp	w4, w3
	beq	.L897
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L898
	mov	w0, w2
.L897:
	add	x19, x19, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x19, 3168
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x2, x1
	add	x0, x0, 88
	mov	w2, 32
	add	x19, x19, 472
	bl	ftl_memcpy
	mov	x0, x19
	mov	x1, x20
	mov	w2, 32
	bl	ftl_memcpy
	ldrh	w0, [x19,10]
	bl	FlashBlockAlignInit
	b	.L894
.L899:
	mov	w23, -1
.L894:
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
	bne	.L904
	cbz	x3, .L905
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L905:
	cbz	x20, .L903
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
	b	.L903
.L904:
	cbz	x3, .L908
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L908:
	cbz	x20, .L903
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
.L903:
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
.L920:
	cmp	w2, w3
	bls	.L922
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L920
.L922:
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
	adrp	x4, .LANCHOR4+1584
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1584]
	add	x4, x4, 4096
	cbnz	w2, .L926
	add	x1, x4, x1
	b	.L928
.L926:
	add	x0, x4, x1
	mov	x1, x6
.L928:
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
	adrp	x0, .LANCHOR0+2576
	ldr	w0, [x0,#:lo12:.LANCHOR0+2576]
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
.L937:
	cmp	x19, x26
	beq	.L945
	cbnz	w20, .L938
	mov	x0, x23
	mov	x1, x27
	mov	x2, x22
	mov	w3, w19
	bl	printk
.L938:
	cmp	w25, 4
	mov	x0, x21
	bne	.L939
	ldr	w1, [x22,x19,lsl 2]
	b	.L944
.L939:
	cmp	w25, 2
	bne	.L941
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L944
.L941:
	ldrb	w1, [x22,x19]
.L944:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L942
	adrp	x1, .LC83
	mov	x0, x24
	add	x1, x1, :lo12:.LC83
	mov	w20, 0
	bl	printk
.L942:
	add	x19, x19, 1
	b	.L937
.L945:
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
	bne	.L947
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+3313
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+3313]
	b	.L1010
.L947:
	cmp	w27, 3
	bne	.L949
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
	b	.L1050
.L949:
	cmp	w27, 4
	bne	.L950
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
.L1050:
	mov	w26, 8
	strb	w0, [x1,11]
	mov	w28, w26
	b	.L948
.L950:
	cmp	w27, 5
	bne	.L951
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w26, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L1051
.L951:
	cmp	w27, 6
	bne	.L952
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w26, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L1051
.L952:
	cmp	w27, 7
	bne	.L953
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
	b	.L948
.L953:
	cmp	w27, 8
	bne	.L1010
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
	b	.L948
.L1010:
	mov	w26, 7
.L1051:
	mov	w28, 4
.L948:
	sub	w0, w27, #1
	cmp	w0, 1
	bhi	.L1047
	adrp	x22, .LANCHOR1
	add	x25, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	add	x4, x25, 1272
	add	x27, x25, 8
	add	x22, x22, 3296
.L954:
	ldrb	w0, [x25,2357]
	cmp	w0, w21
	bls	.L961
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
.L956:
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
	bhi	.L956
	mov	x0, 0
.L957:
	add	w2, w0, 8
	mov	x1, 0
.L958:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x22, x3
	cmp	x1, 24
	ldrb	w6, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w6, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L958
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L957
	add	w21, w21, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w21, w21
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L954
.L1047:
	sub	w0, w27, #3
	cmp	w0, 5
	bhi	.L961
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
.L962:
	ldrb	w0, [x20,2357]
	cmp	w0, w21
	bhi	.L1008
.L961:
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
.L1008:
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
	bne	.L963
	mov	x0, 160
	madd	x6, x6, x0, x20
	add	x6, x6, 1300
	b	.L964
.L963:
	cmp	w27, 8
	beq	.L965
	add	x6, x20, x6, lsl 6
	add	x6, x6, 1292
.L964:
	mov	w0, 54
	cmp	w27, 4
	str	w0, [x24,2056]
	bne	.L966
	mov	w0, 255
	str	w0, [x24,2052]
	mov	w0, 64
	str	w0, [x24,2048]
	mov	w0, 204
	b	.L1052
.L966:
	ldr	w0, [x29,136]
	cmp	w0, 1
	bhi	.L968
	ldrb	w0, [x20,1276]
	str	w0, [x24,2052]
	mov	w0, 82
	b	.L1053
.L968:
	cmp	w27, 7
	bne	.L967
	mov	w0, 174
	str	w0, [x24,2052]
	str	wzr, [x24,2048]
	mov	w0, 176
.L1052:
	str	w0, [x24,2052]
	mov	w0, 77
.L1053:
	str	w0, [x24,2048]
.L967:
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
	bne	.L969
	mov	w0, 31
	str	w0, [x24,2052]
	b	.L970
.L969:
	str	wzr, [x24,2052]
.L970:
	mov	w0, 2
	str	w0, [x24,2052]
	str	wzr, [x24,2052]
.L1009:
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
	cbnz	w9, .L1012
	cbnz	w8, .L1012
	cmp	w27, 7
	mov	w7, 32
	mov	w0, 2
	csel	w7, w0, w7, ne
	b	.L971
.L1012:
	mov	w7, 16
.L971:
	adrp	x1, .LANCHOR4
	mov	x10, 0
	add	x0, x1, :lo12:.LANCHOR4
	ldr	x0, [x0,1592]
.L972:
	ldr	w11, [x24,2048]
	strb	w11, [x0,x10]
	add	x10, x10, 1
	cmp	w7, w10, uxtb
	bhi	.L972
	cbz	w8, .L973
	mov	w7, 0
.L975:
	ldrb	w10, [x0]
	cmp	w10, 50
	beq	.L974
	ldrb	w10, [x0,1]
	cmp	w10, 5
	beq	.L974
	add	w7, w7, 1
	add	x0, x0, 4
	uxtb	w7, w7
	cmp	w7, 8
	bne	.L975
	b	.L976
.L974:
	cmp	w7, 6
	bls	.L977
.L976:
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L978:
	b	.L978
.L973:
	cmp	w27, 7
	bne	.L979
	mov	w7, w8
.L981:
	ldrb	w10, [x0]
	cmp	w10, 12
	beq	.L980
	ldrb	w10, [x0,1]
	cmp	w10, 10
	beq	.L980
	add	w7, w7, 1
	add	x0, x0, 4
	uxtb	w7, w7
	cmp	w7, 8
	bne	.L981
	b	.L982
.L980:
	cmp	w7, 7
	bne	.L977
.L982:
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L983:
	b	.L983
.L979:
	cmp	w27, 6
	bne	.L977
	mov	x7, 0
.L984:
	ldrb	w10, [x0,x7]
	cmp	w10, 12
	beq	.L977
	add	x10, x0, x7
	ldrb	w10, [x10,8]
	cmp	w10, 4
	beq	.L977
	add	x7, x7, 1
	cmp	x7, 8
	bne	.L984
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L986:
	b	.L986
.L977:
	add	x0, x1, :lo12:.LANCHOR4
	ldr	x11, [x0,1592]
	mov	x0, 0
.L987:
	ldr	w2, [x29,128]
	cmp	w2, w0
	ble	.L1055
	ldr	w7, [x24,2048]
	strb	w7, [x11,x0]
	add	x0, x0, 1
	b	.L987
.L1055:
	add	x0, x1, :lo12:.LANCHOR4
	mov	w12, w23
	mov	w10, 8
	ldr	x13, [x0,1592]
.L990:
	mov	w0, 0
.L989:
	add	w7, w0, w12
	add	w0, w0, 1
	sbfiz	x7, x7, 1, 32
	cmp	w0, w23
	ldrh	w14, [x13,x7]
	mvn	w14, w14
	strh	w14, [x13,x7]
	bne	.L989
	ldr	w0, [x29,124]
	subs	w10, w10, #1
	add	w12, w12, w0
	bne	.L990
	mov	x10, 0
	mov	w18, 1
.L991:
	mov	w7, 0
	mov	w12, w7
.L995:
	lsl	w14, w18, w12
	mov	w16, w10
	mov	w0, 16
	mov	w15, 0
.L993:
	ldrh	w17, [x13,w16,sxtw 1]
	add	w16, w16, w23
	and	w17, w17, w14
	cmp	w17, w14
	csinc	w15, w15, w15, ne
	subs	w0, w0, #1
	bne	.L993
	cmp	w15, 8
	bls	.L994
	orr	w7, w7, w14
	uxth	w7, w7
.L994:
	add	w12, w12, 1
	cmp	w12, 16
	bne	.L995
	strh	w7, [x13,x10,lsl 1]
	add	x10, x10, 1
	cmp	w23, w10
	bgt	.L991
	add	x1, x1, :lo12:.LANCHOR4
	mov	x7, 0
	mov	w10, w7
	ldr	x1, [x1,1592]
.L998:
	ldr	w12, [x1,x7]
	add	x7, x7, 4
	cmp	w12, wzr
	csinc	w10, w10, w10, ne
	cmp	x7, 32
	bne	.L998
	cmp	w10, 7
	ble	.L999
	adrp	x0, .LC85
	mov	w2, 1
	add	x0, x0, :lo12:.LC85
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L1000:
	b	.L1000
.L999:
	cmp	w27, 6
	mov	w7, 4
	beq	.L1001
	cmp	w27, 7
	mov	w7, 10
	beq	.L1001
	cmp	w8, wzr
	mov	w7, 5
	mov	w1, 8
	csel	w7, w1, w7, eq
.L1001:
	mov	w10, 0
.L1002:
	mov	x1, 0
.L1003:
	add	w12, w0, w1
	ldrb	w13, [x11,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w13, [x6,w12,sxtw]
	bhi	.L1003
	add	w10, w10, 1
	ldr	x1, [x29,112]
	cmp	w10, w26
	add	w0, w0, w7
	add	x11, x11, x1
	blt	.L1002
	mov	w0, 255
	str	w0, [x24,2056]
	mov	w0, w22
	str	x8, [x29,96]
	str	x9, [x29,104]
	bl	NandcWaitFlashReady
	ldr	x9, [x29,104]
	ldr	x8, [x29,96]
	cbz	w9, .L1005
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
	b	.L1006
.L1005:
	mov	w0, 190
	cbnz	w8, .L1054
	mov	w0, 56
.L1054:
	str	w0, [x24,2056]
.L1006:
	mov	w0, w22
	add	w21, w21, 1
	bl	NandcWaitFlashReady
	uxtb	w21, w21
	b	.L962
.L965:
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
	b	.L1009
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L1056
	sub	w2, w0, #1
	cmp	w2, 7
	bhi	.L1058
	bl	HynixGetReadRetryDefault
	b	.L1056
.L1058:
	cmp	w0, 49
	bne	.L1059
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
	b	.L1074
.L1059:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L1065
	cmp	w1, 33
	bne	.L1060
.L1065:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,1272]
	mov	w1, 4
	b	.L1075
.L1060:
	cmp	w1, 67
	beq	.L1066
	cmp	w1, 34
	bne	.L1062
.L1066:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,1272]
	mov	w1, 5
.L1075:
	strb	w1, [x0,1273]
	mov	w1, 7
	strb	w1, [x0,1274]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 1276
	add	x1, x1, 256
	mov	w2, 45
	b	.L1074
.L1062:
	cmp	w1, 68
	beq	.L1067
	cmp	w1, 35
	bne	.L1056
.L1067:
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
.L1074:
	bl	ftl_memcpy
.L1056:
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
	ldr	w0, [x1,2380]
	cmp	w0, 3
	bls	.L1107
	ldr	w0, [x19,16]
	tbz	x0, 2, .L1107
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L1078
	adrp	x22, .LC86
	adrp	x23, .LC87
	mov	w21, 0
	add	x22, x22, :lo12:.LC86
	add	x23, x23, :lo12:.LC87
.L1079:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1087
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,2380]
	cmp	w0, 5
	bhi	.L1080
.L1083:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L1082
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
	b	.L1082
.L1080:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1083
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1083
.L1087:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 2400
	ldr	w0, [x19,40]
	cbz	w0, .L1088
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
	b	.L1088
.L1082:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L1079
.L1078:
	adrp	x22, .LC88
	adrp	x23, .LC87
	add	x22, x22, :lo12:.LC88
	add	x23, x23, :lo12:.LC87
.L1089:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L1115
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L1090
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
.L1090:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L1089
.L1115:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,2448]
	cbz	w0, .L1092
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L1092:
	add	x21, x20, :lo12:.LANCHOR0
	add	x21, x21, 2400
	ldr	w0, [x21,40]
	cbz	w0, .L1093
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
.L1093:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,2448]
	cbz	w0, .L1088
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L1088:
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x20,2440]
	b	.L1076
.L1107:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1107
.L1076:
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
	cbnz	x0, .L1117
	cbnz	x4, .L1118
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x22, x29, 112
	bl	ftl_memset
.L1118:
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
	cbnz	w21, .L1119
	add	x1, x19, :lo12:.LANCHOR0
	ubfx	x2, x24, 1, 7
	mov	w3, 128
	add	x2, x22, x2, lsl 2
	ldr	w0, [x1,2452]
	cmp	w0, 25
	mov	w0, 64
	csel	w3, w0, w3, cc
	mov	w0, w21
.L1121:
	cmp	x22, x2
	add	w4, w0, w3
	beq	.L1163
	ldr	x5, [x1,2408]
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
	b	.L1121
.L1163:
	add	x0, x19, :lo12:.LANCHOR0
	lsr	w24, w24, 2
	ldr	w5, [x0,2452]
	ldr	w4, [x0,2380]
	mov	w0, 0
	mov	w23, w0
.L1123:
	cmp	w0, w24
	bcs	.L1119
	cbz	w5, .L1119
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x20,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L1147
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L1147
	cmp	w4, 5
	bls	.L1125
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
	bls	.L1126
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L1127
.L1126:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L1162
.L1125:
	cmp	w4, 3
	bls	.L1127
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
	bls	.L1128
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L1162
.L1128:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L1162:
	orr	w3, w1, w3, lsl 5
.L1127:
	cmp	w23, w3
	csel	w23, w23, w3, cs
	b	.L1124
.L1147:
	mov	w23, -1
.L1124:
	add	w0, w0, 1
	b	.L1123
.L1119:
	str	wzr, [x20,16]
	b	.L1130
.L1117:
	cmp	w21, 1
	bne	.L1161
	mov	w23, 0
	mov	w28, 2
.L1131:
	cmp	w23, w24
	bcs	.L1164
	and	w27, w23, 3
	mov	x3, x26
	cbz	x26, .L1133
	ubfiz	x0, x23, 9, 23
	add	x3, x26, x0
.L1133:
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
	b	.L1131
.L1164:
	mov	w23, 0
	b	.L1130
.L1161:
	mov	w1, 0
	mov	x4, 0
	mov	w27, 0
	mov	w0, w25
	mov	w2, 2
	mov	w3, w1
	mov	x5, x4
	mov	w23, w27
	bl	NandcXferStart
.L1136:
	cmp	w27, w24
	bcs	.L1130
	mov	w0, w25
	mov	w1, w21
	add	w28, w27, 2
	bl	NandcXferComp
	ldr	w0, [x20,32]
	cmp	w28, w24
	str	w0, [x29,104]
	bcs	.L1137
	mov	x4, 0
	mov	w0, w25
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L1137:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L1152
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w23, w0
	csel	w23, w23, w0, cs
	b	.L1138
.L1152:
	mov	w23, -1
.L1138:
	and	w2, w27, 3
	mov	x3, x26
	cbz	x26, .L1139
	ubfiz	x3, x27, 9, 23
	add	x3, x26, x3
.L1139:
	cmp	x22, xzr
	mov	w0, 2
	csel	w4, w0, wzr, ne
	mov	w1, 0
	mov	x0, x20
	mul	w4, w4, w27
	mov	w27, w28
	add	x4, x22, x4
	bl	NandcCopy1KB
	b	.L1136
.L1130:
	cbnz	w21, .L1142
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19,2380]
	cmp	w0, 5
	bls	.L1142
	ldr	w0, [x20]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L1142
	orr	w0, w0, 131072
	mov	w23, -1
	str	w0, [x20]
.L1142:
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
	cbnz	w19, .L1166
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,73]
	ldr	w0, [x0,76]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L1166:
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
	ldr	x0, [x19,128]
	mov	w24, w1
	mov	x28, x2
	mov	x27, x3
	mov	w25, w4
	mov	w21, 1024
	ldr	w0, [x0,304]
	str	w0, [x29,124]
	ldr	w0, [x19,2380]
	cmp	w0, 8
	mov	w0, 12
	csel	w22, w22, w0, cc
	cbz	w4, .L1169
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
	ldrb	w0, [x19,2356]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,2356]
	bl	NandcSetMode
	cmn	w21, #1
	bne	.L1170
.L1179:
	mov	w21, -1
	b	.L1171
.L1170:
	adrp	x0, .LC89
	mov	w1, w24
	add	x0, x0, :lo12:.LC89
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L1172
	add	x19, x19, x23, sxtw 4
	ldr	x0, [x19,8]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L1172:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,1600]
	add	w1, w1, 1
	str	w1, [x0,1600]
	cmp	w1, 2047
	bls	.L1171
	mov	x27, 0
	str	wzr, [x0,1600]
	mov	x28, x27
.L1169:
	mov	w19, 0
	mov	w26, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L1177:
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
	bhi	.L1173
	cmp	w0, 2
	bhi	.L1183
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L1183
	sub	w19, w22, w20
	mov	w21, w0
	mov	w26, 0
	b	.L1175
.L1173:
	cmp	w6, w20
	bcs	.L1184
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L1185
	mov	w6, w20
	b	.L1184
.L1183:
	mov	x27, 0
	mov	w19, w22
	mov	w21, w0
	mov	w26, 0
	mov	x28, x27
	b	.L1174
.L1184:
	mov	w20, 0
.L1174:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L1177
.L1175:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L1176
.L1185:
	mov	w19, w5
.L1176:
	cbz	w19, .L1178
	adrp	x0, .LC90
	mov	w1, w19
	add	x0, x0, :lo12:.LC90
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L1178:
	cbz	w26, .L1171
	adrp	x0, .LC91
	mov	w1, w23
	add	x0, x0, :lo12:.LC91
	mov	w2, w24
	bl	printk
	cbz	w25, .L1179
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L1171:
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
	bne	.L1195
	adrp	x21, .LANCHOR0
	add	x25, x21, :lo12:.LANCHOR0
	ldrb	w26, [x25,80]
	cbnz	w26, .L1196
.L1198:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,2372]
	cbz	w0, .L1195
	b	.L1216
.L1196:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	strb	wzr, [x25,80]
	bl	FlashReadRawPage
	strb	w26, [x25,80]
	cmn	w0, #1
	beq	.L1198
	mov	w19, w0
	b	.L1195
.L1216:
	ldr	x0, [x21,128]
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w4, 1
	ldr	w25, [x0,304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w19, w0
	beq	.L1199
	ldrb	w0, [x21,2456]
	cmp	w19, w0, lsr 1
	bls	.L1195
.L1199:
	lsr	w0, w25, 8
	bl	NandcSetDdrPara
.L1195:
	adrp	x21, .LANCHOR4
	cmn	w19, #1
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x4, [x21,1608]
	bne	.L1200
	cbz	x4, .L1200
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
	bne	.L1200
	adrp	x0, .LANCHOR0+196
	ldrb	w0, [x0,#:lo12:.LANCHOR0+196]
	cbz	w0, .L1200
	mov	w0, w20
	bl	flash_enter_slc_mode
	ldr	x4, [x21,1608]
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	blr	x4
	mov	w19, w0
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1200:
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
	ldrb	w0, [x22,2356]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,2356]
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
	beq	.L1221
	cmn	w22, #1
	bne	.L1218
.L1221:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,2356]
	tbz	x0, 0, .L1218
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,2372]
	b	.L1220
.L1218:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,2372]
.L1220:
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
	ldr	x1, [x0,1592]
	add	x21, x22, :lo12:.LANCHOR0
	str	x1, [x0,1616]
	mov	w20, 0
	str	wzr, [x0,1624]
	mov	w25, 4
	mov	w0, 0
	mov	w28, -1
	add	x24, x24, 472
	add	x27, x21, 88
	ldr	w26, [x21,76]
	bl	flash_enter_slc_mode
.L1229:
	add	w0, w20, 1
	mov	x19, 0
	str	w0, [x29,104]
.L1231:
	add	x0, x29, 120
	ldrb	w0, [x19,x0]
	bl	FlashBchSel
	add	x5, x23, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w1, w20
	mov	x3, 0
	str	x5, [x29,96]
	ldr	x2, [x5,1592]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1230
	ldr	x5, [x29,96]
	mov	w0, 0
	ldr	w1, [x29,104]
	mov	x3, 0
	ldr	x2, [x5,1592]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1230
	add	x19, x19, 1
	cmp	x19, 4
	beq	.L1232
	b	.L1231
.L1233:
	add	x0, x2, 12
	mov	w1, 2036
	str	x2, [x29,104]
	mov	w28, -1
	bl	js_hash
	ldr	x2, [x29,104]
	ldr	w1, [x2,8]
	cmp	w1, w0
	beq	.L1243
.L1232:
	subs	w25, w25, #1
	add	w20, w20, w26
	bne	.L1229
	mov	w0, w25
.L1242:
	bl	flash_exit_slc_mode
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
.L1230:
	add	x19, x23, :lo12:.LANCHOR4
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x2, [x19,1616]
	ldr	w1, [x2]
	cmp	w1, w0
	bne	.L1232
	cbnz	w28, .L1233
	add	x22, x22, :lo12:.LANCHOR0
	ldr	w0, [x22,76]
	udiv	w20, w20, w0
	mov	w0, w28
	add	w20, w20, 1
	str	w20, [x19,1628]
	b	.L1242
.L1243:
	add	x1, x2, 160
	mov	x0, x24
	mov	w2, 32
	bl	ftl_memcpy
	ldr	x1, [x19,1616]
	mov	w2, 32
	mov	x0, x27
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x19,1616]
	mov	w2, 852
	add	x0, x21, 1272
	add	x1, x1, 224
	bl	ftl_memcpy
	ldrh	w0, [x24,10]
	bl	FlashBlockAlignInit
	str	w20, [x19,1624]
	ldr	w1, [x21,76]
	ldr	x2, [x19,1616]
	udiv	w1, w20, w1
	ldr	w0, [x2,1076]
	add	w1, w1, 1
	strb	w0, [x21,2372]
	cmp	w1, 1
	bls	.L1235
	str	w1, [x19,1628]
	b	.L1236
.L1235:
	mov	w0, 2
	str	w0, [x19,1628]
.L1236:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w1, [x2,14]
	mov	w28, 0
	strb	w1, [x0,1632]
	b	.L1232
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
	ldrb	w0, [x2,120]
	add	x20, x24, x20, lsl 8
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1245
	ldrb	w0, [x2,2372]
	cbz	w0, .L1246
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L1246:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L1245:
	ldr	x0, [x29,120]
	mov	w21, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x28, 8, 8
	str	x0, [x29,96]
.L1247:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1633]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L1272
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w21
	ldrb	w0, [x0,120]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1248
	bl	SandiskSetRRPara
	b	.L1249
.L1248:
	bl	ToshibaSetRRPara
.L1249:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cmp	w0, 34
	bne	.L1250
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1633]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L1250
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L1250:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L1251
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
	b	.L1252
.L1251:
	ldr	w1, [x29,116]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L1252:
	cmn	w28, #1
	beq	.L1253
	add	x0, x19, :lo12:.LANCHOR0
	cmn	w25, #1
	csel	w25, w25, w28, ne
	ldrb	w0, [x0,2456]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1255
	mov	x26, 0
	mov	x27, x26
.L1253:
	add	w21, w21, 1
	b	.L1247
.L1272:
	mov	w28, w25
.L1255:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,120]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1257
	bl	SandiskSetRRPara
	b	.L1258
.L1257:
	bl	ToshibaSetRRPara
.L1258:
	ldr	x0, [x29,120]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	ldrb	w0, [x19,2456]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1259
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1259:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L1260
	mov	w0, 4
	bl	NandcSetDdrMode
.L1260:
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
.L1274:
	add	x0, x27, :lo12:.LANCHOR4
	ldrb	w0, [x0,1633]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L1277
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1275
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,2456]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1280
	mov	x23, 0
	mov	x24, x23
.L1275:
	add	w26, w26, 1
	b	.L1274
.L1280:
	mov	w19, w0
.L1277:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,2456]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1279
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1279:
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
	ldrb	w19, [x0,2456]
	mov	x22, x3
	ldrb	w0, [x0,196]
	cbnz	w0, .L1287
	add	w19, w19, w19, lsl 1
	ubfx	x19, x19, 2, 8
	b	.L1288
.L1287:
	mov	w0, 3
	sdiv	w19, w19, w0
.L1288:
	adrp	x27, .LC93
	mov	w25, 0
	add	x0, x27, :lo12:.LC93
	str	x0, [x29,152]
	add	x20, x20, :lo12:.LANCHOR0
	add	x23, x20, 8
	add	x23, x23, x21, sxtw 4
.L1299:
	mov	w0, w21
	mov	w24, 0
	mov	w27, -1
	bl	NandcWaitFlashReady
	ldrb	w4, [x23,8]
	mov	w9, 239
	ldr	x6, [x23]
	mov	w10, 137
	lsl	x8, x4, 8
.L1289:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1633]
	cmp	w24, w0
	bcs	.L1292
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
	beq	.L1290
	cmn	w27, #1
	csel	w27, w27, w0, ne
	cmp	w0, w19
	bcc	.L1301
	mov	x22, 0
	mov	x26, x22
.L1290:
	mov	w24, w7
	b	.L1289
.L1301:
	mov	x22, 0
	mov	w27, w0
	mov	x26, x22
.L1292:
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
	bcc	.L1294
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
.L1294:
	cmn	w27, #1
	cset	w6, eq
	cbnz	w6, .L1303
	cmp	w27, 256
	cset	w1, eq
	cbz	w1, .L1295
.L1303:
	ldr	x0, [x29,152]
	mov	w1, w24
	mov	w2, w28
	mov	w3, w24
	mov	w4, w27
	str	x6, [x29,144]
	bl	printk
	cbnz	w25, .L1297
	ldr	x6, [x29,144]
	cbz	w6, .L1308
	ldrb	w0, [x20,196]
	cbz	w0, .L1308
	mov	w0, w21
	mov	w1, 3
	mov	w25, 1
	bl	micron_auto_read_calibration_config
	b	.L1299
.L1297:
	mov	w0, w21
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
	b	.L1308
.L1295:
	cbz	w25, .L1308
	mov	w0, w21
	mov	w27, 256
	bl	micron_auto_read_calibration_config
.L1308:
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
	ldr	x0, [x0,1256]
	add	x1, x2, x22
	mov	x23, x22
	mov	x24, x3
	ldrb	w0, [x0,19]
	ldrb	w19, [x1,12]
	sub	w0, w0, #7
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L1317
	ldrb	w19, [x1,20]
.L1317:
	mov	w0, w23
	mov	w28, 0
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	add	x5, x4, 1276
.L1318:
	cmp	w28, w25
	bcs	.L1322
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
	beq	.L1320
	ldrb	w1, [x4,2456]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1327
	mov	x24, 0
	mov	x26, x24
.L1320:
	add	w28, w28, 1
	b	.L1318
.L1327:
	mov	w20, w0
.L1322:
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 1272
	ldr	x1, [x0,-16]
	add	x0, x0, x22
	ldrb	w1, [x1,19]
	sub	w1, w1, #7
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L1324
	strb	w19, [x0,20]
	b	.L1325
.L1324:
	strb	w19, [x0,12]
.L1325:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,2456]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1326
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1326:
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
	cbnz	w19, .L1334
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,73]
	ldr	w2, [x0,76]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L1334
	ldrb	w0, [x0,72]
	cmp	w0, wzr
	mov	w0, 4
	csel	w21, w21, w0, eq
.L1334:
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
	adrp	x24, .LANCHOR0
	stp	x19, x20, [sp,16]
	add	x19, x23, :lo12:.LANCHOR4
	add	x20, x24, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w21, 0
	ldr	x0, [x19,1592]
	str	x0, [x19,1616]
	ldrb	w0, [x19,1634]
	bl	FlashBchSel
	ldr	x0, [x19,1592]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19,1616]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	mov	w2, 32
	str	w0, [x1]
	ldr	x0, [x19,1616]
	ldrb	w1, [x20,2357]
	add	x0, x0, 16
	strh	w1, [x0,-4]
	ldrb	w1, [x20,73]
	strh	w1, [x0,-2]
	ldrb	w1, [x20,2372]
	str	w1, [x0,1060]
	add	x1, x20, 2132
	bl	ftl_memcpy
	ldr	x0, [x19,1616]
	add	x1, x20, 2360
	mov	w2, 8
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x19,1616]
	add	x1, x20, 1224
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x19,1616]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 472
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x19,1616]
	add	x1, x20, 88
	mov	w2, 32
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x19,1616]
	add	x1, x20, 1272
	mov	w2, 852
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x20, [x19,1616]
	mov	w1, 2036
	add	x0, x20, 12
	bl	js_hash
	str	w0, [x20,8]
	mov	w0, 1592
	str	w0, [x20,4]
	ldr	x0, [x19,1640]
	str	x0, [x19,1616]
	mov	w0, 0
	mov	w19, w21
	bl	flash_enter_slc_mode
.L1339:
	add	x22, x24, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w0, 0
	add	x20, x23, :lo12:.LANCHOR4
	add	w26, w19, 1
	ldr	w1, [x22,76]
	mul	w1, w19, w1
	bl	FlashEraseBlock
	ldr	w1, [x22,76]
	mov	x3, 0
	ldr	x2, [x20,1592]
	mov	w0, 0
	mul	w1, w19, w1
	bl	FlashProgPage
	ldr	w1, [x22,76]
	mov	x3, 0
	ldr	x2, [x20,1592]
	mov	w0, 0
	mul	w1, w19, w1
	add	w1, w1, 1
	bl	FlashProgPage
	ldr	w1, [x22,76]
	mov	w0, 0
	ldr	x2, [x20,1640]
	mov	x3, 0
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1337
	ldr	x25, [x20,1616]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L1337
	mov	w1, 2036
	add	x0, x25, 12
	bl	js_hash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L1337
	ldr	w0, [x22,76]
	cmp	w21, 1
	str	w26, [x20,1628]
	mul	w19, w19, w0
	str	w19, [x20,1624]
	beq	.L1340
	mov	w21, 1
.L1337:
	cmp	w26, 4
	mov	w19, w26
	bne	.L1339
	b	.L1338
.L1340:
	mov	w21, 2
.L1338:
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
	ldrb	w27, [x0,2456]
	ldr	w0, [x0,2368]
	cbz	w0, .L1346
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1346:
	mov	x0, x26
	mov	w1, 0
	mov	w2, 2048
	adrp	x24, .LC94
	mov	w23, -1
	mov	w20, 2
	bl	ftl_memset
	adrp	x28, .LANCHOR4
	add	x0, x24, :lo12:.LC94
	str	x0, [x29,104]
.L1347:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,73]
	cmp	w20, w0
	bcs	.L1351
	mov	x22, 0
.L1349:
	add	x0, x29, 120
	add	x24, x19, :lo12:.LANCHOR0
	add	x21, x28, :lo12:.LANCHOR4
	ldrb	w25, [x22,x0]
	mov	w0, w25
	bl	FlashBchSel
	ldr	w1, [x24,76]
	mov	w0, 0
	ldr	x2, [x21,1592]
	mov	x3, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1348
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L1349
	b	.L1350
.L1348:
	ldr	x0, [x21,1592]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L1350
	ldr	x0, [x29,104]
	mov	w1, w25
	bl	printk
	ldr	x1, [x21,1592]
	mov	x0, x26
	mov	w2, 2048
	bl	ftl_memcpy
	ldr	x0, [x21,1592]
	ldr	w0, [x0,512]
	strb	w0, [x24,73]
	ldr	w0, [x21,1628]
	cmp	w0, w20
	bls	.L1354
	mov	w23, 0
	str	w20, [x21,1628]
	bl	FlashSavePhyInfo
.L1350:
	add	w20, w20, 1
	b	.L1347
.L1354:
	mov	w23, 0
.L1351:
	mov	w0, w27
	add	x19, x19, :lo12:.LANCHOR0
	bl	FlashBchSel
	ldr	w0, [x19,2368]
	cbz	w0, .L1358
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1358:
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
	str	x0, [x22,1592]
	mov	w0, 32768
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x24, .LC95
	bl	ftl_malloc
	str	x0, [x22,1640]
	mov	w0, 4096
	mov	w27, 144
	mov	w28, 239
	bl	ftl_malloc
	str	x0, [x22,1648]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x22,1656]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x22,1664]
	mov	w0, 128
	str	w0, [x20,76]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x22,1634]
	mov	x0, x23
	strb	wzr, [x20,2372]
	add	x23, x20, 8
	strb	w1, [x20,73]
	strb	w1, [x22,1632]
	str	wzr, [x22,1628]
	str	wzr, [x22,1600]
	strb	wzr, [x20,72]
	add	x20, x20, 2132
	strb	wzr, [x22,1672]
	bl	NandcInit
	add	x0, x24, :lo12:.LC95
	mov	w22, 0
	str	x0, [x29,104]
	mov	x26, x20
.L1370:
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
	bne	.L1365
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
.L1365:
	mov	w0, w25
	bl	NandcFlashDeCs
	ldrb	w2, [x20]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1366
	ldr	x0, [x29,104]
	add	w1, w22, 1
	ldrb	w3, [x20,1]
	ldrb	w4, [x20,2]
	ldrb	w5, [x20,3]
	ldrb	w6, [x20,4]
	ldrb	w7, [x20,5]
	bl	printk
.L1366:
	cbnz	w22, .L1367
	ldrb	w0, [x26]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1421
	ldrb	w0, [x26,1]
	cmp	w0, 255
	beq	.L1421
.L1367:
	ldrb	w0, [x20]
	cmp	w0, 181
	bne	.L1369
	mov	w0, 44
	strb	w0, [x20]
.L1369:
	add	w22, w22, 1
	add	x23, x23, 16
	cmp	w22, 4
	add	x20, x20, 8
	bne	.L1370
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,2132]
	cmp	w1, 173
	beq	.L1371
	ldr	w0, [x0,2376]
	bl	NandcSetDdrMode
.L1371:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 0
	add	x0, x20, 1272
	mov	w2, 852
	bl	ftl_memset
	strb	wzr, [x20,80]
	adrp	x1, .LANCHOR1
	ldr	w4, [x20,2384]
	add	x0, x1, :lo12:.LANCHOR1
	add	x0, x0, 472
	str	x0, [x20,1256]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w4, w0
	bne	.L1372
	mov	w0, 1
	str	w0, [x20,2368]
.L1372:
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
	cbnz	w2, .L1373
	and	w2, w0, -3
	cmp	w2, 209
	beq	.L1373
	cmp	w0, 220
	bne	.L1374
	ldrb	w2, [x3,3]
	cmp	w2, 149
	bne	.L1374
.L1373:
	add	x2, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR4
	mov	w7, 16
	mov	w5, 1
	add	x8, x2, 2132
	strb	w7, [x2,73]
	strb	w5, [x2,72]
	strb	w7, [x3,1634]
	ldrb	w7, [x2,2132]
	add	x2, x1, :lo12:.LANCHOR1
	cmp	w7, 152
	strb	w7, [x2,3329]
	strb	w0, [x2,3330]
	bne	.L1376
	ldrsb	w2, [x8,4]
	tbnz	w2, #31, .L1377
	mov	w2, 24
	strb	w2, [x3,1634]
.L1376:
	cmp	w4, 2049
	beq	.L1424
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L1379
.L1424:
	add	x2, x21, :lo12:.LANCHOR4
	mov	w3, 16
	strb	w3, [x2,1634]
.L1379:
	cbz	w6, .L1381
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,3342]
	mov	w2, -38
	b	.L1465
.L1381:
	cmp	w0, 220
	bne	.L1383
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3342]
	mov	w2, -36
.L1465:
	strb	w2, [x0,3330]
	b	.L1382
.L1383:
	cmp	w0, 211
	bne	.L1382
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3342]
	mov	w2, 2
	strb	w2, [x0,3341]
.L1382:
	add	x20, x1, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x20, 3200
	mov	w2, 32
	add	x0, x0, 88
	bl	ftl_memcpy
	add	x0, x20, 472
	add	x1, x20, 3328
	mov	w2, 32
	bl	ftl_memcpy
.L1374:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,72]
	cbnz	w0, .L1384
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L1386
	ldr	x0, [x20,1256]
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x20,2356]
	tbnz	x1, 0, .L1386
	mov	w1, 1
	strb	w1, [x20,2372]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,2356]
	bl	NandcSetMode
.L1386:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,1256]
	ldrb	w0, [x0,26]
	strb	w0, [x20,196]
	bl	FlashLoadPhyInfo
	cbz	w0, .L1384
	ldr	w0, [x20,2376]
	cbz	w0, .L1389
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L1462
.L1389:
	ldrb	w0, [x20,2356]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,2356]
.L1462:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L1384
	mov	w0, 1
	add	x20, x19, :lo12:.LANCHOR0
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x1, [x20,1256]
	adrp	x0, .LC96
	add	x0, x0, :lo12:.LC96
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L1438
	bl	FlashDieInfoInit
	ldr	x0, [x20,1256]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	adrp	x1, .LANCHOR2+3550
	ldr	x0, [x20,1256]
	ldrh	w1, [x1,#:lo12:.LANCHOR2+3550]
	ldrb	w2, [x0,9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L1391
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L1392
.L1391:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L1392:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2356]
	tst	w0, 6
	beq	.L1393
	bl	FlashSavePhyInfo
	add	x1, x21, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x1,1624]
	bl	FlashDdrParaScan
.L1393:
	bl	FlashSavePhyInfo
.L1384:
	add	x20, x19, :lo12:.LANCHOR0
	add	x22, x21, :lo12:.LANCHOR4
	ldr	x2, [x20,1256]
	str	xzr, [x22,1608]
	ldrb	w0, [x2,26]
	strb	w0, [x20,196]
	ldrh	w0, [x2,16]
	ldrh	w3, [x2,10]
	ubfx	x1, x0, 7, 1
	strb	w1, [x20,80]
	ubfx	x1, x0, 3, 1
	strb	w1, [x22,1673]
	ubfx	x1, x0, 4, 1
	strb	w1, [x20,2457]
	ldrb	w1, [x2,12]
	ubfx	x0, x0, 8, 3
	strb	w0, [x20,2356]
	ldrb	w0, [x2,18]
	sdiv	w1, w3, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x20,1256]
	ldrh	w1, [x0,16]
	tbz	x1, 6, .L1395
	ldrb	w0, [x0,19]
	ldrb	w1, [x20,1274]
	strb	w1, [x22,1633]
	sub	w1, w0, #1
	ldrb	w2, [x20,1273]
	uxtb	w1, w1
	strb	w0, [x20,120]
	strb	w2, [x20,121]
	cmp	w1, 7
	bhi	.L1396
	adrp	x1, HynixReadRetrial
	cmp	w0, 8
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,1608]
	sub	w1, w0, #5
	cset	w2, eq
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1425
	cbz	w2, .L1397
.L1425:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 1
	str	w3, [x1,2448]
.L1397:
	cmp	w0, 7
	add	x1, x19, :lo12:.LANCHOR0
	beq	.L1423
	cbnz	w2, .L1423
	add	x1, x1, 1292
	b	.L1400
.L1423:
	add	x1, x1, 1300
.L1400:
	mov	x2, 0
	mov	w3, w2
.L1402:
	ldrsb	w4, [x1,x2]
	add	x2, x2, 1
	cmp	w4, wzr
	csinc	w3, w3, w3, ne
	cmp	x2, 32
	bne	.L1402
	cmp	w3, 27
	bls	.L1395
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1395
.L1396:
	sub	w1, w0, #17
	uxtb	w1, w1
	cmp	w1, 2
	bhi	.L1404
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x22,1608]
	beq	.L1405
	mov	w0, 7
	b	.L1463
.L1405:
	mov	w0, 15
.L1463:
	strb	w0, [x22,1633]
	b	.L1395
.L1404:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1426
	cmp	w0, 33
	bne	.L1406
.L1426:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	mov	w2, 4
	str	x1, [x0,1608]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,121]
	mov	w1, 7
	strb	w1, [x0,1633]
	b	.L1395
.L1406:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w2, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1427
	cbz	w2, .L1408
.L1427:
	add	x3, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x1, [x3,1608]
	beq	.L1410
	cmp	w0, 68
	beq	.L1410
	mov	w0, 7
	strb	w0, [x3,1633]
	b	.L1411
.L1410:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,1633]
.L1411:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w2, .L1464
	mov	w1, 5
.L1464:
	strb	w1, [x0,121]
	b	.L1395
.L1408:
	cmp	w0, 49
	bne	.L1395
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,1608]
.L1395:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ldr	w2, [x0,2384]
	cmp	w2, w1
	bne	.L1413
	ldrb	w1, [x0,196]
	cbz	w1, .L1413
	ldr	x0, [x0,1256]
	strb	wzr, [x0,18]
.L1413:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,2132]
	cmp	w1, 44
	bne	.L1414
	ldrb	w1, [x0,2372]
	cbz	w1, .L1414
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L1415
	ldrb	w0, [x0,196]
	cbnz	w0, .L1414
.L1415:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,2372]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L1414:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w0, [x1,2356]
	tst	w0, 6
	beq	.L1416
	ldrb	w1, [x1,2372]
	cbnz	w1, .L1417
	tbnz	x0, 0, .L1416
.L1417:
	mov	w0, 0
	add	x21, x21, :lo12:.LANCHOR4
	bl	flash_enter_slc_mode
	ldr	w1, [x21,1624]
	mov	w0, 0
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1416:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 16
	ldr	x0, [x19,1256]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 3888
	bl	FlashReadIdbDataRaw
	strb	w20, [x19,73]
	mov	w0, 18928
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	ldr	x1, [x19,1256]
	ldrb	w2, [x1,12]
	strh	w2, [x19,176]
	ldrh	w4, [x1,14]
	ldrb	w2, [x1,7]
	str	w2, [x19,172]
	ldr	w2, [x19,2132]
	str	w2, [x19,168]
	ldrb	w2, [x19,2357]
	strh	w2, [x19,178]
	ldrb	w2, [x1,13]
	strh	w2, [x19,180]
	strh	w4, [x19,182]
	ldrh	w2, [x1,10]
	strh	w2, [x19,184]
	ldrb	w3, [x1,12]
	ldrh	w2, [x1,10]
	sdiv	w2, w2, w3
	strh	w2, [x19,186]
	ldrb	w3, [x1,9]
	strh	w3, [x19,188]
	ldrh	w5, [x1,10]
	ldrb	w2, [x1,9]
	mul	w2, w2, w5
	mov	w5, 512
	strh	w5, [x19,192]
	ldrb	w5, [x19,73]
	strh	w5, [x19,194]
	uxth	w2, w2
	ldrb	w5, [x19,72]
	strh	w2, [x19,190]
	cmp	w5, 1
	bne	.L1419
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,190]
	strb	w20, [x19,73]
	mov	w2, 8
	strh	w4, [x19,182]
	strh	w3, [x19,188]
	strh	w2, [x19,194]
.L1419:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
	b	.L1438
.L1421:
	mov	w0, -2
	b	.L1438
.L1377:
	strb	w5, [x3,1672]
	b	.L1376
.L1438:
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
	ldr	x3, [x0,1256]
	ldrb	w4, [x0,196]
	ldrb	w3, [x3,19]
	cbz	w4, .L1467
	ldr	w0, [x0,2368]
	cbnz	w0, .L1466
.L1467:
	sub	w2, w3, #5
	uxtb	w2, w2
	cmp	w2, 2
	bls	.L1468
	cmp	w3, 68
	beq	.L1468
	sub	w3, w3, #19
	and	w3, w3, -17
	uxtb	w3, w3
	cbnz	w3, .L1466
.L1468:
	adrp	x20, .LANCHOR2
	mov	w24, w1
	add	x20, x20, :lo12:.LANCHOR2
	mov	w25, 65535
	add	x20, x20, 1500
	adrp	x26, .LANCHOR4
.L1470:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,1256]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L1466
	ldrh	w0, [x20,w19,sxtw 1]
	cmp	w0, w25
	bne	.L1466
	add	x22, x26, :lo12:.LANCHOR4
	mov	w1, 255
	mov	w2, 32768
	ldr	x0, [x22,1640]
	bl	ftl_memset
	ldr	x2, [x22,1640]
	add	w1, w19, w24
	mov	w0, w21
	add	w19, w19, 1
	mov	x3, x2
	uxth	w19, w19
	bl	FlashProgPage
	b	.L1470
.L1466:
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
	adrp	x0, .LC97
	str	w2, [x29,116]
	add	x0, x0, :lo12:.LC97
	str	x0, [x29,104]
	stp	x25, x26, [sp,64]
	adrp	x0, .LC99
	ldrb	w25, [x1,#:lo12:.LANCHOR1+481]
	stp	x21, x22, [sp,32]
	add	x0, x0, :lo12:.LC99
	mov	w22, 0
	stp	x27, x28, [sp,80]
	str	x0, [x29,96]
.L1479:
	cmp	w22, w24
	beq	.L1543
	ldr	w1, [x29,116]
	sub	w4, w24, w22
	mov	x0, x19
	add	x2, x29, 140
	add	x3, x29, 136
	adrp	x21, .LANCHOR0
	bl	LogAddr2PhyAddr
	ldr	x1, [x29,120]
	ldr	w0, [x29,136]
	ldrb	w1, [x1,2357]
	cmp	w0, w1
	bcc	.L1480
	mov	w0, -1
	str	w0, [x19]
	b	.L1481
.L1480:
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
.L1482:
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
	ldrb	w0, [x0,1672]
	cbz	w0, .L1483
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w26, w26, w28, ls
.L1483:
	cmn	w26, #1
	bne	.L1484
	cmp	w23, 9
	bhi	.L1484
	add	w23, w23, 1
	b	.L1482
.L1484:
	cmp	w23, wzr
	mov	w27, 256
	csel	w26, w26, w27, eq
	mov	w23, 0
.L1486:
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
	ldrb	w0, [x0,1672]
	cbz	w0, .L1489
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w28, w28, w27, ls
.L1489:
	cmn	w28, #1
	bne	.L1490
	cmp	w23, 9
	bhi	.L1490
	add	w23, w23, 1
	b	.L1486
.L1490:
	cmp	w23, wzr
	mov	w0, w20
	mov	w27, 256
	csel	w28, w28, w27, eq
	bl	NandcFlashDeCs
	add	x0, x21, :lo12:.LANCHOR0
	cmp	w28, w26
	csel	w28, w28, w26, cs
	ldrb	w0, [x0,2456]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1492
	cmn	w28, #1
	csel	w28, w28, w27, eq
.L1492:
	cmp	w28, 256
	beq	.L1503
	cmn	w28, #1
	bne	.L1493
.L1503:
	str	w28, [x19]
	b	.L1495
.L1493:
	str	wzr, [x19]
.L1495:
	ldr	x0, [x19,16]
	cbz	x0, .L1496
	ldr	w1, [x0,12]
	cmn	w1, #1
	bne	.L1496
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L1496
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1496
	str	w1, [x19]
.L1496:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1481
	add	x21, x21, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	ldr	x0, [x29,104]
	ldrb	w2, [x21,2456]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L1498
	adrp	x0, .LC98
	mov	w2, 4
	add	x0, x0, :lo12:.LC98
	mov	w3, 8
	bl	rknand_print_hex
.L1498:
	ldr	x1, [x19,16]
	cbz	x1, .L1481
	mov	w2, 4
	ldr	x0, [x29,96]
	mov	w3, w2
	bl	rknand_print_hex
.L1481:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L1479
.L1543:
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
	cbz	w23, .L1584
	bl	FlashReadSlc2KPages
	b	.L1633
.L1584:
	adrp	x0, .LC97
	mov	w24, w23
	add	x0, x0, :lo12:.LC97
	str	x0, [x29,128]
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	str	x0, [x29,120]
	adrp	x0, .LC100
	add	x0, x0, :lo12:.LC100
	str	x0, [x29,112]
.L1545:
	ldr	w0, [x29,144]
	cmp	w24, w0
	bcs	.L1635
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
	ldrb	w2, [x1,2357]
	cmp	w0, w2
	bcc	.L1547
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L1548
.L1547:
	add	x0, x1, x0, uxtw
	adrp	x25, .LANCHOR4
	ldrb	w19, [x0,2360]
	add	x0, x25, :lo12:.LANCHOR4
	str	x1, [x29,152]
	ldrb	w0, [x0,1673]
	cmp	w0, wzr
	mov	w0, w19
	csel	w20, w20, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x1, [x29,152]
	ldr	x0, [x1,1256]
	ldrb	w0, [x0,19]
	sub	w2, w0, #1
	uxtb	w2, w2
	cmp	w2, 7
	bhi	.L1550
	sub	w0, w0, #7
	add	x1, x1, 1272
	sxtw	x4, w19
	add	x1, x1, x4
	uxtb	w0, w0
	cmp	w0, 1
	ldrb	w3, [x1,12]
	bhi	.L1551
	ldrb	w3, [x1,20]
.L1551:
	add	x2, x22, :lo12:.LANCHOR0
	add	x4, x2, x4
	ldrb	w0, [x4,2128]
	cmp	w0, w3
	beq	.L1550
	ldrb	w1, [x2,1273]
	mov	w0, w19
	add	x2, x2, 1276
	bl	HynixSetRRPara
.L1550:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,140]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,152]
	cbz	w0, .L1552
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L1552
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1553
.L1552:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1553:
	cmp	w19, 255
	ldr	w1, [x29,172]
	bne	.L1591
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1585
.L1591:
	cbz	w20, .L1557
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x0,76]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1558
.L1557:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1558
.L1585:
	mov	w20, w0
.L1555:
	ldrb	w2, [x29,148]
	mov	w0, w19
	ldr	x3, [x21,8]
	mov	w1, 0
	ldr	x4, [x21,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1559
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0,80]
	cbz	w1, .L1559
	strb	wzr, [x0,80]
	mov	w20, 0
	b	.L1553
.L1559:
	cbz	w20, .L1560
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
.L1560:
	mov	w0, w19
	bl	NandcFlashDeCs
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w0, [x29,136]
	cmn	w28, #1
	strb	w0, [x6,80]
	bne	.L1567
	ldrb	w0, [x6,2372]
	cbnz	w0, .L1562
.L1566:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x20, [x0,1608]
	cbnz	x20, .L1563
	b	.L1636
.L1562:
	ldr	x0, [x6,128]
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
	beq	.L1565
	ldr	x6, [x29,104]
	ldrb	w0, [x6,2456]
	cmp	w28, w0, lsr 1
	bls	.L1587
.L1565:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1566
	b	.L1587
.L1563:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L1568
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,1256]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L1569
	ldrb	w1, [x2,1273]
	mov	w0, w19
	add	x2, x2, 1276
	mov	w3, w20
	bl	HynixSetRRPara
.L1569:
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
	ldrb	w2, [x4,2456]
	bl	printk
	cmn	w28, #1
	ldr	x4, [x29,104]
	bne	.L1568
	ldrb	w0, [x4,196]
	cbz	w0, .L1568
	ldr	w0, [x29,152]
	cbz	w0, .L1570
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1571
.L1570:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1571:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	mov	w20, 0
	ldr	x3, [x21,16]
	ldr	x4, [x0,1608]
	mov	w0, w19
	blr	x4
	mov	w28, w0
	b	.L1568
.L1636:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1568
.L1587:
	mov	w20, 0
.L1567:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,2456]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1568
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x0, [x0,1608]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1568:
	cmp	w28, 256
	beq	.L1592
	cmn	w28, #1
	bne	.L1572
.L1592:
	str	w28, [x26,x27]
	b	.L1574
.L1572:
	str	wzr, [x26,x27]
.L1574:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L1576
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x21,4]
	ldr	x0, [x29,128]
	ldrb	w2, [x2,2456]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L1576
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	bl	rknand_print_hex
.L1576:
	cbz	w20, .L1578
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,2456]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L1579
	add	x25, x25, :lo12:.LANCHOR4
	ldr	x0, [x25,1608]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L1579:
	add	w0, w24, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L1593
	cmn	w23, #1
	bne	.L1580
.L1593:
	str	w23, [x26,x0]
	b	.L1578
.L1580:
	str	wzr, [x26,x0]
.L1578:
	ldr	w0, [x29,152]
	add	w24, w24, w20
	cbz	w0, .L1548
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L1548
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1548:
	add	w24, w24, 1
	b	.L1545
.L1635:
	mov	w0, 0
	b	.L1633
.L1558:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L1555
	ldr	w1, [x29,172]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L1555
.L1633:
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
	ldrh	w1, [x0,180]
	add	x19, x23, :lo12:.LANCHOR4
	ldrh	w21, [x0,182]
	add	x0, x19, 1680
	mov	w28, 0
	adrp	x26, .LC101
	add	x26, x26, :lo12:.LC101
	mul	w21, w1, w21
	mov	w1, 0
	uxth	w21, w21
	bl	ftl_memset
	add	w25, w21, w27
	ldr	x0, [x19,1648]
	str	x0, [x29,152]
	mov	w19, w28
	sub	w0, w21, #12
	str	xzr, [x29,144]
	uxth	w25, w25
	str	w0, [x29,124]
.L1638:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,2357]
	cmp	w1, w19
	bls	.L1648
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1639:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1641
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
	beq	.L1640
	add	x24, x23, :lo12:.LANCHOR4
	ldr	x0, [x24,1648]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1640
	add	x24, x24, 1680
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	bl	printk
	strh	w20, [x24,w19,sxtw 1]
	uxth	w28, w28
	b	.L1641
.L1640:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1639
.L1641:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,2357]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1638
.L1648:
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
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w24, w2
	mov	w27, w3
	mov	x19, x0
	mov	x21, x0
	mov	w25, 0
	add	x22, x22, :lo12:.LANCHOR0
	mov	w28, -1
.L1650:
	cmp	w25, w23
	beq	.L1679
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,2357]
	cmp	w0, w1
	bcc	.L1651
	str	w28, [x21]
	b	.L1652
.L1651:
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
	tbz	x0, 0, .L1655
	str	w28, [x21]
.L1655:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1652:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1650
.L1679:
	mov	w21, 0
	cbz	w27, .L1677
	adrp	x25, .LC105
	adrp	x26, .LC104
	adrp	x27, .LC103
	add	x25, x25, :lo12:.LC105
	add	x26, x26, :lo12:.LC104
	add	x27, x27, :lo12:.LC103
.L1678:
	cmp	w21, w23
	beq	.L1677
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1658
	adrp	x0, .LC102
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC102
	bl	printk
	b	.L1659
.L1658:
	adrp	x20, .LANCHOR4
	sub	w4, w23, w21
	add	x22, x20, :lo12:.LANCHOR4
	add	x3, x29, 100
	mov	w1, w24
	add	x2, x29, 96
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,1656]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,1664]
	str	wzr, [x0]
	add	x0, x29, 104
	bl	memcpy
	ldr	x0, [x22,1656]
	mov	w1, 1
	str	x0, [x29,112]
	mov	w2, w24
	ldr	x0, [x22,1664]
	str	x0, [x29,120]
	add	x0, x29, 104
	bl	FlashReadPages
	ldr	w22, [x29,104]
	cmn	w22, #1
	bne	.L1660
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1660:
	ldr	x0, [x19,16]
	cbz	x0, .L1661
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,1664]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1661
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1661:
	ldr	x0, [x19,8]
	cbz	x0, .L1659
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,1656]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1659
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1659:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1678
.L1677:
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
	add	x19, x19, 1696
	adrp	x22, .LANCHOR0
	ldr	x1, [x0,3608]
	add	x21, x22, :lo12:.LANCHOR0
	ldr	x24, [x0,3656]
	add	x21, x21, 2616
	str	x1, [x19,8]
	mov	w23, 0
	str	x24, [x19,16]
	mov	w25, -1
	mov	w26, 61664
.L1681:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,2490]
	cmp	w23, w1
	bcs	.L1690
	ldrh	w20, [x0,2534]
	strh	w25, [x21,12]
.L1683:
	add	x0, x22, :lo12:.LANCHOR0
	sub	w20, w20, #1
	uxth	w20, w20
	ldrh	w0, [x0,2534]
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1684
	madd	w0, w23, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1683
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1683
	strh	w20, [x21,12]
.L1684:
	add	w23, w23, 1
	add	x21, x21, 2
	b	.L1681
.L1690:
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
	bne	.L1692
	ldrh	w19, [x3,2538]
	b	.L1693
.L1692:
	ldrh	w19, [x3,2536]
.L1693:
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
	bne	.L1694
	mov	w22, 0
	mov	w25, 2
.L1695:
	cmp	w22, w19
	bgt	.L1694
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
	bne	.L1696
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1696
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1696
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1695
.L1696:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1695
.L1694:
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
	ldr	x1, [x0,3608]
	add	x19, x19, 1696
	ldr	x22, [x0,3656]
	mov	w25, 61649
	str	x1, [x19,8]
	str	x22, [x19,16]
	bl	FtlBbtMemInit
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w21, [x0,2534]
	sub	w21, w21, #1
	uxth	w21, w21
.L1703:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2534]
	sub	w0, w0, #48
	cmp	w21, w0
	ble	.L1706
	lsl	w0, w21, 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1704
	ldr	w0, [x19,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
.L1704:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1705
	ldrh	w0, [x22]
	cmp	w0, w25
	bne	.L1705
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2616
	strh	w21, [x1,2616]
	ldr	w1, [x22,4]
	str	w1, [x0,8]
	ldrh	w1, [x22,8]
	strh	w1, [x0,4]
	b	.L1706
.L1705:
	sub	w21, w21, #1
	uxth	w21, w21
	b	.L1703
.L1706:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,2616]
	cmp	w1, w2
	beq	.L1708
	ldrh	w1, [x21,2620]
	cmp	w1, w2
	beq	.L1710
	add	x25, x23, :lo12:.LANCHOR4
	lsl	w1, w1, 10
	add	x0, x25, 1696
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x25,1696]
	cmn	w0, #1
	beq	.L1710
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1710
	ldr	w0, [x22,4]
	ldr	w1, [x21,2624]
	cmp	w0, w1
	bls	.L1710
	str	w0, [x21,2624]
	ldrh	w1, [x21,2620]
	ldrh	w0, [x22,8]
	strh	w1, [x21,2616]
	strh	w0, [x21,2620]
.L1710:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 1
	add	x21, x23, :lo12:.LANCHOR4
	mov	w26, 61649
	add	x21, x21, 1696
	ldrh	w0, [x25,2616]
	bl	FtlGetLastWrittenPage
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x25,2618]
.L1712:
	tbnz	w19, #31, .L1717
	ldrh	w0, [x25,2616]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w19, w0, lsl 10
	str	w0, [x21,4]
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,3608]
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1713
	ldrh	w0, [x22]
	cmp	w0, w26
	bne	.L1713
.L1717:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,2622]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1714
	b	.L1715
.L1713:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1712
.L1714:
	ldr	w2, [x1,2464]
	cmp	w0, w2
	beq	.L1715
	ldrh	w1, [x1,2478]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1715
	cmp	w2, w1
	bcs	.L1715
	bl	FtlSysBlkNumInit
.L1715:
	add	x20, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x21, x20, 2616
	add	x23, x23, :lo12:.LANCHOR4
.L1718:
	ldrh	w0, [x20,2490]
	mov	w1, w19
	add	x19, x19, 1
	cmp	w1, w0
	bcs	.L1736
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x3, [x23,1704]
	ldrh	w2, [x0,3784]
	add	x0, x21, x19, lsl 3
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1718
.L1736:
	mov	w0, 0
.L1708:
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
	strh	wzr, [x21,3856]
	mov	x24, x22
	ldr	w2, [x20,2564]
	mov	w22, 65535
	ldr	x0, [x21,3768]
	lsl	w2, w2, 2
	strh	wzr, [x20,2582]
	bl	ftl_memset
	ldr	w2, [x20,2564]
	mov	w1, 0
	ldr	x0, [x21,3712]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2556]
	mov	w1, 0
	ldr	x0, [x21,3736]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2556]
	mov	w1, 0
	ldr	x0, [x20,2584]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x21, 776
	mov	w1, 255
	mov	w2, 16
	bl	ftl_memset
	ldrh	w21, [x20,2476]
.L1738:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,2478]
	cmp	w0, w21
	bls	.L1779
	mov	x27, 0
	ldrh	w5, [x1,2468]
	ldrh	w4, [x1,2548]
	mov	w20, w27
	mov	x23, x1
.L1780:
	cmp	w5, w27, uxth
	bls	.L1813
	add	x0, x23, 2496
	mov	w1, w21
	str	x4, [x29,112]
	str	x5, [x29,120]
	ldrb	w0, [x0,x27]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x5, [x29,120]
	ldr	x4, [x29,112]
	cbnz	w0, .L1739
	add	x2, x24, :lo12:.LANCHOR2
	mov	w0, 56
	lsl	w28, w28, 10
	mov	w3, 4
	umull	x0, w20, w0
	ldr	x1, [x2,3584]
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x2,3584]
	add	x0, x1, x0
	ldr	x1, [x2,1392]
	ldr	x2, [x2,1400]
	str	x1, [x0,8]
	mul	w1, w20, w4
	add	w20, w20, 1
	sdiv	w1, w1, w3
	uxth	w20, w20
	add	x1, x2, x1, sxtw 2
	str	x1, [x0,16]
.L1739:
	add	x27, x27, 1
	b	.L1780
.L1813:
	cbz	w20, .L1742
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	mov	w28, 61604
	ldr	x0, [x0,3584]
	bl	FlashReadPages
.L1743:
	cmp	w20, w23, uxth
	bls	.L1742
	mov	x0, 56
	mul	x6, x23, x0
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,3584]
	add	x1, x0, x6
	ldr	w0, [x0,x6]
	ldr	w5, [x1,4]
	cmn	w0, #1
	ldr	x26, [x1,16]
	ubfx	x25, x5, 10, 16
	bne	.L1745
	mov	w7, 16
.L1744:
	add	x8, x24, :lo12:.LANCHOR2
	str	x7, [x29,104]
	str	x8, [x29,112]
	str	x6, [x29,120]
	ldr	x0, [x8,3584]
	add	x0, x0, x6
	ldr	w1, [x0,4]
	add	w1, w1, 1
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x0, [x8,3584]
	mov	w2, w1
	add	x0, x0, x6
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	x8, [x29,112]
	cmp	w0, w22
	ldr	x6, [x29,120]
	ldr	x7, [x29,104]
	ldr	x0, [x8,3584]
	bne	.L1746
	str	w27, [x0,x6]
	b	.L1745
.L1746:
	ldr	w0, [x0,x6]
	cmn	w0, #1
	bne	.L1745
	sub	w7, w7, #1
	uxth	w7, w7
	cbnz	w7, .L1744
.L1745:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x1, [x0,3584]
	ldr	w1, [x1,x6]
	cmn	w1, #1
	beq	.L1748
	ldr	w0, [x0,744]
	cmn	w0, #1
	beq	.L1749
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1750
.L1749:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1750
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,744]
.L1750:
	ldrh	w0, [x26]
	cmp	w0, w28
	beq	.L1752
	bhi	.L1753
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1751
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldrh	w6, [x0,2556]
	ldrh	w0, [x0,2582]
	sub	w2, w6, #1
	ldr	x7, [x7,3736]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1766
.L1753:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1755
	cmp	w0, w22
	bne	.L1751
	mov	w0, w25
	b	.L1812
.L1755:
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldr	w6, [x0,2564]
	ldrh	w0, [x7,3856]
	ldr	x7, [x7,3768]
	uxth	w2, w6
	sub	w1, w2, #1
	sub	w2, w2, w0
	sub	w2, w2, #1
	sxth	x1, w1
	sxth	w2, w2
.L1757:
	cmp	w1, w2
	ble	.L1814
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bls	.L1758
	ldr	w2, [x7]
	cbnz	w2, .L1759
	cmp	w0, w6
	beq	.L1759
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,3856]
.L1759:
	mov	w0, 0
.L1760:
	cmp	w0, w1
	beq	.L1815
	add	x6, x24, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x7, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x10, [x6,3768]
	sxth	w0, w0
	add	x11, x10, x7
	ldr	w11, [x11,4]
	str	w11, [x10,x7]
	ldr	x6, [x6,3712]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1760
.L1815:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x2, [x0,3768]
	str	w6, [x2,x8]
	ldr	x2, [x0,3712]
	strh	w25, [x2,x9,lsl 1]
	tbz	w1, #31, .L1762
	b	.L1751
.L1758:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1757
.L1814:
	tbz	w1, #31, .L1795
	b	.L1751
.L1762:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3856]
	ldr	w2, [x2,2564]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1751
.L1795:
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldr	w4, [x26,4]
	strh	w0, [x2,3856]
	ldr	x0, [x2,3768]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,3712]
	b	.L1811
.L1772:
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bhi	.L1816
	sub	w1, w1, #1
	sxth	x1, w1
.L1766:
	cmp	w1, w2
	bgt	.L1772
	b	.L1771
.L1816:
	ldr	w2, [x7]
	cbnz	w2, .L1768
	cmp	w0, w6
	beq	.L1768
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	strh	w0, [x2,2582]
.L1768:
	mov	w0, 0
.L1769:
	cmp	w0, w1
	beq	.L1817
	add	x2, x24, :lo12:.LANCHOR2
	ldr	x7, [x2,3736]
	sxtw	x2, w0
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	x10, x7, x6
	add	w0, w0, 1
	sxth	w0, w0
	ldr	w10, [x10,4]
	str	w10, [x7,x6]
	add	x6, x19, :lo12:.LANCHOR0
	ldr	x6, [x6,2584]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1769
.L1817:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	ldr	x0, [x0,3736]
	str	w2, [x0,x8]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2584]
	strh	w25, [x0,x9,lsl 1]
.L1771:
	tbnz	w1, #31, .L1751
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2556]
	ldrh	w6, [x2,2582]
	sub	w0, w0, #1
	sub	w0, w0, w6
	cmp	w1, w0, sxth
	bgt	.L1751
	add	x0, x24, :lo12:.LANCHOR2
	add	w6, w6, 1
	ldr	w4, [x26,4]
	strh	w6, [x2,2582]
	ldr	x0, [x0,3736]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2584]
.L1811:
	strh	w25, [x0,x1,lsl 1]
	b	.L1751
.L1752:
	add	x0, x24, :lo12:.LANCHOR2
	add	x1, x0, 776
	ldrh	w2, [x0,776]
	cmp	w2, w22
	bne	.L1773
	strh	w25, [x0,776]
	ldr	w0, [x26,4]
	str	w0, [x1,8]
	b	.L1751
.L1773:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1774
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1774:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	add	x0, x1, 776
	ldr	w6, [x0,8]
	cmp	w6, w2
	bcs	.L1775
	ldrh	w2, [x1,776]
	strh	w2, [x0,4]
	strh	w25, [x1,776]
	ldr	w1, [x26,4]
	str	w1, [x0,8]
	b	.L1751
.L1775:
	strh	w25, [x0,4]
	b	.L1751
.L1748:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,196]
	mov	w0, w25
	cbz	w1, .L1776
.L1812:
	mov	w1, 0
.L1776:
	bl	FtlFreeSysBlkQueueIn
.L1751:
	add	x23, x23, 1
	b	.L1743
.L1742:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1738
.L1779:
	add	x4, x24, :lo12:.LANCHOR2
	ldr	x2, [x4,3712]
	ldrh	w0, [x2]
	cbz	w0, .L1781
.L1784:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,2584]
	ldrh	w0, [x2]
	cbz	w0, .L1782
	b	.L1805
.L1781:
	ldrh	w4, [x4,3856]
	cbz	w4, .L1784
	ldr	w1, [x1,2564]
.L1785:
	cmp	w0, w1
	bcs	.L1784
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1786
	mov	w7, w0
.L1787:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w1, [x1,2564]
	cmp	w0, w1
	bcs	.L1784
	add	x2, x24, :lo12:.LANCHOR2
	sxtw	x6, w0
	sub	w1, w0, w7
	lsl	x4, x6, 1
	add	w0, w0, 1
	ldr	x5, [x2,3712]
	sxtw	x1, w1
	sxth	w0, w0
	ldrh	w8, [x5,x4]
	strh	w8, [x5,x1,lsl 1]
	ldr	x5, [x2,3768]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,3712]
	strh	wzr, [x1,x4]
	b	.L1787
.L1786:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1785
.L1782:
	ldrh	w4, [x1,2582]
	cbz	w4, .L1805
	ldrh	w1, [x1,2556]
.L1790:
	cmp	w0, w1
	mov	w7, w0
	bge	.L1805
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1791
.L1792:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2,2556]
	cmp	w0, w1
	bge	.L1805
	sxtw	x6, w0
	sub	w1, w0, w7
	ldr	x5, [x2,2584]
	lsl	x4, x6, 1
	sxtw	x1, w1
	add	w0, w0, 1
	ldrh	w8, [x5,x4]
	sxth	w0, w0
	strh	w8, [x5,x1,lsl 1]
	add	x5, x24, :lo12:.LANCHOR2
	ldr	x5, [x5,3736]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,2584]
	strh	wzr, [x1,x4]
	b	.L1792
.L1791:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1790
.L1805:
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
	add	x19, x19, 1696
	adrp	x20, .LANCHOR0
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	ldr	x0, [x24,3608]
	add	x25, x24, 776
	str	x0, [x19,8]
	ldr	x0, [x24,3656]
	str	x0, [x19,16]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,2476]
	ldr	x0, [x24,512]
	lsl	w2, w2, 1
	bl	ftl_memset
	str	x23, [x29,120]
	ldrh	w0, [x24,776]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1819
.L1830:
	mov	w19, -1
	b	.L1820
.L1819:
	mov	w1, 1
	adrp	x26, .LC106
	add	x26, x26, :lo12:.LC106
	bl	FtlGetLastWrittenPage
	ldrsh	w23, [x24,776]
	sxth	w22, w0
	add	w0, w0, 1
	strh	w0, [x25,2]
.L1821:
	tbnz	w22, #31, .L1829
	orr	w0, w22, w23, lsl 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	ldr	x0, [x24,3608]
	add	x27, x20, :lo12:.LANCHOR0
	str	x0, [x19,8]
	mov	x0, x19
	bl	FlashReadPages
	ldrb	w0, [x27,72]
	cbz	w0, .L1822
	ldr	x25, [x19,16]
	ldr	w7, [x25,12]
	cbz	w7, .L1822
	ldr	x2, [x19,8]
	ldrh	w1, [x27,2546]
	mov	x0, x2
	str	x7, [x29,104]
	str	x2, [x29,112]
	bl	js_hash
	ldr	x7, [x29,104]
	cmp	w7, w0
	beq	.L1822
	ldr	x2, [x29,112]
	cbnz	w22, .L1823
	ldrh	w0, [x24,780]
	cmp	w23, w0
	beq	.L1823
	ldr	w0, [x2]
	str	w0, [sp]
	mov	x0, x26
	ldrh	w1, [x24,776]
	ldr	w2, [x19]
	ldr	w3, [x19,4]
	ldr	w4, [x25]
	ldr	w5, [x25,4]
	ldr	w6, [x25,8]
	bl	printk
	ldrsh	w23, [x24,780]
	ldrh	w22, [x27,2538]
	b	.L1825
.L1823:
	mov	w0, -1
	str	w0, [x19]
.L1822:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1825
	ldr	x0, [x24,3608]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1825
	ldr	x0, [x24,3656]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L1825
.L1829:
	ldr	x0, [x29,120]
	add	x24, x21, :lo12:.LANCHOR2
	add	x22, x20, :lo12:.LANCHOR0
	mov	w2, 48
	add	x19, x0, :lo12:.LANCHOR4
	add	x0, x24, 448
	add	x19, x19, 1696
	ldr	x1, [x19,8]
	bl	ftl_memcpy
	ldr	x1, [x19,8]
	ldrh	w2, [x22,2476]
	ldr	x0, [x24,512]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x22,2476]
	ldr	x3, [x19,8]
	ldr	x0, [x20,#:lo12:.LANCHOR0]
	uxth	x1, w2
	lsr	w2, w2, 3
	add	x1, x1, 24
	add	w2, w2, 4
	lsr	x1, x1, 1
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	ldrh	w0, [x22,2580]
	cbz	w0, .L1827
	ldrh	w0, [x22,2476]
	ldr	x3, [x19,8]
	lsr	w1, w0, 3
	ldrh	w2, [x22,2572]
	add	w1, w1, w0, lsl 1
	ldr	x0, [x24,3760]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x3, x1
	bl	ftl_memcpy
	b	.L1827
.L1825:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1821
.L1827:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w2, [x0,448]
	cmp	w2, w1
	bne	.L1830
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w3, [x0,458]
	ldrh	w5, [x0,456]
	strh	w5, [x0,782]
	ldrh	w2, [x1,2490]
	cmp	w3, w2
	bne	.L1830
	ldrh	w3, [x1,2536]
	ldr	x2, [x29,120]
	strh	wzr, [x0,794]
	mul	w3, w3, w5
	add	x23, x2, :lo12:.LANCHOR4
	ldrh	w2, [x1,2542]
	str	w3, [x1,2608]
	strb	wzr, [x0,798]
	mul	w3, w3, w2
	str	w3, [x1,2576]
	ldrh	w2, [x1,2622]
	ldr	w3, [x1,2480]
	ldrh	w1, [x1,2468]
	sub	w2, w3, w2
	str	w5, [x23,1752]
	sub	w2, w2, w5
	ldrh	w3, [x0,462]
	strh	w3, [x0,552]
	udiv	w1, w2, w1
	ldrh	w2, [x0,464]
	strh	w1, [x0,772]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0,558]
	ldrb	w2, [x0,459]
	strb	w2, [x0,560]
	mov	w2, -1
	strh	w2, [x0,792]
	ldrh	w2, [x0,466]
	strh	w2, [x0,600]
	ldrh	w2, [x0,468]
	strh	w5, [x0,554]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0,606]
	ldrb	w2, [x0,460]
	strb	w2, [x0,608]
	ldrh	w2, [x0,470]
	strh	w2, [x0,648]
	ldrh	w2, [x0,472]
	strh	w5, [x0,602]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	wzr, [x0,800]
	strh	w5, [x0,650]
	strb	w2, [x0,654]
	ldrb	w2, [x0,461]
	ldr	w1, [x0,480]
	strb	w2, [x0,656]
	str	w1, [x0,752]
	ldr	w2, [x0,744]
	ldr	w1, [x0,488]
	str	wzr, [x0,732]
	str	wzr, [x0,720]
	cmp	w1, w2
	str	wzr, [x0,712]
	str	wzr, [x0,728]
	str	wzr, [x0,756]
	str	wzr, [x0,764]
	str	wzr, [x0,724]
	bls	.L1831
	str	w1, [x0,744]
.L1831:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,484]
	ldr	w2, [x0,748]
	cmp	w1, w2
	bls	.L1832
	str	w1, [x0,748]
.L1832:
	mov	w0, 65535
	cmp	w3, w0
	beq	.L1833
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, 552
	bl	make_superblock
.L1833:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 600
	ldrh	w2, [x1,600]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1834
	bl	make_superblock
.L1834:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 648
	ldrh	w2, [x1,648]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1835
	bl	make_superblock
.L1835:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x21, 792
	mov	w19, 0
	ldrh	w2, [x21,792]
	cmp	w2, w1
	beq	.L1820
	bl	make_superblock
.L1820:
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
	adrp	x0, .LC107
	add	x1, x1, 16
	add	x0, x0, :lo12:.LC107
	adrp	x21, .LANCHOR2
	ldrh	w24, [x19,2536]
	bl	printk
	add	x0, x21, :lo12:.LANCHOR2
	ubfiz	x2, x23, 1, 16
	mov	w1, w23
	ldr	x3, [x0,512]
	adrp	x0, .LC108
	add	x0, x0, :lo12:.LC108
	ldrh	w2, [x3,x2]
	bl	printk
	strh	w23, [x29,112]
	add	x0, x29, 112
	bl	make_superblock
	cbnz	w25, .L1855
	ldrb	w0, [x19,196]
	cbz	w0, .L1855
	mov	w0, w23
	bl	ftl_get_blk_mode
	cmp	w0, 1
	mov	w23, w0
	bne	.L1847
	ldrh	w24, [x19,2538]
	b	.L1847
.L1855:
	mov	w23, 0
.L1847:
	add	x3, x22, :lo12:.LANCHOR0
	adrp	x0, .LC109
	adrp	x25, .LC110
	add	x0, x0, :lo12:.LC109
	mov	w1, w23
	mov	w2, w24
	ldrh	w3, [x3,2536]
	mov	w20, 0
	mov	w27, 65535
	mov	w28, 56
	mov	w26, 4
	add	x25, x25, :lo12:.LC110
	bl	printk
.L1848:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w5, [x0,2468]
	ldrh	w6, [x0,2546]
	ldrh	w7, [x0,2548]
	mov	x0, 0
	mov	w19, w0
.L1849:
	cmp	w5, w0, uxth
	bls	.L1863
	add	x1, x29, 112
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w27
	beq	.L1850
	add	x3, x21, :lo12:.LANCHOR2
	orr	w2, w20, w2, lsl 10
	umull	x1, w19, w28
	ldr	x4, [x3,3584]
	add	x4, x4, x1
	str	w2, [x4,4]
	ldr	x2, [x3,3584]
	ldr	x4, [x3,1392]
	add	x1, x2, x1
	ldr	x3, [x3,1400]
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
.L1850:
	add	x0, x0, 1
	b	.L1849
.L1863:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, w23
	ldr	x0, [x0,3584]
	bl	FlashReadPages
	mov	x8, 0
	mov	x9, 56
.L1852:
	cmp	w19, w8, uxth
	bls	.L1864
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,112]
	mul	x2, x8, x9
	str	x9, [x29,96]
	ldr	x4, [x0,3584]
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
	b	.L1852
.L1864:
	add	w20, w20, 1
	uxth	w20, w20
	cmp	w20, w24
	bne	.L1848
.L1854:
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
	adrp	x0, .LC107
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC107
	add	x1, x1, 40
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x23, x24, [sp,64]
	stp	x21, x22, [sp,48]
	adrp	x23, .LC112
	adrp	x22, .LC111
	adrp	x24, .LC113
	add	x22, x22, :lo12:.LC111
	stp	x19, x20, [sp,32]
	add	x23, x23, :lo12:.LC112
	mov	w20, 0
	add	x24, x24, :lo12:.LC113
	stp	x25, x26, [sp,80]
	bl	printk
.L1866:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,2478]
	cmp	w0, w20
	bls	.L1875
	mov	w0, w20
	strh	w20, [x29,80]
	adrp	x21, .LANCHOR2
	bl	ftl_get_blk_mode
	mov	w3, w0
	add	x0, x21, :lo12:.LANCHOR2
	ubfiz	x2, x20, 1, 16
	mov	w1, w20
	ldr	x4, [x0,512]
	mov	x0, x22
	ldrh	w2, [x4,x2]
	bl	printk
	add	x0, x29, 80
	bl	make_superblock
	mov	x1, 0
	ldrh	w6, [x19,2468]
	ldrh	w7, [x19,2546]
	mov	w9, 65535
	ldrh	w8, [x19,2548]
	mov	w10, 56
	mov	w19, w1
	mov	w5, 4
.L1867:
	cmp	w6, w1, uxth
	bls	.L1876
	add	x0, x29, 80
	add	x0, x0, x1, lsl 1
	ldrh	w2, [x0,16]
	cmp	w2, w9
	beq	.L1868
	add	x3, x21, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x0, w19, w10
	ldr	x4, [x3,3584]
	add	x4, x4, x0
	str	w2, [x4,4]
	ldr	x2, [x3,3584]
	ldr	x4, [x3,1392]
	add	x2, x2, x0
	ldr	x3, [x3,1400]
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
.L1868:
	add	x1, x1, 1
	b	.L1867
.L1876:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x25, 0
	mov	x26, 56
	ldr	x0, [x0,3584]
	bl	FlashReadPages
.L1870:
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w19, w25, uxth
	bls	.L1877
	ldr	x4, [x0,3584]
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
	b	.L1870
.L1877:
	ldr	x0, [x0,3584]
	mov	w1, w19
	mov	w2, 1
	mov	x25, 0
	mov	x26, 56
	bl	FlashReadPages
.L1872:
	cmp	w19, w25, uxth
	bls	.L1878
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,80]
	mul	x2, x25, x26
	add	x25, x25, 1
	ldr	x4, [x0,3584]
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
	b	.L1872
.L1878:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1866
.L1875:
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
	adrp	x0, .LC114
	add	x0, x0, :lo12:.LC114
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x20, .LANCHOR0
	str	x25, [sp,80]
	add	x2, x20, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR4
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	add	x19, x19, :lo12:.LANCHOR4
	adrp	x23, .LC115
	ldrh	w1, [x2,2534]
	mov	w22, 0
	ldrh	w2, [x2,2536]
	add	x19, x19, 1696
	add	x23, x23, :lo12:.LC115
	bl	printk
.L1880:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2534]
	cmp	w22, w0
	bcs	.L1882
	lsl	w25, w22, 10
	mov	w21, 0
	adrp	x24, .LANCHOR2
.L1883:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2536]
	cmp	w21, w0
	bcs	.L1884
	add	w0, w21, w25
	str	w0, [x19,4]
	add	x0, x24, :lo12:.LANCHOR2
	mov	w2, 0
	str	wzr, [x19]
	add	w21, w21, 1
	ldr	x1, [x0,3608]
	ldr	x0, [x0,3656]
	str	x1, [x19,8]
	mov	w1, 1
	str	x0, [x19,16]
	mov	x0, x19
	bl	FlashReadPages
	ldr	x7, [x19,8]
	ldr	x6, [x19,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	mov	x0, x23
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x19,4]
	ldr	w2, [x19]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L1883
.L1884:
	add	w22, w22, 1
	b	.L1880
.L1882:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC116
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC116
	add	x1, x1, 56
	bl	printk
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldr	x25, [sp,80]
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
	ldr	x5, [x4,1256]
	mov	w25, w1
	ldrb	w4, [x4,72]
	mov	w23, w2
	mov	w22, 0
	ldrb	w5, [x5,19]
	cbz	w4, .L1886
	bl	FlashProgSlc2KPages
	b	.L1887
.L1886:
	sub	w0, w5, #1
	mov	w27, 56
	mov	x28, 24
	str	w0, [x29,116]
.L1938:
	cmp	w22, w25
	bcs	.L1939
	umull	x20, w22, w27
	sub	w4, w25, w22
	add	x2, x29, 128
	add	x26, x19, x20
	mov	w1, w23
	mov	x0, x26
	add	x3, x29, 132
	bl	LogAddr2PhyAddr
	mov	w24, w0
	add	x2, x21, :lo12:.LANCHOR0
	ldr	w0, [x29,132]
	ldrb	w4, [x2,2357]
	cmp	w0, w4
	bcc	.L1888
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1889
.L1888:
	ldrb	w1, [x2,2457]
	add	x3, x2, 2164
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x0, x1, x28, x3
	ldr	x0, [x0,8]
	cbz	x0, .L1891
	cmp	w4, 1
	bne	.L1892
	ldr	x0, [x2,128]
	bl	NandcIqrWaitFlashReady
.L1892:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1891:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	add	x0, x0, 2164
	ldr	w1, [x29,128]
	madd	x0, x2, x28, x0
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1893
	add	w1, w22, 1
	umull	x1, w1, w27
	add	x1, x19, x1
	str	x1, [x0,16]
.L1893:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, x2
	madd	x2, x2, x28, x1
	ldrb	w20, [x0,2360]
	ldrb	w0, [x1,2357]
	strb	w20, [x2,2164]
	cmp	w0, 1
	bne	.L1894
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1895
.L1894:
	mov	w0, w20
	str	x1, [x29,104]
	bl	NandcFlashCs
	ldr	x1, [x29,104]
	ldr	w0, [x29,132]
	add	x1, x1, 1224
	ldr	w0, [x1,x0,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
.L1895:
	ldr	w0, [x29,116]
	cmp	w0, 7
	bhi	.L1896
	add	x2, x21, :lo12:.LANCHOR0
	add	x0, x2, x20, sxtw
	ldrb	w0, [x0,2128]
	cbz	w0, .L1896
	ldrb	w1, [x2,1273]
	mov	w0, w20
	add	x2, x2, 1276
	mov	w3, 0
	bl	HynixSetRRPara
.L1896:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1897
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L1897
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1898
.L1897:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1898:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldrb	w2, [x29,124]
	mov	w0, w20
	ldr	x3, [x26,8]
	mov	w1, 1
	ldr	x4, [x26,16]
	bl	NandcXferData
	cbz	w24, .L1899
	ldr	w1, [x29,128]
	mov	w0, w20
	add	x26, x21, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29,132]
	add	x0, x26, 1224
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
.L1899:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1889:
	add	w22, w22, 1
	b	.L1938
.L1939:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	add	x22, x21, 2164
	mov	x24, 24
	ldr	x0, [x21,128]
	bl	NandcIqrWaitFlashReady
.L1901:
	ldrb	w0, [x21,2357]
	cmp	w0, w20
	bls	.L1940
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1902
	ldrb	w0, [x21,196]
	cbz	w0, .L1902
	mul	x0, x20, x24
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L1902:
	add	x20, x20, 1
	b	.L1901
.L1940:
	ldr	w0, [x29,120]
	cbnz	w0, .L1904
.L1912:
	mov	w0, 0
	b	.L1887
.L1904:
	adrp	x24, .LC105
	adrp	x26, .LC104
	adrp	x27, .LC103
	mov	w21, 0
	add	x24, x24, :lo12:.LC105
	add	x26, x26, :lo12:.LC104
	add	x27, x27, :lo12:.LC103
.L1905:
	cmp	w21, w25
	beq	.L1912
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1906
	adrp	x0, .LC102
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC102
	bl	printk
	b	.L1907
.L1906:
	adrp	x20, .LANCHOR4
	sub	w4, w25, w21
	add	x22, x20, :lo12:.LANCHOR4
	add	x3, x29, 132
	mov	w1, w23
	add	x2, x29, 128
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,1656]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,1664]
	str	wzr, [x0]
	add	x0, x29, 136
	bl	memcpy
	ldr	x0, [x22,1656]
	mov	w1, 1
	str	x0, [x29,144]
	mov	w2, w23
	ldr	x0, [x22,1664]
	str	x0, [x29,152]
	add	x0, x29, 136
	bl	FlashReadPages
	ldr	w22, [x29,136]
	cmn	w22, #1
	bne	.L1908
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1908:
	ldr	x0, [x19,16]
	cbz	x0, .L1909
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,1664]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1909
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1909:
	ldr	x0, [x19,8]
	cbz	x0, .L1907
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,1656]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1907
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1907:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1905
.L1887:
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
	ldr	x0, [x19,1640]
	str	x0, [x29,48]
	add	x0, x29, 96
	str	x0, [x29,56]
	bl	ftl_memset
	ldr	x0, [x19,1640]
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
	adrp	x0, .LANCHOR4+1628
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x0,#:lo12:.LANCHOR4+1628]
	mov	w0, 0
	cmp	w1, w2
	bcc	.L1943
	mov	w0, w1
	bl	FlashTestBlk.part.18
.L1943:
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
	ldr	x0, [x0,1648]
	str	x0, [x29,136]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,180]
	ldrh	w23, [x0,182]
	ldrh	w26, [x0,76]
	mul	w23, w1, w23
	ldr	x1, [x0,1256]
	ldrb	w0, [x0,72]
	uxth	w23, w23
	ldrb	w1, [x1,24]
	cmp	w0, 1
	str	w1, [x29,132]
	adrp	x0, .LC117
	ubfiz	w1, w26, 1, 15
	add	x0, x0, :lo12:.LC117
	csel	w26, w1, w26, eq
	mov	w1, 1
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w2, 4096
	ldr	x0, [x0,1648]
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
.L1947:
	add	x24, x21, :lo12:.LANCHOR0
	ldrb	w0, [x24,2357]
	cmp	w0, w19
	bls	.L1998
	add	x0, x20, :lo12:.LANCHOR4
	sxtw	x25, w19
	add	x1, x0, 1680
	ldrh	w22, [x1,w19,sxtw 1]
	cbnz	w22, .L1977
	ldrh	w2, [x24,188]
	mov	w1, w22
	ldr	x0, [x0,1592]
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
.L1949:
	uxth	w0, w27
	str	w0, [x29,144]
	cmp	w0, w23
	bcs	.L1959
	mov	w0, -1
	strb	w0, [x29,166]
	strb	w0, [x29,167]
	ldr	w0, [x29,116]
	cbz	w0, .L1951
	add	x4, x21, :lo12:.LANCHOR0
	add	x2, x29, 166
	add	x0, x4, 1224
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
	bne	.L1951
	ldr	w1, [x4,76]
	mov	w0, w28
	add	x2, x29, 167
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,166]
	ldrb	w1, [x29,167]
	and	w0, w1, w0
	strb	w0, [x29,166]
.L1951:
	ldr	w0, [x29,112]
	cbz	w0, .L1953
	add	x0, x21, :lo12:.LANCHOR0
	add	x2, x29, 167
	add	x0, x0, 1224
	ldr	x1, [x0,32]
	ldr	w0, [x0,x25,lsl 2]
	ldrh	w1, [x1,10]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w28
	add	w1, w1, w22
	bl	FlashReadSpare
.L1953:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x29,166]
	ldr	x0, [x0,1256]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1978
	cmp	w0, 1
	bne	.L1954
.L1978:
	mov	w0, 1
	cbz	w1, .L1956
	ldrb	w0, [x29,167]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1956
.L1954:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1956
	ldrb	w0, [x29,167]
	cmp	w0, 255
	cset	w0, ne
.L1956:
	ldr	w1, [x29,120]
	cbz	w1, .L1957
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 1224
	ldr	w1, [x0,x25,lsl 2]
	mov	w0, w28
	add	w1, w22, w1
	bl	SandiskProgTestBadBlock
.L1957:
	cbz	w0, .L1958
	adrp	x0, .LC118
	mov	w1, w19
	mov	w2, w27
	add	x0, x0, :lo12:.LC118
	add	w24, w24, 1
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x1, [x29,144]
	ldrb	w3, [x29,144]
	uxth	w24, w24
	ubfx	x1, x1, 5, 11
	lsl	x1, x1, 2
	ldr	x2, [x0,1592]
	mov	w0, 1
	lsl	w0, w0, w3
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29,128]
	ldrb	w0, [x0,2357]
	mul	w0, w1, w0
	cmp	w24, w0
	bgt	.L1959
.L1958:
	add	w27, w27, 1
	add	w22, w22, w26
	b	.L1949
.L1959:
	adrp	x0, .LC119
	mov	w1, w19
	mov	w2, w24
	add	x0, x0, :lo12:.LC119
	bl	printk
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,128]
	ldrb	w0, [x1,2357]
	mul	w0, w2, w0
	cmp	w24, w0
	blt	.L1961
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w2, [x1,188]
	mov	w1, 0
	lsl	w2, w2, 9
	ldr	x0, [x0,1592]
	bl	ftl_memset
.L1961:
	cbnz	w19, .L1963
	add	x0, x20, :lo12:.LANCHOR4
	adrp	x27, .LC120
	mov	w22, w19
	add	x27, x27, :lo12:.LC120
	mov	w24, 1
	ldrh	w28, [x0,1628]
.L1964:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,73]
	cmp	w0, w28
	bls	.L1999
	mov	w0, w28
	bl	FlashTestBlk
	cbz	w0, .L1965
	mov	w1, w28
	mov	x0, x27
	add	w22, w22, 1
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ubfx	x1, x28, 5, 11
	lsl	x1, x1, 2
	uxth	w22, w22
	ldr	x2, [x0,1592]
	lsl	w0, w24, w28
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1965:
	add	w28, w28, 1
	uxth	w28, w28
	b	.L1964
.L1999:
	adrp	x27, .LC120
	ldr	w24, [x29,124]
	sub	w28, w23, #50
	add	x27, x27, :lo12:.LC120
.L1967:
	cmp	w24, w28
	ble	.L2000
	mov	w0, w24
	bl	FlashTestBlk
	cbz	w0, .L1968
	mov	w1, w24
	mov	x0, x27
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ubfx	x1, x24, 5, 11
	lsl	x1, x1, 2
	ldr	x3, [x0,1592]
	mov	w0, 1
	lsl	w0, w0, w24
	ldr	w4, [x3,x1]
	orr	w0, w4, w0
	str	w0, [x3,x1]
.L1968:
	sub	w2, w24, #1
	uxth	w24, w2
	b	.L1967
.L2000:
	add	x2, x21, :lo12:.LANCHOR0
	add	x1, x20, :lo12:.LANCHOR4
	ldrb	w0, [x2,73]
	ldr	w3, [x1,1628]
	sub	w0, w0, w3
	cmp	w22, w0
	bcc	.L1963
	ldrh	w2, [x2,188]
	ldr	x0, [x1,1592]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L1963:
	adrp	x24, .LC121
	ldr	w28, [x29,124]
	mul	w27, w19, w23
	add	x24, x24, :lo12:.LC121
	mov	w22, -3872
.L1971:
	mov	w2, w28
	mov	x0, x24
	mov	w1, w19
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x2, [x0,1592]
.L1972:
	ubfx	x0, x28, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w28
	and	w1, w0, 1
	tbz	x0, 0, .L2001
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L1972
.L2001:
	add	x0, x20, :lo12:.LANCHOR4
	add	x2, x0, 1680
	strh	w28, [x2,x25,lsl 1]
	ldr	x2, [x29,136]
	strh	w22, [x2]
	strh	w28, [x2,2]
	strh	w1, [x2,8]
	ldr	x1, [x0,1592]
	ldr	x0, [x0,1648]
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
	cbz	w0, .L1977
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L1971
.L1977:
	add	w19, w19, 1
	uxtb	w19, w19
	b	.L1947
.L1998:
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
	ldrb	w0, [x24,2457]
	ldr	w2, [x1,416]
	str	w0, [x29,120]
	mov	w0, 0
	cbnz	w2, .L2003
	ldrb	w0, [x24,196]
	mov	w28, w2
	str	w0, [x29,124]
	mov	w21, w2
	str	w26, [x1,3572]
	mov	w19, w2
	mov	w27, 56
.L2004:
	ldrh	w0, [x24,2468]
	cmp	w0, w28
	bls	.L2048
	add	x1, x20, :lo12:.LANCHOR2
	umull	x0, w28, w27
	ldr	x1, [x1,424]
	str	wzr, [x1,x0]
	add	x0, x24, 2496
	mov	w1, w26
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	uxth	w23, w0
	mov	w1, w23
	cbz	w25, .L2005
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L2006
.L2005:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2007
	add	x3, x20, :lo12:.LANCHOR2
	lsl	w23, w23, 10
	umull	x0, w21, w27
	mov	w2, 4
	ldr	x1, [x3,424]
	add	x1, x1, x0
	str	w23, [x1,4]
	ldr	x1, [x3,424]
	ldr	x3, [x3,3664]
	add	x1, x1, x0
	ldrh	w0, [x24,2548]
	mul	w0, w21, w0
	add	w21, w21, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L2006
.L2007:
	add	w19, w19, 1
	uxth	w19, w19
.L2006:
	add	w2, w28, 1
	uxth	w28, w2
	b	.L2004
.L2048:
	cbz	w21, .L2027
	ldr	w0, [x29,124]
	mov	w2, w21
	strb	wzr, [x24,2457]
	mov	x27, 56
	cmp	w0, wzr
	add	x0, x20, :lo12:.LANCHOR2
	cset	w23, ne
	mov	w1, w23
	ldr	x0, [x0,424]
	bl	FlashEraseBlocks
	ldrb	w0, [x29,120]
	strb	w0, [x24,2457]
	mov	x24, 0
.L2010:
	cmp	w21, w24, uxth
	bls	.L2049
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x24, x27
	ldr	x1, [x1,424]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L2011
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L2011:
	add	x24, x24, 1
	b	.L2010
.L2049:
	cbnz	w25, .L2013
	mov	w0, 1
	uxth	w23, w23
	mov	w28, 6
	str	w0, [x29,124]
	b	.L2014
.L2013:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,2538]
	ldrb	w0, [x0,196]
	str	w1, [x29,124]
	cbnz	w0, .L2030
	uxtw	x0, w1
	mov	w23, 1
	lsr	w28, w0, 2
	b	.L2014
.L2030:
	mov	w23, 1
	mov	w28, w23
.L2014:
	mov	w24, 0
	add	x22, x22, :lo12:.LANCHOR0
.L2023:
	mov	w27, 0
	mov	w21, w27
.L2015:
	ldrh	w0, [x22,2468]
	cmp	w0, w27
	bls	.L2050
	add	x1, x20, :lo12:.LANCHOR2
	mov	w0, 56
	umull	x0, w27, w0
	ldr	x1, [x1,424]
	str	wzr, [x1,x0]
	add	x0, x22, 2496
	mov	w1, w26
	ldrb	w0, [x0,w27,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w25, .L2016
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L2017
.L2016:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2017
	add	x2, x20, :lo12:.LANCHOR2
	mov	w0, 56
	ldr	w3, [x29,112]
	umull	x0, w21, w0
	ldr	x1, [x2,424]
	add	w3, w24, w3, lsl 10
	add	x1, x1, x0
	str	w3, [x1,4]
	mov	w3, 4
	ldr	x1, [x2,424]
	add	x1, x1, x0
	ldr	x0, [x2,3640]
	ldr	x2, [x2,3648]
	str	x0, [x1,8]
	ldrh	w0, [x22,2548]
	mul	w0, w21, w0
	add	w21, w21, 1
	sdiv	w0, w0, w3
	uxth	w21, w21
	add	x0, x2, x0, sxtw 2
	str	x0, [x1,16]
.L2017:
	add	w4, w27, 1
	uxth	w27, w4
	b	.L2015
.L2050:
	cbz	w21, .L2027
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w21
	mov	w2, w23
	mov	w3, 1
	strb	wzr, [x22,2457]
	ldr	x0, [x0,424]
	bl	FlashProgPages
	ldrb	w0, [x29,120]
	mov	x1, 0
	strb	w0, [x22,2457]
.L2020:
	cmp	w21, w1, uxth
	bls	.L2051
	add	x2, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x1, x0
	ldr	x2, [x2,424]
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L2021
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L2021:
	add	x1, x1, 1
	b	.L2020
.L2051:
	add	w24, w24, w28
	ldr	w0, [x29,124]
	uxth	w24, w24
	cmp	w24, w0
	bcc	.L2023
	mov	x22, 0
	mov	x24, 56
.L2024:
	cmp	w21, w22, uxth
	bls	.L2052
	cbz	w25, .L2025
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x22, x24
	ldr	x1, [x1,424]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L2025
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L2025:
	add	x22, x22, 1
	b	.L2024
.L2052:
	cmp	w26, 63
	bls	.L2031
	cbz	w25, .L2027
.L2031:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, w21
	ldr	x0, [x20,424]
	bl	FlashEraseBlocks
.L2027:
	mov	w0, w19
.L2003:
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
	ldr	w1, [x2,416]
	cbnz	w1, .L2055
	adrp	x19, .LANCHOR4
	adrp	x20, .LANCHOR0
	add	x24, x19, :lo12:.LANCHOR4
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x2,3608]
	mov	x21, 0
	ldr	x2, [x2,3656]
	add	x23, x20, 2616
	str	x2, [x24,1712]
	ldrh	w2, [x20,2546]
	str	x0, [x24,1704]
	bl	ftl_memset
.L2056:
	ldrh	w1, [x20,2490]
	mov	w0, w21
	add	x21, x21, 1
	cmp	w0, w1
	bge	.L2069
	add	x1, x22, :lo12:.LANCHOR2
	ldr	x3, [x24,1704]
	ldrh	w2, [x1,3784]
	add	x1, x23, x21, lsl 3
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L2056
.L2069:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 255
	add	x19, x19, 1696
	mov	w2, 16
	mov	w24, 0
	adrp	x26, .LC122
	ldr	x21, [x19,16]
	adrp	x27, .LC123
	mov	w25, w24
	add	x26, x26, :lo12:.LC122
	mov	x0, x21
	add	x27, x27, :lo12:.LC123
	bl	ftl_memset
	mov	w0, -3887
	strh	w0, [x21]
	ldr	w0, [x23,8]
	str	w0, [x21,4]
	ldrh	w0, [x20,2616]
	strh	w0, [x21,2]
	ldrh	w0, [x23,4]
	strh	w0, [x21,8]
	ldrh	w0, [x23,6]
	strh	w0, [x21,10]
	ldr	w0, [x20,2464]
	strh	w0, [x21,12]
.L2058:
	add	x28, x22, :lo12:.LANCHOR2
	ldrh	w1, [x20,2616]
	ldrh	w2, [x20,2618]
	ldrh	w3, [x20,2620]
	ldr	x0, [x28,3608]
	str	x0, [x19,8]
	ldr	x0, [x28,3656]
	str	x0, [x19,16]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19,4]
	mov	x0, x26
	ldrh	w4, [x21,10]
	str	wzr, [x19]
	bl	printk
	ldrh	w0, [x20,2538]
	ldrh	w1, [x20,2618]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2059
	ldr	w0, [x20,2624]
	strh	wzr, [x20,2618]
	add	w0, w0, 1
	str	w0, [x20,2624]
	str	w0, [x21,4]
	ldrh	w0, [x20,2616]
	ldrh	w1, [x20,2620]
	strh	w0, [x21,8]
	strh	w0, [x20,2620]
	ldr	x0, [x28,424]
	strh	w1, [x20,2616]
	lsl	w1, w1, 10
	str	w1, [x19,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x28,424]
	bl	FlashEraseBlocks
.L2059:
	mov	w1, 1
	mov	x0, x19
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w0, [x20,2618]
	add	w0, w0, 1
	strh	w0, [x20,2618]
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L2060
	ldr	w1, [x19,4]
	mov	x0, x27
	add	w24, w24, 1
	bl	printk
	uxth	w24, w24
	cmp	w24, 3
	bls	.L2058
	adrp	x0, .LC124
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC124
	mov	w2, w24
	add	x22, x22, :lo12:.LANCHOR2
	bl	printk
	mov	w0, 1
	str	w0, [x22,416]
	b	.L2055
.L2060:
	add	w25, w25, 1
	cmp	w25, 1
	beq	.L2058
	cmp	w0, 256
	beq	.L2058
.L2055:
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
	ldr	w1, [x19,416]
	stp	x27, x28, [sp,80]
	cbnz	w1, .L2071
	mov	x20, x0
	add	x23, x19, 648
	adrp	x22, .LANCHOR0
	mov	w24, 7
	add	x25, x19, 536
.L2072:
	cmp	x20, x23
	bne	.L2073
	ldrh	w2, [x19,544]
	ldr	w4, [x19,1372]
	lsr	w3, w2, 1
	add	w1, w3, 1
	mul	w0, w2, w4
	add	w1, w1, w0, lsr 2
	add	x0, x22, :lo12:.LANCHOR0
	uxth	w1, w1
	ldr	w0, [x0,2460]
	cbz	w0, .L2074
	ldr	w0, [x19,768]
	cmp	w0, 39
	bhi	.L2074
	cmp	w0, 2
	mov	w1, 0
	bls	.L2075
	tbz	x2, 0, .L2102
	cbz	w4, .L2075
.L2102:
	mov	w1, w3
	b	.L2074
.L2073:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L2075
	add	x2, x22, :lo12:.LANCHOR0
	ldrh	w0, [x2,2488]
	cmp	w0, 1
	beq	.L2075
	ldrb	w0, [x2,196]
	cbnz	w0, .L2075
	ldr	w2, [x2,2460]
	ldrh	w0, [x19,544]
	lsr	w1, w0, 3
	cbz	w2, .L2074
	ldr	w2, [x19,768]
	cmp	w2, 1
	bhi	.L2074
	mul	w1, w0, w24
	lsr	w1, w1, 3
.L2074:
	cbz	w1, .L2075
	sub	w1, w1, #1
	uxth	w1, w1
.L2075:
	ldrb	w2, [x20,8]
	mov	x0, x25
	add	x27, x22, :lo12:.LANCHOR0
	bl	List_pop_index_node
	uxth	w28, w0
	ldrh	w0, [x19,544]
	sub	w0, w0, #1
	strh	w0, [x19,544]
	ldrh	w0, [x27,2476]
	cmp	w0, w28
	bls	.L2072
	ubfiz	x26, x28, 1, 16
	ldr	x0, [x19,512]
	ldrh	w0, [x0,x26]
	cbnz	w0, .L2072
	strh	w28, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L2127
	mov	x0, 0
	ldrh	w5, [x27,2468]
	mov	x3, 56
	mov	w27, w0
	mov	w6, 65535
.L2079:
	cmp	w5, w0, uxth
	bls	.L2130
	ldr	x4, [x19,424]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L2080
	umull	x2, w27, w3
	add	w27, w27, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w27, w27
	str	w1, [x2,4]
.L2080:
	add	x0, x0, 1
	b	.L2079
.L2130:
	add	x0, x19, 552
	cmp	x20, x0
	bne	.L2082
	add	x1, x22, :lo12:.LANCHOR0
	ldr	w1, [x1,2460]
	cbz	w1, .L2082
	ldr	x1, [x19,432]
	ldrh	w1, [x1,x26]
	cmp	w1, 40
	bls	.L2082
	strb	wzr, [x19,560]
.L2082:
	ldrb	w0, [x20,8]
	cbnz	w0, .L2083
	ldr	x0, [x19,432]
	mov	w1, 2
	ldrh	w2, [x0,x26]
	cbz	w2, .L2128
	add	x1, x22, :lo12:.LANCHOR0
	ldrh	w1, [x1,2528]
	add	w1, w2, w1
.L2128:
	strh	w1, [x0,x26]
	mov	w1, 0
	ldr	w0, [x19,752]
	add	w0, w0, 1
	str	w0, [x19,752]
	mov	w0, w28
	bl	ftl_set_blk_mode
	b	.L2086
.L2083:
	ldr	x1, [x19,432]
	ldrh	w0, [x1,x26]
	add	w0, w0, 1
	strh	w0, [x1,x26]
	ldr	w0, [x19,756]
	add	w0, w0, 1
	str	w0, [x19,756]
	mov	w0, w28
	bl	ftl_set_blk_mode.part.9
.L2086:
	ldr	x0, [x19,432]
	ldr	w1, [x19,764]
	ldrh	w0, [x0,x26]
	cmp	w0, w1
	bls	.L2087
	str	w0, [x19,764]
.L2087:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w3, [x19,752]
	ldr	w0, [x19,756]
	ldrh	w1, [x2,2528]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,2476]
	mov	x3, 56
	udiv	w0, w0, w1
	ldr	x1, [x19,3696]
	str	w0, [x19,760]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L2088:
	cmp	w27, w0, uxth
	bls	.L2131
	ldr	x1, [x19,424]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L2088
.L2131:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L2090
	ldrb	w0, [x20,8]
	cmp	w0, 1
	ldr	x0, [x19,424]
	bne	.L2091
	mov	w1, 0
	b	.L2129
.L2091:
	mov	w1, 1
.L2129:
	mov	w2, w27
	bl	FlashEraseBlocks
.L2090:
	ldrb	w1, [x20,8]
	mov	w2, w27
	ldr	x0, [x19,424]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	x4, 56
	mov	w2, w1
.L2092:
	cmp	w27, w1, uxth
	bls	.L2132
	mul	x0, x1, x4
	ldr	x3, [x19,424]
	add	x5, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	bne	.L2093
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
.L2093:
	add	x1, x1, 1
	b	.L2092
.L2132:
	cbz	w2, .L2095
	mov	w0, w28
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2095:
	ldrb	w0, [x20,7]
	cbnz	w0, .L2096
.L2127:
	ldr	x0, [x19,512]
	mov	w1, -1
	strh	w1, [x0,x26]
	b	.L2072
.L2096:
	adrp	x1, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x1,2536]
	strh	w28, [x20]
	mul	w0, w0, w1
	ldr	w1, [x21,744]
	str	w1, [x20,12]
	uxth	w0, w0
	add	w1, w1, 1
	strh	w0, [x20,4]
	str	w1, [x21,744]
	ldr	x1, [x21,512]
	ldrh	w2, [x20]
	strh	w0, [x1,x2,lsl 1]
.L2071:
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
	ldrh	w0, [x19,1456]
	stp	x21, x22, [sp,32]
	mov	w21, 0
	cbz	w0, .L2135
	adrp	x22, .LANCHOR0
	adrp	x24, .LC125
	add	x22, x22, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LC125
.L2143:
	ldrh	w0, [x22,2468]
	cmp	w0, w21
	bls	.L2144
	add	x0, x22, 2496
	mov	w1, w23
	mov	w20, 0
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w25, w0
.L2136:
	ldrh	w0, [x19,1456]
	cmp	w0, w20
	bls	.L2145
	add	x0, x19, 1464
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w25
	bne	.L2137
	mov	w1, w25
	mov	x0, x24
	bl	printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,1456]
	mov	w3, w20
.L2138:
	cmp	w3, w2
	bcs	.L2146
	add	x4, x19, 1464
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2138
.L2146:
	sub	w2, w2, #1
	strh	w2, [x19,1456]
.L2137:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2136
.L2145:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2143
.L2144:
	bl	FtlGcReFreshBadBlk
.L2135:
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
	ldr	x2, [x1,512]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2148
	ldrh	w0, [x1,792]
	cmp	w0, w19
	bne	.L2149
	mov	w0, -1
	strh	w0, [x1,792]
	b	.L2150
.L2149:
	ldrh	w3, [x1,552]
	mov	w0, w2
	cmp	w3, w19
	beq	.L2151
	ldrh	w3, [x1,600]
	cmp	w3, w19
	beq	.L2151
	ldrh	w1, [x1,648]
	cmp	w1, w19
	beq	.L2151
.L2150:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w19
	add	x0, x20, 504
	bl	List_remove_node
	ldrh	w0, [x20,528]
	sub	w0, w0, #1
	strh	w0, [x20,528]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L2151
.L2148:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2151:
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
	beq	.L2156
	add	x21, x20, :lo12:.LANCHOR2
	ubfiz	x22, x19, 1, 16
	ldr	x0, [x21,512]
	ldrh	w2, [x0,x22]
	cbnz	w2, .L2157
	adrp	x0, .LC126
	mov	w1, w19
	add	x0, x0, :lo12:.LC126
	add	x20, x21, 536
	bl	printk
	ldr	x0, [x21,512]
	mov	w1, 32
	strh	w1, [x0,x22]
	mov	x0, x20
	mov	w1, w19
	bl	test_node_in_list
	cbz	w0, .L2158
	mov	w1, w19
	mov	x0, x20
	bl	List_remove_node
	ldrh	w0, [x21,544]
	sub	w0, w0, #1
	strh	w0, [x21,544]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x2, [x21,512]
	adrp	x0, .LC127
	add	x0, x0, :lo12:.LC127
	mov	w1, w19
	ldrh	w2, [x2,x22]
	bl	printk
.L2158:
	mov	w0, w19
	bl	FtlGcRefreshBlock
	b	.L2165
.L2157:
	sub	w2, w2, #1
	strh	w2, [x0,x22]
.L2156:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w1, [x20,3872]
	cmp	w1, w0
	bne	.L2160
	strh	w19, [x20,3872]
.L2165:
	mov	w0, 0
	b	.L2159
.L2160:
	cmp	w1, w19
	mov	w0, 0
	beq	.L2159
	mov	w0, w1
	bl	update_vpc_list
	cmp	w0, wzr
	cset	w0, ne
	strh	w19, [x20,3872]
.L2159:
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
	cbz	w1, .L2166
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2166
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR2
	adrp	x22, .LANCHOR0
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR2
	add	x21, x21, 1500
	ldrh	w0, [x19,x0,lsl 1]
.L2170:
	cmp	w0, w20
	bne	.L2177
.L2172:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,2468]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2171
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2171:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2170
.L2177:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2173
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbnz	w0, .L2173
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w20
	bne	.L2173
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2172
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2166
.L2173:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,196]
	cbz	w2, .L2166
	cmp	w1, 1
	bne	.L2166
	ldrh	w2, [x19,2]
	ldrh	w1, [x0,2538]
	cmp	w2, w1
	bcc	.L2166
	adrp	x2, .LANCHOR2+512
	ldrh	w1, [x19]
	ldrh	w4, [x19,4]
	lsl	x1, x1, 1
	ldr	x3, [x2,#:lo12:.LANCHOR2+512]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,2536]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2166:
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
	add	x21, x21, 1500
	ldrh	w0, [x19,x0,lsl 1]
.L2179:
	cmp	w0, w22
	adrp	x20, .LANCHOR0
	bne	.L2194
.L2180:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,2468]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2181
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2181:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2179
.L2194:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2182
	add	x1, x23, :lo12:.LANCHOR0
	ldrb	w1, [x1,196]
	cbnz	w1, .L2182
	ldrh	w1, [x19,2]
	ldrh	w1, [x21,w1,sxtw 1]
	cmp	w1, w22
	bne	.L2182
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2180
.L2182:
	ldrh	w1, [x19,4]
	adrp	x21, .LANCHOR2
	ldrh	w22, [x19,2]
	mov	w23, 65535
	add	x21, x21, :lo12:.LANCHOR2
	sub	w1, w1, #1
	orr	w22, w22, w0, lsl 10
	strh	w1, [x19,4]
	mov	w24, w23
	add	x21, x21, 1500
.L2183:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,2468]
.L2185:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2184
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2184:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2185
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2190
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbnz	w0, .L2187
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w24
	bne	.L2187
	ldrh	w1, [x19,4]
	cbz	w1, .L2187
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2183
.L2187:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,196]
	cbz	w0, .L2190
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,2538]
	cmp	w1, w0
	bcc	.L2190
	adrp	x0, .LANCHOR2+512
	ldrh	w1, [x19]
	ldrh	w3, [x19,4]
	lsl	x1, x1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR2+512]
	ldrh	w2, [x0,x1]
	sub	w2, w2, w3
	strh	w2, [x0,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x20,2536]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2190:
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
	ldr	w0, [x19,416]
	stp	x27, x28, [sp,80]
	cbnz	w0, .L2197
	adrp	x26, .LANCHOR4
	ldr	x21, [x19,3656]
	add	x22, x26, :lo12:.LANCHOR4
	ldr	x1, [x19,3608]
	ldrh	w2, [x19,776]
	add	x20, x19, 448
	adrp	x24, .LANCHOR0
	add	x22, x22, 1696
	str	x1, [x22,8]
	add	x25, x24, :lo12:.LANCHOR0
	str	x21, [x22,16]
	strh	w2, [x21,2]
	mov	w2, -3932
	strh	w2, [x21]
	str	w0, [x21,12]
	ldr	w2, [x19,784]
	str	w2, [x21,4]
	str	w0, [x21,8]
	mov	w0, 19539
	ldrh	w1, [x19,552]
	movk	w0, 0x4654, lsl 16
	strh	w1, [x20,14]
	ldrh	w1, [x19,554]
	ldrb	w2, [x19,558]
	str	w0, [x19,448]
	mov	w0, 96
	orr	w1, w2, w1, lsl 6
	strh	w1, [x20,16]
	ldrh	w1, [x19,600]
	movk	w0, 0x5000, lsl 16
	strh	w1, [x20,18]
	ldrh	w1, [x19,602]
	str	w0, [x20,4]
	ldrb	w2, [x19,606]
	ldrh	w0, [x19,782]
	strh	w0, [x20,8]
	orr	w1, w2, w1, lsl 6
	ldrh	w0, [x25,2490]
	strb	w0, [x20,10]
	ldrb	w0, [x19,560]
	strb	w0, [x20,11]
	strh	w1, [x20,20]
	ldrb	w0, [x19,608]
	ldrh	w1, [x19,648]
	strb	w0, [x20,12]
	strh	w1, [x20,22]
	ldrh	w1, [x19,650]
	ldrb	w0, [x19,656]
	strb	w0, [x20,13]
	ldr	w0, [x19,752]
	str	w0, [x20,32]
	ldr	w0, [x19,744]
	str	w0, [x20,40]
	ldr	w0, [x19,748]
	ldrb	w2, [x19,654]
	str	w0, [x20,36]
	ldrh	w0, [x19,1448]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x20,44]
	ldrh	w0, [x19,1450]
	strh	w0, [x20,46]
	ldrh	w2, [x25,2546]
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
	ldrh	w2, [x25,2476]
	ldr	x1, [x19,512]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x25,2476]
	ldr	x1, [x22,8]
	uxth	x0, w2
	lsr	w2, w2, 3
	add	x0, x0, 24
	add	w2, w2, 4
	lsr	x0, x0, 1
	add	x0, x1, x0, lsl 2
	ldr	x1, [x24,#:lo12:.LANCHOR0]
	bl	ftl_memcpy
	ldrh	w0, [x25,2580]
	cbz	w0, .L2198
	ldrh	w0, [x25,2476]
	ldrh	w2, [x25,2572]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x22,8]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,3752]
	bl	ftl_memcpy
.L2198:
	mov	w0, 0
	mov	w22, 0
	mov	w25, 65535
	add	x26, x20, :lo12:.LANCHOR4
	add	x19, x23, :lo12:.LANCHOR2
	bl	FtlUpdateVaildLpn
.L2199:
	ldrh	w1, [x19,776]
	ldr	x0, [x19,3608]
	ldrh	w2, [x19,778]
	str	x0, [x26,1704]
	ldr	x0, [x19,3656]
	str	x0, [x26,1712]
	orr	w0, w2, w1, lsl 10
	str	w0, [x26,1700]
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w0, [x0,2538]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L2200
	ldrh	w25, [x19,780]
	strh	wzr, [x19,778]
	strh	w1, [x19,780]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,744]
	str	w1, [x19,784]
	add	w2, w1, 1
	str	w2, [x19,744]
	ubfiz	w2, w0, 10, 16
	str	w2, [x26,1700]
	strh	w0, [x19,776]
	str	w1, [x21,4]
	strh	w0, [x21,2]
.L2200:
	add	x1, x24, :lo12:.LANCHOR0
	ldrb	w0, [x1,72]
	cbz	w0, .L2201
	ldr	x0, [x19,3608]
	ldrh	w1, [x1,2546]
	bl	js_hash
	str	w0, [x21,12]
.L2201:
	mov	w1, 1
	add	x0, x26, 1696
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,778]
	ldr	w2, [x26,1696]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x19,778]
	bne	.L2202
	cmp	w1, 1
	bne	.L2203
	add	x1, x24, :lo12:.LANCHOR0
	ldrh	w1, [x1,2538]
	sub	w1, w1, #1
	strh	w1, [x19,778]
.L2203:
	add	w22, w22, 1
	uxth	w22, w22
	cmp	w22, 3
	bls	.L2199
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x0, .LC128
	add	x0, x0, :lo12:.LC128
	mov	w2, w22
	add	x23, x23, :lo12:.LANCHOR2
	ldr	w1, [x20,1700]
	bl	printk
	mov	w0, 1
	str	w0, [x23,416]
	b	.L2197
.L2202:
	cmp	w1, 1
	beq	.L2199
	cmp	w2, 256
	beq	.L2199
	mov	w0, 65535
	cmp	w25, w0
	beq	.L2197
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L2197:
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
	ldr	w1, [x1,416]
	cbnz	w1, .L2212
	adrp	x21, .LANCHOR0
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w22, [x2,196]
	cbz	w22, .L2227
	ldrb	w22, [x0,8]
	cmp	w22, 1
	bne	.L2223
	ldrh	w24, [x0,4]
	b	.L2214
.L2223:
	mov	w22, w1
.L2227:
	mov	w24, 12
.L2214:
	mov	x19, x0
	mov	w0, -1
	str	w0, [x29,96]
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 61589
	ldr	x23, [x0,3656]
	ldr	x1, [x0,3608]
	str	x1, [x29,80]
	mov	w1, -3
	str	x23, [x29,88]
	str	w1, [x23,8]
	mov	w1, -2
	str	w1, [x23,12]
	ldrh	w1, [x19]
	strh	w1, [x23,2]
	strh	wzr, [x23]
	ldr	x1, [x0,3608]
	str	w2, [x1]
	ldr	x1, [x0,3608]
	mov	w0, 22136
	movk	w0, 0x1234, lsl 16
	str	w0, [x1,4]
.L2215:
	sub	w24, w24, #1
	cmn	w24, #1
	beq	.L2218
	ldrh	w0, [x19,4]
	cbnz	w0, .L2216
.L2218:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19]
	ldrh	w3, [x19,4]
	add	x21, x21, :lo12:.LANCHOR0
	lsl	x0, x0, 1
	ldr	x2, [x20,512]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x21,2536]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	b	.L2212
.L2216:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2218
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, w22
	mov	w3, 0
	ldr	w0, [x1,748]
	str	w0, [x23,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,748]
	add	x0, x29, 72
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2215
.L2212:
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
	blt	.L2229
	ubfiz	x0, x0, 1, 16
	ldrh	w24, [x20,x0]
	cbz	w24, .L2229
	ldr	w1, [x19,52]
	cbnz	w1, .L2229
	mov	w2, 1
	str	w2, [x19,52]
	strh	w1, [x20,x0]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2538]
	cmp	w1, w0
	bcc	.L2230
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2230:
	adrp	x20, .LANCHOR4
	mov	w21, 0
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x26, .LANCHOR2
	add	x20, x20, 1696
.L2231:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L2240
	ubfiz	x0, x21, 2, 16
	add	x27, x25, x0
	ldr	w1, [x25,x0]
	cmp	w24, w1, lsr 10
	bne	.L2232
	add	x1, x26, :lo12:.LANCHOR2
	ldr	x2, [x1,3616]
	ldr	x23, [x1,3656]
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
	bne	.L2233
.L2235:
	adrp	x1, .LANCHOR4
	str	wzr, [x27]
	add	x1, x1, :lo12:.LANCHOR4
	adrp	x0, .LC129
	ldrh	w2, [x23,8]
	add	x0, x0, :lo12:.LC129
	ldr	w1, [x1,1700]
	bl	printk
	adrp	x0, .LANCHOR2
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR2
	str	w1, [x0,416]
	b	.L2234
.L2233:
	ldrh	w0, [x23,8]
	cmp	w0, w21
	bne	.L2235
	ldrh	w1, [x23]
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L2235
	ldr	x2, [x20,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L2232:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2231
.L2240:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L2229:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x22,2538]
	cmp	w1, w0
	bcc	.L2234
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2234:
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
	ldr	w20, [x1,416]
	ldr	x21, [x0,16]
	ldr	x26, [x0,40]
	cbnz	w20, .L2242
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L2243
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
	b	.L2242
.L2243:
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x19,2]
	ldr	x23, [x1,3656]
	adrp	x22, .LANCHOR0
	ldrh	w25, [x21,x0]
	adrp	x21, .LANCHOR4
	add	x0, x21, :lo12:.LANCHOR4
	orr	w2, w2, w25, lsl 10
	str	w2, [x0,1700]
	ldr	x2, [x1,3608]
	str	x2, [x0,1704]
	str	x23, [x0,1712]
	ldr	w0, [x19,48]
	str	w0, [x23,4]
	mov	w0, -1291
	strh	w0, [x23,8]
	ldrh	w0, [x19,4]
	strh	w0, [x23]
	add	x0, x22, :lo12:.LANCHOR0
	strh	w25, [x23,2]
	ldrh	w2, [x0,2538]
	ldr	x0, [x1,3608]
	mov	w1, 255
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, w20
.L2244:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L2251
	ubfiz	x3, x0, 2, 32
	ldr	w1, [x26,x3]
	cmp	w25, w1, lsr 10
	bne	.L2245
	add	x2, x24, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x4, [x2,3608]
	ubfiz	x1, x20, 3, 16
	str	w0, [x4,x1]
	ldr	x2, [x2,3608]
	ldr	w3, [x26,x3]
	add	x1, x2, x1
	str	w3, [x1,4]
.L2245:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L2244
.L2251:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w0, [x22,72]
	cbz	w0, .L2247
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w1, [x22,2546]
	ldr	x0, [x0,1704]
	bl	js_hash
	str	w0, [x23,12]
.L2247:
	mov	w1, 1
	add	x0, x21, :lo12:.LANCHOR4
	add	x0, x0, 1696
	mov	w2, w1
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	strh	w0, [x19,2]
	mov	x0, x19
	bl	ftl_map_blk_gc
.L2242:
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
	adrp	x24, .LC130
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w26, w1
	str	x2, [x29,104]
	mov	w22, 0
	add	x23, x23, 1696
	add	x24, x24, :lo12:.LC130
.L2253:
	adrp	x19, .LANCHOR2
	adrp	x20, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,728]
	add	w0, w0, 1
	str	w0, [x1,728]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x28,2]
	ldrh	w0, [x0,2538]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L2254
	ldrh	w1, [x28]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2255
.L2254:
	mov	x0, x28
	bl	Ftl_write_map_blk_to_last_page
.L2255:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	w1, [x2,416]
	cbnz	w1, .L2256
	ldrh	w3, [x28]
	adrp	x21, .LANCHOR4
	ldr	x0, [x28,16]
	ldrh	w25, [x0,x3,lsl 1]
	add	x3, x27, :lo12:.LANCHOR4
	ldrh	w0, [x28,2]
	add	x3, x3, 1696
	orr	w0, w0, w25, lsl 10
	str	w0, [x3,4]
	ldr	x0, [x29,104]
	str	x0, [x3,8]
	ldr	x0, [x2,3656]
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
	cbz	w0, .L2257
	ldr	x0, [x3,8]
	ldrh	w1, [x1,2546]
	str	x2, [x29,96]
	bl	js_hash
	ldr	x2, [x29,96]
	str	w0, [x2,12]
.L2257:
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
	bne	.L2258
	ldr	w1, [x23,4]
	mov	x0, x24
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	ldrh	w0, [x28,2]
	cmp	w0, 2
	bhi	.L2259
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20,2538]
	sub	w0, w0, #1
	strh	w0, [x28,2]
.L2259:
	cmp	w22, 3
	bls	.L2253
	add	x21, x21, :lo12:.LANCHOR4
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	mov	w2, w22
	add	x19, x19, :lo12:.LANCHOR2
	ldr	w1, [x21,1700]
	bl	printk
	mov	w0, 1
	str	w0, [x19,416]
	b	.L2256
.L2258:
	cbz	w1, .L2261
	strh	w25, [x28,60]
.L2261:
	cmp	w0, 1
	beq	.L2262
	cmp	w1, 256
	beq	.L2262
	ldr	w0, [x28,56]
	cbz	w0, .L2263
.L2262:
	str	wzr, [x28,56]
	b	.L2253
.L2263:
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x0, [x28,40]
	ldr	w1, [x21,1700]
	str	w1, [x0,w26,uxtw 2]
.L2256:
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
	add	x0, x20, 3792
	ldr	x1, [x20,696]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20,696]
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
	add	x4, x4, 1696
	str	x21, [x29,128]
	ldr	x2, [x1,3608]
	sub	w0, w27, #1
	ldr	x20, [x1,3656]
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
.L2272:
	cmp	w19, w27
	adrp	x22, .LANCHOR0
	bge	.L2290
	ldr	w0, [x29,108]
	sxtw	x2, w19
	cmp	w19, w0
	bne	.L2273
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
	add	x19, x19, 1696
.L2274:
	cmp	w26, w24
	bge	.L2290
	ldrh	w0, [x21]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w26, w0, lsl 10
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w0, [x1,72]
	cbz	w0, .L2275
	ldr	x0, [x19,16]
	ldr	w28, [x0,12]
	cbz	w28, .L2275
	ldr	x0, [x19,8]
	ldrh	w1, [x1,2546]
	bl	js_hash
	cmp	w28, w0
	beq	.L2275
	str	w27, [x19]
.L2275:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2276
	ldrh	w0, [x20,8]
	cmp	w0, w25
	bcs	.L2276
	ldrh	w2, [x20]
	ldrh	w1, [x23,4]
	cmp	w2, w1
	bne	.L2276
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x19,4]
	str	w1, [x2,x0]
.L2276:
	add	w26, w26, 1
	sxth	w26, w26
	b	.L2274
.L2290:
	mov	x0, x23
	add	x22, x22, :lo12:.LANCHOR0
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x23,2]
	ldrh	w0, [x22,2538]
	cmp	w1, w0
	bne	.L2279
	mov	x0, x23
	bl	ftl_map_blk_alloc_new_blk
	b	.L2279
.L2273:
	ldr	x0, [x29,128]
	lsl	x2, x2, 1
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,3608]
	str	x0, [x21,8]
	add	x0, x24, x2
	str	x0, [x29,120]
	add	x0, x26, :lo12:.LANCHOR0
	ldrh	w1, [x24,x2]
	ldrh	w0, [x0,2538]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x21,4]
	mov	w2, w1
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L2291
	ldrh	w1, [x20]
	mov	w22, 0
	ldrh	w0, [x23,4]
	cmp	w1, w0
	bne	.L2312
	ldrh	w1, [x20,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2312
	mov	w5, w22
.L2281:
	add	x0, x26, :lo12:.LANCHOR0
	ldrh	w0, [x0,2538]
	sub	w0, w0, #1
	cmp	w5, w0
	bge	.L2284
	ldr	x0, [x29,128]
	sbfiz	x1, x5, 3, 32
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x2, [x0,3608]
	ldrh	w0, [x2,x1]
	cmp	w0, w25
	bcs	.L2282
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x1,4]
	str	w1, [x2,x0]
.L2282:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L2281
.L2291:
	mov	w22, 0
.L2312:
	add	x6, x26, :lo12:.LANCHOR0
	ldrh	w0, [x6,2538]
	cmp	w22, w0
	bge	.L2284
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
	cbz	w0, .L2285
	ldr	x0, [x21,16]
	ldr	w2, [x0,12]
	cbz	w2, .L2285
	ldr	x0, [x21,8]
	ldrh	w1, [x6,2546]
	str	x2, [x29,96]
	bl	js_hash
	ldr	x2, [x29,96]
	cmp	w2, w0
	beq	.L2285
	mov	w0, -1
	str	w0, [x21]
.L2285:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L2286
	ldrh	w0, [x20,8]
	cmp	w0, w25
	bcs	.L2286
	ldrh	w2, [x20]
	ldrh	w1, [x23,4]
	cmp	w2, w1
	bne	.L2286
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x21,4]
	str	w1, [x2,x0]
.L2286:
	add	w5, w22, 1
	sxth	w22, w5
	b	.L2312
.L2284:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2272
.L2279:
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
	add	x0, x0, 1760
	add	x29, sp, 0
	ldrh	w2, [x1,2556]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,2582]
	strh	w2, [x0,8]
	ldrh	w2, [x1,2558]
	ldr	x1, [x1,2584]
	str	x1, [x0,16]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	strh	w2, [x0,6]
	ldr	x2, [x1,3736]
	str	x2, [x0,24]
	ldr	x2, [x1,3728]
	ldr	x1, [x1,3744]
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
	ldrh	w21, [x0,2544]
	mov	x25, x2
	ldrh	w0, [x0,2532]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L2316
	adrp	x24, .LANCHOR4
	lsr	w21, w26, w21
	add	x24, x24, :lo12:.LANCHOR4
	mov	w20, 0
	adrp	x27, .LANCHOR2
	add	x24, x24, 1760
.L2317:
	cbz	w28, .L2316
	adrp	x0, .LANCHOR2
	add	x2, x0, :lo12:.LANCHOR2
	ldr	x0, [x2,3744]
	ldr	w4, [x0,w21,uxtw 2]
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2542]
	uxth	w0, w28
	udiv	w22, w26, w1
	msub	w22, w22, w1, w26
	sub	w19, w1, w22
	uxth	w19, w19
	cmp	w19, w28
	csel	w19, w0, w19, hi
	cmp	w19, w1
	beq	.L2319
	cbz	w4, .L2319
	ldr	x0, [x2,3624]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w4, [x29,124]
	str	xzr, [x29,136]
	bl	FlashReadPages
	b	.L2320
.L2319:
	add	x0, x27, :lo12:.LANCHOR2
	add	x2, x23, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x0,3624]
	ldrh	w2, [x2,2546]
	bl	ftl_memset
.L2320:
	add	x5, x27, :lo12:.LANCHOR2
	lsl	w4, w19, 9
	ubfiz	x22, x22, 9, 16
	mov	w2, w4
	mov	x1, x25
	str	x4, [x29,104]
	ldr	x0, [x5,3624]
	sub	w28, w28, w19
	str	x5, [x29,96]
	add	w26, w26, w19
	add	x0, x0, x22
	bl	ftl_memcpy
	ldr	x5, [x29,96]
	mov	w1, w21
	mov	x0, x24
	add	w21, w21, 1
	ldr	x2, [x5,3624]
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	x4, [x29,104]
	csinv	w20, w20, wzr, ne
	add	x25, x25, x4, sxtw
	b	.L2317
.L2316:
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
	add	x2, x0, 848
	add	x29, sp, 0
	ldr	w3, [x0,848]
	cmp	w3, w1
	bne	.L2330
	mov	w1, 96
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	ldr	w1, [x0,736]
	str	w1, [x2,88]
	ldr	w1, [x0,740]
	str	w1, [x2,92]
	ldr	w1, [x0,732]
	str	w1, [x2,8]
	ldr	w1, [x0,720]
	str	w1, [x2,12]
	ldr	w1, [x0,712]
	str	w1, [x2,16]
	ldr	w1, [x0,728]
	str	w1, [x2,20]
	ldr	w1, [x0,756]
	str	w1, [x2,28]
	ldr	w1, [x0,440]
	str	w1, [x2,32]
	ldr	w1, [x0,716]
	str	w1, [x2,36]
	ldr	w1, [x0,724]
	str	w1, [x2,40]
	ldr	w1, [x0,764]
	str	w1, [x2,44]
	ldr	w1, [x0,768]
	str	w1, [x2,48]
	ldr	w1, [x0,840]
	ldr	w0, [x0,3556]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2330:
	ldp	x29, x30, [sp], 16
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x1, .LANCHOR0+2460
	adrp	x2, .LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w3, [x1,#:lo12:.LANCHOR0+2460]
	mov	w1, 32
	cbz	w3, .L2333
	add	x3, x2, :lo12:.LANCHOR2
	ldr	w3, [x3,768]
	cmp	w3, 39
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2333:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,1824]
	cmp	w4, 31
	bhi	.L2334
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,1824]
.L2334:
	cbnz	w0, .L2335
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,3696]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2336
.L2335:
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x0, [x2,3696]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	ldr	x0, [x2,3696]
	ldrh	w1, [x2,3680]
	str	wzr, [x0,4]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	ldr	x2, [x2,3696]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2336:
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
	ldrh	w2, [x0,2532]
	str	x1, [x29,136]
	cmp	w3, w2
	ldrh	w20, [x0,2544]
	mov	w0, -1
	bhi	.L2341
	adrp	x27, .LANCHOR4
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	add	x27, x27, :lo12:.LANCHOR4
	str	x0, [x29,128]
	lsr	w20, w23, w20
	add	x0, x27, 1760
	mov	w21, 0
	str	x0, [x29,120]
.L2342:
	cbz	w22, .L2350
	adrp	x25, .LANCHOR2
	add	x4, x25, :lo12:.LANCHOR2
	ldr	x0, [x4,3744]
	ldr	w3, [x0,w20,uxtw 2]
	ldr	x0, [x29,136]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w19, [x0,2542]
	uxth	w0, w22
	udiv	w24, w23, w19
	msub	w24, w24, w19, w23
	sub	w19, w19, w24
	uxth	w19, w19
	cmp	w19, w22
	csel	w19, w0, w19, hi
	lsl	w28, w19, 9
	cbz	w3, .L2344
	ldr	x0, [x4,3624]
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
	ldr	w0, [x27,1696]
	csinv	w21, w21, wzr, ne
	cmp	w0, 256
	bne	.L2346
	ldr	x0, [x29,128]
	mov	w1, w20
	mov	w2, w3
	bl	printk
	ldr	x4, [x29,112]
	mov	w1, w20
	ldr	x0, [x29,120]
	ldr	x2, [x4,3624]
	bl	FtlMapWritePage
.L2346:
	add	x25, x25, :lo12:.LANCHOR2
	ubfiz	x24, x24, 9, 16
	mov	x0, x26
	mov	w2, w28
	ldr	x1, [x25,3624]
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L2347
.L2344:
	mov	x0, x26
	mov	w1, w3
	mov	w2, w28
	bl	ftl_memset
.L2347:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x26, x26, x28, sxtw
	b	.L2342
.L2350:
	mov	w0, w21
.L2341:
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
	ldrh	w1, [x19,3680]
	ldr	x2, [x19,3696]
	bl	FtlVendorPartRead
	ldr	x0, [x19,3696]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L2352
	adrp	x0, .LC75
	adrp	x1, .LC133
	add	x1, x1, :lo12:.LC133
	add	x0, x0, :lo12:.LC75
	bl	printk
	ldrh	w2, [x19,3680]
	mov	w1, 0
	ldr	x0, [x19,3696]
	lsl	w2, w2, 9
	bl	ftl_memset
.L2352:
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
	add	x21, x20, 848
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,848]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L2354
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,848]
.L2354:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,848]
	cmp	w3, w2
	bne	.L2355
	ldr	w2, [x0,936]
	str	w2, [x0,736]
	ldr	w2, [x0,940]
	str	w2, [x0,740]
	ldr	w2, [x0,856]
	str	w2, [x0,732]
	ldr	w2, [x0,860]
	str	w2, [x0,720]
	ldr	w2, [x0,864]
	str	w2, [x0,712]
	ldr	w2, [x0,868]
	str	w2, [x0,728]
	ldr	w2, [x0,876]
	str	w2, [x0,756]
	ldr	w2, [x0,880]
	str	w2, [x0,440]
	ldr	w2, [x0,884]
	str	w2, [x0,716]
	ldr	w2, [x0,888]
	str	w2, [x0,724]
	ldr	w2, [x0,892]
	str	w2, [x0,764]
	ldr	w1, [x0,908]
	ldr	w2, [x0,896]
	str	w2, [x0,768]
	str	w1, [x0,840]
.L2355:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 34661
	movk	w1, 0x1234, lsl 16
	add	x0, x0, 848
	adrp	x20, .LANCHOR0
	ldr	w2, [x0,68]
	str	wzr, [x0,2708]
	cmp	w2, w1
	bne	.L2356
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w2, [x1,196]
	cbz	w2, .L2357
	str	wzr, [x0,68]
	bl	Ftl_save_ext_data
	b	.L2356
.L2357:
	mov	w0, 1
	str	w0, [x1,2460]
	adrp	x0, .LC75
	adrp	x1, .LC134
	add	x0, x0, :lo12:.LC75
	add	x1, x1, :lo12:.LC134
	bl	printk
.L2356:
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x21, [sp,32]
	ldrh	w1, [x20,2528]
	ldr	w2, [x19,752]
	ldr	w0, [x19,756]
	madd	w0, w1, w2, w0
	ldrh	w1, [x20,2476]
	udiv	w0, w0, w1
	str	w0, [x19,760]
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
	cbz	w1, .L2358
	adrp	x3, .LANCHOR2
	str	wzr, [x0,56]
	add	x3, x3, :lo12:.LANCHOR2
	ldr	w1, [x3,416]
	cbnz	w1, .L2358
	adrp	x2, .LANCHOR4
	sub	w20, w20, #1
	add	x1, x2, :lo12:.LANCHOR4
	mov	x21, x0
	uxth	w20, w20
	ldr	x0, [x3,3616]
	ldr	x3, [x3,3656]
	add	x1, x1, 1696
	str	x3, [x1,16]
	ubfiz	x3, x20, 2, 16
	str	x0, [x1,8]
	mov	x19, x2
	ldr	w3, [x4,x3]
	str	w3, [x1,4]
	cbz	w3, .L2362
	mov	x0, x1
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L2363
.L2362:
	adrp	x2, .LANCHOR0+2546
	mov	w1, 255
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2546]
	bl	ftl_memset
.L2363:
	add	x2, x19, :lo12:.LANCHOR4
	mov	x0, x21
	mov	w1, w20
	ldr	x2, [x2,1704]
	bl	FtlMapWritePage
.L2358:
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
	cbnz	w1, .L2367
.L2369:
	add	x0, x4, :lo12:.LANCHOR4
	ldr	w1, [x0,1628]
	cmp	w3, w1
	bcs	.L2367
	ubfx	x5, x3, 5, 11
	ldr	x0, [x0,1592]
	lsl	x5, x5, 2
	lsl	w6, w7, w3
	add	w3, w3, 1
	ldr	w1, [x0,x5]
	uxth	w3, w3
	orr	w6, w1, w6
	str	w6, [x0,x5]
	b	.L2369
.L2367:
	add	x4, x4, :lo12:.LANCHOR4
	mov	x0, x19
	ldr	x1, [x4,1592]
	bl	ftl_memcpy
	mov	w2, 4
	adrp	x0, .LC135
	mov	x1, x19
	add	x0, x0, :lo12:.LC135
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
	ldrh	w0, [x4,182]
	mov	w25, w2
	ldrh	w3, [x4,180]
	adrp	x4, .LANCHOR4
	mov	x21, x4
	mov	w26, 61664
	mul	w3, w3, w0
	add	x0, x4, :lo12:.LANCHOR4
	uxth	w3, w3
	sub	w20, w3, #1
	ldr	x1, [x0,1592]
	mul	w24, w23, w3
	ldr	x0, [x0,1648]
	uxth	w20, w20
	str	x1, [x29,96]
	sub	w19, w3, #16
	str	x0, [x29,104]
.L2372:
	cmp	w20, w19
	ble	.L2380
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L2373
	add	x0, x21, :lo12:.LANCHOR4
	ldr	x0, [x0,1648]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L2373
	mov	w0, w22
	cbz	x22, .L2374
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.17
	b	.L2374
.L2373:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2372
.L2380:
	mov	w0, -1
.L2374:
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
	adrp	x0, .LANCHOR0+1256
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	ldr	x0, [x0,#:lo12:.LANCHOR0+1256]
	ldrb	w2, [x0,13]
	ldrh	w19, [x0,14]
	add	x0, x20, :lo12:.LANCHOR4
	mul	w19, w2, w19
	ldr	x0, [x0,1640]
	uxth	w19, w19
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L2382
.L2386:
	mov	w1, 0
	b	.L2383
.L2382:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L2384:
	cmp	w2, w19
	bge	.L2383
	add	x3, x20, :lo12:.LANCHOR4
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,1640]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L2385
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L2385:
	cmp	w1, w0
	bcs	.L2386
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2384
.L2383:
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
	ldr	w21, [x0,416]
	cbnz	w21, .L2392
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR4
	add	x24, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	add	x24, x24, 2616
	add	x20, x20, 1696
	mov	x25, x24
	mov	x26, x20
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L2393:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22,2490]
	cmp	w21, w0
	bcs	.L2411
	add	x1, x23, :lo12:.LANCHOR2
	ldrh	w28, [x25,12]
	ldr	x0, [x1,3608]
	ldr	x27, [x1,3656]
	mov	w1, 65535
	str	x0, [x20,8]
	cmp	w28, w1
	str	x27, [x20,16]
	beq	.L2394
	ldrh	w0, [x22,2534]
	mov	w1, 1
	mov	w2, w1
	madd	w28, w21, w0, w28
	lsl	w0, w28, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x22,2534]
	ldr	x0, [x24,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L2395
.L2394:
	mov	w1, w21
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x22,2534]
.L2397:
	sub	w22, w22, #1
	uxth	w22, w22
.L2396:
	add	x28, x19, :lo12:.LANCHOR0
	ldrh	w0, [x28,2534]
	madd	w0, w21, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2397
	add	x3, x23, :lo12:.LANCHOR2
	mov	w1, 0
	mov	w2, 16
	strh	w22, [x25,12]
	str	x3, [x29,104]
	ldr	x0, [x3,3656]
	bl	ftl_memset
	str	wzr, [x27,4]
	mov	w0, -3872
	strh	w0, [x27]
	ldr	x3, [x29,104]
	ldrh	w0, [x25,12]
	strh	w0, [x27,2]
	ldrh	w0, [x28,2534]
	ldrh	w1, [x25,12]
	ldrh	w2, [x3,3784]
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
	bne	.L2395
	mov	w0, w28
	bl	FtlBbmMapBadBlock
	b	.L2396
.L2395:
	mov	w0, w28
	add	w21, w21, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L2393
.L2411:
	mov	w20, 0
.L2400:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,2550]
	cmp	w0, w20
	bls	.L2412
	mov	w0, w20
	add	w20, w20, 1
	bl	FtlBbmMapBadBlock
	uxth	w20, w20
	b	.L2400
.L2412:
	ldrh	w20, [x21,2628]
	mov	w24, 65535
	sub	w20, w20, #1
	uxth	w20, w20
.L2402:
	add	x22, x21, 2616
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L2406
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2403
	mov	w0, w20
	bl	FlashTestBlk
	cbz	w0, .L2404
	mov	w0, w20
	bl	FtlBbmMapBadBlock
	b	.L2403
.L2404:
	ldrh	w0, [x21,2616]
	cmp	w0, w24
	bne	.L2405
	strh	w20, [x21,2616]
	b	.L2403
.L2405:
	strh	w20, [x22,4]
	b	.L2406
.L2403:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2402
.L2406:
	add	x23, x23, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 2
	ldr	x1, [x23,424]
	ldrh	w0, [x19,2616]
	str	wzr, [x19,2624]
	lsl	w0, w0, 10
	str	w0, [x1,4]
	strh	wzr, [x19,2618]
	ldr	x0, [x23,424]
	ldrh	w1, [x19,2620]
	lsl	w1, w1, 10
	str	w1, [x0,60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x19,2616]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19,2620]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19,2618]
	ldr	w0, [x19,2624]
	ldrh	w1, [x19,2620]
	add	w0, w0, 1
	str	w0, [x19,2624]
	ldrh	w0, [x19,2616]
	strh	w0, [x19,2620]
	strh	w1, [x19,2616]
	bl	FtlBbmTblFlush
.L2392:
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
	ldr	w4, [x3,2608]
	ldrh	w27, [x3,2544]
	cmp	w0, w4
	bcs	.L2414
	add	w27, w27, 7
	adrp	x20, .LANCHOR2
	lsr	w24, w0, w27
	str	w0, [x29,116]
	add	x0, x20, :lo12:.LANCHOR2
	str	w2, [x29,112]
	mov	x26, x1
	uxth	w23, w24
	ldrh	w2, [x3,2574]
	ldr	x22, [x0,696]
	mov	x0, 0
	b	.L2415
.L2414:
	mov	w0, -1
	cbnz	w2, .L2416
	str	w0, [x1]
	b	.L2416
.L2420:
	add	x0, x0, 1
	add	x3, x22, x0, lsl 4
	ldrh	w3, [x3,-16]
	cmp	w3, w23
	beq	.L2417
.L2415:
	uxth	x19, w0
	cmp	w19, w2
	bcc	.L2420
	bl	select_l2p_ram_region
	uxth	x19, w0
	ubfiz	x21, x19, 4, 16
	mov	w1, 65535
	add	x2, x22, x21
	ldrh	w3, [x22,x21]
	cmp	w3, w1
	bne	.L2428
.L2421:
	add	x28, x20, :lo12:.LANCHOR2
	ubfiz	x0, x23, 2, 16
	ldr	x1, [x28,3752]
	ldr	w25, [x1,x0]
	cbnz	w25, .L2422
	ldr	x1, [x29,120]
	ldr	x0, [x28,696]
	add	x22, x1, :lo12:.LANCHOR0
	mov	w1, 255
	add	x0, x0, x21
	ldrh	w2, [x22,2546]
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x28,696]
	strh	w23, [x0,x21]
	ldr	x0, [x28,696]
	add	x21, x0, x21
	str	w25, [x21,4]
.L2417:
	mov	x0, 1
	lsl	x27, x0, x27
	ldr	w0, [x29,116]
	sub	w27, w27, #1
	and	w25, w0, w27
	ldr	w0, [x29,112]
	uxth	x25, w25
	cbnz	w0, .L2418
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,696]
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0,8]
	ldr	w0, [x0,x25,lsl 2]
	str	w0, [x26]
	b	.L2419
.L2418:
	add	x1, x20, :lo12:.LANCHOR2
	lsl	x0, x19, 4
	ldr	w3, [x26]
	ldr	x2, [x1,696]
	add	x2, x2, x0
	ldr	x2, [x2,8]
	str	w3, [x2,x25,lsl 2]
	strh	w23, [x1,704]
	ldr	x2, [x1,696]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2419:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x20,696]
	add	x19, x0, x19, lsl 4
	mov	w0, 0
	ldr	w1, [x19,4]
	cmn	w1, #1
	beq	.L2416
	add	w1, w1, 1
	str	w1, [x19,4]
	b	.L2416
.L2428:
	ldr	w1, [x2,4]
	tbz	w1, #31, .L2421
	bl	flush_l2p_region
	b	.L2421
.L2422:
	ldr	x0, [x28,696]
	adrp	x6, .LANCHOR4
	add	x6, x6, :lo12:.LANCHOR4
	mov	w1, 1
	add	x0, x0, x21
	add	x22, x6, 1696
	mov	w2, w1
	str	x6, [x29,104]
	str	w25, [x6,1700]
	ldr	x0, [x0,8]
	str	x0, [x6,1704]
	ldr	x0, [x28,3656]
	str	x0, [x6,1712]
	mov	x0, x22
	bl	FlashReadPages
	ldr	x0, [x22,16]
	ldr	x6, [x29,104]
	ldrh	w0, [x0,8]
	cmp	w0, w23
	beq	.L2423
	adrp	x0, .LC136
	uxth	w1, w24
	mov	w2, w25
	add	x0, x0, :lo12:.LC136
	bl	printk
	ldr	x1, [x22,16]
	mov	w2, 4
	adrp	x0, .LC99
	mov	w3, w2
	add	x0, x0, :lo12:.LC99
	bl	rknand_print_hex
	ldr	x0, [x29,120]
	mov	w2, 4
	ldr	x1, [x28,3752]
	add	x22, x0, :lo12:.LANCHOR0
	adrp	x0, .LC137
	add	x0, x0, :lo12:.LC137
	ldrh	w3, [x22,2572]
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x28,416]
	b	.L2424
.L2423:
	ldr	w0, [x6,1696]
	cmp	w0, 256
	bne	.L2424
	and	w24, w24, 65535
	adrp	x0, .LC138
	mov	w1, w24
	mov	w2, w25
	add	x0, x0, :lo12:.LC138
	bl	printk
	ldr	x2, [x28,696]
	add	x0, x28, 3792
	mov	w1, w24
	add	x2, x2, x21
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L2424:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x0, [x1,696]
	add	x0, x0, x21
	str	wzr, [x0,4]
	ldr	x0, [x1,696]
	strh	w23, [x0,x21]
	b	.L2417
.L2416:
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
	ldr	w0, [x23,416]
	cbnz	w0, .L2429
	ldrh	w2, [x23,556]
	cbz	w2, .L2431
	ldrb	w1, [x23,560]
	cbnz	w1, .L2431
	adrp	x21, .LANCHOR0
	ldrb	w1, [x23,559]
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w3, [x21,2536]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L2431
	ldrb	w24, [x23,562]
	ldr	w22, [x21,2608]
	ldrh	w25, [x21,2468]
	cbnz	w24, .L2429
	sub	w22, w22, #1
	add	x1, x29, 84
	mov	w0, w22
	mov	w2, w24
	bl	log2phys
	ldr	x20, [x23,3656]
	ldr	w0, [x29,84]
	ldr	x1, [x23,3608]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w22, [x29,112]
	str	x1, [x29,96]
	str	x20, [x29,104]
	str	w24, [x20,4]
	beq	.L2433
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w24
	bl	FlashReadPages
	b	.L2434
.L2433:
	ldr	x0, [x23,3608]
	mov	w1, 255
	ldrh	w2, [x21,2546]
	bl	ftl_memset
.L2434:
	add	x24, x19, :lo12:.LANCHOR2
	mov	w0, -3947
	lsl	w25, w25, 2
	strh	w0, [x20]
	mov	w23, 0
	add	x21, x24, 552
.L2435:
	cmp	w23, w25
	beq	.L2436
	ldrh	w0, [x21,4]
	cbz	w0, .L2436
	ldr	w0, [x29,92]
	add	w23, w23, 1
	str	w0, [x20,12]
	ldrh	w0, [x21]
	strh	w0, [x20,2]
	mov	x0, x21
	str	w22, [x20,8]
	bl	get_new_active_ppa
	str	w0, [x29,92]
	ldr	w1, [x24,748]
	mov	w2, 0
	str	w1, [x20,4]
	mov	w3, w2
	add	w1, w1, 1
	add	x0, x29, 88
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x24,748]
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x21]
	bl	decrement_vpc_count
	b	.L2435
.L2436:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,562]
	b	.L2429
.L2431:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,562]
.L2429:
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
.L2447:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2574]
	cmp	w0, w19
	bls	.L2450
	add	x1, x21, :lo12:.LANCHOR2
	ubfiz	x0, x19, 4, 16
	ldr	x1, [x1,696]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2448
	mov	w0, w19
	bl	flush_l2p_region
.L2448:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2447
.L2450:
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
	ldr	w1, [x2,416]
	cbnz	w1, .L2452
	mov	x21, x0
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2453
	ubfiz	x1, x20, 1, 16
	ldr	x0, [x2,512]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2454
	mov	w0, w20
	bl	INSERT_DATA_LIST
	b	.L2453
.L2454:
	mov	w0, w20
	bl	INSERT_FREE_LIST
.L2453:
	add	x2, x19, :lo12:.LANCHOR2
	strb	wzr, [x21,8]
	add	x0, x2, 600
	cmp	x21, x0
	beq	.L2455
	adrp	x1, .LANCHOR0
	add	x4, x1, :lo12:.LANCHOR0
	ldrh	w3, [x4,2488]
	cmp	w3, 1
	beq	.L2455
	ldrb	w0, [x4,196]
	cbz	w0, .L2456
.L2455:
	mov	w0, 1
	strb	w0, [x21,8]
	b	.L2457
.L2456:
	add	x0, x2, 552
	cmp	x21, x0
	bne	.L2457
	cmp	w3, 3
	beq	.L2459
	ldr	w0, [x2,840]
	cmp	w0, 1
	bne	.L2460
.L2459:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x2,560]
.L2460:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x1,2460]
	cbz	w0, .L2457
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,768]
	cmp	w0, 39
	bhi	.L2457
	mov	w0, 1
	strb	w0, [x1,560]
.L2457:
	add	x3, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w1, [x3,3872]
	cmp	w1, w0
	beq	.L2462
	cmp	w20, w1
	bne	.L2463
	ubfiz	x2, x1, 1, 16
	ldr	x0, [x3,512]
	ldrh	w0, [x0,x2]
	cbz	w0, .L2464
.L2463:
	mov	w0, w1
	bl	update_vpc_list
.L2464:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,3872]
.L2462:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2452:
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
	stp	x29, x30, [sp, -96]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC107
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 80
	add	x0, x0, :lo12:.LC107
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	mov	w19, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	add	x21, x20, :lo12:.LANCHOR4
	bl	printk
	add	x0, x21, 1832
	mov	w1, 0
	mov	x2, 8192
	adrp	x22, .LANCHOR0
	bl	memset
.L2478:
	add	x0, x22, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR0
	ldr	w0, [x0,2608]
	cmp	w19, w0
	bcs	.L2495
	mov	w0, w19
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	cmn	w0, #1
	beq	.L2479
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x2, x21, 1832
	ldrh	w1, [x2,w0,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x2,w0,sxtw 1]
.L2479:
	add	w19, w19, 1
	b	.L2478
.L2495:
	adrp	x21, .LC139
	mov	w19, 0
	adrp	x22, .LANCHOR2
	add	x25, x20, :lo12:.LANCHOR4
	add	x21, x21, :lo12:.LC139
.L2481:
	add	x0, x24, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR2
	ldrh	w0, [x0,2476]
	cmp	w0, w19
	bls	.L2496
	add	x1, x22, :lo12:.LANCHOR2
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x1,512]
	ldrh	w2, [x1,x0]
	add	x0, x25, 1832
	ldrh	w3, [x0,w19,sxtw 1]
	cmp	w2, w3
	beq	.L2482
	mov	x0, x21
	mov	w1, w19
	bl	printk
.L2482:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2481
.L2496:
	add	x1, x23, :lo12:.LANCHOR2
	ldr	x0, [x1,536]
	cbz	x0, .L2477
	ldr	x19, [x1,496]
	adrp	x22, .LC140
	ldrh	w24, [x1,544]
	mov	w21, 0
	sub	x19, x0, x19
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	add	x22, x22, :lo12:.LC140
	add	x20, x20, :lo12:.LANCHOR4
	mov	w25, 6
	madd	x19, x0, x19, x19
	mov	w26, 65535
	uxth	w19, w19
.L2485:
	cmp	w21, w24
	beq	.L2477
	add	x1, x23, :lo12:.LANCHOR2
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x1,512]
	ldrh	w2, [x1,x0]
	cbz	w2, .L2486
	add	x3, x20, 1832
	mov	x0, x22
	mov	w1, w19
	ldrh	w3, [x3,w19,sxtw 1]
	bl	printk
.L2486:
	add	x0, x23, :lo12:.LANCHOR2
	umull	x19, w19, w25
	ldr	x0, [x0,496]
	ldrh	w19, [x0,x19]
	cmp	w19, w26
	beq	.L2477
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2485
.L2477:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	2
	.global	Ftlscanalldata
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	sub	sp, sp, #80
	adrp	x0, .LC141
	add	x0, x0, :lo12:.LC141
	mov	w1, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR4
	mov	w20, 0
	stp	x21, x22, [sp,48]
	add	x19, x19, :lo12:.LANCHOR4
	adrp	x21, .LC142
	add	x21, x21, :lo12:.LC142
	add	x19, x19, 1696
	bl	printk
.L2498:
	adrp	x0, .LANCHOR0+2608
	ldr	w0, [x0,#:lo12:.LANCHOR0+2608]
	cmp	w20, w0
	bcs	.L2505
	mov	w0, w20
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	and	w0, w20, 2047
	cbnz	w0, .L2499
	ldr	w2, [x29,60]
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L2499:
	ldr	w0, [x29,60]
	cmn	w0, #1
	beq	.L2501
	adrp	x1, .LANCHOR2
	str	w0, [x19,4]
	add	x1, x1, :lo12:.LANCHOR2
	mov	w2, 0
	str	w20, [x19,24]
	str	wzr, [x19]
	ldr	x0, [x1,3608]
	ldr	x22, [x1,3656]
	mov	w1, 1
	str	x0, [x19,8]
	mov	x0, x19
	str	x22, [x19,16]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2502
	cmp	w0, 256
	beq	.L2502
	ldr	w0, [x22,8]
	cmp	w0, w20
	beq	.L2501
.L2502:
	adrp	x2, .LANCHOR4
	mov	w1, w20
	add	x2, x2, :lo12:.LANCHOR4
	add	x2, x2, 1696
	ldr	x7, [x2,8]
	ldr	x6, [x2,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x2,4]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
.L2501:
	add	w20, w20, 1
	b	.L2498
.L2505:
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
	ldr	x3, [x2,512]
	ldrh	w1, [x3,x22]
	cbnz	w1, .L2507
	ldr	x19, [x2,536]
	cbz	x19, .L2508
	ldrh	w3, [x2,544]
	mov	x4, -6148914691236517206
	ldr	x2, [x2,496]
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L2509:
	cmp	w1, w3
	beq	.L2508
	cmp	w19, w0
	bne	.L2510
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w19
	add	x0, x21, 536
	bl	List_remove_node
	ldrh	w0, [x21,544]
	sub	w0, w0, #1
	strh	w0, [x21,544]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x1, [x21,512]
	ldrh	w0, [x1,x22]
	add	w0, w0, 1
	strh	w0, [x1,x22]
	b	.L2508
.L2510:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2508
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2509
.L2507:
	add	w1, w1, 1
	strh	w1, [x3,x22]
.L2508:
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
	beq	.L2676
	ldrh	w0, [x19,2]
	adrp	x23, .LANCHOR0
	str	w0, [x29,188]
	ldrb	w0, [x19,6]
	str	w0, [x29,176]
	add	x0, x23, :lo12:.LANCHOR0
	ldr	w1, [x29,188]
	ldrh	w0, [x0,2536]
	cmp	w0, w1
	bne	.L2518
	strh	wzr, [x19,4]
	b	.L2682
.L2518:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2519:
	cmp	w0, w2
	bne	.L2685
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2519
.L2685:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2521
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L2522
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, w20
	ldrb	w1, [x0,196]
	cbnz	w1, .L2596
	add	x0, x0, 200
	ldrh	w24, [x0,w20,sxtw 1]
	b	.L2596
.L2521:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L2522
	mov	w24, w0
.L2596:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w26, 0
	mov	w8, 65535
	mov	w9, 56
	adrp	x10, .LANCHOR2
	ldrh	w4, [x0,2468]
	mov	w11, 4
	ldrh	w7, [x0,2548]
	add	x4, x19, x4, lsl 1
	b	.L2524
.L2522:
	strh	wzr, [x19,2]
.L2682:
	strb	wzr, [x19,6]
	b	.L2676
.L2526:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2525
	add	x5, x10, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x0, w26, w9
	add	w3, w26, 1
	ldr	x6, [x5,3584]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x5,3584]
	ldr	x5, [x5,1400]
	add	x0, x1, x0
	mul	w1, w26, w7
	uxth	w26, w3
	str	xzr, [x0,8]
	sdiv	w1, w1, w11
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L2525:
	add	x2, x2, 2
.L2524:
	cmp	x2, x4
	bne	.L2526
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2527
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2527:
	adrp	x21, .LANCHOR2
	ldr	w2, [x29,184]
	add	x22, x21, :lo12:.LANCHOR2
	mov	w1, w26
	mov	x27, 0
	mov	w25, 65535
	mov	x28, 56
	ldr	x0, [x22,3584]
	bl	FlashReadPages
	ldr	w0, [x22,748]
	adrp	x22, .LC144
	str	w0, [x29,168]
	add	x22, x22, :lo12:.LC144
	uxth	w0, w24
	str	w0, [x29,160]
.L2528:
	uxth	w2, w27
	cmp	w2, w26
	bcs	.L2535
	add	x6, x21, :lo12:.LANCHOR2
	mul	x0, x27, x28
	ldr	x4, [x6,3584]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2529
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2530
	ldr	w1, [x6,748]
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
	cbz	w0, .L2530
	add	w4, w4, 1
	str	w4, [x6,748]
.L2530:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2531
.L2535:
	cmp	w2, w26
	bne	.L2677
	add	x0, x21, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x0, [x0,3584]
	ldr	w0, [x0,4]
	b	.L2678
.L2529:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w25, [x29,160]
	adrp	x0, .LANCHOR5
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LANCHOR5
	strh	w1, [x0,1320]
.L2531:
	add	x27, x27, 1
	b	.L2528
.L2677:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w20, w20
	umull	x2, w2, w1
	ldr	x0, [x0,3584]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2678:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2537
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w2, [x0,196]
	cbnz	w2, .L2537
	add	x0, x0, 200
	ldrh	w20, [x0,w20,sxtw 1]
.L2537:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w0, [x0,2536]
	cmp	w0, w20
	bne	.L2538
	strh	w20, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2538:
	ldrh	w0, [x29,176]
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w20, w0
	bne	.L2539
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2683
.L2539:
	ldr	w0, [x29,168]
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w25, w0
	bne	.L2540
	cbnz	w1, .L2541
.L2540:
	add	x0, x21, :lo12:.LANCHOR2
	uxth	w5, w24
	ldr	w1, [x0,3884]
	cmn	w1, #1
	bne	.L2542
	str	w22, [x0,3884]
.L2542:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w25, [x29,188]
	ldr	w6, [x0,3884]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w24, uxth
	bge	.L2543
	sub	w25, w5, #7
	uxth	w25, w25
.L2543:
	mov	w3, -1
	add	x28, x21, :lo12:.LANCHOR2
	mov	w27, w3
	mov	w4, 65535
	mov	w7, 56
	add	x8, x28, 1500
.L2544:
	cmp	w25, w5
	bhi	.L2557
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, 0
	ldrh	w2, [x0,2468]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2558:
	cmp	x0, x2
	beq	.L2686
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2545
	umull	x9, w24, w7
	ldr	x10, [x28,3584]
	add	w24, w24, 1
	orr	w1, w25, w1, lsl 10
	add	x9, x10, x9
	uxth	w24, w24
	str	w1, [x9,4]
.L2545:
	add	x0, x0, 2
	b	.L2558
.L2686:
	ldr	x0, [x28,3584]
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
	ldrb	w9, [x0,196]
	ldr	x0, [x28,3584]
	ldr	x4, [x29,144]
	ldr	x7, [x29,136]
	add	x0, x0, 16
	ldr	x8, [x29,128]
.L2547:
	cmp	w1, w24
	beq	.L2687
	ldr	w2, [x0,-16]
	cbnz	w2, .L2548
	ldr	x2, [x0]
	ldrh	w11, [x2]
	cmp	w11, w4
	beq	.L2549
	ldr	w2, [x2,4]
	cmn	w2, #1
	beq	.L2549
	ldr	w27, [x28,3884]
	cmn	w3, #1
	str	w2, [x28,3884]
	bne	.L2549
	ldrh	w2, [x8,x10,lsl 1]
	cmp	w2, w4
	bne	.L2550
	cbz	w9, .L2549
.L2550:
	cmp	w27, w22
	csinv	w3, w27, wzr, ne
	b	.L2549
.L2548:
	adrp	x0, .LANCHOR5+1320
	ldrh	w1, [x19]
	strh	w1, [x0,#:lo12:.LANCHOR5+1320]
	ldrb	w0, [x19,8]
	cbnz	w0, .L2541
	add	x0, x21, :lo12:.LANCHOR2
	add	x1, x0, 1500
	ldrh	w2, [x1,w25,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2552
	cmn	w3, #1
	beq	.L2553
	str	w3, [x0,3884]
	b	.L2541
.L2553:
	cmp	w6, w22
	beq	.L2554
	str	w6, [x0,3884]
	b	.L2541
.L2554:
	ldr	w1, [x0,3884]
	b	.L2684
.L2552:
	cmp	w27, w22
	beq	.L2555
	cmn	w27, #1
	beq	.L2541
	str	w27, [x0,3884]
	b	.L2541
.L2555:
	ldr	w1, [x0,3884]
	cmp	w1, w22
	beq	.L2541
.L2684:
	sub	w1, w1, #1
	b	.L2679
.L2549:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2547
.L2687:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2544
.L2557:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, -1
.L2679:
	str	w1, [x0,3884]
.L2541:
	adrp	x0, .LANCHOR5
	str	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR5
	adrp	x0, .LC146
	add	x0, x0, :lo12:.LC146
	ldr	w24, [x29,188]
	str	x0, [x29,128]
	adrp	x0, .LC145
	mov	w2, 1
	add	x0, x0, :lo12:.LC145
	strh	w2, [x1,1322]
	str	x0, [x29,120]
.L2559:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x1, x19
	str	wzr, [x29,176]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,2468]
	ldrb	w6, [x0,196]
	add	x3, x19, x3, lsl 1
.L2560:
	cmp	x1, x3
	beq	.L2688
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2561
	ldr	w0, [x29,176]
	add	x5, x21, :lo12:.LANCHOR2
	orr	w2, w24, w2, lsl 10
	ldr	x4, [x5,3584]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2562
	cbz	w6, .L2562
	ldr	x2, [x5,3584]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2562:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2561:
	add	x1, x1, 2
	b	.L2560
.L2688:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,3584]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2564:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2689
	ldr	x0, [x29,160]
	mov	x25, 56
	mul	x25, x0, x25
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x28, [x0,3584]
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
	bcc	.L2565
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2600
	ldr	w2, [x29,188]
	cmp	w24, w2
	beq	.L2565
.L2600:
	cmp	w0, w26
	bne	.L2601
	cmp	w24, w20
	beq	.L2567
.L2601:
	ldr	w0, [x28,x25]
	cmn	w0, #1
	beq	.L2569
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	beq	.L2570
	ldrh	w0, [x19]
	b	.L2680
.L2570:
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2571
	add	x27, x21, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,748]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2571
	add	w0, w22, 1
	str	w0, [x27,748]
.L2571:
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
	ldr	w1, [x3,3884]
	cmn	w1, #1
	beq	.L2572
	mov	w0, w22
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,168]
	cbz	w0, .L2572
	cmn	w28, #1
	beq	.L2573
	ldr	x0, [x3,3584]
	mov	w2, 0
	str	x3, [x29,112]
	add	x0, x0, x25
	str	w28, [x0,4]
	ldr	x1, [x0,16]
	ldr	x0, [x3,3584]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x3, [x29,112]
	ldr	x0, [x29,168]
	ldr	x2, [x3,3584]
	ldr	w0, [x0,4]
	str	w0, [x29,144]
	add	x4, x2, x25
	ldr	w0, [x2,x25]
	cmn	w0, #1
	bne	.L2574
	b	.L2575
.L2573:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2565
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
	b	.L2565
.L2574:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2575
	ldr	w0, [x3,3884]
	ldr	w1, [x29,144]
	str	x2, [x29,96]
	str	x4, [x29,104]
	str	x3, [x29,112]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	ldr	x2, [x29,96]
	cbz	w0, .L2575
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2580
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2575
	cmn	w0, #1
	beq	.L2578
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,3584]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	b	.L2579
.L2578:
	str	w0, [x2,x25]
.L2579:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,3584]
	ldr	w1, [x1,x25]
	cmn	w1, #1
	beq	.L2580
	ldr	x1, [x29,168]
	ldr	w0, [x0,3884]
	ldr	w25, [x1,4]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2580
	ldr	w0, [x29,144]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2575
.L2580:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2575:
	mov	w0, -1
	str	w0, [x29,196]
	b	.L2582
.L2572:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2582
	cmn	w28, #1
	beq	.L2584
	add	x0, x23, :lo12:.LANCHOR0
	ubfx	x28, x28, 10, 21
	ldr	w0, [x0,2484]
	cmp	w28, w0
	bcs	.L2565
.L2584:
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w25, [x29,200]
	cmn	w25, #1
	beq	.L2582
	ldr	w0, [x29,196]
	cmp	w25, w0
	beq	.L2582
	lsr	x0, x25, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x1, x21, :lo12:.LANCHOR2
	ldrh	w2, [x1,552]
	cmp	w2, w0
	beq	.L2586
	ldrh	w2, [x1,600]
	cmp	w2, w0
	beq	.L2586
	ldrh	w1, [x1,648]
	cmp	w1, w0
	bne	.L2582
.L2586:
	add	x28, x21, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, 0
	ldr	x0, [x28,3584]
	str	w25, [x0,4]
	ldr	x3, [x0,16]
	ldr	x0, [x28,3584]
	str	x3, [x29,168]
	bl	FlashReadPages
	ldr	x0, [x28,3584]
	ldr	x3, [x29,168]
	ldr	w0, [x0]
	ldr	w1, [x3,4]
	cmn	w0, #1
	beq	.L2582
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2582
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2582:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2565
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x21, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,512]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2587
.L2680:
	bl	decrement_vpc_count
	b	.L2565
.L2587:
	ldr	x0, [x29,120]
	bl	printk
	b	.L2565
.L2569:
	ldr	x2, [x29,136]
	ldrh	w0, [x19]
	add	x25, x2, :lo12:.LANCHOR5
	mov	w2, w22
	strh	w0, [x25,1320]
	ldr	x0, [x29,128]
	bl	printk
	ldr	w0, [x25,1324]
	cmp	w0, 31
	bhi	.L2588
	add	x1, x25, 1328
	ldr	w2, [x29,204]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x25,1324]
.L2588:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,3884]
	cmn	w1, #1
	beq	.L2681
	cmp	w1, w22
	bls	.L2565
.L2681:
	str	w22, [x0,3884]
.L2565:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2564
.L2689:
	ldrb	w0, [x19,8]
	add	w24, w24, 1
	cmp	w0, 1
	uxth	w24, w24
	bne	.L2591
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,196]
	cbz	w1, .L2591
	cmp	w20, w24
	bne	.L2591
	ldrh	w0, [x0,2538]
	cmp	w0, w24
	beq	.L2567
.L2591:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2536]
	cmp	w24, w1
	bne	.L2559
	ldrh	w2, [x0,2468]
	mov	w3, 65535
	strh	w24, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2593:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2676
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2593
	strb	w1, [x19,6]
	b	.L2676
.L2567:
	strb	w26, [x19,6]
	strh	w20, [x19,2]
.L2683:
	mov	x0, x19
	mov	w1, w20
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2676:
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
	adrp	x0, .LC107
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 96
	add	x0, x0, :lo12:.LC107
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x22, .LANCHOR0
	bl	printk
	add	x1, x22, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	mov	w19, 0
	ldrh	w2, [x1,2478]
	mov	w1, 0
	ldr	x0, [x0,3704]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2691:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2608]
	cmp	w19, w0
	bcs	.L2707
	mov	w0, w19
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2692
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x2, [x1,3704]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2692:
	add	w19, w19, 1
	b	.L2691
.L2707:
	adrp	x23, .LC147
	mov	w19, 0
	add	x20, x20, :lo12:.LANCHOR2
	mov	w24, 65535
	add	x23, x23, :lo12:.LC147
.L2694:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2476]
	cmp	w0, w19
	bls	.L2708
	ubfiz	x21, x19, 1, 16
	ldr	x0, [x20,512]
	ldrh	w2, [x0,x21]
	ldr	x0, [x20,3704]
	cmp	w2, w24
	ldrh	w3, [x0,x21]
	beq	.L2695
	cmp	w2, w3
	beq	.L2695
	ldrh	w0, [x20,552]
	cmp	w0, w19
	beq	.L2695
	ldrh	w0, [x20,648]
	cmp	w0, w19
	beq	.L2695
	ldrh	w0, [x20,600]
	cmp	w0, w19
	beq	.L2695
	mov	w1, w19
	mov	x0, x23
	bl	printk
	ldr	x0, [x20,512]
	ldrh	w1, [x0,x21]
	cbnz	w1, .L2696
	ldr	x1, [x20,3704]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	b	.L2695
.L2696:
	ldr	x1, [x20,3704]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	mov	w0, w19
	bl	update_vpc_list
.L2695:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2694
.L2708:
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
	ldrh	w27, [x1,3360]
	mov	w1, 65535
	cmp	w27, w1
	beq	.L2742
	cbnz	w27, .L2710
	b	.L2711
.L2742:
	mov	w27, 0
.L2710:
	adrp	x0, .LANCHOR0+2536
	ldr	w1, [x29,148]
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2536]
	cmp	w1, w0
	bne	.L2712
.L2711:
	bl	FtlGcPageVarInit
.L2712:
	mov	w0, -1
	str	w0, [x29,152]
	adrp	x0, .LC149
	mov	w23, 0
	add	x0, x0, :lo12:.LC149
	adrp	x28, .LANCHOR0
	str	x0, [x29,128]
.L2713:
	ldrh	w1, [x20]
	mov	w0, 65535
	strb	wzr, [x20,8]
	cmp	w1, w0
	beq	.L2743
.L2715:
	str	w0, [x29,156]
.L2739:
	adrp	x0, .LANCHOR0
	mov	x2, x20
	add	x0, x0, :lo12:.LANCHOR0
	mov	w21, 0
	mov	w10, 56
	adrp	x11, .LANCHOR2
	mov	w7, 4
	ldrh	w5, [x0,2468]
	ldrh	w8, [x0,2546]
	ldrh	w9, [x0,2548]
	add	x5, x20, x5, lsl 1
.L2716:
	cmp	x2, x5
	beq	.L2762
	ldrh	w1, [x2,16]
	ldr	w0, [x29,156]
	cmp	w1, w0
	beq	.L2717
	add	x4, x11, :lo12:.LANCHOR2
	orr	w1, w27, w1, lsl 10
	umull	x0, w21, w10
	ldr	x6, [x4,3584]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x4,3584]
	ldr	x6, [x4,1392]
	add	x0, x1, x0
	ldr	x4, [x4,1400]
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
.L2717:
	add	x2, x2, 2
	b	.L2716
.L2762:
	adrp	x19, .LANCHOR2
	mov	w1, w21
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 0
	mov	x24, 0
	ldr	x0, [x0,3584]
	bl	FlashReadPages
.L2719:
	cmp	w21, w24, uxth
	bls	.L2763
	add	x25, x19, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x22, x24, x0
	ldr	x1, [x25,3584]
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
	cbnz	w8, .L2720
	ldrh	w0, [x4]
	ldr	w1, [x29,156]
	cmp	w0, w1
	bne	.L2721
.L2724:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L2758
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	str	w0, [x19,3880]
	b	.L2714
.L2721:
	add	x7, x28, :lo12:.LANCHOR0
	ldr	w0, [x4,8]
	ldr	w1, [x7,2608]
	cmp	w0, w1
	bls	.L2759
	b	.L2724
.L2758:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w2, [x20]
	ldr	x1, [x19,512]
	strh	w0, [x1,x2,lsl 1]
	b	.L2761
.L2759:
	ldrb	w1, [x7,72]
	cbz	w1, .L2728
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
	bne	.L2728
	cmn	w1, #1
	beq	.L2728
	str	w0, [x29,172]
	mov	w1, 1
	ldr	x0, [x25,3648]
	ldr	x8, [x29,112]
	str	x0, [x29,176]
	ldr	x0, [x25,3664]
	mov	w2, w8
	str	x0, [x29,184]
	add	x0, x29, 168
	bl	FlashReadPages
	ldr	x7, [x29,104]
	ldr	x0, [x25,3584]
	ldr	x5, [x29,176]
	ldrh	w2, [x7,2542]
	add	x22, x0, x22
	ldr	x4, [x29,120]
	mov	x0, 0
	lsl	w2, w2, 7
.L2730:
	cmp	w2, w0
	bls	.L2728
	lsl	x1, x0, 2
	ldr	x6, [x22,8]
	add	x0, x0, 1
	ldr	w6, [x6,x1]
	ldr	w1, [x5,x1]
	cmp	w6, w1
	beq	.L2730
	ldrh	w1, [x20]
	adrp	x0, .LC148
	ldr	w2, [x29,172]
	add	x0, x0, :lo12:.LC148
	add	x19, x19, :lo12:.LANCHOR2
	bl	printk
	ldrh	w1, [x20]
	ldr	x0, [x19,512]
	strh	wzr, [x0,x1,lsl 1]
.L2761:
	ldrh	w0, [x20]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x20]
	strh	w0, [x19,792]
	b	.L2760
.L2728:
	ldr	w0, [x4,12]
	mov	w1, w26
	ldr	w2, [x4,8]
	add	x24, x24, 1
	bl	FtlGcUpdatePage
	b	.L2719
.L2720:
	ldrh	w1, [x20]
	mov	w2, w26
	ldr	x0, [x29,128]
	bl	printk
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	w1, [x0,2460]
	cbnz	w1, .L2733
	ldrb	w0, [x0,196]
	cbz	w0, .L2734
.L2733:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x20]
	ldr	x0, [x0,432]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 159
	bls	.L2735
.L2734:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,3584]
	ldr	w0, [x0,x22]
	cmn	w0, #1
	bne	.L2736
.L2735:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,3584]
	add	x22, x0, x22
	ldr	w0, [x22,4]
	str	w0, [x29,152]
.L2736:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x20]
	ldr	x0, [x19,512]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x20]
.L2760:
	bl	FtlGcPageVarInit
	mov	w27, 0
	b	.L2713
.L2763:
	ldr	w0, [x29,148]
	add	w23, w23, 1
	add	w3, w27, 1
	cmp	w23, w0
	uxth	w27, w3
	bcs	.L2738
.L2740:
	add	x0, x28, :lo12:.LANCHOR0
	ldrh	w0, [x0,2536]
	cmp	w0, w27
	bhi	.L2739
	b	.L2743
.L2738:
	ldr	x0, [x29,136]
	ldr	w2, [x29,156]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3360]
	cmp	w0, w2
	beq	.L2740
	add	w0, w0, w23
	strh	w0, [x1,3360]
	add	x0, x28, :lo12:.LANCHOR0
	ldrh	w0, [x0,2536]
	cmp	w0, w27
	bls	.L2740
	b	.L2741
.L2743:
	mov	w5, 0
.L2714:
	ldr	x0, [x29,136]
	mov	w1, -1
	strh	w27, [x20,2]
	mov	w2, w5
	add	x0, x0, :lo12:.LANCHOR1
	strb	w5, [x20,6]
	strh	w1, [x0,3360]
	mov	x0, x20
	mov	w1, w27
	bl	ftl_sb_update_avl_pages
.L2741:
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
	add	x3, x1, 848
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR0
	ldr	w0, [x3,80]
	cbz	w0, .L2765
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x3,84]
	ldr	w0, [x0,2608]
	cmp	w2, w0
	bcs	.L2766
	mov	w21, 2048
	mov	x19, x1
.L2771:
	add	x22, x19, 848
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x22,84]
	ldr	w1, [x1,2608]
	cmp	w0, w1
	bcs	.L2770
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x22,84]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x22,84]
	cmn	w1, #1
	beq	.L2769
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
	bne	.L2770
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2770:
	mov	w0, -1
	b	.L2773
.L2769:
	subs	w21, w21, #1
	bne	.L2771
	b	.L2770
.L2766:
	ldr	w0, [x1,712]
	str	wzr, [x3,80]
	str	wzr, [x3,84]
	str	w0, [x3,76]
	b	.L2780
.L2765:
	ldr	w4, [x1,764]
	mov	w6, 10000
	mov	w8, 63
	ldr	w7, [x1,712]
	cmp	w4, w6
	mov	w6, 31
	csel	w8, w6, w8, hi
	ldr	w6, [x3,76]
	add	x20, x20, :lo12:.LANCHOR0
	add	w9, w7, 1048576
	cmp	w6, w9
	ldr	w5, [x20,2608]
	bhi	.L2775
	mov	w9, 1000
	lsr	w4, w4, 10
	add	w4, w4, 1
	mul	w5, w5, w9
	udiv	w4, w5, w4
	add	w4, w4, w6
	cmp	w4, w7
	bcc	.L2775
	ldrh	w1, [x1,476]
	tst	w8, w1
	bne	.L2773
	ldr	w3, [x3,100]
	cmp	w3, w1
	beq	.L2773
.L2775:
	add	x0, x2, :lo12:.LANCHOR2
	add	x1, x0, 848
	ldrh	w0, [x0,476]
	str	w0, [x1,100]
	mov	w0, 1
	str	wzr, [x1,84]
	str	w0, [x1,80]
	str	w7, [x1,76]
.L2780:
	mov	w0, 0
.L2773:
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
	ldr	w3, [x2,416]
	ldrh	w1, [x1,2536]
	cbnz	w3, .L2817
	ldrh	w20, [x2,648]
	mov	w4, 65535
	cmp	w20, w4
	bne	.L2784
.L2793:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x22, x20, 648
	ldrh	w1, [x20,648]
	str	wzr, [x20,3880]
	cmp	w1, w0
	beq	.L2817
	bl	FtlCacheWriteBack
	mov	w26, 12
	add	x3, x21, :lo12:.LANCHOR0
	ldrb	w0, [x22,7]
	ldr	x1, [x20,512]
	mov	w22, 0
	ldrh	w2, [x20,648]
	ldrh	w3, [x3,2536]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldr	w0, [x20,732]
	ldrh	w1, [x20,1432]
	add	w0, w1, w0
	str	w0, [x20,732]
	b	.L2794
.L2784:
	cbz	w0, .L2787
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3360]
	cmp	w5, w4
	beq	.L2788
.L2789:
	mov	w1, 2
	b	.L2787
.L2788:
	strh	w3, [x0,3360]
	ldrh	w0, [x2,544]
	cmp	w0, 17
	bhi	.L2789
.L2787:
	add	x22, x19, :lo12:.LANCHOR2
	add	x0, x22, 648
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L2790
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x22,432]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L2791
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2791:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,3880]
	cbnz	w1, .L2792
	ldr	w1, [x0,944]
	add	w1, w1, 1
	str	w1, [x0,944]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2792:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	str	wzr, [x19,3880]
	b	.L2783
.L2790:
	adrp	x0, .LANCHOR1+3360
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3360]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2783
	b	.L2793
.L2797:
	ldr	w1, [x23,4]
	cmp	w0, w1
	bne	.L2815
.L2796:
	add	w22, w22, 1
	uxth	w22, w22
.L2794:
	ldrh	w0, [x20,1432]
	cmp	w0, w22
	bls	.L2818
	umull	x24, w22, w26
	ldr	x25, [x20,1440]
	add	x1, x21, :lo12:.LANCHOR0
	add	x23, x25, x24
	ldr	w1, [x1,2608]
	ldr	w0, [x23,8]
	cmp	w0, w1
	bcs	.L2815
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	ldr	w1, [x25,x24]
	cmp	w0, w1
	bne	.L2797
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
	b	.L2816
.L2815:
	ldrh	w0, [x20,648]
.L2816:
	bl	decrement_vpc_count
	b	.L2796
.L2818:
	mov	w0, -1
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L2799
	adrp	x0, .LC150
	ldrh	w1, [x20,648]
	add	x0, x0, :lo12:.LC150
	bl	printk
.L2799:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,648]
	ldr	x1, [x1,512]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1,x2]
	cbz	w1, .L2800
	bl	INSERT_DATA_LIST
	b	.L2801
.L2800:
	bl	INSERT_FREE_LIST
.L2801:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w22, -1
	strh	wzr, [x20,1432]
	strh	w22, [x20,648]
	strh	wzr, [x20,1420]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w22, [x20,792]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,2460]
	cbz	w0, .L2802
	ldr	w0, [x20,768]
	cmp	w0, 39
	bhi	.L2802
	ldrh	w0, [x20,772]
	ldrh	w1, [x20,544]
	cmp	w1, w0
	bcs	.L2817
	lsl	w0, w0, 1
	strh	w0, [x20,1360]
.L2817:
	mov	w0, 0
	b	.L2783
.L2802:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,772]
	ldrh	w0, [x19,544]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L2817
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,196]
	cbz	w0, .L2803
	sub	w1, w1, #2
	strh	w1, [x19,1360]
	b	.L2817
.L2803:
	mov	w1, 20
	strh	w1, [x19,1360]
.L2783:
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
	add	x21, x19, 648
	mov	x0, x21
	ldrh	w1, [x20,2536]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,650]
	ldrh	w0, [x20,2536]
	cmp	w1, w0
	bcc	.L2819
	add	x0, x19, 3792
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19,3880]
.L2819:
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
	adrp	x0, .LANCHOR5+1324
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR5+1324]
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x19, 552
	add	x19, x19, 600
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
	strh	w1, [x0,1320]
	strh	wzr, [x0,1322]
	ldrh	w0, [x24,2472]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x23,776]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2823
.L2825:
	mov	w22, -1
	b	.L2824
.L2823:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L2825
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x24,2574]
	mov	x1, 0
	ldr	x0, [x23,696]
.L2826:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2830
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2826
.L2830:
	add	x0, x19, :lo12:.LANCHOR2
	cmp	w3, w2
	ldrh	w1, [x0,476]
	add	w1, w1, 1
	strh	w1, [x0,476]
	blt	.L2827
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w0, [x21,1322]
	cbz	w0, .L2831
.L2827:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,552]
	ldr	x4, [x1,512]
	lsl	x2, x2, 1
	ldrh	w5, [x1,556]
	ldrh	w3, [x4,x2]
	sub	w3, w3, w5
	strh	w3, [x4,x2]
	add	x4, x20, :lo12:.LANCHOR0
	strb	wzr, [x1,558]
	ldr	x5, [x1,512]
	strh	wzr, [x1,556]
	ldrh	w2, [x4,2536]
	strh	w2, [x1,554]
	ldrh	w2, [x1,600]
	ldrh	w6, [x1,604]
	lsl	x2, x2, 1
	ldrh	w3, [x5,x2]
	sub	w3, w3, w6
	strh	w3, [x5,x2]
	strb	wzr, [x1,606]
	ldrh	w0, [x1,478]
	ldrh	w2, [x4,2536]
	add	w0, w0, 1
	strh	w2, [x1,602]
	strh	wzr, [x1,604]
	strh	w0, [x1,478]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2831:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x24, x21, 552
	ldrh	w0, [x21,552]
	cmp	w0, w1
	beq	.L2832
	ldrh	w1, [x21,556]
	cbnz	w1, .L2832
	ldrh	w1, [x21,604]
	add	x23, x21, 600
	cbnz	w1, .L2832
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x21,600]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x24
	bl	allocate_new_data_superblock
	mov	x0, x23
	bl	allocate_new_data_superblock
.L2832:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,72]
	cbnz	w0, .L2833
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,476]
	and	w0, w0, 31
	cbnz	w0, .L2824
.L2833:
	bl	FtlVpcCheckAndModify
.L2824:
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
	ldr	w22, [x20,416]
	cbnz	w22, .L2841
	adrp	x21, .LANCHOR0
	ldr	x0, [x20,3760]
	add	x23, x21, :lo12:.LANCHOR0
	mov	w1, w22
	ldrh	w2, [x23,2572]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x23,2572]
	mov	w1, w22
	ldr	x0, [x20,3752]
	lsl	w2, w2, 2
	bl	ftl_memset
	str	w22, [x20,744]
	ldrh	w0, [x23,2472]
	str	w22, [x20,748]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2842
	bl	FtlMakeBbt
.L2842:
	mov	w0, 0
.L2843:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w2, [x1,2542]
	cmp	w0, w2, lsl 7
	bge	.L2870
	add	x3, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 2, 16
	mvn	w1, w0
	ldr	x4, [x3,3640]
	orr	w1, w0, w1, lsl 16
	add	w0, w0, 1
	str	w1, [x4,x2]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	uxth	w0, w0
	ldr	x3, [x3,3648]
	str	w1, [x3,x2]
	b	.L2843
.L2870:
	ldrh	w22, [x1,2476]
	mov	w20, 0
.L2845:
	add	x23, x21, :lo12:.LANCHOR0
	ldrh	w0, [x23,2478]
	cmp	w0, w22
	bls	.L2871
	mov	w0, w22
	mov	w1, 1
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w22, w22
	uxth	w20, w0
	b	.L2845
.L2871:
	ldrh	w0, [x23,2468]
	sub	w1, w20, #3
	cmp	w1, w0, lsl 1
	bge	.L2847
.L2851:
	mov	w20, 0
	mov	w22, w20
	b	.L2848
.L2847:
	udiv	w20, w20, w0
	ldr	w0, [x23,2568]
	add	w0, w20, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x23,2472]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w20, [x23,2476]
.L2849:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2478]
	cmp	w0, w20
	bls	.L2851
	mov	w0, w20
	mov	w1, 1
	add	w20, w20, 1
	bl	FtlLowFormatEraseBlock
	uxth	w20, w20
	b	.L2849
.L2848:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,2476]
	cmp	w1, w22
	bls	.L2872
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w22, w22
	uxth	w20, w0
	b	.L2848
.L2872:
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,2478]
	ldrh	w2, [x0,2468]
	str	w1, [x4,3572]
	ldr	w1, [x0,2480]
	udiv	w3, w1, w2
	str	w3, [x0,2608]
	ubfx	x6, x3, 5, 16
	add	w5, w6, 36
	strh	w5, [x4,772]
	mov	w5, 24
	mul	w5, w2, w5
	cmp	w20, w5
	ble	.L2853
	sub	w1, w1, w20
	udiv	w1, w1, w2
	str	w1, [x0,2608]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x4,772]
.L2853:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,2460]
	cmp	w0, 1
	bne	.L2854
	add	x1, x19, :lo12:.LANCHOR2
	udiv	w0, w20, w2
	ldrh	w4, [x1,772]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,772]
.L2854:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L2855
	add	x1, x19, :lo12:.LANCHOR2
	udiv	w0, w20, w2
	ldrh	w4, [x1,772]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,772]
.L2855:
	add	x5, x21, :lo12:.LANCHOR0
	ldrh	w1, [x5,2530]
	cbz	w1, .L2857
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w4, [x0,772]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,772]
	mul	w4, w1, w2
	cmp	w4, w20
	ble	.L2857
	add	w1, w1, 32
	str	w3, [x5,2608]
	add	w1, w6, w1
	strh	w1, [x0,772]
.L2857:
	add	x19, x19, :lo12:.LANCHOR2
	add	x22, x21, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR4
	add	x20, x19, 552
	mov	w23, -1
	ldr	w1, [x22,2608]
	ldrh	w0, [x19,772]
	sub	w0, w1, w0
	add	x1, x24, :lo12:.LANCHOR4
	mul	w0, w0, w2
	str	w0, [x1,1752]
	ldrh	w1, [x22,2536]
	mul	w0, w0, w1
	ldrh	w1, [x22,2542]
	str	w0, [x22,2608]
	mul	w0, w0, w1
	str	w0, [x22,2576]
	bl	FtlBbmTblFlush
	ldrh	w2, [x22,2478]
	mov	w1, 0
	ldr	x0, [x19,512]
	lsl	w2, w2, 1
	bl	ftl_memset
	str	wzr, [x19,708]
	mov	w0, 1
	ldrh	w2, [x22,2476]
	strb	w0, [x20,8]
	mov	w1, 255
	ldr	x0, [x21,#:lo12:.LANCHOR0]
	lsr	w2, w2, 3
	mov	x21, x24
	strh	w23, [x19,792]
	strh	wzr, [x19,794]
	strb	wzr, [x19,798]
	strb	wzr, [x19,800]
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	strh	wzr, [x19,552]
	bl	ftl_memset
.L2859:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2860
	ldrh	w1, [x20]
	ldr	x0, [x19,512]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2859
.L2860:
	ldr	w0, [x19,744]
	mov	w22, -1
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,744]
	ldr	x0, [x19,512]
	ldrh	w2, [x20,4]
	strh	w2, [x0,x1,lsl 1]
	add	x0, x19, 600
	strh	wzr, [x19,602]
	ldrh	w1, [x20]
	mov	x20, x0
	strb	wzr, [x19,606]
	add	w1, w1, 1
	strh	w1, [x19,600]
	mov	w1, 1
	strb	w1, [x19,608]
.L2861:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2862
	ldrh	w1, [x20]
	ldr	x0, [x19,512]
	strh	w22, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2861
.L2862:
	ldr	w0, [x19,744]
	add	x21, x21, :lo12:.LANCHOR4
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,744]
	ldr	x0, [x19,512]
	ldrh	w2, [x20,4]
	mov	w20, -1
	strh	w2, [x0,x1,lsl 1]
	strh	w20, [x19,648]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19,776]
	ldr	w0, [x21,1752]
	strh	w0, [x19,782]
	ldr	w0, [x19,744]
	str	w0, [x19,784]
	add	w0, w0, 1
	strh	wzr, [x19,778]
	strh	w20, [x19,780]
	str	w0, [x19,744]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L2841
	adrp	x0, .LANCHOR1+504
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+504]
.L2841:
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
	ldrb	w0, [x0,196]
	cbz	w0, .L2874
.L2876:
	mov	w0, 0
	b	.L2875
.L2874:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,1584]
	bl	FlashInit
	cbnz	w0, .L2876
	bl	FlashLoadFactorBbt
	cbz	w0, .L2877
	bl	FlashMakeFactorBbt
.L2877:
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20,1640]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L2878
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L2880:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L2880
	cmp	w0, 6
	bhi	.L2881
	add	x0, x19, :lo12:.LANCHOR0
	b	.L2903
.L2881:
	mov	w1, 0
	mov	w4, 1
.L2884:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L2884
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L2903
	mov	w1, 36
.L2903:
	strb	w1, [x0,73]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,73]
	strh	w1, [x0,194]
.L2878:
	adrp	x0, .LC75
	adrp	x1, .LC76
	add	x1, x1, :lo12:.LC76
	add	x0, x0, :lo12:.LC75
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	add	x0, x19, 168
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,2472]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L2886:
	bl	FtlLoadBbt
	cbz	w0, .L2887
.L2905:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L2906
	add	w19, w19, 1
	b	.L2886
.L2906:
	mov	w0, -1
	b	.L2875
.L2887:
	bl	FtlSysBlkInit
	cbnz	w0, .L2905
	adrp	x1, .LANCHOR1+504
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+504]
.L2875:
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
	ldr	w1, [x0,416]
	cbz	w1, .L2908
.L2911:
	mov	w0, 0
	b	.L2909
.L2908:
	adrp	x1, .LANCHOR0+196
	ldrb	w1, [x1,#:lo12:.LANCHOR0+196]
	cbz	w1, .L2910
	ldr	w1, [x0,1376]
	tbz	x1, 0, .L2910
	ldrh	w0, [x0,652]
	cbnz	w0, .L2911
.L2910:
	add	x1, x21, :lo12:.LANCHOR2
	mov	w2, 0
	mov	w3, w2
	mov	w20, 0
	mov	w22, 56
	ldr	x0, [x1,3592]
	ldr	w1, [x1,1376]
	bl	FlashProgPages
.L2912:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w1, [x19,1376]
	cmp	w20, w1
	bcs	.L2922
	umull	x0, w20, w22
	ldr	x3, [x19,3592]
	add	x1, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	ldr	x2, [x1,16]
	bne	.L2913
	ldrh	w2, [x19,648]
	ldr	x1, [x19,512]
	strh	wzr, [x1,x2,lsl 1]
	strh	w3, [x19,648]
	ldr	w1, [x19,944]
	add	w1, w1, 1
	str	w1, [x19,944]
	ldr	x1, [x19,3592]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2921
.L2913:
	ldr	w0, [x2,12]
	add	w20, w20, 1
	ldr	w1, [x1,4]
	ldr	w2, [x2,8]
	uxth	w20, w20
	bl	FtlGcUpdatePage
	b	.L2912
.L2922:
	ldr	x0, [x19,3592]
	bl	FtlGcBufFree
	str	wzr, [x19,1376]
	ldrh	w0, [x19,652]
	cbnz	w0, .L2911
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L2921:
	mov	w0, 1
.L2909:
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
	add	x1, x0, 648
	ldrh	w2, [x0,648]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2924
	ldrh	w0, [x1,4]
	cbnz	w0, .L2925
.L2924:
	bl	FtlCacheWriteBack
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	strb	wzr, [x20,656]
	add	x0, x20, 648
	bl	allocate_data_superblock
	strh	wzr, [x20,1420]
	strh	wzr, [x20,1432]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2925:
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 648
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
	ldr	w4, [x3,416]
	cbnz	w4, .L3079
	adrp	x4, .LANCHOR1
	add	x4, x4, :lo12:.LANCHOR1
	ldr	w5, [x4,504]
	cmn	w5, #1
	beq	.L3079
	ldr	w5, [x3,3560]
	cbnz	w5, .L3079
	ldrh	w5, [x3,528]
	cmp	w5, 47
	bls	.L3079
	mov	w22, w1
	ldrh	w1, [x4,3360]
	str	w0, [x29,124]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2928
.L2931:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0,1450]
	cmp	w4, w2
	bne	.L2929
	b	.L2930
.L2928:
	ldrh	w1, [x3,648]
	cmp	w1, w0
	beq	.L2931
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2931
	b	.L3079
.L2929:
	ldrh	w1, [x0,1448]
	cmp	w1, w2
	bne	.L2930
	ldrh	w3, [x0,1452]
	cmp	w3, w1
	beq	.L2930
	ldrh	w2, [x0,1454]
	cmp	w2, w1
	beq	.L2930
	mov	w1, -1
	strh	w4, [x0,1448]
	strh	w3, [x0,1450]
	strh	w2, [x0,1452]
	strh	w1, [x0,1454]
.L2930:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x29,124]
	cmp	w2, 1
	ldr	w0, [x1,1364]
	add	w0, w0, 1
	add	w0, w0, w2, lsl 7
	str	w0, [x1,1364]
	bne	.L2942
	adrp	x2, .LANCHOR0
	add	x1, x2, :lo12:.LANCHOR0
	mov	x26, x2
	ldr	w3, [x1,2460]
	cbnz	w3, .L2933
	ldrb	w1, [x1,196]
	cbz	w1, .L2942
.L2933:
	add	x20, x19, :lo12:.LANCHOR2
	ldr	w1, [x20,768]
	cmp	w1, 39
	bhi	.L2942
	adrp	x21, .LANCHOR5
	add	x1, x21, :lo12:.LANCHOR5
	ldrh	w1, [x1,1456]
	add	w0, w0, w1
	str	w0, [x20,1364]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x20,792]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2942
	ldrh	w1, [x20,1448]
	cmp	w1, w0
	bne	.L2942
	ldr	w0, [x20,1364]
	cmp	w0, 1024
	bhi	.L2934
	ldrh	w0, [x20,544]
	cmp	w0, 63
	bhi	.L2942
.L2934:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR5
	ldrh	w2, [x0,1362]
	ldrh	w3, [x0,544]
	add	w2, w2, 64
	strh	wzr, [x1,1456]
	cmp	w3, w2
	bgt	.L2942
	str	wzr, [x0,1364]
	ldr	w0, [x0,768]
	cbnz	w0, .L2935
	mov	w0, 6
	b	.L3101
.L2935:
	cmp	w0, 5
	bhi	.L2936
	mov	w0, 18
.L3101:
	strh	w0, [x1,1456]
.L2936:
	mov	w0, 32
	mov	w28, 65535
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w28
	beq	.L2941
	add	x23, x19, :lo12:.LANCHOR2
	ldrh	w0, [x23,1368]
	cbz	w0, .L2938
	add	x2, x26, :lo12:.LANCHOR0
	ldr	x27, [x23,512]
	ubfiz	x25, x25, 1, 16
	ldrh	w1, [x2,2538]
	ldrh	w2, [x2,2468]
	ldrh	w3, [x27,x25]
	mul	w1, w1, w2
	add	w1, w1, 1
	cmp	w3, w1
	bgt	.L2941
	add	w1, w0, 1
	str	wzr, [x23,1372]
	uxth	w1, w1
	strh	w1, [x23,1368]
	str	x1, [x29,112]
	bl	List_get_gc_head_node
	uxth	w20, w0
	cmp	w20, w28
	ldr	x1, [x29,112]
	beq	.L2941
	ubfiz	x26, x20, 1, 16
	adrp	x0, .LC151
	ldrh	w4, [x27,x25]
	add	x0, x0, :lo12:.LC151
	mov	w2, w20
	ldrh	w3, [x27,x26]
	bl	printk
	ldrh	w0, [x23,1368]
	cmp	w0, 40
	bls	.L2939
	ldr	x0, [x23,512]
	ldrh	w0, [x0,x26]
	cmp	w0, 32
	bls	.L2939
	strh	wzr, [x23,1368]
.L2939:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w0, 6
	strh	w0, [x21,1456]
	b	.L2943
.L2938:
	mov	w0, 1
	strh	w0, [x23,1368]
.L2941:
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2943
	add	x21, x21, :lo12:.LANCHOR5
	strh	wzr, [x21,1456]
.L2942:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w20, [x21,792]
	cmp	w20, w0
	bne	.L3105
	ldrh	w0, [x21,648]
	cmp	w0, w20
	beq	.L3106
.L2943:
	mov	w0, 65535
	cmp	w20, w0
	cset	w2, eq
	cbz	w2, .L2956
	ldr	w0, [x29,124]
	cbnz	w0, .L2956
	add	x0, x19, :lo12:.LANCHOR2
	mov	w21, 1
	ldrh	w1, [x0,544]
	cmp	w1, 24
	bhi	.L2957
	adrp	x0, .LANCHOR0
	cmp	w1, 16
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w21, [x0,2536]
	bls	.L2958
	lsr	w21, w21, 5
	b	.L2957
.L2958:
	cmp	w1, 12
	bls	.L2959
	lsr	w21, w21, 4
	b	.L2957
.L2959:
	cmp	w1, 8
	bls	.L2957
	lsr	w21, w21, 2
.L2957:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,1360]
	cmp	w2, w1
	bcs	.L2961
	ldrh	w1, [x0,648]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L2962
	ldrh	w2, [x0,1448]
	cmp	w2, w1
	bne	.L2962
	adrp	x1, .LANCHOR5+1456
	ldrh	w2, [x1,#:lo12:.LANCHOR5+1456]
	cbnz	w2, .L2963
	adrp	x1, .LANCHOR0+2608
	ldr	w3, [x0,708]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2608]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2964
.L2963:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,772]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,1360]
	b	.L2965
.L2964:
	mov	w1, 18
	strh	w1, [x0,1360]
.L2965:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,1372]
	b	.L3079
.L2962:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,772]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,1360]
.L2961:
	cmp	w22, 2
	bhi	.L3033
	adrp	x0, .LANCHOR0+2460
	ldr	w0, [x0,#:lo12:.LANCHOR0+2460]
	cbz	w0, .L3033
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3033
.L2956:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w3, 65535
	ldrh	w1, [x0,648]
	cmp	w1, w3
	bne	.L2968
	cbz	w2, .L2968
	ldrh	w2, [x0,1448]
	cmp	w2, w1
	bne	.L2968
	ldrh	w2, [x0,792]
	cmp	w2, w1
	beq	.L2969
.L2974:
	mov	w20, 65535
	b	.L2968
.L2969:
	ldrh	w2, [x0,544]
	adrp	x20, .LANCHOR5
	ldrh	w1, [x0,1360]
	str	wzr, [x0,1372]
	cmp	w2, w1
	bls	.L2970
	add	x1, x20, :lo12:.LANCHOR5
	ldrh	w1, [x1,1456]
	cbnz	w1, .L2971
	adrp	x1, .LANCHOR0+2608
	ldr	w2, [x0,708]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2608]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L2972
.L2971:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,772]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,1360]
	b	.L2973
.L2972:
	mov	w1, 18
	strh	w1, [x0,1360]
.L2973:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x1, [x1,512]
	ldrh	w0, [x1,x0]
	cmp	w0, 4
	bls	.L2970
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w2, [x20,1456]
	b	.L3079
.L2970:
	add	x21, x20, :lo12:.LANCHOR5
	ldrh	w0, [x21,1456]
	cbnz	w0, .L2974
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w20, [x22,772]
	add	w1, w20, w20, lsl 1
	asr	w1, w1, 2
	strh	w1, [x22,1360]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x22,512]
	ldrh	w2, [x1,x0]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1,2538]
	ldrh	w1, [x1,2468]
	mul	w0, w0, w1
	mov	w1, 2
	sdiv	w0, w0, w1
	cmp	w2, w0
	ble	.L2975
	ldrh	w0, [x22,544]
	sub	w20, w20, #1
	cmp	w0, w20
	blt	.L2975
	bl	FtlReadRefresh
	b	.L3104
.L2975:
	cbnz	w2, .L2974
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,544]
	add	w2, w2, 1
	b	.L3079
.L2968:
	adrp	x0, .LANCHOR0+2460
	mov	w21, 2
	ldr	w0, [x0,#:lo12:.LANCHOR0+2460]
	cmp	w0, wzr
	csinc	w21, w21, wzr, ne
	b	.L2967
.L3033:
	mov	w20, 65535
.L2967:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,792]
	cmp	w1, w2
	bne	.L2977
	cmp	w20, w1
	beq	.L2978
	strh	w20, [x0,792]
	b	.L2979
.L2978:
	ldrh	w1, [x0,1448]
	cmp	w1, w20
	beq	.L2979
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x0,512]
	ldrh	w1, [x2,x1]
	cbnz	w1, .L2980
	mov	w1, -1
	strh	w1, [x0,1448]
.L2980:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,1448]
	strh	w1, [x0,792]
	mov	w1, -1
	strh	w1, [x0,1448]
.L2979:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x22,792]
	strb	wzr, [x22,800]
	cmp	w0, w1
	beq	.L2977
	bl	IsBlkInGcList
	cbz	w0, .L2982
	mov	w0, -1
	strh	w0, [x22,792]
.L2982:
	adrp	x0, .LANCHOR0+196
	ldrb	w0, [x0,#:lo12:.LANCHOR0+196]
	cbz	w0, .L2983
	add	x0, x19, :lo12:.LANCHOR2
	add	x22, x0, 792
	ldrh	w0, [x0,792]
	bl	ftl_get_blk_mode
	strb	w0, [x22,8]
.L2983:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x23, x22, 792
	ldrh	w1, [x22,792]
	cmp	w1, w0
	beq	.L2977
	mov	x0, x23
	bl	make_superblock
	strh	wzr, [x22,794]
	adrp	x0, .LANCHOR5
	ldrh	w2, [x22,792]
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x1, [x22,512]
	strb	wzr, [x22,798]
	strh	wzr, [x0,1458]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,1460]
.L2977:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,792]
	ldrh	w2, [x0,552]
	cmp	w2, w1
	beq	.L2984
	ldrh	w2, [x0,600]
	cmp	w2, w1
	beq	.L2984
	ldrh	w0, [x0,648]
	cmp	w0, w1
	bne	.L2985
.L2984:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, -1
	strh	w1, [x0,792]
.L2985:
	add	x24, x19, :lo12:.LANCHOR2
	mov	w25, 65535
.L3018:
	ldrh	w0, [x24,792]
	cmp	w0, w25
	bne	.L2986
	str	wzr, [x24,1372]
.L2987:
	ldrh	w22, [x24,1368]
	mov	w0, w22
	bl	List_get_gc_head_node
	uxth	w23, w0
	cmp	w23, w25
	strh	w23, [x24,792]
	bne	.L2988
	strh	wzr, [x24,1368]
	mov	w2, 8
	b	.L3079
.L2988:
	mov	w0, w23
	add	w22, w22, 1
	bl	IsBlkInGcList
	cbz	w0, .L2989
	strh	w22, [x24,1368]
	b	.L2987
.L2989:
	adrp	x0, .LANCHOR0
	ubfiz	x1, x23, 1, 16
	add	x5, x0, :lo12:.LANCHOR0
	ldr	x2, [x24,512]
	uxth	w22, w22
	mov	w3, 2
	strh	w22, [x24,1368]
	ldrh	w0, [x5,2536]
	ldrh	w5, [x5,2468]
	ldrh	w4, [x2,x1]
	mul	w0, w0, w5
	sdiv	w5, w0, w3
	cmp	w4, w5
	bgt	.L2991
	cmp	w4, 8
	bls	.L2992
	cmp	w22, 48
	bls	.L2992
	ldrh	w4, [x24,1420]
	cmp	w4, 35
	bhi	.L2992
.L2991:
	strh	wzr, [x24,1368]
.L2992:
	cmp	w20, w25
	ldrh	w1, [x2,x1]
	bne	.L2993
	cmp	w1, w0
	blt	.L2993
	ldrh	w0, [x24,1368]
	cmp	w0, 3
	bhi	.L2993
	mov	w0, -1
	strh	w0, [x24,792]
	adrp	x0, .LANCHOR5+1456
	strh	wzr, [x24,1368]
	ldrh	w2, [x0,#:lo12:.LANCHOR5+1456]
	b	.L3079
.L2993:
	cbnz	w1, .L2994
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x24,1368]
	add	w0, w0, 1
	strh	w0, [x24,1368]
	b	.L2987
.L2994:
	adrp	x0, .LANCHOR0
	strb	wzr, [x24,800]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L2995
	mov	w0, w23
	bl	ftl_get_blk_mode
	strb	w0, [x24,800]
.L2995:
	add	x22, x24, 792
	mov	x0, x22
	bl	make_superblock
	adrp	x1, .LANCHOR5
	ldrh	w2, [x24,792]
	add	x1, x1, :lo12:.LANCHOR5
	ldr	x0, [x24,512]
	strh	wzr, [x1,1458]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,1460]
	strh	wzr, [x24,794]
	strb	wzr, [x24,798]
.L2986:
	ldr	w0, [x29,124]
	cmp	w0, 1
	bne	.L2996
	bl	FtlReadRefresh
.L2996:
	mov	w0, 1
	str	w0, [x24,3560]
	adrp	x0, .LANCHOR0
	adrp	x22, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,196]
	ldrh	w26, [x0,2536]
	cbz	w1, .L2997
	ldrb	w1, [x24,800]
	cmp	w1, 1
	bne	.L2997
	ldrh	w26, [x0,2538]
.L2997:
	ldrh	w0, [x24,794]
	add	w1, w0, w21
	cmp	w1, w26
	ble	.L2998
	sub	w21, w26, w0
	uxth	w21, w21
.L2998:
	mov	w27, 0
.L2999:
	cmp	w21, w27, uxth
	bls	.L3006
	add	x1, x24, 792
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w4, [x1,2]
	ldrh	w6, [x0,2468]
	mov	x0, 0
	add	w4, w4, w27
	mov	w23, w0
.L3007:
	cmp	w6, w0, uxth
	bls	.L3107
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,16]
	cmp	w2, w25
	beq	.L3000
	mov	w3, 56
	ldr	x7, [x24,1408]
	orr	w2, w4, w2, lsl 10
	umull	x5, w23, w3
	add	w23, w23, 1
	add	x5, x7, x5
	uxth	w23, w23
	str	w2, [x5,4]
.L3000:
	add	x0, x0, 1
	b	.L3007
.L3107:
	ldr	x0, [x24,1408]
	mov	w1, w23
	ldrb	w2, [x24,800]
	mov	x28, 0
	bl	FlashReadPages
.L3002:
	cmp	w23, w28, uxth
	bls	.L3108
	mov	x0, 56
	mul	x4, x28, x0
	ldr	x0, [x24,1408]
	add	x1, x0, x4
	ldr	w0, [x0,x4]
	cmn	w0, #1
	ldr	x5, [x1,16]
	beq	.L3036
	ldrh	w0, [x5]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L3036
	ldr	w0, [x5,8]
	add	x1, x29, 136
	mov	w2, 0
	str	x4, [x29,104]
	str	x5, [x29,112]
	bl	log2phys
	ldr	x2, [x24,1408]
	ldr	x4, [x29,104]
	ldr	w0, [x29,136]
	add	x2, x2, x4
	ldr	x5, [x29,112]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L3036
	adrp	x0, .LANCHOR5
	mov	x3, 56
	add	x1, x0, :lo12:.LANCHOR5
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrh	w0, [x1,1458]
	add	w0, w0, 1
	strh	w0, [x1,1458]
	ldr	w0, [x24,1376]
	ldr	x1, [x24,3592]
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
	ldr	w0, [x24,1376]
	ldr	x1, [x24,3592]
	madd	x0, x0, x2, x1
	ldr	x1, [x24,1408]
	add	x1, x1, x4
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	add	x2, x24, 648
	str	x2, [x29,112]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,136]
	str	w0, [x5,12]
	ldrh	w0, [x24,648]
	strh	w0, [x5,2]
	ldr	w0, [x24,748]
	str	w0, [x5,4]
	ldr	w0, [x24,1376]
	add	w0, w0, 1
	str	w0, [x24,1376]
	ldr	x0, [x24,1408]
	add	x0, x0, x4
	bl	FtlGcBufAlloc
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbnz	w0, .L3004
	ldr	x2, [x29,112]
	ldr	w1, [x24,1376]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L3004
	ldrh	w0, [x2,4]
	cbnz	w0, .L3036
.L3004:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L3036
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,792]
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x19,3560]
	strh	wzr, [x19,794]
	ldrh	w2, [x0,1456]
	b	.L3079
.L3036:
	add	x28, x28, 1
	b	.L3002
.L3108:
	add	w27, w27, 1
	b	.L2999
.L3006:
	ldrh	w0, [x24,794]
	add	w21, w21, w0
	uxth	w21, w21
	strh	w21, [x24,794]
	cmp	w21, w26
	bcc	.L3008
	ldr	w0, [x24,1376]
	adrp	x21, .LANCHOR5
	cbz	w0, .L3009
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L3009
	str	wzr, [x24,3560]
	add	x21, x21, :lo12:.LANCHOR5
.L3104:
	ldrh	w2, [x21,1456]
	b	.L3079
.L3009:
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w21, [x21,1458]
	cbnz	w21, .L3010
	ldrh	w1, [x24,792]
	ldr	x0, [x24,512]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L3010
.L3011:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2608]
	cmp	w21, w0
	bcs	.L3016
	mov	w0, w21
	add	x1, x29, 140
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,140]
	cmn	w0, #1
	beq	.L3012
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x24,792]
	cmp	w1, w0, uxth
	bne	.L3012
.L3016:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2608]
	cmp	w21, w0
	bcc	.L3010
	ldrh	w1, [x24,792]
	ldr	x0, [x24,512]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x24,792]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3010
.L3012:
	add	w21, w21, 1
	b	.L3011
.L3010:
	mov	w0, -1
	strh	w0, [x24,792]
.L3008:
	ldrh	w0, [x24,544]
	cmp	w0, 2
	bhi	.L3017
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w21, [x22,2536]
	b	.L3018
.L3017:
	adrp	x1, .LANCHOR5+1456
	str	wzr, [x24,3560]
	ldrh	w2, [x1,#:lo12:.LANCHOR5+1456]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L3079
.L3106:
	ldrh	w25, [x21,1448]
	cmp	w25, w20
	bne	.L2943
	ldrh	w0, [x21,544]
	mov	w2, 1024
	ldr	w1, [x21,1364]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2943
	adrp	x0, .LANCHOR5+1456
	str	wzr, [x21,1364]
	mov	x23, x21
	strh	wzr, [x0,#:lo12:.LANCHOR5+1456]
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	cmp	w20, w25
	bne	.L2945
	ldrh	w1, [x21,544]
	ldrh	w0, [x21,1362]
	cmp	w1, w0
	bcs	.L2946
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w20
	beq	.L2955
	ldr	w2, [x21,3556]
	uxtw	x1, w0
	adrp	x21, .LANCHOR0
	cbnz	w2, .L2948
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w2, [x0,2488]
	cmp	w2, 3
	beq	.L2948
	ldr	w2, [x23,840]
	cbnz	w2, .L2948
	ldr	w2, [x0,2460]
	cbnz	w2, .L2948
	ldrb	w0, [x0,196]
	cbz	w0, .L2949
.L2948:
	add	x0, x19, :lo12:.LANCHOR2
	add	x3, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,512]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x3,2538]
	ldrh	w0, [x3,2468]
	ldrh	w3, [x3,2488]
	mul	w2, w1, w0
	cmp	w3, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w2, w0
	cmp	w4, w0
	bgt	.L2951
	mov	w0, 0
	add	x21, x21, :lo12:.LANCHOR0
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x21,2608]
	ldr	w2, [x1,708]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2952
	mov	w0, 128
	b	.L3102
.L2952:
	mov	w0, 160
.L3102:
	strh	w0, [x1,1362]
	b	.L2953
.L2951:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L3103
.L2949:
	ldr	x2, [x23,512]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2954
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0,1362]
	b	.L2953
.L2954:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L3103:
	strh	w1, [x0,1362]
	b	.L2955
.L2946:
	mov	w0, 80
	strh	w0, [x21,1362]
	b	.L2955
.L2953:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2955
.L2945:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x20, 1, 32
	adrp	x0, .LC152
	mov	w1, w20
	add	x0, x0, :lo12:.LC152
	ldr	x3, [x5,512]
	ldr	x6, [x5,432]
	ldrh	w2, [x5,544]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,1360]
	ldrh	w4, [x6,x4]
	bl	printk
.L2955:
	bl	FtlGcReFreshBadBlk
	b	.L2943
.L3105:
	mov	w20, w0
	b	.L2943
.L3079:
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
	adrp	x0, .LANCHOR5+1464
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	ldr	x20, [x0,#:lo12:.LANCHOR5+1464]
	add	x0, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w23, [x0,416]
	cbnz	w23, .L3111
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2592]
	cbz	w1, .L3111
	ldrb	w0, [x0,196]
	cbz	w0, .L3113
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w23, eq
.L3113:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x20,9]
	adrp	x26, .LC153
	mov	w2, w23
	mov	w24, 0
	mov	w27, 56
	ldr	x0, [x0,2600]
	add	x26, x26, :lo12:.LC153
	bl	FlashProgPages
.L3114:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2592]
	cmp	w24, w1
	bcs	.L3132
	umull	x21, w24, w27
	ldr	x1, [x0,2600]
	add	x0, x1, x21
	ldr	w25, [x1,x21]
	cmn	w25, #1
	beq	.L3135
	ldr	w1, [x0,4]
	cbz	w23, .L3149
	orr	w1, w1, -2147483648
.L3149:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,124]
	add	x1, x29, 124
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2600]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L3118
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x22, :lo12:.LANCHOR2
	ubfiz	x0, x1, 1, 16
	mov	w21, w1
	ldr	x2, [x2,512]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L3119
	mov	x0, x26
	bl	printk
.L3119:
	mov	w0, w21
	bl	decrement_vpc_count
.L3118:
	add	w24, w24, 1
	b	.L3114
.L3151:
	mov	w20, 16386
.L3131:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w0, [x0,1456]
	cbz	w0, .L3132
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w20, w20, #1
	bne	.L3131
.L3132:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19,2592]
	b	.L3111
.L3135:
	adrp	x26, .LC153
	mov	w24, 0
	mov	w27, 56
	str	w25, [x29,108]
	mov	w28, 1
	add	x26, x26, :lo12:.LC153
.L3115:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2592]
	cmp	w24, w1
	bcs	.L3151
	umull	x21, w24, w27
	ldr	x0, [x0,2600]
	ldr	w1, [x29,108]
	str	w1, [x0,x21]
.L3121:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x0, [x2,2600]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L3152
	ldr	w0, [x1,4]
	str	x2, [x29,96]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	ldr	x2, [x29,96]
	bne	.L3122
	add	x0, x22, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldrh	w4, [x20,4]
	ldr	x3, [x0,512]
	ldrh	w0, [x3,x1]
	sub	w0, w0, w4
	strh	w0, [x3,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x2,2536]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L3122:
	ldrh	w0, [x20,4]
	cbnz	w0, .L3123
	mov	x0, x20
	bl	allocate_new_data_superblock
.L3123:
	add	x5, x22, :lo12:.LANCHOR2
	add	x25, x19, :lo12:.LANCHOR0
	str	x5, [x29,96]
	ldr	w0, [x5,944]
	add	w0, w0, 1
	str	w0, [x5,944]
	ldr	x0, [x25,2600]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,124]
	ldr	x1, [x25,2600]
	mov	w2, w23
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x25,2600]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	ldr	x0, [x25,2600]
	ldr	x5, [x29,96]
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L3124
	str	w28, [x5,416]
.L3124:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w0, [x0,416]
	cbz	w0, .L3121
	b	.L3111
.L3152:
	ldr	w0, [x1,4]
	cbz	w23, .L3150
	orr	w0, w0, -2147483648
.L3150:
	str	w0, [x29,124]
	mov	w2, 1
	ldr	w0, [x1,24]
	add	x1, x29, 124
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2600]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L3128
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x22, :lo12:.LANCHOR2
	ubfiz	x0, x1, 1, 16
	mov	w21, w1
	ldr	x2, [x2,512]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L3129
	mov	x0, x26
	bl	printk
.L3129:
	mov	w0, w21
	bl	decrement_vpc_count
.L3128:
	add	w24, w24, 1
	b	.L3115
.L3111:
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
	adrp	x0, .LANCHOR2+416
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2+416]
	cbnz	w0, .L3154
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0,#:lo12:.LANCHOR1+504]
	cmn	w0, #1
	beq	.L3154
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3154:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
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
	bne	.L3159
	bl	FtlSysFlush
.L3159:
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
	adrp	x0, .LC154
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC154
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
	add	w2, w0, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	str	x23, [sp,48]
	add	x21, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w19, w1
	mov	w20, w0
	ldr	w1, [x21,2576]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L3165
	adrp	x0, .LANCHOR1+504
	ldr	w1, [x0,#:lo12:.LANCHOR1+504]
	cmn	w1, #1
	mov	w0, w1
	beq	.L3165
	cmp	w19, 31
	mov	w0, 0
	bls	.L3165
	adrp	x23, .LANCHOR2
	add	x1, x23, :lo12:.LANCHOR2
	ldr	w1, [x1,416]
	cbnz	w1, .L3165
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,2542]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L3166
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L3166:
	mov	w0, -1
	adrp	x20, .LANCHOR5
	str	w0, [x29,76]
.L3167:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2542]
	cmp	w19, w0
	bcc	.L3181
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L3168
	add	x1, x20, :lo12:.LANCHOR5
	mov	w2, 1
	ldr	w0, [x1,1472]
	add	w0, w0, 1
	str	w0, [x1,1472]
	add	x1, x23, :lo12:.LANCHOR2
	ldr	w0, [x1,716]
	add	w0, w0, 1
	str	w0, [x1,716]
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,72]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3168:
	add	x0, x22, :lo12:.LANCHOR0
	add	w21, w21, 1
	ldrh	w0, [x0,2542]
	sub	w19, w19, w0
	b	.L3167
.L3181:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,1472]
	cmp	w1, 32
	bls	.L3170
	str	wzr, [x0,1472]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3170:
	mov	w0, 0
.L3165:
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
	ldr	w5, [x0,#:lo12:.LANCHOR1+504]
	stp	x19, x20, [sp,32]
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	cmn	w5, #1
	mov	w0, w5
	beq	.L3184
	cmp	w4, 16
	mov	x24, x3
	mov	w27, w2
	mov	w20, w1
	bne	.L3185
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L3184
.L3185:
	adrp	x21, .LANCHOR0
	add	w0, w1, w2
	add	x1, x21, :lo12:.LANCHOR0
	str	w0, [x29,168]
	mov	w0, -1
	ldr	w3, [x29,168]
	ldr	w2, [x1,2576]
	cmp	w3, w2
	bhi	.L3184
	ldrh	w0, [x1,2542]
	sub	w26, w3, #1
	adrp	x19, .LANCHOR2
	udiv	w1, w20, w0
	str	w1, [x29,172]
	udiv	w26, w26, w0
	ldr	w0, [x29,172]
	sub	w25, w26, w0
	add	x0, x19, :lo12:.LANCHOR2
	add	w25, w25, 1
	ldr	w1, [x0,740]
	add	w1, w27, w1
	str	w1, [x0,740]
	ldr	w1, [x0,712]
	add	w1, w25, w1
	str	w1, [x0,712]
	ldr	w0, [x29,172]
	mov	w1, w26
	bl	FtlCacheMetchLpa
	cbz	w0, .L3186
	bl	FtlCacheWriteBack
.L3186:
	mov	w28, 0
	adrp	x0, .LC143
	ldr	w22, [x29,172]
	add	x0, x0, :lo12:.LC143
	str	w28, [x29,156]
	mov	w23, w28
	str	w28, [x29,152]
	str	x0, [x29,112]
.L3187:
	cbz	w25, .L3231
	mov	w0, w22
	add	x1, x29, 188
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,188]
	cmn	w3, #1
	bne	.L3228
	mov	w3, 0
.L3188:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2542]
	cmp	w3, w0
	bcs	.L3192
	madd	w0, w22, w0, w3
	cmp	w0, w20
	bcc	.L3190
	ldr	w1, [x29,168]
	cmp	w0, w1
	bcs	.L3190
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x24, x0
	str	x3, [x29,160]
	bl	ftl_memset
	ldr	x3, [x29,160]
.L3190:
	add	w3, w3, 1
	b	.L3188
.L3228:
	mov	w0, 56
	umull	x1, w23, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x0,3584]
	add	x2, x2, x1
	str	w3, [x2,4]
	ldr	w2, [x29,172]
	cmp	w22, w2
	ldr	x2, [x0,3584]
	add	x2, x2, x1
	bne	.L3193
	ldr	x0, [x0,3640]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2542]
	udiv	w3, w20, w0
	msub	w3, w3, w0, w20
	str	w3, [x29,128]
	sub	w3, w0, w3
	cmp	w3, w27
	csel	w3, w3, w27, ls
	str	w3, [x29,156]
	cmp	w3, w0
	bne	.L3194
	str	x24, [x2,8]
	b	.L3194
.L3193:
	cmp	w22, w26
	bne	.L3195
	ldr	x0, [x0,3648]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w4, [x29,168]
	ldrh	w3, [x0,2542]
	mul	w0, w22, w3
	sub	w28, w4, w0
	cmp	w28, w3
	bne	.L3194
	b	.L3229
.L3195:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2542]
	mul	w0, w22, w0
.L3229:
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x24, x0
	str	x0, [x2,8]
.L3194:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,3584]
	ldr	x2, [x2,3664]
	add	x1, x0, x1
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2548]
	str	w22, [x1,24]
	mul	w0, w23, w0
	add	w23, w23, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L3192:
	subs	w25, w25, #1
	add	w22, w22, 1
	beq	.L3196
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2468]
	cmp	w23, w0, lsl 3
	bne	.L3187
.L3196:
	cbz	w23, .L3187
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, 0
	ldr	x0, [x0,3584]
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
.L3198:
	ldr	w0, [x29,160]
	cmp	w23, w0
	bls	.L3232
	ldr	x0, [x29,160]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	ldr	w3, [x29,172]
	mul	x8, x0, x1
	ldr	x0, [x2,3584]
	add	x0, x0, x8
	ldr	w1, [x0,24]
	cmp	w1, w3
	bne	.L3199
	ldr	x1, [x0,8]
	ldr	x0, [x2,3640]
	cmp	x1, x0
	bne	.L3200
	ldr	x2, [x29,144]
	mov	x0, x24
	str	x8, [x29,104]
	add	x1, x1, x2
	ldr	w2, [x29,124]
	b	.L3230
.L3199:
	cmp	w1, w26
	bne	.L3200
	ldr	x1, [x0,8]
	ldr	x0, [x2,3648]
	cmp	x1, x0
	bne	.L3200
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,120]
	str	x8, [x29,104]
	ldrh	w0, [x0,2542]
	mul	w0, w26, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x24, x0
.L3230:
	bl	ftl_memcpy
	ldr	x8, [x29,104]
.L3200:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,3584]
	add	x2, x1, x8
	ldr	w3, [x1,x8]
	cmn	w3, #1
	bne	.L3201
	ldr	w1, [x0,920]
	str	w3, [x29,152]
	add	w1, w1, 1
	str	w1, [x0,920]
.L3201:
	ldr	x0, [x2,16]
	ldr	w1, [x2,24]
	ldr	w0, [x0,8]
	cmp	w1, w0
	beq	.L3202
	add	x0, x19, :lo12:.LANCHOR2
	str	x8, [x29,104]
	ldr	w1, [x0,920]
	add	w1, w1, 1
	str	w1, [x0,920]
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
.L3202:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,3584]
	add	x1, x0, x8
	ldr	w0, [x0,x8]
	cmp	w0, 256
	bne	.L3203
	ldr	w0, [x1,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3203:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L3198
.L3232:
	mov	w23, 0
	b	.L3187
.L3231:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,1456]
	cbz	w0, .L3206
	mov	w0, w25
	mov	w1, 1
	bl	ftl_do_gc
.L3206:
	ldr	w0, [x29,152]
.L3184:
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
	bne	.L3235
	bl	ftl_vendor_read
	mov	w19, w0
.L3235:
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
	adrp	x0, .LANCHOR5+1476
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR5+1476]
	adrp	x0, .LC75
	add	x0, x0, :lo12:.LC75
	str	wzr, [x19,416]
	bl	printk
	add	x0, x21, 168
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x21,2472]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3241
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
	b	.L3255
.L3241:
	bl	FtlSysBlkInit
	mov	w20, w0
	cbz	w0, .L3243
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
.L3255:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 120
	bl	printk
	b	.L3242
.L3243:
	mov	w1, 1
	str	w1, [x22,504]
	bl	ftl_do_gc
	ldrh	w0, [x19,544]
	cmp	w0, 15
	bhi	.L3244
	mov	w22, 65535
	add	w21, w0, 2
.L3247:
	ldrh	w0, [x19,792]
	cmp	w0, w22
	bne	.L3245
	ldrh	w0, [x19,1448]
	cmp	w0, w22
	bne	.L3245
	and	w0, w20, 63
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3245:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldrh	w0, [x19,544]
	cmp	w0, w21
	bhi	.L3242
	add	w20, w20, 1
	cmp	w20, 4096
	bne	.L3247
	b	.L3242
.L3244:
	ldrb	w0, [x21,196]
	cbz	w0, .L3242
	mov	w19, 128
.L3249:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3249
.L3242:
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
	add	x1, x20, 1488
	str	x0, [x20,1480]
	add	x0, x19, 1584
	str	xzr, [x19,1584]
	str	xzr, [x20,1488]
	bl	rknand_get_reg_addr
	ldr	x1, [x19,1584]
	mov	w0, -1
	cbz	x1, .L3257
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x20,1480]
	mov	w2, w1
	mov	w3, 2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	x0, [x19,1584]
	bl	FlashInit
	mov	w19, w0
	cbnz	w0, .L3258
	bl	FtlInit
.L3258:
	adrp	x0, .LC157
	mov	w1, w19
	add	x0, x0, :lo12:.LC157
	bl	printk
	mov	w0, w19
.L3257:
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
	ldrb	w0, [x0,196]
	cbz	w0, .L3261
	adrp	x21, .LANCHOR5
	adrp	x19, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR5
	add	x24, x19, :lo12:.LANCHOR2
	adrp	x25, .LC158
	add	x27, x24, 552
	add	x26, x24, 600
	ldrh	w23, [x0,1320]
	add	x0, x25, :lo12:.LC158
	ldr	x2, [x24,512]
	ubfiz	x22, x23, 1, 16
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldrh	w0, [x24,552]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x24,600]
	mov	w24, 4097
	bl	FtlGcRefreshOpenBlock
	mov	x0, x27
	bl	allocate_new_data_superblock
	mov	x0, x26
	bl	allocate_new_data_superblock
.L3263:
	subs	w24, w24, #1
	beq	.L3267
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,512]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3263
.L3267:
	add	x24, x19, :lo12:.LANCHOR2
	add	x0, x25, :lo12:.LC158
	mov	w1, w23
	ldr	x2, [x24,512]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x24,512]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3265
	add	x0, x29, 96
	add	x20, x20, :lo12:.LANCHOR0
	strh	w23, [x29,96]
	bl	make_superblock
	mov	x0, 0
	ldrh	w5, [x20,2468]
	mov	w6, 65535
	mov	w20, w0
	mov	w7, 56
.L3268:
	cmp	w5, w0, uxth
	bls	.L3275
	add	x1, x29, 96
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L3269
	add	x4, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x1, w20, w7
	add	w20, w20, 1
	ldr	x3, [x4,424]
	uxth	w20, w20
	add	x3, x3, x1
	str	w2, [x3,4]
	ldr	x2, [x4,424]
	add	x1, x2, x1
	str	xzr, [x1,8]
	str	xzr, [x1,16]
.L3269:
	add	x0, x0, 1
	b	.L3268
.L3275:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC159
	mov	w1, w23
	add	x0, x0, :lo12:.LC159
	ldr	x2, [x19,512]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x19,424]
	mov	w1, 0
	mov	w2, w20
	bl	FlashEraseBlocks
	ldr	x0, [x19,424]
	mov	w1, 1
	mov	w2, w20
	bl	FlashEraseBlocks
.L3265:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w0, -1
	strh	w0, [x21,1320]
.L3261:
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
	ldr	w3, [x2,416]
	cbnz	w3, .L3278
	adrp	x1, .LANCHOR1
	str	x1, [x29,152]
	add	x3, x1, :lo12:.LANCHOR1
	ldr	w3, [x3,504]
	cmn	w3, #1
	beq	.L3278
	cmp	w4, 16
	bne	.L3279
	add	w0, w23, 256
	mov	w1, w25
	mov	x2, x26
	bl	FtlVendorPartWrite
	b	.L3278
.L3279:
	adrp	x21, .LANCHOR0
	add	w3, w23, w25
	add	x4, x21, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	w1, [x4,2576]
	cmp	w3, w1
	bhi	.L3278
	adrp	x5, .LANCHOR5
	ldrh	w1, [x4,2542]
	add	x22, x5, :lo12:.LANCHOR5
	sub	w3, w3, #1
	mov	w0, 2048
	str	x5, [x29,144]
	udiv	w27, w23, w1
	cmp	w25, w1, lsl 1
	str	w0, [x22,1496]
	udiv	w0, w3, w1
	ldr	w3, [x4,2592]
	str	w0, [x29,208]
	sub	w24, w0, w27
	add	w20, w24, 1
	ldr	w0, [x2,720]
	add	w0, w20, w0
	str	w0, [x2,720]
	ldr	w0, [x2,736]
	add	w0, w25, w0
	str	w0, [x2,736]
	cset	w0, cs
	str	w0, [x29,212]
	cbz	w3, .L3281
	mov	w0, 56
	sub	w3, w3, #1
	umull	x3, w3, w0
	ldr	x0, [x4,2600]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w27, w0
	bne	.L3282
	ldr	w0, [x2,724]
	add	w0, w0, 1
	str	w0, [x2,724]
	ldr	w0, [x22,1500]
	add	w0, w0, 1
	str	w0, [x22,1500]
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
	cbnz	w24, .L3283
	ldr	w0, [x22,1500]
	cmp	w0, 2
	bgt	.L3283
.L3317:
	mov	w0, 0
	b	.L3278
.L3283:
	add	x26, x26, x20
	sub	w25, w25, w19
	add	w23, w23, w19
	add	w27, w27, 1
	mov	w20, w24
.L3282:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,1500]
.L3281:
	ldr	w1, [x29,208]
	mov	w0, w27
	bl	FtlCacheMetchLpa
	cbz	w0, .L3284
	bl	FtlCacheWriteBack
.L3284:
	ldr	x0, [x29,144]
	ldr	x1, [x29,176]
	add	x0, x0, :lo12:.LANCHOR5
	str	w27, [x29,192]
	add	x24, x1, :lo12:.LANCHOR2
	add	x22, x24, 552
	str	x22, [x29,184]
	str	x22, [x0,1464]
	adrp	x0, .LC160
	add	x0, x0, :lo12:.LC160
	str	x0, [x29,104]
.L3285:
	cbz	w20, .L3348
	ldrh	w0, [x22,4]
	cbnz	w0, .L3286
	ldr	x1, [x29,184]
	cmp	x22, x1
	bne	.L3287
	add	x0, x24, 600
	ldrh	w19, [x0,4]
	cbnz	w19, .L3288
	bl	allocate_new_data_superblock
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR1
	str	w19, [x0,3364]
.L3288:
	ldr	x0, [x29,184]
	add	x22, x24, 600
	bl	allocate_new_data_superblock
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR1
	ldr	w0, [x0,3364]
	cbnz	w0, .L3289
.L3290:
	ldr	x22, [x29,184]
	b	.L3289
.L3287:
	ldr	x1, [x29,152]
	add	x1, x1, :lo12:.LANCHOR1
	str	w0, [x1,3364]
	ldr	x0, [x29,184]
	ldrh	w0, [x0,4]
	cbnz	w0, .L3290
	mov	x0, x22
	bl	allocate_new_data_superblock
.L3289:
	ldrh	w0, [x22,4]
	cbnz	w0, .L3291
	mov	x0, x22
	bl	allocate_new_data_superblock
.L3291:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	x22, [x0,1464]
.L3286:
	add	x2, x21, :lo12:.LANCHOR0
	ldr	w0, [x24,3580]
	ldrh	w1, [x22,4]
	str	xzr, [x29,200]
	ldr	w2, [x2,2592]
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
.L3292:
	ldr	w0, [x29,200]
	ldr	w2, [x29,140]
	ldr	w1, [x29,192]
	cmp	w0, w2
	add	w28, w1, w0
	bcs	.L3349
	ldrh	w1, [x22,4]
	cbz	w1, .L3324
	ldr	w1, [x29,208]
	cmp	w28, w1
	cset	w6, eq
	cbz	w0, .L3294
	ldr	w1, [x29,116]
	tst	w6, w1
	beq	.L3294
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,136]
	ldrh	w1, [x1,2542]
	msub	w2, w28, w1, w2
	cmp	w2, w1
	bne	.L3324
.L3294:
	add	x1, x29, 228
	mov	w2, 0
	mov	w0, w28
	str	x6, [x29,96]
	add	x19, x21, :lo12:.LANCHOR0
	bl	log2phys
	mov	x0, x22
	bl	get_new_active_ppa
	ldr	w5, [x19,2592]
	mov	w4, 56
	ldr	x2, [x19,2600]
	str	x4, [x29,160]
	umull	x1, w5, w4
	add	x2, x2, x1
	str	w0, [x2,4]
	ldrh	w2, [x19,2548]
	ldr	x0, [x19,2600]
	add	x1, x0, x1
	mul	w0, w2, w5
	and	x0, x0, 4294967292
	str	x0, [x29,128]
	ldr	x0, [x24,3672]
	ldr	x3, [x29,128]
	str	x0, [x29,120]
	add	x0, x0, x3
	str	x0, [x29,216]
	str	x0, [x1,16]
	ldrh	w0, [x19,2546]
	str	w28, [x1,24]
	mul	w0, w0, w5
	ldr	x5, [x24,3632]
	and	x0, x0, 4294967292
	add	x0, x5, x0
	str	x0, [x1,8]
	ldr	x0, [x29,216]
	mov	w1, 0
	bl	ftl_memset
	cmp	w28, w27
	cset	w0, eq
	str	w0, [x29,196]
	cbnz	w0, .L3328
	ldr	x6, [x29,96]
	ldr	x4, [x29,160]
	cbz	w6, .L3295
	ldr	w0, [x29,136]
	ldrh	w19, [x19,2542]
	msub	w19, w28, w19, w0
	ldr	w0, [x29,196]
	str	w0, [x29,160]
	uxth	w19, w19
	b	.L3298
.L3328:
	ldrh	w19, [x19,2542]
	udiv	w0, w23, w19
	msub	w0, w0, w19, w23
	str	w0, [x29,160]
	sub	w19, w19, w0
	cmp	w19, w25
	csel	w19, w19, w25, ls
.L3298:
	add	x2, x21, :lo12:.LANCHOR0
	ldrh	w0, [x2,2542]
	cmp	w19, w0
	bne	.L3299
	ldr	w0, [x29,196]
	mov	x1, x26
	cbnz	w0, .L3300
	mul	w1, w19, w28
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x26, x1
.L3300:
	ldr	w2, [x29,212]
	mov	w0, 56
	add	x4, x21, :lo12:.LANCHOR0
	cbz	w2, .L3301
	ldr	w2, [x4,2592]
	umull	x0, w2, w0
	ldr	x2, [x4,2600]
	add	x0, x2, x0
	b	.L3346
.L3301:
	ldr	w2, [x4,2592]
	umull	x0, w2, w0
	ldr	x2, [x4,2600]
	add	x0, x2, x0
	ldrh	w2, [x4,2546]
	ldr	x0, [x0,8]
	b	.L3347
.L3299:
	ldr	w0, [x29,228]
	cmn	w0, #1
	beq	.L3303
	str	w0, [x29,236]
	mov	w1, 56
	ldr	w0, [x2,2592]
	str	w28, [x29,256]
	umull	x0, w0, w1
	ldr	x1, [x2,2600]
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
	bne	.L3304
	ldr	w0, [x24,920]
	add	w0, w0, 1
	str	w0, [x24,920]
	b	.L3306
.L3304:
	ldr	x0, [x29,216]
	ldr	w0, [x0,8]
	cmp	w0, w28
	beq	.L3306
	ldr	w0, [x24,920]
	mov	w2, w28
	ldr	x1, [x29,216]
	add	w0, w0, 1
	str	w0, [x24,920]
	ldr	x0, [x29,104]
	ldr	w1, [x1,8]
	bl	printk
	b	.L3306
.L3303:
	ldr	w0, [x2,2592]
	mov	w1, 56
	umull	x0, w0, w1
	ldr	x1, [x2,2600]
	ldrh	w2, [x2,2546]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L3306:
	ldr	w1, [x29,196]
	mov	w0, 56
	lsl	w2, w19, 9
	cbz	w1, .L3307
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w4, [x1,2592]
	umull	x0, w4, w0
	ldr	x4, [x1,2600]
	add	x4, x4, x0
	ldr	x0, [x29,160]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x4,8]
	add	x0, x1, x0
	mov	x1, x26
	b	.L3347
.L3307:
	add	x5, x21, :lo12:.LANCHOR0
	ldr	w4, [x5,2592]
	ldr	x1, [x5,2600]
	umull	x0, w4, w0
	add	x0, x1, x0
	ldrh	w1, [x5,2542]
	mul	w1, w28, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x26, x1
	b	.L3347
.L3295:
	ldr	w0, [x29,212]
	ldrh	w1, [x19,2542]
	ldr	w2, [x19,2592]
	cbz	w0, .L3308
	mul	w1, w28, w1
	umull	x0, w2, w4
	sub	w1, w1, w23
	ldr	x2, [x19,2600]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x26, x1
.L3346:
	str	x1, [x0,8]
	b	.L3302
.L3308:
	umull	x0, w2, w4
	ldr	x2, [x19,2600]
	mul	w1, w28, w1
	add	x0, x2, x0
	ldrh	w2, [x19,2546]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x26, x1
.L3347:
	bl	ftl_memcpy
.L3302:
	ldr	x1, [x29,120]
	mov	w0, -3947
	ldr	x2, [x29,128]
	strh	w0, [x1,x2]
	ldr	x1, [x29,216]
	ldr	w0, [x24,748]
	str	w0, [x1,4]
	add	w0, w0, 1
	cmn	w0, #1
	str	w28, [x1,8]
	csel	w0, w0, wzr, ne
	str	w0, [x24,748]
	ldr	w0, [x29,228]
	str	w0, [x1,12]
	ldrh	w0, [x22]
	strh	w0, [x1,2]
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w0, [x1,2592]
	add	w0, w0, 1
	str	w0, [x1,2592]
	ldr	x0, [x29,200]
	add	x0, x0, 1
	str	x0, [x29,200]
	b	.L3292
.L3349:
	str	w28, [x29,192]
	mov	x0, x2
	b	.L3293
.L3324:
	str	w28, [x29,192]
.L3293:
	sub	w20, w20, w0
	ldr	w0, [x29,212]
	cbnz	w0, .L3312
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x0,2592]
	ldr	w0, [x24,3580]
	cmp	w1, w0
	bcs	.L3312
	ldrh	w0, [x22,4]
	cbz	w0, .L3312
.L3314:
	str	wzr, [x29,212]
	b	.L3285
.L3312:
	bl	FtlCacheWriteBack
	add	x0, x21, :lo12:.LANCHOR0
	cmp	w20, 1
	str	wzr, [x0,2592]
	bhi	.L3285
	b	.L3314
.L3348:
	ldr	w1, [x29,208]
	mov	w0, w20
	sub	w1, w1, w27
	bl	ftl_do_gc
	ldr	x0, [x29,176]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w0, [x0,544]
	cmp	w0, 5
	bls	.L3316
	cmp	w0, 31
	bhi	.L3317
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,72]
	cbnz	w0, .L3317
.L3316:
	ldr	x0, [x29,176]
	mov	w21, 65535
	mov	w22, 128
	add	x19, x0, :lo12:.LANCHOR2
.L3339:
	ldrh	w0, [x19,792]
	cmp	w0, w21
	bne	.L3318
	ldrh	w0, [x19,1448]
	cmp	w0, w21
	bne	.L3318
	ldrh	w0, [x19,1450]
	cmp	w0, w21
	bne	.L3318
	and	w0, w20, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3318:
	mov	w0, 1
	strh	w22, [x19,1362]
	mov	w1, w0
	strh	w22, [x19,1360]
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldr	w0, [x19,416]
	cbnz	w0, .L3317
	ldrh	w0, [x19,544]
	cmp	w0, 2
	bhi	.L3317
	add	w20, w20, 1
	cmp	w20, 256
	bne	.L3339
	b	.L3317
.L3278:
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
	bne	.L3352
	bl	ftl_vendor_write
	mov	w19, w0
.L3352:
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
	add	x19, x19, 1696
	adrp	x22, .LC161
	adrp	x24, .LC162
	lsl	w25, w23, 10
	ldr	x1, [x0,3608]
	mov	w20, 0
	ldr	x0, [x0,3656]
	add	x22, x22, :lo12:.LC161
	str	x1, [x19,8]
	add	x24, x24, :lo12:.LC162
	str	x0, [x19,16]
.L3357:
	adrp	x0, .LANCHOR0+2538
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2538]
	cmp	w20, w0
	bge	.L3363
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
	beq	.L3358
	add	x1, x21, :lo12:.LANCHOR2
	mov	x0, x24
	mov	w2, 4
	mov	w3, 768
	ldr	x1, [x1,3608]
	bl	rknand_print_hex
.L3358:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L3357
.L3363:
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
	adrp	x23, .LC163
	mov	w25, 56
	ldrh	w21, [x0,2476]
	adrp	x24, .LANCHOR2
	mov	w26, 4
	add	x23, x23, :lo12:.LC163
.L3365:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2478]
	cmp	w0, w21
	bls	.L3378
	mov	x22, 0
	ldrh	w5, [x2,2468]
	ldrh	w4, [x2,2548]
	mov	w27, w22
	mov	x20, x2
.L3374:
	cmp	w5, w22, uxth
	bls	.L3379
	add	x0, x20, 2496
	mov	w1, w21
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrb	w0, [x0,x22]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x5, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L3366
	add	x3, x24, :lo12:.LANCHOR2
	lsl	w28, w28, 10
	umull	x0, w27, w25
	mul	w2, w27, w4
	ldr	x1, [x3,3584]
	add	w27, w27, 1
	add	x1, x1, x0
	uxth	w27, w27
	sdiv	w2, w2, w26
	str	w28, [x1,4]
	ldr	x1, [x3,3584]
	add	x0, x1, x0
	ldr	x1, [x3,1392]
	str	x1, [x0,8]
	ldr	x1, [x3,1400]
	add	x2, x1, x2, sxtw 2
	str	x2, [x0,16]
.L3366:
	add	x22, x22, 1
	b	.L3374
.L3379:
	cbz	w27, .L3369
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w27
	mov	w2, 1
	mov	x20, 0
	adrp	x22, .LANCHOR2
	mov	x28, 56
	ldr	x0, [x0,3584]
	bl	FlashReadPages
.L3370:
	cmp	w27, w20, uxth
	bls	.L3369
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x3, [x0,3584]
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
	b	.L3370
.L3369:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3365
.L3378:
	adrp	x20, .LANCHOR4
	adrp	x23, .LC110
	add	x20, x20, :lo12:.LANCHOR4
	mov	w22, 0
	adrp	x27, .LANCHOR2
	add	x20, x20, 1696
	add	x23, x23, :lo12:.LC110
.L3373:
	add	x21, x27, :lo12:.LANCHOR2
	adrp	x26, .LANCHOR2
	ldrh	w0, [x21,3856]
	cmp	w22, w0
	bge	.L3376
	sbfiz	x24, x22, 1, 32
	mov	w21, 0
.L3377:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2538]
	cmp	w21, w0
	bge	.L3380
	add	x25, x26, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x25,3712]
	ldrh	w0, [x0,x24]
	orr	w0, w21, w0, lsl 10
	str	w0, [x20,4]
	mov	x0, x20
	add	w21, w21, 1
	bl	FlashReadPages
	sxth	w21, w21
	ldr	x0, [x20,8]
	ldr	x1, [x25,3712]
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
	b	.L3377
.L3380:
	add	w22, w22, 1
	sxth	w22, w22
	b	.L3373
.L3376:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x21,3712]
	adrp	x0, .LC164
	mov	w2, 2
	add	x0, x0, :lo12:.LC164
	ldr	w3, [x19,2564]
	bl	rknand_print_hex
	ldr	x1, [x21,3752]
	adrp	x0, .LC165
	ldrh	w3, [x19,2572]
	add	x0, x0, :lo12:.LC165
	mov	w2, 4
	bl	rknand_print_hex
	ldr	x1, [x21,3760]
	adrp	x0, .LC166
	ldrh	w3, [x19,2572]
	add	x0, x0, :lo12:.LC166
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
	adrp	x1, .LANCHOR0+2384
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+2384]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3381
	bl	flash_enter_slc_mode
.L3381:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	2
	.global	flash_boot_exit_slc_mode
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	adrp	x1, .LANCHOR0+2384
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+2384]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3383
	bl	flash_exit_slc_mode
.L3383:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	2
	.type	IdBlockReadData, %function
IdBlockReadData:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	mov	w23, w0
	ldr	x0, [x19,1256]
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	ldr	w20, [x19,76]
	mov	w26, w1
	ldrb	w27, [x0,9]
	adrp	x0, .LC167
	mov	x21, x2
	add	x0, x0, :lo12:.LC167
	mov	w1, w23
	mov	w2, w26
	mul	w20, w20, w27
	bl	printk
	mov	w25, 0
	mov	w5, 4
	adrp	x6, .LANCHOR4
	uxth	w20, w20
	udiv	w24, w23, w20
	msub	w24, w24, w20, w23
	sub	w28, w23, w24
	mul	w3, w24, w27
	ubfx	x3, x3, 2, 2
.L3386:
	cmp	w25, w26
	bcs	.L3392
	add	w0, w25, w24
	add	x1, x19, 200
	ubfx	x0, x0, 2, 16
	sub	w20, w5, w3
	uxth	w20, w20
	ldrh	w4, [x1,w0,sxtw 1]
	ldrb	w1, [x19,196]
	cbz	w1, .L3387
	ldr	w2, [x19,2384]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w4, w4, w0, ne
.L3387:
	ldr	x0, [x19,1256]
	add	w3, w3, w28
	str	x5, [x29,96]
	madd	w22, w4, w27, w3
	ldrb	w1, [x0,9]
	add	x0, x6, :lo12:.LANCHOR4
	ldrb	w4, [x19,2456]
	str	x6, [x29,120]
	ldrb	w0, [x0,1634]
	str	x4, [x29,112]
	str	x1, [x29,104]
	bl	FlashBchSel
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	x1, [x29,104]
	mov	x2, x21
	mov	x3, 0
	mov	w0, 0
	udiv	w1, w22, w1
	bl	FlashReadPage
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	ldr	x4, [x29,112]
	mov	w0, w4
	bl	FlashBchSel
	ubfiz	x0, x20, 9, 16
	add	w20, w20, w25
	add	x21, x21, x0
	mov	w3, 0
	uxth	w25, w20
	ldr	x5, [x29,96]
	ldr	x6, [x29,120]
	b	.L3386
.L3392:
	adrp	x0, .LC168
	mov	w1, w23
	mov	w2, w26
	mov	w3, 0
	add	x0, x0, :lo12:.LC168
	bl	printk
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	IdBlockReadData, .-IdBlockReadData
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	stp	x29, x30, [sp, -304]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR0
	mov	x23, x2
	stp	x19, x20, [sp,16]
	mov	w19, w0
	add	x0, x24, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x22, x1
	mov	x28, x24
	ldr	x1, [x0,1256]
	ldr	w21, [x0,76]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	ldrb	w20, [x1,9]
	bl	ftl_malloc
	mov	x26, x0
	mov	w0, -1
	cbz	x26, .L3431
	add	w19, w19, 511
	lsr	w19, w19, 9
	cmp	w19, 8
	bls	.L3415
	cmp	w19, 500
	bhi	.L3431
	b	.L3395
.L3415:
	mov	w19, 8
.L3395:
	ldr	w2, [x22]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	mov	w0, -1
	cmp	w2, w1
	bne	.L3431
	mul	w21, w20, w21
	mov	w3, 0
	mov	w2, 63871
	mov	w4, 4097
	uxth	w21, w21
	add	w0, w21, w0
	add	w0, w0, w19
	udiv	w0, w0, w21
	str	w0, [x29,224]
	add	x0, x22, 253952
	add	x0, x0, 1532
.L3399:
	ldr	w1, [x0]
	cbnz	w1, .L3396
	ldr	w1, [x22,w3,uxtw 2]
	add	w3, w3, 1
	cmp	w3, w4
	sub	w2, w2, #1
	str	w1, [x0,512]
	csel	w3, w3, wzr, cc
	cmp	w2, 4096
	sub	x0, x0, #4
	bne	.L3399
	b	.L3398
.L3396:
	adrp	x0, .LC169
	add	x0, x0, :lo12:.LC169
	bl	printk
.L3398:
	adrp	x0, .LC170
	mov	w3, 5
	add	x20, x28, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC170
	mov	x1, x23
	mov	w2, 4
	adrp	x24, .LANCHOR4
	bl	rknand_print_hex
	ldr	w1, [x22,512]
	adrp	x0, .LC171
	ldrb	w2, [x20,73]
	add	x0, x0, :lo12:.LC171
	bl	printk
	add	x4, x24, :lo12:.LANCHOR4
	adrp	x0, .LC172
	ldrh	w3, [x20,194]
	mov	w1, w19
	add	x0, x0, :lo12:.LC172
	mov	w2, w19
	ldr	w4, [x4,1628]
	bl	printk
	str	x24, [x29,208]
	ldrb	w0, [x20,73]
	ldr	w1, [x22,512]
	cmp	w1, w0
	bls	.L3400
	str	w0, [x22,512]
.L3400:
	lsl	w0, w19, 7
	str	w0, [x29,204]
	adrp	x0, .LC173
	mov	x20, 0
	add	x0, x0, :lo12:.LC173
	str	x0, [x29,192]
	adrp	x0, .LC174
	str	w20, [x29,236]
	add	x0, x0, :lo12:.LC174
	str	x0, [x29,184]
	adrp	x0, .LC175
	add	x0, x0, :lo12:.LC175
	str	x0, [x29,176]
.L3413:
	add	x0, x28, :lo12:.LANCHOR0
	ldr	w1, [x23,x20,lsl 2]
	str	w20, [x29,232]
	ldrb	w0, [x0,73]
	cmp	w1, w0
	bcs	.L3401
	ldr	x0, [x29,208]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w0, [x0,1628]
	cmp	w1, w0
	bcc	.L3401
	ldr	w0, [x29,224]
	cmp	w0, 1
	cset	w0, hi
	str	w0, [x29,228]
	uxtw	x0, w20
	cbz	w0, .L3402
	ldr	w0, [x29,228]
	cbz	w0, .L3402
	uxtw	x0, w20
	sub	w0, w0, #1
	ldr	w0, [x23,x0,lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	beq	.L3401
.L3402:
	mov	w1, 0
	mov	x2, 512
	mov	x0, x26
	bl	memset
	ldr	w24, [x23,x20,lsl 2]
	add	x0, x28, :lo12:.LANCHOR0
	mov	w2, w19
	mul	w24, w21, w24
	ldr	x1, [x0,1256]
	ldr	w25, [x0,76]
	ldr	x0, [x29,192]
	ldrb	w27, [x1,9]
	mov	w1, w24
	bl	printk
	mov	w0, 0
	mul	w25, w25, w27
	bl	flash_boot_enter_slc_mode
	udiv	w1, w24, w27
	mov	w2, 0
	mov	w0, 0
	uxth	w25, w25
	bl	FlashEraseBlock
	cmp	w19, w25
	mov	w2, 1
	bls	.L3403
	mov	w2, 0
	mov	w0, 0
	add	w1, w24, w25
	bl	FlashEraseBlock
	mov	w2, 2
.L3403:
	mov	w0, 0
	str	x2, [x29,216]
	bl	flash_boot_exit_slc_mode
	add	x4, x28, :lo12:.LANCHOR0
	ldr	x2, [x29,216]
	udiv	w8, w24, w25
	mov	w6, 0
	mov	w10, 1073741823
	mov	w11, 61424
	ldr	x1, [x4,1256]
	ldrh	w0, [x1,10]
	ldrb	w5, [x1,12]
	lsl	w0, w0, 2
	msub	w8, w8, w25, w24
	mul	w0, w0, w2
	mov	x25, x22
	sdiv	w5, w0, w5
	sub	w0, w24, w8
	str	w0, [x29,216]
.L3404:
	cmp	w6, w5
	bcs	.L3442
	add	w0, w6, w8
	lsr	w0, w0, 2
	uxth	w1, w0
	and	w0, w0, 65535
	cbz	w1, .L3405
	add	w2, w0, 1
	add	x1, x4, 200
	ldrb	w3, [x4,196]
	ldrh	w1, [x1,w2,sxtw 1]
	cbz	w3, .L3406
	ldr	w7, [x4,2384]
	mov	w3, 12336
	movk	w3, 0x5638, lsl 16
	cmp	w7, w3
	csel	w1, w1, w2, ne
.L3406:
	add	w1, w1, w10
	lsl	w1, w1, 2
	str	w1, [x29,240]
.L3405:
	add	x1, x4, 200
	str	w11, [x29,244]
	ldrh	w7, [x1,w0,sxtw 1]
	ldrb	w1, [x4,196]
	cbz	w1, .L3407
	ldr	w2, [x4,2384]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w7, w7, w0, ne
.L3407:
	ldr	w0, [x29,216]
	ldrb	w12, [x4,2456]
	str	x11, [x29,104]
	madd	w2, w7, w27, w0
	ldr	x0, [x29,208]
	str	x10, [x29,112]
	add	x0, x0, :lo12:.LANCHOR4
	str	x6, [x29,120]
	str	x8, [x29,128]
	str	x5, [x29,136]
	ldrb	w0, [x0,1634]
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
	ldr	x0, [x4,1256]
	ldrb	w1, [x0,9]
	mov	w0, 0
	udiv	w1, w2, w1
	mov	x2, x25
	add	x25, x25, 2048
	bl	FlashProgPage
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	ldr	x12, [x29,160]
	mov	w0, w12
	bl	FlashBchSel
	ldr	w1, [x29,216]
	mov	w0, 0
	ldr	x7, [x29,144]
	udiv	w1, w1, w27
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
	b	.L3404
.L3442:
	ldr	x0, [x29,184]
	mov	w1, w24
	mov	w2, w19
	mov	w3, 0
	bl	printk
	ldr	w0, [x23,x20,lsl 2]
	mov	w1, w19
	mov	x2, x26
	mul	w0, w21, w0
	bl	IdBlockReadData
	mov	x2, x26
	mov	x1, x22
	mov	x0, 0
.L3409:
	ldr	w4, [x29,204]
	mov	w3, w0
	cmp	w0, w4
	bcs	.L3443
	mov	x27, x2
	mov	x25, x1
	mov	x24, x0
	add	x2, x2, 4
	add	x0, x0, 1
	add	x1, x1, 4
	ldr	w5, [x27]
	ldr	w4, [x25]
	cmp	w5, w4
	beq	.L3409
	mov	w1, 0
	mov	x2, 512
	mov	x0, x26
	str	x3, [x29,216]
	and	x24, x24, -256
	bl	memset
	lsl	x24, x24, 2
	ldr	w4, [x27]
	ldr	w5, [x25]
	ldr	w1, [x29,232]
	ldr	w2, [x23,x20,lsl 2]
	ldr	x3, [x29,216]
	ldr	x0, [x29,176]
	bl	printk
	adrp	x0, .LC176
	add	x1, x22, x24
	add	x0, x0, :lo12:.LC176
	mov	w2, 4
	mov	w3, 256
	bl	rknand_print_hex
	adrp	x0, .LC177
	add	x1, x26, x24
	mov	w2, 4
	mov	w3, 256
	add	x0, x0, :lo12:.LC177
	bl	rknand_print_hex
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	w1, [x23,x20,lsl 2]
	mov	w0, 0
	mov	w2, 0
	mul	w1, w21, w1
	bl	FlashEraseBlock
	ldr	w0, [x29,228]
	cbz	w0, .L3410
	ldr	w1, [x23,x20,lsl 2]
	mov	w0, 0
	mov	w2, 0
	madd	w1, w1, w21, w21
	bl	FlashEraseBlock
.L3410:
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	adrp	x0, .LC178
	ldr	w1, [x23,x20,lsl 2]
	add	x0, x0, :lo12:.LC178
	bl	printk
	b	.L3401
.L3443:
	ldr	w0, [x29,236]
	add	w0, w0, 1
	str	w0, [x29,236]
.L3401:
	add	x20, x20, 1
	cmp	x20, 5
	bne	.L3413
	mov	x0, x26
	bl	ftl_free
	ldr	w0, [x29,236]
	cmp	w0, wzr
	csetm	w0, eq
.L3431:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 304
	ret
	.size	write_idblock, .-write_idblock
	.align	2
	.global	CRC_32
	.type	CRC_32, %function
CRC_32:
	mov	x2, 0
	adrp	x3, .LANCHOR1
	mov	x5, x0
	add	x3, x3, :lo12:.LANCHOR1
	mov	w0, w2
.L3445:
	cmp	w1, w2
	bls	.L3447
	ldrb	w4, [x5,x2]
	add	x6, x3, 3368
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3445
.L3447:
	ret
	.size	CRC_32, .-CRC_32
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
	bne	.L3449
	ldr	w1, [x2]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L3449
	add	x22, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x22,1504]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	str	x0, [x22,1512]
	mov	w2, 59392
	mov	w1, 0
	movk	w2, 0x3, lsl 16
	bl	ftl_memset
	str	w19, [x22,1520]
.L3449:
	add	x23, x20, :lo12:.LANCHOR5
	adrp	x0, .LC179
	ldr	w2, [x24]
	add	x0, x0, :lo12:.LC179
	mov	w3, w19
	mov	w4, w21
	ldr	x1, [x23,1512]
	bl	printk
	ldrb	w0, [x23,1504]
	cbz	w0, .L3448
	sub	w0, w19, #64
	ldr	x22, [x23,1512]
	cmp	w0, 499
	bhi	.L3451
	mov	w3, 564
	ubfiz	x0, x0, 9, 25
	sub	w3, w3, w19
	add	x0, x22, x0
	cmp	w3, w21
	mov	x1, x24
	csel	w2, w3, w21, ls
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L3452
.L3451:
	cmp	w19, 563
	bhi	.L3462
.L3452:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,1520]
	cmp	w1, w19
	beq	.L3460
	strb	wzr, [x0,1504]
	cbz	x22, .L3461
	mov	x0, x22
	bl	ftl_free
.L3461:
	add	x0, x20, :lo12:.LANCHOR5
	str	xzr, [x0,1512]
.L3460:
	add	x20, x20, :lo12:.LANCHOR5
	add	w19, w19, w21
	str	w19, [x20,1520]
	b	.L3448
.L3462:
	ldr	w2, [x23,1520]
	mov	w0, 500
	sub	w2, w2, #64
	cmp	w2, 500
	csel	w2, w2, w0, ls
	adrp	x0, .LANCHOR0+1256
	ldr	x0, [x0,#:lo12:.LANCHOR0+1256]
	ldrb	w0, [x0,9]
	cmp	w0, 4
	beq	.L3463
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
	b	.L3454
.L3463:
	mov	x3, 0
.L3453:
	cmp	w2, 256
	add	x0, x29, 72
	bls	.L3455
	lsl	w1, w3, 1
	str	w1, [x0,x3,lsl 2]
	b	.L3456
.L3455:
	str	w3, [x0,x3,lsl 2]
.L3456:
	add	x3, x3, 1
	cmp	x3, 5
	bne	.L3453
.L3454:
	add	x0, x22, 253952
	mov	w3, 63872
	add	x0, x0, 1536
.L3459:
	ldr	w1, [x0]
	cbz	w1, .L3457
	add	w3, w3, 128
	lsl	w0, w3, 2
	b	.L3458
.L3457:
	sub	w3, w3, #1
	sub	x0, x0, #4
	cmp	w3, 4096
	bne	.L3459
	lsl	w0, w2, 9
.L3458:
	mov	x1, x22
	add	x2, x29, 72
	add	x22, x20, :lo12:.LANCHOR5
	bl	write_idblock
	ldr	x0, [x22,1512]
	strb	wzr, [x22,1504]
	bl	ftl_free
	str	xzr, [x22,1512]
	b	.L3460
.L3448:
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
	bhi	.L3473
	cbnz	w21, .L3473
	mov	w0, w1
	mov	w1, w2
	mov	x2, x3
	str	x3, [x29,56]
	bl	write_loader_lba
	ldr	x3, [x29,56]
.L3473:
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
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	w19, w1
	mov	x20, x2
	beq	.L3482
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3483
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3484
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3485
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3486
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3487
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3544
	bl	rknand_dev_flush
	b	.L3578
.L3487:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3489
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3490
	b	.L3544
.L3485:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3491
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3492
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3493
	b	.L3544
.L3492:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3491
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3491
	b	.L3544
.L3483:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3494
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3495
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3496
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3497
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3498
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3499
	b	.L3544
.L3495:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3500
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3501
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3502
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3503
	b	.L3544
.L3501:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3503
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3500
	b	.L3544
.L3497:
	adrp	x0, .LC180
	add	x0, x0, :lo12:.LC180
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3504
.L3509:
	mov	x0, -12
	b	.L3481
.L3504:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3585
	adrp	x0, .LC182
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC182
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3582
	bl	rknand_device_lock
	ldr	w1, [x29,52]
	mov	x2, x19
	ldr	w0, [x19]
	bl	IdBlockReadData
	bl	rknand_device_unlock
	ldr	w2, [x29,52]
	mov	x0, x20
	mov	x1, x19
	ubfiz	x2, x2, 9, 23
	bl	rk_copy_to_user
	cbz	x0, .L3588
	adrp	x0, .LC183
	add	x0, x0, :lo12:.LC183
.L3580:
	bl	printk
.L3582:
	mov	x0, x19
.L3583:
	bl	ftl_free
.L3581:
	mov	x0, -14
	b	.L3481
.L3496:
	adrp	x0, .LC184
	add	x0, x0, :lo12:.LC184
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3509
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3585
	adrp	x0, .LC185
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC185
	bl	printk
	adrp	x0, .LANCHOR5
	add	x22, x0, :lo12:.LANCHOR5
	mov	x20, x0
	ldr	x21, [x22,1528]
	cbnz	x21, .L3511
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x22,1528]
	cbz	x0, .L3582
	mov	w1, w21
	mov	x2, 260096
	bl	memset
.L3511:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3582
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3582
	add	x20, x20, :lo12:.LANCHOR5
	uxtw	x2, w2
	ldr	x1, [x20,1528]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3588:
	mov	x0, x19
	bl	ftl_free
	b	.L3578
.L3499:
	adrp	x0, .LC186
	add	x0, x0, :lo12:.LC186
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3509
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3514
.L3585:
	adrp	x0, .LC181
	add	x0, x0, :lo12:.LC181
	b	.L3580
.L3514:
	ldr	w1, [x19]
	adrp	x0, .LC187
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC187
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3582
	adrp	x20, .LANCHOR5
	add	x20, x20, :lo12:.LANCHOR5
	ldr	x0, [x20,1528]
	cbz	x0, .L3582
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3517
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3481
.L3517:
	bl	rknand_device_lock
	ldr	x1, [x20,1528]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,1528]
	bl	ftl_free
	str	xzr, [x20,1528]
	b	.L3588
.L3498:
	adrp	x0, .LC188
	add	x0, x0, :lo12:.LC188
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3509
	bl	ftl_read_flash_info
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3590
.L3482:
	adrp	x0, .LC189
	add	x0, x0, :lo12:.LC189
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3509
	mov	w1, 0
	mov	w2, 64
	bl	ftl_memset
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3590
.L3494:
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
	bl	printk
	adrp	x0, .LANCHOR5+1536
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR5+1536]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
.L3573:
	bl	rk_copy_to_user
	cbnz	x0, .L3581
	b	.L3578
.L3502:
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3509
	bl	rknand_device_lock
	mov	w1, 264
	mov	w2, 2
	mov	x3, x21
	mov	w0, 16
	bl	ftl_read
	bl	rknand_device_unlock
	mov	x0, x20
	mov	x1, x21
	mov	x2, 1024
.L3590:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3583
	bl	ftl_free
	b	.L3508
.L3486:
	adrp	x0, .LC192
	add	x0, x0, :lo12:.LC192
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3522
.L3527:
	adrp	x0, .LC181
	add	x0, x0, :lo12:.LC181
	bl	printk
	b	.L3581
.L3522:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3523
.L3524:
	mov	x19, -1
	b	.L3508
.L3523:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3524
	adrp	x21, .LANCHOR5
	add	x0, x29, 56
	add	x21, x21, :lo12:.LANCHOR5
	mov	x2, 512
	ldr	x1, [x21,1536]
	bl	memcpy
	ldr	w1, [x21,1544]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3525
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3525:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
	b	.L3587
.L3489:
	adrp	x0, .LC193
	add	x0, x0, :lo12:.LC193
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3527
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3524
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3524
	adrp	x1, .LANCHOR5
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR5
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,1544]
	cmp	w3, w2
	bne	.L3481
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L3481
	ldr	x19, [x1,1536]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3579
.L3493:
	adrp	x0, .LC194
	add	x0, x0, :lo12:.LC194
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3527
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3524
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3524
	adrp	x19, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR5
	ldr	w1, [x0,1548]
	cbnz	w1, .L3528
.L3531:
	mov	x0, 0
	b	.L3481
.L3528:
	ldr	x1, [x0,1552]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3529
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,1552]
	str	w2, [x1,4]
	ldr	x0, [x0,1552]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3529:
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
	beq	.L3530
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1536]
	str	w1, [x0,4]
	ldr	x0, [x20,1536]
	str	wzr, [x0,8]
.L3530:
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
	b	.L3578
.L3484:
	adrp	x0, .LC195
	add	x0, x0, :lo12:.LC195
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3527
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3524
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3524
	adrp	x19, .LANCHOR5
	add	x1, x19, :lo12:.LANCHOR5
	ldr	w2, [x1,1548]
	cmp	w2, 1
	beq	.L3531
	ldr	x2, [x1,1552]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3532
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,1552]
	str	w3, [x2,4]
	ldr	x1, [x1,1552]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3532:
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
	beq	.L3533
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1536]
	str	w1, [x0,4]
	ldr	x0, [x20,1536]
	str	wzr, [x0,8]
.L3533:
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
	b	.L3578
.L3490:
	adrp	x0, .LC196
	add	x0, x0, :lo12:.LC196
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3527
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3524
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3524
	adrp	x1, .LANCHOR5
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR5
	uxtw	x2, w2
	add	x1, x1, 1560
	b	.L3586
.L3491:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3534
	adrp	x0, .LC197
	add	x0, x0, :lo12:.LC197
	b	.L3576
.L3534:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3536
	adrp	x0, .LC198
	add	x0, x0, :lo12:.LC198
	b	.L3576
.L3536:
	adrp	x0, .LC199
	add	x0, x0, :lo12:.LC199
.L3576:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3527
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3581
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR5
	bne	.L3537
	add	x0, x0, :lo12:.LANCHOR5
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,1536]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3481
	b	.L3581
.L3537:
	add	x20, x0, :lo12:.LANCHOR5
	ldr	w1, [x20,2072]
	cmp	w1, 10
	bhi	.L3581
	ldr	x1, [x20,1536]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3538
	cbz	w3, .L3538
	adrp	x0, .LC200
	mov	w1, w2
	add	x0, x0, :lo12:.LC200
	bl	printk
	ldr	w0, [x20,2072]
	add	w0, w0, 1
	str	w0, [x20,2072]
	b	.L3581
.L3538:
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,2072]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3539
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3540
.L3539:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3540:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3578
	b	.L3508
.L3503:
	adrp	x0, .LC201
	add	x0, x0, :lo12:.LC201
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3527
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3524
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3524
	mov	w0, 30224
	adrp	x1, .LANCHOR5
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR5
	add	x0, x29, 64
	bne	.L3541
	ldr	x1, [x1,2080]
	b	.L3577
.L3541:
	ldr	x1, [x1,2088]
.L3577:
	add	x1, x1, 8
.L3586:
	bl	memcpy
.L3587:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
	b	.L3573
.L3500:
	adrp	x0, .LC202
	add	x0, x0, :lo12:.LC202
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3527
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3524
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3524
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	bne	.L3543
	ldr	x0, [x19,2080]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,2080]
	b	.L3579
.L3543:
	ldr	x0, [x19,2088]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,2088]
	mov	w0, 3
.L3579:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3508
.L3578:
	mov	x19, 0
.L3508:
	adrp	x0, .LC203
	mov	x1, x19
	add	x0, x0, :lo12:.LC203
	bl	printk
	mov	x0, x19
	b	.L3481
.L3544:
	mov	x0, -22
.L3481:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
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
	ldr	x1, [x19,1480]
	str	w0, [x19,1520]
	add	x0, x1, 512
	str	x0, [x19,1536]
	add	x0, x1, 1024
	str	x1, [x19,1552]
	str	x0, [x19,2080]
	add	x1, x1, 1536
	add	x0, x19, 1560
	strb	wzr, [x19,1504]
	str	xzr, [x19,1512]
	str	xzr, [x19,1528]
	str	x1, [x19,2088]
	bl	ftl_memcpy
	ldr	x21, [x19,1552]
	str	wzr, [x19,1544]
	str	wzr, [x19,2072]
	ldr	w0, [x21,16]
	ldr	w22, [x21,508]
	str	w0, [x19,1548]
	cbz	w22, .L3593
	mov	x0, x21
	mov	w1, 508
	bl	js_hash
	cmp	w22, w0
	beq	.L3593
	adrp	x0, .LC204
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC204
	str	wzr, [x19,1548]
	bl	printk
.L3593:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,1548]
	cbz	w1, .L3594
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,1544]
.L3594:
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
	str	x0, [x1,2096]
	cbz	x0, .L3603
	mov	w23, 0
	adrp	x24, .LC205
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC205
.L3607:
	add	x20, x21, :lo12:.LANCHOR5
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,2096]
	bl	FlashBootVendorRead
	cbnz	w0, .L3604
	ldr	x3, [x20,2096]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,2096]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3605
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3605
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L3605:
	cbnz	w22, .L3615
	mov	w22, 1
	b	.L3607
.L3615:
	cbz	w19, .L3608
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L3603
	b	.L3604
.L3608:
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
	b	.L3603
.L3604:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w19, -1
	ldr	x0, [x21,2096]
	bl	kfree
	str	xzr, [x21,2096]
.L3603:
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
	adrp	x3, .LANCHOR5+2096
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR5+2096]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3617
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3618:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3622
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3618
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
	b	.L3617
.L3622:
	mov	w3, -1
.L3617:
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
	adrp	x0, .LANCHOR5+2096
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR5+2096]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3624
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3625:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3640
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3625
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3626
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3624
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3627:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3641
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
	b	.L3627
.L3641:
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
	b	.L3639
.L3626:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3639
.L3640:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3624
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
.L3639:
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
.L3624:
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
	cbz	x0, .L3643
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3645
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3646
	b	.L3657
.L3645:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3657
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3648
.L3649:
	mov	x20, -1
	b	.L3644
.L3648:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3649
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3644
.L3646:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3657
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3649
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3649
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3657
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3644
.L3657:
	mov	x20, -14
.L3644:
	mov	x0, x19
	bl	kfree
.L3643:
	mov	x0, x20
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
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
	.type	__func__.20892, %object
	.size	__func__.20892, 11
__func__.20892:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.21667, %object
	.size	__func__.21667, 17
__func__.21667:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.21686, %object
	.size	__func__.21686, 16
__func__.21686:
	.string	"FtlScanAllBlock"
	.type	__func__.21952, %object
	.size	__func__.21952, 17
__func__.21952:
	.string	"ftl_scan_all_ppa"
	.zero	7
	.type	__func__.21635, %object
	.size	__func__.21635, 12
__func__.21635:
	.string	"FtlCheckVpc"
	.zero	4
	.type	__func__.21934, %object
	.size	__func__.21934, 21
__func__.21934:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.20965, %object
	.size	__func__.20965, 8
__func__.20965:
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
	.string	"FTL version: 5.0.60 20200218"
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
	.string	"ECC:%d\n"
.LC95:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC96:
	.string	"FlashLoadPhyInfo fail %x!!\n"
.LC97:
	.string	"Read pageadd=%x  ecc=%x err=%x\n"
.LC98:
	.string	"data:"
.LC99:
	.string	"spare:"
.LC100:
	.string	"ReadRetry pageadd=%x ecc=%x err=%x\n"
.LC101:
	.string	"FLFB:%d %d\n"
.LC102:
	.string	"prog error: = %x\n"
.LC103:
	.string	"prog read error: = %x\n"
.LC104:
	.string	"prog read s error: = %x %x %x\n"
.LC105:
	.string	"prog read d error: = %x %x %x\n"
.LC106:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data= %x\n"
.LC107:
	.string	"...%s enter...\n"
.LC108:
	.string	"superBlkID = %x vpc=%x\n"
.LC109:
	.string	"flashmode = %x pagenum = %x %x\n"
.LC110:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC111:
	.string	"blk = %x vpc=%x mode = %x\n"
.LC112:
	.string	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC113:
	.string	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC114:
	.string	"ftl_scan_all_ppa blk %x page %x\n"
.LC115:
	.string	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\n"
.LC116:
	.string	"%s finished\n"
.LC117:
	.string	"FlashMakeFactorBbt %d\n"
.LC118:
	.string	"bad block:%d %d\n"
.LC119:
	.string	"FMFB:%d %d\n"
.LC120:
	.string	"E:bad block:%d\n"
.LC121:
	.string	"FMFB:Save %d %d\n"
.LC122:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC123:
	.string	"FtlBbmTblFlush error:%x\n"
.LC124:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC125:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC126:
	.string	"decrement_vpc_count %x = %d\n"
.LC127:
	.string	"decrement_vpc_count %x = %d in free list\n"
.LC128:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC129:
	.string	"page map lost: %x %x\n"
.LC130:
	.string	"FtlMapWritePage error = %x\n"
.LC131:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC132:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC133:
	.string	"no ect"
.LC134:
	.string	"slc mode"
.LC135:
	.string	"BBT:"
.LC136:
	.string	"region_id = %x phyAddr = %x\n"
.LC137:
	.string	"map_ppn:"
.LC138:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC139:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC140:
	.string	"free blk vpc error %x = %x  %x\n"
.LC141:
	.string	"Ftlscanalldata = %x\n"
.LC142:
	.string	"scan lpa = %x ppa= %x\n"
.LC143:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC144:
	.string	"RSB refresh addr %x\n"
.LC145:
	.string	"spuer block %x vpn is 0\n "
.LC146:
	.string	"g_recovery_ppa %x ver %x\n "
.LC147:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC148:
	.string	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
.LC149:
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC150:
	.string	"GC des block %x done\n"
.LC151:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC152:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC153:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC154:
	.string	"rk_ftl_de_init %x\n"
.LC155:
	.string	"...%s: no bad block mapping table, format device\n"
.LC156:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
.LC157:
	.string	"FtlInit %x\n"
.LC158:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC159:
	.string	"erase power lost blk = %x vpc=%x\n"
.LC160:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC161:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\n"
.LC162:
	.string	":"
.LC163:
	.string	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC164:
	.string	"Mblk:"
.LC165:
	.string	"L2P:"
.LC166:
	.string	"L2PC:"
.LC167:
	.string	"IdBlockReadData %x %x\n"
.LC168:
	.string	"IdBlockReadData %x %x ret= %x\n"
.LC169:
	.string	"write_idblock fix data %x %x\n"
.LC170:
	.string	"idblk:"
.LC171:
	.string	"idb reverse %x %x\n"
.LC172:
	.string	"write_idblock totle_sec %x %x %x %x\n"
.LC173:
	.string	"IDBlockWriteData %x %x\n"
.LC174:
	.string	"IDBlockWriteData %x %x ret= %x\n"
.LC175:
	.string	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x\n"
.LC176:
	.string	"write"
.LC177:
	.string	"read"
.LC178:
	.string	"write_idblock error %d\n"
.LC179:
	.string	"wl_lba %p %x %x %x\n"
.LC180:
	.string	"READ_SECTOR_IO\n"
.LC181:
	.string	"rk_copy_from_user error\n"
.LC182:
	.string	"READ_SECTOR_IO %x %x\n"
.LC183:
	.string	"rk_copy_to_user error\n"
.LC184:
	.string	"WRITE_SECTOR_IO\n"
.LC185:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC186:
	.string	"END_WRITE_SECTOR_IO\n"
.LC187:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC188:
	.string	"GET_FLASH_INFO_IO\n"
.LC189:
	.string	"GET_BAD_BLOCK_IO\n"
.LC190:
	.string	"GET_LOCK_FLAG_IO\n"
.LC191:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC192:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC193:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC194:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC195:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC196:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC197:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC198:
	.string	"RKNAND_LOADER_STATUS\n"
.LC199:
	.string	"RKNAND_LOADER_LOCK\n"
.LC200:
	.string	"LockKey not match %d\n"
.LC201:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC202:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC203:
	.string	"return ret = %lx\n"
.LC204:
	.string	"secureBootEn check error\n"
.LC205:
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
	.type	ftl_gc_temp_block_bops_scan_page_addr, %object
	.size	ftl_gc_temp_block_bops_scan_page_addr, 2
ftl_gc_temp_block_bops_scan_page_addr:
	.hword	-1
	.zero	2
	.type	power_up_flag, %object
	.size	power_up_flag, 4
power_up_flag:
	.word	1
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
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 8
gpNandParaInfo:
	.zero	8
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
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.zero	1
	.type	gNandMaxDie, %object
	.size	gNandMaxDie, 1
gNandMaxDie:
	.zero	1
	.zero	2
	.type	DieCsIndex, %object
	.size	DieCsIndex, 8
DieCsIndex:
	.zero	8
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
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.zero	1
	.zero	2
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
	.zero	4
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
