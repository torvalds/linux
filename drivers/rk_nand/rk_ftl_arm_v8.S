/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2017-10-16
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
	.global	NandcFlashCsDebug
	.type	NandcFlashCsDebug, %function
NandcFlashCsDebug:
	adrp	x1, .LANCHOR0+24
	uxtb	w0, w0
	ldr	x2, [x1,#:lo12:.LANCHOR0+24]
	ldr	w1, [x2]
	bfi	w1, w0, 0, 8
	str	w1, [x2]
	ret
	.size	NandcFlashCsDebug, .-NandcFlashCsDebug
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
.L82:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L82
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
.L86:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,753]
	cmp	w0, w20
	bls	.L92
	add	x0, x19, 256
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	NandcDelayns
	ldrb	w0, [x25,752]
	cmp	w0, 34
	bne	.L87
	ldrsb	w0, [x24,x20]
	b	.L91
.L87:
	cmp	w0, 35
	bne	.L89
	ldrsb	w0, [x21,x20]
	b	.L91
.L89:
	ldrsb	w0, [x23,400]
.L91:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L86
.L92:
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
.L94:
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,753]
	cmp	w0, w20
	bls	.L96
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
	b	.L94
.L96:
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
	bne	.L98
	add	x19, x19, x25, lsl 6
	lsl	w4, w21, 2
	add	x19, x19, 20
	b	.L103
.L98:
	cmp	w0, 7
	bne	.L100
	mov	x2, 160
	mov	w4, 10
	madd	x19, x25, x2, x19
	mul	w4, w21, w4
	add	x19, x19, 28
.L103:
	add	x19, x19, x4, sxtw
	b	.L99
.L100:
	lsl	x4, x25, 3
	add	x4, x4, x21, sxtw
	add	x19, x19, x4, lsl 3
	add	x19, x19, 20
.L99:
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
.L101:
	cmp	x24, x27
	beq	.L104
	ldrb	w0, [x22,x24]
	str	w0, [x23,2052]
	mov	w0, 200
	bl	NandcDelayns
	ldrsb	w0, [x19,x24]
	add	x24, x24, 1
	str	w0, [x23,2048]
	b	.L101
.L104:
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
	bhi	.L105
	add	x21, x19, 1620
	add	x22, x19, 764
.L110:
	lsl	x1, x20, 3
	uxtb	w0, w20
	ldrb	w1, [x1,x21]
	cmp	w1, 173
	bne	.L107
	ldrb	w1, [x19,761]
	mov	x2, x22
	mov	w3, 0
	bl	HynixSetRRPara
.L107:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L110
.L105:
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
	bne	.L113
	add	x4, x4, 88
	cbnz	w2, .L114
	ldrb	w2, [x4,13]
	b	.L122
.L114:
	ldrb	w2, [x4,14]
.L122:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,103]
	cbz	w4, .L117
	add	x3, x19, 8
	lsl	x3, x3, 8
.L121:
	cmp	w2, w4
	bcs	.L117
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L121
.L113:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L117:
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
.L127:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L127
	tbz	x3, 6, .L127
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
	cbz	x0, .L132
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
	cbz	x1, .L132
	str	w0, [x1]
	str	xzr, [x19,16]
.L132:
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
.L139:
	mov	w0, 100
	bl	NandcDelayns
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L140
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
.L155:
	ldrb	w3, [x6,x4]
	ldr	x1, [x7]
	cmp	w3, 152
	ldrb	w2, [x7,8]
	beq	.L145
	cmp	w3, 69
	beq	.L145
	cmp	w3, 173
	beq	.L145
	cmp	w3, 44
	bne	.L146
.L145:
	cmp	w0, 1
	bne	.L148
	cbz	w11, .L146
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L149
	str	w0, [x1,2052]
	b	.L166
.L149:
	cmp	w3, 44
	bne	.L151
	str	w0, [x1,2052]
	str	w14, [x1,2048]
	b	.L153
.L151:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L153
.L148:
	cbz	w5, .L146
	ubfiz	x2, x2, 8, 8
	cmp	w3, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L152
	str	w10, [x1,2052]
	str	w13, [x1,2048]
	b	.L153
.L152:
	cmp	w3, 44
	bne	.L154
	str	w10, [x1,2052]
	str	w12, [x1,2048]
	b	.L153
.L154:
	str	w9, [x1,2052]
.L166:
	str	wzr, [x1,2048]
.L153:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L146:
	add	x6, x6, 8
	add	x7, x7, 16
	cmp	x6, 32
	bne	.L155
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
.L170:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,753]
	cmp	w4, w2
	bls	.L175
	ldrb	w3, [x3,752]
	cmp	w3, 67
	bne	.L171
	ldrsb	w3, [x0,x2]
	b	.L174
.L171:
	ldrsb	w3, [x1,x2]
.L174:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L170
.L175:
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
.L177:
	cmp	w22, w24
	beq	.L186
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,1845]
	cmp	w2, w0
	bcc	.L178
	mov	w0, -1
	str	w0, [x20]
	b	.L179
.L178:
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
	tbz	x0, 0, .L180
	mov	w0, -1
	str	w0, [x20]
.L180:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L181
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L181:
	mov	w0, w19
	bl	NandcFlashDeCs
.L179:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L177
.L186:
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
	bne	.L188
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
	b	.L189
.L188:
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
.L189:
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
	cbz	w1, .L191
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L191
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1856]
.L191:
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
	ble	.L200
	ldr	x0, [x1,128]
	mov	w1, 8354
	b	.L207
.L200:
	cmp	w0, 220
	ble	.L202
	ldr	x0, [x1,128]
	b	.L208
.L202:
	cmp	w0, 185
	ble	.L203
	ldr	x0, [x1,128]
	mov	w1, 4226
	b	.L207
.L203:
	cmp	w0, 160
	ldr	x0, [x1,128]
	ble	.L204
	mov	w1, 4194
	b	.L207
.L204:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L207
	cmp	w19, 99
	mov	w1, 4225
	bls	.L207
.L208:
	mov	w1, 8322
.L207:
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
	bls	.L210
	mov	w1, 8322
	cmp	w0, w1
	bne	.L211
.L210:
	adrp	x1, .LANCHOR0+128
	ldr	x1, [x1,#:lo12:.LANCHOR0+128]
	str	w0, [x1,4]
.L211:
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
	bne	.L215
.L218:
	and	w1, w1, -17
	b	.L216
.L215:
	cmp	w0, 24
	bne	.L217
	orr	w1, w1, 16
	b	.L216
.L217:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L218
.L216:
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
.L225:
	lsl	x0, x21, 3
	ldrb	w0, [x0,x19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L224
	mov	w0, w21
	bl	FlashReset
.L224:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L225
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,1856]
	cbz	w0, .L226
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,1844]
	bl	NandcSetMode
	ldr	w0, [x19,152]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L226:
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
	tbnz	x0, 9, .L236
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L235
.L236:
	mov	x0, x19
	mov	w1, 1
	bl	NandCIrqDisable
.L235:
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
	cbz	w2, .L239
	mov	w1, w23
	bl	FlashEraseSLc2KBlocks
	b	.L240
.L239:
	mov	w28, 56
	add	x25, x19, 1652
	mov	x26, 24
.L265:
	cmp	w20, w23
	bcs	.L266
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
	bcc	.L241
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L242
.L241:
	ldrb	w1, [x19,1937]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w22, w22, wzr, ne
	madd	x1, x1, x26, x25
	ldr	x1, [x1,8]
	cbz	x1, .L244
	bl	FlashWaitCmdDone
.L244:
	ldr	w0, [x29,124]
	ldr	w1, [x29,120]
	madd	x2, x0, x26, x25
	str	x21, [x2,8]
	str	xzr, [x2,16]
	str	w1, [x2,4]
	cbz	w22, .L245
	add	w1, w20, 1
	umull	x1, w1, w28
	add	x1, x24, x1
	str	x1, [x2,16]
.L245:
	add	x1, x19, x0
	mul	x0, x0, x26
	ldrb	w21, [x1,1848]
	strb	w21, [x25,x0]
	mov	w0, w21
	bl	NandcFlashCs
	cmp	w27, 1
	bne	.L246
	ldrb	w0, [x19,120]
	cbz	w0, .L246
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L247
.L246:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L247:
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
.L242:
	add	w20, w20, 1
	b	.L265
.L266:
	ldr	x0, [x29,104]
	mov	x20, 0
	mov	x22, 24
	add	x19, x0, :lo12:.LANCHOR0
	add	x21, x19, 1652
	ldr	x0, [x19,128]
	bl	NandcIqrWaitFlashReady
.L249:
	ldrb	w0, [x19,1845]
	cmp	w0, w20
	bls	.L267
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w27, 1
	bne	.L250
	ldrb	w0, [x19,120]
	cbz	w0, .L250
	mul	x0, x20, x22
	ldrb	w0, [x0,x21]
	bl	flash_exit_slc_mode
.L250:
	add	x20, x20, 1
	b	.L249
.L267:
	mov	w0, 0
.L240:
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
.L271:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L271
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
	bls	.L275
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w8, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w8, .L289
	cbz	x22, .L276
.L289:
	cbnz	w24, .L278
.L286:
	mov	x0, x21
	bl	rk_nandc_xfer_irq_flag_init
	mov	x0, x21
	mov	w1, 0
	bl	NandCIrqEnable
	add	w2, w25, 1
	mov	x0, x22
	asr	w2, w2, 1
	bfi	w20, w2, 22, 6
	cbnz	x22, .L280
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1880]
	b	.L280
.L278:
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
.L282:
	cmp	w4, w2
	bcs	.L286
	lsr	w0, w3, 2
	ubfiz	x0, x0, 2, 30
	cbz	w8, .L283
	ldrh	w7, [x5,2]
	ldr	x10, [x1,1888]
	ldrh	w11, [x5],4
	orr	w7, w11, w7, lsl 16
	str	w7, [x10,x0]
	b	.L284
.L283:
	ldr	x7, [x1,1888]
	str	w9, [x7,x0]
.L284:
	add	w4, w4, 1
	add	w3, w3, w6
	b	.L282
.L280:
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
	cbnz	x22, .L287
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L287:
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
.L276:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L275:
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
.L294:
	cmp	w2, w0
	bhi	.L296
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L294
.L296:
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
.L301:
	add	x5, x0, 1976
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L301
	ldrh	w3, [x23,20]
	ldrb	w0, [x23,15]
	cmp	w3, w0
	bcs	.L302
	uxtb	w8, w1
	mov	w3, 0
	mul	w11, w1, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x19, :lo12:.LANCHOR0
.L303:
	cmp	w3, w1
	bcs	.L305
	uxtb	w0, w3
	mov	w5, w3
	mov	w7, 0
.L306:
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
	b	.L306
.L328:
	add	w3, w3, 1
	b	.L303
.L305:
	add	x0, x19, :lo12:.LANCHOR0
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x0,1966]
	strh	w4, [x0,1954]
.L302:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x0,2008]
	strh	wzr, [x0,2010]
	bne	.L307
	strh	w6, [x0,2008]
.L307:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 4352
	ldrb	w26, [x0,8]
	strh	w2, [x0,2012]
	cbz	w26, .L308
	mov	w2, 384
	strh	w2, [x0,2012]
.L308:
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
	bls	.L309
	and	w0, w20, 255
	strh	w0, [x21,2010]
.L309:
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
	cbz	w26, .L312
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 640
	strh	w1, [x0,2012]
.L312:
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
	add	x19, x19, :lo12:.LANCHOR0
	str	w20, [x1,1940]
	ldrh	w0, [x19,1940]
	bl	FtlSysBlkNumInit
	str	wzr, [x19,2060]
	ldr	w0, [x19,1940]
	str	w0, [x19,2052]
	ldr	w0, [x19,1956]
	ldrh	w1, [x19,2018]
	lsl	w0, w0, 2
	mul	w1, w0, w1
	ldrh	w0, [x19,2026]
	add	w0, w0, 9
	lsr	w0, w1, w0
	ldrh	w1, [x19,2024]
	add	w0, w0, 2
	strh	w0, [x19,2056]
	mov	w0, 2048
	sdiv	w0, w0, w1
	ldrh	w1, [x19,2040]
	strh	w0, [x19,2058]
	add	w0, w1, 3
	strh	w0, [x19,2040]
	ldr	w0, [x19,2048]
	add	w2, w0, 3
	str	w2, [x19,2048]
	ldrb	w2, [x19,120]
	cbz	w2, .L315
	add	w1, w1, 4
	add	w0, w0, 5
	strh	w1, [x19,2040]
	str	w0, [x19,2048]
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
	ldrh	w2, [x1,2064]
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
	mov	w0, w22
	add	x20, x20, :lo12:.LANCHOR2
	bl	P2V_block_in_plane
	uxth	w21, w0
	lsl	w1, w22, 10
	ldr	x0, [x20,-96]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,-96]
	bl	FlashEraseBlocks
	ubfiz	x1, x21, 1, 16
	ldr	x2, [x20,-88]
	ldrh	w0, [x2,x1]
	add	w0, w0, 1
	strh	w0, [x2,x1]
	ldr	w0, [x20,-80]
	add	w0, w0, 1
	str	w0, [x20,-80]
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
	ldr	x3, [x1,-72]
	uxth	w19, w19
	ldr	x1, [x1,-88]
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
	ldr	x3, [x1,-72]
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
	ldr	x3, [x3,-72]
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
	.global	IsInFreeQueue
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	adrp	x1, .LANCHOR0
	uxth	w6, w0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 2200
	ldrh	w4, [x1,6]
	cmp	w4, 1024
	beq	.L395
	ldrh	w5, [x1,2]
	mov	w3, w0
.L396:
	cmp	w3, w4
	bcs	.L400
	add	w2, w3, w5
	ubfiz	x2, x2, 1, 10
	add	x2, x1, x2
	ldrh	w0, [x2,8]
	cmp	w0, w6
	beq	.L399
	add	w3, w3, 1
	b	.L396
.L400:
	mov	w0, 0
	b	.L395
.L399:
	mov	w0, 1
.L395:
	ret
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x2, x0, 2200
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	w20, 65535
	ldrh	w3, [x0,2206]
	cbz	w3, .L402
	ldrh	w1, [x0,2202]
	add	x4, x2, x1, sxtw 1
	sub	w3, w3, #1
	strh	w3, [x0,2206]
	add	w1, w1, 1
	ldrh	w20, [x4,8]
	and	w1, w1, 1023
	strh	w1, [x0,2202]
	ldrb	w0, [x0,120]
	cbz	w0, .L402
	adrp	x19, .LANCHOR2
	mov	w0, w20
	add	x19, x19, :lo12:.LANCHOR2
	bl	P2V_block_in_plane
	uxth	w21, w0
	lsl	w1, w20, 10
	ldr	x0, [x19,-96]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x19,-96]
	bl	FlashEraseBlocks
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x19,-88]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x19,-80]
	add	w0, w0, 1
	str	w0, [x19,-80]
.L402:
	mov	w0, w20
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
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
	bls	.L410
	adrp	x2, .LANCHOR2
	mov	w7, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x10, w0, w7
	mov	x3, x2
	ldr	x11, [x5,-64]
	add	x4, x11, x10
	strh	w1, [x4,2]
	strh	w1, [x11,x10]
	ldr	x1, [x5,-56]
	cbnz	x1, .L411
	str	x4, [x5,-56]
	b	.L410
.L411:
	ubfiz	x8, x0, 1, 16
	ldr	x13, [x5,-48]
	ldrh	w2, [x4,4]
	mov	x14, -6148914691236517206
	ldr	x12, [x5,-64]
	mov	w18, 65535
	ldrh	w6, [x13,x8]
	cmp	w2, wzr
	ldrh	w17, [x9,1952]
	mul	w6, w6, w2
	sub	x2, x1, x12
	asr	x2, x2, 1
	csinv	w6, w6, wzr, ne
	madd	x2, x14, x2, x2
	ldr	x14, [x5,-88]
	mov	w5, 0
	add	x9, x14, x8
	uxth	w2, w2
	mov	w8, w7
.L419:
	add	w5, w5, 1
	cmp	w0, w2
	uxth	w5, w5
	beq	.L410
	cmp	w5, w17
	bhi	.L410
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w7, [x13,x15]
	mul	w7, w7, w16
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L415
	ldrh	w15, [x14,x15]
	ldrh	w7, [x9]
	cmp	w15, w7
	bcc	.L417
	b	.L416
.L415:
	bhi	.L416
.L417:
	ldrh	w7, [x1]
	cmp	w7, w18
	bne	.L418
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x4, [x2,-40]
	b	.L410
.L418:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x12, x1
	b	.L419
.L416:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,-56]
	cmp	x1, x3
	bne	.L420
	strh	w0, [x1,2]
	str	x4, [x2,-56]
	b	.L410
.L420:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-64]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L410:
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
	ldrh	w1, [x0,-32]
	add	w1, w1, 1
	strh	w1, [x0,-32]
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
	beq	.L425
	adrp	x2, .LANCHOR2
	mov	w6, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x8, w0, w6
	mov	x3, x2
	ldr	x9, [x5,-64]
	add	x4, x9, x8
	strh	w1, [x4,2]
	strh	w1, [x9,x8]
	ldr	x1, [x5,-24]
	cbnz	x1, .L426
	str	x4, [x5,-24]
	b	.L425
.L426:
	ldr	x11, [x5,-88]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,-64]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L429:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L427
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L428
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L425
.L428:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L429
.L427:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,-24]
	cmp	x1, x3
	bne	.L430
	strh	w0, [x1,2]
	str	x4, [x2,-24]
	b	.L425
.L430:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-64]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L425:
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
	ldrh	w1, [x0,-16]
	add	w1, w1, 1
	strh	w1, [x0,-16]
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
	ldr	x2, [x6,-64]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L433
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L434
	str	xzr, [x0]
	b	.L435
.L434:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L435
.L433:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L436
	cmp	w3, w0
	beq	.L435
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L435
.L436:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,-64]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L435:
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
	cbz	x2, .L438
	adrp	x3, .LANCHOR2-64
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2-64]
.L439:
	cbnz	w1, .L440
.L442:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L438
.L440:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L442
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L439
.L438:
	mov	w0, w19
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_pop_head_node
	.type	List_pop_head_node, %function
List_pop_head_node:
	stp	x29, x30, [sp, -16]!
	mov	w1, 0
	add	x29, sp, 0
	bl	List_pop_index_node
	ldp	x29, x30, [sp], 16
	ret
	.size	List_pop_head_node, .-List_pop_head_node
	.align	2
	.global	List_get_gc_head_node
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	adrp	x2, .LANCHOR2
	uxth	w3, w0
	add	x2, x2, :lo12:.LANCHOR2
	mov	w0, 65535
	ldr	x1, [x2,-56]
	cbz	x1, .L451
	ldr	x4, [x2,-64]
	mov	w5, 6
.L447:
	cbz	w3, .L448
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L451
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L447
.L448:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L451:
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
	ldrh	w1, [x0,-8]
	cmp	w1, w20
	beq	.L453
	ldrh	w1, [x0,40]
	cmp	w1, w20
	beq	.L453
	ldrh	w1, [x0,88]
	cmp	w1, w20
	beq	.L453
	mov	w3, 6
	ldr	x4, [x0,-64]
	ldr	x2, [x0,-56]
	umull	x3, w20, w3
	add	x1, x4, x3
	cmp	x1, x2
	beq	.L453
	ldr	x5, [x0,-48]
	ubfiz	x0, x20, 1, 16
	ldrh	w2, [x1,4]
	ldrh	w1, [x1,2]
	ldrh	w0, [x5,x0]
	mul	w2, w0, w2
	mov	w0, 65535
	cmp	w2, wzr
	csinv	w2, w2, wzr, ne
	cmp	w1, w0
	bne	.L455
	ldrh	w0, [x4,x3]
	cmp	w0, w1
	beq	.L453
.L455:
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
	bcs	.L453
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, w20
	sub	x0, x19, #56
	bl	List_remove_node
	ldrh	w0, [x19,-32]
	sub	w0, w0, #1
	strh	w0, [x19,-32]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L453:
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
.L458:
	cmp	w20, w2
	beq	.L461
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L459
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	uxth	w1, w0
	strh	w0, [x22]
	cbz	w1, .L461
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L461
.L459:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L458
.L461:
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
	ldr	x1, [x1,136]
.L465:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L481
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L465
	b	.L466
.L481:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L468:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L482
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L477
	cmp	w5, w7
	bcc	.L469
.L477:
	mov	w5, w7
	mov	w6, w0
.L469:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L468
.L482:
	cmp	w0, w2
	bcc	.L466
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,144]
	mov	w0, w2
.L472:
	cmp	w3, w2
	beq	.L466
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L473
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L473:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L472
.L466:
	ret
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3,146]
	cbnz	w0, .L484
	cmp	w2, 4
	bhi	.L484
	add	w2, w2, 1
	strh	w2, [x3,146]
	b	.L483
.L484:
	adrp	x2, .LANCHOR0+1952
	add	x0, x1, :lo12:.LANCHOR2
	mov	w7, 65535
	ldrh	w5, [x2,#:lo12:.LANCHOR0+1952]
	strh	wzr, [x0,146]
	str	wzr, [x0,148]
	ldr	x6, [x0,-48]
	mov	x0, 0
.L485:
	cmp	w5, w0, uxth
	bls	.L483
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L486
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,148]
	add	w2, w4, w2
	str	w2, [x3,148]
.L486:
	add	x0, x0, 1
	b	.L485
.L483:
	ret
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	stp	x29, x30, [sp, -16]!
	uxth	w0, w0
	add	x29, sp, 0
	cbz	w1, .L490
	bl	ftl_set_blk_mode.part.8
	b	.L489
.L490:
	adrp	x1, .LANCHOR0
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L489:
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
.L494:
	cmp	w2, w4
	bcs	.L500
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L495
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L495:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L494
.L500:
	add	x3, x3, :lo12:.LANCHOR0
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,2018]
	mov	x3, x0
.L497:
	cmp	x3, x4
	beq	.L501
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L498
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L498:
	add	x3, x3, 2
	b	.L497
.L501:
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
.L503:
	cmp	w24, w21, uxth
	bls	.L514
	add	x0, x23, 1976
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L504
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L504:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L503
.L514:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w1, [x1,2018]
	mul	w0, w0, w1
	strh	w0, [x19,4]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w1, [x0,152]
	cbz	w1, .L506
	ldrh	w1, [x19]
	ldr	x0, [x0,-88]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 59
	bhi	.L506
	mov	w0, 1
	strb	w0, [x19,9]
.L506:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,120]
	cbz	w0, .L507
	mov	w0, 1
	strb	w0, [x19,9]
.L507:
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
.L516:
	cmp	w23, w22, uxth
	bls	.L523
	add	x0, x20, 1976
	mov	w1, w21
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L517
	add	w19, w19, w24
	uxth	w19, w19
.L517:
	add	x22, x22, 1
	b	.L516
.L523:
	cbz	w19, .L519
	mov	w0, 32768
	sdiv	w19, w0, w19
.L519:
	mov	w0, 6
	umull	x21, w21, w0
	adrp	x0, .LANCHOR2-64
	ldr	x0, [x0,#:lo12:.LANCHOR2-64]
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
	ldr	x1, [x2,-24]
	mov	w0, w1
	cbz	x1, .L525
	ldr	x0, [x2,-64]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,-88]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L525:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR2
	uxth	w4, w0
	add	x5, x2, :lo12:.LANCHOR2
	ldr	x1, [x5,-24]
	mov	w0, w1
	cbz	x1, .L528
	ldrh	w3, [x5,-16]
	mov	w0, 7
	mov	w6, 6
	mov	w7, 65535
	mul	w3, w3, w0
	ldr	x0, [x5,-64]
	asr	w3, w3, 3
	cmp	w4, w3
	sub	x1, x1, x0
	csel	w4, w3, w4, gt
	asr	x1, x1, 1
	mov	x3, -6148914691236517206
	madd	x1, x3, x1, x1
	mov	w3, 0
	uxth	w1, w1
.L530:
	cmp	w3, w4
	beq	.L533
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L531
.L533:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,-88]
	ldrh	w0, [x0,x1]
	b	.L528
.L531:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L530
.L528:
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC2
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR0
	add	x1, x1, :lo12:.LC2
	add	x25, x25, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x22, x0
	bl	strcpy
	add	x20, x22, 12
	ldr	w2, [x25,168]
	adrp	x1, .LC3
	mov	x0, x20
	add	x1, x1, :lo12:.LC3
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC4
	ldr	w2, [x25,2036]
	mov	x0, x20
	add	x1, x1, :lo12:.LC4
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0,#:lo12:.LANCHOR1+504]
	cmp	w0, 1
	beq	.L536
	sub	w0, w20, w22
	b	.L537
.L536:
	add	x0, x29, 96
	add	x1, x29, 100
	add	x2, x29, 104
	add	x3, x29, 108
	adrp	x24, .LANCHOR2
	bl	NandcGetTimeCfg
	add	x23, x24, :lo12:.LANCHOR2
	ldr	w4, [x29,104]
	adrp	x1, .LC5
	ldr	w5, [x29,108]
	add	x1, x1, :lo12:.LC5
	ldr	w3, [x29,100]
	mov	x0, x20
	ldr	w2, [x29,96]
	bl	sprintf
	add	x21, x20, x0, sxtw
	adrp	x1, .LC6
	mov	x0, x21
	add	x1, x1, :lo12:.LC6
	add	x21, x21, 10
	bl	strcpy
	ldr	w2, [x25,2096]
	adrp	x1, .LC7
	mov	x0, x21
	add	x1, x1, :lo12:.LC7
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,148]
	adrp	x1, .LC8
	mov	x0, x21
	add	x1, x1, :lo12:.LC8
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,156]
	adrp	x1, .LC9
	mov	x0, x21
	add	x1, x1, :lo12:.LC9
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,160]
	adrp	x1, .LC10
	mov	x0, x21
	add	x1, x1, :lo12:.LC10
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,164]
	adrp	x1, .LC11
	mov	x0, x21
	add	x1, x1, :lo12:.LC11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,168]
	adrp	x1, .LC12
	mov	x0, x21
	add	x1, x1, :lo12:.LC12
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,172]
	adrp	x1, .LC13
	mov	x0, x21
	add	x1, x1, :lo12:.LC13
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,176]
	adrp	x1, .LC14
	mov	x0, x21
	add	x1, x1, :lo12:.LC14
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,180]
	adrp	x1, .LC15
	mov	x0, x21
	add	x1, x1, :lo12:.LC15
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,184]
	adrp	x1, .LC16
	mov	x0, x21
	add	x1, x1, :lo12:.LC16
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,188]
	adrp	x1, .LC17
	mov	x0, x21
	add	x1, x1, :lo12:.LC17
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,192]
	adrp	x1, .LC18
	add	x1, x1, :lo12:.LC18
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x25,2110]
	adrp	x1, .LC19
	mov	x0, x21
	add	x1, x1, :lo12:.LC19
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,-16]
	adrp	x1, .LC20
	mov	x0, x21
	add	x1, x1, :lo12:.LC20
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,196]
	adrp	x1, .LC21
	mov	x0, x21
	add	x1, x1, :lo12:.LC21
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,200]
	adrp	x1, .LC22
	mov	x0, x21
	add	x1, x1, :lo12:.LC22
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,204]
	adrp	x1, .LC23
	mov	x0, x21
	add	x1, x1, :lo12:.LC23
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,-80]
	adrp	x1, .LC24
	mov	x0, x21
	add	x1, x1, :lo12:.LC24
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,208]
	adrp	x1, .LC25
	mov	x0, x21
	add	x1, x1, :lo12:.LC25
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x23,212]
	adrp	x1, .LC26
	mov	x0, x21
	add	x1, x1, :lo12:.LC26
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,246]
	adrp	x1, .LC27
	mov	x0, x21
	add	x1, x1, :lo12:.LC27
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,244]
	adrp	x1, .LC28
	mov	x0, x21
	add	x1, x1, :lo12:.LC28
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,2060]
	adrp	x1, .LC29
	mov	x0, x21
	add	x1, x1, :lo12:.LC29
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,2052]
	adrp	x1, .LC30
	mov	x0, x21
	add	x1, x1, :lo12:.LC30
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,1940]
	adrp	x1, .LC31
	mov	x0, x21
	add	x1, x1, :lo12:.LC31
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,2206]
	adrp	x1, .LC32
	mov	x0, x21
	add	x1, x1, :lo12:.LC32
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,1952]
	adrp	x1, .LC33
	mov	x0, x21
	add	x1, x1, :lo12:.LC33
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,264]
	adrp	x1, .LC34
	mov	x0, x21
	add	x1, x1, :lo12:.LC34
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,1956]
	adrp	x1, .LC35
	mov	x0, x21
	add	x1, x1, :lo12:.LC35
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,272]
	adrp	x1, .LC36
	mov	x0, x21
	add	x1, x1, :lo12:.LC36
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,2104]
	adrp	x1, .LC37
	mov	x0, x21
	add	x1, x1, :lo12:.LC37
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,-6]
	adrp	x1, .LC38
	mov	x0, x21
	add	x1, x1, :lo12:.LC38
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,-2]
	adrp	x1, .LC39
	mov	x0, x21
	add	x1, x1, :lo12:.LC39
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,-8]
	adrp	x1, .LC40
	mov	x0, x21
	add	x1, x1, :lo12:.LC40
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23]
	adrp	x1, .LC41
	mov	x0, x21
	add	x1, x1, :lo12:.LC41
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,-4]
	adrp	x1, .LC42
	mov	x0, x21
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x23,-8]
	adrp	x1, .LC43
	ldr	x2, [x23,-48]
	mov	x0, x21
	add	x1, x1, :lo12:.LC43
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,42]
	adrp	x1, .LC44
	mov	x0, x21
	add	x1, x1, :lo12:.LC44
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,46]
	adrp	x1, .LC45
	mov	x0, x21
	add	x1, x1, :lo12:.LC45
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,40]
	adrp	x1, .LC46
	mov	x0, x21
	add	x1, x1, :lo12:.LC46
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,48]
	adrp	x1, .LC47
	mov	x0, x21
	add	x1, x1, :lo12:.LC47
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,44]
	adrp	x1, .LC48
	mov	x0, x21
	add	x1, x1, :lo12:.LC48
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x23,40]
	adrp	x1, .LC49
	ldr	x2, [x23,-48]
	mov	x0, x21
	add	x1, x1, :lo12:.LC49
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,90]
	adrp	x1, .LC50
	mov	x0, x21
	add	x1, x1, :lo12:.LC50
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,94]
	adrp	x1, .LC51
	mov	x0, x21
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,88]
	adrp	x1, .LC52
	mov	x0, x21
	add	x1, x1, :lo12:.LC52
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,96]
	adrp	x1, .LC53
	mov	x0, x21
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,92]
	adrp	x1, .LC54
	mov	x0, x21
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,290]
	adrp	x1, .LC55
	mov	x0, x21
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,294]
	adrp	x1, .LC56
	mov	x0, x21
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,288]
	adrp	x1, .LC57
	mov	x0, x21
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x23,296]
	adrp	x1, .LC58
	mov	x0, x21
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x23,292]
	adrp	x1, .LC59
	mov	x0, x21
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x23,336]
	adrp	x1, .LC60
	ldr	w2, [x23,152]
	add	x1, x1, :lo12:.LC60
	ldr	w4, [x23,428]
	mov	x0, x21
	ldr	w5, [x23,420]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x23,424]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x23,416]
	adrp	x1, .LC61
	mov	x0, x19
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x23,440]
	adrp	x1, .LC62
	mov	x0, x19
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,856]
	adrp	x1, .LC63
	mov	x0, x19
	add	x1, x1, :lo12:.LC63
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,858]
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x23,860]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x23,864]
	adrp	x1, .LC66
	add	x1, x1, :lo12:.LC66
	mov	x0, x19
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	uxth	w2, w0
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x23,-16]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x23,288]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L538
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x23,-48]
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L538:
	mov	w0, 0
	adrp	x23, .LC70
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x23, x23, :lo12:.LC70
.L540:
	cmp	w3, w27
	beq	.L539
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x6, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x5, [x21,-64]
	mov	x1, x23
	ldr	x4, [x21,-48]
	add	w20, w20, 1
	ldr	x7, [x21,-88]
	add	x5, x5, x25
	ldrh	w4, [x4,x6]
	ldrh	w5, [x5,4]
	ldrh	w6, [x7,x6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,-64]
	cmp	w20, 16
	ldrh	w3, [x0,x25]
	bne	.L540
.L539:
	add	x1, x24, :lo12:.LANCHOR2
	adrp	x23, .LC71
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
	add	x23, x23, :lo12:.LC71
	ldr	x0, [x1,-24]
	ldr	x3, [x1,-64]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L542:
	cmp	w3, w27
	beq	.L541
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x5, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x4, [x21,-64]
	mov	x1, x23
	ldr	x6, [x21,-88]
	add	w20, w20, 1
	add	x4, x4, x25
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,-64]
	cmp	w20, 4
	ldrh	w3, [x0,x25]
	bne	.L542
.L541:
	sub	w0, w19, w22
.L537:
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
	adrp	x1, .LC72
	adrp	x2, .LC73
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC72
	add	x2, x2, :lo12:.LC73
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
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x27, [sp,80]
	add	x0, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w2, [x0,204]
	ldr	w1, [x0,212]
	cmp	w2, w1
	bcs	.L547
	adrp	x1, .LANCHOR0
	str	wzr, [x0,196]
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x5, [x0,-88]
	mov	x0, 0
	ldrh	w3, [x2,1952]
.L548:
	cmp	w3, w0
	bls	.L589
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x0,lsl 1]
	add	x0, x0, 1
	ldr	w2, [x4,196]
	add	w2, w6, w2
	str	w2, [x4,196]
	b	.L548
.L589:
	add	x2, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x2,196]
	ldrh	w1, [x1,2008]
	udiv	w3, w0, w3
	str	w3, [x2,204]
	ldr	w3, [x2,200]
	sub	w0, w0, w3
	udiv	w0, w0, w1
	str	w0, [x2,196]
	b	.L550
.L547:
	ldr	w1, [x0,208]
	cmp	w2, w1
	bls	.L550
	add	w1, w1, 1
	adrp	x4, .LANCHOR0
	str	w1, [x0,208]
	mov	w0, 0
.L552:
	add	x1, x4, :lo12:.LANCHOR0
	ldrh	w1, [x1,1952]
	cmp	w0, w1
	bcs	.L550
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 1, 32
	add	w0, w0, 1
	ldr	x3, [x1,-88]
	ldrh	w1, [x3,x2]
	add	w1, w1, 1
	strh	w1, [x3,x2]
	b	.L552
.L550:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w22, [x0,212]
	ldr	w24, [x0,204]
	add	w1, w22, 256
	cmp	w1, w24
	bls	.L555
	ldr	w2, [x0,208]
	add	w1, w22, 768
	cmp	w1, w2
	bls	.L555
	ldr	w0, [x0,152]
	cbz	w0, .L558
	cmp	w22, 30
	bls	.L555
.L558:
	mov	w0, 65535
	b	.L557
.L555:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,-16]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w22, 64
	cmp	w6, w0
	bcs	.L569
	cmp	w22, 30
	bhi	.L558
.L569:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x3, [x0,-56]
	cbz	x3, .L558
	adrp	x2, .LANCHOR0
	mov	w23, 65535
	add	x1, x2, :lo12:.LANCHOR0
	ldr	x7, [x0,-64]
	ldr	x25, [x0,-88]
	mov	x0, -6148914691236517206
	mov	w20, w23
	mov	x26, x2
	ldrh	w9, [x1,1952]
	mov	w8, w23
	mov	w1, 0
	add	x0, x0, 1
	mov	w10, 6
.L560:
	ldrh	w5, [x3]
	cmp	w5, w8
	beq	.L562
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L558
	ldrh	w2, [x3,4]
	cbz	w2, .L561
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w3, w4
	and	x4, x4, 65535
	ldrh	w2, [x25,x4,lsl 1]
	cmp	w2, w22
	bls	.L568
	cmp	w2, w23
	bcs	.L561
	mov	w23, w2
	mov	w20, w3
.L561:
	umull	x3, w5, w10
	add	x3, x7, x3
	b	.L560
.L568:
	mov	w20, w3
.L562:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L558
	ubfiz	x27, x20, 1, 16
	ldrh	w21, [x25,x27]
	cmp	w21, w22
	bls	.L564
	str	x6, [x29,104]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,104]
	cmp	w22, w0, uxth
	bcs	.L564
	add	x0, x19, :lo12:.LANCHOR2
	str	w23, [x0,212]
.L564:
	cmp	w21, 29
	bhi	.L565
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,152]
	cbz	w0, .L565
	add	w0, w21, 10
	cmp	w0, w24
	bls	.L566
	add	x1, x26, :lo12:.LANCHOR0
	ldrh	w0, [x1,2008]
	cmp	w0, w21
	bls	.L565
.L566:
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,-32]
	cmp	w0, 64
	bls	.L565
	adrp	x0, .LC74
	ldrh	w2, [x25,x27]
	add	x0, x0, :lo12:.LC74
	mov	w1, w20
	bl	printk
	mov	w0, 1
	str	w0, [x22,868]
	b	.L588
