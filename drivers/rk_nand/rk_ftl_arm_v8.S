/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2019-05-08
 */
	.cpu generic+fp+simd
	.file	"rk_ftl_arm_v8.S"
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
	cmp	w0, 256
	add	x1, x1, :lo12:.LANCHOR0
	bls	.L29
	mov	w0, 512
	b	.L32
.L29:
	cmp	w0, 128
	bls	.L32
	mov	w0, 256
.L32:
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
	bne	.L35
	mov	w6, 6
	str	w6, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w5, [x2,2052]
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	b	.L37
.L35:
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
.L37:
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
	cbz	w2, .L40
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
.L40:
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
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 8
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
.L49:
	cmp	w1, w2
	bls	.L51
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L49
.L51:
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
.L55:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,121]
	cmp	w0, w20
	bls	.L61
	add	x0, x19, 256
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	x0, 1000
	bl	__const_udelay
	ldrb	w0, [x25,120]
	cmp	w0, 34
	bne	.L56
	ldrsb	w0, [x24,x20]
	b	.L60
.L56:
	cmp	w0, 35
	bne	.L58
	ldrsb	w0, [x21,x20]
	b	.L60
.L58:
	ldrsb	w0, [x23,400]
.L60:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L55
.L61:
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
.L63:
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,121]
	cmp	w0, w20
	bls	.L65
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
	b	.L63
.L65:
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
	bne	.L69
	add	x1, x7, :lo12:.LANCHOR0
	ldrb	w5, [x1,196]
	cbnz	w5, .L69
	add	x1, x1, 200
	ldrh	w8, [x1,w8,sxtw 1]
.L69:
	add	x7, x7, :lo12:.LANCHOR0
	cmp	w4, 1
	add	x7, x7, 712
	mov	w1, 0
	ldr	w5, [x7,w10,uxtw 2]
	madd	w5, w6, w9, w5
	add	w5, w5, w8
	str	w5, [x2]
	str	w10, [x3]
	bls	.L70
	ldr	w1, [x0,4]
	ldr	w0, [x0,60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w1, eq
.L70:
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
	ldr	x0, [x4,744]
	ldrb	w19, [x6,8]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	bne	.L73
	add	x4, x4, 88
	cbnz	w2, .L74
	ldrb	w2, [x4,13]
	b	.L82
.L74:
	ldrb	w2, [x4,14]
.L82:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,103]
	cbz	w4, .L77
	add	x3, x19, 8
	lsl	x3, x3, 8
.L81:
	cmp	w2, w4
	bcs	.L77
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L81
.L73:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L77:
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
.L87:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L87
	tbz	x3, 6, .L87
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
	ldr	w2, [x1,752]
	str	w0, [x1,752]
	mov	w0, w2
	ret
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	2
	.global	FlashGetPageSize
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	adrp	x0, .LANCHOR0+744
	ldr	x0, [x0,#:lo12:.LANCHOR0+744]
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
	beq	.L100
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
	b	.L102
.L100:
	and	w1, w1, -8193
.L102:
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
	ldr	x0, [x19,744]
	mov	x26, x24
	uxtb	w25, w3
	ldrb	w0, [x0,19]
	cmp	w0, 6
	bne	.L108
	add	x19, x19, x24, lsl 6
	lsl	w4, w25, 2
	add	x19, x19, 780
	b	.L114
.L108:
	cmp	w0, 7
	bne	.L110
	mov	x2, 160
	mov	w4, 10
	madd	x19, x24, x2, x19
	mul	w4, w25, w4
	add	x19, x19, 788
.L114:
	add	x19, x19, x4, sxtw
	b	.L109
.L110:
	cmp	w0, 8
	bne	.L111
	add	x19, x19, 788
	add	w4, w25, w25, lsl 2
	b	.L114
.L111:
	lsl	x4, x24, 3
	add	x4, x4, x25, sxtw
	add	x19, x19, x4, lsl 3
	add	x19, x19, 780
.L109:
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
.L112:
	cmp	x23, x27
	beq	.L115
	ldrb	w0, [x21,x23]
	str	w0, [x22,2052]
	mov	x0, 1000
	bl	__const_udelay
	ldrsb	w0, [x19,x23]
	add	x23, x23, 1
	str	w0, [x22,2048]
	b	.L112
.L115:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, 22
	add	x24, x20, x24
	str	w0, [x22,2056]
	mov	w0, w26
	bl	NandcFlashDeCs
	strb	w25, [x24,1616]
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
	ldr	x0, [x19,744]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L116
	add	x21, x19, 1620
	add	x22, x19, 764
.L121:
	lsl	x1, x20, 3
	uxtb	w0, w20
	ldrb	w1, [x1,x21]
	cmp	w1, 173
	bne	.L118
	ldrb	w1, [x19,761]
	mov	x2, x22
	mov	w3, 0
	bl	HynixSetRRPara
.L118:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L121
.L116:
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
	add	x0, x20, 1652
	add	x19, x0, x1
	ldrb	w21, [x0,x1]
	ldr	x0, [x19,8]
	cbz	x0, .L125
	mov	w0, w21
	add	x20, x20, 712
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
	cbz	x1, .L125
	str	w0, [x1]
	str	xzr, [x19,16]
.L125:
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
.L133:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L134
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L133
	mov	w0, -1
	b	.L132
.L134:
	mov	w0, 0
.L132:
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
.L138:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L139
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L138
	mov	w0, -1
	b	.L137
.L139:
	mov	w0, 0
.L137:
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
	cbz	w0, .L142
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
.L142:
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
	cbz	w0, .L147
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
.L147:
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
	add	x4, x1, 1620
	add	x29, sp, 0
	ldrb	w2, [x1,1844]
	mov	w9, 128
	mov	w10, 1
	mov	w12, 35
	and	w11, w2, 1
	and	w2, w2, 4
	mov	w13, 32
	mov	w14, 5
	uxtb	w5, w2
.L164:
	ldrb	w3, [x6,x4]
	ldr	x1, [x7]
	cmp	w3, 152
	ldrb	w2, [x7,8]
	beq	.L154
	cmp	w3, 69
	beq	.L154
	cmp	w3, 173
	beq	.L154
	cmp	w3, 44
	bne	.L155
.L154:
	cmp	w0, 1
	bne	.L157
	cbz	w11, .L155
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L158
	str	w0, [x1,2052]
	b	.L175
.L158:
	cmp	w3, 44
	bne	.L160
	str	w0, [x1,2052]
	str	w14, [x1,2048]
	b	.L162
.L160:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L162
.L157:
	cbz	w5, .L155
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L161
	str	w10, [x1,2052]
	str	w13, [x1,2048]
	b	.L162
.L161:
	cmp	w3, 44
	bne	.L163
	str	w10, [x1,2052]
	str	w12, [x1,2048]
	b	.L162
.L163:
	str	w9, [x1,2052]
.L175:
	str	wzr, [x1,2048]
.L162:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L155:
	add	x6, x6, 8
	add	x7, x7, 16
	cmp	x6, 32
	bne	.L164
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
.L179:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,121]
	cmp	w4, w2
	bls	.L184
	ldrb	w3, [x3,120]
	cmp	w3, 67
	bne	.L180
	ldrsb	w3, [x0,x2]
	b	.L183
.L180:
	ldrsb	w3, [x1,x2]
.L183:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L179
.L184:
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
	add	x25, x21, 1652
	add	x23, x23, :lo12:.LC0
.L187:
	cmp	w22, w24
	beq	.L196
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,1845]
	cmp	w2, w0
	bcc	.L188
	mov	w0, -1
	str	w0, [x20]
	b	.L189
.L188:
	uxtw	x2, w2
	add	x0, x21, x2
	ldrb	w19, [x0,1848]
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
	tbz	x0, 0, .L190
	mov	w0, -1
	str	w0, [x20]
.L190:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L191
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L191:
	mov	w0, w19
	bl	NandcFlashDeCs
.L189:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L187
.L196:
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
	bne	.L198
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
	b	.L199
.L198:
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
.L199:
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
	ldr	w0, [x0,1856]
	cbz	w0, .L201
	mov	w0, 0
	bl	flash_enter_slc_mode
	b	.L202
.L201:
	bl	flash_exit_slc_mode
.L202:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,1860]
	cbz	w0, .L203
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L203
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1860]
.L203:
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
	ble	.L212
	ldr	x0, [x1,128]
	mov	w1, 8354
	b	.L219
.L212:
	cmp	w0, 220
	ble	.L214
	ldr	x0, [x1,128]
	b	.L220
.L214:
	cmp	w0, 185
	ble	.L215
	ldr	x0, [x1,128]
	mov	w1, 4226
	b	.L219
.L215:
	cmp	w0, 160
	ldr	x0, [x1,128]
	ble	.L216
	mov	w1, 4194
	b	.L219
.L216:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L219
	cmp	w19, 99
	mov	w1, 4225
	bls	.L219
.L220:
	mov	w1, 8322
.L219:
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
	bls	.L222
	mov	w1, 8322
	cmp	w0, w1
	bne	.L223
.L222:
	adrp	x1, .LANCHOR0+128
	ldr	x1, [x1,#:lo12:.LANCHOR0+128]
	str	w0, [x1,4]
.L223:
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
	str	w3, [x1,1864]
	ldr	w3, [x0,352]
	and	w2, w2, 245760
	orr	w2, w2, 256
	ubfx	x3, x3, 16, 4
	str	w3, [x1,1868]
	ldr	w3, [x0,352]
	str	w3, [x1,1872]
	cmp	w3, 2049
	bne	.L225
	mov	w3, 8
	str	w3, [x1,1868]
.L225:
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
	str	wzr, [x19,1928]
	str	x0, [x19,1880]
	str	x0, [x19,1888]
	add	x0, x0, 32768
	str	wzr, [x19,1936]
	str	x0, [x19,1896]
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
	str	w0, [x1,1940]
	mov	w1, 4096
	str	w3, [x2,8]
	bne	.L228
.L231:
	and	w1, w1, -17
	b	.L229
.L228:
	cmp	w0, 24
	bne	.L230
	orr	w1, w1, 16
	b	.L229
.L230:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L231
.L229:
	orr	w1, w1, 1
	str	w1, [x2,12]
	ret
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0+1944
	uxtb	w0, w0
	add	x29, sp, 0
	strb	w0, [x1,#:lo12:.LANCHOR0+1944]
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
	add	x19, x0, 1620
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
.L238:
	lsl	x0, x21, 3
	ldrb	w0, [x0,x19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L237
	mov	w0, w21
	bl	FlashReset
.L237:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L238
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,1860]
	cbz	w0, .L239
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,1844]
	bl	NandcSetMode
	ldr	w0, [x19,152]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L239:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,744]
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
	tbnz	x0, 9, .L247
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L246
.L247:
	ldr	w0, [x19,368]
	orr	w0, w0, 2
	str	w0, [x19,368]
	ldr	w0, [x19,364]
	and	w0, w0, -3
	str	w0, [x19,364]
.L246:
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
	cbz	w2, .L250
	mov	w1, w23
	bl	FlashEraseSLc2KBlocks
	b	.L251
.L250:
	mov	w28, 56
	add	x25, x19, 1652
	mov	x26, 24
.L281:
	cmp	w20, w23
	bcs	.L282
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
	ldrb	w1, [x19,1845]
	ldr	w0, [x29,124]
	ldr	x5, [x29,96]
	cmp	w0, w1
	bcc	.L252
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L253
.L252:
	ldrb	w1, [x19,1945]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w22, w22, wzr, ne
	madd	x1, x1, x26, x25
	ldr	x1, [x1,8]
	cbz	x1, .L255
	bl	FlashWaitCmdDone
.L255:
	ldr	w0, [x29,124]
	ldr	w1, [x29,120]
	madd	x2, x0, x26, x25
	str	x21, [x2,8]
	str	xzr, [x2,16]
	str	w1, [x2,4]
	cbz	w22, .L256
	add	w1, w20, 1
	umull	x1, w1, w28
	add	x1, x24, x1
	str	x1, [x2,16]
.L256:
	add	x1, x19, x0
	mul	x0, x0, x26
	ldrb	w21, [x1,1848]
	strb	w21, [x25,x0]
	mov	w0, w21
	bl	NandcFlashCs
	cmp	w27, 1
	bne	.L257
	ldrb	w0, [x19,196]
	cbz	w0, .L257
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L258
.L257:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L258:
	ldr	w1, [x29,124]
	add	x0, x19, 712
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
.L253:
	add	w20, w20, 1
	b	.L281
.L282:
	ldr	x0, [x29,104]
	mov	x20, 0
	mov	x22, 24
	add	x19, x0, :lo12:.LANCHOR0
	add	x21, x19, 1652
	ldr	x0, [x19,128]
	bl	NandcIqrWaitFlashReady
.L260:
	ldrb	w0, [x19,1845]
	cmp	w0, w20
	bls	.L283
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w27, 1
	bne	.L261
	ldrb	w0, [x19,196]
	cbz	w0, .L261
	mul	x0, x20, x22
	ldrb	w0, [x0,x21]
	bl	flash_exit_slc_mode
.L261:
	add	x20, x20, 1
	b	.L260
.L283:
	ldr	w0, [x19,1948]
	cbnz	w0, .L263
.L265:
	mov	w0, 0
	b	.L251
.L263:
	ldrb	w0, [x19,1620]
	cmp	w0, 69
	bne	.L265
	mov	x0, 0
	mov	x1, 56
.L264:
	cmp	w23, w0
	bls	.L265
	mul	x2, x0, x1
	add	x0, x0, 1
	str	wzr, [x24,x2]
	b	.L264
.L251:
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
.L287:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L287
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
	str	x25, [sp,64]
	uxtb	w25, w1
	add	x1, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	uxtb	w23, w2
	add	x2, x1, 8
	stp	x21, x22, [sp,32]
	mov	x22, x4
	add	x4, x2, x0
	ldr	x21, [x2,x0]
	mov	w2, 16
	ubfiz	w20, w25, 1, 1
	ldrb	w0, [x4,8]
	orr	w20, w20, 8
	ldr	w24, [x21,12]
	bfi	w24, w2, 8, 8
	and	w24, w24, -9
	bfi	w24, w0, 5, 3
	mov	w0, 1
	bfi	w20, w0, 5, 2
	ldr	w0, [x1,1868]
	orr	w20, w20, 536870912
	cmp	w0, 3
	orr	w20, w20, 1024
	bfi	w20, w3, 4, 1
	bls	.L291
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w7, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w7, .L305
	cbz	x22, .L292
.L305:
	cbnz	w25, .L294
.L302:
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
	cbnz	x22, .L296
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1888]
	b	.L296
.L294:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w4, 128
	mov	w2, 0
	lsr	w8, w23, 1
	mov	w3, w2
	mov	w9, -1
	ldr	w0, [x1,1940]
	cmp	w0, 25
	mov	w0, 64
	csel	w4, w0, w4, cc
.L298:
	cmp	w3, w8
	bcs	.L302
	lsr	w0, w2, 2
	ubfiz	x0, x0, 2, 30
	cbz	w7, .L299
	ldrh	w6, [x5,2]
	ldr	x10, [x1,1896]
	ldrh	w11, [x5],4
	orr	w6, w11, w6, lsl 16
	str	w6, [x10,x0]
	b	.L300
.L299:
	ldr	x6, [x1,1896]
	str	w9, [x6,x0]
.L300:
	add	w3, w3, 1
	add	w2, w2, w4
	b	.L298
.L296:
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x23, x20, 22, 5
	mov	w2, w25
	and	x22, x22, 3
	ldr	x1, [x19,1896]
	str	x1, [x19,1912]
	lsl	w1, w23, 10
	str	x0, [x19,1904]
	bl	rknand_dma_map_single
	str	w0, [x19,1920]
	ldr	x0, [x19,1912]
	lsl	w1, w23, 7
	mov	w2, w25
	bl	rknand_dma_map_single
	str	w0, [x19,1924]
	mov	w0, 1
	str	w0, [x19,1928]
	ldr	w0, [x19,1920]
	mov	w1, 16
	str	w0, [x21,20]
	ldr	w0, [x19,1924]
	str	w0, [x21,24]
	str	wzr, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 9, 5
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 448
	str	w0, [x29,88]
	cbnz	x22, .L303
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L303:
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
.L292:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L291:
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
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	mov	w2, 1
	mov	w1, 0
.L310:
	cmp	w2, w0
	bhi	.L312
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L310
.L312:
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
	ldrh	w2, [x1,1956]
	ldrh	w3, [x1,1966]
	str	w0, [x1,1952]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,1964]
	ldr	w0, [x1,1972]
	str	w2, [x1,1960]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,1968]
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
	strh	w6, [x1,1976]
	strh	w2, [x1,1978]
	strh	w0, [x1,1980]
	strh	w4, [x1,1966]
.L317:
	add	x5, x1, 1984
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L317
	ldrh	w3, [x23,20]
	ldrb	w1, [x23,15]
	cmp	w3, w1
	bcs	.L318
	uxtb	w8, w0
	mov	w3, 0
	mul	w11, w0, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x20, :lo12:.LANCHOR0
.L319:
	cmp	w3, w0
	bcs	.L321
	uxtb	w1, w3
	mov	w5, w3
	mov	w7, 0
.L322:
	cmp	w7, w2
	bcs	.L344
	add	x9, x12, 1984
	add	w13, w5, w11
	add	w14, w1, w8
	add	w7, w7, 1
	strb	w1, [x9,w5,uxtw]
	add	w1, w1, w10
	strb	w14, [x9,x13]
	add	w5, w5, w0
	uxtb	w1, w1
	b	.L322
.L344:
	add	w3, w3, 1
	b	.L319
.L321:
	add	x1, x20, :lo12:.LANCHOR0
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x1,1978]
	strh	w4, [x1,1966]
.L318:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x1,2016]
	strh	wzr, [x1,2018]
	bne	.L323
	strh	w6, [x1,2016]
.L323:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w2, 4352
	ldrb	w26, [x1,72]
	strh	w2, [x1,2020]
	cbz	w26, .L324
	mov	w2, 384
	strh	w2, [x1,2020]
.L324:
	add	x21, x20, :lo12:.LANCHOR0
	ldrh	w25, [x23,16]
	ldrh	w24, [x23,20]
	ldrh	w27, [x23,18]
	ldrh	w22, [x21,1978]
	ldrh	w19, [x21,1966]
	strh	w25, [x21,2024]
	mul	w22, w0, w22
	mul	w0, w19, w0
	strh	w0, [x21,2022]
	uxth	w22, w22
	strh	w27, [x21,2026]
	strh	w22, [x21,1956]
	mul	w0, w22, w25
	strh	w0, [x21,2028]
	mov	w0, w24
	strh	w24, [x21,2030]
	bl	Ftl_log2
	uxth	w3, w0
	lsl	w2, w24, 9
	strh	w0, [x21,2032]
	ldrh	w0, [x23,26]
	cmp	w19, 1024
	strh	w2, [x21,2034]
	ubfx	x2, x2, 9, 7
	strh	w0, [x21,2038]
	lsl	w2, w2, 1
	mul	w0, w22, w19
	strh	w2, [x21,2036]
	str	w0, [x21,1972]
	bls	.L325
	and	w0, w19, 255
	strh	w0, [x21,2018]
.L325:
	add	x2, x20, :lo12:.LANCHOR0
	mul	w0, w24, w27
	ldrh	w1, [x2,2018]
	sub	w1, w19, w1
	mul	w1, w22, w1
	mul	w1, w1, w24
	mul	w1, w1, w25
	asr	w1, w1, 11
	str	w1, [x2,2040]
	ldrh	w1, [x2,2020]
	lsl	w1, w1, 3
	sdiv	w0, w1, w0
	uxth	w0, w0
	cmp	w0, 4
	bhi	.L341
	mov	w0, 4
.L341:
	strh	w0, [x2,2044]
	cbz	w26, .L328
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 640
	strh	w1, [x0,2020]
.L328:
	add	x1, x20, :lo12:.LANCHOR0
	lsl	w19, w19, 6
	cmp	w22, 1
	ldrh	w0, [x1,2020]
	asr	w0, w0, w3
	add	w3, w3, 9
	asr	w19, w19, w3
	strh	w19, [x1,2048]
	and	w19, w19, 65535
	add	w0, w0, 2
	strh	w0, [x1,2046]
	mul	w0, w19, w22
	str	w0, [x1,2052]
	add	w19, w19, 8
	ldrh	w0, [x1,2044]
	udiv	w0, w0, w22
	add	w19, w19, w0
	bne	.L342
	add	w19, w19, 4
.L342:
	str	w19, [x1,1952]
	add	x19, x20, :lo12:.LANCHOR0
	ldrh	w0, [x19,1952]
	bl	FtlSysBlkNumInit
	str	wzr, [x19,2064]
	ldr	w0, [x19,1952]
	str	w0, [x19,2056]
	ldr	w0, [x19,1968]
	ldrh	w1, [x19,2024]
	lsl	w0, w0, 2
	ldrh	w3, [x19,2030]
	ldrh	w2, [x19,2044]
	mul	w1, w0, w1
	ldrh	w0, [x19,2032]
	ldrb	w5, [x19,196]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	strh	w1, [x19,2062]
	uxth	w0, w0
	strh	w0, [x19,2060]
	add	w1, w2, 3
	strh	w1, [x19,2044]
	ldr	w1, [x19,2052]
	add	w4, w1, 3
	str	w4, [x19,2052]
	cbz	w5, .L331
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x19,2044]
	b	.L343
.L331:
	cmp	w4, 7
	bhi	.L332
	mov	w1, 8
.L343:
	str	w1, [x19,2052]
.L332:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,1964]
	strh	wzr, [x20,2068]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L333
	mov	w0, 1
	strh	w0, [x20,2068]
.L333:
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
	ldrh	w2, [x1,2070]
	cbz	w2, .L346
	ldrh	w2, [x1,2044]
	ldr	x3, [x1,2072]
	mov	x1, 0
.L347:
	cmp	w2, w1, uxth
	bls	.L352
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L347
	mov	w0, 1
	b	.L346
.L352:
	mov	w0, 0
.L346:
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
	ldr	w3, [x2,2080]
	cbz	w3, .L354
	ldr	x5, [x2,2088]
	mov	x0, 56
	mov	x2, 0
.L355:
	cmp	w3, w2
	bls	.L364
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L355
	cmp	w4, w1
	bhi	.L355
	mov	w0, 1
	b	.L354
.L364:
	mov	w0, 0
.L354:
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR0+2064
	ldr	w0, [x0,#:lo12:.LANCHOR0+2064]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	adrp	x0, .LANCHOR0+2064
	ldr	w0, [x0,#:lo12:.LANCHOR0+2064]
	ret
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR0+2096
	ldr	w0, [x0,#:lo12:.LANCHOR0+2096]
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
	add	x19, x19, 2104
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
	ldrh	w1, [x3,2022]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	ldr	x1, [x2,2136]
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
	ldrh	w1, [x0,2022]
	ldrh	w21, [x0,1978]
	mul	w21, w1, w21
.L372:
	cmp	w20, w21
	bge	.L378
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L373
	add	w19, w19, 1
	uxth	w19, w19
.L373:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L372
.L378:
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
	ldrh	w4, [x3,1980]
	udiv	w2, w0, w4
	msub	w0, w2, w4, w0
	madd	w0, w1, w4, w0
	ldrh	w1, [x3,2022]
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
	ldrh	w1, [x2,1980]
	ldrh	w3, [x2,2022]
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
	ldrh	w3, [x2,2022]
	udiv	w1, w0, w3
	msub	w0, w1, w3, w0
	ldrh	w1, [x2,1980]
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
	bls	.L383
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L384
.L383:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L384:
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	adrp	x0, .LANCHOR0+2206
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2206]
	cmp	w0, wzr
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR0+2206
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2206]
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
	bcc	.L387
	adrp	x2, .LANCHOR0
	add	x0, x2, :lo12:.LANCHOR0
	mov	x19, x2
	ldrh	w0, [x0,2206]
	cmp	w0, 1024
	beq	.L387
	cbz	w1, .L389
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w0, [x20,-96]
	cbnz	w0, .L389
	mov	w0, w21
	bl	P2V_block_in_plane
	uxth	w22, w0
	ldr	x0, [x20,-88]
	lsl	w1, w21, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,-88]
	bl	FlashEraseBlocks
	ubfiz	x0, x22, 1, 16
	ldr	x2, [x20,-80]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x20,-72]
	add	w0, w0, 1
	str	w0, [x20,-72]
.L389:
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 2200
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w21, [x0,8]
	strh	w2, [x1,4]
.L387:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	adrp	x0, .LANCHOR2-36
	ldrh	w6, [x0,#:lo12:.LANCHOR2-36]
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x1, x1, 2200
	ldrh	w2, [x1,6]
	cbz	w2, .L396
	mov	w5, 0
	ldrh	w3, [x1,2]
	ldrh	w2, [x1,4]
	mov	w4, w5
	and	w6, w6, 31
.L398:
	cmp	w4, w6
	bge	.L406
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
	b	.L398
.L406:
	cbz	w5, .L396
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0,2202]
	strh	w2, [x0,2204]
.L396:
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
	add	x22, x21, 2200
	add	x23, x23, :lo12:.LC3
	mov	x24, x21
.L408:
	ldrh	w1, [x22,6]
	cbz	w1, .L409
	adrp	x19, .LANCHOR2
	ldrh	w2, [x22,2]
	add	x25, x19, :lo12:.LANCHOR2
	sub	w1, w1, #1
	add	x0, x22, x2, sxtw 1
	strh	w1, [x22,6]
	add	w2, w2, 1
	ldrh	w20, [x0,8]
	ldr	w1, [x25,-96]
	and	w2, w2, 1023
	strh	w2, [x22,2]
	cbnz	w1, .L410
	mov	w0, w20
	str	x1, [x29,88]
	bl	P2V_block_in_plane
	uxth	w26, w0
	ldr	x0, [x25,-88]
	lsl	w2, w20, 10
	ldr	x1, [x29,88]
	str	w2, [x0,4]
	ldrb	w0, [x21,196]
	cbz	w0, .L411
	ldr	x0, [x25,-88]
	mov	w2, 1
	bl	FlashEraseBlocks
.L411:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x19,-88]
	bl	FlashEraseBlocks
	ubfiz	x0, x26, 1, 16
	ldr	x2, [x19,-80]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x19,-72]
	add	w0, w0, 1
	str	w0, [x19,-72]
	b	.L410
.L409:
	adrp	x0, .LC2
	add	x0, x0, :lo12:.LC2
	bl	printk
.L412:
	b	.L412
.L410:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L413
	ldrh	w2, [x24,2206]
	mov	x0, x23
	mov	w1, w20
	bl	printk
	b	.L408
.L413:
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
	adrp	x0, .LANCHOR2-16
	mov	x4, -6148914691236517206
	uxth	w1, w1
	mov	w5, 65535
	ldr	x3, [x0,#:lo12:.LANCHOR2-16]
	sub	x0, x2, x3
	asr	x0, x0, 1
	madd	x0, x4, x0, x0
	mov	w4, 6
	uxth	w0, w0
.L419:
	cmp	w1, w0
	beq	.L420
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L421
	umull	x2, w0, w4
	add	x2, x3, x2
	b	.L419
.L420:
	mov	w0, 1
	b	.L418
.L421:
	mov	w0, 0
.L418:
	ret
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x9, .LANCHOR0
	uxth	w0, w0
	add	x9, x9, :lo12:.LANCHOR0
	ldrh	w1, [x9,1964]
	cmp	w1, w0
	bls	.L424
	adrp	x2, .LANCHOR2
	mov	w7, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x10, w0, w7
	mov	x3, x2
	ldr	x11, [x5,-16]
	add	x4, x11, x10
	strh	w1, [x4,2]
	strh	w1, [x11,x10]
	ldr	x1, [x5,-8]
	cbnz	x1, .L425
	str	x4, [x5,-8]
	b	.L424
.L425:
	ubfiz	x8, x0, 1, 16
	ldr	x13, [x2,#:lo12:.LANCHOR2]
	ldrh	w2, [x4,4]
	mov	x14, -6148914691236517206
	ldr	x12, [x5,-16]
	mov	w18, 65535
	ldrh	w6, [x13,x8]
	cmp	w2, wzr
	ldrh	w17, [x9,1964]
	mul	w6, w6, w2
	sub	x2, x1, x12
	asr	x2, x2, 1
	csinv	w6, w6, wzr, ne
	madd	x2, x14, x2, x2
	ldr	x14, [x5,-80]
	mov	w5, 0
	add	x9, x14, x8
	uxth	w2, w2
	mov	w8, w7
.L433:
	add	w5, w5, 1
	cmp	w0, w2
	uxth	w5, w5
	beq	.L424
	cmp	w5, w17
	bhi	.L424
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w7, [x13,x15]
	mul	w7, w7, w16
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L429
	ldrh	w15, [x14,x15]
	ldrh	w7, [x9]
	cmp	w15, w7
	bcc	.L431
	b	.L430
.L429:
	bhi	.L430
.L431:
	ldrh	w7, [x1]
	cmp	w7, w18
	bne	.L432
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x4, [x2,8]
	b	.L424
.L432:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x12, x1
	b	.L433
.L430:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,-8]
	cmp	x1, x3
	bne	.L434
	strh	w0, [x1,2]
	str	x4, [x2,-8]
	b	.L424
.L434:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-16]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L424:
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
	ldrh	w1, [x0,16]
	add	w1, w1, 1
	strh	w1, [x0,16]
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
	beq	.L439
	adrp	x2, .LANCHOR2
	mov	w6, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x8, w0, w6
	mov	x3, x2
	ldr	x9, [x5,-16]
	add	x4, x9, x8
	strh	w1, [x4,2]
	strh	w1, [x9,x8]
	ldr	x1, [x5,24]
	cbnz	x1, .L440
	str	x4, [x5,24]
	b	.L439
.L440:
	ldr	x11, [x5,-80]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,-16]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L443:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L441
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L442
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L439
.L442:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L443
.L441:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,24]
	cmp	x1, x3
	bne	.L444
	strh	w0, [x1,2]
	str	x4, [x2,24]
	b	.L439
.L444:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-16]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L439:
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
	ldrh	w1, [x0,32]
	add	w1, w1, 1
	strh	w1, [x0,32]
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
	ldr	x2, [x6,-16]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L447
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L448
	str	xzr, [x0]
	b	.L449
.L448:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L449
.L447:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L450
	cmp	w3, w0
	beq	.L449
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L449
.L450:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,-16]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L449:
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
	cbz	x2, .L452
	adrp	x3, .LANCHOR2-16
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2-16]
.L453:
	cbnz	w1, .L454
.L456:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L452
.L454:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L456
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L453
.L452:
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
	ldr	x1, [x2,-8]
	cbz	x1, .L464
	ldr	x4, [x2,-16]
	mov	w5, 6
.L460:
	cbz	w3, .L461
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L464
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L460
.L461:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L464:
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
	ldrh	w1, [x0,40]
	cmp	w1, w20
	beq	.L466
	ldrh	w1, [x0,88]
	cmp	w1, w20
	beq	.L466
	ldrh	w1, [x0,136]
	cmp	w1, w20
	beq	.L466
	mov	w3, 6
	ldr	x4, [x0,-16]
	ldr	x0, [x0,-8]
	umull	x3, w20, w3
	add	x1, x4, x3
	cmp	x1, x0
	beq	.L466
	ubfiz	x0, x20, 1, 16
	ldr	x5, [x19,#:lo12:.LANCHOR2]
	ldrh	w2, [x5,x0]
	ldrh	w0, [x1,4]
	ldrh	w1, [x1,2]
	cmp	w0, wzr
	mul	w2, w2, w0
	mov	w0, 65535
	csinv	w2, w2, wzr, ne
	cmp	w1, w0
	bne	.L468
	ldrh	w0, [x4,x3]
	cmp	w0, w1
	beq	.L466
.L468:
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
	bcs	.L466
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, w20
	sub	x0, x19, #8
	bl	List_remove_node
	ldrh	w0, [x19,16]
	sub	w0, w0, #1
	strh	w0, [x19,16]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L466:
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
.L473:
	cmp	w20, w2
	beq	.L477
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L474
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	uxth	w1, w0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L475
	adrp	x2, .LANCHOR0+2206
	adrp	x0, .LC4
	add	x0, x0, :lo12:.LC4
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2206]
	bl	printk
.L476:
	b	.L476
.L475:
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L477
.L474:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L473
.L477:
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
	adrp	x0, .LANCHOR0+2062
	mov	x4, 0
	mov	w5, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR0+2062]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	mov	x3, x0
	ldr	x1, [x1,184]
.L480:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L496
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L480
	b	.L481
.L496:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L483:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L497
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L492
	cmp	w5, w7
	bcc	.L484
.L492:
	mov	w5, w7
	mov	w6, w0
.L484:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L483
.L497:
	cmp	w0, w2
	bcc	.L481
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,192]
	mov	w0, w2
.L487:
	cmp	w3, w2
	beq	.L481
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L488
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L488:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L487
.L481:
	ret
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3,194]
	cbnz	w0, .L499
	cmp	w2, 4
	bhi	.L499
	add	w2, w2, 1
	strh	w2, [x3,194]
	b	.L498
.L499:
	add	x0, x1, :lo12:.LANCHOR2
	ldr	x6, [x1,#:lo12:.LANCHOR2]
	mov	w7, 65535
	strh	wzr, [x0,194]
	str	wzr, [x0,196]
	adrp	x0, .LANCHOR0+1964
	ldrh	w5, [x0,#:lo12:.LANCHOR0+1964]
	mov	x0, 0
.L500:
	cmp	w5, w0, uxth
	bls	.L498
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L501
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,196]
	add	w2, w4, w2
	str	w2, [x3,196]
.L501:
	add	x0, x0, 1
	b	.L500
.L498:
	ret
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	stp	x29, x30, [sp, -16]!
	uxth	w0, w0
	add	x29, sp, 0
	cbz	w1, .L505
	bl	ftl_set_blk_mode.part.9
	b	.L504
.L505:
	adrp	x1, .LANCHOR0
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L504:
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
	ldrh	w4, [x4,1956]
.L509:
	cmp	w2, w4
	bcs	.L515
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L510
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L510:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L509
.L515:
	add	x3, x3, :lo12:.LANCHOR0
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,2024]
	mov	x3, x0
.L512:
	cmp	x3, x4
	beq	.L516
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L513
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L513:
	add	x3, x3, 2
	b	.L512
.L516:
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
	ldrh	w24, [x0,1956]
.L518:
	cmp	w24, w21, uxth
	bls	.L529
	add	x0, x23, 1984
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L519
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L519:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L518
.L529:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w2, [x1,2024]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldr	w0, [x1,1948]
	cbz	w0, .L521
	adrp	x0, .LANCHOR2-80
	ldrh	w1, [x19]
	ldr	x0, [x0,#:lo12:.LANCHOR2-80]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 79
	bhi	.L521
	mov	w0, 1
	strb	w0, [x19,9]
.L521:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,72]
	cbz	w0, .L522
	mov	w0, 1
	strb	w0, [x19,9]
.L522:
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
	ldrh	w23, [x1,1956]
	mov	w19, w22
	ldrh	w24, [x1,2024]
	mov	x20, x1
.L531:
	cmp	w23, w22, uxth
	bls	.L538
	add	x0, x20, 1984
	mov	w1, w21
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L532
	add	w19, w19, w24
	uxth	w19, w19
.L532:
	add	x22, x22, 1
	b	.L531
.L538:
	cbz	w19, .L534
	mov	w0, 32768
	sdiv	w19, w0, w19
.L534:
	mov	w0, 6
	umull	x21, w21, w0
	adrp	x0, .LANCHOR2-16
	ldr	x0, [x0,#:lo12:.LANCHOR2-16]
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
	ldr	x1, [x2,24]
	mov	w0, w1
	cbz	x1, .L540
	ldr	x0, [x2,-16]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,-80]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L540:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR2
	uxth	w4, w0
	add	x5, x2, :lo12:.LANCHOR2
	ldr	x1, [x5,24]
	mov	w0, w1
	cbz	x1, .L543
	ldrh	w3, [x5,32]
	mov	w0, 7
	mov	w6, 6
	mov	w7, 65535
	mul	w3, w3, w0
	ldr	x0, [x5,-16]
	asr	w3, w3, 3
	cmp	w4, w3
	sub	x1, x1, x0
	csel	w4, w3, w4, gt
	asr	x1, x1, 1
	mov	x3, -6148914691236517206
	madd	x1, x3, x1, x1
	mov	w3, 0
	uxth	w1, w1
.L545:
	cmp	w3, w4
	beq	.L548
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L546
.L548:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,-80]
	ldrh	w0, [x0,x1]
	b	.L543
.L546:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L545
.L543:
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
	ldr	w2, [x24,2040]
	mov	x0, x20
	add	x1, x1, :lo12:.LC7
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0,#:lo12:.LANCHOR1+504]
	cmp	w0, 1
	beq	.L551
	sub	w0, w20, w22
	b	.L552
.L551:
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
	ldr	w2, [x24,2096]
	adrp	x1, .LC10
	mov	x0, x21
	add	x1, x1, :lo12:.LC10
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,196]
	adrp	x1, .LC11
	mov	x0, x21
	add	x1, x1, :lo12:.LC11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,200]
	adrp	x1, .LC12
	mov	x0, x21
	add	x1, x1, :lo12:.LC12
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,204]
	adrp	x1, .LC13
	mov	x0, x21
	add	x1, x1, :lo12:.LC13
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,208]
	adrp	x1, .LC14
	mov	x0, x21
	add	x1, x1, :lo12:.LC14
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,212]
	adrp	x1, .LC15
	mov	x0, x21
	add	x1, x1, :lo12:.LC15
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,216]
	adrp	x1, .LC16
	mov	x0, x21
	add	x1, x1, :lo12:.LC16
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,220]
	adrp	x1, .LC17
	mov	x0, x21
	add	x1, x1, :lo12:.LC17
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,224]
	adrp	x1, .LC18
	mov	x0, x21
	add	x1, x1, :lo12:.LC18
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,228]
	adrp	x1, .LC19
	mov	x0, x21
	add	x1, x1, :lo12:.LC19
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,232]
	adrp	x1, .LC20
	mov	x0, x21
	add	x1, x1, :lo12:.LC20
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,236]
	adrp	x1, .LC21
	add	x1, x1, :lo12:.LC21
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x24,2110]
	adrp	x1, .LC22
	mov	x0, x21
	add	x1, x1, :lo12:.LC22
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,32]
	adrp	x1, .LC23
	mov	x0, x21
	add	x1, x1, :lo12:.LC23
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,240]
	adrp	x1, .LC24
	mov	x0, x21
	add	x1, x1, :lo12:.LC24
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,244]
	adrp	x1, .LC25
	mov	x0, x21
	add	x1, x1, :lo12:.LC25
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,248]
	adrp	x1, .LC26
	mov	x0, x21
	add	x1, x1, :lo12:.LC26
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,-72]
	adrp	x1, .LC27
	mov	x0, x21
	add	x1, x1, :lo12:.LC27
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,252]
	adrp	x1, .LC28
	mov	x0, x21
	add	x1, x1, :lo12:.LC28
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,256]
	adrp	x1, .LC29
	mov	x0, x21
	add	x1, x1, :lo12:.LC29
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,-34]
	adrp	x1, .LC30
	mov	x0, x21
	add	x1, x1, :lo12:.LC30
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,-36]
	adrp	x1, .LC31
	mov	x0, x21
	add	x1, x1, :lo12:.LC31
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2064]
	adrp	x1, .LC32
	mov	x0, x21
	add	x1, x1, :lo12:.LC32
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2056]
	adrp	x1, .LC33
	mov	x0, x21
	add	x1, x1, :lo12:.LC33
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,1952]
	adrp	x1, .LC34
	mov	x0, x21
	add	x1, x1, :lo12:.LC34
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,2206]
	adrp	x1, .LC35
	mov	x0, x21
	add	x1, x1, :lo12:.LC35
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,1964]
	adrp	x1, .LC36
	mov	x0, x21
	add	x1, x1, :lo12:.LC36
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,260]
	adrp	x1, .LC37
	mov	x0, x21
	add	x1, x1, :lo12:.LC37
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,1968]
	adrp	x1, .LC38
	mov	x0, x21
	add	x1, x1, :lo12:.LC38
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,264]
	adrp	x1, .LC39
	mov	x0, x21
	add	x1, x1, :lo12:.LC39
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,2104]
	adrp	x1, .LC40
	mov	x0, x21
	add	x1, x1, :lo12:.LC40
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,42]
	adrp	x1, .LC41
	mov	x0, x21
	add	x1, x1, :lo12:.LC41
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,46]
	adrp	x1, .LC42
	mov	x0, x21
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,40]
	adrp	x1, .LC43
	mov	x0, x21
	add	x1, x1, :lo12:.LC43
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,48]
	adrp	x1, .LC44
	mov	x0, x21
	add	x1, x1, :lo12:.LC44
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,44]
	adrp	x1, .LC45
	mov	x0, x21
	add	x1, x1, :lo12:.LC45
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x23,40]
	adrp	x1, .LC46
	ldr	x2, [x25,#:lo12:.LANCHOR2]
	mov	x0, x21
	add	x1, x1, :lo12:.LC46
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,90]
	adrp	x1, .LC47
	mov	x0, x21
	add	x1, x1, :lo12:.LC47
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,94]
	adrp	x1, .LC48
	mov	x0, x21
	add	x1, x1, :lo12:.LC48
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,88]
	adrp	x1, .LC49
	mov	x0, x21
	add	x1, x1, :lo12:.LC49
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,96]
	adrp	x1, .LC50
	mov	x0, x21
	add	x1, x1, :lo12:.LC50
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,92]
	adrp	x1, .LC51
	mov	x0, x21
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x23,88]
	adrp	x1, .LC52
	ldr	x2, [x25,#:lo12:.LANCHOR2]
	mov	x0, x21
	add	x1, x1, :lo12:.LC52
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,138]
	adrp	x1, .LC53
	mov	x0, x21
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,142]
	adrp	x1, .LC54
	mov	x0, x21
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,136]
	adrp	x1, .LC55
	mov	x0, x21
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,144]
	adrp	x1, .LC56
	mov	x0, x21
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,140]
	adrp	x1, .LC57
	mov	x0, x21
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,282]
	adrp	x1, .LC58
	mov	x0, x21
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,286]
	adrp	x1, .LC59
	mov	x0, x21
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,280]
	adrp	x1, .LC60
	mov	x0, x21
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,288]
	adrp	x1, .LC61
	mov	x0, x21
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,284]
	adrp	x1, .LC62
	mov	x0, x21
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x23,328]
	adrp	x1, .LC63
	ldr	w2, [x24,1948]
	add	x1, x1, :lo12:.LC63
	ldr	w4, [x23,420]
	mov	x0, x21
	ldr	w5, [x23,412]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x23,416]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x23,408]
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x23,432]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,848]
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,850]
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x23,852]
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,856]
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
	ldrh	w0, [x23,32]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x23,280]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L553
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x25,#:lo12:.LANCHOR2]
	adrp	x1, .LC72
	mov	x0, x19
	add	x1, x1, :lo12:.LC72
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L553:
	mov	w0, 0
	adrp	x23, .LC73
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x23, x23, :lo12:.LC73
.L555:
	cmp	w3, w27
	beq	.L554
	add	x21, x25, :lo12:.LANCHOR2
	ubfiz	x6, x3, 1, 16
	umull	x24, w3, w26
	ldr	x4, [x25,#:lo12:.LANCHOR2]
	mov	x0, x19
	ldr	x5, [x21,-16]
	mov	w2, w20
	ldr	x7, [x21,-80]
	mov	x1, x23
	add	x5, x5, x24
	ldrh	w4, [x4,x6]
	add	w20, w20, 1
	ldrh	w6, [x7,x6]
	ldrh	w5, [x5,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,-16]
	cmp	w20, 16
	ldrh	w3, [x0,x24]
	bne	.L555
.L554:
	add	x1, x25, :lo12:.LANCHOR2
	adrp	x23, .LC74
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
	add	x23, x23, :lo12:.LC74
	ldr	x0, [x1,24]
	ldr	x3, [x1,-16]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L557:
	cmp	w3, w27
	beq	.L556
	add	x21, x25, :lo12:.LANCHOR2
	ubfiz	x5, x3, 1, 16
	umull	x24, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x4, [x21,-16]
	mov	x1, x23
	ldr	x6, [x21,-80]
	add	w20, w20, 1
	add	x4, x4, x24
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,-16]
	cmp	w20, 4
	ldrh	w3, [x0,x24]
	bne	.L557
.L556:
	sub	w0, w19, w22
.L552:
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
	ldr	w2, [x0,248]
	ldr	w1, [x0,256]
	cmp	w2, w1
	bcs	.L562
	adrp	x1, .LANCHOR0
	str	wzr, [x0,240]
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x5, [x0,-80]
	mov	x0, 0
	ldrh	w3, [x2,1964]
.L563:
	cmp	w3, w0
	bls	.L595
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x0,lsl 1]
	add	x0, x0, 1
	ldr	w2, [x4,240]
	add	w2, w6, w2
	str	w2, [x4,240]
	b	.L563
.L595:
	add	x2, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x2,240]
	ldrh	w1, [x1,2016]
	udiv	w3, w0, w3
	str	w3, [x2,248]
	ldr	w3, [x2,244]
	sub	w0, w0, w3
	udiv	w0, w0, w1
	str	w0, [x2,240]
	b	.L565
.L562:
	ldr	w1, [x0,252]
	cmp	w2, w1
	bls	.L565
	add	w1, w1, 1
	adrp	x4, .LANCHOR0
	str	w1, [x0,252]
	mov	w0, 0
.L567:
	add	x1, x4, :lo12:.LANCHOR0
	ldrh	w1, [x1,1964]
	cmp	w0, w1
	bcs	.L565
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 1, 32
	add	w0, w0, 1
	ldr	x3, [x1,-80]
	ldrh	w1, [x3,x2]
	add	w1, w1, 1
	strh	w1, [x3,x2]
	b	.L567
.L565:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w21, [x1,256]
	ldr	w24, [x1,248]
	add	w0, w21, 256
	cmp	w0, w24
	bls	.L570
	ldr	w1, [x1,252]
	add	w0, w21, 768
	cmp	w0, w1
	bls	.L570
	adrp	x0, .LANCHOR0+1948
	ldr	w0, [x0,#:lo12:.LANCHOR0+1948]
	cbz	w0, .L573
	cmp	w21, 40
	bls	.L570
.L573:
	mov	w0, 65535
	b	.L572
.L570:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,32]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L582
	cmp	w21, 40
	bhi	.L573
.L582:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x3, [x0,-8]
	cbz	x3, .L573
	adrp	x1, .LANCHOR0+1964
	mov	w23, 65535
	ldr	x7, [x0,-16]
	mov	w20, w23
	ldr	x26, [x0,-80]
	mov	x0, -6148914691236517206
	ldrh	w9, [x1,#:lo12:.LANCHOR0+1964]
	mov	w8, w23
	mov	w1, 0
	add	x0, x0, 1
	mov	w10, 6
.L575:
	ldrh	w2, [x3]
	cmp	w2, w8
	beq	.L577
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L573
	ldrh	w4, [x3,4]
	cbz	w4, .L576
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w5, w4
	and	x4, x4, 65535
	ldrh	w3, [x26,x4,lsl 1]
	cmp	w3, w21
	bls	.L581
	cmp	w3, w23
	bcs	.L576
	mov	w23, w3
	mov	w20, w5
.L576:
	umull	x3, w2, w10
	add	x3, x7, x3
	b	.L575
.L581:
	mov	w20, w5
.L577:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L573
	ubfiz	x25, x20, 1, 16
	ldrh	w22, [x26,x25]
	cmp	w22, w21
	bls	.L579
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L579
	add	x0, x19, :lo12:.LANCHOR2
	str	w23, [x0,256]
.L579:
	cmp	w22, w24
	bcs	.L573
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L573
	add	w0, w22, 256
	cmp	w0, w24
	bcc	.L580
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,252]
	cmp	w22, w0
	bcs	.L573
.L580:
	add	x21, x19, :lo12:.LANCHOR2
	ldr	x4, [x19,#:lo12:.LANCHOR2]
	adrp	x0, .LC77
	ldrh	w5, [x26,x25]
	add	x0, x0, :lo12:.LC77
	mov	w1, w20
	ldr	w3, [x21,252]
	mov	w2, w24
	ldrh	w4, [x4,x25]
	bl	printk
	mov	w0, 1
	str	w0, [x21,860]
	mov	w0, w20
.L572:
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
	beq	.L597
	adrp	x2, .LANCHOR2
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L597:
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
	str	wzr, [x0,864]
.L599:
	add	x5, x10, :lo12:.LANCHOR0
	adrp	x9, .LANCHOR0
	ldrh	w0, [x5,1956]
	cmp	w1, w0
	bcs	.L603
	add	x2, x3, :lo12:.LANCHOR2
	umull	x4, w1, w11
	ldr	x7, [x2,872]
	ldr	x8, [x2,880]
	add	x0, x7, x4
	str	w12, [x0,16]
	ldrh	w0, [x5,2034]
	mul	w0, w1, w0
	sdiv	w0, w0, w6
	add	x0, x8, x0, sxtw 2
	str	x0, [x7,x4]
	ldrh	w0, [x5,2036]
	ldr	x8, [x2,872]
	ldr	x5, [x2,888]
	mul	w0, w1, w0
	add	x7, x8, x4
	ldr	x2, [x2,896]
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
	b	.L599
.L603:
	mov	w8, 24
	mov	w5, 4
.L601:
	add	x2, x3, :lo12:.LANCHOR2
	ldr	w1, [x2,904]
	cmp	w0, w1
	bcs	.L604
	umull	x4, w0, w8
	ldr	x7, [x2,872]
	add	x6, x9, :lo12:.LANCHOR0
	ldr	x10, [x2,880]
	add	x1, x7, x4
	str	wzr, [x1,16]
	ldrh	w1, [x6,2034]
	mul	w1, w0, w1
	sdiv	w1, w1, w5
	add	x1, x10, x1, sxtw 2
	str	x1, [x7,x4]
	ldrh	w1, [x6,2036]
	ldr	x6, [x2,872]
	ldr	x2, [x2,888]
	mul	w1, w0, w1
	add	x4, x6, x4
	add	w0, w0, 1
	sdiv	w1, w1, w5
	uxth	w0, w0
	add	x1, x2, x1, sxtw 2
	str	x1, [x4,8]
	b	.L601
.L604:
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
	ldr	w8, [x2,904]
	ldr	x5, [x2,872]
.L606:
	cmp	w3, w1
	bcs	.L605
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L611:
	cmp	w2, w8
	bcs	.L608
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L607
	str	wzr, [x10,16]
	b	.L608
.L607:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L611
.L608:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L606
.L605:
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
	ldr	w5, [x2,904]
	ldr	x6, [x2,872]
	mov	w2, 0
.L613:
	cmp	w2, w1
	bcs	.L619
	mov	w3, 0
.L617:
	cmp	w3, w5
	bcs	.L615
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L614
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L615
.L614:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L617
.L615:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L613
.L619:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR2
	uxth	w0, w0
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w2, [x1,908]
	ldr	x3, [x1,912]
	mov	x1, 0
.L621:
	cmp	w2, w1, uxth
	bls	.L625
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L621
	mov	w0, 1
	b	.L622
.L625:
	mov	w0, 0
.L622:
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
	ldrh	w7, [x4,908]
	ldr	x6, [x4,912]
.L627:
	uxth	w4, w5
	cmp	w4, w7
	bcs	.L631
	add	x5, x5, 1
	add	x9, x6, x5, lsl 1
	ldrh	w9, [x9,-2]
	cmp	w9, w8
	bne	.L627
.L631:
	cmp	w4, w7
	bne	.L629
	ubfiz	x4, x4, 1, 16
	strh	w0, [x6,x4]
	add	x0, x3, :lo12:.LANCHOR2
	ldrh	w4, [x0,908]
	add	w4, w4, 1
	strh	w4, [x0,908]
.L629:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w4, [x3,920]
	ldr	x5, [x3,928]
	umull	x4, w4, w0
	add	x5, x5, x4
	str	w1, [x5,4]
	ldr	x1, [x3,928]
	add	x0, x1, x4
	str	w2, [x0,8]
	str	w19, [x1,x4]
	ldrh	w0, [x3,920]
	add	w0, w0, 1
	strh	w0, [x3,920]
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
	ldrh	w0, [x19,936]
	cmp	w0, w20
	beq	.L635
	ldrh	w0, [x19,938]
	cmp	w0, w20
	beq	.L635
	ldrh	w0, [x19,940]
	cmp	w0, w20
	beq	.L635
	ldrh	w0, [x19,942]
	cmp	w0, w20
	beq	.L635
	adrp	x0, .LC78
	mov	w1, w20
	add	x0, x0, :lo12:.LC78
	bl	printk
	ldrh	w1, [x19,936]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L637
	strh	w20, [x19,936]
	b	.L635
.L637:
	ldrh	w1, [x19,938]
	cmp	w1, w0
	bne	.L638
	strh	w20, [x19,938]
	b	.L635
.L638:
	ldrh	w1, [x19,940]
	cmp	w1, w0
	bne	.L639
	strh	w20, [x19,940]
	b	.L635
.L639:
	ldrh	w1, [x19,942]
	cmp	w1, w0
	bne	.L635
	strh	w20, [x19,942]
.L635:
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
	ldrh	w0, [x19,936]
	cmp	w0, w20
	beq	.L641
	ldrh	w0, [x19,938]
	cmp	w0, w20
	beq	.L641
	ldrh	w0, [x19,940]
	cmp	w0, w20
	beq	.L641
	ldrh	w0, [x19,942]
	cmp	w0, w20
	beq	.L641
	adrp	x0, .LC78
	mov	w1, w20
	add	x0, x0, :lo12:.LC78
	bl	printk
	ldrh	w1, [x19,936]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L642
	strh	w20, [x19,936]
	b	.L641
.L642:
	ldrh	w1, [x19,938]
	cmp	w1, w0
	bne	.L643
	strh	w20, [x19,938]
	b	.L641
.L643:
	ldrh	w1, [x19,940]
	cmp	w1, w0
	bne	.L644
	strh	w20, [x19,940]
	b	.L641
.L644:
	ldrh	w1, [x19,942]
	cmp	w1, w0
	bne	.L649
	strh	w20, [x19,942]
	b	.L641
.L649:
	mov	w21, -1
.L641:
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
	ldrh	w1, [x22,944]
	adrp	x0, .LC79
	mov	w2, w20
	add	x0, x0, :lo12:.LC79
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	adrp	x0, .LANCHOR0+1948
	ldr	w0, [x0,#:lo12:.LANCHOR0+1948]
	cbz	w0, .L651
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,-80]
	ldrh	w1, [x2,x0]
	cmp	w1, 39
	bls	.L651
	sub	w1, w1, #40
	strh	w1, [x2,x0]
.L651:
	add	x2, x19, :lo12:.LANCHOR2
	mov	x0, 0
	add	x2, x2, 952
	ldrh	w1, [x2,-8]
.L652:
	cmp	w1, w0, uxth
	bls	.L658
	add	x0, x0, 1
	add	x3, x2, x0, lsl 1
	ldrh	w3, [x3,-2]
	cmp	w3, w20
	bne	.L652
	b	.L653
.L658:
	cmp	w1, 15
	bhi	.L653
	add	x19, x19, :lo12:.LANCHOR2
	add	w0, w1, 1
	add	x19, x19, 952
	strh	w0, [x19,-8]
	strh	w20, [x19,w1,sxtw 1]
.L653:
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
	ldrh	w1, [x0,944]
	cbz	w1, .L660
	ldrh	w3, [x0,936]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L660
	ldrh	w2, [x0,986]
	cmp	w2, w1
	bcc	.L661
	strh	wzr, [x0,986]
.L661:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x19, 952
	ldrh	w1, [x19,986]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,986]
	add	w0, w0, 1
	strh	w0, [x19,986]
.L660:
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
	cbnz	w0, .L667
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
.L668:
	add	x2, x1, 200
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L668
.L674:
	adrp	x19, .LANCHOR2
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR2
	mov	w2, 1024
	add	x19, x19, 988
	uxth	w20, w20
	mov	x0, x19
	bl	ftl_memset
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
	b	.L669
.L667:
	cmp	w0, 1
	bne	.L670
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x3, :lo12:.LANCHOR0
.L673:
	cmp	x0, 3
	uxth	w1, w0
	mov	w2, w1
	bls	.L671
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L671:
	add	x1, x3, 200
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L673
	b	.L674
.L670:
	cmp	w0, 2
	bne	.L675
	adrp	x3, .LANCHOR0
	mov	w1, 65535
	mov	x0, 0
	add	x3, x3, :lo12:.LANCHOR0
.L677:
	add	x4, x3, 200
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L677
	b	.L674
.L675:
	cmp	w0, 3
	bne	.L678
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x3, :lo12:.LANCHOR0
.L681:
	cmp	x0, 5
	uxth	w1, w0
	mov	w2, w1
	bls	.L679
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L679:
	add	x1, x3, 200
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L681
	b	.L674
.L678:
	cmp	w0, 4
	bne	.L682
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
.L684:
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
	bne	.L684
	b	.L674
.L682:
	cmp	w0, 5
	bne	.L685
	adrp	x2, .LANCHOR0
	mov	x0, 0
	add	x2, x2, :lo12:.LANCHOR0
.L686:
	add	x1, x2, 200
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L686
	mov	x0, 0
.L687:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L687
	b	.L674
.L685:
	cmp	w0, 6
	bne	.L688
	adrp	x1, .LANCHOR0
	mov	x2, 0
	mov	w4, 12
	mov	w5, 10
	add	x1, x1, :lo12:.LANCHOR0
.L691:
	cmp	x2, 5
	uxth	w3, w2
	mov	w0, w3
	bls	.L689
	add	w0, w3, w3, lsl 1
	and	w3, w3, 1
	cmp	w3, wzr
	uxth	w0, w0
	csel	w3, w4, w5, ne
	sub	w0, w0, w3
	uxth	w0, w0
.L689:
	add	x3, x1, 200
	strh	w0, [x3,x2,lsl 1]
	add	x2, x2, 1
	cmp	x2, 256
	bne	.L691
	b	.L674
.L688:
	cmp	w0, 9
	bne	.L674
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x0, 200
	strh	wzr, [x0,200]
	mov	w0, 1
	strh	w0, [x1,2]
	mov	w0, 2
	strh	w0, [x1,4]
	mov	x0, 0
.L692:
	add	x2, x1, x0
	add	w3, w0, 3
	add	x0, x0, 2
	cmp	x0, 506
	strh	w3, [x2,6]
	bne	.L692
	b	.L674
.L669:
	cmp	w20, w0, uxth
	bls	.L711
	add	x2, x1, 200
	ldrh	w2, [x2,x0,lsl 1]
	add	x0, x0, 1
	strh	w2, [x19,w2,sxtw 1]
	b	.L669
.L711:
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
	strb	wzr, [x0,2012]
	adrp	x0, .LANCHOR1+482
	add	x19, x21, :lo12:.LANCHOR0
	mov	x24, 0
	add	x25, x19, 1620
	ldrh	w0, [x0,#:lo12:.LANCHOR1+482]
	strb	wzr, [x19,1845]
	bl	FlashBlockAlignInit
	mov	w1, 0
	mov	w2, 8
	add	x0, x19, 1848
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 32
	add	x0, x19, 712
	bl	ftl_memset
	add	x0, x19, 1652
	mov	w1, 0
	mov	w2, 192
	bl	ftl_memset
	ldr	x20, [x19,744]
	add	x23, x20, 1
.L714:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x25, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L713
	ldrb	w1, [x19,1845]
	add	x2, x19, 712
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x19, x1, sxtw
	strb	w0, [x19,1845]
	strb	w24, [x1,1848]
.L713:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L714
	add	x19, x21, :lo12:.LANCHOR0
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w1, [x19,1845]
	strb	w1, [x0,2012]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L715
.L719:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x20,13]
	add	x22, x22, :lo12:.LANCHOR2
	ldp	x23, x24, [sp,48]
	ldrb	w0, [x21,1845]
	ldp	x25, x26, [sp,64]
	mul	w0, w0, w1
	ldrh	w1, [x20,14]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,2014]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
.L715:
	ldr	w25, [x19,76]
	mov	x24, 0
	add	x26, x19, 1620
.L718:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x26, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L716
	ldrb	w1, [x20,13]
	add	x4, x19, 712
	ldrb	w3, [x19,1845]
	mul	w0, w1, w25
	ldrh	w1, [x20,14]
	mov	x2, x3
	and	w1, w1, 65280
	mul	w1, w0, w1
	str	w1, [x4,w3,sxtw 2]
	ldrb	w0, [x20,23]
	cbz	w0, .L717
	lsl	w1, w1, 1
	str	w1, [x4,w3,sxtw 2]
.L717:
	add	x0, x19, x3
	add	w2, w2, 1
	strb	w2, [x19,1845]
	strb	w24, [x0,1848]
.L716:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L718
	b	.L719
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
	ldr	x0, [x1,744]
	ldrb	w2, [x0,9]
	ldr	w0, [x1,76]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldrb	w0, [x1,1944]
	strb	w0, [x19,7]
	ldr	w0, [x1,2064]
	str	w0, [x19]
	ldr	x0, [x1,744]
	ldrb	w4, [x1,1845]
	ldrb	w0, [x0,9]
	strb	w0, [x19,6]
	mov	w0, 32
	strb	w0, [x19,8]
	ldr	x0, [x1,744]
	ldrb	w0, [x0,7]
	strb	w0, [x19,9]
	mov	x0, 0
	strb	wzr, [x19,10]
.L727:
	cmp	w4, w0, uxtb
	bls	.L729
	add	x2, x1, 1848
	ldrb	w3, [x19,10]
	ldrb	w2, [x0,x2]
	add	x0, x0, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L727
.L729:
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
	str	w1, [x0,2028]
	strh	wzr, [x0,2016]
	mov	w1, -1
	str	wzr, [x0,2020]
	strh	w1, [x0,936]
	str	wzr, [x0,328]
	str	wzr, [x0,232]
	str	wzr, [x0,236]
	str	wzr, [x0,220]
	str	wzr, [x0,208]
	str	wzr, [x0,204]
	str	wzr, [x0,212]
	str	wzr, [x0,216]
	str	wzr, [x0,200]
	str	wzr, [x0,240]
	str	wzr, [x0,244]
	str	wzr, [x0,-72]
	str	wzr, [x0,252]
	str	wzr, [x0,256]
	str	wzr, [x0,2024]
	str	wzr, [x0,860]
	str	wzr, [x0,2032]
	str	wzr, [x0,852]
	str	wzr, [x0,2036]
	strh	w1, [x0,938]
	strh	w1, [x0,940]
	strh	w1, [x0,942]
	mov	w1, 32
	ldrh	w2, [x3,2030]
	strh	w1, [x0,848]
	mov	w1, 128
	strh	w1, [x0,850]
	mov	w1, 1024
	strh	wzr, [x0,856]
	sdiv	w2, w1, w2
	ldrh	w1, [x3,1956]
	strh	wzr, [x0,944]
	lsl	w1, w1, 2
	strh	wzr, [x0,2040]
	strh	wzr, [x0,986]
	cmp	w2, w1
	str	w2, [x0,2044]
	bls	.L731
	str	w1, [x0,2044]
.L731:
	add	x20, x22, :lo12:.LANCHOR0
	add	x19, x21, :lo12:.LANCHOR2
	mov	w24, 56
	ldrh	w0, [x20,2028]
	str	wzr, [x20,2080]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,912]
	ldrh	w1, [x20,2028]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,1956]
	str	x0, [x19,928]
	mul	w23, w23, w24
	lsl	w25, w23, 3
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,2048]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2056]
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,2064]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,-88]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,896]
	ldr	w0, [x19,2044]
	mul	w0, w0, w24
	bl	ftl_malloc
	str	x0, [x20,2088]
	ldrh	w0, [x20,1956]
	ldrh	w23, [x20,2034]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,904]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2072]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2080]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2088]
	ldr	w0, [x19,904]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,880]
	ldr	w0, [x19,2044]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,2096]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2104]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2112]
	ldr	w1, [x19,904]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,1956]
	str	x0, [x19,872]
	ldrh	w0, [x20,2036]
	mul	w23, w0, w23
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2120]
	lsl	w0, w23, 3
	bl	ftl_malloc
	str	x0, [x19,2128]
	ldrh	w1, [x20,2036]
	ldr	w0, [x19,904]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,888]
	ldrh	w1, [x20,2036]
	ldr	w0, [x19,2044]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2136]
	ldrh	w0, [x20,1966]
	lsl	w0, w0, 1
	strh	w0, [x19,2144]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19,2152]
	ldrh	w0, [x19,2144]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19,2144]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w23, [x20,1966]
	str	x0, [x19,2160]
	add	x0, x0, 32
	lsl	w23, w23, 1
	str	x0, [x19,-80]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2168]
	mov	w0, w23
	bl	ftl_malloc
	ldr	w23, [x20,2052]
	str	x0, [x21,#:lo12:.LANCHOR2]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2176]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2184]
	ldrh	w0, [x20,1966]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x22,#:lo12:.LANCHOR0]
	ldrh	w0, [x20,2044]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x20,2072]
	ldrh	w0, [x20,2044]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,2192]
	ldrh	w0, [x20,2044]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2200]
	ldrh	w0, [x20,2046]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2208]
	ldrh	w2, [x20,2046]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w23, [x20,2060]
	lsl	w23, w23, 2
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2216]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2224]
	ldr	w0, [x20,2052]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2232]
	ldrh	w0, [x20,2062]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,184]
	ldrh	w1, [x20,2062]
	ldrh	w0, [x20,2034]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2240]
	ldrh	w1, [x20,1966]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,-16]
	ldrh	w0, [x20,2022]
	ldrh	w1, [x20,1978]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,2248]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,2136]
	add	x1, x20, 2104
	ldrh	w4, [x19,2248]
	ldrh	w5, [x20,1978]
	mov	w0, 1
	mov	w2, w4
.L732:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L832
	ldr	x3, [x20,2136]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L732
.L832:
	add	x1, x22, :lo12:.LANCHOR0
.L734:
	cmp	w0, 8
	beq	.L833
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,2136]
	b	.L734
.L833:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,2176]
	cbnz	x1, .L736
.L738:
	adrp	x0, .LC80
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC80
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L737
.L736:
	ldr	x1, [x0,2184]
	cbz	x1, .L738
	ldr	x1, [x0,2216]
	cbz	x1, .L738
	ldr	x1, [x0,2232]
	cbz	x1, .L738
	ldr	x1, [x0,184]
	cbz	x1, .L738
	ldr	x1, [x0,2240]
	cbz	x1, .L738
	ldr	x0, [x0,-16]
	cbz	x0, .L738
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,2136]
	cbz	x0, .L738
	adrp	x0, .LANCHOR2
	ldr	x0, [x0,#:lo12:.LANCHOR2]
	cbz	x0, .L738
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,912]
	cbz	x1, .L738
	ldr	x1, [x0,928]
	cbz	x1, .L738
	ldr	x1, [x0,2048]
	cbz	x1, .L738
	ldr	x1, [x0,2064]
	cbz	x1, .L738
	ldr	x1, [x0,-88]
	cbz	x1, .L738
	ldr	x1, [x0,896]
	cbz	x1, .L738
	ldr	x1, [x0,2056]
	cbz	x1, .L738
	ldr	x1, [x0,2072]
	cbz	x1, .L738
	ldr	x1, [x0,2080]
	cbz	x1, .L738
	ldr	x0, [x0,2088]
	cbz	x0, .L738
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,880]
	cbz	x1, .L738
	ldr	x1, [x0,2104]
	cbz	x1, .L738
	ldr	x1, [x0,2112]
	cbz	x1, .L738
	ldr	x1, [x0,872]
	cbz	x1, .L738
	ldr	x1, [x0,2120]
	cbz	x1, .L738
	ldr	x1, [x0,2128]
	cbz	x1, .L738
	ldr	x1, [x0,888]
	cbz	x1, .L738
	ldr	x1, [x0,-80]
	cbz	x1, .L738
	ldr	x0, [x0,2152]
	cbz	x0, .L738
	adrp	x0, .LANCHOR0+2072
	ldr	x0, [x0,#:lo12:.LANCHOR0+2072]
	cbz	x0, .L738
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,2192]
	cbz	x1, .L738
	ldr	x1, [x0,2200]
	cbz	x1, .L738
	ldr	x0, [x0,2208]
	cbz	x0, .L738
	mov	w0, 0
.L737:
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
	adrp	x0, .LANCHOR2+2248
	mov	x19, x1
	ldrh	w2, [x0,#:lo12:.LANCHOR2+2248]
	mov	x0, x1
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	x3, 0
	mov	w0, 65535
	mov	w5, 1
.L836:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L834
	ubfx	x4, x2, 5, 11
	add	x3, x3, 2
	lsl	x4, x4, 2
	lsl	w2, w5, w2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	bne	.L836
.L834:
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
	add	x0, x0, 2116
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
	strh	w0, [x1,2200]
	add	x0, x1, 2208
	strh	wzr, [x1,2202]
	strh	wzr, [x1,2204]
	strh	wzr, [x1,2206]
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
.L842:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L864
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L843:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L865
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22,x3]
	cmp	w2, w4
	bne	.L844
	cbz	w4, .L844
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L844:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L843
.L865:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L842
.L864:
	adrp	x0, .LANCHOR0+2026
	mov	w23, 0
	mov	w20, w23
	ldrh	w1, [x0,#:lo12:.LANCHOR0+2026]
	ldrh	w0, [x19]
	strh	w1, [x21,x0,lsl 1]
	ldrh	w24, [x21]
.L847:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L866
	ubfiz	x2, x20, 1, 16
	ldrh	w1, [x21,x2]
	cmp	w24, w1
	bls	.L848
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbnz	w0, .L849
	b	.L850
.L848:
	cbnz	w1, .L850
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbz	w0, .L850
.L852:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L850:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L847
.L849:
	mov	w23, w20
	mov	w24, w1
	cbz	w1, .L852
	b	.L850
.L866:
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
	ldr	w2, [x21,2052]
	ldr	x0, [x22,2184]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x21,2034]
	mov	w1, 255
	ldrh	w2, [x21,2062]
	ldr	x0, [x22,2240]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w1, 0
	mov	w5, -1
.L868:
	add	x3, x20, :lo12:.LANCHOR0
	ldrh	w0, [x3,2062]
	cmp	w0, w1
	bls	.L870
	add	x4, x19, :lo12:.LANCHOR2
	ubfiz	x0, x1, 4, 16
	ldr	x2, [x4,184]
	add	x2, x2, x0
	str	wzr, [x2,4]
	ldr	x2, [x4,184]
	strh	w5, [x2,x0]
	ldr	x2, [x4,184]
	add	x2, x2, x0
	ldrh	w0, [x3,2034]
	ldr	x3, [x4,2240]
	mul	w0, w0, w1
	add	w1, w1, 1
	sxtw	x0, w0
	uxth	w1, w1
	and	x0, x0, -4
	add	x0, x3, x0
	str	x0, [x2,8]
	b	.L868
.L870:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, -1
	add	x1, x0, 2256
	strh	w2, [x0,2258]
	strh	w2, [x0,2256]
	ldr	w2, [x3,2052]
	strh	w2, [x0,2266]
	mov	w2, -3902
	strh	w2, [x0,2260]
	ldrh	w2, [x0,2320]
	strh	w2, [x0,2264]
	ldrh	w2, [x3,2060]
	strh	w2, [x0,2262]
	ldr	x2, [x0,2176]
	str	x2, [x0,2272]
	ldr	x2, [x0,2232]
	str	x2, [x0,2280]
	ldr	x2, [x0,2184]
	str	x2, [x0,2288]
	ldr	x0, [x0,2216]
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
	strh	w0, [x19,2336]
	mov	w0, -1
	ldrh	w2, [x20,2044]
	str	w0, [x19,2348]
	ldr	x0, [x20,2072]
	lsl	w2, w2, 1
	str	xzr, [x19,2328]
	str	wzr, [x19,2340]
	str	wzr, [x19,2344]
	str	wzr, [x20,1948]
	strh	wzr, [x20,2070]
	bl	ftl_memset
	ldrh	w2, [x20,1966]
	mov	w1, 0
	ldr	x0, [x19,-80]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,1966]
	mov	w1, 0
	ldr	x0, [x19,2152]
	lsl	w2, w2, 1
	bl	ftl_memset
	sub	x0, x19, #64
	mov	w1, 0
	mov	w2, 48
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 512
	add	x0, x19, 336
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
	ldrh	w2, [x0,1966]
	mov	w22, w25
	mov	w26, 32768
	ldr	x0, [x19,-16]
	mul	w2, w2, w24
	bl	ftl_memset
	str	xzr, [x19,24]
	str	xzr, [x19,-8]
	str	xzr, [x19,8]
	strh	wzr, [x19,16]
	strh	wzr, [x19,32]
	strh	wzr, [x19,2016]
	mov	w19, w25
.L873:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,1964]
	cmp	w19, w1
	bge	.L880
	mov	w20, 0
	ldrh	w3, [x0,1956]
	ldrh	w2, [x0,2024]
	mov	w28, w20
	mov	x27, x0
.L881:
	cmp	w28, w3
	bge	.L882
	add	x0, x27, 1984
	mov	w1, w19
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L874
	add	w20, w2, w20
	sxth	w20, w20
.L874:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L881
.L882:
	cbz	w20, .L876
	sdiv	w20, w26, w20
	sxth	w20, w20
	b	.L877
.L876:
	ldr	x0, [x21,#:lo12:.LANCHOR2]
	mov	w1, -1
	strh	w1, [x0,w19,sxtw 1]
.L877:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,-16]
	smaddl	x1, w19, w24, x1
	strh	w20, [x1,4]
	ldrh	w1, [x0,40]
	cmp	w19, w1
	beq	.L878
	ldrh	w1, [x0,88]
	cmp	w19, w1
	beq	.L878
	ldrh	w0, [x0,136]
	cmp	w19, w0
	beq	.L878
	ldr	x0, [x21,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L879
	add	w4, w25, 1
	mov	w0, w19
	uxth	w25, w4
	bl	INSERT_FREE_LIST
	b	.L878
.L879:
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_DATA_LIST
.L878:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L873
.L880:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x23, x24, [sp,48]
	strh	w25, [x21,32]
	strh	w22, [x21,16]
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
	ldrh	w2, [x20,2028]
	ldr	x0, [x19,912]
	lsl	w2, w2, 1
	strh	wzr, [x19,908]
	strh	wzr, [x19,920]
	bl	ftl_memset
	ldrh	w3, [x20,2028]
	mov	w2, 12
	ldr	x0, [x19,928]
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
	add	x1, x1, 2352
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
	add	x22, x22, 1620
.L890:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x22
	bl	FlashMemCmp8
	mov	w23, w0
	cbnz	w0, .L887
	add	x0, x19, :lo12:.LANCHOR1
	ubfiz	x20, x20, 5, 32
	add	x1, x0, 512
	adds	x20, x1, x20
	beq	.L893
	ldrb	w3, [x20,22]
	mov	x2, 0
	mov	x1, x0
	b	.L892
.L887:
	add	w20, w20, 1
	add	x21, x21, 32
	cmp	w20, 81
	bne	.L890
	b	.L893
.L892:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,3104]
	cmp	w4, w3
	beq	.L891
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L892
	mov	w0, w2
.L891:
	add	x19, x19, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x19, 3104
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
	b	.L888
.L893:
	mov	w23, -1
.L888:
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
	bne	.L898
	cbz	x3, .L899
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L899:
	cbz	x20, .L897
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
	b	.L897
.L898:
	cbz	x3, .L902
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L902:
	cbz	x20, .L897
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
.L897:
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
.L914:
	cmp	w2, w3
	bls	.L916
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L914
.L916:
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
	adrp	x4, .LANCHOR4+48
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+48]
	add	x4, x4, 4096
	cbnz	w2, .L920
	add	x1, x4, x1
	b	.L922
.L920:
	add	x0, x4, x1
	mov	x1, x6
.L922:
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
	adrp	x0, .LANCHOR0+2064
	ldr	w0, [x0,#:lo12:.LANCHOR0+2064]
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
.L931:
	cmp	x19, x26
	beq	.L939
	cbnz	w20, .L932
	mov	x0, x23
	mov	x1, x27
	mov	x2, x22
	mov	w3, w19
	bl	printk
.L932:
	cmp	w25, 4
	mov	x0, x21
	bne	.L933
	ldr	w1, [x22,x19,lsl 2]
	b	.L938
.L933:
	cmp	w25, 2
	bne	.L935
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L938
.L935:
	ldrb	w1, [x22,x19]
.L938:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L936
	adrp	x1, .LC83
	mov	x0, x24
	add	x1, x1, :lo12:.LC83
	mov	w20, 0
	bl	printk
.L936:
	add	x19, x19, 1
	b	.L931
.L939:
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
	add	x1, x0, 760
	cmp	w27, 2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	strb	w27, [x0,760]
	mov	w0, -84
	strb	w4, [x1,5]
	strb	w0, [x1,4]
	mov	w0, -81
	strb	w2, [x1,6]
	strb	w0, [x1,7]
	bne	.L941
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+3249
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+3249]
	b	.L1004
.L941:
	cmp	w27, 3
	bne	.L943
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
	b	.L1044
.L943:
	cmp	w27, 4
	bne	.L944
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
.L1044:
	mov	w26, 8
	strb	w0, [x1,11]
	mov	w28, w26
	b	.L942
.L944:
	cmp	w27, 5
	bne	.L945
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w26, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L1045
.L945:
	cmp	w27, 6
	bne	.L946
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w26, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L1045
.L946:
	cmp	w27, 7
	bne	.L947
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
	b	.L942
.L947:
	cmp	w27, 8
	bne	.L1004
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
	b	.L942
.L1004:
	mov	w26, 7
.L1045:
	mov	w28, 4
.L942:
	sub	w0, w27, #1
	cmp	w0, 1
	bhi	.L1041
	adrp	x22, .LANCHOR1
	add	x25, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	add	x4, x25, 760
	add	x27, x25, 8
	add	x22, x22, 3232
.L948:
	ldrb	w0, [x25,1845]
	cmp	w0, w21
	bls	.L955
	add	x0, x25, x21, sxtw
	ldrb	w0, [x0,1848]
	mov	x23, 0
	add	x20, x4, x0, lsl 6
	lsl	x0, x0, 4
	add	x1, x27, x0
	add	x20, x20, 20
	ldr	x24, [x27,x0]
	ldrb	w0, [x1,8]
	mov	w1, 55
	add	x24, x24, x0, lsl 8
.L950:
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
	bhi	.L950
	mov	x0, 0
.L951:
	add	w2, w0, 8
	mov	x1, 0
.L952:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x22, x3
	cmp	x1, 24
	ldrb	w6, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w6, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L952
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L951
	add	w21, w21, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w21, w21
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L948
.L1041:
	sub	w0, w27, #3
	cmp	w0, 5
	bhi	.L955
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
.L956:
	ldrb	w0, [x20,1845]
	cmp	w0, w21
	bhi	.L1002
.L955:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	strb	w28, [x19,761]
	strb	w26, [x19,762]
	ldp	x27, x28, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 144
	ret
.L1002:
	add	x0, x20, x21, sxtw
	ldrb	w6, [x0,1848]
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
	bne	.L957
	mov	x0, 160
	madd	x6, x6, x0, x20
	add	x6, x6, 788
	b	.L958
.L957:
	cmp	w27, 8
	beq	.L959
	add	x6, x20, x6, lsl 6
	add	x6, x6, 780
.L958:
	mov	w0, 54
	cmp	w27, 4
	str	w0, [x24,2056]
	bne	.L960
	mov	w0, 255
	str	w0, [x24,2052]
	mov	w0, 64
	str	w0, [x24,2048]
	mov	w0, 204
	b	.L1046
.L960:
	ldr	w0, [x29,136]
	cmp	w0, 1
	bhi	.L962
	ldrb	w0, [x20,764]
	str	w0, [x24,2052]
	mov	w0, 82
	b	.L1047
.L962:
	cmp	w27, 7
	bne	.L961
	mov	w0, 174
	str	w0, [x24,2052]
	str	wzr, [x24,2048]
	mov	w0, 176
.L1046:
	str	w0, [x24,2052]
	mov	w0, 77
.L1047:
	str	w0, [x24,2048]
.L961:
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
	bne	.L963
	mov	w0, 31
	str	w0, [x24,2052]
	b	.L964
.L963:
	str	wzr, [x24,2052]
.L964:
	mov	w0, 2
	str	w0, [x24,2052]
	str	wzr, [x24,2052]
.L1003:
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
	cbnz	w9, .L1006
	cbnz	w8, .L1006
	cmp	w27, 7
	mov	w7, 32
	mov	w0, 2
	csel	w7, w0, w7, ne
	b	.L965
.L1006:
	mov	w7, 16
.L965:
	adrp	x1, .LANCHOR4
	mov	x10, 0
	add	x0, x1, :lo12:.LANCHOR4
	ldr	x0, [x0,56]
.L966:
	ldr	w11, [x24,2048]
	strb	w11, [x0,x10]
	add	x10, x10, 1
	cmp	w7, w10, uxtb
	bhi	.L966
	cbz	w8, .L967
	mov	w7, 0
.L969:
	ldrb	w10, [x0]
	cmp	w10, 50
	beq	.L968
	ldrb	w10, [x0,1]
	cmp	w10, 5
	beq	.L968
	add	w7, w7, 1
	add	x0, x0, 4
	uxtb	w7, w7
	cmp	w7, 8
	bne	.L969
	b	.L970
.L968:
	cmp	w7, 6
	bls	.L971
.L970:
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L972:
	b	.L972
.L967:
	cmp	w27, 7
	bne	.L973
	mov	w7, w8
.L975:
	ldrb	w10, [x0]
	cmp	w10, 12
	beq	.L974
	ldrb	w10, [x0,1]
	cmp	w10, 10
	beq	.L974
	add	w7, w7, 1
	add	x0, x0, 4
	uxtb	w7, w7
	cmp	w7, 8
	bne	.L975
	b	.L976
.L974:
	cmp	w7, 7
	bne	.L971
.L976:
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L977:
	b	.L977
.L973:
	cmp	w27, 6
	bne	.L971
	mov	x7, 0
.L978:
	ldrb	w10, [x0,x7]
	cmp	w10, 12
	beq	.L971
	add	x10, x0, x7
	ldrb	w10, [x10,8]
	cmp	w10, 4
	beq	.L971
	add	x7, x7, 1
	cmp	x7, 8
	bne	.L978
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L980:
	b	.L980
.L971:
	add	x0, x1, :lo12:.LANCHOR4
	ldr	x11, [x0,56]
	mov	x0, 0
.L981:
	ldr	w2, [x29,128]
	cmp	w2, w0
	ble	.L1049
	ldr	w7, [x24,2048]
	strb	w7, [x11,x0]
	add	x0, x0, 1
	b	.L981
.L1049:
	add	x0, x1, :lo12:.LANCHOR4
	mov	w12, w23
	mov	w10, 8
	ldr	x13, [x0,56]
.L984:
	mov	w0, 0
.L983:
	add	w7, w0, w12
	add	w0, w0, 1
	sbfiz	x7, x7, 1, 32
	cmp	w0, w23
	ldrh	w14, [x13,x7]
	mvn	w14, w14
	strh	w14, [x13,x7]
	bne	.L983
	ldr	w0, [x29,124]
	subs	w10, w10, #1
	add	w12, w12, w0
	bne	.L984
	mov	x10, 0
	mov	w18, 1
.L985:
	mov	w7, 0
	mov	w12, w7
.L989:
	lsl	w14, w18, w12
	mov	w16, w10
	mov	w0, 16
	mov	w15, 0
.L987:
	ldrh	w17, [x13,w16,sxtw 1]
	add	w16, w16, w23
	and	w17, w17, w14
	cmp	w17, w14
	csinc	w15, w15, w15, ne
	subs	w0, w0, #1
	bne	.L987
	cmp	w15, 8
	bls	.L988
	orr	w7, w7, w14
	uxth	w7, w7
.L988:
	add	w12, w12, 1
	cmp	w12, 16
	bne	.L989
	strh	w7, [x13,x10,lsl 1]
	add	x10, x10, 1
	cmp	w23, w10
	bgt	.L985
	add	x1, x1, :lo12:.LANCHOR4
	mov	x7, 0
	mov	w10, w7
	ldr	x1, [x1,56]
.L992:
	ldr	w12, [x1,x7]
	add	x7, x7, 4
	cmp	w12, wzr
	csinc	w10, w10, w10, ne
	cmp	x7, 32
	bne	.L992
	cmp	w10, 7
	ble	.L993
	adrp	x0, .LC85
	mov	w2, 1
	add	x0, x0, :lo12:.LC85
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L994:
	b	.L994
.L993:
	cmp	w27, 6
	mov	w7, 4
	beq	.L995
	cmp	w27, 7
	mov	w7, 10
	beq	.L995
	cmp	w8, wzr
	mov	w7, 5
	mov	w1, 8
	csel	w7, w1, w7, eq
.L995:
	mov	w10, 0
.L996:
	mov	x1, 0
.L997:
	add	w12, w0, w1
	ldrb	w13, [x11,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w13, [x6,w12,sxtw]
	bhi	.L997
	add	w10, w10, 1
	ldr	x1, [x29,112]
	cmp	w10, w26
	add	w0, w0, w7
	add	x11, x11, x1
	blt	.L996
	mov	w0, 255
	str	w0, [x24,2056]
	mov	w0, w22
	str	x8, [x29,96]
	str	x9, [x29,104]
	bl	NandcWaitFlashReady
	ldr	x9, [x29,104]
	ldr	x8, [x29,96]
	cbz	w9, .L999
	mov	w0, 54
	str	w0, [x24,2056]
	adrp	x0, .LANCHOR0+764
	mov	w1, -1
	ldrb	w0, [x0,#:lo12:.LANCHOR0+764]
	str	w0, [x24,2052]
	mov	w0, 22
	str	wzr, [x24,2048]
	str	w0, [x24,2056]
	mov	w0, w21
	bl	FlashReadCmd
	b	.L1000
.L999:
	mov	w0, 190
	cbnz	w8, .L1048
	mov	w0, 56
.L1048:
	str	w0, [x24,2056]
.L1000:
	mov	w0, w22
	add	w21, w21, 1
	bl	NandcWaitFlashReady
	uxtb	w21, w21
	b	.L956
.L959:
	mov	w0, 120
	str	w0, [x24,2056]
	str	wzr, [x24,2052]
	mov	w0, 23
	str	wzr, [x24,2052]
	mov	w1, 25
	str	wzr, [x24,2052]
	add	x6, x20, 788
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
	b	.L1003
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L1050
	sub	w2, w0, #1
	cmp	w2, 7
	bhi	.L1052
	bl	HynixGetReadRetryDefault
	b	.L1050
.L1052:
	cmp	w0, 49
	bne	.L1053
	adrp	x0, .LANCHOR0
	mov	w2, 64
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 764
	strb	w1, [x0,-4]
	mov	w1, 4
	strb	w1, [x0,-3]
	mov	w1, 15
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 408
	b	.L1068
.L1053:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L1059
	cmp	w1, 33
	bne	.L1054
.L1059:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 4
	b	.L1069
.L1054:
	cmp	w1, 67
	beq	.L1060
	cmp	w1, 34
	bne	.L1056
.L1060:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 5
.L1069:
	strb	w1, [x0,761]
	mov	w1, 7
	strb	w1, [x0,762]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 764
	add	x1, x1, 256
	mov	w2, 45
	b	.L1068
.L1056:
	cmp	w1, 68
	beq	.L1061
	cmp	w1, 35
	bne	.L1050
.L1061:
	adrp	x0, .LANCHOR0
	mov	w2, 95
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 764
	strb	w1, [x0,-4]
	mov	w1, 5
	strb	w1, [x0,-3]
	mov	w1, 17
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 304
.L1068:
	bl	ftl_memcpy
.L1050:
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
	ldr	w0, [x1,1868]
	cmp	w0, 3
	bls	.L1101
	ldr	w0, [x19,16]
	tbz	x0, 2, .L1101
	mov	x0, x19
	bl	wait_for_nandc_xfer_completed
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L1072
	adrp	x22, .LC86
	adrp	x23, .LC87
	mov	w21, 0
	add	x22, x22, :lo12:.LC86
	add	x23, x23, :lo12:.LC87
.L1073:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1081
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1868]
	cmp	w0, 5
	bhi	.L1074
.L1077:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L1073
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
	b	.L1073
.L1074:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1077
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1077
.L1081:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 1888
	ldr	w0, [x19,40]
	cbz	w0, .L1082
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
	b	.L1082
.L1072:
	adrp	x22, .LC88
	adrp	x23, .LC87
	add	x22, x22, :lo12:.LC88
	add	x23, x23, :lo12:.LC87
.L1083:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L1109
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L1083
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
	b	.L1083
.L1109:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1936]
	cbz	w0, .L1086
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L1086:
	add	x21, x20, :lo12:.LANCHOR0
	add	x21, x21, 1888
	ldr	w0, [x21,40]
	cbz	w0, .L1087
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
.L1087:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1936]
	cbz	w0, .L1082
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L1082:
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x20,1928]
	b	.L1070
.L1101:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1101
.L1070:
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
	cbnz	x0, .L1111
	cbnz	x4, .L1112
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x22, x29, 112
	bl	ftl_memset
.L1112:
	mov	w0, w25
	mov	w1, w21
	mov	w2, w24
	mov	w3, 0
	mov	x4, x26
	mov	x5, x22
	mov	w23, 0
	bl	NandcXferStart
	mov	w0, w25
	bl	NandcXferComp
	cbnz	w21, .L1113
	add	x1, x19, :lo12:.LANCHOR0
	ubfx	x2, x24, 1, 7
	mov	w3, 128
	add	x2, x22, x2, lsl 2
	ldr	w0, [x1,1940]
	cmp	w0, 25
	mov	w0, 64
	csel	w3, w0, w3, cc
	mov	w0, w21
.L1115:
	cmp	x22, x2
	add	w4, w0, w3
	beq	.L1157
	ldr	x5, [x1,1896]
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
	b	.L1115
.L1157:
	add	x0, x19, :lo12:.LANCHOR0
	lsr	w24, w24, 2
	ldr	w5, [x0,1940]
	ldr	w4, [x0,1868]
	mov	w0, 0
	mov	w23, w0
.L1117:
	cmp	w0, w24
	bcs	.L1113
	cbz	w5, .L1113
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x20,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L1141
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L1141
	cmp	w4, 5
	bls	.L1119
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
	bls	.L1120
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L1121
.L1120:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L1156
.L1119:
	cmp	w4, 3
	bls	.L1121
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
	bls	.L1122
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L1156
.L1122:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L1156:
	orr	w3, w1, w3, lsl 5
.L1121:
	cmp	w23, w3
	csel	w23, w23, w3, cs
	b	.L1118
.L1141:
	mov	w23, -1
.L1118:
	add	w0, w0, 1
	b	.L1117
.L1113:
	str	wzr, [x20,16]
	b	.L1124
.L1111:
	cmp	w21, 1
	bne	.L1155
	mov	w23, 0
	mov	w28, 2
.L1125:
	cmp	w23, w24
	bcs	.L1158
	and	w27, w23, 3
	mov	x3, x26
	cbz	x26, .L1127
	ubfiz	x0, x23, 9, 23
	add	x3, x26, x0
.L1127:
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
	bl	NandcXferComp
	b	.L1125
.L1158:
	mov	w23, 0
	b	.L1124
.L1155:
	mov	w1, 0
	mov	x4, 0
	mov	w27, 0
	mov	w0, w25
	mov	w2, 2
	mov	w3, w1
	mov	x5, x4
	mov	w23, w27
	bl	NandcXferStart
.L1130:
	cmp	w27, w24
	bcs	.L1124
	mov	w0, w25
	add	w28, w27, 2
	bl	NandcXferComp
	ldr	w0, [x20,32]
	cmp	w28, w24
	str	w0, [x29,104]
	bcs	.L1131
	mov	x4, 0
	mov	w0, w25
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L1131:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L1146
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w23, w0
	csel	w23, w23, w0, cs
	b	.L1132
.L1146:
	mov	w23, -1
.L1132:
	and	w2, w27, 3
	mov	x3, x26
	cbz	x26, .L1133
	ubfiz	x3, x27, 9, 23
	add	x3, x26, x3
.L1133:
	cmp	x22, xzr
	mov	w0, 2
	csel	w4, w0, wzr, ne
	mov	w1, 0
	mov	x0, x20
	mul	w4, w4, w27
	mov	w27, w28
	add	x4, x22, x4
	bl	NandcCopy1KB
	b	.L1130
.L1124:
	cbnz	w21, .L1136
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19,1868]
	cmp	w0, 5
	bls	.L1136
	ldr	w0, [x20]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L1136
	orr	w0, w0, 131072
	mov	w23, -1
	str	w0, [x20]
.L1136:
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
	cbnz	w19, .L1160
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,73]
	ldr	w0, [x0,76]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L1160:
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
	ldr	w0, [x19,1868]
	cmp	w0, 8
	mov	w0, 12
	csel	w22, w22, w0, cc
	cbz	w4, .L1163
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
	ldrb	w0, [x19,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,1844]
	bl	NandcSetMode
	cmn	w21, #1
	bne	.L1164
.L1173:
	mov	w21, -1
	b	.L1165
.L1164:
	adrp	x0, .LC89
	mov	w1, w24
	add	x0, x0, :lo12:.LC89
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L1166
	add	x19, x19, x23, sxtw 4
	ldr	x0, [x19,8]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L1166:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,64]
	add	w1, w1, 1
	str	w1, [x0,64]
	cmp	w1, 2047
	bls	.L1165
	mov	x27, 0
	str	wzr, [x0,64]
	mov	x28, x27
.L1163:
	mov	w19, 0
	mov	w26, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L1171:
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
	bhi	.L1167
	cmp	w0, 2
	bhi	.L1177
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L1177
	sub	w19, w22, w20
	mov	w21, w0
	mov	w26, 0
	b	.L1169
.L1167:
	cmp	w6, w20
	bcs	.L1178
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L1179
	mov	w6, w20
	b	.L1178
.L1177:
	mov	x27, 0
	mov	w19, w22
	mov	w21, w0
	mov	w26, 0
	mov	x28, x27
	b	.L1168
.L1178:
	mov	w20, 0
.L1168:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L1171
.L1169:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L1170
.L1179:
	mov	w19, w5
.L1170:
	cbz	w19, .L1172
	adrp	x0, .LC90
	mov	w1, w19
	add	x0, x0, :lo12:.LC90
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L1172:
	cbz	w26, .L1165
	adrp	x0, .LC91
	mov	w1, w23
	add	x0, x0, :lo12:.LC91
	mov	w2, w24
	bl	printk
	cbz	w25, .L1173
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L1165:
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
	bne	.L1189
	adrp	x21, .LANCHOR0
	add	x25, x21, :lo12:.LANCHOR0
	ldrb	w26, [x25,80]
	cbnz	w26, .L1190
.L1192:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1860]
	cbz	w0, .L1189
	b	.L1210
.L1190:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	strb	wzr, [x25,80]
	bl	FlashReadRawPage
	strb	w26, [x25,80]
	cmn	w0, #1
	beq	.L1192
	mov	w19, w0
	b	.L1189
.L1210:
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
	beq	.L1193
	ldrb	w0, [x21,1944]
	cmp	w19, w0, lsr 1
	bls	.L1189
.L1193:
	lsr	w0, w25, 8
	bl	NandcSetDdrPara
.L1189:
	adrp	x21, .LANCHOR4
	cmn	w19, #1
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x4, [x21,72]
	bne	.L1194
	cbz	x4, .L1194
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
	bne	.L1194
	adrp	x0, .LANCHOR0+196
	ldrb	w0, [x0,#:lo12:.LANCHOR0+196]
	cbz	w0, .L1194
	mov	w0, w20
	bl	flash_enter_slc_mode
	ldr	x4, [x21,72]
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	blr	x4
	mov	w19, w0
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1194:
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
	ldrb	w0, [x22,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,1844]
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
	beq	.L1215
	cmn	w22, #1
	bne	.L1212
.L1215:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L1212
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1860]
	b	.L1214
.L1212:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,1860]
.L1214:
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
	ldr	x1, [x0,56]
	add	x21, x22, :lo12:.LANCHOR0
	str	x1, [x0,80]
	mov	w20, 0
	str	wzr, [x0,88]
	mov	w25, 4
	mov	w0, 0
	mov	w28, -1
	add	x24, x24, 472
	add	x27, x21, 88
	ldr	w26, [x21,76]
	bl	flash_enter_slc_mode
.L1223:
	add	w0, w20, 1
	mov	x19, 0
	str	w0, [x29,104]
.L1225:
	add	x0, x29, 120
	ldrb	w0, [x19,x0]
	bl	FlashBchSel
	add	x5, x23, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w1, w20
	mov	x3, 0
	str	x5, [x29,96]
	ldr	x2, [x5,56]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1224
	ldr	x5, [x29,96]
	mov	w0, 0
	ldr	w1, [x29,104]
	mov	x3, 0
	ldr	x2, [x5,56]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1224
	add	x19, x19, 1
	cmp	x19, 4
	beq	.L1226
	b	.L1225
.L1227:
	add	x0, x2, 12
	mov	w1, 2036
	str	x2, [x29,104]
	mov	w28, -1
	bl	js_hash
	ldr	x2, [x29,104]
	ldr	w1, [x2,8]
	cmp	w1, w0
	beq	.L1237
.L1226:
	subs	w25, w25, #1
	add	w20, w20, w26
	bne	.L1223
	mov	w0, w25
.L1236:
	bl	flash_exit_slc_mode
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
.L1224:
	add	x19, x23, :lo12:.LANCHOR4
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x2, [x19,80]
	ldr	w1, [x2]
	cmp	w1, w0
	bne	.L1226
	cbnz	w28, .L1227
	add	x22, x22, :lo12:.LANCHOR0
	ldr	w0, [x22,76]
	udiv	w20, w20, w0
	mov	w0, w28
	add	w20, w20, 1
	str	w20, [x19,92]
	b	.L1236
.L1237:
	add	x1, x2, 160
	mov	x0, x24
	mov	w2, 32
	bl	ftl_memcpy
	ldr	x1, [x19,80]
	mov	w2, 32
	mov	x0, x27
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x19,80]
	mov	w2, 852
	add	x0, x21, 760
	add	x1, x1, 224
	bl	ftl_memcpy
	ldrh	w0, [x24,10]
	bl	FlashBlockAlignInit
	str	w20, [x19,88]
	ldr	w1, [x21,76]
	ldr	x2, [x19,80]
	udiv	w1, w20, w1
	ldr	w0, [x2,1076]
	add	w1, w1, 1
	strb	w0, [x21,1860]
	cmp	w1, 1
	bls	.L1229
	str	w1, [x19,92]
	b	.L1230
.L1229:
	mov	w0, 2
	str	w0, [x19,92]
.L1230:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w1, [x2,14]
	mov	w28, 0
	strb	w1, [x0,96]
	b	.L1226
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
	bls	.L1239
	ldrb	w0, [x2,1860]
	cbz	w0, .L1240
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L1240:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L1239:
	ldr	x0, [x29,120]
	mov	w21, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x28, 8, 8
	str	x0, [x29,96]
.L1241:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L1266
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w21
	ldrb	w0, [x0,120]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1242
	bl	SandiskSetRRPara
	b	.L1243
.L1242:
	bl	ToshibaSetRRPara
.L1243:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cmp	w0, 34
	bne	.L1244
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L1244
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L1244:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L1245
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
	b	.L1246
.L1245:
	ldr	w1, [x29,116]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L1246:
	cmn	w28, #1
	beq	.L1247
	add	x0, x19, :lo12:.LANCHOR0
	cmn	w25, #1
	csel	w25, w25, w28, ne
	ldrb	w0, [x0,1944]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1249
	mov	x26, 0
	mov	x27, x26
.L1247:
	add	w21, w21, 1
	b	.L1241
.L1266:
	mov	w28, w25
.L1249:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,120]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1251
	bl	SandiskSetRRPara
	b	.L1252
.L1251:
	bl	ToshibaSetRRPara
.L1252:
	ldr	x0, [x29,120]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	ldrb	w0, [x19,1944]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1253
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1253:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L1254
	mov	w0, 4
	bl	NandcSetDdrMode
.L1254:
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
.L1268:
	add	x0, x27, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L1271
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1269
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,1944]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1274
	mov	x23, 0
	mov	x24, x23
.L1269:
	add	w26, w26, 1
	b	.L1268
.L1274:
	mov	w19, w0
.L1271:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,1944]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1273
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1273:
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
	ldrb	w19, [x0,1944]
	mov	x22, x3
	ldrb	w0, [x0,196]
	cbnz	w0, .L1281
	add	w19, w19, w19, lsl 1
	ubfx	x19, x19, 2, 8
	b	.L1282
.L1281:
	mov	w0, 3
	sdiv	w19, w19, w0
.L1282:
	adrp	x27, .LC93
	mov	w25, 0
	add	x0, x27, :lo12:.LC93
	str	x0, [x29,152]
	add	x20, x20, :lo12:.LANCHOR0
	add	x23, x20, 8
	add	x23, x23, x21, sxtw 4
.L1293:
	mov	w0, w21
	mov	w24, 0
	mov	w27, -1
	bl	NandcWaitFlashReady
	ldrb	w4, [x23,8]
	mov	w9, 239
	ldr	x6, [x23]
	mov	w10, 137
	lsl	x8, x4, 8
.L1283:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	cmp	w24, w0
	bcs	.L1286
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
	beq	.L1284
	cmn	w27, #1
	csel	w27, w27, w0, ne
	cmp	w0, w19
	bcc	.L1295
	mov	x22, 0
	mov	x26, x22
.L1284:
	mov	w24, w7
	b	.L1283
.L1295:
	mov	x22, 0
	mov	w27, w0
	mov	x26, x22
.L1286:
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
	bcc	.L1288
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
.L1288:
	cmn	w27, #1
	cset	w6, eq
	cbnz	w6, .L1297
	cmp	w27, 256
	cset	w1, eq
	cbz	w1, .L1289
.L1297:
	ldr	x0, [x29,152]
	mov	w1, w24
	mov	w2, w28
	mov	w3, w24
	mov	w4, w27
	str	x6, [x29,144]
	bl	printk
	cbnz	w25, .L1291
	ldr	x6, [x29,144]
	cbz	w6, .L1302
	ldrb	w0, [x20,196]
	cbz	w0, .L1302
	mov	w0, w21
	mov	w1, 3
	mov	w25, 1
	bl	micron_auto_read_calibration_config
	b	.L1293
.L1291:
	mov	w0, w21
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
	b	.L1302
.L1289:
	cbz	w25, .L1302
	mov	w0, w21
	mov	w27, 256
	bl	micron_auto_read_calibration_config
.L1302:
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
	add	x2, x0, 760
	ldrb	w25, [x0,762]
	mov	w27, w1
	ldr	x0, [x0,744]
	add	x1, x2, x22
	mov	x23, x22
	mov	x24, x3
	ldrb	w0, [x0,19]
	ldrb	w19, [x1,12]
	sub	w0, w0, #7
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L1311
	ldrb	w19, [x1,20]
.L1311:
	mov	w0, w23
	mov	w28, 0
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	add	x5, x4, 764
.L1312:
	cmp	w28, w25
	bcs	.L1316
	add	w19, w19, 1
	ldrb	w1, [x4,761]
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
	beq	.L1314
	ldrb	w1, [x4,1944]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1321
	mov	x24, 0
	mov	x26, x24
.L1314:
	add	w28, w28, 1
	b	.L1312
.L1321:
	mov	w20, w0
.L1316:
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 760
	ldr	x1, [x0,-16]
	add	x0, x0, x22
	ldrb	w1, [x1,19]
	sub	w1, w1, #7
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L1318
	strb	w19, [x0,20]
	b	.L1319
.L1318:
	strb	w19, [x0,12]
.L1319:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1944]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1320
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1320:
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
	cbnz	w19, .L1328
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,73]
	ldr	w2, [x0,76]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L1328
	ldrb	w0, [x0,72]
	cbnz	w0, .L1329
	sub	w21, w21, #2
	b	.L1328
.L1329:
	mov	w21, 4
.L1328:
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
	ldr	x0, [x19,56]
	str	x0, [x19,80]
	ldrb	w0, [x19,98]
	bl	FlashBchSel
	ldr	x0, [x19,56]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19,80]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	mov	w2, 32
	str	w0, [x1]
	ldr	x0, [x19,80]
	ldrb	w1, [x20,1845]
	add	x0, x0, 16
	strh	w1, [x0,-4]
	ldrb	w1, [x20,73]
	strh	w1, [x0,-2]
	ldrb	w1, [x20,1860]
	str	w1, [x0,1060]
	add	x1, x20, 1620
	bl	ftl_memcpy
	ldr	x0, [x19,80]
	add	x1, x20, 1848
	mov	w2, 8
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x19,80]
	add	x1, x20, 712
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x19,80]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 472
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x19,80]
	add	x1, x20, 88
	mov	w2, 32
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x19,80]
	add	x1, x20, 760
	mov	w2, 852
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x20, [x19,80]
	mov	w1, 2036
	add	x0, x20, 12
	bl	js_hash
	str	w0, [x20,8]
	mov	w0, 1592
	str	w0, [x20,4]
	ldr	x0, [x19,104]
	str	x0, [x19,80]
	mov	w0, 0
	mov	w19, w21
	bl	flash_enter_slc_mode
.L1333:
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
	ldr	x2, [x20,56]
	mov	w0, 0
	mul	w1, w19, w1
	bl	FlashProgPage
	ldr	w1, [x22,76]
	mov	x3, 0
	ldr	x2, [x20,56]
	mov	w0, 0
	mul	w1, w19, w1
	add	w1, w1, 1
	bl	FlashProgPage
	ldr	w1, [x22,76]
	mov	w0, 0
	ldr	x2, [x20,104]
	mov	x3, 0
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1331
	ldr	x25, [x20,80]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L1331
	mov	w1, 2036
	add	x0, x25, 12
	bl	js_hash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L1331
	ldr	w0, [x22,76]
	cmp	w21, 1
	str	w26, [x20,92]
	mul	w19, w19, w0
	str	w19, [x20,88]
	beq	.L1334
	mov	w21, 1
.L1331:
	cmp	w26, 4
	mov	w19, w26
	bne	.L1333
	b	.L1332
.L1334:
	mov	w21, 2
.L1332:
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
	ldrb	w27, [x0,1944]
	ldr	w0, [x0,1856]
	cbz	w0, .L1340
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1340:
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
.L1341:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,73]
	cmp	w20, w0
	bcs	.L1345
	mov	x22, 0
.L1343:
	add	x0, x29, 120
	add	x24, x19, :lo12:.LANCHOR0
	add	x21, x28, :lo12:.LANCHOR4
	ldrb	w25, [x22,x0]
	mov	w0, w25
	bl	FlashBchSel
	ldr	w1, [x24,76]
	mov	w0, 0
	ldr	x2, [x21,56]
	mov	x3, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1342
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L1343
	b	.L1344
.L1342:
	ldr	x0, [x21,56]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L1344
	ldr	x0, [x29,104]
	mov	w1, w25
	bl	printk
	ldr	x1, [x21,56]
	mov	x0, x26
	mov	w2, 2048
	bl	ftl_memcpy
	ldr	x0, [x21,56]
	ldr	w0, [x0,512]
	strb	w0, [x24,73]
	ldr	w0, [x21,92]
	cmp	w0, w20
	bls	.L1348
	mov	w23, 0
	str	w20, [x21,92]
	bl	FlashSavePhyInfo
.L1344:
	add	w20, w20, 1
	b	.L1341
.L1348:
	mov	w23, 0
.L1345:
	mov	w0, w27
	add	x19, x19, :lo12:.LANCHOR0
	bl	FlashBchSel
	ldr	w0, [x19,1856]
	cbz	w0, .L1352
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1352:
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
	str	x0, [x22,56]
	mov	w0, 32768
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x24, .LC95
	bl	ftl_malloc
	str	x0, [x22,104]
	mov	w0, 4096
	mov	w27, 144
	mov	w28, 239
	bl	ftl_malloc
	str	x0, [x22,112]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x22,120]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x22,128]
	mov	w0, 128
	str	w0, [x20,76]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x22,98]
	mov	x0, x23
	strb	wzr, [x20,1860]
	add	x23, x20, 8
	strb	w1, [x20,73]
	strb	w1, [x22,96]
	str	wzr, [x22,92]
	str	wzr, [x22,64]
	strb	wzr, [x20,72]
	add	x20, x20, 1620
	strb	wzr, [x22,136]
	bl	NandcInit
	add	x0, x24, :lo12:.LC95
	mov	w22, 0
	str	x0, [x29,104]
	mov	x26, x20
.L1364:
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
	bne	.L1359
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
.L1359:
	mov	w0, w25
	bl	NandcFlashDeCs
	ldrb	w2, [x20]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1360
	ldr	x0, [x29,104]
	add	w1, w22, 1
	ldrb	w3, [x20,1]
	ldrb	w4, [x20,2]
	ldrb	w5, [x20,3]
	ldrb	w6, [x20,4]
	ldrb	w7, [x20,5]
	bl	printk
.L1360:
	cbnz	w22, .L1361
	ldrb	w0, [x26]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1415
	ldrb	w0, [x26,1]
	cmp	w0, 255
	beq	.L1415
.L1361:
	ldrb	w0, [x20]
	cmp	w0, 181
	bne	.L1363
	mov	w0, 44
	strb	w0, [x20]
.L1363:
	add	w22, w22, 1
	add	x23, x23, 16
	cmp	w22, 4
	add	x20, x20, 8
	bne	.L1364
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 173
	beq	.L1365
	ldr	w0, [x0,1864]
	bl	NandcSetDdrMode
.L1365:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 0
	add	x0, x20, 760
	mov	w2, 852
	bl	ftl_memset
	strb	wzr, [x20,80]
	adrp	x1, .LANCHOR1
	ldr	w4, [x20,1872]
	add	x0, x1, :lo12:.LANCHOR1
	add	x0, x0, 472
	str	x0, [x20,744]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w4, w0
	bne	.L1366
	mov	w0, 1
	str	w0, [x20,1856]
.L1366:
	add	x3, x19, :lo12:.LANCHOR0
	add	x3, x3, 1620
	ldrb	w0, [x3,1]
	cmp	w0, 218
	cset	w6, eq
	cmp	w0, 241
	cset	w5, eq
	cmp	w0, 161
	cset	w2, eq
	orr	w2, w5, w2
	orr	w2, w6, w2
	cbnz	w2, .L1367
	and	w2, w0, -3
	cmp	w2, 209
	beq	.L1367
	cmp	w0, 220
	bne	.L1368
	ldrb	w2, [x3,3]
	cmp	w2, 149
	bne	.L1368
.L1367:
	add	x2, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR4
	mov	w7, 16
	mov	w5, 1
	add	x8, x2, 1620
	strb	w7, [x2,73]
	strb	w5, [x2,72]
	strb	w7, [x3,98]
	ldrb	w7, [x2,1620]
	add	x2, x1, :lo12:.LANCHOR1
	cmp	w7, 152
	strb	w7, [x2,3265]
	strb	w0, [x2,3266]
	bne	.L1370
	ldrsb	w2, [x8,4]
	tbnz	w2, #31, .L1371
	mov	w2, 24
	strb	w2, [x3,98]
.L1370:
	cmp	w4, 2049
	beq	.L1418
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L1373
.L1418:
	add	x2, x21, :lo12:.LANCHOR4
	mov	w3, 16
	strb	w3, [x2,98]
.L1373:
	cbz	w6, .L1375
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,3278]
	mov	w2, -38
	b	.L1459
.L1375:
	cmp	w0, 220
	bne	.L1377
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3278]
	mov	w2, -36
.L1459:
	strb	w2, [x0,3266]
	b	.L1376
.L1377:
	cmp	w0, 211
	bne	.L1376
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3278]
	mov	w2, 2
	strb	w2, [x0,3277]
.L1376:
	add	x20, x1, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x20, 3136
	mov	w2, 32
	add	x0, x0, 88
	bl	ftl_memcpy
	add	x0, x20, 472
	add	x1, x20, 3264
	mov	w2, 32
	bl	ftl_memcpy
.L1368:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,72]
	cbnz	w0, .L1378
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L1380
	ldr	x0, [x20,744]
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x20,1844]
	tbnz	x1, 0, .L1380
	mov	w1, 1
	strb	w1, [x20,1860]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
	bl	NandcSetMode
.L1380:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,744]
	ldrb	w0, [x0,26]
	strb	w0, [x20,196]
	bl	FlashLoadPhyInfo
	cbz	w0, .L1378
	ldr	w0, [x20,1864]
	cbz	w0, .L1383
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L1456
.L1383:
	ldrb	w0, [x20,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
.L1456:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L1378
	mov	w0, 1
	add	x20, x19, :lo12:.LANCHOR0
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x1, [x20,744]
	adrp	x0, .LC96
	add	x0, x0, :lo12:.LC96
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L1432
	bl	FlashDieInfoInit
	ldr	x0, [x20,744]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	adrp	x1, .LANCHOR2+2014
	ldr	x0, [x20,744]
	ldrh	w1, [x1,#:lo12:.LANCHOR2+2014]
	ldrb	w2, [x0,9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L1385
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L1386
.L1385:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L1386:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1844]
	tst	w0, 6
	beq	.L1387
	bl	FlashSavePhyInfo
	add	x1, x21, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x1,88]
	bl	FlashDdrParaScan
.L1387:
	bl	FlashSavePhyInfo
.L1378:
	add	x20, x19, :lo12:.LANCHOR0
	add	x22, x21, :lo12:.LANCHOR4
	ldr	x2, [x20,744]
	str	xzr, [x22,72]
	ldrb	w0, [x2,26]
	strb	w0, [x20,196]
	ldrh	w0, [x2,16]
	ldrh	w3, [x2,10]
	ubfx	x1, x0, 7, 1
	strb	w1, [x20,80]
	ubfx	x1, x0, 3, 1
	strb	w1, [x22,137]
	ubfx	x1, x0, 4, 1
	strb	w1, [x20,1945]
	ldrb	w1, [x2,12]
	ubfx	x0, x0, 8, 3
	strb	w0, [x20,1844]
	ldrb	w0, [x2,18]
	sdiv	w1, w3, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x20,744]
	ldrh	w1, [x0,16]
	tbz	x1, 6, .L1389
	ldrb	w0, [x0,19]
	ldrb	w1, [x20,762]
	strb	w1, [x22,97]
	sub	w1, w0, #1
	ldrb	w2, [x20,761]
	uxtb	w1, w1
	strb	w0, [x20,120]
	strb	w2, [x20,121]
	cmp	w1, 7
	bhi	.L1390
	adrp	x1, HynixReadRetrial
	cmp	w0, 8
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,72]
	sub	w1, w0, #5
	cset	w2, eq
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1419
	cbz	w2, .L1391
.L1419:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 1
	str	w3, [x1,1936]
.L1391:
	cmp	w0, 7
	add	x1, x19, :lo12:.LANCHOR0
	beq	.L1417
	cbnz	w2, .L1417
	add	x1, x1, 780
	b	.L1394
.L1417:
	add	x1, x1, 788
.L1394:
	mov	x2, 0
	mov	w3, w2
.L1396:
	ldrsb	w4, [x1,x2]
	add	x2, x2, 1
	cmp	w4, wzr
	csinc	w3, w3, w3, ne
	cmp	x2, 32
	bne	.L1396
	cmp	w3, 27
	bls	.L1389
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1389
.L1390:
	sub	w1, w0, #17
	uxtb	w1, w1
	cmp	w1, 2
	bhi	.L1398
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x22,72]
	beq	.L1399
	mov	w0, 7
	b	.L1457
.L1399:
	mov	w0, 15
.L1457:
	strb	w0, [x22,97]
	b	.L1389
.L1398:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1420
	cmp	w0, 33
	bne	.L1400
.L1420:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	mov	w2, 4
	str	x1, [x0,72]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,121]
	mov	w1, 7
	strb	w1, [x0,97]
	b	.L1389
.L1400:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w2, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1421
	cbz	w2, .L1402
.L1421:
	add	x3, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x1, [x3,72]
	beq	.L1404
	cmp	w0, 68
	beq	.L1404
	mov	w0, 7
	strb	w0, [x3,97]
	b	.L1405
.L1404:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,97]
.L1405:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w2, .L1458
	mov	w1, 5
.L1458:
	strb	w1, [x0,121]
	b	.L1389
.L1402:
	cmp	w0, 49
	bne	.L1389
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,72]
.L1389:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ldr	w2, [x0,1872]
	cmp	w2, w1
	bne	.L1407
	ldrb	w1, [x0,196]
	cbz	w1, .L1407
	ldr	x0, [x0,744]
	strb	wzr, [x0,18]
.L1407:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 44
	bne	.L1408
	ldrb	w1, [x0,1860]
	cbz	w1, .L1408
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L1409
	ldrb	w0, [x0,196]
	cbnz	w0, .L1408
.L1409:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,1860]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L1408:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w0, [x1,1844]
	tst	w0, 6
	beq	.L1410
	ldrb	w1, [x1,1860]
	cbnz	w1, .L1411
	tbnz	x0, 0, .L1410
.L1411:
	mov	w0, 0
	add	x21, x21, :lo12:.LANCHOR4
	bl	flash_enter_slc_mode
	ldr	w1, [x21,88]
	mov	w0, 0
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1410:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 16
	ldr	x0, [x19,744]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2352
	bl	FlashReadIdbDataRaw
	strb	w20, [x19,73]
	mov	w0, 18928
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	ldr	x1, [x19,744]
	ldrb	w2, [x1,12]
	strh	w2, [x19,176]
	ldrh	w4, [x1,14]
	ldrb	w2, [x1,7]
	str	w2, [x19,172]
	ldr	w2, [x19,1620]
	str	w2, [x19,168]
	ldrb	w2, [x19,1845]
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
	bne	.L1413
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,190]
	strb	w20, [x19,73]
	mov	w2, 8
	strh	w4, [x19,182]
	strh	w3, [x19,188]
	strh	w2, [x19,194]
.L1413:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
	b	.L1432
.L1415:
	mov	w0, -2
	b	.L1432
.L1371:
	strb	w5, [x3,136]
	b	.L1370
.L1432:
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
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	uxth	w19, w2
	ldr	x2, [x0,744]
	ldrb	w22, [x2,19]
	ldrb	w2, [x0,196]
	cbz	w2, .L1461
	ldr	w0, [x0,1856]
	cbnz	w0, .L1460
.L1461:
	sub	w0, w22, #5
	uxtb	w0, w0
	cmp	w0, 3
	bls	.L1462
	cmp	w22, 68
	beq	.L1462
	sub	w2, w22, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L1460
.L1462:
	adrp	x20, .LANCHOR2
	mov	w25, w1
	add	x20, x20, :lo12:.LANCHOR2
	mov	w26, 65535
	add	x20, x20, 988
	adrp	x24, .LANCHOR4
.L1464:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,744]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L1460
	ldrh	w0, [x20,w19,sxtw 1]
	cmp	w0, w26
	bne	.L1460
	add	x0, x24, :lo12:.LANCHOR4
	cmp	w22, 8
	mov	w1, 0
	ldr	x0, [x0,104]
	beq	.L1474
	mov	w1, 255
.L1474:
	mov	w2, 32768
	bl	ftl_memset
	add	x0, x24, :lo12:.LANCHOR4
	add	w1, w19, w25
	add	w19, w19, 1
	ldr	x2, [x0,104]
	mov	w0, w23
	uxth	w19, w19
	mov	x3, x2
	bl	FlashProgPage
	b	.L1464
.L1460:
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
.L1476:
	cmp	w22, w24
	beq	.L1540
	ldr	w1, [x29,116]
	sub	w4, w24, w22
	mov	x0, x19
	add	x2, x29, 140
	add	x3, x29, 136
	adrp	x21, .LANCHOR0
	bl	LogAddr2PhyAddr
	ldr	x1, [x29,120]
	ldr	w0, [x29,136]
	ldrb	w1, [x1,1845]
	cmp	w0, w1
	bcc	.L1477
	mov	w0, -1
	str	w0, [x19]
	b	.L1478
.L1477:
	ldr	x1, [x29,120]
	add	x0, x1, x0, uxtw
	ldrb	w20, [x0,1848]
	mov	w23, 0
	adrp	x27, .LANCHOR4
	mov	w28, 256
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashCs
.L1479:
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
	ldrb	w0, [x0,136]
	cbz	w0, .L1480
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w26, w26, w28, ls
.L1480:
	cmn	w26, #1
	bne	.L1481
	cmp	w23, 9
	bhi	.L1481
	add	w23, w23, 1
	b	.L1479
.L1481:
	cmp	w23, wzr
	mov	w27, 256
	csel	w26, w26, w27, eq
	mov	w23, 0
.L1483:
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
	ldrb	w0, [x0,136]
	cbz	w0, .L1486
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w28, w28, w27, ls
.L1486:
	cmn	w28, #1
	bne	.L1487
	cmp	w23, 9
	bhi	.L1487
	add	w23, w23, 1
	b	.L1483
.L1487:
	cmp	w23, wzr
	mov	w0, w20
	mov	w27, 256
	csel	w28, w28, w27, eq
	bl	NandcFlashDeCs
	add	x0, x21, :lo12:.LANCHOR0
	cmp	w28, w26
	csel	w28, w28, w26, cs
	ldrb	w0, [x0,1944]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1489
	cmn	w28, #1
	csel	w28, w28, w27, eq
.L1489:
	cmp	w28, 256
	beq	.L1500
	cmn	w28, #1
	bne	.L1490
.L1500:
	str	w28, [x19]
	b	.L1492
.L1490:
	str	wzr, [x19]
.L1492:
	ldr	x0, [x19,16]
	cbz	x0, .L1493
	ldr	w1, [x0,12]
	cmn	w1, #1
	bne	.L1493
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L1493
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1493
	str	w1, [x19]
.L1493:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1478
	add	x21, x21, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	ldr	x0, [x29,104]
	ldrb	w2, [x21,1944]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L1495
	adrp	x0, .LC98
	mov	w2, 4
	add	x0, x0, :lo12:.LC98
	mov	w3, 8
	bl	rknand_print_hex
.L1495:
	ldr	x1, [x19,16]
	cbz	x1, .L1478
	mov	w2, 4
	ldr	x0, [x29,96]
	mov	w3, w2
	bl	rknand_print_hex
.L1478:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L1476
.L1540:
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
	cbz	w23, .L1581
	bl	FlashReadSlc2KPages
	b	.L1630
.L1581:
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
.L1542:
	ldr	w0, [x29,144]
	cmp	w24, w0
	bcs	.L1632
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
	ldrb	w2, [x1,1845]
	cmp	w0, w2
	bcc	.L1544
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L1545
.L1544:
	add	x0, x1, x0, uxtw
	adrp	x25, .LANCHOR4
	ldrb	w19, [x0,1848]
	add	x0, x25, :lo12:.LANCHOR4
	str	x1, [x29,152]
	ldrb	w0, [x0,137]
	cmp	w0, wzr
	mov	w0, w19
	csel	w20, w20, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x1, [x29,152]
	ldr	x0, [x1,744]
	ldrb	w0, [x0,19]
	sub	w2, w0, #1
	uxtb	w2, w2
	cmp	w2, 7
	bhi	.L1547
	sub	w0, w0, #7
	add	x1, x1, 760
	sxtw	x4, w19
	add	x1, x1, x4
	uxtb	w0, w0
	cmp	w0, 1
	ldrb	w3, [x1,12]
	bhi	.L1548
	ldrb	w3, [x1,20]
.L1548:
	add	x2, x22, :lo12:.LANCHOR0
	add	x4, x2, x4
	ldrb	w0, [x4,1616]
	cmp	w0, w3
	beq	.L1547
	ldrb	w1, [x2,761]
	mov	w0, w19
	add	x2, x2, 764
	bl	HynixSetRRPara
.L1547:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,140]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,152]
	cbz	w0, .L1549
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L1549
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1550
.L1549:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1550:
	cmp	w19, 255
	ldr	w1, [x29,172]
	bne	.L1588
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1582
.L1588:
	cbz	w20, .L1554
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x0,76]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1555
.L1554:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1555
.L1582:
	mov	w20, w0
.L1552:
	ldrb	w2, [x29,148]
	mov	w0, w19
	ldr	x3, [x21,8]
	mov	w1, 0
	ldr	x4, [x21,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1556
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0,80]
	cbz	w1, .L1556
	strb	wzr, [x0,80]
	mov	w20, 0
	b	.L1550
.L1556:
	cbz	w20, .L1557
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
.L1557:
	mov	w0, w19
	bl	NandcFlashDeCs
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w0, [x29,136]
	cmn	w28, #1
	strb	w0, [x6,80]
	bne	.L1564
	ldrb	w0, [x6,1860]
	cbnz	w0, .L1559
.L1563:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x20, [x0,72]
	cbnz	x20, .L1560
	b	.L1633
.L1559:
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
	beq	.L1562
	ldr	x6, [x29,104]
	ldrb	w0, [x6,1944]
	cmp	w28, w0, lsr 1
	bls	.L1584
.L1562:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1563
	b	.L1584
.L1560:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L1565
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,744]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L1566
	ldrb	w1, [x2,761]
	mov	w0, w19
	add	x2, x2, 764
	mov	w3, w20
	bl	HynixSetRRPara
.L1566:
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
	ldrb	w2, [x4,1944]
	bl	printk
	cmn	w28, #1
	ldr	x4, [x29,104]
	bne	.L1565
	ldrb	w0, [x4,196]
	cbz	w0, .L1565
	ldr	w0, [x29,152]
	cbz	w0, .L1567
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1568
.L1567:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1568:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	mov	w20, 0
	ldr	x3, [x21,16]
	ldr	x4, [x0,72]
	mov	w0, w19
	blr	x4
	mov	w28, w0
	b	.L1565
.L1633:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1565
.L1584:
	mov	w20, 0
.L1564:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1944]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1565
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x0, [x0,72]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1565:
	cmp	w28, 256
	beq	.L1589
	cmn	w28, #1
	bne	.L1569
.L1589:
	str	w28, [x26,x27]
	b	.L1571
.L1569:
	str	wzr, [x26,x27]
.L1571:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L1573
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x21,4]
	ldr	x0, [x29,128]
	ldrb	w2, [x2,1944]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L1573
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	bl	rknand_print_hex
.L1573:
	cbz	w20, .L1575
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1944]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L1576
	add	x25, x25, :lo12:.LANCHOR4
	ldr	x0, [x25,72]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L1576:
	add	w0, w24, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L1590
	cmn	w23, #1
	bne	.L1577
.L1590:
	str	w23, [x26,x0]
	b	.L1575
.L1577:
	str	wzr, [x26,x0]
.L1575:
	ldr	w0, [x29,152]
	add	w24, w24, w20
	cbz	w0, .L1545
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L1545
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1545:
	add	w24, w24, 1
	b	.L1542
.L1632:
	mov	w0, 0
	b	.L1630
.L1555:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L1552
	ldr	w1, [x29,172]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L1552
.L1630:
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
	add	x0, x19, 144
	mov	w28, 0
	adrp	x26, .LC101
	add	x26, x26, :lo12:.LC101
	mul	w21, w1, w21
	mov	w1, 0
	uxth	w21, w21
	bl	ftl_memset
	add	w25, w21, w27
	ldr	x0, [x19,112]
	str	x0, [x29,152]
	mov	w19, w28
	sub	w0, w21, #12
	str	xzr, [x29,144]
	uxth	w25, w25
	str	w0, [x29,124]
.L1635:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,1845]
	cmp	w1, w19
	bls	.L1645
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1636:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1638
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
	beq	.L1637
	add	x24, x23, :lo12:.LANCHOR4
	ldr	x0, [x24,112]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1637
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	add	x24, x24, x19, sxtw 1
	bl	printk
	uxth	w28, w28
	strh	w20, [x24,144]
	b	.L1638
.L1637:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1636
.L1638:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,1845]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1635
.L1645:
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
.L1647:
	cmp	w25, w23
	beq	.L1676
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,1845]
	cmp	w0, w1
	bcc	.L1648
	str	w28, [x21]
	b	.L1649
.L1648:
	add	x0, x22, x0, uxtw
	ldrb	w20, [x0,1848]
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
	tbz	x0, 0, .L1652
	str	w28, [x21]
.L1652:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1649:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1647
.L1676:
	mov	w21, 0
	cbz	w27, .L1674
	adrp	x25, .LC105
	adrp	x26, .LC104
	adrp	x27, .LC103
	add	x25, x25, :lo12:.LC105
	add	x26, x26, :lo12:.LC104
	add	x27, x27, :lo12:.LC103
.L1675:
	cmp	w21, w23
	beq	.L1674
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1655
	adrp	x0, .LC102
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC102
	bl	printk
	b	.L1656
.L1655:
	adrp	x20, .LANCHOR4
	sub	w4, w23, w21
	add	x22, x20, :lo12:.LANCHOR4
	add	x3, x29, 100
	mov	w1, w24
	add	x2, x29, 96
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,120]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,128]
	str	wzr, [x0]
	add	x0, x29, 104
	bl	memcpy
	ldr	x0, [x22,120]
	mov	w1, 1
	str	x0, [x29,112]
	mov	w2, w24
	ldr	x0, [x22,128]
	str	x0, [x29,120]
	add	x0, x29, 104
	bl	FlashReadPages
	ldr	w22, [x29,104]
	cmn	w22, #1
	bne	.L1657
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1657:
	ldr	x0, [x19,16]
	cbz	x0, .L1658
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,128]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1658
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1658:
	ldr	x0, [x19,8]
	cbz	x0, .L1656
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,120]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1656
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1656:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1675
.L1674:
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
	add	x19, x19, 160
	adrp	x22, .LANCHOR0
	ldr	x1, [x0,2072]
	add	x21, x22, :lo12:.LANCHOR0
	ldr	x24, [x0,2120]
	add	x21, x21, 2104
	str	x1, [x19,8]
	mov	w23, 0
	str	x24, [x19,16]
	mov	w25, -1
	mov	w26, 61664
.L1678:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,1978]
	cmp	w23, w1
	bcs	.L1687
	ldrh	w20, [x0,2022]
	strh	w25, [x21,12]
.L1680:
	add	x0, x22, :lo12:.LANCHOR0
	sub	w20, w20, #1
	uxth	w20, w20
	ldrh	w0, [x0,2022]
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1681
	madd	w0, w23, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1680
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1680
	strh	w20, [x21,12]
.L1681:
	add	w23, w23, 1
	add	x21, x21, 2
	b	.L1678
.L1687:
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
	bne	.L1689
	ldrh	w19, [x3,2026]
	b	.L1690
.L1689:
	ldrh	w19, [x3,2024]
.L1690:
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
	bne	.L1691
	mov	w22, 0
	mov	w25, 2
.L1692:
	cmp	w22, w19
	bgt	.L1691
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
	bne	.L1693
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1693
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1693
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1692
.L1693:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1692
.L1691:
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
	ldr	x1, [x0,2072]
	add	x19, x19, 160
	ldr	x22, [x0,2120]
	mov	w25, 61649
	str	x1, [x19,8]
	str	x22, [x19,16]
	bl	FtlBbtMemInit
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w21, [x0,2022]
	sub	w21, w21, #1
	uxth	w21, w21
.L1700:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2022]
	sub	w0, w0, #48
	cmp	w21, w0
	ble	.L1703
	lsl	w0, w21, 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1701
	ldr	w0, [x19,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
.L1701:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1702
	ldrh	w0, [x22]
	cmp	w0, w25
	bne	.L1702
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2104
	strh	w21, [x1,2104]
	ldr	w1, [x22,4]
	str	w1, [x0,8]
	ldrh	w1, [x22,8]
	strh	w1, [x0,4]
	b	.L1703
.L1702:
	sub	w21, w21, #1
	uxth	w21, w21
	b	.L1700
.L1703:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,2104]
	cmp	w1, w2
	beq	.L1705
	ldrh	w1, [x21,2108]
	cmp	w1, w2
	beq	.L1707
	add	x25, x23, :lo12:.LANCHOR4
	lsl	w1, w1, 10
	add	x0, x25, 160
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x25,160]
	cmn	w0, #1
	beq	.L1707
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1707
	ldr	w0, [x22,4]
	ldr	w1, [x21,2112]
	cmp	w0, w1
	bls	.L1707
	str	w0, [x21,2112]
	ldrh	w1, [x21,2108]
	ldrh	w0, [x22,8]
	strh	w1, [x21,2104]
	strh	w0, [x21,2108]
.L1707:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 1
	add	x21, x23, :lo12:.LANCHOR4
	mov	w26, 61649
	add	x21, x21, 160
	ldrh	w0, [x25,2104]
	bl	FtlGetLastWrittenPage
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x25,2106]
.L1709:
	tbnz	w19, #31, .L1714
	ldrh	w0, [x25,2104]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w19, w0, lsl 10
	str	w0, [x21,4]
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,2072]
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1710
	ldrh	w0, [x22]
	cmp	w0, w26
	bne	.L1710
.L1714:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,2110]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1711
	b	.L1712
.L1710:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1709
.L1711:
	ldr	w2, [x1,1952]
	cmp	w0, w2
	beq	.L1712
	ldrh	w1, [x1,1966]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1712
	cmp	w2, w1
	bcs	.L1712
	bl	FtlSysBlkNumInit
.L1712:
	add	x20, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x21, x20, 2104
	add	x23, x23, :lo12:.LANCHOR4
.L1715:
	ldrh	w0, [x20,1978]
	mov	w1, w19
	add	x19, x19, 1
	cmp	w1, w0
	bcs	.L1733
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x3, [x23,168]
	ldrh	w2, [x0,2248]
	add	x0, x21, x19, lsl 3
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1715
.L1733:
	mov	w0, 0
.L1705:
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
	strh	wzr, [x21,2320]
	mov	x24, x22
	ldr	w2, [x20,2052]
	mov	w22, 65535
	ldr	x0, [x21,2232]
	lsl	w2, w2, 2
	strh	wzr, [x20,2070]
	bl	ftl_memset
	ldr	w2, [x20,2052]
	mov	w1, 0
	ldr	x0, [x21,2176]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2044]
	mov	w1, 0
	ldr	x0, [x21,2200]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2044]
	mov	w1, 0
	ldr	x0, [x20,2072]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x21, 264
	mov	w1, 255
	mov	w2, 16
	bl	ftl_memset
	ldrh	w21, [x20,1964]
.L1735:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,1966]
	cmp	w0, w21
	bls	.L1776
	mov	x27, 0
	ldrh	w5, [x1,1956]
	ldrh	w4, [x1,2036]
	mov	w20, w27
	mov	x23, x1
.L1777:
	cmp	w5, w27, uxth
	bls	.L1810
	add	x0, x23, 1984
	mov	w1, w21
	str	x4, [x29,112]
	str	x5, [x29,120]
	ldrb	w0, [x0,x27]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x5, [x29,120]
	ldr	x4, [x29,112]
	cbnz	w0, .L1736
	add	x2, x24, :lo12:.LANCHOR2
	mov	w0, 56
	lsl	w28, w28, 10
	mov	w3, 4
	umull	x0, w20, w0
	ldr	x1, [x2,2048]
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x2,2048]
	add	x0, x1, x0
	ldr	x1, [x2,880]
	ldr	x2, [x2,888]
	str	x1, [x0,8]
	mul	w1, w20, w4
	add	w20, w20, 1
	sdiv	w1, w1, w3
	uxth	w20, w20
	add	x1, x2, x1, sxtw 2
	str	x1, [x0,16]
.L1736:
	add	x27, x27, 1
	b	.L1777
.L1810:
	cbz	w20, .L1739
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	mov	w28, 61604
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L1740:
	cmp	w20, w23, uxth
	bls	.L1739
	mov	x0, 56
	mul	x6, x23, x0
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,2048]
	add	x1, x0, x6
	ldr	w0, [x0,x6]
	ldr	w5, [x1,4]
	cmn	w0, #1
	ldr	x26, [x1,16]
	ubfx	x25, x5, 10, 16
	bne	.L1742
	mov	w7, 16
.L1741:
	add	x8, x24, :lo12:.LANCHOR2
	str	x7, [x29,104]
	str	x8, [x29,112]
	str	x6, [x29,120]
	ldr	x0, [x8,2048]
	add	x0, x0, x6
	ldr	w1, [x0,4]
	add	w1, w1, 1
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x0, [x8,2048]
	mov	w2, w1
	add	x0, x0, x6
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	x8, [x29,112]
	cmp	w0, w22
	ldr	x6, [x29,120]
	ldr	x7, [x29,104]
	ldr	x0, [x8,2048]
	bne	.L1743
	str	w27, [x0,x6]
	b	.L1742
.L1743:
	ldr	w0, [x0,x6]
	cmn	w0, #1
	bne	.L1742
	sub	w7, w7, #1
	uxth	w7, w7
	cbnz	w7, .L1741
.L1742:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
	ldr	w1, [x1,x6]
	cmn	w1, #1
	beq	.L1745
	ldr	w0, [x0,232]
	cmn	w0, #1
	beq	.L1746
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1747
.L1746:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1747
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,232]
.L1747:
	ldrh	w0, [x26]
	cmp	w0, w28
	beq	.L1749
	bhi	.L1750
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1748
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldrh	w6, [x0,2044]
	ldrh	w0, [x0,2070]
	sub	w2, w6, #1
	ldr	x7, [x7,2200]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1763
.L1750:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1752
	cmp	w0, w22
	bne	.L1748
	mov	w0, w25
	b	.L1809
.L1752:
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldr	w6, [x0,2052]
	ldrh	w0, [x7,2320]
	ldr	x7, [x7,2232]
	uxth	w2, w6
	sub	w1, w2, #1
	sub	w2, w2, w0
	sub	w2, w2, #1
	sxth	x1, w1
	sxth	w2, w2
.L1754:
	cmp	w1, w2
	ble	.L1811
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bls	.L1755
	ldr	w2, [x7]
	cbnz	w2, .L1756
	cmp	w0, w6
	beq	.L1756
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,2320]
.L1756:
	mov	w0, 0
.L1757:
	cmp	w0, w1
	beq	.L1812
	add	x6, x24, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x7, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x10, [x6,2232]
	sxth	w0, w0
	add	x11, x10, x7
	ldr	w11, [x11,4]
	str	w11, [x10,x7]
	ldr	x6, [x6,2176]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1757
.L1812:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x2, [x0,2232]
	str	w6, [x2,x8]
	ldr	x2, [x0,2176]
	strh	w25, [x2,x9,lsl 1]
	tbz	w1, #31, .L1759
	b	.L1748
.L1755:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1754
.L1811:
	tbz	w1, #31, .L1792
	b	.L1748
.L1759:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2320]
	ldr	w2, [x2,2052]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1748
.L1792:
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldr	w4, [x26,4]
	strh	w0, [x2,2320]
	ldr	x0, [x2,2232]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2176]
	b	.L1808
.L1769:
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bhi	.L1813
	sub	w1, w1, #1
	sxth	x1, w1
.L1763:
	cmp	w1, w2
	bgt	.L1769
	b	.L1768
.L1813:
	ldr	w2, [x7]
	cbnz	w2, .L1765
	cmp	w0, w6
	beq	.L1765
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	strh	w0, [x2,2070]
.L1765:
	mov	w0, 0
.L1766:
	cmp	w0, w1
	beq	.L1814
	add	x2, x24, :lo12:.LANCHOR2
	ldr	x7, [x2,2200]
	sxtw	x2, w0
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	x10, x7, x6
	add	w0, w0, 1
	sxth	w0, w0
	ldr	w10, [x10,4]
	str	w10, [x7,x6]
	add	x6, x19, :lo12:.LANCHOR0
	ldr	x6, [x6,2072]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1766
.L1814:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	ldr	x0, [x0,2200]
	str	w2, [x0,x8]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2072]
	strh	w25, [x0,x9,lsl 1]
.L1768:
	tbnz	w1, #31, .L1748
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2044]
	ldrh	w6, [x2,2070]
	sub	w0, w0, #1
	sub	w0, w0, w6
	cmp	w1, w0, sxth
	bgt	.L1748
	add	x0, x24, :lo12:.LANCHOR2
	add	w6, w6, 1
	ldr	w4, [x26,4]
	strh	w6, [x2,2070]
	ldr	x0, [x0,2200]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2072]
.L1808:
	strh	w25, [x0,x1,lsl 1]
	b	.L1748
.L1749:
	add	x0, x24, :lo12:.LANCHOR2
	add	x1, x0, 264
	ldrh	w2, [x0,264]
	cmp	w2, w22
	bne	.L1770
	strh	w25, [x0,264]
	ldr	w0, [x26,4]
	str	w0, [x1,8]
	b	.L1748
.L1770:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1771
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1771:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	add	x0, x1, 264
	ldr	w6, [x0,8]
	cmp	w6, w2
	bcs	.L1772
	ldrh	w2, [x1,264]
	strh	w2, [x0,4]
	strh	w25, [x1,264]
	ldr	w1, [x26,4]
	str	w1, [x0,8]
	b	.L1748
.L1772:
	strh	w25, [x0,4]
	b	.L1748
.L1745:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,196]
	mov	w0, w25
	cbz	w1, .L1773
.L1809:
	mov	w1, 0
.L1773:
	bl	FtlFreeSysBlkQueueIn
.L1748:
	add	x23, x23, 1
	b	.L1740
.L1739:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1735
.L1776:
	add	x4, x24, :lo12:.LANCHOR2
	ldr	x2, [x4,2176]
	ldrh	w0, [x2]
	cbz	w0, .L1778
.L1781:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,2072]
	ldrh	w0, [x2]
	cbz	w0, .L1779
	b	.L1802
.L1778:
	ldrh	w4, [x4,2320]
	cbz	w4, .L1781
	ldr	w1, [x1,2052]
.L1782:
	cmp	w0, w1
	bcs	.L1781
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1783
	mov	w7, w0
.L1784:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w1, [x1,2052]
	cmp	w0, w1
	bcs	.L1781
	add	x2, x24, :lo12:.LANCHOR2
	sxtw	x6, w0
	sub	w1, w0, w7
	lsl	x4, x6, 1
	add	w0, w0, 1
	ldr	x5, [x2,2176]
	sxtw	x1, w1
	sxth	w0, w0
	ldrh	w8, [x5,x4]
	strh	w8, [x5,x1,lsl 1]
	ldr	x5, [x2,2232]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,2176]
	strh	wzr, [x1,x4]
	b	.L1784
.L1783:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1782
.L1779:
	ldrh	w4, [x1,2070]
	cbz	w4, .L1802
	ldrh	w1, [x1,2044]
.L1787:
	cmp	w0, w1
	mov	w7, w0
	bge	.L1802
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1788
.L1789:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2,2044]
	cmp	w0, w1
	bge	.L1802
	sxtw	x6, w0
	sub	w1, w0, w7
	ldr	x5, [x2,2072]
	lsl	x4, x6, 1
	sxtw	x1, w1
	add	w0, w0, 1
	ldrh	w8, [x5,x4]
	sxth	w0, w0
	strh	w8, [x5,x1,lsl 1]
	add	x5, x24, :lo12:.LANCHOR2
	ldr	x5, [x5,2200]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,2072]
	strh	wzr, [x1,x4]
	b	.L1789
.L1788:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1787
.L1802:
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
	stp	x29, x30, [sp, -96]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x20, .LANCHOR2
	adrp	x24, .LANCHOR4
	stp	x21, x22, [sp,32]
	add	x23, x20, :lo12:.LANCHOR2
	add	x21, x24, :lo12:.LANCHOR4
	add	x21, x21, 160
	adrp	x19, .LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x0, [x23,2072]
	add	x25, x23, 264
	str	x0, [x21,8]
	ldr	x0, [x23,2120]
	str	x0, [x21,16]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,1964]
	ldr	x0, [x20,#:lo12:.LANCHOR2]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x23,264]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1816
.L1827:
	mov	w19, -1
	b	.L1817
.L1816:
	mov	w1, 1
	mov	w26, -1
	bl	FtlGetLastWrittenPage
	sxth	w22, w0
	add	w0, w0, 1
	strh	w0, [x25,2]
	ldrsh	w25, [x23,264]
.L1818:
	tbnz	w22, #31, .L1826
	orr	w0, w22, w25, lsl 10
	mov	w1, 1
	str	w0, [x21,4]
	mov	w2, w1
	ldr	x0, [x23,2072]
	add	x27, x19, :lo12:.LANCHOR0
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldrb	w0, [x27,72]
	cbz	w0, .L1819
	ldr	x0, [x21,16]
	ldr	w28, [x0,12]
	cbz	w28, .L1819
	ldr	x0, [x21,8]
	ldrh	w1, [x27,2034]
	bl	js_hash
	cmp	w28, w0
	beq	.L1819
	cbnz	w22, .L1820
	ldrh	w0, [x23,268]
	cmp	w25, w0
	beq	.L1820
	sxth	w25, w0
	ldrh	w22, [x27,2026]
	b	.L1822
.L1820:
	str	w26, [x21]
.L1819:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1822
	ldr	x0, [x23,2072]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1822
	ldr	x0, [x23,2120]
	mov	w1, 61604
	ldrh	w0, [x0]
	cmp	w0, w1
	bne	.L1822
.L1826:
	add	x21, x24, :lo12:.LANCHOR4
	add	x23, x20, :lo12:.LANCHOR2
	add	x21, x21, 160
	add	x22, x19, :lo12:.LANCHOR0
	mov	w2, 48
	sub	x0, x23, #64
	ldr	x1, [x21,8]
	bl	ftl_memcpy
	ldr	x1, [x21,8]
	ldrh	w2, [x22,1964]
	ldr	x0, [x20,#:lo12:.LANCHOR2]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x22,1964]
	ldr	x3, [x21,8]
	ldr	x0, [x19,#:lo12:.LANCHOR0]
	uxth	x1, w2
	lsr	w2, w2, 3
	add	x1, x1, 24
	add	w2, w2, 4
	lsr	x1, x1, 1
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	ldrh	w0, [x22,2068]
	cbz	w0, .L1824
	ldrh	w0, [x22,1964]
	ldr	x3, [x21,8]
	lsr	w1, w0, 3
	ldrh	w2, [x22,2060]
	add	w1, w1, w0, lsl 1
	ldr	x0, [x23,2224]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x3, x1
	bl	ftl_memcpy
	b	.L1824
.L1822:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1818
.L1824:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w2, [x0,-64]
	cmp	w2, w1
	bne	.L1827
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w3, [x0,-54]
	ldrh	w4, [x0,-56]
	strh	w4, [x0,270]
	ldrh	w2, [x1,1978]
	cmp	w3, w2
	bne	.L1827
	ldrh	w3, [x1,2024]
	add	x24, x24, :lo12:.LANCHOR4
	ldrh	w2, [x1,2030]
	strh	wzr, [x0,282]
	mul	w3, w3, w4
	str	w3, [x1,2096]
	str	w4, [x24,216]
	mul	w3, w3, w2
	str	w3, [x1,2064]
	ldrh	w2, [x1,2110]
	ldr	w3, [x1,1968]
	ldrh	w1, [x1,1956]
	sub	w2, w3, w2
	ldrh	w3, [x0,-50]
	sub	w2, w2, w4
	strh	w3, [x0,40]
	strb	wzr, [x0,286]
	udiv	w1, w2, w1
	ldrh	w2, [x0,-48]
	strh	w1, [x0,260]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strb	w2, [x0,46]
	ldrb	w2, [x0,-53]
	strb	w2, [x0,48]
	mov	w2, -1
	strh	w2, [x0,280]
	ldrh	w2, [x0,-46]
	strh	w2, [x0,88]
	ldrh	w2, [x0,-44]
	strh	w4, [x0,42]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strb	w2, [x0,94]
	ldrb	w2, [x0,-52]
	strb	w2, [x0,96]
	ldrh	w2, [x0,-42]
	strh	w2, [x0,136]
	ldrh	w2, [x0,-40]
	strh	w4, [x0,90]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strb	wzr, [x0,288]
	strb	w2, [x0,142]
	strh	w4, [x0,138]
	ldrb	w2, [x0,-51]
	strb	w2, [x0,144]
	ldr	w1, [x0,-32]
	str	w1, [x0,240]
	ldr	w2, [x0,232]
	ldr	w1, [x0,-24]
	str	wzr, [x0,220]
	str	wzr, [x0,208]
	cmp	w1, w2
	str	wzr, [x0,200]
	str	wzr, [x0,216]
	str	wzr, [x0,244]
	str	wzr, [x0,252]
	str	wzr, [x0,212]
	bls	.L1828
	str	w1, [x0,232]
.L1828:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,-28]
	ldr	w2, [x0,236]
	cmp	w1, w2
	bls	.L1829
	str	w1, [x0,236]
.L1829:
	mov	w0, 65535
	cmp	w3, w0
	beq	.L1830
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 40
	bl	make_superblock
.L1830:
	add	x1, x20, :lo12:.LANCHOR2
	add	x0, x1, 88
	ldrh	w2, [x1,88]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1831
	bl	make_superblock
.L1831:
	add	x1, x20, :lo12:.LANCHOR2
	add	x0, x1, 136
	ldrh	w2, [x1,136]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1832
	bl	make_superblock
.L1832:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x20, 280
	mov	w19, 0
	ldrh	w2, [x20,280]
	cmp	w2, w1
	beq	.L1817
	bl	make_superblock
.L1817:
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
	adrp	x0, .LC106
	add	x1, x1, 16
	add	x0, x0, :lo12:.LC106
	adrp	x21, .LANCHOR2
	ldrh	w24, [x19,2024]
	bl	printk
	ubfiz	x2, x23, 1, 16
	adrp	x0, .LC107
	ldr	x3, [x21,#:lo12:.LANCHOR2]
	add	x0, x0, :lo12:.LC107
	mov	w1, w23
	ldrh	w2, [x3,x2]
	bl	printk
	strh	w23, [x29,112]
	add	x0, x29, 112
	bl	make_superblock
	cbnz	w25, .L1852
	ldrb	w0, [x19,196]
	cbz	w0, .L1852
	mov	w0, w23
	bl	ftl_get_blk_mode
	cmp	w0, 1
	mov	w23, w0
	bne	.L1844
	ldrh	w24, [x19,2026]
	b	.L1844
.L1852:
	mov	w23, 0
.L1844:
	add	x3, x22, :lo12:.LANCHOR0
	adrp	x0, .LC108
	adrp	x25, .LC109
	add	x0, x0, :lo12:.LC108
	mov	w1, w23
	mov	w2, w24
	ldrh	w3, [x3,2024]
	mov	w20, 0
	mov	w27, 65535
	mov	w28, 56
	mov	w26, 4
	add	x25, x25, :lo12:.LC109
	bl	printk
.L1845:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w5, [x0,1956]
	ldrh	w6, [x0,2034]
	ldrh	w7, [x0,2036]
	mov	x0, 0
	mov	w19, w0
.L1846:
	cmp	w5, w0, uxth
	bls	.L1860
	add	x1, x29, 112
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w27
	beq	.L1847
	add	x3, x21, :lo12:.LANCHOR2
	orr	w2, w20, w2, lsl 10
	umull	x1, w19, w28
	ldr	x4, [x3,2048]
	add	x4, x4, x1
	str	w2, [x4,4]
	ldr	x2, [x3,2048]
	ldr	x4, [x3,880]
	add	x1, x2, x1
	ldr	x3, [x3,888]
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
.L1847:
	add	x0, x0, 1
	b	.L1846
.L1860:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, w23
	ldr	x0, [x0,2048]
	bl	FlashReadPages
	mov	x8, 0
	mov	x9, 56
.L1849:
	cmp	w19, w8, uxth
	bls	.L1861
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,112]
	mul	x2, x8, x9
	str	x9, [x29,96]
	ldr	x4, [x0,2048]
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
	b	.L1849
.L1861:
	add	w20, w20, 1
	uxth	w20, w20
	cmp	w20, w24
	bne	.L1845
.L1851:
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
	adrp	x0, .LC106
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC106
	add	x1, x1, 40
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x23, x24, [sp,64]
	stp	x21, x22, [sp,48]
	adrp	x23, .LC111
	adrp	x22, .LC110
	adrp	x24, .LC112
	add	x22, x22, :lo12:.LC110
	stp	x19, x20, [sp,32]
	add	x23, x23, :lo12:.LC111
	mov	w20, 0
	add	x24, x24, :lo12:.LC112
	stp	x25, x26, [sp,80]
	bl	printk
.L1863:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,1966]
	cmp	w0, w20
	bls	.L1872
	mov	w0, w20
	adrp	x21, .LANCHOR2
	strh	w20, [x29,80]
	bl	ftl_get_blk_mode
	mov	w3, w0
	ldr	x4, [x21,#:lo12:.LANCHOR2]
	ubfiz	x2, x20, 1, 16
	mov	w1, w20
	mov	x0, x22
	ldrh	w2, [x4,x2]
	bl	printk
	add	x0, x29, 80
	bl	make_superblock
	mov	x1, 0
	ldrh	w6, [x19,1956]
	ldrh	w7, [x19,2034]
	mov	w9, 65535
	ldrh	w8, [x19,2036]
	mov	w10, 56
	mov	w19, w1
	mov	w5, 4
.L1864:
	cmp	w6, w1, uxth
	bls	.L1873
	add	x0, x29, 80
	add	x0, x0, x1, lsl 1
	ldrh	w2, [x0,16]
	cmp	w2, w9
	beq	.L1865
	add	x3, x21, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x0, w19, w10
	ldr	x4, [x3,2048]
	add	x4, x4, x0
	str	w2, [x4,4]
	ldr	x2, [x3,2048]
	ldr	x4, [x3,880]
	add	x2, x2, x0
	ldr	x3, [x3,888]
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
.L1865:
	add	x1, x1, 1
	b	.L1864
.L1873:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x25, 0
	mov	x26, 56
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L1867:
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w19, w25, uxth
	bls	.L1874
	ldr	x4, [x0,2048]
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
	b	.L1867
.L1874:
	ldr	x0, [x0,2048]
	mov	w1, w19
	mov	w2, 1
	mov	x25, 0
	mov	x26, 56
	bl	FlashReadPages
.L1869:
	cmp	w19, w25, uxth
	bls	.L1875
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,80]
	mul	x2, x25, x26
	add	x25, x25, 1
	ldr	x4, [x0,2048]
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
	b	.L1869
.L1875:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1863
.L1872:
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
	adrp	x0, .LC113
	add	x0, x0, :lo12:.LC113
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
	adrp	x23, .LC114
	ldrh	w1, [x2,2022]
	mov	w22, 0
	ldrh	w2, [x2,2024]
	add	x19, x19, 160
	add	x23, x23, :lo12:.LC114
	bl	printk
.L1877:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2022]
	cmp	w22, w0
	bcs	.L1879
	lsl	w25, w22, 10
	mov	w21, 0
	adrp	x24, .LANCHOR2
.L1880:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w21, w0
	bcs	.L1881
	add	w0, w21, w25
	str	w0, [x19,4]
	add	x0, x24, :lo12:.LANCHOR2
	mov	w2, 0
	str	wzr, [x19]
	add	w21, w21, 1
	ldr	x1, [x0,2072]
	ldr	x0, [x0,2120]
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
	b	.L1880
.L1881:
	add	w22, w22, 1
	b	.L1877
.L1879:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC115
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC115
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
	ldr	x5, [x4,744]
	mov	w25, w1
	ldrb	w4, [x4,72]
	mov	w23, w2
	mov	w22, 0
	ldrb	w5, [x5,19]
	cbz	w4, .L1883
	bl	FlashProgSlc2KPages
	b	.L1884
.L1883:
	sub	w0, w5, #1
	mov	w27, 56
	mov	x28, 24
	str	w0, [x29,116]
.L1935:
	cmp	w22, w25
	bcs	.L1936
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
	ldrb	w4, [x2,1845]
	cmp	w0, w4
	bcc	.L1885
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1886
.L1885:
	ldrb	w1, [x2,1945]
	add	x3, x2, 1652
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x0, x1, x28, x3
	ldr	x0, [x0,8]
	cbz	x0, .L1888
	cmp	w4, 1
	bne	.L1889
	ldr	x0, [x2,128]
	bl	NandcIqrWaitFlashReady
.L1889:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1888:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	add	x0, x0, 1652
	ldr	w1, [x29,128]
	madd	x0, x2, x28, x0
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1890
	add	w1, w22, 1
	umull	x1, w1, w27
	add	x1, x19, x1
	str	x1, [x0,16]
.L1890:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, x2
	madd	x2, x2, x28, x1
	ldrb	w20, [x0,1848]
	ldrb	w0, [x1,1845]
	strb	w20, [x2,1652]
	cmp	w0, 1
	bne	.L1891
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1892
.L1891:
	mov	w0, w20
	str	x1, [x29,104]
	bl	NandcFlashCs
	ldr	x1, [x29,104]
	ldr	w0, [x29,132]
	add	x1, x1, 712
	ldr	w0, [x1,x0,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
.L1892:
	ldr	w0, [x29,116]
	cmp	w0, 7
	bhi	.L1893
	add	x2, x21, :lo12:.LANCHOR0
	add	x0, x2, x20, sxtw
	ldrb	w0, [x0,1616]
	cbz	w0, .L1893
	ldrb	w1, [x2,761]
	mov	w0, w20
	add	x2, x2, 764
	mov	w3, 0
	bl	HynixSetRRPara
.L1893:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1894
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L1894
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1895
.L1894:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1895:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldrb	w2, [x29,124]
	mov	w0, w20
	ldr	x3, [x26,8]
	mov	w1, 1
	ldr	x4, [x26,16]
	bl	NandcXferData
	cbz	w24, .L1896
	ldr	w1, [x29,128]
	mov	w0, w20
	add	x26, x21, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29,132]
	add	x0, x26, 712
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
.L1896:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1886:
	add	w22, w22, 1
	b	.L1935
.L1936:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	add	x22, x21, 1652
	mov	x24, 24
	ldr	x0, [x21,128]
	bl	NandcIqrWaitFlashReady
.L1898:
	ldrb	w0, [x21,1845]
	cmp	w0, w20
	bls	.L1937
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1899
	ldrb	w0, [x21,196]
	cbz	w0, .L1899
	mul	x0, x20, x24
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L1899:
	add	x20, x20, 1
	b	.L1898
.L1937:
	ldr	w0, [x29,120]
	cbnz	w0, .L1901
.L1909:
	mov	w0, 0
	b	.L1884
.L1901:
	adrp	x24, .LC105
	adrp	x26, .LC104
	adrp	x27, .LC103
	mov	w21, 0
	add	x24, x24, :lo12:.LC105
	add	x26, x26, :lo12:.LC104
	add	x27, x27, :lo12:.LC103
.L1902:
	cmp	w21, w25
	beq	.L1909
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1903
	adrp	x0, .LC102
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC102
	bl	printk
	b	.L1904
.L1903:
	adrp	x20, .LANCHOR4
	sub	w4, w25, w21
	add	x22, x20, :lo12:.LANCHOR4
	add	x3, x29, 132
	mov	w1, w23
	add	x2, x29, 128
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,120]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,128]
	str	wzr, [x0]
	add	x0, x29, 136
	bl	memcpy
	ldr	x0, [x22,120]
	mov	w1, 1
	str	x0, [x29,144]
	mov	w2, w23
	ldr	x0, [x22,128]
	str	x0, [x29,152]
	add	x0, x29, 136
	bl	FlashReadPages
	ldr	w22, [x29,136]
	cmn	w22, #1
	bne	.L1905
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1905:
	ldr	x0, [x19,16]
	cbz	x0, .L1906
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,128]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1906
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1906:
	ldr	x0, [x19,8]
	cbz	x0, .L1904
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,120]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1904
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1904:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1902
.L1884:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FlashTestBlk.part.17, %function
FlashTestBlk.part.17:
	stp	x29, x30, [sp, -160]!
	mov	w1, 165
	mov	w2, 32
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	uxth	w20, w0
	add	x19, x19, :lo12:.LANCHOR4
	lsl	w20, w20, 10
	ldr	x0, [x19,104]
	str	x0, [x29,48]
	add	x0, x29, 96
	str	x0, [x29,56]
	bl	ftl_memset
	ldr	x0, [x19,104]
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
	.size	FlashTestBlk.part.17, .-FlashTestBlk.part.17
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	uxth	w1, w0
	adrp	x0, .LANCHOR4+92
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x0,#:lo12:.LANCHOR4+92]
	mov	w0, 0
	cmp	w1, w2
	bcc	.L1940
	mov	w0, w1
	bl	FlashTestBlk.part.17
.L1940:
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
	adrp	x19, .LANCHOR4
	mov	w20, 0
	add	x0, x19, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR0
	ldr	x0, [x0,112]
	str	x0, [x29,136]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,180]
	ldrh	w23, [x0,182]
	ldrh	w26, [x0,76]
	mul	w23, w1, w23
	ldr	x1, [x0,744]
	ldrb	w0, [x0,72]
	uxth	w23, w23
	ldrb	w1, [x1,24]
	cmp	w0, 1
	str	w1, [x29,132]
	adrp	x0, .LC116
	ubfiz	w1, w26, 1, 15
	add	x0, x0, :lo12:.LC116
	csel	w26, w1, w26, eq
	mov	w1, 1
	bl	printk
	add	x0, x19, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w2, 4096
	ldr	x0, [x0,112]
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
.L1944:
	add	x24, x21, :lo12:.LANCHOR0
	ldrb	w0, [x24,1845]
	cmp	w0, w20
	bls	.L1995
	sxtw	x25, w20
	add	x0, x19, :lo12:.LANCHOR4
	add	x1, x0, x25, lsl 1
	ldrh	w22, [x1,144]
	cbnz	w22, .L1974
	ldrh	w2, [x24,188]
	mov	w1, w22
	ldr	x0, [x0,56]
	add	x24, x24, x25
	lsl	w2, w2, 9
	mov	w27, w22
	bl	ftl_memset
	ldrb	w28, [x24,1848]
	ldr	w0, [x29,132]
	mov	w24, w22
	and	w0, w0, 4
	uxtb	w0, w0
	str	w0, [x29,120]
.L1946:
	uxth	w0, w27
	str	w0, [x29,144]
	cmp	w0, w23
	bcs	.L1956
	mov	w0, -1
	strb	w0, [x29,166]
	strb	w0, [x29,167]
	ldr	w0, [x29,116]
	cbz	w0, .L1948
	add	x4, x21, :lo12:.LANCHOR0
	add	x2, x29, 166
	add	x0, x4, 712
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
	bne	.L1948
	ldr	w1, [x4,76]
	mov	w0, w28
	add	x2, x29, 167
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,166]
	ldrb	w1, [x29,167]
	and	w0, w1, w0
	strb	w0, [x29,166]
.L1948:
	ldr	w0, [x29,112]
	cbz	w0, .L1950
	add	x0, x21, :lo12:.LANCHOR0
	add	x2, x29, 167
	add	x0, x0, 712
	ldr	x1, [x0,32]
	ldr	w0, [x0,x25,lsl 2]
	ldrh	w1, [x1,10]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w28
	add	w1, w1, w22
	bl	FlashReadSpare
.L1950:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x29,166]
	ldr	x0, [x0,744]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1975
	cmp	w0, 1
	bne	.L1951
.L1975:
	mov	w0, 1
	cbz	w1, .L1953
	ldrb	w0, [x29,167]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1953
.L1951:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1953
	ldrb	w0, [x29,167]
	cmp	w0, 255
	cset	w0, ne
.L1953:
	ldr	w1, [x29,120]
	cbz	w1, .L1954
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 712
	ldr	w1, [x0,x25,lsl 2]
	mov	w0, w28
	add	w1, w22, w1
	bl	SandiskProgTestBadBlock
.L1954:
	cbz	w0, .L1955
	adrp	x0, .LC117
	mov	w1, w20
	mov	w2, w27
	add	x0, x0, :lo12:.LC117
	add	w24, w24, 1
	bl	printk
	add	x0, x19, :lo12:.LANCHOR4
	ldr	x1, [x29,144]
	ldrb	w3, [x29,144]
	uxth	w24, w24
	ubfx	x1, x1, 5, 11
	lsl	x1, x1, 2
	ldr	x2, [x0,56]
	mov	w0, 1
	lsl	w0, w0, w3
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29,128]
	ldrb	w0, [x0,1845]
	mul	w0, w1, w0
	cmp	w24, w0
	bgt	.L1956
.L1955:
	add	w27, w27, 1
	add	w22, w22, w26
	b	.L1946
.L1956:
	adrp	x0, .LC118
	mov	w1, w20
	mov	w2, w24
	add	x0, x0, :lo12:.LC118
	bl	printk
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,128]
	ldrb	w0, [x1,1845]
	mul	w0, w2, w0
	cmp	w24, w0
	blt	.L1958
	add	x0, x19, :lo12:.LANCHOR4
	ldrh	w2, [x1,188]
	mov	w1, 0
	lsl	w2, w2, 9
	ldr	x0, [x0,56]
	bl	ftl_memset
.L1958:
	cbnz	w20, .L1960
	add	x0, x19, :lo12:.LANCHOR4
	adrp	x27, .LC119
	mov	w22, w20
	add	x27, x27, :lo12:.LC119
	mov	w24, 1
	ldrh	w28, [x0,92]
.L1961:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,73]
	cmp	w0, w28
	bls	.L1996
	mov	w0, w28
	bl	FlashTestBlk
	cbz	w0, .L1962
	mov	w1, w28
	mov	x0, x27
	add	w22, w22, 1
	bl	printk
	add	x0, x19, :lo12:.LANCHOR4
	ubfx	x1, x28, 5, 11
	lsl	x1, x1, 2
	uxth	w22, w22
	ldr	x2, [x0,56]
	lsl	w0, w24, w28
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1962:
	add	w28, w28, 1
	uxth	w28, w28
	b	.L1961
.L1996:
	adrp	x27, .LC119
	ldr	w24, [x29,124]
	sub	w28, w23, #50
	add	x27, x27, :lo12:.LC119
.L1964:
	cmp	w24, w28
	ble	.L1997
	mov	w0, w24
	bl	FlashTestBlk
	cbz	w0, .L1965
	mov	w1, w24
	mov	x0, x27
	bl	printk
	add	x0, x19, :lo12:.LANCHOR4
	ubfx	x1, x24, 5, 11
	lsl	x1, x1, 2
	ldr	x3, [x0,56]
	mov	w0, 1
	lsl	w0, w0, w24
	ldr	w4, [x3,x1]
	orr	w0, w4, w0
	str	w0, [x3,x1]
.L1965:
	sub	w2, w24, #1
	uxth	w24, w2
	b	.L1964
.L1997:
	add	x2, x21, :lo12:.LANCHOR0
	add	x1, x19, :lo12:.LANCHOR4
	ldrb	w0, [x2,73]
	ldr	w3, [x1,92]
	sub	w0, w0, w3
	cmp	w22, w0
	bcc	.L1960
	ldrh	w2, [x2,188]
	ldr	x0, [x1,56]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L1960:
	adrp	x24, .LC120
	ldr	w28, [x29,124]
	mul	w27, w20, w23
	add	x24, x24, :lo12:.LC120
	lsl	x25, x25, 1
	mov	w22, -3872
.L1968:
	mov	w2, w28
	mov	x0, x24
	mov	w1, w20
	bl	printk
	add	x0, x19, :lo12:.LANCHOR4
	ldr	x2, [x0,56]
.L1969:
	ubfx	x0, x28, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w28
	and	w1, w0, 1
	tbz	x0, 0, .L1998
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L1969
.L1998:
	add	x0, x19, :lo12:.LANCHOR4
	add	x2, x0, x25
	strh	w28, [x2,144]
	ldr	x2, [x29,136]
	strh	w22, [x2]
	strh	w28, [x2,2]
	strh	w1, [x2,8]
	ldr	x1, [x0,56]
	ldr	x0, [x0,112]
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
	cbz	w0, .L1974
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L1968
.L1974:
	add	w20, w20, 1
	uxtb	w20, w20
	b	.L1944
.L1995:
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
	ldrb	w0, [x24,1945]
	ldr	w2, [x1,-96]
	str	w0, [x29,120]
	mov	w0, 0
	cbnz	w2, .L2000
	ldrb	w0, [x24,196]
	mov	w28, w2
	str	w0, [x29,124]
	mov	w21, w2
	str	w26, [x1,2036]
	mov	w19, w2
	mov	w27, 56
.L2001:
	ldrh	w0, [x24,1956]
	cmp	w0, w28
	bls	.L2045
	add	x1, x20, :lo12:.LANCHOR2
	umull	x0, w28, w27
	ldr	x1, [x1,-88]
	str	wzr, [x1,x0]
	add	x0, x24, 1984
	mov	w1, w26
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	uxth	w23, w0
	mov	w1, w23
	cbz	w25, .L2002
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L2003
.L2002:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2004
	add	x3, x20, :lo12:.LANCHOR2
	lsl	w23, w23, 10
	umull	x0, w21, w27
	mov	w2, 4
	ldr	x1, [x3,-88]
	add	x1, x1, x0
	str	w23, [x1,4]
	ldr	x1, [x3,-88]
	ldr	x3, [x3,2128]
	add	x1, x1, x0
	ldrh	w0, [x24,2036]
	mul	w0, w21, w0
	add	w21, w21, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L2003
.L2004:
	add	w19, w19, 1
	uxth	w19, w19
.L2003:
	add	w2, w28, 1
	uxth	w28, w2
	b	.L2001
.L2045:
	cbz	w21, .L2024
	ldr	w0, [x29,124]
	mov	w2, w21
	strb	wzr, [x24,1945]
	mov	x27, 56
	cmp	w0, wzr
	add	x0, x20, :lo12:.LANCHOR2
	cset	w23, ne
	mov	w1, w23
	ldr	x0, [x0,-88]
	bl	FlashEraseBlocks
	ldrb	w0, [x29,120]
	strb	w0, [x24,1945]
	mov	x24, 0
.L2007:
	cmp	w21, w24, uxth
	bls	.L2046
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x24, x27
	ldr	x1, [x1,-88]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L2008
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L2008:
	add	x24, x24, 1
	b	.L2007
.L2046:
	cbnz	w25, .L2010
	mov	w0, 1
	uxth	w23, w23
	mov	w28, 6
	str	w0, [x29,124]
	b	.L2011
.L2010:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,2026]
	ldrb	w0, [x0,196]
	str	w1, [x29,124]
	cbnz	w0, .L2027
	uxtw	x0, w1
	mov	w23, 1
	lsr	w28, w0, 2
	b	.L2011
.L2027:
	mov	w23, 1
	mov	w28, w23
.L2011:
	mov	w24, 0
	add	x22, x22, :lo12:.LANCHOR0
.L2020:
	mov	w27, 0
	mov	w21, w27
.L2012:
	ldrh	w0, [x22,1956]
	cmp	w0, w27
	bls	.L2047
	add	x1, x20, :lo12:.LANCHOR2
	mov	w0, 56
	umull	x0, w27, w0
	ldr	x1, [x1,-88]
	str	wzr, [x1,x0]
	add	x0, x22, 1984
	mov	w1, w26
	ldrb	w0, [x0,w27,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w25, .L2013
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L2014
.L2013:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2014
	add	x2, x20, :lo12:.LANCHOR2
	mov	w0, 56
	ldr	w3, [x29,112]
	umull	x0, w21, w0
	ldr	x1, [x2,-88]
	add	w3, w24, w3, lsl 10
	add	x1, x1, x0
	str	w3, [x1,4]
	mov	w3, 4
	ldr	x1, [x2,-88]
	add	x1, x1, x0
	ldr	x0, [x2,2104]
	ldr	x2, [x2,2112]
	str	x0, [x1,8]
	ldrh	w0, [x22,2036]
	mul	w0, w21, w0
	add	w21, w21, 1
	sdiv	w0, w0, w3
	uxth	w21, w21
	add	x0, x2, x0, sxtw 2
	str	x0, [x1,16]
.L2014:
	add	w4, w27, 1
	uxth	w27, w4
	b	.L2012
.L2047:
	cbz	w21, .L2024
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w21
	mov	w2, w23
	mov	w3, 1
	strb	wzr, [x22,1945]
	ldr	x0, [x0,-88]
	bl	FlashProgPages
	ldrb	w0, [x29,120]
	mov	x1, 0
	strb	w0, [x22,1945]
.L2017:
	cmp	w21, w1, uxth
	bls	.L2048
	add	x2, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x1, x0
	ldr	x2, [x2,-88]
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L2018
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L2018:
	add	x1, x1, 1
	b	.L2017
.L2048:
	add	w24, w24, w28
	ldr	w0, [x29,124]
	uxth	w24, w24
	cmp	w24, w0
	bcc	.L2020
	mov	x22, 0
	mov	x24, 56
.L2021:
	cmp	w21, w22, uxth
	bls	.L2049
	cbz	w25, .L2022
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x22, x24
	ldr	x1, [x1,-88]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L2022
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L2022:
	add	x22, x22, 1
	b	.L2021
.L2049:
	cmp	w26, 63
	bls	.L2028
	cbz	w25, .L2024
.L2028:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, w21
	ldr	x0, [x20,-88]
	bl	FlashEraseBlocks
.L2024:
	mov	w0, w19
.L2000:
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
	ldr	w1, [x2,-96]
	cbnz	w1, .L2052
	adrp	x19, .LANCHOR4
	adrp	x20, .LANCHOR0
	add	x24, x19, :lo12:.LANCHOR4
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x2,2072]
	mov	x21, 0
	ldr	x2, [x2,2120]
	add	x23, x20, 2104
	str	x2, [x24,176]
	ldrh	w2, [x20,2034]
	str	x0, [x24,168]
	bl	ftl_memset
.L2053:
	ldrh	w1, [x20,1978]
	mov	w0, w21
	add	x21, x21, 1
	cmp	w0, w1
	bge	.L2066
	add	x1, x22, :lo12:.LANCHOR2
	ldr	x3, [x24,168]
	ldrh	w2, [x1,2248]
	add	x1, x23, x21, lsl 3
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L2053
.L2066:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 255
	add	x19, x19, 160
	mov	w2, 16
	mov	w24, 0
	adrp	x26, .LC121
	ldr	x21, [x19,16]
	adrp	x27, .LC122
	mov	w25, w24
	add	x26, x26, :lo12:.LC121
	mov	x0, x21
	add	x27, x27, :lo12:.LC122
	bl	ftl_memset
	mov	w0, -3887
	strh	w0, [x21]
	ldr	w0, [x23,8]
	str	w0, [x21,4]
	ldrh	w0, [x20,2104]
	strh	w0, [x21,2]
	ldrh	w0, [x23,4]
	strh	w0, [x21,8]
	ldrh	w0, [x23,6]
	strh	w0, [x21,10]
	ldr	w0, [x20,1952]
	strh	w0, [x21,12]
.L2055:
	add	x28, x22, :lo12:.LANCHOR2
	ldrh	w1, [x20,2104]
	ldrh	w2, [x20,2106]
	ldrh	w3, [x20,2108]
	ldr	x0, [x28,2072]
	str	x0, [x19,8]
	ldr	x0, [x28,2120]
	str	x0, [x19,16]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19,4]
	mov	x0, x26
	ldrh	w4, [x21,10]
	str	wzr, [x19]
	bl	printk
	ldrh	w0, [x20,2026]
	ldrh	w1, [x20,2106]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2056
	ldr	w0, [x20,2112]
	strh	wzr, [x20,2106]
	add	w0, w0, 1
	str	w0, [x20,2112]
	str	w0, [x21,4]
	ldrh	w0, [x20,2104]
	ldrh	w1, [x20,2108]
	strh	w0, [x21,8]
	strh	w0, [x20,2108]
	ldr	x0, [x28,-88]
	strh	w1, [x20,2104]
	lsl	w1, w1, 10
	str	w1, [x19,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x28,-88]
	bl	FlashEraseBlocks
.L2056:
	mov	w1, 1
	mov	x0, x19
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w0, [x20,2106]
	add	w0, w0, 1
	strh	w0, [x20,2106]
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L2057
	ldr	w1, [x19,4]
	mov	x0, x27
	add	w24, w24, 1
	bl	printk
	uxth	w24, w24
	cmp	w24, 3
	bls	.L2055
	adrp	x0, .LC123
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC123
	mov	w2, w24
	add	x22, x22, :lo12:.LANCHOR2
	bl	printk
	mov	w0, 1
	str	w0, [x22,-96]
	b	.L2052
.L2057:
	add	w25, w25, 1
	cmp	w25, 1
	beq	.L2055
	cmp	w0, 256
	beq	.L2055
.L2052:
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
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w1, [x19,-96]
	stp	x27, x28, [sp,80]
	cbnz	w1, .L2068
	mov	x20, x0
	add	x23, x19, 136
	adrp	x21, .LANCHOR0
	mov	w24, 7
	add	x25, x19, 24
.L2069:
	cmp	x20, x23
	bne	.L2070
	ldrh	w2, [x19,32]
	ldr	w4, [x19,860]
	lsr	w3, w2, 1
	add	w1, w3, 1
	mul	w0, w2, w4
	add	w1, w1, w0, lsr 2
	add	x0, x21, :lo12:.LANCHOR0
	uxth	w1, w1
	ldr	w0, [x0,1948]
	cbz	w0, .L2071
	ldr	w0, [x19,256]
	cmp	w0, 39
	bhi	.L2071
	cmp	w0, 2
	mov	w1, 0
	bls	.L2072
	tbz	x2, 0, .L2099
	cbz	w4, .L2072
.L2099:
	mov	w1, w3
	b	.L2071
.L2070:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L2072
	add	x2, x21, :lo12:.LANCHOR0
	ldrh	w0, [x2,1976]
	cmp	w0, 1
	beq	.L2072
	ldrb	w0, [x2,196]
	cbnz	w0, .L2072
	ldr	w2, [x2,1948]
	ldrh	w0, [x19,32]
	lsr	w1, w0, 3
	cbz	w2, .L2071
	ldr	w2, [x19,256]
	cmp	w2, 1
	bhi	.L2071
	mul	w1, w0, w24
	lsr	w1, w1, 3
.L2071:
	cbz	w1, .L2072
	sub	w1, w1, #1
	uxth	w1, w1
.L2072:
	ldrb	w2, [x20,8]
	mov	x0, x25
	add	x27, x21, :lo12:.LANCHOR0
	bl	List_pop_index_node
	uxth	w28, w0
	ldrh	w0, [x19,32]
	sub	w0, w0, #1
	strh	w0, [x19,32]
	ldrh	w0, [x27,1964]
	cmp	w0, w28
	bls	.L2069
	ubfiz	x26, x28, 1, 16
	ldr	x0, [x19]
	ldrh	w0, [x0,x26]
	cbnz	w0, .L2069
	strh	w28, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L2124
	mov	x0, 0
	ldrh	w5, [x27,1956]
	mov	x3, 56
	mov	w27, w0
	mov	w6, 65535
.L2076:
	cmp	w5, w0, uxth
	bls	.L2127
	ldr	x4, [x19,-88]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L2077
	umull	x2, w27, w3
	add	w27, w27, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w27, w27
	str	w1, [x2,4]
.L2077:
	add	x0, x0, 1
	b	.L2076
.L2127:
	add	x0, x19, 40
	cmp	x20, x0
	bne	.L2079
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w1, [x1,1948]
	cbz	w1, .L2079
	ldr	x1, [x19,-80]
	ldrh	w1, [x1,x26]
	cmp	w1, 40
	bls	.L2079
	strb	wzr, [x19,48]
.L2079:
	ldrb	w0, [x20,8]
	cbnz	w0, .L2080
	ldr	x0, [x19,-80]
	mov	w1, 2
	ldrh	w2, [x0,x26]
	cbz	w2, .L2125
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w1, [x1,2016]
	add	w1, w2, w1
.L2125:
	strh	w1, [x0,x26]
	mov	w1, 0
	ldr	w0, [x19,240]
	add	w0, w0, 1
	str	w0, [x19,240]
	mov	w0, w28
	bl	ftl_set_blk_mode
	b	.L2083
.L2080:
	ldr	x1, [x19,-80]
	ldrh	w0, [x1,x26]
	add	w0, w0, 1
	strh	w0, [x1,x26]
	ldr	w0, [x19,244]
	add	w0, w0, 1
	str	w0, [x19,244]
	mov	w0, w28
	bl	ftl_set_blk_mode.part.9
.L2083:
	ldr	x0, [x19,-80]
	ldr	w1, [x19,252]
	ldrh	w0, [x0,x26]
	cmp	w0, w1
	bls	.L2084
	str	w0, [x19,252]
.L2084:
	add	x2, x21, :lo12:.LANCHOR0
	ldr	w3, [x19,240]
	ldr	w0, [x19,244]
	ldrh	w1, [x2,2016]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,1964]
	mov	x3, 56
	udiv	w0, w0, w1
	ldr	x1, [x19,2160]
	str	w0, [x19,248]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L2085:
	cmp	w27, w0, uxth
	bls	.L2128
	ldr	x1, [x19,-88]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L2085
.L2128:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L2087
	ldrb	w0, [x20,8]
	cmp	w0, 1
	ldr	x0, [x19,-88]
	bne	.L2088
	mov	w1, 0
	b	.L2126
.L2088:
	mov	w1, 1
.L2126:
	mov	w2, w27
	bl	FlashEraseBlocks
.L2087:
	ldrb	w1, [x20,8]
	mov	w2, w27
	ldr	x0, [x19,-88]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	x4, 56
	mov	w2, w1
.L2089:
	cmp	w27, w1, uxth
	bls	.L2129
	mul	x0, x1, x4
	ldr	x3, [x19,-88]
	add	x5, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	bne	.L2090
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
.L2090:
	add	x1, x1, 1
	b	.L2089
.L2129:
	cbz	w2, .L2092
	mov	w0, w28
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2092:
	ldrb	w0, [x20,7]
	cbnz	w0, .L2093
.L2124:
	ldr	x0, [x19]
	mov	w1, -1
	strh	w1, [x0,x26]
	b	.L2069
.L2093:
	adrp	x1, .LANCHOR0
	add	x2, x22, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x1,2024]
	strh	w28, [x20]
	mul	w0, w0, w1
	ldr	w1, [x2,232]
	str	w1, [x20,12]
	uxth	w0, w0
	add	w1, w1, 1
	strh	w0, [x20,4]
	str	w1, [x2,232]
	ldr	x1, [x22,#:lo12:.LANCHOR2]
	ldrh	w2, [x20]
	strh	w0, [x1,x2,lsl 1]
.L2068:
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
	ldrh	w0, [x19,944]
	stp	x21, x22, [sp,32]
	mov	w21, 0
	cbz	w0, .L2132
	adrp	x22, .LANCHOR0
	adrp	x24, .LC124
	add	x22, x22, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LC124
.L2140:
	ldrh	w0, [x22,1956]
	cmp	w0, w21
	bls	.L2141
	add	x0, x22, 1984
	mov	w1, w23
	mov	w20, 0
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w25, w0
.L2133:
	ldrh	w0, [x19,944]
	cmp	w0, w20
	bls	.L2142
	add	x0, x19, 952
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w25
	bne	.L2134
	mov	w1, w25
	mov	x0, x24
	bl	printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,944]
	mov	w3, w20
.L2135:
	cmp	w3, w2
	bcs	.L2143
	add	x4, x19, 952
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2135
.L2143:
	sub	w2, w2, #1
	strh	w2, [x19,944]
.L2134:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2133
.L2142:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2140
.L2141:
	bl	FtlGcReFreshBadBlk
.L2132:
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
	ubfiz	x0, x19, 1, 16
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x2, [x20,#:lo12:.LANCHOR2]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2145
	ldrh	w0, [x1,280]
	cmp	w0, w19
	bne	.L2146
	mov	w0, -1
	strh	w0, [x1,280]
	b	.L2147
.L2146:
	ldrh	w3, [x1,40]
	mov	w0, w2
	cmp	w3, w19
	beq	.L2148
	ldrh	w3, [x1,88]
	cmp	w3, w19
	beq	.L2148
	ldrh	w1, [x1,136]
	cmp	w1, w19
	beq	.L2148
.L2147:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w19
	sub	x0, x20, #8
	bl	List_remove_node
	ldrh	w0, [x20,16]
	sub	w0, w0, #1
	strh	w0, [x20,16]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L2148
.L2145:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2148:
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
	adrp	x20, .LANCHOR2
	beq	.L2153
	ubfiz	x21, x19, 1, 16
	ldr	x0, [x20,#:lo12:.LANCHOR2]
	add	x22, x20, :lo12:.LANCHOR2
	ldrh	w2, [x0,x21]
	cbnz	w2, .L2154
	adrp	x0, .LC125
	mov	w1, w19
	add	x0, x0, :lo12:.LC125
	add	x23, x22, 24
	bl	printk
	ldr	x0, [x20,#:lo12:.LANCHOR2]
	mov	w1, 32
	strh	w1, [x0,x21]
	mov	x0, x23
	mov	w1, w19
	bl	test_node_in_list
	cbz	w0, .L2155
	mov	w1, w19
	mov	x0, x23
	bl	List_remove_node
	ldrh	w0, [x22,32]
	sub	w0, w0, #1
	strh	w0, [x22,32]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x2, [x20,#:lo12:.LANCHOR2]
	adrp	x0, .LC126
	add	x0, x0, :lo12:.LC126
	mov	w1, w19
	ldrh	w2, [x2,x21]
	bl	printk
.L2155:
	mov	w0, w19
	bl	FtlGcRefreshBlock
	b	.L2162
.L2154:
	sub	w2, w2, #1
	strh	w2, [x0,x21]
.L2153:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w1, [x20,2336]
	cmp	w1, w0
	bne	.L2157
	strh	w19, [x20,2336]
.L2162:
	mov	w0, 0
	b	.L2156
.L2157:
	cmp	w1, w19
	mov	w0, 0
	beq	.L2156
	mov	w0, w1
	bl	update_vpc_list
	cmp	w0, wzr
	cset	w0, ne
	strh	w19, [x20,2336]
.L2156:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
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
	cbz	w1, .L2163
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2163
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR2
	adrp	x22, .LANCHOR0
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR2
	add	x21, x21, 988
	ldrh	w0, [x19,x0,lsl 1]
.L2167:
	cmp	w0, w20
	bne	.L2174
.L2169:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1956]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2168
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2168:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2167
.L2174:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2170
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbnz	w0, .L2170
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w20
	bne	.L2170
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2169
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2163
.L2170:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,196]
	cbz	w2, .L2163
	cmp	w1, 1
	bne	.L2163
	ldrh	w2, [x19,2]
	ldrh	w1, [x0,2026]
	cmp	w2, w1
	bcc	.L2163
	adrp	x2, .LANCHOR2
	ldrh	w1, [x19]
	ldrh	w4, [x19,4]
	lsl	x1, x1, 1
	ldr	x3, [x2,#:lo12:.LANCHOR2]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,2024]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2163:
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
	add	x21, x21, 988
	ldrh	w0, [x19,x0,lsl 1]
.L2176:
	cmp	w0, w22
	adrp	x20, .LANCHOR0
	bne	.L2191
.L2177:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1956]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2178
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2178:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2176
.L2191:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2179
	add	x1, x23, :lo12:.LANCHOR0
	ldrb	w1, [x1,196]
	cbnz	w1, .L2179
	ldrh	w1, [x19,2]
	ldrh	w1, [x21,w1,sxtw 1]
	cmp	w1, w22
	bne	.L2179
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2177
.L2179:
	ldrh	w1, [x19,4]
	adrp	x21, .LANCHOR2
	ldrh	w22, [x19,2]
	mov	w23, 65535
	add	x21, x21, :lo12:.LANCHOR2
	sub	w1, w1, #1
	orr	w22, w22, w0, lsl 10
	strh	w1, [x19,4]
	mov	w24, w23
	add	x21, x21, 988
.L2180:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,1956]
.L2182:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2181
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2181:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2182
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2187
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbnz	w0, .L2184
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w24
	bne	.L2184
	ldrh	w1, [x19,4]
	cbz	w1, .L2184
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2180
.L2184:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,196]
	cbz	w0, .L2187
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,2026]
	cmp	w1, w0
	bcc	.L2187
	adrp	x0, .LANCHOR2
	ldrh	w1, [x19]
	ldrh	w3, [x19,4]
	lsl	x1, x1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR2]
	ldrh	w2, [x0,x1]
	sub	w2, w2, w3
	strh	w2, [x0,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x20,2024]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2187:
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
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w0, [x19,-96]
	stp	x27, x28, [sp,80]
	cbnz	w0, .L2194
	adrp	x23, .LANCHOR4
	ldr	x20, [x19,2120]
	add	x21, x23, :lo12:.LANCHOR4
	ldr	x1, [x19,2072]
	ldrh	w2, [x19,264]
	adrp	x24, .LANCHOR0
	add	x25, x24, :lo12:.LANCHOR0
	sub	x26, x19, #64
	str	x1, [x21,168]
	add	x21, x21, 160
	str	x20, [x21,16]
	strh	w2, [x20,2]
	mov	w2, -3932
	strh	w2, [x20]
	str	w0, [x20,12]
	ldr	w2, [x19,272]
	str	w2, [x20,4]
	str	w0, [x20,8]
	mov	w0, 19539
	ldrh	w1, [x19,40]
	movk	w0, 0x4654, lsl 16
	strh	w1, [x19,-50]
	ldrh	w1, [x19,42]
	ldrb	w2, [x19,46]
	str	w0, [x19,-64]
	mov	w0, 86
	orr	w1, w2, w1, lsl 6
	strh	w1, [x19,-48]
	ldrh	w1, [x19,88]
	movk	w0, 0x5000, lsl 16
	strh	w1, [x19,-46]
	ldrh	w1, [x19,90]
	ldrb	w2, [x19,94]
	str	w0, [x19,-60]
	ldrh	w0, [x19,270]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x19,-56]
	ldrh	w0, [x25,1978]
	strb	w0, [x19,-54]
	ldrb	w0, [x19,48]
	strb	w0, [x19,-53]
	strh	w1, [x19,-44]
	ldrb	w0, [x19,96]
	ldrh	w1, [x19,136]
	strb	w0, [x19,-52]
	strh	w1, [x19,-42]
	ldrh	w1, [x19,138]
	ldrb	w0, [x19,144]
	strb	w0, [x19,-51]
	ldr	w0, [x19,240]
	str	w0, [x19,-32]
	ldr	w0, [x19,232]
	str	w0, [x19,-24]
	ldr	w0, [x19,236]
	ldrb	w2, [x19,142]
	str	w0, [x19,-28]
	ldrh	w0, [x19,936]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x19,-20]
	ldrh	w0, [x19,938]
	strh	w0, [x19,-18]
	ldrh	w2, [x25,2034]
	ldr	x0, [x21,8]
	strh	w1, [x19,-40]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x21,8]
	mov	x1, x26
	mov	w2, 48
	bl	ftl_memcpy
	ldr	x0, [x21,8]
	ldrh	w2, [x25,1964]
	ldr	x1, [x22,#:lo12:.LANCHOR2]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x25,1964]
	ldr	x1, [x21,8]
	uxth	x0, w2
	lsr	w2, w2, 3
	add	x0, x0, 24
	add	w2, w2, 4
	lsr	x0, x0, 1
	add	x0, x1, x0, lsl 2
	ldr	x1, [x24,#:lo12:.LANCHOR0]
	bl	ftl_memcpy
	ldrh	w0, [x25,2068]
	cbz	w0, .L2195
	ldrh	w0, [x25,1964]
	ldrh	w2, [x25,2060]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x21,8]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,2216]
	bl	ftl_memcpy
.L2195:
	mov	w0, 0
	mov	w21, 0
	mov	w25, 65535
	add	x26, x23, :lo12:.LANCHOR4
	add	x19, x22, :lo12:.LANCHOR2
	bl	FtlUpdateVaildLpn
.L2196:
	ldrh	w1, [x19,264]
	ldr	x0, [x19,2072]
	ldrh	w2, [x19,266]
	str	x0, [x26,168]
	ldr	x0, [x19,2120]
	str	x0, [x26,176]
	orr	w0, w2, w1, lsl 10
	str	w0, [x26,164]
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w0, [x0,2026]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L2197
	ldrh	w25, [x19,268]
	strh	wzr, [x19,266]
	strh	w1, [x19,268]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,232]
	str	w1, [x19,272]
	add	w2, w1, 1
	str	w2, [x19,232]
	ubfiz	w2, w0, 10, 16
	str	w2, [x26,164]
	strh	w0, [x19,264]
	str	w1, [x20,4]
	strh	w0, [x20,2]
.L2197:
	add	x1, x24, :lo12:.LANCHOR0
	ldrb	w0, [x1,72]
	cbz	w0, .L2198
	ldr	x0, [x19,2072]
	ldrh	w1, [x1,2034]
	bl	js_hash
	str	w0, [x20,12]
.L2198:
	mov	w1, 1
	add	x0, x26, 160
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,266]
	ldr	w2, [x26,160]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x19,266]
	bne	.L2199
	cmp	w1, 1
	bne	.L2200
	add	x1, x24, :lo12:.LANCHOR0
	ldrh	w1, [x1,2026]
	sub	w1, w1, #1
	strh	w1, [x19,266]
.L2200:
	add	w21, w21, 1
	uxth	w21, w21
	cmp	w21, 3
	bls	.L2196
	add	x23, x23, :lo12:.LANCHOR4
	adrp	x0, .LC127
	add	x0, x0, :lo12:.LC127
	mov	w2, w21
	add	x22, x22, :lo12:.LANCHOR2
	ldr	w1, [x23,164]
	bl	printk
	mov	w0, 1
	str	w0, [x22,-96]
	b	.L2194
.L2199:
	cmp	w1, 1
	beq	.L2196
	cmp	w2, 256
	beq	.L2196
	mov	w0, 65535
	cmp	w25, w0
	beq	.L2194
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L2194:
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
	ldr	w1, [x1,-96]
	cbnz	w1, .L2209
	adrp	x21, .LANCHOR0
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w22, [x2,196]
	cbz	w22, .L2224
	ldrb	w22, [x0,8]
	cmp	w22, 1
	bne	.L2220
	ldrh	w24, [x0,4]
	b	.L2211
.L2220:
	mov	w22, w1
.L2224:
	mov	w24, 12
.L2211:
	mov	x19, x0
	mov	w0, -1
	str	w0, [x29,96]
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 61589
	ldr	x23, [x0,2120]
	ldr	x1, [x0,2072]
	str	x1, [x29,80]
	mov	w1, -3
	str	x23, [x29,88]
	str	w1, [x23,8]
	mov	w1, -2
	str	w1, [x23,12]
	ldrh	w1, [x19]
	strh	w1, [x23,2]
	strh	wzr, [x23]
	ldr	x1, [x0,2072]
	str	w2, [x1]
	ldr	x1, [x0,2072]
	mov	w0, 22136
	movk	w0, 0x1234, lsl 16
	str	w0, [x1,4]
.L2212:
	sub	w24, w24, #1
	cmn	w24, #1
	beq	.L2215
	ldrh	w0, [x19,4]
	cbnz	w0, .L2213
.L2215:
	ldrh	w0, [x19]
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x2, [x20,#:lo12:.LANCHOR2]
	lsl	x0, x0, 1
	ldrh	w3, [x19,4]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x21,2024]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	b	.L2209
.L2213:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2215
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, w22
	mov	w3, 0
	ldr	w0, [x1,236]
	str	w0, [x23,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,236]
	add	x0, x29, 72
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2212
.L2209:
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
	blt	.L2226
	ubfiz	x0, x0, 1, 16
	ldrh	w24, [x20,x0]
	cbz	w24, .L2226
	ldr	w1, [x19,52]
	cbnz	w1, .L2226
	mov	w2, 1
	str	w2, [x19,52]
	strh	w1, [x20,x0]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2026]
	cmp	w1, w0
	bcc	.L2227
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2227:
	adrp	x20, .LANCHOR4
	mov	w21, 0
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x26, .LANCHOR2
	add	x20, x20, 160
.L2228:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L2237
	ubfiz	x0, x21, 2, 16
	add	x27, x25, x0
	ldr	w1, [x25,x0]
	cmp	w24, w1, lsr 10
	bne	.L2229
	add	x1, x26, :lo12:.LANCHOR2
	ldr	x2, [x1,2080]
	ldr	x23, [x1,2120]
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
	bne	.L2230
.L2232:
	adrp	x1, .LANCHOR4
	str	wzr, [x27]
	add	x1, x1, :lo12:.LANCHOR4
	adrp	x0, .LC128
	ldrh	w2, [x23,8]
	add	x0, x0, :lo12:.LC128
	ldr	w1, [x1,164]
	bl	printk
	adrp	x0, .LANCHOR2
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR2
	str	w1, [x0,-96]
	b	.L2231
.L2230:
	ldrh	w0, [x23,8]
	cmp	w0, w21
	bne	.L2232
	ldrh	w1, [x23]
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L2232
	ldr	x2, [x20,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L2229:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2228
.L2237:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L2226:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x22,2026]
	cmp	w1, w0
	bcc	.L2231
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2231:
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
	ldr	w20, [x1,-96]
	ldr	x21, [x0,16]
	ldr	x26, [x0,40]
	cbnz	w20, .L2239
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L2240
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
	b	.L2239
.L2240:
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x19,2]
	ldr	x23, [x1,2120]
	adrp	x22, .LANCHOR0
	ldrh	w25, [x21,x0]
	adrp	x21, .LANCHOR4
	add	x0, x21, :lo12:.LANCHOR4
	orr	w2, w2, w25, lsl 10
	str	w2, [x0,164]
	ldr	x2, [x1,2072]
	str	x2, [x0,168]
	str	x23, [x0,176]
	ldr	w0, [x19,48]
	str	w0, [x23,4]
	mov	w0, -1291
	strh	w0, [x23,8]
	ldrh	w0, [x19,4]
	strh	w0, [x23]
	add	x0, x22, :lo12:.LANCHOR0
	strh	w25, [x23,2]
	ldrh	w2, [x0,2026]
	ldr	x0, [x1,2072]
	mov	w1, 255
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, w20
.L2241:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L2248
	ubfiz	x3, x0, 2, 32
	ldr	w1, [x26,x3]
	cmp	w25, w1, lsr 10
	bne	.L2242
	add	x2, x24, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x4, [x2,2072]
	ubfiz	x1, x20, 3, 16
	str	w0, [x4,x1]
	ldr	x2, [x2,2072]
	ldr	w3, [x26,x3]
	add	x1, x2, x1
	str	w3, [x1,4]
.L2242:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L2241
.L2248:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w0, [x22,72]
	cbz	w0, .L2244
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w1, [x22,2034]
	ldr	x0, [x0,168]
	bl	js_hash
	str	w0, [x23,12]
.L2244:
	mov	w1, 1
	add	x0, x21, :lo12:.LANCHOR4
	add	x0, x0, 160
	mov	w2, w1
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	strh	w0, [x19,2]
	mov	x0, x19
	bl	ftl_map_blk_gc
.L2239:
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
	adrp	x24, .LC129
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w26, w1
	str	x2, [x29,104]
	mov	w22, 0
	add	x23, x23, 160
	add	x24, x24, :lo12:.LC129
.L2250:
	adrp	x19, .LANCHOR2
	adrp	x20, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,216]
	add	w0, w0, 1
	str	w0, [x1,216]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x28,2]
	ldrh	w0, [x0,2026]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L2251
	ldrh	w1, [x28]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2252
.L2251:
	mov	x0, x28
	bl	Ftl_write_map_blk_to_last_page
.L2252:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	w1, [x2,-96]
	cbnz	w1, .L2253
	ldrh	w3, [x28]
	adrp	x21, .LANCHOR4
	ldr	x0, [x28,16]
	ldrh	w25, [x0,x3,lsl 1]
	add	x3, x27, :lo12:.LANCHOR4
	ldrh	w0, [x28,2]
	add	x3, x3, 160
	orr	w0, w0, w25, lsl 10
	str	w0, [x3,4]
	ldr	x0, [x29,104]
	str	x0, [x3,8]
	ldr	x0, [x2,2120]
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
	cbz	w0, .L2254
	ldr	x0, [x3,8]
	ldrh	w1, [x1,2034]
	str	x2, [x29,96]
	bl	js_hash
	ldr	x2, [x29,96]
	str	w0, [x2,12]
.L2254:
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
	bne	.L2255
	ldr	w1, [x23,4]
	mov	x0, x24
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	ldrh	w0, [x28,2]
	cmp	w0, 2
	bhi	.L2256
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20,2026]
	sub	w0, w0, #1
	strh	w0, [x28,2]
.L2256:
	cmp	w22, 3
	bls	.L2250
	add	x21, x21, :lo12:.LANCHOR4
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
	mov	w2, w22
	add	x19, x19, :lo12:.LANCHOR2
	ldr	w1, [x21,164]
	bl	printk
	mov	w0, 1
	str	w0, [x19,-96]
	b	.L2253
.L2255:
	cbz	w1, .L2258
	strh	w25, [x28,60]
.L2258:
	cmp	w0, 1
	beq	.L2259
	cmp	w1, 256
	beq	.L2259
	ldr	w0, [x28,56]
	cbz	w0, .L2260
.L2259:
	str	wzr, [x28,56]
	b	.L2250
.L2260:
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x0, [x28,40]
	ldr	w1, [x21,164]
	str	w1, [x0,w26,uxtw 2]
.L2253:
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
	add	x0, x20, 2256
	ldr	x1, [x20,184]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20,184]
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
	add	x4, x4, 160
	str	x21, [x29,128]
	ldr	x2, [x1,2072]
	sub	w0, w27, #1
	ldr	x20, [x1,2120]
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
.L2269:
	cmp	w19, w27
	adrp	x22, .LANCHOR0
	bge	.L2287
	ldr	w0, [x29,108]
	sxtw	x2, w19
	cmp	w19, w0
	bne	.L2270
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
	add	x19, x19, 160
.L2271:
	cmp	w26, w24
	bge	.L2287
	ldrh	w0, [x21]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w26, w0, lsl 10
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w0, [x1,72]
	cbz	w0, .L2272
	ldr	x0, [x19,16]
	ldr	w28, [x0,12]
	cbz	w28, .L2272
	ldr	x0, [x19,8]
	ldrh	w1, [x1,2034]
	bl	js_hash
	cmp	w28, w0
	beq	.L2272
	str	w27, [x19]
.L2272:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2273
	ldrh	w0, [x20,8]
	cmp	w0, w25
	bcs	.L2273
	ldrh	w2, [x20]
	ldrh	w1, [x23,4]
	cmp	w2, w1
	bne	.L2273
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x19,4]
	str	w1, [x2,x0]
.L2273:
	add	w26, w26, 1
	sxth	w26, w26
	b	.L2271
.L2287:
	mov	x0, x23
	add	x22, x22, :lo12:.LANCHOR0
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x23,2]
	ldrh	w0, [x22,2026]
	cmp	w1, w0
	bne	.L2276
	mov	x0, x23
	bl	ftl_map_blk_alloc_new_blk
	b	.L2276
.L2270:
	ldr	x0, [x29,128]
	lsl	x2, x2, 1
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,2072]
	str	x0, [x21,8]
	add	x0, x24, x2
	str	x0, [x29,120]
	add	x0, x26, :lo12:.LANCHOR0
	ldrh	w1, [x24,x2]
	ldrh	w0, [x0,2026]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x21,4]
	mov	w2, w1
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L2288
	ldrh	w1, [x20]
	mov	w22, 0
	ldrh	w0, [x23,4]
	cmp	w1, w0
	bne	.L2309
	ldrh	w1, [x20,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2309
	mov	w5, w22
.L2278:
	add	x0, x26, :lo12:.LANCHOR0
	ldrh	w0, [x0,2026]
	sub	w0, w0, #1
	cmp	w5, w0
	bge	.L2281
	ldr	x0, [x29,128]
	sbfiz	x1, x5, 3, 32
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x2, [x0,2072]
	ldrh	w0, [x2,x1]
	cmp	w0, w25
	bcs	.L2279
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x1,4]
	str	w1, [x2,x0]
.L2279:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L2278
.L2288:
	mov	w22, 0
.L2309:
	add	x6, x26, :lo12:.LANCHOR0
	ldrh	w0, [x6,2026]
	cmp	w22, w0
	bge	.L2281
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
	cbz	w0, .L2282
	ldr	x0, [x21,16]
	ldr	w2, [x0,12]
	cbz	w2, .L2282
	ldr	x0, [x21,8]
	ldrh	w1, [x6,2034]
	str	x2, [x29,96]
	bl	js_hash
	ldr	x2, [x29,96]
	cmp	w2, w0
	beq	.L2282
	mov	w0, -1
	str	w0, [x21]
.L2282:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L2283
	ldrh	w0, [x20,8]
	cmp	w0, w25
	bcs	.L2283
	ldrh	w2, [x20]
	ldrh	w1, [x23,4]
	cmp	w2, w1
	bne	.L2283
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x21,4]
	str	w1, [x2,x0]
.L2283:
	add	w5, w22, 1
	sxth	w22, w5
	b	.L2309
.L2281:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2269
.L2276:
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
	add	x0, x0, 224
	add	x29, sp, 0
	ldrh	w2, [x1,2044]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,2070]
	strh	w2, [x0,8]
	ldrh	w2, [x1,2046]
	ldr	x1, [x1,2072]
	str	x1, [x0,16]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	strh	w2, [x0,6]
	ldr	x2, [x1,2200]
	str	x2, [x0,24]
	ldr	x2, [x1,2192]
	ldr	x1, [x1,2208]
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
	add	x0, x0, 2256
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
	ldrh	w21, [x0,2032]
	mov	x25, x2
	ldrh	w0, [x0,2020]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L2313
	adrp	x24, .LANCHOR4
	lsr	w21, w26, w21
	add	x24, x24, :lo12:.LANCHOR4
	mov	w20, 0
	adrp	x27, .LANCHOR2
	add	x24, x24, 224
.L2314:
	cbz	w28, .L2313
	adrp	x0, .LANCHOR2
	add	x2, x0, :lo12:.LANCHOR2
	ldr	x0, [x2,2208]
	ldr	w4, [x0,w21,uxtw 2]
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2030]
	uxth	w0, w28
	udiv	w22, w26, w1
	msub	w22, w22, w1, w26
	sub	w19, w1, w22
	uxth	w19, w19
	cmp	w19, w28
	csel	w19, w0, w19, hi
	cmp	w19, w1
	beq	.L2316
	cbz	w4, .L2316
	ldr	x0, [x2,2088]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w4, [x29,124]
	str	xzr, [x29,136]
	bl	FlashReadPages
	b	.L2317
.L2316:
	add	x0, x27, :lo12:.LANCHOR2
	add	x2, x23, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x0,2088]
	ldrh	w2, [x2,2034]
	bl	ftl_memset
.L2317:
	add	x5, x27, :lo12:.LANCHOR2
	lsl	w4, w19, 9
	ubfiz	x22, x22, 9, 16
	mov	w2, w4
	mov	x1, x25
	str	x4, [x29,104]
	ldr	x0, [x5,2088]
	sub	w28, w28, w19
	str	x5, [x29,96]
	add	w26, w26, w19
	add	x0, x0, x22
	bl	ftl_memcpy
	ldr	x5, [x29,96]
	mov	w1, w21
	mov	x0, x24
	add	w21, w21, 1
	ldr	x2, [x5,2088]
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	x4, [x29,104]
	csinv	w20, w20, wzr, ne
	add	x25, x25, x4, sxtw
	b	.L2314
.L2313:
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
	add	x2, x0, 336
	add	x29, sp, 0
	ldr	w3, [x0,336]
	cmp	w3, w1
	bne	.L2327
	mov	w1, 86
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	ldr	w1, [x0,224]
	str	w1, [x2,88]
	ldr	w1, [x0,228]
	str	w1, [x2,92]
	ldr	w1, [x0,220]
	str	w1, [x2,8]
	ldr	w1, [x0,208]
	str	w1, [x2,12]
	ldr	w1, [x0,200]
	str	w1, [x2,16]
	ldr	w1, [x0,216]
	str	w1, [x2,20]
	ldr	w1, [x0,244]
	str	w1, [x2,28]
	ldr	w1, [x0,-72]
	str	w1, [x2,32]
	ldr	w1, [x0,204]
	str	w1, [x2,36]
	ldr	w1, [x0,212]
	str	w1, [x2,40]
	ldr	w1, [x0,252]
	str	w1, [x2,44]
	ldr	w1, [x0,256]
	str	w1, [x2,48]
	ldr	w1, [x0,328]
	ldr	w0, [x0,2020]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2327:
	ldp	x29, x30, [sp], 16
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x1, .LANCHOR0+1948
	adrp	x2, .LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w3, [x1,#:lo12:.LANCHOR0+1948]
	mov	w1, 32
	cbz	w3, .L2330
	add	x3, x2, :lo12:.LANCHOR2
	ldr	w3, [x3,256]
	cmp	w3, 39
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2330:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,288]
	cmp	w4, 31
	bhi	.L2331
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,288]
.L2331:
	cbnz	w0, .L2332
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,2160]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2333
.L2332:
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x0, [x2,2160]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	ldr	x0, [x2,2160]
	ldrh	w1, [x2,2144]
	str	wzr, [x0,4]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	ldr	x2, [x2,2160]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2333:
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
	ldrh	w2, [x0,2020]
	str	x1, [x29,136]
	cmp	w3, w2
	ldrh	w20, [x0,2032]
	mov	w0, -1
	bhi	.L2338
	adrp	x27, .LANCHOR4
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	add	x27, x27, :lo12:.LANCHOR4
	str	x0, [x29,128]
	lsr	w20, w23, w20
	add	x0, x27, 224
	mov	w21, 0
	str	x0, [x29,120]
.L2339:
	cbz	w22, .L2347
	adrp	x25, .LANCHOR2
	add	x4, x25, :lo12:.LANCHOR2
	ldr	x0, [x4,2208]
	ldr	w3, [x0,w20,uxtw 2]
	ldr	x0, [x29,136]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w19, [x0,2030]
	uxth	w0, w22
	udiv	w24, w23, w19
	msub	w24, w24, w19, w23
	sub	w19, w19, w24
	uxth	w19, w19
	cmp	w19, w22
	csel	w19, w0, w19, hi
	lsl	w28, w19, 9
	cbz	w3, .L2341
	ldr	x0, [x4,2088]
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
	ldr	w0, [x27,160]
	csinv	w21, w21, wzr, ne
	cmp	w0, 256
	bne	.L2343
	ldr	x0, [x29,128]
	mov	w1, w20
	mov	w2, w3
	bl	printk
	ldr	x4, [x29,112]
	mov	w1, w20
	ldr	x0, [x29,120]
	ldr	x2, [x4,2088]
	bl	FtlMapWritePage
.L2343:
	add	x25, x25, :lo12:.LANCHOR2
	ubfiz	x24, x24, 9, 16
	mov	x0, x26
	mov	w2, w28
	ldr	x1, [x25,2088]
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L2344
.L2341:
	mov	x0, x26
	mov	w1, w3
	mov	w2, w28
	bl	ftl_memset
.L2344:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x26, x26, x28, sxtw
	b	.L2339
.L2347:
	mov	w0, w21
.L2338:
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
	ldrh	w1, [x19,2144]
	ldr	x2, [x19,2160]
	bl	FtlVendorPartRead
	ldr	x0, [x19,2160]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L2349
	adrp	x0, .LC75
	adrp	x1, .LC132
	add	x1, x1, :lo12:.LC132
	add	x0, x0, :lo12:.LC75
	bl	printk
	ldrh	w2, [x19,2144]
	mov	w1, 0
	ldr	x0, [x19,2160]
	lsl	w2, w2, 9
	bl	ftl_memset
.L2349:
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
	add	x21, x20, 336
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,336]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L2351
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,336]
.L2351:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,336]
	cmp	w3, w2
	bne	.L2352
	ldr	w2, [x0,424]
	str	w2, [x0,224]
	ldr	w2, [x0,428]
	str	w2, [x0,228]
	ldr	w2, [x0,344]
	str	w2, [x0,220]
	ldr	w2, [x0,348]
	str	w2, [x0,208]
	ldr	w2, [x0,352]
	str	w2, [x0,200]
	ldr	w2, [x0,356]
	str	w2, [x0,216]
	ldr	w2, [x0,364]
	str	w2, [x0,244]
	ldr	w2, [x0,368]
	str	w2, [x0,-72]
	ldr	w2, [x0,372]
	str	w2, [x0,204]
	ldr	w2, [x0,376]
	str	w2, [x0,212]
	ldr	w2, [x0,380]
	str	w2, [x0,252]
	ldr	w1, [x0,396]
	ldr	w2, [x0,384]
	str	w2, [x0,256]
	str	w1, [x0,328]
.L2352:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR0
	ldr	w1, [x0,404]
	str	wzr, [x0,2020]
	mov	w0, 34661
	movk	w0, 0x1234, lsl 16
	cmp	w1, w0
	bne	.L2353
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 1
	str	w1, [x0,1948]
	adrp	x0, .LC75
	adrp	x1, .LC133
	add	x0, x0, :lo12:.LC75
	add	x1, x1, :lo12:.LC133
	bl	printk
.L2353:
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x21, [sp,32]
	ldrh	w1, [x20,2016]
	ldr	w2, [x19,240]
	ldr	w0, [x19,244]
	madd	w0, w1, w2, w0
	ldrh	w1, [x20,1964]
	udiv	w0, w0, w1
	str	w0, [x19,248]
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
	cbz	w1, .L2354
	adrp	x3, .LANCHOR2
	str	wzr, [x0,56]
	add	x3, x3, :lo12:.LANCHOR2
	ldr	w1, [x3,-96]
	cbnz	w1, .L2354
	adrp	x2, .LANCHOR4
	sub	w20, w20, #1
	add	x1, x2, :lo12:.LANCHOR4
	mov	x21, x0
	uxth	w20, w20
	ldr	x0, [x3,2080]
	ldr	x3, [x3,2120]
	add	x1, x1, 160
	str	x3, [x1,16]
	ubfiz	x3, x20, 2, 16
	str	x0, [x1,8]
	mov	x19, x2
	ldr	w3, [x4,x3]
	str	w3, [x1,4]
	cbz	w3, .L2358
	mov	x0, x1
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L2359
.L2358:
	adrp	x2, .LANCHOR0+2034
	mov	w1, 255
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2034]
	bl	ftl_memset
.L2359:
	add	x2, x19, :lo12:.LANCHOR4
	mov	x0, x21
	mov	w1, w20
	ldr	x2, [x2,168]
	bl	FtlMapWritePage
.L2354:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.type	FlashReadFacBbtData.part.16, %function
FlashReadFacBbtData.part.16:
	stp	x29, x30, [sp, -32]!
	mov	w3, 0
	adrp	x4, .LANCHOR4
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w7, 1
	mov	x19, x0
	cbnz	w1, .L2363
.L2365:
	add	x0, x4, :lo12:.LANCHOR4
	ldr	w1, [x0,92]
	cmp	w3, w1
	bcs	.L2363
	ubfx	x5, x3, 5, 11
	ldr	x0, [x0,56]
	lsl	x5, x5, 2
	lsl	w6, w7, w3
	add	w3, w3, 1
	ldr	w1, [x0,x5]
	uxth	w3, w3
	orr	w6, w1, w6
	str	w6, [x0,x5]
	b	.L2365
.L2363:
	add	x4, x4, :lo12:.LANCHOR4
	mov	x0, x19
	ldr	x1, [x4,56]
	bl	ftl_memcpy
	mov	w2, 4
	adrp	x0, .LC134
	mov	x1, x19
	add	x0, x0, :lo12:.LC134
	mov	w3, w2
	bl	rknand_print_hex
	ldr	x19, [sp,16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadFacBbtData.part.16, .-FlashReadFacBbtData.part.16
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
	ldr	x1, [x0,56]
	mul	w24, w23, w3
	ldr	x0, [x0,112]
	uxth	w20, w20
	str	x1, [x29,96]
	sub	w19, w3, #16
	str	x0, [x29,104]
.L2368:
	cmp	w20, w19
	ble	.L2376
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L2369
	add	x0, x21, :lo12:.LANCHOR4
	ldr	x0, [x0,112]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L2369
	mov	w0, w22
	cbz	x22, .L2370
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.16
	b	.L2370
.L2369:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2368
.L2376:
	mov	w0, -1
.L2370:
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
	adrp	x0, .LANCHOR0+744
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	ldr	x0, [x0,#:lo12:.LANCHOR0+744]
	ldrb	w2, [x0,13]
	ldrh	w19, [x0,14]
	add	x0, x20, :lo12:.LANCHOR4
	mul	w19, w2, w19
	ldr	x0, [x0,104]
	uxth	w19, w19
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L2378
.L2382:
	mov	w1, 0
	b	.L2379
.L2378:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L2380:
	cmp	w2, w19
	bge	.L2379
	add	x3, x20, :lo12:.LANCHOR4
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,104]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L2381
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L2381:
	cmp	w1, w0
	bcs	.L2382
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2380
.L2379:
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
	ldr	w21, [x0,-96]
	cbnz	w21, .L2388
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR4
	add	x24, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	add	x24, x24, 2104
	add	x20, x20, 160
	mov	x25, x24
	mov	x26, x20
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L2389:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22,1978]
	cmp	w21, w0
	bcs	.L2407
	add	x1, x23, :lo12:.LANCHOR2
	ldrh	w28, [x25,12]
	ldr	x0, [x1,2072]
	ldr	x27, [x1,2120]
	mov	w1, 65535
	str	x0, [x20,8]
	cmp	w28, w1
	str	x27, [x20,16]
	beq	.L2390
	ldrh	w0, [x22,2022]
	mov	w1, 1
	mov	w2, w1
	madd	w28, w21, w0, w28
	lsl	w0, w28, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x22,2022]
	ldr	x0, [x24,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L2391
.L2390:
	mov	w1, w21
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x22,2022]
.L2393:
	sub	w22, w22, #1
	uxth	w22, w22
.L2392:
	add	x28, x19, :lo12:.LANCHOR0
	ldrh	w0, [x28,2022]
	madd	w0, w21, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2393
	add	x3, x23, :lo12:.LANCHOR2
	mov	w1, 0
	mov	w2, 16
	strh	w22, [x25,12]
	str	x3, [x29,104]
	ldr	x0, [x3,2120]
	bl	ftl_memset
	str	wzr, [x27,4]
	mov	w0, -3872
	strh	w0, [x27]
	ldr	x3, [x29,104]
	ldrh	w0, [x25,12]
	strh	w0, [x27,2]
	ldrh	w0, [x28,2022]
	ldrh	w1, [x25,12]
	ldrh	w2, [x3,2248]
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
	bne	.L2391
	mov	w0, w28
	bl	FtlBbmMapBadBlock
	b	.L2392
.L2391:
	mov	w0, w28
	add	w21, w21, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L2389
.L2407:
	mov	w20, 0
.L2396:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,2038]
	cmp	w0, w20
	bls	.L2408
	mov	w0, w20
	add	w20, w20, 1
	bl	FtlBbmMapBadBlock
	uxth	w20, w20
	b	.L2396
.L2408:
	ldrh	w20, [x21,2116]
	mov	w24, 65535
	sub	w20, w20, #1
	uxth	w20, w20
.L2398:
	add	x22, x21, 2104
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L2402
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2399
	mov	w0, w20
	bl	FlashTestBlk
	cbz	w0, .L2400
	mov	w0, w20
	bl	FtlBbmMapBadBlock
	b	.L2399
.L2400:
	ldrh	w0, [x21,2104]
	cmp	w0, w24
	bne	.L2401
	strh	w20, [x21,2104]
	b	.L2399
.L2401:
	strh	w20, [x22,4]
	b	.L2402
.L2399:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2398
.L2402:
	add	x23, x23, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 2
	ldr	x1, [x23,-88]
	ldrh	w0, [x19,2104]
	str	wzr, [x19,2112]
	lsl	w0, w0, 10
	str	w0, [x1,4]
	strh	wzr, [x19,2106]
	ldr	x0, [x23,-88]
	ldrh	w1, [x19,2108]
	lsl	w1, w1, 10
	str	w1, [x0,60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x19,2104]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19,2108]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19,2106]
	ldr	w0, [x19,2112]
	ldrh	w1, [x19,2108]
	add	w0, w0, 1
	str	w0, [x19,2112]
	ldrh	w0, [x19,2104]
	strh	w0, [x19,2108]
	strh	w1, [x19,2104]
	bl	FtlBbmTblFlush
.L2388:
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
	ldr	w4, [x3,2096]
	ldrh	w27, [x3,2032]
	cmp	w0, w4
	bcs	.L2410
	add	w27, w27, 7
	adrp	x20, .LANCHOR2
	lsr	w24, w0, w27
	str	w0, [x29,116]
	add	x0, x20, :lo12:.LANCHOR2
	str	w2, [x29,112]
	mov	x26, x1
	uxth	w23, w24
	ldrh	w2, [x3,2062]
	ldr	x22, [x0,184]
	mov	x0, 0
	b	.L2411
.L2410:
	mov	w0, -1
	cbnz	w2, .L2412
	str	w0, [x1]
	b	.L2412
.L2416:
	add	x0, x0, 1
	add	x3, x22, x0, lsl 4
	ldrh	w3, [x3,-16]
	cmp	w3, w23
	beq	.L2413
.L2411:
	uxth	x19, w0
	cmp	w19, w2
	bcc	.L2416
	bl	select_l2p_ram_region
	uxth	x19, w0
	ubfiz	x21, x19, 4, 16
	mov	w1, 65535
	add	x2, x22, x21
	ldrh	w3, [x22,x21]
	cmp	w3, w1
	bne	.L2424
.L2417:
	add	x28, x20, :lo12:.LANCHOR2
	ubfiz	x0, x23, 2, 16
	ldr	x1, [x28,2216]
	ldr	w25, [x1,x0]
	cbnz	w25, .L2418
	ldr	x1, [x29,120]
	ldr	x0, [x28,184]
	add	x22, x1, :lo12:.LANCHOR0
	mov	w1, 255
	add	x0, x0, x21
	ldrh	w2, [x22,2034]
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x28,184]
	strh	w23, [x0,x21]
	ldr	x0, [x28,184]
	add	x21, x0, x21
	str	w25, [x21,4]
.L2413:
	mov	x0, 1
	lsl	x27, x0, x27
	ldr	w0, [x29,116]
	sub	w27, w27, #1
	and	w25, w0, w27
	ldr	w0, [x29,112]
	uxth	x25, w25
	cbnz	w0, .L2414
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,184]
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0,8]
	ldr	w0, [x0,x25,lsl 2]
	str	w0, [x26]
	b	.L2415
.L2414:
	add	x1, x20, :lo12:.LANCHOR2
	lsl	x0, x19, 4
	ldr	w3, [x26]
	ldr	x2, [x1,184]
	add	x2, x2, x0
	ldr	x2, [x2,8]
	str	w3, [x2,x25,lsl 2]
	strh	w23, [x1,192]
	ldr	x2, [x1,184]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2415:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x20,184]
	add	x19, x0, x19, lsl 4
	mov	w0, 0
	ldr	w1, [x19,4]
	cmn	w1, #1
	beq	.L2412
	add	w1, w1, 1
	str	w1, [x19,4]
	b	.L2412
.L2424:
	ldr	w1, [x2,4]
	tbz	w1, #31, .L2417
	bl	flush_l2p_region
	b	.L2417
.L2418:
	ldr	x0, [x28,184]
	adrp	x6, .LANCHOR4
	add	x6, x6, :lo12:.LANCHOR4
	mov	w1, 1
	add	x0, x0, x21
	add	x22, x6, 160
	mov	w2, w1
	str	x6, [x29,104]
	str	w25, [x6,164]
	ldr	x0, [x0,8]
	str	x0, [x6,168]
	ldr	x0, [x28,2120]
	str	x0, [x6,176]
	mov	x0, x22
	bl	FlashReadPages
	ldr	x0, [x22,16]
	ldr	x6, [x29,104]
	ldrh	w0, [x0,8]
	cmp	w0, w23
	beq	.L2419
	adrp	x0, .LC135
	uxth	w1, w24
	mov	w2, w25
	add	x0, x0, :lo12:.LC135
	bl	printk
	ldr	x1, [x22,16]
	mov	w2, 4
	adrp	x0, .LC99
	mov	w3, w2
	add	x0, x0, :lo12:.LC99
	bl	rknand_print_hex
	ldr	x0, [x29,120]
	mov	w2, 4
	ldr	x1, [x28,2216]
	add	x22, x0, :lo12:.LANCHOR0
	adrp	x0, .LC136
	add	x0, x0, :lo12:.LC136
	ldrh	w3, [x22,2060]
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x28,-96]
	b	.L2420
.L2419:
	ldr	w0, [x6,160]
	cmp	w0, 256
	bne	.L2420
	and	w24, w24, 65535
	adrp	x0, .LC137
	mov	w1, w24
	mov	w2, w25
	add	x0, x0, :lo12:.LC137
	bl	printk
	ldr	x2, [x28,184]
	add	x0, x28, 2256
	mov	w1, w24
	add	x2, x2, x21
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L2420:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x0, [x1,184]
	add	x0, x0, x21
	str	wzr, [x0,4]
	ldr	x0, [x1,184]
	strh	w23, [x0,x21]
	b	.L2413
.L2412:
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
	ldr	w0, [x23,-96]
	cbnz	w0, .L2425
	ldrh	w2, [x23,44]
	cbz	w2, .L2427
	ldrb	w1, [x23,48]
	cbnz	w1, .L2427
	adrp	x21, .LANCHOR0
	ldrb	w1, [x23,47]
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w3, [x21,2024]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L2427
	ldrb	w24, [x23,50]
	ldr	w22, [x21,2096]
	ldrh	w25, [x21,1956]
	cbnz	w24, .L2425
	sub	w22, w22, #1
	add	x1, x29, 84
	mov	w0, w22
	mov	w2, w24
	bl	log2phys
	ldr	x20, [x23,2120]
	ldr	w0, [x29,84]
	ldr	x1, [x23,2072]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w22, [x29,112]
	str	x1, [x29,96]
	str	x20, [x29,104]
	str	w24, [x20,4]
	beq	.L2429
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w24
	bl	FlashReadPages
	b	.L2430
.L2429:
	ldr	x0, [x23,2072]
	mov	w1, 255
	ldrh	w2, [x21,2034]
	bl	ftl_memset
.L2430:
	add	x24, x19, :lo12:.LANCHOR2
	mov	w0, -3947
	lsl	w25, w25, 2
	strh	w0, [x20]
	mov	w23, 0
	add	x21, x24, 40
.L2431:
	cmp	w23, w25
	beq	.L2432
	ldrh	w0, [x21,4]
	cbz	w0, .L2432
	ldr	w0, [x29,92]
	add	w23, w23, 1
	str	w0, [x20,12]
	ldrh	w0, [x21]
	strh	w0, [x20,2]
	mov	x0, x21
	str	w22, [x20,8]
	bl	get_new_active_ppa
	str	w0, [x29,92]
	ldr	w1, [x24,236]
	mov	w2, 0
	str	w1, [x20,4]
	mov	w3, w2
	add	w1, w1, 1
	add	x0, x29, 88
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x24,236]
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x21]
	bl	decrement_vpc_count
	b	.L2431
.L2432:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,50]
	b	.L2425
.L2427:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,50]
.L2425:
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
.L2443:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2062]
	cmp	w0, w19
	bls	.L2446
	add	x1, x21, :lo12:.LANCHOR2
	ubfiz	x0, x19, 4, 16
	ldr	x1, [x1,184]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2444
	mov	w0, w19
	bl	flush_l2p_region
.L2444:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2443
.L2446:
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
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w20, [x0]
	ldr	w1, [x1,-96]
	cbnz	w1, .L2448
	mov	x21, x0
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2449
	ubfiz	x1, x20, 1, 16
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2450
	mov	w0, w20
	bl	INSERT_DATA_LIST
	b	.L2449
.L2450:
	mov	w0, w20
	bl	INSERT_FREE_LIST
.L2449:
	add	x2, x19, :lo12:.LANCHOR2
	strb	wzr, [x21,8]
	add	x0, x2, 88
	cmp	x21, x0
	beq	.L2451
	adrp	x1, .LANCHOR0
	add	x4, x1, :lo12:.LANCHOR0
	ldrh	w3, [x4,1976]
	cmp	w3, 1
	beq	.L2451
	ldrb	w0, [x4,196]
	cbz	w0, .L2452
.L2451:
	mov	w0, 1
	strb	w0, [x21,8]
	b	.L2453
.L2452:
	add	x0, x2, 40
	cmp	x21, x0
	bne	.L2453
	cmp	w3, 3
	beq	.L2455
	ldr	w0, [x2,328]
	cmp	w0, 1
	bne	.L2456
.L2455:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x2,48]
.L2456:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x1,1948]
	cbz	w0, .L2453
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,256]
	cmp	w0, 39
	bhi	.L2453
	mov	w0, 1
	strb	w0, [x1,48]
.L2453:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,2336]
	mov	w0, 65535
	cmp	w1, w0
	beq	.L2458
	cmp	w20, w1
	bne	.L2459
	adrp	x0, .LANCHOR2
	ubfiz	x2, x1, 1, 16
	ldr	x0, [x0,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x2]
	cbz	w0, .L2460
.L2459:
	mov	w0, w1
	bl	update_vpc_list
.L2460:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,2336]
.L2458:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2448:
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
	adrp	x0, .LC106
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 80
	add	x0, x0, :lo12:.LC106
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	mov	w19, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	add	x21, x20, :lo12:.LANCHOR4
	bl	printk
	add	x0, x21, 296
	mov	w1, 0
	mov	x2, 8192
	adrp	x22, .LANCHOR0
	bl	memset
.L2474:
	add	x0, x22, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR0
	ldr	w0, [x0,2096]
	cmp	w19, w0
	bcs	.L2491
	mov	w0, w19
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	cmn	w0, #1
	beq	.L2475
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x2, x21, 296
	ldrh	w1, [x2,w0,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x2,w0,sxtw 1]
.L2475:
	add	w19, w19, 1
	b	.L2474
.L2491:
	adrp	x21, .LC138
	mov	w19, 0
	adrp	x22, .LANCHOR2
	add	x25, x20, :lo12:.LANCHOR4
	add	x21, x21, :lo12:.LC138
.L2477:
	add	x0, x24, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR2
	ldrh	w0, [x0,1964]
	cmp	w0, w19
	bls	.L2492
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x22,#:lo12:.LANCHOR2]
	ldrh	w2, [x1,x0]
	add	x0, x25, 296
	ldrh	w3, [x0,w19,sxtw 1]
	cmp	w2, w3
	beq	.L2478
	mov	x0, x21
	mov	w1, w19
	bl	printk
.L2478:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2477
.L2492:
	add	x1, x23, :lo12:.LANCHOR2
	ldr	x0, [x1,24]
	cbz	x0, .L2473
	ldr	x19, [x1,-16]
	adrp	x22, .LC139
	ldrh	w24, [x1,32]
	mov	w21, 0
	sub	x19, x0, x19
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	add	x22, x22, :lo12:.LC139
	add	x20, x20, :lo12:.LANCHOR4
	mov	w25, 6
	madd	x19, x0, x19, x19
	mov	w26, 65535
	uxth	w19, w19
.L2481:
	cmp	w21, w24
	beq	.L2473
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x23,#:lo12:.LANCHOR2]
	ldrh	w2, [x1,x0]
	cbz	w2, .L2482
	add	x3, x20, 296
	mov	x0, x22
	mov	w1, w19
	ldrh	w3, [x3,w19,sxtw 1]
	bl	printk
.L2482:
	add	x0, x23, :lo12:.LANCHOR2
	umull	x19, w19, w25
	ldr	x0, [x0,-16]
	ldrh	w19, [x0,x19]
	cmp	w19, w26
	beq	.L2473
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2481
.L2473:
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
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	mov	w1, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR4
	mov	w20, 0
	stp	x21, x22, [sp,48]
	add	x19, x19, :lo12:.LANCHOR4
	adrp	x21, .LC141
	add	x21, x21, :lo12:.LC141
	add	x19, x19, 160
	bl	printk
.L2494:
	adrp	x0, .LANCHOR0+2096
	ldr	w0, [x0,#:lo12:.LANCHOR0+2096]
	cmp	w20, w0
	bcs	.L2501
	mov	w0, w20
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	and	w0, w20, 2047
	cbnz	w0, .L2495
	ldr	w2, [x29,60]
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L2495:
	ldr	w0, [x29,60]
	cmn	w0, #1
	beq	.L2497
	adrp	x1, .LANCHOR2
	str	w0, [x19,4]
	add	x1, x1, :lo12:.LANCHOR2
	mov	w2, 0
	str	w20, [x19,24]
	str	wzr, [x19]
	ldr	x0, [x1,2072]
	ldr	x22, [x1,2120]
	mov	w1, 1
	str	x0, [x19,8]
	mov	x0, x19
	str	x22, [x19,16]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2498
	cmp	w0, 256
	beq	.L2498
	ldr	w0, [x22,8]
	cmp	w0, w20
	beq	.L2497
.L2498:
	adrp	x2, .LANCHOR4
	mov	w1, w20
	add	x2, x2, :lo12:.LANCHOR4
	add	x2, x2, 160
	ldr	x7, [x2,8]
	ldr	x6, [x2,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	adrp	x0, .LC142
	add	x0, x0, :lo12:.LC142
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x2,4]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
.L2497:
	add	w20, w20, 1
	b	.L2494
.L2501:
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	lsr	x0, x1, 10
	stp	x21, x22, [sp,32]
	str	w1, [x29,76]
	str	x23, [sp,48]
	bl	P2V_block_in_plane
	adrp	x2, .LANCHOR2
	uxth	w0, w0
	ubfiz	x22, x0, 1, 16
	add	x3, x2, :lo12:.LANCHOR2
	mov	x21, x2
	ldr	x4, [x2,#:lo12:.LANCHOR2]
	ldrh	w1, [x4,x22]
	cbnz	w1, .L2503
	ldr	x19, [x3,24]
	cbz	x19, .L2504
	ldr	x2, [x3,-16]
	mov	w5, 65535
	ldrh	w4, [x3,32]
	mov	x3, -6148914691236517206
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x3, x19, x19
	mov	w3, 6
	uxth	w19, w19
.L2505:
	cmp	w1, w4
	beq	.L2504
	cmp	w19, w0
	bne	.L2506
	add	x23, x21, :lo12:.LANCHOR2
	mov	w1, w19
	add	x0, x23, 24
	bl	List_remove_node
	ldrh	w0, [x23,32]
	sub	w0, w0, #1
	strh	w0, [x23,32]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x1, [x21,#:lo12:.LANCHOR2]
	ldrh	w0, [x1,x22]
	add	w0, w0, 1
	strh	w0, [x1,x22]
	b	.L2504
.L2506:
	umull	x19, w19, w3
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2504
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2505
.L2503:
	add	w1, w1, 1
	strh	w1, [x4,x22]
.L2504:
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
	beq	.L2673
	ldrh	w0, [x19,2]
	adrp	x23, .LANCHOR0
	str	w0, [x29,188]
	ldrb	w0, [x19,6]
	str	w0, [x29,176]
	add	x0, x23, :lo12:.LANCHOR0
	ldr	w1, [x29,188]
	ldrh	w0, [x0,2024]
	cmp	w0, w1
	bne	.L2514
	strh	wzr, [x19,4]
	b	.L2679
.L2514:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2515:
	cmp	w0, w2
	bne	.L2682
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2515
.L2682:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2517
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L2518
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, w20
	ldrb	w1, [x0,196]
	cbnz	w1, .L2592
	add	x0, x0, 200
	ldrh	w24, [x0,w20,sxtw 1]
	b	.L2592
.L2517:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L2518
	mov	w24, w0
.L2592:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w26, 0
	mov	w8, 65535
	mov	w9, 56
	adrp	x10, .LANCHOR2
	ldrh	w4, [x0,1956]
	mov	w11, 4
	ldrh	w7, [x0,2036]
	add	x4, x19, x4, lsl 1
	b	.L2520
.L2518:
	strh	wzr, [x19,2]
.L2679:
	strb	wzr, [x19,6]
	b	.L2673
.L2522:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2521
	add	x5, x10, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x0, w26, w9
	add	w3, w26, 1
	ldr	x6, [x5,2048]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x5,2048]
	ldr	x5, [x5,888]
	add	x0, x1, x0
	mul	w1, w26, w7
	uxth	w26, w3
	str	xzr, [x0,8]
	sdiv	w1, w1, w11
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L2521:
	add	x2, x2, 2
.L2520:
	cmp	x2, x4
	bne	.L2522
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2523
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2523:
	adrp	x21, .LANCHOR2
	ldr	w2, [x29,184]
	add	x22, x21, :lo12:.LANCHOR2
	mov	w1, w26
	mov	x27, 0
	mov	w25, 65535
	mov	x28, 56
	ldr	x0, [x22,2048]
	bl	FlashReadPages
	ldr	w0, [x22,236]
	adrp	x22, .LC143
	str	w0, [x29,168]
	add	x22, x22, :lo12:.LC143
	uxth	w0, w24
	str	w0, [x29,160]
.L2524:
	uxth	w2, w27
	cmp	w2, w26
	bcs	.L2531
	add	x6, x21, :lo12:.LANCHOR2
	mul	x0, x27, x28
	ldr	x4, [x6,2048]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2525
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2526
	ldr	w1, [x6,236]
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
	cbz	w0, .L2526
	add	w4, w4, 1
	str	w4, [x6,236]
.L2526:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2527
.L2531:
	cmp	w2, w26
	bne	.L2674
	add	x0, x21, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x0, [x0,2048]
	ldr	w0, [x0,4]
	b	.L2675
.L2525:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w25, [x29,160]
	adrp	x0, .LANCHOR5
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LANCHOR5
	strh	w1, [x0,-216]
.L2527:
	add	x27, x27, 1
	b	.L2524
.L2674:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w20, w20
	umull	x2, w2, w1
	ldr	x0, [x0,2048]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2675:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2533
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w2, [x0,196]
	cbnz	w2, .L2533
	add	x0, x0, 200
	ldrh	w20, [x0,w20,sxtw 1]
.L2533:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w0, w20
	bne	.L2534
	strh	w20, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2534:
	ldrh	w0, [x29,176]
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w20, w0
	bne	.L2535
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2680
.L2535:
	ldr	w0, [x29,168]
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w25, w0
	bne	.L2536
	cbnz	w1, .L2537
.L2536:
	add	x0, x21, :lo12:.LANCHOR2
	uxth	w5, w24
	ldr	w1, [x0,2348]
	cmn	w1, #1
	bne	.L2538
	str	w22, [x0,2348]
.L2538:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w25, [x29,188]
	ldr	w6, [x0,2348]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w24, uxth
	bge	.L2539
	sub	w25, w5, #7
	uxth	w25, w25
.L2539:
	mov	w3, -1
	add	x28, x21, :lo12:.LANCHOR2
	mov	w27, w3
	mov	w4, 65535
	mov	w7, 56
	add	x8, x28, 988
.L2540:
	cmp	w25, w5
	bhi	.L2553
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, 0
	ldrh	w2, [x0,1956]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2554:
	cmp	x0, x2
	beq	.L2683
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2541
	umull	x9, w24, w7
	ldr	x10, [x28,2048]
	add	w24, w24, 1
	orr	w1, w25, w1, lsl 10
	add	x9, x10, x9
	uxth	w24, w24
	str	w1, [x9,4]
.L2541:
	add	x0, x0, 2
	b	.L2554
.L2683:
	ldr	x0, [x28,2048]
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
	ldr	x0, [x28,2048]
	ldr	x4, [x29,144]
	ldr	x7, [x29,136]
	add	x0, x0, 16
	ldr	x8, [x29,128]
.L2543:
	cmp	w1, w24
	beq	.L2684
	ldr	w2, [x0,-16]
	cbnz	w2, .L2544
	ldr	x2, [x0]
	ldrh	w11, [x2]
	cmp	w11, w4
	beq	.L2545
	ldr	w2, [x2,4]
	cmn	w2, #1
	beq	.L2545
	ldr	w27, [x28,2348]
	cmn	w3, #1
	str	w2, [x28,2348]
	bne	.L2545
	ldrh	w2, [x8,x10,lsl 1]
	cmp	w2, w4
	bne	.L2546
	cbz	w9, .L2545
.L2546:
	cmp	w27, w22
	csinv	w3, w27, wzr, ne
	b	.L2545
.L2544:
	adrp	x0, .LANCHOR5-216
	ldrh	w1, [x19]
	strh	w1, [x0,#:lo12:.LANCHOR5-216]
	ldrb	w0, [x19,8]
	cbnz	w0, .L2537
	add	x0, x21, :lo12:.LANCHOR2
	add	x1, x0, 988
	ldrh	w2, [x1,w25,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2548
	cmn	w3, #1
	beq	.L2549
	str	w3, [x0,2348]
	b	.L2537
.L2549:
	cmp	w6, w22
	beq	.L2550
	str	w6, [x0,2348]
	b	.L2537
.L2550:
	ldr	w1, [x0,2348]
	b	.L2681
.L2548:
	cmp	w27, w22
	beq	.L2551
	cmn	w27, #1
	beq	.L2537
	str	w27, [x0,2348]
	b	.L2537
.L2551:
	ldr	w1, [x0,2348]
	cmp	w1, w22
	beq	.L2537
.L2681:
	sub	w1, w1, #1
	b	.L2676
.L2545:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2543
.L2684:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2540
.L2553:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, -1
.L2676:
	str	w1, [x0,2348]
.L2537:
	adrp	x0, .LANCHOR5
	str	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR5
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	ldr	w24, [x29,188]
	str	x0, [x29,128]
	adrp	x0, .LC144
	mov	w2, 1
	add	x0, x0, :lo12:.LC144
	strh	w2, [x1,-214]
	str	x0, [x29,120]
.L2555:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x1, x19
	str	wzr, [x29,176]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,1956]
	ldrb	w6, [x0,196]
	add	x3, x19, x3, lsl 1
.L2556:
	cmp	x1, x3
	beq	.L2685
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2557
	ldr	w0, [x29,176]
	add	x5, x21, :lo12:.LANCHOR2
	orr	w2, w24, w2, lsl 10
	ldr	x4, [x5,2048]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2558
	cbz	w6, .L2558
	ldr	x2, [x5,2048]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2558:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2557:
	add	x1, x1, 2
	b	.L2556
.L2685:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,2048]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2560:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2686
	ldr	x0, [x29,160]
	mov	x25, 56
	mul	x25, x0, x25
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x28, [x0,2048]
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
	bcc	.L2561
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2596
	ldr	w2, [x29,188]
	cmp	w24, w2
	beq	.L2561
.L2596:
	cmp	w0, w26
	bne	.L2597
	cmp	w24, w20
	beq	.L2563
.L2597:
	ldr	w0, [x28,x25]
	cmn	w0, #1
	beq	.L2565
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	beq	.L2566
	ldrh	w0, [x19]
	b	.L2677
.L2566:
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2567
	add	x27, x21, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,236]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2567
	add	w0, w22, 1
	str	w0, [x27,236]
.L2567:
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
	ldr	w1, [x3,2348]
	cmn	w1, #1
	beq	.L2568
	mov	w0, w22
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,168]
	cbz	w0, .L2568
	cmn	w28, #1
	beq	.L2569
	ldr	x0, [x3,2048]
	mov	w2, 0
	str	x3, [x29,112]
	add	x0, x0, x25
	str	w28, [x0,4]
	ldr	x1, [x0,16]
	ldr	x0, [x3,2048]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x3, [x29,112]
	ldr	x0, [x29,168]
	ldr	x2, [x3,2048]
	ldr	w0, [x0,4]
	str	w0, [x29,144]
	add	x4, x2, x25
	ldr	w0, [x2,x25]
	cmn	w0, #1
	bne	.L2570
	b	.L2571
.L2569:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2561
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
	b	.L2561
.L2570:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2571
	ldr	w0, [x3,2348]
	ldr	w1, [x29,144]
	str	x2, [x29,96]
	str	x4, [x29,104]
	str	x3, [x29,112]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	ldr	x2, [x29,96]
	cbz	w0, .L2571
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2576
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2571
	cmn	w0, #1
	beq	.L2574
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,2048]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	b	.L2575
.L2574:
	str	w0, [x2,x25]
.L2575:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
	ldr	w1, [x1,x25]
	cmn	w1, #1
	beq	.L2576
	ldr	x1, [x29,168]
	ldr	w0, [x0,2348]
	ldr	w25, [x1,4]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2576
	ldr	w0, [x29,144]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2571
.L2576:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2571:
	mov	w0, -1
	str	w0, [x29,196]
	b	.L2578
.L2568:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2578
	add	x0, x23, :lo12:.LANCHOR0
	ldr	w0, [x0,1972]
	cmp	w0, w28, lsr 10
	bhi	.L2598
	cmn	w28, #1
	bne	.L2561
.L2598:
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w25, [x29,200]
	cmn	w25, #1
	beq	.L2578
	ldr	w0, [x29,196]
	cmp	w25, w0
	beq	.L2578
	lsr	x0, x25, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x1, x21, :lo12:.LANCHOR2
	ldrh	w2, [x1,40]
	cmp	w2, w0
	beq	.L2582
	ldrh	w2, [x1,88]
	cmp	w2, w0
	beq	.L2582
	ldrh	w1, [x1,136]
	cmp	w1, w0
	bne	.L2578
.L2582:
	add	x28, x21, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, 0
	ldr	x0, [x28,2048]
	str	w25, [x0,4]
	ldr	x3, [x0,16]
	ldr	x0, [x28,2048]
	str	x3, [x29,168]
	bl	FlashReadPages
	ldr	x0, [x28,2048]
	ldr	x3, [x29,168]
	ldr	w0, [x0]
	ldr	w1, [x3,4]
	cmn	w0, #1
	beq	.L2578
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2578
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2578:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2561
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x2, x1, 1, 16
	ldr	x3, [x21,#:lo12:.LANCHOR2]
	ldrh	w2, [x3,x2]
	cbz	w2, .L2583
.L2677:
	bl	decrement_vpc_count
	b	.L2561
.L2583:
	ldr	x0, [x29,120]
	bl	printk
	b	.L2561
.L2565:
	ldr	x2, [x29,136]
	ldrh	w0, [x19]
	add	x25, x2, :lo12:.LANCHOR5
	mov	w2, w22
	strh	w0, [x25,-216]
	ldr	x0, [x29,128]
	bl	printk
	ldr	w0, [x25,-212]
	cmp	w0, 31
	bhi	.L2584
	sub	x1, x25, #208
	ldr	w2, [x29,204]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x25,-212]
.L2584:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,2348]
	cmn	w1, #1
	beq	.L2678
	cmp	w1, w22
	bls	.L2561
.L2678:
	str	w22, [x0,2348]
.L2561:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2560
.L2686:
	ldrb	w0, [x19,8]
	add	w24, w24, 1
	cmp	w0, 1
	uxth	w24, w24
	bne	.L2587
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,196]
	cbz	w1, .L2587
	cmp	w20, w24
	bne	.L2587
	ldrh	w0, [x0,2026]
	cmp	w0, w24
	beq	.L2563
.L2587:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2024]
	cmp	w24, w1
	bne	.L2555
	ldrh	w2, [x0,1956]
	mov	w3, 65535
	strh	w24, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2589:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2673
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2589
	strb	w1, [x19,6]
	b	.L2673
.L2563:
	strb	w26, [x19,6]
	strh	w20, [x19,2]
.L2680:
	mov	x0, x19
	mov	w1, w20
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2673:
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
	adrp	x0, .LC106
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 96
	add	x0, x0, :lo12:.LC106
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x22, .LANCHOR0
	bl	printk
	add	x1, x22, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	mov	w19, 0
	ldrh	w2, [x1,1966]
	mov	w1, 0
	ldr	x0, [x0,2168]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2688:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2096]
	cmp	w19, w0
	bcs	.L2704
	mov	w0, w19
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2689
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x2, [x1,2168]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2689:
	add	w19, w19, 1
	b	.L2688
.L2704:
	adrp	x23, .LC146
	mov	w19, 0
	add	x20, x20, :lo12:.LANCHOR2
	mov	w24, 65535
	add	x23, x23, :lo12:.LC146
.L2691:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,1964]
	cmp	w0, w19
	bls	.L2705
	ubfiz	x21, x19, 1, 16
	ldr	x0, [x20]
	ldrh	w2, [x0,x21]
	ldr	x0, [x20,2168]
	ldrh	w3, [x0,x21]
	cmp	w2, w3
	beq	.L2694
	cmp	w2, w24
	beq	.L2694
	mov	x0, x23
	mov	w1, w19
	bl	printk
	ldrh	w0, [x20,40]
	cmp	w0, w19
	beq	.L2694
	ldrh	w0, [x20,136]
	cmp	w0, w19
	beq	.L2694
	ldrh	w0, [x20,88]
	cmp	w0, w19
	beq	.L2694
	ldr	x0, [x20]
	ldrh	w1, [x0,x21]
	cbnz	w1, .L2696
	ldr	x1, [x20,2168]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	b	.L2694
.L2696:
	ldr	x1, [x20,2168]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	mov	w0, w19
	bl	update_vpc_list
.L2694:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2691
.L2705:
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
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR1
	mov	x19, x0
	add	x0, x20, :lo12:.LANCHOR1
	str	w1, [x29,164]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	x20, [x29,152]
	ldrh	w26, [x0,3296]
	mov	w0, 65535
	cmp	w26, w0
	beq	.L2736
	cbnz	w26, .L2707
	b	.L2708
.L2736:
	mov	w26, 0
.L2707:
	adrp	x0, .LANCHOR0+2024
	ldr	w1, [x29,164]
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2024]
	cmp	w1, w0
	bne	.L2709
.L2708:
	bl	FtlGcPageVarInit
.L2709:
	mov	w0, -1
	adrp	x25, .LANCHOR0
	str	w0, [x29,168]
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	mov	w22, 0
	mov	x24, x25
	str	x0, [x29,144]
.L2710:
	ldrh	w0, [x19]
	mov	w21, 65535
	strb	wzr, [x19,8]
	cmp	w0, w21
	beq	.L2711
.L2712:
.L2733:
	add	x0, x25, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w20, 0
	mov	w10, 56
	adrp	x11, .LANCHOR2
	mov	w7, 4
	ldrh	w6, [x0,1956]
	ldrh	w8, [x0,2034]
	ldrh	w9, [x0,2036]
	add	x6, x19, x6, lsl 1
.L2713:
	cmp	x2, x6
	beq	.L2753
	ldrh	w1, [x2,16]
	cmp	w1, w21
	beq	.L2714
	add	x4, x11, :lo12:.LANCHOR2
	orr	w1, w26, w1, lsl 10
	umull	x0, w20, w10
	add	w5, w20, 1
	ldr	x12, [x4,2048]
	add	x12, x12, x0
	str	w1, [x12,4]
	ldr	x1, [x4,2048]
	ldr	x12, [x4,880]
	add	x0, x1, x0
	ldr	x4, [x4,888]
	mul	w1, w20, w8
	sdiv	w1, w1, w7
	add	x1, x12, x1, sxtw 2
	str	x1, [x0,8]
	mul	w1, w20, w9
	uxth	w20, w5
	sdiv	w1, w1, w7
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L2714:
	add	x2, x2, 2
	b	.L2713
.L2753:
	adrp	x28, .LANCHOR2
	mov	w1, w20
	add	x0, x28, :lo12:.LANCHOR2
	mov	w2, 0
	mov	x23, 0
	mov	x27, x28
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L2716:
	cmp	w20, w23, uxth
	bls	.L2754
	add	x8, x27, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x28, x23, x0
	ldr	x0, [x8,2048]
	add	x1, x0, x28
	ldr	w10, [x0,x28]
	ldr	w2, [x1,4]
	str	w2, [x29,172]
	ldr	x7, [x1,16]
	cbnz	w10, .L2717
	ldrh	w0, [x7]
	cmp	w0, w21
	beq	.L2752
	add	x9, x24, :lo12:.LANCHOR0
	ldr	w0, [x7,8]
	ldr	w1, [x9,2096]
	cmp	w0, w1
	bhi	.L2752
	ldrb	w1, [x9,72]
	cbz	w1, .L2722
	add	x1, x29, 180
	mov	w2, w10
	str	x7, [x29,136]
	str	x9, [x29,104]
	str	x8, [x29,112]
	str	x10, [x29,120]
	bl	log2phys
	ldr	x7, [x29,136]
	ldr	w1, [x29,180]
	ldr	w0, [x7,12]
	cmp	w1, w0
	bne	.L2722
	cmn	w1, #1
	beq	.L2722
	ldr	x8, [x29,112]
	mov	w1, 1
	str	w0, [x29,188]
	ldr	x10, [x29,120]
	ldr	x0, [x8,2112]
	str	x0, [x29,192]
	mov	w2, w10
	ldr	x0, [x8,2128]
	str	x0, [x29,200]
	add	x0, x29, 184
	str	x7, [x29,128]
	str	x8, [x29,136]
	bl	FlashReadPages
	ldr	x8, [x29,136]
	ldr	x9, [x29,104]
	ldr	x7, [x29,128]
	ldr	x0, [x8,2048]
	ldrh	w2, [x9,2030]
	ldr	x8, [x29,192]
	add	x6, x0, x28
	lsl	w2, w2, 7
	mov	x0, 0
.L2724:
	cmp	w2, w0
	bls	.L2722
	lsl	x1, x0, 2
	ldr	x9, [x6,8]
	add	x0, x0, 1
	ldr	w9, [x9,x1]
	ldr	w1, [x8,x1]
	cmp	w9, w1
	beq	.L2724
	adrp	x0, .LC147
	ldrh	w1, [x19]
	ldr	w2, [x29,188]
	add	x0, x0, :lo12:.LC147
	bl	printk
.L2752:
	ldr	x0, [x27,#:lo12:.LANCHOR2]
	add	x21, x27, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x21,280]
	b	.L2751
.L2722:
	ldr	w0, [x7,12]
	add	x23, x23, 1
	ldr	w1, [x29,172]
	ldr	w2, [x7,8]
	bl	FtlGcUpdatePage
	b	.L2716
.L2717:
	ldrh	w1, [x19]
	ldr	x0, [x29,144]
	ldr	w2, [x29,172]
	bl	printk
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	w1, [x0,1948]
	cbnz	w1, .L2727
	ldrb	w0, [x0,196]
	cbz	w0, .L2728
.L2727:
	add	x0, x27, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,-80]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 159
	bls	.L2729
.L2728:
	add	x0, x27, :lo12:.LANCHOR2
	ldr	x0, [x0,2048]
	ldr	w0, [x0,x28]
	cmn	w0, #1
	bne	.L2730
.L2729:
	add	x0, x27, :lo12:.LANCHOR2
	ldr	x0, [x0,2048]
	add	x6, x0, x28
	ldr	w0, [x6,4]
	str	w0, [x29,168]
.L2730:
	ldr	x0, [x27,#:lo12:.LANCHOR2]
	ldrh	w1, [x19]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
.L2751:
	bl	FtlGcPageVarInit
	mov	w26, 0
	b	.L2710
.L2754:
	ldr	w0, [x29,164]
	add	w22, w22, 1
	add	w3, w26, 1
	cmp	w22, w0
	uxth	w26, w3
	bcs	.L2732
.L2734:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w0, w26
	bhi	.L2733
	b	.L2711
.L2732:
	ldr	x0, [x29,152]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3296]
	cmp	w0, w21
	beq	.L2734
	add	w0, w0, w22
	strh	w0, [x1,3296]
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w0, w26
	bls	.L2734
	b	.L2735
.L2711:
	ldr	x0, [x29,152]
	mov	w1, w26
	strh	w26, [x19,2]
	mov	w2, 0
	add	x20, x0, :lo12:.LANCHOR1
	mov	w0, -1
	strb	wzr, [x19,6]
	strh	w0, [x20,3296]
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
.L2735:
	ldr	w0, [x29,168]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 240
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
	add	x3, x1, 336
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR0
	ldr	w0, [x3,80]
	cbz	w0, .L2756
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x3,84]
	ldr	w0, [x0,2096]
	cmp	w2, w0
	bcs	.L2757
	mov	w21, 2048
	mov	x19, x1
.L2762:
	add	x22, x19, 336
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x22,84]
	ldr	w1, [x1,2096]
	cmp	w0, w1
	bcs	.L2761
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x22,84]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x22,84]
	cmn	w1, #1
	beq	.L2760
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
	bne	.L2761
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2761:
	mov	w0, -1
	b	.L2764
.L2760:
	subs	w21, w21, #1
	bne	.L2762
	b	.L2761
.L2757:
	ldr	w0, [x1,200]
	str	wzr, [x3,80]
	str	wzr, [x3,84]
	str	w0, [x3,76]
	b	.L2771
.L2756:
	ldr	w4, [x1,252]
	mov	w6, 10000
	mov	w8, 63
	ldr	w7, [x1,200]
	cmp	w4, w6
	mov	w6, 31
	csel	w8, w6, w8, hi
	ldr	w6, [x3,76]
	add	x20, x20, :lo12:.LANCHOR0
	add	w9, w7, 1048576
	cmp	w6, w9
	ldr	w5, [x20,2096]
	bhi	.L2766
	mov	w9, 1000
	lsr	w4, w4, 10
	add	w4, w4, 1
	mul	w5, w5, w9
	udiv	w4, w5, w4
	add	w4, w4, w6
	cmp	w4, w7
	bcc	.L2766
	ldrh	w1, [x1,-36]
	tst	w8, w1
	bne	.L2764
	ldr	w3, [x3,100]
	cmp	w3, w1
	beq	.L2764
.L2766:
	add	x0, x2, :lo12:.LANCHOR2
	add	x1, x0, 336
	ldrh	w0, [x0,-36]
	str	w0, [x1,100]
	mov	w0, 1
	str	wzr, [x1,84]
	str	w0, [x1,80]
	str	w7, [x1,76]
.L2771:
	mov	w0, 0
.L2764:
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
	ldr	w3, [x2,-96]
	ldrh	w1, [x1,2024]
	cbnz	w3, .L2808
	ldrh	w20, [x2,136]
	mov	w4, 65535
	cmp	w20, w4
	bne	.L2775
.L2784:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x22, x20, 136
	ldrh	w1, [x20,136]
	str	wzr, [x20,2344]
	cmp	w1, w0
	beq	.L2808
	bl	FtlCacheWriteBack
	mov	w26, 12
	adrp	x0, .LANCHOR2
	add	x3, x21, :lo12:.LANCHOR0
	ldrh	w2, [x20,136]
	ldr	x1, [x0,#:lo12:.LANCHOR2]
	ldrh	w3, [x3,2024]
	ldrb	w0, [x22,7]
	mov	w22, 0
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldr	w0, [x20,220]
	ldrh	w1, [x20,920]
	add	w0, w1, w0
	str	w0, [x20,220]
	b	.L2785
.L2775:
	cbz	w0, .L2778
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3296]
	cmp	w5, w4
	beq	.L2779
.L2780:
	mov	w1, 2
	b	.L2778
.L2779:
	strh	w3, [x0,3296]
	ldrh	w0, [x2,32]
	cmp	w0, 17
	bhi	.L2780
.L2778:
	add	x22, x19, :lo12:.LANCHOR2
	add	x0, x22, 136
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L2781
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x22,-80]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L2782
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2782:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2344]
	cbnz	w1, .L2783
	ldr	w1, [x0,432]
	add	w1, w1, 1
	str	w1, [x0,432]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2783:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	str	wzr, [x19,2344]
	b	.L2774
.L2781:
	adrp	x0, .LANCHOR1+3296
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3296]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2774
	b	.L2784
.L2788:
	ldr	w1, [x23,4]
	cmp	w0, w1
	bne	.L2806
.L2787:
	add	w22, w22, 1
	uxth	w22, w22
.L2785:
	ldrh	w0, [x20,920]
	cmp	w0, w22
	bls	.L2809
	umull	x24, w22, w26
	ldr	x25, [x20,928]
	add	x1, x21, :lo12:.LANCHOR0
	add	x23, x25, x24
	ldr	w1, [x1,2096]
	ldr	w0, [x23,8]
	cmp	w0, w1
	bcs	.L2806
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	ldr	w1, [x25,x24]
	cmp	w0, w1
	bne	.L2788
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
	b	.L2807
.L2806:
	ldrh	w0, [x20,136]
.L2807:
	bl	decrement_vpc_count
	b	.L2787
.L2809:
	mov	w0, -1
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L2790
	adrp	x0, .LC149
	ldrh	w1, [x20,136]
	add	x0, x0, :lo12:.LC149
	bl	printk
.L2790:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,136]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2,x1]
	cbz	w1, .L2791
	bl	INSERT_DATA_LIST
	b	.L2792
.L2791:
	bl	INSERT_FREE_LIST
.L2792:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w22, -1
	strh	wzr, [x20,920]
	strh	w22, [x20,136]
	strh	wzr, [x20,908]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w22, [x20,280]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,1948]
	cbz	w0, .L2793
	ldr	w0, [x20,256]
	cmp	w0, 39
	bhi	.L2793
	ldrh	w0, [x20,260]
	ldrh	w1, [x20,32]
	cmp	w1, w0
	bcs	.L2808
	lsl	w0, w0, 1
	strh	w0, [x20,848]
.L2808:
	mov	w0, 0
	b	.L2774
.L2793:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,260]
	ldrh	w0, [x19,32]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L2808
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,196]
	cbz	w0, .L2794
	sub	w1, w1, #2
	strh	w1, [x19,848]
	b	.L2808
.L2794:
	mov	w1, 20
	strh	w1, [x19,848]
.L2774:
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
	add	x21, x19, 136
	mov	x0, x21
	ldrh	w1, [x20,2024]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,138]
	ldrh	w0, [x20,2024]
	cmp	w1, w0
	bcc	.L2810
	add	x0, x19, 2256
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19,2344]
.L2810:
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
	adrp	x0, .LANCHOR5-212
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR5-212]
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x19, 40
	add	x19, x19, 88
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
	strh	w1, [x0,-216]
	strh	wzr, [x0,-214]
	ldrh	w0, [x24,1960]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x23,264]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2814
.L2816:
	mov	w22, -1
	b	.L2815
.L2814:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L2816
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x24,2062]
	mov	x1, 0
	ldr	x0, [x23,184]
.L2817:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2821
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2817
.L2821:
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w3, w2
	ldrh	w0, [x1,-36]
	add	w0, w0, 1
	strh	w0, [x1,-36]
	blt	.L2818
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w0, [x21,-214]
	cbz	w0, .L2822
.L2818:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x4, [x19,#:lo12:.LANCHOR2]
	ldrh	w2, [x1,40]
	ldrh	w5, [x1,44]
	lsl	x2, x2, 1
	ldrh	w3, [x4,x2]
	sub	w3, w3, w5
	strh	w3, [x4,x2]
	add	x4, x20, :lo12:.LANCHOR0
	strb	wzr, [x1,46]
	ldrh	w0, [x1,88]
	ldr	x5, [x19,#:lo12:.LANCHOR2]
	ldrh	w2, [x4,2024]
	lsl	x0, x0, 1
	strh	w2, [x1,42]
	strh	wzr, [x1,44]
	ldrh	w6, [x1,92]
	ldrh	w3, [x5,x0]
	sub	w3, w3, w6
	strh	w3, [x5,x0]
	strb	wzr, [x1,94]
	ldrh	w0, [x4,2024]
	strh	w0, [x1,90]
	ldrh	w0, [x1,-34]
	strh	wzr, [x1,92]
	add	w0, w0, 1
	strh	w0, [x1,-34]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2822:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x24, x21, 40
	ldrh	w0, [x21,40]
	cmp	w0, w1
	beq	.L2823
	ldrh	w1, [x21,44]
	cbnz	w1, .L2823
	ldrh	w1, [x21,92]
	add	x23, x21, 88
	cbnz	w1, .L2823
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x21,88]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x24
	bl	allocate_new_data_superblock
	mov	x0, x23
	bl	allocate_new_data_superblock
.L2823:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,72]
	cbnz	w0, .L2824
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,-36]
	and	w0, w0, 31
	cbnz	w0, .L2815
.L2824:
	bl	FtlVpcCheckAndModify
.L2815:
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
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	ldr	w22, [x19,-96]
	cbnz	w22, .L2832
	adrp	x20, .LANCHOR0
	ldr	x0, [x19,2224]
	add	x23, x20, :lo12:.LANCHOR0
	mov	w1, w22
	ldrh	w2, [x23,2060]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x23,2060]
	mov	w1, w22
	ldr	x0, [x19,2216]
	lsl	w2, w2, 2
	bl	ftl_memset
	str	w22, [x19,232]
	ldrh	w0, [x23,1960]
	str	w22, [x19,236]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2833
	bl	FtlMakeBbt
.L2833:
	mov	w0, 0
.L2834:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2030]
	cmp	w0, w2, lsl 7
	bge	.L2861
	add	x3, x21, :lo12:.LANCHOR2
	ubfiz	x2, x0, 2, 16
	mvn	w1, w0
	ldr	x4, [x3,2104]
	orr	w1, w0, w1, lsl 16
	add	w0, w0, 1
	str	w1, [x4,x2]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	uxth	w0, w0
	ldr	x3, [x3,2112]
	str	w1, [x3,x2]
	b	.L2834
.L2861:
	ldrh	w22, [x1,1964]
	mov	w19, 0
.L2836:
	add	x23, x20, :lo12:.LANCHOR0
	ldrh	w0, [x23,1966]
	cmp	w0, w22
	bls	.L2862
	mov	w0, w22
	mov	w1, 1
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w19, w0
	uxth	w22, w22
	uxth	w19, w0
	b	.L2836
.L2862:
	ldrh	w0, [x23,1956]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L2838
.L2842:
	mov	w19, 0
	mov	w22, w19
	b	.L2839
.L2838:
	udiv	w19, w19, w0
	ldr	w0, [x23,2056]
	add	w0, w19, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x23,1960]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x23,1964]
.L2840:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,1966]
	cmp	w0, w19
	bls	.L2842
	mov	w0, w19
	mov	w1, 1
	add	w19, w19, 1
	bl	FtlLowFormatEraseBlock
	uxth	w19, w19
	b	.L2840
.L2839:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,1964]
	cmp	w1, w22
	bls	.L2863
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w19, w0
	uxth	w22, w22
	uxth	w19, w0
	b	.L2839
.L2863:
	add	x4, x21, :lo12:.LANCHOR2
	ldrh	w1, [x0,1966]
	ldrh	w2, [x0,1956]
	str	w1, [x4,2036]
	ldr	w1, [x0,1968]
	udiv	w3, w1, w2
	str	w3, [x0,2096]
	ubfx	x6, x3, 5, 16
	add	w5, w6, 36
	strh	w5, [x4,260]
	mov	w5, 24
	mul	w5, w2, w5
	cmp	w19, w5
	ble	.L2844
	sub	w1, w1, w19
	udiv	w1, w1, w2
	str	w1, [x0,2096]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x4,260]
.L2844:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1948]
	cmp	w0, 1
	bne	.L2845
	add	x1, x21, :lo12:.LANCHOR2
	udiv	w0, w19, w2
	ldrh	w4, [x1,260]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,260]
.L2845:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L2846
	add	x1, x21, :lo12:.LANCHOR2
	udiv	w0, w19, w2
	ldrh	w4, [x1,260]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,260]
.L2846:
	add	x5, x20, :lo12:.LANCHOR0
	ldrh	w1, [x5,2018]
	cbz	w1, .L2848
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w4, [x0,260]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,260]
	mul	w4, w1, w2
	cmp	w4, w19
	ble	.L2848
	add	w1, w1, 32
	str	w3, [x5,2096]
	add	w1, w6, w1
	strh	w1, [x0,260]
.L2848:
	add	x19, x21, :lo12:.LANCHOR2
	add	x22, x20, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR4
	mov	w23, -1
	ldr	w1, [x22,2096]
	ldrh	w0, [x19,260]
	sub	w0, w1, w0
	add	x1, x24, :lo12:.LANCHOR4
	mul	w0, w0, w2
	str	w0, [x1,216]
	ldrh	w1, [x22,2024]
	mul	w0, w0, w1
	ldrh	w1, [x22,2030]
	str	w0, [x22,2096]
	mul	w0, w0, w1
	str	w0, [x22,2064]
	bl	FtlBbmTblFlush
	ldrh	w2, [x22,1966]
	mov	w1, 0
	ldr	x0, [x21,#:lo12:.LANCHOR2]
	add	x21, x19, 40
	lsl	w2, w2, 1
	bl	ftl_memset
	str	wzr, [x19,196]
	ldrh	w2, [x22,1964]
	mov	w0, 1
	strb	w0, [x21,8]
	mov	w1, 255
	ldr	x0, [x20,#:lo12:.LANCHOR0]
	lsr	w2, w2, 3
	mov	x22, x24
	strh	w23, [x19,280]
	strh	wzr, [x19,282]
	strb	wzr, [x19,286]
	strb	wzr, [x19,288]
	strh	wzr, [x21,2]
	strb	wzr, [x21,6]
	strh	wzr, [x19,40]
	bl	ftl_memset
.L2850:
	mov	x0, x21
	bl	make_superblock
	ldrb	w0, [x21,7]
	cbnz	w0, .L2851
	ldrh	w1, [x21]
	ldr	x0, [x19]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L2850
.L2851:
	ldr	w0, [x19,232]
	mov	x20, x19
	str	w0, [x21,12]
	add	w0, w0, 1
	ldrh	w1, [x21]
	str	w0, [x19,232]
	ldr	x0, [x20],88
	ldrh	w2, [x21,4]
	strh	w2, [x0,x1,lsl 1]
	strh	wzr, [x20,2]
	ldrh	w0, [x21]
	mov	w21, -1
	strb	wzr, [x20,6]
	add	w0, w0, 1
	strh	w0, [x19,88]
	mov	w0, 1
	strb	w0, [x20,8]
.L2852:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2853
	ldrh	w1, [x20]
	ldr	x0, [x19]
	strh	w21, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2852
.L2853:
	ldr	w0, [x19,232]
	add	x22, x22, :lo12:.LANCHOR4
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,232]
	ldr	x0, [x19]
	ldrh	w2, [x20,4]
	mov	w20, -1
	strh	w2, [x0,x1,lsl 1]
	strh	w20, [x19,136]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19,264]
	ldr	w0, [x22,216]
	strh	w0, [x19,270]
	ldr	w0, [x19,232]
	str	w0, [x19,272]
	add	w0, w0, 1
	strh	wzr, [x19,266]
	strh	w20, [x19,268]
	str	w0, [x19,232]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L2832
	adrp	x0, .LANCHOR1+504
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+504]
.L2832:
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
	cbz	w0, .L2865
.L2867:
	mov	w0, 0
	b	.L2866
.L2865:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,48]
	bl	FlashInit
	cbnz	w0, .L2867
	bl	FlashLoadFactorBbt
	cbz	w0, .L2868
	bl	FlashMakeFactorBbt
.L2868:
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20,104]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L2869
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L2871:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L2871
	cmp	w0, 6
	bhi	.L2872
	add	x0, x19, :lo12:.LANCHOR0
	b	.L2894
.L2872:
	mov	w1, 0
	mov	w4, 1
.L2875:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L2875
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L2894
	mov	w1, 36
.L2894:
	strb	w1, [x0,73]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,73]
	strh	w1, [x0,194]
.L2869:
	adrp	x0, .LC75
	adrp	x1, .LC76
	add	x1, x1, :lo12:.LC76
	add	x0, x0, :lo12:.LC75
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	add	x0, x19, 168
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,1960]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L2877:
	bl	FtlLoadBbt
	cbz	w0, .L2878
.L2896:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L2897
	add	w19, w19, 1
	b	.L2877
.L2897:
	mov	w0, -1
	b	.L2866
.L2878:
	bl	FtlSysBlkInit
	cbnz	w0, .L2896
	adrp	x1, .LANCHOR1+504
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+504]
.L2866:
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
	ldr	w1, [x0,-96]
	cbz	w1, .L2899
.L2902:
	mov	w0, 0
	b	.L2900
.L2899:
	adrp	x1, .LANCHOR0+196
	ldrb	w1, [x1,#:lo12:.LANCHOR0+196]
	cbz	w1, .L2901
	ldr	w1, [x0,864]
	tbz	x1, 0, .L2901
	ldrh	w0, [x0,140]
	cbnz	w0, .L2902
.L2901:
	add	x1, x21, :lo12:.LANCHOR2
	mov	w2, 0
	mov	w3, w2
	mov	w20, 0
	mov	w22, 56
	ldr	x0, [x1,2056]
	ldr	w1, [x1,864]
	bl	FlashProgPages
.L2903:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w1, [x19,864]
	cmp	w20, w1
	bcs	.L2913
	umull	x0, w20, w22
	ldr	x3, [x19,2056]
	add	x1, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	ldr	x2, [x1,16]
	bne	.L2904
	ldrh	w2, [x19,136]
	ldr	x1, [x19]
	strh	wzr, [x1,x2,lsl 1]
	strh	w3, [x19,136]
	ldr	w1, [x19,432]
	add	w1, w1, 1
	str	w1, [x19,432]
	ldr	x1, [x19,2056]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2912
.L2904:
	ldr	w0, [x2,12]
	add	w20, w20, 1
	ldr	w1, [x1,4]
	ldr	w2, [x2,8]
	uxth	w20, w20
	bl	FtlGcUpdatePage
	b	.L2903
.L2913:
	ldr	x0, [x19,2056]
	bl	FtlGcBufFree
	str	wzr, [x19,864]
	ldrh	w0, [x19,140]
	cbnz	w0, .L2902
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L2912:
	mov	w0, 1
.L2900:
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
	add	x1, x0, 136
	ldrh	w2, [x0,136]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2915
	ldrh	w0, [x1,4]
	cbnz	w0, .L2916
.L2915:
	bl	FtlCacheWriteBack
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	strb	wzr, [x20,144]
	add	x0, x20, 136
	bl	allocate_data_superblock
	strh	wzr, [x20,908]
	strh	wzr, [x20,920]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2916:
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 136
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
	ldr	w4, [x3,-96]
	cbnz	w4, .L3070
	adrp	x4, .LANCHOR1
	add	x4, x4, :lo12:.LANCHOR1
	ldr	w5, [x4,504]
	cmn	w5, #1
	beq	.L3070
	ldr	w5, [x3,2024]
	cbnz	w5, .L3070
	ldrh	w5, [x3,16]
	cmp	w5, 47
	bls	.L3070
	mov	w22, w1
	ldrh	w1, [x4,3296]
	str	w0, [x29,124]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2919
.L2922:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0,938]
	cmp	w4, w2
	bne	.L2920
	b	.L2921
.L2919:
	ldrh	w1, [x3,136]
	cmp	w1, w0
	beq	.L2922
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2922
	b	.L3070
.L2920:
	ldrh	w1, [x0,936]
	cmp	w1, w2
	bne	.L2921
	ldrh	w3, [x0,940]
	cmp	w3, w1
	beq	.L2921
	ldrh	w2, [x0,942]
	cmp	w2, w1
	beq	.L2921
	mov	w1, -1
	strh	w4, [x0,936]
	strh	w3, [x0,938]
	strh	w2, [x0,940]
	strh	w1, [x0,942]
.L2921:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x29,124]
	cmp	w2, 1
	ldr	w0, [x1,852]
	add	w0, w0, 1
	add	w0, w0, w2, lsl 7
	str	w0, [x1,852]
	bne	.L2933
	adrp	x25, .LANCHOR0
	add	x1, x25, :lo12:.LANCHOR0
	ldr	w2, [x1,1948]
	cbnz	w2, .L2924
	ldrb	w1, [x1,196]
	cbz	w1, .L2933
.L2924:
	add	x20, x19, :lo12:.LANCHOR2
	ldr	w1, [x20,256]
	cmp	w1, 39
	bhi	.L2933
	adrp	x21, .LANCHOR5
	add	x1, x21, :lo12:.LANCHOR5
	ldrh	w1, [x1,-80]
	add	w0, w0, w1
	str	w0, [x20,852]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x20,280]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2933
	ldrh	w1, [x20,936]
	cmp	w1, w0
	bne	.L2933
	ldr	w0, [x20,852]
	cmp	w0, 1024
	bhi	.L2925
	ldrh	w0, [x20,32]
	cmp	w0, 63
	bhi	.L2933
.L2925:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR5
	ldrh	w2, [x0,850]
	ldrh	w3, [x0,32]
	add	w2, w2, 64
	strh	wzr, [x1,-80]
	cmp	w3, w2
	bgt	.L2933
	str	wzr, [x0,852]
	ldr	w0, [x0,256]
	cbnz	w0, .L2926
	mov	w0, 6
	b	.L3092
.L2926:
	cmp	w0, 5
	bhi	.L2927
	mov	w0, 18
.L3092:
	strh	w0, [x1,-80]
.L2927:
	mov	w0, 32
	mov	w24, 65535
	bl	List_get_gc_head_node
	uxth	w26, w0
	cmp	w26, w24
	beq	.L2932
	add	x23, x19, :lo12:.LANCHOR2
	adrp	x27, .LANCHOR2
	ldrh	w0, [x23,856]
	cbz	w0, .L2929
	add	x25, x25, :lo12:.LANCHOR0
	ldr	x28, [x27,#:lo12:.LANCHOR2]
	ubfiz	x26, x26, 1, 16
	ldrh	w1, [x25,2026]
	ldrh	w4, [x25,1956]
	ldrh	w2, [x28,x26]
	mul	w1, w1, w4
	add	w1, w1, 1
	cmp	w2, w1
	bgt	.L2932
	add	w1, w0, 1
	str	wzr, [x23,860]
	uxth	w1, w1
	strh	w1, [x23,856]
	str	x1, [x29,112]
	bl	List_get_gc_head_node
	uxth	w20, w0
	cmp	w20, w24
	ldr	x1, [x29,112]
	beq	.L2932
	ubfiz	x25, x20, 1, 16
	adrp	x0, .LC150
	ldrh	w4, [x28,x26]
	add	x0, x0, :lo12:.LC150
	mov	w2, w20
	ldrh	w3, [x28,x25]
	bl	printk
	ldrh	w0, [x23,856]
	cmp	w0, 40
	bls	.L2930
	ldr	x0, [x27,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x25]
	cmp	w0, 32
	bls	.L2930
	strh	wzr, [x23,856]
.L2930:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w0, 6
	strh	w0, [x21,-80]
	b	.L2934
.L2929:
	mov	w0, 1
	strh	w0, [x23,856]
.L2932:
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2934
	add	x21, x21, :lo12:.LANCHOR5
	strh	wzr, [x21,-80]
.L2933:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w20, [x21,280]
	cmp	w20, w0
	bne	.L3096
	ldrh	w0, [x21,136]
	cmp	w0, w20
	beq	.L3097
.L2934:
	mov	w0, 65535
	cmp	w20, w0
	cset	w2, eq
	cbz	w2, .L2947
	ldr	w0, [x29,124]
	cbnz	w0, .L2947
	add	x0, x19, :lo12:.LANCHOR2
	mov	w21, 1
	ldrh	w1, [x0,32]
	cmp	w1, 24
	bhi	.L2948
	adrp	x0, .LANCHOR0
	cmp	w1, 16
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w21, [x0,2024]
	bls	.L2949
	lsr	w21, w21, 5
	b	.L2948
.L2949:
	cmp	w1, 12
	bls	.L2950
	lsr	w21, w21, 4
	b	.L2948
.L2950:
	cmp	w1, 8
	bls	.L2948
	lsr	w21, w21, 2
.L2948:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,848]
	cmp	w2, w1
	bcs	.L2952
	ldrh	w1, [x0,136]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L2953
	ldrh	w2, [x0,936]
	cmp	w2, w1
	bne	.L2953
	adrp	x1, .LANCHOR5-80
	ldrh	w2, [x1,#:lo12:.LANCHOR5-80]
	cbnz	w2, .L2954
	adrp	x1, .LANCHOR0+2096
	ldr	w3, [x0,196]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2096]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2955
.L2954:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,260]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,848]
	b	.L2956
.L2955:
	mov	w1, 18
	strh	w1, [x0,848]
.L2956:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,860]
	b	.L3070
.L2953:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,260]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,848]
.L2952:
	cmp	w22, 2
	bhi	.L3024
	adrp	x0, .LANCHOR0+1948
	ldr	w0, [x0,#:lo12:.LANCHOR0+1948]
	cbz	w0, .L3024
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3024
.L2947:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w3, 65535
	ldrh	w1, [x0,136]
	cmp	w1, w3
	bne	.L2959
	cbz	w2, .L2959
	ldrh	w2, [x0,936]
	cmp	w2, w1
	bne	.L2959
	ldrh	w2, [x0,280]
	cmp	w2, w1
	beq	.L2960
.L2965:
	mov	w20, 65535
	b	.L2959
.L2960:
	ldrh	w2, [x0,32]
	adrp	x20, .LANCHOR5
	ldrh	w1, [x0,848]
	str	wzr, [x0,860]
	cmp	w2, w1
	bls	.L2961
	add	x1, x20, :lo12:.LANCHOR5
	ldrh	w1, [x1,-80]
	cbnz	w1, .L2962
	adrp	x1, .LANCHOR0+2096
	ldr	w2, [x0,196]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2096]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L2963
.L2962:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,260]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,848]
	b	.L2964
.L2963:
	mov	w1, 18
	strh	w1, [x0,848]
.L2964:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x1,x0]
	cmp	w0, 4
	bls	.L2961
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w2, [x20,-80]
	b	.L3070
.L2961:
	add	x21, x20, :lo12:.LANCHOR5
	ldrh	w0, [x21,-80]
	cbnz	w0, .L2965
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w20, [x22,260]
	add	w1, w20, w20, lsl 1
	asr	w1, w1, 2
	strh	w1, [x22,848]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x19,#:lo12:.LANCHOR2]
	ldrh	w2, [x1,x0]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1,2026]
	ldrh	w1, [x1,1956]
	mul	w0, w0, w1
	mov	w1, 2
	sdiv	w0, w0, w1
	cmp	w2, w0
	ble	.L2966
	ldrh	w0, [x22,32]
	sub	w20, w20, #1
	cmp	w0, w20
	blt	.L2966
	bl	FtlReadRefresh
	b	.L3095
.L2966:
	cbnz	w2, .L2965
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,32]
	add	w2, w2, 1
	b	.L3070
.L2959:
	adrp	x0, .LANCHOR0+1948
	mov	w21, 2
	ldr	w0, [x0,#:lo12:.LANCHOR0+1948]
	cmp	w0, wzr
	csinc	w21, w21, wzr, ne
	b	.L2958
.L3024:
	mov	w20, 65535
.L2958:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,280]
	cmp	w1, w2
	bne	.L2968
	cmp	w20, w1
	beq	.L2969
	strh	w20, [x0,280]
	b	.L2970
.L2969:
	ldrh	w1, [x0,936]
	cmp	w1, w20
	beq	.L2970
	adrp	x2, .LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2]
	ldrh	w1, [x2,x1]
	cbnz	w1, .L2971
	mov	w1, -1
	strh	w1, [x0,936]
.L2971:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,936]
	strh	w1, [x0,280]
	mov	w1, -1
	strh	w1, [x0,936]
.L2970:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x22,280]
	strb	wzr, [x22,288]
	cmp	w0, w1
	beq	.L2968
	bl	IsBlkInGcList
	cbz	w0, .L2973
	mov	w0, -1
	strh	w0, [x22,280]
.L2973:
	adrp	x0, .LANCHOR0+196
	ldrb	w0, [x0,#:lo12:.LANCHOR0+196]
	cbz	w0, .L2974
	add	x0, x19, :lo12:.LANCHOR2
	add	x22, x0, 280
	ldrh	w0, [x0,280]
	bl	ftl_get_blk_mode
	strb	w0, [x22,8]
.L2974:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x23, x22, 280
	ldrh	w1, [x22,280]
	cmp	w1, w0
	beq	.L2968
	mov	x0, x23
	bl	make_superblock
	strh	wzr, [x22,282]
	adrp	x0, .LANCHOR5
	adrp	x1, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR5
	ldrh	w2, [x22,280]
	strb	wzr, [x22,286]
	ldr	x1, [x1,#:lo12:.LANCHOR2]
	strh	wzr, [x0,-78]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,-76]
.L2968:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,280]
	ldrh	w2, [x0,40]
	cmp	w2, w1
	beq	.L2975
	ldrh	w2, [x0,88]
	cmp	w2, w1
	beq	.L2975
	ldrh	w0, [x0,136]
	cmp	w0, w1
	bne	.L2976
.L2975:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, -1
	strh	w1, [x0,280]
.L2976:
	add	x24, x19, :lo12:.LANCHOR2
	mov	w25, 65535
.L3009:
	ldrh	w0, [x24,280]
	cmp	w0, w25
	bne	.L2977
	str	wzr, [x24,860]
.L2978:
	ldrh	w22, [x24,856]
	mov	w0, w22
	bl	List_get_gc_head_node
	uxth	w23, w0
	cmp	w23, w25
	strh	w23, [x24,280]
	bne	.L2979
	strh	wzr, [x24,856]
	mov	w2, 8
	b	.L3070
.L2979:
	mov	w0, w23
	add	w22, w22, 1
	bl	IsBlkInGcList
	cbz	w0, .L2980
	strh	w22, [x24,856]
	b	.L2978
.L2980:
	adrp	x0, .LANCHOR0
	ubfiz	x1, x23, 1, 16
	add	x5, x0, :lo12:.LANCHOR0
	ldr	x2, [x24]
	uxth	w22, w22
	mov	w3, 2
	strh	w22, [x24,856]
	ldrh	w0, [x5,2024]
	ldrh	w5, [x5,1956]
	ldrh	w4, [x2,x1]
	mul	w0, w0, w5
	sdiv	w5, w0, w3
	cmp	w4, w5
	bgt	.L2982
	cmp	w4, 8
	bls	.L2983
	cmp	w22, 48
	bls	.L2983
	ldrh	w4, [x24,908]
	cmp	w4, 35
	bhi	.L2983
.L2982:
	strh	wzr, [x24,856]
.L2983:
	cmp	w20, w25
	ldrh	w1, [x2,x1]
	bne	.L2984
	cmp	w1, w0
	blt	.L2984
	ldrh	w0, [x24,856]
	cmp	w0, 3
	bhi	.L2984
	mov	w0, -1
	strh	w0, [x24,280]
	adrp	x0, .LANCHOR5-80
	strh	wzr, [x24,856]
	ldrh	w2, [x0,#:lo12:.LANCHOR5-80]
	b	.L3070
.L2984:
	cbnz	w1, .L2985
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x24,856]
	add	w0, w0, 1
	strh	w0, [x24,856]
	b	.L2978
.L2985:
	adrp	x0, .LANCHOR0
	strb	wzr, [x24,288]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbz	w0, .L2986
	mov	w0, w23
	bl	ftl_get_blk_mode
	strb	w0, [x24,288]
.L2986:
	add	x22, x24, 280
	mov	x0, x22
	bl	make_superblock
	adrp	x1, .LANCHOR5
	ldrh	w2, [x24,280]
	add	x1, x1, :lo12:.LANCHOR5
	ldr	x0, [x24]
	strh	wzr, [x1,-78]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,-76]
	strh	wzr, [x24,282]
	strb	wzr, [x24,286]
.L2977:
	ldr	w0, [x29,124]
	cmp	w0, 1
	bne	.L2987
	bl	FtlReadRefresh
.L2987:
	mov	w0, 1
	str	w0, [x24,2024]
	adrp	x0, .LANCHOR0
	adrp	x22, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,196]
	ldrh	w26, [x0,2024]
	cbz	w1, .L2988
	ldrb	w1, [x24,288]
	cmp	w1, 1
	bne	.L2988
	ldrh	w26, [x0,2026]
.L2988:
	ldrh	w0, [x24,282]
	add	w1, w0, w21
	cmp	w1, w26
	ble	.L2989
	sub	w21, w26, w0
	uxth	w21, w21
.L2989:
	mov	w27, 0
.L2990:
	cmp	w21, w27, uxth
	bls	.L2997
	add	x1, x24, 280
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w4, [x1,2]
	ldrh	w6, [x0,1956]
	mov	x0, 0
	add	w4, w4, w27
	mov	w23, w0
.L2998:
	cmp	w6, w0, uxth
	bls	.L3098
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,16]
	cmp	w2, w25
	beq	.L2991
	mov	w3, 56
	ldr	x7, [x24,896]
	orr	w2, w4, w2, lsl 10
	umull	x5, w23, w3
	add	w23, w23, 1
	add	x5, x7, x5
	uxth	w23, w23
	str	w2, [x5,4]
.L2991:
	add	x0, x0, 1
	b	.L2998
.L3098:
	ldr	x0, [x24,896]
	mov	w1, w23
	ldrb	w2, [x24,288]
	mov	x28, 0
	bl	FlashReadPages
.L2993:
	cmp	w23, w28, uxth
	bls	.L3099
	mov	x0, 56
	mul	x5, x28, x0
	ldr	x0, [x24,896]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	cmn	w0, #1
	ldr	x4, [x1,16]
	beq	.L3027
	ldrh	w0, [x4]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L3027
	ldr	w0, [x4,8]
	add	x1, x29, 136
	mov	w2, 0
	str	x5, [x29,104]
	str	x4, [x29,112]
	bl	log2phys
	ldr	x2, [x24,896]
	ldr	x5, [x29,104]
	ldr	w0, [x29,136]
	add	x2, x2, x5
	ldr	x4, [x29,112]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L3027
	adrp	x0, .LANCHOR5
	mov	x3, 56
	add	x1, x0, :lo12:.LANCHOR5
	str	x5, [x29,96]
	str	x4, [x29,104]
	ldrh	w0, [x1,-78]
	add	w0, w0, 1
	strh	w0, [x1,-78]
	ldr	w0, [x24,864]
	ldr	x1, [x24,2056]
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
	ldr	w0, [x24,864]
	ldr	x1, [x24,2056]
	madd	x0, x0, x2, x1
	ldr	x1, [x24,896]
	add	x1, x1, x5
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	add	x2, x24, 136
	str	x2, [x29,112]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,136]
	str	w0, [x4,12]
	ldrh	w0, [x24,136]
	strh	w0, [x4,2]
	ldr	w0, [x24,236]
	str	w0, [x4,4]
	ldr	w0, [x24,864]
	add	w0, w0, 1
	str	w0, [x24,864]
	ldr	x0, [x24,896]
	add	x0, x0, x5
	bl	FtlGcBufAlloc
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,196]
	cbnz	w0, .L2995
	ldr	x2, [x29,112]
	ldr	w1, [x24,864]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2995
	ldrh	w0, [x2,4]
	cbnz	w0, .L3027
.L2995:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L3027
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,280]
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x19,2024]
	strh	wzr, [x19,282]
	ldrh	w2, [x0,-80]
	b	.L3070
.L3027:
	add	x28, x28, 1
	b	.L2993
.L3099:
	add	w27, w27, 1
	b	.L2990
.L2997:
	ldrh	w0, [x24,282]
	add	w21, w21, w0
	uxth	w21, w21
	strh	w21, [x24,282]
	cmp	w21, w26
	bcc	.L2999
	ldr	w0, [x24,864]
	adrp	x21, .LANCHOR5
	cbz	w0, .L3000
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L3000
	str	wzr, [x24,2024]
	add	x21, x21, :lo12:.LANCHOR5
.L3095:
	ldrh	w2, [x21,-80]
	b	.L3070
.L3000:
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w21, [x21,-78]
	cbnz	w21, .L3001
	ldrh	w1, [x24,280]
	ldr	x0, [x24]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L3001
.L3002:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2096]
	cmp	w21, w0
	bcs	.L3007
	mov	w0, w21
	add	x1, x29, 140
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,140]
	cmn	w0, #1
	beq	.L3003
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x24,280]
	cmp	w1, w0, uxth
	bne	.L3003
.L3007:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2096]
	cmp	w21, w0
	bcc	.L3001
	ldrh	w1, [x24,280]
	ldr	x0, [x24]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x24,280]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3001
.L3003:
	add	w21, w21, 1
	b	.L3002
.L3001:
	mov	w0, -1
	strh	w0, [x24,280]
.L2999:
	ldrh	w0, [x24,32]
	cmp	w0, 2
	bhi	.L3008
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w21, [x22,2024]
	b	.L3009
.L3008:
	adrp	x1, .LANCHOR5-80
	str	wzr, [x24,2024]
	ldrh	w2, [x1,#:lo12:.LANCHOR5-80]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L3070
.L3097:
	ldrh	w23, [x21,936]
	cmp	w23, w20
	bne	.L2934
	ldrh	w0, [x21,32]
	mov	w2, 1024
	ldr	w1, [x21,852]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2934
	adrp	x0, .LANCHOR5-80
	str	wzr, [x21,852]
	strh	wzr, [x0,#:lo12:.LANCHOR5-80]
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	cmp	w20, w23
	bne	.L2936
	ldrh	w1, [x21,32]
	ldrh	w0, [x21,850]
	cmp	w1, w0
	bcs	.L2937
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w20
	beq	.L2946
	ldr	w2, [x21,2020]
	adrp	x23, .LANCHOR0
	uxtw	x1, w0
	cbnz	w2, .L2939
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w2, [x0,1976]
	cmp	w2, 3
	beq	.L2939
	ldr	w2, [x21,328]
	cbnz	w2, .L2939
	ldr	w2, [x0,1948]
	cbnz	w2, .L2939
	ldrb	w0, [x0,196]
	cbz	w0, .L2940
.L2939:
	add	x3, x23, :lo12:.LANCHOR0
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x3,2026]
	ldrh	w0, [x3,1956]
	ldrh	w3, [x3,1976]
	mul	w2, w1, w0
	cmp	w3, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w2, w0
	cmp	w4, w0
	bgt	.L2942
	mov	w0, 0
	add	x23, x23, :lo12:.LANCHOR0
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x23,2096]
	ldr	w2, [x1,196]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2943
	mov	w0, 128
	b	.L3093
.L2943:
	mov	w0, 160
.L3093:
	strh	w0, [x1,850]
	b	.L2944
.L2942:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L3094
.L2940:
	adrp	x2, .LANCHOR2
	ldr	x2, [x2,#:lo12:.LANCHOR2]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2945
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0,850]
	b	.L2944
.L2945:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L3094:
	strh	w1, [x0,850]
	b	.L2946
.L2937:
	mov	w0, 80
	strh	w0, [x21,850]
	b	.L2946
.L2944:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2946
.L2936:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x20, 1, 32
	ldr	x3, [x19,#:lo12:.LANCHOR2]
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	mov	w1, w20
	ldr	x6, [x5,-80]
	ldrh	w2, [x5,32]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,848]
	ldrh	w4, [x6,x4]
	bl	printk
.L2946:
	bl	FtlGcReFreshBadBlk
	b	.L2934
.L3096:
	mov	w20, w0
	b	.L2934
.L3070:
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
	adrp	x0, .LANCHOR5-72
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	ldr	x20, [x0,#:lo12:.LANCHOR5-72]
	add	x0, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w23, [x0,-96]
	cbnz	w23, .L3102
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cbz	w1, .L3102
	ldrb	w0, [x0,196]
	cbz	w0, .L3104
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w23, eq
.L3104:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x20,9]
	adrp	x26, .LC152
	mov	w2, w23
	mov	w24, 0
	mov	w27, 56
	ldr	x0, [x0,2088]
	add	x26, x26, :lo12:.LC152
	bl	FlashProgPages
.L3105:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cmp	w24, w1
	bcs	.L3123
	umull	x21, w24, w27
	ldr	x1, [x0,2088]
	add	x0, x1, x21
	ldr	w25, [x1,x21]
	cmn	w25, #1
	beq	.L3126
	ldr	w1, [x0,4]
	cbz	w23, .L3140
	orr	w1, w1, -2147483648
.L3140:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,124]
	add	x1, x29, 124
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2088]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L3109
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x22,#:lo12:.LANCHOR2]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L3110
	mov	x0, x26
	bl	printk
.L3110:
	mov	w0, w21
	bl	decrement_vpc_count
.L3109:
	add	w24, w24, 1
	b	.L3105
.L3142:
	mov	w20, 16386
.L3122:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w0, [x0,944]
	cbz	w0, .L3123
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w20, w20, #1
	bne	.L3122
.L3123:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19,2080]
	b	.L3102
.L3126:
	adrp	x26, .LC152
	mov	w24, 0
	mov	w27, 56
	str	w25, [x29,108]
	mov	w28, 1
	add	x26, x26, :lo12:.LC152
.L3106:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cmp	w24, w1
	bcs	.L3142
	umull	x21, w24, w27
	ldr	x0, [x0,2088]
	ldr	w1, [x29,108]
	str	w1, [x0,x21]
.L3112:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x0, [x2,2088]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L3143
	ldr	w0, [x1,4]
	str	x2, [x29,96]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	ldr	x2, [x29,96]
	bne	.L3113
	ldr	x3, [x22,#:lo12:.LANCHOR2]
	ubfiz	x1, x1, 1, 16
	ldrh	w4, [x20,4]
	ldrh	w0, [x3,x1]
	sub	w0, w0, w4
	strh	w0, [x3,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x2,2024]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L3113:
	ldrh	w0, [x20,4]
	cbnz	w0, .L3114
	mov	x0, x20
	bl	allocate_new_data_superblock
.L3114:
	add	x5, x22, :lo12:.LANCHOR2
	add	x25, x19, :lo12:.LANCHOR0
	str	x5, [x29,96]
	ldr	w0, [x5,432]
	add	w0, w0, 1
	str	w0, [x5,432]
	ldr	x0, [x25,2088]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,124]
	ldr	x1, [x25,2088]
	mov	w2, w23
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x25,2088]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	ldr	x0, [x25,2088]
	ldr	x5, [x29,96]
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L3115
	str	w28, [x5,-96]
.L3115:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w0, [x0,-96]
	cbz	w0, .L3112
	b	.L3102
.L3143:
	ldr	w0, [x1,4]
	cbz	w23, .L3141
	orr	w0, w0, -2147483648
.L3141:
	str	w0, [x29,124]
	mov	w2, 1
	ldr	w0, [x1,24]
	add	x1, x29, 124
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2088]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L3119
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x22,#:lo12:.LANCHOR2]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L3120
	mov	x0, x26
	bl	printk
.L3120:
	mov	w0, w21
	bl	decrement_vpc_count
.L3119:
	add	w24, w24, 1
	b	.L3106
.L3102:
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
	adrp	x0, .LANCHOR2-96
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2-96]
	cbnz	w0, .L3145
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0,#:lo12:.LANCHOR1+504]
	cmn	w0, #1
	beq	.L3145
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3145:
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
	bne	.L3150
	bl	FtlSysFlush
.L3150:
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
	adrp	x0, .LC153
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC153
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
	ldr	w1, [x21,2064]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L3156
	adrp	x0, .LANCHOR1+504
	ldr	w1, [x0,#:lo12:.LANCHOR1+504]
	cmn	w1, #1
	mov	w0, w1
	beq	.L3156
	cmp	w19, 31
	mov	w0, 0
	bls	.L3156
	adrp	x23, .LANCHOR2
	add	x1, x23, :lo12:.LANCHOR2
	ldr	w1, [x1,-96]
	cbnz	w1, .L3156
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,2030]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L3157
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L3157:
	mov	w0, -1
	adrp	x20, .LANCHOR5
	str	w0, [x29,76]
.L3158:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2030]
	cmp	w19, w0
	bcc	.L3172
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L3159
	add	x1, x20, :lo12:.LANCHOR5
	mov	w2, 1
	ldr	w0, [x1,-64]
	add	w0, w0, 1
	str	w0, [x1,-64]
	add	x1, x23, :lo12:.LANCHOR2
	ldr	w0, [x1,204]
	add	w0, w0, 1
	str	w0, [x1,204]
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,72]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3159:
	add	x0, x22, :lo12:.LANCHOR0
	add	w21, w21, 1
	ldrh	w0, [x0,2030]
	sub	w19, w19, w0
	b	.L3158
.L3172:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,-64]
	cmp	w1, 32
	bls	.L3161
	str	wzr, [x0,-64]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3161:
	mov	w0, 0
.L3156:
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
	beq	.L3175
	cmp	w4, 16
	mov	x24, x3
	mov	w27, w2
	mov	w20, w1
	bne	.L3176
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L3175
.L3176:
	adrp	x21, .LANCHOR0
	add	w0, w1, w2
	add	x1, x21, :lo12:.LANCHOR0
	str	w0, [x29,168]
	mov	w0, -1
	ldr	w3, [x29,168]
	ldr	w2, [x1,2064]
	cmp	w3, w2
	bhi	.L3175
	ldrh	w0, [x1,2030]
	sub	w26, w3, #1
	adrp	x19, .LANCHOR2
	udiv	w1, w20, w0
	str	w1, [x29,172]
	udiv	w26, w26, w0
	ldr	w0, [x29,172]
	sub	w25, w26, w0
	add	x0, x19, :lo12:.LANCHOR2
	add	w25, w25, 1
	ldr	w1, [x0,228]
	add	w1, w27, w1
	str	w1, [x0,228]
	ldr	w1, [x0,200]
	add	w1, w25, w1
	str	w1, [x0,200]
	ldr	w0, [x29,172]
	mov	w1, w26
	bl	FtlCacheMetchLpa
	cbz	w0, .L3177
	bl	FtlCacheWriteBack
.L3177:
	mov	w28, 0
	adrp	x0, .LC142
	ldr	w22, [x29,172]
	add	x0, x0, :lo12:.LC142
	str	w28, [x29,156]
	mov	w23, w28
	str	w28, [x29,152]
	str	x0, [x29,112]
.L3178:
	cbz	w25, .L3222
	mov	w0, w22
	add	x1, x29, 188
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,188]
	cmn	w3, #1
	bne	.L3219
	mov	w3, 0
.L3179:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2030]
	cmp	w3, w0
	bcs	.L3183
	madd	w0, w22, w0, w3
	cmp	w0, w20
	bcc	.L3181
	ldr	w1, [x29,168]
	cmp	w0, w1
	bcs	.L3181
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x24, x0
	str	x3, [x29,160]
	bl	ftl_memset
	ldr	x3, [x29,160]
.L3181:
	add	w3, w3, 1
	b	.L3179
.L3219:
	mov	w0, 56
	umull	x1, w23, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x0,2048]
	add	x2, x2, x1
	str	w3, [x2,4]
	ldr	w2, [x29,172]
	cmp	w22, w2
	ldr	x2, [x0,2048]
	add	x2, x2, x1
	bne	.L3184
	ldr	x0, [x0,2104]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2030]
	udiv	w3, w20, w0
	msub	w3, w3, w0, w20
	str	w3, [x29,128]
	sub	w3, w0, w3
	cmp	w3, w27
	csel	w3, w3, w27, ls
	str	w3, [x29,156]
	cmp	w3, w0
	bne	.L3185
	str	x24, [x2,8]
	b	.L3185
.L3184:
	cmp	w22, w26
	bne	.L3186
	ldr	x0, [x0,2112]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w4, [x29,168]
	ldrh	w3, [x0,2030]
	mul	w0, w22, w3
	sub	w28, w4, w0
	cmp	w28, w3
	bne	.L3185
	b	.L3220
.L3186:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2030]
	mul	w0, w22, w0
.L3220:
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x24, x0
	str	x0, [x2,8]
.L3185:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2048]
	ldr	x2, [x2,2128]
	add	x1, x0, x1
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2036]
	str	w22, [x1,24]
	mul	w0, w23, w0
	add	w23, w23, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L3183:
	subs	w25, w25, #1
	add	w22, w22, 1
	beq	.L3187
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,1956]
	cmp	w23, w0, lsl 3
	bne	.L3178
.L3187:
	cbz	w23, .L3178
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, 0
	ldr	x0, [x0,2048]
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
.L3189:
	ldr	w0, [x29,160]
	cmp	w23, w0
	bls	.L3223
	ldr	x0, [x29,160]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	ldr	w3, [x29,172]
	mul	x8, x0, x1
	ldr	x0, [x2,2048]
	add	x0, x0, x8
	ldr	w1, [x0,24]
	cmp	w1, w3
	bne	.L3190
	ldr	x1, [x0,8]
	ldr	x0, [x2,2104]
	cmp	x1, x0
	bne	.L3191
	ldr	x2, [x29,144]
	mov	x0, x24
	str	x8, [x29,104]
	add	x1, x1, x2
	ldr	w2, [x29,124]
	b	.L3221
.L3190:
	cmp	w1, w26
	bne	.L3191
	ldr	x1, [x0,8]
	ldr	x0, [x2,2112]
	cmp	x1, x0
	bne	.L3191
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,120]
	str	x8, [x29,104]
	ldrh	w0, [x0,2030]
	mul	w0, w26, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x24, x0
.L3221:
	bl	ftl_memcpy
	ldr	x8, [x29,104]
.L3191:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
	add	x2, x1, x8
	ldr	w3, [x1,x8]
	cmn	w3, #1
	bne	.L3192
	ldr	w1, [x0,408]
	str	w3, [x29,152]
	add	w1, w1, 1
	str	w1, [x0,408]
.L3192:
	ldr	x0, [x2,16]
	ldr	w1, [x2,24]
	ldr	w0, [x0,8]
	cmp	w1, w0
	beq	.L3193
	add	x0, x19, :lo12:.LANCHOR2
	str	x8, [x29,104]
	ldr	w1, [x0,408]
	add	w1, w1, 1
	str	w1, [x0,408]
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
.L3193:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2048]
	add	x1, x0, x8
	ldr	w0, [x0,x8]
	cmp	w0, 256
	bne	.L3194
	ldr	w0, [x1,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3194:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L3189
.L3223:
	mov	w23, 0
	b	.L3178
.L3222:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,944]
	cbz	w0, .L3197
	mov	w0, w25
	mov	w1, 1
	bl	ftl_do_gc
.L3197:
	ldr	w0, [x29,152]
.L3175:
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
	bne	.L3226
	bl	ftl_vendor_read
	mov	w19, w0
.L3226:
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
	adrp	x0, .LANCHOR5-60
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR5-60]
	adrp	x0, .LC75
	add	x0, x0, :lo12:.LC75
	str	wzr, [x19,-96]
	bl	printk
	add	x0, x21, 168
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x21,1960]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3232
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	b	.L3246
.L3232:
	bl	FtlSysBlkInit
	mov	w20, w0
	cbz	w0, .L3234
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
.L3246:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 120
	bl	printk
	b	.L3233
.L3234:
	mov	w1, 1
	str	w1, [x22,504]
	bl	ftl_do_gc
	ldrh	w0, [x19,32]
	cmp	w0, 15
	bhi	.L3235
	mov	w22, 65535
	add	w21, w0, 2
.L3238:
	ldrh	w0, [x19,280]
	cmp	w0, w22
	bne	.L3236
	ldrh	w0, [x19,936]
	cmp	w0, w22
	bne	.L3236
	and	w0, w20, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3236:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldrh	w0, [x19,32]
	cmp	w0, w21
	bhi	.L3233
	add	w20, w20, 1
	cmp	w20, 1024
	bne	.L3238
	b	.L3233
.L3235:
	ldrb	w0, [x21,196]
	cbz	w0, .L3233
	mov	w19, 128
.L3240:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3240
.L3233:
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
	mov	x1, x20
	str	x0, [x20,-56]
	mov	x0, x19
	str	xzr, [x1,-48]!
	str	xzr, [x0,48]!
	bl	rknand_get_reg_addr
	ldr	x1, [x19,48]
	mov	w0, -1
	cbz	x1, .L3248
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x20,-56]
	mov	w2, w1
	mov	w3, 2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	x0, [x19,48]
	bl	FlashInit
	mov	w19, w0
	cbnz	w0, .L3249
	bl	FtlInit
.L3249:
	adrp	x0, .LC156
	mov	w1, w19
	add	x0, x0, :lo12:.LC156
	bl	printk
	mov	w0, w19
.L3248:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	stp	x29, x30, [sp, -144]!
	adrp	x0, .LANCHOR0
	add	x29, sp, 0
	add	x1, x0, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	str	x27, [sp,80]
	mov	x25, x0
	ldrb	w1, [x1,196]
	cbz	w1, .L3252
	adrp	x21, .LANCHOR5
	adrp	x19, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x20, .LC157
	add	x24, x19, :lo12:.LANCHOR2
	ldr	x2, [x19,#:lo12:.LANCHOR2]
	add	x27, x24, 40
	ldrh	w23, [x0,-216]
	add	x0, x20, :lo12:.LC157
	add	x26, x24, 88
	ubfiz	x22, x23, 1, 16
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldrh	w0, [x24,40]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x24,88]
	mov	w24, 4097
	bl	FtlGcRefreshOpenBlock
	mov	x0, x27
	bl	allocate_new_data_superblock
	mov	x0, x26
	bl	allocate_new_data_superblock
.L3254:
	subs	w24, w24, #1
	beq	.L3258
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3254
.L3258:
	ldr	x2, [x19,#:lo12:.LANCHOR2]
	add	x0, x20, :lo12:.LC157
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3256
	add	x0, x29, 96
	strh	w23, [x29,96]
	bl	make_superblock
	add	x0, x25, :lo12:.LANCHOR0
	mov	w6, 65535
	mov	w7, 56
	ldrh	w5, [x0,1956]
	mov	x0, 0
	mov	w20, w0
.L3259:
	cmp	w5, w0, uxth
	bls	.L3266
	add	x1, x29, 96
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L3260
	add	x4, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x1, w20, w7
	add	w20, w20, 1
	ldr	x3, [x4,-88]
	uxth	w20, w20
	add	x3, x3, x1
	str	w2, [x3,4]
	ldr	x2, [x4,-88]
	add	x1, x2, x1
	str	xzr, [x1,8]
	str	xzr, [x1,16]
.L3260:
	add	x0, x0, 1
	b	.L3259
.L3266:
	ldr	x2, [x19,#:lo12:.LANCHOR2]
	adrp	x0, .LC158
	add	x24, x19, :lo12:.LANCHOR2
	mov	w1, w23
	add	x0, x0, :lo12:.LC158
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x24,-88]
	mov	w1, 0
	mov	w2, w20
	bl	FlashEraseBlocks
	ldr	x0, [x24,-88]
	mov	w1, 1
	mov	w2, w20
	bl	FlashEraseBlocks
.L3256:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w0, -1
	strh	w0, [x21,-216]
.L3252:
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
	ldr	w3, [x2,-96]
	cbnz	w3, .L3269
	adrp	x1, .LANCHOR1
	str	x1, [x29,152]
	add	x3, x1, :lo12:.LANCHOR1
	ldr	w3, [x3,504]
	cmn	w3, #1
	beq	.L3269
	cmp	w4, 16
	bne	.L3270
	add	w0, w23, 256
	mov	w1, w25
	mov	x2, x26
	bl	FtlVendorPartWrite
	b	.L3269
.L3270:
	adrp	x21, .LANCHOR0
	add	w3, w23, w25
	add	x4, x21, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	w1, [x4,2064]
	cmp	w3, w1
	bhi	.L3269
	adrp	x5, .LANCHOR5
	ldrh	w1, [x4,2030]
	add	x22, x5, :lo12:.LANCHOR5
	sub	w3, w3, #1
	mov	w0, 2048
	str	x5, [x29,144]
	udiv	w27, w23, w1
	cmp	w25, w1, lsl 1
	str	w0, [x22,-40]
	udiv	w0, w3, w1
	ldr	w3, [x4,2080]
	str	w0, [x29,208]
	sub	w24, w0, w27
	add	w20, w24, 1
	ldr	w0, [x2,208]
	add	w0, w20, w0
	str	w0, [x2,208]
	ldr	w0, [x2,224]
	add	w0, w25, w0
	str	w0, [x2,224]
	cset	w0, cs
	str	w0, [x29,212]
	cbz	w3, .L3272
	mov	w0, 56
	sub	w3, w3, #1
	umull	x3, w3, w0
	ldr	x0, [x4,2088]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w27, w0
	bne	.L3273
	ldr	w0, [x2,212]
	add	w0, w0, 1
	str	w0, [x2,212]
	ldr	w0, [x22,-36]
	add	w0, w0, 1
	str	w0, [x22,-36]
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
	cbnz	w24, .L3274
	ldr	w0, [x22,-36]
	cmp	w0, 2
	bgt	.L3274
.L3308:
	mov	w0, 0
	b	.L3269
.L3274:
	add	x26, x26, x20
	sub	w25, w25, w19
	add	w23, w23, w19
	add	w27, w27, 1
	mov	w20, w24
.L3273:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,-36]
.L3272:
	ldr	w1, [x29,208]
	mov	w0, w27
	bl	FtlCacheMetchLpa
	cbz	w0, .L3275
	bl	FtlCacheWriteBack
.L3275:
	ldr	x0, [x29,144]
	ldr	x1, [x29,176]
	add	x0, x0, :lo12:.LANCHOR5
	str	w27, [x29,192]
	add	x24, x1, :lo12:.LANCHOR2
	add	x22, x24, 40
	str	x22, [x29,184]
	str	x22, [x0,-72]
	adrp	x0, .LC159
	add	x0, x0, :lo12:.LC159
	str	x0, [x29,104]
.L3276:
	cbz	w20, .L3339
	ldrh	w0, [x22,4]
	cbnz	w0, .L3277
	ldr	x1, [x29,184]
	cmp	x22, x1
	bne	.L3278
	add	x0, x24, 88
	ldrh	w19, [x0,4]
	cbnz	w19, .L3279
	bl	allocate_new_data_superblock
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR1
	str	w19, [x0,3300]
.L3279:
	ldr	x0, [x29,184]
	add	x22, x24, 88
	bl	allocate_new_data_superblock
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR1
	ldr	w0, [x0,3300]
	cbnz	w0, .L3280
.L3281:
	ldr	x22, [x29,184]
	b	.L3280
.L3278:
	ldr	x1, [x29,152]
	add	x1, x1, :lo12:.LANCHOR1
	str	w0, [x1,3300]
	ldr	x0, [x29,184]
	ldrh	w0, [x0,4]
	cbnz	w0, .L3281
	mov	x0, x22
	bl	allocate_new_data_superblock
.L3280:
	ldrh	w0, [x22,4]
	cbnz	w0, .L3282
	mov	x0, x22
	bl	allocate_new_data_superblock
.L3282:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	x22, [x0,-72]
.L3277:
	add	x2, x21, :lo12:.LANCHOR0
	ldr	w0, [x24,2044]
	ldrh	w1, [x22,4]
	str	xzr, [x29,200]
	ldr	w2, [x2,2080]
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
.L3283:
	ldr	w0, [x29,200]
	ldr	w2, [x29,140]
	ldr	w1, [x29,192]
	cmp	w0, w2
	add	w28, w1, w0
	bcs	.L3340
	ldrh	w1, [x22,4]
	cbz	w1, .L3315
	ldr	w1, [x29,208]
	cmp	w28, w1
	cset	w6, eq
	cbz	w0, .L3285
	ldr	w1, [x29,116]
	tst	w6, w1
	beq	.L3285
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,136]
	ldrh	w1, [x1,2030]
	msub	w2, w28, w1, w2
	cmp	w2, w1
	bne	.L3315
.L3285:
	add	x1, x29, 228
	mov	w2, 0
	mov	w0, w28
	str	x6, [x29,96]
	add	x19, x21, :lo12:.LANCHOR0
	bl	log2phys
	mov	x0, x22
	bl	get_new_active_ppa
	ldr	w5, [x19,2080]
	mov	w4, 56
	ldr	x2, [x19,2088]
	str	x4, [x29,160]
	umull	x1, w5, w4
	add	x2, x2, x1
	str	w0, [x2,4]
	ldrh	w2, [x19,2036]
	ldr	x0, [x19,2088]
	add	x1, x0, x1
	mul	w0, w2, w5
	and	x0, x0, 4294967292
	str	x0, [x29,128]
	ldr	x0, [x24,2136]
	ldr	x3, [x29,128]
	str	x0, [x29,120]
	add	x0, x0, x3
	str	x0, [x29,216]
	str	x0, [x1,16]
	ldrh	w0, [x19,2034]
	str	w28, [x1,24]
	mul	w0, w0, w5
	ldr	x5, [x24,2096]
	and	x0, x0, 4294967292
	add	x0, x5, x0
	str	x0, [x1,8]
	ldr	x0, [x29,216]
	mov	w1, 0
	bl	ftl_memset
	cmp	w28, w27
	cset	w0, eq
	str	w0, [x29,196]
	cbnz	w0, .L3319
	ldr	x6, [x29,96]
	ldr	x4, [x29,160]
	cbz	w6, .L3286
	ldr	w0, [x29,136]
	ldrh	w19, [x19,2030]
	msub	w19, w28, w19, w0
	ldr	w0, [x29,196]
	str	w0, [x29,160]
	uxth	w19, w19
	b	.L3289
.L3319:
	ldrh	w19, [x19,2030]
	udiv	w0, w23, w19
	msub	w0, w0, w19, w23
	str	w0, [x29,160]
	sub	w19, w19, w0
	cmp	w19, w25
	csel	w19, w19, w25, ls
.L3289:
	add	x2, x21, :lo12:.LANCHOR0
	ldrh	w0, [x2,2030]
	cmp	w19, w0
	bne	.L3290
	ldr	w0, [x29,196]
	mov	x1, x26
	cbnz	w0, .L3291
	mul	w1, w19, w28
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x26, x1
.L3291:
	ldr	w2, [x29,212]
	mov	w0, 56
	add	x4, x21, :lo12:.LANCHOR0
	cbz	w2, .L3292
	ldr	w2, [x4,2080]
	umull	x0, w2, w0
	ldr	x2, [x4,2088]
	add	x0, x2, x0
	b	.L3337
.L3292:
	ldr	w2, [x4,2080]
	umull	x0, w2, w0
	ldr	x2, [x4,2088]
	add	x0, x2, x0
	ldrh	w2, [x4,2034]
	ldr	x0, [x0,8]
	b	.L3338
.L3290:
	ldr	w0, [x29,228]
	cmn	w0, #1
	beq	.L3294
	str	w0, [x29,236]
	mov	w1, 56
	ldr	w0, [x2,2080]
	str	w28, [x29,256]
	umull	x0, w0, w1
	ldr	x1, [x2,2088]
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
	bne	.L3295
	ldr	w0, [x24,408]
	add	w0, w0, 1
	str	w0, [x24,408]
	b	.L3297
.L3295:
	ldr	x0, [x29,216]
	ldr	w0, [x0,8]
	cmp	w0, w28
	beq	.L3297
	ldr	w0, [x24,408]
	mov	w2, w28
	ldr	x1, [x29,216]
	add	w0, w0, 1
	str	w0, [x24,408]
	ldr	x0, [x29,104]
	ldr	w1, [x1,8]
	bl	printk
	b	.L3297
.L3294:
	ldr	w0, [x2,2080]
	mov	w1, 56
	umull	x0, w0, w1
	ldr	x1, [x2,2088]
	ldrh	w2, [x2,2034]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L3297:
	ldr	w1, [x29,196]
	mov	w0, 56
	lsl	w2, w19, 9
	cbz	w1, .L3298
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w4, [x1,2080]
	umull	x0, w4, w0
	ldr	x4, [x1,2088]
	add	x4, x4, x0
	ldr	x0, [x29,160]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x4,8]
	add	x0, x1, x0
	mov	x1, x26
	b	.L3338
.L3298:
	add	x5, x21, :lo12:.LANCHOR0
	ldr	w4, [x5,2080]
	ldr	x1, [x5,2088]
	umull	x0, w4, w0
	add	x0, x1, x0
	ldrh	w1, [x5,2030]
	mul	w1, w28, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x26, x1
	b	.L3338
.L3286:
	ldr	w0, [x29,212]
	ldrh	w1, [x19,2030]
	ldr	w2, [x19,2080]
	cbz	w0, .L3299
	mul	w1, w28, w1
	umull	x0, w2, w4
	sub	w1, w1, w23
	ldr	x2, [x19,2088]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x26, x1
.L3337:
	str	x1, [x0,8]
	b	.L3293
.L3299:
	umull	x0, w2, w4
	ldr	x2, [x19,2088]
	mul	w1, w28, w1
	add	x0, x2, x0
	ldrh	w2, [x19,2034]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x26, x1
.L3338:
	bl	ftl_memcpy
.L3293:
	ldr	x1, [x29,120]
	mov	w0, -3947
	ldr	x2, [x29,128]
	strh	w0, [x1,x2]
	ldr	x1, [x29,216]
	ldr	w0, [x24,236]
	str	w0, [x1,4]
	add	w0, w0, 1
	cmn	w0, #1
	str	w28, [x1,8]
	csel	w0, w0, wzr, ne
	str	w0, [x24,236]
	ldr	w0, [x29,228]
	str	w0, [x1,12]
	ldrh	w0, [x22]
	strh	w0, [x1,2]
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w0, [x1,2080]
	add	w0, w0, 1
	str	w0, [x1,2080]
	ldr	x0, [x29,200]
	add	x0, x0, 1
	str	x0, [x29,200]
	b	.L3283
.L3340:
	str	w28, [x29,192]
	mov	x0, x2
	b	.L3284
.L3315:
	str	w28, [x29,192]
.L3284:
	sub	w20, w20, w0
	ldr	w0, [x29,212]
	cbnz	w0, .L3303
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	ldr	w0, [x24,2044]
	cmp	w1, w0
	bcs	.L3303
	ldrh	w0, [x22,4]
	cbz	w0, .L3303
.L3305:
	str	wzr, [x29,212]
	b	.L3276
.L3303:
	bl	FtlCacheWriteBack
	add	x0, x21, :lo12:.LANCHOR0
	cmp	w20, 1
	str	wzr, [x0,2080]
	bhi	.L3276
	b	.L3305
.L3339:
	ldr	w1, [x29,208]
	mov	w0, w20
	sub	w1, w1, w27
	bl	ftl_do_gc
	ldr	x0, [x29,176]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w0, [x0,32]
	cmp	w0, 5
	bls	.L3307
	cmp	w0, 31
	bhi	.L3308
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,72]
	cbnz	w0, .L3308
.L3307:
	ldr	x0, [x29,176]
	mov	w21, 65535
	mov	w22, 128
	add	x19, x0, :lo12:.LANCHOR2
.L3330:
	ldrh	w0, [x19,280]
	cmp	w0, w21
	bne	.L3309
	ldrh	w0, [x19,936]
	cmp	w0, w21
	bne	.L3309
	ldrh	w0, [x19,938]
	cmp	w0, w21
	bne	.L3309
	and	w0, w20, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3309:
	mov	w0, 1
	strh	w22, [x19,850]
	mov	w1, w0
	strh	w22, [x19,848]
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldr	w0, [x19,-96]
	cbnz	w0, .L3308
	ldrh	w0, [x19,32]
	cmp	w0, 2
	bhi	.L3308
	add	w20, w20, 1
	cmp	w20, 256
	bne	.L3330
	b	.L3308
.L3269:
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
	bne	.L3343
	bl	ftl_vendor_write
	mov	w19, w0
.L3343:
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
	add	x19, x19, 160
	adrp	x22, .LC160
	adrp	x24, .LC161
	lsl	w25, w23, 10
	ldr	x1, [x0,2072]
	mov	w20, 0
	ldr	x0, [x0,2120]
	add	x22, x22, :lo12:.LC160
	str	x1, [x19,8]
	add	x24, x24, :lo12:.LC161
	str	x0, [x19,16]
.L3348:
	adrp	x0, .LANCHOR0+2026
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2026]
	cmp	w20, w0
	bge	.L3354
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
	beq	.L3349
	add	x1, x21, :lo12:.LANCHOR2
	mov	x0, x24
	mov	w2, 4
	mov	w3, 768
	ldr	x1, [x1,2072]
	bl	rknand_print_hex
.L3349:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L3348
.L3354:
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
	adrp	x23, .LC162
	mov	w25, 56
	ldrh	w21, [x0,1964]
	adrp	x24, .LANCHOR2
	mov	w26, 4
	add	x23, x23, :lo12:.LC162
.L3356:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,1966]
	cmp	w0, w21
	bls	.L3369
	mov	x22, 0
	ldrh	w5, [x2,1956]
	ldrh	w4, [x2,2036]
	mov	w27, w22
	mov	x20, x2
.L3365:
	cmp	w5, w22, uxth
	bls	.L3370
	add	x0, x20, 1984
	mov	w1, w21
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrb	w0, [x0,x22]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x5, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L3357
	add	x3, x24, :lo12:.LANCHOR2
	lsl	w28, w28, 10
	umull	x0, w27, w25
	mul	w2, w27, w4
	ldr	x1, [x3,2048]
	add	w27, w27, 1
	add	x1, x1, x0
	uxth	w27, w27
	sdiv	w2, w2, w26
	str	w28, [x1,4]
	ldr	x1, [x3,2048]
	add	x0, x1, x0
	ldr	x1, [x3,880]
	str	x1, [x0,8]
	ldr	x1, [x3,888]
	add	x2, x1, x2, sxtw 2
	str	x2, [x0,16]
.L3357:
	add	x22, x22, 1
	b	.L3365
.L3370:
	cbz	w27, .L3360
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w27
	mov	w2, 1
	mov	x20, 0
	adrp	x22, .LANCHOR2
	mov	x28, 56
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L3361:
	cmp	w27, w20, uxth
	bls	.L3360
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x3, [x0,2048]
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
	b	.L3361
.L3360:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3356
.L3369:
	adrp	x20, .LANCHOR4
	adrp	x23, .LC109
	add	x20, x20, :lo12:.LANCHOR4
	mov	w22, 0
	adrp	x27, .LANCHOR2
	add	x20, x20, 160
	add	x23, x23, :lo12:.LC109
.L3364:
	add	x21, x27, :lo12:.LANCHOR2
	adrp	x26, .LANCHOR2
	ldrh	w0, [x21,2320]
	cmp	w22, w0
	bge	.L3367
	sbfiz	x24, x22, 1, 32
	mov	w21, 0
.L3368:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2026]
	cmp	w21, w0
	bge	.L3371
	add	x25, x26, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x25,2176]
	ldrh	w0, [x0,x24]
	orr	w0, w21, w0, lsl 10
	str	w0, [x20,4]
	mov	x0, x20
	add	w21, w21, 1
	bl	FlashReadPages
	sxth	w21, w21
	ldr	x0, [x20,8]
	ldr	x1, [x25,2176]
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
	b	.L3368
.L3371:
	add	w22, w22, 1
	sxth	w22, w22
	b	.L3364
.L3367:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x21,2176]
	adrp	x0, .LC163
	mov	w2, 2
	add	x0, x0, :lo12:.LC163
	ldr	w3, [x19,2052]
	bl	rknand_print_hex
	ldr	x1, [x21,2216]
	adrp	x0, .LC164
	ldrh	w3, [x19,2060]
	add	x0, x0, :lo12:.LC164
	mov	w2, 4
	bl	rknand_print_hex
	ldr	x1, [x21,2224]
	adrp	x0, .LC165
	ldrh	w3, [x19,2060]
	add	x0, x0, :lo12:.LC165
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
	adrp	x1, .LANCHOR0+1872
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+1872]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3372
	bl	flash_enter_slc_mode
.L3372:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	2
	.global	flash_boot_exit_slc_mode
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	adrp	x1, .LANCHOR0+1872
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+1872]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3374
	bl	flash_exit_slc_mode
.L3374:
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
	ldr	x0, [x19,744]
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	ldr	w20, [x19,76]
	mov	w26, w1
	ldrb	w27, [x0,9]
	adrp	x0, .LC166
	mov	x21, x2
	add	x0, x0, :lo12:.LC166
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
.L3377:
	cmp	w25, w26
	bcs	.L3383
	add	w0, w25, w24
	add	x1, x19, 200
	ubfx	x0, x0, 2, 16
	sub	w20, w5, w3
	uxth	w20, w20
	ldrh	w4, [x1,w0,sxtw 1]
	ldrb	w1, [x19,196]
	cbz	w1, .L3378
	ldr	w2, [x19,1872]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w4, w4, w0, ne
.L3378:
	ldr	x0, [x19,744]
	add	w3, w3, w28
	str	x5, [x29,96]
	madd	w22, w4, w27, w3
	ldrb	w1, [x0,9]
	add	x0, x6, :lo12:.LANCHOR4
	ldrb	w4, [x19,1944]
	str	x6, [x29,120]
	ldrb	w0, [x0,98]
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
	b	.L3377
.L3383:
	adrp	x0, .LC167
	mov	w1, w23
	mov	w2, w26
	mov	w3, 0
	add	x0, x0, :lo12:.LC167
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
	ldr	x1, [x0,744]
	ldr	w21, [x0,76]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	ldrb	w20, [x1,9]
	bl	ftl_malloc
	mov	x26, x0
	mov	w0, -1
	cbz	x26, .L3422
	add	w19, w19, 511
	lsr	w19, w19, 9
	cmp	w19, 8
	bls	.L3406
	cmp	w19, 500
	bhi	.L3422
	b	.L3386
.L3406:
	mov	w19, 8
.L3386:
	ldr	w2, [x22]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	mov	w0, -1
	cmp	w2, w1
	bne	.L3422
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
.L3390:
	ldr	w1, [x0]
	cbnz	w1, .L3387
	ldr	w1, [x22,w3,uxtw 2]
	add	w3, w3, 1
	cmp	w3, w4
	sub	w2, w2, #1
	str	w1, [x0,512]
	csel	w3, w3, wzr, cc
	cmp	w2, 4096
	sub	x0, x0, #4
	bne	.L3390
	b	.L3389
.L3387:
	adrp	x0, .LC168
	add	x0, x0, :lo12:.LC168
	bl	printk
.L3389:
	adrp	x0, .LC169
	mov	w3, 5
	add	x20, x28, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC169
	mov	x1, x23
	mov	w2, 4
	adrp	x24, .LANCHOR4
	bl	rknand_print_hex
	ldr	w1, [x22,512]
	adrp	x0, .LC170
	ldrb	w2, [x20,73]
	add	x0, x0, :lo12:.LC170
	bl	printk
	add	x4, x24, :lo12:.LANCHOR4
	adrp	x0, .LC171
	ldrh	w3, [x20,194]
	mov	w1, w19
	add	x0, x0, :lo12:.LC171
	mov	w2, w19
	ldr	w4, [x4,92]
	bl	printk
	str	x24, [x29,208]
	ldrb	w0, [x20,73]
	ldr	w1, [x22,512]
	cmp	w1, w0
	bls	.L3391
	str	w0, [x22,512]
.L3391:
	lsl	w0, w19, 7
	str	w0, [x29,204]
	adrp	x0, .LC172
	mov	x20, 0
	add	x0, x0, :lo12:.LC172
	str	x0, [x29,192]
	adrp	x0, .LC173
	str	w20, [x29,236]
	add	x0, x0, :lo12:.LC173
	str	x0, [x29,184]
	adrp	x0, .LC174
	add	x0, x0, :lo12:.LC174
	str	x0, [x29,176]
.L3404:
	add	x0, x28, :lo12:.LANCHOR0
	ldr	w1, [x23,x20,lsl 2]
	str	w20, [x29,232]
	ldrb	w0, [x0,73]
	cmp	w1, w0
	bcs	.L3392
	ldr	x0, [x29,208]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w0, [x0,92]
	cmp	w1, w0
	bcc	.L3392
	ldr	w0, [x29,224]
	cmp	w0, 1
	cset	w0, hi
	str	w0, [x29,228]
	uxtw	x0, w20
	cbz	w0, .L3393
	ldr	w0, [x29,228]
	cbz	w0, .L3393
	uxtw	x0, w20
	sub	w0, w0, #1
	ldr	w0, [x23,x0,lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	beq	.L3392
.L3393:
	mov	w1, 0
	mov	x2, 512
	mov	x0, x26
	bl	memset
	ldr	w24, [x23,x20,lsl 2]
	add	x0, x28, :lo12:.LANCHOR0
	mov	w2, w19
	mul	w24, w21, w24
	ldr	x1, [x0,744]
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
	bls	.L3394
	mov	w2, 0
	mov	w0, 0
	add	w1, w24, w25
	bl	FlashEraseBlock
	mov	w2, 2
.L3394:
	mov	w0, 0
	str	x2, [x29,216]
	bl	flash_boot_exit_slc_mode
	add	x4, x28, :lo12:.LANCHOR0
	ldr	x2, [x29,216]
	udiv	w8, w24, w25
	mov	w6, 0
	mov	w10, 1073741823
	mov	w11, 61424
	ldr	x1, [x4,744]
	ldrh	w0, [x1,10]
	ldrb	w5, [x1,12]
	lsl	w0, w0, 2
	msub	w8, w8, w25, w24
	mul	w0, w0, w2
	mov	x25, x22
	sdiv	w5, w0, w5
	sub	w0, w24, w8
	str	w0, [x29,216]
.L3395:
	cmp	w6, w5
	bcs	.L3433
	add	w0, w6, w8
	lsr	w0, w0, 2
	uxth	w1, w0
	and	w0, w0, 65535
	cbz	w1, .L3396
	add	w2, w0, 1
	add	x1, x4, 200
	ldrb	w3, [x4,196]
	ldrh	w1, [x1,w2,sxtw 1]
	cbz	w3, .L3397
	ldr	w7, [x4,1872]
	mov	w3, 12336
	movk	w3, 0x5638, lsl 16
	cmp	w7, w3
	csel	w1, w1, w2, ne
.L3397:
	add	w1, w1, w10
	lsl	w1, w1, 2
	str	w1, [x29,240]
.L3396:
	add	x1, x4, 200
	str	w11, [x29,244]
	ldrh	w7, [x1,w0,sxtw 1]
	ldrb	w1, [x4,196]
	cbz	w1, .L3398
	ldr	w2, [x4,1872]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w7, w7, w0, ne
.L3398:
	ldr	w0, [x29,216]
	ldrb	w12, [x4,1944]
	str	x11, [x29,104]
	madd	w2, w7, w27, w0
	ldr	x0, [x29,208]
	str	x10, [x29,112]
	add	x0, x0, :lo12:.LANCHOR4
	str	x6, [x29,120]
	str	x8, [x29,128]
	str	x5, [x29,136]
	ldrb	w0, [x0,98]
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
	ldr	x0, [x4,744]
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
	b	.L3395
.L3433:
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
.L3400:
	ldr	w4, [x29,204]
	mov	w3, w0
	cmp	w0, w4
	bcs	.L3434
	mov	x27, x2
	mov	x25, x1
	mov	x24, x0
	add	x2, x2, 4
	add	x0, x0, 1
	add	x1, x1, 4
	ldr	w5, [x27]
	ldr	w4, [x25]
	cmp	w5, w4
	beq	.L3400
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
	adrp	x0, .LC175
	add	x1, x22, x24
	add	x0, x0, :lo12:.LC175
	mov	w2, 4
	mov	w3, 256
	bl	rknand_print_hex
	adrp	x0, .LC176
	add	x1, x26, x24
	mov	w2, 4
	mov	w3, 256
	add	x0, x0, :lo12:.LC176
	bl	rknand_print_hex
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	w1, [x23,x20,lsl 2]
	mov	w0, 0
	mov	w2, 0
	mul	w1, w21, w1
	bl	FlashEraseBlock
	ldr	w0, [x29,228]
	cbz	w0, .L3401
	ldr	w1, [x23,x20,lsl 2]
	mov	w0, 0
	mov	w2, 0
	madd	w1, w1, w21, w21
	bl	FlashEraseBlock
.L3401:
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	adrp	x0, .LC177
	ldr	w1, [x23,x20,lsl 2]
	add	x0, x0, :lo12:.LC177
	bl	printk
	b	.L3392
.L3434:
	ldr	w0, [x29,236]
	add	w0, w0, 1
	str	w0, [x29,236]
.L3392:
	add	x20, x20, 1
	cmp	x20, 5
	bne	.L3404
	mov	x0, x26
	bl	ftl_free
	ldr	w0, [x29,236]
	cmp	w0, wzr
	csetm	w0, eq
.L3422:
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
.L3436:
	cmp	w1, w2
	bls	.L3438
	ldrb	w4, [x5,x2]
	add	x6, x3, 3304
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3436
.L3438:
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
	bne	.L3440
	ldr	w1, [x2]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L3440
	add	x22, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x22,-32]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	str	x0, [x22,-24]
	mov	w2, 59392
	mov	w1, 0
	movk	w2, 0x3, lsl 16
	bl	ftl_memset
	str	w19, [x22,-16]
.L3440:
	add	x23, x20, :lo12:.LANCHOR5
	adrp	x0, .LC178
	ldr	w2, [x24]
	add	x0, x0, :lo12:.LC178
	mov	w3, w19
	mov	w4, w21
	ldr	x1, [x23,-24]
	bl	printk
	ldrb	w0, [x23,-32]
	cbz	w0, .L3439
	sub	w0, w19, #64
	ldr	x22, [x23,-24]
	cmp	w0, 499
	bhi	.L3442
	mov	w3, 564
	ubfiz	x0, x0, 9, 25
	sub	w3, w3, w19
	add	x0, x22, x0
	cmp	w3, w21
	mov	x1, x24
	csel	w2, w3, w21, ls
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L3443
.L3442:
	cmp	w19, 563
	bhi	.L3453
.L3443:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,-16]
	cmp	w1, w19
	beq	.L3451
	strb	wzr, [x0,-32]
	cbz	x22, .L3452
	mov	x0, x22
	bl	ftl_free
.L3452:
	add	x0, x20, :lo12:.LANCHOR5
	str	xzr, [x0,-24]
.L3451:
	add	x20, x20, :lo12:.LANCHOR5
	add	w19, w19, w21
	str	w19, [x20,-16]
	b	.L3439
.L3453:
	ldr	w2, [x23,-16]
	mov	w0, 500
	sub	w2, w2, #64
	cmp	w2, 500
	csel	w2, w2, w0, ls
	adrp	x0, .LANCHOR0+744
	ldr	x0, [x0,#:lo12:.LANCHOR0+744]
	ldrb	w0, [x0,9]
	cmp	w0, 4
	beq	.L3454
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
	b	.L3445
.L3454:
	mov	x3, 0
.L3444:
	cmp	w2, 256
	add	x0, x29, 72
	bls	.L3446
	lsl	w1, w3, 1
	str	w1, [x0,x3,lsl 2]
	b	.L3447
.L3446:
	str	w3, [x0,x3,lsl 2]
.L3447:
	add	x3, x3, 1
	cmp	x3, 5
	bne	.L3444
.L3445:
	add	x0, x22, 253952
	mov	w3, 63872
	add	x0, x0, 1536
.L3450:
	ldr	w1, [x0]
	cbz	w1, .L3448
	add	w3, w3, 128
	lsl	w0, w3, 2
	b	.L3449
.L3448:
	sub	w3, w3, #1
	sub	x0, x0, #4
	cmp	w3, 4096
	bne	.L3450
	lsl	w0, w2, 9
.L3449:
	mov	x1, x22
	add	x2, x29, 72
	add	x22, x20, :lo12:.LANCHOR5
	bl	write_idblock
	ldr	x0, [x22,-24]
	strb	wzr, [x22,-32]
	bl	ftl_free
	str	xzr, [x22,-24]
	b	.L3451
.L3439:
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
	bhi	.L3464
	cbnz	w21, .L3464
	mov	w0, w1
	mov	w1, w2
	mov	x2, x3
	str	x3, [x29,56]
	bl	write_loader_lba
	ldr	x3, [x29,56]
.L3464:
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
	beq	.L3473
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3474
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3475
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3476
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3477
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3478
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3535
	bl	rknand_dev_flush
	b	.L3569
.L3478:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3480
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3481
	b	.L3535
.L3476:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3482
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3483
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3484
	b	.L3535
.L3483:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3482
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3482
	b	.L3535
.L3474:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3485
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3486
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3487
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3488
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3489
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3490
	b	.L3535
.L3486:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3491
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3492
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3493
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3494
	b	.L3535
.L3492:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3494
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3491
	b	.L3535
.L3488:
	adrp	x0, .LC179
	add	x0, x0, :lo12:.LC179
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3495
.L3500:
	mov	x0, -12
	b	.L3472
.L3495:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3576
	adrp	x0, .LC181
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC181
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3573
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
	cbz	x0, .L3579
	adrp	x0, .LC182
	add	x0, x0, :lo12:.LC182
.L3571:
	bl	printk
.L3573:
	mov	x0, x19
.L3574:
	bl	ftl_free
.L3572:
	mov	x0, -14
	b	.L3472
.L3487:
	adrp	x0, .LC183
	add	x0, x0, :lo12:.LC183
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3500
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3576
	adrp	x0, .LC184
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC184
	bl	printk
	adrp	x0, .LANCHOR5
	add	x22, x0, :lo12:.LANCHOR5
	mov	x20, x0
	ldr	x21, [x22,-8]
	cbnz	x21, .L3502
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x22,-8]
	cbz	x0, .L3573
	mov	w1, w21
	mov	x2, 260096
	bl	memset
.L3502:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3573
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3573
	add	x20, x20, :lo12:.LANCHOR5
	uxtw	x2, w2
	ldr	x1, [x20,-8]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3579:
	mov	x0, x19
	bl	ftl_free
	b	.L3569
.L3490:
	adrp	x0, .LC185
	add	x0, x0, :lo12:.LC185
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3500
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3505
.L3576:
	adrp	x0, .LC180
	add	x0, x0, :lo12:.LC180
	b	.L3571
.L3505:
	ldr	w1, [x19]
	adrp	x0, .LC186
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC186
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3573
	adrp	x20, .LANCHOR5
	add	x20, x20, :lo12:.LANCHOR5
	ldr	x0, [x20,-8]
	cbz	x0, .L3573
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3508
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3472
.L3508:
	bl	rknand_device_lock
	ldr	x1, [x20,-8]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,-8]
	bl	ftl_free
	str	xzr, [x20,-8]
	b	.L3579
.L3489:
	adrp	x0, .LC187
	add	x0, x0, :lo12:.LC187
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3500
	bl	ftl_read_flash_info
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3581
.L3473:
	adrp	x0, .LC188
	add	x0, x0, :lo12:.LC188
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3500
	mov	w1, 0
	mov	w2, 64
	bl	ftl_memset
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3581
.L3485:
	adrp	x0, .LC189
	add	x0, x0, :lo12:.LC189
	bl	printk
	adrp	x0, .LANCHOR5
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR5]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
.L3564:
	bl	rk_copy_to_user
	cbnz	x0, .L3572
	b	.L3569
.L3493:
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3500
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
.L3581:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3574
	bl	ftl_free
	b	.L3499
.L3477:
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3513
.L3518:
	adrp	x0, .LC180
	add	x0, x0, :lo12:.LC180
	bl	printk
	b	.L3572
.L3513:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3514
.L3515:
	mov	x19, -1
	b	.L3499
.L3514:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3515
	adrp	x1, .LANCHOR5
	add	x0, x29, 56
	add	x21, x1, :lo12:.LANCHOR5
	mov	x2, 512
	ldr	x1, [x1,#:lo12:.LANCHOR5]
	bl	memcpy
	ldr	w1, [x21,8]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3516
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3516:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
	b	.L3578
.L3480:
	adrp	x0, .LC192
	add	x0, x0, :lo12:.LC192
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3518
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3515
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3515
	adrp	x2, .LANCHOR5
	mov	w1, 5161
	add	x0, x2, :lo12:.LANCHOR5
	movk	w1, 0xc059, lsl 16
	ldr	w3, [x0,8]
	mov	x0, -2
	cmp	w3, w1
	bne	.L3472
	ldr	w1, [x29,68]
	mov	x0, -3
	sub	w3, w1, #1
	cmp	w3, 127
	bhi	.L3472
	ldr	x19, [x2,#:lo12:.LANCHOR5]
	add	x0, x19, 64
	str	w1, [x19,12]
	add	x1, x29, 120
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3570
.L3484:
	adrp	x0, .LC193
	add	x0, x0, :lo12:.LC193
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3518
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3515
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3515
	adrp	x19, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR5
	ldr	w1, [x0,12]
	cbnz	w1, .L3519
.L3522:
	mov	x0, 0
	b	.L3472
.L3519:
	ldr	x1, [x0,16]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3520
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,16]
	str	w2, [x1,4]
	ldr	x0, [x0,16]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3520:
	add	x0, x19, :lo12:.LANCHOR5
	adrp	x20, .LANCHOR5
	ldr	x1, [x0,16]
	mov	w0, 0
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x19,#:lo12:.LANCHOR5]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3521
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,#:lo12:.LANCHOR5]
	str	w1, [x0,4]
	ldr	x0, [x20,#:lo12:.LANCHOR5]
	str	wzr, [x0,8]
.L3521:
	add	x20, x19, :lo12:.LANCHOR5
	ldr	x19, [x19,#:lo12:.LANCHOR5]
	mov	w1, 0
	mov	x2, 128
	add	x0, x19, 64
	str	wzr, [x19,12]
	bl	memset
	mov	w0, 1
	mov	x1, x19
	bl	StorageSysDataStore
	str	wzr, [x20,12]
	str	wzr, [x20,8]
	b	.L3569
.L3475:
	adrp	x0, .LC194
	add	x0, x0, :lo12:.LC194
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3518
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3515
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3515
	adrp	x19, .LANCHOR5
	add	x1, x19, :lo12:.LANCHOR5
	ldr	w2, [x1,12]
	cmp	w2, 1
	beq	.L3522
	ldr	x2, [x1,16]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3523
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,16]
	str	w3, [x2,4]
	ldr	x1, [x1,16]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3523:
	add	x0, x19, :lo12:.LANCHOR5
	adrp	x20, .LANCHOR5
	ldr	x1, [x0,16]
	mov	w0, 1
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x19,#:lo12:.LANCHOR5]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3524
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,#:lo12:.LANCHOR5]
	str	w1, [x0,4]
	ldr	x0, [x20,#:lo12:.LANCHOR5]
	str	wzr, [x0,8]
.L3524:
	add	x20, x19, :lo12:.LANCHOR5
	ldr	x19, [x19,#:lo12:.LANCHOR5]
	mov	w1, 0
	mov	x2, 128
	add	x0, x19, 64
	str	wzr, [x19,12]
	bl	memset
	mov	w0, 1
	mov	x1, x19
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x20,12]
	b	.L3569
.L3481:
	adrp	x0, .LC195
	add	x0, x0, :lo12:.LC195
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3518
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3515
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3515
	adrp	x1, .LANCHOR5
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR5
	uxtw	x2, w2
	add	x1, x1, 24
	b	.L3577
.L3482:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3525
	adrp	x0, .LC196
	add	x0, x0, :lo12:.LC196
	b	.L3567
.L3525:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3527
	adrp	x0, .LC197
	add	x0, x0, :lo12:.LC197
	b	.L3567
.L3527:
	adrp	x0, .LC198
	add	x0, x0, :lo12:.LC198
.L3567:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3518
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3572
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR5
	bne	.L3528
	ldr	x0, [x0,#:lo12:.LANCHOR5]
	add	x1, x29, 56
	mov	x2, 16
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3472
	b	.L3572
.L3528:
	add	x20, x0, :lo12:.LANCHOR5
	ldr	w1, [x20,536]
	cmp	w1, 10
	bhi	.L3572
	ldr	x1, [x0,#:lo12:.LANCHOR5]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3529
	cbz	w3, .L3529
	adrp	x0, .LC199
	mov	w1, w2
	add	x0, x0, :lo12:.LC199
	bl	printk
	ldr	w0, [x20,536]
	add	w0, w0, 1
	str	w0, [x20,536]
	b	.L3572
.L3529:
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,536]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3530
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3531
.L3530:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3531:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3569
	b	.L3499
.L3494:
	adrp	x0, .LC200
	add	x0, x0, :lo12:.LC200
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3518
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3515
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3515
	mov	w0, 30224
	adrp	x1, .LANCHOR5
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR5
	add	x0, x29, 64
	bne	.L3532
	ldr	x1, [x1,544]
	b	.L3568
.L3532:
	ldr	x1, [x1,552]
.L3568:
	add	x1, x1, 8
.L3577:
	bl	memcpy
.L3578:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
	b	.L3564
.L3491:
	adrp	x0, .LC201
	add	x0, x0, :lo12:.LC201
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3518
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3515
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3515
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	bne	.L3534
	ldr	x0, [x19,544]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,544]
	b	.L3570
.L3534:
	ldr	x0, [x19,552]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,552]
	mov	w0, 3
.L3570:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3499
.L3569:
	mov	x19, 0
.L3499:
	adrp	x0, .LC202
	mov	x1, x19
	add	x0, x0, :lo12:.LC202
	bl	printk
	mov	x0, x19
	b	.L3472
.L3535:
	mov	x0, -22
.L3472:
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
	ldr	x1, [x19,-56]
	str	w0, [x19,-16]
	add	x0, x1, 512
	str	x0, [x20,#:lo12:.LANCHOR5]
	add	x0, x1, 1024
	str	x1, [x19,16]
	str	x0, [x19,544]
	add	x1, x1, 1536
	add	x0, x19, 24
	strb	wzr, [x19,-32]
	str	xzr, [x19,-24]
	str	xzr, [x19,-8]
	str	x1, [x19,552]
	bl	ftl_memcpy
	ldr	x21, [x19,16]
	str	wzr, [x19,8]
	str	wzr, [x19,536]
	ldr	w0, [x21,16]
	ldr	w22, [x21,508]
	str	w0, [x19,12]
	cbz	w22, .L3584
	mov	x0, x21
	mov	w1, 508
	bl	js_hash
	cmp	w22, w0
	beq	.L3584
	adrp	x0, .LC203
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC203
	str	wzr, [x19,12]
	bl	printk
.L3584:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,12]
	cbz	w1, .L3585
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,8]
.L3585:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 2
	ldr	x1, [x20,544]
	bl	StorageSysDataLoad
	ldr	x1, [x20,552]
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
	str	x0, [x1,560]
	cbz	x0, .L3594
	mov	w23, 0
	adrp	x24, .LC204
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC204
.L3598:
	add	x20, x21, :lo12:.LANCHOR5
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,560]
	bl	FlashBootVendorRead
	cbnz	w0, .L3595
	ldr	x3, [x20,560]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,560]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3596
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3596
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L3596:
	cbnz	w22, .L3606
	mov	w22, 1
	b	.L3598
.L3606:
	cbz	w19, .L3599
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L3594
	b	.L3595
.L3599:
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
	b	.L3594
.L3595:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w19, -1
	ldr	x0, [x21,560]
	bl	kfree
	str	xzr, [x21,560]
.L3594:
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
	adrp	x3, .LANCHOR5+560
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR5+560]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3608
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3609:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3613
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3609
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
	b	.L3608
.L3613:
	mov	w3, -1
.L3608:
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
	adrp	x0, .LANCHOR5+560
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR5+560]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3615
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3616:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3631
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3616
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3617
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3615
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3618:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3632
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
	b	.L3618
.L3632:
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
	b	.L3630
.L3617:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3630
.L3631:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3615
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
.L3630:
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
.L3615:
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
	cbz	x0, .L3634
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3636
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3637
	b	.L3648
.L3636:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3648
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3639
.L3640:
	mov	x20, -1
	b	.L3635
.L3639:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3640
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3635
.L3637:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3648
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3640
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3640
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3648
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3635
.L3648:
	mov	x20, -14
.L3635:
	mov	x0, x19
	bl	kfree
.L3634:
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
	.type	__func__.19599, %object
	.size	__func__.19599, 11
__func__.19599:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.20374, %object
	.size	__func__.20374, 17
__func__.20374:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.20393, %object
	.size	__func__.20393, 16
__func__.20393:
	.string	"FtlScanAllBlock"
	.type	__func__.20659, %object
	.size	__func__.20659, 17
__func__.20659:
	.string	"ftl_scan_all_ppa"
	.zero	7
	.type	__func__.20342, %object
	.size	__func__.20342, 12
__func__.20342:
	.string	"FtlCheckVpc"
	.zero	4
	.type	__func__.20641, %object
	.size	__func__.20641, 21
__func__.20641:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.19672, %object
	.size	__func__.19672, 8
__func__.19672:
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
	.string	"FTL version: 5.0.56 20190508"
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
	.string	"...%s enter...\n"
.LC107:
	.string	"superBlkID = %x vpc=%x\n"
.LC108:
	.string	"flashmode = %x pagenum = %x %x\n"
.LC109:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC110:
	.string	"blk = %x vpc=%x mode = %x\n"
.LC111:
	.string	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC112:
	.string	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC113:
	.string	"ftl_scan_all_ppa blk %x page %x\n"
.LC114:
	.string	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\n"
.LC115:
	.string	"%s finished\n"
.LC116:
	.string	"FlashMakeFactorBbt %d\n"
.LC117:
	.string	"bad block:%d %d\n"
.LC118:
	.string	"FMFB:%d %d\n"
.LC119:
	.string	"E:bad block:%d\n"
.LC120:
	.string	"FMFB:Save %d %d\n"
.LC121:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC122:
	.string	"FtlBbmTblFlush error:%x\n"
.LC123:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC124:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC125:
	.string	"decrement_vpc_count %x = %d\n"
.LC126:
	.string	"decrement_vpc_count %x = %d in free list\n"
.LC127:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC128:
	.string	"page map lost: %x %x\n"
.LC129:
	.string	"FtlMapWritePage error = %x\n"
.LC130:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC131:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC132:
	.string	"no ect"
.LC133:
	.string	"slc mode"
.LC134:
	.string	"BBT:"
.LC135:
	.string	"region_id = %x phyAddr = %x\n"
.LC136:
	.string	"map_ppn:"
.LC137:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC138:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC139:
	.string	"free blk vpc error %x = %x  %x\n"
.LC140:
	.string	"Ftlscanalldata = %x\n"
.LC141:
	.string	"scan lpa = %x ppa= %x\n"
.LC142:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC143:
	.string	"RSB refresh addr %x\n"
.LC144:
	.string	"spuer block %x vpn is 0\n "
.LC145:
	.string	"g_recovery_ppa %x ver %x\n "
.LC146:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC147:
	.string	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
.LC148:
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC149:
	.string	"GC des block %x done\n"
.LC150:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC151:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC152:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC153:
	.string	"rk_ftl_de_init %x\n"
.LC154:
	.string	"...%s: no bad block mapping table, format device\n"
.LC155:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
.LC156:
	.string	"FtlInit %x\n"
.LC157:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC158:
	.string	"erase power lost blk = %x vpc=%x\n"
.LC159:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC160:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\n"
.LC161:
	.string	":"
.LC162:
	.string	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC163:
	.string	"Mblk:"
.LC164:
	.string	"L2P:"
.LC165:
	.string	"L2PC:"
.LC166:
	.string	"IdBlockReadData %x %x\n"
.LC167:
	.string	"IdBlockReadData %x %x ret= %x\n"
.LC168:
	.string	"write_idblock fix data %x %x\n"
.LC169:
	.string	"idblk:"
.LC170:
	.string	"idb reverse %x %x\n"
.LC171:
	.string	"write_idblock totle_sec %x %x %x %x\n"
.LC172:
	.string	"IDBlockWriteData %x %x\n"
.LC173:
	.string	"IDBlockWriteData %x %x ret= %x\n"
.LC174:
	.string	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x\n"
.LC175:
	.string	"write"
.LC176:
	.string	"read"
.LC177:
	.string	"write_idblock error %d\n"
.LC178:
	.string	"wl_lba %p %x %x %x\n"
.LC179:
	.string	"READ_SECTOR_IO\n"
.LC180:
	.string	"rk_copy_from_user error\n"
.LC181:
	.string	"READ_SECTOR_IO %x %x\n"
.LC182:
	.string	"rk_copy_to_user error\n"
.LC183:
	.string	"WRITE_SECTOR_IO\n"
.LC184:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC185:
	.string	"END_WRITE_SECTOR_IO\n"
.LC186:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC187:
	.string	"GET_FLASH_INFO_IO\n"
.LC188:
	.string	"GET_BAD_BLOCK_IO\n"
.LC189:
	.string	"GET_LOCK_FLAG_IO\n"
.LC190:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC191:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC192:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC193:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC194:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC195:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC196:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC197:
	.string	"RKNAND_LOADER_STATUS\n"
.LC198:
	.string	"RKNAND_LOADER_LOCK\n"
.LC199:
	.string	"LockKey not match %d\n"
.LC200:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC201:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC202:
	.string	"return ret = %lx\n"
.LC203:
	.string	"secureBootEn check error\n"
.LC204:
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
	.size	slcPageToMlcPageTbl, 512
slcPageToMlcPageTbl:
	.zero	512
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
	.size	mlcPageToSlcPageTbl, 1024
mlcPageToSlcPageTbl:
	.zero	1024
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
