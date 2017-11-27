/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2017-11-20
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
	.type	ftl_set_blk_mode.part.8, %function
ftl_set_blk_mode.part.8:
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
	.size	ftl_set_blk_mode.part.8, .-ftl_set_blk_mode.part.8
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	mov	x5, x0
	adrp	x0, .LANCHOR0+8
	ldrb	w0, [x0,#:lo12:.LANCHOR0+8]
	cbz	w0, .L7
	ldrb	w4, [x5,1]
	mov	w0, 0
	ldrb	w3, [x1,1]
	cmp	w4, w3
	beq	.L4
.L7:
	mov	x3, 0
.L5:
	cmp	w3, w2
	mov	w4, w3
	bcs	.L11
	ldrb	w0, [x5,x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6,-1]
	cmp	w6, w0
	beq	.L5
	add	w0, w4, 1
	b	.L4
.L11:
	mov	w0, 0
.L4:
	ret
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	2
	.global	FlashRsvdBlkChk
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	adrp	x2, .LANCHOR0
	uxtb	w0, w0
	add	x2, x2, :lo12:.LANCHOR0
	ldrb	w3, [x2,9]
	ldr	w2, [x2,12]
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
	adrp	x2, .LANCHOR0+16
	ldrb	w2, [x2,#:lo12:.LANCHOR0+16]
	cbz	w2, .L14
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w19, -1073741824
	csel	w19, w1, w19, ne
.L14:
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
	ldrb	w0, [x0,16]
	cbz	w0, .L21
	mov	w0, w21
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w20, -1073741824
	csel	w20, w1, w20, ne
.L21:
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
	ldrb	w2, [x3,104]
	lsr	w3, w1, 16
	cmp	w2, 1
	ldrb	w2, [x5,8]
	and	w5, w1, 255
	add	x2, x6, x2, lsl 8
	bne	.L29
	mov	w6, 6
	str	w6, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w5, [x2,2052]
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	b	.L31
.L29:
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
.L31:
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
	ldrb	w2, [x1,120]
	cbz	w2, .L32
	sbfiz	x0, x0, 4, 32
	add	x1, x1, 24
	add	x2, x1, x0
	ldr	x1, [x1,x0]
	ldrb	w0, [x2,8]
	add	x0, x0, 8
	add	x0, x1, x0, lsl 8
	mov	w1, 218
	str	w1, [x0,8]
.L32:
	ret
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	2
	.global	flash_exit_slc_mode
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	adrp	x1, .LANCHOR0
	uxtb	w0, w0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w2, [x1,120]
	cbz	w2, .L37
	sbfiz	x0, x0, 4, 32
	add	x1, x1, 24
	add	x2, x1, x0
	ldr	x1, [x1,x0]
	ldrb	w0, [x2,8]
	add	x0, x0, 8
	add	x0, x1, x0, lsl 8
	mov	w1, 223
	str	w1, [x0,8]
.L37:
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
	cbz	w2, .L44
	add	x2, x4, x0, lsl 8
	mov	w5, 96
	str	w5, [x2,2056]
	and	w5, w1, 255
	str	w5, [x2,2052]
	lsr	w5, w1, 8
	str	w5, [x2,2052]
	lsr	w5, w1, 16
	str	w5, [x2,2052]
	ldr	w2, [x3,12]
	add	w1, w1, w2
.L44:
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
	ldrb	w1, [x1,98]
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
.L52:
	cmp	w1, w2
	bls	.L54
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L52
.L54:
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
	.global	FlashPrintInfo
	.type	FlashPrintInfo, %function
FlashPrintInfo:
	ret
	.size	FlashPrintInfo, .-FlashPrintInfo
	.align	2
	.global	FlashSuspend
	.type	FlashSuspend, %function
FlashSuspend:
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
	mov	w0, 0
	ret
	.size	FlashSuspend, .-FlashSuspend
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	adrp	x7, .LANCHOR0
	uxtb	w4, w4
	add	x6, x7, :lo12:.LANCHOR0
	ldrh	w8, [x6,182]
	ldrh	w5, [x6,180]
	ldrh	w9, [x6,12]
	ldrb	w6, [x6,8]
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
	bne	.L60
	add	x1, x7, :lo12:.LANCHOR0
	ldrb	w5, [x1,120]
	cbnz	w5, .L60
	add	x1, x1, 196
	ldrh	w8, [x1,w8,sxtw 1]
.L60:
	add	x7, x7, :lo12:.LANCHOR0
	cmp	w4, 1
	add	x7, x7, 708
	mov	w1, 0
	ldr	w5, [x7,w10,uxtw 2]
	madd	w5, w6, w9, w5
	add	w5, w5, w8
	str	w5, [x2]
	str	w10, [x3]
	bls	.L61
	ldr	w1, [x0,4]
	ldr	w0, [x0,60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w1, eq
.L61:
	mov	w0, w1
	ret
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	FlashScheduleEnSet
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w2, [x1,740]
	str	w0, [x1,740]
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
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	uxtb	w0, w0
	add	x29, sp, 0
	str	x21, [sp,32]
	add	x21, x1, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	tst	w0, 6
	mov	x20, x1
	ldr	x2, [x21,128]
	ldr	w19, [x2]
	beq	.L73
	orr	w19, w19, 24576
	and	w0, w0, 4
	and	w19, w19, -32769
	cmp	w0, wzr
	orr	w19, w19, 196608
	mov	w0, 0
	orr	w1, w19, 32768
	csel	w19, w1, w19, ne
	bl	rknand_get_clk_rate
	ldr	x0, [x21,128]
	mov	w1, 8322
	str	w1, [x0,344]
	mov	w1, 4099
	movk	w1, 0x10, lsl 16
	str	w1, [x0,304]
	mov	w1, 38
	str	w1, [x0,308]
	mov	w1, 39
	str	w1, [x0,308]
	b	.L75
.L73:
	and	w19, w19, -8193
.L75:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,128]
	str	w19, [x0]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
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
	.global	NandcDelayns
	.type	NandcDelayns, %function
NandcDelayns:
	sub	sp, sp, #16
	lsr	w0, w0, 4
	str	w0, [sp,12]
.L81:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L81
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
	add	x1, x1, 24
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
.L85:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,753]
	cmp	w0, w20
	bls	.L91
	add	x0, x19, 256
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	NandcDelayns
	ldrb	w0, [x25,752]
	cmp	w0, 34
	bne	.L86
	ldrsb	w0, [x24,x20]
	b	.L90
.L86:
	cmp	w0, 35
	bne	.L88
	ldrsb	w0, [x21,x20]
	b	.L90
.L88:
	ldrsb	w0, [x23,400]
.L90:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L85
.L91:
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
.L93:
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,753]
	cmp	w0, w20
	bls	.L95
	str	w24, [x21,8]
	add	x0, x19, 408
	str	wzr, [x21]
	ldrsb	w0, [x20,x0]
	str	w0, [x21]
	ldrsb	w0, [x22,x20]
	add	x20, x20, 1
	str	w0, [x21]
	mov	w0, 300
	bl	NandcDelayns
	b	.L93
.L95:
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x27, [sp,80]
	add	x19, x20, :lo12:.LANCHOR0
	uxtb	w27, w1
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	uxtb	x25, w0
	mov	x22, x2
	ldr	x0, [x19,744]
	mov	x26, x25
	uxtb	w21, w3
	add	x19, x19, 760
	ldrb	w0, [x0,19]
	cmp	w0, 6
	bne	.L97
	add	x19, x19, x25, lsl 6
	lsl	w4, w21, 2
	add	x19, x19, 20
	b	.L102
.L97:
	cmp	w0, 7
	bne	.L99
	mov	x2, 160
	mov	w4, 10
	madd	x19, x25, x2, x19
	mul	w4, w21, w4
	add	x19, x19, 28
.L102:
	add	x19, x19, x4, sxtw
	b	.L98
.L99:
	lsl	x4, x25, 3
	add	x4, x4, x21, sxtw
	add	x19, x19, x4, lsl 3
	add	x19, x19, 20
.L98:
	add	x2, x20, :lo12:.LANCHOR0
	lsl	x0, x25, 4
	add	x2, x2, 24
	uxtb	x27, w27
	add	x1, x2, x0
	ldr	x23, [x2,x0]
	mov	w0, w26
	ldrb	w24, [x1,8]
	bl	NandcFlashCs
	ubfiz	x24, x24, 8, 8
	mov	w0, 54
	add	x23, x23, x24
	mov	x24, 0
	str	w0, [x23,2056]
.L100:
	cmp	x24, x27
	beq	.L103
	ldrb	w0, [x22,x24]
	str	w0, [x23,2052]
	mov	w0, 200
	bl	NandcDelayns
	ldrsb	w0, [x19,x24]
	add	x24, x24, 1
	str	w0, [x23,2048]
	b	.L100
.L103:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, 22
	add	x25, x20, x25
	str	w0, [x23,2056]
	mov	w0, w26
	bl	NandcFlashDeCs
	strb	w21, [x25,1616]
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
	cmp	w0, 6
	bhi	.L104
	add	x21, x19, 1620
	add	x22, x19, 764
.L109:
	lsl	x1, x20, 3
	uxtb	w0, w20
	ldrb	w1, [x1,x21]
	cmp	w1, 173
	bne	.L106
	ldrb	w1, [x19,761]
	mov	x2, x22
	mov	w3, 0
	bl	HynixSetRRPara
.L106:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L109
.L104:
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
	add	x5, x4, 24
	stp	x19, x20, [sp,16]
	add	x6, x5, x0
	uxtb	w2, w2
	ldr	x20, [x5,x0]
	ldr	x0, [x4,744]
	ldrb	w19, [x6,8]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	bne	.L112
	add	x4, x4, 88
	cbnz	w2, .L113
	ldrb	w2, [x4,13]
	b	.L121
.L113:
	ldrb	w2, [x4,14]
.L121:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,103]
	cbz	w4, .L116
	add	x3, x19, 8
	lsl	x3, x3, 8
.L120:
	cmp	w2, w4
	bcs	.L116
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L120
.L112:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L116:
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
.L126:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L126
	tbz	x3, 6, .L126
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
	add	x0, x20, 1652
	add	x19, x0, x1
	ldrb	w21, [x0,x1]
	ldr	x0, [x19,8]
	cbz	x0, .L131
	mov	w0, w21
	add	x20, x20, 708
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
	cbz	x1, .L131
	str	w0, [x1]
	str	xzr, [x19,16]
.L131:
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
	add	x1, x1, 24
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	ldr	x20, [x1,x0]
	movk	w19, 0x1, lsl 16
.L138:
	mov	w0, 100
	bl	NandcDelayns
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L139
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
	add	x7, x1, 24
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
.L154:
	ldrb	w3, [x6,x4]
	ldr	x1, [x7]
	cmp	w3, 152
	ldrb	w2, [x7,8]
	beq	.L144
	cmp	w3, 69
	beq	.L144
	cmp	w3, 173
	beq	.L144
	cmp	w3, 44
	bne	.L145
.L144:
	cmp	w0, 1
	bne	.L147
	cbz	w11, .L145
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L148
	str	w0, [x1,2052]
	b	.L165
.L148:
	cmp	w3, 44
	bne	.L150
	str	w0, [x1,2052]
	str	w14, [x1,2048]
	b	.L152
.L150:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L152
.L147:
	cbz	w5, .L145
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L151
	str	w10, [x1,2052]
	str	w13, [x1,2048]
	b	.L152
.L151:
	cmp	w3, 44
	bne	.L153
	str	w10, [x1,2052]
	str	w12, [x1,2048]
	b	.L152
.L153:
	str	w9, [x1,2052]
.L165:
	str	wzr, [x1,2048]
.L152:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L145:
	add	x6, x6, 8
	add	x7, x7, 16
	cmp	x6, 32
	bne	.L154
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
	adrp	x5, .LANCHOR0
	smull	x1, w19, w1
	add	x0, x2, 256
	add	x2, x2, 304
	add	x0, x0, x1
	add	x1, x2, x1
	mov	x2, 0
.L169:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,753]
	cmp	w4, w2
	bls	.L174
	ldrb	w3, [x3,752]
	cmp	w3, 67
	bne	.L170
	ldrsb	w3, [x0,x2]
	b	.L173
.L170:
	ldrsb	w3, [x1,x2]
.L173:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L169
.L174:
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
	adrp	x23, .LC0
	stp	x19, x20, [sp,16]
	mov	w24, w1
	mov	x20, x0
	mov	w22, 0
	add	x25, x21, 1652
	add	x23, x23, :lo12:.LC0
.L176:
	cmp	w22, w24
	beq	.L185
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,1845]
	cmp	w2, w0
	bcc	.L177
	mov	w0, -1
	str	w0, [x20]
	b	.L178
.L177:
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
	ldr	w2, [x21,12]
	add	w1, w2, w1
	mov	w2, 0
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29,88]
	mov	w0, w19
	bl	FlashReadStatus
	tbz	x0, 0, .L179
	mov	w0, -1
	str	w0, [x20]
.L179:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L180
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L180:
	mov	w0, w19
	bl	NandcFlashDeCs
.L178:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L176
.L185:
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
	add	x4, x3, 24
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
	bne	.L187
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
	b	.L188
.L187:
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
.L188:
	mov	w0, w21
	bl	FlashSetRandomizer
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	2
	.global	FlashDeInit
	.type	FlashDeInit, %function
FlashDeInit:
	stp	x29, x30, [sp, -32]!
	mov	w0, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	adrp	x0, .LANCHOR0
	add	x20, x0, :lo12:.LANCHOR0
	mov	x19, x0
	ldrb	w1, [x20,1856]
	cbz	w1, .L190
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L190
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1856]
.L190:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,24]
	str	wzr, [x0,336]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashDeInit, .-FlashDeInit
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
	ble	.L199
	ldr	x0, [x1,128]
	mov	w1, 8354
	b	.L206
.L199:
	cmp	w0, 220
	ble	.L201
	ldr	x0, [x1,128]
	b	.L207
.L201:
	cmp	w0, 185
	ble	.L202
	ldr	x0, [x1,128]
	mov	w1, 4226
	b	.L206
.L202:
	cmp	w0, 160
	ldr	x0, [x1,128]
	ble	.L203
	mov	w1, 4194
	b	.L206
.L203:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L206
	cmp	w19, 99
	mov	w1, 4225
	bls	.L206
.L207:
	mov	w1, 8322
.L206:
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
	bls	.L209
	mov	w1, 8322
	cmp	w0, w1
	bne	.L210
.L209:
	adrp	x1, .LANCHOR0+128
	ldr	x1, [x1,#:lo12:.LANCHOR0+128]
	str	w0, [x1,4]
.L210:
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
	mov	w2, 1
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	w2, [x19,48]
	mov	w2, 2
	str	x0, [x19,24]
	str	x0, [x19,40]
	str	w2, [x19,64]
	mov	w2, 3
	str	x0, [x19,56]
	str	w2, [x19,80]
	str	x0, [x19,72]
	str	x0, [x19,128]
	str	wzr, [x19,32]
	ldr	w1, [x0]
	ubfx	x2, x1, 13, 1
	str	w2, [x19,1860]
	ldr	w2, [x0,352]
	and	w1, w1, 245760
	orr	w1, w1, 256
	ubfx	x2, x2, 16, 4
	str	w2, [x19,1864]
	ldr	w2, [x0,352]
	str	w2, [x19,1868]
	str	w1, [x0]
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
	str	wzr, [x19,1920]
	str	x0, [x19,1872]
	str	x0, [x19,1880]
	add	x0, x0, 32768
	str	wzr, [x19,1928]
	str	x0, [x19,1888]
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
	str	w0, [x1,1932]
	mov	w1, 4096
	str	w3, [x2,8]
	bne	.L214
.L217:
	and	w1, w1, -17
	b	.L215
.L214:
	cmp	w0, 24
	bne	.L216
	orr	w1, w1, 16
	b	.L215
.L216:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L217
.L215:
	orr	w1, w1, 1
	str	w1, [x2,12]
	ret
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0+1936
	uxtb	w0, w0
	add	x29, sp, 0
	strb	w0, [x1,#:lo12:.LANCHOR0+1936]
	bl	NandcBchSel
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashBchSel, .-FlashBchSel
	.align	2
	.global	FlashResume
	.type	FlashResume, %function
FlashResume:
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
.L224:
	lsl	x0, x21, 3
	ldrb	w0, [x0,x19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L223
	mov	w0, w21
	bl	FlashReset
.L223:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L224
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,1856]
	cbz	w0, .L225
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,1844]
	bl	NandcSetMode
	ldr	w0, [x19,152]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L225:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,744]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	ldr	x21, [sp,32]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashResume, .-FlashResume
	.align	2
	.global	NandCIrqEnable
	.type	NandCIrqEnable, %function
NandCIrqEnable:
	ldr	w3, [x0,368]
	mov	x2, 1
	lsl	x1, x2, x1
	orr	w3, w3, w1
	str	w3, [x0,368]
	ldr	w2, [x0,364]
	orr	w1, w2, w1
	str	w1, [x0,364]
	ret
	.size	NandCIrqEnable, .-NandCIrqEnable
	.align	2
	.global	NandCIrqDisable
	.type	NandCIrqDisable, %function
NandCIrqDisable:
	ldr	w3, [x0,368]
	mov	x2, 1
	lsl	x1, x2, x1
	orr	w3, w3, w1
	str	w3, [x0,368]
	ldr	w2, [x0,364]
	bic	w1, w2, w1
	str	w1, [x0,364]
	ret
	.size	NandCIrqDisable, .-NandCIrqDisable
	.align	2
	.global	rk_nandc_get_irq_status
	.type	rk_nandc_get_irq_status, %function
rk_nandc_get_irq_status:
	ldr	w0, [x0,372]
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rk_nandc_flash_ready
	.type	rk_nandc_flash_ready, %function
rk_nandc_flash_ready:
	stp	x29, x30, [sp, -16]!
	mov	w1, 1
	add	x29, sp, 0
	bl	NandCIrqDisable
	ldp	x29, x30, [sp], 16
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
	mov	x0, x19
	mov	w1, 1
	bl	NandCIrqEnable
	ldr	w0, [x19]
	tbnz	x0, 9, .L235
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L234
.L235:
	mov	x0, x19
	mov	w1, 1
	bl	NandCIrqDisable
.L234:
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
	ldrb	w2, [x19,8]
	mov	w20, 0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	cbz	w2, .L238
	mov	w1, w23
	bl	FlashEraseSLc2KBlocks
	b	.L239
.L238:
	mov	w28, 56
	add	x25, x19, 1652
	mov	x26, 24
.L264:
	cmp	w20, w23
	bcs	.L265
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
	bcc	.L240
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L241
.L240:
	ldrb	w1, [x19,1937]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w22, w22, wzr, ne
	madd	x1, x1, x26, x25
	ldr	x1, [x1,8]
	cbz	x1, .L243
	bl	FlashWaitCmdDone
.L243:
	ldr	w0, [x29,124]
	ldr	w1, [x29,120]
	madd	x2, x0, x26, x25
	str	x21, [x2,8]
	str	xzr, [x2,16]
	str	w1, [x2,4]
	cbz	w22, .L244
	add	w1, w20, 1
	umull	x1, w1, w28
	add	x1, x24, x1
	str	x1, [x2,16]
.L244:
	add	x1, x19, x0
	mul	x0, x0, x26
	ldrb	w21, [x1,1848]
	strb	w21, [x25,x0]
	mov	w0, w21
	bl	NandcFlashCs
	cmp	w27, 1
	bne	.L245
	ldrb	w0, [x19,120]
	cbz	w0, .L245
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L246
.L245:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L246:
	ldr	w1, [x29,124]
	add	x0, x19, 708
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
.L241:
	add	w20, w20, 1
	b	.L264
.L265:
	ldr	x0, [x29,104]
	mov	x20, 0
	mov	x22, 24
	add	x19, x0, :lo12:.LANCHOR0
	add	x21, x19, 1652
	ldr	x0, [x19,128]
	bl	NandcIqrWaitFlashReady
.L248:
	ldrb	w0, [x19,1845]
	cmp	w0, w20
	bls	.L266
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w27, 1
	bne	.L249
	ldrb	w0, [x19,120]
	cbz	w0, .L249
	mul	x0, x20, x22
	ldrb	w0, [x0,x21]
	bl	flash_exit_slc_mode
.L249:
	add	x20, x20, 1
	b	.L248
.L266:
	mov	w0, 0
.L239:
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
	stp	x29, x30, [sp, -16]!
	mov	w1, 0
	add	x29, sp, 0
	bl	NandCIrqDisable
	ldp	x29, x30, [sp], 16
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
.L270:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L270
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
	stp	x23, x24, [sp,48]
	uxtb	w24, w1
	add	x1, x19, :lo12:.LANCHOR0
	uxtb	w25, w2
	add	x2, x1, 24
	stp	x21, x22, [sp,32]
	mov	x22, x4
	add	x4, x2, x0
	ldr	x21, [x2,x0]
	mov	w2, 16
	ubfiz	w20, w24, 1, 1
	ldrb	w0, [x4,8]
	orr	w20, w20, 8
	ldr	w23, [x21,12]
	bfi	w23, w2, 8, 8
	and	w23, w23, -9
	bfi	w23, w0, 5, 3
	mov	w0, 1
	bfi	w20, w0, 5, 2
	ldr	w0, [x1,1864]
	orr	w20, w20, 536870912
	cmp	w0, 3
	orr	w20, w20, 1024
	bfi	w20, w3, 4, 1
	bls	.L274
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w8, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w8, .L288
	cbz	x22, .L275
.L288:
	cbnz	w24, .L277
.L285:
	mov	x0, x21
	bl	rk_nandc_xfer_irq_flag_init
	mov	x0, x21
	mov	w1, 0
	bl	NandCIrqEnable
	add	w2, w25, 1
	mov	x0, x22
	asr	w2, w2, 1
	bfi	w20, w2, 22, 6
	cbnz	x22, .L279
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1880]
	b	.L279
.L277:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w6, 128
	mov	w3, 0
	lsr	w2, w25, 1
	mov	w4, w3
	mov	w9, -1
	ldr	w0, [x1,1932]
	cmp	w0, 25
	mov	w0, 64
	csel	w6, w0, w6, cc
.L281:
	cmp	w4, w2
	bcs	.L285
	lsr	w0, w3, 2
	ubfiz	x0, x0, 2, 30
	cbz	w8, .L282
	ldrh	w7, [x5,2]
	ldr	x10, [x1,1888]
	ldrh	w11, [x5],4
	orr	w7, w11, w7, lsl 16
	str	w7, [x10,x0]
	b	.L283
.L282:
	ldr	x7, [x1,1888]
	str	w9, [x7,x0]
.L283:
	add	w4, w4, 1
	add	w3, w3, w6
	b	.L281
.L279:
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x25, x20, 22, 5
	mov	w2, w24
	and	x22, x22, 3
	ldr	x1, [x19,1888]
	str	x1, [x19,1904]
	lsl	w1, w25, 10
	str	x0, [x19,1896]
	bl	rknand_dma_map_single
	str	w0, [x19,1912]
	ldr	x0, [x19,1904]
	lsl	w1, w25, 7
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x19,1916]
	mov	w0, 1
	str	w0, [x19,1920]
	ldr	w0, [x19,1912]
	mov	w1, 16
	str	w0, [x21,20]
	ldr	w0, [x19,1916]
	str	w0, [x21,24]
	str	wzr, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 9, 5
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 448
	str	w0, [x29,88]
	cbnz	x22, .L286
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L286:
	ldr	w0, [x29,88]
	cmp	w24, wzr
	cset	w1, eq
	orr	w0, w0, 4
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 1, 1
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 1
	str	w0, [x29,88]
.L275:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L274:
	str	w23, [x21,12]
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
.L293:
	cmp	w2, w0
	bhi	.L295
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L293
.L295:
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
	ldrh	w2, [x1,1944]
	ldrh	w3, [x1,1954]
	str	w0, [x1,1940]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,1952]
	ldr	w0, [x1,1960]
	str	w2, [x1,1948]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,1956]
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
	str	x27, [sp,80]
	mov	x23, x0
	adrp	x19, .LANCHOR0
	ldrh	w6, [x0,8]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x23,10]
	ldrh	w1, [x23,12]
	ldrh	w4, [x23,14]
	strh	w6, [x0,1964]
	strh	w2, [x0,1966]
	strh	w1, [x0,1968]
	strh	w4, [x0,1954]
.L300:
	add	x5, x0, 1976
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L300
	ldrh	w3, [x23,20]
	ldrb	w0, [x23,15]
	cmp	w3, w0
	bcs	.L301
	uxtb	w8, w1
	mov	w3, 0
	mul	w11, w1, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x19, :lo12:.LANCHOR0
.L302:
	cmp	w3, w1
	bcs	.L304
	uxtb	w0, w3
	mov	w5, w3
	mov	w7, 0
.L305:
	cmp	w7, w2
	bcs	.L328
	add	x9, x12, 1976
	add	w13, w5, w11
	add	w14, w0, w8
	add	w7, w7, 1
	strb	w0, [x9,w5,uxtw]
	add	w0, w0, w10
	strb	w14, [x9,x13]
	add	w5, w5, w1
	uxtb	w0, w0
	b	.L305
.L328:
	add	w3, w3, 1
	b	.L302
.L304:
	add	x0, x19, :lo12:.LANCHOR0
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x0,1966]
	strh	w4, [x0,1954]
.L301:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x0,2008]
	strh	wzr, [x0,2010]
	bne	.L306
	strh	w6, [x0,2008]
.L306:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 4352
	ldrb	w26, [x0,8]
	strh	w2, [x0,2012]
	cbz	w26, .L307
	mov	w2, 384
	strh	w2, [x0,2012]
.L307:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w20, [x21,1954]
	ldrh	w22, [x21,1966]
	mul	w22, w1, w22
	mul	w1, w20, w1
	uxth	w22, w22
	strh	w22, [x21,1944]
	uxth	w0, w1
	strh	w0, [x21,2014]
	bl	Ftl_log2
	ldrh	w25, [x23,16]
	ldrh	w24, [x23,20]
	strh	w0, [x21,2016]
	mul	w0, w22, w25
	strh	w0, [x21,2022]
	mov	w0, w24
	ldrh	w27, [x23,18]
	strh	w25, [x21,2018]
	strh	w27, [x21,2020]
	strh	w24, [x21,2024]
	bl	Ftl_log2
	lsl	w2, w24, 9
	uxth	w3, w0
	strh	w0, [x21,2026]
	cmp	w20, 1024
	ldrh	w0, [x23,26]
	strh	w2, [x21,2028]
	ubfx	x2, x2, 9, 7
	strh	w0, [x21,2032]
	lsl	w2, w2, 1
	mul	w0, w22, w20
	strh	w2, [x21,2030]
	str	w0, [x21,1960]
	bls	.L308
	and	w0, w20, 255
	strh	w0, [x21,2010]
.L308:
	add	x2, x19, :lo12:.LANCHOR0
	mul	w0, w24, w27
	ldrh	w1, [x2,2010]
	sub	w1, w20, w1
	mul	w1, w22, w1
	mul	w1, w1, w24
	mul	w1, w1, w25
	asr	w1, w1, 11
	str	w1, [x2,2036]
	ldrh	w1, [x2,2012]
	lsl	w1, w1, 3
	sdiv	w0, w1, w0
	uxth	w0, w0
	cmp	w0, 4
	bhi	.L326
	mov	w0, 4
.L326:
	strh	w0, [x2,2040]
	cbz	w26, .L311
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 640
	strh	w1, [x0,2012]
.L311:
	add	x1, x19, :lo12:.LANCHOR0
	lsl	w20, w20, 6
	cmp	w22, 1
	ldrh	w0, [x1,2012]
	asr	w0, w0, w3
	add	w3, w3, 9
	asr	w20, w20, w3
	strh	w20, [x1,2044]
	and	w20, w20, 65535
	add	w0, w0, 2
	strh	w0, [x1,2042]
	mul	w0, w20, w22
	str	w0, [x1,2048]
	add	w20, w20, 8
	ldrh	w0, [x1,2040]
	udiv	w0, w0, w22
	add	w20, w20, w0
	bne	.L327
	add	w20, w20, 4
.L327:
	str	w20, [x1,1940]
	add	x20, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20,1940]
	bl	FtlSysBlkNumInit
	str	wzr, [x20,2060]
	ldr	w0, [x20,1940]
	str	w0, [x20,2052]
	ldr	w0, [x20,1956]
	ldrh	w1, [x20,2018]
	lsl	w0, w0, 2
	ldrh	w3, [x20,2024]
	ldrh	w2, [x20,2040]
	mul	w1, w0, w1
	ldrh	w0, [x20,2026]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	strh	w1, [x20,2058]
	uxth	w0, w0
	strh	w0, [x20,2056]
	add	w1, w2, 3
	strh	w1, [x20,2040]
	ldr	w1, [x20,2048]
	add	w4, w1, 3
	str	w4, [x20,2048]
	ldrb	w4, [x20,120]
	cbz	w4, .L314
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x20,2040]
	str	w1, [x20,2048]
.L314:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19,1952]
	strh	wzr, [x19,2064]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L315
	mov	w0, 1
	strh	w0, [x19,2064]
.L315:
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
	ldrh	w2, [x1,2066]
	cbz	w2, .L330
	ldrh	w2, [x1,2040]
	ldr	x3, [x1,2072]
	mov	x1, 0
.L331:
	cmp	w2, w1, uxth
	bls	.L336
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L331
	mov	w0, 1
	b	.L330
.L336:
	mov	w0, 0
.L330:
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
	cbz	w3, .L338
	ldr	x5, [x2,2088]
	mov	x0, 56
	mov	x2, 0
.L339:
	cmp	w3, w2
	bls	.L348
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L339
	cmp	w4, w1
	bhi	.L339
	mov	w0, 1
	b	.L338
.L348:
	mov	w0, 0
.L338:
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR0+2060
	ldr	w0, [x0,#:lo12:.LANCHOR0+2060]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	FtlGetCapacity
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	adrp	x0, .LANCHOR0+2060
	ldr	w0, [x0,#:lo12:.LANCHOR0+2060]
	ret
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR0+2096
	ldr	w0, [x0,#:lo12:.LANCHOR0+2096]
	ret
	.size	FtlGetLpn, .-FtlGetLpn
	.align	2
	.global	FtlGetCurEraseBlock
	.type	FtlGetCurEraseBlock, %function
FtlGetCurEraseBlock:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w2, [x1,1944]
	ldr	w0, [x1,2100]
	mul	w0, w2, w0
	ret
	.size	FtlGetCurEraseBlock, .-FtlGetCurEraseBlock
	.align	2
	.global	FtlGetAllBlockNum
	.type	FtlGetAllBlockNum, %function
FtlGetAllBlockNum:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w2, [x1,1954]
	ldrh	w0, [x1,1944]
	mul	w0, w2, w0
	ret
	.size	FtlGetAllBlockNum, .-FtlGetAllBlockNum
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
	ldrh	w3, [x19,-90]
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
	ldrh	w1, [x3,2014]
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
	ldrh	w1, [x0,2014]
	ldrh	w21, [x0,1966]
	mul	w21, w1, w21
.L358:
	cmp	w20, w21
	bge	.L364
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L359
	add	w19, w19, 1
	uxth	w19, w19
.L359:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L358
.L364:
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
	ldrh	w4, [x3,1968]
	udiv	w2, w0, w4
	msub	w0, w2, w4, w0
	madd	w0, w1, w4, w0
	ldrh	w1, [x3,2014]
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
	ldrh	w1, [x2,1968]
	ldrh	w3, [x2,2014]
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
	ldrh	w3, [x2,2014]
	udiv	w1, w0, w3
	msub	w0, w1, w3, w0
	ldrh	w1, [x2,1968]
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
	bls	.L369
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L370
.L369:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L370:
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
	adrp	x2, .LANCHOR0
	uxth	w1, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	w22, w0
	add	x0, x2, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	mov	x19, x2
	ldrh	w0, [x0,2206]
	cmp	w0, 1024
	beq	.L373
	cbz	w1, .L375
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w0, [x20,-96]
	cbnz	w0, .L375
	mov	w0, w22
	bl	P2V_block_in_plane
	uxth	w21, w0
	ldr	x0, [x20,-88]
	lsl	w1, w22, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,-88]
	bl	FlashEraseBlocks
	ubfiz	x1, x21, 1, 16
	ldr	x2, [x20,-80]
	ldrh	w0, [x2,x1]
	add	w0, w0, 1
	strh	w0, [x2,x1]
	ldr	w0, [x20,-72]
	add	w0, w0, 1
	str	w0, [x20,-72]
.L375:
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 2200
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w22, [x0,8]
	strh	w2, [x1,4]
.L373:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x20, x20, :lo12:.LANCHOR0
	mov	w19, 0
	add	x20, x20, 2200
	adrp	x21, .LANCHOR2
.L383:
	ldrh	w0, [x20,6]
	cmp	w0, w19
	bls	.L391
	ldrh	w0, [x20,2]
	add	w0, w0, w19
	add	x0, x20, x0, sxtw 1
	ldrh	w0, [x0,8]
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	add	x1, x21, :lo12:.LANCHOR2
	ubfiz	x2, x19, 2, 16
	add	w19, w19, 1
	ldr	x3, [x1,-64]
	uxth	w19, w19
	ldr	x1, [x1,-80]
	ldrh	w0, [x1,x0]
	str	w0, [x3,x2]
	b	.L383
.L391:
	adrp	x4, .LANCHOR0
	adrp	x7, .LANCHOR2
	mov	w0, 0
	add	x4, x4, :lo12:.LANCHOR0
	mov	x8, x7
.L385:
	ldrh	w6, [x4,2206]
	sub	w1, w6, #1
	cmp	w0, w1
	bge	.L392
	add	x1, x7, :lo12:.LANCHOR2
	add	w5, w0, 1
	uxth	w5, w5
	ldr	x3, [x1,-64]
	mov	w2, w5
	mov	w1, w0
.L386:
	cmp	w2, w6
	bcs	.L393
	ubfiz	x9, x2, 2, 16
	ldr	w10, [x3,w1,uxtw 2]
	ldr	w9, [x3,x9]
	cmp	w10, w9
	csel	w1, w1, w2, ls
	add	w2, w2, 1
	uxth	w2, w2
	b	.L386
.L393:
	cmp	w0, w1
	beq	.L389
	ubfiz	x2, x0, 2, 16
	ubfiz	x6, x1, 2, 32
	ldr	w10, [x3,x2]
	ldr	w9, [x3,x6]
	str	w10, [x3,x6]
	add	x3, x8, :lo12:.LANCHOR2
	ldr	x3, [x3,-64]
	str	w9, [x3,x2]
	add	x2, x4, 2200
	ldrh	w3, [x4,2202]
	add	w1, w3, w1
	add	w0, w3, w0
	add	x1, x2, x1, sxtw 1
	add	x0, x2, x0, sxtw 1
	ldrh	w2, [x0,8]
	ldrh	w6, [x1,8]
	strh	w2, [x1,8]
	strh	w6, [x0,8]
.L389:
	mov	w0, w5
	b	.L385
.L392:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
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
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	add	x20, x20, 2200
	ldrh	w3, [x20,6]
	cbz	w3, .L395
	ldrh	w4, [x20,2]
	mov	w19, w0
.L396:
	cmp	w19, w3
	bcs	.L401
	add	w2, w19, w4
	ubfiz	x2, x2, 1, 10
	add	x2, x20, x2
	ldrh	w0, [x2,8]
	cmp	w0, w1
	bne	.L397
	adrp	x0, .LC2
	add	x0, x0, :lo12:.LC2
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
	b	.L395
.L397:
	add	w19, w19, 1
	b	.L396
.L401:
	mov	w0, 0
.L395:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	remove_from_free_sys_Queue, .-remove_from_free_sys_Queue
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	str	x23, [sp,48]
	add	x22, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	add	x2, x22, 2200
	mov	w20, 65535
	ldrh	w0, [x22,2206]
	cbz	w0, .L403
	ldrh	w1, [x22,2202]
	adrp	x19, .LANCHOR2
	add	x23, x19, :lo12:.LANCHOR2
	add	x3, x2, x1, sxtw 1
	add	w1, w1, 1
	sub	w0, w0, #1
	and	w1, w1, 1023
	strh	w1, [x22,2202]
	ldr	w1, [x23,-96]
	strh	w0, [x22,2206]
	str	x1, [x29,72]
	ldrh	w20, [x3,8]
	cbnz	w1, .L403
	mov	w0, w20
	bl	P2V_block_in_plane
	uxth	w21, w0
	ldr	x0, [x23,-88]
	lsl	w2, w20, 10
	ldr	x1, [x29,72]
	str	w2, [x0,4]
	ldrb	w0, [x22,120]
	cbz	w0, .L404
	ldr	x0, [x23,-88]
	mov	w2, 1
	bl	FlashEraseBlocks
.L404:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x19,-88]
	bl	FlashEraseBlocks
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x19,-80]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x19,-72]
	add	w0, w0, 1
	str	w0, [x19,-72]
.L403:
	mov	w0, w20
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x9, .LANCHOR0
	uxth	w0, w0
	add	x9, x9, :lo12:.LANCHOR0
	ldrh	w1, [x9,1952]
	cmp	w1, w0
	bls	.L412
	adrp	x2, .LANCHOR2
	mov	w7, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x10, w0, w7
	mov	x3, x2
	ldr	x11, [x5,-56]
	add	x4, x11, x10
	strh	w1, [x4,2]
	strh	w1, [x11,x10]
	ldr	x1, [x5,-48]
	cbnz	x1, .L413
	str	x4, [x5,-48]
	b	.L412
.L413:
	ubfiz	x8, x0, 1, 16
	ldr	x13, [x5,-40]
	ldrh	w2, [x4,4]
	mov	x14, -6148914691236517206
	ldr	x12, [x5,-56]
	mov	w18, 65535
	ldrh	w6, [x13,x8]
	cmp	w2, wzr
	ldrh	w17, [x9,1952]
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
.L421:
	add	w5, w5, 1
	cmp	w0, w2
	uxth	w5, w5
	beq	.L412
	cmp	w5, w17
	bhi	.L412
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w7, [x13,x15]
	mul	w7, w7, w16
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L417
	ldrh	w15, [x14,x15]
	ldrh	w7, [x9]
	cmp	w15, w7
	bcc	.L419
	b	.L418
.L417:
	bhi	.L418
.L419:
	ldrh	w7, [x1]
	cmp	w7, w18
	bne	.L420
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x4, [x2,-32]
	b	.L412
.L420:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x12, x1
	b	.L421
.L418:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,-48]
	cmp	x1, x3
	bne	.L422
	strh	w0, [x1,2]
	str	x4, [x2,-48]
	b	.L412
.L422:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-56]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L412:
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
	ldrh	w1, [x0,-24]
	add	w1, w1, 1
	strh	w1, [x0,-24]
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
	beq	.L427
	adrp	x2, .LANCHOR2
	mov	w6, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x8, w0, w6
	mov	x3, x2
	ldr	x9, [x5,-56]
	add	x4, x9, x8
	strh	w1, [x4,2]
	strh	w1, [x9,x8]
	ldr	x1, [x5,-16]
	cbnz	x1, .L428
	str	x4, [x5,-16]
	b	.L427
.L428:
	ldr	x11, [x5,-80]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,-56]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L431:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L429
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L430
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L427
.L430:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L431
.L429:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,-16]
	cmp	x1, x3
	bne	.L432
	strh	w0, [x1,2]
	str	x4, [x2,-16]
	b	.L427
.L432:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-56]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L427:
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
	ldrh	w1, [x0,-8]
	add	w1, w1, 1
	strh	w1, [x0,-8]
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
	ldr	x2, [x6,-56]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L435
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L436
	str	xzr, [x0]
	b	.L437
.L436:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L437
.L435:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L438
	cmp	w3, w0
	beq	.L437
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L437
.L438:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,-56]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L437:
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
	cbz	x2, .L440
	adrp	x3, .LANCHOR2-56
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2-56]
.L441:
	cbnz	w1, .L442
.L444:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L440
.L442:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L444
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L441
.L440:
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
	ldr	x1, [x2,-48]
	cbz	x1, .L452
	ldr	x4, [x2,-56]
	mov	w5, 6
.L448:
	cbz	w3, .L449
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L452
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L448
.L449:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L452:
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
	ldrh	w1, [x19,#:lo12:.LANCHOR2]
	cmp	w1, w20
	beq	.L454
	ldrh	w1, [x0,48]
	cmp	w1, w20
	beq	.L454
	ldrh	w1, [x0,96]
	cmp	w1, w20
	beq	.L454
	mov	w3, 6
	ldr	x4, [x0,-56]
	ldr	x2, [x0,-48]
	umull	x3, w20, w3
	add	x1, x4, x3
	cmp	x1, x2
	beq	.L454
	ldr	x5, [x0,-40]
	ubfiz	x0, x20, 1, 16
	ldrh	w2, [x1,4]
	ldrh	w1, [x1,2]
	ldrh	w0, [x5,x0]
	mul	w2, w0, w2
	mov	w0, 65535
	cmp	w2, wzr
	csinv	w2, w2, wzr, ne
	cmp	w1, w0
	bne	.L456
	ldrh	w0, [x4,x3]
	cmp	w0, w1
	beq	.L454
.L456:
	mov	w0, 6
	mov	x3, -6148914691236517206
	umull	x1, w1, w0
	asr	x0, x1, 1
	add	x1, x4, x1
	madd	x0, x3, x0, x0
	ldrh	w1, [x1,4]
	ldrh	w0, [x5,x0,lsl 1]
	mul	w0, w0, w1
	cmp	w0, wzr
	csinv	w0, w0, wzr, ne
	cmp	w2, w0
	bcs	.L454
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, w20
	sub	x0, x19, #48
	bl	List_remove_node
	ldrh	w0, [x19,-24]
	sub	w0, w0, #1
	strh	w0, [x19,-24]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L454:
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
.L459:
	cmp	w20, w2
	beq	.L462
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L460
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	uxth	w1, w0
	strh	w0, [x22]
	cbz	w1, .L462
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L462
.L460:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L459
.L462:
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
	adrp	x0, .LANCHOR0+2058
	mov	x4, 0
	mov	w5, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR0+2058]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	mov	x3, x0
	ldr	x1, [x1,144]
.L466:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L482
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L466
	b	.L467
.L482:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L469:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L483
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L478
	cmp	w5, w7
	bcc	.L470
.L478:
	mov	w5, w7
	mov	w6, w0
.L470:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L469
.L483:
	cmp	w0, w2
	bcc	.L467
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,152]
	mov	w0, w2
.L473:
	cmp	w3, w2
	beq	.L467
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L474
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L474:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L473
.L467:
	ret
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3,154]
	cbnz	w0, .L485
	cmp	w2, 4
	bhi	.L485
	add	w2, w2, 1
	strh	w2, [x3,154]
	b	.L484
.L485:
	adrp	x2, .LANCHOR0+1952
	add	x0, x1, :lo12:.LANCHOR2
	mov	w7, 65535
	ldrh	w5, [x2,#:lo12:.LANCHOR0+1952]
	strh	wzr, [x0,154]
	str	wzr, [x0,156]
	ldr	x6, [x0,-40]
	mov	x0, 0
.L486:
	cmp	w5, w0, uxth
	bls	.L484
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L487
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,156]
	add	w2, w4, w2
	str	w2, [x3,156]
.L487:
	add	x0, x0, 1
	b	.L486
.L484:
	ret
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	stp	x29, x30, [sp, -16]!
	uxth	w0, w0
	add	x29, sp, 0
	cbz	w1, .L491
	bl	ftl_set_blk_mode.part.8
	b	.L490
.L491:
	adrp	x1, .LANCHOR0
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L490:
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
	ldrh	w4, [x4,1944]
.L495:
	cmp	w2, w4
	bcs	.L501
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L496
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L496:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L495
.L501:
	add	x3, x3, :lo12:.LANCHOR0
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,2018]
	mov	x3, x0
.L498:
	cmp	x3, x4
	beq	.L502
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L499
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L499:
	add	x3, x3, 2
	b	.L498
.L502:
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
	ldrh	w24, [x0,1944]
.L504:
	cmp	w24, w21, uxth
	bls	.L515
	add	x0, x23, 1976
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L505
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L505:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L504
.L515:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w1, [x1,2018]
	mul	w0, w0, w1
	strh	w0, [x19,4]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w1, [x0,160]
	cbz	w1, .L507
	ldrh	w1, [x19]
	ldr	x0, [x0,-80]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 59
	bhi	.L507
	mov	w0, 1
	strb	w0, [x19,9]
.L507:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,8]
	cbz	w0, .L508
	mov	w0, 1
	strb	w0, [x19,9]
.L508:
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
	ldrh	w23, [x1,1944]
	mov	w19, w22
	ldrh	w24, [x1,2018]
	mov	x20, x1
.L517:
	cmp	w23, w22, uxth
	bls	.L524
	add	x0, x20, 1976
	mov	w1, w21
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L518
	add	w19, w19, w24
	uxth	w19, w19
.L518:
	add	x22, x22, 1
	b	.L517
.L524:
	cbz	w19, .L520
	mov	w0, 32768
	sdiv	w19, w0, w19
.L520:
	mov	w0, 6
	umull	x21, w21, w0
	adrp	x0, .LANCHOR2-56
	ldr	x0, [x0,#:lo12:.LANCHOR2-56]
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
	ldr	x1, [x2,-16]
	mov	w0, w1
	cbz	x1, .L526
	ldr	x0, [x2,-56]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,-80]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L526:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR2
	uxth	w4, w0
	add	x5, x2, :lo12:.LANCHOR2
	ldr	x1, [x5,-16]
	mov	w0, w1
	cbz	x1, .L529
	ldrh	w3, [x5,-8]
	mov	w0, 7
	mov	w6, 6
	mov	w7, 65535
	mul	w3, w3, w0
	ldr	x0, [x5,-56]
	asr	w3, w3, 3
	cmp	w4, w3
	sub	x1, x1, x0
	csel	w4, w3, w4, gt
	asr	x1, x1, 1
	mov	x3, -6148914691236517206
	madd	x1, x3, x1, x1
	mov	w3, 0
	uxth	w1, w1
.L531:
	cmp	w3, w4
	beq	.L534
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L532
.L534:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,-80]
	ldrh	w0, [x0,x1]
	b	.L529
.L532:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L531
.L529:
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC3
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR0
	add	x1, x1, :lo12:.LC3
	add	x25, x25, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	mov	x23, x0
	bl	strcpy
	add	x20, x23, 12
	ldr	w2, [x25,168]
	adrp	x1, .LC4
	mov	x0, x20
	add	x1, x1, :lo12:.LC4
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC5
	ldr	w2, [x25,2036]
	mov	x0, x20
	add	x1, x1, :lo12:.LC5
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0,#:lo12:.LANCHOR1+504]
	cmp	w0, 1
	beq	.L537
	sub	w0, w20, w23
	b	.L538
.L537:
	add	x0, x29, 96
	add	x1, x29, 100
	add	x2, x29, 104
	add	x3, x29, 108
	adrp	x24, .LANCHOR2
	bl	NandcGetTimeCfg
	add	x22, x24, :lo12:.LANCHOR2
	ldr	w4, [x29,104]
	adrp	x1, .LC6
	ldr	w5, [x29,108]
	add	x1, x1, :lo12:.LC6
	ldr	w3, [x29,100]
	mov	x0, x20
	ldr	w2, [x29,96]
	bl	sprintf
	add	x21, x20, x0, sxtw
	adrp	x1, .LC7
	mov	x0, x21
	add	x1, x1, :lo12:.LC7
	add	x21, x21, 10
	bl	strcpy
	ldr	w2, [x25,2096]
	adrp	x1, .LC8
	mov	x0, x21
	add	x1, x1, :lo12:.LC8
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,156]
	adrp	x1, .LC9
	mov	x0, x21
	add	x1, x1, :lo12:.LC9
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,164]
	adrp	x1, .LC10
	mov	x0, x21
	add	x1, x1, :lo12:.LC10
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,168]
	adrp	x1, .LC11
	mov	x0, x21
	add	x1, x1, :lo12:.LC11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,172]
	adrp	x1, .LC12
	mov	x0, x21
	add	x1, x1, :lo12:.LC12
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,176]
	adrp	x1, .LC13
	mov	x0, x21
	add	x1, x1, :lo12:.LC13
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,180]
	adrp	x1, .LC14
	mov	x0, x21
	add	x1, x1, :lo12:.LC14
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,184]
	adrp	x1, .LC15
	mov	x0, x21
	add	x1, x1, :lo12:.LC15
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,188]
	adrp	x1, .LC16
	mov	x0, x21
	add	x1, x1, :lo12:.LC16
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,192]
	adrp	x1, .LC17
	mov	x0, x21
	add	x1, x1, :lo12:.LC17
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,196]
	adrp	x1, .LC18
	mov	x0, x21
	add	x1, x1, :lo12:.LC18
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,200]
	adrp	x1, .LC19
	add	x1, x1, :lo12:.LC19
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x25,2110]
	adrp	x1, .LC20
	mov	x0, x21
	add	x1, x1, :lo12:.LC20
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,-8]
	adrp	x1, .LC21
	mov	x0, x21
	add	x1, x1, :lo12:.LC21
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,204]
	adrp	x1, .LC22
	mov	x0, x21
	add	x1, x1, :lo12:.LC22
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,208]
	adrp	x1, .LC23
	mov	x0, x21
	add	x1, x1, :lo12:.LC23
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,212]
	adrp	x1, .LC24
	mov	x0, x21
	add	x1, x1, :lo12:.LC24
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,-72]
	adrp	x1, .LC25
	mov	x0, x21
	add	x1, x1, :lo12:.LC25
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,216]
	adrp	x1, .LC26
	mov	x0, x21
	add	x1, x1, :lo12:.LC26
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,220]
	adrp	x1, .LC27
	mov	x0, x21
	add	x1, x1, :lo12:.LC27
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,254]
	adrp	x1, .LC28
	mov	x0, x21
	add	x1, x1, :lo12:.LC28
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,252]
	adrp	x1, .LC29
	mov	x0, x21
	add	x1, x1, :lo12:.LC29
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,2060]
	adrp	x1, .LC30
	mov	x0, x21
	add	x1, x1, :lo12:.LC30
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,2052]
	adrp	x1, .LC31
	mov	x0, x21
	add	x1, x1, :lo12:.LC31
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,1940]
	adrp	x1, .LC32
	mov	x0, x21
	add	x1, x1, :lo12:.LC32
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,2206]
	adrp	x1, .LC33
	mov	x0, x21
	add	x1, x1, :lo12:.LC33
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,1952]
	adrp	x1, .LC34
	mov	x0, x21
	add	x1, x1, :lo12:.LC34
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,272]
	adrp	x1, .LC35
	mov	x0, x21
	add	x1, x1, :lo12:.LC35
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,1956]
	adrp	x1, .LC36
	mov	x0, x21
	add	x1, x1, :lo12:.LC36
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,280]
	adrp	x1, .LC37
	mov	x0, x21
	add	x1, x1, :lo12:.LC37
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,2104]
	adrp	x1, .LC38
	mov	x0, x21
	add	x1, x1, :lo12:.LC38
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,2]
	adrp	x1, .LC39
	mov	x0, x21
	add	x1, x1, :lo12:.LC39
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,6]
	adrp	x1, .LC40
	mov	x0, x21
	add	x1, x1, :lo12:.LC40
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,#:lo12:.LANCHOR2]
	adrp	x1, .LC41
	mov	x0, x21
	add	x1, x1, :lo12:.LC41
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,8]
	adrp	x1, .LC42
	mov	x0, x21
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,4]
	adrp	x1, .LC43
	mov	x0, x21
	add	x1, x1, :lo12:.LC43
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x24,#:lo12:.LANCHOR2]
	adrp	x1, .LC44
	ldr	x2, [x22,-40]
	mov	x0, x21
	add	x1, x1, :lo12:.LC44
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,50]
	adrp	x1, .LC45
	mov	x0, x21
	add	x1, x1, :lo12:.LC45
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,54]
	adrp	x1, .LC46
	mov	x0, x21
	add	x1, x1, :lo12:.LC46
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,48]
	adrp	x1, .LC47
	mov	x0, x21
	add	x1, x1, :lo12:.LC47
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,56]
	adrp	x1, .LC48
	mov	x0, x21
	add	x1, x1, :lo12:.LC48
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,52]
	adrp	x1, .LC49
	mov	x0, x21
	add	x1, x1, :lo12:.LC49
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x22,48]
	adrp	x1, .LC50
	ldr	x2, [x22,-40]
	mov	x0, x21
	add	x1, x1, :lo12:.LC50
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,98]
	adrp	x1, .LC51
	mov	x0, x21
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,102]
	adrp	x1, .LC52
	mov	x0, x21
	add	x1, x1, :lo12:.LC52
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,96]
	adrp	x1, .LC53
	mov	x0, x21
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,104]
	adrp	x1, .LC54
	mov	x0, x21
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,100]
	adrp	x1, .LC55
	mov	x0, x21
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,298]
	adrp	x1, .LC56
	mov	x0, x21
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,302]
	adrp	x1, .LC57
	mov	x0, x21
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,296]
	adrp	x1, .LC58
	mov	x0, x21
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,304]
	adrp	x1, .LC59
	mov	x0, x21
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,300]
	adrp	x1, .LC60
	mov	x0, x21
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x22,344]
	adrp	x1, .LC61
	ldr	w2, [x22,160]
	add	x1, x1, :lo12:.LC61
	ldr	w4, [x22,436]
	mov	x0, x21
	ldr	w5, [x22,428]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x22,432]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x22,424]
	adrp	x1, .LC62
	mov	x0, x19
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,448]
	adrp	x1, .LC63
	mov	x0, x19
	add	x1, x1, :lo12:.LC63
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,864]
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,866]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,868]
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,872]
	adrp	x1, .LC67
	add	x1, x1, :lo12:.LC67
	mov	x0, x19
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	uxth	w2, w0
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x22,-8]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x22,296]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L539
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x22,-40]
	adrp	x1, .LC70
	mov	x0, x19
	add	x1, x1, :lo12:.LC70
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L539:
	mov	w0, 0
	adrp	x22, .LC71
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x22, x22, :lo12:.LC71
.L541:
	cmp	w3, w27
	beq	.L540
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x6, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x5, [x21,-56]
	mov	x1, x22
	ldr	x4, [x21,-40]
	add	w20, w20, 1
	ldr	x7, [x21,-80]
	add	x5, x5, x25
	ldrh	w4, [x4,x6]
	ldrh	w5, [x5,4]
	ldrh	w6, [x7,x6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,-56]
	cmp	w20, 16
	ldrh	w3, [x0,x25]
	bne	.L541
.L540:
	add	x1, x24, :lo12:.LANCHOR2
	adrp	x22, .LC72
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
	add	x22, x22, :lo12:.LC72
	ldr	x0, [x1,-16]
	ldr	x3, [x1,-56]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L543:
	cmp	w3, w27
	beq	.L542
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x5, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x4, [x21,-56]
	mov	x1, x22
	ldr	x6, [x21,-80]
	add	w20, w20, 1
	add	x4, x4, x25
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,-56]
	cmp	w20, 4
	ldrh	w3, [x0,x25]
	bne	.L543
.L542:
	sub	w0, w19, w23
.L538:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LC73
	adrp	x2, .LC74
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC73
	add	x2, x2, :lo12:.LC74
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
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
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
	ldr	w2, [x0,212]
	ldr	w1, [x0,220]
	cmp	w2, w1
	bcs	.L548
	adrp	x1, .LANCHOR0
	str	wzr, [x0,204]
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x5, [x0,-80]
	mov	x0, 0
	ldrh	w3, [x2,1952]
.L549:
	cmp	w3, w0
	bls	.L581
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x0,lsl 1]
	add	x0, x0, 1
	ldr	w2, [x4,204]
	add	w2, w6, w2
	str	w2, [x4,204]
	b	.L549
.L581:
	add	x2, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x2,204]
	ldrh	w1, [x1,2008]
	udiv	w3, w0, w3
	str	w3, [x2,212]
	ldr	w3, [x2,208]
	sub	w0, w0, w3
	udiv	w0, w0, w1
	str	w0, [x2,204]
	b	.L551
.L548:
	ldr	w1, [x0,216]
	cmp	w2, w1
	bls	.L551
	add	w1, w1, 1
	adrp	x4, .LANCHOR0
	str	w1, [x0,216]
	mov	w0, 0
.L553:
	add	x1, x4, :lo12:.LANCHOR0
	ldrh	w1, [x1,1952]
	cmp	w0, w1
	bcs	.L551
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 1, 32
	add	w0, w0, 1
	ldr	x3, [x1,-80]
	ldrh	w1, [x3,x2]
	add	w1, w1, 1
	strh	w1, [x3,x2]
	b	.L553
.L551:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w21, [x0,220]
	ldr	w24, [x0,212]
	add	w1, w21, 256
	cmp	w1, w24
	bls	.L556
	ldr	w2, [x0,216]
	add	w1, w21, 768
	cmp	w1, w2
	bls	.L556
	ldr	w0, [x0,160]
	cbz	w0, .L559
	cmp	w21, 30
	bls	.L556
.L559:
	mov	w0, 65535
	b	.L558
.L556:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,-8]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L568
	cmp	w21, 30
	bhi	.L559
.L568:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x3, [x0,-48]
	cbz	x3, .L559
	adrp	x1, .LANCHOR0+1952
	mov	w23, 65535
	ldr	x7, [x0,-56]
	mov	w20, w23
	ldr	x26, [x0,-80]
	mov	x0, -6148914691236517206
	ldrh	w9, [x1,#:lo12:.LANCHOR0+1952]
	mov	w8, w23
	mov	w1, 0
	add	x0, x0, 1
	mov	w10, 6
.L561:
	ldrh	w2, [x3]
	cmp	w2, w8
	beq	.L563
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L559
	ldrh	w4, [x3,4]
	cbz	w4, .L562
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w5, w4
	and	x4, x4, 65535
	ldrh	w3, [x26,x4,lsl 1]
	cmp	w3, w21
	bls	.L567
	cmp	w3, w23
	bcs	.L562
	mov	w23, w3
	mov	w20, w5
.L562:
	umull	x3, w2, w10
	add	x3, x7, x3
	b	.L561
.L567:
	mov	w20, w5
.L563:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L559
	ubfiz	x25, x20, 1, 16
	ldrh	w22, [x26,x25]
	cmp	w22, w21
	bls	.L565
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L565
	add	x0, x19, :lo12:.LANCHOR2
	str	w23, [x0,220]
.L565:
	cmp	w22, w24
	bcs	.L559
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L559
	add	w0, w22, 256
	cmp	w0, w24
	bcc	.L566
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,216]
	cmp	w22, w0
	bcs	.L559
.L566:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC75
	ldrh	w5, [x26,x25]
	add	x0, x0, :lo12:.LC75
	mov	w1, w20
	mov	w2, w24
	ldr	x4, [x19,-40]
	ldr	w3, [x19,216]
	ldrh	w4, [x4,x25]
	bl	printk
	mov	w0, 1
	str	w0, [x19,876]
	mov	w0, w20
.L558:
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
	beq	.L583
	adrp	x2, .LANCHOR2-40
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2-40]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L583:
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
	str	wzr, [x0,880]
.L585:
	add	x5, x10, :lo12:.LANCHOR0
	adrp	x9, .LANCHOR0
	ldrh	w0, [x5,1944]
	cmp	w1, w0
	bcs	.L589
	add	x2, x3, :lo12:.LANCHOR2
	umull	x4, w1, w11
	ldr	x7, [x2,888]
	ldr	x8, [x2,896]
	add	x0, x7, x4
	str	w12, [x0,16]
	ldrh	w0, [x5,2028]
	mul	w0, w1, w0
	sdiv	w0, w0, w6
	add	x0, x8, x0, sxtw 2
	str	x0, [x7,x4]
	ldrh	w0, [x5,2030]
	ldr	x8, [x2,888]
	ldr	x5, [x2,904]
	mul	w0, w1, w0
	add	x7, x8, x4
	ldr	x2, [x2,912]
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
	b	.L585
.L589:
	mov	w8, 24
	mov	w5, 4
.L587:
	add	x2, x3, :lo12:.LANCHOR2
	ldr	w1, [x2,920]
	cmp	w0, w1
	bcs	.L590
	umull	x4, w0, w8
	ldr	x7, [x2,888]
	add	x6, x9, :lo12:.LANCHOR0
	ldr	x10, [x2,896]
	add	x1, x7, x4
	str	wzr, [x1,16]
	ldrh	w1, [x6,2028]
	mul	w1, w0, w1
	sdiv	w1, w1, w5
	add	x1, x10, x1, sxtw 2
	str	x1, [x7,x4]
	ldrh	w1, [x6,2030]
	ldr	x6, [x2,888]
	ldr	x2, [x2,904]
	mul	w1, w0, w1
	add	x4, x6, x4
	add	w0, w0, 1
	sdiv	w1, w1, w5
	uxth	w0, w0
	add	x1, x2, x1, sxtw 2
	str	x1, [x4,8]
	b	.L587
.L590:
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
	ldr	w8, [x2,920]
	ldr	x5, [x2,888]
.L592:
	cmp	w3, w1
	bcs	.L591
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L597:
	cmp	w2, w8
	bcs	.L594
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L593
	str	wzr, [x10,16]
	b	.L594
.L593:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L597
.L594:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L592
.L591:
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
	ldr	w5, [x2,920]
	ldr	x6, [x2,888]
	mov	w2, 0
.L599:
	cmp	w2, w1
	bcs	.L605
	mov	w3, 0
.L603:
	cmp	w3, w5
	bcs	.L601
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L600
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L601
.L600:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L603
.L601:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L599
.L605:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR2
	uxth	w0, w0
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w2, [x1,924]
	ldr	x3, [x1,928]
	mov	x1, 0
.L607:
	cmp	w2, w1, uxth
	bls	.L611
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L607
	mov	w0, 1
	b	.L608
.L611:
	mov	w0, 0
.L608:
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
	ldrh	w7, [x4,924]
	ldr	x6, [x4,928]
.L613:
	uxth	w4, w5
	cmp	w4, w7
	bcs	.L617
	add	x5, x5, 1
	add	x9, x6, x5, lsl 1
	ldrh	w9, [x9,-2]
	cmp	w9, w8
	bne	.L613
.L617:
	cmp	w4, w7
	bne	.L615
	ubfiz	x4, x4, 1, 16
	strh	w0, [x6,x4]
	add	x0, x3, :lo12:.LANCHOR2
	ldrh	w4, [x0,924]
	add	w4, w4, 1
	strh	w4, [x0,924]
.L615:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w4, [x3,936]
	ldr	x5, [x3,944]
	umull	x4, w4, w0
	add	x5, x5, x4
	str	w1, [x5,4]
	ldr	x1, [x3,944]
	add	x0, x1, x4
	str	w2, [x0,8]
	str	w19, [x1,x4]
	ldrh	w0, [x3,936]
	add	w0, w0, 1
	strh	w0, [x3,936]
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
	str	x19, [sp,16]
	uxth	w19, w0
	adrp	x0, .LC76
	mov	w1, w19
	add	x0, x0, :lo12:.LC76
	bl	printk
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w3, [x2,952]
	cmp	w3, w19
	beq	.L620
	ldrh	w1, [x2,954]
	cmp	w1, w19
	beq	.L620
	mov	w0, 65535
	cmp	w3, w0
	bne	.L621
	strh	w19, [x2,952]
	b	.L620
.L621:
	cmp	w1, w0
	bne	.L620
	strh	w19, [x2,954]
.L620:
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
	adrp	x0, .LC76
	mov	w1, w19
	add	x0, x0, :lo12:.LC76
	bl	printk
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w3, [x2,952]
	cmp	w3, w19
	beq	.L623
	ldrh	w1, [x2,954]
	cmp	w1, w19
	beq	.L623
	mov	w0, 65535
	cmp	w3, w0
	bne	.L624
	strh	w19, [x2,952]
	b	.L623
.L624:
	cmp	w1, w0
	bne	.L623
	strh	w19, [x2,954]
.L623:
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
	ldrh	w1, [x22,956]
	adrp	x0, .LC77
	mov	w2, w20
	add	x0, x0, :lo12:.LC77
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	ldr	w0, [x22,160]
	cbz	w0, .L626
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,-80]
	ldrh	w1, [x2,x0]
	cmp	w1, 29
	bls	.L626
	sub	w1, w1, #30
	strh	w1, [x2,x0]
.L626:
	add	x2, x19, :lo12:.LANCHOR2
	mov	x0, 0
	add	x2, x2, 960
	ldrh	w1, [x2,-4]
.L627:
	cmp	w1, w0, uxth
	bls	.L633
	add	x0, x0, 1
	add	x3, x2, x0, lsl 1
	ldrh	w3, [x3,-2]
	cmp	w3, w20
	bne	.L627
	b	.L628
.L633:
	cmp	w1, 15
	bhi	.L628
	add	x19, x19, :lo12:.LANCHOR2
	add	w0, w1, 1
	add	x19, x19, 960
	strh	w0, [x19,-4]
	strh	w20, [x19,w1,sxtw 1]
.L628:
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
	ldrh	w1, [x0,956]
	cbz	w1, .L635
	ldrh	w3, [x0,952]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L635
	ldrh	w2, [x0,994]
	cmp	w2, w1
	bcc	.L636
	strh	wzr, [x0,994]
.L636:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x19, 960
	ldrh	w1, [x19,994]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,994]
	add	w0, w0, 1
	strh	w0, [x19,994]
.L635:
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
	cbnz	w0, .L642
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
.L643:
	add	x2, x1, 196
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L643
.L649:
	adrp	x19, .LANCHOR2
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR2
	mov	w2, 1024
	add	x19, x19, 996
	uxth	w20, w20
	mov	x0, x19
	bl	ftl_memset
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
	b	.L644
.L642:
	cmp	w0, 1
	bne	.L645
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x3, :lo12:.LANCHOR0
.L648:
	cmp	x0, 3
	uxth	w1, w0
	mov	w2, w1
	bls	.L646
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L646:
	add	x1, x3, 196
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L648
	b	.L649
.L645:
	cmp	w0, 2
	bne	.L650
	adrp	x3, .LANCHOR0
	mov	w1, 65535
	mov	x0, 0
	add	x3, x3, :lo12:.LANCHOR0
.L652:
	add	x4, x3, 196
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L652
	b	.L649
.L650:
	cmp	w0, 3
	bne	.L653
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x3, :lo12:.LANCHOR0
.L656:
	cmp	x0, 5
	uxth	w1, w0
	mov	w2, w1
	bls	.L654
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L654:
	add	x1, x3, 196
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L656
	b	.L649
.L653:
	cmp	w0, 4
	bne	.L657
	adrp	x1, .LANCHOR0
	mov	w5, 7
	add	x1, x1, :lo12:.LANCHOR0
	mov	w4, 6
	add	x2, x1, 196
	strh	w0, [x2,8]
	mov	w0, 5
	strh	wzr, [x1,196]
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
.L659:
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
	bne	.L659
	b	.L649
.L657:
	cmp	w0, 5
	bne	.L660
	adrp	x2, .LANCHOR0
	mov	x0, 0
	add	x2, x2, :lo12:.LANCHOR0
.L661:
	add	x1, x2, 196
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L661
	mov	x0, 0
.L662:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L662
	b	.L649
.L660:
	cmp	w0, 6
	bne	.L649
	adrp	x1, .LANCHOR0
	mov	x2, 0
	mov	w4, 12
	mov	w5, 10
	add	x1, x1, :lo12:.LANCHOR0
.L665:
	cmp	x2, 5
	uxth	w3, w2
	mov	w0, w3
	bls	.L663
	add	w0, w3, w3, lsl 1
	and	w3, w3, 1
	cmp	w3, wzr
	uxth	w0, w0
	csel	w3, w4, w5, ne
	sub	w0, w0, w3
	uxth	w0, w0
.L663:
	add	x3, x1, 196
	strh	w0, [x3,x2,lsl 1]
	add	x2, x2, 1
	cmp	x2, 256
	bne	.L665
	b	.L649
.L644:
	cmp	w20, w0, uxth
	bls	.L683
	add	x2, x1, 196
	ldrh	w2, [x2,x0,lsl 1]
	add	x0, x0, 1
	strh	w2, [x19,w2,sxtw 1]
	b	.L644
.L683:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	str	x27, [sp,80]
	add	x1, x22, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	strb	wzr, [x1,2020]
	adrp	x1, .LANCHOR1+482
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x1,#:lo12:.LANCHOR1+482]
	strb	wzr, [x0,1845]
	cmp	w1, 256
	bls	.L685
	mov	w1, 512
	b	.L701
.L685:
	cmp	w1, 128
	bls	.L701
	mov	w1, 256
.L701:
	add	x19, x21, :lo12:.LANCHOR0
	str	w1, [x0,12]
	mov	w2, 8
	mov	w1, 0
	add	x0, x19, 1848
	add	x25, x19, 708
	mov	x24, 0
	add	x26, x19, 1620
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 32
	mov	x0, x25
	bl	ftl_memset
	add	x0, x19, 1652
	mov	w1, 0
	mov	w2, 192
	bl	ftl_memset
	ldr	x20, [x19,744]
	add	x23, x20, 1
.L689:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x26, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L688
	ldrb	w1, [x19,1845]
	str	w0, [x25,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x19, x1, sxtw
	strb	w0, [x19,1845]
	strb	w24, [x1,1848]
.L688:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L689
	add	x19, x21, :lo12:.LANCHOR0
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w1, [x19,1845]
	strb	w1, [x0,2020]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L690
.L694:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x20,13]
	add	x22, x22, :lo12:.LANCHOR2
	ldr	x27, [sp,80]
	ldp	x23, x24, [sp,48]
	ldrb	w0, [x21,1845]
	ldp	x25, x26, [sp,64]
	mul	w0, w0, w1
	ldrh	w1, [x20,14]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,2022]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 96
	ret
.L690:
	ldr	w26, [x19,12]
	mov	x24, 0
	add	x27, x19, 1620
	add	x25, x19, 708
.L693:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x27, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L691
	ldrb	w1, [x20,13]
	ldrb	w3, [x19,1845]
	mul	w0, w1, w26
	ldrh	w1, [x20,14]
	mov	x2, x3
	and	w1, w1, 65280
	mul	w1, w0, w1
	str	w1, [x25,w3,sxtw 2]
	ldrb	w0, [x20,23]
	cbz	w0, .L692
	lsl	w1, w1, 1
	str	w1, [x25,w3,sxtw 2]
.L692:
	add	x0, x19, x3
	add	w2, w2, 1
	strb	w2, [x19,1845]
	strb	w24, [x0,1848]
.L691:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L693
	b	.L694
	.size	FlashDieInfoInit, .-FlashDieInfoInit
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
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
	ldr	w0, [x1,12]
	mul	w0, w0, w2
	ldrb	w2, [x1,1936]
	strb	w2, [x19,7]
	uxth	w0, w0
	strh	w0, [x19,4]
	ldr	x3, [x1,744]
	ldrb	w2, [x3,13]
	mul	w0, w2, w0
	ldrh	w2, [x3,14]
	mul	w0, w0, w2
	ldrb	w2, [x3,8]
	mul	w0, w0, w2
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
.L703:
	cmp	w4, w0, uxtb
	bls	.L705
	add	x2, x1, 1848
	ldrb	w3, [x19,10]
	ldrb	w2, [x0,x2]
	add	x0, x0, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L703
.L705:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ReadFlashInfo, .-ReadFlashInfo
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
	add	x3, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	str	w1, [x0,2036]
	mov	w1, -1
	strh	wzr, [x0,2024]
	strh	w1, [x0,952]
	str	wzr, [x0,2028]
	str	wzr, [x0,344]
	str	wzr, [x0,196]
	str	wzr, [x0,200]
	str	wzr, [x0,184]
	str	wzr, [x0,172]
	str	wzr, [x0,168]
	str	wzr, [x0,176]
	str	wzr, [x0,180]
	str	wzr, [x0,164]
	str	wzr, [x0,204]
	str	wzr, [x0,208]
	str	wzr, [x0,-72]
	str	wzr, [x0,216]
	str	wzr, [x0,220]
	str	wzr, [x0,2032]
	str	wzr, [x0,876]
	str	wzr, [x0,2040]
	str	wzr, [x0,868]
	str	wzr, [x3,2100]
	strh	w1, [x0,954]
	mov	w1, 32
	ldrh	w2, [x3,2024]
	strh	w1, [x0,864]
	mov	w1, 128
	strh	w1, [x0,866]
	mov	w1, 1024
	strh	wzr, [x0,872]
	sdiv	w2, w1, w2
	ldrh	w1, [x3,1944]
	strh	wzr, [x0,956]
	lsl	w1, w1, 2
	strh	wzr, [x0,2044]
	strh	wzr, [x0,994]
	cmp	w2, w1
	str	w2, [x0,2048]
	bls	.L707
	str	w1, [x0,2048]
.L707:
	add	x20, x22, :lo12:.LANCHOR0
	add	x19, x21, :lo12:.LANCHOR2
	mov	w24, 56
	ldrh	w0, [x20,2022]
	str	wzr, [x20,2080]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,928]
	ldrh	w1, [x20,2022]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,1944]
	str	x0, [x19,944]
	mul	w23, w23, w24
	lsl	w25, w23, 3
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,2056]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2064]
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,2072]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,-88]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,912]
	ldr	w0, [x19,2048]
	mul	w0, w0, w24
	bl	ftl_malloc
	str	x0, [x20,2088]
	ldrh	w0, [x20,1944]
	ldrh	w23, [x20,2028]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,920]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,-64]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2080]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2088]
	ldr	w0, [x19,920]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,896]
	ldr	w0, [x19,2048]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,2096]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2104]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2112]
	ldr	w1, [x19,920]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,1944]
	str	x0, [x19,888]
	ldrh	w0, [x20,2030]
	mul	w23, w0, w23
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2120]
	lsl	w0, w23, 3
	bl	ftl_malloc
	str	x0, [x19,2128]
	ldrh	w1, [x20,2030]
	ldr	w0, [x19,920]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,904]
	ldrh	w1, [x20,2030]
	ldr	w0, [x19,2048]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2136]
	ldrh	w0, [x20,1954]
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
	ldrh	w23, [x20,1954]
	str	x0, [x19,2160]
	add	x0, x0, 32
	lsl	w23, w23, 1
	str	x0, [x19,-80]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2168]
	mov	w0, w23
	bl	ftl_malloc
	ldr	w23, [x20,2048]
	str	x0, [x19,-40]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2176]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2184]
	ldrh	w0, [x20,1954]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x22,#:lo12:.LANCHOR0]
	ldrh	w0, [x20,2040]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x20,2072]
	ldrh	w0, [x20,2040]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,2192]
	ldrh	w0, [x20,2040]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2200]
	ldrh	w0, [x20,2042]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2208]
	ldrh	w2, [x20,2042]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w23, [x20,2056]
	lsl	w23, w23, 2
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2216]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2224]
	ldr	w0, [x20,2048]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2232]
	ldrh	w0, [x20,2058]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,144]
	ldrh	w1, [x20,2058]
	ldrh	w0, [x20,2028]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2240]
	ldrh	w1, [x20,1954]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,-56]
	ldrh	w0, [x20,2014]
	ldrh	w1, [x20,1966]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,2248]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,2136]
	add	x1, x20, 2104
	ldrh	w4, [x19,2248]
	ldrh	w5, [x20,1966]
	mov	w0, 1
	mov	w2, w4
.L708:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L808
	ldr	x3, [x20,2136]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L708
.L808:
	add	x1, x22, :lo12:.LANCHOR0
.L710:
	cmp	w0, 8
	beq	.L809
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,2136]
	b	.L710
.L809:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,2176]
	cbnz	x1, .L712
.L714:
	adrp	x0, .LC78
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC78
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L713
.L712:
	ldr	x1, [x0,2184]
	cbz	x1, .L714
	ldr	x1, [x0,2216]
	cbz	x1, .L714
	ldr	x1, [x0,2232]
	cbz	x1, .L714
	ldr	x1, [x0,144]
	cbz	x1, .L714
	ldr	x1, [x0,2240]
	cbz	x1, .L714
	ldr	x1, [x0,-56]
	cbz	x1, .L714
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x1, [x22,2136]
	cbz	x1, .L714
	ldr	x0, [x0,-40]
	cbz	x0, .L714
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,928]
	cbz	x1, .L714
	ldr	x1, [x0,944]
	cbz	x1, .L714
	ldr	x1, [x0,2056]
	cbz	x1, .L714
	ldr	x1, [x0,2072]
	cbz	x1, .L714
	ldr	x1, [x0,-88]
	cbz	x1, .L714
	ldr	x1, [x0,912]
	cbz	x1, .L714
	ldr	x1, [x0,2064]
	cbz	x1, .L714
	ldr	x1, [x0,-64]
	cbz	x1, .L714
	ldr	x1, [x0,2080]
	cbz	x1, .L714
	ldr	x0, [x0,2088]
	cbz	x0, .L714
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,896]
	cbz	x1, .L714
	ldr	x1, [x0,2104]
	cbz	x1, .L714
	ldr	x1, [x0,2112]
	cbz	x1, .L714
	ldr	x1, [x0,888]
	cbz	x1, .L714
	ldr	x1, [x0,2120]
	cbz	x1, .L714
	ldr	x1, [x0,2128]
	cbz	x1, .L714
	ldr	x1, [x0,904]
	cbz	x1, .L714
	ldr	x1, [x0,-80]
	cbz	x1, .L714
	ldr	x0, [x0,2152]
	cbz	x0, .L714
	adrp	x0, .LANCHOR0+2072
	ldr	x0, [x0,#:lo12:.LANCHOR0+2072]
	cbz	x0, .L714
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,2192]
	cbz	x1, .L714
	ldr	x1, [x0,2200]
	cbz	x1, .L714
	ldr	x0, [x0,2208]
	cbz	x0, .L714
	mov	w0, 0
.L713:
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
.L812:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L810
	ubfx	x4, x2, 5, 11
	add	x3, x3, 2
	lsl	x4, x4, 2
	lsl	w2, w5, w2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	bne	.L812
.L810:
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
.L818:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L831
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L819:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L832
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x25,x3]
	cmp	w4, w2
	bne	.L820
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L820:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L819
.L832:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L818
.L831:
	mov	w24, 0
	ldrh	w26, [x21]
	mov	w20, w24
	adrp	x27, .LANCHOR0
.L823:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L833
	ldrh	w0, [x19]
	uxtw	x22, w20
	cmp	w0, w20
	bne	.L824
	add	x0, x27, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,2020]
	cmp	w1, w0
	bcs	.L824
	strh	w0, [x21,x22,lsl 1]
.L824:
	lsl	x22, x22, 1
	ldrh	w23, [x21,x22]
	cmp	w26, w23
	bls	.L825
	mov	w24, w20
	mov	w26, w23
.L825:
	cbnz	w23, .L826
	ldrh	w0, [x25,x22]
	cbz	w0, .L826
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	w23, [x25,x22]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L826:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L823
.L833:
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
	ldr	w2, [x21,2048]
	ldr	x0, [x22,2184]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x21,2028]
	mov	w1, 255
	ldrh	w2, [x21,2058]
	ldr	x0, [x22,2240]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w1, 0
	mov	w5, -1
.L835:
	add	x3, x20, :lo12:.LANCHOR0
	ldrh	w0, [x3,2058]
	cmp	w0, w1
	bls	.L837
	add	x4, x19, :lo12:.LANCHOR2
	ubfiz	x0, x1, 4, 16
	ldr	x2, [x4,144]
	add	x2, x2, x0
	str	wzr, [x2,4]
	ldr	x2, [x4,144]
	strh	w5, [x2,x0]
	ldr	x2, [x4,144]
	add	x2, x2, x0
	ldrh	w0, [x3,2028]
	ldr	x3, [x4,2240]
	mul	w0, w0, w1
	add	w1, w1, 1
	sxtw	x0, w0
	uxth	w1, w1
	and	x0, x0, -4
	add	x0, x3, x0
	str	x0, [x2,8]
	b	.L835
.L837:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w4, [x3,2048]
	ldrh	w3, [x3,2056]
	add	x1, x0, 2256
	mov	w2, -1
	strh	w3, [x0,2262]
	ldr	x3, [x0,2176]
	strh	w4, [x0,2266]
	mov	w4, -3902
	str	x3, [x0,2272]
	ldr	x3, [x0,2232]
	strh	w4, [x0,2260]
	str	x3, [x0,2280]
	ldrh	w4, [x0,2320]
	ldr	x3, [x0,2184]
	strh	w2, [x0,2258]
	strh	w2, [x0,2256]
	strh	w4, [x0,2264]
	str	x3, [x0,2288]
	ldr	x0, [x0,2216]
	str	x0, [x1,40]
	strh	w2, [x1,60]
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
	ldrh	w2, [x20,2040]
	str	w0, [x19,2348]
	ldr	x0, [x20,2072]
	lsl	w2, w2, 1
	str	xzr, [x19,2328]
	str	wzr, [x19,2340]
	str	wzr, [x19,2344]
	str	wzr, [x19,160]
	strh	wzr, [x20,2066]
	bl	ftl_memset
	ldrh	w2, [x20,1954]
	mov	w1, 0
	ldr	x0, [x19,-80]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,1954]
	mov	w1, 0
	ldr	x0, [x19,2152]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x19, 224
	mov	w1, 0
	mov	w2, 48
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 512
	add	x0, x19, 352
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
	ldrh	w2, [x0,1954]
	mov	w22, w25
	mov	w26, 32768
	ldr	x0, [x19,-56]
	mul	w2, w2, w24
	bl	ftl_memset
	str	xzr, [x19,-16]
	str	xzr, [x19,-48]
	str	xzr, [x19,-32]
	strh	wzr, [x19,-24]
	strh	wzr, [x19,-8]
	strh	wzr, [x19,2024]
	mov	w19, w25
.L840:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,1952]
	cmp	w19, w1
	bge	.L847
	mov	w20, 0
	ldrh	w3, [x0,1944]
	ldrh	w2, [x0,2018]
	mov	w28, w20
	mov	x27, x0
.L848:
	cmp	w28, w3
	bge	.L849
	add	x0, x27, 1976
	mov	w1, w19
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L841
	add	w20, w2, w20
	sxth	w20, w20
.L841:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L848
.L849:
	cbz	w20, .L843
	sdiv	w20, w26, w20
	sxth	w20, w20
	b	.L844
.L843:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, -1
	ldr	x0, [x0,-40]
	strh	w1, [x0,w19,sxtw 1]
.L844:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,-56]
	smaddl	x1, w19, w24, x1
	strh	w20, [x1,4]
	ldrh	w1, [x21,#:lo12:.LANCHOR2]
	cmp	w19, w1
	beq	.L845
	ldrh	w1, [x0,48]
	cmp	w19, w1
	beq	.L845
	ldrh	w1, [x0,96]
	cmp	w19, w1
	beq	.L845
	ldr	x0, [x0,-40]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L846
	add	w4, w25, 1
	mov	w0, w19
	uxth	w25, w4
	bl	INSERT_FREE_LIST
	b	.L845
.L846:
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_DATA_LIST
.L845:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L840
.L847:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x23, x24, [sp,48]
	strh	w25, [x21,-8]
	strh	w22, [x21,-24]
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
	ldrh	w2, [x20,2022]
	ldr	x0, [x19,928]
	lsl	w2, w2, 1
	strh	wzr, [x19,924]
	strh	wzr, [x19,936]
	bl	ftl_memset
	ldrh	w3, [x20,2022]
	mov	w2, 12
	ldr	x0, [x19,944]
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
	adrp	x20, .LANCHOR1
	adrp	x22, .LANCHOR0
	add	x21, x20, :lo12:.LANCHOR1
	add	x22, x22, :lo12:.LANCHOR0
	str	x23, [sp,48]
	add	x21, x21, 512
	mov	w19, 0
	add	x22, x22, 1620
.L857:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x22
	bl	FlashMemCmp8
	mov	w23, w0
	cbnz	w0, .L854
	add	x0, x20, :lo12:.LANCHOR1
	ubfiz	x19, x19, 5, 32
	add	x1, x0, 512
	adds	x19, x1, x19
	beq	.L860
	ldrb	w3, [x19,22]
	mov	x2, 0
	mov	x1, x0
	b	.L859
.L854:
	add	w19, w19, 1
	add	x21, x21, 32
	cmp	w19, 72
	bne	.L857
	b	.L860
.L859:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,2816]
	cmp	w4, w3
	beq	.L858
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L859
	mov	w0, w2
.L858:
	add	x20, x20, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x20, 2816
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x2, x1
	add	x0, x0, 88
	mov	w2, 32
	bl	ftl_memcpy
	add	x0, x20, 472
	mov	x1, x19
	mov	w2, 32
	bl	ftl_memcpy
	b	.L855
.L860:
	mov	w23, -1
.L855:
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
	bne	.L865
	cbz	x3, .L866
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L866:
	cbz	x20, .L864
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
	b	.L864
.L865:
	cbz	x3, .L869
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L869:
	cbz	x20, .L864
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
.L864:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	NandcCopy1KB, .-NandcCopy1KB
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
	cbnz	w2, .L882
	add	x1, x4, x1
	b	.L884
.L882:
	add	x0, x4, x1
	mov	x1, x6
.L884:
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
	.global	rk_nand_de_init
	.type	rk_nand_de_init, %function
rk_nand_de_init:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FlashDeInit
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_de_init, .-rk_nand_de_init
	.align	2
	.global	rk_nand_suspend
	.type	rk_nand_suspend, %function
rk_nand_suspend:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FlashSuspend
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FlashResume
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	adrp	x0, .LANCHOR0+2060
	ldr	w0, [x0,#:lo12:.LANCHOR0+2060]
	ret
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
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
	adrp	x23, .LC79
	adrp	x21, .LC80
	adrp	x24, .LC73
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x22, x1
	mov	x27, x0
	mov	w25, w2
	uxtw	x26, w3
	mov	w20, w19
	add	x23, x23, :lo12:.LC79
	add	x21, x21, :lo12:.LC80
	add	x24, x24, :lo12:.LC73
.L891:
	cmp	x19, x26
	beq	.L899
	cbnz	w20, .L892
	mov	x0, x23
	mov	x1, x27
	mov	w2, w19
	bl	printk
.L892:
	cmp	w25, 4
	mov	x0, x21
	bne	.L893
	ldr	w1, [x22,x19,lsl 2]
	b	.L898
.L893:
	cmp	w25, 2
	bne	.L895
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L898
.L895:
	ldrb	w1, [x22,x19]
.L898:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L896
	adrp	x1, .LC81
	mov	x0, x24
	add	x1, x1, :lo12:.LC81
	mov	w20, 0
	bl	printk
.L896:
	add	x19, x19, 1
	b	.L891
.L899:
	adrp	x0, .LC73
	adrp	x1, .LC81
	add	x1, x1, :lo12:.LC81
	add	x0, x0, :lo12:.LC73
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
	stp	x25, x26, [sp,64]
	mov	w26, w0
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 760
	cmp	w26, 2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	strb	w26, [x0,760]
	mov	w0, -84
	strb	w4, [x1,5]
	strb	w0, [x1,4]
	mov	w0, -81
	strb	w2, [x1,6]
	strb	w0, [x1,7]
	bne	.L901
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+2961
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+2961]
	b	.L956
.L901:
	cmp	w26, 3
	bne	.L903
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
	b	.L989
.L903:
	cmp	w26, 4
	bne	.L904
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
.L989:
	mov	w27, 8
	strb	w0, [x1,11]
	mov	w28, w27
	b	.L902
.L904:
	cmp	w26, 5
	bne	.L905
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w27, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L988
.L905:
	cmp	w26, 6
	bne	.L906
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w27, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L988
.L906:
	cmp	w26, 7
	bne	.L956
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
	b	.L902
.L956:
	mov	w27, 7
.L988:
	mov	w28, 4
.L902:
	sub	w0, w26, #1
	cmp	w0, 1
	bhi	.L985
	adrp	x22, .LANCHOR1
	add	x25, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	add	x26, x25, 760
	add	x4, x25, 24
	add	x22, x22, 2944
.L907:
	ldrb	w0, [x25,1845]
	cmp	w0, w21
	bls	.L914
	add	x0, x25, x21, sxtw
	ldrb	w0, [x0,1848]
	mov	x23, 0
	add	x20, x26, x0, lsl 6
	lsl	x0, x0, 4
	add	x1, x4, x0
	add	x20, x20, 20
	ldr	x24, [x4,x0]
	ldrb	w0, [x1,8]
	mov	w1, 55
	add	x24, x24, x0, lsl 8
.L909:
	add	x0, x26, x23
	str	w1, [x24,2056]
	str	x4, [x29,128]
	str	x1, [x29,136]
	ldrb	w0, [x0,4]
	str	w0, [x24,2052]
	mov	w0, 80
	bl	NandcDelayns
	ldr	w0, [x24,2048]
	strb	w0, [x20,x23]
	add	x23, x23, 1
	ldr	x1, [x29,136]
	cmp	w28, w23, uxtb
	ldr	x4, [x29,128]
	bhi	.L909
	mov	x0, 0
.L910:
	add	w2, w0, 8
	mov	x1, 0
.L911:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x22, x3
	cmp	x1, 24
	ldrb	w6, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w6, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L911
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L910
	add	w21, w21, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w21, w21
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L907
.L985:
	sub	w0, w26, #3
	cmp	w0, 4
	bhi	.L914
	mul	w25, w28, w27
	sub	w23, w28, #1
	add	x21, x19, :lo12:.LANCHOR0
	lsl	w0, w25, 4
	asr	w22, w25, 2
	str	w0, [x29,128]
	lsl	w0, w22, 1
	uxtb	x23, w23
	str	w0, [x29,124]
	add	x0, x21, 24
	mov	w20, 0
	str	x0, [x29,136]
	sub	w25, w26, #5
	add	x0, x23, 1
	str	x0, [x29,112]
.L915:
	ldrb	w0, [x21,1845]
	cmp	w0, w20
	bhi	.L955
.L914:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	strb	w28, [x19,761]
	strb	w27, [x19,762]
	ldp	x25, x26, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L955:
	add	x0, x21, x20, sxtw
	ldrb	w1, [x0,1848]
	str	x1, [x29,104]
	lsl	x0, x1, 4
	mov	x24, x1
	ldr	x1, [x29,136]
	add	x2, x1, x0
	ldr	x0, [x1,x0]
	ldrb	w2, [x2,8]
	add	x23, x0, x2, lsl 8
	mov	w0, 255
	str	w0, [x23,2056]
	mov	w0, w24
	bl	NandcWaitFlashReady
	cmp	w26, 7
	add	x6, x21, 760
	ldr	x1, [x29,104]
	beq	.L916
	add	x1, x6, x1, lsl 6
	add	x6, x1, 20
	b	.L917
.L916:
	mov	x0, 160
	madd	x1, x1, x0, x6
	add	x6, x1, 28
.L917:
	mov	w0, 54
	cmp	w26, 4
	str	w0, [x23,2056]
	bne	.L918
	mov	w0, 255
	str	w0, [x23,2052]
	mov	w0, 64
	str	w0, [x23,2048]
	mov	w0, 204
	b	.L990
.L918:
	cmp	w25, 1
	bhi	.L920
	ldrb	w0, [x21,764]
	str	w0, [x23,2052]
	mov	w0, 82
	b	.L991
.L920:
	mov	w0, 174
	str	w0, [x23,2052]
	str	wzr, [x23,2048]
	mov	w0, 176
.L990:
	str	w0, [x23,2052]
	mov	w0, 77
.L991:
	str	w0, [x23,2048]
	mov	w0, 22
	str	w0, [x23,2056]
	mov	w0, 23
	str	w0, [x23,2056]
	mov	w0, 4
	str	w0, [x23,2056]
	mov	w0, 25
	str	w0, [x23,2056]
	cmp	w26, 6
	str	wzr, [x23,2056]
	str	wzr, [x23,2052]
	str	wzr, [x23,2052]
	bne	.L921
	mov	w0, 31
	str	w0, [x23,2052]
	b	.L922
.L921:
	str	wzr, [x23,2052]
.L922:
	mov	w1, 2
	str	w1, [x23,2052]
	str	wzr, [x23,2052]
	mov	w0, 48
	str	w0, [x23,2056]
	mov	w0, w24
	str	x6, [x29,96]
	str	x1, [x29,104]
	bl	NandcWaitFlashReady
	cmp	w25, 1
	mov	w7, 16
	ldr	x6, [x29,96]
	bls	.L924
	ldr	x1, [x29,104]
	cmp	w26, 7
	mov	w7, 32
	csel	w7, w7, w1, eq
.L924:
	adrp	x0, .LANCHOR4
	mov	x8, 0
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x1, [x1,56]
.L925:
	ldr	w9, [x23,2048]
	strb	w9, [x1,x8]
	add	x8, x8, 1
	cmp	w7, w8, uxtb
	bhi	.L925
	cmp	w26, 7
	bne	.L926
	mov	w7, 0
.L928:
	ldrb	w8, [x1]
	cmp	w8, 12
	beq	.L927
	ldrb	w8, [x1,1]
	cmp	w8, 10
	beq	.L927
	add	w7, w7, 1
	add	x1, x1, 4
	uxtb	w7, w7
	cmp	w7, 8
	bne	.L928
	b	.L929
.L927:
	cmp	w7, 7
	bne	.L930
.L929:
	adrp	x0, .LC82
	mov	w1, 0
	add	x0, x0, :lo12:.LC82
	bl	printk
.L931:
	b	.L931
.L926:
	cmp	w26, 6
	bne	.L930
	mov	x7, 0
.L932:
	ldrb	w8, [x1,x7]
	cmp	w8, 12
	beq	.L930
	add	x8, x1, x7
	ldrb	w8, [x8,8]
	cmp	w8, 4
	beq	.L930
	add	x7, x7, 1
	cmp	x7, 8
	bne	.L932
	adrp	x0, .LC82
	mov	w1, 0
	add	x0, x0, :lo12:.LC82
	bl	printk
.L934:
	b	.L934
.L930:
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x10, [x1,56]
	mov	x1, 0
.L935:
	ldr	w2, [x29,128]
	cmp	w2, w1
	ble	.L992
	ldr	w7, [x23,2048]
	strb	w7, [x10,x1]
	add	x1, x1, 1
	b	.L935
.L992:
	add	x1, x0, :lo12:.LANCHOR4
	mov	w9, w22
	mov	w8, 8
	ldr	x12, [x1,56]
.L938:
	mov	w1, 0
.L937:
	add	w7, w1, w9
	add	w1, w1, 1
	sbfiz	x7, x7, 2, 32
	cmp	w1, w22
	ldr	w11, [x12,x7]
	mvn	w11, w11
	str	w11, [x12,x7]
	bne	.L937
	ldr	w1, [x29,124]
	subs	w8, w8, #1
	add	w9, w9, w1
	bne	.L938
	mov	x8, 0
	mov	w16, 1
.L939:
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x15, [x1,56]
	mov	w1, 0
	mov	w11, w1
.L943:
	lsl	w9, w16, w11
	mov	w13, w8
	mov	w7, 16
	mov	w12, 0
.L941:
	ldr	w14, [x15,w13,sxtw 2]
	add	w13, w13, w22
	and	w14, w9, w14
	cmp	w14, w9
	csinc	w12, w12, w12, ne
	subs	w7, w7, #1
	bne	.L941
	cmp	w12, 9
	orr	w9, w1, w9
	add	w11, w11, 1
	csel	w1, w9, w1, cs
	cmp	w11, 32
	bne	.L943
	str	w1, [x15,x8,lsl 2]
	add	x8, x8, 1
	cmp	w22, w8
	bgt	.L939
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x1, [x0,56]
	mov	x0, 0
	mov	w8, w0
.L946:
	ldr	w9, [x1,x0]
	add	x0, x0, 4
	cmp	w9, wzr
	csinc	w8, w8, w8, ne
	cmp	x0, 32
	bne	.L946
	cmp	w8, 7
	ble	.L947
	adrp	x0, .LC83
	mov	w2, 1
	add	x0, x0, :lo12:.LC83
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC82
	mov	w1, 0
	add	x0, x0, :lo12:.LC82
	bl	printk
.L948:
	b	.L948
.L947:
	cmp	w26, 6
	mov	w1, 4
	beq	.L949
	cmp	w26, 7
	mov	w1, 10
	mov	w0, 8
	csel	w1, w0, w1, ne
.L949:
	mov	w8, 0
.L950:
	mov	x0, 0
.L951:
	add	w9, w7, w0
	ldrb	w11, [x10,x0]
	add	x0, x0, 1
	cmp	w28, w0, uxtb
	strb	w11, [x6,w9,sxtw]
	bhi	.L951
	add	w8, w8, 1
	ldr	x0, [x29,112]
	cmp	w8, w27
	add	w7, w7, w1
	add	x10, x10, x0
	blt	.L950
	mov	w0, 255
	str	w0, [x23,2056]
	mov	w0, w24
	bl	NandcWaitFlashReady
	cmp	w25, 1
	bhi	.L953
	mov	w0, 54
	str	w0, [x23,2056]
	adrp	x0, .LANCHOR0+764
	mov	w1, -1
	ldrb	w0, [x0,#:lo12:.LANCHOR0+764]
	str	w0, [x23,2052]
	mov	w0, 22
	str	wzr, [x23,2048]
	str	w0, [x23,2056]
	mov	w0, w20
	bl	FlashReadCmd
	b	.L954
.L953:
	mov	w0, 56
	str	w0, [x23,2056]
.L954:
	mov	w0, w24
	add	w20, w20, 1
	bl	NandcWaitFlashReady
	uxtb	w20, w20
	b	.L915
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L993
	sub	w2, w0, #1
	cmp	w2, 6
	bhi	.L995
	bl	HynixGetReadRetryDefault
	b	.L993
.L995:
	cmp	w0, 49
	bne	.L996
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
	b	.L1011
.L996:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L1002
	cmp	w1, 33
	bne	.L997
.L1002:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 4
	b	.L1012
.L997:
	cmp	w1, 67
	beq	.L1003
	cmp	w1, 34
	bne	.L999
.L1003:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 5
.L1012:
	strb	w1, [x0,761]
	mov	w1, 7
	strb	w1, [x0,762]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 764
	add	x1, x1, 256
	mov	w2, 45
	b	.L1011
.L999:
	cmp	w1, 68
	beq	.L1004
	cmp	w1, 35
	bne	.L993
.L1004:
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
.L1011:
	bl	ftl_memcpy
.L993:
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
	add	x2, x1, 24
	stp	x21, x22, [sp,32]
	ldr	x19, [x2,x0]
	ldr	w0, [x1,1864]
	cmp	w0, 3
	bls	.L1044
	ldr	w0, [x19,16]
	tbz	x0, 2, .L1044
	mov	x0, x19
	bl	wait_for_nandc_xfer_completed
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L1015
	adrp	x22, .LC84
	adrp	x23, .LC85
	mov	w21, 0
	add	x22, x22, :lo12:.LC84
	add	x23, x23, :lo12:.LC85
.L1016:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1024
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1864]
	cmp	w0, 5
	bhi	.L1017
.L1020:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L1016
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
	b	.L1016
.L1017:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1020
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1020
.L1024:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 1880
	ldr	w0, [x19,40]
	cbz	w0, .L1025
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
	b	.L1025
.L1015:
	adrp	x22, .LC86
	adrp	x23, .LC85
	add	x22, x22, :lo12:.LC86
	add	x23, x23, :lo12:.LC85
.L1026:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L1052
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L1026
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
	b	.L1026
.L1052:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1928]
	cbz	w0, .L1029
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L1029:
	add	x21, x20, :lo12:.LANCHOR0
	add	x21, x21, 1880
	ldr	w0, [x21,40]
	cbz	w0, .L1030
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
.L1030:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1928]
	cbz	w0, .L1025
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L1025:
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x20,1920]
	b	.L1013
.L1044:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1044
.L1013:
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
	ldr	x20, [x0,24]
	and	x0, x3, 63
	uxtb	w21, w1
	uxtb	w24, w2
	mov	x26, x3
	mov	x22, x4
	cbnz	x0, .L1054
	cbnz	x4, .L1055
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x22, x29, 112
	bl	ftl_memset
.L1055:
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
	cbnz	w21, .L1056
	add	x1, x19, :lo12:.LANCHOR0
	ubfx	x2, x24, 1, 7
	mov	w3, 128
	add	x2, x22, x2, lsl 2
	ldr	w0, [x1,1932]
	cmp	w0, 25
	mov	w0, 64
	csel	w3, w0, w3, cc
	mov	w0, w21
.L1058:
	cmp	x22, x2
	add	w4, w0, w3
	beq	.L1100
	ldr	x5, [x1,1888]
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
	b	.L1058
.L1100:
	add	x0, x19, :lo12:.LANCHOR0
	lsr	w24, w24, 2
	ldr	w5, [x0,1932]
	ldr	w4, [x0,1864]
	mov	w0, 0
	mov	w23, w0
.L1060:
	cmp	w0, w24
	bcs	.L1056
	cbz	w5, .L1056
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x20,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L1084
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L1084
	cmp	w4, 5
	bls	.L1062
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
	bls	.L1063
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L1064
.L1063:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L1099
.L1062:
	cmp	w4, 3
	bls	.L1064
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
	bls	.L1065
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L1099
.L1065:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L1099:
	orr	w3, w1, w3, lsl 5
.L1064:
	cmp	w23, w3
	csel	w23, w23, w3, cs
	b	.L1061
.L1084:
	mov	w23, -1
.L1061:
	add	w0, w0, 1
	b	.L1060
.L1056:
	str	wzr, [x20,16]
	b	.L1067
.L1054:
	cmp	w21, 1
	bne	.L1098
	mov	w23, 0
	mov	w28, 2
.L1068:
	cmp	w23, w24
	bcs	.L1101
	and	w27, w23, 3
	mov	x3, x26
	cbz	x26, .L1070
	ubfiz	x0, x23, 9, 23
	add	x3, x26, x0
.L1070:
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
	b	.L1068
.L1101:
	mov	w23, 0
	b	.L1067
.L1098:
	mov	w1, 0
	mov	x4, 0
	mov	w27, 0
	mov	w0, w25
	mov	w2, 2
	mov	w3, w1
	mov	x5, x4
	mov	w23, w27
	bl	NandcXferStart
.L1073:
	cmp	w27, w24
	bcs	.L1067
	mov	w0, w25
	add	w28, w27, 2
	bl	NandcXferComp
	ldr	w0, [x20,32]
	cmp	w28, w24
	str	w0, [x29,104]
	bcs	.L1074
	mov	x4, 0
	mov	w0, w25
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L1074:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L1089
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w23, w0
	csel	w23, w23, w0, cs
	b	.L1075
.L1089:
	mov	w23, -1
.L1075:
	and	w2, w27, 3
	mov	x3, x26
	cbz	x26, .L1076
	ubfiz	x3, x27, 9, 23
	add	x3, x26, x3
.L1076:
	cmp	x22, xzr
	mov	w0, 2
	csel	w4, w0, wzr, ne
	mov	w1, 0
	mov	x0, x20
	mul	w4, w4, w27
	mov	w27, w28
	add	x4, x22, x4
	bl	NandcCopy1KB
	b	.L1073
.L1067:
	cbnz	w21, .L1079
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19,1864]
	cmp	w0, 5
	bls	.L1079
	ldr	w0, [x20]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L1079
	orr	w0, w0, 131072
	mov	w23, -1
	str	w0, [x20]
.L1079:
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
	cbnz	w19, .L1103
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,9]
	ldr	w0, [x0,12]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L1103:
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
	ldr	w0, [x19,1864]
	cmp	w0, 8
	mov	w0, 12
	csel	w22, w22, w0, cc
	cbz	w4, .L1106
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
	bne	.L1107
.L1116:
	mov	w21, -1
	b	.L1108
.L1107:
	adrp	x0, .LC87
	mov	w1, w24
	add	x0, x0, :lo12:.LC87
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L1109
	add	x19, x19, x23, sxtw 4
	ldr	x0, [x19,24]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L1109:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,64]
	add	w1, w1, 1
	str	w1, [x0,64]
	cmp	w1, 2047
	bls	.L1108
	mov	x27, 0
	str	wzr, [x0,64]
	mov	x28, x27
.L1106:
	mov	w19, 0
	mov	w26, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L1114:
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
	bhi	.L1110
	cmp	w0, 2
	bhi	.L1120
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L1120
	sub	w19, w22, w20
	mov	w21, w0
	mov	w26, 0
	b	.L1112
.L1110:
	cmp	w6, w20
	bcs	.L1121
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L1122
	mov	w6, w20
	b	.L1121
.L1120:
	mov	x27, 0
	mov	w19, w22
	mov	w21, w0
	mov	w26, 0
	mov	x28, x27
	b	.L1111
.L1121:
	mov	w20, 0
.L1111:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L1114
.L1112:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L1113
.L1122:
	mov	w19, w5
.L1113:
	cbz	w19, .L1115
	adrp	x0, .LC88
	mov	w1, w19
	add	x0, x0, :lo12:.LC88
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L1115:
	cbz	w26, .L1108
	adrp	x0, .LC89
	mov	w1, w23
	add	x0, x0, :lo12:.LC89
	mov	w2, w24
	bl	printk
	cbz	w25, .L1116
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L1108:
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
	bne	.L1132
	adrp	x21, .LANCHOR0
	add	x25, x21, :lo12:.LANCHOR0
	ldrb	w26, [x25,16]
	cbnz	w26, .L1133
.L1135:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1856]
	cbz	w0, .L1132
	b	.L1153
.L1133:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	strb	wzr, [x25,16]
	bl	FlashReadRawPage
	strb	w26, [x25,16]
	cmn	w0, #1
	beq	.L1135
	mov	w19, w0
	b	.L1132
.L1153:
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
	beq	.L1136
	ldrb	w0, [x21,1936]
	cmp	w19, w0, lsr 1
	bls	.L1132
.L1136:
	lsr	w0, w25, 8
	bl	NandcSetDdrPara
.L1132:
	adrp	x21, .LANCHOR4
	cmn	w19, #1
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x4, [x21,72]
	bne	.L1137
	cbz	x4, .L1137
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w0, w20
	blr	x4
	mov	w19, w0
	adrp	x0, .LC90
	mov	w1, w19
	add	x0, x0, :lo12:.LC90
	mov	w2, w20
	mov	w3, w22
	bl	printk
	cmn	w19, #1
	bne	.L1137
	adrp	x0, .LANCHOR0+120
	ldrb	w0, [x0,#:lo12:.LANCHOR0+120]
	cbz	w0, .L1137
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
.L1137:
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
	beq	.L1158
	cmn	w22, #1
	bne	.L1155
.L1158:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L1155
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1856]
	b	.L1157
.L1155:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,1856]
.L1157:
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
	mov	w24, -1
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
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	adrp	x22, .LANCHOR1
	adrp	x25, .LANCHOR0
	add	x21, x22, :lo12:.LANCHOR1
	ldr	x1, [x0,56]
	add	x21, x21, 472
	add	x25, x25, :lo12:.LANCHOR0
	str	x1, [x0,80]
	mov	w20, 0
	str	wzr, [x0,88]
	mov	w26, 4
	mov	w0, 0
	add	x28, x25, 88
	ldrh	w27, [x21,10]
	bl	flash_enter_slc_mode
.L1166:
	add	w0, w20, 1
	mov	x19, 0
	str	w0, [x29,104]
.L1168:
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
	bne	.L1167
	ldr	x5, [x29,96]
	mov	w0, 0
	ldr	w1, [x29,104]
	mov	x3, 0
	ldr	x2, [x5,56]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1167
	add	x19, x19, 1
	cmp	x19, 4
	beq	.L1169
	b	.L1168
.L1170:
	add	x0, x2, 12
	mov	w1, 2036
	str	x2, [x29,104]
	mov	w24, -1
	bl	JSHash
	ldr	x2, [x29,104]
	ldr	w1, [x2,8]
	cmp	w1, w0
	beq	.L1180
.L1169:
	subs	w26, w26, #1
	add	w20, w20, w27
	bne	.L1166
	mov	w0, w26
.L1179:
	bl	flash_exit_slc_mode
	mov	w0, w24
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
.L1167:
	add	x19, x23, :lo12:.LANCHOR4
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x2, [x19,80]
	ldr	w1, [x2]
	cmp	w1, w0
	bne	.L1169
	cbnz	w24, .L1170
	add	x22, x22, :lo12:.LANCHOR1
	ldrh	w0, [x22,482]
	udiv	w20, w20, w0
	mov	w0, w24
	add	w20, w20, 1
	str	w20, [x19,92]
	b	.L1179
.L1180:
	add	x1, x2, 160
	mov	x0, x21
	mov	w2, 32
	bl	ftl_memcpy
	ldr	x1, [x19,80]
	mov	w2, 32
	mov	x0, x28
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x19,80]
	mov	w2, 852
	add	x0, x25, 760
	add	x1, x1, 224
	bl	ftl_memcpy
	str	w20, [x19,88]
	ldr	x1, [x19,80]
	ldr	w0, [x1,1076]
	strb	w0, [x25,1856]
	ldrh	w0, [x21,10]
	udiv	w0, w20, w0
	add	w2, w0, 1
	cbz	w0, .L1172
	str	w2, [x19,92]
	b	.L1173
.L1172:
	mov	w0, 2
	str	w0, [x19,92]
.L1173:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w1, [x1,14]
	mov	w24, 0
	strb	w1, [x0,96]
	b	.L1169
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
	add	x1, x2, 24
	mov	w22, 0
	add	x3, x1, x0
	ldr	x24, [x1,x0]
	ldrb	w0, [x3,8]
	str	x0, [x29,120]
	add	x20, x0, 8
	mov	x28, x0
	ldrb	w0, [x2,752]
	add	x20, x24, x20, lsl 8
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1182
	ldrb	w0, [x2,1856]
	cbz	w0, .L1183
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L1183:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L1182:
	ldr	x0, [x29,120]
	mov	w21, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x28, 8, 8
	str	x0, [x29,96]
.L1184:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L1209
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w21
	ldrb	w0, [x0,752]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1185
	bl	SandiskSetRRPara
	b	.L1186
.L1185:
	bl	ToshibaSetRRPara
.L1186:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,752]
	cmp	w0, 34
	bne	.L1187
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L1187
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L1187:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L1188
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
	b	.L1189
.L1188:
	ldr	w1, [x29,116]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L1189:
	cmn	w28, #1
	beq	.L1190
	add	x0, x19, :lo12:.LANCHOR0
	cmn	w25, #1
	csel	w25, w25, w28, ne
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1192
	mov	x26, 0
	mov	x27, x26
.L1190:
	add	w21, w21, 1
	b	.L1184
.L1209:
	mov	w28, w25
.L1192:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,752]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1194
	bl	SandiskSetRRPara
	b	.L1195
.L1194:
	bl	ToshibaSetRRPara
.L1195:
	ldr	x0, [x29,120]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	ldrb	w0, [x19,1936]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1196
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1196:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L1197
	mov	w0, 4
	bl	NandcSetDdrMode
.L1197:
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
	add	x0, x0, 24
	mov	w19, -1
	add	x2, x0, x1
	ldr	x0, [x0,x1]
	ldrb	w21, [x2,8]
	add	x21, x21, 8
	add	x21, x0, x21, lsl 8
.L1211:
	add	x0, x27, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L1214
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1212
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,1936]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1217
	mov	x23, 0
	mov	x24, x23
.L1212:
	add	w26, w26, 1
	b	.L1211
.L1217:
	mov	w19, w0
.L1214:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,1936]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1216
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1216:
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
	ldrb	w20, [x0,1936]
	mov	x23, x3
	ldrb	w0, [x0,120]
	mov	x19, x4
	cbnz	w0, .L1224
	add	w20, w20, w20, lsl 1
	ubfx	x20, x20, 2, 8
	b	.L1225
.L1224:
	mov	w0, 3
	sdiv	w20, w20, w0
.L1225:
	mov	w0, w22
	mov	w27, 0
	bl	NandcWaitFlashReady
	add	x4, x19, :lo12:.LANCHOR0
	sbfiz	x0, x22, 4, 32
	add	x4, x4, 24
	mov	w19, -1
	add	x1, x4, x0
	adrp	x5, .LANCHOR4
	mov	w7, 239
	mov	w8, 137
	ldr	x28, [x4,x0]
	ldrb	w21, [x1,8]
	lsl	x6, x21, 8
.L1226:
	add	x0, x5, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	cmp	w27, w0
	bcs	.L1229
	add	x26, x28, x6
	mov	w0, 200
	str	x5, [x29,104]
	str	x6, [x29,112]
	str	w7, [x26,2056]
	str	w8, [x26,2052]
	str	x7, [x29,120]
	str	x8, [x29,128]
	bl	NandcDelayns
	add	w4, w27, 1
	str	w4, [x26,2048]
	str	wzr, [x26,2048]
	mov	w0, w22
	str	wzr, [x26,2048]
	mov	w1, w24
	str	wzr, [x26,2048]
	mov	x2, x25
	mov	x3, x23
	str	x4, [x29,136]
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x4, [x29,136]
	ldr	x8, [x29,128]
	ldr	x7, [x29,120]
	ldr	x6, [x29,112]
	ldr	x5, [x29,104]
	beq	.L1227
	cmn	w19, #1
	csel	w19, w19, w0, ne
	cmp	w0, w20
	bcc	.L1234
	mov	x23, 0
	mov	x25, x23
.L1227:
	mov	w27, w4
	b	.L1226
.L1234:
	mov	w19, w0
.L1229:
	add	x21, x28, x21, lsl 8
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
	bcc	.L1231
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1231:
	cmn	w19, #1
	beq	.L1235
	cmp	w19, 256
	bne	.L1232
.L1235:
	adrp	x0, .LC91
	mov	w1, w27
	add	x0, x0, :lo12:.LC91
	mov	w2, w24
	mov	w3, w27
	mov	w4, w19
	bl	printk
.L1232:
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
	add	x2, x1, 760
	ldrb	w25, [x1,762]
	add	x0, x2, x22
	ldr	x1, [x1,744]
	mov	x23, x22
	mov	x24, x3
	ldrb	w19, [x0,12]
	ldrb	w1, [x1,19]
	cmp	w1, 7
	bne	.L1245
	ldrb	w19, [x0,20]
.L1245:
	mov	w0, w23
	mov	w28, 0
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	add	x5, x4, 764
.L1246:
	cmp	w28, w25
	bcs	.L1250
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
	beq	.L1248
	ldrb	w1, [x4,1936]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1255
	mov	x24, 0
	mov	x26, x24
.L1248:
	add	w28, w28, 1
	b	.L1246
.L1255:
	mov	w20, w0
.L1250:
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 760
	ldr	x1, [x0,-16]
	add	x0, x0, x22
	ldrb	w1, [x1,19]
	cmp	w1, 7
	bne	.L1252
	strb	w19, [x0,20]
	b	.L1253
.L1252:
	strb	w19, [x0,12]
.L1253:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1936]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1254
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1254:
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
	cbnz	w19, .L1262
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,9]
	ldr	w2, [x0,12]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L1262
	ldrb	w0, [x0,8]
	cbnz	w0, .L1263
	sub	w21, w21, #2
	b	.L1262
.L1263:
	mov	w21, 4
.L1262:
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
	ldrb	w1, [x20,9]
	strh	w1, [x0,-2]
	ldrb	w1, [x20,1856]
	str	w1, [x0,1060]
	add	x1, x20, 1620
	bl	ftl_memcpy
	ldr	x0, [x19,80]
	add	x1, x20, 1848
	mov	w2, 8
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x19,80]
	add	x1, x20, 708
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
	bl	JSHash
	str	w0, [x20,8]
	mov	w0, 1592
	str	w0, [x20,4]
	ldr	x0, [x19,104]
	str	x0, [x19,80]
	mov	w0, 0
	mov	w19, w21
	bl	flash_enter_slc_mode
.L1267:
	add	x22, x24, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w0, 0
	add	x20, x23, :lo12:.LANCHOR4
	add	w26, w19, 1
	ldr	w1, [x22,12]
	mul	w1, w19, w1
	bl	FlashEraseBlock
	ldr	w1, [x22,12]
	mov	x3, 0
	ldr	x2, [x20,56]
	mov	w0, 0
	mul	w1, w19, w1
	bl	FlashProgPage
	ldr	w1, [x22,12]
	mov	x3, 0
	ldr	x2, [x20,56]
	mov	w0, 0
	mul	w1, w19, w1
	add	w1, w1, 1
	bl	FlashProgPage
	ldr	w1, [x22,12]
	mov	w0, 0
	ldr	x2, [x20,104]
	mov	x3, 0
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1265
	ldr	x25, [x20,80]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L1265
	mov	w1, 2036
	add	x0, x25, 12
	bl	JSHash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L1265
	ldr	w0, [x22,12]
	cmp	w21, 1
	str	w26, [x20,92]
	mul	w19, w19, w0
	str	w19, [x20,88]
	beq	.L1268
	mov	w21, 1
.L1265:
	cmp	w26, 4
	mov	w19, w26
	bne	.L1267
	b	.L1266
.L1268:
	mov	w21, 2
.L1266:
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
	ldr	w1, [x0,1868]
	ldrb	w27, [x0,1936]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L1274
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1274:
	mov	x0, x26
	mov	w1, 0
	mov	w2, 2048
	adrp	x24, .LC92
	mov	w23, -1
	mov	w20, 2
	bl	ftl_memset
	adrp	x28, .LANCHOR4
	add	x0, x24, :lo12:.LC92
	str	x0, [x29,104]
.L1275:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,9]
	cmp	w20, w0
	bcs	.L1279
	mov	x22, 0
.L1277:
	add	x0, x29, 120
	add	x24, x19, :lo12:.LANCHOR0
	add	x21, x28, :lo12:.LANCHOR4
	ldrb	w25, [x22,x0]
	mov	w0, w25
	bl	FlashBchSel
	ldr	w1, [x24,12]
	mov	w0, 0
	ldr	x2, [x21,56]
	mov	x3, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1276
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L1277
	b	.L1278
.L1276:
	ldr	x0, [x21,56]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L1278
	ldr	x0, [x29,104]
	mov	w1, w25
	bl	printk
	ldr	x1, [x21,56]
	mov	x0, x26
	mov	w2, 2048
	bl	ftl_memcpy
	ldr	x0, [x21,56]
	ldr	w0, [x0,512]
	strb	w0, [x24,9]
	ldr	w0, [x21,92]
	cmp	w0, w20
	bls	.L1282
	mov	w23, 0
	str	w20, [x21,92]
	bl	FlashSavePhyInfo
.L1278:
	add	w20, w20, 1
	b	.L1275
.L1282:
	mov	w23, 0
.L1279:
	mov	w0, w27
	add	x19, x19, :lo12:.LANCHOR0
	bl	FlashBchSel
	ldr	w1, [x19,1868]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L1284
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1284:
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
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	bl	ftl_malloc
	str	x0, [x22,56]
	mov	w0, 32768
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x25, .LC93
	bl	ftl_malloc
	str	x0, [x22,104]
	mov	w0, 4096
	add	x24, x20, 24
	mov	w27, 0
	mov	w28, 44
	bl	ftl_malloc
	str	x0, [x22,112]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x22,120]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x22,128]
	mov	w0, 128
	str	w0, [x20,12]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x22,98]
	mov	x0, x23
	strb	wzr, [x20,1856]
	strb	w1, [x20,9]
	strb	wzr, [x20,8]
	add	x20, x20, 1620
	strb	w1, [x22,96]
	mov	x26, x20
	str	wzr, [x22,92]
	str	wzr, [x22,64]
	bl	NandcInit
	add	x0, x25, :lo12:.LC93
	str	x0, [x29,104]
.L1291:
	uxtb	w25, w27
	mov	w0, w25
	bl	FlashReset
	ldrb	w23, [x24,8]
	ldr	x22, [x24]
	mov	w0, w25
	ubfiz	x23, x23, 8, 8
	add	x22, x22, x23
	bl	NandcFlashCs
	mov	w0, 144
	str	w0, [x22,2056]
	mov	w0, 200
	str	wzr, [x22,2052]
	bl	NandcDelayns
	ldr	w0, [x22,2048]
	strb	w0, [x20]
	ldr	w0, [x22,2048]
	strb	w0, [x20,1]
	ldr	w0, [x22,2048]
	strb	w0, [x20,2]
	ldr	w0, [x22,2048]
	strb	w0, [x20,3]
	ldr	w0, [x22,2048]
	strb	w0, [x20,4]
	ldr	w0, [x22,2048]
	strb	w0, [x20,5]
	mov	w0, w25
	bl	NandcFlashDeCs
	ldrb	w2, [x20]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1287
	ldr	x0, [x29,104]
	add	w1, w27, 1
	ldrb	w3, [x20,1]
	ldrb	w4, [x20,2]
	ldrb	w5, [x20,3]
	ldrb	w6, [x20,4]
	ldrb	w7, [x20,5]
	bl	printk
.L1287:
	cbnz	w27, .L1288
	ldrb	w0, [x26]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1333
	ldrb	w0, [x26,1]
	cmp	w0, 255
	beq	.L1333
.L1288:
	ldrb	w0, [x20]
	cmp	w0, 181
	bne	.L1290
	strb	w28, [x20]
.L1290:
	add	w27, w27, 1
	add	x24, x24, 16
	cmp	w27, 4
	add	x20, x20, 8
	bne	.L1291
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 173
	beq	.L1292
	ldr	w0, [x0,1860]
	bl	NandcSetDdrMode
.L1292:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w2, 852
	add	x0, x20, 760
	bl	ftl_memset
	strb	wzr, [x20,16]
	adrp	x1, .LANCHOR1
	ldrb	w2, [x20,1621]
	add	x0, x1, :lo12:.LANCHOR1
	add	x0, x0, 472
	str	x0, [x20,744]
	add	w0, w2, 38
	and	w0, w0, -3
	uxtb	w0, w0
	cbz	w0, .L1335
	cmp	w2, 241
	bne	.L1293
.L1335:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w3, 1
	mov	w4, 16
	add	x5, x1, :lo12:.LANCHOR1
	strb	w3, [x0,8]
	add	x3, x21, :lo12:.LANCHOR4
	strb	w4, [x0,9]
	strb	w4, [x3,98]
	add	x4, x0, 1620
	ldrb	w0, [x0,1620]
	strb	w0, [x5,2977]
	cmp	w0, 152
	bne	.L1295
	ldrsb	w0, [x4,4]
	tbnz	w0, #31, .L1295
	mov	w0, 24
	strb	w0, [x3,98]
.L1295:
	cmp	w2, 218
	bne	.L1296
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,2990]
	mov	w2, -38
	b	.L1368
.L1296:
	cmp	w2, 220
	bne	.L1297
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,2990]
	mov	w2, -36
.L1368:
	strb	w2, [x0,2978]
.L1297:
	add	x20, x1, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x20, 2848
	mov	w2, 32
	add	x0, x0, 88
	bl	ftl_memcpy
	add	x0, x20, 472
	add	x1, x20, 2976
	mov	w2, 32
	bl	ftl_memcpy
.L1293:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,8]
	cbnz	w0, .L1298
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L1300
	ldr	x0, [x20,744]
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x20,1844]
	tbnz	x1, 0, .L1300
	mov	w1, 1
	strb	w1, [x20,1856]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
	bl	NandcSetMode
.L1300:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,744]
	ldrb	w0, [x0,26]
	strb	w0, [x20,120]
	bl	FlashLoadPhyInfo
	cbz	w0, .L1298
	ldr	w0, [x20,1860]
	cbz	w0, .L1303
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L1369
.L1303:
	ldrb	w0, [x20,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
.L1369:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L1298
	mov	w0, 1
	add	x20, x19, :lo12:.LANCHOR0
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x1, [x20,744]
	adrp	x0, .LC94
	add	x0, x0, :lo12:.LC94
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L1289
	bl	FlashDieInfoInit
	ldr	x0, [x20,744]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	adrp	x1, .LANCHOR2+2022
	ldr	x0, [x20,744]
	ldrh	w1, [x1,#:lo12:.LANCHOR2+2022]
	ldrb	w2, [x0,9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L1305
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L1306
.L1305:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L1306:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1844]
	tst	w0, 6
	beq	.L1307
	bl	FlashSavePhyInfo
	add	x1, x21, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x1,88]
	bl	FlashDdrParaScan
.L1307:
	bl	FlashSavePhyInfo
.L1298:
	add	x20, x19, :lo12:.LANCHOR0
	add	x22, x21, :lo12:.LANCHOR4
	ldr	x2, [x20,744]
	str	xzr, [x22,72]
	ldrb	w0, [x2,26]
	strb	w0, [x20,120]
	ldrh	w0, [x2,16]
	ldrh	w3, [x2,10]
	ubfx	x1, x0, 7, 1
	strb	w1, [x20,16]
	ubfx	x1, x0, 3, 1
	strb	w1, [x22,136]
	ubfx	x1, x0, 4, 1
	strb	w1, [x20,1937]
	ldrb	w1, [x2,12]
	ubfx	x0, x0, 8, 3
	strb	w0, [x20,1844]
	ldrb	w0, [x2,18]
	sdiv	w1, w3, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x20,744]
	ldrh	w1, [x0,16]
	tbz	x1, 6, .L1309
	ldrb	w0, [x0,19]
	ldrb	w1, [x20,762]
	strb	w1, [x22,97]
	sub	w1, w0, #1
	ldrb	w2, [x20,761]
	uxtb	w1, w1
	strb	w0, [x20,752]
	strb	w2, [x20,753]
	cmp	w1, 6
	bhi	.L1310
	adrp	x1, HynixReadRetrial
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,72]
	sub	w1, w0, #5
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L1311
	mov	w1, 1
	str	w1, [x20,1928]
	b	.L1312
.L1311:
	cmp	w0, 7
	add	x20, x20, 788
	beq	.L1313
.L1312:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 780
.L1313:
	mov	x1, 0
	mov	w2, w1
.L1315:
	ldrsb	w3, [x20,x1]
	add	x1, x1, 1
	cmp	w3, wzr
	csinc	w2, w2, w2, ne
	cmp	x1, 32
	bne	.L1315
	cmp	w2, 27
	bls	.L1309
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1309
.L1310:
	sub	w1, w0, #17
	uxtb	w1, w1
	cmp	w1, 2
	bhi	.L1317
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x22,72]
	beq	.L1318
	mov	w0, 7
	b	.L1370
.L1318:
	mov	w0, 15
.L1370:
	strb	w0, [x22,97]
	b	.L1309
.L1317:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1336
	cmp	w0, 33
	bne	.L1319
.L1336:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	mov	w2, 4
	str	x1, [x0,72]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,753]
	mov	w1, 7
	strb	w1, [x0,97]
	b	.L1309
.L1319:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w2, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1337
	cbz	w2, .L1321
.L1337:
	add	x3, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x1, [x3,72]
	beq	.L1323
	cmp	w0, 68
	beq	.L1323
	mov	w0, 7
	strb	w0, [x3,97]
	b	.L1324
.L1323:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,97]
.L1324:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w2, .L1371
	mov	w1, 5
.L1371:
	strb	w1, [x0,753]
	b	.L1309
.L1321:
	cmp	w0, 49
	bne	.L1309
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,72]
.L1309:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ldr	w2, [x0,1868]
	cmp	w2, w1
	bne	.L1326
	ldrb	w1, [x0,120]
	cbz	w1, .L1326
	ldr	x0, [x0,744]
	strb	wzr, [x0,18]
.L1326:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 44
	bne	.L1327
	ldrb	w1, [x0,1856]
	cbz	w1, .L1327
	strb	wzr, [x0,1856]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L1327:
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w0, [x1,1844]
	tst	w0, 6
	beq	.L1328
	ldrb	w1, [x1,1856]
	cbnz	w1, .L1329
	tbnz	x0, 0, .L1328
.L1329:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x21,88]
	bl	FlashDdrParaScan
.L1328:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w20, 16
	bl	flash_exit_slc_mode
	ldr	x0, [x19,744]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2352
	bl	FlashReadIdbDataRaw
	strb	w20, [x19,9]
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
	ldrb	w5, [x19,9]
	strh	w5, [x19,194]
	uxth	w2, w2
	ldrb	w5, [x19,8]
	strh	w2, [x19,190]
	cmp	w5, 1
	bne	.L1331
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,190]
	strb	w20, [x19,9]
	mov	w2, 8
	strh	w4, [x19,182]
	strh	w3, [x19,188]
	strh	w2, [x19,194]
.L1331:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	FlashSuspend
	mov	w0, 0
	b	.L1289
.L1333:
	mov	w0, -2
.L1289:
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
	ldr	x3, [x0,744]
	ldrb	w4, [x0,120]
	ldrb	w3, [x3,19]
	cbz	w4, .L1373
	ldr	w2, [x0,1868]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w2, w0
	beq	.L1372
.L1373:
	sub	w2, w3, #5
	uxtb	w2, w2
	cmp	w2, 2
	bls	.L1374
	cmp	w3, 68
	beq	.L1374
	sub	w3, w3, #19
	and	w3, w3, -17
	uxtb	w3, w3
	cbnz	w3, .L1372
.L1374:
	adrp	x20, .LANCHOR2
	mov	w24, w1
	add	x20, x20, :lo12:.LANCHOR2
	mov	w25, 65535
	add	x20, x20, 996
	adrp	x26, .LANCHOR4
.L1376:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,744]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L1372
	ldrh	w0, [x20,w19,sxtw 1]
	cmp	w0, w25
	bne	.L1372
	add	x22, x26, :lo12:.LANCHOR4
	mov	w1, 255
	mov	w2, 32768
	ldr	x0, [x22,104]
	bl	ftl_memset
	ldr	x2, [x22,104]
	add	w1, w19, w24
	mov	w0, w21
	mov	x3, 0
	add	w19, w19, 1
	bl	FlashProgPage
	uxth	w19, w19
	b	.L1376
.L1372:
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
	adrp	x1, .LANCHOR1+481
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR0
	adrp	x23, .LC95
	adrp	x24, .LC97
	ldrb	w26, [x1,#:lo12:.LANCHOR1+481]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w27, w2
	mov	w22, 0
	add	x21, x21, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC95
	add	x24, x24, :lo12:.LC97
.L1382:
	cmp	w22, w25
	beq	.L1416
	mov	w1, w27
	sub	w4, w25, w22
	mov	x0, x19
	add	x2, x29, 124
	add	x3, x29, 120
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,120]
	adrp	x5, .LANCHOR0
	ldrb	w1, [x21,1845]
	cmp	w0, w1
	bcc	.L1383
	mov	w0, -1
	str	w0, [x19]
	b	.L1384
.L1383:
	add	x0, x21, x0, uxtw
	ldrb	w28, [x0,1848]
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
	ldr	w2, [x21,12]
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
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w3, w0, lsr 2
	bls	.L1387
	cmn	w3, #1
	mov	w0, 256
	csel	w3, w3, w0, eq
.L1387:
	cmp	w3, 256
	beq	.L1398
	cmn	w3, #1
	bne	.L1388
.L1398:
	str	w3, [x19]
	b	.L1390
.L1388:
	str	wzr, [x19]
.L1390:
	ldr	x0, [x19,16]
	cbz	x0, .L1391
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L1391
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1391
	str	w1, [x19]
.L1391:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1384
	add	x5, x5, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	mov	x0, x23
	ldrb	w2, [x5,1936]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L1393
	adrp	x0, .LC96
	mov	w2, 4
	add	x0, x0, :lo12:.LC96
	mov	w3, 8
	bl	rknand_print_hex
.L1393:
	ldr	x1, [x19,16]
	cbz	x1, .L1384
	mov	w2, 4
	mov	x0, x24
	mov	w3, w2
	bl	rknand_print_hex
.L1384:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L1382
.L1416:
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
	ldrb	w4, [x3,16]
	ldrb	w23, [x3,8]
	str	w2, [x29,140]
	str	w4, [x29,136]
	cbz	w23, .L1457
	bl	FlashReadSlc2KPages
	b	.L1506
.L1457:
	adrp	x0, .LC95
	mov	w24, w23
	add	x0, x0, :lo12:.LC95
	str	x0, [x29,128]
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
	str	x0, [x29,120]
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	str	x0, [x29,112]
.L1418:
	ldr	w0, [x29,144]
	cmp	w24, w0
	bcs	.L1508
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
	bcc	.L1420
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L1421
.L1420:
	add	x0, x1, x0, uxtw
	adrp	x25, .LANCHOR4
	ldrb	w19, [x0,1848]
	add	x0, x25, :lo12:.LANCHOR4
	str	x1, [x29,152]
	ldrb	w0, [x0,136]
	cmp	w0, wzr
	mov	w0, w19
	csel	w20, w20, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x1, [x29,152]
	ldr	x0, [x1,744]
	ldrb	w2, [x0,19]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L1423
	add	x1, x1, 760
	sxtw	x0, w19
	add	x1, x1, x0
	cmp	w2, 7
	ldrb	w3, [x1,12]
	bne	.L1424
	ldrb	w3, [x1,20]
.L1424:
	add	x2, x22, :lo12:.LANCHOR0
	add	x0, x2, x0
	ldrb	w0, [x0,1616]
	cmp	w0, w3
	beq	.L1423
	ldrb	w1, [x2,761]
	mov	w0, w19
	add	x2, x2, 764
	bl	HynixSetRRPara
.L1423:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,140]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,152]
	cbz	w0, .L1425
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1425
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1426
.L1425:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1426:
	cmp	w19, 255
	ldr	w1, [x29,172]
	bne	.L1464
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1458
.L1464:
	cbz	w20, .L1430
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x0,12]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1431
.L1430:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1431
.L1458:
	mov	w20, w0
.L1428:
	ldrb	w2, [x29,148]
	mov	w0, w19
	ldr	x3, [x21,8]
	mov	w1, 0
	ldr	x4, [x21,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1432
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0,16]
	cbz	w1, .L1432
	strb	wzr, [x0,16]
	mov	w20, 0
	b	.L1426
.L1432:
	cbz	w20, .L1433
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x29,172]
	ldr	w2, [x0,12]
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
.L1433:
	mov	w0, w19
	bl	NandcFlashDeCs
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w0, [x29,136]
	cmn	w28, #1
	strb	w0, [x6,16]
	bne	.L1440
	ldrb	w0, [x6,1856]
	cbnz	w0, .L1435
.L1439:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x20, [x0,72]
	cbnz	x20, .L1436
	b	.L1509
.L1435:
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
	beq	.L1438
	ldr	x6, [x29,104]
	ldrb	w0, [x6,1936]
	cmp	w28, w0, lsr 1
	bls	.L1460
.L1438:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1439
	b	.L1460
.L1436:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L1441
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,744]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L1442
	ldrb	w1, [x2,761]
	mov	w0, w19
	add	x2, x2, 764
	mov	w3, w20
	bl	HynixSetRRPara
.L1442:
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
	ldrb	w2, [x4,1936]
	bl	printk
	cmn	w28, #1
	ldr	x4, [x29,104]
	bne	.L1441
	ldrb	w0, [x4,120]
	cbz	w0, .L1441
	ldr	w0, [x29,152]
	cbz	w0, .L1443
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1444
.L1443:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1444:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	mov	w20, 0
	ldr	x3, [x21,16]
	ldr	x4, [x0,72]
	mov	w0, w19
	blr	x4
	mov	w28, w0
	b	.L1441
.L1509:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1441
.L1460:
	mov	w20, 0
.L1440:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1441
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x0, [x0,72]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1441:
	cmp	w28, 256
	beq	.L1465
	cmn	w28, #1
	bne	.L1445
.L1465:
	str	w28, [x26,x27]
	b	.L1447
.L1445:
	str	wzr, [x26,x27]
.L1447:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L1449
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x21,4]
	ldr	x0, [x29,128]
	ldrb	w2, [x2,1936]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L1449
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	bl	rknand_print_hex
.L1449:
	cbz	w20, .L1451
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L1452
	add	x25, x25, :lo12:.LANCHOR4
	ldr	x0, [x25,72]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L1452:
	add	w0, w24, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L1466
	cmn	w23, #1
	bne	.L1453
.L1466:
	str	w23, [x26,x0]
	b	.L1451
.L1453:
	str	wzr, [x26,x0]
.L1451:
	ldr	w0, [x29,152]
	add	w24, w24, w20
	cbz	w0, .L1421
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1421
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1421:
	add	w24, w24, 1
	b	.L1418
.L1508:
	mov	w0, 0
	b	.L1506
.L1431:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L1428
	ldr	w1, [x29,172]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L1428
.L1506:
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
	adrp	x26, .LC99
	add	x26, x26, :lo12:.LC99
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
.L1511:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,1845]
	cmp	w1, w19
	bls	.L1521
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1512:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1514
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
	beq	.L1513
	add	x24, x23, :lo12:.LANCHOR4
	ldr	x0, [x24,112]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1513
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	add	x24, x24, x19, sxtw 1
	bl	printk
	uxth	w28, w28
	strh	w20, [x24,144]
	b	.L1514
.L1513:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1512
.L1514:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,1845]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1511
.L1521:
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
.L1523:
	cmp	w25, w23
	beq	.L1552
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,1845]
	cmp	w0, w1
	bcc	.L1524
	str	w28, [x21]
	b	.L1525
.L1524:
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
	ldr	w2, [x22,12]
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
	ldr	w2, [x0,12]
	mov	w0, w20
	add	w1, w2, w1
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	w1, [x29,96]
	mov	w0, w20
	bl	FlashReadStatus
	tbz	x0, 0, .L1528
	str	w28, [x21]
.L1528:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1525:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1523
.L1552:
	mov	w21, 0
	cbz	w27, .L1550
	adrp	x25, .LC103
	adrp	x26, .LC102
	adrp	x27, .LC101
	add	x25, x25, :lo12:.LC103
	add	x26, x26, :lo12:.LC102
	add	x27, x27, :lo12:.LC101
.L1551:
	cmp	w21, w23
	beq	.L1550
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1531
	adrp	x0, .LC100
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC100
	bl	printk
	b	.L1532
.L1531:
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
	bne	.L1533
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1533:
	ldr	x0, [x19,16]
	cbz	x0, .L1534
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,128]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1534
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1534:
	ldr	x0, [x19,8]
	cbz	x0, .L1532
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,120]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1532
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1532:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1551
.L1550:
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
	ldr	x1, [x0,-64]
	add	x21, x22, :lo12:.LANCHOR0
	ldr	x24, [x0,2120]
	add	x21, x21, 2104
	str	x1, [x19,8]
	mov	w23, 0
	str	x24, [x19,16]
	mov	w25, -1
	mov	w26, 61664
.L1554:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,1966]
	cmp	w23, w1
	bcs	.L1563
	ldrh	w20, [x0,2014]
	strh	w25, [x21,12]
.L1556:
	add	x0, x22, :lo12:.LANCHOR0
	sub	w20, w20, #1
	uxth	w20, w20
	ldrh	w0, [x0,2014]
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1557
	madd	w0, w23, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1556
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1556
	strh	w20, [x21,12]
.L1557:
	add	w23, w23, 1
	add	x21, x21, 2
	b	.L1554
.L1563:
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
	bne	.L1565
	ldrh	w19, [x3,2020]
	b	.L1566
.L1565:
	ldrh	w19, [x3,2018]
.L1566:
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
	bne	.L1567
	mov	w22, 0
	mov	w25, 2
.L1568:
	cmp	w22, w19
	bgt	.L1567
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
	bne	.L1569
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1569
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1569
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1568
.L1569:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1568
.L1567:
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
	adrp	x23, .LANCHOR4
	adrp	x24, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x0, x24, :lo12:.LANCHOR2
	add	x19, x23, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	add	x19, x19, 160
	adrp	x20, .LANCHOR0
	ldr	x22, [x0,2120]
	mov	w25, 61649
	str	xzr, [x19,8]
	str	x22, [x19,16]
	bl	FtlBbtMemInit
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w21, [x0,2014]
	sub	w21, w21, #1
	uxth	w21, w21
.L1576:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2014]
	sub	w0, w0, #48
	cmp	w21, w0
	ble	.L1579
	lsl	w0, w21, 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1577
	ldr	w0, [x19,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
.L1577:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1578
	ldrh	w0, [x22]
	cmp	w0, w25
	bne	.L1578
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2104
	strh	w21, [x1,2104]
	ldr	w1, [x22,4]
	str	w1, [x0,8]
	ldrh	w1, [x22,8]
	strh	w1, [x0,4]
	b	.L1579
.L1578:
	sub	w21, w21, #1
	uxth	w21, w21
	b	.L1576
.L1579:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,2104]
	cmp	w1, w2
	beq	.L1581
	ldrh	w1, [x21,2108]
	cmp	w1, w2
	beq	.L1583
	add	x25, x23, :lo12:.LANCHOR4
	lsl	w1, w1, 10
	add	x0, x25, 160
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x25,160]
	cmn	w0, #1
	beq	.L1583
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1583
	ldr	w0, [x22,4]
	ldr	w1, [x21,2112]
	cmp	w0, w1
	bls	.L1583
	str	w0, [x21,2112]
	ldrh	w1, [x21,2108]
	ldrh	w0, [x22,8]
	strh	w1, [x21,2104]
	strh	w0, [x21,2108]
.L1583:
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
.L1585:
	tbnz	w19, #31, .L1590
	ldrh	w0, [x25,2104]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w19, w0, lsl 10
	str	w0, [x21,4]
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,-64]
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1586
	ldrh	w0, [x22]
	cmp	w0, w26
	bne	.L1586
.L1590:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,2110]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1587
	b	.L1588
.L1586:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1585
.L1587:
	ldr	w2, [x1,1940]
	cmp	w0, w2
	beq	.L1588
	ldrh	w1, [x1,1954]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1588
	cmp	w2, w1
	bcs	.L1588
	bl	FtlSysBlkNumInit
.L1588:
	add	x20, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x21, x20, 2104
	add	x23, x23, :lo12:.LANCHOR4
.L1591:
	ldrh	w0, [x20,1966]
	mov	w1, w19
	add	x19, x19, 1
	cmp	w1, w0
	bcs	.L1609
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x3, [x23,168]
	ldrh	w2, [x0,2248]
	add	x0, x21, x19, lsl 3
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1591
.L1609:
	mov	w0, 0
.L1581:
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
	ldr	w2, [x20,2048]
	mov	w22, 65535
	ldr	x0, [x21,2232]
	lsl	w2, w2, 2
	strh	wzr, [x20,2066]
	bl	ftl_memset
	ldr	w2, [x20,2048]
	mov	w1, 0
	ldr	x0, [x21,2176]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2040]
	mov	w1, 0
	ldr	x0, [x21,2200]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2040]
	mov	w1, 0
	ldr	x0, [x20,2072]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x21, 280
	mov	w1, 255
	mov	w2, 12
	bl	ftl_memset
	ldrh	w21, [x20,1952]
.L1611:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,1954]
	cmp	w0, w21
	bls	.L1652
	mov	x27, 0
	ldrh	w5, [x1,1944]
	ldrh	w4, [x1,2030]
	mov	w20, w27
	mov	x23, x1
.L1653:
	cmp	w5, w27, uxth
	bls	.L1686
	add	x0, x23, 1976
	mov	w1, w21
	str	x4, [x29,112]
	str	x5, [x29,120]
	ldrb	w0, [x0,x27]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x5, [x29,120]
	ldr	x4, [x29,112]
	cbnz	w0, .L1612
	add	x2, x24, :lo12:.LANCHOR2
	mov	w0, 56
	lsl	w28, w28, 10
	mov	w3, 4
	umull	x0, w20, w0
	ldr	x1, [x2,2056]
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x2,2056]
	add	x0, x1, x0
	ldr	x1, [x2,896]
	ldr	x2, [x2,904]
	str	x1, [x0,8]
	mul	w1, w20, w4
	add	w20, w20, 1
	sdiv	w1, w1, w3
	uxth	w20, w20
	add	x1, x2, x1, sxtw 2
	str	x1, [x0,16]
.L1612:
	add	x27, x27, 1
	b	.L1653
.L1686:
	cbz	w20, .L1615
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	mov	w28, 61604
	ldr	x0, [x0,2056]
	bl	FlashReadPages
.L1616:
	cmp	w20, w23, uxth
	bls	.L1615
	mov	x0, 56
	mul	x6, x23, x0
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,2056]
	add	x1, x0, x6
	ldr	w0, [x0,x6]
	ldr	w5, [x1,4]
	cmn	w0, #1
	ldr	x26, [x1,16]
	ubfx	x25, x5, 10, 16
	bne	.L1618
	mov	w7, 16
.L1617:
	add	x8, x24, :lo12:.LANCHOR2
	str	x7, [x29,104]
	str	x8, [x29,112]
	str	x6, [x29,120]
	ldr	x0, [x8,2056]
	add	x0, x0, x6
	ldr	w1, [x0,4]
	add	w1, w1, 1
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x0, [x8,2056]
	mov	w2, w1
	add	x0, x0, x6
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	x8, [x29,112]
	cmp	w0, w22
	ldr	x6, [x29,120]
	ldr	x7, [x29,104]
	ldr	x0, [x8,2056]
	bne	.L1619
	str	w27, [x0,x6]
	b	.L1618
.L1619:
	ldr	w0, [x0,x6]
	cmn	w0, #1
	bne	.L1618
	sub	w7, w7, #1
	uxth	w7, w7
	cbnz	w7, .L1617
.L1618:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x1, [x0,2056]
	ldr	w1, [x1,x6]
	cmn	w1, #1
	beq	.L1621
	ldr	w0, [x0,196]
	cmn	w0, #1
	beq	.L1622
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1623
.L1622:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1623
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,196]
.L1623:
	ldrh	w0, [x26]
	cmp	w0, w28
	beq	.L1625
	bhi	.L1626
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1624
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldrh	w6, [x0,2040]
	ldrh	w0, [x0,2066]
	sub	w2, w6, #1
	ldr	x7, [x7,2200]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1639
.L1626:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1628
	cmp	w0, w22
	bne	.L1624
	mov	w0, w25
	b	.L1685
.L1628:
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldr	w6, [x0,2048]
	ldrh	w0, [x7,2320]
	ldr	x7, [x7,2232]
	uxth	w2, w6
	sub	w1, w2, #1
	sub	w2, w2, w0
	sub	w2, w2, #1
	sxth	x1, w1
	sxth	w2, w2
.L1630:
	cmp	w1, w2
	ble	.L1687
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bls	.L1631
	ldr	w2, [x7]
	cbnz	w2, .L1632
	cmp	w0, w6
	beq	.L1632
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,2320]
.L1632:
	mov	w0, 0
.L1633:
	cmp	w0, w1
	beq	.L1688
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
	b	.L1633
.L1688:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x2, [x0,2232]
	str	w6, [x2,x8]
	ldr	x2, [x0,2176]
	strh	w25, [x2,x9,lsl 1]
	tbz	w1, #31, .L1635
	b	.L1624
.L1631:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1630
.L1687:
	tbz	w1, #31, .L1668
	b	.L1624
.L1635:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2320]
	ldr	w2, [x2,2048]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1624
.L1668:
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldr	w4, [x26,4]
	strh	w0, [x2,2320]
	ldr	x0, [x2,2232]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2176]
	b	.L1684
.L1645:
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bhi	.L1689
	sub	w1, w1, #1
	sxth	x1, w1
.L1639:
	cmp	w1, w2
	bgt	.L1645
	b	.L1644
.L1689:
	ldr	w2, [x7]
	cbnz	w2, .L1641
	cmp	w0, w6
	beq	.L1641
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	strh	w0, [x2,2066]
.L1641:
	mov	w0, 0
.L1642:
	cmp	w0, w1
	beq	.L1690
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
	b	.L1642
.L1690:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	ldr	x0, [x0,2200]
	str	w2, [x0,x8]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2072]
	strh	w25, [x0,x9,lsl 1]
.L1644:
	tbnz	w1, #31, .L1624
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2040]
	ldrh	w6, [x2,2066]
	sub	w0, w0, #1
	sub	w0, w0, w6
	cmp	w1, w0, sxth
	bgt	.L1624
	add	x0, x24, :lo12:.LANCHOR2
	add	w6, w6, 1
	ldr	w4, [x26,4]
	strh	w6, [x2,2066]
	ldr	x0, [x0,2200]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2072]
.L1684:
	strh	w25, [x0,x1,lsl 1]
	b	.L1624
.L1625:
	add	x0, x24, :lo12:.LANCHOR2
	add	x1, x0, 280
	ldrh	w2, [x0,280]
	cmp	w2, w22
	bne	.L1646
	strh	w25, [x0,280]
	ldr	w0, [x26,4]
	str	w0, [x1,8]
	b	.L1624
.L1646:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1647
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1647:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	add	x0, x1, 280
	ldr	w6, [x0,8]
	cmp	w6, w2
	bcs	.L1648
	ldrh	w2, [x1,280]
	strh	w2, [x0,4]
	strh	w25, [x1,280]
	ldr	w1, [x26,4]
	str	w1, [x0,8]
	b	.L1624
.L1648:
	strh	w25, [x0,4]
	b	.L1624
.L1621:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,120]
	mov	w0, w25
	cbz	w1, .L1649
.L1685:
	mov	w1, 0
.L1649:
	bl	FtlFreeSysBlkQueueIn
.L1624:
	add	x23, x23, 1
	b	.L1616
.L1615:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1611
.L1652:
	add	x4, x24, :lo12:.LANCHOR2
	ldr	x2, [x4,2176]
	ldrh	w0, [x2]
	cbz	w0, .L1654
.L1657:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,2072]
	ldrh	w0, [x2]
	cbz	w0, .L1655
	b	.L1678
.L1654:
	ldrh	w4, [x4,2320]
	cbz	w4, .L1657
	ldr	w1, [x1,2048]
.L1658:
	cmp	w0, w1
	bcs	.L1657
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1659
	mov	w7, w0
.L1660:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w1, [x1,2048]
	cmp	w0, w1
	bcs	.L1657
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
	b	.L1660
.L1659:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1658
.L1655:
	ldrh	w4, [x1,2066]
	cbz	w4, .L1678
	ldrh	w1, [x1,2040]
.L1663:
	cmp	w0, w1
	mov	w7, w0
	bge	.L1678
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1664
.L1665:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2,2040]
	cmp	w0, w1
	bge	.L1678
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
	b	.L1665
.L1664:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1663
.L1678:
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
	stp	x29, x30, [sp, -80]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x20, .LANCHOR2
	adrp	x23, .LANCHOR4
	stp	x21, x22, [sp,32]
	add	x22, x20, :lo12:.LANCHOR2
	add	x21, x23, :lo12:.LANCHOR4
	str	x25, [sp,64]
	add	x21, x21, 160
	adrp	x19, .LANCHOR0
	add	x25, x22, 280
	ldr	x0, [x22,2120]
	str	x0, [x21,16]
	add	x0, x19, :lo12:.LANCHOR0
	str	xzr, [x21,8]
	ldrh	w2, [x0,1952]
	ldr	x0, [x22,-40]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x22,280]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1692
.L1700:
	mov	w19, -1
	b	.L1693
.L1692:
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w24, w0
	add	w0, w0, 1
	strh	w0, [x25,2]
	mov	w25, 61604
.L1694:
	tbnz	w24, #31, .L1699
	ldrh	w0, [x22,280]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w24, w0, lsl 10
	str	w0, [x21,4]
	ldr	x0, [x22,-64]
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1695
	ldr	x0, [x22,-64]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1695
	ldr	x0, [x22,2120]
	ldrh	w0, [x0]
	cmp	w0, w25
	bne	.L1695
.L1699:
	add	x21, x23, :lo12:.LANCHOR4
	add	x24, x20, :lo12:.LANCHOR2
	add	x21, x21, 160
	add	x22, x19, :lo12:.LANCHOR0
	mov	w2, 48
	add	x0, x24, 224
	ldr	x1, [x21,8]
	bl	ftl_memcpy
	ldr	x1, [x21,8]
	ldrh	w2, [x22,1952]
	ldr	x0, [x24,-40]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x22,1952]
	ldr	x3, [x21,8]
	ldr	x0, [x19,#:lo12:.LANCHOR0]
	uxth	x1, w2
	lsr	w2, w2, 3
	add	x1, x1, 24
	add	w2, w2, 4
	lsr	x1, x1, 1
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	ldrh	w0, [x22,2064]
	cbz	w0, .L1697
	ldrh	w0, [x22,1952]
	ldr	x3, [x21,8]
	lsr	w1, w0, 3
	ldrh	w2, [x22,2056]
	add	w1, w1, w0, lsl 1
	ldr	x0, [x24,2224]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x3, x1
	bl	ftl_memcpy
	b	.L1697
.L1695:
	sub	w24, w24, #1
	sxth	w24, w24
	b	.L1694
.L1697:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,224]
	cmp	w3, w2
	bne	.L1700
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w4, [x0,234]
	ldrh	w5, [x0,232]
	strh	w5, [x0,286]
	ldrh	w3, [x2,1966]
	cmp	w4, w3
	bne	.L1700
	ldrh	w4, [x2,2018]
	add	x23, x23, :lo12:.LANCHOR4
	ldrh	w3, [x2,2024]
	strh	wzr, [x0,298]
	mul	w4, w4, w5
	str	w4, [x2,2096]
	str	w5, [x23,216]
	mul	w4, w4, w3
	str	w4, [x2,2060]
	ldrh	w3, [x2,2110]
	ldr	w4, [x2,1956]
	ldrh	w2, [x2,1944]
	sub	w3, w4, w3
	ldrh	w4, [x0,238]
	sub	w3, w3, w5
	strb	wzr, [x0,302]
	strb	wzr, [x0,304]
	udiv	w2, w3, w2
	strh	w2, [x0,272]
	adrp	x2, .LANCHOR2
	strh	w4, [x2,#:lo12:.LANCHOR2]
	ldrh	w2, [x0,240]
	lsr	w3, w2, 6
	strh	w3, [x0,2]
	mov	w3, -1
	strh	w3, [x0,296]
	ldrh	w3, [x0,242]
	and	w2, w2, 63
	strh	w3, [x0,48]
	ldrh	w3, [x0,244]
	strb	w2, [x0,6]
	lsr	w5, w3, 6
	and	w3, w3, 63
	strb	w3, [x0,54]
	ldrb	w3, [x0,236]
	strb	w3, [x0,56]
	ldrh	w3, [x0,246]
	strh	w3, [x0,96]
	ldrh	w3, [x0,248]
	ldrb	w2, [x0,235]
	strh	w5, [x0,50]
	lsr	w5, w3, 6
	and	w3, w3, 63
	strb	w2, [x0,8]
	strb	w3, [x0,102]
	strh	w5, [x0,98]
	ldrb	w3, [x0,237]
	strb	w3, [x0,104]
	ldr	w2, [x0,256]
	str	w2, [x0,204]
	ldr	w1, [x0,264]
	ldr	w2, [x0,196]
	str	wzr, [x0,184]
	str	wzr, [x0,172]
	cmp	w1, w2
	str	wzr, [x0,164]
	str	wzr, [x0,180]
	str	wzr, [x0,208]
	str	wzr, [x0,216]
	str	wzr, [x0,176]
	bls	.L1701
	str	w1, [x0,196]
.L1701:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,260]
	ldr	w2, [x0,200]
	cmp	w1, w2
	bls	.L1702
	str	w1, [x0,200]
.L1702:
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1703
	add	x0, x20, :lo12:.LANCHOR2
	bl	make_superblock
.L1703:
	add	x1, x20, :lo12:.LANCHOR2
	add	x0, x1, 48
	ldrh	w2, [x1,48]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1704
	bl	make_superblock
.L1704:
	add	x1, x20, :lo12:.LANCHOR2
	add	x0, x1, 96
	ldrh	w2, [x1,96]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1705
	bl	make_superblock
.L1705:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x20, 296
	mov	w19, 0
	ldrh	w2, [x20,296]
	cmp	w2, w1
	beq	.L1693
	bl	make_superblock
.L1693:
	mov	w0, w19
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR1
	mov	x19, x0
	add	x0, x20, :lo12:.LANCHOR1
	str	w1, [x29,136]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	x20, [x29,128]
	ldrh	w28, [x0,3008]
	mov	w0, 65535
	cmp	w28, w0
	beq	.L1732
	cbnz	w28, .L1711
	b	.L1712
.L1732:
	mov	w28, 0
.L1711:
	adrp	x0, .LANCHOR0+2018
	ldr	w1, [x29,136]
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2018]
	cmp	w1, w0
	bne	.L1713
.L1712:
	bl	FtlGcPageVarInit
.L1713:
	mov	w0, -1
	str	w0, [x29,140]
	adrp	x0, .LC104
	mov	w21, 0
	add	x0, x0, :lo12:.LC104
	mov	w22, 65535
	adrp	x23, .LANCHOR0
	str	x0, [x29,120]
.L1726:
	ldrh	w0, [x19]
	strb	wzr, [x19,8]
	cmp	w0, w22
	beq	.L1733
.L1715:
.L1729:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w20, 0
	mov	w9, 4
	ldrh	w5, [x0,1944]
	ldrh	w8, [x0,2030]
	add	x5, x19, x5, lsl 1
.L1716:
	cmp	x2, x5
	beq	.L1741
	ldrh	w1, [x2,16]
	cmp	w1, w22
	beq	.L1717
	adrp	x3, .LANCHOR2
	mov	w0, 56
	add	x6, x3, :lo12:.LANCHOR2
	orr	w1, w28, w1, lsl 10
	umull	x0, w20, w0
	add	w4, w20, 1
	ldr	x7, [x6,2056]
	add	x7, x7, x0
	str	w1, [x7,4]
	ldr	x1, [x6,2056]
	ldr	x6, [x6,904]
	add	x0, x1, x0
	mul	w1, w20, w8
	uxth	w20, w4
	str	xzr, [x0,8]
	sdiv	w1, w1, w9
	add	x1, x6, x1, sxtw 2
	str	x1, [x0,16]
.L1717:
	add	x2, x2, 2
	b	.L1716
.L1741:
	adrp	x0, .LANCHOR2
	mov	w1, w20
	add	x0, x0, :lo12:.LANCHOR2
	mov	w2, 0
	mov	x24, 0
	adrp	x25, .LANCHOR2
	ldr	x0, [x0,2056]
	bl	FlashReadPages
.L1719:
	cmp	w20, w24, uxth
	bls	.L1742
	add	x27, x25, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x26, x24, x0
	ldr	x10, [x27,2056]
	str	x10, [x29,96]
	add	x1, x10, x26
	str	x1, [x29,104]
	ldr	w2, [x1,4]
	str	x2, [x29,112]
	lsr	x0, x2, 10
	bl	P2V_plane
	uxth	w7, w0
	ldr	x10, [x29,96]
	ldr	x2, [x29,112]
	ldr	w0, [x10,x26]
	cbnz	w0, .L1720
	ldr	x1, [x29,104]
	add	x24, x24, 1
	ldr	x6, [x1,16]
	ldrh	w0, [x6]
	cmp	w0, w22
	bne	.L1721
	mov	w0, 1
	str	w0, [x27,2344]
	b	.L1714
.L1721:
	mov	w1, w2
	ldr	w0, [x6,12]
	ldr	w2, [x6,8]
	bl	FtlGcUpdatePage
	b	.L1719
.L1720:
	ldr	x0, [x29,120]
	ldrh	w1, [x19]
	bl	printk
	ldr	w0, [x27,160]
	cbnz	w0, .L1722
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1723
.L1722:
	add	x0, x25, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,-80]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 119
	bls	.L1724
.L1723:
	add	x0, x25, :lo12:.LANCHOR2
	ldr	x0, [x0,2056]
	ldr	w0, [x0,x26]
	cmn	w0, #1
	bne	.L1725
.L1724:
	add	x0, x25, :lo12:.LANCHOR2
	ldr	x0, [x0,2056]
	add	x6, x0, x26
	ldr	w0, [x6,4]
	str	w0, [x29,140]
.L1725:
	add	x5, x25, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	mov	w28, 0
	ldr	x0, [x5,-40]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	bl	FtlGcPageVarInit
	b	.L1726
.L1742:
	ldr	w0, [x29,136]
	add	w21, w21, 1
	add	w3, w28, 1
	cmp	w21, w0
	uxth	w28, w3
	bcs	.L1728
.L1730:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2018]
	cmp	w0, w28
	bhi	.L1729
	b	.L1733
.L1728:
	ldr	x0, [x29,128]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3008]
	cmp	w0, w22
	beq	.L1730
	add	w0, w0, w21
	strh	w0, [x1,3008]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2018]
	cmp	w0, w28
	bls	.L1730
	b	.L1731
.L1733:
	mov	w7, 0
.L1714:
	ldr	x0, [x29,128]
	mov	w1, w28
	strh	w28, [x19,2]
	mov	w2, w7
	add	x20, x0, :lo12:.LANCHOR1
	mov	w0, -1
	strb	w7, [x19,6]
	strh	w0, [x20,3008]
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
.L1731:
	ldr	w0, [x29,140]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
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
	ldrb	w4, [x4,8]
	mov	w23, w2
	mov	w22, 0
	ldrb	w5, [x5,19]
	cbz	w4, .L1744
	bl	FlashProgSlc2KPages
	b	.L1745
.L1744:
	sub	w0, w5, #1
	mov	w27, 56
	mov	x28, 24
	str	w0, [x29,116]
.L1796:
	cmp	w22, w25
	bcs	.L1797
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
	bcc	.L1746
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1747
.L1746:
	ldrb	w1, [x2,1937]
	add	x3, x2, 1652
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x0, x1, x28, x3
	ldr	x0, [x0,8]
	cbz	x0, .L1749
	cmp	w4, 1
	bne	.L1750
	ldr	x0, [x2,128]
	bl	NandcIqrWaitFlashReady
.L1750:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1749:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	add	x0, x0, 1652
	ldr	w1, [x29,128]
	madd	x0, x2, x28, x0
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1751
	add	w1, w22, 1
	umull	x1, w1, w27
	add	x1, x19, x1
	str	x1, [x0,16]
.L1751:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, x2
	madd	x2, x2, x28, x1
	ldrb	w20, [x0,1848]
	ldrb	w0, [x1,1845]
	strb	w20, [x2,1652]
	cmp	w0, 1
	bne	.L1752
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1753
.L1752:
	mov	w0, w20
	str	x1, [x29,104]
	bl	NandcFlashCs
	ldr	x1, [x29,104]
	ldr	w0, [x29,132]
	add	x1, x1, 708
	ldr	w0, [x1,x0,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
.L1753:
	ldr	w0, [x29,116]
	cmp	w0, 6
	bhi	.L1754
	add	x2, x21, :lo12:.LANCHOR0
	add	x0, x2, x20, sxtw
	ldrb	w0, [x0,1616]
	cbz	w0, .L1754
	ldrb	w1, [x2,761]
	mov	w0, w20
	add	x2, x2, 764
	mov	w3, 0
	bl	HynixSetRRPara
.L1754:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1755
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1755
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1756
.L1755:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1756:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldrb	w2, [x29,124]
	mov	w0, w20
	ldr	x3, [x26,8]
	mov	w1, 1
	ldr	x4, [x26,16]
	bl	NandcXferData
	cbz	w24, .L1757
	ldr	w1, [x29,128]
	mov	w0, w20
	add	x26, x21, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29,132]
	add	x0, x26, 708
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w2, [x26,12]
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
.L1757:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1747:
	add	w22, w22, 1
	b	.L1796
.L1797:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	add	x22, x21, 1652
	mov	x24, 24
	ldr	x0, [x21,128]
	bl	NandcIqrWaitFlashReady
.L1759:
	ldrb	w0, [x21,1845]
	cmp	w0, w20
	bls	.L1798
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1760
	ldrb	w0, [x21,120]
	cbz	w0, .L1760
	mul	x0, x20, x24
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L1760:
	add	x20, x20, 1
	b	.L1759
.L1798:
	ldr	w0, [x29,120]
	cbnz	w0, .L1762
.L1770:
	mov	w0, 0
	b	.L1745
.L1762:
	adrp	x24, .LC103
	adrp	x26, .LC102
	adrp	x27, .LC101
	mov	w21, 0
	add	x24, x24, :lo12:.LC103
	add	x26, x26, :lo12:.LC102
	add	x27, x27, :lo12:.LC101
.L1763:
	cmp	w21, w25
	beq	.L1770
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1764
	adrp	x0, .LC100
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC100
	bl	printk
	b	.L1765
.L1764:
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
	bne	.L1766
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1766:
	ldr	x0, [x19,16]
	cbz	x0, .L1767
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,128]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1767
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1767:
	ldr	x0, [x19,8]
	cbz	x0, .L1765
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,120]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1765
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1765:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1763
.L1745:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FlashTestBlk.part.14, %function
FlashTestBlk.part.14:
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
	.size	FlashTestBlk.part.14, .-FlashTestBlk.part.14
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
	bcc	.L1801
	mov	w0, w1
	bl	FlashTestBlk.part.14
.L1801:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	adrp	x19, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w21, 0
	ldr	x0, [x0,112]
	str	x0, [x29,160]
	add	x0, x19, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x28, x19, 708
	ldrh	w1, [x0,180]
	ldrh	w23, [x0,182]
	mul	w23, w1, w23
	ldr	x1, [x0,744]
	uxth	w23, w23
	ldrb	w1, [x1,24]
	str	w1, [x29,128]
	ldrh	w1, [x0,12]
	str	w1, [x29,172]
	ldrb	w0, [x0,8]
	ubfiz	w1, w1, 1, 15
	cmp	w0, 1
	ldr	w0, [x29,172]
	csel	w0, w1, w0, eq
	str	w0, [x29,172]
	adrp	x0, .LC105
	mov	w1, 1
	add	x0, x0, :lo12:.LC105
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w2, 4096
	ldr	x0, [x0,112]
	bl	ftl_memset
	lsr	w0, w23, 4
	str	w0, [x29,168]
	ldr	w0, [x29,128]
	and	w0, w0, 1
	str	w0, [x29,116]
	sub	w0, w23, #1
	uxth	w0, w0
	str	w0, [x29,124]
.L1805:
	ldrb	w0, [x19,1845]
	cmp	w0, w21
	bls	.L1856
	sxtw	x25, w21
	add	x0, x20, :lo12:.LANCHOR4
	add	x1, x0, x25, lsl 1
	ldrh	w22, [x1,144]
	cbnz	w22, .L1835
	ldrh	w2, [x19,188]
	mov	w1, w22
	ldr	x0, [x0,56]
	mov	w26, w22
	lsl	w2, w2, 9
	mov	w24, w22
	bl	ftl_memset
	add	x0, x19, x25
	ldrb	w27, [x0,1848]
	ldr	w0, [x29,128]
	and	w0, w0, 2
	uxtb	w0, w0
	str	w0, [x29,120]
.L1807:
	uxth	w0, w26
	str	w0, [x29,144]
	cmp	w0, w23
	bcs	.L1817
	mov	w0, -1
	strb	w0, [x29,182]
	strb	w0, [x29,183]
	ldr	w0, [x29,116]
	cbz	w0, .L1809
	ldr	w3, [x28,x25,lsl 2]
	mov	w0, w27
	add	x2, x29, 182
	add	w3, w22, w3
	str	x3, [x29,104]
	mov	w1, w3
	bl	FlashReadSpare
	ldrb	w0, [x19,8]
	ldr	x3, [x29,104]
	cmp	w0, 1
	bne	.L1809
	ldr	w1, [x19,12]
	mov	w0, w27
	add	x2, x29, 183
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,182]
	ldrb	w1, [x29,183]
	and	w0, w1, w0
	strb	w0, [x29,182]
.L1809:
	ldr	w0, [x29,120]
	cbz	w0, .L1811
	ldr	x0, [x19,744]
	add	x2, x29, 183
	ldrh	w1, [x0,10]
	ldr	w0, [x28,x25,lsl 2]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w27
	add	w1, w1, w22
	bl	FlashReadSpare
.L1811:
	ldr	x0, [x19,744]
	ldrb	w1, [x29,182]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1836
	cmp	w0, 1
	bne	.L1812
.L1836:
	mov	w0, 1
	cbz	w1, .L1814
	ldrb	w0, [x29,183]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1814
.L1812:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1814
	ldrb	w0, [x29,183]
	cmp	w0, 255
	cset	w0, ne
.L1814:
	ldr	x1, [x29,128]
	tbz	x1, 2, .L1815
	ldr	w1, [x28,x25,lsl 2]
	mov	w0, w27
	add	w1, w22, w1
	bl	SandiskProgTestBadBlock
.L1815:
	cbz	w0, .L1816
	adrp	x0, .LC106
	mov	w1, w21
	mov	w2, w26
	add	x0, x0, :lo12:.LC106
	add	w24, w24, 1
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
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
	ldr	w1, [x29,168]
	ldrb	w0, [x19,1845]
	mul	w0, w1, w0
	cmp	w24, w0
	bgt	.L1817
.L1816:
	ldr	w0, [x29,172]
	add	w26, w26, 1
	add	w22, w22, w0
	b	.L1807
.L1817:
	adrp	x0, .LC107
	mov	w1, w21
	add	x0, x0, :lo12:.LC107
	mov	w2, w24
	bl	printk
	ldrb	w0, [x19,1845]
	ldr	w1, [x29,168]
	mul	w0, w1, w0
	cmp	w24, w0
	blt	.L1819
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w2, [x19,188]
	mov	w1, 0
	lsl	w2, w2, 9
	ldr	x0, [x0,56]
	bl	ftl_memset
.L1819:
	cbnz	w21, .L1821
	add	x0, x20, :lo12:.LANCHOR4
	adrp	x26, .LC108
	mov	w22, w21
	add	x26, x26, :lo12:.LC108
	mov	w24, 1
	ldrh	w27, [x0,92]
.L1822:
	ldrb	w0, [x19,9]
	cmp	w0, w27
	bls	.L1857
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1823
	mov	w1, w27
	mov	x0, x26
	add	w22, w22, 1
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ubfx	x1, x27, 5, 11
	lsl	x1, x1, 2
	uxth	w22, w22
	ldr	x2, [x0,56]
	lsl	w0, w24, w27
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1823:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1822
.L1857:
	adrp	x26, .LC108
	ldr	w24, [x29,124]
	sub	w27, w23, #50
	add	x26, x26, :lo12:.LC108
.L1825:
	cmp	w24, w27
	ble	.L1858
	mov	w0, w24
	bl	FlashTestBlk
	cbz	w0, .L1826
	mov	w1, w24
	mov	x0, x26
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ubfx	x1, x24, 5, 11
	lsl	x1, x1, 2
	ldr	x3, [x0,56]
	mov	w0, 1
	lsl	w0, w0, w24
	ldr	w4, [x3,x1]
	orr	w0, w4, w0
	str	w0, [x3,x1]
.L1826:
	sub	w2, w24, #1
	uxth	w24, w2
	b	.L1825
.L1858:
	add	x1, x20, :lo12:.LANCHOR4
	ldrb	w0, [x19,9]
	ldr	w2, [x1,92]
	sub	w0, w0, w2
	cmp	w22, w0
	bcc	.L1821
	ldrh	w2, [x19,188]
	ldr	x0, [x1,56]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L1821:
	adrp	x24, .LC109
	ldr	w27, [x29,124]
	mul	w26, w21, w23
	add	x24, x24, :lo12:.LC109
	lsl	x25, x25, 1
	mov	w22, -3872
.L1829:
	mov	w2, w27
	mov	x0, x24
	mov	w1, w21
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x2, [x0,56]
.L1830:
	ubfx	x0, x27, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w27
	and	w1, w0, 1
	tbz	x0, 0, .L1859
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1830
.L1859:
	add	x0, x20, :lo12:.LANCHOR4
	add	x2, x0, x25
	strh	w27, [x2,144]
	ldr	x2, [x29,160]
	strh	w22, [x2]
	strh	w27, [x2,2]
	strh	w1, [x2,8]
	ldr	x1, [x0,56]
	ldr	x0, [x0,112]
	str	x0, [x29,200]
	add	w0, w27, w26
	str	x1, [x29,192]
	lsl	w0, w0, 10
	mov	w1, 1
	str	w0, [x29,188]
	mov	w2, w1
	add	x0, x29, 184
	bl	FlashEraseBlocks
	mov	w1, 1
	add	x0, x29, 184
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x29,184]
	cbz	w0, .L1835
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1829
.L1835:
	add	w21, w21, 1
	uxtb	w21, w21
	b	.L1805
.L1856:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 240
	ret
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	stp	x25, x26, [sp,64]
	uxth	w26, w0
	add	x0, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	uxtb	w25, w1
	ldr	w2, [x0,-96]
	mov	w0, 0
	cbnz	w2, .L1861
	adrp	x22, .LANCHOR0
	mov	w28, w2
	add	x23, x22, :lo12:.LANCHOR0
	mov	w21, w2
	mov	w24, w2
	mov	w27, 56
	ldrb	w0, [x23,120]
	str	w0, [x29,120]
	str	w26, [x23,2100]
.L1862:
	ldrh	w0, [x23,1944]
	cmp	w0, w28
	bls	.L1906
	add	x1, x20, :lo12:.LANCHOR2
	umull	x0, w28, w27
	ldr	x1, [x1,-88]
	str	wzr, [x1,x0]
	add	x0, x23, 1976
	mov	w1, w26
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	uxth	w19, w0
	mov	w1, w19
	cbz	w25, .L1863
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L1864
.L1863:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1865
	add	x3, x20, :lo12:.LANCHOR2
	lsl	w19, w19, 10
	umull	x0, w21, w27
	mov	w2, 4
	ldr	x1, [x3,-88]
	add	x1, x1, x0
	str	w19, [x1,4]
	ldr	x1, [x3,-88]
	ldr	x3, [x3,2128]
	add	x1, x1, x0
	ldrh	w0, [x23,2030]
	mul	w0, w21, w0
	add	w21, w21, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1864
.L1865:
	add	w24, w24, 1
	uxth	w24, w24
.L1864:
	add	w2, w28, 1
	uxth	w28, w2
	b	.L1862
.L1906:
	mov	w19, w24
	mov	w0, w24
	cbz	w21, .L1861
	ldr	w0, [x29,120]
	mov	w2, w21
	mov	x24, 0
	mov	x27, 56
	cmp	w0, wzr
	add	x0, x20, :lo12:.LANCHOR2
	cset	w23, ne
	mov	w1, w23
	ldr	x0, [x0,-88]
	bl	FlashEraseBlocks
.L1868:
	cmp	w21, w24, uxth
	bls	.L1907
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x24, x27
	ldr	x1, [x1,-88]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1869
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1869:
	add	x24, x24, 1
	b	.L1868
.L1907:
	cbnz	w25, .L1871
	uxth	w23, w23
	mov	w28, 6
	mov	w27, 1
	b	.L1872
.L1871:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w27, [x0,2020]
	ldrb	w0, [x0,120]
	cbnz	w0, .L1888
	lsr	w28, w27, 2
	mov	w23, 1
	b	.L1872
.L1888:
	mov	w23, 1
	mov	w28, w23
.L1872:
	add	x0, x22, :lo12:.LANCHOR0
	mov	w24, 0
	str	x0, [x29,120]
.L1881:
	mov	w22, 0
	mov	w21, w22
.L1873:
	ldr	x0, [x29,120]
	ldrh	w0, [x0,1944]
	cmp	w0, w22
	bls	.L1908
	add	x1, x20, :lo12:.LANCHOR2
	mov	w0, 56
	umull	x0, w22, w0
	ldr	x1, [x1,-88]
	str	wzr, [x1,x0]
	mov	w1, w26
	ldr	x0, [x29,120]
	add	x0, x0, 1976
	ldrb	w0, [x0,w22,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w25, .L1874
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L1875
.L1874:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1875
	add	x3, x20, :lo12:.LANCHOR2
	mov	w0, 56
	ldr	w2, [x29,112]
	umull	x0, w21, w0
	ldr	x1, [x3,-88]
	add	w4, w24, w2, lsl 10
	mov	w2, 4
	add	x1, x1, x0
	str	w4, [x1,4]
	ldr	x1, [x3,-88]
	add	x1, x1, x0
	ldr	x0, [x3,2104]
	ldr	x3, [x3,2112]
	str	x0, [x1,8]
	ldr	x0, [x29,120]
	ldrh	w0, [x0,2030]
	mul	w0, w21, w0
	add	w21, w21, 1
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
.L1875:
	add	w2, w22, 1
	uxth	w22, w2
	b	.L1873
.L1908:
	cbz	w21, .L1885
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w21
	mov	w2, w23
	mov	w3, 1
	ldr	x0, [x0,-88]
	bl	FlashProgPages
	mov	x1, 0
.L1878:
	cmp	w21, w1, uxth
	bls	.L1909
	add	x2, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x1, x0
	ldr	x2, [x2,-88]
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L1879
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L1879:
	add	x1, x1, 1
	b	.L1878
.L1909:
	add	w24, w24, w28
	uxth	w24, w24
	cmp	w24, w27
	bcc	.L1881
	mov	x22, 0
	mov	x24, 56
.L1882:
	cmp	w21, w22, uxth
	bls	.L1910
	cbz	w25, .L1883
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x22, x24
	ldr	x1, [x1,-88]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L1883
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1883:
	add	x22, x22, 1
	b	.L1882
.L1910:
	cmp	w26, 63
	bls	.L1889
	cbz	w25, .L1885
.L1889:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, w21
	ldr	x0, [x20,-88]
	bl	FlashEraseBlocks
.L1885:
	mov	w0, w19
.L1861:
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
	adrp	x21, .LANCHOR2
	add	x2, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w1, [x2,-96]
	cbnz	w1, .L1913
	adrp	x19, .LANCHOR4
	adrp	x20, .LANCHOR0
	add	x24, x19, :lo12:.LANCHOR4
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x2,-64]
	mov	x23, 0
	ldr	x2, [x2,2120]
	add	x22, x20, 2104
	str	x2, [x24,176]
	ldrh	w2, [x20,2028]
	str	x0, [x24,168]
	bl	ftl_memset
.L1914:
	ldrh	w1, [x20,1966]
	mov	w0, w23
	add	x23, x23, 1
	cmp	w0, w1
	bge	.L1922
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x3, [x24,168]
	ldrh	w2, [x1,2248]
	add	x1, x22, x23, lsl 3
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L1914
.L1922:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 255
	add	x19, x19, 160
	mov	w2, 16
	mov	w28, 0
	adrp	x25, .LC110
	ldr	x27, [x19,16]
	adrp	x26, .LC111
	mov	w24, w28
	add	x25, x25, :lo12:.LC110
	mov	x0, x27
	add	x26, x26, :lo12:.LC111
	bl	ftl_memset
	mov	w0, -3887
	strh	w0, [x27]
	ldr	w0, [x22,8]
	str	w0, [x27,4]
	ldrh	w0, [x20,2104]
	strh	w0, [x27,2]
	ldrh	w0, [x22,4]
	strh	w0, [x27,8]
	ldrh	w0, [x22,6]
	strh	w0, [x27,10]
	ldr	w0, [x20,1940]
	strh	w0, [x27,12]
.L1916:
	add	x23, x21, :lo12:.LANCHOR2
	ldrh	w1, [x20,2104]
	ldrh	w2, [x20,2106]
	ldrh	w3, [x20,2108]
	ldr	x0, [x23,-64]
	str	x0, [x19,8]
	ldr	x0, [x23,2120]
	str	x0, [x19,16]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19,4]
	mov	x0, x25
	ldrh	w4, [x27,10]
	str	wzr, [x19]
	bl	printk
	mov	w1, 1
	mov	x0, x19
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w0, [x20,2020]
	ldrh	w1, [x20,2106]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L1917
	ldr	w1, [x20,2112]
	ldrh	w0, [x20,2104]
	add	w1, w1, 1
	str	w1, [x20,2112]
	str	w1, [x27,4]
	strh	w0, [x27,8]
	ldrh	w1, [x20,2108]
	strh	w0, [x20,2108]
	ldr	x0, [x23,-88]
	strh	w1, [x20,2104]
	lsl	w1, w1, 10
	str	w1, [x19,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	strh	wzr, [x20,2106]
	ldr	x0, [x23,-88]
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x19
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
.L1917:
	ldrh	w0, [x20,2106]
	add	w0, w0, 1
	strh	w0, [x20,2106]
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1918
	ldr	w1, [x19,4]
	mov	x0, x26
	add	w28, w28, 1
	bl	printk
	uxth	w28, w28
	cmp	w28, 3
	bls	.L1916
	adrp	x0, .LC112
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC112
	mov	w2, w28
	add	x21, x21, :lo12:.LANCHOR2
	bl	printk
	mov	w0, 1
	str	w0, [x21,-96]
	b	.L1913
.L1918:
	cbnz	w24, .L1913
	mov	w24, 1
	b	.L1916
.L1913:
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
	ldr	w1, [x19,-96]
	stp	x27, x28, [sp,80]
	cbnz	w1, .L1924
	adrp	x22, .LANCHOR0
	mov	x20, x0
	add	x24, x19, 96
	sub	x25, x19, #16
	mov	x23, x22
.L1925:
	cmp	x20, x24
	bne	.L1926
	ldrh	w2, [x19,-8]
	ldr	w4, [x19,876]
	lsr	w3, w2, 1
	add	w1, w3, 1
	mul	w0, w2, w4
	add	w1, w1, w0, lsr 2
	ldr	w0, [x19,160]
	uxth	w1, w1
	cbz	w0, .L1927
	ldr	w0, [x19,220]
	cmp	w0, 29
	bhi	.L1927
	cmp	w0, 2
	mov	w1, 0
	bls	.L1928
	tbz	x2, 0, .L1955
	cbz	w4, .L1928
.L1955:
	mov	w1, w3
	b	.L1927
.L1926:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L1928
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w2, [x0,1964]
	cmp	w2, 1
	beq	.L1928
	ldrb	w0, [x0,120]
	cbnz	w0, .L1928
	ldr	w2, [x19,160]
	ldrh	w0, [x19,-8]
	lsr	w1, w0, 3
	cbz	w2, .L1927
	ldr	w2, [x19,220]
	cmp	w2, 1
	bhi	.L1927
	mov	w1, 7
	mul	w1, w0, w1
	lsr	w1, w1, 3
.L1927:
	cbz	w1, .L1928
	sub	w1, w1, #1
	uxth	w1, w1
.L1928:
	ldrb	w2, [x20,8]
	mov	x0, x25
	bl	List_pop_index_node
	uxth	w27, w0
	ldrh	w0, [x19,-8]
	sub	w0, w0, #1
	strh	w0, [x19,-8]
	strh	w27, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L1930
	add	x0, x22, :lo12:.LANCHOR0
	mov	x3, 56
	mov	w6, 65535
	ldrh	w5, [x0,1944]
	mov	x0, 0
	mov	w26, w0
	b	.L1931
.L1930:
	ubfiz	x27, x27, 1, 16
	ldr	x0, [x19,-40]
	mov	w1, -1
	strh	w1, [x0,x27]
	b	.L1925
.L1934:
	ldr	x4, [x19,-88]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L1933
	umull	x2, w26, w3
	add	w26, w26, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w26, w26
	str	w1, [x2,4]
.L1933:
	add	x0, x0, 1
.L1931:
	cmp	w5, w0, uxth
	bhi	.L1934
	cmp	x20, x19
	uxtw	x28, w27
	bne	.L1935
	ldr	w0, [x19,160]
	cbz	w0, .L1935
	ldr	x0, [x19,-80]
	ldrh	w0, [x0,x28,lsl 1]
	cmp	w0, 30
	bls	.L1935
	strb	wzr, [x19,8]
.L1935:
	ldrb	w0, [x20,8]
	cbnz	w0, .L1936
	lsl	x0, x28, 1
	ldr	x1, [x19,-80]
	mov	w2, 2
	ldrh	w3, [x1,x0]
	cbz	w3, .L1980
	add	x2, x23, :lo12:.LANCHOR0
	ldrh	w2, [x2,2008]
	add	w2, w3, w2
.L1980:
	strh	w2, [x1,x0]
	mov	w1, 0
	ldr	w0, [x19,204]
	add	w0, w0, 1
	str	w0, [x19,204]
	mov	w0, w27
	bl	ftl_set_blk_mode
	b	.L1939
.L1936:
	lsl	x0, x28, 1
	ldr	x2, [x19,-80]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x19,208]
	add	w0, w0, 1
	str	w0, [x19,208]
	mov	w0, w27
	bl	ftl_set_blk_mode.part.8
.L1939:
	lsl	x28, x28, 1
	ldr	x0, [x19,-80]
	ldr	w1, [x19,216]
	ldrh	w0, [x0,x28]
	cmp	w0, w1
	bls	.L1940
	str	w0, [x19,216]
.L1940:
	add	x2, x23, :lo12:.LANCHOR0
	ldr	w3, [x19,204]
	ldr	w0, [x19,208]
	ldrh	w1, [x2,2008]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,1952]
	mov	x3, 56
	udiv	w0, w0, w1
	ldr	x1, [x19,2160]
	str	w0, [x19,212]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L1941:
	cmp	w26, w0, uxth
	bls	.L1982
	ldr	x1, [x19,-88]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L1941
.L1982:
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1943
	ldrb	w0, [x20,8]
	cmp	w0, 1
	ldr	x0, [x19,-88]
	bne	.L1944
	mov	w1, 0
	b	.L1981
.L1944:
	mov	w1, 1
.L1981:
	mov	w2, w26
	bl	FlashEraseBlocks
.L1943:
	ldrb	w1, [x20,8]
	mov	w2, w26
	ldr	x0, [x19,-88]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	x4, 56
	mov	w2, w1
.L1945:
	cmp	w26, w1, uxth
	bls	.L1983
	mul	x0, x1, x4
	ldr	x3, [x19,-88]
	add	x5, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	bne	.L1946
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
.L1946:
	add	x1, x1, 1
	b	.L1945
.L1983:
	cbz	w2, .L1948
	mov	w0, w27
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1948:
	ldrb	w0, [x20,7]
	cbnz	w0, .L1949
	ldr	x0, [x19,-40]
	mov	w1, -1
	strh	w1, [x0,x28]
	b	.L1925
.L1949:
	adrp	x1, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x1,2018]
	strh	w27, [x20]
	mul	w0, w0, w1
	ldr	w1, [x21,196]
	str	w1, [x20,12]
	uxth	w0, w0
	add	w1, w1, 1
	strh	w0, [x20,4]
	str	w1, [x21,196]
	ldr	x1, [x21,-40]
	ldrh	w2, [x20]
	strh	w0, [x1,x2,lsl 1]
.L1924:
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
	ldrh	w0, [x19,956]
	stp	x21, x22, [sp,32]
	mov	w21, 0
	cbz	w0, .L1986
	adrp	x22, .LANCHOR0
	adrp	x24, .LC113
	add	x22, x22, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LC113
.L1994:
	ldrh	w0, [x22,1944]
	cmp	w0, w21
	bls	.L1995
	add	x0, x22, 1976
	mov	w1, w23
	mov	w20, 0
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w25, w0
.L1987:
	ldrh	w0, [x19,956]
	cmp	w0, w20
	bls	.L1996
	add	x0, x19, 960
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w25
	bne	.L1988
	mov	w1, w25
	mov	x0, x24
	bl	printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,956]
	mov	w3, w20
.L1989:
	cmp	w3, w2
	bcs	.L1997
	add	x4, x19, 960
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L1989
.L1997:
	sub	w2, w2, #1
	strh	w2, [x19,956]
.L1988:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1987
.L1996:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1994
.L1995:
	bl	FtlGcReFreshBadBlk
.L1986:
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
	uxth	w19, w0
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	ubfiz	x2, x19, 1, 16
	mov	x20, x0
	ldr	x3, [x1,-40]
	ldrh	w2, [x3,x2]
	cbnz	w2, .L1999
	ldrh	w3, [x1,296]
	cmp	w3, w19
	bne	.L2000
	mov	w0, -1
	strh	w0, [x1,296]
	b	.L2001
.L2000:
	ldrh	w3, [x0,#:lo12:.LANCHOR2]
	mov	w0, w2
	cmp	w3, w19
	beq	.L2002
	ldrh	w3, [x1,48]
	cmp	w3, w19
	beq	.L2002
	ldrh	w1, [x1,96]
	cmp	w1, w19
	beq	.L2002
.L2001:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w19
	sub	x0, x20, #48
	bl	List_remove_node
	ldrh	w0, [x20,-24]
	sub	w0, w0, #1
	strh	w0, [x20,-24]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L2002
.L1999:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2002:
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
	adrp	x2, .LANCHOR2
	beq	.L2007
	add	x0, x2, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x0, [x0,-40]
	ldrh	w19, [x0,x3]
	cbnz	w19, .L2008
	adrp	x0, .LC114
	mov	w2, w19
	add	x0, x0, :lo12:.LC114
	bl	printk
	b	.L2012
.L2008:
	sub	w19, w19, #1
	strh	w19, [x0,x3]
.L2007:
	add	x19, x2, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w2, [x19,2336]
	cmp	w2, w0
	bne	.L2010
	strh	w1, [x19,2336]
.L2012:
	mov	w0, 0
	b	.L2009
.L2010:
	cmp	w2, w1
	str	x1, [x29,40]
	mov	w0, 0
	beq	.L2009
	mov	w0, w2
	bl	update_vpc_list
	cmp	w0, wzr
	ldr	x1, [x29,40]
	cset	w0, ne
	strh	w1, [x19,2336]
.L2009:
	ldr	x19, [sp,16]
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
	cbz	w1, .L2013
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2013
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR2
	adrp	x22, .LANCHOR0
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR2
	add	x21, x21, 996
	ldrh	w0, [x19,x0,lsl 1]
.L2017:
	cmp	w0, w20
	bne	.L2024
.L2019:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1944]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2018
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2018:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2017
.L2024:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2020
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2020
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w20
	bne	.L2020
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2019
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2013
.L2020:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,120]
	cbz	w2, .L2013
	cmp	w1, 1
	bne	.L2013
	ldrh	w2, [x19,2]
	ldrh	w1, [x0,2020]
	cmp	w2, w1
	bcc	.L2013
	adrp	x2, .LANCHOR2-40
	ldrh	w1, [x19]
	ldrh	w4, [x19,4]
	lsl	x1, x1, 1
	ldr	x3, [x2,#:lo12:.LANCHOR2-40]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,2018]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2013:
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
	add	x21, x21, 996
	ldrh	w0, [x19,x0,lsl 1]
.L2026:
	cmp	w0, w22
	adrp	x20, .LANCHOR0
	bne	.L2041
.L2027:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1944]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2028
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2028:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2026
.L2041:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2029
	add	x1, x23, :lo12:.LANCHOR0
	ldrb	w1, [x1,120]
	cbnz	w1, .L2029
	ldrh	w1, [x19,2]
	ldrh	w1, [x21,w1,sxtw 1]
	cmp	w1, w22
	bne	.L2029
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2027
.L2029:
	ldrh	w1, [x19,4]
	adrp	x21, .LANCHOR2
	ldrh	w22, [x19,2]
	mov	w23, 65535
	add	x21, x21, :lo12:.LANCHOR2
	sub	w1, w1, #1
	orr	w22, w22, w0, lsl 10
	strh	w1, [x19,4]
	mov	w24, w23
	add	x21, x21, 996
.L2030:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,1944]
.L2032:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2031
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2031:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2032
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2037
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2034
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w24
	bne	.L2034
	ldrh	w1, [x19,4]
	cbz	w1, .L2034
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2030
.L2034:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,120]
	cbz	w0, .L2037
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,2020]
	cmp	w1, w0
	bcc	.L2037
	adrp	x0, .LANCHOR2-40
	ldrh	w1, [x19]
	ldrh	w3, [x19,4]
	lsl	x1, x1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR2-40]
	ldrh	w2, [x0,x1]
	sub	w2, w2, w3
	strh	w2, [x0,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x20,2018]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2037:
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
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	add	x19, x23, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	ldr	w0, [x19,-96]
	cbnz	w0, .L2044
	adrp	x24, .LANCHOR4
	ldr	x22, [x19,2120]
	add	x21, x24, :lo12:.LANCHOR4
	ldr	x1, [x19,-64]
	ldrh	w2, [x19,280]
	adrp	x25, .LANCHOR0
	add	x26, x25, :lo12:.LANCHOR0
	add	x20, x19, 224
	str	x1, [x21,168]
	add	x21, x21, 160
	str	x22, [x21,16]
	strh	w2, [x22,2]
	mov	w2, -3932
	strh	w2, [x22]
	str	w0, [x22,12]
	ldr	w2, [x19,288]
	str	w0, [x22,8]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w2, [x22,4]
	str	w0, [x19,224]
	mov	w0, 71
	movk	w0, 0x5000, lsl 16
	str	w0, [x19,228]
	ldrh	w0, [x19,286]
	strh	w0, [x19,232]
	ldrh	w0, [x26,1966]
	strb	w0, [x19,234]
	ldrh	w0, [x23,#:lo12:.LANCHOR2]
	strh	w0, [x19,238]
	ldrh	w0, [x19,2]
	ldrb	w1, [x19,6]
	ldrb	w2, [x19,54]
	orr	w0, w1, w0, lsl 6
	ldrh	w1, [x19,48]
	strh	w1, [x19,242]
	ldrh	w1, [x19,50]
	strh	w0, [x19,240]
	ldrb	w0, [x19,8]
	orr	w1, w2, w1, lsl 6
	strb	w0, [x19,235]
	strh	w1, [x19,244]
	ldrb	w0, [x19,56]
	ldrh	w1, [x19,96]
	strb	w0, [x19,236]
	strh	w1, [x19,246]
	ldrh	w1, [x19,98]
	ldrb	w0, [x19,104]
	strb	w0, [x19,237]
	ldr	w0, [x19,204]
	ldrb	w2, [x19,102]
	str	w0, [x19,256]
	ldr	w0, [x19,196]
	orr	w1, w2, w1, lsl 6
	str	w0, [x19,264]
	ldr	w0, [x19,200]
	str	w0, [x19,260]
	ldrh	w2, [x26,2028]
	ldr	x0, [x21,8]
	strh	w1, [x19,248]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x21,8]
	mov	x1, x20
	mov	w2, 48
	bl	ftl_memcpy
	ldr	x0, [x21,8]
	ldrh	w2, [x26,1952]
	ldr	x1, [x19,-40]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x26,1952]
	ldr	x1, [x21,8]
	uxth	x0, w2
	lsr	w2, w2, 3
	add	x0, x0, 24
	add	w2, w2, 4
	lsr	x0, x0, 1
	add	x0, x1, x0, lsl 2
	ldr	x1, [x25,#:lo12:.LANCHOR0]
	bl	ftl_memcpy
	ldrh	w0, [x26,2064]
	cbz	w0, .L2045
	ldrh	w0, [x26,1952]
	ldrh	w2, [x26,2056]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x21,8]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,2216]
	bl	ftl_memcpy
.L2045:
	add	x19, x24, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w21, 0
	mov	w27, 65535
	add	x19, x19, 160
	add	x20, x23, :lo12:.LANCHOR2
	bl	FtlUpdateVaildLpn
.L2046:
	ldr	x0, [x20,-64]
	str	x0, [x19,8]
	ldr	x0, [x20,2120]
	str	x0, [x19,16]
	ldrh	w0, [x20,280]
	ldrh	w1, [x20,282]
	orr	w0, w1, w0, lsl 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	w3, w1
	mov	x0, x19
	bl	FlashProgPages
	add	x0, x25, :lo12:.LANCHOR0
	ldrh	w1, [x20,282]
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2047
	ldrh	w0, [x20,280]
	ldrh	w27, [x20,284]
	strh	wzr, [x20,282]
	strh	w0, [x20,284]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x20,196]
	str	w1, [x20,288]
	add	w2, w1, 1
	str	w2, [x20,196]
	ubfiz	w2, w0, 10, 16
	str	w2, [x19,4]
	str	w1, [x22,4]
	mov	w1, 1
	strh	w0, [x20,280]
	mov	w2, w1
	strh	w0, [x22,2]
	mov	w3, w1
	mov	x0, x19
	bl	FlashProgPages
.L2047:
	ldrh	w1, [x20,282]
	ldr	w2, [x19]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x20,282]
	bne	.L2048
	cmp	w1, 1
	bne	.L2049
	add	x1, x25, :lo12:.LANCHOR0
	ldrh	w1, [x1,2020]
	sub	w1, w1, #1
	strh	w1, [x20,282]
.L2049:
	add	w21, w21, 1
	uxth	w21, w21
	cmp	w21, 3
	bls	.L2046
	add	x24, x24, :lo12:.LANCHOR4
	adrp	x0, .LC115
	add	x0, x0, :lo12:.LC115
	mov	w2, w21
	add	x23, x23, :lo12:.LANCHOR2
	ldr	w1, [x24,164]
	bl	printk
	mov	w0, 1
	str	w0, [x23,-96]
	b	.L2044
.L2048:
	cmp	w1, 1
	beq	.L2046
	cmp	w2, 256
	beq	.L2046
	mov	w0, 65535
	cmp	w27, w0
	beq	.L2044
	mov	w0, w27
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L2044:
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
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp,16]
	mov	x19, x0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	ldrb	w20, [x0,120]
	cbz	w20, .L2069
	ldrb	w20, [x19,8]
	cmp	w20, 1
	bne	.L2064
	ldrh	w22, [x19,4]
	b	.L2056
.L2064:
	mov	w20, 0
.L2069:
	mov	w22, 12
.L2056:
	adrp	x23, .LANCHOR2
	mov	w24, -1
.L2057:
	sub	w22, w22, #1
	cmn	w22, #1
	beq	.L2059
	ldrh	w0, [x19,4]
	cbnz	w0, .L2058
.L2059:
	adrp	x1, .LANCHOR2
	ldrh	w0, [x19]
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w3, [x19,4]
	lsl	x0, x0, 1
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x2, [x1,-40]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x21,2018]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	ldp	x21, x22, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 128
	ret
.L2058:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2059
	add	x2, x23, :lo12:.LANCHOR2
	str	w24, [x29,96]
	mov	w3, 0
	ldr	x1, [x2,2120]
	ldr	x0, [x2,-64]
	str	x0, [x29,80]
	str	x1, [x29,88]
	str	w24, [x1,8]
	str	w24, [x1,12]
	ldrh	w0, [x19]
	strh	w0, [x1,2]
	strh	wzr, [x1]
	ldr	w0, [x2,200]
	str	w0, [x1,4]
	mov	w1, 1
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x2,200]
	add	x0, x29, 72
	mov	w2, w20
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2057
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	adrp	x22, .LANCHOR0
	ldr	x21, [x0,16]
	ldr	x23, [x0,40]
	bl	ftl_free_no_use_map_blk
	ldrh	w4, [x19,10]
	ldrh	w2, [x19,8]
	sub	w1, w4, #4
	cmp	w2, w1
	bge	.L2071
	ldrh	w2, [x19,60]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2073
	add	x1, x22, :lo12:.LANCHOR0
	ldrh	w2, [x19,2]
	ldrh	w1, [x1,2020]
	cmp	w2, w1
	bcc	.L2073
.L2071:
	ldrh	w1, [x19,60]
	uxth	w20, w0
	mov	w0, 65535
	cmp	w1, w0
	beq	.L2075
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w3, [x19,2]
	ldrh	w0, [x0,2020]
	cmp	w0, w3
	bhi	.L2075
	mov	x2, 0
.L2076:
	uxth	w0, w2
	cmp	w0, w4
	bcs	.L2077
	add	x2, x2, 1
	add	x5, x21, x2, lsl 1
	ldrh	w5, [x5,-2]
	cmp	w5, w1
	bne	.L2076
	mov	w20, w0
.L2077:
	ubfiz	x2, x0, 1, 16
	adrp	x0, .LC116
	add	x0, x0, :lo12:.LC116
	ldrh	w2, [x21,x2]
	bl	printk
	mov	w0, -1
	strh	w0, [x19,60]
.L2075:
	ubfiz	x20, x20, 1, 16
	ldrh	w24, [x21,x20]
	cbz	w24, .L2073
	ldr	w0, [x19,52]
	cbnz	w0, .L2073
	mov	w1, 1
	str	w1, [x19,52]
	strh	w0, [x21,x20]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	cmp	w1, w0
	bcc	.L2079
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2079:
	adrp	x20, .LANCHOR4
	adrp	x25, .LC117
	add	x20, x20, :lo12:.LANCHOR4
	mov	w21, 0
	add	x20, x20, 160
	adrp	x27, .LANCHOR2
	add	x25, x25, :lo12:.LC117
.L2080:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L2090
	ubfiz	x28, x21, 2, 16
	ldr	w0, [x23,x28]
	cmp	w24, w0, lsr 10
	bne	.L2081
	add	x26, x27, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x3, [x26,2120]
	ldr	x0, [x26,2080]
	str	x3, [x20,16]
	str	x0, [x20,8]
	str	x3, [x29,104]
	ldr	w0, [x23,x28]
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	ldr	x3, [x29,104]
	cmn	w0, #1
	bne	.L2082
	str	wzr, [x23,x28]
	mov	x0, x25
	ldr	w1, [x20,4]
	ldrh	w2, [x3,8]
	bl	printk
	mov	w0, 1
	str	w0, [x26,-96]
	b	.L2081
.L2082:
	ldr	x2, [x20,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L2081:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2080
.L2090:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L2073:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x22,2020]
	cmp	w1, w0
	bcc	.L2084
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2084:
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
	adrp	x1, .LANCHOR2
	add	x29, sp, 0
	add	x3, x1, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x23, x1
	ldr	w20, [x3,-96]
	ldr	x21, [x0,16]
	ldr	x24, [x0,40]
	cbnz	w20, .L2092
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2093
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
	b	.L2092
.L2093:
	ubfiz	x0, x0, 1, 16
	ldrh	w22, [x21,x0]
	adrp	x21, .LANCHOR4
	ldrh	w0, [x19,2]
	add	x1, x21, :lo12:.LANCHOR4
	orr	w0, w0, w22, lsl 10
	str	w0, [x1,164]
	ldr	x0, [x3,-64]
	str	x0, [x1,168]
	ldr	x0, [x3,2120]
	str	x0, [x1,176]
	ldr	w1, [x19,48]
	str	w1, [x0,4]
	mov	w1, -1291
	strh	w1, [x0,8]
	ldrh	w1, [x19,4]
	strh	w1, [x0]
	mov	w1, 255
	strh	w22, [x0,2]
	adrp	x0, .LANCHOR0+2020
	ldrh	w2, [x0,#:lo12:.LANCHOR0+2020]
	ldr	x0, [x3,-64]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w1, w20
.L2094:
	ldrh	w0, [x19,6]
	cmp	w0, w1
	bls	.L2097
	ubfiz	x3, x1, 2, 32
	ldr	w0, [x24,x3]
	cmp	w22, w0, lsr 10
	bne	.L2095
	add	x2, x23, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x4, [x2,-64]
	ubfiz	x0, x20, 3, 16
	str	w1, [x4,x0]
	ldr	x2, [x2,-64]
	ldr	w3, [x24,x3]
	add	x0, x2, x0
	str	w3, [x0,4]
.L2095:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2094
.L2097:
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
.L2092:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	mov	x19, x0
	add	x0, x22, :lo12:.LANCHOR2
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	mov	w25, w1
	mov	x27, x2
	ldr	w21, [x0,-96]
	cbnz	w21, .L2099
	adrp	x20, .LANCHOR4
	adrp	x26, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x24, .LC118
	add	x0, x24, :lo12:.LC118
	mov	x28, x26
	add	x20, x20, 160
	str	x0, [x29,104]
.L2100:
	add	x1, x22, :lo12:.LANCHOR2
	ldr	w0, [x1,180]
	add	w0, w0, 1
	str	w0, [x1,180]
	add	x0, x26, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L2101
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2102
.L2101:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L2102:
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19,16]
	adrp	x23, .LANCHOR4
	ldrh	w24, [x0,x1,lsl 1]
	mov	w1, 0
	ldrh	w0, [x19,2]
	str	x27, [x20,8]
	orr	w0, w0, w24, lsl 10
	str	w0, [x20,4]
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x0, [x0,2120]
	str	x0, [x20,16]
	bl	ftl_memset
	ldr	x0, [x20,16]
	ldr	w1, [x19,48]
	str	w1, [x0,4]
	strh	w25, [x0,8]
	ldrh	w1, [x19,4]
	strh	w1, [x0]
	mov	w1, 1
	strh	w24, [x0,2]
	mov	w2, w1
	mov	w3, w1
	mov	x0, x20
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	uxth	w0, w0
	strh	w0, [x19,2]
	ldr	w1, [x20]
	cmn	w1, #1
	bne	.L2103
	ldr	x0, [x29,104]
	add	w21, w21, 1
	ldr	w1, [x20,4]
	uxth	w21, w21
	bl	printk
	ldrh	w0, [x19,2]
	cmp	w0, 2
	bhi	.L2104
	add	x0, x28, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	strh	w0, [x19,2]
.L2104:
	cmp	w21, 3
	bls	.L2100
	add	x23, x23, :lo12:.LANCHOR4
	adrp	x0, .LC119
	add	x0, x0, :lo12:.LC119
	mov	w2, w21
	add	x22, x22, :lo12:.LANCHOR2
	ldr	w1, [x23,164]
	bl	printk
	mov	w0, 1
	str	w0, [x22,-96]
	b	.L2099
.L2103:
	cbz	w1, .L2106
	strh	w24, [x19,60]
.L2106:
	cmp	w0, 1
	beq	.L2100
	cmp	w1, 256
	beq	.L2100
	add	x23, x23, :lo12:.LANCHOR4
	ldr	x0, [x19,40]
	ldr	w1, [x23,164]
	str	w1, [x0,w25,uxtw 2]
.L2099:
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
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -64]!
	adrp	x2, .LANCHOR2
	uxth	x1, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	add	x20, x2, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	uxth	w23, w0
	ubfiz	x0, x23, 2, 16
	ldr	x3, [x20,2216]
	stp	x21, x22, [sp,32]
	mov	x22, x2
	ldr	w21, [x3,x0]
	cbnz	w21, .L2111
	ldr	x0, [x20,144]
	lsl	x19, x1, 4
	adrp	x2, .LANCHOR0+2028
	mov	w1, 255
	add	x0, x0, x19
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2028]
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x20,144]
	strh	w23, [x0,x19]
	ldr	x1, [x20,144]
	add	x19, x1, x19
	str	w21, [x19,4]
	b	.L2112
.L2111:
	lsl	x19, x1, 4
	ldr	x1, [x20,144]
	adrp	x24, .LANCHOR4
	add	x24, x24, :lo12:.LANCHOR4
	add	x1, x1, x19
	add	x0, x24, 160
	ldr	x1, [x1,8]
	str	x1, [x0,8]
	ldr	x1, [x20,2120]
	str	x1, [x0,16]
	mov	w1, 1
	mov	w2, w1
	str	w21, [x0,4]
	bl	FlashReadPages
	ldr	w0, [x24,160]
	cmp	w0, 256
	bne	.L2113
	adrp	x0, .LC120
	mov	w2, w21
	mov	w1, w23
	add	x0, x0, :lo12:.LC120
	lsr	w21, w21, 10
	bl	printk
	strh	w21, [x20,2316]
	add	x0, x20, 2256
	ldr	x2, [x20,144]
	mov	w1, w23
	add	x2, x2, x19
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L2113:
	add	x2, x22, :lo12:.LANCHOR2
	ldr	x0, [x2,144]
	add	x0, x0, x19
	str	wzr, [x0,4]
	ldr	x0, [x2,144]
	strh	w23, [x0,x19]
.L2112:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	load_l2p_region, .-load_l2p_region
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
	ldr	x1, [x20,144]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20,144]
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
	.global	log2phys
	.type	log2phys, %function
log2phys:
	stp	x29, x30, [sp, -96]!
	adrp	x5, .LANCHOR0
	mov	x4, 1
	add	x5, x5, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	str	x25, [sp,64]
	adrp	x23, .LANCHOR2
	mov	x21, x1
	add	x1, x23, :lo12:.LANCHOR2
	ldrh	w3, [x5,2026]
	mov	w22, w2
	add	w3, w3, 7
	ldr	x25, [x1,144]
	lsr	w20, w0, w3
	lsl	x3, x4, x3
	sub	w3, w3, #1
	mov	x4, 0
	and	w0, w3, w0
	uxth	w20, w20
	uxth	x24, w0
	ldrh	w0, [x5,2058]
.L2116:
	uxth	x19, w4
	cmp	w19, w0
	bcs	.L2128
	add	x4, x4, 1
	add	x1, x25, x4, lsl 4
	ldrh	w1, [x1,-16]
	cmp	w1, w20
	bne	.L2116
.L2117:
	cbnz	w22, .L2118
	add	x0, x23, :lo12:.LANCHOR2
	ldr	x1, [x0,144]
	add	x1, x1, x19, lsl 4
	ldr	x0, [x1,8]
	ldr	w0, [x0,x24,lsl 2]
	str	w0, [x21]
	b	.L2119
.L2118:
	add	x1, x23, :lo12:.LANCHOR2
	lsl	x0, x19, 4
	ldr	w3, [x21]
	ldr	x2, [x1,144]
	add	x2, x2, x0
	ldr	x2, [x2,8]
	str	w3, [x2,x24,lsl 2]
	strh	w20, [x1,152]
	ldr	x2, [x1,144]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2119:
	add	x23, x23, :lo12:.LANCHOR2
	ldr	x0, [x23,144]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19,4]
	cmn	w0, #1
	beq	.L2125
	add	w0, w0, 1
	str	w0, [x19,4]
	b	.L2125
.L2128:
	bl	select_l2p_ram_region
	uxth	x19, w0
	ubfiz	x2, x19, 4, 16
	mov	w1, w0
	add	x3, x25, x2
	ldrh	w4, [x25,x2]
	mov	w2, 65535
	cmp	w4, w2
	beq	.L2122
	ldr	w2, [x3,4]
	tbz	w2, #31, .L2122
	str	x1, [x29,88]
	bl	flush_l2p_region
	ldr	x1, [x29,88]
.L2122:
	mov	w0, w20
	bl	load_l2p_region
	b	.L2117
.L2125:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 96
	ret
	.size	log2phys, .-log2phys
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
	ldr	x3, [x2,-40]
	ldrh	w1, [x3,x22]
	cbnz	w1, .L2130
	ldr	x19, [x2,-16]
	cbz	x19, .L2131
	ldrh	w3, [x2,-8]
	mov	x4, -6148914691236517206
	ldr	x2, [x2,-56]
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L2132:
	cmp	w1, w3
	beq	.L2131
	cmp	w19, w0
	bne	.L2133
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w19
	sub	x0, x21, #16
	bl	List_remove_node
	ldrh	w0, [x21,-8]
	sub	w0, w0, #1
	strh	w0, [x21,-8]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x1, [x21,-40]
	ldrh	w0, [x1,x22]
	add	w0, w0, 1
	strh	w0, [x1,x22]
	b	.L2131
.L2133:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2131
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2132
.L2130:
	add	w1, w1, 1
	strh	w1, [x3,x22]
.L2131:
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
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	stp	x29, x30, [sp, -144]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x27, x0
	adrp	x19, .LANCHOR2
	ldr	x0, [x0,40]
	adrp	x20, .LANCHOR4
	str	x0, [x29,136]
	adrp	x25, .LANCHOR0
	ldr	x0, [x27,24]
	mov	w24, 0
	str	x0, [x29,112]
	mov	x26, x25
	ldrh	w0, [x27,8]
	ldrh	w23, [x27,6]
	str	w0, [x29,132]
	ldr	x0, [x29,136]
	lsl	w2, w23, 2
	ldr	x21, [x27,16]
	bl	ftl_memset
	str	x20, [x29,120]
	add	x0, x19, :lo12:.LANCHOR2
	add	x4, x20, :lo12:.LANCHOR4
	add	x4, x4, 160
	str	wzr, [x27,52]
	str	wzr, [x27,48]
	ldr	x1, [x0,-64]
	ldr	x22, [x0,2120]
	mov	w0, -1
	str	x1, [x4,8]
	str	x22, [x4,16]
	mov	x20, x4
	strh	w0, [x27]
	strh	w0, [x27,2]
	mov	w0, 1
	str	w0, [x27,56]
	ldr	w0, [x29,132]
	sub	w0, w0, #1
	str	w0, [x29,128]
.L2139:
	ldr	w0, [x29,132]
	cmp	w24, w0
	bge	.L2156
	ldr	w0, [x29,128]
	sxtw	x28, w24
	cmp	w24, w0
	bne	.L2140
	lsl	x0, x28, 1
	mov	w1, 1
	add	x25, x21, x0
	ldrh	w0, [x21,x0]
	bl	FtlGetLastWrittenPage
	sxth	w1, w0
	add	w0, w0, 1
	strh	w0, [x27,2]
	ldr	x0, [x29,112]
	add	w20, w1, 1
	strh	w24, [x27]
	mov	w24, 0
	ldr	w0, [x0,x28,lsl 2]
	str	w0, [x27,48]
	ldr	x0, [x29,120]
	add	x26, x0, :lo12:.LANCHOR4
	add	x26, x26, 160
.L2141:
	cmp	w24, w20
	bge	.L2156
	ldrh	w0, [x25]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w24, w0, lsl 10
	str	w0, [x26,4]
	mov	x0, x26
	bl	FlashReadPages
	ldr	w0, [x26]
	cmn	w0, #1
	beq	.L2142
	ldrh	w0, [x22,8]
	cmp	w0, w23
	bcs	.L2143
	ldrh	w2, [x22]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2143
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x26,4]
	str	w1, [x2,x0]
	b	.L2143
.L2142:
	ldrh	w0, [x25]
	strh	w0, [x27,60]
.L2143:
	add	w24, w24, 1
	sxth	w24, w24
	b	.L2141
.L2156:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR0
	add	x0, x19, 2256
	cmp	x27, x0
	bne	.L2146
	b	.L2181
.L2140:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,-64]
	str	x0, [x20,8]
	lsl	x0, x28, 1
	add	x28, x21, x0
	ldrh	w1, [x21,x0]
	add	x0, x25, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x20,4]
	mov	w2, w1
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L2169
	ldrh	w1, [x22]
	mov	w3, 0
	ldrh	w0, [x27,4]
	cmp	w1, w0
	bne	.L2178
	ldrh	w1, [x22,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2178
.L2148:
	add	x0, x26, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	cmp	w3, w0
	bge	.L2151
	add	x0, x19, :lo12:.LANCHOR2
	sbfiz	x1, x3, 3, 32
	ldr	x2, [x0,-64]
	ldrh	w0, [x2,x1]
	cmp	w0, w23
	bcs	.L2149
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x1,4]
	str	w1, [x2,x0]
.L2149:
	add	w3, w3, 1
	sxth	w3, w3
	b	.L2148
.L2169:
	mov	w3, 0
.L2178:
	add	x0, x26, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	cmp	w3, w0
	bge	.L2151
	ldrh	w0, [x28]
	mov	w1, 1
	mov	w2, w1
	str	x3, [x29,104]
	orr	w0, w3, w0, lsl 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	ldr	x3, [x29,104]
	cmn	w0, #1
	beq	.L2152
	ldrh	w0, [x22,8]
	cmp	w0, w23
	bcs	.L2152
	ldrh	w2, [x22]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2152
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,136]
	ldr	w1, [x20,4]
	str	w1, [x2,x0]
.L2152:
	add	w3, w3, 1
	sxth	w3, w3
	b	.L2178
.L2151:
	add	w6, w24, 1
	sxth	w24, w6
	b	.L2139
.L2167:
	ubfiz	x24, x23, 2, 32
	ldr	x0, [x19,2224]
	ldr	w1, [x0,x24]
	sub	w2, w1, #1
	cmn	w2, #3
	bls	.L2182
.L2161:
	add	w23, w23, 1
.L2159:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2056]
	cmp	w23, w0
	bcc	.L2167
.L2146:
	mov	x0, x27
	add	x22, x22, :lo12:.LANCHOR0
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x27,2]
	ldrh	w0, [x22,2020]
	cmp	w1, w0
	bne	.L2158
	mov	x0, x27
	bl	ftl_map_blk_alloc_new_blk
	b	.L2158
.L2181:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2064]
	cbz	w0, .L2146
	ldr	x0, [x29,120]
	adrp	x25, .LC121
	mov	w23, 0
	add	x25, x25, :lo12:.LC121
	add	x20, x0, :lo12:.LANCHOR4
	add	x20, x20, 160
	b	.L2159
.L2182:
	ldr	x2, [x19,2216]
	ldr	w2, [x2,x24]
	cmp	w1, w2
	beq	.L2161
	ldr	x1, [x19,-64]
	ldr	x26, [x19,2120]
	str	x1, [x20,8]
	mov	w1, 1
	str	x26, [x20,16]
	mov	w2, w1
	ldr	w0, [x0,x24]
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	mov	w3, 0
	cmn	w0, #1
	beq	.L2163
	ldrh	w1, [x26]
	ldrh	w0, [x19,2260]
	cmp	w1, w0
	bne	.L2163
	ldr	w3, [x26,4]
.L2163:
	ldr	x0, [x19,2216]
	mov	w1, 1
	mov	w2, w1
	str	x3, [x29,136]
	ldr	w0, [x0,x24]
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w4, [x26,4]
	ldr	x3, [x29,136]
	cmp	w3, w4
	bls	.L2161
	ldr	x1, [x19,2224]
	mov	x0, x25
	ldr	x2, [x19,2216]
	ldr	w1, [x1,x24]
	ldr	w2, [x2,x24]
	bl	printk
	ldr	x0, [x19,2224]
	ldr	w1, [x0,x24]
	ldr	x0, [x19,2216]
	str	w1, [x0,x24]
	mov	w1, 0
	ldr	x0, [x19,2224]
	ldrh	w2, [x19,2320]
	ldr	w0, [x0,x24]
	ubfx	x0, x0, 10, 16
.L2164:
	cmp	w1, w2
	bge	.L2183
	ldrh	w3, [x21,w1,sxtw 1]
	cmp	w3, w0
	beq	.L2161
	add	w1, w1, 1
	sxth	w1, w1
	b	.L2164
.L2183:
	add	x1, x22, :lo12:.LANCHOR0
	ldr	w1, [x1,2048]
	cmp	w2, w1
	bcs	.L2161
	ubfiz	x2, x2, 1, 16
	strh	w0, [x21,x2]
	ldrh	w1, [x19,2320]
	add	w1, w1, 1
	strh	w1, [x19,2320]
	bl	remove_from_free_sys_Queue
	b	.L2161
.L2158:
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
	adrp	x1, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR4
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x0, x0, 224
	add	x29, sp, 0
	ldrh	w2, [x1,2040]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,2066]
	strh	w2, [x0,8]
	ldrh	w2, [x1,2042]
	ldr	x1, [x1,2072]
	str	x1, [x0,16]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	strh	w2, [x0,6]
	ldr	x2, [x1,2200]
	str	x2, [x0,24]
	ldr	x2, [x1,2192]
	ldr	x1, [x1,2208]
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
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LANCHOR2
	add	x0, x1, :lo12:.LANCHOR2
	add	x29, sp, 0
	add	x2, x0, 352
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR0
	ldr	w3, [x2,80]
	cbz	w3, .L2187
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w3, [x2,84]
	ldr	w1, [x1,2096]
	cmp	w3, w1
	bcs	.L2188
	mov	w21, 2048
	mov	x19, x0
.L2193:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x19,436]
	ldr	w1, [x1,2096]
	cmp	w0, w1
	bcs	.L2190
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x19,436]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x19,436]
	cmn	w1, #1
	beq	.L2191
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
	bne	.L2190
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2190
.L2191:
	subs	w21, w21, #1
	bne	.L2193
.L2190:
	mov	w0, -1
	b	.L2195
.L2188:
	ldr	w0, [x0,164]
	str	wzr, [x2,80]
	str	wzr, [x2,84]
	str	w0, [x2,76]
	b	.L2201
.L2187:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	w3, [x0,216]
	mov	w2, 4194304
	ldr	w0, [x0,160]
	ldrb	w4, [x20,120]
	cbnz	w4, .L2196
	add	w0, w0, w3, lsr 10
	mov	w3, 33554432
	asr	w2, w3, w0
.L2196:
	add	x4, x1, :lo12:.LANCHOR2
	ldr	w3, [x4,164]
	ldr	w0, [x4,428]
	add	w5, w3, 1048576
	cmp	w0, w5
	bhi	.L2197
	add	w0, w2, w0
	cmp	w0, w3
	bcc	.L2197
	ldrb	w2, [x4,252]
	mov	w0, 0
	cbnz	w2, .L2195
.L2197:
	add	x0, x1, :lo12:.LANCHOR2
	mov	w1, 1
	str	wzr, [x0,436]
	str	w1, [x0,432]
	str	w3, [x0,428]
.L2201:
	mov	w0, 0
.L2195:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	ldrh	w21, [x0,2026]
	mov	x25, x2
	ldrh	w0, [x0,2012]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L2203
	adrp	x24, .LANCHOR4
	lsr	w21, w26, w21
	add	x24, x24, :lo12:.LANCHOR4
	mov	w20, 0
	adrp	x27, .LANCHOR2
	add	x24, x24, 224
.L2204:
	cbz	w28, .L2203
	adrp	x0, .LANCHOR2
	add	x2, x0, :lo12:.LANCHOR2
	ldr	x0, [x2,2208]
	ldr	w4, [x0,w21,uxtw 2]
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2024]
	uxth	w0, w28
	udiv	w22, w26, w1
	msub	w22, w22, w1, w26
	sub	w19, w1, w22
	uxth	w19, w19
	cmp	w19, w28
	csel	w19, w0, w19, hi
	cmp	w19, w1
	beq	.L2206
	cbz	w4, .L2206
	ldr	x0, [x2,2088]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w4, [x29,124]
	str	xzr, [x29,136]
	bl	FlashReadPages
	b	.L2207
.L2206:
	add	x0, x27, :lo12:.LANCHOR2
	add	x2, x23, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x0,2088]
	ldrh	w2, [x2,2028]
	bl	ftl_memset
.L2207:
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
	b	.L2204
.L2203:
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
	add	x2, x0, 352
	add	x29, sp, 0
	ldr	w3, [x0,352]
	cmp	w3, w1
	bne	.L2217
	mov	w1, 71
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	ldr	w1, [x0,188]
	str	w1, [x2,88]
	ldr	w1, [x0,192]
	str	w1, [x2,92]
	ldr	w1, [x0,184]
	str	w1, [x2,8]
	ldr	w1, [x0,172]
	str	w1, [x2,12]
	ldr	w1, [x0,164]
	str	w1, [x2,16]
	ldr	w1, [x0,180]
	str	w1, [x2,20]
	ldr	w1, [x0,208]
	str	w1, [x2,28]
	ldr	w1, [x0,-72]
	str	w1, [x2,32]
	ldr	w1, [x0,168]
	str	w1, [x2,36]
	ldr	w1, [x0,176]
	str	w1, [x2,40]
	ldr	w1, [x0,216]
	str	w1, [x2,44]
	ldr	w1, [x0,220]
	str	w1, [x2,48]
	ldr	w1, [x0,344]
	ldr	w0, [x0,2028]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2217:
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
	ldr	w4, [x3,160]
	cbz	w4, .L2220
	ldr	w3, [x3,220]
	cmp	w3, 29
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2220:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,288]
	cmp	w4, 31
	bhi	.L2221
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,288]
.L2221:
	cbnz	w0, .L2222
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,2160]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2223
.L2222:
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
.L2223:
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
	ldrh	w2, [x0,2012]
	str	x1, [x29,136]
	cmp	w3, w2
	ldrh	w20, [x0,2026]
	mov	w0, -1
	bhi	.L2228
	adrp	x27, .LANCHOR4
	adrp	x0, .LC122
	add	x0, x0, :lo12:.LC122
	add	x27, x27, :lo12:.LANCHOR4
	str	x0, [x29,128]
	lsr	w20, w23, w20
	add	x0, x27, 224
	mov	w21, 0
	str	x0, [x29,120]
.L2229:
	cbz	w22, .L2237
	adrp	x25, .LANCHOR2
	add	x4, x25, :lo12:.LANCHOR2
	ldr	x0, [x4,2208]
	ldr	w3, [x0,w20,uxtw 2]
	ldr	x0, [x29,136]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w19, [x0,2024]
	uxth	w0, w22
	udiv	w24, w23, w19
	msub	w24, w24, w19, w23
	sub	w19, w19, w24
	uxth	w19, w19
	cmp	w19, w22
	csel	w19, w0, w19, hi
	lsl	w28, w19, 9
	cbz	w3, .L2231
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
	bne	.L2233
	ldr	x0, [x29,128]
	mov	w1, w20
	mov	w2, w3
	bl	printk
	ldr	x4, [x29,112]
	mov	w1, w20
	ldr	x0, [x29,120]
	ldr	x2, [x4,2088]
	bl	FtlMapWritePage
.L2233:
	add	x25, x25, :lo12:.LANCHOR2
	ubfiz	x24, x24, 9, 16
	mov	x0, x26
	mov	w2, w28
	ldr	x1, [x25,2088]
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L2234
.L2231:
	mov	x0, x26
	mov	w1, w3
	mov	w2, w28
	bl	ftl_memset
.L2234:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x26, x26, x28, sxtw
	b	.L2229
.L2237:
	mov	w0, w21
.L2228:
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
	beq	.L2239
	adrp	x0, .LC73
	adrp	x1, .LC123
	add	x1, x1, :lo12:.LC123
	add	x0, x0, :lo12:.LC73
	bl	printk
	ldrh	w2, [x19,2144]
	mov	w1, 0
	ldr	x0, [x19,2160]
	lsl	w2, w2, 9
	bl	ftl_memset
.L2239:
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
	add	x21, x20, 352
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,352]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L2241
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,352]
.L2241:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,352]
	cmp	w3, w2
	bne	.L2242
	ldr	w2, [x0,440]
	str	w2, [x0,188]
	ldr	w2, [x0,444]
	str	w2, [x0,192]
	ldr	w2, [x0,360]
	str	w2, [x0,184]
	ldr	w2, [x0,364]
	str	w2, [x0,172]
	ldr	w2, [x0,368]
	str	w2, [x0,164]
	ldr	w2, [x0,372]
	str	w2, [x0,180]
	ldr	w2, [x0,380]
	str	w2, [x0,208]
	ldr	w2, [x0,384]
	str	w2, [x0,-72]
	ldr	w2, [x0,388]
	str	w2, [x0,168]
	ldr	w2, [x0,392]
	str	w2, [x0,176]
	ldr	w2, [x0,396]
	str	w2, [x0,216]
	ldr	w1, [x0,412]
	ldr	w2, [x0,400]
	str	w2, [x0,220]
	str	w1, [x0,344]
.L2242:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 34661
	movk	w1, 0x1234, lsl 16
	ldr	w2, [x0,420]
	str	wzr, [x0,2028]
	cmp	w2, w1
	bne	.L2243
	mov	w1, 1
	str	w1, [x0,160]
	adrp	x0, .LC73
	adrp	x1, .LC124
	add	x0, x0, :lo12:.LC73
	add	x1, x1, :lo12:.LC124
	bl	printk
.L2243:
	adrp	x2, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR2
	add	x2, x2, :lo12:.LANCHOR0
	ldr	x21, [sp,32]
	ldr	w3, [x19,204]
	ldrh	w1, [x2,2008]
	ldr	w0, [x19,208]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,1952]
	udiv	w0, w0, w1
	str	w0, [x19,212]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	StorageSysDataLoad
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	stp	x29, x30, [sp, -48]!
	mov	x2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	mov	x19, x1
	mov	x0, x1
	mov	w1, 0
	bl	memset
	bl	rknand_device_lock
	mov	x2, x19
	mov	w1, 1
	add	w0, w20, 512
	bl	FtlVendorPartRead
	str	x0, [x29,40]
	bl	rknand_device_unlock
	ldr	x0, [x29,40]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	StorageSysDataLoad, .-StorageSysDataLoad
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
	cbz	w1, .L2245
	adrp	x3, .LANCHOR2
	str	wzr, [x0,56]
	add	x3, x3, :lo12:.LANCHOR2
	ldr	w1, [x3,-96]
	cbnz	w1, .L2245
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
	cbz	w3, .L2249
	mov	x0, x1
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L2250
.L2249:
	adrp	x2, .LANCHOR0+2028
	mov	w1, 255
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2028]
	bl	ftl_memset
.L2250:
	add	x2, x19, :lo12:.LANCHOR4
	mov	x0, x21
	mov	w1, w20
	ldr	x2, [x2,168]
	bl	FtlMapWritePage
.L2245:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
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
	beq	.L2409
	ldrh	w0, [x19,2]
	adrp	x23, .LANCHOR0
	str	w0, [x29,188]
	ldrb	w0, [x19,6]
	str	w0, [x29,176]
	add	x0, x23, :lo12:.LANCHOR0
	ldr	w1, [x29,188]
	ldrh	w0, [x0,2018]
	cmp	w0, w1
	bne	.L2254
	strh	wzr, [x19,4]
	b	.L2415
.L2254:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2255:
	cmp	w0, w2
	bne	.L2418
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2255
.L2418:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2257
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2258
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, w21
	ldrb	w1, [x0,120]
	cbnz	w1, .L2332
	add	x0, x0, 196
	ldrh	w24, [x0,w21,sxtw 1]
	b	.L2332
.L2257:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2258
	mov	w24, w0
.L2332:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w26, 0
	mov	w8, 65535
	mov	w9, 56
	adrp	x10, .LANCHOR2
	ldrh	w4, [x0,1944]
	mov	w11, 4
	ldrh	w7, [x0,2030]
	add	x4, x19, x4, lsl 1
	b	.L2260
.L2258:
	strh	wzr, [x19,2]
.L2415:
	strb	wzr, [x19,6]
	b	.L2409
.L2262:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2261
	add	x5, x10, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x0, w26, w9
	add	w3, w26, 1
	ldr	x6, [x5,2056]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x5,2056]
	ldr	x5, [x5,904]
	add	x0, x1, x0
	mul	w1, w26, w7
	uxth	w26, w3
	str	xzr, [x0,8]
	sdiv	w1, w1, w11
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L2261:
	add	x2, x2, 2
.L2260:
	cmp	x2, x4
	bne	.L2262
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2263
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2263:
	adrp	x20, .LANCHOR2
	ldr	w2, [x29,184]
	add	x22, x20, :lo12:.LANCHOR2
	mov	w1, w26
	mov	x27, 0
	mov	w25, 65535
	mov	x28, 56
	ldr	x0, [x22,2056]
	bl	FlashReadPages
	ldr	w0, [x22,200]
	adrp	x22, .LC125
	str	w0, [x29,168]
	add	x22, x22, :lo12:.LC125
	uxth	w0, w24
	str	w0, [x29,160]
.L2264:
	uxth	w2, w27
	cmp	w2, w26
	bcs	.L2271
	add	x6, x20, :lo12:.LANCHOR2
	mul	x0, x27, x28
	ldr	x4, [x6,2056]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2265
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2266
	ldr	w1, [x6,200]
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
	cbz	w0, .L2266
	add	w4, w4, 1
	str	w4, [x6,200]
.L2266:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2267
.L2271:
	cmp	w2, w26
	bne	.L2410
	add	x0, x20, :lo12:.LANCHOR2
	add	w21, w21, 1
	uxth	w21, w21
	ldr	x0, [x0,2056]
	ldr	w0, [x0,4]
	b	.L2411
.L2265:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w25, [x29,160]
	adrp	x0, .LANCHOR4
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LANCHOR4
	strh	w1, [x0,290]
.L2267:
	add	x27, x27, 1
	b	.L2264
.L2410:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w21, w21
	umull	x2, w2, w1
	ldr	x0, [x0,2056]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2411:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2273
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w2, [x0,120]
	cbnz	w2, .L2273
	add	x0, x0, 196
	ldrh	w21, [x0,w21,sxtw 1]
.L2273:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w0, [x0,2018]
	cmp	w0, w21
	bne	.L2274
	strh	w21, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2274:
	ldrh	w0, [x29,176]
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w21, w0
	bne	.L2275
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2416
.L2275:
	ldr	w0, [x29,168]
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w25, w0
	bne	.L2276
	cbnz	w1, .L2277
.L2276:
	add	x0, x20, :lo12:.LANCHOR2
	uxth	w5, w24
	ldr	w1, [x0,2348]
	cmn	w1, #1
	bne	.L2278
	str	w22, [x0,2348]
.L2278:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w25, [x29,188]
	ldr	w6, [x0,2348]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w24, uxth
	bge	.L2279
	sub	w25, w5, #7
	uxth	w25, w25
.L2279:
	mov	w3, -1
	add	x28, x20, :lo12:.LANCHOR2
	mov	w27, w3
	mov	w4, 65535
	mov	w7, 56
	add	x8, x28, 996
.L2280:
	cmp	w25, w5
	bhi	.L2293
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, 0
	ldrh	w2, [x0,1944]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2294:
	cmp	x0, x2
	beq	.L2419
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2281
	umull	x9, w24, w7
	ldr	x10, [x28,2056]
	add	w24, w24, 1
	orr	w1, w25, w1, lsl 10
	add	x9, x10, x9
	uxth	w24, w24
	str	w1, [x9,4]
.L2281:
	add	x0, x0, 2
	b	.L2294
.L2419:
	ldr	x0, [x28,2056]
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
	ldrb	w9, [x0,120]
	ldr	x0, [x28,2056]
	ldr	x4, [x29,144]
	ldr	x7, [x29,136]
	add	x0, x0, 16
	ldr	x8, [x29,128]
.L2283:
	cmp	w1, w24
	beq	.L2420
	ldr	w2, [x0,-16]
	cbnz	w2, .L2284
	ldr	x2, [x0]
	ldrh	w11, [x2]
	cmp	w11, w4
	beq	.L2285
	ldr	w2, [x2,4]
	cmn	w2, #1
	beq	.L2285
	ldr	w27, [x28,2348]
	cmn	w3, #1
	str	w2, [x28,2348]
	bne	.L2285
	ldrh	w2, [x8,x10,lsl 1]
	cmp	w2, w4
	bne	.L2286
	cbz	w9, .L2285
.L2286:
	cmp	w27, w22
	csinv	w3, w27, wzr, ne
	b	.L2285
.L2284:
	adrp	x0, .LANCHOR4+290
	ldrh	w1, [x19]
	strh	w1, [x0,#:lo12:.LANCHOR4+290]
	ldrb	w0, [x19,8]
	cbnz	w0, .L2277
	add	x0, x20, :lo12:.LANCHOR2
	add	x1, x0, 996
	ldrh	w2, [x1,w25,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2288
	cmn	w3, #1
	beq	.L2289
	str	w3, [x0,2348]
	b	.L2277
.L2289:
	cmp	w6, w22
	beq	.L2290
	str	w6, [x0,2348]
	b	.L2277
.L2290:
	ldr	w1, [x0,2348]
	b	.L2417
.L2288:
	cmp	w27, w22
	beq	.L2291
	cmn	w27, #1
	beq	.L2277
	str	w27, [x0,2348]
	b	.L2277
.L2291:
	ldr	w1, [x0,2348]
	cmp	w1, w22
	beq	.L2277
.L2417:
	sub	w1, w1, #1
	b	.L2412
.L2285:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2283
.L2420:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2280
.L2293:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, -1
.L2412:
	str	w1, [x0,2348]
.L2277:
	adrp	x25, .LANCHOR4
	mov	w1, 1
	add	x0, x25, :lo12:.LANCHOR4
	strh	w1, [x0,292]
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 2256
	bl	FtlMapBlkWriteDumpData
	ldr	w24, [x29,188]
	adrp	x0, .LC127
	str	x25, [x29,136]
	add	x0, x0, :lo12:.LC127
	str	x0, [x29,128]
	adrp	x0, .LC126
	add	x0, x0, :lo12:.LC126
	str	x0, [x29,120]
.L2295:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x1, x19
	str	wzr, [x29,176]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,1944]
	ldrb	w6, [x0,120]
	add	x3, x19, x3, lsl 1
.L2296:
	cmp	x1, x3
	beq	.L2421
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2297
	ldr	w0, [x29,176]
	add	x5, x20, :lo12:.LANCHOR2
	orr	w2, w24, w2, lsl 10
	ldr	x4, [x5,2056]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2298
	cbz	w6, .L2298
	ldr	x2, [x5,2056]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2298:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2297:
	add	x1, x1, 2
	b	.L2296
.L2421:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,2056]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2300:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2422
	ldr	x0, [x29,160]
	mov	x25, 56
	mul	x25, x0, x25
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x28, [x0,2056]
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
	bcc	.L2301
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2336
	ldr	w2, [x29,188]
	cmp	w24, w2
	beq	.L2301
.L2336:
	cmp	w0, w26
	bne	.L2337
	cmp	w24, w21
	beq	.L2303
.L2337:
	ldr	w0, [x28,x25]
	cmn	w0, #1
	beq	.L2305
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	bne	.L2312
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2307
	add	x27, x20, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,200]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2307
	add	w0, w22, 1
	str	w0, [x27,200]
.L2307:
	ldr	w27, [x28,8]
	add	x1, x29, 200
	ldr	w0, [x28,12]
	mov	w2, 0
	str	w0, [x29,196]
	mov	w0, w27
	bl	log2phys
	add	x3, x20, :lo12:.LANCHOR2
	str	x3, [x29,168]
	ldr	w1, [x3,2348]
	cmn	w1, #1
	beq	.L2308
	mov	w0, w22
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,168]
	cbz	w0, .L2308
	ldr	w1, [x29,196]
	cmn	w1, #1
	beq	.L2309
	ldr	x0, [x3,2056]
	str	x3, [x29,112]
	add	x0, x0, x25
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x2, [x0,16]
	ldr	x0, [x3,2056]
	str	x2, [x29,168]
	mov	w2, 0
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x3, [x29,112]
	ldr	x0, [x29,168]
	ldr	x2, [x3,2056]
	ldr	w0, [x0,4]
	str	w0, [x29,144]
	add	x4, x2, x25
	ldr	w0, [x2,x25]
	cmn	w0, #1
	bne	.L2310
	b	.L2311
.L2309:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2312
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
.L2312:
	ldrh	w0, [x19]
	b	.L2414
.L2310:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2311
	ldr	w0, [x3,2348]
	ldr	w1, [x29,144]
	str	x2, [x29,96]
	str	x4, [x29,104]
	str	x3, [x29,112]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	ldr	x2, [x29,96]
	cbz	w0, .L2311
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2317
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2311
	cmn	w0, #1
	beq	.L2315
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,2056]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	b	.L2316
.L2315:
	str	w0, [x2,x25]
.L2316:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2056]
	ldr	w1, [x1,x25]
	cmn	w1, #1
	beq	.L2317
	ldr	x1, [x29,168]
	ldr	w0, [x0,2348]
	ldr	w25, [x1,4]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2317
	ldr	w0, [x29,144]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2311
.L2317:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2311:
	mov	w0, -1
	str	w0, [x29,196]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2319
.L2308:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2319
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w25, [x29,200]
	cmn	w25, #1
	beq	.L2319
	ldr	w0, [x29,196]
	cmp	w25, w0
	beq	.L2319
	lsr	x0, x25, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	ldrh	w2, [x20,#:lo12:.LANCHOR2]
	add	x1, x20, :lo12:.LANCHOR2
	cmp	w2, w0
	beq	.L2322
	ldrh	w2, [x1,48]
	cmp	w2, w0
	beq	.L2322
	ldrh	w1, [x1,96]
	cmp	w1, w0
	bne	.L2319
.L2322:
	add	x28, x20, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, 0
	ldr	x0, [x28,2056]
	str	w25, [x0,4]
	ldr	x3, [x0,16]
	ldr	x0, [x28,2056]
	str	x3, [x29,168]
	bl	FlashReadPages
	ldr	x0, [x28,2056]
	ldr	x3, [x29,168]
	ldr	w0, [x0]
	ldr	w1, [x3,4]
	cmn	w0, #1
	beq	.L2319
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2319
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2319:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2301
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,-40]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2323
.L2414:
	bl	decrement_vpc_count
	b	.L2301
.L2323:
	ldr	x0, [x29,120]
	bl	printk
	b	.L2301
.L2305:
	ldr	x2, [x29,136]
	ldrh	w0, [x19]
	add	x25, x2, :lo12:.LANCHOR4
	mov	w2, w22
	strh	w0, [x25,290]
	ldr	x0, [x29,128]
	bl	printk
	ldr	w0, [x25,296]
	cmp	w0, 31
	bhi	.L2324
	add	x1, x25, 304
	ldr	w2, [x29,204]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x25,296]
.L2324:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,2348]
	cmn	w1, #1
	beq	.L2413
	cmp	w1, w22
	bls	.L2301
.L2413:
	str	w22, [x0,2348]
.L2301:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2300
.L2422:
	ldrb	w0, [x19,8]
	add	w24, w24, 1
	cmp	w0, 1
	uxth	w24, w24
	bne	.L2327
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,120]
	cbz	w1, .L2327
	cmp	w21, w24
	bne	.L2327
	ldrh	w0, [x0,2020]
	cmp	w0, w24
	beq	.L2303
.L2327:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2018]
	cmp	w24, w1
	bne	.L2295
	ldrh	w2, [x0,1944]
	mov	w3, 65535
	strh	w24, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2329:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2409
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2329
	strb	w1, [x19,6]
	b	.L2409
.L2303:
	strb	w26, [x19,6]
	strh	w21, [x19,2]
.L2416:
	mov	x0, x19
	mov	w1, w21
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2409:
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
	ldr	w0, [x20,-96]
	cbnz	w0, .L2423
	ldrh	w1, [x20,4]
	cbz	w1, .L2425
	ldrb	w0, [x20,8]
	cbnz	w0, .L2425
	adrp	x22, .LANCHOR0
	ldrb	w0, [x20,7]
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w2, [x22,2018]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2425
	ldrb	w24, [x20,10]
	ldr	w23, [x22,2096]
	ldrh	w25, [x22,1944]
	cbnz	w24, .L2423
	sub	w23, w23, #1
	add	x1, x29, 84
	mov	w0, w23
	mov	w2, w24
	bl	log2phys
	ldr	x21, [x20,2120]
	ldr	w0, [x29,84]
	ldr	x1, [x20,-64]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w23, [x29,112]
	str	x1, [x29,96]
	str	x21, [x29,104]
	str	w24, [x21,4]
	beq	.L2427
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w24
	bl	FlashReadPages
	b	.L2428
.L2427:
	ldr	x0, [x20,-64]
	mov	w1, 255
	ldrh	w2, [x22,2028]
	bl	ftl_memset
.L2428:
	mov	w0, -3947
	lsl	w25, w25, 2
	strh	w0, [x21]
	mov	w22, 0
	add	x20, x19, :lo12:.LANCHOR2
.L2429:
	cmp	w22, w25
	beq	.L2430
	ldrh	w0, [x20,4]
	cbz	w0, .L2430
	ldr	w0, [x29,92]
	add	w22, w22, 1
	str	w0, [x21,12]
	ldrh	w0, [x20]
	strh	w0, [x21,2]
	mov	x0, x20
	str	w23, [x21,8]
	bl	get_new_active_ppa
	str	w0, [x29,92]
	ldr	w1, [x20,200]
	mov	w2, 0
	str	w1, [x21,4]
	mov	w3, w2
	add	w1, w1, 1
	add	x0, x29, 88
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x20,200]
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	b	.L2429
.L2430:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,10]
	b	.L2423
.L2425:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,10]
.L2423:
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
.L2441:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2058]
	cmp	w0, w19
	bls	.L2444
	add	x1, x21, :lo12:.LANCHOR2
	ubfiz	x0, x19, 4, 16
	ldr	x1, [x1,144]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2442
	mov	w0, w19
	bl	flush_l2p_region
.L2442:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2441
.L2444:
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC128
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 16
	add	x0, x0, :lo12:.LC128
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR0
	bl	printk
	add	x1, x21, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	mov	w19, 0
	ldrh	w2, [x1,1954]
	mov	w1, 0
	ldr	x0, [x0,2168]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2446:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,2096]
	cmp	w19, w0
	bcs	.L2461
	mov	w0, w19
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2447
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x2, [x1,2168]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2447:
	add	w19, w19, 1
	b	.L2446
.L2461:
	adrp	x23, .LC129
	mov	w19, 0
	add	x20, x20, :lo12:.LANCHOR2
	mov	w24, 65535
	add	x23, x23, :lo12:.LC129
.L2449:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,1952]
	cmp	w0, w19
	bls	.L2462
	ubfiz	x22, x19, 1, 16
	ldr	x0, [x20,-40]
	ldrh	w2, [x0,x22]
	ldr	x0, [x20,2168]
	ldrh	w3, [x0,x22]
	cmp	w2, w3
	beq	.L2452
	cmp	w2, w24
	beq	.L2452
	mov	x0, x23
	mov	w1, w19
	bl	printk
	ldrh	w0, [x20]
	cmp	w0, w19
	beq	.L2452
	ldrh	w0, [x20,96]
	cmp	w0, w19
	beq	.L2452
	ldrh	w0, [x20,48]
	cmp	w0, w19
	beq	.L2452
	ldr	x0, [x20,2168]
	ldrh	w1, [x0,x22]
	ldr	x0, [x20,-40]
	strh	w1, [x0,x22]
	mov	w0, w19
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2452:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2449
.L2462:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	str	x21, [sp,32]
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w20, [x0]
	ldr	w1, [x2,-96]
	cbnz	w1, .L2464
	mov	x21, x0
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2465
	ubfiz	x1, x20, 1, 16
	ldr	x0, [x2,-40]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2466
	mov	w0, w20
	bl	INSERT_DATA_LIST
	b	.L2465
.L2466:
	mov	w0, w20
	bl	INSERT_FREE_LIST
.L2465:
	add	x3, x19, :lo12:.LANCHOR2
	strb	wzr, [x21,8]
	add	x0, x3, 48
	cmp	x21, x0
	beq	.L2467
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w2, [x1,1964]
	cmp	w2, 1
	beq	.L2467
	ldrb	w0, [x1,120]
	cbz	w0, .L2468
.L2467:
	mov	w0, 1
	strb	w0, [x21,8]
	b	.L2469
.L2468:
	cmp	x21, x3
	bne	.L2469
	cmp	w2, 3
	beq	.L2471
	ldr	w0, [x21,344]
	cmp	w0, 1
	bne	.L2472
.L2471:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x1,8]
.L2472:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,160]
	cbz	w0, .L2469
	ldr	w0, [x1,220]
	cmp	w0, 29
	bhi	.L2469
	mov	w0, 1
	strb	w0, [x1,8]
.L2469:
	add	x3, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w1, [x3,2336]
	cmp	w1, w0
	beq	.L2474
	cmp	w20, w1
	bne	.L2475
	ubfiz	x2, x1, 1, 16
	ldr	x0, [x3,-40]
	ldrh	w0, [x0,x2]
	cbz	w0, .L2476
.L2475:
	mov	w0, w1
	bl	update_vpc_list
.L2476:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,2336]
.L2474:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2464:
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
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
	cbnz	w4, .L2634
	ldr	w4, [x3,2032]
	cbnz	w4, .L2634
	ldrh	w4, [x3,-24]
	cmp	w4, 47
	bls	.L2634
	mov	w23, w0
	adrp	x0, .LANCHOR1+3008
	mov	w22, w1
	ldrh	w1, [x0,#:lo12:.LANCHOR1+3008]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2491
.L2494:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,954]
	cmp	w1, w2
	bne	.L2492
	b	.L2493
.L2491:
	ldrh	w1, [x3,96]
	cmp	w1, w0
	beq	.L2494
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2494
	b	.L2634
.L2492:
	ldrh	w3, [x0,952]
	cmp	w3, w2
	bne	.L2493
	strh	w1, [x0,952]
	mov	w1, -1
	strh	w1, [x0,954]
.L2493:
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w23, 1
	ldr	w0, [x1,868]
	add	w0, w0, 1
	add	w0, w0, w23, lsl 7
	str	w0, [x1,868]
	bne	.L2505
	ldr	w1, [x1,160]
	cbnz	w1, .L2496
	adrp	x1, .LANCHOR0+120
	ldrb	w1, [x1,#:lo12:.LANCHOR0+120]
	cbz	w1, .L2505
.L2496:
	add	x20, x19, :lo12:.LANCHOR2
	ldr	w1, [x20,220]
	cmp	w1, 29
	bhi	.L2505
	adrp	x21, .LANCHOR4
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w1, [x1,432]
	add	w0, w0, w1
	str	w0, [x20,868]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x20,296]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2505
	ldrh	w1, [x20,952]
	cmp	w1, w0
	bne	.L2505
	ldr	w0, [x20,868]
	cmp	w0, 1024
	bhi	.L2497
	ldrh	w0, [x20,-8]
	cmp	w0, 63
	bhi	.L2505
.L2497:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w2, [x0,866]
	ldrh	w3, [x0,-8]
	add	w2, w2, 64
	strh	wzr, [x1,432]
	cmp	w3, w2
	bgt	.L2505
	str	wzr, [x0,868]
	ldr	w0, [x0,220]
	cbnz	w0, .L2498
	mov	w0, 6
	b	.L2656
.L2498:
	cmp	w0, 5
	bhi	.L2499
	mov	w0, 18
.L2656:
	strh	w0, [x1,432]
.L2499:
	mov	w0, 32
	mov	w27, 65535
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w27
	beq	.L2504
	add	x24, x19, :lo12:.LANCHOR2
	ldrh	w0, [x24,872]
	cbz	w0, .L2501
	adrp	x2, .LANCHOR0
	ldr	x26, [x24,-40]
	add	x2, x2, :lo12:.LANCHOR0
	ubfiz	x25, x25, 1, 16
	ldrh	w1, [x2,2020]
	ldrh	w2, [x2,1944]
	ldrh	w3, [x26,x25]
	mul	w1, w1, w2
	add	w1, w1, 1
	cmp	w3, w1
	bgt	.L2504
	add	w1, w0, 1
	str	wzr, [x24,876]
	uxth	w1, w1
	strh	w1, [x24,872]
	str	x1, [x29,120]
	bl	List_get_gc_head_node
	uxth	w20, w0
	cmp	w20, w27
	ldr	x1, [x29,120]
	beq	.L2504
	ubfiz	x27, x20, 1, 16
	adrp	x0, .LC130
	ldrh	w4, [x26,x25]
	add	x0, x0, :lo12:.LC130
	mov	w2, w20
	ldrh	w3, [x26,x27]
	bl	printk
	ldrh	w0, [x24,872]
	cmp	w0, 40
	bls	.L2502
	ldr	x0, [x24,-40]
	ldrh	w0, [x0,x27]
	cmp	w0, 32
	bls	.L2502
	strh	wzr, [x24,872]
.L2502:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, 6
	strh	w0, [x21,432]
	b	.L2506
.L2501:
	mov	w0, 1
	strh	w0, [x24,872]
.L2504:
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2506
	add	x21, x21, :lo12:.LANCHOR4
	strh	wzr, [x21,432]
.L2505:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w20, [x21,296]
	cmp	w20, w0
	bne	.L2660
	ldrh	w0, [x21,96]
	cmp	w0, w20
	beq	.L2661
.L2506:
	mov	w0, 65535
	cmp	w20, w0
	cset	w1, eq
	cbz	w1, .L2519
	cbnz	w23, .L2519
	add	x0, x19, :lo12:.LANCHOR2
	mov	w21, 1
	ldrh	w1, [x0,-8]
	cmp	w1, 24
	bhi	.L2520
	adrp	x0, .LANCHOR0
	cmp	w1, 16
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w21, [x0,2018]
	bls	.L2521
	lsr	w21, w21, 5
	b	.L2520
.L2521:
	cmp	w1, 12
	bls	.L2522
	lsr	w21, w21, 4
	b	.L2520
.L2522:
	cmp	w1, 8
	bls	.L2520
	lsr	w21, w21, 2
.L2520:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,864]
	cmp	w2, w1
	bcs	.L2524
	ldrh	w1, [x0,96]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L2525
	ldrh	w2, [x0,952]
	cmp	w2, w1
	bne	.L2525
	adrp	x1, .LANCHOR4+432
	ldrh	w2, [x1,#:lo12:.LANCHOR4+432]
	cbnz	w2, .L2526
	adrp	x1, .LANCHOR0+2096
	ldr	w3, [x0,156]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2096]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2527
.L2526:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,272]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,864]
	b	.L2528
.L2527:
	mov	w1, 18
	strh	w1, [x0,864]
.L2528:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,876]
	b	.L2634
.L2525:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,272]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,864]
.L2524:
	cmp	w22, 2
	bhi	.L2589
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,160]
	cbz	w0, .L2589
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2589
.L2519:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x21,96]
	cmp	w0, w2
	bne	.L2531
	cbz	w1, .L2531
	ldrh	w1, [x21,952]
	cmp	w1, w0
	bne	.L2531
	ldrh	w1, [x21,296]
	cmp	w1, w0
	beq	.L2532
.L2537:
	mov	w20, 65535
	b	.L2531
.L2532:
	ldrh	w24, [x21,-8]
	adrp	x20, .LANCHOR4
	ldrh	w0, [x21,864]
	str	wzr, [x21,876]
	cmp	w0, w24
	bcs	.L2533
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0,432]
	cbnz	w0, .L2534
	adrp	x0, .LANCHOR0+2096
	ldr	w1, [x21,156]
	ldr	w0, [x0,#:lo12:.LANCHOR0+2096]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bcs	.L2535
.L2534:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,272]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x19,864]
	b	.L2536
.L2535:
	mov	w0, 18
	strh	w0, [x21,864]
.L2536:
	add	x20, x20, :lo12:.LANCHOR4
	bl	FtlReadRefresh
	ldrh	w2, [x20,432]
	b	.L2634
.L2533:
	add	x22, x20, :lo12:.LANCHOR4
	ldrh	w0, [x22,432]
	cbnz	w0, .L2537
	ldrh	w20, [x21,272]
	add	w1, w20, w20, lsl 1
	asr	w1, w1, 2
	strh	w1, [x21,864]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x21,-40]
	ldrh	w2, [x1,x0]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1,2020]
	ldrh	w1, [x1,1944]
	mul	w0, w0, w1
	mov	w1, 2
	sdiv	w0, w0, w1
	cmp	w2, w0
	ble	.L2538
	sub	w20, w20, #1
	cmp	w24, w20
	blt	.L2538
	bl	FtlReadRefresh
	ldrh	w2, [x22,432]
	b	.L2634
.L2538:
	cbnz	w2, .L2537
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,-8]
	add	w2, w2, 1
	b	.L2634
.L2531:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w21, 2
	ldr	w0, [x0,160]
	cmp	w0, wzr
	csinc	w21, w21, wzr, ne
	b	.L2530
.L2589:
	mov	w20, 65535
.L2530:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,296]
	cmp	w1, w2
	bne	.L2540
	cmp	w20, w1
	beq	.L2541
	strh	w20, [x0,296]
	b	.L2542
.L2541:
	ldrh	w1, [x0,952]
	cmp	w1, w20
	beq	.L2542
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x0,-40]
	ldrh	w1, [x2,x1]
	cbnz	w1, .L2543
	mov	w1, -1
	strh	w1, [x0,952]
.L2543:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,952]
	strh	w1, [x0,296]
	mov	w1, -1
	strh	w1, [x0,952]
.L2542:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x22,296]
	strb	wzr, [x22,304]
	cmp	w0, w1
	beq	.L2540
	bl	IsBlkInGcList
	cbz	w0, .L2545
	mov	w0, -1
	strh	w0, [x22,296]
.L2545:
	adrp	x0, .LANCHOR0+120
	ldrb	w0, [x0,#:lo12:.LANCHOR0+120]
	cbz	w0, .L2546
	add	x0, x19, :lo12:.LANCHOR2
	add	x22, x0, 296
	ldrh	w0, [x0,296]
	bl	ftl_get_blk_mode
	strb	w0, [x22,8]
.L2546:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x24, x22, 296
	ldrh	w1, [x22,296]
	cmp	w1, w0
	beq	.L2540
	mov	x0, x24
	bl	make_superblock
	strh	wzr, [x22,298]
	adrp	x0, .LANCHOR4
	ldrh	w2, [x22,296]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x1, [x22,-40]
	strb	wzr, [x22,302]
	strh	wzr, [x0,434]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,436]
.L2540:
	add	x3, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x3,296]
	cmp	w1, w0
	beq	.L2547
	ldrh	w1, [x3,48]
	cmp	w1, w0
	beq	.L2547
.L2548:
	mov	x28, x3
	mov	w24, 65535
	b	.L2549
.L2547:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,296]
	b	.L2659
.L2570:
	ldrh	w0, [x28,298]
	add	w21, w21, w0
	uxth	w21, w21
	strh	w21, [x28,298]
	cmp	w21, w25
	bcs	.L2662
.L2572:
	ldrh	w0, [x28,-8]
	cmp	w0, 2
	bhi	.L2575
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w21, [x0,2018]
.L2549:
	ldrh	w0, [x28,296]
	cmp	w0, w24
	bne	.L2550
	str	wzr, [x28,876]
.L2551:
	ldrh	w22, [x28,872]
	mov	w0, w22
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w24
	strh	w25, [x28,296]
	bne	.L2552
	strh	wzr, [x28,872]
	mov	w2, 8
	b	.L2634
.L2552:
	mov	w0, w25
	add	w22, w22, 1
	bl	IsBlkInGcList
	cbz	w0, .L2553
	strh	w22, [x28,872]
	b	.L2551
.L2553:
	adrp	x0, .LANCHOR0
	ubfiz	x1, x25, 1, 16
	add	x5, x0, :lo12:.LANCHOR0
	ldr	x2, [x28,-40]
	uxth	w22, w22
	mov	w3, 2
	strh	w22, [x28,872]
	ldrh	w0, [x5,2018]
	ldrh	w5, [x5,1944]
	ldrh	w4, [x2,x1]
	mul	w0, w0, w5
	sdiv	w5, w0, w3
	cmp	w4, w5
	bgt	.L2555
	cmp	w4, 8
	bls	.L2556
	cmp	w22, 48
	bls	.L2556
	ldrh	w4, [x28,924]
	cmp	w4, 35
	bhi	.L2556
.L2555:
	strh	wzr, [x28,872]
.L2556:
	ldrh	w1, [x2,x1]
	cmp	w1, w0
	blt	.L2557
	cmp	w20, w24
	bne	.L2557
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x19,872]
	strh	w0, [x19,296]
.L2659:
	adrp	x0, .LANCHOR4+432
	ldrh	w2, [x0,#:lo12:.LANCHOR4+432]
	b	.L2634
.L2557:
	cbnz	w1, .L2558
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x28,872]
	add	w0, w0, 1
	strh	w0, [x28,872]
	b	.L2551
.L2558:
	adrp	x0, .LANCHOR0
	strb	wzr, [x28,304]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L2559
	mov	w0, w25
	bl	ftl_get_blk_mode
	strb	w0, [x28,304]
.L2559:
	add	x22, x28, 296
	mov	x0, x22
	bl	make_superblock
	adrp	x1, .LANCHOR4
	ldrh	w2, [x28,296]
	add	x1, x1, :lo12:.LANCHOR4
	ldr	x0, [x28,-40]
	strh	wzr, [x1,434]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,436]
	strh	wzr, [x28,298]
	strb	wzr, [x28,302]
.L2550:
	cmp	w23, 1
	bne	.L2560
	bl	FtlReadRefresh
.L2560:
	mov	w0, 1
	str	w0, [x28,2032]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,120]
	ldrh	w25, [x0,2018]
	cbz	w1, .L2561
	ldrb	w1, [x28,304]
	cmp	w1, 1
	bne	.L2561
	ldrh	w25, [x0,2020]
.L2561:
	ldrh	w0, [x28,298]
	add	w1, w0, w21
	cmp	w1, w25
	ble	.L2562
	sub	w21, w25, w0
	uxth	w21, w21
.L2562:
	mov	w26, 0
.L2563:
	cmp	w21, w26, uxth
	bls	.L2570
	adrp	x0, .LANCHOR0
	add	x1, x28, 296
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w4, [x1,2]
	add	w4, w4, w26
	ldrh	w6, [x0,1944]
	mov	x0, 0
	mov	w22, w0
.L2571:
	cmp	w6, w0, uxth
	bls	.L2663
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,16]
	cmp	w2, w24
	beq	.L2564
	mov	w3, 56
	ldr	x7, [x28,912]
	orr	w2, w4, w2, lsl 10
	umull	x5, w22, w3
	add	w22, w22, 1
	add	x5, x7, x5
	uxth	w22, w22
	str	w2, [x5,4]
.L2564:
	add	x0, x0, 1
	b	.L2571
.L2663:
	ldr	x0, [x28,912]
	mov	w1, w22
	ldrb	w2, [x28,304]
	mov	x27, 0
	bl	FlashReadPages
.L2566:
	cmp	w22, w27, uxth
	bls	.L2664
	mov	x0, 56
	mul	x4, x27, x0
	ldr	x0, [x28,912]
	add	x1, x0, x4
	ldr	w0, [x0,x4]
	cmn	w0, #1
	ldr	x5, [x1,16]
	beq	.L2591
	ldrh	w0, [x5]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2591
	ldr	w0, [x5,8]
	add	x1, x29, 140
	mov	w2, 0
	str	x4, [x29,112]
	str	x5, [x29,120]
	bl	log2phys
	ldr	x2, [x28,912]
	ldr	x4, [x29,112]
	ldr	w0, [x29,140]
	add	x2, x2, x4
	ldr	x5, [x29,120]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L2591
	adrp	x0, .LANCHOR4
	mov	x3, 56
	add	x1, x0, :lo12:.LANCHOR4
	str	x4, [x29,104]
	str	x5, [x29,112]
	ldrh	w0, [x1,434]
	add	w0, w0, 1
	strh	w0, [x1,434]
	ldr	w0, [x28,880]
	ldr	x1, [x28,2064]
	madd	x1, x0, x3, x1
	ldr	w0, [x2,24]
	str	x1, [x29,120]
	str	w0, [x1,24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29,120]
	mov	x2, 56
	ldr	x4, [x29,104]
	ldr	x5, [x29,112]
	str	w0, [x1,4]
	ldr	w0, [x28,880]
	ldr	x1, [x28,2064]
	madd	x0, x0, x2, x1
	ldr	x1, [x28,912]
	add	x1, x1, x4
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	add	x2, x28, 96
	str	x2, [x29,120]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,140]
	str	w0, [x5,12]
	ldrh	w0, [x28,96]
	strh	w0, [x5,2]
	ldr	w0, [x28,200]
	str	w0, [x5,4]
	ldr	w0, [x28,880]
	add	w0, w0, 1
	str	w0, [x28,880]
	ldr	x0, [x28,912]
	add	x0, x0, x4
	bl	FtlGcBufAlloc
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2568
	ldr	x2, [x29,120]
	ldr	w1, [x28,880]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2568
	ldrh	w0, [x2,4]
	cbnz	w0, .L2591
.L2568:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2591
	adrp	x0, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x19,2032]
	ldrh	w2, [x0,432]
	b	.L2634
.L2591:
	add	x27, x27, 1
	b	.L2566
.L2664:
	add	w26, w26, 1
	b	.L2563
.L2662:
	ldr	w0, [x28,880]
	adrp	x21, .LANCHOR4
	cbz	w0, .L2573
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2573
	add	x21, x21, :lo12:.LANCHOR4
	str	wzr, [x28,2032]
	ldrh	w2, [x21,432]
	b	.L2634
.L2573:
	add	x21, x21, :lo12:.LANCHOR4
	ldrh	w2, [x21,434]
	cbnz	w2, .L2574
	ldrh	w0, [x28,296]
	ldr	x1, [x28,-40]
	lsl	x0, x0, 1
	ldrh	w4, [x1,x0]
	cbz	w4, .L2574
	strh	w2, [x1,x0]
	ldrh	w0, [x28,296]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2574:
	mov	w0, -1
	strh	w0, [x28,296]
	b	.L2572
.L2575:
	adrp	x1, .LANCHOR4+432
	str	wzr, [x28,2032]
	ldrh	w2, [x1,#:lo12:.LANCHOR4+432]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L2634
.L2661:
	ldrh	w25, [x21,952]
	cmp	w25, w20
	bne	.L2506
	ldrh	w0, [x21,-8]
	mov	w2, 1024
	ldr	w1, [x21,868]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2506
	adrp	x0, .LANCHOR4+432
	str	wzr, [x21,868]
	mov	x24, x21
	strh	wzr, [x0,#:lo12:.LANCHOR4+432]
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	cmp	w20, w25
	bne	.L2508
	ldrh	w1, [x21,-8]
	ldrh	w0, [x21,866]
	cmp	w1, w0
	bcs	.L2509
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w20
	beq	.L2518
	ldr	w2, [x21,2028]
	uxtw	x1, w0
	adrp	x21, .LANCHOR0
	cbnz	w2, .L2511
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w2, [x0,1964]
	cmp	w2, 3
	beq	.L2511
	ldr	w2, [x24,344]
	cbnz	w2, .L2511
	ldr	w2, [x24,160]
	cbnz	w2, .L2511
	ldrb	w0, [x0,120]
	cbz	w0, .L2512
.L2511:
	add	x0, x19, :lo12:.LANCHOR2
	add	x3, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,-40]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x3,2020]
	ldrh	w0, [x3,1944]
	ldrh	w3, [x3,1964]
	mul	w2, w1, w0
	cmp	w3, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w2, w0
	cmp	w4, w0
	bgt	.L2514
	mov	w0, 0
	add	x21, x21, :lo12:.LANCHOR0
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x21,2096]
	ldr	w2, [x1,156]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2515
	mov	w0, 128
	b	.L2657
.L2515:
	mov	w0, 160
.L2657:
	strh	w0, [x1,866]
	b	.L2516
.L2514:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L2658
.L2512:
	ldr	x2, [x24,-40]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2517
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0,866]
	b	.L2516
.L2517:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L2658:
	strh	w1, [x0,866]
	b	.L2518
.L2509:
	mov	w0, 80
	strh	w0, [x21,866]
	b	.L2518
.L2516:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2518
.L2508:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x20, 1, 32
	adrp	x0, .LC131
	mov	w1, w20
	add	x0, x0, :lo12:.LC131
	ldr	x3, [x5,-40]
	ldr	x6, [x5,-80]
	ldrh	w2, [x5,-8]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,864]
	ldrh	w4, [x6,x4]
	bl	printk
.L2518:
	bl	FtlGcReFreshBadBlk
	b	.L2506
.L2660:
	mov	w20, w0
	b	.L2506
.L2634:
	mov	w0, w2
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	stp	x29, x30, [sp, -128]!
	adrp	x0, .LANCHOR4+440
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	ldr	x20, [x0,#:lo12:.LANCHOR4+440]
	add	x0, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w23, [x0,-96]
	cbnz	w23, .L2667
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cbz	w1, .L2667
	ldrb	w0, [x0,120]
	cbz	w0, .L2669
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w23, eq
.L2669:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x20,9]
	adrp	x26, .LC132
	mov	w2, w23
	mov	w24, 0
	mov	w27, 56
	ldr	x0, [x0,2088]
	add	x26, x26, :lo12:.LC132
	bl	FlashProgPages
.L2670:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cmp	w24, w1
	bcs	.L2687
	umull	x21, w24, w27
	ldr	x1, [x0,2088]
	add	x0, x1, x21
	ldr	w25, [x1,x21]
	cmn	w25, #1
	beq	.L2690
	ldr	w1, [x0,4]
	cbz	w23, .L2704
	orr	w1, w1, -2147483648
.L2704:
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
	beq	.L2674
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x22, :lo12:.LANCHOR2
	ubfiz	x0, x1, 1, 16
	mov	w21, w1
	ldr	x2, [x2,-40]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2675
	mov	x0, x26
	bl	printk
.L2675:
	mov	w0, w21
	bl	decrement_vpc_count
.L2674:
	add	w24, w24, 1
	b	.L2670
.L2706:
	mov	w20, 16386
.L2686:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w0, [x0,956]
	cbz	w0, .L2687
	mov	w0, 1
	mov	w1, w0
	bl	rk_ftl_garbage_collect
	subs	w20, w20, #1
	bne	.L2686
.L2687:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19,2080]
	b	.L2667
.L2690:
	adrp	x26, .LC132
	mov	w24, 0
	mov	w27, 56
	add	x26, x26, :lo12:.LC132
.L2671:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cmp	w24, w1
	bcs	.L2706
	umull	x21, w24, w27
	ldr	x0, [x0,2088]
	str	w25, [x0,x21]
.L2677:
	add	x28, x19, :lo12:.LANCHOR0
	ldr	x0, [x28,2088]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	ldr	w0, [x1,4]
	bne	.L2707
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	bne	.L2678
	add	x0, x22, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x20,4]
	ldr	x2, [x0,-40]
	ldrh	w0, [x2,x1]
	sub	w0, w0, w3
	strh	w0, [x2,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x28,2018]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L2678:
	ldrh	w0, [x20,4]
	cbnz	w0, .L2679
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2679:
	add	x4, x22, :lo12:.LANCHOR2
	add	x28, x19, :lo12:.LANCHOR0
	str	x4, [x29,104]
	ldr	w0, [x4,448]
	add	w0, w0, 1
	str	w0, [x4,448]
	ldr	x0, [x28,2088]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,124]
	ldr	x1, [x28,2088]
	mov	w2, w23
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x28,2088]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	ldr	x4, [x29,104]
	ldr	w0, [x4,-96]
	cbz	w0, .L2677
	b	.L2667
.L2707:
	cbz	w23, .L2705
	orr	w0, w0, -2147483648
.L2705:
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
	beq	.L2683
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x22, :lo12:.LANCHOR2
	ubfiz	x0, x1, 1, 16
	mov	w21, w1
	ldr	x2, [x2,-40]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2684
	mov	x0, x26
	bl	printk
.L2684:
	mov	w0, w21
	bl	decrement_vpc_count
.L2683:
	add	w24, w24, 1
	b	.L2671
.L2667:
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
	cbnz	w0, .L2709
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2709:
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
	bne	.L2711
	bl	FtlSysFlush
.L2711:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDeInit, .-FtlDeInit
	.align	2
	.global	rk_ftl_de_init
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FlashDeInit
	bl	FtlDeInit
	bl	FlashDeInit
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
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
	ldr	w1, [x21,2060]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L2714
	cmp	w19, 31
	bls	.L2728
	adrp	x23, .LANCHOR2
	add	x0, x23, :lo12:.LANCHOR2
	ldr	w0, [x0,-96]
	cbnz	w0, .L2728
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,2024]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L2717
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L2717:
	mov	w0, -1
	adrp	x20, .LANCHOR4
	str	w0, [x29,76]
.L2718:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w19, w0
	bcc	.L2729
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L2719
	add	x1, x20, :lo12:.LANCHOR4
	mov	w2, 1
	ldr	w0, [x1,448]
	add	w0, w0, 1
	str	w0, [x1,448]
	add	x1, x23, :lo12:.LANCHOR2
	ldr	w0, [x1,168]
	add	w0, w0, 1
	str	w0, [x1,168]
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,72]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2719:
	add	x0, x22, :lo12:.LANCHOR0
	add	w21, w21, 1
	ldrh	w0, [x0,2024]
	sub	w19, w19, w0
	b	.L2718
.L2729:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,448]
	cmp	w1, 32
	bls	.L2728
	str	wzr, [x0,448]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2728:
	mov	w0, 0
.L2714:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlDiscard, .-FtlDiscard
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
	ldrh	w1, [x1,2018]
	cbnz	w3, .L2765
	ldrh	w20, [x2,96]
	mov	w4, 65535
	cmp	w20, w4
	bne	.L2733
.L2742:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x22, x20, 96
	ldrh	w1, [x20,96]
	str	wzr, [x20,2344]
	cmp	w1, w0
	beq	.L2765
	bl	FtlCacheWriteBack
	mov	w26, 12
	add	x3, x21, :lo12:.LANCHOR0
	ldrb	w0, [x22,7]
	ldr	x1, [x20,-40]
	mov	w22, 0
	ldrh	w2, [x20,96]
	ldrh	w3, [x3,2018]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldr	w0, [x20,184]
	ldrh	w1, [x20,936]
	add	w0, w1, w0
	str	w0, [x20,184]
	b	.L2743
.L2733:
	cbz	w0, .L2736
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3008]
	cmp	w5, w4
	beq	.L2737
.L2738:
	mov	w1, 2
	b	.L2736
.L2737:
	strh	w3, [x0,3008]
	ldrh	w0, [x2,-8]
	cmp	w0, 17
	bhi	.L2738
.L2736:
	add	x22, x19, :lo12:.LANCHOR2
	add	x0, x22, 96
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L2739
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x22,-80]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L2740
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2740:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2344]
	cbnz	w1, .L2741
	ldr	w1, [x0,448]
	add	w1, w1, 1
	str	w1, [x0,448]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2741:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	str	wzr, [x19,2344]
	b	.L2732
.L2739:
	adrp	x0, .LANCHOR1+3008
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3008]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2732
	b	.L2742
.L2746:
	ldr	x25, [x20,944]
	add	x1, x29, 92
	umull	x24, w22, w26
	mov	w2, 0
	add	x23, x25, x24
	ldr	w0, [x23,8]
	bl	log2phys
	ldr	w0, [x29,92]
	ldr	w1, [x25,x24]
	cmp	w0, w1
	bne	.L2744
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
.L2764:
	bl	decrement_vpc_count
.L2745:
	add	w22, w22, 1
	uxth	w22, w22
.L2743:
	ldrh	w0, [x20,936]
	cmp	w0, w22
	bhi	.L2746
	b	.L2766
.L2744:
	ldr	w1, [x23,4]
	cmp	w0, w1
	beq	.L2745
	ldrh	w0, [x20,96]
	b	.L2764
.L2766:
	mov	w0, -1
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L2747
	adrp	x0, .LC133
	ldrh	w1, [x20,96]
	add	x0, x0, :lo12:.LC133
	bl	printk
.L2747:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,96]
	ldr	x1, [x1,-40]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1,x2]
	cbz	w1, .L2748
	bl	INSERT_DATA_LIST
	b	.L2749
.L2748:
	bl	INSERT_FREE_LIST
.L2749:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x20,936]
	strh	w0, [x20,96]
	strh	wzr, [x20,924]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	w0, [x20,160]
	cbz	w0, .L2750
	ldr	w0, [x20,220]
	cmp	w0, 29
	bhi	.L2750
	ldrh	w0, [x20,272]
	ldrh	w1, [x20,-8]
	cmp	w1, w0
	bcs	.L2751
	lsl	w0, w0, 1
	strh	w0, [x20,864]
.L2751:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,296]
.L2765:
	mov	w0, 0
	b	.L2732
.L2750:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,272]
	ldrh	w0, [x19,-8]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L2765
	add	x21, x21, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,296]
	ldrb	w0, [x21,120]
	cbz	w0, .L2752
	sub	w1, w1, #2
	strh	w1, [x19,864]
	b	.L2765
.L2752:
	mov	w1, 20
	strh	w1, [x19,864]
.L2732:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	adrp	x0, .LANCHOR0+120
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+120]
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	adrp	x21, .LANCHOR2
	cbz	w0, .L2768
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,880]
	tbz	x1, 0, .L2768
	ldrh	w0, [x0,100]
	cbz	w0, .L2768
.L2773:
	mov	w0, 0
	b	.L2769
.L2768:
	add	x1, x21, :lo12:.LANCHOR2
	mov	w2, 0
	mov	w3, w2
	mov	w20, 0
	mov	w22, 56
	ldr	x0, [x1,2064]
	ldr	w1, [x1,880]
	bl	FlashProgPages
.L2770:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w1, [x19,880]
	cmp	w20, w1
	bcs	.L2784
	umull	x0, w20, w22
	ldr	x2, [x19,2064]
	add	x1, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L2771
	ldrh	w3, [x19,96]
	ldr	x1, [x19,-40]
	strh	wzr, [x1,x3,lsl 1]
	strh	w2, [x19,96]
	ldr	w1, [x19,448]
	add	w1, w1, 1
	str	w1, [x19,448]
	ldr	x1, [x19,2064]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2783
.L2771:
	ldr	x2, [x1,16]
	add	w20, w20, 1
	ldr	w1, [x1,4]
	uxth	w20, w20
	ldr	w0, [x2,12]
	ldr	w2, [x2,8]
	bl	FtlGcUpdatePage
	b	.L2770
.L2784:
	ldr	x0, [x19,2064]
	bl	FtlGcBufFree
	str	wzr, [x19,880]
	ldrh	w0, [x19,100]
	cbnz	w0, .L2773
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L2783:
	mov	w0, 1
.L2769:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
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
	add	x21, x19, 96
	mov	x0, x21
	ldrh	w1, [x20,2018]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,98]
	ldrh	w0, [x20,2018]
	cmp	w1, w0
	bcc	.L2785
	add	x0, x19, 2256
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19,2344]
.L2785:
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
	adrp	x0, .LANCHOR4+296
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR4+296]
	mov	x0, x19
	bl	FtlRecoverySuperblock
	mov	x0, x19
	add	x19, x19, 48
	bl	FtlSlcSuperblockCheck
	mov	x0, x19
	bl	FtlRecoverySuperblock
	mov	x0, x19
	bl	FtlSlcSuperblockCheck
	bl	FtlGcPageRecovery
	mov	w0, -1
	bl	decrement_vpc_count
	ldr	x19, [sp,16]
	mov	w0, 0
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
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR4
	adrp	x24, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR4
	add	x22, x24, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	strh	w1, [x0,290]
	add	x19, x20, :lo12:.LANCHOR2
	strh	wzr, [x0,292]
	ldrh	w0, [x22,1948]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x19,280]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2789
.L2791:
	mov	w23, -1
	b	.L2790
.L2789:
	bl	FtlLoadSysInfo
	mov	w23, w0
	cbnz	w0, .L2791
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x22,2058]
	mov	x1, 0
	ldr	x0, [x19,144]
.L2792:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2796
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2792
.L2796:
	add	x0, x20, :lo12:.LANCHOR2
	cmp	w3, w2
	ldrh	w1, [x0,252]
	add	w1, w1, 1
	strh	w1, [x0,252]
	blt	.L2793
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w0, [x0,292]
	cbz	w0, .L2797
.L2793:
	add	x19, x20, :lo12:.LANCHOR2
	mov	x0, x19
	add	x22, x19, 48
	bl	FtlSuperblockPowerLostFix
	mov	x0, x22
	bl	FtlSuperblockPowerLostFix
	ldrh	w0, [x20,#:lo12:.LANCHOR2]
	ldr	x2, [x19,-40]
	lsl	x0, x0, 1
	ldrh	w3, [x19,4]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	add	x0, x24, :lo12:.LANCHOR0
	strb	wzr, [x19,6]
	ldr	x3, [x19,-40]
	strh	wzr, [x19,4]
	ldrh	w1, [x0,2018]
	strh	w1, [x19,2]
	ldrh	w1, [x19,48]
	ldrh	w4, [x19,52]
	lsl	x1, x1, 1
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strb	wzr, [x19,54]
	ldrh	w0, [x0,2018]
	strh	w0, [x19,50]
	add	x0, x19, 2256
	strh	wzr, [x19,52]
	bl	FtlMapBlkWriteDumpData
	add	x0, x21, :lo12:.LANCHOR4
	add	x0, x0, 224
	bl	FtlMapBlkWriteDumpData
	ldrh	w0, [x19,254]
	add	w0, w0, 1
	strh	w0, [x19,254]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2797:
	ldrh	w1, [x20,#:lo12:.LANCHOR2]
	mov	w0, 65535
	add	x19, x20, :lo12:.LANCHOR2
	cmp	w1, w0
	beq	.L2798
	ldrh	w0, [x19,4]
	cbnz	w0, .L2798
	ldrh	w0, [x19,52]
	add	x22, x19, 48
	cbnz	w0, .L2798
	bl	FtlVpcTblFlush
	adrp	x0, .LANCHOR2
	ldrh	w0, [x0,#:lo12:.LANCHOR2]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x19,48]
	bl	FtlGcRefreshOpenBlock
	mov	x0, x19
	bl	allocate_new_data_superblock
	mov	x0, x22
	bl	allocate_new_data_superblock
	add	x0, x21, :lo12:.LANCHOR4
	add	x0, x0, 224
	bl	FtlMapBlkWriteDumpData
.L2798:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x20,252]
	and	w0, w0, 31
	cbnz	w0, .L2790
	bl	FtlVpcCheckAndModify
.L2790:
	mov	w0, w23
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 96
	ldrh	w2, [x0,96]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2805
	ldrh	w0, [x1,4]
	cbnz	w0, .L2806
.L2805:
	bl	FtlCacheWriteBack
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	strb	wzr, [x20,104]
	add	x0, x20, 96
	bl	allocate_data_superblock
	strh	wzr, [x20,924]
	strh	wzr, [x20,936]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2806:
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 96
	bl	get_new_active_ppa
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
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
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LC74
	add	x29, sp, 0
	str	x21, [sp,32]
	adrp	x21, .LANCHOR1
	add	x21, x21, :lo12:.LANCHOR1
	add	x1, x1, :lo12:.LC74
	stp	x19, x20, [sp,16]
	mov	x19, x0
	mov	w0, -1
	str	w0, [x21,504]
	adrp	x0, .LANCHOR4+452
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR4+452]
	adrp	x0, .LC73
	add	x0, x0, :lo12:.LC73
	str	wzr, [x20,-96]
	bl	printk
	mov	x0, x19
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,1948]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2809
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
	b	.L2821
.L2809:
	bl	FtlSysBlkInit
	cbz	w0, .L2811
	adrp	x0, .LC135
	add	x0, x0, :lo12:.LC135
.L2821:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 40
	bl	printk
	b	.L2810
.L2811:
	mov	w1, 1
	str	w1, [x21,504]
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x20,-8]
	cmp	w0, 15
	bhi	.L2812
	mov	w19, 1024
.L2813:
	mov	w0, 1
	mov	w1, w0
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L2813
	b	.L2810
.L2812:
	ldrb	w0, [x19,120]
	cbz	w0, .L2810
	mov	w19, 128
.L2815:
	mov	w0, 1
	mov	w1, w0
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L2815
.L2810:
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
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
	str	x19, [sp,16]
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	bl	ftl_malloc
	add	x1, x19, 464
	str	x0, [x19,456]
	add	x0, x19, 48
	str	xzr, [x19,48]
	str	xzr, [x19,464]
	bl	rknand_get_reg_addr
	ldr	x1, [x19,48]
	mov	w0, -1
	cbz	x1, .L2823
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x19,456]
	mov	w2, w1
	mov	w3, 2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	x0, [x19,48]
	bl	FlashInit
	mov	w19, w0
	cbnz	w0, .L2824
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 168
	bl	FtlInit
.L2824:
	adrp	x0, .LC136
	mov	w1, w19
	add	x0, x0, :lo12:.LC136
	bl	printk
	mov	w0, w19
.L2823:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrb	w0, [x0,120]
	cbz	w0, .L2827
	adrp	x21, .LANCHOR4
	adrp	x19, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR4
	add	x25, x19, :lo12:.LANCHOR2
	adrp	x24, .LC137
	add	x26, x25, 48
	ldrh	w23, [x0,290]
	add	x0, x24, :lo12:.LC137
	ldr	x2, [x25,-40]
	ubfiz	x22, x23, 1, 16
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldrh	w0, [x19,#:lo12:.LANCHOR2]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x25,48]
	bl	FtlGcRefreshOpenBlock
	mov	x0, x25
	mov	w25, 4097
	bl	allocate_new_data_superblock
	mov	x0, x26
	bl	allocate_new_data_superblock
.L2829:
	subs	w25, w25, #1
	beq	.L2833
	mov	w0, 1
	mov	w1, w0
	bl	rk_ftl_garbage_collect
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,-40]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L2829
.L2833:
	add	x25, x19, :lo12:.LANCHOR2
	add	x0, x24, :lo12:.LC137
	mov	w1, w23
	ldr	x2, [x25,-40]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x25,-40]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L2831
	add	x0, x29, 80
	add	x20, x20, :lo12:.LANCHOR0
	strh	w23, [x29,80]
	bl	make_superblock
	mov	x0, 0
	ldrh	w5, [x20,1944]
	mov	w6, 65535
	mov	w20, w0
	mov	w7, 56
.L2834:
	cmp	w5, w0, uxth
	bls	.L2841
	add	x1, x29, 80
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L2835
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
.L2835:
	add	x0, x0, 1
	b	.L2834
.L2841:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC138
	mov	w1, w23
	add	x0, x0, :lo12:.LC138
	ldr	x2, [x19,-40]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x19,-88]
	mov	w1, 0
	mov	w2, w20
	bl	FlashEraseBlocks
	ldr	x0, [x19,-88]
	mov	w1, 1
	mov	w2, w20
	bl	FlashEraseBlocks
.L2831:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x21,290]
.L2827:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 128
	ret
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	stp	x29, x30, [sp, -176]!
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	cmp	w0, 16
	mov	w20, w1
	mov	w28, w2
	mov	x26, x3
	bne	.L2843
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L2844
.L2843:
	adrp	x21, .LANCHOR0
	add	w0, w1, w2
	add	x1, x21, :lo12:.LANCHOR0
	str	w0, [x29,152]
	mov	w0, -1
	ldr	w3, [x29,152]
	ldr	w2, [x1,2060]
	cmp	w3, w2
	bhi	.L2844
	ldrh	w0, [x1,2024]
	sub	w25, w3, #1
	adrp	x19, .LANCHOR2
	udiv	w1, w20, w0
	str	w1, [x29,156]
	udiv	w25, w25, w0
	ldr	w0, [x29,156]
	sub	w24, w25, w0
	add	x0, x19, :lo12:.LANCHOR2
	add	w24, w24, 1
	ldr	w1, [x0,192]
	add	w1, w28, w1
	str	w1, [x0,192]
	ldr	w1, [x0,164]
	add	w1, w24, w1
	str	w1, [x0,164]
	ldr	w0, [x29,156]
	mov	w1, w25
	bl	FtlCacheMetchLpa
	cbz	w0, .L2845
	bl	FtlCacheWriteBack
.L2845:
	mov	w27, 0
	ldr	w22, [x29,156]
	str	w27, [x29,140]
	mov	w23, w27
	str	w27, [x29,136]
.L2846:
	cbz	w24, .L2888
	mov	w0, w22
	add	x1, x29, 172
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,172]
	cmn	w3, #1
	bne	.L2885
	mov	w3, 0
.L2847:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w3, w0
	bcs	.L2851
	madd	w0, w22, w0, w3
	cmp	w0, w20
	bcc	.L2849
	ldr	w1, [x29,152]
	cmp	w0, w1
	bcs	.L2849
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x26, x0
	str	x3, [x29,144]
	bl	ftl_memset
	ldr	x3, [x29,144]
.L2849:
	add	w3, w3, 1
	b	.L2847
.L2885:
	mov	w0, 56
	umull	x1, w23, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x0,2056]
	add	x2, x2, x1
	str	w3, [x2,4]
	ldr	w2, [x29,156]
	cmp	w22, w2
	ldr	x2, [x0,2056]
	add	x2, x2, x1
	bne	.L2852
	ldr	x0, [x0,2104]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	udiv	w3, w20, w0
	msub	w3, w3, w0, w20
	str	w3, [x29,112]
	sub	w3, w0, w3
	cmp	w3, w28
	csel	w3, w3, w28, ls
	str	w3, [x29,140]
	cmp	w3, w0
	bne	.L2853
	str	x26, [x2,8]
	b	.L2853
.L2852:
	cmp	w22, w25
	bne	.L2854
	ldr	x0, [x0,2112]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w4, [x29,152]
	ldrh	w3, [x0,2024]
	mul	w0, w22, w3
	sub	w27, w4, w0
	cmp	w27, w3
	bne	.L2853
	b	.L2886
.L2854:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	mul	w0, w22, w0
.L2886:
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x26, x0
	str	x0, [x2,8]
.L2853:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2056]
	ldr	x2, [x2,2128]
	add	x1, x0, x1
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2030]
	str	w22, [x1,24]
	mul	w0, w23, w0
	add	w23, w23, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L2851:
	subs	w24, w24, #1
	add	w22, w22, 1
	beq	.L2855
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,1944]
	cmp	w23, w0, lsl 3
	bne	.L2846
.L2855:
	cbz	w23, .L2846
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, 0
	ldr	x0, [x0,2056]
	bl	FlashReadPages
	str	xzr, [x29,144]
	ldr	x0, [x29,112]
	ubfiz	x0, x0, 9, 23
	str	x0, [x29,128]
	ldr	w0, [x29,140]
	lsl	w0, w0, 9
	str	w0, [x29,108]
	lsl	w0, w27, 9
	str	w0, [x29,104]
.L2857:
	ldr	w0, [x29,144]
	cmp	w23, w0
	bls	.L2889
	ldr	x0, [x29,144]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	ldr	w4, [x29,156]
	mul	x3, x0, x1
	ldr	x0, [x2,2056]
	add	x0, x0, x3
	ldr	w1, [x0,24]
	cmp	w1, w4
	bne	.L2858
	ldr	x1, [x0,8]
	ldr	x0, [x2,2104]
	cmp	x1, x0
	bne	.L2859
	ldr	x2, [x29,128]
	mov	x0, x26
	str	x3, [x29,96]
	add	x1, x1, x2
	ldr	w2, [x29,108]
	b	.L2887
.L2858:
	cmp	w1, w25
	bne	.L2859
	ldr	x1, [x0,8]
	ldr	x0, [x2,2112]
	cmp	x1, x0
	bne	.L2859
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,104]
	str	x3, [x29,96]
	ldrh	w0, [x0,2024]
	mul	w0, w25, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x26, x0
.L2887:
	bl	ftl_memcpy
	ldr	x3, [x29,96]
.L2859:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,2056]
	add	x5, x1, x3
	ldr	w4, [x1,x3]
	cmn	w4, #1
	bne	.L2860
	ldr	w2, [x0,424]
	str	w4, [x29,136]
	add	w2, w2, 1
	str	w2, [x0,424]
.L2860:
	ldr	w0, [x1,x3]
	cmp	w0, 256
	bne	.L2861
	ldr	w0, [x5,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2861:
	ldr	x0, [x29,144]
	add	x0, x0, 1
	str	x0, [x29,144]
	b	.L2857
.L2889:
	mov	w23, 0
	b	.L2846
.L2888:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,956]
	cbz	w0, .L2864
	mov	w0, w24
	mov	w1, 1
	bl	rk_ftl_garbage_collect
.L2864:
	ldr	w0, [x29,136]
.L2844:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	stp	x29, x30, [sp, -272]!
	uxtb	w4, w0
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
	str	x1, [x29,152]
	ldr	w3, [x2,-96]
	cbnz	w3, .L2891
	cmp	w4, 16
	bne	.L2892
	add	w0, w22, 256
	mov	w1, w23
	mov	x2, x24
	bl	FtlVendorPartWrite
	b	.L2891
.L2892:
	adrp	x20, .LANCHOR0
	add	w26, w22, w23
	add	x4, x20, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	w1, [x4,2060]
	cmp	w26, w1
	bhi	.L2891
	adrp	x3, .LANCHOR4
	ldrh	w1, [x4,2024]
	add	x28, x3, :lo12:.LANCHOR4
	sub	w26, w26, #1
	mov	w0, 2048
	str	x3, [x29,144]
	udiv	w25, w22, w1
	cmp	w23, w1, lsl 1
	str	w0, [x28,472]
	add	x19, x2, 48
	ldr	w3, [x4,2080]
	udiv	w0, w26, w1
	str	w0, [x29,200]
	sub	w26, w0, w25
	add	w27, w26, 1
	ldr	w0, [x2,172]
	add	w0, w27, w0
	str	w0, [x2,172]
	ldr	w0, [x2,188]
	add	w0, w23, w0
	str	w0, [x2,188]
	cset	w0, cs
	str	w0, [x29,204]
	cmp	w23, 8
	csel	x19, x2, x19, hi
	cbz	w3, .L2895
	mov	w0, 56
	sub	w3, w3, #1
	umull	x3, w3, w0
	ldr	x0, [x4,2088]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w25, w0
	bne	.L2896
	ldr	w0, [x2,176]
	add	w0, w0, 1
	str	w0, [x2,176]
	ldr	w0, [x28,476]
	add	w0, w0, 1
	str	w0, [x28,476]
	msub	w0, w25, w1, w22
	sub	w1, w1, w0
	ubfiz	x0, x0, 9, 23
	cmp	w1, w23
	csel	w19, w1, w23, ls
	ldr	x1, [x3,8]
	lsl	w21, w19, 9
	add	x0, x1, x0
	mov	w2, w21
	mov	x1, x24
	bl	ftl_memcpy
	cbnz	w26, .L2897
	ldr	w0, [x28,476]
	cmp	w0, 2
	bgt	.L2897
.L2930:
	mov	w0, 0
	b	.L2891
.L2897:
	sub	w23, w23, w19
	add	w22, w22, w19
	add	x24, x24, x21
	add	w25, w25, 1
	mov	w27, w26
.L2896:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x19, [x0,440]
	str	wzr, [x0,476]
.L2895:
	ldr	w1, [x29,200]
	mov	w0, w25
	bl	FtlCacheMetchLpa
	cbz	w0, .L2898
	bl	FtlCacheWriteBack
.L2898:
	ldr	x0, [x29,144]
	str	w25, [x29,184]
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [x0,440]
	ldr	x0, [x29,152]
	add	x21, x0, :lo12:.LANCHOR2
	adrp	x0, .LC139
	add	x0, x0, :lo12:.LC139
	str	x0, [x29,104]
.L2899:
	cbz	w27, .L2960
	ldrh	w0, [x19,4]
	cbnz	w0, .L2900
	cmp	x19, x21
	bne	.L2901
	add	x0, x21, 48
	ldrh	w19, [x0,4]
	cbnz	w19, .L2902
	bl	allocate_new_data_superblock
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	str	w19, [x0,3012]
.L2902:
	mov	x0, x21
	add	x19, x21, 48
	bl	allocate_new_data_superblock
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldr	w0, [x0,3012]
	cbnz	w0, .L2903
.L2904:
	mov	x19, x21
	b	.L2903
.L2901:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	str	w0, [x1,3012]
	ldrh	w0, [x21,4]
	cbnz	w0, .L2904
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2903:
	ldrh	w0, [x19,4]
	cbnz	w0, .L2905
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2905:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [x0,440]
.L2900:
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w0, [x21,2048]
	ldrh	w1, [x19,4]
	str	xzr, [x29,192]
	ldr	w2, [x2,2080]
	sub	w0, w0, w2
	cmp	w1, w0
	csel	w0, w1, w0, ls
	cmp	w0, w27
	csel	w0, w0, w27, ls
	str	w0, [x29,140]
	ldr	w0, [x29,204]
	and	w0, w0, 1
	str	w0, [x29,116]
	add	w0, w23, w22
	str	w0, [x29,136]
.L2906:
	ldr	w0, [x29,192]
	ldr	w2, [x29,140]
	ldr	w1, [x29,184]
	cmp	w0, w2
	add	w26, w1, w0
	bcs	.L2961
	ldrh	w1, [x19,4]
	cbz	w1, .L2937
	ldr	w1, [x29,200]
	cmp	w26, w1
	cset	w7, eq
	cbz	w0, .L2908
	ldr	w1, [x29,116]
	tst	w7, w1
	beq	.L2908
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w2, [x29,136]
	ldrh	w1, [x1,2024]
	msub	w2, w26, w1, w2
	cmp	w2, w1
	bne	.L2937
.L2908:
	add	x1, x29, 212
	mov	w2, 0
	mov	w0, w26
	str	x7, [x29,96]
	bl	log2phys
	mov	x0, x19
	bl	get_new_active_ppa
	add	x4, x20, :lo12:.LANCHOR0
	mov	w5, 56
	str	x4, [x29,176]
	str	x5, [x29,160]
	ldr	w6, [x4,2080]
	ldr	x2, [x4,2088]
	umull	x1, w6, w5
	add	x2, x2, x1
	str	w0, [x2,4]
	ldrh	w2, [x4,2030]
	ldr	x0, [x4,2088]
	add	x1, x0, x1
	mul	w0, w2, w6
	and	x0, x0, 4294967292
	str	x0, [x29,128]
	ldr	x0, [x21,2136]
	ldr	x3, [x29,128]
	str	x0, [x29,120]
	add	x28, x0, x3
	ldrh	w0, [x4,2028]
	str	x28, [x1,16]
	str	w26, [x1,24]
	mul	w0, w0, w6
	ldr	x6, [x21,2096]
	and	x0, x0, 4294967292
	add	x0, x6, x0
	str	x0, [x1,8]
	mov	x0, x28
	mov	w1, 0
	bl	ftl_memset
	cmp	w26, w25
	ldr	x4, [x29,176]
	cset	w0, eq
	str	w0, [x29,188]
	cbnz	w0, .L2940
	ldr	x7, [x29,96]
	ldr	x5, [x29,160]
	cbz	w7, .L2909
	ldr	w0, [x29,136]
	ldrh	w2, [x4,2024]
	msub	w2, w26, w2, w0
	uxth	w0, w2
	str	w0, [x29,176]
	ldr	w0, [x29,188]
	str	w0, [x29,160]
	b	.L2912
.L2940:
	ldrh	w2, [x4,2024]
	udiv	w0, w22, w2
	msub	w0, w0, w2, w22
	str	w0, [x29,160]
	sub	w2, w2, w0
	cmp	w2, w23
	csel	w0, w2, w23, ls
	str	w0, [x29,176]
.L2912:
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w1, [x29,176]
	ldrh	w0, [x2,2024]
	cmp	w1, w0
	bne	.L2913
	ldr	w0, [x29,188]
	mov	x1, x24
	cbnz	w0, .L2914
	ldr	w0, [x29,176]
	mul	w1, w0, w26
	sub	w1, w1, w22
	ubfiz	x1, x1, 9, 23
	add	x1, x24, x1
.L2914:
	ldr	w2, [x29,204]
	mov	w0, 56
	add	x4, x20, :lo12:.LANCHOR0
	cbz	w2, .L2915
	ldr	w2, [x4,2080]
	umull	x0, w2, w0
	ldr	x2, [x4,2088]
	add	x0, x2, x0
	b	.L2957
.L2915:
	ldr	w2, [x4,2080]
	umull	x0, w2, w0
	ldr	x2, [x4,2088]
	add	x0, x2, x0
	ldr	x0, [x0,8]
	b	.L2958
.L2913:
	ldr	w0, [x29,212]
	cmn	w0, #1
	beq	.L2917
	str	w0, [x29,220]
	mov	w1, 56
	ldr	w0, [x2,2080]
	str	w26, [x29,240]
	umull	x0, w0, w1
	ldr	x1, [x2,2088]
	mov	w2, 0
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
	bne	.L2918
	ldr	w0, [x21,424]
	add	w0, w0, 1
	str	w0, [x21,424]
	b	.L2920
.L2918:
	ldr	w0, [x28,8]
	cmp	w0, w26
	beq	.L2920
	ldr	w0, [x21,424]
	mov	w2, w26
	add	w0, w0, 1
	str	w0, [x21,424]
	ldr	x0, [x29,104]
	ldr	w1, [x28,8]
	bl	printk
	b	.L2920
.L2917:
	ldr	w0, [x2,2080]
	mov	w1, 56
	umull	x0, w0, w1
	ldr	x1, [x2,2088]
	ldrh	w2, [x2,2028]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L2920:
	ldr	w1, [x29,176]
	mov	w0, 56
	lsl	w2, w1, 9
	ldr	w1, [x29,188]
	cbz	w1, .L2921
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w4, [x1,2080]
	umull	x0, w4, w0
	ldr	x4, [x1,2088]
	add	x4, x4, x0
	ldr	x0, [x29,160]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x4,8]
	add	x0, x1, x0
	mov	x1, x24
	b	.L2959
.L2921:
	add	x5, x20, :lo12:.LANCHOR0
	ldr	w4, [x5,2080]
	ldr	x1, [x5,2088]
	umull	x0, w4, w0
	add	x0, x1, x0
	ldrh	w1, [x5,2024]
	mul	w1, w26, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w22
	ubfiz	x1, x1, 9, 23
	add	x1, x24, x1
	b	.L2959
.L2909:
	ldr	w0, [x29,204]
	ldrh	w1, [x4,2024]
	ldr	w2, [x4,2080]
	cbz	w0, .L2922
	mul	w1, w26, w1
	umull	x0, w2, w5
	sub	w1, w1, w22
	ldr	x2, [x4,2088]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x24, x1
.L2957:
	str	x1, [x0,8]
	b	.L2916
.L2922:
	umull	x0, w2, w5
	ldr	x2, [x4,2088]
	mul	w1, w26, w1
	add	x0, x2, x0
	sub	w1, w1, w22
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x24, x1
.L2958:
	ldrh	w2, [x4,2028]
.L2959:
	bl	ftl_memcpy
.L2916:
	ldr	x1, [x29,120]
	mov	w0, -3947
	ldr	x2, [x29,128]
	strh	w0, [x1,x2]
	add	x1, x20, :lo12:.LANCHOR0
	str	w26, [x28,8]
	ldr	w0, [x21,200]
	str	w0, [x28,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x21,200]
	ldr	w0, [x29,212]
	str	w0, [x28,12]
	ldrh	w0, [x19]
	strh	w0, [x28,2]
	ldr	w0, [x1,2080]
	add	w0, w0, 1
	str	w0, [x1,2080]
	ldr	x0, [x29,192]
	add	x0, x0, 1
	str	x0, [x29,192]
	b	.L2906
.L2961:
	str	w26, [x29,184]
	mov	x0, x2
	b	.L2907
.L2937:
	str	w26, [x29,184]
.L2907:
	sub	w27, w27, w0
	ldr	w0, [x29,204]
	cbnz	w0, .L2926
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	ldr	w0, [x21,2048]
	cmp	w1, w0
	bcs	.L2926
	ldrh	w0, [x19,4]
	cbz	w0, .L2926
.L2928:
	str	wzr, [x29,204]
	b	.L2899
.L2926:
	bl	FtlCacheWriteBack
	add	x0, x20, :lo12:.LANCHOR0
	cmp	w27, 3
	str	wzr, [x0,2080]
	bls	.L2928
	b	.L2899
.L2960:
	ldr	w1, [x29,200]
	mov	w0, w27
	sub	w1, w1, w25
	bl	rk_ftl_garbage_collect
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,-8]
	cmp	w1, 31
	bhi	.L2930
	mov	w1, 128
	mov	w19, 16
	strh	w1, [x0,866]
	strh	w1, [x0,864]
.L2931:
	mov	w0, 0
	mov	w1, 1
	bl	rk_ftl_garbage_collect
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,-96]
	cbnz	w0, .L2930
	subs	w19, w19, #1
	bne	.L2931
	b	.L2930
.L2891:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 272
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	StorageSysDataStore
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w19, w0
	str	x1, [x29,40]
	bl	rknand_device_lock
	ldr	x3, [x29,40]
	add	w1, w19, 256
	mov	w2, 1
	mov	w0, 16
	bl	FtlWrite
	str	x0, [x29,40]
	bl	rknand_device_unlock
	ldr	x19, [sp,16]
	ldr	x0, [x29,40]
	ldp	x29, x30, [sp], 48
	ret
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.type	FlashReadFacBbtData.part.13, %function
FlashReadFacBbtData.part.13:
	stp	x29, x30, [sp, -32]!
	mov	w3, 0
	adrp	x4, .LANCHOR4
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w7, 1
	mov	x19, x0
	cbnz	w1, .L2966
.L2968:
	add	x0, x4, :lo12:.LANCHOR4
	ldr	w1, [x0,92]
	cmp	w3, w1
	bcs	.L2966
	ubfx	x5, x3, 5, 11
	ldr	x0, [x0,56]
	lsl	x5, x5, 2
	lsl	w6, w7, w3
	add	w3, w3, 1
	ldr	w1, [x0,x5]
	uxth	w3, w3
	orr	w6, w1, w6
	str	w6, [x0,x5]
	b	.L2968
.L2966:
	add	x4, x4, :lo12:.LANCHOR4
	mov	x0, x19
	ldr	x1, [x4,56]
	bl	ftl_memcpy
	mov	w2, 4
	adrp	x0, .LC140
	mov	x1, x19
	add	x0, x0, :lo12:.LC140
	mov	w3, w2
	bl	rknand_print_hex
	ldr	x19, [sp,16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadFacBbtData.part.13, .-FlashReadFacBbtData.part.13
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
.L2971:
	cmp	w20, w19
	ble	.L2979
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L2972
	add	x0, x21, :lo12:.LANCHOR4
	ldr	x0, [x0,112]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L2972
	mov	w0, w22
	cbz	x22, .L2973
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.13
	b	.L2973
.L2972:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2971
.L2979:
	mov	w0, -1
.L2973:
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
	bne	.L2981
.L2985:
	mov	w1, 0
	b	.L2982
.L2981:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L2983:
	cmp	w2, w19
	bge	.L2982
	add	x3, x20, :lo12:.LANCHOR4
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,104]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L2984
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L2984:
	cmp	w1, w0
	bcs	.L2985
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2983
.L2982:
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
	cbnz	w21, .L2991
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
.L2992:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22,1966]
	cmp	w21, w0
	bcs	.L3010
	add	x1, x23, :lo12:.LANCHOR2
	ldrh	w28, [x25,12]
	ldr	x0, [x1,-64]
	ldr	x27, [x1,2120]
	mov	w1, 65535
	str	x0, [x20,8]
	cmp	w28, w1
	str	x27, [x20,16]
	beq	.L2993
	ldrh	w0, [x22,2014]
	mov	w1, 1
	mov	w2, w1
	madd	w28, w21, w0, w28
	lsl	w0, w28, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x22,2014]
	ldr	x0, [x24,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L2994
.L2993:
	mov	w1, w21
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x22,2014]
.L2996:
	sub	w22, w22, #1
	uxth	w22, w22
.L2995:
	add	x28, x19, :lo12:.LANCHOR0
	ldrh	w0, [x28,2014]
	madd	w0, w21, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2996
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
	ldrh	w0, [x28,2014]
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
	bne	.L2994
	mov	w0, w28
	bl	FtlBbmMapBadBlock
	b	.L2995
.L2994:
	mov	w0, w28
	add	w21, w21, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L2992
.L3010:
	mov	w20, 0
.L2999:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,2032]
	cmp	w0, w20
	bls	.L3011
	mov	w0, w20
	add	w20, w20, 1
	bl	FtlBbmMapBadBlock
	uxth	w20, w20
	b	.L2999
.L3011:
	ldrh	w20, [x21,2116]
	mov	w24, 65535
	sub	w20, w20, #1
	uxth	w20, w20
.L3001:
	add	x22, x21, 2104
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L3005
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L3002
	mov	w0, w20
	bl	FlashTestBlk
	cbz	w0, .L3003
	mov	w0, w20
	bl	FtlBbmMapBadBlock
	b	.L3002
.L3003:
	ldrh	w0, [x21,2104]
	cmp	w0, w24
	bne	.L3004
	strh	w20, [x21,2104]
	b	.L3002
.L3004:
	strh	w20, [x22,4]
	b	.L3005
.L3002:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L3001
.L3005:
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
.L2991:
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
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	w1, [x0,-96]
	cbnz	w1, .L3014
	adrp	x20, .LANCHOR0
	str	w1, [x0,196]
	str	w1, [x0,200]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,1948]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3015
	bl	FtlMakeBbt
.L3015:
	mov	w0, 0
.L3016:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2024]
	cmp	w0, w2, lsl 7
	bge	.L3043
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
	b	.L3016
.L3043:
	ldrh	w22, [x1,1952]
	mov	w19, 0
.L3018:
	add	x23, x20, :lo12:.LANCHOR0
	ldrh	w0, [x23,1954]
	cmp	w0, w22
	bls	.L3044
	mov	w0, w22
	mov	w1, 1
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w19, w0
	uxth	w22, w22
	uxth	w19, w0
	b	.L3018
.L3044:
	ldrh	w0, [x23,1944]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L3020
.L3024:
	mov	w19, 0
	mov	w22, w19
	b	.L3021
.L3020:
	udiv	w19, w19, w0
	ldr	w0, [x23,2052]
	add	w0, w19, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x23,1948]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x23,1952]
.L3022:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,1954]
	cmp	w0, w19
	bls	.L3024
	mov	w0, w19
	mov	w1, 1
	add	w19, w19, 1
	bl	FtlLowFormatEraseBlock
	uxth	w19, w19
	b	.L3022
.L3021:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,1952]
	cmp	w1, w22
	bls	.L3045
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w19, w0
	uxth	w22, w22
	uxth	w19, w0
	b	.L3021
.L3045:
	ldrh	w1, [x0,1954]
	add	x6, x21, :lo12:.LANCHOR2
	ldrh	w2, [x0,1944]
	str	w1, [x0,2100]
	ldr	w1, [x0,1956]
	udiv	w3, w1, w2
	str	w3, [x0,2096]
	ubfx	x5, x3, 5, 16
	add	w4, w5, 36
	strh	w4, [x6,272]
	mov	w4, 24
	mul	w4, w2, w4
	cmp	w19, w4
	ble	.L3026
	sub	w1, w1, w19
	udiv	w1, w1, w2
	str	w1, [x0,2096]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x6,272]
.L3026:
	add	x4, x21, :lo12:.LANCHOR2
	ldr	w0, [x4,160]
	cmp	w0, 1
	bne	.L3027
	ldrh	w1, [x4,272]
	udiv	w0, w19, w2
	add	w0, w1, w0
	add	w0, w1, w0, asr 2
	strh	w0, [x4,272]
.L3027:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L3028
	add	x1, x21, :lo12:.LANCHOR2
	udiv	w0, w19, w2
	ldrh	w4, [x1,272]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,272]
.L3028:
	add	x6, x20, :lo12:.LANCHOR0
	ldrh	w1, [x6,2010]
	cbz	w1, .L3030
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w4, [x0,272]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,272]
	mul	w4, w1, w2
	cmp	w4, w19
	ble	.L3030
	add	w1, w1, 32
	str	w3, [x6,2096]
	add	w1, w5, w1
	strh	w1, [x0,272]
.L3030:
	add	x19, x21, :lo12:.LANCHOR2
	add	x22, x20, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR4
	mov	w23, -1
	ldr	w1, [x22,2096]
	ldrh	w0, [x19,272]
	sub	w0, w1, w0
	add	x1, x24, :lo12:.LANCHOR4
	mul	w0, w0, w2
	str	w0, [x1,216]
	ldrh	w1, [x22,2018]
	mul	w0, w0, w1
	ldrh	w1, [x22,2024]
	str	w0, [x22,2096]
	mul	w0, w0, w1
	str	w0, [x22,2060]
	bl	FtlBbmTblFlush
	ldrh	w2, [x22,1954]
	mov	w1, 0
	ldr	x0, [x19,-40]
	lsl	w2, w2, 1
	bl	ftl_memset
	strh	wzr, [x21,#:lo12:.LANCHOR2]
	mov	w0, 1
	ldrh	w2, [x22,1952]
	strb	w0, [x19,8]
	mov	w1, 255
	ldr	x0, [x20,#:lo12:.LANCHOR0]
	lsr	w2, w2, 3
	mov	x21, x24
	str	wzr, [x19,156]
	strh	w23, [x19,296]
	strh	wzr, [x19,298]
	strb	wzr, [x19,302]
	strb	wzr, [x19,304]
	strh	wzr, [x19,2]
	strb	wzr, [x19,6]
	bl	ftl_memset
.L3032:
	mov	x0, x19
	bl	make_superblock
	ldrb	w0, [x19,7]
	cbnz	w0, .L3033
	ldrh	w1, [x19]
	ldr	x0, [x19,-40]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	add	w0, w0, 1
	strh	w0, [x19]
	b	.L3032
.L3033:
	ldr	w0, [x19,196]
	add	x20, x19, 48
	str	w0, [x19,12]
	mov	w22, -1
	add	w0, w0, 1
	ldrh	w1, [x19]
	str	w0, [x19,196]
	ldr	x0, [x19,-40]
	ldrh	w2, [x19,4]
	strh	w2, [x0,x1,lsl 1]
	strh	wzr, [x20,2]
	ldrh	w0, [x19]
	strb	wzr, [x20,6]
	add	w0, w0, 1
	strh	w0, [x19,48]
	mov	w0, 1
	strb	w0, [x20,8]
.L3034:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L3035
	ldrh	w1, [x20]
	ldr	x0, [x19,-40]
	strh	w22, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L3034
.L3035:
	ldr	w0, [x19,196]
	add	x21, x21, :lo12:.LANCHOR4
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,196]
	ldr	x0, [x19,-40]
	ldrh	w2, [x20,4]
	mov	w20, -1
	strh	w2, [x0,x1,lsl 1]
	strh	w20, [x19,96]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19,280]
	ldr	w0, [x21,216]
	strh	w0, [x19,286]
	ldr	w0, [x19,196]
	str	w0, [x19,288]
	add	w0, w0, 1
	strh	wzr, [x19,282]
	strh	w20, [x19,284]
	str	w0, [x19,196]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L3014
	adrp	x0, .LANCHOR1+504
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+504]
.L3014:
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
	ldrb	w0, [x0,120]
	cbz	w0, .L3047
.L3049:
	mov	w0, 0
	b	.L3048
.L3047:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,48]
	bl	FlashInit
	cbnz	w0, .L3049
	bl	FlashLoadFactorBbt
	cbz	w0, .L3050
	bl	FlashMakeFactorBbt
.L3050:
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20,104]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L3051
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L3053:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L3053
	cmp	w0, 6
	bhi	.L3054
	add	x0, x19, :lo12:.LANCHOR0
	b	.L3077
.L3054:
	mov	w1, 0
	mov	w4, 1
.L3057:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L3057
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L3077
	mov	w1, 36
.L3077:
	strb	w1, [x0,9]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,9]
	strh	w1, [x0,194]
.L3051:
	adrp	x0, .LC73
	adrp	x1, .LC74
	add	x1, x1, :lo12:.LC74
	add	x0, x0, :lo12:.LC73
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	add	x0, x19, 168
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,1948]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L3059:
	bl	FtlLoadBbt
	cbz	w0, .L3060
.L3079:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L3080
	add	w19, w19, 1
	b	.L3059
.L3080:
	mov	w0, -1
	b	.L3048
.L3060:
	bl	FtlSysBlkInit
	cbnz	w0, .L3079
	adrp	x1, .LANCHOR1+504
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+504]
.L3048:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	flash_boot_enter_slc_mode
	.type	flash_boot_enter_slc_mode, %function
flash_boot_enter_slc_mode:
	adrp	x1, .LANCHOR0+1868
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+1868]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3081
	bl	flash_enter_slc_mode
.L3081:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	2
	.global	flash_boot_exit_slc_mode
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	adrp	x1, .LANCHOR0+1868
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+1868]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3083
	bl	flash_exit_slc_mode
.L3083:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	2
	.global	IdBlockReadData
	.type	IdBlockReadData, %function
IdBlockReadData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	mov	x20, x2
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	mov	w22, w0
	ldr	x0, [x19,744]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w3, [x19,12]
	mov	w26, w1
	ldrb	w21, [x0,9]
	adrp	x0, .LC141
	add	x0, x0, :lo12:.LC141
	mov	w1, w22
	mov	w2, w26
	mov	w23, 0
	mul	w21, w3, w21
	bl	printk
	ldr	x0, [x19,744]
	mov	w6, 4
	uxth	w21, w21
	adrp	x7, .LANCHOR4
	ldrb	w28, [x0,9]
	udiv	w0, w22, w21
	msub	w21, w0, w21, w22
	sub	w5, w22, w21
	mul	w3, w21, w28
	ubfx	x3, x3, 2, 2
.L3086:
	cmp	w23, w26
	bcs	.L3092
	ldrb	w1, [x19,120]
	sub	w24, w6, w3
	add	w4, w23, w21
	lsr	w4, w4, 2
	uxth	w24, w24
	add	x0, x19, x4, uxth 1
	ldrh	w0, [x0,196]
	cbz	w1, .L3087
	ldr	w2, [x19,1868]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	uxth	w4, w4
	cmp	w2, w1
	csel	w0, w4, w0, eq
.L3087:
	add	w3, w3, w5
	add	x27, x7, :lo12:.LANCHOR4
	ldrb	w4, [x19,1936]
	madd	w25, w0, w28, w3
	ldr	x0, [x19,744]
	ldr	x2, [x27,104]
	str	x6, [x29,96]
	ldrb	w1, [x0,9]
	ldrb	w0, [x27,98]
	str	x5, [x29,104]
	str	x7, [x29,112]
	str	x4, [x29,136]
	str	x2, [x29,120]
	str	x1, [x29,128]
	bl	FlashBchSel
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	x1, [x29,128]
	mov	x3, 0
	ldr	x2, [x29,120]
	mov	w0, 0
	udiv	w1, w25, w1
	bl	FlashReadPage
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	ldr	x4, [x29,136]
	mov	w0, w4
	bl	FlashBchSel
	ubfiz	x0, x23, 9, 16
	ldr	x1, [x27,104]
	add	x0, x20, x0
	mov	w2, 2048
	add	w23, w24, w23
	bl	ftl_memcpy
	uxth	w23, w23
	mov	w3, 0
	ldr	x6, [x29,96]
	ldr	x5, [x29,104]
	ldr	x7, [x29,112]
	b	.L3086
.L3092:
	adrp	x0, .LC142
	mov	w1, w22
	mov	w2, w26
	mov	w3, 0
	add	x0, x0, :lo12:.LC142
	bl	printk
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	IdBlockReadData, .-IdBlockReadData
	.align	2
	.global	IDBlockWriteData
	.type	IDBlockWriteData, %function
IDBlockWriteData:
	stp	x29, x30, [sp, -192]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x27, x28, [sp,80]
	mov	w27, w1
	adrp	x28, .LANCHOR4
	ldr	x1, [x19,744]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w24, w0
	ldr	w0, [x19,12]
	ldrb	w21, [x1,9]
	mov	x22, x2
	mov	w1, w24
	mov	w2, w27
	mov	w23, 0
	mul	w21, w0, w21
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	uxth	w21, w21
	bl	printk
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldrh	w1, [x19,188]
	mov	w2, 0
	mov	w0, 0
	udiv	w1, w24, w1
	bl	FlashEraseBlock
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	udiv	w26, w24, w21
	ldr	x0, [x19,744]
	ldrb	w20, [x0,9]
	msub	w26, w26, w21, w24
	sub	w21, w24, w26
.L3094:
	cmp	w23, w27
	bcs	.L3108
	add	w4, w23, w26
	lsr	w4, w4, 2
	uxth	w0, w4
	and	w4, w4, 65535
	cbz	w0, .L3095
	add	x1, x19, 196
	add	w2, w4, 1
	ldrh	w3, [x1,w2,sxtw 1]
	ldrb	w1, [x19,120]
	cbz	w1, .L3096
	ldr	w2, [x19,1868]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3096
	add	w3, w0, 1
	uxth	w3, w3
.L3096:
	sub	w3, w3, #1
	str	wzr, [x29,132]
	lsl	w3, w3, 2
	str	w3, [x29,128]
.L3095:
	add	x1, x19, 196
	ldrh	w25, [x1,w4,sxtw 1]
	ldrb	w1, [x19,120]
	cbz	w1, .L3097
	ldr	w2, [x19,1868]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w25, w25, w0, ne
.L3097:
	add	x0, x28, :lo12:.LANCHOR4
	ldrb	w4, [x19,1936]
	madd	w3, w25, w20, w21
	ubfiz	x2, x23, 9, 16
	str	x4, [x29,120]
	ldrb	w0, [x0,98]
	add	x2, x22, x2
	str	x3, [x29,104]
	add	w23, w23, 4
	str	x2, [x29,112]
	bl	FlashBchSel
	uxth	w23, w23
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	x0, [x19,744]
	ldr	x3, [x29,104]
	ldr	x2, [x29,112]
	ldrb	w1, [x0,9]
	mov	w0, 0
	udiv	w1, w3, w1
	add	x3, x29, 128
	bl	FlashProgPage
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	ldr	x4, [x29,120]
	mov	w0, w4
	bl	FlashBchSel
	udiv	w1, w21, w20
	add	w2, w25, 1
	mov	w0, 0
	bl	FlashPageProgMsbFFData
	b	.L3094
.L3108:
	adrp	x0, .LC144
	mov	w1, w24
	mov	w2, w27
	mov	w3, 0
	add	x0, x0, :lo12:.LC144
	bl	printk
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	IDBlockWriteData, .-IDBlockWriteData
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR0
	mov	x23, x2
	stp	x25, x26, [sp,64]
	mov	w25, w0
	add	x0, x24, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	x21, x1
	ldr	w26, [x0,12]
	ldr	x1, [x0,744]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	ldrb	w22, [x1,9]
	bl	ftl_malloc
	str	x24, [x29,120]
	mov	x20, x0
	mov	w0, -1
	cbz	x20, .L3119
	add	w19, w25, 511
	lsr	w19, w19, 9
	cmp	w19, 255
	bhi	.L3111
	ubfiz	x0, x19, 9, 23
	mov	w2, 256
	add	x0, x21, x0
	mov	x1, x21
	sub	w2, w2, w19
	bl	memcpy
.L3111:
	add	w19, w19, 128
	mov	w0, 256
	cmp	w19, 256
	mov	x1, x23
	csel	w19, w19, w0, ls
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	mov	w2, 4
	mov	w3, 5
	bl	rknand_print_hex
	ldr	x0, [x29,120]
	ldr	w1, [x21,512]
	add	x27, x0, :lo12:.LANCHOR0
	adrp	x0, .LC146
	add	x0, x0, :lo12:.LC146
	ldrb	w2, [x27,9]
	bl	printk
	ldrb	w0, [x27,9]
	ldr	w1, [x21,512]
	cmp	w1, w0
	bls	.L3112
	str	w0, [x21,512]
.L3112:
	adrp	x0, .LC147
	mov	w2, w25
	mul	w22, w22, w26
	add	x0, x0, :lo12:.LC147
	mov	w1, w19
	mov	x24, 0
	adrp	x26, .LC148
	bl	printk
	adrp	x27, .LC149
	adrp	x28, .LC150
	lsl	w0, w19, 7
	uxth	w22, w22
	str	w0, [x29,116]
	mov	w25, w24
	add	x26, x26, :lo12:.LC148
	add	x27, x27, :lo12:.LC149
	add	x28, x28, :lo12:.LC150
.L3117:
	ldr	x1, [x29,120]
	mov	w8, w24
	ldr	w0, [x23,x24,lsl 2]
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w1, [x1,194]
	cmp	w0, w1
	bcs	.L3113
	adrp	x1, .LANCHOR4+92
	ldr	w1, [x1,#:lo12:.LANCHOR4+92]
	cmp	w0, w1
	bcc	.L3113
	mov	w1, 0
	mov	x2, 512
	mov	x0, x20
	str	x8, [x29,104]
	bl	memset
	ldr	w0, [x23,x24,lsl 2]
	mov	w1, w19
	mov	x2, x21
	mul	w0, w22, w0
	bl	IDBlockWriteData
	ldr	w0, [x23,x24,lsl 2]
	mov	w1, w19
	mov	x2, x20
	mul	w0, w22, w0
	bl	IdBlockReadData
	ldr	x8, [x29,104]
	mov	x0, 0
.L3114:
	ldr	w1, [x29,116]
	mov	w3, w0
	cmp	w0, w1
	bcs	.L3124
	ldr	w4, [x20,x0,lsl 2]
	mov	x7, x0
	add	x0, x0, 1
	add	x1, x21, x0, lsl 2
	ldr	w5, [x1,-4]
	cmp	w4, w5
	beq	.L3114
	ldr	w2, [x23,x24,lsl 2]
	mov	w1, w8
	mov	x0, x26
	str	x7, [x29,104]
	bl	printk
	ldr	x7, [x29,104]
	mov	x0, x27
	mov	w2, 4
	mov	w3, 256
	and	x4, x7, -256
	lsl	x4, x4, 2
	str	x4, [x29,104]
	add	x1, x21, x4
	bl	rknand_print_hex
	ldr	x4, [x29,104]
	mov	w3, 256
	mov	x0, x28
	mov	w2, 4
	add	x1, x20, x4
	bl	rknand_print_hex
	mov	x0, x20
	mov	w1, 0
	mov	x2, 512
	bl	memset
	ldr	w0, [x23,x24,lsl 2]
	mov	w1, 4
	mov	x2, x20
	mul	w0, w22, w0
	bl	IDBlockWriteData
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	bl	printk
	b	.L3113
.L3124:
	add	w25, w25, 1
.L3113:
	add	x24, x24, 1
	cmp	x24, 5
	bne	.L3117
	mov	x0, x20
	bl	ftl_free
	cmp	w25, wzr
	csetm	w0, eq
.L3119:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
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
.L3126:
	cmp	w1, w2
	bls	.L3128
	ldrb	w4, [x5,x2]
	add	x6, x3, 3016
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3126
.L3128:
	ret
	.size	CRC_32, .-CRC_32
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
	str	x21, [sp,32]
	mov	w19, w1
	mov	x20, x2
	beq	.L3131
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3132
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3133
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3134
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3135
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3136
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3193
	bl	rknand_dev_flush
	b	.L3225
.L3136:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3138
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3139
	b	.L3193
.L3134:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3140
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3141
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3142
	b	.L3193
.L3141:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3140
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3140
	b	.L3193
.L3132:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3143
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3144
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3145
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3146
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3147
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3148
	b	.L3193
.L3144:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3149
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3150
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3151
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3152
	b	.L3193
.L3150:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3152
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3149
	b	.L3193
.L3146:
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3153
.L3158:
	mov	x0, -12
	b	.L3130
.L3153:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3232
	adrp	x0, .LC154
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC154
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3228
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
	cbz	x0, .L3234
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
.L3227:
	bl	printk
.L3228:
	mov	x0, x19
.L3229:
	bl	ftl_free
.L3230:
	mov	x0, -14
	b	.L3130
.L3145:
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3158
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3232
	ldr	w1, [x19]
	adrp	x0, .LC157
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC157
	bl	printk
	adrp	x0, .LANCHOR4
	add	x21, x0, :lo12:.LANCHOR4
	mov	x20, x0
	ldr	x1, [x21,480]
	cbnz	x1, .L3160
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x21,480]
	cbz	x0, .L3228
.L3160:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3228
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3228
	add	x20, x20, :lo12:.LANCHOR4
	uxtw	x2, w2
	ldr	x1, [x20,480]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3234:
	mov	x0, x19
	bl	ftl_free
	b	.L3225
.L3148:
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3158
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3162
.L3232:
	adrp	x0, .LC153
	add	x0, x0, :lo12:.LC153
	b	.L3227
.L3162:
	ldr	w1, [x19]
	adrp	x0, .LC159
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC159
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3228
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20,480]
	cbz	x0, .L3228
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3165
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3130
.L3165:
	bl	rknand_device_lock
	ldr	x1, [x20,480]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,480]
	bl	ftl_free
	str	xzr, [x20,480]
	b	.L3234
.L3147:
	adrp	x0, .LC160
	add	x0, x0, :lo12:.LC160
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3158
	bl	ReadFlashInfo
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3236
.L3131:
	adrp	x0, .LC161
	add	x0, x0, :lo12:.LC161
	bl	printk
	bl	rknand_device_lock
	bl	FtlReInitForSDUpdata
	mov	w19, w0
	bl	rknand_device_unlock
	cbnz	w19, .L3230
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3158
	bl	rknand_device_lock
	mov	w1, w19
	mov	w2, 64
	mov	x0, x21
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	adrp	x0, .LC162
	mov	x1, x21
	add	x0, x0, :lo12:.LC162
	mov	w2, 4
	mov	w3, 8
	bl	rknand_print_hex
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3236
.L3143:
	adrp	x0, .LC163
	add	x0, x0, :lo12:.LC163
	bl	printk
	adrp	x0, .LANCHOR4+488
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR4+488]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
	b	.L3221
.L3151:
	adrp	x0, .LC164
	add	x0, x0, :lo12:.LC164
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3158
	bl	rknand_device_lock
	mov	w1, 2
	mov	x2, x21
	mov	w0, 520
	bl	FtlVendorPartRead
	bl	rknand_device_unlock
	mov	x0, x20
	mov	x1, x21
	mov	x2, 1024
.L3236:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3229
	bl	ftl_free
	b	.L3157
.L3135:
	adrp	x0, .LC165
	add	x0, x0, :lo12:.LC165
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3171
.L3176:
	adrp	x0, .LC153
	add	x0, x0, :lo12:.LC153
	bl	printk
	b	.L3230
.L3171:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3172
.L3173:
	mov	x19, -1
	b	.L3157
.L3172:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3173
	adrp	x21, .LANCHOR4
	add	x0, x29, 56
	add	x21, x21, :lo12:.LANCHOR4
	mov	x2, 512
	ldr	x1, [x21,488]
	bl	memcpy
	ldr	w1, [x21,496]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3174
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3174:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
.L3222:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
.L3221:
	bl	rk_copy_to_user
	cbnz	x0, .L3230
	b	.L3225
.L3138:
	adrp	x0, .LC166
	add	x0, x0, :lo12:.LC166
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3176
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3173
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3173
	adrp	x1, .LANCHOR4
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR4
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,496]
	cmp	w3, w2
	bne	.L3130
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L3130
	ldr	x19, [x1,488]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3226
.L3142:
	adrp	x0, .LC167
	add	x0, x0, :lo12:.LC167
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3176
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3173
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3173
	adrp	x19, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR4
	ldr	w1, [x0,500]
	cbnz	w1, .L3177
.L3180:
	mov	x0, 0
	b	.L3130
.L3177:
	ldr	x1, [x0,504]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3178
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,504]
	str	w2, [x1,4]
	ldr	x0, [x0,504]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3178:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	x1, [x20,504]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,488]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3179
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,488]
	str	w1, [x0,4]
	ldr	x0, [x20,488]
	str	wzr, [x0,8]
.L3179:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,488]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,500]
	str	wzr, [x19,496]
	b	.L3225
.L3133:
	adrp	x0, .LC168
	add	x0, x0, :lo12:.LC168
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3176
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3173
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3173
	adrp	x19, .LANCHOR4
	add	x1, x19, :lo12:.LANCHOR4
	ldr	w2, [x1,500]
	cmp	w2, 1
	beq	.L3180
	ldr	x2, [x1,504]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3181
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,504]
	str	w3, [x2,4]
	ldr	x1, [x1,504]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3181:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w0, 1
	ldr	x1, [x20,504]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,488]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3182
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,488]
	str	w1, [x0,4]
	ldr	x0, [x20,488]
	str	wzr, [x0,8]
.L3182:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,488]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,500]
	b	.L3225
.L3139:
	adrp	x0, .LC169
	add	x0, x0, :lo12:.LC169
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3176
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3173
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3173
	adrp	x1, .LANCHOR4
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR4
	uxtw	x2, w2
	add	x1, x1, 512
	b	.L3223
.L3140:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3183
	adrp	x0, .LC170
	add	x0, x0, :lo12:.LC170
	b	.L3224
.L3183:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3185
	adrp	x0, .LC171
	add	x0, x0, :lo12:.LC171
	b	.L3224
.L3185:
	adrp	x0, .LC172
	add	x0, x0, :lo12:.LC172
.L3224:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3176
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3230
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR4
	bne	.L3186
	add	x0, x0, :lo12:.LANCHOR4
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,488]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3130
	b	.L3230
.L3186:
	add	x20, x0, :lo12:.LANCHOR4
	ldr	w1, [x20,1024]
	cmp	w1, 10
	bhi	.L3230
	ldr	x1, [x20,488]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3187
	cbz	w3, .L3187
	adrp	x0, .LC173
	mov	w1, w2
	add	x0, x0, :lo12:.LC173
	bl	printk
	ldr	w0, [x20,1024]
	add	w0, w0, 1
	str	w0, [x20,1024]
	b	.L3230
.L3187:
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x0,1024]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3188
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3189
.L3188:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3189:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3225
	b	.L3157
.L3152:
	adrp	x0, .LC174
	add	x0, x0, :lo12:.LC174
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3176
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3173
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3173
	mov	w0, 30224
	adrp	x1, .LANCHOR4
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x29, 64
	bne	.L3190
	ldr	x1, [x1,1032]
	b	.L3233
.L3190:
	ldr	x1, [x1,1040]
.L3233:
	add	x1, x1, 8
.L3223:
	bl	memcpy
	b	.L3222
.L3149:
	adrp	x0, .LC175
	add	x0, x0, :lo12:.LC175
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3176
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3173
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3173
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	bne	.L3192
	ldr	x0, [x19,1032]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,1032]
	b	.L3226
.L3192:
	ldr	x0, [x19,1040]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,1040]
	mov	w0, 3
.L3226:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3157
.L3225:
	mov	x19, 0
.L3157:
	adrp	x0, .LC176
	mov	x1, x19
	add	x0, x0, :lo12:.LC176
	bl	printk
	mov	x0, x19
	b	.L3130
.L3193:
	mov	x0, -22
.L3130:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	add	sp, sp, 528
	ret
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	stp	x29, x30, [sp, -48]!
	mov	w2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	add	x19, x20, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	ldr	x1, [x19,456]
	str	x1, [x19,504]
	add	x0, x1, 512
	str	x0, [x19,488]
	add	x0, x1, 1024
	add	x1, x1, 1536
	str	x0, [x19,1032]
	add	x0, x19, 512
	str	xzr, [x19,480]
	str	x1, [x19,1040]
	bl	ftl_memcpy
	ldr	x21, [x19,504]
	str	wzr, [x19,496]
	str	wzr, [x19,1024]
	ldr	w0, [x21,16]
	ldr	w22, [x21,508]
	str	w0, [x19,500]
	cbz	w22, .L3239
	mov	x0, x21
	mov	w1, 508
	bl	JSHash
	cmp	w22, w0
	beq	.L3239
	adrp	x0, .LC177
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC177
	str	wzr, [x19,500]
	bl	printk
.L3239:
	add	x0, x20, :lo12:.LANCHOR4
	ldr	w1, [x0,500]
	cbz	w1, .L3240
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,496]
.L3240:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 2
	ldr	x1, [x20,1032]
	bl	StorageSysDataLoad
	ldr	x1, [x20,1040]
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
	stp	x29, x30, [sp, -80]!
	mov	w1, 193
	mov	x0, 65536
	add	x29, sp, 0
	movk	w1, 0x240, lsl 16
	mov	w2, 4
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR4
	bl	kmalloc_order_trace
	add	x1, x21, :lo12:.LANCHOR4
	mov	w19, -12
	str	x0, [x1,1048]
	cbz	x0, .L3249
	mov	w22, 0
	adrp	x24, .LC178
	mov	w19, w22
	mov	w23, w22
	add	x24, x24, :lo12:.LC178
.L3253:
	add	x20, x21, :lo12:.LANCHOR4
	ldr	x2, [x20,1048]
	str	x2, [x29,72]
	bl	rknand_device_lock
	ldr	x2, [x29,72]
	add	w0, w23, 2
	mov	w1, 128
	lsl	w0, w0, 7
	bl	FtlVendorPartRead
	str	x0, [x29,72]
	bl	rknand_device_unlock
	ldr	x0, [x29,72]
	cbnz	w0, .L3250
	ldr	x3, [x20,1048]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,1048]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3251
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3251
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w22, w22, w23, ne
	csel	w19, w19, w0, ne
.L3251:
	cbnz	w23, .L3261
	mov	w23, 1
	b	.L3253
.L3261:
	cbz	w19, .L3254
	bl	rknand_device_lock
	lsl	w0, w22, 7
	mov	w1, 128
	add	w0, w0, 256
	mov	x2, x20
	bl	FtlVendorPartRead
	mov	w19, w0
	bl	rknand_device_unlock
	cbz	w19, .L3249
	b	.L3250
.L3254:
	mov	x0, x20
	mov	w1, w19
	mov	x2, 65536
	bl	memset
	str	w23, [x20,4]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	str	w0, [x20]
	add	x0, x20, 61440
	str	w23, [x0,4092]
	mov	w0, -1032
	strh	w19, [x20,12]
	strh	w0, [x20,14]
	b	.L3249
.L3250:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w19, -1
	ldr	x0, [x21,1048]
	bl	kfree
	str	xzr, [x21,1048]
.L3249:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	2
	.global	rk_ftl_vendor_read
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	adrp	x3, .LANCHOR4+1048
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR4+1048]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3263
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3264:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3268
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3264
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
	b	.L3263
.L3268:
	mov	w3, -1
.L3263:
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
	adrp	x0, .LANCHOR4+1048
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR4+1048]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3270
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3271:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3286
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3271
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3272
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3270
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3273:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3287
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
	b	.L3273
.L3287:
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
	b	.L3285
.L3272:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3285
.L3286:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3270
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
.L3285:
	ldr	w0, [x19,4]
	add	x1, x19, 61440
	add	w0, w0, 1
	str	w0, [x19,4]
	str	w0, [x1,4092]
	ldrh	w0, [x19,8]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 1
	csel	w0, w0, wzr, ls
	strh	w0, [x19,8]
	bl	rknand_device_lock
	mov	w2, 128
	lsl	w1, w23, 7
	mov	x3, x19
	mov	w0, 16
	bl	FtlWrite
	bl	rknand_device_unlock
	mov	w2, 0
.L3270:
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
	stp	x29, x30, [sp, -48]!
	adrp	x0, kmalloc_caches+96
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	ldr	x0, [x0,#:lo12:kmalloc_caches+96]
	mov	w22, w1
	mov	w1, 192
	mov	x21, x2
	movk	w1, 0x240, lsl 16
	mov	x2, 4096
	stp	x19, x20, [sp,16]
	bl	kmem_cache_alloc_trace
	mov	x20, -1
	mov	x19, x0
	cbz	x0, .L3289
	mov	w1, 30209
	movk	w1, 0x4004, lsl 16
	cmp	w22, w1
	beq	.L3291
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w22, w2
	beq	.L3292
	b	.L3303
.L3291:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3303
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3294
.L3295:
	mov	x20, -1
	b	.L3290
.L3294:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3295
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3290
.L3292:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3303
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3295
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3295
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3303
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3290
.L3303:
	mov	x20, -14
.L3290:
	mov	x0, x19
	bl	kfree
.L3289:
	mov	x0, x20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
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
	.global	NandFlashParaTbl
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
	.type	__func__.19317, %object
	.size	__func__.19317, 11
__func__.19317:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.20247, %object
	.size	__func__.20247, 21
__func__.20247:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.19388, %object
	.size	__func__.19388, 8
__func__.19388:
	.string	"FtlInit"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"FlashEraseBlocks pageAddr error %x\n"
.LC1:
	.string	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\n"
.LC2:
	.string	"remove_from_free_sys_Queue %x\n"
.LC3:
	.string	"FLASH INFO:\n"
.LC4:
	.string	"FLASH ID: %x\n"
.LC5:
	.string	"Device Capacity: %d MB\n"
.LC6:
	.string	"FMWAIT: %x %x %x %x\n"
.LC7:
	.string	"FTL INFO:\n"
.LC8:
	.string	"g_MaxLpn = 0x%x\n"
.LC9:
	.string	"g_VaildLpn = 0x%x\n"
.LC10:
	.string	"read_page_count = 0x%x\n"
.LC11:
	.string	"discard_page_count = 0x%x\n"
.LC12:
	.string	"write_page_count = 0x%x\n"
.LC13:
	.string	"cache_write_count = 0x%x\n"
.LC14:
	.string	"l2p_write_count = 0x%x\n"
.LC15:
	.string	"gc_page_count = 0x%x\n"
.LC16:
	.string	"totle_write = %d MB\n"
.LC17:
	.string	"totle_read = %d MB\n"
.LC18:
	.string	"GSV = 0x%x\n"
.LC19:
	.string	"GDV = 0x%x\n"
.LC20:
	.string	"bad blk num = %d %d\n"
.LC21:
	.string	"free_superblocks = 0x%x\n"
.LC22:
	.string	"mlc_EC = 0x%x\n"
.LC23:
	.string	"slc_EC = 0x%x\n"
.LC24:
	.string	"avg_EC = 0x%x\n"
.LC25:
	.string	"sys_EC = 0x%x\n"
.LC26:
	.string	"max_EC = 0x%x\n"
.LC27:
	.string	"min_EC = 0x%x\n"
.LC28:
	.string	"PLT = 0x%x\n"
.LC29:
	.string	"POT = 0x%x\n"
.LC30:
	.string	"MaxSector = 0x%x\n"
.LC31:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC32:
	.string	"sys_blks_pp = 0x%x\n"
.LC33:
	.string	"free sysblock = 0x%x\n"
.LC34:
	.string	"data_blks_pp = 0x%x\n"
.LC35:
	.string	"data_op_blks_pp = 0x%x\n"
.LC36:
	.string	"max_data_blks = 0x%x\n"
.LC37:
	.string	"Sys.id = 0x%x\n"
.LC38:
	.string	"Bbt.id = 0x%x\n"
.LC39:
	.string	"ACT.page = 0x%x\n"
.LC40:
	.string	"ACT.plane = 0x%x\n"
.LC41:
	.string	"ACT.id = 0x%x\n"
.LC42:
	.string	"ACT.mode = 0x%x\n"
.LC43:
	.string	"ACT.a_pages = 0x%x\n"
.LC44:
	.string	"ACT VPC = 0x%x\n"
.LC45:
	.string	"BUF.page = 0x%x\n"
.LC46:
	.string	"BUF.plane = 0x%x\n"
.LC47:
	.string	"BUF.id = 0x%x\n"
.LC48:
	.string	"BUF.mode = 0x%x\n"
.LC49:
	.string	"BUF.a_pages = 0x%x\n"
.LC50:
	.string	"BUF VPC = 0x%x\n"
.LC51:
	.string	"TMP.page = 0x%x\n"
.LC52:
	.string	"TMP.plane = 0x%x\n"
.LC53:
	.string	"TMP.id = 0x%x\n"
.LC54:
	.string	"TMP.mode = 0x%x\n"
.LC55:
	.string	"TMP.a_pages = 0x%x\n"
.LC56:
	.string	"GC.page = 0x%x\n"
.LC57:
	.string	"GC.plane = 0x%x\n"
.LC58:
	.string	"GC.id = 0x%x\n"
.LC59:
	.string	"GC.mode = 0x%x\n"
.LC60:
	.string	"GC.a_pages = 0x%x\n"
.LC61:
	.string	"WR_CHK = 0x%x %x %x %x\n"
.LC62:
	.string	"Read Err Cnt = 0x%x\n"
.LC63:
	.string	"Prog Err Cnt = 0x%x\n"
.LC64:
	.string	"gc_free_blk_th= 0x%x\n"
.LC65:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC66:
	.string	"gc_skip_write_count= 0x%x\n"
.LC67:
	.string	"gc_blk_index= 0x%x\n"
.LC68:
	.string	"free min EC= 0x%x\n"
.LC69:
	.string	"free max EC= 0x%x\n"
.LC70:
	.string	"GC__SB VPC = 0x%x\n"
.LC71:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC72:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC73:
	.string	"%s\n"
.LC74:
	.string	"FTL version: 5.0.47 20171120"
.LC75:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC76:
	.string	"FtlGcRefreshBlock  0x%x\n"
.LC77:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
.LC78:
	.string	"%s error allocating memory. return -1\n"
.LC79:
	.string	"%s 0x%x:"
.LC80:
	.string	"%x "
.LC81:
	.string	""
.LC82:
	.string	"otp error! %d"
.LC83:
	.string	"rr"
.LC84:
	.string	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\n"
.LC85:
	.string	"nandc:"
.LC86:
	.string	"%d flReg.d32=%x %x\n"
.LC87:
	.string	"sdr read ok %x ecc=%d\n"
.LC88:
	.string	"sync para %d\n"
.LC89:
	.string	"TOG mode Read error %x %x\n"
.LC90:
	.string	"read retry status %x %x %x\n"
.LC91:
	.string	"micron RR %d row=%x,count %d,status=%d\n"
.LC92:
	.string	"ECC:%d\n"
.LC93:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC94:
	.string	"FlashLoadPhyInfo fail %x!!\n"
.LC95:
	.string	"Read pageadd=%x  ecc=%x err=%x\n"
.LC96:
	.string	"data:"
.LC97:
	.string	"spare:"
.LC98:
	.string	"ReadRetry pageadd=%x ecc=%x err=%x\n"
.LC99:
	.string	"FLFB:%d %d\n"
.LC100:
	.string	"prog error: = %x\n"
.LC101:
	.string	"prog read error: = %x\n"
.LC102:
	.string	"prog read s error: = %x %x %x\n"
.LC103:
	.string	"prog read d error: = %x %x %x\n"
.LC104:
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC105:
	.string	"FlashMakeFactorBbt %d\n"
.LC106:
	.string	"bad block:%d %d\n"
.LC107:
	.string	"FMFB:%d %d\n"
.LC108:
	.string	"E:bad block:%d\n"
.LC109:
	.string	"FMFB:Save %d %d\n"
.LC110:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC111:
	.string	"FtlBbmTblFlush error:%x\n"
.LC112:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC113:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC114:
	.string	"decrement_vpc_count %x = %d\n"
.LC115:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC116:
	.string	"ftl_map_blk_gc blk info: %x %x %x\n"
.LC117:
	.string	"page map lost: %x %x\n"
.LC118:
	.string	"FtlMapWritePage error = %x\n"
.LC119:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC120:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC121:
	.string	"page map lost %x %x %x %x\n"
.LC122:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC123:
	.string	"no ect"
.LC124:
	.string	"slc mode"
.LC125:
	.string	"RSB refresh addr %x\n"
.LC126:
	.string	"spuer block %x vpn is 0\n "
.LC127:
	.string	"g_recovery_ppa %x ver %x\n "
.LC128:
	.string	"...%s enter...\n"
.LC129:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC130:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC131:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC132:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC133:
	.string	"GC des block %x done\n"
.LC134:
	.string	"...%s: no bad block mapping table, format device\n"
.LC135:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
.LC136:
	.string	"FtlInit %x\n"
.LC137:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC138:
	.string	"erase power lost blk = %x vpc=%x\n"
.LC139:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC140:
	.string	"BBT:"
.LC141:
	.string	"IdBlockReadData %x %x\n"
.LC142:
	.string	"IdBlockReadData %x %x ret= %x\n"
.LC143:
	.string	"IDBlockWriteData %x %x\n"
.LC144:
	.string	"IDBlockWriteData %x %x ret= %x\n"
.LC145:
	.string	"idblk:"
.LC146:
	.string	"idb reverse %x %x\n"
.LC147:
	.string	"write_idblock totle_sec %x %x\n"
.LC148:
	.string	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x\n"
.LC149:
	.string	"write"
.LC150:
	.string	"read"
.LC151:
	.string	"write_idblock error\n"
.LC152:
	.string	"READ_SECTOR_IO\n"
.LC153:
	.string	"rk_copy_from_user error\n"
.LC154:
	.string	"READ_SECTOR_IO %x %x\n"
.LC155:
	.string	"rk_copy_to_user error\n"
.LC156:
	.string	"WRITE_SECTOR_IO\n"
.LC157:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC158:
	.string	"END_WRITE_SECTOR_IO\n"
.LC159:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC160:
	.string	"GET_FLASH_INFO_IO\n"
.LC161:
	.string	"GET_BAD_BLOCK_IO\n"
.LC162:
	.string	"bbt:"
.LC163:
	.string	"GET_LOCK_FLAG_IO\n"
.LC164:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC165:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC166:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC167:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC168:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC169:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC170:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC171:
	.string	"RKNAND_LOADER_STATUS\n"
.LC172:
	.string	"RKNAND_LOADER_LOCK\n"
.LC173:
	.string	"LockKey not match %d\n"
.LC174:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC175:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC176:
	.string	"return ret = %lx\n"
.LC177:
	.string	"secureBootEn check error\n"
.LC178:
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
	.size	NandFlashParaTbl, 2304
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
	.hword	1497
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
	.hword	1497
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
	.zero	7
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
	.type	slcPageToMlcPageTbl, %object
	.size	slcPageToMlcPageTbl, 512
slcPageToMlcPageTbl:
	.zero	512
	.type	DieAddrs, %object
	.size	DieAddrs, 32
DieAddrs:
	.zero	32
	.type	FlashWaitBusyScheduleEn, %object
	.size	FlashWaitBusyScheduleEn, 4
FlashWaitBusyScheduleEn:
	.zero	4
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 8
gpNandParaInfo:
	.zero	8
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
	.zero	1
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.zero	1
	.zero	6
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
	.type	gNandFlashEccBits, %object
	.size	gNandFlashEccBits, 1
gNandFlashEccBits:
	.zero	1
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.zero	1
	.zero	2
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
	.zero	4
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
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
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
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 8
p_sys_data_buf:
	.zero	8
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
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
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
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.zero	48
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.zero	2
	.zero	6
	.type	gSysInfo, %object
	.size	gSysInfo, 12
gSysInfo:
	.zero	12
	.zero	4
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
	.type	g_gc_bad_block_temp_num, %object
	.size	g_gc_bad_block_temp_num, 2
g_gc_bad_block_temp_num:
	.zero	2
	.zero	2
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
	.type	g_gc_refresh_block_temp_num, %object
	.size	g_gc_refresh_block_temp_num, 2
g_gc_refresh_block_temp_num:
	.zero	2
	.zero	2
	.type	c_wr_page_buf_num, %object
	.size	c_wr_page_buf_num, 4
c_wr_page_buf_num:
	.zero	4
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
	.type	gMultiPageReadEn, %object
	.size	gMultiPageReadEn, 1
gMultiPageReadEn:
	.zero	1
	.zero	7
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