.L565:
	cmp	w21, w24
	bcs	.L558
	add	w0, w21, 128
	cmp	w6, w0
	ble	.L558
	add	w0, w21, 256
	cmp	w0, w24
	bcc	.L567
	add	x0, x19, :lo12:.LANCHOR2
	add	w21, w21, 768
	ldr	w0, [x0,208]
	cmp	w21, w0
	bcs	.L558
.L567:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC75
	ldrh	w5, [x25,x27]
	add	x0, x0, :lo12:.LC75
	mov	w1, w20
	mov	w2, w24
	ldr	x4, [x19,-48]
	ldr	w3, [x19,208]
	ldrh	w4, [x4,x27]
	bl	printk
	mov	w0, 1
	str	w0, [x19,868]
.L588:
	mov	w0, w20
.L557:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 112
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
	beq	.L591
	adrp	x2, .LANCHOR2-48
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2-48]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L591:
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
	str	wzr, [x0,872]
.L593:
	add	x5, x10, :lo12:.LANCHOR0
	adrp	x9, .LANCHOR0
	ldrh	w0, [x5,1944]
	cmp	w1, w0
	bcs	.L597
	add	x2, x3, :lo12:.LANCHOR2
	umull	x4, w1, w11
	ldr	x7, [x2,880]
	ldr	x8, [x2,888]
	add	x0, x7, x4
	str	w12, [x0,16]
	ldrh	w0, [x5,2028]
	mul	w0, w1, w0
	sdiv	w0, w0, w6
	add	x0, x8, x0, sxtw 2
	str	x0, [x7,x4]
	ldrh	w0, [x5,2030]
	ldr	x8, [x2,880]
	ldr	x5, [x2,896]
	mul	w0, w1, w0
	add	x7, x8, x4
	ldr	x2, [x2,904]
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
	b	.L593
.L597:
	mov	w8, 24
	mov	w5, 4
.L595:
	add	x2, x3, :lo12:.LANCHOR2
	ldr	w1, [x2,912]
	cmp	w0, w1
	bcs	.L598
	umull	x4, w0, w8
	ldr	x7, [x2,880]
	add	x6, x9, :lo12:.LANCHOR0
	ldr	x10, [x2,888]
	add	x1, x7, x4
	str	wzr, [x1,16]
	ldrh	w1, [x6,2028]
	mul	w1, w0, w1
	sdiv	w1, w1, w5
	add	x1, x10, x1, sxtw 2
	str	x1, [x7,x4]
	ldrh	w1, [x6,2030]
	ldr	x6, [x2,880]
	ldr	x2, [x2,896]
	mul	w1, w0, w1
	add	x4, x6, x4
	add	w0, w0, 1
	sdiv	w1, w1, w5
	uxth	w0, w0
	add	x1, x2, x1, sxtw 2
	str	x1, [x4,8]
	b	.L595
.L598:
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
	ldr	w8, [x2,912]
	ldr	x5, [x2,880]
.L600:
	cmp	w3, w1
	bcs	.L599
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L605:
	cmp	w2, w8
	bcs	.L602
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L601
	str	wzr, [x10,16]
	b	.L602
.L601:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L605
.L602:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L600
.L599:
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
	ldr	w5, [x2,912]
	ldr	x6, [x2,880]
	mov	w2, 0
.L607:
	cmp	w2, w1
	bcs	.L613
	mov	w3, 0
.L611:
	cmp	w3, w5
	bcs	.L609
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L608
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L609
.L608:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L611
.L609:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L607
.L613:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR2
	uxth	w0, w0
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w2, [x1,916]
	ldr	x3, [x1,920]
	mov	x1, 0
.L615:
	cmp	w2, w1, uxth
	bls	.L619
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L615
	mov	w0, 1
	b	.L616
.L619:
	mov	w0, 0
.L616:
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
	ldrh	w7, [x4,916]
	ldr	x6, [x4,920]
.L621:
	uxth	w4, w5
	cmp	w4, w7
	bcs	.L625
	add	x5, x5, 1
	add	x9, x6, x5, lsl 1
	ldrh	w9, [x9,-2]
	cmp	w9, w8
	bne	.L621
.L625:
	cmp	w4, w7
	bne	.L623
	ubfiz	x4, x4, 1, 16
	strh	w0, [x6,x4]
	add	x0, x3, :lo12:.LANCHOR2
	ldrh	w4, [x0,916]
	add	w4, w4, 1
	strh	w4, [x0,916]
.L623:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w4, [x3,928]
	ldr	x5, [x3,936]
	umull	x4, w4, w0
	add	x5, x5, x4
	str	w1, [x5,4]
	ldr	x1, [x3,936]
	add	x0, x1, x4
	str	w2, [x0,8]
	str	w19, [x1,x4]
	ldrh	w0, [x3,928]
	add	w0, w0, 1
	strh	w0, [x3,928]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
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
	ldrh	w3, [x2,944]
	cmp	w3, w19
	beq	.L628
	ldrh	w1, [x2,946]
	cmp	w1, w19
	beq	.L628
	mov	w0, 65535
	cmp	w3, w0
	bne	.L629
	strh	w19, [x2,944]
	b	.L628
.L629:
	cmp	w1, w0
	bne	.L628
	strh	w19, [x2,946]
.L628:
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
	ldrh	w1, [x22,948]
	adrp	x0, .LC77
	mov	w2, w20
	add	x0, x0, :lo12:.LC77
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	ldr	w0, [x22,152]
	cbz	w0, .L631
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,-88]
	ldrh	w1, [x2,x0]
	cmp	w1, 29
	bls	.L631
	sub	w1, w1, #30
	strh	w1, [x2,x0]
.L631:
	add	x2, x19, :lo12:.LANCHOR2
	mov	x0, 0
	add	x2, x2, 952
	ldrh	w1, [x2,-4]
.L632:
	cmp	w1, w0, uxth
	bls	.L638
	add	x0, x0, 1
	add	x3, x2, x0, lsl 1
	ldrh	w3, [x3,-2]
	cmp	w3, w20
	bne	.L632
	b	.L633
.L638:
	cmp	w1, 15
	bhi	.L633
	add	x19, x19, :lo12:.LANCHOR2
	add	w0, w1, 1
	add	x19, x19, 952
	strh	w0, [x19,-4]
	strh	w20, [x19,w1,sxtw 1]
.L633:
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
	ldrh	w1, [x0,948]
	cbz	w1, .L640
	ldrh	w3, [x0,944]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L640
	ldrh	w2, [x0,986]
	cmp	w2, w1
	bcc	.L641
	strh	wzr, [x0,986]
.L641:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x19, 952
	ldrh	w1, [x19,986]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,986]
	add	w0, w0, 1
	strh	w0, [x19,986]
.L640:
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
	cbnz	w0, .L647
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
.L648:
	add	x2, x1, 196
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L648
.L654:
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
	b	.L649
.L647:
	cmp	w0, 1
	bne	.L650
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x3, :lo12:.LANCHOR0
.L653:
	cmp	x0, 3
	uxth	w1, w0
	mov	w2, w1
	bls	.L651
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L651:
	add	x1, x3, 196
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L653
	b	.L654
.L650:
	cmp	w0, 2
	bne	.L655
	adrp	x3, .LANCHOR0
	mov	w1, 65535
	mov	x0, 0
	add	x3, x3, :lo12:.LANCHOR0
.L657:
	add	x4, x3, 196
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L657
	b	.L654
.L655:
	cmp	w0, 3
	bne	.L658
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x3, :lo12:.LANCHOR0
.L661:
	cmp	x0, 5
	uxth	w1, w0
	mov	w2, w1
	bls	.L659
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L659:
	add	x1, x3, 196
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L661
	b	.L654
.L658:
	cmp	w0, 4
	bne	.L662
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
.L664:
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
	bne	.L664
	b	.L654
.L662:
	cmp	w0, 5
	bne	.L665
	adrp	x2, .LANCHOR0
	mov	x0, 0
	add	x2, x2, :lo12:.LANCHOR0
.L666:
	add	x1, x2, 196
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L666
	mov	x0, 0
.L667:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L667
	b	.L654
.L665:
	cmp	w0, 6
	bne	.L654
	adrp	x1, .LANCHOR0
	mov	x2, 0
	mov	w4, 12
	mov	w5, 10
	add	x1, x1, :lo12:.LANCHOR0
.L670:
	cmp	x2, 5
	uxth	w3, w2
	mov	w0, w3
	bls	.L668
	add	w0, w3, w3, lsl 1
	and	w3, w3, 1
	cmp	w3, wzr
	uxth	w0, w0
	csel	w3, w4, w5, ne
	sub	w0, w0, w3
	uxth	w0, w0
.L668:
	add	x3, x1, 196
	strh	w0, [x3,x2,lsl 1]
	add	x2, x2, 1
	cmp	x2, 256
	bne	.L670
	b	.L654
.L649:
	cmp	w20, w0, uxth
	bls	.L688
	add	x2, x1, 196
	ldrh	w2, [x2,x0,lsl 1]
	add	x0, x0, 1
	strh	w2, [x19,w2,sxtw 1]
	b	.L649
.L688:
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
	strb	wzr, [x1,2012]
	adrp	x1, .LANCHOR1+482
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x1,#:lo12:.LANCHOR1+482]
	strb	wzr, [x0,1845]
	cmp	w1, 256
	bls	.L690
	mov	w1, 512
	b	.L706
.L690:
	cmp	w1, 128
	bls	.L706
	mov	w1, 256
.L706:
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
.L694:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x26, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L693
	ldrb	w1, [x19,1845]
	str	w0, [x25,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x19, x1, sxtw
	strb	w0, [x19,1845]
	strb	w24, [x1,1848]
.L693:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L694
	add	x19, x21, :lo12:.LANCHOR0
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w1, [x19,1845]
	strb	w1, [x0,2012]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L695
.L699:
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
	strh	w0, [x22,2014]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 96
	ret
.L695:
	ldr	w26, [x19,12]
	mov	x24, 0
	add	x27, x19, 1620
	add	x25, x19, 708
.L698:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x27, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L696
	ldrb	w1, [x20,13]
	ldrb	w3, [x19,1845]
	mul	w0, w1, w26
	ldrh	w1, [x20,14]
	mov	x2, x3
	and	w1, w1, 65280
	mul	w1, w0, w1
	str	w1, [x25,w3,sxtw 2]
	ldrb	w0, [x20,23]
	cbz	w0, .L697
	lsl	w1, w1, 1
	str	w1, [x25,w3,sxtw 2]
.L697:
	add	x0, x19, x3
	add	w2, w2, 1
	strb	w2, [x19,1845]
	strb	w24, [x0,1848]
.L696:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L698
	b	.L699
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
.L708:
	cmp	w4, w0, uxtb
	bls	.L710
	add	x2, x1, 1848
	ldrb	w3, [x19,10]
	ldrb	w2, [x0,x2]
	add	x0, x0, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L708
.L710:
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
	str	w1, [x0,2024]
	mov	w1, -1
	str	wzr, [x0,2016]
	strh	w1, [x0,944]
	strh	w1, [x0,946]
	mov	w1, 32
	str	wzr, [x0,336]
	str	wzr, [x0,188]
	str	wzr, [x0,192]
	str	wzr, [x0,176]
	str	wzr, [x0,164]
	str	wzr, [x0,160]
	str	wzr, [x0,168]
	str	wzr, [x0,172]
	str	wzr, [x0,156]
	str	wzr, [x0,196]
	str	wzr, [x0,200]
	str	wzr, [x0,-80]
	str	wzr, [x0,208]
	str	wzr, [x0,212]
	str	wzr, [x0,2020]
	str	wzr, [x0,868]
	str	wzr, [x0,2028]
	str	wzr, [x0,860]
	str	wzr, [x3,2100]
	strh	w1, [x0,856]
	mov	w1, 128
	ldrh	w2, [x3,2024]
	strh	w1, [x0,858]
	mov	w1, 1024
	strh	wzr, [x0,864]
	sdiv	w2, w1, w2
	ldrh	w1, [x3,1944]
	strh	wzr, [x0,948]
	lsl	w1, w1, 2
	strh	wzr, [x0,986]
	str	w2, [x0,2032]
	cmp	w2, w1
	bls	.L712
	str	w1, [x0,2032]
.L712:
	add	x20, x22, :lo12:.LANCHOR0
	add	x19, x21, :lo12:.LANCHOR2
	mov	w24, 56
	ldrh	w0, [x20,2022]
	str	wzr, [x20,2080]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,920]
	ldrh	w1, [x20,2022]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,1944]
	str	x0, [x19,936]
	mul	w23, w23, w24
	lsl	w25, w23, 3
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,2040]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2048]
	mov	w0, w25
	bl	ftl_malloc
	str	x0, [x19,2056]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,-96]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,904]
	ldr	w0, [x19,2032]
	mul	w0, w0, w24
	bl	ftl_malloc
	str	x0, [x20,2088]
	ldrh	w0, [x20,1944]
	ldrh	w23, [x20,2028]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,912]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,-72]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2064]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2072]
	ldr	w0, [x19,912]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,888]
	ldr	w0, [x19,2032]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,2080]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2088]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2096]
	ldr	w1, [x19,912]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,1944]
	str	x0, [x19,880]
	ldrh	w0, [x20,2030]
	mul	w23, w0, w23
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2104]
	lsl	w0, w23, 3
	bl	ftl_malloc
	str	x0, [x19,2112]
	ldrh	w1, [x20,2030]
	ldr	w0, [x19,912]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,896]
	ldrh	w1, [x20,2030]
	ldr	w0, [x19,2032]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2120]
	ldrh	w0, [x20,1954]
	lsl	w0, w0, 1
	strh	w0, [x19,2128]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19,2136]
	ldrh	w0, [x19,2128]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19,2128]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w23, [x20,1954]
	str	x0, [x19,2144]
	add	x0, x0, 32
	lsl	w23, w23, 1
	str	x0, [x19,-88]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2152]
	mov	w0, w23
	bl	ftl_malloc
	ldr	w23, [x20,2048]
	str	x0, [x19,-48]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2160]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2168]
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
	str	x0, [x19,2176]
	ldrh	w0, [x20,2040]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2184]
	ldrh	w0, [x20,2042]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2192]
	ldrh	w2, [x20,2042]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x20,2056]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2200]
	ldr	w0, [x20,2048]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2208]
	ldrh	w0, [x20,2058]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,136]
	ldrh	w1, [x20,2058]
	ldrh	w0, [x20,2028]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2216]
	ldrh	w1, [x20,1954]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,-64]
	ldrh	w0, [x20,2014]
	ldrh	w1, [x20,1966]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,2224]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,2136]
	add	x1, x20, 2104
	ldrh	w4, [x19,2224]
	ldrh	w5, [x20,1966]
	mov	w0, 1
	mov	w2, w4
.L713:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L813
	ldr	x3, [x20,2136]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L713
.L813:
	add	x1, x22, :lo12:.LANCHOR0
.L715:
	cmp	w0, 8
	beq	.L814
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,2136]
	b	.L715
.L814:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,2160]
	cbnz	x1, .L717
.L719:
	adrp	x0, .LC78
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC78
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L718
.L717:
	ldr	x1, [x0,2168]
	cbz	x1, .L719
	ldr	x1, [x0,2200]
	cbz	x1, .L719
	ldr	x1, [x0,2208]
	cbz	x1, .L719
	ldr	x1, [x0,136]
	cbz	x1, .L719
	ldr	x1, [x0,2216]
	cbz	x1, .L719
	ldr	x1, [x0,-64]
	cbz	x1, .L719
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x1, [x22,2136]
	cbz	x1, .L719
	ldr	x0, [x0,-48]
	cbz	x0, .L719
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,920]
	cbz	x1, .L719
	ldr	x1, [x0,936]
	cbz	x1, .L719
	ldr	x1, [x0,2040]
	cbz	x1, .L719
	ldr	x1, [x0,2056]
	cbz	x1, .L719
	ldr	x1, [x0,-96]
	cbz	x1, .L719
	ldr	x1, [x0,904]
	cbz	x1, .L719
	ldr	x1, [x0,2048]
	cbz	x1, .L719
	ldr	x1, [x0,-72]
	cbz	x1, .L719
	ldr	x1, [x0,2064]
	cbz	x1, .L719
	ldr	x0, [x0,2072]
	cbz	x0, .L719
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,888]
	cbz	x1, .L719
	ldr	x1, [x0,2088]
	cbz	x1, .L719
	ldr	x1, [x0,2096]
	cbz	x1, .L719
	ldr	x1, [x0,880]
	cbz	x1, .L719
	ldr	x1, [x0,2104]
	cbz	x1, .L719
	ldr	x1, [x0,2112]
	cbz	x1, .L719
	ldr	x1, [x0,896]
	cbz	x1, .L719
	ldr	x1, [x0,-88]
	cbz	x1, .L719
	ldr	x0, [x0,2136]
	cbz	x0, .L719
	adrp	x0, .LANCHOR0+2072
	ldr	x0, [x0,#:lo12:.LANCHOR0+2072]
	cbz	x0, .L719
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,2176]
	cbz	x1, .L719
	ldr	x1, [x0,2184]
	cbz	x1, .L719
	ldr	x0, [x0,2192]
	cbz	x0, .L719
	mov	w0, 0
.L718:
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
	adrp	x0, .LANCHOR2+2224
	mov	x19, x1
	ldrh	w2, [x0,#:lo12:.LANCHOR2+2224]
	mov	x0, x1
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	x3, 0
	mov	w0, 65535
	mov	w5, 1
.L817:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L815
	ubfx	x4, x2, 5, 11
	add	x3, x3, 2
	lsl	x4, x4, 2
	lsl	w2, w5, w2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	bne	.L817
.L815:
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
.L823:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L836
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L824:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L837
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x25,x3]
	cmp	w4, w2
	bne	.L825
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L825:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L824
.L837:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L823
.L836:
	mov	w24, 0
	ldrh	w26, [x21]
	mov	w20, w24
	adrp	x27, .LANCHOR0
.L828:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L838
	ldrh	w0, [x19]
	uxtw	x22, w20
	cmp	w0, w20
	bne	.L829
	add	x0, x27, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,2020]
	cmp	w1, w0
	bcs	.L829
	strh	w0, [x21,x22,lsl 1]
.L829:
	lsl	x22, x22, 1
	ldrh	w23, [x21,x22]
	cmp	w26, w23
	bls	.L830
	mov	w24, w20
	mov	w26, w23
.L830:
	cbnz	w23, .L831
	ldrh	w0, [x25,x22]
	cbz	w0, .L831
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	w23, [x25,x22]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L831:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L828
.L838:
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
	ldr	x0, [x22,2168]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x21,2028]
	mov	w1, 255
	ldrh	w2, [x21,2058]
	ldr	x0, [x22,2216]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w1, 0
	mov	w5, -1
.L840:
	add	x3, x20, :lo12:.LANCHOR0
	ldrh	w0, [x3,2058]
	cmp	w0, w1
	bls	.L842
	add	x4, x19, :lo12:.LANCHOR2
	ubfiz	x0, x1, 4, 16
	ldr	x2, [x4,136]
	add	x2, x2, x0
	str	wzr, [x2,4]
	ldr	x2, [x4,136]
	strh	w5, [x2,x0]
	ldr	x2, [x4,136]
	add	x2, x2, x0
	ldrh	w0, [x3,2028]
	ldr	x3, [x4,2216]
	mul	w0, w0, w1
	add	w1, w1, 1
	sxtw	x0, w0
	uxth	w1, w1
	and	x0, x0, -4
	add	x0, x3, x0
	str	x0, [x2,8]
	b	.L840
.L842:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, -1
	add	x1, x0, 2232
	strh	w2, [x0,2234]
	strh	w2, [x0,2232]
	ldr	w2, [x3,2048]
	strh	w2, [x0,2242]
	mov	w2, -3902
	strh	w2, [x0,2236]
	ldrh	w2, [x0,2296]
	strh	w2, [x0,2240]
	ldrh	w2, [x3,2056]
	strh	w2, [x0,2238]
	ldr	x2, [x0,2160]
	str	x2, [x0,2248]
	ldr	x2, [x0,2208]
	str	x2, [x0,2256]
	ldr	x2, [x0,2168]
	str	x2, [x0,2264]
	ldr	x0, [x0,2200]
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
	strh	w0, [x19,2312]
	mov	w0, -1
	ldrh	w2, [x20,2040]
	str	w0, [x19,2324]
	ldr	x0, [x20,2072]
	lsl	w2, w2, 1
	str	xzr, [x19,2304]
	str	wzr, [x19,2316]
	str	wzr, [x19,2320]
	str	wzr, [x19,152]
	strh	wzr, [x20,2064]
	bl	ftl_memset
	ldrh	w2, [x20,1954]
	mov	w1, 0
	ldr	x0, [x19,-88]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,1954]
	mov	w1, 0
	ldr	x0, [x19,2136]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x19, 216
	mov	w1, 0
	mov	w2, 48
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 512
	add	x0, x19, 344
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
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR0
	add	x0, x24, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	adrp	x20, .LANCHOR2
	mov	w25, 6
	add	x19, x20, :lo12:.LANCHOR2
	ldrh	w2, [x0,1954]
	mov	w22, 0
	mov	w26, -1
	mov	w23, w22
	mov	w27, 32768
	ldr	x0, [x19,-64]
	mul	w2, w2, w25
	bl	ftl_memset
	str	xzr, [x19,-24]
	str	xzr, [x19,-56]
	str	xzr, [x19,-40]
	strh	wzr, [x19,-32]
	strh	wzr, [x19,-16]
	mov	w19, w22
.L845:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x0,1952]
	cmp	w1, w19
	bls	.L852
	mov	x2, 0
	ldrh	w4, [x0,1944]
	ldrh	w3, [x0,2018]
	mov	w21, w2
	mov	x28, x0
.L853:
	cmp	w4, w2, uxth
	bls	.L854
	add	x0, x28, 1976
	mov	w1, w19
	str	x3, [x29,104]
	str	x4, [x29,112]
	ldrb	w0, [x0,x2]
	str	x2, [x29,120]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x2, [x29,120]
	ldr	x4, [x29,112]
	ldr	x3, [x29,104]
	cbnz	w0, .L846
	add	w21, w21, w3
	uxth	w21, w21
.L846:
	add	x2, x2, 1
	b	.L853
.L854:
	cbz	w21, .L848
	sdiv	w21, w27, w21
	b	.L849
.L848:
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x1,-48]
	strh	w26, [x1,x0]
.L849:
	add	x0, x20, :lo12:.LANCHOR2
	umull	x1, w19, w25
	ldr	x2, [x0,-64]
	add	x1, x2, x1
	strh	w21, [x1,4]
	ldrh	w1, [x0,-8]
	cmp	w1, w19
	beq	.L850
	ldrh	w1, [x0,40]
	cmp	w1, w19
	beq	.L850
	ldrh	w1, [x0,88]
	cmp	w1, w19
	beq	.L850
	ubfiz	x1, x19, 1, 16
	ldr	x2, [x0,-48]
	ldrh	w0, [x2,x1]
	cbnz	w0, .L851
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_FREE_LIST
	b	.L850
.L851:
	add	w23, w23, 1
	mov	w0, w19
	uxth	w23, w23
	bl	INSERT_DATA_LIST
.L850:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L845
.L852:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, 0
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	strh	w23, [x20,-32]
	strh	w22, [x20,-16]
	ldp	x23, x24, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 128
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
	ldr	x0, [x19,920]
	lsl	w2, w2, 1
	strh	wzr, [x19,916]
	strh	wzr, [x19,928]
	bl	ftl_memset
	ldrh	w3, [x20,2022]
	mov	w2, 12
	ldr	x0, [x19,936]
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
	add	x1, x1, 2328
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
.L862:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x22
	bl	FlashMemCmp8
	mov	w23, w0
	cbnz	w0, .L859
	add	x0, x20, :lo12:.LANCHOR1
	ubfiz	x19, x19, 5, 32
	add	x1, x0, 512
	adds	x19, x1, x19
	beq	.L865
	ldrb	w3, [x19,22]
	mov	x2, 0
	mov	x1, x0
	b	.L864
.L859:
	add	w19, w19, 1
	add	x21, x21, 32
	cmp	w19, 72
	bne	.L862
	b	.L865
.L864:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,2816]
	cmp	w4, w3
	beq	.L863
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L864
	mov	w0, w2
.L863:
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
	b	.L860
.L865:
	mov	w23, -1
.L860:
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
	bne	.L870
	cbz	x3, .L871
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L871:
	cbz	x20, .L869
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
	b	.L869
.L870:
	cbz	x3, .L874
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L874:
	cbz	x20, .L869
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
.L869:
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
	adrp	x4, .LANCHOR4+24
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+24]
	add	x4, x4, 4096
	cbnz	w2, .L887
	add	x1, x4, x1
	b	.L889
.L887:
	add	x0, x4, x1
	mov	x1, x6
.L889:
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
	adrp	x24, .LC72
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x22, x1
	mov	x27, x0
	mov	w25, w2
	uxtw	x26, w3
	mov	w20, w19
	add	x23, x23, :lo12:.LC79
	add	x21, x21, :lo12:.LC80
	add	x24, x24, :lo12:.LC72
.L896:
	cmp	x19, x26
	beq	.L904
	cbnz	w20, .L897
	mov	x0, x23
	mov	x1, x27
	mov	w2, w19
	bl	printk
.L897:
	cmp	w25, 4
	mov	x0, x21
	bne	.L898
	ldr	w1, [x22,x19,lsl 2]
	b	.L903
.L898:
	cmp	w25, 2
	bne	.L900
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L903
.L900:
	ldrb	w1, [x22,x19]
.L903:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L901
	adrp	x1, .LC81
	mov	x0, x24
	add	x1, x1, :lo12:.LC81
	mov	w20, 0
	bl	printk
.L901:
	add	x19, x19, 1
	b	.L896
.L904:
	adrp	x0, .LC72
	adrp	x1, .LC81
	add	x1, x1, :lo12:.LC81
	add	x0, x0, :lo12:.LC72
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
	bne	.L906
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+2961
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+2961]
	b	.L961
.L906:
	cmp	w26, 3
	bne	.L908
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
	b	.L994
.L908:
	cmp	w26, 4
	bne	.L909
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
.L994:
	mov	w27, 8
	strb	w0, [x1,11]
	mov	w28, w27
	b	.L907
.L909:
	cmp	w26, 5
	bne	.L910
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w27, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L993
.L910:
	cmp	w26, 6
	bne	.L911
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w27, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L993
.L911:
	cmp	w26, 7
	bne	.L961
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
	b	.L907
.L961:
	mov	w27, 7
.L993:
	mov	w28, 4
.L907:
	sub	w0, w26, #1
	cmp	w0, 1
	bhi	.L990
	adrp	x22, .LANCHOR1
	add	x25, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	add	x26, x25, 760
	add	x4, x25, 24
	add	x22, x22, 2944
.L912:
	ldrb	w0, [x25,1845]
	cmp	w0, w21
	bls	.L919
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
.L914:
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
	bhi	.L914
	mov	x0, 0
.L915:
	add	w2, w0, 8
	mov	x1, 0
.L916:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x22, x3
	cmp	x1, 24
	ldrb	w6, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w6, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L916
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L915
	add	w21, w21, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w21, w21
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L912
.L990:
	sub	w0, w26, #3
	cmp	w0, 4
	bhi	.L919
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
.L920:
	ldrb	w0, [x21,1845]
	cmp	w0, w20
	bhi	.L960
.L919:
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
.L960:
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
	beq	.L921
	add	x1, x6, x1, lsl 6
	add	x6, x1, 20
	b	.L922
.L921:
	mov	x0, 160
	madd	x1, x1, x0, x6
	add	x6, x1, 28
.L922:
	mov	w0, 54
	cmp	w26, 4
	str	w0, [x23,2056]
	bne	.L923
	mov	w0, 255
	str	w0, [x23,2052]
	mov	w0, 64
	str	w0, [x23,2048]
	mov	w0, 204
	b	.L995
.L923:
	cmp	w25, 1
	bhi	.L925
	ldrb	w0, [x21,764]
	str	w0, [x23,2052]
	mov	w0, 82
	b	.L996
.L925:
	mov	w0, 174
	str	w0, [x23,2052]
	str	wzr, [x23,2048]
	mov	w0, 176
.L995:
	str	w0, [x23,2052]
	mov	w0, 77
.L996:
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
	bne	.L926
	mov	w0, 31
	str	w0, [x23,2052]
	b	.L927
.L926:
	str	wzr, [x23,2052]
.L927:
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
	bls	.L929
	ldr	x1, [x29,104]
	cmp	w26, 7
	mov	w7, 32
	csel	w7, w7, w1, eq
.L929:
	adrp	x0, .LANCHOR4
	mov	x8, 0
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x1, [x1,32]
.L930:
	ldr	w9, [x23,2048]
	strb	w9, [x1,x8]
	add	x8, x8, 1
	cmp	w7, w8, uxtb
	bhi	.L930
	cmp	w26, 7
	bne	.L931
	mov	w7, 0
.L933:
	ldrb	w8, [x1]
	cmp	w8, 12
	beq	.L932
	ldrb	w8, [x1,1]
	cmp	w8, 10
	beq	.L932
	add	w7, w7, 1
	add	x1, x1, 4
	uxtb	w7, w7
	cmp	w7, 8
	bne	.L933
	b	.L934
.L932:
	cmp	w7, 7
	bne	.L935
.L934:
	adrp	x0, .LC82
	mov	w1, 0
	add	x0, x0, :lo12:.LC82
	bl	printk
.L936:
	b	.L936
.L931:
	cmp	w26, 6
	bne	.L935
	mov	x7, 0
.L937:
	ldrb	w8, [x1,x7]
	cmp	w8, 12
	beq	.L935
	add	x8, x1, x7
	ldrb	w8, [x8,8]
	cmp	w8, 4
	beq	.L935
	add	x7, x7, 1
	cmp	x7, 8
	bne	.L937
	adrp	x0, .LC82
	mov	w1, 0
	add	x0, x0, :lo12:.LC82
	bl	printk
.L939:
	b	.L939
.L935:
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x10, [x1,32]
	mov	x1, 0
.L940:
	ldr	w2, [x29,128]
	cmp	w2, w1
	ble	.L997
	ldr	w7, [x23,2048]
	strb	w7, [x10,x1]
	add	x1, x1, 1
	b	.L940
.L997:
	add	x1, x0, :lo12:.LANCHOR4
	mov	w9, w22
	mov	w8, 8
	ldr	x12, [x1,32]
.L943:
	mov	w1, 0
.L942:
	add	w7, w1, w9
	add	w1, w1, 1
	sbfiz	x7, x7, 2, 32
	cmp	w1, w22
	ldr	w11, [x12,x7]
	mvn	w11, w11
	str	w11, [x12,x7]
	bne	.L942
	ldr	w1, [x29,124]
	subs	w8, w8, #1
	add	w9, w9, w1
	bne	.L943
	mov	x8, 0
	mov	w16, 1
.L944:
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x15, [x1,32]
	mov	w1, 0
	mov	w11, w1
.L948:
	lsl	w9, w16, w11
	mov	w13, w8
	mov	w7, 16
	mov	w12, 0
.L946:
	ldr	w14, [x15,w13,sxtw 2]
	add	w13, w13, w22
	and	w14, w9, w14
	cmp	w14, w9
	csinc	w12, w12, w12, ne
	subs	w7, w7, #1
	bne	.L946
	cmp	w12, 9
	orr	w9, w1, w9
	add	w11, w11, 1
	csel	w1, w9, w1, cs
	cmp	w11, 32
	bne	.L948
	str	w1, [x15,x8,lsl 2]
	add	x8, x8, 1
	cmp	w22, w8
	bgt	.L944
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x1, [x0,32]
	mov	x0, 0
	mov	w8, w0
.L951:
	ldr	w9, [x1,x0]
	add	x0, x0, 4
	cmp	w9, wzr
	csinc	w8, w8, w8, ne
	cmp	x0, 32
	bne	.L951
	cmp	w8, 7
	ble	.L952
	adrp	x0, .LC83
	mov	w2, 1
	add	x0, x0, :lo12:.LC83
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC82
	mov	w1, 0
	add	x0, x0, :lo12:.LC82
	bl	printk
.L953:
	b	.L953
.L952:
	cmp	w26, 6
	mov	w1, 4
	beq	.L954
	cmp	w26, 7
	mov	w1, 10
	mov	w0, 8
	csel	w1, w0, w1, ne
.L954:
	mov	w8, 0
.L955:
	mov	x0, 0
.L956:
	add	w9, w7, w0
	ldrb	w11, [x10,x0]
	add	x0, x0, 1
	cmp	w28, w0, uxtb
	strb	w11, [x6,w9,sxtw]
	bhi	.L956
	add	w8, w8, 1
	ldr	x0, [x29,112]
	cmp	w8, w27
	add	w7, w7, w1
	add	x10, x10, x0
	blt	.L955
	mov	w0, 255
	str	w0, [x23,2056]
	mov	w0, w24
	bl	NandcWaitFlashReady
	cmp	w25, 1
	bhi	.L958
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
	b	.L959
.L958:
	mov	w0, 56
	str	w0, [x23,2056]
.L959:
	mov	w0, w24
	add	w20, w20, 1
	bl	NandcWaitFlashReady
	uxtb	w20, w20
	b	.L920
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L998
	sub	w2, w0, #1
	cmp	w2, 6
	bhi	.L1000
	bl	HynixGetReadRetryDefault
	b	.L998
.L1000:
	cmp	w0, 49
	bne	.L1001
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
	b	.L1016
.L1001:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L1007
	cmp	w1, 33
	bne	.L1002
.L1007:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 4
	b	.L1017
.L1002:
	cmp	w1, 67
	beq	.L1008
	cmp	w1, 34
	bne	.L1004
.L1008:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 5
.L1017:
	strb	w1, [x0,761]
	mov	w1, 7
	strb	w1, [x0,762]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 764
	add	x1, x1, 256
	mov	w2, 45
	b	.L1016
.L1004:
	cmp	w1, 68
	beq	.L1009
	cmp	w1, 35
	bne	.L998
.L1009:
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
.L1016:
	bl	ftl_memcpy
.L998:
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
	bls	.L1049
	ldr	w0, [x19,16]
	tbz	x0, 2, .L1049
	mov	x0, x19
	bl	wait_for_nandc_xfer_completed
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L1020
	adrp	x22, .LC84
	adrp	x23, .LC85
	mov	w21, 0
	add	x22, x22, :lo12:.LC84
	add	x23, x23, :lo12:.LC85
.L1021:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1029
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1864]
	cmp	w0, 5
	bhi	.L1022
.L1025:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L1021
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
	b	.L1021
.L1022:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1025
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1025
.L1029:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 1880
	ldr	w0, [x19,40]
	cbz	w0, .L1030
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
	b	.L1030
.L1020:
	adrp	x22, .LC86
	adrp	x23, .LC85
	add	x22, x22, :lo12:.LC86
	add	x23, x23, :lo12:.LC85
.L1031:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L1057
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L1031
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
	b	.L1031
.L1057:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1928]
	cbz	w0, .L1034
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L1034:
	add	x21, x20, :lo12:.LANCHOR0
	add	x21, x21, 1880
	ldr	w0, [x21,40]
	cbz	w0, .L1035
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
.L1035:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1928]
	cbz	w0, .L1030
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L1030:
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x20,1920]
	b	.L1018
.L1049:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1049
.L1018:
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
	cbnz	x0, .L1059
	cbnz	x4, .L1060
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x22, x29, 112
	bl	ftl_memset
.L1060:
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
	cbnz	w21, .L1061
	add	x1, x19, :lo12:.LANCHOR0
	ubfx	x2, x24, 1, 7
	mov	w3, 128
	add	x2, x22, x2, lsl 2
	ldr	w0, [x1,1932]
	cmp	w0, 25
	mov	w0, 64
	csel	w3, w0, w3, cc
	mov	w0, w21
.L1063:
	cmp	x22, x2
	add	w4, w0, w3
	beq	.L1105
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
	b	.L1063
.L1105:
	add	x0, x19, :lo12:.LANCHOR0
	lsr	w24, w24, 2
	ldr	w5, [x0,1932]
	ldr	w4, [x0,1864]
	mov	w0, 0
	mov	w23, w0
.L1065:
	cmp	w0, w24
	bcs	.L1061
	cbz	w5, .L1061
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x20,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L1089
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L1089
	cmp	w4, 5
	bls	.L1067
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
	bls	.L1068
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L1069
.L1068:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L1104
.L1067:
	cmp	w4, 3
	bls	.L1069
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
	bls	.L1070
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L1104
.L1070:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L1104:
	orr	w3, w1, w3, lsl 5
.L1069:
	cmp	w23, w3
	csel	w23, w23, w3, cs
	b	.L1066
.L1089:
	mov	w23, -1
.L1066:
	add	w0, w0, 1
	b	.L1065
.L1061:
	str	wzr, [x20,16]
	b	.L1072
.L1059:
	cmp	w21, 1
	bne	.L1103
	mov	w23, 0
	mov	w28, 2
.L1073:
	cmp	w23, w24
	bcs	.L1106
	and	w27, w23, 3
	mov	x3, x26
	cbz	x26, .L1075
	ubfiz	x0, x23, 9, 23
	add	x3, x26, x0
.L1075:
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
	b	.L1073
.L1106:
	mov	w23, 0
	b	.L1072
.L1103:
	mov	w1, 0
	mov	x4, 0
	mov	w27, 0
	mov	w0, w25
	mov	w2, 2
	mov	w3, w1
	mov	x5, x4
	mov	w23, w27
	bl	NandcXferStart
.L1078:
	cmp	w27, w24
	bcs	.L1072
	mov	w0, w25
	add	w28, w27, 2
	bl	NandcXferComp
	ldr	w0, [x20,32]
	cmp	w28, w24
	str	w0, [x29,104]
	bcs	.L1079
	mov	x4, 0
	mov	w0, w25
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L1079:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L1094
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w23, w0
	csel	w23, w23, w0, cs
	b	.L1080
.L1094:
	mov	w23, -1
.L1080:
	and	w2, w27, 3
	mov	x3, x26
	cbz	x26, .L1081
	ubfiz	x3, x27, 9, 23
	add	x3, x26, x3
.L1081:
	cmp	x22, xzr
	mov	w0, 2
	csel	w4, w0, wzr, ne
	mov	w1, 0
	mov	x0, x20
	mul	w4, w4, w27
	mov	w27, w28
	add	x4, x22, x4
	bl	NandcCopy1KB
	b	.L1078
.L1072:
	cbnz	w21, .L1084
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19,1864]
	cmp	w0, 5
	bls	.L1084
	ldr	w0, [x20]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L1084
	orr	w0, w0, 131072
	mov	w23, -1
	str	w0, [x20]
.L1084:
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
	cbnz	w19, .L1108
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,9]
	ldr	w0, [x0,12]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L1108:
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
	cbz	w4, .L1111
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
	bne	.L1112
.L1121:
	mov	w21, -1
	b	.L1113
.L1112:
	adrp	x0, .LC87
	mov	w1, w24
	add	x0, x0, :lo12:.LC87
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L1114
	add	x19, x19, x23, sxtw 4
	ldr	x0, [x19,24]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L1114:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,40]
	add	w1, w1, 1
	str	w1, [x0,40]
	cmp	w1, 2047
	bls	.L1113
	mov	x27, 0
	str	wzr, [x0,40]
	mov	x28, x27
.L1111:
	mov	w19, 0
	mov	w26, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L1119:
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
	bhi	.L1115
	cmp	w0, 2
	bhi	.L1125
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L1125
	sub	w19, w22, w20
	mov	w21, w0
	mov	w26, 0
	b	.L1117
.L1115:
	cmp	w6, w20
	bcs	.L1126
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L1127
	mov	w6, w20
	b	.L1126
.L1125:
	mov	x27, 0
	mov	w19, w22
	mov	w21, w0
	mov	w26, 0
	mov	x28, x27
	b	.L1116
.L1126:
	mov	w20, 0
.L1116:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L1119
.L1117:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L1118
.L1127:
	mov	w19, w5
.L1118:
	cbz	w19, .L1120
	adrp	x0, .LC88
	mov	w1, w19
	add	x0, x0, :lo12:.LC88
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L1120:
	cbz	w26, .L1113
	adrp	x0, .LC89
	mov	w1, w23
	add	x0, x0, :lo12:.LC89
	mov	w2, w24
	bl	printk
	cbz	w25, .L1121
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L1113:
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
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	mov	w22, w1
	mov	w0, w21
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	mov	x24, x2
	mov	x23, x3
	bl	FlashReadRawPage
	cmn	w0, #1
	mov	w19, w0
	bne	.L1137
	adrp	x20, .LANCHOR0
	add	x25, x20, :lo12:.LANCHOR0
	ldrb	w26, [x25,16]
	cbnz	w26, .L1138
.L1140:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,1856]
	cbz	w0, .L1137
	b	.L1158
.L1138:
	mov	w0, w21
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	strb	wzr, [x25,16]
	bl	FlashReadRawPage
	strb	w26, [x25,16]
	cmn	w0, #1
	beq	.L1140
	mov	w19, w0
	b	.L1137
.L1158:
	ldr	x0, [x20,128]
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w4, 1
	ldr	w25, [x0,304]
	mov	w0, w21
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w19, w0
	beq	.L1141
	ldrb	w0, [x20,1936]
	cmp	w19, w0, lsr 1
	bls	.L1137
.L1141:
	lsr	w0, w25, 8
	bl	NandcSetDdrPara
.L1137:
	adrp	x20, .LANCHOR4
	cmn	w19, #1
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x4, [x20,48]
	bne	.L1142
	cbz	x4, .L1142
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w0, w21
	blr	x4
	mov	w19, w0
	adrp	x0, .LC90
	mov	w1, w19
	add	x0, x0, :lo12:.LC90
	mov	w2, w21
	mov	w3, w22
	bl	printk
	cmn	w19, #1
	bne	.L1142
	adrp	x0, .LANCHOR0+120
	ldrb	w0, [x0,#:lo12:.LANCHOR0+120]
	cbz	w0, .L1142
	mov	w0, w21
	bl	flash_enter_slc_mode
	ldr	x4, [x20,48]
	mov	w0, w21
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	blr	x4
	mov	w19, w0
.L1142:
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
	beq	.L1163
	cmn	w22, #1
	bne	.L1160
.L1163:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L1160
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1856]
	b	.L1162
.L1160:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,1856]
.L1162:
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
	ldr	x1, [x0,32]
	add	x21, x21, 472
	add	x25, x25, :lo12:.LANCHOR0
	str	x1, [x0,56]
	mov	w20, 0
	str	wzr, [x0,64]
	mov	w26, 4
	mov	w0, 0
	add	x28, x25, 88
	ldrh	w27, [x21,10]
	bl	flash_enter_slc_mode
.L1171:
	add	w0, w20, 1
	mov	x19, 0
	str	w0, [x29,104]
.L1173:
	add	x0, x29, 120
	ldrb	w0, [x19,x0]
	bl	FlashBchSel
	add	x5, x23, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w1, w20
	mov	x3, 0
	str	x5, [x29,96]
	ldr	x2, [x5,32]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1172
	ldr	x5, [x29,96]
	mov	w0, 0
	ldr	w1, [x29,104]
	mov	x3, 0
	ldr	x2, [x5,32]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1172
	add	x19, x19, 1
	cmp	x19, 4
	beq	.L1174
	b	.L1173
.L1175:
	add	x0, x2, 12
	mov	w1, 2036
	str	x2, [x29,104]
	mov	w24, -1
	bl	JSHash
	ldr	x2, [x29,104]
	ldr	w1, [x2,8]
	cmp	w1, w0
	beq	.L1185
.L1174:
	subs	w26, w26, #1
	add	w20, w20, w27
	bne	.L1171
	mov	w0, w26
.L1184:
	bl	flash_exit_slc_mode
	mov	w0, w24
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
.L1172:
	add	x19, x23, :lo12:.LANCHOR4
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x2, [x19,56]
	ldr	w1, [x2]
	cmp	w1, w0
	bne	.L1174
	cbnz	w24, .L1175
	add	x22, x22, :lo12:.LANCHOR1
	ldrh	w0, [x22,482]
	udiv	w20, w20, w0
	mov	w0, w24
	add	w20, w20, 1
	str	w20, [x19,68]
	b	.L1184
.L1185:
	add	x1, x2, 160
	mov	x0, x21
	mov	w2, 32
	bl	ftl_memcpy
	ldr	x1, [x19,56]
	mov	w2, 32
	mov	x0, x28
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x19,56]
	mov	w2, 852
	add	x0, x25, 760
	add	x1, x1, 224
	bl	ftl_memcpy
	str	w20, [x19,64]
	ldr	x1, [x19,56]
	ldr	w0, [x1,1076]
	strb	w0, [x25,1856]
	ldrh	w0, [x21,10]
	udiv	w0, w20, w0
	add	w2, w0, 1
	cbz	w0, .L1177
	str	w2, [x19,68]
	b	.L1178
.L1177:
	mov	w0, 2
	str	w0, [x19,68]
.L1178:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w1, [x1,14]
	mov	w24, 0
	strb	w1, [x0,72]
	b	.L1174
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
	bls	.L1187
	ldrb	w0, [x2,1856]
	cbz	w0, .L1188
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L1188:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L1187:
	ldr	x0, [x29,120]
	mov	w21, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x28, 8, 8
	str	x0, [x29,96]
.L1189:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,73]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L1214
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w21
	ldrb	w0, [x0,752]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1190
	bl	SandiskSetRRPara
	b	.L1191
.L1190:
	bl	ToshibaSetRRPara
.L1191:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,752]
	cmp	w0, 34
	bne	.L1192
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,73]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L1192
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L1192:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L1193
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
	b	.L1194
.L1193:
	ldr	w1, [x29,116]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L1194:
	cmn	w28, #1
	beq	.L1195
	add	x0, x19, :lo12:.LANCHOR0
	cmn	w25, #1
	csel	w25, w25, w28, ne
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1197
	mov	x26, 0
	mov	x27, x26
.L1195:
	add	w21, w21, 1
	b	.L1189
.L1214:
	mov	w28, w25
.L1197:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,752]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1199
	bl	SandiskSetRRPara
	b	.L1200
.L1199:
	bl	ToshibaSetRRPara
.L1200:
	ldr	x0, [x29,120]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	ldrb	w0, [x19,1936]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1201
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1201:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L1202
	mov	w0, 4
	bl	NandcSetDdrMode
.L1202:
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
.L1216:
	add	x0, x27, :lo12:.LANCHOR4
	ldrb	w0, [x0,73]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L1219
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1217
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,1936]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1222
	mov	x23, 0
	mov	x24, x23
.L1217:
	add	w26, w26, 1
	b	.L1216
.L1222:
	mov	w19, w0
.L1219:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,1936]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1221
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1221:
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
	stp	x23, x24, [sp,48]
	mov	w24, w1
	add	x1, x4, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxtb	w22, w0
	mov	x25, x2
	ldrb	w0, [x1,1936]
	mov	x23, x3
	ldrb	w1, [x1,120]
	mov	x19, x4
	lsr	w21, w0, 2
	cbnz	w1, .L1230
	add	w0, w0, w0, lsl 1
	ubfx	x21, x0, 2, 8
.L1230:
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
	ldrb	w20, [x1,8]
	lsl	x6, x20, 8
.L1231:
	add	x0, x5, :lo12:.LANCHOR4
	ldrb	w0, [x0,73]
	cmp	w27, w0
	bcs	.L1234
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
	beq	.L1232
	cmn	w19, #1
	csel	w19, w19, w0, ne
	cmp	w0, w21
	bcc	.L1239
	mov	x23, 0
	mov	x25, x23
.L1232:
	mov	w27, w4
	b	.L1231
.L1239:
	mov	w19, w0
.L1234:
	add	x20, x28, x20, lsl 8
	mov	w0, 239
	str	w0, [x20,2056]
	mov	w0, 137
	str	w0, [x20,2052]
	mov	w0, 200
	bl	NandcDelayns
	str	wzr, [x20,2048]
	str	wzr, [x20,2048]
	cmp	w19, w21
	str	wzr, [x20,2048]
	str	wzr, [x20,2048]
	bcc	.L1236
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1236:
	cmn	w19, #1
	beq	.L1240
	cmp	w19, 256
	bne	.L1237
.L1240:
	adrp	x0, .LC91
	mov	w1, w27
	add	x0, x0, :lo12:.LC91
	mov	w2, w24
	mov	w3, w27
	mov	w4, w19
	bl	printk
.L1237:
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
	bne	.L1250
	ldrb	w19, [x0,20]
.L1250:
	mov	w0, w23
	mov	w28, 0
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	add	x5, x4, 764
.L1251:
	cmp	w28, w25
	bcs	.L1255
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
	beq	.L1253
	ldrb	w1, [x4,1936]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1260
	mov	x24, 0
	mov	x26, x24
.L1253:
	add	w28, w28, 1
	b	.L1251
.L1260:
	mov	w20, w0
.L1255:
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 760
	ldr	x1, [x0,-16]
	add	x0, x0, x22
	ldrb	w1, [x1,19]
	cmp	w1, 7
	bne	.L1257
	strb	w19, [x0,20]
	b	.L1258
.L1257:
	strb	w19, [x0,12]
.L1258:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1936]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1259
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1259:
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
	cbnz	w19, .L1267
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,9]
	ldr	w2, [x0,12]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L1267
	ldrb	w0, [x0,8]
	cbnz	w0, .L1268
	sub	w21, w21, #2
	b	.L1267
.L1268:
	mov	w21, 4
.L1267:
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
	ldr	x0, [x19,32]
	str	x0, [x19,56]
	ldrb	w0, [x19,74]
	bl	FlashBchSel
	ldr	x0, [x19,32]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19,56]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	mov	w2, 32
	str	w0, [x1]
	ldr	x0, [x19,56]
	ldrb	w1, [x20,1845]
	add	x0, x0, 16
	strh	w1, [x0,-4]
	ldrb	w1, [x20,9]
	strh	w1, [x0,-2]
	ldrb	w1, [x20,1856]
	str	w1, [x0,1060]
	add	x1, x20, 1620
	bl	ftl_memcpy
	ldr	x0, [x19,56]
	add	x1, x20, 1848
	mov	w2, 8
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x19,56]
	add	x1, x20, 708
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x19,56]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 472
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x19,56]
	add	x1, x20, 88
	mov	w2, 32
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x19,56]
	add	x1, x20, 760
	mov	w2, 852
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x20, [x19,56]
	mov	w1, 2036
	add	x0, x20, 12
	bl	JSHash
	str	w0, [x20,8]
	mov	w0, 1592
	str	w0, [x20,4]
	ldr	x0, [x19,80]
	str	x0, [x19,56]
	mov	w0, 0
	mov	w19, w21
	bl	flash_enter_slc_mode
.L1272:
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
	ldr	x2, [x20,32]
	mov	w0, 0
	mul	w1, w19, w1
	bl	FlashProgPage
	ldr	w1, [x22,12]
	mov	x3, 0
	ldr	x2, [x20,32]
	mov	w0, 0
	mul	w1, w19, w1
	add	w1, w1, 1
	bl	FlashProgPage
	ldr	w1, [x22,12]
	mov	w0, 0
	ldr	x2, [x20,80]
	mov	x3, 0
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1270
	ldr	x25, [x20,56]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L1270
	mov	w1, 2036
	add	x0, x25, 12
	bl	JSHash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L1270
	ldr	w0, [x22,12]
	cmp	w21, 1
	str	w26, [x20,68]
	mul	w19, w19, w0
	str	w19, [x20,64]
	beq	.L1273
	mov	w21, 1
.L1270:
	cmp	w26, 4
	mov	w19, w26
	bne	.L1272
	b	.L1271
.L1273:
	mov	w21, 2
.L1271:
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
	bne	.L1279
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1279:
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
.L1280:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,9]
	cmp	w20, w0
	bcs	.L1284
	mov	x22, 0
.L1282:
	add	x0, x29, 120
	add	x24, x19, :lo12:.LANCHOR0
	add	x21, x28, :lo12:.LANCHOR4
	ldrb	w25, [x22,x0]
	mov	w0, w25
	bl	FlashBchSel
	ldr	w1, [x24,12]
	mov	w0, 0
	ldr	x2, [x21,32]
	mov	x3, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1281
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L1282
	b	.L1283
.L1281:
	ldr	x0, [x21,32]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L1283
	ldr	x0, [x29,104]
	mov	w1, w25
	bl	printk
	ldr	x1, [x21,32]
	mov	x0, x26
	mov	w2, 2048
	bl	ftl_memcpy
	ldr	x0, [x21,32]
	ldr	w0, [x0,512]
	strb	w0, [x24,9]
	ldr	w0, [x21,68]
	cmp	w0, w20
	bls	.L1287
	mov	w23, 0
	str	w20, [x21,68]
	bl	FlashSavePhyInfo
.L1283:
	add	w20, w20, 1
	b	.L1280
.L1287:
	mov	w23, 0
.L1284:
	mov	w0, w27
	add	x19, x19, :lo12:.LANCHOR0
	bl	FlashBchSel
	ldr	w1, [x19,1868]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L1289
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1289:
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
	str	x0, [x22,32]
	mov	w0, 32768
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x25, .LC93
	bl	ftl_malloc
	str	x0, [x22,80]
	mov	w0, 4096
	add	x24, x20, 24
	mov	w27, 0
	mov	w28, 44
	bl	ftl_malloc
	str	x0, [x22,88]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x22,96]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x22,104]
	mov	w0, 128
	str	w0, [x20,12]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x22,74]
	mov	x0, x23
	strb	wzr, [x20,1856]
	strb	w1, [x20,9]
	strb	wzr, [x20,8]
	add	x20, x20, 1620
	strb	w1, [x22,72]
	mov	x26, x20
	str	wzr, [x22,68]
	str	wzr, [x22,40]
	bl	NandcInit
	add	x0, x25, :lo12:.LC93
	str	x0, [x29,104]
.L1296:
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
	bhi	.L1292
	ldr	x0, [x29,104]
	add	w1, w27, 1
	ldrb	w3, [x20,1]
	ldrb	w4, [x20,2]
	ldrb	w5, [x20,3]
	ldrb	w6, [x20,4]
	ldrb	w7, [x20,5]
	bl	printk
.L1292:
	cbnz	w27, .L1293
	ldrb	w0, [x26]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1341
	ldrb	w0, [x26,1]
	cmp	w0, 255
	beq	.L1341
.L1293:
	ldrb	w0, [x20]
	cmp	w0, 181
	bne	.L1295
	strb	w28, [x20]
.L1295:
	add	w27, w27, 1
	add	x24, x24, 16
	cmp	w27, 4
	add	x20, x20, 8
	bne	.L1296
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 173
	beq	.L1297
	ldr	w0, [x0,1860]
	bl	NandcSetDdrMode
.L1297:
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
	cbz	w0, .L1343
	cmp	w2, 241
	bne	.L1298
.L1343:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w3, 1
	mov	w4, 16
	add	x5, x1, :lo12:.LANCHOR1
	strb	w3, [x0,8]
	add	x3, x21, :lo12:.LANCHOR4
	strb	w4, [x0,9]
	strb	w4, [x3,74]
	add	x4, x0, 1620
	ldrb	w0, [x0,1620]
	strb	w0, [x5,2977]
	cmp	w0, 152
	bne	.L1300
	ldrsb	w0, [x4,4]
	tbnz	w0, #31, .L1300
	mov	w0, 24
	strb	w0, [x3,74]
.L1300:
	cmp	w2, 218
	bne	.L1301
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,2990]
	mov	w2, -38
	b	.L1376
.L1301:
	cmp	w2, 220
	bne	.L1302
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,2990]
	mov	w2, -36
.L1376:
	strb	w2, [x0,2978]
.L1302:
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
.L1298:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,8]
	cbnz	w0, .L1303
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L1305
	ldr	x0, [x20,744]
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x20,1844]
	tbnz	x1, 0, .L1305
	mov	w1, 1
	strb	w1, [x20,1856]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
	bl	NandcSetMode
.L1305:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,744]
	ldrb	w0, [x0,26]
	strb	w0, [x20,120]
	bl	FlashLoadPhyInfo
	cbz	w0, .L1303
	ldr	w0, [x20,1860]
	cbz	w0, .L1308
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L1377
.L1308:
	ldrb	w0, [x20,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
.L1377:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L1303
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
	beq	.L1294
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
	blt	.L1310
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L1311
.L1310:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L1311:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1844]
	tst	w0, 6
	beq	.L1312
	bl	FlashSavePhyInfo
	add	x1, x21, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x1,64]
	bl	FlashDdrParaScan
.L1312:
	bl	FlashSavePhyInfo
.L1303:
	add	x20, x19, :lo12:.LANCHOR0
	add	x22, x21, :lo12:.LANCHOR4
	ldr	x2, [x20,744]
	str	xzr, [x22,48]
	ldrb	w0, [x2,26]
	strb	w0, [x20,120]
	ldrh	w0, [x2,16]
	ldrh	w3, [x2,10]
	ubfx	x1, x0, 7, 1
	strb	w1, [x20,16]
	ubfx	x1, x0, 3, 1
	strb	w1, [x22,112]
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
	tbz	x1, 6, .L1314
	ldrb	w0, [x0,19]
	ldrb	w1, [x20,762]
	strb	w1, [x22,73]
	sub	w1, w0, #1
	ldrb	w2, [x20,761]
	uxtb	w1, w1
	strb	w0, [x20,752]
	strb	w2, [x20,753]
	cmp	w1, 6
	bhi	.L1315
	adrp	x1, HynixReadRetrial
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,48]
	sub	w1, w0, #5
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L1316
	mov	w1, 1
	str	w1, [x20,1928]
	b	.L1317
.L1316:
	cmp	w0, 7
	add	x20, x20, 788
	beq	.L1318
.L1317:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 780
.L1318:
	mov	x1, 0
	mov	w2, w1
.L1320:
	ldrsb	w3, [x20,x1]
	add	x1, x1, 1
	cmp	w3, wzr
	csinc	w2, w2, w2, ne
	cmp	x1, 32
	bne	.L1320
	cmp	w2, 27
	bls	.L1314
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1314
.L1315:
	sub	w1, w0, #17
	uxtb	w1, w1
	cmp	w1, 2
	bhi	.L1322
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x22,48]
	beq	.L1323
	mov	w0, 7
	b	.L1378
.L1323:
	mov	w0, 15
.L1378:
	strb	w0, [x22,73]
	b	.L1314
.L1322:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1344
	cmp	w0, 33
	bne	.L1324
.L1344:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	mov	w2, 4
	str	x1, [x0,48]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,753]
	mov	w1, 7
	strb	w1, [x0,73]
	b	.L1314
.L1324:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w2, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1345
	cbz	w2, .L1326
.L1345:
	add	x3, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x1, [x3,48]
	beq	.L1328
	cmp	w0, 68
	beq	.L1328
	mov	w0, 7
	strb	w0, [x3,73]
	b	.L1329
.L1328:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,73]
.L1329:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w2, .L1379
	mov	w1, 5
.L1379:
	strb	w1, [x0,753]
	b	.L1314
.L1326:
	cmp	w0, 49
	bne	.L1314
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,48]
.L1314:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ldr	w2, [x0,1868]
	cmp	w2, w1
	bne	.L1331
	ldrb	w1, [x0,120]
	cbz	w1, .L1331
	ldr	x0, [x0,744]
	strb	wzr, [x0,18]
.L1331:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 44
	bne	.L1332
	ldrb	w1, [x0,1856]
	cbz	w1, .L1332
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L1333
	ldrb	w0, [x0,120]
	cbnz	w0, .L1332
.L1333:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,1856]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L1332:
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w0, [x1,1844]
	tst	w0, 6
	beq	.L1334
	ldrb	w1, [x1,1856]
	cbnz	w1, .L1335
	tbnz	x0, 0, .L1334
.L1335:
	add	x1, x21, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x1,64]
	bl	FlashDdrParaScan
.L1334:
	mov	w0, 0
	add	x20, x19, :lo12:.LANCHOR0
	bl	flash_exit_slc_mode
	ldr	x0, [x20,744]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2328
	bl	FlashReadIdbDataRaw
	cbnz	w0, .L1336
	add	x21, x21, :lo12:.LANCHOR4
	ldrb	w1, [x20,9]
	ldrb	w0, [x21,72]
	cmp	w1, w0
	bls	.L1337
	strb	w0, [x20,9]
.L1337:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,9]
	cmp	w0, 15
	bhi	.L1338
.L1336:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 16
	strb	w1, [x0,9]
.L1338:
	mov	w0, 18928
	add	x19, x19, :lo12:.LANCHOR0
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
	bne	.L1339
	lsl	w2, w2, 1
	mov	w5, 16
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,190]
	mov	w2, 8
	strb	w5, [x19,9]
	strh	w4, [x19,182]
	strh	w3, [x19,188]
	strh	w2, [x19,194]
.L1339:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	FlashSuspend
	mov	w0, 0
	b	.L1294
.L1341:
	mov	w0, -2
.L1294:
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
	cbz	w4, .L1381
	ldr	w2, [x0,1868]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w2, w0
	beq	.L1380
.L1381:
	sub	w2, w3, #5
	uxtb	w2, w2
	cmp	w2, 2
	bls	.L1382
	cmp	w3, 68
	beq	.L1382
	sub	w3, w3, #19
	and	w3, w3, -17
	uxtb	w3, w3
	cbnz	w3, .L1380
.L1382:
	adrp	x20, .LANCHOR2
	mov	w24, w1
	add	x20, x20, :lo12:.LANCHOR2
	mov	w25, 65535
	add	x20, x20, 988
	adrp	x26, .LANCHOR4
.L1384:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,744]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L1380
	ldrh	w0, [x20,w19,sxtw 1]
	cmp	w0, w25
	bne	.L1380
	add	x22, x26, :lo12:.LANCHOR4
	mov	w1, 255
	mov	w2, 32768
	ldr	x0, [x22,80]
	bl	ftl_memset
	ldr	x2, [x22,80]
	add	w1, w19, w24
	mov	w0, w21
	mov	x3, 0
	add	w19, w19, 1
	bl	FlashProgPage
	uxth	w19, w19
	b	.L1384
.L1380:
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
.L1390:
	cmp	w22, w25
	beq	.L1424
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
	bcc	.L1391
	mov	w0, -1
	str	w0, [x19]
	b	.L1392
.L1391:
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
	bls	.L1395
	cmn	w3, #1
	mov	w0, 256
	csel	w3, w3, w0, eq
.L1395:
	cmp	w3, 256
	beq	.L1406
	cmn	w3, #1
	bne	.L1396
.L1406:
	str	w3, [x19]
	b	.L1398
.L1396:
	str	wzr, [x19]
.L1398:
	ldr	x0, [x19,16]
	cbz	x0, .L1399
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L1399
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1399
	str	w1, [x19]
.L1399:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1392
	add	x5, x5, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	mov	x0, x23
	ldrb	w2, [x5,1936]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L1401
	adrp	x0, .LC96
	mov	w2, 4
	add	x0, x0, :lo12:.LC96
	mov	w3, 8
	bl	rknand_print_hex
.L1401:
	ldr	x1, [x19,16]
	cbz	x1, .L1392
	mov	w2, 4
	mov	x0, x24
	mov	w3, w2
	bl	rknand_print_hex
.L1392:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L1390
.L1424:
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
	cbz	w23, .L1465
	bl	FlashReadSlc2KPages
	b	.L1514
.L1465:
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
.L1426:
	ldr	w0, [x29,144]
	cmp	w24, w0
	bcs	.L1516
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
	bcc	.L1428
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L1429
.L1428:
	add	x0, x1, x0, uxtw
	adrp	x25, .LANCHOR4
	ldrb	w19, [x0,1848]
	add	x0, x25, :lo12:.LANCHOR4
	str	x1, [x29,152]
	ldrb	w0, [x0,112]
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
	bhi	.L1431
	add	x1, x1, 760
	sxtw	x0, w19
	add	x1, x1, x0
	cmp	w2, 7
	ldrb	w3, [x1,12]
	bne	.L1432
	ldrb	w3, [x1,20]
.L1432:
	add	x2, x22, :lo12:.LANCHOR0
	add	x0, x2, x0
	ldrb	w0, [x0,1616]
	cmp	w0, w3
	beq	.L1431
	ldrb	w1, [x2,761]
	mov	w0, w19
	add	x2, x2, 764
	bl	HynixSetRRPara
.L1431:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,140]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,152]
	cbz	w0, .L1433
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1433
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1434
.L1433:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1434:
	cmp	w19, 255
	ldr	w1, [x29,172]
	bne	.L1472
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1466
.L1472:
	cbz	w20, .L1438
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x0,12]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1439
.L1438:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1439
.L1466:
	mov	w20, w0
.L1436:
	ldrb	w2, [x29,148]
	mov	w0, w19
	ldr	x3, [x21,8]
	mov	w1, 0
	ldr	x4, [x21,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1440
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0,16]
	cbz	w1, .L1440
	strb	wzr, [x0,16]
	mov	w20, 0
	b	.L1434
.L1440:
	cbz	w20, .L1441
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
.L1441:
	mov	w0, w19
	bl	NandcFlashDeCs
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w0, [x29,136]
	cmn	w28, #1
	strb	w0, [x6,16]
	bne	.L1448
	ldrb	w0, [x6,1856]
	cbnz	w0, .L1443
.L1447:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x20, [x0,48]
	cbnz	x20, .L1444
	b	.L1517
.L1443:
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
	beq	.L1446
	ldr	x6, [x29,104]
	ldrb	w0, [x6,1936]
	cmp	w28, w0, lsr 1
	bls	.L1468
.L1446:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1447
	b	.L1468
.L1444:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L1449
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,744]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L1450
	ldrb	w1, [x2,761]
	mov	w0, w19
	add	x2, x2, 764
	mov	w3, w20
	bl	HynixSetRRPara
.L1450:
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
	bne	.L1449
	ldrb	w0, [x4,120]
	cbz	w0, .L1449
	ldr	w0, [x29,152]
	cbz	w0, .L1451
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1452
.L1451:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1452:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	mov	w20, 0
	ldr	x3, [x21,16]
	ldr	x4, [x0,48]
	mov	w0, w19
	blr	x4
	mov	w28, w0
	b	.L1449
.L1517:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1449
.L1468:
	mov	w20, 0
.L1448:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1449
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x0, [x0,48]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1449:
	cmp	w28, 256
	beq	.L1473
	cmn	w28, #1
	bne	.L1453
.L1473:
	str	w28, [x26,x27]
	b	.L1455
.L1453:
	str	wzr, [x26,x27]
.L1455:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L1457
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x21,4]
	ldr	x0, [x29,128]
	ldrb	w2, [x2,1936]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L1457
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	bl	rknand_print_hex
.L1457:
	cbz	w20, .L1459
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L1460
	add	x25, x25, :lo12:.LANCHOR4
	ldr	x0, [x25,48]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L1460:
	add	w0, w24, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L1474
	cmn	w23, #1
	bne	.L1461
.L1474:
	str	w23, [x26,x0]
	b	.L1459
.L1461:
	str	wzr, [x26,x0]
.L1459:
	ldr	w0, [x29,152]
	add	w24, w24, w20
	cbz	w0, .L1429
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1429
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1429:
	add	w24, w24, 1
	b	.L1426
.L1516:
	mov	w0, 0
	b	.L1514
.L1439:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L1436
	ldr	w1, [x29,172]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L1436
.L1514:
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
	add	x0, x19, 120
	mov	w28, 0
	adrp	x26, .LC99
	add	x26, x26, :lo12:.LC99
	mul	w21, w1, w21
	mov	w1, 0
	uxth	w21, w21
	bl	ftl_memset
	add	w25, w21, w27
	ldr	x0, [x19,88]
	str	x0, [x29,152]
	mov	w19, w28
	sub	w0, w21, #12
	str	xzr, [x29,144]
	uxth	w25, w25
	str	w0, [x29,124]
.L1519:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,1845]
	cmp	w1, w19
	bls	.L1529
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1520:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1522
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
	beq	.L1521
	add	x24, x23, :lo12:.LANCHOR4
	ldr	x0, [x24,88]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1521
	add	x24, x24, 120
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	bl	printk
	strh	w20, [x24,w19,sxtw 1]
	uxth	w28, w28
	b	.L1522
.L1521:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1520
.L1522:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,1845]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1519
.L1529:
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
.L1531:
	cmp	w25, w23
	beq	.L1560
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,1845]
	cmp	w0, w1
	bcc	.L1532
	str	w28, [x21]
	b	.L1533
.L1532:
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
	tbz	x0, 0, .L1536
	str	w28, [x21]
.L1536:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1533:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1531
.L1560:
	mov	w21, 0
	cbz	w27, .L1558
	adrp	x25, .LC103
	adrp	x26, .LC102
	adrp	x27, .LC101
	add	x25, x25, :lo12:.LC103
	add	x26, x26, :lo12:.LC102
	add	x27, x27, :lo12:.LC101
.L1559:
	cmp	w21, w23
	beq	.L1558
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1539
	adrp	x0, .LC100
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC100
	bl	printk
	b	.L1540
.L1539:
	adrp	x20, .LANCHOR4
	sub	w4, w23, w21
	add	x22, x20, :lo12:.LANCHOR4
	add	x3, x29, 100
	mov	w1, w24
	add	x2, x29, 96
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,96]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,104]
	str	wzr, [x0]
	add	x0, x29, 104
	bl	memcpy
	ldr	x0, [x22,96]
	mov	w1, 1
	str	x0, [x29,112]
	mov	w2, w24
	ldr	x0, [x22,104]
	str	x0, [x29,120]
	add	x0, x29, 104
	bl	FlashReadPages
	ldr	w22, [x29,104]
	cmn	w22, #1
	bne	.L1541
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1541:
	ldr	x0, [x19,16]
	cbz	x0, .L1542
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,104]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1542
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1542:
	ldr	x0, [x19,8]
	cbz	x0, .L1540
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,96]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1540
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1540:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1559
.L1558:
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
	add	x19, x19, 136
	adrp	x22, .LANCHOR0
	ldr	x1, [x0,-72]
	add	x21, x22, :lo12:.LANCHOR0
	ldr	x24, [x0,2104]
	add	x21, x21, 2104
	str	x1, [x19,8]
	mov	w23, 0
	str	x24, [x19,16]
	mov	w25, -1
	mov	w26, 61664
.L1562:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,1966]
	cmp	w23, w1
	bcs	.L1571
	ldrh	w20, [x0,2014]
	strh	w25, [x21,12]
.L1564:
	add	x0, x22, :lo12:.LANCHOR0
	sub	w20, w20, #1
	uxth	w20, w20
	ldrh	w0, [x0,2014]
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1565
	madd	w0, w23, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1564
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1564
	strh	w20, [x21,12]
.L1565:
	add	w23, w23, 1
	add	x21, x21, 2
	b	.L1562
.L1571:
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
	bne	.L1573
	ldrh	w19, [x3,2020]
	b	.L1574
.L1573:
	ldrh	w19, [x3,2018]
.L1574:
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
	bne	.L1575
	mov	w22, 0
	mov	w25, 2
.L1576:
	cmp	w22, w19
	bgt	.L1575
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
	bne	.L1577
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1577
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1577
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1576
.L1577:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1576
.L1575:
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
	add	x19, x19, 136
	adrp	x20, .LANCHOR0
	ldr	x22, [x0,2104]
	mov	w25, 61649
	str	xzr, [x19,8]
	str	x22, [x19,16]
	bl	FtlBbtMemInit
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w21, [x0,2014]
	sub	w21, w21, #1
	uxth	w21, w21
.L1584:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2014]
	sub	w0, w0, #48
	cmp	w21, w0
	ble	.L1587
	lsl	w0, w21, 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1585
	ldr	w0, [x19,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
.L1585:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1586
	ldrh	w0, [x22]
	cmp	w0, w25
	bne	.L1586
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2104
	strh	w21, [x1,2104]
	ldr	w1, [x22,4]
	str	w1, [x0,8]
	ldrh	w1, [x22,8]
	strh	w1, [x0,4]
	b	.L1587
.L1586:
	sub	w21, w21, #1
	uxth	w21, w21
	b	.L1584
.L1587:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,2104]
	cmp	w1, w2
	beq	.L1589
	ldrh	w1, [x21,2108]
	cmp	w1, w2
	beq	.L1591
	add	x25, x23, :lo12:.LANCHOR4
	lsl	w1, w1, 10
	add	x0, x25, 136
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x25,136]
	cmn	w0, #1
	beq	.L1591
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1591
	ldr	w0, [x22,4]
	ldr	w1, [x21,2112]
	cmp	w0, w1
	bls	.L1591
	str	w0, [x21,2112]
	ldrh	w1, [x21,2108]
	ldrh	w0, [x22,8]
	strh	w1, [x21,2104]
	strh	w0, [x21,2108]
.L1591:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 1
	add	x21, x23, :lo12:.LANCHOR4
	mov	w26, 61649
	add	x21, x21, 136
	ldrh	w0, [x25,2104]
	bl	FtlGetLastWrittenPage
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x25,2106]
.L1593:
	tbnz	w19, #31, .L1598
	ldrh	w0, [x25,2104]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w19, w0, lsl 10
	str	w0, [x21,4]
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,-72]
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1594
	ldrh	w0, [x22]
	cmp	w0, w26
	bne	.L1594
.L1598:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,2110]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1595
	b	.L1596
.L1594:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1593
.L1595:
	ldr	w2, [x1,1940]
	cmp	w0, w2
	beq	.L1596
	ldrh	w1, [x1,1954]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1596
	cmp	w2, w1
	bcs	.L1596
	bl	FtlSysBlkNumInit
.L1596:
	add	x20, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x21, x20, 2104
	add	x23, x23, :lo12:.LANCHOR4
.L1599:
	ldrh	w0, [x20,1966]
	mov	w1, w19
	add	x19, x19, 1
	cmp	w1, w0
	bcs	.L1617
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x3, [x23,144]
	ldrh	w2, [x0,2224]
	add	x0, x21, x19, lsl 3
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1599
.L1617:
	mov	w0, 0
.L1589:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -48]!
	uxth	x1, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	uxth	w21, w0
	ubfiz	x0, x21, 2, 16
	ldr	x2, [x20,2200]
	ldr	w22, [x2,x0]
	cbnz	w22, .L1619
	ldr	x0, [x20,136]
	lsl	x19, x1, 4
	adrp	x2, .LANCHOR0+2028
	mov	w1, 255
	add	x0, x0, x19
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2028]
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x20,136]
	strh	w21, [x0,x19]
	ldr	x1, [x20,136]
	add	x19, x1, x19
	str	w22, [x19,4]
	b	.L1620
.L1619:
	lsl	x19, x1, 4
	ldr	x1, [x20,136]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x1, x1, x19
	add	x0, x0, 136
	ldr	x1, [x1,8]
	str	x1, [x0,8]
	ldr	x1, [x20,2104]
	str	x1, [x0,16]
	mov	w1, 1
	mov	w2, w1
	str	w22, [x0,4]
	bl	FlashReadPages
	ldr	x0, [x20,136]
	add	x0, x0, x19
	str	wzr, [x0,4]
	ldr	x0, [x20,136]
	strh	w21, [x0,x19]
.L1620:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR0
	stp	x21, x22, [sp,32]
	mov	w22, w0
	add	x0, x25, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	mov	w21, w1
	add	w1, w22, w1
	ldrh	w28, [x0,2026]
	mov	x24, x2
	ldrh	w0, [x0,2012]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L1622
	lsr	w28, w22, w28
	mov	w20, 0
	adrp	x26, .LANCHOR2
.L1623:
	cbz	w21, .L1622
	add	x3, x26, :lo12:.LANCHOR2
	ldr	x0, [x3,2192]
	ldr	w1, [x0,w28,uxtw 2]
	add	x0, x25, :lo12:.LANCHOR0
	ldrh	w19, [x0,2024]
	uxth	w0, w21
	udiv	w23, w22, w19
	msub	w23, w23, w19, w22
	sub	w19, w19, w23
	uxth	w19, w19
	cmp	w19, w21
	csel	w19, w0, w19, hi
	lsl	w27, w19, 9
	cbz	w1, .L1625
	ldr	x0, [x3,2072]
	ubfiz	x23, x23, 9, 16
	str	w1, [x29,124]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	x3, [x29,104]
	str	xzr, [x29,136]
	bl	FlashReadPages
	ldr	x3, [x29,104]
	mov	w2, w27
	ldr	w0, [x29,120]
	ldr	x1, [x3,2072]
	cmn	w0, #1
	mov	x0, x24
	csinv	w20, w20, wzr, ne
	add	x1, x1, x23
	bl	ftl_memcpy
	b	.L1627
.L1625:
	mov	x0, x24
	mov	w2, w27
	bl	ftl_memset
.L1627:
	add	w28, w28, 1
	sub	w21, w21, w19
	add	w22, w22, w19
	add	x24, x24, x27, sxtw
	b	.L1623
.L1622:
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
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
	ldrh	w1, [x19,2128]
	ldr	x2, [x19,2144]
	bl	FtlVendorPartRead
	ldr	x0, [x19,2144]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1631
	adrp	x0, .LC72
	adrp	x1, .LC104
	add	x1, x1, :lo12:.LC104
	add	x0, x0, :lo12:.LC72
	bl	printk
	ldrh	w2, [x19,2128]
	mov	w1, 0
	ldr	x0, [x19,2144]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1631:
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
	add	x21, x20, 344
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,344]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L1633
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,344]
.L1633:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,344]
	cmp	w3, w2
	bne	.L1634
	ldr	w2, [x0,432]
	str	w2, [x0,180]
	ldr	w2, [x0,436]
	str	w2, [x0,184]
	ldr	w2, [x0,352]
	str	w2, [x0,176]
	ldr	w2, [x0,356]
	str	w2, [x0,164]
	ldr	w2, [x0,360]
	str	w2, [x0,156]
	ldr	w2, [x0,364]
	str	w2, [x0,172]
	ldr	w2, [x0,372]
	str	w2, [x0,200]
	ldr	w2, [x0,376]
	str	w2, [x0,-80]
	ldr	w2, [x0,380]
	str	w2, [x0,160]
	ldr	w2, [x0,384]
	str	w2, [x0,168]
	ldr	w2, [x0,388]
	str	w2, [x0,208]
	ldr	w1, [x0,404]
	ldr	w2, [x0,392]
	str	w2, [x0,212]
	str	w1, [x0,336]
.L1634:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 34661
	movk	w1, 0x1234, lsl 16
	ldr	w2, [x0,412]
	str	wzr, [x0,2016]
	cmp	w2, w1
	bne	.L1635
	mov	w1, 1
	str	w1, [x0,152]
	adrp	x0, .LC72
	adrp	x1, .LC105
	add	x0, x0, :lo12:.LC72
	add	x1, x1, :lo12:.LC105
	bl	printk
.L1635:
	adrp	x2, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR2
	add	x2, x2, :lo12:.LANCHOR0
	ldr	x21, [sp,32]
	ldr	w3, [x19,196]
	ldrh	w1, [x2,2008]
	ldr	w0, [x19,200]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,1952]
	udiv	w0, w0, w1
	str	w0, [x19,204]
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
	strh	wzr, [x21,2296]
	mov	x24, x22
	ldr	w2, [x20,2048]
	mov	w22, 65535
	ldr	x0, [x21,2208]
	lsl	w2, w2, 2
	strh	wzr, [x20,2064]
	bl	ftl_memset
	ldr	w2, [x20,2048]
	mov	w1, 0
	ldr	x0, [x21,2160]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2040]
	mov	w1, 0
	ldr	x0, [x21,2184]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2040]
	mov	w1, 0
	ldr	x0, [x20,2072]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x21, 272
	mov	w1, 255
	mov	w2, 12
	bl	ftl_memset
	ldrh	w21, [x20,1952]
.L1638:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,1954]
	cmp	w0, w21
	bls	.L1679
	mov	x27, 0
	ldrh	w5, [x1,1944]
	ldrh	w4, [x1,2030]
	mov	w20, w27
	mov	x23, x1
.L1680:
	cmp	w5, w27, uxth
	bls	.L1714
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
	cbnz	w0, .L1639
	add	x2, x24, :lo12:.LANCHOR2
	mov	w0, 56
	lsl	w28, w28, 10
	mov	w3, 4
	umull	x0, w20, w0
	ldr	x1, [x2,2040]
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x2,2040]
	add	x0, x1, x0
	ldr	x1, [x2,888]
	ldr	x2, [x2,896]
	str	x1, [x0,8]
	mul	w1, w20, w4
	add	w20, w20, 1
	sdiv	w1, w1, w3
	uxth	w20, w20
	add	x1, x2, x1, sxtw 2
	str	x1, [x0,16]
.L1639:
	add	x27, x27, 1
	b	.L1680
.L1714:
	cbz	w20, .L1642
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	mov	w28, 61604
	ldr	x0, [x0,2040]
	bl	FlashReadPages
.L1643:
	cmp	w20, w23, uxth
	bls	.L1642
	mov	x0, 56
	mul	x6, x23, x0
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,2040]
	add	x1, x0, x6
	ldr	w0, [x0,x6]
	ldr	w5, [x1,4]
	cmn	w0, #1
	ldr	x26, [x1,16]
	ubfx	x25, x5, 10, 16
	bne	.L1645
	mov	w7, 16
.L1644:
	add	x8, x24, :lo12:.LANCHOR2
	str	x7, [x29,104]
	str	x8, [x29,112]
	str	x6, [x29,120]
	ldr	x0, [x8,2040]
	add	x0, x0, x6
	ldr	w1, [x0,4]
	add	w1, w1, 1
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x0, [x8,2040]
	mov	w2, w1
	add	x0, x0, x6
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	x8, [x29,112]
	cmp	w0, w22
	ldr	x6, [x29,120]
	ldr	x7, [x29,104]
	ldr	x0, [x8,2040]
	bne	.L1646
	str	w27, [x0,x6]
	b	.L1645
.L1646:
	ldr	w0, [x0,x6]
	cmn	w0, #1
	bne	.L1645
	sub	w7, w7, #1
	uxth	w7, w7
	cbnz	w7, .L1644
.L1645:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x1, [x0,2040]
	ldr	w1, [x1,x6]
	cmn	w1, #1
	beq	.L1648
	ldr	w0, [x0,188]
	cmn	w0, #1
	beq	.L1649
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1650
.L1649:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1650
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,188]
.L1650:
	ldrh	w0, [x26]
	cmp	w0, w28
	beq	.L1652
	bhi	.L1653
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1651
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldrh	w6, [x0,2040]
	ldrh	w0, [x0,2064]
	sub	w2, w6, #1
	ldr	x7, [x7,2184]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1666
.L1653:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1655
	cmp	w0, w22
	beq	.L1713
	b	.L1651
.L1655:
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldr	w6, [x0,2048]
	ldrh	w0, [x7,2296]
	ldr	x7, [x7,2208]
	uxth	w2, w6
	sub	w1, w2, #1
	sub	w2, w2, w0
	sub	w2, w2, #1
	sxth	x1, w1
	sxth	w2, w2
.L1657:
	cmp	w1, w2
	ble	.L1715
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bls	.L1658
	ldr	w2, [x7]
	cbnz	w2, .L1659
	cmp	w0, w6
	beq	.L1659
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,2296]
.L1659:
	mov	w0, 0
.L1660:
	cmp	w0, w1
	beq	.L1716
	add	x6, x24, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x7, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x10, [x6,2208]
	sxth	w0, w0
	add	x11, x10, x7
	ldr	w11, [x11,4]
	str	w11, [x10,x7]
	ldr	x6, [x6,2160]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1660
.L1716:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x2, [x0,2208]
	str	w6, [x2,x8]
	ldr	x2, [x0,2160]
	strh	w25, [x2,x9,lsl 1]
	tbz	w1, #31, .L1662
	b	.L1651
.L1658:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1657
.L1715:
	tbz	w1, #31, .L1695
	b	.L1651
.L1662:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2296]
	ldr	w2, [x2,2048]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1651
.L1695:
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldr	w4, [x26,4]
	strh	w0, [x2,2296]
	ldr	x0, [x2,2208]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2160]
	b	.L1712
.L1672:
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bhi	.L1717
	sub	w1, w1, #1
	sxth	x1, w1
.L1666:
	cmp	w1, w2
	bgt	.L1672
	b	.L1671
.L1717:
	ldr	w2, [x7]
	cbnz	w2, .L1668
	cmp	w0, w6
	beq	.L1668
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	strh	w0, [x2,2064]
.L1668:
	mov	w0, 0
.L1669:
	cmp	w0, w1
	beq	.L1718
	add	x2, x24, :lo12:.LANCHOR2
	ldr	x7, [x2,2184]
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
	b	.L1669
.L1718:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	ldr	x0, [x0,2184]
	str	w2, [x0,x8]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2072]
	strh	w25, [x0,x9,lsl 1]
.L1671:
	tbnz	w1, #31, .L1651
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2040]
	ldrh	w6, [x2,2064]
	sub	w0, w0, #1
	sub	w0, w0, w6
	cmp	w1, w0, sxth
	bgt	.L1651
	add	x0, x24, :lo12:.LANCHOR2
	add	w6, w6, 1
	ldr	w4, [x26,4]
	strh	w6, [x2,2064]
	ldr	x0, [x0,2184]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2072]
.L1712:
	strh	w25, [x0,x1,lsl 1]
	b	.L1651
.L1652:
	add	x0, x24, :lo12:.LANCHOR2
	add	x1, x0, 272
	ldrh	w2, [x0,272]
	cmp	w2, w22
	bne	.L1673
	strh	w25, [x0,272]
	ldr	w0, [x26,4]
	str	w0, [x1,8]
	b	.L1651
.L1673:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1674
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1674:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	add	x0, x1, 272
	ldr	w6, [x0,8]
	cmp	w6, w2
	bcs	.L1675
	ldrh	w2, [x1,272]
	strh	w2, [x0,4]
	strh	w25, [x1,272]
	ldr	w1, [x26,4]
	str	w1, [x0,8]
	b	.L1651
.L1675:
	strh	w25, [x0,4]
	b	.L1651
.L1648:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1676
.L1713:
	mov	w0, w25
	mov	w1, 0
	b	.L1711
.L1676:
	mov	w0, w25
	mov	w1, 1
.L1711:
	bl	FtlFreeSysBlkQueueIn
.L1651:
	add	x23, x23, 1
	b	.L1643
.L1642:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1638
.L1679:
	add	x4, x24, :lo12:.LANCHOR2
	ldr	x2, [x4,2160]
	ldrh	w0, [x2]
	cbz	w0, .L1681
.L1684:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,2072]
	ldrh	w0, [x2]
	cbz	w0, .L1682
	b	.L1705
.L1681:
	ldrh	w4, [x4,2296]
	cbz	w4, .L1684
	ldr	w1, [x1,2048]
.L1685:
	cmp	w0, w1
	bcs	.L1684
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1686
	mov	w7, w0
.L1687:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w1, [x1,2048]
	cmp	w0, w1
	bcs	.L1684
	add	x2, x24, :lo12:.LANCHOR2
	sxtw	x6, w0
	sub	w1, w0, w7
	lsl	x4, x6, 1
	add	w0, w0, 1
	ldr	x5, [x2,2160]
	sxtw	x1, w1
	sxth	w0, w0
	ldrh	w8, [x5,x4]
	strh	w8, [x5,x1,lsl 1]
	ldr	x5, [x2,2208]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,2160]
	strh	wzr, [x1,x4]
	b	.L1687
.L1686:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1685
.L1682:
	ldrh	w4, [x1,2064]
	cbz	w4, .L1705
	ldrh	w1, [x1,2040]
.L1690:
	cmp	w0, w1
	mov	w7, w0
	bge	.L1705
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1691
.L1692:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2,2040]
	cmp	w0, w1
	bge	.L1705
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
	ldr	x5, [x5,2184]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,2072]
	strh	wzr, [x1,x4]
	b	.L1692
.L1691:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1690
.L1705:
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
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR4
	adrp	x21, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x20, x21, :lo12:.LANCHOR2
	add	x19, x22, :lo12:.LANCHOR4
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	add	x19, x19, 136
	adrp	x23, .LANCHOR0
	str	xzr, [x19,8]
	ldr	x0, [x20,2104]
	add	x25, x20, 272
	str	x0, [x19,16]
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w2, [x0,1952]
	ldr	x0, [x20,-48]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x20,272]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1720
.L1724:
	mov	w19, -1
	b	.L1721
.L1720:
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w24, w0
	add	w0, w0, 1
	strh	w0, [x25,2]
	mov	w25, 61604
.L1722:
	tbnz	w24, #31, .L1727
	ldrh	w0, [x20,272]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w24, w0, lsl 10
	str	w0, [x19,4]
	ldr	x0, [x20,-72]
	str	x0, [x19,8]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1723
	ldr	x0, [x20,-72]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1723
	ldr	x0, [x20,2104]
	ldrh	w0, [x0]
	cmp	w0, w25
	bne	.L1723
.L1727:
	add	x24, x22, :lo12:.LANCHOR4
	add	x19, x21, :lo12:.LANCHOR2
	add	x20, x19, 216
	mov	w2, 48
	add	x22, x23, :lo12:.LANCHOR0
	mov	x0, x20
	ldr	x1, [x24,144]
	bl	ftl_memcpy
	ldr	x1, [x24,144]
	ldrh	w2, [x22,1952]
	ldr	x0, [x19,-48]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x22,1952]
	ldr	x3, [x24,144]
	ldr	x0, [x23,#:lo12:.LANCHOR0]
	uxth	x1, w2
	lsr	w2, w2, 3
	add	x1, x1, 24
	add	w2, w2, 4
	lsr	x1, x1, 1
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	ldr	w1, [x19,216]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L1737
	b	.L1724
.L1723:
	sub	w24, w24, #1
	sxth	w24, w24
	b	.L1722
.L1737:
	ldrb	w1, [x20,10]
	ldrh	w0, [x22,1966]
	ldrh	w2, [x20,8]
	strh	w2, [x19,278]
	cmp	w1, w0
	bne	.L1724
	ldrh	w1, [x22,2018]
	ldrh	w0, [x22,2024]
	str	w2, [x24,192]
	mul	w1, w1, w2
	str	w1, [x22,2096]
	strh	wzr, [x19,290]
	mul	w1, w1, w0
	str	w1, [x22,2060]
	ldrh	w0, [x22,2110]
	ldr	w1, [x22,1956]
	strb	wzr, [x19,294]
	sub	w0, w1, w0
	ldrh	w1, [x22,1944]
	sub	w0, w0, w2
	ldrh	w2, [x20,14]
	strh	w2, [x19,-8]
	udiv	w0, w0, w1
	strb	wzr, [x19,296]
	strh	w0, [x19,264]
	ldrh	w0, [x20,16]
	lsr	w1, w0, 6
	strh	w1, [x19,-6]
	mov	w1, -1
	strh	w1, [x19,288]
	ldrh	w1, [x20,18]
	and	w0, w0, 63
	strh	w1, [x19,40]
	ldrh	w1, [x20,20]
	strb	w0, [x19,-2]
	lsr	w3, w1, 6
	and	w1, w1, 63
	strb	w1, [x19,46]
	ldrb	w1, [x20,12]
	strb	w1, [x19,48]
	ldrh	w1, [x20,22]
	strh	w1, [x19,88]
	ldrh	w1, [x20,24]
	ldrb	w0, [x20,11]
	strh	w3, [x19,42]
	lsr	w3, w1, 6
	and	w1, w1, 63
	strb	w0, [x19]
	strb	w1, [x19,94]
	strh	w3, [x19,90]
	ldrb	w1, [x20,13]
	strb	w1, [x19,96]
	ldr	w0, [x20,32]
	str	w0, [x19,196]
	ldr	w1, [x19,188]
	ldr	w0, [x20,40]
	str	wzr, [x19,176]
	str	wzr, [x19,164]
	cmp	w0, w1
	str	wzr, [x19,156]
	str	wzr, [x19,172]
	str	wzr, [x19,200]
	str	wzr, [x19,208]
	str	wzr, [x19,168]
	bls	.L1728
	str	w0, [x19,188]
.L1728:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,252]
	ldr	w3, [x0,192]
	cmp	w1, w3
	bls	.L1729
	str	w1, [x0,192]
.L1729:
	mov	w0, 65535
	cmp	w2, w0
	beq	.L1730
	add	x0, x21, :lo12:.LANCHOR2
	sub	x0, x0, #8
	bl	make_superblock
.L1730:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 40
	ldrh	w2, [x1,40]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1731
	bl	make_superblock
.L1731:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 88
	ldrh	w2, [x1,88]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1732
	bl	make_superblock
.L1732:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x21, 288
	mov	w19, 0
	ldrh	w2, [x21,288]
	cmp	w2, w1
	beq	.L1721
	bl	make_superblock
.L1721:
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
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR1
	stp	x27, x28, [sp,80]
	mov	x28, x0
	add	x0, x19, :lo12:.LANCHOR1
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	str	w1, [x29,156]
	ldrh	w27, [x0,3008]
	mov	w0, 65535
	str	x19, [x29,144]
	cmp	w27, w0
	beq	.L1759
	cbnz	w27, .L1739
	b	.L1740
.L1759:
	mov	w27, 0
.L1739:
	adrp	x0, .LANCHOR0+2018
	ldr	w1, [x29,156]
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2018]
	cmp	w1, w0
	bne	.L1741
.L1740:
	bl	FtlGcPageVarInit
.L1741:
	mov	w22, -1
	mov	w20, 0
	mov	w21, 65535
	adrp	x23, .LANCHOR0
.L1753:
	ldrh	w0, [x28]
	strb	wzr, [x28,8]
	cmp	w0, w21
	beq	.L1760
.L1743:
	adrp	x26, .LANCHOR2
.L1756:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x28
	mov	w19, 0
	mov	w10, 4
	ldrh	w6, [x0,1944]
	ldrh	w9, [x0,2030]
	add	x6, x28, x6, lsl 1
.L1744:
	cmp	x2, x6
	beq	.L1770
	ldrh	w1, [x2,16]
	cmp	w1, w21
	beq	.L1745
	add	x7, x26, :lo12:.LANCHOR2
	mov	w0, 56
	orr	w1, w27, w1, lsl 10
	add	w5, w19, 1
	umull	x0, w19, w0
	ldr	x8, [x7,2040]
	add	x8, x8, x0
	str	w1, [x8,4]
	ldr	x1, [x7,2040]
	ldr	x7, [x7,896]
	add	x0, x1, x0
	mul	w1, w19, w9
	uxth	w19, w5
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	add	x1, x7, x1, sxtw 2
	str	x1, [x0,16]
.L1745:
	add	x2, x2, 2
	b	.L1744
.L1770:
	add	x0, x26, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x24, 0
	adrp	x25, .LANCHOR2
	ldr	x0, [x0,2040]
	bl	FlashReadPages
.L1747:
	cmp	w19, w24, uxth
	bls	.L1771
	add	x9, x25, :lo12:.LANCHOR2
	mov	x0, 56
	str	x9, [x29,104]
	mul	x8, x24, x0
	ldr	x11, [x9,2040]
	str	x8, [x29,112]
	add	x10, x11, x8
	str	x11, [x29,120]
	str	x10, [x29,128]
	ldr	w1, [x10,4]
	str	x1, [x29,136]
	lsr	x0, x1, 10
	bl	P2V_plane
	uxth	w2, w0
	ldr	x11, [x29,120]
	ldr	x8, [x29,112]
	ldr	x1, [x29,136]
	ldr	x9, [x29,104]
	ldr	w8, [x11,x8]
	cbnz	w8, .L1748
	ldr	x10, [x29,128]
	add	x24, x24, 1
	ldr	x8, [x10,16]
	ldrh	w0, [x8]
	cmp	w0, w21
	bne	.L1749
	mov	w0, 1
	str	w0, [x9,2320]
	b	.L1742
.L1749:
	ldr	w0, [x8,12]
	ldr	w2, [x8,8]
	bl	FtlGcUpdatePage
	b	.L1747
.L1748:
	ldr	w0, [x9,152]
	cbnz	w0, .L1750
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1751
.L1750:
	add	x0, x25, :lo12:.LANCHOR2
	ldrh	w2, [x28]
	ldr	x0, [x0,-88]
	ldrh	w0, [x0,x2,lsl 1]
	cmp	w0, 119
	bls	.L1752
.L1751:
	cmn	w8, #1
	csel	w1, w1, w22, eq
.L1752:
	add	x6, x25, :lo12:.LANCHOR2
	ldrh	w2, [x28]
	str	x1, [x29,136]
	mov	w27, 0
	ldr	x0, [x6,-48]
	strh	wzr, [x0,x2,lsl 1]
	ldrh	w0, [x28]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x28]
	bl	FtlGcPageVarInit
	ldr	x1, [x29,136]
	mov	w22, w1
	b	.L1753
.L1771:
	ldr	w0, [x29,156]
	add	w20, w20, 1
	add	w3, w27, 1
	cmp	w20, w0
	uxth	w27, w3
	bcs	.L1755
.L1757:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2018]
	cmp	w0, w27
	bhi	.L1756
	b	.L1760
.L1755:
	ldr	x0, [x29,144]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3008]
	cmp	w0, w21
	beq	.L1757
	add	w0, w0, w20
	strh	w0, [x1,3008]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2018]
	cmp	w0, w27
	bls	.L1757
	b	.L1758
.L1760:
	mov	w2, 0
.L1742:
	ldr	x0, [x29,144]
	mov	w1, w27
	strh	w27, [x28,2]
	add	x19, x0, :lo12:.LANCHOR1
	mov	w0, -1
	strb	w2, [x28,6]
	strh	w0, [x19,3008]
	mov	x0, x28
	bl	ftl_sb_update_avl_pages
.L1758:
	mov	w0, w22
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
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
	cbz	w4, .L1773
	bl	FlashProgSlc2KPages
	b	.L1774
.L1773:
	sub	w0, w5, #1
	mov	w27, 56
	mov	x28, 24
	str	w0, [x29,116]
.L1825:
	cmp	w22, w25
	bcs	.L1826
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
	bcc	.L1775
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1776
.L1775:
	ldrb	w1, [x2,1937]
	add	x3, x2, 1652
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x0, x1, x28, x3
	ldr	x0, [x0,8]
	cbz	x0, .L1778
	cmp	w4, 1
	bne	.L1779
	ldr	x0, [x2,128]
	bl	NandcIqrWaitFlashReady
.L1779:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1778:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	add	x0, x0, 1652
	ldr	w1, [x29,128]
	madd	x0, x2, x28, x0
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1780
	add	w1, w22, 1
	umull	x1, w1, w27
	add	x1, x19, x1
	str	x1, [x0,16]
.L1780:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, x2
	madd	x2, x2, x28, x1
	ldrb	w20, [x0,1848]
	ldrb	w0, [x1,1845]
	strb	w20, [x2,1652]
	cmp	w0, 1
	bne	.L1781
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1782
.L1781:
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
.L1782:
	ldr	w0, [x29,116]
	cmp	w0, 6
	bhi	.L1783
	add	x2, x21, :lo12:.LANCHOR0
	add	x0, x2, x20, sxtw
	ldrb	w0, [x0,1616]
	cbz	w0, .L1783
	ldrb	w1, [x2,761]
	mov	w0, w20
	add	x2, x2, 764
	mov	w3, 0
	bl	HynixSetRRPara
.L1783:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1784
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1784
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1785
.L1784:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1785:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldrb	w2, [x29,124]
	mov	w0, w20
	ldr	x3, [x26,8]
	mov	w1, 1
	ldr	x4, [x26,16]
	bl	NandcXferData
	cbz	w24, .L1786
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
.L1786:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1776:
	add	w22, w22, 1
	b	.L1825
.L1826:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	add	x22, x21, 1652
	mov	x24, 24
	ldr	x0, [x21,128]
	bl	NandcIqrWaitFlashReady
.L1788:
	ldrb	w0, [x21,1845]
	cmp	w0, w20
	bls	.L1827
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1789
	ldrb	w0, [x21,120]
	cbz	w0, .L1789
	mul	x0, x20, x24
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L1789:
	add	x20, x20, 1
	b	.L1788
.L1827:
	ldr	w0, [x29,120]
	cbnz	w0, .L1791
.L1799:
	mov	w0, 0
	b	.L1774
.L1791:
	adrp	x24, .LC103
	adrp	x26, .LC102
	adrp	x27, .LC101
	mov	w21, 0
	add	x24, x24, :lo12:.LC103
	add	x26, x26, :lo12:.LC102
	add	x27, x27, :lo12:.LC101
.L1792:
	cmp	w21, w25
	beq	.L1799
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1793
	adrp	x0, .LC100
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC100
	bl	printk
	b	.L1794
.L1793:
	adrp	x20, .LANCHOR4
	sub	w4, w25, w21
	add	x22, x20, :lo12:.LANCHOR4
	add	x3, x29, 132
	mov	w1, w23
	add	x2, x29, 128
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,96]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,104]
	str	wzr, [x0]
	add	x0, x29, 136
	bl	memcpy
	ldr	x0, [x22,96]
	mov	w1, 1
	str	x0, [x29,144]
	mov	w2, w23
	ldr	x0, [x22,104]
	str	x0, [x29,152]
	add	x0, x29, 136
	bl	FlashReadPages
	ldr	w22, [x29,136]
	cmn	w22, #1
	bne	.L1795
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1795:
	ldr	x0, [x19,16]
	cbz	x0, .L1796
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,104]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1796
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1796:
	ldr	x0, [x19,8]
	cbz	x0, .L1794
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,96]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1794
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1794:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1792
.L1774:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FlashTestBlk.part.16, %function
FlashTestBlk.part.16:
	stp	x29, x30, [sp, -160]!
	mov	w1, 165
	mov	w2, 32
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	uxth	w20, w0
	add	x19, x19, :lo12:.LANCHOR4
	lsl	w20, w20, 10
	ldr	x0, [x19,80]
	str	x0, [x29,48]
	add	x0, x29, 96
	str	x0, [x29,56]
	bl	ftl_memset
	ldr	x0, [x19,80]
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
	.size	FlashTestBlk.part.16, .-FlashTestBlk.part.16
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	uxth	w1, w0
	adrp	x0, .LANCHOR4+68
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x0,#:lo12:.LANCHOR4+68]
	mov	w0, 0
	cmp	w1, w2
	bcc	.L1830
	mov	w0, w1
	bl	FlashTestBlk.part.16
.L1830:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR4
	add	x0, x21, :lo12:.LANCHOR4
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	adrp	x19, .LANCHOR0
	mov	w20, 0
	ldr	x0, [x0,88]
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
	adrp	x0, .LC106
	mov	w1, 1
	add	x0, x0, :lo12:.LC106
	bl	printk
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w2, 4096
	ldr	x0, [x0,88]
	bl	ftl_memset
	lsr	w0, w23, 4
	str	w0, [x29,168]
	ldr	w0, [x29,128]
	and	w0, w0, 1
	str	w0, [x29,116]
	sub	w0, w23, #1
	uxth	w0, w0
	str	w0, [x29,124]
.L1834:
	ldrb	w0, [x19,1845]
	cmp	w0, w20
	bls	.L1885
	add	x0, x21, :lo12:.LANCHOR4
	sxtw	x25, w20
	add	x1, x0, 120
	ldrh	w22, [x1,w20,sxtw 1]
	cbnz	w22, .L1864
	ldrh	w2, [x19,188]
	mov	w1, w22
	ldr	x0, [x0,32]
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
.L1836:
	uxth	w0, w26
	str	w0, [x29,144]
	cmp	w0, w23
	bcs	.L1846
	mov	w0, -1
	strb	w0, [x29,182]
	strb	w0, [x29,183]
	ldr	w0, [x29,116]
	cbz	w0, .L1838
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
	bne	.L1838
	ldr	w1, [x19,12]
	mov	w0, w27
	add	x2, x29, 183
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,182]
	ldrb	w1, [x29,183]
	and	w0, w1, w0
	strb	w0, [x29,182]
.L1838:
	ldr	w0, [x29,120]
	cbz	w0, .L1840
	ldr	x0, [x19,744]
	add	x2, x29, 183
	ldrh	w1, [x0,10]
	ldr	w0, [x28,x25,lsl 2]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w27
	add	w1, w1, w22
	bl	FlashReadSpare
.L1840:
	ldr	x0, [x19,744]
	ldrb	w1, [x29,182]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1865
	cmp	w0, 1
	bne	.L1841
.L1865:
	mov	w0, 1
	cbz	w1, .L1843
	ldrb	w0, [x29,183]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1843
.L1841:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1843
	ldrb	w0, [x29,183]
	cmp	w0, 255
	cset	w0, ne
.L1843:
	ldr	x1, [x29,128]
	tbz	x1, 2, .L1844
	ldr	w1, [x28,x25,lsl 2]
	mov	w0, w27
	add	w1, w22, w1
	bl	SandiskProgTestBadBlock
.L1844:
	cbz	w0, .L1845
	adrp	x0, .LC107
	mov	w1, w20
	mov	w2, w26
	add	x0, x0, :lo12:.LC107
	add	w24, w24, 1
	bl	printk
	add	x0, x21, :lo12:.LANCHOR4
	ldr	x1, [x29,144]
	ldrb	w3, [x29,144]
	uxth	w24, w24
	ubfx	x1, x1, 5, 11
	lsl	x1, x1, 2
	ldr	x2, [x0,32]
	mov	w0, 1
	lsl	w0, w0, w3
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
	ldr	w1, [x29,168]
	ldrb	w0, [x19,1845]
	mul	w0, w1, w0
	cmp	w24, w0
	bgt	.L1846
.L1845:
	ldr	w0, [x29,172]
	add	w26, w26, 1
	add	w22, w22, w0
	b	.L1836
.L1846:
	adrp	x0, .LC108
	mov	w1, w20
	add	x0, x0, :lo12:.LC108
	mov	w2, w24
	bl	printk
	ldrb	w0, [x19,1845]
	ldr	w1, [x29,168]
	mul	w0, w1, w0
	cmp	w24, w0
	blt	.L1848
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w2, [x19,188]
	mov	w1, 0
	lsl	w2, w2, 9
	ldr	x0, [x0,32]
	bl	ftl_memset
.L1848:
	cbnz	w20, .L1850
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x26, .LC109
	mov	w22, w20
	add	x26, x26, :lo12:.LC109
	mov	w24, 1
	ldrh	w27, [x0,68]
.L1851:
	ldrb	w0, [x19,9]
	cmp	w0, w27
	bls	.L1886
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1852
	mov	w1, w27
	mov	x0, x26
	add	w22, w22, 1
	bl	printk
	add	x0, x21, :lo12:.LANCHOR4
	ubfx	x1, x27, 5, 11
	lsl	x1, x1, 2
	uxth	w22, w22
	ldr	x2, [x0,32]
	lsl	w0, w24, w27
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1852:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1851
.L1886:
	adrp	x26, .LC109
	ldr	w24, [x29,124]
	sub	w27, w23, #50
	add	x26, x26, :lo12:.LC109
.L1854:
	cmp	w24, w27
	ble	.L1887
	mov	w0, w24
	bl	FlashTestBlk
	cbz	w0, .L1855
	mov	w1, w24
	mov	x0, x26
	bl	printk
	add	x0, x21, :lo12:.LANCHOR4
	ubfx	x1, x24, 5, 11
	lsl	x1, x1, 2
	ldr	x3, [x0,32]
	mov	w0, 1
	lsl	w0, w0, w24
	ldr	w4, [x3,x1]
	orr	w0, w4, w0
	str	w0, [x3,x1]
.L1855:
	sub	w2, w24, #1
	uxth	w24, w2
	b	.L1854
.L1887:
	add	x1, x21, :lo12:.LANCHOR4
	ldrb	w0, [x19,9]
	ldr	w2, [x1,68]
	sub	w0, w0, w2
	cmp	w22, w0
	bcc	.L1850
	ldrh	w2, [x19,188]
	ldr	x0, [x1,32]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L1850:
	adrp	x24, .LC110
	ldr	w27, [x29,124]
	mul	w26, w20, w23
	add	x24, x24, :lo12:.LC110
	mov	w22, -3872
.L1858:
	mov	w2, w27
	mov	x0, x24
	mov	w1, w20
	bl	printk
	add	x0, x21, :lo12:.LANCHOR4
	ldr	x2, [x0,32]
.L1859:
	ubfx	x0, x27, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w27
	and	w1, w0, 1
	tbz	x0, 0, .L1888
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1859
.L1888:
	add	x0, x21, :lo12:.LANCHOR4
	add	x2, x0, 120
	strh	w27, [x2,x25,lsl 1]
	ldr	x2, [x29,160]
	strh	w22, [x2]
	strh	w27, [x2,2]
	strh	w1, [x2,8]
	ldr	x1, [x0,32]
	ldr	x0, [x0,88]
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
	cbz	w0, .L1864
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1858
.L1864:
	add	w20, w20, 1
	uxtb	w20, w20
	b	.L1834
.L1885:
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
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	stp	x19, x20, [sp,16]
	add	x20, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	uxth	w26, w0
	mov	w28, 0
	ldrb	w0, [x20,120]
	uxtb	w25, w1
	str	w0, [x29,120]
	mov	w21, w28
	str	w26, [x20,2100]
	mov	w19, w28
	adrp	x24, .LANCHOR2
	mov	w27, 56
.L1890:
	ldrh	w0, [x20,1944]
	cmp	w0, w28
	bls	.L1934
	add	x1, x24, :lo12:.LANCHOR2
	umull	x0, w28, w27
	ldr	x1, [x1,-96]
	str	wzr, [x1,x0]
	add	x0, x20, 1976
	mov	w1, w26
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	uxth	w23, w0
	mov	w1, w23
	cbz	w25, .L1891
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L1892
.L1891:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1893
	adrp	x1, .LANCHOR2
	lsl	w23, w23, 10
	add	x3, x1, :lo12:.LANCHOR2
	mov	w2, 4
	umull	x0, w21, w27
	ldr	x1, [x3,-96]
	add	x1, x1, x0
	str	w23, [x1,4]
	ldr	x1, [x3,-96]
	ldr	x3, [x3,2112]
	add	x1, x1, x0
	ldrh	w0, [x20,2030]
	mul	w0, w21, w0
	add	w21, w21, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1892
.L1893:
	add	w19, w19, 1
	uxth	w19, w19
.L1892:
	add	w2, w28, 1
	uxth	w28, w2
	b	.L1890
.L1934:
	cbz	w21, .L1896
	ldr	w0, [x29,120]
	adrp	x20, .LANCHOR2
	mov	w2, w21
	mov	x24, 0
	cmp	w0, wzr
	add	x0, x20, :lo12:.LANCHOR2
	cset	w23, ne
	mov	x27, 56
	mov	w1, w23
	ldr	x0, [x0,-96]
	bl	FlashEraseBlocks
.L1897:
	cmp	w21, w24, uxth
	bls	.L1935
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x24, x27
	ldr	x1, [x1,-96]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1898
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1898:
	add	x24, x24, 1
	b	.L1897
.L1935:
	cbnz	w25, .L1900
	uxth	w23, w23
	mov	w28, 6
	mov	w27, 1
	b	.L1901
.L1900:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w27, [x0,2020]
	ldrb	w0, [x0,120]
	cbnz	w0, .L1916
	lsr	w28, w27, 2
	mov	w23, 1
	b	.L1901
.L1916:
	mov	w23, 1
	mov	w28, w23
.L1901:
	add	x0, x22, :lo12:.LANCHOR0
	mov	w24, 0
	str	x0, [x29,120]
.L1910:
	mov	w22, 0
	mov	w21, w22
.L1902:
	ldr	x0, [x29,120]
	ldrh	w0, [x0,1944]
	cmp	w0, w22
	bls	.L1936
	add	x1, x20, :lo12:.LANCHOR2
	mov	w0, 56
	umull	x0, w22, w0
	ldr	x1, [x1,-96]
	str	wzr, [x1,x0]
	mov	w1, w26
	ldr	x0, [x29,120]
	add	x0, x0, 1976
	ldrb	w0, [x0,w22,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w25, .L1903
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L1904
.L1903:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1904
	add	x3, x20, :lo12:.LANCHOR2
	mov	w0, 56
	ldr	w2, [x29,112]
	umull	x0, w21, w0
	ldr	x1, [x3,-96]
	add	w4, w24, w2, lsl 10
	mov	w2, 4
	add	x1, x1, x0
	str	w4, [x1,4]
	ldr	x1, [x3,-96]
	add	x1, x1, x0
	ldr	x0, [x3,2088]
	ldr	x3, [x3,2096]
	str	x0, [x1,8]
	ldr	x0, [x29,120]
	ldrh	w0, [x0,2030]
	mul	w0, w21, w0
	add	w21, w21, 1
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
.L1904:
	add	w2, w22, 1
	uxth	w22, w2
	b	.L1902
.L1936:
	cbz	w21, .L1896
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w21
	mov	w2, w23
	mov	w3, 1
	ldr	x0, [x0,-96]
	bl	FlashProgPages
	mov	x1, 0
.L1907:
	cmp	w21, w1, uxth
	bls	.L1937
	add	x2, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x1, x0
	ldr	x2, [x2,-96]
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L1908
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L1908:
	add	x1, x1, 1
	b	.L1907
.L1937:
	add	w24, w24, w28
	uxth	w24, w24
	cmp	w24, w27
	bcc	.L1910
	mov	x22, 0
	mov	x24, 56
.L1911:
	cmp	w21, w22, uxth
	bls	.L1938
	cbz	w25, .L1912
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x22, x24
	ldr	x1, [x1,-96]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L1912
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1912:
	add	x22, x22, 1
	b	.L1911
.L1938:
	cmp	w26, 63
	bls	.L1917
	cbz	w25, .L1896
.L1917:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, w21
	ldr	x0, [x20,-96]
	bl	FlashEraseBlocks
.L1896:
	mov	w0, w19
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
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x2, x22, :lo12:.LANCHOR2
	adrp	x19, .LANCHOR4
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	add	x24, x19, :lo12:.LANCHOR4
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x2,-72]
	mov	x23, 0
	ldr	x2, [x2,2104]
	add	x21, x20, 2104
	str	x2, [x24,152]
	ldrh	w2, [x20,2028]
	str	x0, [x24,144]
	bl	ftl_memset
.L1940:
	ldrh	w1, [x20,1966]
	mov	w0, w23
	add	x23, x23, 1
	cmp	w0, w1
	bge	.L1948
	add	x1, x22, :lo12:.LANCHOR2
	ldr	x3, [x24,144]
	ldrh	w2, [x1,2224]
	add	x1, x21, x23, lsl 3
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L1940
.L1948:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 255
	add	x19, x19, 136
	mov	w2, 16
	mov	w28, 0
	adrp	x25, .LC111
	ldr	x27, [x19,16]
	adrp	x26, .LC112
	mov	w24, w28
	add	x25, x25, :lo12:.LC111
	mov	x0, x27
	add	x26, x26, :lo12:.LC112
	bl	ftl_memset
	mov	w0, -3887
	strh	w0, [x27]
	ldr	w0, [x21,8]
	str	w0, [x27,4]
	ldrh	w0, [x20,2104]
	strh	w0, [x27,2]
	ldrh	w0, [x21,4]
	strh	w0, [x27,8]
	ldrh	w0, [x21,6]
	strh	w0, [x27,10]
	ldr	w0, [x20,1940]
	strh	w0, [x27,12]
.L1942:
	add	x23, x22, :lo12:.LANCHOR2
	ldrh	w1, [x20,2104]
	ldrh	w2, [x20,2106]
	ldrh	w3, [x20,2108]
	ldr	x0, [x23,-72]
	str	x0, [x19,8]
	ldr	x0, [x23,2104]
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
	blt	.L1943
	ldr	w1, [x20,2112]
	ldrh	w0, [x20,2104]
	add	w1, w1, 1
	str	w1, [x20,2112]
	str	w1, [x27,4]
	strh	w0, [x27,8]
	ldrh	w1, [x20,2108]
	strh	w0, [x20,2108]
	ldr	x0, [x23,-96]
	strh	w1, [x20,2104]
	lsl	w1, w1, 10
	str	w1, [x19,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	strh	wzr, [x20,2106]
	ldr	x0, [x23,-96]
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x19
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
.L1943:
	ldrh	w0, [x20,2106]
	add	w0, w0, 1
	strh	w0, [x20,2106]
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1944
	ldr	w1, [x19,4]
	mov	x0, x26
	add	w28, w28, 1
	bl	printk
	uxth	w28, w28
	cmp	w28, 3
	bls	.L1942
	adrp	x0, .LC113
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC113
	mov	w2, w28
	bl	printk
.L1946:
	b	.L1946
.L1944:
	cbnz	w24, .L1949
	mov	w24, 1
	b	.L1942
.L1949:
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
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x20, x0
	add	x23, x21, 88
	mov	x19, x21
	sub	x24, x21, #24
.L1951:
	cmp	x20, x23
	bne	.L1952
	ldrh	w0, [x21,-16]
	ldr	w3, [x21,868]
	lsr	w2, w0, 1
	add	w4, w2, 1
	mul	w1, w0, w3
	add	w1, w4, w1, lsr 2
	ldr	w4, [x21,152]
	uxth	w1, w1
	cbz	w4, .L1953
	ldr	w4, [x21,212]
	cmp	w4, 29
	bhi	.L1953
	cmp	w4, 2
	mov	w1, 0
	bls	.L1954
	tbz	x0, 0, .L1979
	cbz	w3, .L1954
.L1979:
	mov	w1, w2
	b	.L1953
.L1952:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L1954
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w2, [x0,1964]
	cmp	w2, 1
	beq	.L1954
	ldrb	w0, [x0,120]
	cbnz	w0, .L1954
	ldr	w2, [x21,152]
	ldrh	w0, [x21,-16]
	lsr	w1, w0, 3
	cbz	w2, .L1953
	ldr	w2, [x21,212]
	cmp	w2, 1
	bhi	.L1953
	mov	w1, 7
	mul	w1, w0, w1
	lsr	w1, w1, 3
.L1953:
	cbz	w1, .L1954
	sub	w1, w1, #1
	uxth	w1, w1
.L1954:
	mov	x0, x24
	bl	List_pop_index_node
	uxth	w22, w0
	ldrh	w0, [x19,-16]
	sub	w0, w0, #1
	strh	w0, [x19,-16]
	strh	w22, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L1956
	adrp	x25, .LANCHOR0
	mov	x3, 56
	add	x0, x25, :lo12:.LANCHOR0
	mov	w6, 65535
	ldrh	w5, [x0,1944]
	mov	x0, 0
	mov	w26, w0
	b	.L1957
.L1956:
	ubfiz	x22, x22, 1, 16
	ldr	x0, [x19,-48]
	mov	w1, -1
	strh	w1, [x0,x22]
	b	.L1951
.L1960:
	ldr	x4, [x19,-96]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L1959
	umull	x2, w26, w3
	add	w26, w26, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w26, w26
	str	w1, [x2,4]
.L1959:
	add	x0, x0, 1
.L1957:
	cmp	w5, w0, uxth
	bhi	.L1960
	sub	x0, x19, #8
	uxtw	x27, w22
	cmp	x20, x0
	bne	.L1961
	ldr	w1, [x19,152]
	cbz	w1, .L1961
	ldr	x1, [x19,-88]
	ldrh	w1, [x1,x27,lsl 1]
	cmp	w1, 30
	bls	.L1961
	strb	wzr, [x19]
.L1961:
	ldrb	w0, [x20,8]
	cbnz	w0, .L1962
	lsl	x0, x27, 1
	ldr	x1, [x19,-88]
	mov	w2, 2
	ldrh	w3, [x1,x0]
	cbz	w3, .L2001
	add	x2, x25, :lo12:.LANCHOR0
	ldrh	w2, [x2,2008]
	add	w2, w3, w2
.L2001:
	strh	w2, [x1,x0]
	mov	w1, 0
	ldr	w0, [x19,196]
	add	w0, w0, 1
	str	w0, [x19,196]
	mov	w0, w22
	bl	ftl_set_blk_mode
	b	.L1965
.L1962:
	lsl	x0, x27, 1
	ldr	x2, [x19,-88]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x19,200]
	add	w0, w0, 1
	str	w0, [x19,200]
	mov	w0, w22
	bl	ftl_set_blk_mode.part.8
.L1965:
	lsl	x27, x27, 1
	ldr	x0, [x19,-88]
	ldr	w1, [x19,208]
	ldrh	w0, [x0,x27]
	cmp	w0, w1
	bls	.L1966
	str	w0, [x19,208]
.L1966:
	add	x2, x25, :lo12:.LANCHOR0
	ldr	w3, [x19,196]
	ldr	w0, [x19,200]
	ldrh	w1, [x2,2008]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,1952]
	mov	x3, 56
	udiv	w0, w0, w1
	ldr	x1, [x19,2144]
	str	w0, [x19,204]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L1967:
	cmp	w26, w0, uxth
	bls	.L2002
	ldr	x1, [x19,-96]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L1967
.L2002:
	ldrb	w1, [x20,8]
	mov	w2, w26
	ldr	x0, [x19,-96]
	mov	x28, 0
	bl	FlashEraseBlocks
	mov	w1, w28
	mov	x3, 56
.L1969:
	cmp	w26, w28, uxth
	bls	.L2003
	mul	x0, x28, x3
	ldr	x2, [x19,-96]
	add	x4, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L1970
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
.L1970:
	add	x28, x28, 1
	b	.L1969
.L2003:
	cbz	w1, .L1972
	mov	w0, w22
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1972:
	ldrb	w0, [x20,7]
	cbnz	w0, .L1973
	ldr	x0, [x19,-48]
	mov	w1, -1
	strh	w1, [x0,x27]
	b	.L1951
.L1973:
	add	x25, x25, :lo12:.LANCHOR0
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x25,2018]
	strh	w22, [x20]
	mul	w0, w0, w1
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	uxth	w0, w0
	strh	w0, [x20,4]
	ldr	w2, [x1,188]
	str	w2, [x20,12]
	add	w2, w2, 1
	str	w2, [x1,188]
	ldr	x1, [x1,-48]
	ldrh	w2, [x20]
	strh	w0, [x1,x2,lsl 1]
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
	ldrh	w0, [x19,948]
	stp	x21, x22, [sp,32]
	mov	w21, 0
	cbz	w0, .L2006
	adrp	x22, .LANCHOR0
	adrp	x24, .LC114
	add	x22, x22, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LC114
.L2014:
	ldrh	w0, [x22,1944]
	cmp	w0, w21
	bls	.L2015
	add	x0, x22, 1976
	mov	w1, w23
	mov	w20, 0
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w25, w0
.L2007:
	ldrh	w0, [x19,948]
	cmp	w0, w20
	bls	.L2016
	add	x0, x19, 952
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w25
	bne	.L2008
	mov	w1, w25
	mov	x0, x24
	bl	printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,948]
	mov	w3, w20
.L2009:
	cmp	w3, w2
	bcs	.L2017
	add	x4, x19, 952
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2009
.L2017:
	sub	w2, w2, #1
	strh	w2, [x19,948]
.L2008:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2007
.L2016:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2014
.L2015:
	bl	FtlGcReFreshBadBlk
.L2006:
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
	ldr	x2, [x1,-48]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2019
	ldrh	w0, [x1,288]
	cmp	w0, w19
	bne	.L2020
	mov	w0, -1
	strh	w0, [x1,288]
	b	.L2021
.L2020:
	ldrh	w3, [x1,-8]
	mov	w0, w2
	cmp	w3, w19
	beq	.L2022
	ldrh	w3, [x1,40]
	cmp	w3, w19
	beq	.L2022
	ldrh	w1, [x1,88]
	cmp	w1, w19
	beq	.L2022
.L2021:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w19
	sub	x0, x20, #56
	bl	List_remove_node
	ldrh	w0, [x20,-32]
	sub	w0, w0, #1
	strh	w0, [x20,-32]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L2022
.L2019:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2022:
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
	beq	.L2027
	add	x0, x2, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x0, [x0,-48]
	ldrh	w19, [x0,x3]
	cbnz	w19, .L2028
	adrp	x0, .LC115
	mov	w2, w19
	add	x0, x0, :lo12:.LC115
	bl	printk
	b	.L2032
.L2028:
	sub	w19, w19, #1
	strh	w19, [x0,x3]
.L2027:
	add	x19, x2, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w2, [x19,2312]
	cmp	w2, w0
	bne	.L2030
	strh	w1, [x19,2312]
.L2032:
	mov	w0, 0
	b	.L2029
.L2030:
	cmp	w2, w1
	str	x1, [x29,40]
	mov	w0, 0
	beq	.L2029
	mov	w0, w2
	bl	update_vpc_list
	cmp	w0, wzr
	ldr	x1, [x29,40]
	cset	w0, ne
	strh	w1, [x19,2312]
.L2029:
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
	cbz	w1, .L2033
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2033
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR2
	adrp	x22, .LANCHOR0
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR2
	add	x21, x21, 988
	ldrh	w0, [x19,x0,lsl 1]
.L2037:
	cmp	w0, w20
	bne	.L2044
.L2039:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1944]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2038
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2038:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2037
.L2044:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2040
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2040
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w20
	bne	.L2040
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2039
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2033
.L2040:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,120]
	cbz	w2, .L2033
	cmp	w1, 1
	bne	.L2033
	ldrh	w2, [x19,2]
	ldrh	w1, [x0,2020]
	cmp	w2, w1
	bcc	.L2033
	adrp	x2, .LANCHOR2-48
	ldrh	w1, [x19]
	ldrh	w4, [x19,4]
	lsl	x1, x1, 1
	ldr	x3, [x2,#:lo12:.LANCHOR2-48]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,2018]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2033:
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
.L2046:
	cmp	w0, w22
	adrp	x20, .LANCHOR0
	bne	.L2061
.L2047:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1944]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2048
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2048:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2046
.L2061:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2049
	add	x1, x23, :lo12:.LANCHOR0
	ldrb	w1, [x1,120]
	cbnz	w1, .L2049
	ldrh	w1, [x19,2]
	ldrh	w1, [x21,w1,sxtw 1]
	cmp	w1, w22
	bne	.L2049
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2047
.L2049:
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
.L2050:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,1944]
.L2052:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2051
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2051:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2052
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2057
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2054
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w24
	bne	.L2054
	ldrh	w1, [x19,4]
	cbz	w1, .L2054
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2050
.L2054:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,120]
	cbz	w0, .L2057
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,2020]
	cmp	w1, w0
	bcc	.L2057
	adrp	x0, .LANCHOR2-48
	ldrh	w1, [x19]
	ldrh	w3, [x19,4]
	lsl	x1, x1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR2-48]
	ldrh	w2, [x0,x1]
	sub	w2, w2, w3
	strh	w2, [x0,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x20,2018]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2057:
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR4
	adrp	x24, .LANCHOR0
	add	x20, x23, :lo12:.LANCHOR4
	ldr	x0, [x19,-72]
	stp	x21, x22, [sp,32]
	ldr	x22, [x19,2104]
	add	x21, x20, 136
	stp	x25, x26, [sp,64]
	str	x0, [x20,144]
	add	x25, x24, :lo12:.LANCHOR0
	str	x22, [x20,152]
	add	x20, x19, 216
	ldrh	w1, [x19,272]
	mov	w26, 65535
	strh	w1, [x22,2]
	mov	w1, -3932
	strh	w1, [x22]
	str	wzr, [x22,12]
	ldr	w1, [x19,280]
	str	w1, [x22,4]
	mov	w1, 19539
	str	wzr, [x22,8]
	movk	w1, 0x4654, lsl 16
	ldrh	w0, [x19,278]
	strh	w0, [x19,224]
	ldrh	w0, [x25,1966]
	str	w1, [x19,216]
	mov	w1, 64
	strb	w0, [x19,226]
	movk	w1, 0x5000, lsl 16
	ldrh	w0, [x19,-8]
	strh	w0, [x19,230]
	ldrh	w0, [x19,-6]
	str	w1, [x19,220]
	ldrb	w1, [x19,-2]
	orr	w0, w1, w0, lsl 6
	strh	w0, [x19,232]
	ldrb	w0, [x19]
	ldrh	w1, [x19,40]
	strh	w1, [x19,234]
	ldrh	w1, [x19,42]
	ldrb	w2, [x19,46]
	strb	w0, [x19,227]
	orr	w1, w2, w1, lsl 6
	ldrb	w0, [x19,48]
	strh	w1, [x19,236]
	strb	w0, [x19,228]
	ldrh	w1, [x19,88]
	ldrb	w0, [x19,96]
	strh	w1, [x19,238]
	strb	w0, [x19,229]
	ldrh	w1, [x19,90]
	ldr	w0, [x19,196]
	ldrb	w2, [x19,94]
	str	w0, [x19,248]
	ldr	w0, [x19,188]
	orr	w1, w2, w1, lsl 6
	str	w0, [x19,256]
	ldr	w0, [x19,192]
	ldrh	w2, [x25,2028]
	str	w0, [x19,252]
	ldr	x0, [x21,8]
	strh	w1, [x19,240]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x21,8]
	mov	x1, x20
	mov	w2, 48
	mov	x20, x21
	bl	ftl_memcpy
	ldrh	w2, [x25,1952]
	ldr	x0, [x21,8]
	ldr	x1, [x19,-48]
	lsl	w2, w2, 1
	add	x0, x0, 48
	bl	ftl_memcpy
	ldrh	w2, [x25,1952]
	mov	w25, 0
	ldr	x1, [x21,8]
	uxth	x0, w2
	lsr	w2, w2, 3
	add	x0, x0, 24
	add	w2, w2, 4
	lsr	x0, x0, 1
	add	x0, x1, x0, lsl 2
	ldr	x1, [x24,#:lo12:.LANCHOR0]
	bl	ftl_memcpy
	mov	w0, 0
	bl	FtlUpdateVaildLpn
.L2063:
	ldr	x0, [x19,-72]
	str	x0, [x20,8]
	ldr	x0, [x19,2104]
	str	x0, [x20,16]
	ldrh	w0, [x19,272]
	ldrh	w1, [x19,274]
	orr	w0, w1, w0, lsl 10
	mov	w1, 1
	str	w0, [x20,4]
	mov	w2, w1
	mov	w3, w1
	mov	x0, x20
	bl	FlashProgPages
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x19,274]
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2064
	ldrh	w0, [x19,272]
	ldrh	w26, [x19,276]
	strh	wzr, [x19,274]
	strh	w0, [x19,276]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,188]
	str	w1, [x19,280]
	add	w2, w1, 1
	str	w2, [x19,188]
	ubfiz	w2, w0, 10, 16
	str	w2, [x20,4]
	str	w1, [x22,4]
	mov	w1, 1
	strh	w0, [x19,272]
	mov	w2, w1
	strh	w0, [x22,2]
	mov	w3, w1
	mov	x0, x20
	bl	FlashProgPages
.L2064:
	ldrh	w1, [x19,274]
	ldr	w2, [x20]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x19,274]
	bne	.L2065
	cmp	w1, 1
	bne	.L2066
	add	x1, x24, :lo12:.LANCHOR0
	ldrh	w1, [x1,2020]
	sub	w1, w1, #1
	strh	w1, [x19,274]
.L2066:
	add	w25, w25, 1
	uxth	w25, w25
	cmp	w25, 3
	bls	.L2063
	add	x23, x23, :lo12:.LANCHOR4
	adrp	x0, .LC116
	add	x0, x0, :lo12:.LC116
	mov	w2, w25
	ldr	w1, [x23,140]
	bl	printk
.L2068:
	b	.L2068
.L2065:
	cmp	w1, 1
	beq	.L2063
	mov	w0, 65535
	cmp	w26, w0
	beq	.L2069
	mov	w0, w26
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L2069:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	mov	x19, x0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w22, 0
	ldrb	w0, [x0,120]
	cbz	w0, .L2074
	ldrb	w0, [x19,8]
	cmp	w0, 1
	cset	w22, eq
.L2074:
	mov	w21, 7
	adrp	x23, .LANCHOR2
	mov	w24, -1
.L2075:
	subs	w21, w21, #1
	beq	.L2077
	ldrh	w0, [x19,4]
	cbnz	w0, .L2076
.L2077:
	adrp	x1, .LANCHOR2
	ldrh	w0, [x19]
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w3, [x19,4]
	lsl	x0, x0, 1
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x2, [x1,-48]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x20,2018]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	ldp	x21, x22, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 128
	ret
.L2076:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2077
	add	x3, x23, :lo12:.LANCHOR2
	str	w24, [x29,96]
	mov	w2, w22
	ldr	x1, [x3,2104]
	ldr	x0, [x3,-72]
	str	x0, [x29,80]
	str	x1, [x29,88]
	str	w24, [x1,8]
	str	w24, [x1,12]
	ldrh	w0, [x19]
	strh	w0, [x1,2]
	strh	wzr, [x1]
	ldr	w0, [x3,192]
	str	w0, [x1,4]
	mov	w1, 1
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x3,192]
	add	x0, x29, 72
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2075
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x19, x0
	adrp	x22, .LANCHOR0
	ldr	x20, [x0,16]
	ldr	x23, [x0,40]
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19,10]
	ldrh	w2, [x19,8]
	sub	w1, w1, #4
	cmp	w2, w1
	blt	.L2088
	ubfiz	x1, x0, 1, 16
	ldrh	w24, [x20,x1]
	cbz	w24, .L2088
	ldr	w0, [x19,52]
	cbnz	w0, .L2088
	mov	w2, 1
	str	w2, [x19,52]
	strh	w0, [x20,x1]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	cmp	w1, w0
	bcc	.L2089
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2089:
	adrp	x20, .LANCHOR4
	mov	w21, 0
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x26, .LANCHOR2
	add	x20, x20, 136
.L2090:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L2098
	ubfiz	x25, x21, 2, 16
	ldr	w0, [x23,x25]
	cmp	w24, w0, lsr 10
	bne	.L2091
	add	x0, x26, :lo12:.LANCHOR2
	ldr	x1, [x0,2064]
	ldr	x0, [x0,2104]
	str	x1, [x20,8]
	mov	w1, 1
	str	x0, [x20,16]
	mov	w2, w1
	ldr	w0, [x23,x25]
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L2092
	str	wzr, [x23,x25]
	b	.L2091
.L2092:
	ldr	x2, [x20,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L2091:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2090
.L2098:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L2088:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x22,2020]
	cmp	w1, w0
	bcc	.L2094
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2094:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	stp	x29, x30, [sp, -64]!
	mov	w1, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	mov	x19, x0
	ldr	x20, [x0,16]
	ldr	x23, [x0,40]
	ldrh	w0, [x0]
	cmp	w0, w1
	bne	.L2100
	ldrh	w0, [x19,8]
	add	w0, w0, 1
	strh	w0, [x19,8]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x20]
	strh	wzr, [x19,2]
	ldr	w0, [x19,48]
	strh	wzr, [x19]
	add	w0, w0, 1
	str	w0, [x19,48]
	b	.L2101
.L2100:
	ubfiz	x0, x0, 1, 16
	adrp	x21, .LANCHOR2
	add	x3, x21, :lo12:.LANCHOR2
	ldrh	w22, [x20,x0]
	adrp	x20, .LANCHOR4
	ldrh	w0, [x19,2]
	add	x1, x20, :lo12:.LANCHOR4
	orr	w0, w0, w22, lsl 10
	str	w0, [x1,140]
	ldr	x0, [x3,-72]
	str	x0, [x1,144]
	ldr	x0, [x3,2104]
	str	x0, [x1,152]
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
	ldr	x0, [x3,-72]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, 0
	mov	w1, w0
.L2102:
	ldrh	w2, [x19,6]
	cmp	w2, w1
	bls	.L2105
	ubfiz	x4, x1, 2, 16
	ldr	w2, [x23,x4]
	cmp	w22, w2, lsr 10
	bne	.L2103
	add	x3, x21, :lo12:.LANCHOR2
	add	w0, w0, 1
	uxth	w0, w0
	ldr	x5, [x3,-72]
	ubfiz	x2, x0, 3, 16
	str	w1, [x5,x2]
	ldr	x3, [x3,-72]
	ldr	w4, [x23,x4]
	add	x2, x3, x2
	str	w4, [x2,4]
.L2103:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2102
.L2105:
	mov	w1, 1
	add	x0, x20, :lo12:.LANCHOR4
	add	x0, x0, 136
	mov	w2, w1
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	strh	w0, [x19,2]
	mov	x0, x19
	bl	ftl_map_blk_gc
.L2101:
	mov	w0, 0
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	mov	x19, x0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x25, .LANCHOR2
	adrp	x23, .LC117
	mov	w24, w1
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	x26, x2
	mov	w21, 0
	mov	x27, x25
	add	x20, x20, 136
	add	x23, x23, :lo12:.LC117
.L2107:
	add	x1, x25, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR0
	ldr	w0, [x1,172]
	add	w0, w0, 1
	str	w0, [x1,172]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L2108
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2109
.L2108:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L2109:
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19,16]
	ldrh	w28, [x0,x1,lsl 1]
	mov	w1, 0
	ldrh	w0, [x19,2]
	str	x26, [x20,8]
	orr	w0, w0, w28, lsl 10
	str	w0, [x20,4]
	add	x0, x27, :lo12:.LANCHOR2
	ldr	x0, [x0,2104]
	str	x0, [x20,16]
	bl	ftl_memset
	ldr	x0, [x20,16]
	ldr	w1, [x19,48]
	str	w1, [x0,4]
	strh	w24, [x0,8]
	ldrh	w1, [x19,4]
	strh	w1, [x0]
	mov	w1, 1
	strh	w28, [x0,2]
	mov	w3, w1
	mov	x0, x20
	mov	w2, w1
	bl	FlashProgPages
	ldrh	w3, [x19,2]
	add	w3, w3, 1
	uxth	w3, w3
	strh	w3, [x19,2]
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L2110
	ldr	w1, [x20,4]
	mov	x0, x23
	add	w21, w21, 1
	bl	printk
	uxth	w21, w21
	ldrh	w0, [x19,2]
	cmp	w0, 2
	bhi	.L2111
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w0, [x22,2020]
	sub	w0, w0, #1
	strh	w0, [x19,2]
.L2111:
	cmp	w21, 3
	bls	.L2112
	adrp	x1, .LANCHOR4
	adrp	x0, .LC118
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC118
	mov	w2, w21
	ldr	w1, [x1,140]
	bl	printk
.L2113:
	b	.L2113
.L2112:
	ldr	w0, [x19,52]
	cbz	w0, .L2107
.L2122:
	b	.L2122
.L2110:
	cmp	w3, 1
	beq	.L2107
	ldr	x0, [x19,40]
	ldr	w1, [x20,4]
	str	w1, [x0,w24,uxtw 2]
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
	.global	flush_l2p_region
	.type	flush_l2p_region, %function
flush_l2p_region:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ubfiz	x19, x0, 4, 16
	add	x20, x20, :lo12:.LANCHOR2
	add	x0, x20, 2232
	ldr	x1, [x20,136]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20,136]
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
	ldr	x25, [x1,136]
	lsr	w20, w0, w3
	lsl	x3, x4, x3
	sub	w3, w3, #1
	mov	x4, 0
	and	w0, w3, w0
	uxth	w20, w20
	uxth	x24, w0
	ldrh	w0, [x5,2058]
.L2125:
	uxth	x19, w4
	cmp	w19, w0
	bcs	.L2137
	add	x4, x4, 1
	add	x1, x25, x4, lsl 4
	ldrh	w1, [x1,-16]
	cmp	w1, w20
	bne	.L2125
.L2126:
	cbnz	w22, .L2127
	add	x0, x23, :lo12:.LANCHOR2
	ldr	x1, [x0,136]
	add	x1, x1, x19, lsl 4
	ldr	x0, [x1,8]
	ldr	w0, [x0,x24,lsl 2]
	str	w0, [x21]
	b	.L2128
.L2127:
	add	x1, x23, :lo12:.LANCHOR2
	lsl	x0, x19, 4
	ldr	w3, [x21]
	ldr	x2, [x1,136]
	add	x2, x2, x0
	ldr	x2, [x2,8]
	str	w3, [x2,x24,lsl 2]
	strh	w20, [x1,144]
	ldr	x2, [x1,136]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2128:
	add	x23, x23, :lo12:.LANCHOR2
	ldr	x0, [x23,136]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19,4]
	cmn	w0, #1
	beq	.L2134
	add	w0, w0, 1
	str	w0, [x19,4]
	b	.L2134
.L2137:
	bl	select_l2p_ram_region
	uxth	x19, w0
	ubfiz	x2, x19, 4, 16
	mov	w1, w0
	add	x3, x25, x2
	ldrh	w4, [x25,x2]
	mov	w2, 65535
	cmp	w4, w2
	beq	.L2131
	ldr	w2, [x3,4]
	tbz	w2, #31, .L2131
	str	x1, [x29,88]
	bl	flush_l2p_region
	ldr	x1, [x29,88]
.L2131:
	mov	w0, w20
	bl	load_l2p_region
	b	.L2126
.L2134:
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
	ldr	x3, [x2,-48]
	ldrh	w1, [x3,x22]
	cbnz	w1, .L2139
	ldr	x19, [x2,-24]
	cbz	x19, .L2140
	ldrh	w3, [x2,-16]
	mov	x4, -6148914691236517206
	ldr	x2, [x2,-64]
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L2141:
	cmp	w1, w3
	beq	.L2140
	cmp	w19, w0
	bne	.L2142
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w19
	sub	x0, x21, #24
	bl	List_remove_node
	ldrh	w0, [x21,-16]
	sub	w0, w0, #1
	strh	w0, [x21,-16]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x1, [x21,-48]
	ldrh	w0, [x1,x22]
	add	w0, w0, 1
	strh	w0, [x1,x22]
	b	.L2140
.L2142:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2140
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2141
.L2139:
	add	w1, w1, 1
	strh	w1, [x3,x22]
.L2140:
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
	stp	x27, x28, [sp,80]
	mov	x27, x0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	ldrh	w22, [x27,6]
	adrp	x19, .LANCHOR2
	ldr	x20, [x0,40]
	mov	w25, 0
	ldr	x21, [x0,16]
	lsl	w2, w22, 2
	ldr	x0, [x0,24]
	adrp	x24, .LANCHOR0
	str	x0, [x29,128]
	mov	x0, x20
	ldrh	w23, [x27,8]
	bl	ftl_memset
	str	x19, [x29,136]
	add	x1, x19, :lo12:.LANCHOR2
	adrp	x0, .LANCHOR4
	add	x5, x0, :lo12:.LANCHOR4
	str	x0, [x29,120]
	add	x5, x5, 136
	sub	w0, w23, #1
	ldr	x2, [x1,-72]
	ldr	x26, [x1,2104]
	mov	w1, -1
	str	x2, [x5,8]
	str	x26, [x5,16]
	mov	x19, x5
	strh	w1, [x27]
	strh	w1, [x27,2]
	mov	w1, 1
	str	wzr, [x27,52]
	str	wzr, [x27,48]
	str	w1, [x27,56]
	str	w0, [x29,116]
.L2148:
	cmp	w25, w23
	bge	.L2164
	ldr	w0, [x29,116]
	sxtw	x28, w25
	cmp	w25, w0
	bne	.L2149
	lsl	x0, x28, 1
	mov	w1, 1
	add	x23, x21, x0
	mov	w19, 0
	ldrh	w0, [x21,x0]
	bl	FtlGetLastWrittenPage
	sxth	w24, w0
	add	w0, w0, 1
	strh	w0, [x27,2]
	ldr	x0, [x29,128]
	add	w24, w24, 1
	strh	w25, [x27]
	ldr	w0, [x0,x28,lsl 2]
	str	w0, [x27,48]
	ldr	x0, [x29,120]
	add	x21, x0, :lo12:.LANCHOR4
	add	x21, x21, 136
.L2150:
	cmp	w19, w24
	bge	.L2164
	ldrh	w0, [x23]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w19, w0, lsl 10
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L2151
	ldrh	w0, [x26,8]
	cmp	w0, w22
	bcs	.L2151
	ldrh	w2, [x26]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2151
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x21,4]
	str	w1, [x20,x0]
.L2151:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2150
.L2164:
	mov	x0, x27
	bl	ftl_free_no_use_map_blk
	adrp	x0, .LANCHOR0+2020
	ldrh	w1, [x27,2]
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2020]
	cmp	w1, w0
	bne	.L2154
	mov	x0, x27
	bl	ftl_map_blk_alloc_new_blk
	b	.L2154
.L2149:
	ldr	x0, [x29,136]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,-72]
	str	x0, [x19,8]
	lsl	x0, x28, 1
	add	x28, x21, x0
	ldrh	w1, [x21,x0]
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2165
	ldrh	w1, [x26]
	mov	w4, 0
	ldrh	w0, [x27,4]
	cmp	w1, w0
	bne	.L2174
	ldrh	w1, [x26,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2174
.L2156:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	cmp	w4, w0
	bge	.L2159
	ldr	x0, [x29,136]
	sbfiz	x1, x4, 3, 32
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x2, [x0,-72]
	ldrh	w0, [x2,x1]
	cmp	w0, w22
	bcs	.L2157
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x1,4]
	str	w1, [x20,x0]
.L2157:
	add	w4, w4, 1
	sxth	w4, w4
	b	.L2156
.L2165:
	mov	w4, 0
.L2174:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	cmp	w4, w0
	bge	.L2159
	ldrh	w0, [x28]
	mov	w1, 1
	mov	w2, w1
	str	x4, [x29,104]
	orr	w0, w4, w0, lsl 10
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	ldr	x4, [x29,104]
	cmn	w0, #1
	beq	.L2160
	ldrh	w0, [x26,8]
	cmp	w0, w22
	bcs	.L2160
	ldrh	w2, [x26]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2160
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x19,4]
	str	w1, [x20,x0]
.L2160:
	add	w4, w4, 1
	sxth	w4, w4
	b	.L2174
.L2159:
	add	w6, w25, 1
	sxth	w25, w6
	b	.L2148
.L2154:
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
	add	x0, x0, 200
	add	x29, sp, 0
	ldrh	w2, [x1,2040]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,2064]
	strh	w2, [x0,8]
	ldrh	w2, [x1,2042]
	ldr	x1, [x1,2072]
	str	x1, [x0,16]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	strh	w2, [x0,6]
	ldr	x2, [x1,2184]
	str	x2, [x0,24]
	ldr	x2, [x1,2176]
	ldr	x1, [x1,2192]
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
	add	x0, x0, 2232
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
	add	x2, x0, 344
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR0
	ldr	w3, [x2,80]
	cbz	w3, .L2178
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w3, [x2,84]
	ldr	w1, [x1,2096]
	cmp	w3, w1
	bcs	.L2179
	mov	w21, 2048
	mov	x19, x0
.L2184:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x19,428]
	ldr	w1, [x1,2096]
	cmp	w0, w1
	bcs	.L2181
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x19,428]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x19,428]
	cmn	w1, #1
	beq	.L2182
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
	bne	.L2181
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2181
.L2182:
	subs	w21, w21, #1
	bne	.L2184
.L2181:
	mov	w0, -1
	b	.L2186
.L2179:
	ldr	w0, [x0,156]
	str	wzr, [x2,80]
	str	wzr, [x2,84]
	str	w0, [x2,76]
	b	.L2192
.L2178:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	w3, [x0,208]
	mov	w2, 4194304
	ldr	w0, [x0,152]
	ldrb	w4, [x20,120]
	cbnz	w4, .L2187
	add	w0, w0, w3, lsr 10
	mov	w3, 33554432
	asr	w2, w3, w0
.L2187:
	add	x4, x1, :lo12:.LANCHOR2
	ldr	w3, [x4,156]
	ldr	w0, [x4,420]
	add	w5, w3, 1048576
	cmp	w0, w5
	bhi	.L2188
	add	w0, w2, w0
	cmp	w0, w3
	bcc	.L2188
	ldrb	w2, [x4,244]
	mov	w0, 0
	cbnz	w2, .L2186
.L2188:
	add	x0, x1, :lo12:.LANCHOR2
	mov	w1, 1
	str	wzr, [x0,428]
	str	w1, [x0,424]
	str	w3, [x0,420]
.L2192:
	mov	w0, 0
.L2186:
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
	bhi	.L2194
	adrp	x24, .LANCHOR4
	lsr	w21, w26, w21
	add	x24, x24, :lo12:.LANCHOR4
	mov	w20, 0
	adrp	x27, .LANCHOR2
	add	x24, x24, 200
.L2195:
	cbz	w28, .L2194
	adrp	x0, .LANCHOR2
	add	x2, x0, :lo12:.LANCHOR2
	ldr	x0, [x2,2192]
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
	beq	.L2197
	cbz	w4, .L2197
	ldr	x0, [x2,2072]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w4, [x29,124]
	str	xzr, [x29,136]
	bl	FlashReadPages
	b	.L2198
.L2197:
	add	x0, x27, :lo12:.LANCHOR2
	add	x2, x23, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x0,2072]
	ldrh	w2, [x2,2028]
	bl	ftl_memset
.L2198:
	add	x5, x27, :lo12:.LANCHOR2
	lsl	w4, w19, 9
	ubfiz	x22, x22, 9, 16
	mov	w2, w4
	mov	x1, x25
	str	x4, [x29,104]
	ldr	x0, [x5,2072]
	sub	w28, w28, w19
	str	x5, [x29,96]
	add	w26, w26, w19
	add	x0, x0, x22
	bl	ftl_memcpy
	ldr	x5, [x29,96]
	mov	w1, w21
	mov	x0, x24
	add	w21, w21, 1
	ldr	x2, [x5,2072]
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	x4, [x29,104]
	csinv	w20, w20, wzr, ne
	add	x25, x25, x4, sxtw
	b	.L2195
.L2194:
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
	add	x2, x0, 344
	add	x29, sp, 0
	ldr	w3, [x0,344]
	cmp	w3, w1
	bne	.L2208
	mov	w1, 64
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	ldr	w1, [x0,180]
	str	w1, [x2,88]
	ldr	w1, [x0,184]
	str	w1, [x2,92]
	ldr	w1, [x0,176]
	str	w1, [x2,8]
	ldr	w1, [x0,164]
	str	w1, [x2,12]
	ldr	w1, [x0,156]
	str	w1, [x2,16]
	ldr	w1, [x0,172]
	str	w1, [x2,20]
	ldr	w1, [x0,200]
	str	w1, [x2,28]
	ldr	w1, [x0,-80]
	str	w1, [x2,32]
	ldr	w1, [x0,160]
	str	w1, [x2,36]
	ldr	w1, [x0,168]
	str	w1, [x2,40]
	ldr	w1, [x0,208]
	str	w1, [x2,44]
	ldr	w1, [x0,212]
	str	w1, [x2,48]
	ldr	w1, [x0,336]
	ldr	w0, [x0,2016]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2208:
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
	ldr	w4, [x3,152]
	cbz	w4, .L2211
	ldr	w3, [x3,212]
	cmp	w3, 29
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2211:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,264]
	cmp	w4, 31
	bhi	.L2212
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,264]
.L2212:
	cbnz	w0, .L2213
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,2144]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2214
.L2213:
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x0, [x2,2144]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	ldr	x0, [x2,2144]
	ldrh	w1, [x2,2128]
	str	wzr, [x0,4]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	ldr	x2, [x2,2144]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2214:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	StorageSysDataStore
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x0, [x29,16]
	str	x1, [x29,24]
	bl	rknand_device_lock
	ldr	x0, [x29,16]
	mov	w1, 1
	ldr	x2, [x29,24]
	add	w0, w0, 512
	bl	FtlVendorPartWrite
	str	x0, [x29,24]
	bl	rknand_device_unlock
	ldr	x0, [x29,24]
	ldp	x29, x30, [sp], 32
	ret
	.size	StorageSysDataStore, .-StorageSysDataStore
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
	cbz	w1, .L2219
	adrp	x3, .LANCHOR2
	adrp	x2, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR2
	add	x1, x2, :lo12:.LANCHOR4
	sub	w20, w20, #1
	str	wzr, [x0,56]
	mov	x21, x0
	add	x1, x1, 136
	ldr	x0, [x3,2064]
	uxth	w20, w20
	ldr	x3, [x3,2104]
	mov	x19, x2
	str	x3, [x1,16]
	ubfiz	x3, x20, 2, 16
	str	x0, [x1,8]
	ldr	w3, [x4,x3]
	str	w3, [x1,4]
	cbz	w3, .L2221
	mov	x0, x1
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L2222
.L2221:
	adrp	x2, .LANCHOR0+2028
	mov	w1, 255
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2028]
	bl	ftl_memset
.L2222:
	add	x2, x19, :lo12:.LANCHOR4
	mov	x0, x21
	mov	w1, w20
	ldr	x2, [x2,144]
	bl	FtlMapWritePage
.L2219:
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
	beq	.L2382
	ldrh	w0, [x19,2]
	adrp	x23, .LANCHOR0
	str	w0, [x29,188]
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w27, [x19,6]
	ldr	w1, [x29,188]
	ldrh	w0, [x0,2018]
	cmp	w0, w1
	bne	.L2229
	strh	wzr, [x19,4]
	b	.L2388
.L2229:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2230:
	cmp	w0, w2
	bne	.L2391
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2230
.L2391:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2232
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2233
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, w21
	ldrb	w1, [x0,120]
	cbnz	w1, .L2305
	add	x0, x0, 196
	ldrh	w24, [x0,w21,sxtw 1]
	b	.L2305
.L2232:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2233
	mov	w24, w0
.L2305:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w28, 0
	mov	w7, 65535
	mov	w8, 56
	adrp	x9, .LANCHOR2
	ldrh	w3, [x0,1944]
	mov	w10, 4
	ldrh	w6, [x0,2030]
	add	x3, x19, x3, lsl 1
	b	.L2235
.L2233:
	strh	wzr, [x19,2]
.L2388:
	strb	wzr, [x19,6]
	b	.L2382
.L2237:
	ldrh	w1, [x2,16]
	cmp	w1, w7
	beq	.L2236
	add	x4, x9, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x0, w28, w8
	ldr	x5, [x4,2040]
	add	x5, x5, x0
	str	w1, [x5,4]
	ldr	x1, [x4,2040]
	ldr	x4, [x4,896]
	add	x0, x1, x0
	mul	w1, w28, w6
	add	w28, w28, 1
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	uxth	w28, w28
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L2236:
	add	x2, x2, 2
.L2235:
	cmp	x2, x3
	bne	.L2237
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2238
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2238:
	adrp	x20, .LANCHOR2
	ldr	w2, [x29,184]
	add	x25, x20, :lo12:.LANCHOR2
	mov	w1, w28
	mov	w26, 0
	ldr	x0, [x25,2040]
	bl	FlashReadPages
	ldr	w22, [x25,192]
	ldr	x3, [x25,2040]
	uxth	w6, w24
	mov	w25, 65535
.L2239:
	cmp	w26, w28
	beq	.L2392
	ldr	w0, [x3]
	cbnz	w0, .L2240
	ldr	x4, [x3,16]
	ldr	w2, [x4,4]
	cmn	w2, #1
	beq	.L2241
	add	x5, x20, :lo12:.LANCHOR2
	mov	w0, w2
	str	x6, [x29,144]
	str	x4, [x29,152]
	ldr	w1, [x5,192]
	str	x3, [x29,160]
	str	x2, [x29,168]
	str	x5, [x29,176]
	bl	ftl_cmp_data_ver
	ldr	x5, [x29,176]
	ldr	x2, [x29,168]
	ldr	x3, [x29,160]
	ldr	x4, [x29,152]
	ldr	x6, [x29,144]
	cbz	w0, .L2241
	add	w2, w2, 1
	str	w2, [x5,192]
.L2241:
	ldr	w0, [x4]
	cmn	w0, #1
	bne	.L2242
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w21, w21
	umull	x26, w26, w1
	ldr	x0, [x0,2040]
	add	x26, x0, x26
	ldr	w0, [x26,4]
	b	.L2384
.L2240:
	mov	w25, w6
.L2242:
	add	w26, w26, 1
	add	x3, x3, 56
	uxth	w26, w26
	b	.L2239
.L2392:
	add	x0, x20, :lo12:.LANCHOR2
	add	w21, w21, 1
	uxth	w21, w21
	ldr	x0, [x0,2040]
	ldr	w0, [x0,4]
.L2384:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2246
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w2, [x0,120]
	cbnz	w2, .L2246
	add	x0, x0, 196
	ldrh	w21, [x0,w21,sxtw 1]
.L2246:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w0, [x0,2018]
	cmp	w0, w21
	bne	.L2247
	strh	w21, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2247:
	uxth	w0, w27
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w21, w0
	bne	.L2248
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2389
.L2248:
	mov	w0, 65535
	sub	w22, w22, #1
	cmp	w25, w0
	bne	.L2249
	cbnz	w1, .L2250
.L2249:
	add	x0, x20, :lo12:.LANCHOR2
	uxth	w5, w24
	ldr	w1, [x0,2324]
	cmn	w1, #1
	bne	.L2251
	str	w22, [x0,2324]
.L2251:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w25, [x29,188]
	ldr	w6, [x0,2324]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w24, uxth
	bge	.L2252
	sub	w25, w5, #7
	uxth	w25, w25
.L2252:
	mov	w3, -1
	add	x28, x20, :lo12:.LANCHOR2
	mov	w27, w3
	mov	w4, 65535
	mov	w7, 56
	add	x8, x28, 988
.L2253:
	cmp	w25, w5
	bhi	.L2266
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, 0
	ldrh	w2, [x0,1944]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2267:
	cmp	x0, x2
	beq	.L2393
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2254
	umull	x9, w24, w7
	ldr	x10, [x28,2040]
	add	w24, w24, 1
	orr	w1, w25, w1, lsl 10
	add	x9, x10, x9
	uxth	w24, w24
	str	w1, [x9,4]
.L2254:
	add	x0, x0, 2
	b	.L2267
.L2393:
	ldr	x0, [x28,2040]
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
	ldr	x0, [x28,2040]
	ldr	x4, [x29,144]
	ldr	x7, [x29,136]
	add	x0, x0, 16
	ldr	x8, [x29,128]
.L2256:
	cmp	w1, w24
	beq	.L2394
	ldr	w2, [x0,-16]
	cbnz	w2, .L2257
	ldr	x2, [x0]
	ldrh	w11, [x2]
	cmp	w11, w4
	beq	.L2258
	ldr	w2, [x2,4]
	cmn	w2, #1
	beq	.L2258
	ldr	w27, [x28,2324]
	cmn	w3, #1
	str	w2, [x28,2324]
	bne	.L2258
	ldrh	w2, [x8,x10,lsl 1]
	cmp	w2, w4
	bne	.L2259
	cbz	w9, .L2258
.L2259:
	cmp	w27, w22
	csinv	w3, w27, wzr, ne
	b	.L2258
.L2257:
	ldrb	w0, [x19,8]
	cbnz	w0, .L2250
	add	x0, x20, :lo12:.LANCHOR2
	add	x1, x0, 988
	ldrh	w2, [x1,w25,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2261
	cmn	w3, #1
	beq	.L2262
	str	w3, [x0,2324]
	b	.L2250
.L2262:
	cmp	w6, w22
	beq	.L2263
	str	w6, [x0,2324]
	b	.L2250
.L2263:
	ldr	w1, [x0,2324]
	b	.L2390
.L2261:
	cmp	w27, w22
	beq	.L2264
	cmn	w27, #1
	beq	.L2250
	str	w27, [x0,2324]
	b	.L2250
.L2264:
	ldr	w1, [x0,2324]
	cmp	w1, w22
	beq	.L2250
.L2390:
	sub	w1, w1, #1
	b	.L2385
.L2258:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2256
.L2394:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2253
.L2266:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, -1
.L2385:
	str	w1, [x0,2324]
.L2250:
	adrp	x25, .LANCHOR4
	mov	w1, 1
	add	x0, x25, :lo12:.LANCHOR4
	strh	w1, [x0,266]
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 2232
	bl	FtlMapBlkWriteDumpData
	ldr	w24, [x29,188]
	adrp	x0, .LC119
	str	x25, [x29,136]
	add	x0, x0, :lo12:.LC119
	str	x0, [x29,128]
.L2268:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x1, x19
	str	wzr, [x29,176]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,1944]
	ldrb	w6, [x0,120]
	add	x3, x19, x3, lsl 1
.L2269:
	cmp	x1, x3
	beq	.L2395
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2270
	ldr	w0, [x29,176]
	add	x5, x20, :lo12:.LANCHOR2
	orr	w2, w24, w2, lsl 10
	ldr	x4, [x5,2040]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2271
	cbz	w6, .L2271
	ldr	x2, [x5,2040]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2271:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2270:
	add	x1, x1, 2
	b	.L2269
.L2395:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,2040]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2273:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2396
	ldr	x0, [x29,160]
	mov	x1, 56
	mul	x25, x0, x1
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2040]
	str	x1, [x29,168]
	add	x28, x1, x25
	ldr	w27, [x28,4]
	str	w27, [x29,204]
	lsr	x0, x27, 10
	bl	P2V_plane
	uxth	w0, w0
	ldr	w1, [x29,188]
	cmp	w24, w1
	ldr	x1, [x29,168]
	bcc	.L2274
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2309
	ldr	w2, [x29,188]
	cmp	w24, w2
	beq	.L2274
.L2309:
	cmp	w0, w26
	bne	.L2310
	cmp	w24, w21
	beq	.L2276
.L2310:
	ldr	w0, [x1,x25]
	cmn	w0, #1
	beq	.L2278
	ldr	x28, [x28,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	bne	.L2285
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2280
	add	x27, x20, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,192]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2280
	add	w0, w22, 1
	str	w0, [x27,192]
.L2280:
	ldr	w27, [x28,8]
	add	x1, x29, 200
	ldr	w0, [x28,12]
	mov	w2, 0
	str	w0, [x29,196]
	mov	w0, w27
	bl	log2phys
	add	x3, x20, :lo12:.LANCHOR2
	str	x3, [x29,168]
	ldr	w1, [x3,2324]
	cmn	w1, #1
	beq	.L2281
	mov	w0, w22
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,168]
	cbz	w0, .L2281
	ldr	w1, [x29,196]
	cmn	w1, #1
	beq	.L2282
	ldr	x0, [x3,2040]
	str	x3, [x29,120]
	add	x0, x0, x25
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x2, [x0,16]
	ldr	x0, [x3,2040]
	str	x2, [x29,168]
	mov	w2, 0
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x3, [x29,120]
	ldr	x0, [x29,168]
	ldr	x2, [x3,2040]
	ldr	w0, [x0,4]
	str	w0, [x29,144]
	add	x4, x2, x25
	ldr	w0, [x2,x25]
	cmn	w0, #1
	bne	.L2283
	b	.L2284
.L2282:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2285
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
.L2285:
	ldrh	w0, [x19]
	b	.L2387
.L2283:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2284
	ldr	w0, [x3,2324]
	ldr	w1, [x29,144]
	str	x2, [x29,104]
	str	x4, [x29,112]
	str	x3, [x29,120]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,120]
	ldr	x4, [x29,112]
	ldr	x2, [x29,104]
	cbz	w0, .L2284
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2290
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2284
	cmn	w0, #1
	beq	.L2288
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,2040]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	b	.L2289
.L2288:
	str	w0, [x2,x25]
.L2289:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2040]
	ldr	w1, [x1,x25]
	cmn	w1, #1
	beq	.L2290
	ldr	x1, [x29,168]
	ldr	w0, [x0,2324]
	ldr	w25, [x1,4]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2290
	ldr	w0, [x29,144]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2284
.L2290:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2284:
	mov	w0, -1
	str	w0, [x29,196]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2292
.L2281:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2292
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w25, [x29,200]
	cmn	w25, #1
	beq	.L2292
	ldr	w0, [x29,196]
	cmp	w25, w0
	beq	.L2292
	lsr	x0, x25, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w2, [x1,-8]
	cmp	w2, w0
	beq	.L2295
	ldrh	w2, [x1,40]
	cmp	w2, w0
	beq	.L2295
	ldrh	w1, [x1,88]
	cmp	w1, w0
	bne	.L2292
.L2295:
	add	x28, x20, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, 0
	ldr	x0, [x28,2040]
	str	w25, [x0,4]
	ldr	x3, [x0,16]
	ldr	x0, [x28,2040]
	str	x3, [x29,168]
	bl	FlashReadPages
	ldr	x0, [x28,2040]
	ldr	x3, [x29,168]
	ldr	w0, [x0]
	ldr	w1, [x3,4]
	cmn	w0, #1
	beq	.L2292
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2292
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2292:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2274
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,-48]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2296
.L2387:
	bl	decrement_vpc_count
	b	.L2274
.L2296:
	ldr	x0, [x29,128]
	bl	printk
	b	.L2274
.L2278:
	ldr	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR4
	ldr	w0, [x1,268]
	cmp	w0, 31
	bhi	.L2297
	add	x2, x1, 272
	str	w27, [x2,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x1,268]
.L2297:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,2324]
	cmn	w1, #1
	beq	.L2386
	cmp	w1, w22
	bls	.L2274
.L2386:
	str	w22, [x0,2324]
.L2274:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2273
.L2396:
	ldrb	w0, [x19,8]
	add	w24, w24, 1
	cmp	w0, 1
	uxth	w24, w24
	bne	.L2300
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,120]
	cbz	w1, .L2300
	cmp	w21, w24
	bne	.L2300
	ldrh	w0, [x0,2020]
	cmp	w0, w24
	beq	.L2276
.L2300:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2018]
	cmp	w24, w1
	bne	.L2268
	ldrh	w2, [x0,1944]
	mov	w3, 65535
	strh	w24, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2302:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2382
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2302
	strb	w1, [x19,6]
	b	.L2382
.L2276:
	strb	w26, [x19,6]
	strh	w21, [x19,2]
.L2389:
	mov	x0, x19
	mov	w1, w21
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2382:
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
	stp	x21, x22, [sp,32]
	add	x22, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	ldrh	w2, [x22,-4]
	cbz	w2, .L2398
	ldrb	w0, [x22]
	cbnz	w0, .L2398
	adrp	x21, .LANCHOR0
	ldrb	w0, [x22,-1]
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w3, [x21,2018]
	mul	w0, w0, w3
	cmp	w2, w0
	beq	.L2398
	ldrb	w24, [x22,2]
	ldr	w23, [x21,2096]
	ldrh	w25, [x21,1944]
	cbnz	w24, .L2397
	sub	w23, w23, #1
	add	x1, x29, 84
	mov	w0, w23
	mov	w2, w24
	bl	log2phys
	ldr	x20, [x22,2104]
	ldr	w0, [x29,84]
	ldr	x1, [x22,-72]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w23, [x29,112]
	str	x1, [x29,96]
	str	x20, [x29,104]
	str	w24, [x20,4]
	beq	.L2400
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w24
	bl	FlashReadPages
	b	.L2401
.L2400:
	ldr	x0, [x22,-72]
	mov	w1, 255
	ldrh	w2, [x21,2028]
	bl	ftl_memset
.L2401:
	add	x24, x19, :lo12:.LANCHOR2
	mov	w0, -3947
	lsl	w25, w25, 2
	strh	w0, [x20]
	mov	w22, 0
	sub	x21, x24, #8
.L2402:
	cmp	w22, w25
	beq	.L2403
	ldrh	w0, [x21,4]
	cbz	w0, .L2403
	ldr	w0, [x29,92]
	add	w22, w22, 1
	str	w0, [x20,12]
	ldrh	w0, [x21]
	strh	w0, [x20,2]
	mov	x0, x21
	str	w23, [x20,8]
	bl	get_new_active_ppa
	str	w0, [x29,92]
	ldr	w1, [x24,192]
	mov	w2, 0
	str	w1, [x20,4]
	mov	w3, w2
	add	w1, w1, 1
	add	x0, x29, 88
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x24,192]
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x21]
	bl	decrement_vpc_count
	b	.L2402
.L2403:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,2]
	b	.L2397
.L2398:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,2]
.L2397:
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
.L2415:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2058]
	cmp	w0, w19
	bls	.L2418
	add	x1, x21, :lo12:.LANCHOR2
	ubfiz	x0, x19, 4, 16
	ldr	x1, [x1,136]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2416
	mov	w0, w19
	bl	flush_l2p_region
.L2416:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2415
.L2418:
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
	adrp	x0, .LC120
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 16
	add	x0, x0, :lo12:.LC120
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
	ldr	x0, [x0,2152]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2420:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,2096]
	cmp	w19, w0
	bcs	.L2435
	mov	w0, w19
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2421
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x2, [x1,2152]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2421:
	add	w19, w19, 1
	b	.L2420
.L2435:
	adrp	x23, .LC121
	mov	w19, 0
	add	x20, x20, :lo12:.LANCHOR2
	mov	w24, 65535
	add	x23, x23, :lo12:.LC121
.L2423:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,1952]
	cmp	w0, w19
	bls	.L2436
	ubfiz	x22, x19, 1, 16
	ldr	x0, [x20,-48]
	ldrh	w2, [x0,x22]
	ldr	x0, [x20,2152]
	ldrh	w3, [x0,x22]
	cmp	w2, w3
	beq	.L2426
	cmp	w2, w24
	beq	.L2426
	mov	x0, x23
	mov	w1, w19
	bl	printk
	ldrh	w0, [x20,-8]
	cmp	w0, w19
	beq	.L2426
	ldrh	w0, [x20,88]
	cmp	w0, w19
	beq	.L2426
	ldrh	w0, [x20,40]
	cmp	w0, w19
	beq	.L2426
	ldr	x0, [x20,2152]
	ldrh	w1, [x0,x22]
	ldr	x0, [x20,-48]
	strh	w1, [x0,x22]
	mov	w0, w19
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2426:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2423
.L2436:
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
	str	x21, [sp,32]
	mov	x20, x0
	adrp	x19, .LANCHOR2
	ldrh	w21, [x0]
	mov	w0, 65535
	cmp	w21, w0
	beq	.L2438
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x0, x21, 1, 16
	ldr	x1, [x1,-48]
	ldrh	w0, [x1,x0]
	cbz	w0, .L2439
	mov	w0, w21
	bl	INSERT_DATA_LIST
	b	.L2438
.L2439:
	mov	w0, w21
	bl	INSERT_FREE_LIST
.L2438:
	add	x1, x19, :lo12:.LANCHOR2
	strb	wzr, [x20,8]
	add	x0, x1, 40
	cmp	x20, x0
	beq	.L2440
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w2, [x0,1964]
	cmp	w2, 1
	beq	.L2440
	ldrb	w0, [x0,120]
	cbz	w0, .L2441
.L2440:
	mov	w0, 1
	strb	w0, [x20,8]
	b	.L2442
.L2441:
	sub	x0, x1, #8
	cmp	x20, x0
	bne	.L2442
	cmp	w2, 3
	beq	.L2444
	ldr	w0, [x1,336]
	cmp	w0, 1
	bne	.L2445
.L2444:
	mov	w0, 1
	strb	w0, [x19,#:lo12:.LANCHOR2]
.L2445:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,152]
	cbz	w1, .L2442
	ldr	w1, [x0,212]
	cmp	w1, 29
	bhi	.L2442
	mov	w1, 1
	strb	w1, [x0]
.L2442:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x2,2312]
	cmp	w0, w1
	beq	.L2447
	cmp	w21, w0
	bne	.L2448
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,-48]
	ldrh	w1, [x2,x1]
	cbz	w1, .L2449
.L2448:
	bl	update_vpc_list
.L2449:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,2312]
.L2447:
	mov	x0, x20
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	ldr	x21, [sp,32]
	mov	w0, 0
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
	ldr	w4, [x3,2020]
	cbnz	w4, .L2609
	ldrh	w4, [x3,-32]
	cmp	w4, 47
	bls	.L2609
	mov	w24, w0
	adrp	x0, .LANCHOR1+3008
	mov	w22, w1
	ldrh	w1, [x0,#:lo12:.LANCHOR1+3008]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2464
.L2467:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,946]
	cmp	w1, w2
	bne	.L2465
	b	.L2466
.L2464:
	ldrh	w1, [x3,88]
	cmp	w1, w0
	beq	.L2467
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2467
	b	.L2609
.L2465:
	ldrh	w3, [x0,944]
	cmp	w3, w2
	bne	.L2466
	strh	w1, [x0,944]
	mov	w1, -1
	strh	w1, [x0,946]
.L2466:
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w24, 1
	ldr	w0, [x1,860]
	add	w0, w0, 1
	add	w0, w0, w24, lsl 7
	str	w0, [x1,860]
	bne	.L2479
	ldr	w1, [x1,152]
	cbnz	w1, .L2469
	adrp	x1, .LANCHOR0+120
	ldrb	w1, [x1,#:lo12:.LANCHOR0+120]
	cbz	w1, .L2479
.L2469:
	add	x20, x19, :lo12:.LANCHOR2
	ldr	w1, [x20,212]
	cmp	w1, 29
	bhi	.L2479
	adrp	x21, .LANCHOR4
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w1, [x1,400]
	add	w0, w0, w1
	str	w0, [x20,860]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x20,288]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2479
	ldrh	w1, [x20,944]
	cmp	w1, w0
	bne	.L2479
	ldr	w0, [x20,860]
	cmp	w0, 1024
	bhi	.L2470
	ldrh	w0, [x20,-16]
	cmp	w0, 63
	bhi	.L2479
.L2470:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x21, :lo12:.LANCHOR4
	str	wzr, [x1,860]
	ldr	w1, [x1,212]
	strh	wzr, [x0,400]
	cbnz	w1, .L2471
	mov	w1, 6
	b	.L2632
.L2471:
	cmp	w1, 5
	bhi	.L2472
	mov	w1, 18
.L2632:
	strh	w1, [x0,400]
.L2472:
	adrp	x1, .LANCHOR0
	add	x0, x1, :lo12:.LANCHOR0
	mov	x20, x1
	ldrb	w0, [x0,120]
	cbz	w0, .L2473
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,-16]
	ldrh	w0, [x0,858]
	add	w0, w0, 64
	cmp	w1, w0
	bgt	.L2479
.L2473:
	mov	w0, 32
	mov	w27, 65535
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w27
	beq	.L2478
	add	x23, x19, :lo12:.LANCHOR2
	ldrh	w0, [x23,864]
	cbz	w0, .L2475
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x26, [x23,-48]
	ubfiz	x25, x25, 1, 16
	ldrh	w2, [x1,2020]
	ldrh	w1, [x1,1944]
	ldrh	w3, [x26,x25]
	mul	w1, w2, w1
	add	w1, w1, 1
	cmp	w3, w1
	bgt	.L2478
	add	w1, w0, 1
	str	wzr, [x23,868]
	uxth	w1, w1
	strh	w1, [x23,864]
	str	x1, [x29,120]
	bl	List_get_gc_head_node
	uxth	w20, w0
	cmp	w20, w27
	ldr	x1, [x29,120]
	beq	.L2478
	ubfiz	x27, x20, 1, 16
	adrp	x0, .LC122
	ldrh	w4, [x26,x25]
	add	x0, x0, :lo12:.LC122
	mov	w2, w20
	ldrh	w3, [x26,x27]
	bl	printk
	ldrh	w0, [x23,864]
	cmp	w0, 40
	bls	.L2476
	ldr	x0, [x23,-48]
	ldrh	w0, [x0,x27]
	cmp	w0, 32
	bls	.L2476
	strh	wzr, [x23,864]
.L2476:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, 6
	strh	w0, [x21,400]
	b	.L2480
.L2475:
	mov	w0, 1
	strh	w0, [x23,864]
.L2478:
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2480
	add	x21, x21, :lo12:.LANCHOR4
	strh	wzr, [x21,400]
.L2479:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w20, [x21,288]
	cmp	w20, w0
	bne	.L2636
	ldrh	w0, [x21,88]
	cmp	w0, w20
	beq	.L2637
.L2480:
	mov	w0, 65535
	cmp	w20, w0
	cset	w1, eq
	cbz	w1, .L2493
	cbnz	w24, .L2493
	add	x0, x19, :lo12:.LANCHOR2
	mov	w21, 1
	ldrh	w1, [x0,-16]
	cmp	w1, 24
	bhi	.L2494
	adrp	x0, .LANCHOR0
	cmp	w1, 16
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w21, [x0,2018]
	bls	.L2495
	lsr	w21, w21, 5
	b	.L2494
.L2495:
	cmp	w1, 12
	bls	.L2496
	lsr	w21, w21, 4
	b	.L2494
.L2496:
	cmp	w1, 8
	bls	.L2494
	lsr	w21, w21, 2
.L2494:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,856]
	cmp	w2, w1
	bcs	.L2498
	ldrh	w1, [x0,88]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L2499
	ldrh	w2, [x0,944]
	cmp	w2, w1
	bne	.L2499
	adrp	x1, .LANCHOR4+400
	ldrh	w2, [x1,#:lo12:.LANCHOR4+400]
	cbnz	w2, .L2500
	adrp	x1, .LANCHOR0+2096
	ldr	w3, [x0,148]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2096]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2501
.L2500:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,264]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,856]
	b	.L2502
.L2501:
	mov	w1, 18
	strh	w1, [x0,856]
.L2502:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,868]
	b	.L2609
.L2499:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,264]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,856]
.L2498:
	cmp	w22, 2
	bhi	.L2562
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,152]
	cbz	w0, .L2562
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2562
.L2493:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x21,88]
	cmp	w0, w2
	bne	.L2505
	cbz	w1, .L2505
	ldrh	w1, [x21,944]
	cmp	w1, w0
	bne	.L2505
	ldrh	w1, [x21,288]
	cmp	w1, w0
	beq	.L2506
.L2511:
	mov	w20, 65535
	b	.L2505
.L2506:
	ldrh	w23, [x21,-16]
	adrp	x20, .LANCHOR4
	ldrh	w0, [x21,856]
	str	wzr, [x21,868]
	cmp	w0, w23
	bcs	.L2507
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0,400]
	cbnz	w0, .L2508
	adrp	x0, .LANCHOR0+2096
	ldr	w1, [x21,148]
	ldr	w0, [x0,#:lo12:.LANCHOR0+2096]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bcs	.L2509
.L2508:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,264]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x19,856]
	b	.L2510
.L2509:
	mov	w0, 18
	strh	w0, [x21,856]
.L2510:
	add	x20, x20, :lo12:.LANCHOR4
	bl	FtlReadRefresh
	ldrh	w2, [x20,400]
	b	.L2609
.L2507:
	add	x22, x20, :lo12:.LANCHOR4
	ldrh	w0, [x22,400]
	cbnz	w0, .L2511
	ldrh	w20, [x21,264]
	add	w1, w20, w20, lsl 1
	asr	w1, w1, 2
	strh	w1, [x21,856]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x21,-48]
	ldrh	w2, [x1,x0]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1,2020]
	ldrh	w1, [x1,1944]
	mul	w0, w0, w1
	mov	w1, 2
	sdiv	w0, w0, w1
	cmp	w2, w0
	ble	.L2512
	sub	w20, w20, #1
	cmp	w23, w20
	blt	.L2512
	bl	FtlReadRefresh
	ldrh	w2, [x22,400]
	b	.L2609
.L2512:
	cbnz	w2, .L2511
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,-16]
	add	w2, w2, 1
	b	.L2609
.L2505:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w21, 2
	ldr	w0, [x0,152]
	cmp	w0, wzr
	csinc	w21, w21, wzr, ne
	b	.L2504
.L2562:
	mov	w20, 65535
.L2504:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,288]
	cmp	w1, w2
	bne	.L2514
	cmp	w20, w1
	beq	.L2515
	strh	w20, [x0,288]
	b	.L2516
.L2515:
	ldrh	w1, [x0,944]
	cmp	w1, w20
	beq	.L2516
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x0,-48]
	ldrh	w1, [x2,x1]
	cbnz	w1, .L2517
	mov	w1, -1
	strh	w1, [x0,944]
.L2517:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,944]
	strh	w1, [x0,288]
	mov	w1, -1
	strh	w1, [x0,944]
.L2516:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x22,288]
	strb	wzr, [x22,296]
	cmp	w0, w1
	beq	.L2514
	bl	IsBlkInGcList
	cbz	w0, .L2519
	mov	w0, -1
	strh	w0, [x22,288]
.L2519:
	adrp	x0, .LANCHOR0+120
	ldrb	w0, [x0,#:lo12:.LANCHOR0+120]
	cbz	w0, .L2520
	add	x0, x19, :lo12:.LANCHOR2
	add	x22, x0, 288
	ldrh	w0, [x0,288]
	bl	ftl_get_blk_mode
	strb	w0, [x22,8]
.L2520:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x23, x22, 288
	ldrh	w1, [x22,288]
	cmp	w1, w0
	beq	.L2514
	mov	x0, x23
	bl	make_superblock
	strh	wzr, [x22,290]
	adrp	x0, .LANCHOR4
	ldrh	w2, [x22,288]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x1, [x22,-48]
	strb	wzr, [x22,294]
	strh	wzr, [x0,402]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,404]
.L2514:
	add	x3, x19, :lo12:.LANCHOR2
	ldrh	w0, [x3,288]
	ldrh	w1, [x3,-8]
	cmp	w1, w0
	beq	.L2521
	ldrh	w1, [x3,40]
	cmp	w1, w0
	beq	.L2521
.L2522:
	mov	x28, x3
	mov	w24, 65535
	b	.L2523
.L2521:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,288]
	b	.L2635
.L2543:
	ldrh	w0, [x28,290]
	add	w21, w21, w0
	uxth	w21, w21
	strh	w21, [x28,290]
	cmp	w21, w25
	adrp	x21, .LANCHOR4
	bcs	.L2545
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w1, [x0,402]
	ldrh	w0, [x0,404]
	cmp	w1, w0
	beq	.L2545
.L2546:
	ldrh	w0, [x28,-16]
	cmp	w0, 2
	bhi	.L2549
	add	x23, x23, :lo12:.LANCHOR0
	ldrh	w21, [x23,2018]
.L2523:
	ldrh	w0, [x28,288]
	cmp	w0, w24
	bne	.L2524
	str	wzr, [x28,868]
.L2525:
	ldrh	w22, [x28,864]
	mov	w0, w22
	bl	List_get_gc_head_node
	uxth	w23, w0
	cmp	w23, w24
	strh	w23, [x28,288]
	bne	.L2526
	strh	wzr, [x28,864]
	mov	w2, 8
	b	.L2609
.L2526:
	mov	w0, w23
	add	w22, w22, 1
	bl	IsBlkInGcList
	cbz	w0, .L2527
	strh	w22, [x28,864]
	b	.L2525
.L2527:
	adrp	x0, .LANCHOR0
	ubfiz	x1, x23, 1, 16
	add	x5, x0, :lo12:.LANCHOR0
	ldr	x2, [x28,-48]
	uxth	w22, w22
	mov	w3, 2
	strh	w22, [x28,864]
	ldrh	w0, [x5,2018]
	ldrh	w5, [x5,1944]
	ldrh	w4, [x2,x1]
	mul	w0, w0, w5
	sdiv	w5, w0, w3
	cmp	w4, w5
	bgt	.L2529
	cmp	w4, 8
	bls	.L2530
	cmp	w22, 48
	bls	.L2530
	ldrh	w4, [x28,916]
	cmp	w4, 35
	bhi	.L2530
.L2529:
	strh	wzr, [x28,864]
.L2530:
	ldrh	w1, [x2,x1]
	cmp	w1, w0
	blt	.L2531
	cmp	w20, w24
	bne	.L2531
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x19,864]
	strh	w0, [x19,288]
.L2635:
	adrp	x0, .LANCHOR4+400
	ldrh	w2, [x0,#:lo12:.LANCHOR4+400]
	b	.L2609
.L2531:
	cbnz	w1, .L2532
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x28,864]
	add	w0, w0, 1
	strh	w0, [x28,864]
	b	.L2525
.L2532:
	adrp	x0, .LANCHOR0
	strb	wzr, [x28,296]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L2533
	mov	w0, w23
	bl	ftl_get_blk_mode
	strb	w0, [x28,296]
.L2533:
	add	x22, x28, 288
	mov	x0, x22
	bl	make_superblock
	adrp	x1, .LANCHOR4
	ldrh	w2, [x28,288]
	add	x1, x1, :lo12:.LANCHOR4
	ldr	x0, [x28,-48]
	strh	wzr, [x1,402]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,404]
	strh	wzr, [x28,290]
	strb	wzr, [x28,294]
.L2524:
	bl	FtlReadRefresh
	adrp	x23, .LANCHOR0
	mov	w0, 1
	str	w0, [x28,2020]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,120]
	ldrh	w25, [x0,2018]
	cbz	w1, .L2534
	ldrb	w1, [x28,296]
	cmp	w1, 1
	bne	.L2534
	ldrh	w25, [x0,2020]
.L2534:
	ldrh	w0, [x28,290]
	add	w1, w0, w21
	cmp	w1, w25
	ble	.L2535
	sub	w21, w25, w0
	uxth	w21, w21
.L2535:
	mov	w26, 0
.L2536:
	cmp	w21, w26, uxth
	bls	.L2543
	add	x1, x28, 288
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w4, [x1,2]
	ldrh	w6, [x0,1944]
	mov	x0, 0
	add	w4, w4, w26
	mov	w22, w0
.L2544:
	cmp	w6, w0, uxth
	bls	.L2638
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,16]
	cmp	w2, w24
	beq	.L2537
	mov	w3, 56
	ldr	x7, [x28,904]
	orr	w2, w4, w2, lsl 10
	umull	x5, w22, w3
	add	w22, w22, 1
	add	x5, x7, x5
	uxth	w22, w22
	str	w2, [x5,4]
.L2537:
	add	x0, x0, 1
	b	.L2544
.L2638:
	ldr	x0, [x28,904]
	mov	w1, w22
	ldrb	w2, [x28,296]
	mov	x27, 0
	bl	FlashReadPages
.L2539:
	cmp	w22, w27, uxth
	bls	.L2639
	mov	x0, 56
	mul	x5, x27, x0
	ldr	x0, [x28,904]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	cmn	w0, #1
	ldr	x4, [x1,16]
	beq	.L2564
	ldrh	w0, [x4]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2564
	ldr	w0, [x4,8]
	add	x1, x29, 140
	mov	w2, 0
	str	x5, [x29,112]
	str	x4, [x29,120]
	bl	log2phys
	ldr	x2, [x28,904]
	ldr	x5, [x29,112]
	ldr	w0, [x29,140]
	add	x2, x2, x5
	ldr	x4, [x29,120]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L2564
	adrp	x0, .LANCHOR4
	mov	x3, 56
	add	x1, x0, :lo12:.LANCHOR4
	str	x5, [x29,104]
	str	x4, [x29,112]
	ldrh	w0, [x1,402]
	add	w0, w0, 1
	strh	w0, [x1,402]
	ldr	w0, [x28,872]
	ldr	x1, [x28,2048]
	madd	x1, x0, x3, x1
	ldr	w0, [x2,24]
	str	x1, [x29,120]
	str	w0, [x1,24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29,120]
	mov	x2, 56
	ldr	x5, [x29,104]
	ldr	x4, [x29,112]
	str	w0, [x1,4]
	ldr	w0, [x28,872]
	ldr	x1, [x28,2048]
	madd	x0, x0, x2, x1
	ldr	x1, [x28,904]
	add	x1, x1, x5
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	add	x2, x28, 88
	str	x2, [x29,120]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,140]
	str	w0, [x4,12]
	ldrh	w0, [x28,88]
	strh	w0, [x4,2]
	ldr	w0, [x28,192]
	str	w0, [x4,4]
	ldr	w0, [x28,872]
	add	w0, w0, 1
	str	w0, [x28,872]
	ldr	x0, [x28,904]
	add	x0, x0, x5
	bl	FtlGcBufAlloc
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2541
	ldr	x2, [x29,120]
	ldr	w1, [x28,872]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2541
	ldrh	w0, [x2,4]
	cbnz	w0, .L2564
.L2541:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2564
	adrp	x0, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x19,2020]
	ldrh	w2, [x0,400]
	b	.L2609
.L2564:
	add	x27, x27, 1
	b	.L2539
.L2639:
	add	w26, w26, 1
	b	.L2536
.L2545:
	ldr	w0, [x28,872]
	cbz	w0, .L2547
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2547
	add	x21, x21, :lo12:.LANCHOR4
	str	wzr, [x28,2020]
	ldrh	w2, [x21,400]
	b	.L2609
.L2547:
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w2, [x0,402]
	cbnz	w2, .L2548
	ldrh	w0, [x28,288]
	ldr	x1, [x28,-48]
	lsl	x0, x0, 1
	ldrh	w4, [x1,x0]
	cbz	w4, .L2548
	strh	w2, [x1,x0]
	ldrh	w0, [x28,288]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2548:
	mov	w0, -1
	strh	w0, [x28,288]
	b	.L2546
.L2549:
	add	x21, x21, :lo12:.LANCHOR4
	str	wzr, [x28,2020]
	ldrh	w2, [x21,400]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L2609
.L2637:
	ldrh	w25, [x21,944]
	cmp	w25, w20
	bne	.L2480
	ldrh	w0, [x21,-16]
	mov	w2, 1024
	ldr	w1, [x21,860]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2480
	adrp	x0, .LANCHOR4+400
	str	wzr, [x21,860]
	mov	x23, x21
	strh	wzr, [x0,#:lo12:.LANCHOR4+400]
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	cmp	w20, w25
	bne	.L2482
	ldrh	w1, [x21,-16]
	ldrh	w0, [x21,858]
	cmp	w1, w0
	bcs	.L2483
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w20
	beq	.L2492
	ldr	w2, [x21,2016]
	uxtw	x1, w0
	adrp	x21, .LANCHOR0
	cbnz	w2, .L2485
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w2, [x0,1964]
	cmp	w2, 3
	beq	.L2485
	ldr	w2, [x23,336]
	cbnz	w2, .L2485
	ldr	w2, [x23,152]
	cbnz	w2, .L2485
	ldrb	w0, [x0,120]
	cbz	w0, .L2486
.L2485:
	add	x0, x19, :lo12:.LANCHOR2
	add	x3, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,-48]
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
	bgt	.L2488
	mov	w0, 0
	add	x21, x21, :lo12:.LANCHOR0
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x21,2096]
	ldr	w2, [x1,148]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2489
	mov	w0, 128
	b	.L2633
.L2489:
	mov	w0, 160
.L2633:
	strh	w0, [x1,858]
	b	.L2490
.L2488:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L2634
.L2486:
	ldr	x2, [x23,-48]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2491
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0,858]
	b	.L2490
.L2491:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L2634:
	strh	w1, [x0,858]
	b	.L2492
.L2483:
	mov	w0, 80
	strh	w0, [x21,858]
	b	.L2492
.L2490:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2492
.L2482:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x20, 1, 32
	adrp	x0, .LC123
	mov	w1, w20
	add	x0, x0, :lo12:.LC123
	ldr	x3, [x5,-48]
	ldr	x6, [x5,-88]
	ldrh	w2, [x5,-16]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,856]
	ldrh	w4, [x6,x4]
	bl	printk
.L2492:
	bl	FtlGcReFreshBadBlk
	b	.L2480
.L2636:
	mov	w20, w0
	b	.L2480
.L2609:
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
	stp	x29, x30, [sp, -112]!
	adrp	x0, .LANCHOR4+408
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	ldr	x20, [x0,#:lo12:.LANCHOR4+408]
	add	x0, x19, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w1, [x0,2080]
	cbz	w1, .L2641
	ldrb	w0, [x0,120]
	mov	w22, 0
	cbz	w0, .L2642
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w22, eq
.L2642:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x20,9]
	adrp	x24, .LC124
	mov	w2, w22
	mov	w23, 0
	mov	w25, 56
	ldr	x0, [x0,2088]
	adrp	x26, .LANCHOR2
	add	x24, x24, :lo12:.LC124
	bl	FlashProgPages
.L2643:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cmp	w23, w1
	bcs	.L2660
	umull	x21, w23, w25
	ldr	x1, [x0,2088]
	add	x0, x1, x21
	ldr	w1, [x1,x21]
	cmn	w1, #1
	beq	.L2663
	ldr	w1, [x0,4]
	cbz	w22, .L2677
	orr	w1, w1, -2147483648
.L2677:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,108]
	add	x1, x29, 108
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2088]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2647
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x26, :lo12:.LANCHOR2
	ubfiz	x0, x1, 1, 16
	mov	w21, w1
	ldr	x2, [x2,-48]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2648
	mov	x0, x24
	bl	printk
.L2648:
	mov	w0, w21
	bl	decrement_vpc_count
.L2647:
	add	w23, w23, 1
	b	.L2643
.L2679:
	mov	w20, 16386
	adrp	x21, .LANCHOR2
.L2659:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w0, [x0,948]
	cbz	w0, .L2660
	mov	w0, 1
	mov	w1, w0
	bl	rk_ftl_garbage_collect
	subs	w20, w20, #1
	bne	.L2659
.L2660:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19,2080]
	b	.L2641
.L2663:
	adrp	x23, .LANCHOR2
	adrp	x25, .LC124
	mov	w24, 0
	mov	w26, 56
	add	x23, x23, :lo12:.LANCHOR2
	add	x25, x25, :lo12:.LC124
.L2644:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cmp	w24, w1
	bcs	.L2679
	umull	x21, w24, w26
	ldr	x0, [x0,2088]
	mov	w1, -1
	str	w1, [x0,x21]
.L2650:
	add	x27, x19, :lo12:.LANCHOR0
	ldr	x0, [x27,2088]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	ldr	w0, [x1,4]
	bne	.L2680
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	bne	.L2651
	ldr	x2, [x23,-48]
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x20,4]
	ldrh	w0, [x2,x1]
	sub	w0, w0, w3
	strh	w0, [x2,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x27,2018]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L2651:
	ldrh	w0, [x20,4]
	cbnz	w0, .L2652
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2652:
	ldr	w0, [x23,440]
	add	x27, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x23,440]
	ldr	x0, [x27,2088]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,108]
	ldr	x1, [x27,2088]
	mov	w2, w22
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x27,2088]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	b	.L2650
.L2680:
	cbz	w22, .L2678
	orr	w0, w0, -2147483648
.L2678:
	str	w0, [x29,108]
	mov	w2, 1
	ldr	w0, [x1,24]
	add	x1, x29, 108
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2088]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2656
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x23,-48]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2657
	mov	x0, x25
	bl	printk
.L2657:
	mov	w0, w21
	bl	decrement_vpc_count
.L2656:
	add	w24, w24, 1
	b	.L2644
.L2641:
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
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
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
	bne	.L2683
	bl	FtlSysFlush
.L2683:
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
	bhi	.L2686
	cmp	w19, 31
	bls	.L2700
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,2024]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L2688
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L2688:
	mov	w0, -1
	adrp	x20, .LANCHOR4
	str	w0, [x29,76]
	adrp	x23, .LANCHOR2
.L2689:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w19, w0
	bcc	.L2701
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L2690
	add	x1, x20, :lo12:.LANCHOR4
	mov	w2, 1
	ldr	w0, [x1,416]
	add	w0, w0, 1
	str	w0, [x1,416]
	add	x1, x23, :lo12:.LANCHOR2
	ldr	w0, [x1,160]
	add	w0, w0, 1
	str	w0, [x1,160]
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,72]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2690:
	add	x0, x22, :lo12:.LANCHOR0
	add	w21, w21, 1
	ldrh	w0, [x0,2024]
	sub	w19, w19, w0
	b	.L2689
.L2701:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,416]
	cmp	w1, 32
	bls	.L2700
	str	wzr, [x0,416]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2700:
	mov	w0, 0
.L2686:
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
	mov	w3, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x2, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	adrp	x21, .LANCHOR0
	ldrh	w20, [x2,88]
	add	x1, x21, :lo12:.LANCHOR0
	cmp	w20, w3
	ldrh	w1, [x1,2018]
	bne	.L2703
.L2713:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x22, x20, 88
	ldrh	w1, [x20,88]
	str	wzr, [x20,2320]
	cmp	w1, w0
	beq	.L2732
	bl	FtlCacheWriteBack
	mov	w26, 12
	add	x3, x21, :lo12:.LANCHOR0
	ldrb	w0, [x22,7]
	ldr	x1, [x20,-48]
	mov	w22, 0
	ldrh	w2, [x20,88]
	ldrh	w3, [x3,2018]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldr	w0, [x20,176]
	ldrh	w1, [x20,928]
	add	w0, w1, w0
	str	w0, [x20,176]
	b	.L2714
.L2703:
	cbz	w0, .L2706
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w4, [x0,3008]
	cmp	w4, w3
	beq	.L2707
.L2708:
	mov	w1, 2
	b	.L2706
.L2707:
	strh	wzr, [x0,3008]
	ldrh	w0, [x2,-16]
	cmp	w0, 17
	bhi	.L2708
.L2706:
	add	x22, x19, :lo12:.LANCHOR2
	add	x0, x22, 88
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L2709
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x22,-88]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L2710
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2710:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2320]
	cbnz	w1, .L2711
	ldr	w1, [x0,440]
	add	w1, w1, 1
	str	w1, [x0,440]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2711:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	str	wzr, [x19,2320]
	b	.L2712
.L2709:
	adrp	x0, .LANCHOR1+3008
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3008]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2712
	b	.L2713
.L2717:
	ldr	x25, [x20,936]
	add	x1, x29, 92
	umull	x24, w22, w26
	mov	w2, 0
	add	x23, x25, x24
	ldr	w0, [x23,8]
	bl	log2phys
	ldr	w0, [x29,92]
	ldr	w1, [x25,x24]
	cmp	w0, w1
	bne	.L2715
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
.L2731:
	bl	decrement_vpc_count
.L2716:
	add	w22, w22, 1
	uxth	w22, w22
.L2714:
	ldrh	w0, [x20,928]
	cmp	w0, w22
	bhi	.L2717
	b	.L2733
.L2715:
	ldr	w1, [x23,4]
	cmp	w0, w1
	beq	.L2716
	ldrh	w0, [x20,88]
	b	.L2731
.L2733:
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x20,88]
	ldr	x2, [x20,-48]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2,x1]
	cbz	w1, .L2718
	bl	INSERT_DATA_LIST
	b	.L2719
.L2718:
	bl	INSERT_FREE_LIST
.L2719:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x20,928]
	strh	w0, [x20,88]
	strh	wzr, [x20,916]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	w0, [x20,152]
	cbz	w0, .L2720
	ldr	w0, [x20,212]
	cmp	w0, 29
	bhi	.L2720
	ldrh	w0, [x20,264]
	ldrh	w1, [x20,-16]
	cmp	w1, w0
	bcs	.L2721
	lsl	w0, w0, 1
	strh	w0, [x20,856]
.L2721:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,288]
	b	.L2732
.L2720:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,264]
	ldrh	w0, [x19,-16]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L2732
	add	x21, x21, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,288]
	ldrb	w0, [x21,120]
	cbz	w0, .L2722
	sub	w1, w1, #2
	strh	w1, [x19,856]
.L2732:
	mov	w0, 0
	b	.L2712
.L2722:
	mov	w1, 20
	strh	w1, [x19,856]
.L2712:
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
	cbz	w0, .L2735
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,872]
	tbz	x1, 0, .L2735
	ldrh	w0, [x0,92]
	cbz	w0, .L2735
.L2740:
	mov	w0, 0
	b	.L2736
.L2735:
	add	x1, x21, :lo12:.LANCHOR2
	mov	w2, 0
	mov	w3, w2
	mov	w20, 0
	mov	w22, 56
	ldr	x0, [x1,2048]
	ldr	w1, [x1,872]
	bl	FlashProgPages
.L2737:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w1, [x19,872]
	cmp	w20, w1
	bcs	.L2751
	umull	x0, w20, w22
	ldr	x2, [x19,2048]
	add	x1, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L2738
	ldrh	w3, [x19,88]
	ldr	x1, [x19,-48]
	strh	wzr, [x1,x3,lsl 1]
	strh	w2, [x19,88]
	ldr	w1, [x19,440]
	add	w1, w1, 1
	str	w1, [x19,440]
	ldr	x1, [x19,2048]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2750
.L2738:
	ldr	x2, [x1,16]
	add	w20, w20, 1
	ldr	w1, [x1,4]
	uxth	w20, w20
	ldr	w0, [x2,12]
	ldr	w2, [x2,8]
	bl	FtlGcUpdatePage
	b	.L2737
.L2751:
	ldr	x0, [x19,2048]
	bl	FtlGcBufFree
	str	wzr, [x19,872]
	ldrh	w0, [x19,92]
	cbnz	w0, .L2740
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L2750:
	mov	w0, 1
.L2736:
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
	add	x21, x19, 88
	mov	x0, x21
	ldrh	w1, [x20,2018]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,90]
	ldrh	w0, [x20,2018]
	cmp	w1, w0
	bcc	.L2752
	add	x0, x19, 2232
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19,2320]
.L2752:
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
	adrp	x0, .LANCHOR4+268
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR4+268]
	add	x19, x19, :lo12:.LANCHOR2
	sub	x20, x19, #8
	add	x19, x19, 40
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
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR4
	adrp	x24, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR4
	add	x22, x24, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	strh	wzr, [x0,266]
	add	x19, x20, :lo12:.LANCHOR2
	ldrh	w0, [x22,1948]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x19,272]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2756
.L2758:
	mov	w23, -1
	b	.L2757
.L2756:
	bl	FtlLoadSysInfo
	mov	w23, w0
	cbnz	w0, .L2758
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
	ldr	x0, [x19,136]
.L2759:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2763
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2759
.L2763:
	add	x0, x20, :lo12:.LANCHOR2
	cmp	w3, w2
	ldrh	w1, [x0,244]
	add	w1, w1, 1
	strh	w1, [x0,244]
	blt	.L2760
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w0, [x0,266]
	cbz	w0, .L2764
.L2760:
	add	x19, x20, :lo12:.LANCHOR2
	sub	x0, x19, #8
	add	x22, x19, 40
	bl	FtlSuperblockPowerLostFix
	mov	x0, x22
	bl	FtlSuperblockPowerLostFix
	ldrh	w0, [x19,-8]
	ldr	x2, [x19,-48]
	lsl	x0, x0, 1
	ldrh	w3, [x19,-4]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	add	x0, x24, :lo12:.LANCHOR0
	strb	wzr, [x19,-2]
	ldr	x3, [x19,-48]
	strh	wzr, [x19,-4]
	ldrh	w1, [x0,2018]
	strh	w1, [x19,-6]
	ldrh	w1, [x19,40]
	ldrh	w4, [x19,44]
	lsl	x1, x1, 1
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strb	wzr, [x19,46]
	ldrh	w0, [x0,2018]
	strh	w0, [x19,42]
	add	x0, x19, 2232
	strh	wzr, [x19,44]
	bl	FtlMapBlkWriteDumpData
	add	x0, x21, :lo12:.LANCHOR4
	add	x0, x0, 200
	bl	FtlMapBlkWriteDumpData
	ldrh	w0, [x19,246]
	add	w0, w0, 1
	strh	w0, [x19,246]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2764:
	add	x19, x20, :lo12:.LANCHOR2
	mov	w0, 65535
	sub	x24, x19, #8
	ldrh	w1, [x19,-8]
	cmp	w1, w0
	beq	.L2765
	ldrh	w0, [x19,-4]
	cbnz	w0, .L2765
	ldrh	w0, [x19,44]
	add	x22, x19, 40
	cbnz	w0, .L2765
	mov	w0, w1
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,40]
	bl	FtlGcRefreshBlock
	mov	x0, x24
	bl	allocate_new_data_superblock
	mov	x0, x22
	bl	allocate_new_data_superblock
	add	x0, x21, :lo12:.LANCHOR4
	add	x0, x0, 200
	bl	FtlMapBlkWriteDumpData
.L2765:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x20,244]
	and	w0, w0, 31
	cbnz	w0, .L2757
	bl	FtlVpcCheckAndModify
.L2757:
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
	add	x1, x0, 88
	ldrh	w2, [x0,88]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2772
	ldrh	w0, [x1,4]
	cbnz	w0, .L2773
.L2772:
	bl	FtlCacheWriteBack
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	strb	wzr, [x20,96]
	add	x0, x20, 88
	bl	allocate_data_superblock
	strh	wzr, [x20,916]
	strh	wzr, [x20,928]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2773:
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 88
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
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LC73
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR1
	mov	x20, x0
	add	x19, x19, :lo12:.LANCHOR1
	mov	w0, -1
	add	x1, x1, :lo12:.LC73
	str	w0, [x19,504]
	adrp	x0, .LANCHOR4+420
	str	wzr, [x0,#:lo12:.LANCHOR4+420]
	adrp	x0, .LC72
	add	x0, x0, :lo12:.LC72
	bl	printk
	mov	x0, x20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	adrp	x0, .LANCHOR0+1948
	ldrh	w0, [x0,#:lo12:.LANCHOR0+1948]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	w0, .L2779
	bl	FtlSysBlkInit
	cbnz	w0, .L2779
	mov	w0, 1
	str	w0, [x19,504]
	adrp	x0, .LANCHOR2-16
	ldrh	w0, [x0,#:lo12:.LANCHOR2-16]
	cmp	w0, 15
	bhi	.L2779
	mov	w19, 8129
.L2778:
	mov	w0, 0
	mov	w1, 1
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L2778
.L2779:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
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
	add	x1, x19, 432
	str	x0, [x19,424]
	add	x0, x19, 24
	str	xzr, [x19,24]
	str	xzr, [x19,432]
	bl	rknand_get_reg_addr
	ldr	x1, [x19,24]
	mov	w0, -1
	cbz	x1, .L2782
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x19,424]
	mov	w2, w1
	mov	w3, 2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	x0, [x19,24]
	bl	FlashInit
	mov	w19, w0
	cbnz	w0, .L2783
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 168
	bl	FtlInit
.L2783:
	adrp	x0, .LC125
	mov	w1, w19
	add	x0, x0, :lo12:.LC125
	bl	printk
	mov	w0, w19
.L2782:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rk_ftl_init, .-rk_ftl_init
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
	bne	.L2787
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L2788
.L2787:
	adrp	x21, .LANCHOR0
	add	w0, w1, w2
	add	x1, x21, :lo12:.LANCHOR0
	str	w0, [x29,152]
	mov	w0, -1
	ldr	w3, [x29,152]
	ldr	w2, [x1,2060]
	cmp	w3, w2
	bhi	.L2788
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
	ldr	w1, [x0,184]
	add	w1, w28, w1
	str	w1, [x0,184]
	ldr	w1, [x0,156]
	add	w1, w24, w1
	str	w1, [x0,156]
	ldr	w0, [x29,156]
	mov	w1, w25
	bl	FtlCacheMetchLpa
	cbz	w0, .L2789
	bl	FtlCacheWriteBack
.L2789:
	mov	w27, 0
	ldr	w22, [x29,156]
	str	w27, [x29,140]
	mov	w23, w27
	str	w27, [x29,136]
.L2790:
	cbz	w24, .L2832
	mov	w0, w22
	add	x1, x29, 172
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,172]
	cmn	w3, #1
	bne	.L2829
	mov	w3, 0
.L2791:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w3, w0
	bcs	.L2795
	madd	w0, w22, w0, w3
	cmp	w0, w20
	bcc	.L2793
	ldr	w1, [x29,152]
	cmp	w0, w1
	bcs	.L2793
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x26, x0
	str	x3, [x29,144]
	bl	ftl_memset
	ldr	x3, [x29,144]
.L2793:
	add	w3, w3, 1
	b	.L2791
.L2829:
	mov	w0, 56
	umull	x1, w23, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x0,2040]
	add	x2, x2, x1
	str	w3, [x2,4]
	ldr	w2, [x29,156]
	cmp	w22, w2
	ldr	x2, [x0,2040]
	add	x2, x2, x1
	bne	.L2796
	ldr	x0, [x0,2088]
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
	bne	.L2797
	str	x26, [x2,8]
	b	.L2797
.L2796:
	cmp	w22, w25
	bne	.L2798
	ldr	x0, [x0,2096]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w4, [x29,152]
	ldrh	w3, [x0,2024]
	mul	w0, w22, w3
	sub	w27, w4, w0
	cmp	w27, w3
	bne	.L2797
	b	.L2830
.L2798:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	mul	w0, w22, w0
.L2830:
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x26, x0
	str	x0, [x2,8]
.L2797:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2040]
	ldr	x2, [x2,2112]
	add	x1, x0, x1
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2030]
	str	w22, [x1,24]
	mul	w0, w23, w0
	add	w23, w23, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L2795:
	subs	w24, w24, #1
	add	w22, w22, 1
	beq	.L2799
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,1944]
	cmp	w23, w0, lsl 3
	bne	.L2790
.L2799:
	cbz	w23, .L2790
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, 0
	ldr	x0, [x0,2040]
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
.L2801:
	ldr	w0, [x29,144]
	cmp	w23, w0
	bls	.L2833
	ldr	x0, [x29,144]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	ldr	w4, [x29,156]
	mul	x3, x0, x1
	ldr	x0, [x2,2040]
	add	x0, x0, x3
	ldr	w1, [x0,24]
	cmp	w1, w4
	bne	.L2802
	ldr	x1, [x0,8]
	ldr	x0, [x2,2088]
	cmp	x1, x0
	bne	.L2803
	ldr	x2, [x29,128]
	mov	x0, x26
	str	x3, [x29,96]
	add	x1, x1, x2
	ldr	w2, [x29,108]
	b	.L2831
.L2802:
	cmp	w1, w25
	bne	.L2803
	ldr	x1, [x0,8]
	ldr	x0, [x2,2096]
	cmp	x1, x0
	bne	.L2803
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,104]
	str	x3, [x29,96]
	ldrh	w0, [x0,2024]
	mul	w0, w25, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x26, x0
.L2831:
	bl	ftl_memcpy
	ldr	x3, [x29,96]
.L2803:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,2040]
	add	x5, x1, x3
	ldr	w4, [x1,x3]
	cmn	w4, #1
	bne	.L2804
	ldr	w2, [x0,416]
	str	w4, [x29,136]
	add	w2, w2, 1
	str	w2, [x0,416]
.L2804:
	ldr	w0, [x1,x3]
	cmp	w0, 256
	bne	.L2805
	ldr	w0, [x5,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2805:
	ldr	x0, [x29,144]
	add	x0, x0, 1
	str	x0, [x29,144]
	b	.L2801
.L2833:
	mov	w23, 0
	b	.L2790
.L2832:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,948]
	cbz	w0, .L2808
	mov	w0, w24
	mov	w1, 1
	bl	rk_ftl_garbage_collect
.L2808:
	ldr	w0, [x29,136]
.L2788:
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
	stp	x29, x30, [sp, -288]!
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	cmp	w0, 16
	mov	w21, w1
	mov	w22, w2
	mov	x24, x3
	bne	.L2835
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartWrite
	b	.L2836
.L2835:
	adrp	x20, .LANCHOR0
	add	w26, w1, w2
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	w2, [x1,2060]
	cmp	w26, w2
	bhi	.L2836
	adrp	x3, .LANCHOR4
	ldrh	w1, [x1,2024]
	add	x0, x3, :lo12:.LANCHOR4
	sub	w26, w26, #1
	mov	w2, 2048
	str	x3, [x29,184]
	udiv	w25, w21, w1
	cmp	w22, w1, lsl 1
	str	w2, [x0,440]
	adrp	x2, .LANCHOR2
	add	x19, x2, :lo12:.LANCHOR2
	str	x2, [x29,152]
	udiv	w0, w26, w1
	str	w0, [x29,196]
	sub	w28, w0, w25
	add	w27, w28, 1
	ldr	w0, [x19,164]
	add	w0, w27, w0
	str	w0, [x19,164]
	ldr	w0, [x19,180]
	add	w0, w22, w0
	str	w0, [x19,180]
	cset	w0, cs
	cmp	w22, 8
	str	w0, [x29,220]
	bhi	.L2877
	add	x19, x19, 40
	b	.L2837
.L2877:
	sub	x19, x19, #8
.L2837:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w3, [x0,2080]
	cbz	w3, .L2838
	sub	w3, w3, #1
	mov	w2, 56
	ldr	x0, [x0,2088]
	umull	x3, w3, w2
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w25, w0
	bne	.L2839
	ldr	x0, [x29,152]
	add	x2, x0, :lo12:.LANCHOR2
	ldr	w0, [x2,168]
	add	w0, w0, 1
	str	w0, [x2,168]
	ldr	x0, [x29,184]
	add	x27, x0, :lo12:.LANCHOR4
	ldr	w0, [x27,444]
	add	w0, w0, 1
	str	w0, [x27,444]
	msub	w0, w25, w1, w21
	sub	w1, w1, w0
	ubfiz	x0, x0, 9, 23
	cmp	w1, w22
	csel	w19, w1, w22, ls
	ldr	x1, [x3,8]
	lsl	w23, w19, 9
	add	x0, x1, x0
	mov	w2, w23
	mov	x1, x24
	bl	ftl_memcpy
	cbnz	w28, .L2840
	ldr	w0, [x27,444]
	cmp	w0, 2
	bgt	.L2840
.L2873:
	mov	w0, 0
	b	.L2836
.L2840:
	sub	w22, w22, w19
	add	w21, w21, w19
	add	x24, x24, x23
	add	w25, w25, 1
	mov	w27, w28
.L2839:
	ldr	x0, [x29,184]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x19, [x0,408]
	str	wzr, [x0,444]
.L2838:
	ldr	w1, [x29,196]
	mov	w0, w25
	bl	FtlCacheMetchLpa
	cbz	w0, .L2841
	bl	FtlCacheWriteBack
.L2841:
	ldr	x0, [x29,184]
	str	w25, [x29,216]
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [x0,408]
	ldr	x0, [x29,152]
	add	x23, x0, :lo12:.LANCHOR2
	sub	x0, x23, #8
	str	x0, [x29,176]
	adrp	x0, .LC126
	add	x0, x0, :lo12:.LC126
	str	x0, [x29,112]
.L2842:
	cbz	w27, .L2903
	ldrh	w0, [x19,4]
	cbnz	w0, .L2843
	ldr	x1, [x29,176]
	adrp	x28, .LANCHOR1
	cmp	x19, x1
	bne	.L2844
	add	x0, x23, 40
	ldrh	w19, [x0,4]
	cbnz	w19, .L2845
	bl	allocate_new_data_superblock
	add	x0, x28, :lo12:.LANCHOR1
	str	w19, [x0,3012]
.L2845:
	ldr	x0, [x29,176]
	add	x28, x28, :lo12:.LANCHOR1
	add	x19, x23, 40
	bl	allocate_new_data_superblock
	ldr	w0, [x28,3012]
	cbnz	w0, .L2846
.L2847:
	ldr	x19, [x29,176]
	b	.L2846
.L2844:
	add	x28, x28, :lo12:.LANCHOR1
	str	w0, [x28,3012]
	ldr	x0, [x29,176]
	ldrh	w0, [x0,4]
	cbnz	w0, .L2847
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2846:
	ldrh	w0, [x19,4]
	cbnz	w0, .L2848
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2848:
	ldr	x0, [x29,184]
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [x0,408]
.L2843:
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w0, [x23,2032]
	ldrh	w1, [x19,4]
	str	xzr, [x29,208]
	ldr	w2, [x2,2080]
	sub	w0, w0, w2
	cmp	w1, w0
	csel	w0, w1, w0, ls
	cmp	w0, w27
	csel	w0, w0, w27, ls
	str	w0, [x29,148]
	ldr	w0, [x29,220]
	and	w0, w0, 1
	str	w0, [x29,124]
	add	w0, w22, w21
	str	w0, [x29,144]
.L2849:
	ldr	w0, [x29,208]
	ldr	w2, [x29,148]
	ldr	w1, [x29,216]
	cmp	w0, w2
	add	w26, w1, w0
	bcs	.L2904
	ldrh	w1, [x19,4]
	cbz	w1, .L2880
	ldr	w1, [x29,196]
	cmp	w26, w1
	cset	w7, eq
	cbz	w0, .L2851
	ldr	w1, [x29,124]
	tst	w7, w1
	beq	.L2851
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w2, [x29,144]
	ldrh	w1, [x1,2024]
	msub	w2, w26, w1, w2
	cmp	w2, w1
	bne	.L2880
.L2851:
	add	x1, x29, 228
	mov	w2, 0
	mov	w0, w26
	str	x7, [x29,104]
	bl	log2phys
	mov	x0, x19
	bl	get_new_active_ppa
	add	x4, x20, :lo12:.LANCHOR0
	mov	w5, 56
	str	x4, [x29,200]
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
	str	x0, [x29,136]
	ldr	x0, [x23,2120]
	ldr	x3, [x29,136]
	str	x0, [x29,128]
	add	x28, x0, x3
	ldrh	w0, [x4,2028]
	str	x28, [x1,16]
	str	w26, [x1,24]
	mul	w0, w0, w6
	ldr	x6, [x23,2080]
	and	x0, x0, 4294967292
	add	x0, x6, x0
	str	x0, [x1,8]
	mov	x0, x28
	mov	w1, 0
	bl	ftl_memset
	cmp	w26, w25
	ldr	x4, [x29,200]
	cset	w0, eq
	str	w0, [x29,192]
	cbnz	w0, .L2883
	ldr	x7, [x29,104]
	ldr	x5, [x29,160]
	cbz	w7, .L2852
	ldr	w0, [x29,144]
	ldrh	w2, [x4,2024]
	msub	w2, w26, w2, w0
	uxth	w0, w2
	str	w0, [x29,200]
	ldr	w0, [x29,192]
	str	w0, [x29,160]
	b	.L2855
.L2883:
	ldrh	w2, [x4,2024]
	udiv	w0, w21, w2
	msub	w0, w0, w2, w21
	str	w0, [x29,160]
	sub	w2, w2, w0
	cmp	w2, w22
	csel	w0, w2, w22, ls
	str	w0, [x29,200]
.L2855:
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w1, [x29,200]
	ldrh	w0, [x2,2024]
	cmp	w1, w0
	bne	.L2856
	ldr	w0, [x29,192]
	mov	x1, x24
	cbnz	w0, .L2857
	ldr	w0, [x29,200]
	mul	w1, w0, w26
	sub	w1, w1, w21
	ubfiz	x1, x1, 9, 23
	add	x1, x24, x1
.L2857:
	ldr	w2, [x29,220]
	mov	w0, 56
	add	x4, x20, :lo12:.LANCHOR0
	cbz	w2, .L2858
	ldr	w2, [x4,2080]
	umull	x0, w2, w0
	ldr	x2, [x4,2088]
	add	x0, x2, x0
	b	.L2902
.L2858:
	ldr	w2, [x4,2080]
	umull	x0, w2, w0
	ldr	x2, [x4,2088]
	add	x0, x2, x0
	ldr	x0, [x0,8]
	b	.L2900
.L2856:
	ldr	w0, [x29,228]
	cmn	w0, #1
	beq	.L2860
	str	w0, [x29,236]
	mov	w1, 56
	ldr	w0, [x2,2080]
	str	w26, [x29,256]
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
	bne	.L2861
	ldr	w0, [x23,416]
	add	w0, w0, 1
	str	w0, [x23,416]
	b	.L2863
.L2861:
	ldr	w0, [x28,8]
	cmp	w0, w26
	beq	.L2863
	ldr	w0, [x23,416]
	mov	w2, w26
	add	w0, w0, 1
	str	w0, [x23,416]
	ldr	x0, [x29,112]
	ldr	w1, [x28,8]
	bl	printk
	b	.L2863
.L2860:
	ldr	w0, [x2,2080]
	mov	w1, 56
	umull	x0, w0, w1
	ldr	x1, [x2,2088]
	ldrh	w2, [x2,2028]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L2863:
	ldr	w1, [x29,200]
	mov	w0, 56
	lsl	w2, w1, 9
	ldr	w1, [x29,192]
	cbz	w1, .L2864
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
	b	.L2901
.L2864:
	add	x5, x20, :lo12:.LANCHOR0
	ldr	w4, [x5,2080]
	ldr	x1, [x5,2088]
	umull	x0, w4, w0
	add	x0, x1, x0
	ldrh	w1, [x5,2024]
	mul	w1, w26, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w21
	ubfiz	x1, x1, 9, 23
	add	x1, x24, x1
	b	.L2901
.L2852:
	ldr	w0, [x29,220]
	ldrh	w1, [x4,2024]
	ldr	w2, [x4,2080]
	cbz	w0, .L2865
	mul	w1, w26, w1
	umull	x0, w2, w5
	sub	w1, w1, w21
	ldr	x2, [x4,2088]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x24, x1
.L2902:
	str	x1, [x0,8]
	b	.L2859
.L2865:
	umull	x0, w2, w5
	ldr	x2, [x4,2088]
	mul	w1, w26, w1
	add	x0, x2, x0
	sub	w1, w1, w21
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x24, x1
.L2900:
	ldrh	w2, [x4,2028]
.L2901:
	bl	ftl_memcpy
.L2859:
	ldr	x1, [x29,128]
	mov	w0, -3947
	ldr	x2, [x29,136]
	strh	w0, [x1,x2]
	add	x1, x20, :lo12:.LANCHOR0
	str	w26, [x28,8]
	ldr	w0, [x23,192]
	str	w0, [x28,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x23,192]
	ldr	w0, [x29,228]
	str	w0, [x28,12]
	ldrh	w0, [x19]
	strh	w0, [x28,2]
	ldr	w0, [x1,2080]
	add	w0, w0, 1
	str	w0, [x1,2080]
	ldr	x0, [x29,208]
	add	x0, x0, 1
	str	x0, [x29,208]
	b	.L2849
.L2904:
	str	w26, [x29,216]
	mov	x0, x2
	b	.L2850
.L2880:
	str	w26, [x29,216]
.L2850:
	sub	w27, w27, w0
	ldr	w0, [x29,220]
	cbnz	w0, .L2869
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	ldr	w0, [x23,2032]
	cmp	w1, w0
	bcs	.L2869
	ldrh	w0, [x19,4]
	cbz	w0, .L2869
.L2871:
	str	wzr, [x29,220]
	b	.L2842
.L2869:
	bl	FtlCacheWriteBack
	add	x0, x20, :lo12:.LANCHOR0
	cmp	w27, 3
	str	wzr, [x0,2080]
	bls	.L2871
	b	.L2842
.L2903:
	ldr	w1, [x29,196]
	mov	w0, w27
	sub	w1, w1, w25
	bl	rk_ftl_garbage_collect
	ldr	x0, [x29,152]
	add	x19, x0, :lo12:.LANCHOR2
	ldrh	w0, [x19,-16]
	cmp	w0, 15
	bhi	.L2873
	mov	w21, 65535
	mov	w20, 128
.L2899:
	ldrh	w0, [x19,288]
	cmp	w0, w21
	bne	.L2874
	ldrh	w0, [x19,944]
	cmp	w0, w21
	bne	.L2874
	mov	w0, 0
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L2874:
	mov	w0, 1
	strh	w20, [x19,858]
	mov	w1, w0
	strh	w20, [x19,856]
	bl	rk_ftl_garbage_collect
	mov	w0, 0
	mov	w1, 1
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x19,-16]
	cmp	w0, 8
	bls	.L2899
	b	.L2873
.L2836:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 288
	ret
	.size	FtlWrite, .-FtlWrite
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
	cbnz	w1, .L2908
.L2910:
	add	x0, x4, :lo12:.LANCHOR4
	ldr	w1, [x0,68]
	cmp	w3, w1
	bcs	.L2908
	ubfx	x5, x3, 5, 11
	ldr	x0, [x0,32]
	lsl	x5, x5, 2
	lsl	w6, w7, w3
	add	w3, w3, 1
	ldr	w1, [x0,x5]
	uxth	w3, w3
	orr	w6, w1, w6
	str	w6, [x0,x5]
	b	.L2910
.L2908:
	add	x4, x4, :lo12:.LANCHOR4
	mov	x0, x19
	ldr	x1, [x4,32]
	bl	ftl_memcpy
	mov	w2, 4
	adrp	x0, .LC127
	mov	x1, x19
	add	x0, x0, :lo12:.LC127
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
	ldr	x1, [x0,32]
	mul	w24, w23, w3
	ldr	x0, [x0,88]
	uxth	w20, w20
	str	x1, [x29,96]
	sub	w19, w3, #16
	str	x0, [x29,104]
.L2913:
	cmp	w20, w19
	ble	.L2921
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L2914
	add	x0, x21, :lo12:.LANCHOR4
	ldr	x0, [x0,88]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L2914
	mov	w0, w22
	cbz	x22, .L2915
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.13
	b	.L2915
.L2914:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2913
.L2921:
	mov	w0, -1
.L2915:
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
	ldr	x0, [x0,80]
	uxth	w19, w19
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L2923
.L2927:
	mov	w1, 0
	b	.L2924
.L2923:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L2925:
	cmp	w2, w19
	bge	.L2924
	add	x3, x20, :lo12:.LANCHOR4
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,80]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L2926
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L2926:
	cmp	w1, w0
	bcs	.L2927
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2925
.L2924:
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR4
	stp	x21, x22, [sp,32]
	add	x20, x20, :lo12:.LANCHOR4
	add	x22, x19, :lo12:.LANCHOR0
	add	x22, x22, 2104
	add	x20, x20, 136
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x24, x22
	mov	w23, 0
	mov	x25, x20
	stp	x27, x28, [sp,80]
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L2933:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,1966]
	cmp	w23, w0
	bcs	.L2951
	adrp	x1, .LANCHOR2
	ldrh	w27, [x24,12]
	add	x2, x1, :lo12:.LANCHOR2
	str	x1, [x29,104]
	ldr	x0, [x2,-72]
	ldr	x26, [x2,2104]
	mov	w2, 65535
	str	x0, [x20,8]
	cmp	w27, w2
	str	x26, [x20,16]
	beq	.L2934
	ldrh	w0, [x21,2014]
	mov	w1, 1
	mov	w2, w1
	madd	w27, w23, w0, w27
	lsl	w0, w27, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x21,2014]
	ldr	x0, [x22,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L2935
.L2934:
	mov	w1, w23
	mov	w28, -3872
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x22,32]
	bl	FtlBbt2Bitmap
	ldrh	w21, [x21,2014]
	sub	w21, w21, #1
	uxth	w21, w21
.L2936:
	add	x27, x19, :lo12:.LANCHOR0
	ldrh	w0, [x27,2014]
	madd	w0, w23, w0, w21
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	bne	.L2952
	sub	w21, w21, #1
	uxth	w21, w21
	b	.L2936
.L2952:
	ldr	x0, [x29,104]
	mov	w1, 0
	mov	w2, 16
	strh	w21, [x24,12]
	add	x3, x0, :lo12:.LANCHOR2
	str	x3, [x29,96]
	ldr	x0, [x3,2104]
	bl	ftl_memset
	strh	w28, [x26]
	str	wzr, [x26,4]
	ldr	x3, [x29,96]
	ldrh	w0, [x24,12]
	strh	w0, [x26,2]
	ldrh	w0, [x27,2014]
	ldrh	w1, [x24,12]
	ldrh	w2, [x3,2224]
	madd	w27, w23, w0, w1
	ldr	x1, [x22,32]
	lsl	w2, w2, 2
	lsl	w0, w27, 10
	str	w0, [x25,4]
	ldr	x0, [x25,8]
	bl	ftl_memcpy
	mov	w1, 1
	mov	x0, x25
	mov	w2, w1
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x25
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x25]
	cmn	w0, #1
	bne	.L2935
	mov	w0, w27
	bl	FtlBbmMapBadBlock
	b	.L2936
.L2935:
	mov	w0, w27
	add	w23, w23, 1
	add	x22, x22, 8
	add	x24, x24, 2
	bl	FtlBbmMapBadBlock
	b	.L2933
.L2951:
	mov	w20, 0
.L2940:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,2032]
	cmp	w0, w20
	bls	.L2953
	mov	w0, w20
	add	w20, w20, 1
	bl	FtlBbmMapBadBlock
	uxth	w20, w20
	b	.L2940
.L2953:
	ldrh	w20, [x21,2116]
	mov	w23, 65535
	sub	w20, w20, #1
	uxth	w20, w20
.L2942:
	add	x22, x21, 2104
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L2946
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2943
	mov	w0, w20
	bl	FlashTestBlk
	cbz	w0, .L2944
	mov	w0, w20
	bl	FtlBbmMapBadBlock
	b	.L2943
.L2944:
	ldrh	w0, [x21,2104]
	cmp	w0, w23
	bne	.L2945
	strh	w20, [x21,2104]
	b	.L2943
.L2945:
	strh	w20, [x22,4]
	b	.L2946
.L2943:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2942
.L2946:
	adrp	x0, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x19,2104]
	ldr	x2, [x0,-96]
	str	wzr, [x19,2112]
	lsl	w1, w1, 10
	strh	wzr, [x19,2106]
	str	w1, [x2,4]
	mov	w2, 2
	ldrh	w1, [x19,2108]
	ldr	x0, [x0,-96]
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	adrp	x20, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	str	wzr, [x0,188]
	str	wzr, [x0,192]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,1948]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2955
	bl	FtlMakeBbt
.L2955:
	mov	w0, 0
.L2956:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2024]
	cmp	w0, w2, lsl 7
	bge	.L2983
	add	x3, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 2, 16
	mvn	w1, w0
	ldr	x4, [x3,2088]
	orr	w1, w0, w1, lsl 16
	add	w0, w0, 1
	str	w1, [x4,x2]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	uxth	w0, w0
	ldr	x3, [x3,2096]
	str	w1, [x3,x2]
	b	.L2956
.L2983:
	ldrh	w22, [x1,1952]
	mov	w21, 0
.L2958:
	add	x23, x20, :lo12:.LANCHOR0
	ldrh	w0, [x23,1954]
	cmp	w0, w22
	bls	.L2984
	mov	w0, w22
	mov	w1, 1
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w21, w0
	uxth	w22, w22
	uxth	w21, w0
	b	.L2958
.L2984:
	ldrh	w0, [x23,1944]
	sub	w1, w21, #3
	cmp	w1, w0, lsl 1
	bge	.L2960
.L2964:
	mov	w21, 0
	mov	w22, w21
	b	.L2961
.L2960:
	udiv	w21, w21, w0
	ldr	w0, [x23,2052]
	add	w0, w21, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x23,1948]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w21, [x23,1952]
.L2962:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,1954]
	cmp	w0, w21
	bls	.L2964
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	uxth	w21, w21
	b	.L2962
.L2961:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,1952]
	cmp	w1, w22
	bls	.L2985
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w21, w0
	uxth	w22, w22
	uxth	w21, w0
	b	.L2961
.L2985:
	ldrh	w1, [x0,1954]
	add	x6, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,1944]
	str	w1, [x0,2100]
	ldr	w1, [x0,1956]
	udiv	w3, w1, w2
	str	w3, [x0,2096]
	ubfx	x5, x3, 5, 16
	add	w4, w5, 36
	strh	w4, [x6,264]
	mov	w4, 24
	mul	w4, w2, w4
	cmp	w21, w4
	ble	.L2966
	sub	w1, w1, w21
	udiv	w1, w1, w2
	str	w1, [x0,2096]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x6,264]
.L2966:
	add	x4, x19, :lo12:.LANCHOR2
	ldr	w0, [x4,152]
	cmp	w0, 1
	bne	.L2967
	ldrh	w1, [x4,264]
	udiv	w0, w21, w2
	add	w0, w1, w0
	add	w0, w1, w0, asr 2
	strh	w0, [x4,264]
.L2967:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L2968
	add	x1, x19, :lo12:.LANCHOR2
	udiv	w0, w21, w2
	ldrh	w4, [x1,264]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,264]
.L2968:
	add	x6, x20, :lo12:.LANCHOR0
	ldrh	w1, [x6,2010]
	cbz	w1, .L2970
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w4, [x0,264]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,264]
	mul	w4, w1, w2
	cmp	w4, w21
	ble	.L2970
	add	w1, w1, 32
	str	w3, [x6,2096]
	add	w1, w5, w1
	strh	w1, [x0,264]
.L2970:
	add	x19, x19, :lo12:.LANCHOR2
	add	x22, x20, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR4
	mov	x21, x19
	mov	w23, -1
	ldr	w1, [x22,2096]
	ldrh	w0, [x19,264]
	sub	w0, w1, w0
	add	x1, x24, :lo12:.LANCHOR4
	mul	w0, w0, w2
	str	w0, [x1,192]
	ldrh	w1, [x22,2018]
	mul	w0, w0, w1
	ldrh	w1, [x22,2024]
	str	w0, [x22,2096]
	mul	w0, w0, w1
	str	w0, [x22,2060]
	bl	FtlBbmTblFlush
	ldrh	w2, [x22,1954]
	mov	w1, 0
	ldr	x0, [x19,-48]
	lsl	w2, w2, 1
	bl	ftl_memset
	str	wzr, [x19,148]
	ldrh	w2, [x22,1952]
	mov	w0, 1
	strb	w0, [x21],-8
	mov	w1, 255
	ldr	x0, [x20,#:lo12:.LANCHOR0]
	lsr	w2, w2, 3
	mov	x22, x24
	strh	w23, [x19,288]
	strh	wzr, [x19,290]
	strb	wzr, [x19,294]
	strb	wzr, [x19,296]
	strh	wzr, [x19,-6]
	strb	wzr, [x19,-2]
	strh	wzr, [x19,-8]
	bl	ftl_memset
.L2972:
	mov	x0, x21
	bl	make_superblock
	ldrb	w0, [x21,7]
	cbnz	w0, .L2973
	ldrh	w1, [x21]
	ldr	x0, [x19,-48]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L2972
.L2973:
	ldr	w0, [x19,188]
	add	x20, x19, 40
	str	w0, [x21,12]
	add	w0, w0, 1
	ldrh	w1, [x21]
	str	w0, [x19,188]
	ldr	x0, [x19,-48]
	ldrh	w2, [x21,4]
	strh	w2, [x0,x1,lsl 1]
	strh	wzr, [x20,2]
	ldrh	w0, [x21]
	mov	w21, -1
	strb	wzr, [x20,6]
	add	w0, w0, 1
	strh	w0, [x19,40]
	mov	w0, 1
	strb	w0, [x20,8]
.L2974:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2975
	ldrh	w1, [x20]
	ldr	x0, [x19,-48]
	strh	w21, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2974
.L2975:
	ldr	w0, [x19,188]
	add	x22, x22, :lo12:.LANCHOR4
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,188]
	ldr	x0, [x19,-48]
	ldrh	w2, [x20,4]
	mov	w20, -1
	strh	w2, [x0,x1,lsl 1]
	strh	w20, [x19,88]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19,272]
	ldr	w0, [x22,192]
	strh	w0, [x19,278]
	ldr	w0, [x19,188]
	str	w0, [x19,280]
	add	w0, w0, 1
	strh	wzr, [x19,274]
	strh	w20, [x19,276]
	str	w0, [x19,188]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L2976
	adrp	x0, .LANCHOR1+504
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+504]
.L2976:
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
	cbz	w0, .L2987
.L2989:
	mov	w0, 0
	b	.L2988
.L2987:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,24]
	bl	FlashInit
	cbnz	w0, .L2989
	bl	FlashLoadFactorBbt
	cbz	w0, .L2990
	bl	FlashMakeFactorBbt
.L2990:
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20,80]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L2991
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L2993:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L2993
	cmp	w0, 6
	bhi	.L2994
	add	x0, x19, :lo12:.LANCHOR0
	b	.L3017
.L2994:
	mov	w1, 0
	mov	w4, 1
.L2997:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L2997
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L3017
	mov	w1, 36
.L3017:
	strb	w1, [x0,9]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,9]
	strh	w1, [x0,194]
.L2991:
	adrp	x0, .LC72
	adrp	x1, .LC73
	add	x1, x1, :lo12:.LC73
	add	x0, x0, :lo12:.LC72
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	add	x0, x19, 168
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,1948]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L2999:
	bl	FtlLoadBbt
	cbz	w0, .L3000
.L3019:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L3020
	add	w19, w19, 1
	b	.L2999
.L3020:
	mov	w0, -1
	b	.L2988
.L3000:
	bl	FtlSysBlkInit
	cbnz	w0, .L3019
	adrp	x1, .LANCHOR1+504
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+504]
.L2988:
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
	bne	.L3021
	bl	flash_enter_slc_mode
.L3021:
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
	bne	.L3023
	bl	flash_exit_slc_mode
.L3023:
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
	adrp	x0, .LC128
	add	x0, x0, :lo12:.LC128
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
.L3026:
	cmp	w23, w26
	bcs	.L3032
	ldrb	w1, [x19,120]
	sub	w24, w6, w3
	add	w4, w23, w21
	lsr	w4, w4, 2
	uxth	w24, w24
	add	x0, x19, x4, uxth 1
	ldrh	w0, [x0,196]
	cbz	w1, .L3027
	ldr	w2, [x19,1868]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	uxth	w4, w4
	cmp	w2, w1
	csel	w0, w4, w0, eq
.L3027:
	add	w3, w3, w5
	add	x27, x7, :lo12:.LANCHOR4
	ldrb	w4, [x19,1936]
	madd	w25, w0, w28, w3
	ldr	x0, [x19,744]
	ldr	x2, [x27,80]
	str	x6, [x29,96]
	ldrb	w1, [x0,9]
	ldrb	w0, [x27,74]
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
	ldr	x1, [x27,80]
	add	x0, x20, x0
	mov	w2, 2048
	add	w23, w24, w23
	bl	ftl_memcpy
	uxth	w23, w23
	mov	w3, 0
	ldr	x6, [x29,96]
	ldr	x5, [x29,104]
	ldr	x7, [x29,112]
	b	.L3026
.L3032:
	adrp	x0, .LC129
	mov	w1, w22
	mov	w2, w26
	mov	w3, 0
	add	x0, x0, :lo12:.LC129
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
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
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
.L3034:
	cmp	w23, w27
	bcs	.L3048
	add	w4, w23, w26
	lsr	w4, w4, 2
	uxth	w0, w4
	and	w4, w4, 65535
	cbz	w0, .L3035
	add	x1, x19, 196
	add	w2, w4, 1
	ldrh	w3, [x1,w2,sxtw 1]
	ldrb	w1, [x19,120]
	cbz	w1, .L3036
	ldr	w2, [x19,1868]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3036
	add	w3, w0, 1
	uxth	w3, w3
.L3036:
	sub	w3, w3, #1
	str	wzr, [x29,132]
	lsl	w3, w3, 2
	str	w3, [x29,128]
.L3035:
	add	x1, x19, 196
	ldrh	w25, [x1,w4,sxtw 1]
	ldrb	w1, [x19,120]
	cbz	w1, .L3037
	ldr	w2, [x19,1868]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w25, w25, w0, ne
.L3037:
	add	x0, x28, :lo12:.LANCHOR4
	ldrb	w4, [x19,1936]
	madd	w3, w25, w20, w21
	ubfiz	x2, x23, 9, 16
	str	x4, [x29,120]
	ldrb	w0, [x0,74]
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
	b	.L3034
.L3048:
	adrp	x0, .LC131
	mov	w1, w24
	mov	w2, w27
	mov	w3, 0
	add	x0, x0, :lo12:.LC131
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
	cbz	x20, .L3059
	add	w19, w25, 511
	lsr	w19, w19, 9
	cmp	w19, 255
	bhi	.L3051
	ubfiz	x0, x19, 9, 23
	mov	w2, 256
	add	x0, x21, x0
	mov	x1, x21
	sub	w2, w2, w19
	bl	memcpy
.L3051:
	add	w19, w19, 128
	mov	w0, 256
	cmp	w19, 256
	mov	x1, x23
	csel	w19, w19, w0, ls
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	mov	w2, 4
	mov	w3, 5
	bl	rknand_print_hex
	ldr	x0, [x29,120]
	ldr	w1, [x21,512]
	add	x27, x0, :lo12:.LANCHOR0
	adrp	x0, .LC133
	add	x0, x0, :lo12:.LC133
	ldrb	w2, [x27,9]
	bl	printk
	ldrb	w0, [x27,9]
	ldr	w1, [x21,512]
	cmp	w1, w0
	bls	.L3052
	str	w0, [x21,512]
.L3052:
	adrp	x0, .LC134
	mov	w2, w25
	mul	w22, w22, w26
	add	x0, x0, :lo12:.LC134
	mov	w1, w19
	mov	x24, 0
	adrp	x26, .LC135
	bl	printk
	adrp	x27, .LC136
	adrp	x28, .LC137
	lsl	w0, w19, 7
	uxth	w22, w22
	str	w0, [x29,116]
	mov	w25, w24
	add	x26, x26, :lo12:.LC135
	add	x27, x27, :lo12:.LC136
	add	x28, x28, :lo12:.LC137
.L3057:
	ldr	x1, [x29,120]
	mov	w8, w24
	ldr	w0, [x23,x24,lsl 2]
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w1, [x1,194]
	cmp	w0, w1
	bcs	.L3053
	adrp	x1, .LANCHOR4+68
	ldr	w1, [x1,#:lo12:.LANCHOR4+68]
	cmp	w0, w1
	bcc	.L3053
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
.L3054:
	ldr	w1, [x29,116]
	mov	w3, w0
	cmp	w0, w1
	bcs	.L3064
	ldr	w4, [x20,x0,lsl 2]
	mov	x7, x0
	add	x0, x0, 1
	add	x1, x21, x0, lsl 2
	ldr	w5, [x1,-4]
	cmp	w4, w5
	beq	.L3054
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
	adrp	x0, .LC138
	add	x0, x0, :lo12:.LC138
	bl	printk
	b	.L3053
.L3064:
	add	w25, w25, 1
.L3053:
	add	x24, x24, 1
	cmp	x24, 5
	bne	.L3057
	mov	x0, x20
	bl	ftl_free
	cmp	w25, wzr
	csetm	w0, eq
.L3059:
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
.L3066:
	cmp	w1, w2
	bls	.L3068
	ldrb	w4, [x5,x2]
	add	x6, x3, 3016
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3066
.L3068:
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
	beq	.L3071
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3072
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3073
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3074
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3075
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3076
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3133
	bl	rknand_dev_flush
	b	.L3165
.L3076:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3078
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3079
	b	.L3133
.L3074:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3080
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3081
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3082
	b	.L3133
.L3081:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3080
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3080
	b	.L3133
.L3072:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3083
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3084
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3085
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3086
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3087
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3088
	b	.L3133
.L3084:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3089
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3090
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3091
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3092
	b	.L3133
.L3090:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3092
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3089
	b	.L3133
.L3086:
	adrp	x0, .LC139
	add	x0, x0, :lo12:.LC139
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3093
.L3098:
	mov	x0, -12
	b	.L3070
.L3093:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3172
	adrp	x0, .LC141
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC141
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3168
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
	cbz	x0, .L3174
	adrp	x0, .LC142
	add	x0, x0, :lo12:.LC142
.L3167:
	bl	printk
.L3168:
	mov	x0, x19
.L3169:
	bl	ftl_free
.L3170:
	mov	x0, -14
	b	.L3070
.L3085:
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3098
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3172
	ldr	w1, [x19]
	adrp	x0, .LC144
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC144
	bl	printk
	adrp	x0, .LANCHOR4
	add	x21, x0, :lo12:.LANCHOR4
	mov	x20, x0
	ldr	x1, [x21,448]
	cbnz	x1, .L3100
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x21,448]
	cbz	x0, .L3168
.L3100:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3168
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3168
	add	x20, x20, :lo12:.LANCHOR4
	uxtw	x2, w2
	ldr	x1, [x20,448]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3174:
	mov	x0, x19
	bl	ftl_free
	b	.L3165
.L3088:
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3098
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3102
.L3172:
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	b	.L3167
.L3102:
	ldr	w1, [x19]
	adrp	x0, .LC146
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC146
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3168
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20,448]
	cbz	x0, .L3168
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3105
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3070
.L3105:
	bl	rknand_device_lock
	ldr	x1, [x20,448]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,448]
	bl	ftl_free
	str	xzr, [x20,448]
	b	.L3174
.L3087:
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3098
	bl	ReadFlashInfo
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3176
.L3071:
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	bl	printk
	bl	rknand_device_lock
	bl	FtlReInitForSDUpdata
	mov	w19, w0
	bl	rknand_device_unlock
	cbnz	w19, .L3170
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3098
	bl	rknand_device_lock
	mov	w1, w19
	mov	w2, 64
	mov	x0, x21
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	adrp	x0, .LC149
	mov	x1, x21
	add	x0, x0, :lo12:.LC149
	mov	w2, 4
	mov	w3, 8
	bl	rknand_print_hex
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3176
.L3083:
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	bl	printk
	adrp	x0, .LANCHOR4+456
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR4+456]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
	b	.L3161
.L3091:
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3098
	bl	rknand_device_lock
	mov	w1, 2
	mov	x2, x21
	mov	w0, 520
	bl	FtlVendorPartRead
	bl	rknand_device_unlock
	mov	x0, x20
	mov	x1, x21
	mov	x2, 1024
.L3176:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3169
	bl	ftl_free
	b	.L3097
.L3075:
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3111
.L3116:
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	bl	printk
	b	.L3170
.L3111:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3112
.L3113:
	mov	x19, -1
	b	.L3097
.L3112:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3113
	adrp	x21, .LANCHOR4
	add	x0, x29, 56
	add	x21, x21, :lo12:.LANCHOR4
	mov	x2, 512
	ldr	x1, [x21,456]
	bl	memcpy
	ldr	w1, [x21,464]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3114
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3114:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
.L3162:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
.L3161:
	bl	rk_copy_to_user
	cbnz	x0, .L3170
	b	.L3165
.L3078:
	adrp	x0, .LC153
	add	x0, x0, :lo12:.LC153
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3116
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3113
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3113
	adrp	x1, .LANCHOR4
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR4
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,464]
	cmp	w3, w2
	bne	.L3070
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L3070
	ldr	x19, [x1,456]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3166
.L3082:
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3116
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3113
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3113
	adrp	x19, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR4
	ldr	w1, [x0,468]
	cbnz	w1, .L3117
.L3120:
	mov	x0, 0
	b	.L3070
.L3117:
	ldr	x1, [x0,472]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3118
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,472]
	str	w2, [x1,4]
	ldr	x0, [x0,472]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3118:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	x1, [x20,472]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,456]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3119
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,456]
	str	w1, [x0,4]
	ldr	x0, [x20,456]
	str	wzr, [x0,8]
.L3119:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,456]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,468]
	str	wzr, [x19,464]
	b	.L3165
.L3073:
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3116
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3113
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3113
	adrp	x19, .LANCHOR4
	add	x1, x19, :lo12:.LANCHOR4
	ldr	w2, [x1,468]
	cmp	w2, 1
	beq	.L3120
	ldr	x2, [x1,472]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3121
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,472]
	str	w3, [x2,4]
	ldr	x1, [x1,472]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3121:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w0, 1
	ldr	x1, [x20,472]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,456]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3122
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,456]
	str	w1, [x0,4]
	ldr	x0, [x20,456]
	str	wzr, [x0,8]
.L3122:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,456]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,468]
	b	.L3165
.L3079:
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3116
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3113
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3113
	adrp	x1, .LANCHOR4
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR4
	uxtw	x2, w2
	add	x1, x1, 480
	b	.L3163
.L3080:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3123
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	b	.L3164
.L3123:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3125
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
	b	.L3164
.L3125:
	adrp	x0, .LC159
	add	x0, x0, :lo12:.LC159
.L3164:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3116
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3170
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR4
	bne	.L3126
	add	x0, x0, :lo12:.LANCHOR4
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,456]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3070
	b	.L3170
.L3126:
	add	x20, x0, :lo12:.LANCHOR4
	ldr	w1, [x20,992]
	cmp	w1, 10
	bhi	.L3170
	ldr	x1, [x20,456]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3127
	cbz	w3, .L3127
	adrp	x0, .LC160
	mov	w1, w2
	add	x0, x0, :lo12:.LC160
	bl	printk
	ldr	w0, [x20,992]
	add	w0, w0, 1
	str	w0, [x20,992]
	b	.L3170
.L3127:
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x0,992]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3128
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3129
.L3128:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3129:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3165
	b	.L3097
.L3092:
	adrp	x0, .LC161
	add	x0, x0, :lo12:.LC161
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3116
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3113
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3113
	mov	w0, 30224
	adrp	x1, .LANCHOR4
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x29, 64
	bne	.L3130
	ldr	x1, [x1,1000]
	b	.L3173
.L3130:
	ldr	x1, [x1,1008]
.L3173:
	add	x1, x1, 8
.L3163:
	bl	memcpy
	b	.L3162
.L3089:
	adrp	x0, .LC162
	add	x0, x0, :lo12:.LC162
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3116
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3113
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3113
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	bne	.L3132
	ldr	x0, [x19,1000]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,1000]
	b	.L3166
.L3132:
	ldr	x0, [x19,1008]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,1008]
	mov	w0, 3
.L3166:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3097
.L3165:
	mov	x19, 0
.L3097:
	adrp	x0, .LC163
	mov	x1, x19
	add	x0, x0, :lo12:.LC163
	bl	printk
	mov	x0, x19
	b	.L3070
.L3133:
	mov	x0, -22
.L3070:
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
	ldr	x1, [x19,424]
	str	x1, [x19,472]
	add	x0, x1, 512
	str	x0, [x19,456]
	add	x0, x1, 1024
	add	x1, x1, 1536
	str	x0, [x19,1000]
	add	x0, x19, 480
	str	xzr, [x19,448]
	str	x1, [x19,1008]
	bl	ftl_memcpy
	ldr	x21, [x19,472]
	str	wzr, [x19,464]
	str	wzr, [x19,992]
	ldr	w0, [x21,16]
	ldr	w22, [x21,508]
	str	w0, [x19,468]
	cbz	w22, .L3179
	mov	x0, x21
	mov	w1, 508
	bl	JSHash
	cmp	w22, w0
	beq	.L3179
	adrp	x0, .LC164
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC164
	str	wzr, [x19,468]
	bl	printk
.L3179:
	add	x0, x20, :lo12:.LANCHOR4
	ldr	w1, [x0,468]
	cbz	w1, .L3180
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,464]
.L3180:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 2
	ldr	x1, [x20,1000]
	bl	StorageSysDataLoad
	ldr	x1, [x20,1008]
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
	str	x0, [x1,1016]
	cbz	x0, .L3189
	mov	w22, 0
	adrp	x24, .LC165
	mov	w19, w22
	mov	w23, w22
	add	x24, x24, :lo12:.LC165
.L3193:
	add	x20, x21, :lo12:.LANCHOR4
	ldr	x2, [x20,1016]
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
	cbnz	w0, .L3190
	ldr	x3, [x20,1016]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,1016]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3191
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3191
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w22, w22, w23, ne
	csel	w19, w19, w0, ne
.L3191:
	cbnz	w23, .L3201
	mov	w23, 1
	b	.L3193
.L3201:
	cbz	w19, .L3194
	bl	rknand_device_lock
	lsl	w0, w22, 7
	mov	w1, 128
	add	w0, w0, 256
	mov	x2, x20
	bl	FtlVendorPartRead
	mov	w19, w0
	bl	rknand_device_unlock
	cbz	w19, .L3189
	b	.L3190
.L3194:
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
	b	.L3189
.L3190:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w19, -1
	ldr	x0, [x21,1016]
	bl	kfree
	str	xzr, [x21,1016]
.L3189:
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
	adrp	x3, .LANCHOR4+1016
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR4+1016]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3203
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3204:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3208
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3204
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
	b	.L3203
.L3208:
	mov	w3, -1
.L3203:
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
	adrp	x0, .LANCHOR4+1016
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR4+1016]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3210
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3211:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3226
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3211
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3212
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3210
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3213:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3227
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
	b	.L3213
.L3227:
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
	b	.L3225
.L3212:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3225
.L3226:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3210
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
.L3225:
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
	lsl	w0, w23, 7
	mov	x2, x19
	mov	w1, 128
	add	w0, w0, 256
	bl	FtlVendorPartWrite
	bl	rknand_device_unlock
	mov	w2, 0
.L3210:
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
	cbz	x0, .L3229
	mov	w1, 30209
	movk	w1, 0x4004, lsl 16
	cmp	w22, w1
	beq	.L3231
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w22, w2
	beq	.L3232
	b	.L3243
.L3231:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3243
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3234
.L3235:
	mov	x20, -1
	b	.L3230
.L3234:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3235
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3230
.L3232:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3243
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3235
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3235
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3243
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3230
.L3243:
	mov	x20, -14
.L3230:
	mov	x0, x19
	bl	kfree
.L3229:
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
	.type	__func__.19307, %object
	.size	__func__.19307, 11
__func__.19307:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.20205, %object
	.size	__func__.20205, 21
__func__.20205:
	.string	"FtlVpcCheckAndModify"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"FlashEraseBlocks pageAddr error %x\n"
.LC1:
	.string	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\n"
.LC2:
	.string	"FLASH INFO:\n"
.LC3:
	.string	"FLASH ID: %x\n"
.LC4:
	.string	"Device Capacity: %d MB\n"
.LC5:
	.string	"FMWAIT: %x %x %x %x\n"
.LC6:
	.string	"FTL INFO:\n"
.LC7:
	.string	"g_MaxLpn = 0x%x\n"
.LC8:
	.string	"g_VaildLpn = 0x%x\n"
.LC9:
	.string	"read_page_count = 0x%x\n"
.LC10:
	.string	"discard_page_count = 0x%x\n"
.LC11:
	.string	"write_page_count = 0x%x\n"
.LC12:
	.string	"cache_write_count = 0x%x\n"
.LC13:
	.string	"l2p_write_count = 0x%x\n"
.LC14:
	.string	"gc_page_count = 0x%x\n"
.LC15:
	.string	"totle_write = %d MB\n"
.LC16:
	.string	"totle_read = %d MB\n"
.LC17:
	.string	"GSV = 0x%x\n"
.LC18:
	.string	"GDV = 0x%x\n"
.LC19:
	.string	"bad blk num = %d %d\n"
.LC20:
	.string	"free_superblocks = 0x%x\n"
.LC21:
	.string	"mlc_EC = 0x%x\n"
.LC22:
	.string	"slc_EC = 0x%x\n"
.LC23:
	.string	"avg_EC = 0x%x\n"
.LC24:
	.string	"sys_EC = 0x%x\n"
.LC25:
	.string	"max_EC = 0x%x\n"
.LC26:
	.string	"min_EC = 0x%x\n"
.LC27:
	.string	"PLT = 0x%x\n"
.LC28:
	.string	"POT = 0x%x\n"
.LC29:
	.string	"MaxSector = 0x%x\n"
.LC30:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC31:
	.string	"sys_blks_pp = 0x%x\n"
.LC32:
	.string	"free sysblock = 0x%x\n"
.LC33:
	.string	"data_blks_pp = 0x%x\n"
.LC34:
	.string	"data_op_blks_pp = 0x%x\n"
.LC35:
	.string	"max_data_blks = 0x%x\n"
.LC36:
	.string	"Sys.id = 0x%x\n"
.LC37:
	.string	"Bbt.id = 0x%x\n"
.LC38:
	.string	"ACT.page = 0x%x\n"
.LC39:
	.string	"ACT.plane = 0x%x\n"
.LC40:
	.string	"ACT.id = 0x%x\n"
.LC41:
	.string	"ACT.mode = 0x%x\n"
.LC42:
	.string	"ACT.a_pages = 0x%x\n"
.LC43:
	.string	"ACT VPC = 0x%x\n"
.LC44:
	.string	"BUF.page = 0x%x\n"
.LC45:
	.string	"BUF.plane = 0x%x\n"
.LC46:
	.string	"BUF.id = 0x%x\n"
.LC47:
	.string	"BUF.mode = 0x%x\n"
.LC48:
	.string	"BUF.a_pages = 0x%x\n"
.LC49:
	.string	"BUF VPC = 0x%x\n"
.LC50:
	.string	"TMP.page = 0x%x\n"
.LC51:
	.string	"TMP.plane = 0x%x\n"
.LC52:
	.string	"TMP.id = 0x%x\n"
.LC53:
	.string	"TMP.mode = 0x%x\n"
.LC54:
	.string	"TMP.a_pages = 0x%x\n"
.LC55:
	.string	"GC.page = 0x%x\n"
.LC56:
	.string	"GC.plane = 0x%x\n"
.LC57:
	.string	"GC.id = 0x%x\n"
.LC58:
	.string	"GC.mode = 0x%x\n"
.LC59:
	.string	"GC.a_pages = 0x%x\n"
.LC60:
	.string	"WR_CHK = 0x%x %x %x %x\n"
.LC61:
	.string	"Read Err Cnt = 0x%x\n"
.LC62:
	.string	"Prog Err Cnt = 0x%x\n"
.LC63:
	.string	"gc_free_blk_th= 0x%x\n"
.LC64:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC65:
	.string	"gc_skip_write_count= 0x%x\n"
.LC66:
	.string	"gc_blk_index= 0x%x\n"
.LC67:
	.string	"free min EC= 0x%x\n"
.LC68:
	.string	"free max EC= 0x%x\n"
.LC69:
	.string	"GC__SB VPC = 0x%x\n"
.LC70:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC71:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC72:
	.string	"%s\n"
.LC73:
	.string	"FTL version: 5.0.44 20171016"
.LC74:
	.string	"GetSwlReplaceBlock min_ec_id =%x %x\n"
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
	.string	"no ect"
.LC105:
	.string	"slc mode"
.LC106:
	.string	"FlashMakeFactorBbt %d\n"
.LC107:
	.string	"bad block:%d %d\n"
.LC108:
	.string	"FMFB:%d %d\n"
.LC109:
	.string	"E:bad block:%d\n"
.LC110:
	.string	"FMFB:Save %d %d\n"
.LC111:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC112:
	.string	"FtlBbmTblFlush error:%x\n"
.LC113:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC114:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC115:
	.string	"decrement_vpc_count %x = %d\n"
.LC116:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC117:
	.string	"FtlMapWritePage error = %x \n"
.LC118:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC119:
	.string	"spuer block %x vpn is 0\n "
.LC120:
	.string	"...%s enter...\n"
.LC121:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC122:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC123:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC124:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC125:
	.string	"FtlInit %x\n"
.LC126:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC127:
	.string	"BBT:"
.LC128:
	.string	"IdBlockReadData %x %x\n"
.LC129:
	.string	"IdBlockReadData %x %x ret= %x\n"
.LC130:
	.string	"IDBlockWriteData %x %x\n"
.LC131:
	.string	"IDBlockWriteData %x %x ret= %x\n"
.LC132:
	.string	"idblk:"
.LC133:
	.string	"idb reverse %x %x\n"
.LC134:
	.string	"write_idblock totle_sec %x %x\n"
.LC135:
	.string	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x\n"
.LC136:
	.string	"write"
.LC137:
	.string	"read"
.LC138:
	.string	"write_idblock error\n"
.LC139:
	.string	"READ_SECTOR_IO\n"
.LC140:
	.string	"rk_copy_from_user error\n"
.LC141:
	.string	"READ_SECTOR_IO %x %x\n"
.LC142:
	.string	"rk_copy_to_user error\n"
.LC143:
	.string	"WRITE_SECTOR_IO\n"
.LC144:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC145:
	.string	"END_WRITE_SECTOR_IO\n"
.LC146:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC147:
	.string	"GET_FLASH_INFO_IO\n"
.LC148:
	.string	"GET_BAD_BLOCK_IO\n"
.LC149:
	.string	"bbt:"
.LC150:
	.string	"GET_LOCK_FLAG_IO\n"
.LC151:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC152:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC153:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC154:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC155:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC156:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC157:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC158:
	.string	"RKNAND_LOADER_STATUS\n"
.LC159:
	.string	"RKNAND_LOADER_LOCK\n"
.LC160:
	.string	"LockKey not match %d\n"
.LC161:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC162:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC163:
	.string	"return ret = %lx\n"
.LC164:
	.string	"secureBootEn check error\n"
.LC165:
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
	.type	g_totle_vendor_block, %object
	.size	g_totle_vendor_block, 2
g_totle_vendor_block:
	.zero	2
	.zero	6
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
	.type	gToggleModeClkDiv, %object
	.size	gToggleModeClkDiv, 4
gToggleModeClkDiv:
	.zero	4
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
