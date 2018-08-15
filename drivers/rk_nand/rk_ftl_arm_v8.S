/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-09-05
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
	adrp	x1, .LANCHOR0+128
	uxtb	w0, w0
	tst	w0, 6
	ldr	x2, [x1,#:lo12:.LANCHOR0+128]
	ldr	w1, [x2]
	beq	.L73
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
	b	.L75
.L73:
	and	w1, w1, -8193
.L75:
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
	mov	w0, 200
	bl	NandcDelayns
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
	ldr	w1, [x0,1856]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L191
	mov	w0, 0
	bl	flash_enter_slc_mode
.L191:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,1860]
	cbz	w0, .L192
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L192
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1860]
.L192:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,24]
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
	ble	.L201
	ldr	x0, [x1,128]
	mov	w1, 8354
	b	.L208
.L201:
	cmp	w0, 220
	ble	.L203
	ldr	x0, [x1,128]
	b	.L209
.L203:
	cmp	w0, 185
	ble	.L204
	ldr	x0, [x1,128]
	mov	w1, 4226
	b	.L208
.L204:
	cmp	w0, 160
	ldr	x0, [x1,128]
	ble	.L205
	mov	w1, 4194
	b	.L208
.L205:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L208
	cmp	w19, 99
	mov	w1, 4225
	bls	.L208
.L209:
	mov	w1, 8322
.L208:
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
	bls	.L211
	mov	w1, 8322
	cmp	w0, w1
	bne	.L212
.L211:
	adrp	x1, .LANCHOR0+128
	ldr	x1, [x1,#:lo12:.LANCHOR0+128]
	str	w0, [x1,4]
.L212:
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
	str	w3, [x1,48]
	mov	w3, 2
	str	wzr, [x1,32]
	str	w3, [x1,64]
	mov	w3, 3
	str	x0, [x1,24]
	str	w3, [x1,80]
	str	x0, [x1,40]
	str	x0, [x1,56]
	str	x0, [x1,72]
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
	str	w3, [x1,1856]
	cmp	w3, 2049
	bne	.L214
	mov	w3, 8
	str	w3, [x1,1868]
.L214:
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
	bne	.L217
.L220:
	and	w1, w1, -17
	b	.L218
.L217:
	cmp	w0, 24
	bne	.L219
	orr	w1, w1, 16
	b	.L218
.L219:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L220
.L218:
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
	.global	ftl_flash_resume
	.type	ftl_flash_resume, %function
ftl_flash_resume:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x1, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	add	x19, x1, 1620
	mov	x21, 0
	ldr	x0, [x1,128]
	ldr	w2, [x1,136]
	ldr	w22, [x1,152]
	str	w2, [x0]
	ldr	w2, [x1,140]
	ldr	x0, [x1,128]
	str	w2, [x0,4]
	ldr	w0, [x1,144]
	ldr	x2, [x1,128]
	str	w0, [x2,8]
	ldr	w0, [x1,148]
	str	w0, [x2,12]
	str	w22, [x2,304]
	ldr	w0, [x1,156]
	str	w0, [x2,308]
	ldr	w0, [x1,160]
	str	w0, [x2,336]
	ldr	w0, [x1,164]
	str	w0, [x2,344]
.L227:
	lsl	x0, x21, 3
	ldrb	w1, [x0,x19]
	sub	w1, w1, #1
	uxtb	w1, w1
	cmp	w1, 253
	bhi	.L226
	mov	w0, w21
	bl	FlashReset
.L226:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L227
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,1860]
	cbz	w0, .L228
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,1844]
	bl	NandcSetMode
	lsr	w0, w22, 8
	bl	NandcSetDdrPara
.L228:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,744]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
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
	tbnz	x0, 9, .L236
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L235
.L236:
	ldr	w0, [x19,368]
	orr	w0, w0, 2
	str	w0, [x19,368]
	ldr	w0, [x19,364]
	and	w0, w0, -3
	str	w0, [x19,364]
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
.L270:
	cmp	w20, w23
	bcs	.L271
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
	b	.L270
.L271:
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
	bls	.L272
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
.L272:
	ldr	w0, [x19,1940]
	cbnz	w0, .L252
.L254:
	mov	w0, 0
	b	.L240
.L252:
	ldrb	w0, [x19,1620]
	cmp	w0, 69
	bne	.L254
	mov	x0, 0
	mov	x1, 56
.L253:
	cmp	w23, w0
	bls	.L254
	mul	x2, x0, x1
	add	x0, x0, 1
	str	wzr, [x24,x2]
	b	.L253
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
.L276:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L276
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
	add	x2, x1, 24
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
	bls	.L280
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w7, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w7, .L294
	cbz	x22, .L281
.L294:
	cbnz	w25, .L283
.L291:
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
	cbnz	x22, .L285
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1880]
	b	.L285
.L283:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w4, 128
	mov	w2, 0
	lsr	w8, w23, 1
	mov	w3, w2
	mov	w9, -1
	ldr	w0, [x1,1932]
	cmp	w0, 25
	mov	w0, 64
	csel	w4, w0, w4, cc
.L287:
	cmp	w3, w8
	bcs	.L291
	lsr	w0, w2, 2
	ubfiz	x0, x0, 2, 30
	cbz	w7, .L288
	ldrh	w6, [x5,2]
	ldr	x10, [x1,1888]
	ldrh	w11, [x5],4
	orr	w6, w11, w6, lsl 16
	str	w6, [x10,x0]
	b	.L289
.L288:
	ldr	x6, [x1,1888]
	str	w9, [x6,x0]
.L289:
	add	w3, w3, 1
	add	w2, w2, w4
	b	.L287
.L285:
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x23, x20, 22, 5
	mov	w2, w25
	and	x22, x22, 3
	ldr	x1, [x19,1888]
	str	x1, [x19,1904]
	lsl	w1, w23, 10
	str	x0, [x19,1896]
	bl	rknand_dma_map_single
	str	w0, [x19,1912]
	ldr	x0, [x19,1904]
	lsl	w1, w23, 7
	mov	w2, w25
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
	cbnz	x22, .L292
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L292:
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
.L281:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L280:
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
.L299:
	cmp	w2, w0
	bhi	.L301
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L299
.L301:
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
	ldrh	w2, [x1,1948]
	ldrh	w3, [x1,1958]
	str	w0, [x1,1944]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,1956]
	ldr	w0, [x1,1964]
	str	w2, [x1,1952]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,1960]
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
	adrp	x20, .LANCHOR0
	ldrh	w6, [x0,8]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x23,10]
	ldrh	w1, [x23,12]
	ldrh	w4, [x23,14]
	strh	w6, [x0,1968]
	strh	w2, [x0,1970]
	strh	w1, [x0,1972]
	strh	w4, [x0,1958]
.L306:
	add	x5, x0, 1976
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L306
	ldrh	w3, [x23,20]
	ldrb	w0, [x23,15]
	cmp	w3, w0
	bcs	.L307
	uxtb	w8, w1
	mov	w3, 0
	mul	w11, w1, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x20, :lo12:.LANCHOR0
.L308:
	cmp	w3, w1
	bcs	.L310
	uxtb	w0, w3
	mov	w5, w3
	mov	w7, 0
.L311:
	cmp	w7, w2
	bcs	.L333
	add	x9, x12, 1976
	add	w13, w5, w11
	add	w14, w0, w8
	add	w7, w7, 1
	strb	w0, [x9,w5,uxtw]
	add	w0, w0, w10
	strb	w14, [x9,x13]
	add	w5, w5, w1
	uxtb	w0, w0
	b	.L311
.L333:
	add	w3, w3, 1
	b	.L308
.L310:
	add	x0, x20, :lo12:.LANCHOR0
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x0,1970]
	strh	w4, [x0,1958]
.L307:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x0,2008]
	strh	wzr, [x0,2010]
	bne	.L312
	strh	w6, [x0,2008]
.L312:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w2, 4352
	ldrb	w26, [x0,8]
	strh	w2, [x0,2012]
	cbz	w26, .L313
	mov	w2, 384
	strh	w2, [x0,2012]
.L313:
	add	x21, x20, :lo12:.LANCHOR0
	ldrh	w19, [x21,1958]
	ldrh	w22, [x21,1970]
	mul	w22, w1, w22
	mul	w1, w19, w1
	uxth	w22, w22
	strh	w22, [x21,1948]
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
	cmp	w19, 1024
	ldrh	w0, [x23,26]
	strh	w2, [x21,2028]
	ubfx	x2, x2, 9, 7
	strh	w0, [x21,2032]
	lsl	w2, w2, 1
	mul	w0, w22, w19
	strh	w2, [x21,2030]
	str	w0, [x21,1964]
	bls	.L314
	and	w0, w19, 255
	strh	w0, [x21,2010]
.L314:
	add	x2, x20, :lo12:.LANCHOR0
	mul	w0, w24, w27
	ldrh	w1, [x2,2010]
	sub	w1, w19, w1
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
	bhi	.L330
	mov	w0, 4
.L330:
	strh	w0, [x2,2040]
	cbz	w26, .L317
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 640
	strh	w1, [x0,2012]
.L317:
	add	x1, x20, :lo12:.LANCHOR0
	lsl	w19, w19, 6
	cmp	w22, 1
	ldrh	w0, [x1,2012]
	asr	w0, w0, w3
	add	w3, w3, 9
	asr	w19, w19, w3
	strh	w19, [x1,2044]
	and	w19, w19, 65535
	add	w0, w0, 2
	strh	w0, [x1,2042]
	mul	w0, w19, w22
	str	w0, [x1,2048]
	add	w19, w19, 8
	ldrh	w0, [x1,2040]
	udiv	w0, w0, w22
	add	w19, w19, w0
	bne	.L331
	add	w19, w19, 4
.L331:
	str	w19, [x1,1944]
	add	x19, x20, :lo12:.LANCHOR0
	ldrh	w0, [x19,1944]
	bl	FtlSysBlkNumInit
	str	wzr, [x19,2060]
	ldr	w0, [x19,1944]
	str	w0, [x19,2052]
	ldr	w0, [x19,1960]
	ldrh	w1, [x19,2018]
	lsl	w0, w0, 2
	ldrh	w3, [x19,2024]
	ldrh	w2, [x19,2040]
	mul	w1, w0, w1
	ldrh	w0, [x19,2026]
	ldrb	w5, [x19,120]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	strh	w1, [x19,2058]
	uxth	w0, w0
	strh	w0, [x19,2056]
	add	w1, w2, 3
	strh	w1, [x19,2040]
	ldr	w1, [x19,2048]
	add	w4, w1, 3
	str	w4, [x19,2048]
	cbz	w5, .L320
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x19,2040]
	b	.L332
.L320:
	cmp	w4, 7
	bhi	.L321
	mov	w1, 8
.L332:
	str	w1, [x19,2048]
.L321:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,1956]
	strh	wzr, [x20,2064]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L322
	mov	w0, 1
	strh	w0, [x20,2064]
.L322:
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
	cbz	w2, .L335
	ldrh	w2, [x1,2040]
	ldr	x3, [x1,2072]
	mov	x1, 0
.L336:
	cmp	w2, w1, uxth
	bls	.L341
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L336
	mov	w0, 1
	b	.L335
.L341:
	mov	w0, 0
.L335:
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
	cbz	w3, .L343
	ldr	x5, [x2,2088]
	mov	x0, 56
	mov	x2, 0
.L344:
	cmp	w3, w2
	bls	.L353
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L344
	cmp	w4, w1
	bhi	.L344
	mov	w0, 1
	b	.L343
.L353:
	mov	w0, 0
.L343:
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
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	adrp	x0, .LANCHOR0+2060
	ldr	w0, [x0,#:lo12:.LANCHOR0+2060]
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
	ldrh	w21, [x0,1970]
	mul	w21, w1, w21
.L361:
	cmp	w20, w21
	bge	.L367
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L362
	add	w19, w19, 1
	uxth	w19, w19
.L362:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L361
.L367:
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
	ldrh	w4, [x3,1972]
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
	ldrh	w1, [x2,1972]
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
	ldrh	w1, [x2,1972]
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
	bls	.L372
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L373
.L372:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L373:
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
	bcc	.L376
	adrp	x2, .LANCHOR0
	add	x0, x2, :lo12:.LANCHOR0
	mov	x19, x2
	ldrh	w0, [x0,2206]
	cmp	w0, 1024
	beq	.L376
	cbz	w1, .L378
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w0, [x20,-96]
	cbnz	w0, .L378
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
.L378:
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
.L376:
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
	cbz	w2, .L385
	mov	w5, 0
	ldrh	w3, [x1,2]
	ldrh	w2, [x1,4]
	mov	w4, w5
	and	w6, w6, 31
.L387:
	cmp	w4, w6
	bge	.L395
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
	b	.L387
.L395:
	cbz	w5, .L385
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0,2202]
	strh	w2, [x0,2204]
.L385:
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
.L397:
	ldrh	w1, [x22,6]
	cbz	w1, .L398
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
	cbnz	w1, .L399
	mov	w0, w20
	str	x1, [x29,88]
	bl	P2V_block_in_plane
	uxth	w26, w0
	ldr	x0, [x25,-88]
	lsl	w2, w20, 10
	ldr	x1, [x29,88]
	str	w2, [x0,4]
	ldrb	w0, [x21,120]
	cbz	w0, .L400
	ldr	x0, [x25,-88]
	mov	w2, 1
	bl	FlashEraseBlocks
.L400:
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
	b	.L399
.L398:
	adrp	x0, .LC2
	add	x0, x0, :lo12:.LC2
	bl	printk
.L401:
	b	.L401
.L399:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L402
	ldrh	w2, [x24,2206]
	mov	x0, x23
	mov	w1, w20
	bl	printk
	b	.L397
.L402:
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
.L408:
	cmp	w1, w0
	beq	.L409
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L410
	umull	x2, w0, w4
	add	x2, x3, x2
	b	.L408
.L409:
	mov	w0, 1
	b	.L407
.L410:
	mov	w0, 0
.L407:
	ret
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x9, .LANCHOR0
	uxth	w0, w0
	add	x9, x9, :lo12:.LANCHOR0
	ldrh	w1, [x9,1956]
	cmp	w1, w0
	bls	.L413
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
	cbnz	x1, .L414
	str	x4, [x5,-8]
	b	.L413
.L414:
	ubfiz	x8, x0, 1, 16
	ldr	x13, [x2,#:lo12:.LANCHOR2]
	ldrh	w2, [x4,4]
	mov	x14, -6148914691236517206
	ldr	x12, [x5,-16]
	mov	w18, 65535
	ldrh	w6, [x13,x8]
	cmp	w2, wzr
	ldrh	w17, [x9,1956]
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
.L422:
	add	w5, w5, 1
	cmp	w0, w2
	uxth	w5, w5
	beq	.L413
	cmp	w5, w17
	bhi	.L413
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w7, [x13,x15]
	mul	w7, w7, w16
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L418
	ldrh	w15, [x14,x15]
	ldrh	w7, [x9]
	cmp	w15, w7
	bcc	.L420
	b	.L419
.L418:
	bhi	.L419
.L420:
	ldrh	w7, [x1]
	cmp	w7, w18
	bne	.L421
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x4, [x2,8]
	b	.L413
.L421:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x12, x1
	b	.L422
.L419:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,-8]
	cmp	x1, x3
	bne	.L423
	strh	w0, [x1,2]
	str	x4, [x2,-8]
	b	.L413
.L423:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-16]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L413:
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
	beq	.L428
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
	cbnz	x1, .L429
	str	x4, [x5,24]
	b	.L428
.L429:
	ldr	x11, [x5,-80]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,-16]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L432:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L430
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L431
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L428
.L431:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L432
.L430:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,24]
	cmp	x1, x3
	bne	.L433
	strh	w0, [x1,2]
	str	x4, [x2,24]
	b	.L428
.L433:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-16]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L428:
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
	bne	.L436
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L437
	str	xzr, [x0]
	b	.L438
.L437:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L438
.L436:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L439
	cmp	w3, w0
	beq	.L438
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L438
.L439:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,-16]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L438:
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
	cbz	x2, .L441
	adrp	x3, .LANCHOR2-16
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2-16]
.L442:
	cbnz	w1, .L443
.L445:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L441
.L443:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L445
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L442
.L441:
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
	cbz	x1, .L453
	ldr	x4, [x2,-16]
	mov	w5, 6
.L449:
	cbz	w3, .L450
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L453
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L449
.L450:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L453:
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
	beq	.L455
	ldrh	w1, [x0,88]
	cmp	w1, w20
	beq	.L455
	ldrh	w1, [x0,136]
	cmp	w1, w20
	beq	.L455
	mov	w3, 6
	ldr	x4, [x0,-16]
	ldr	x0, [x0,-8]
	umull	x3, w20, w3
	add	x1, x4, x3
	cmp	x1, x0
	beq	.L455
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
	bne	.L457
	ldrh	w0, [x4,x3]
	cmp	w0, w1
	beq	.L455
.L457:
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
	bcs	.L455
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, w20
	sub	x0, x19, #8
	bl	List_remove_node
	ldrh	w0, [x19,16]
	sub	w0, w0, #1
	strh	w0, [x19,16]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L455:
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
.L462:
	cmp	w20, w2
	beq	.L466
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L463
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	uxth	w1, w0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L464
	adrp	x2, .LANCHOR0+2206
	adrp	x0, .LC4
	add	x0, x0, :lo12:.LC4
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2206]
	bl	printk
.L465:
	b	.L465
.L464:
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L466
.L463:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L462
.L466:
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
	ldr	x1, [x1,184]
.L469:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L485
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L469
	b	.L470
.L485:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L472:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L486
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L481
	cmp	w5, w7
	bcc	.L473
.L481:
	mov	w5, w7
	mov	w6, w0
.L473:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L472
.L486:
	cmp	w0, w2
	bcc	.L470
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,192]
	mov	w0, w2
.L476:
	cmp	w3, w2
	beq	.L470
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L477
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L477:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L476
.L470:
	ret
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3,194]
	cbnz	w0, .L488
	cmp	w2, 4
	bhi	.L488
	add	w2, w2, 1
	strh	w2, [x3,194]
	b	.L487
.L488:
	add	x0, x1, :lo12:.LANCHOR2
	ldr	x6, [x1,#:lo12:.LANCHOR2]
	mov	w7, 65535
	strh	wzr, [x0,194]
	str	wzr, [x0,196]
	adrp	x0, .LANCHOR0+1956
	ldrh	w5, [x0,#:lo12:.LANCHOR0+1956]
	mov	x0, 0
.L489:
	cmp	w5, w0, uxth
	bls	.L487
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L490
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,196]
	add	w2, w4, w2
	str	w2, [x3,196]
.L490:
	add	x0, x0, 1
	b	.L489
.L487:
	ret
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	stp	x29, x30, [sp, -16]!
	uxth	w0, w0
	add	x29, sp, 0
	cbz	w1, .L494
	bl	ftl_set_blk_mode.part.9
	b	.L493
.L494:
	adrp	x1, .LANCHOR0
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L493:
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
	ldrh	w4, [x4,1948]
.L498:
	cmp	w2, w4
	bcs	.L504
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L499
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L499:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L498
.L504:
	add	x3, x3, :lo12:.LANCHOR0
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,2018]
	mov	x3, x0
.L501:
	cmp	x3, x4
	beq	.L505
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L502
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L502:
	add	x3, x3, 2
	b	.L501
.L505:
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
	ldrh	w24, [x0,1948]
.L507:
	cmp	w24, w21, uxth
	bls	.L518
	add	x0, x23, 1976
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L508
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L508:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L507
.L518:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w2, [x1,2018]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldr	w0, [x1,1940]
	cbz	w0, .L510
	adrp	x0, .LANCHOR2-80
	ldrh	w1, [x19]
	ldr	x0, [x0,#:lo12:.LANCHOR2-80]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 79
	bhi	.L510
	mov	w0, 1
	strb	w0, [x19,9]
.L510:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,8]
	cbz	w0, .L511
	mov	w0, 1
	strb	w0, [x19,9]
.L511:
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
	ldrh	w23, [x1,1948]
	mov	w19, w22
	ldrh	w24, [x1,2018]
	mov	x20, x1
.L520:
	cmp	w23, w22, uxth
	bls	.L527
	add	x0, x20, 1976
	mov	w1, w21
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L521
	add	w19, w19, w24
	uxth	w19, w19
.L521:
	add	x22, x22, 1
	b	.L520
.L527:
	cbz	w19, .L523
	mov	w0, 32768
	sdiv	w19, w0, w19
.L523:
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
	cbz	x1, .L529
	ldr	x0, [x2,-16]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,-80]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L529:
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
	cbz	x1, .L532
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
.L534:
	cmp	w3, w4
	beq	.L537
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L535
.L537:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,-80]
	ldrh	w0, [x0,x1]
	b	.L532
.L535:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L534
.L532:
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
	ldr	w2, [x24,2036]
	mov	x0, x20
	add	x1, x1, :lo12:.LC7
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+504
	ldr	w0, [x0,#:lo12:.LANCHOR1+504]
	cmp	w0, 1
	beq	.L540
	sub	w0, w20, w22
	b	.L541
.L540:
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
	ldr	w2, [x24,2060]
	adrp	x1, .LC32
	mov	x0, x21
	add	x1, x1, :lo12:.LC32
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,2052]
	adrp	x1, .LC33
	mov	x0, x21
	add	x1, x1, :lo12:.LC33
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x24,1944]
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
	ldrh	w2, [x24,1956]
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
	ldr	w2, [x24,1960]
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
	ldr	w2, [x24,1940]
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
	beq	.L542
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x25,#:lo12:.LANCHOR2]
	adrp	x1, .LC72
	mov	x0, x19
	add	x1, x1, :lo12:.LC72
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L542:
	mov	w0, 0
	adrp	x23, .LC73
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x23, x23, :lo12:.LC73
.L544:
	cmp	w3, w27
	beq	.L543
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
	bne	.L544
.L543:
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
.L546:
	cmp	w3, w27
	beq	.L545
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
	bne	.L546
.L545:
	sub	w0, w19, w22
.L541:
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
	bcs	.L551
	adrp	x1, .LANCHOR0
	str	wzr, [x0,240]
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x5, [x0,-80]
	mov	x0, 0
	ldrh	w3, [x2,1956]
.L552:
	cmp	w3, w0
	bls	.L584
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x0,lsl 1]
	add	x0, x0, 1
	ldr	w2, [x4,240]
	add	w2, w6, w2
	str	w2, [x4,240]
	b	.L552
.L584:
	add	x2, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x2,240]
	ldrh	w1, [x1,2008]
	udiv	w3, w0, w3
	str	w3, [x2,248]
	ldr	w3, [x2,244]
	sub	w0, w0, w3
	udiv	w0, w0, w1
	str	w0, [x2,240]
	b	.L554
.L551:
	ldr	w1, [x0,252]
	cmp	w2, w1
	bls	.L554
	add	w1, w1, 1
	adrp	x4, .LANCHOR0
	str	w1, [x0,252]
	mov	w0, 0
.L556:
	add	x1, x4, :lo12:.LANCHOR0
	ldrh	w1, [x1,1956]
	cmp	w0, w1
	bcs	.L554
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 1, 32
	add	w0, w0, 1
	ldr	x3, [x1,-80]
	ldrh	w1, [x3,x2]
	add	w1, w1, 1
	strh	w1, [x3,x2]
	b	.L556
.L554:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w21, [x1,256]
	ldr	w24, [x1,248]
	add	w0, w21, 256
	cmp	w0, w24
	bls	.L559
	ldr	w1, [x1,252]
	add	w0, w21, 768
	cmp	w0, w1
	bls	.L559
	adrp	x0, .LANCHOR0+1940
	ldr	w0, [x0,#:lo12:.LANCHOR0+1940]
	cbz	w0, .L562
	cmp	w21, 40
	bls	.L559
.L562:
	mov	w0, 65535
	b	.L561
.L559:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,32]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L571
	cmp	w21, 40
	bhi	.L562
.L571:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x3, [x0,-8]
	cbz	x3, .L562
	adrp	x1, .LANCHOR0+1956
	mov	w23, 65535
	ldr	x7, [x0,-16]
	mov	w20, w23
	ldr	x26, [x0,-80]
	mov	x0, -6148914691236517206
	ldrh	w9, [x1,#:lo12:.LANCHOR0+1956]
	mov	w8, w23
	mov	w1, 0
	add	x0, x0, 1
	mov	w10, 6
.L564:
	ldrh	w2, [x3]
	cmp	w2, w8
	beq	.L566
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L562
	ldrh	w4, [x3,4]
	cbz	w4, .L565
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w5, w4
	and	x4, x4, 65535
	ldrh	w3, [x26,x4,lsl 1]
	cmp	w3, w21
	bls	.L570
	cmp	w3, w23
	bcs	.L565
	mov	w23, w3
	mov	w20, w5
.L565:
	umull	x3, w2, w10
	add	x3, x7, x3
	b	.L564
.L570:
	mov	w20, w5
.L566:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L562
	ubfiz	x25, x20, 1, 16
	ldrh	w22, [x26,x25]
	cmp	w22, w21
	bls	.L568
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L568
	add	x0, x19, :lo12:.LANCHOR2
	str	w23, [x0,256]
.L568:
	cmp	w22, w24
	bcs	.L562
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L562
	add	w0, w22, 256
	cmp	w0, w24
	bcc	.L569
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,252]
	cmp	w22, w0
	bcs	.L562
.L569:
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
.L561:
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
	beq	.L586
	adrp	x2, .LANCHOR2
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L586:
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
.L588:
	add	x5, x10, :lo12:.LANCHOR0
	adrp	x9, .LANCHOR0
	ldrh	w0, [x5,1948]
	cmp	w1, w0
	bcs	.L592
	add	x2, x3, :lo12:.LANCHOR2
	umull	x4, w1, w11
	ldr	x7, [x2,872]
	ldr	x8, [x2,880]
	add	x0, x7, x4
	str	w12, [x0,16]
	ldrh	w0, [x5,2028]
	mul	w0, w1, w0
	sdiv	w0, w0, w6
	add	x0, x8, x0, sxtw 2
	str	x0, [x7,x4]
	ldrh	w0, [x5,2030]
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
	b	.L588
.L592:
	mov	w8, 24
	mov	w5, 4
.L590:
	add	x2, x3, :lo12:.LANCHOR2
	ldr	w1, [x2,904]
	cmp	w0, w1
	bcs	.L593
	umull	x4, w0, w8
	ldr	x7, [x2,872]
	add	x6, x9, :lo12:.LANCHOR0
	ldr	x10, [x2,880]
	add	x1, x7, x4
	str	wzr, [x1,16]
	ldrh	w1, [x6,2028]
	mul	w1, w0, w1
	sdiv	w1, w1, w5
	add	x1, x10, x1, sxtw 2
	str	x1, [x7,x4]
	ldrh	w1, [x6,2030]
	ldr	x6, [x2,872]
	ldr	x2, [x2,888]
	mul	w1, w0, w1
	add	x4, x6, x4
	add	w0, w0, 1
	sdiv	w1, w1, w5
	uxth	w0, w0
	add	x1, x2, x1, sxtw 2
	str	x1, [x4,8]
	b	.L590
.L593:
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
.L595:
	cmp	w3, w1
	bcs	.L594
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L600:
	cmp	w2, w8
	bcs	.L597
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L596
	str	wzr, [x10,16]
	b	.L597
.L596:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L600
.L597:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L595
.L594:
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
.L602:
	cmp	w2, w1
	bcs	.L608
	mov	w3, 0
.L606:
	cmp	w3, w5
	bcs	.L604
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L603
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L604
.L603:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L606
.L604:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L602
.L608:
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
.L610:
	cmp	w2, w1, uxth
	bls	.L614
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L610
	mov	w0, 1
	b	.L611
.L614:
	mov	w0, 0
.L611:
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
.L616:
	uxth	w4, w5
	cmp	w4, w7
	bcs	.L620
	add	x5, x5, 1
	add	x9, x6, x5, lsl 1
	ldrh	w9, [x9,-2]
	cmp	w9, w8
	bne	.L616
.L620:
	cmp	w4, w7
	bne	.L618
	ubfiz	x4, x4, 1, 16
	strh	w0, [x6,x4]
	add	x0, x3, :lo12:.LANCHOR2
	ldrh	w4, [x0,908]
	add	w4, w4, 1
	strh	w4, [x0,908]
.L618:
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
	str	x19, [sp,16]
	uxth	w19, w0
	adrp	x0, .LC78
	mov	w1, w19
	add	x0, x0, :lo12:.LC78
	bl	printk
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w5, [x2,936]
	cmp	w5, w19
	beq	.L623
	ldrh	w4, [x2,938]
	cmp	w4, w19
	beq	.L623
	ldrh	w3, [x2,940]
	cmp	w3, w19
	beq	.L623
	ldrh	w1, [x2,942]
	cmp	w1, w19
	beq	.L623
	mov	w0, 65535
	cmp	w5, w0
	bne	.L624
	strh	w19, [x2,936]
	b	.L623
.L624:
	cmp	w4, w0
	bne	.L625
	strh	w19, [x2,938]
	b	.L623
.L625:
	cmp	w3, w0
	bne	.L626
	strh	w19, [x2,940]
	b	.L623
.L626:
	cmp	w1, w0
	bne	.L623
	strh	w19, [x2,942]
.L623:
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
	adrp	x0, .LC78
	mov	w1, w19
	add	x0, x0, :lo12:.LC78
	bl	printk
	adrp	x2, .LANCHOR2
	mov	w0, 0
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w6, [x2,936]
	cmp	w6, w19
	beq	.L628
	ldrh	w5, [x2,938]
	cmp	w5, w19
	beq	.L628
	ldrh	w4, [x2,940]
	cmp	w4, w19
	beq	.L628
	ldrh	w3, [x2,942]
	cmp	w3, w19
	beq	.L628
	mov	w1, 65535
	cmp	w6, w1
	bne	.L629
	strh	w19, [x2,936]
	b	.L628
.L629:
	cmp	w5, w1
	bne	.L630
	strh	w19, [x2,938]
	b	.L637
.L630:
	cmp	w4, w1
	bne	.L631
	strh	w19, [x2,940]
	b	.L637
.L631:
	cmp	w3, w1
	mov	w0, -1
	bne	.L628
	strh	w19, [x2,942]
.L637:
	mov	w0, 0
.L628:
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
	ldrh	w1, [x22,944]
	adrp	x0, .LC79
	mov	w2, w20
	add	x0, x0, :lo12:.LC79
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	adrp	x0, .LANCHOR0+1940
	ldr	w0, [x0,#:lo12:.LANCHOR0+1940]
	cbz	w0, .L639
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,-80]
	ldrh	w1, [x2,x0]
	cmp	w1, 39
	bls	.L639
	sub	w1, w1, #40
	strh	w1, [x2,x0]
.L639:
	add	x2, x19, :lo12:.LANCHOR2
	mov	x0, 0
	add	x2, x2, 952
	ldrh	w1, [x2,-8]
.L640:
	cmp	w1, w0, uxth
	bls	.L646
	add	x0, x0, 1
	add	x3, x2, x0, lsl 1
	ldrh	w3, [x3,-2]
	cmp	w3, w20
	bne	.L640
	b	.L641
.L646:
	cmp	w1, 15
	bhi	.L641
	add	x19, x19, :lo12:.LANCHOR2
	add	w0, w1, 1
	add	x19, x19, 952
	strh	w0, [x19,-8]
	strh	w20, [x19,w1,sxtw 1]
.L641:
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
	cbz	w1, .L648
	ldrh	w3, [x0,936]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L648
	ldrh	w2, [x0,986]
	cmp	w2, w1
	bcc	.L649
	strh	wzr, [x0,986]
.L649:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x19, 952
	ldrh	w1, [x19,986]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,986]
	add	w0, w0, 1
	strh	w0, [x19,986]
.L648:
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
	cbnz	w0, .L655
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
.L656:
	add	x2, x1, 196
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L656
.L662:
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
	b	.L657
.L655:
	cmp	w0, 1
	bne	.L658
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x3, :lo12:.LANCHOR0
.L661:
	cmp	x0, 3
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
	b	.L662
.L658:
	cmp	w0, 2
	bne	.L663
	adrp	x3, .LANCHOR0
	mov	w1, 65535
	mov	x0, 0
	add	x3, x3, :lo12:.LANCHOR0
.L665:
	add	x4, x3, 196
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L665
	b	.L662
.L663:
	cmp	w0, 3
	bne	.L666
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x3, :lo12:.LANCHOR0
.L669:
	cmp	x0, 5
	uxth	w1, w0
	mov	w2, w1
	bls	.L667
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L667:
	add	x1, x3, 196
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L669
	b	.L662
.L666:
	cmp	w0, 4
	bne	.L670
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
.L672:
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
	bne	.L672
	b	.L662
.L670:
	cmp	w0, 5
	bne	.L673
	adrp	x2, .LANCHOR0
	mov	x0, 0
	add	x2, x2, :lo12:.LANCHOR0
.L674:
	add	x1, x2, 196
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L674
	mov	x0, 0
.L675:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L675
	b	.L662
.L673:
	cmp	w0, 6
	bne	.L662
	adrp	x1, .LANCHOR0
	mov	x2, 0
	mov	w4, 12
	mov	w5, 10
	add	x1, x1, :lo12:.LANCHOR0
.L678:
	cmp	x2, 5
	uxth	w3, w2
	mov	w0, w3
	bls	.L676
	add	w0, w3, w3, lsl 1
	and	w3, w3, 1
	cmp	w3, wzr
	uxth	w0, w0
	csel	w3, w4, w5, ne
	sub	w0, w0, w3
	uxth	w0, w0
.L676:
	add	x3, x1, 196
	strh	w0, [x3,x2,lsl 1]
	add	x2, x2, 1
	cmp	x2, 256
	bne	.L678
	b	.L662
.L657:
	cmp	w20, w0, uxth
	bls	.L696
	add	x2, x1, 196
	ldrh	w2, [x2,x0,lsl 1]
	add	x0, x0, 1
	strh	w2, [x19,w2,sxtw 1]
	b	.L657
.L696:
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
	bls	.L698
	mov	w1, 512
	b	.L714
.L698:
	cmp	w1, 128
	bls	.L714
	mov	w1, 256
.L714:
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
.L702:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x26, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L701
	ldrb	w1, [x19,1845]
	str	w0, [x25,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x19, x1, sxtw
	strb	w0, [x19,1845]
	strb	w24, [x1,1848]
.L701:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L702
	add	x19, x21, :lo12:.LANCHOR0
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w1, [x19,1845]
	strb	w1, [x0,2012]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L703
.L707:
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
.L703:
	ldr	w26, [x19,12]
	mov	x24, 0
	add	x27, x19, 1620
	add	x25, x19, 708
.L706:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x27, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L704
	ldrb	w1, [x20,13]
	ldrb	w3, [x19,1845]
	mul	w0, w1, w26
	ldrh	w1, [x20,14]
	mov	x2, x3
	and	w1, w1, 65280
	mul	w1, w0, w1
	str	w1, [x25,w3,sxtw 2]
	ldrb	w0, [x20,23]
	cbz	w0, .L705
	lsl	w1, w1, 1
	str	w1, [x25,w3,sxtw 2]
.L705:
	add	x0, x19, x3
	add	w2, w2, 1
	strb	w2, [x19,1845]
	strb	w24, [x0,1848]
.L704:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L706
	b	.L707
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
	ldr	w0, [x1,12]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldrb	w0, [x1,1936]
	strb	w0, [x19,7]
	ldr	w0, [x1,2060]
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
.L716:
	cmp	w4, w0, uxtb
	bls	.L718
	add	x2, x1, 1848
	ldrb	w3, [x19,10]
	ldrb	w2, [x0,x2]
	add	x0, x0, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L716
.L718:
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
	ldrh	w2, [x3,2024]
	strh	w1, [x0,848]
	mov	w1, 128
	strh	w1, [x0,850]
	mov	w1, 1024
	strh	wzr, [x0,856]
	sdiv	w2, w1, w2
	ldrh	w1, [x3,1948]
	strh	wzr, [x0,944]
	lsl	w1, w1, 2
	strh	wzr, [x0,2040]
	strh	wzr, [x0,986]
	cmp	w2, w1
	str	w2, [x0,2044]
	bls	.L720
	str	w1, [x0,2044]
.L720:
	add	x20, x22, :lo12:.LANCHOR0
	add	x19, x21, :lo12:.LANCHOR2
	mov	w24, 56
	ldrh	w0, [x20,2022]
	str	wzr, [x20,2080]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,912]
	ldrh	w1, [x20,2022]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,1948]
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
	ldrh	w0, [x20,1948]
	ldrh	w23, [x20,2028]
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
	ldrh	w23, [x20,1948]
	str	x0, [x19,872]
	ldrh	w0, [x20,2030]
	mul	w23, w0, w23
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2120]
	lsl	w0, w23, 3
	bl	ftl_malloc
	str	x0, [x19,2128]
	ldrh	w1, [x20,2030]
	ldr	w0, [x19,904]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,888]
	ldrh	w1, [x20,2030]
	ldr	w0, [x19,2044]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2136]
	ldrh	w0, [x20,1958]
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
	ldrh	w23, [x20,1958]
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
	str	x0, [x21,#:lo12:.LANCHOR2]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2176]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2184]
	ldrh	w0, [x20,1958]
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
	str	x0, [x19,184]
	ldrh	w1, [x20,2058]
	ldrh	w0, [x20,2028]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2240]
	ldrh	w1, [x20,1958]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,-16]
	ldrh	w0, [x20,2014]
	ldrh	w1, [x20,1970]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,2248]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,2136]
	add	x1, x20, 2104
	ldrh	w4, [x19,2248]
	ldrh	w5, [x20,1970]
	mov	w0, 1
	mov	w2, w4
.L721:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L821
	ldr	x3, [x20,2136]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L721
.L821:
	add	x1, x22, :lo12:.LANCHOR0
.L723:
	cmp	w0, 8
	beq	.L822
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,2136]
	b	.L723
.L822:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,2176]
	cbnz	x1, .L725
.L727:
	adrp	x0, .LC80
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC80
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L726
.L725:
	ldr	x1, [x0,2184]
	cbz	x1, .L727
	ldr	x1, [x0,2216]
	cbz	x1, .L727
	ldr	x1, [x0,2232]
	cbz	x1, .L727
	ldr	x1, [x0,184]
	cbz	x1, .L727
	ldr	x1, [x0,2240]
	cbz	x1, .L727
	ldr	x0, [x0,-16]
	cbz	x0, .L727
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,2136]
	cbz	x0, .L727
	adrp	x0, .LANCHOR2
	ldr	x0, [x0,#:lo12:.LANCHOR2]
	cbz	x0, .L727
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,912]
	cbz	x1, .L727
	ldr	x1, [x0,928]
	cbz	x1, .L727
	ldr	x1, [x0,2048]
	cbz	x1, .L727
	ldr	x1, [x0,2064]
	cbz	x1, .L727
	ldr	x1, [x0,-88]
	cbz	x1, .L727
	ldr	x1, [x0,896]
	cbz	x1, .L727
	ldr	x1, [x0,2056]
	cbz	x1, .L727
	ldr	x1, [x0,2072]
	cbz	x1, .L727
	ldr	x1, [x0,2080]
	cbz	x1, .L727
	ldr	x0, [x0,2088]
	cbz	x0, .L727
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,880]
	cbz	x1, .L727
	ldr	x1, [x0,2104]
	cbz	x1, .L727
	ldr	x1, [x0,2112]
	cbz	x1, .L727
	ldr	x1, [x0,872]
	cbz	x1, .L727
	ldr	x1, [x0,2120]
	cbz	x1, .L727
	ldr	x1, [x0,2128]
	cbz	x1, .L727
	ldr	x1, [x0,888]
	cbz	x1, .L727
	ldr	x1, [x0,-80]
	cbz	x1, .L727
	ldr	x0, [x0,2152]
	cbz	x0, .L727
	adrp	x0, .LANCHOR0+2072
	ldr	x0, [x0,#:lo12:.LANCHOR0+2072]
	cbz	x0, .L727
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,2192]
	cbz	x1, .L727
	ldr	x1, [x0,2200]
	cbz	x1, .L727
	ldr	x0, [x0,2208]
	cbz	x0, .L727
	mov	w0, 0
.L726:
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
.L825:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L823
	ubfx	x4, x2, 5, 11
	add	x3, x3, 2
	lsl	x4, x4, 2
	lsl	w2, w5, w2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	bne	.L825
.L823:
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
.L831:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L853
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L832:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L854
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22,x3]
	cmp	w2, w4
	bne	.L833
	cbz	w4, .L833
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L833:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L832
.L854:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L831
.L853:
	adrp	x0, .LANCHOR0+2020
	mov	w23, 0
	mov	w20, w23
	ldrh	w1, [x0,#:lo12:.LANCHOR0+2020]
	ldrh	w0, [x19]
	strh	w1, [x21,x0,lsl 1]
	ldrh	w24, [x21]
.L836:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L855
	ubfiz	x2, x20, 1, 16
	ldrh	w1, [x21,x2]
	cmp	w24, w1
	bls	.L837
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbnz	w0, .L838
	b	.L839
.L837:
	cbnz	w1, .L839
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbz	w0, .L839
.L841:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L839:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L836
.L838:
	mov	w23, w20
	mov	w24, w1
	cbz	w1, .L841
	b	.L839
.L855:
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
.L857:
	add	x3, x20, :lo12:.LANCHOR0
	ldrh	w0, [x3,2058]
	cmp	w0, w1
	bls	.L859
	add	x4, x19, :lo12:.LANCHOR2
	ubfiz	x0, x1, 4, 16
	ldr	x2, [x4,184]
	add	x2, x2, x0
	str	wzr, [x2,4]
	ldr	x2, [x4,184]
	strh	w5, [x2,x0]
	ldr	x2, [x4,184]
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
	b	.L857
.L859:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, -1
	add	x1, x0, 2256
	strh	w2, [x0,2258]
	strh	w2, [x0,2256]
	ldr	w2, [x3,2048]
	strh	w2, [x0,2266]
	mov	w2, -3902
	strh	w2, [x0,2260]
	ldrh	w2, [x0,2320]
	strh	w2, [x0,2264]
	ldrh	w2, [x3,2056]
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
	ldrh	w2, [x20,2040]
	str	w0, [x19,2348]
	ldr	x0, [x20,2072]
	lsl	w2, w2, 1
	str	xzr, [x19,2328]
	str	wzr, [x19,2340]
	str	wzr, [x19,2344]
	str	wzr, [x20,1940]
	strh	wzr, [x20,2066]
	bl	ftl_memset
	ldrh	w2, [x20,1958]
	mov	w1, 0
	ldr	x0, [x19,-80]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,1958]
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
	ldrh	w2, [x0,1958]
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
.L862:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,1956]
	cmp	w19, w1
	bge	.L869
	mov	w20, 0
	ldrh	w3, [x0,1948]
	ldrh	w2, [x0,2018]
	mov	w28, w20
	mov	x27, x0
.L870:
	cmp	w28, w3
	bge	.L871
	add	x0, x27, 1976
	mov	w1, w19
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L863
	add	w20, w2, w20
	sxth	w20, w20
.L863:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L870
.L871:
	cbz	w20, .L865
	sdiv	w20, w26, w20
	sxth	w20, w20
	b	.L866
.L865:
	ldr	x0, [x21,#:lo12:.LANCHOR2]
	mov	w1, -1
	strh	w1, [x0,w19,sxtw 1]
.L866:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,-16]
	smaddl	x1, w19, w24, x1
	strh	w20, [x1,4]
	ldrh	w1, [x0,40]
	cmp	w19, w1
	beq	.L867
	ldrh	w1, [x0,88]
	cmp	w19, w1
	beq	.L867
	ldrh	w0, [x0,136]
	cmp	w19, w0
	beq	.L867
	ldr	x0, [x21,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L868
	add	w4, w25, 1
	mov	w0, w19
	uxth	w25, w4
	bl	INSERT_FREE_LIST
	b	.L867
.L868:
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_DATA_LIST
.L867:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L862
.L869:
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
	ldrh	w2, [x20,2022]
	ldr	x0, [x19,912]
	lsl	w2, w2, 1
	strh	wzr, [x19,908]
	strh	wzr, [x19,920]
	bl	ftl_memset
	ldrh	w3, [x20,2022]
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
	adrp	x20, .LANCHOR1
	adrp	x22, .LANCHOR0
	add	x21, x20, :lo12:.LANCHOR1
	add	x22, x22, :lo12:.LANCHOR0
	str	x23, [sp,48]
	add	x21, x21, 512
	mov	w19, 0
	add	x22, x22, 1620
.L879:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x22
	bl	FlashMemCmp8
	mov	w23, w0
	cbnz	w0, .L876
	add	x0, x20, :lo12:.LANCHOR1
	ubfiz	x19, x19, 5, 32
	add	x1, x0, 512
	adds	x19, x1, x19
	beq	.L882
	ldrb	w3, [x19,22]
	mov	x2, 0
	mov	x1, x0
	b	.L881
.L876:
	add	w19, w19, 1
	add	x21, x21, 32
	cmp	w19, 77
	bne	.L879
	b	.L882
.L881:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,2976]
	cmp	w4, w3
	beq	.L880
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L881
	mov	w0, w2
.L880:
	add	x20, x20, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x20, 2976
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x2, x1
	add	x0, x0, 88
	mov	w2, 32
	bl	ftl_memcpy
	add	x0, x20, 472
	mov	x1, x19
	mov	w2, 32
	bl	ftl_memcpy
	b	.L877
.L882:
	mov	w23, -1
.L877:
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
	bne	.L887
	cbz	x3, .L888
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L888:
	cbz	x20, .L886
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
	b	.L886
.L887:
	cbz	x3, .L891
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L891:
	cbz	x20, .L886
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
.L886:
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
.L903:
	cmp	w2, w3
	bls	.L905
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L903
.L905:
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
	.global	js_hash
	.type	js_hash, %function
js_hash:
	mov	x5, x0
	mov	w0, 42982
	mov	x2, 0
	movk	w0, 0x47c6, lsl 16
.L908:
	cmp	w1, w2
	bls	.L910
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L908
.L910:
	ret
	.size	js_hash, .-js_hash
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
	cbnz	w2, .L913
	add	x1, x4, x1
	b	.L915
.L913:
	add	x0, x4, x1
	mov	x1, x6
.L915:
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
	adrp	x0, .LANCHOR0+2060
	ldr	w0, [x0,#:lo12:.LANCHOR0+2060]
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
.L924:
	cmp	x19, x26
	beq	.L932
	cbnz	w20, .L925
	mov	x0, x23
	mov	x1, x27
	mov	x2, x22
	mov	w3, w19
	bl	printk
.L925:
	cmp	w25, 4
	mov	x0, x21
	bne	.L926
	ldr	w1, [x22,x19,lsl 2]
	b	.L931
.L926:
	cmp	w25, 2
	bne	.L928
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L931
.L928:
	ldrb	w1, [x22,x19]
.L931:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L929
	adrp	x1, .LC83
	mov	x0, x24
	add	x1, x1, :lo12:.LC83
	mov	w20, 0
	bl	printk
.L929:
	add	x19, x19, 1
	b	.L924
.L932:
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
	bne	.L934
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+3121
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+3121]
	b	.L989
.L934:
	cmp	w26, 3
	bne	.L936
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
	b	.L1022
.L936:
	cmp	w26, 4
	bne	.L937
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
.L1022:
	mov	w27, 8
	strb	w0, [x1,11]
	mov	w28, w27
	b	.L935
.L937:
	cmp	w26, 5
	bne	.L938
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w27, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L1021
.L938:
	cmp	w26, 6
	bne	.L939
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w27, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L1021
.L939:
	cmp	w26, 7
	bne	.L989
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
	b	.L935
.L989:
	mov	w27, 7
.L1021:
	mov	w28, 4
.L935:
	sub	w0, w26, #1
	cmp	w0, 1
	bhi	.L1018
	adrp	x22, .LANCHOR1
	add	x25, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	add	x26, x25, 760
	add	x4, x25, 24
	add	x22, x22, 3104
.L940:
	ldrb	w0, [x25,1845]
	cmp	w0, w21
	bls	.L947
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
.L942:
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
	bhi	.L942
	mov	x0, 0
.L943:
	add	w2, w0, 8
	mov	x1, 0
.L944:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x22, x3
	cmp	x1, 24
	ldrb	w6, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w6, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L944
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L943
	add	w21, w21, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w21, w21
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L940
.L1018:
	sub	w0, w26, #3
	cmp	w0, 4
	bhi	.L947
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
.L948:
	ldrb	w0, [x21,1845]
	cmp	w0, w20
	bhi	.L988
.L947:
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
.L988:
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
	beq	.L949
	add	x1, x6, x1, lsl 6
	add	x6, x1, 20
	b	.L950
.L949:
	mov	x0, 160
	madd	x1, x1, x0, x6
	add	x6, x1, 28
.L950:
	mov	w0, 54
	cmp	w26, 4
	str	w0, [x23,2056]
	bne	.L951
	mov	w0, 255
	str	w0, [x23,2052]
	mov	w0, 64
	str	w0, [x23,2048]
	mov	w0, 204
	b	.L1023
.L951:
	cmp	w25, 1
	bhi	.L953
	ldrb	w0, [x21,764]
	str	w0, [x23,2052]
	mov	w0, 82
	b	.L1024
.L953:
	mov	w0, 174
	str	w0, [x23,2052]
	str	wzr, [x23,2048]
	mov	w0, 176
.L1023:
	str	w0, [x23,2052]
	mov	w0, 77
.L1024:
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
	bne	.L954
	mov	w0, 31
	str	w0, [x23,2052]
	b	.L955
.L954:
	str	wzr, [x23,2052]
.L955:
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
	bls	.L957
	ldr	x1, [x29,104]
	cmp	w26, 7
	mov	w7, 32
	csel	w7, w7, w1, eq
.L957:
	adrp	x0, .LANCHOR4
	mov	x8, 0
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x1, [x1,56]
.L958:
	ldr	w9, [x23,2048]
	strb	w9, [x1,x8]
	add	x8, x8, 1
	cmp	w7, w8, uxtb
	bhi	.L958
	cmp	w26, 7
	bne	.L959
	mov	w7, 0
.L961:
	ldrb	w8, [x1]
	cmp	w8, 12
	beq	.L960
	ldrb	w8, [x1,1]
	cmp	w8, 10
	beq	.L960
	add	w7, w7, 1
	add	x1, x1, 4
	uxtb	w7, w7
	cmp	w7, 8
	bne	.L961
	b	.L962
.L960:
	cmp	w7, 7
	bne	.L963
.L962:
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L964:
	b	.L964
.L959:
	cmp	w26, 6
	bne	.L963
	mov	x7, 0
.L965:
	ldrb	w8, [x1,x7]
	cmp	w8, 12
	beq	.L963
	add	x8, x1, x7
	ldrb	w8, [x8,8]
	cmp	w8, 4
	beq	.L963
	add	x7, x7, 1
	cmp	x7, 8
	bne	.L965
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L967:
	b	.L967
.L963:
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x10, [x1,56]
	mov	x1, 0
.L968:
	ldr	w2, [x29,128]
	cmp	w2, w1
	ble	.L1025
	ldr	w7, [x23,2048]
	strb	w7, [x10,x1]
	add	x1, x1, 1
	b	.L968
.L1025:
	add	x1, x0, :lo12:.LANCHOR4
	mov	w9, w22
	mov	w8, 8
	ldr	x12, [x1,56]
.L971:
	mov	w1, 0
.L970:
	add	w7, w1, w9
	add	w1, w1, 1
	sbfiz	x7, x7, 2, 32
	cmp	w1, w22
	ldr	w11, [x12,x7]
	mvn	w11, w11
	str	w11, [x12,x7]
	bne	.L970
	ldr	w1, [x29,124]
	subs	w8, w8, #1
	add	w9, w9, w1
	bne	.L971
	mov	x8, 0
	mov	w16, 1
.L972:
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x15, [x1,56]
	mov	w1, 0
	mov	w11, w1
.L976:
	lsl	w9, w16, w11
	mov	w13, w8
	mov	w7, 16
	mov	w12, 0
.L974:
	ldr	w14, [x15,w13,sxtw 2]
	add	w13, w13, w22
	and	w14, w9, w14
	cmp	w14, w9
	csinc	w12, w12, w12, ne
	subs	w7, w7, #1
	bne	.L974
	cmp	w12, 9
	orr	w9, w1, w9
	add	w11, w11, 1
	csel	w1, w9, w1, cs
	cmp	w11, 32
	bne	.L976
	str	w1, [x15,x8,lsl 2]
	add	x8, x8, 1
	cmp	w22, w8
	bgt	.L972
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x1, [x0,56]
	mov	x0, 0
	mov	w8, w0
.L979:
	ldr	w9, [x1,x0]
	add	x0, x0, 4
	cmp	w9, wzr
	csinc	w8, w8, w8, ne
	cmp	x0, 32
	bne	.L979
	cmp	w8, 7
	ble	.L980
	adrp	x0, .LC85
	mov	w2, 1
	add	x0, x0, :lo12:.LC85
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC84
	mov	w1, 0
	add	x0, x0, :lo12:.LC84
	bl	printk
.L981:
	b	.L981
.L980:
	cmp	w26, 6
	mov	w1, 4
	beq	.L982
	cmp	w26, 7
	mov	w1, 10
	mov	w0, 8
	csel	w1, w0, w1, ne
.L982:
	mov	w8, 0
.L983:
	mov	x0, 0
.L984:
	add	w9, w7, w0
	ldrb	w11, [x10,x0]
	add	x0, x0, 1
	cmp	w28, w0, uxtb
	strb	w11, [x6,w9,sxtw]
	bhi	.L984
	add	w8, w8, 1
	ldr	x0, [x29,112]
	cmp	w8, w27
	add	w7, w7, w1
	add	x10, x10, x0
	blt	.L983
	mov	w0, 255
	str	w0, [x23,2056]
	mov	w0, w24
	bl	NandcWaitFlashReady
	cmp	w25, 1
	bhi	.L986
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
	b	.L987
.L986:
	mov	w0, 56
	str	w0, [x23,2056]
.L987:
	mov	w0, w24
	add	w20, w20, 1
	bl	NandcWaitFlashReady
	uxtb	w20, w20
	b	.L948
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L1026
	sub	w2, w0, #1
	cmp	w2, 6
	bhi	.L1028
	bl	HynixGetReadRetryDefault
	b	.L1026
.L1028:
	cmp	w0, 49
	bne	.L1029
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
	b	.L1044
.L1029:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L1035
	cmp	w1, 33
	bne	.L1030
.L1035:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 4
	b	.L1045
.L1030:
	cmp	w1, 67
	beq	.L1036
	cmp	w1, 34
	bne	.L1032
.L1036:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 5
.L1045:
	strb	w1, [x0,761]
	mov	w1, 7
	strb	w1, [x0,762]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 764
	add	x1, x1, 256
	mov	w2, 45
	b	.L1044
.L1032:
	cmp	w1, 68
	beq	.L1037
	cmp	w1, 35
	bne	.L1026
.L1037:
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
.L1044:
	bl	ftl_memcpy
.L1026:
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
	ldr	w0, [x1,1868]
	cmp	w0, 3
	bls	.L1077
	ldr	w0, [x19,16]
	tbz	x0, 2, .L1077
	mov	x0, x19
	bl	wait_for_nandc_xfer_completed
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L1048
	adrp	x22, .LC86
	adrp	x23, .LC87
	mov	w21, 0
	add	x22, x22, :lo12:.LC86
	add	x23, x23, :lo12:.LC87
.L1049:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1057
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1868]
	cmp	w0, 5
	bhi	.L1050
.L1053:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L1049
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
	b	.L1049
.L1050:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1053
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1053
.L1057:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 1880
	ldr	w0, [x19,40]
	cbz	w0, .L1058
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
	b	.L1058
.L1048:
	adrp	x22, .LC88
	adrp	x23, .LC87
	add	x22, x22, :lo12:.LC88
	add	x23, x23, :lo12:.LC87
.L1059:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L1085
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L1059
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
	b	.L1059
.L1085:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1928]
	cbz	w0, .L1062
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L1062:
	add	x21, x20, :lo12:.LANCHOR0
	add	x21, x21, 1880
	ldr	w0, [x21,40]
	cbz	w0, .L1063
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
.L1063:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1928]
	cbz	w0, .L1058
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L1058:
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x20,1920]
	b	.L1046
.L1077:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1077
.L1046:
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
	cbnz	x0, .L1087
	cbnz	x4, .L1088
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x22, x29, 112
	bl	ftl_memset
.L1088:
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
	cbnz	w21, .L1089
	add	x1, x19, :lo12:.LANCHOR0
	ubfx	x2, x24, 1, 7
	mov	w3, 128
	add	x2, x22, x2, lsl 2
	ldr	w0, [x1,1932]
	cmp	w0, 25
	mov	w0, 64
	csel	w3, w0, w3, cc
	mov	w0, w21
.L1091:
	cmp	x22, x2
	add	w4, w0, w3
	beq	.L1133
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
	b	.L1091
.L1133:
	add	x0, x19, :lo12:.LANCHOR0
	lsr	w24, w24, 2
	ldr	w5, [x0,1932]
	ldr	w4, [x0,1868]
	mov	w0, 0
	mov	w23, w0
.L1093:
	cmp	w0, w24
	bcs	.L1089
	cbz	w5, .L1089
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x20,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L1117
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L1117
	cmp	w4, 5
	bls	.L1095
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
	bls	.L1096
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L1097
.L1096:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L1132
.L1095:
	cmp	w4, 3
	bls	.L1097
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
	bls	.L1098
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L1132
.L1098:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L1132:
	orr	w3, w1, w3, lsl 5
.L1097:
	cmp	w23, w3
	csel	w23, w23, w3, cs
	b	.L1094
.L1117:
	mov	w23, -1
.L1094:
	add	w0, w0, 1
	b	.L1093
.L1089:
	str	wzr, [x20,16]
	b	.L1100
.L1087:
	cmp	w21, 1
	bne	.L1131
	mov	w23, 0
	mov	w28, 2
.L1101:
	cmp	w23, w24
	bcs	.L1134
	and	w27, w23, 3
	mov	x3, x26
	cbz	x26, .L1103
	ubfiz	x0, x23, 9, 23
	add	x3, x26, x0
.L1103:
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
	b	.L1101
.L1134:
	mov	w23, 0
	b	.L1100
.L1131:
	mov	w1, 0
	mov	x4, 0
	mov	w27, 0
	mov	w0, w25
	mov	w2, 2
	mov	w3, w1
	mov	x5, x4
	mov	w23, w27
	bl	NandcXferStart
.L1106:
	cmp	w27, w24
	bcs	.L1100
	mov	w0, w25
	add	w28, w27, 2
	bl	NandcXferComp
	ldr	w0, [x20,32]
	cmp	w28, w24
	str	w0, [x29,104]
	bcs	.L1107
	mov	x4, 0
	mov	w0, w25
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L1107:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L1122
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w23, w0
	csel	w23, w23, w0, cs
	b	.L1108
.L1122:
	mov	w23, -1
.L1108:
	and	w2, w27, 3
	mov	x3, x26
	cbz	x26, .L1109
	ubfiz	x3, x27, 9, 23
	add	x3, x26, x3
.L1109:
	cmp	x22, xzr
	mov	w0, 2
	csel	w4, w0, wzr, ne
	mov	w1, 0
	mov	x0, x20
	mul	w4, w4, w27
	mov	w27, w28
	add	x4, x22, x4
	bl	NandcCopy1KB
	b	.L1106
.L1100:
	cbnz	w21, .L1112
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19,1868]
	cmp	w0, 5
	bls	.L1112
	ldr	w0, [x20]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L1112
	orr	w0, w0, 131072
	mov	w23, -1
	str	w0, [x20]
.L1112:
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
	cbnz	w19, .L1136
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,9]
	ldr	w0, [x0,12]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L1136:
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
	cbz	w4, .L1139
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
	bne	.L1140
.L1149:
	mov	w21, -1
	b	.L1141
.L1140:
	adrp	x0, .LC89
	mov	w1, w24
	add	x0, x0, :lo12:.LC89
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L1142
	add	x19, x19, x23, sxtw 4
	ldr	x0, [x19,24]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L1142:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,64]
	add	w1, w1, 1
	str	w1, [x0,64]
	cmp	w1, 2047
	bls	.L1141
	mov	x27, 0
	str	wzr, [x0,64]
	mov	x28, x27
.L1139:
	mov	w19, 0
	mov	w26, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L1147:
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
	bhi	.L1143
	cmp	w0, 2
	bhi	.L1153
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L1153
	sub	w19, w22, w20
	mov	w21, w0
	mov	w26, 0
	b	.L1145
.L1143:
	cmp	w6, w20
	bcs	.L1154
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L1155
	mov	w6, w20
	b	.L1154
.L1153:
	mov	x27, 0
	mov	w19, w22
	mov	w21, w0
	mov	w26, 0
	mov	x28, x27
	b	.L1144
.L1154:
	mov	w20, 0
.L1144:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L1147
.L1145:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L1146
.L1155:
	mov	w19, w5
.L1146:
	cbz	w19, .L1148
	adrp	x0, .LC90
	mov	w1, w19
	add	x0, x0, :lo12:.LC90
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L1148:
	cbz	w26, .L1141
	adrp	x0, .LC91
	mov	w1, w23
	add	x0, x0, :lo12:.LC91
	mov	w2, w24
	bl	printk
	cbz	w25, .L1149
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L1141:
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
	bne	.L1165
	adrp	x21, .LANCHOR0
	add	x25, x21, :lo12:.LANCHOR0
	ldrb	w26, [x25,16]
	cbnz	w26, .L1166
.L1168:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1860]
	cbz	w0, .L1165
	b	.L1186
.L1166:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	strb	wzr, [x25,16]
	bl	FlashReadRawPage
	strb	w26, [x25,16]
	cmn	w0, #1
	beq	.L1168
	mov	w19, w0
	b	.L1165
.L1186:
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
	beq	.L1169
	ldrb	w0, [x21,1936]
	cmp	w19, w0, lsr 1
	bls	.L1165
.L1169:
	lsr	w0, w25, 8
	bl	NandcSetDdrPara
.L1165:
	adrp	x21, .LANCHOR4
	cmn	w19, #1
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x4, [x21,72]
	bne	.L1170
	cbz	x4, .L1170
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
	bne	.L1170
	adrp	x0, .LANCHOR0+120
	ldrb	w0, [x0,#:lo12:.LANCHOR0+120]
	cbz	w0, .L1170
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
.L1170:
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
	beq	.L1191
	cmn	w22, #1
	bne	.L1188
.L1191:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L1188
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1860]
	b	.L1190
.L1188:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,1860]
.L1190:
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
.L1199:
	add	w0, w20, 1
	mov	x19, 0
	str	w0, [x29,104]
.L1201:
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
	bne	.L1200
	ldr	x5, [x29,96]
	mov	w0, 0
	ldr	w1, [x29,104]
	mov	x3, 0
	ldr	x2, [x5,56]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1200
	add	x19, x19, 1
	cmp	x19, 4
	beq	.L1202
	b	.L1201
.L1203:
	add	x0, x2, 12
	mov	w1, 2036
	str	x2, [x29,104]
	mov	w24, -1
	bl	JSHash
	ldr	x2, [x29,104]
	ldr	w1, [x2,8]
	cmp	w1, w0
	beq	.L1213
.L1202:
	subs	w26, w26, #1
	add	w20, w20, w27
	bne	.L1199
	mov	w0, w26
.L1212:
	bl	flash_exit_slc_mode
	mov	w0, w24
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
.L1200:
	add	x19, x23, :lo12:.LANCHOR4
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x2, [x19,80]
	ldr	w1, [x2]
	cmp	w1, w0
	bne	.L1202
	cbnz	w24, .L1203
	add	x22, x22, :lo12:.LANCHOR1
	ldrh	w0, [x22,482]
	udiv	w20, w20, w0
	mov	w0, w24
	add	w20, w20, 1
	str	w20, [x19,92]
	b	.L1212
.L1213:
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
	strb	w0, [x25,1860]
	ldrh	w0, [x21,10]
	udiv	w0, w20, w0
	add	w2, w0, 1
	cbz	w0, .L1205
	str	w2, [x19,92]
	b	.L1206
.L1205:
	mov	w0, 2
	str	w0, [x19,92]
.L1206:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w1, [x1,14]
	mov	w24, 0
	strb	w1, [x0,96]
	b	.L1202
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
	bls	.L1215
	ldrb	w0, [x2,1860]
	cbz	w0, .L1216
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L1216:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L1215:
	ldr	x0, [x29,120]
	mov	w21, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x28, 8, 8
	str	x0, [x29,96]
.L1217:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L1242
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w21
	ldrb	w0, [x0,752]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1218
	bl	SandiskSetRRPara
	b	.L1219
.L1218:
	bl	ToshibaSetRRPara
.L1219:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,752]
	cmp	w0, 34
	bne	.L1220
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L1220
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L1220:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L1221
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
	b	.L1222
.L1221:
	ldr	w1, [x29,116]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L1222:
	cmn	w28, #1
	beq	.L1223
	add	x0, x19, :lo12:.LANCHOR0
	cmn	w25, #1
	csel	w25, w25, w28, ne
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1225
	mov	x26, 0
	mov	x27, x26
.L1223:
	add	w21, w21, 1
	b	.L1217
.L1242:
	mov	w28, w25
.L1225:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,752]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1227
	bl	SandiskSetRRPara
	b	.L1228
.L1227:
	bl	ToshibaSetRRPara
.L1228:
	ldr	x0, [x29,120]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	ldrb	w0, [x19,1936]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1229
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1229:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L1230
	mov	w0, 4
	bl	NandcSetDdrMode
.L1230:
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
.L1244:
	add	x0, x27, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L1247
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1245
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,1936]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1250
	mov	x23, 0
	mov	x24, x23
.L1245:
	add	w26, w26, 1
	b	.L1244
.L1250:
	mov	w19, w0
.L1247:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,1936]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1249
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1249:
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
	ldrb	w19, [x0,1936]
	mov	x22, x3
	ldrb	w0, [x0,120]
	cbnz	w0, .L1257
	add	w19, w19, w19, lsl 1
	ubfx	x19, x19, 2, 8
	b	.L1258
.L1257:
	mov	w0, 3
	sdiv	w19, w19, w0
.L1258:
	adrp	x27, .LC93
	mov	w25, 0
	add	x0, x27, :lo12:.LC93
	str	x0, [x29,152]
	add	x20, x20, :lo12:.LANCHOR0
	add	x23, x20, 24
	add	x23, x23, x21, sxtw 4
.L1269:
	mov	w0, w21
	mov	w24, 0
	mov	w27, -1
	bl	NandcWaitFlashReady
	ldrb	w4, [x23,8]
	mov	w9, 239
	ldr	x6, [x23]
	mov	w10, 137
	lsl	x8, x4, 8
.L1259:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,97]
	cmp	w24, w0
	bcs	.L1262
	add	x2, x6, x8
	mov	w0, 200
	str	x4, [x29,104]
	str	x8, [x29,112]
	str	w9, [x2,2056]
	str	w10, [x2,2052]
	str	x6, [x29,120]
	str	x9, [x29,128]
	str	x10, [x29,136]
	str	x2, [x29,144]
	bl	NandcDelayns
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
	beq	.L1260
	cmn	w27, #1
	csel	w27, w27, w0, ne
	cmp	w0, w19
	bcc	.L1271
	mov	x22, 0
	mov	x26, x22
.L1260:
	mov	w24, w7
	b	.L1259
.L1271:
	mov	x22, 0
	mov	w27, w0
	mov	x26, x22
.L1262:
	add	x1, x6, x4, lsl 8
	mov	w0, 239
	str	x1, [x29,144]
	str	w0, [x1,2056]
	mov	w0, 137
	str	w0, [x1,2052]
	mov	w0, 200
	bl	NandcDelayns
	ldr	x1, [x29,144]
	cmp	w27, w19
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	bcc	.L1264
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
.L1264:
	cmn	w27, #1
	cset	w6, eq
	cbnz	w6, .L1273
	cmp	w27, 256
	cset	w1, eq
	cbz	w1, .L1265
.L1273:
	ldr	x0, [x29,152]
	mov	w1, w24
	mov	w2, w28
	mov	w3, w24
	mov	w4, w27
	str	x6, [x29,144]
	bl	printk
	cbnz	w25, .L1267
	ldr	x6, [x29,144]
	cbz	w6, .L1278
	ldrb	w0, [x20,120]
	cbz	w0, .L1278
	mov	w0, w21
	mov	w1, 3
	mov	w25, 1
	bl	micron_auto_read_calibration_config
	b	.L1269
.L1267:
	mov	w0, w21
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
	b	.L1278
.L1265:
	cbz	w25, .L1278
	mov	w0, w21
	mov	w27, 256
	bl	micron_auto_read_calibration_config
.L1278:
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
	bne	.L1287
	ldrb	w19, [x0,20]
.L1287:
	mov	w0, w23
	mov	w28, 0
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	add	x5, x4, 764
.L1288:
	cmp	w28, w25
	bcs	.L1292
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
	beq	.L1290
	ldrb	w1, [x4,1936]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1297
	mov	x24, 0
	mov	x26, x24
.L1290:
	add	w28, w28, 1
	b	.L1288
.L1297:
	mov	w20, w0
.L1292:
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 760
	ldr	x1, [x0,-16]
	add	x0, x0, x22
	ldrb	w1, [x1,19]
	cmp	w1, 7
	bne	.L1294
	strb	w19, [x0,20]
	b	.L1295
.L1294:
	strb	w19, [x0,12]
.L1295:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1936]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1296
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1296:
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
	cbnz	w19, .L1304
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,9]
	ldr	w2, [x0,12]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L1304
	ldrb	w0, [x0,8]
	cbnz	w0, .L1305
	sub	w21, w21, #2
	b	.L1304
.L1305:
	mov	w21, 4
.L1304:
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
.L1309:
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
	beq	.L1307
	ldr	x25, [x20,80]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L1307
	mov	w1, 2036
	add	x0, x25, 12
	bl	JSHash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L1307
	ldr	w0, [x22,12]
	cmp	w21, 1
	str	w26, [x20,92]
	mul	w19, w19, w0
	str	w19, [x20,88]
	beq	.L1310
	mov	w21, 1
.L1307:
	cmp	w26, 4
	mov	w19, w26
	bne	.L1309
	b	.L1308
.L1310:
	mov	w21, 2
.L1308:
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
	ldr	w1, [x0,1856]
	ldrb	w27, [x0,1936]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L1316
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1316:
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
.L1317:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,9]
	cmp	w20, w0
	bcs	.L1321
	mov	x22, 0
.L1319:
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
	bne	.L1318
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L1319
	b	.L1320
.L1318:
	ldr	x0, [x21,56]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L1320
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
	bls	.L1324
	mov	w23, 0
	str	w20, [x21,92]
	bl	FlashSavePhyInfo
.L1320:
	add	w20, w20, 1
	b	.L1317
.L1324:
	mov	w23, 0
.L1321:
	mov	w0, w27
	add	x19, x19, :lo12:.LANCHOR0
	bl	FlashBchSel
	ldr	w1, [x19,1856]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L1326
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1326:
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
	adrp	x25, .LC95
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
	strb	wzr, [x20,1860]
	strb	w1, [x20,9]
	strb	wzr, [x20,8]
	add	x20, x20, 1620
	strb	w1, [x22,96]
	mov	x26, x20
	str	wzr, [x22,92]
	str	wzr, [x22,64]
	bl	NandcInit
	add	x0, x25, :lo12:.LC95
	str	x0, [x29,104]
.L1333:
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
	bhi	.L1329
	ldr	x0, [x29,104]
	add	w1, w27, 1
	ldrb	w3, [x20,1]
	ldrb	w4, [x20,2]
	ldrb	w5, [x20,3]
	ldrb	w6, [x20,4]
	ldrb	w7, [x20,5]
	bl	printk
.L1329:
	cbnz	w27, .L1330
	ldrb	w0, [x26]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1378
	ldrb	w0, [x26,1]
	cmp	w0, 255
	beq	.L1378
.L1330:
	ldrb	w0, [x20]
	cmp	w0, 181
	bne	.L1332
	strb	w28, [x20]
.L1332:
	add	w27, w27, 1
	add	x24, x24, 16
	cmp	w27, 4
	add	x20, x20, 8
	bne	.L1333
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 173
	beq	.L1334
	ldr	w0, [x0,1864]
	bl	NandcSetDdrMode
.L1334:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, 0
	add	x0, x22, 760
	mov	w2, 852
	adrp	x20, .LANCHOR1
	bl	ftl_memset
	strb	wzr, [x22,16]
	add	x0, x20, :lo12:.LANCHOR1
	add	x0, x0, 472
	str	x0, [x22,744]
	ldrb	w0, [x22,1621]
	add	x22, x22, 1620
	cmp	w0, 218
	cset	w2, eq
	cmp	w0, 161
	cset	w1, eq
	orr	w1, w2, w1
	cbnz	w1, .L1335
	and	w1, w0, -33
	cmp	w1, 209
	beq	.L1335
	cmp	w0, 220
	bne	.L1336
	ldrb	w1, [x22,3]
	cmp	w1, 149
	bne	.L1336
.L1335:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 1
	mov	w4, 16
	add	x5, x1, 1620
	strb	w3, [x1,8]
	add	x3, x21, :lo12:.LANCHOR4
	strb	w4, [x1,9]
	strb	w4, [x3,98]
	ldrb	w4, [x1,1620]
	add	x1, x20, :lo12:.LANCHOR1
	cmp	w4, 152
	strb	w4, [x1,3137]
	strb	w0, [x1,3138]
	bne	.L1337
	ldrsb	w1, [x5,4]
	tbnz	w1, #31, .L1337
	mov	w1, 24
	strb	w1, [x3,98]
.L1337:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w3, [x1,1856]
	cmp	w3, 2049
	beq	.L1380
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w3, w1
	bne	.L1338
.L1380:
	add	x1, x21, :lo12:.LANCHOR4
	mov	w3, 16
	strb	w3, [x1,98]
.L1338:
	cbz	w2, .L1340
	add	x0, x20, :lo12:.LANCHOR1
	mov	w1, 2048
	strh	w1, [x0,3150]
	mov	w1, -38
	b	.L1413
.L1340:
	cmp	w0, 220
	bne	.L1341
	add	x0, x20, :lo12:.LANCHOR1
	mov	w1, 4096
	strh	w1, [x0,3150]
	mov	w1, -36
.L1413:
	strb	w1, [x0,3138]
.L1341:
	add	x20, x20, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x20, 3008
	mov	w2, 32
	add	x0, x0, 88
	bl	ftl_memcpy
	add	x0, x20, 472
	add	x1, x20, 3136
	mov	w2, 32
	bl	ftl_memcpy
.L1336:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,8]
	cbnz	w0, .L1342
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L1344
	ldr	x0, [x20,744]
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x20,1844]
	tbnz	x1, 0, .L1344
	mov	w1, 1
	strb	w1, [x20,1860]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
	bl	NandcSetMode
.L1344:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,744]
	ldrb	w0, [x0,26]
	strb	w0, [x20,120]
	bl	FlashLoadPhyInfo
	cbz	w0, .L1342
	ldr	w0, [x20,1864]
	cbz	w0, .L1347
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L1414
.L1347:
	ldrb	w0, [x20,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
.L1414:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L1342
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
	beq	.L1331
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
	blt	.L1349
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L1350
.L1349:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L1350:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1844]
	tst	w0, 6
	beq	.L1351
	bl	FlashSavePhyInfo
	add	x1, x21, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x1,88]
	bl	FlashDdrParaScan
.L1351:
	bl	FlashSavePhyInfo
.L1342:
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
	tbz	x1, 6, .L1353
	ldrb	w0, [x0,19]
	ldrb	w1, [x20,762]
	strb	w1, [x22,97]
	sub	w1, w0, #1
	ldrb	w2, [x20,761]
	uxtb	w1, w1
	strb	w0, [x20,752]
	strb	w2, [x20,753]
	cmp	w1, 6
	bhi	.L1354
	adrp	x1, HynixReadRetrial
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,72]
	sub	w1, w0, #5
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L1355
	mov	w1, 1
	str	w1, [x20,1928]
	b	.L1356
.L1355:
	cmp	w0, 7
	add	x20, x20, 788
	beq	.L1357
.L1356:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 780
.L1357:
	mov	x1, 0
	mov	w2, w1
.L1359:
	ldrsb	w3, [x20,x1]
	add	x1, x1, 1
	cmp	w3, wzr
	csinc	w2, w2, w2, ne
	cmp	x1, 32
	bne	.L1359
	cmp	w2, 27
	bls	.L1353
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1353
.L1354:
	sub	w1, w0, #17
	uxtb	w1, w1
	cmp	w1, 2
	bhi	.L1361
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x22,72]
	beq	.L1362
	mov	w0, 7
	b	.L1415
.L1362:
	mov	w0, 15
.L1415:
	strb	w0, [x22,97]
	b	.L1353
.L1361:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1381
	cmp	w0, 33
	bne	.L1363
.L1381:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	mov	w2, 4
	str	x1, [x0,72]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,753]
	mov	w1, 7
	strb	w1, [x0,97]
	b	.L1353
.L1363:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w2, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1382
	cbz	w2, .L1365
.L1382:
	add	x3, x21, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x1, [x3,72]
	beq	.L1367
	cmp	w0, 68
	beq	.L1367
	mov	w0, 7
	strb	w0, [x3,97]
	b	.L1368
.L1367:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,97]
.L1368:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w2, .L1416
	mov	w1, 5
.L1416:
	strb	w1, [x0,753]
	b	.L1353
.L1365:
	cmp	w0, 49
	bne	.L1353
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,72]
.L1353:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ldr	w2, [x0,1856]
	cmp	w2, w1
	bne	.L1370
	ldrb	w1, [x0,120]
	cbz	w1, .L1370
	ldr	x0, [x0,744]
	strb	wzr, [x0,18]
.L1370:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 44
	bne	.L1371
	ldrb	w1, [x0,1860]
	cbz	w1, .L1371
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L1372
	ldrb	w0, [x0,120]
	cbnz	w0, .L1371
.L1372:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,1860]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L1371:
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w0, [x1,1844]
	tst	w0, 6
	beq	.L1373
	ldrb	w1, [x1,1860]
	cbnz	w1, .L1374
	tbnz	x0, 0, .L1373
.L1374:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x21,88]
	bl	FlashDdrParaScan
.L1373:
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
	bne	.L1376
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,190]
	strb	w20, [x19,9]
	mov	w2, 8
	strh	w4, [x19,182]
	strh	w3, [x19,188]
	strh	w2, [x19,194]
.L1376:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
	b	.L1331
.L1378:
	mov	w0, -2
.L1331:
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
	cbz	w4, .L1418
	ldr	w2, [x0,1856]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w2, w0
	beq	.L1417
.L1418:
	sub	w2, w3, #5
	uxtb	w2, w2
	cmp	w2, 2
	bls	.L1419
	cmp	w3, 68
	beq	.L1419
	sub	w3, w3, #19
	and	w3, w3, -17
	uxtb	w3, w3
	cbnz	w3, .L1417
.L1419:
	adrp	x20, .LANCHOR2
	mov	w24, w1
	add	x20, x20, :lo12:.LANCHOR2
	mov	w25, 65535
	add	x20, x20, 988
	adrp	x26, .LANCHOR4
.L1421:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,744]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L1417
	ldrh	w0, [x20,w19,sxtw 1]
	cmp	w0, w25
	bne	.L1417
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
	b	.L1421
.L1417:
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
	adrp	x23, .LC97
	adrp	x24, .LC99
	ldrb	w26, [x1,#:lo12:.LANCHOR1+481]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w27, w2
	mov	w22, 0
	add	x21, x21, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC97
	add	x24, x24, :lo12:.LC99
.L1427:
	cmp	w22, w25
	beq	.L1461
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
	bcc	.L1428
	mov	w0, -1
	str	w0, [x19]
	b	.L1429
.L1428:
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
	bls	.L1432
	cmn	w3, #1
	mov	w0, 256
	csel	w3, w3, w0, eq
.L1432:
	cmp	w3, 256
	beq	.L1443
	cmn	w3, #1
	bne	.L1433
.L1443:
	str	w3, [x19]
	b	.L1435
.L1433:
	str	wzr, [x19]
.L1435:
	ldr	x0, [x19,16]
	cbz	x0, .L1436
	ldr	w1, [x0,12]
	cmn	w1, #1
	bne	.L1436
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L1436
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1436
	str	w1, [x19]
.L1436:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1429
	add	x5, x5, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	mov	x0, x23
	ldrb	w2, [x5,1936]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L1438
	adrp	x0, .LC98
	mov	w2, 4
	add	x0, x0, :lo12:.LC98
	mov	w3, 8
	bl	rknand_print_hex
.L1438:
	ldr	x1, [x19,16]
	cbz	x1, .L1429
	mov	w2, 4
	mov	x0, x24
	mov	w3, w2
	bl	rknand_print_hex
.L1429:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L1427
.L1461:
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
	cbz	w23, .L1502
	bl	FlashReadSlc2KPages
	b	.L1551
.L1502:
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
.L1463:
	ldr	w0, [x29,144]
	cmp	w24, w0
	bcs	.L1553
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
	bcc	.L1465
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L1466
.L1465:
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
	bhi	.L1468
	add	x1, x1, 760
	sxtw	x0, w19
	add	x1, x1, x0
	cmp	w2, 7
	ldrb	w3, [x1,12]
	bne	.L1469
	ldrb	w3, [x1,20]
.L1469:
	add	x2, x22, :lo12:.LANCHOR0
	add	x0, x2, x0
	ldrb	w0, [x0,1616]
	cmp	w0, w3
	beq	.L1468
	ldrb	w1, [x2,761]
	mov	w0, w19
	add	x2, x2, 764
	bl	HynixSetRRPara
.L1468:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,140]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,152]
	cbz	w0, .L1470
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1470
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1471
.L1470:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1471:
	cmp	w19, 255
	ldr	w1, [x29,172]
	bne	.L1509
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1503
.L1509:
	cbz	w20, .L1475
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x0,12]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1476
.L1475:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1476
.L1503:
	mov	w20, w0
.L1473:
	ldrb	w2, [x29,148]
	mov	w0, w19
	ldr	x3, [x21,8]
	mov	w1, 0
	ldr	x4, [x21,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1477
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0,16]
	cbz	w1, .L1477
	strb	wzr, [x0,16]
	mov	w20, 0
	b	.L1471
.L1477:
	cbz	w20, .L1478
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
.L1478:
	mov	w0, w19
	bl	NandcFlashDeCs
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w0, [x29,136]
	cmn	w28, #1
	strb	w0, [x6,16]
	bne	.L1485
	ldrb	w0, [x6,1860]
	cbnz	w0, .L1480
.L1484:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x20, [x0,72]
	cbnz	x20, .L1481
	b	.L1554
.L1480:
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
	beq	.L1483
	ldr	x6, [x29,104]
	ldrb	w0, [x6,1936]
	cmp	w28, w0, lsr 1
	bls	.L1505
.L1483:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1484
	b	.L1505
.L1481:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L1486
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,744]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L1487
	ldrb	w1, [x2,761]
	mov	w0, w19
	add	x2, x2, 764
	mov	w3, w20
	bl	HynixSetRRPara
.L1487:
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
	bne	.L1486
	ldrb	w0, [x4,120]
	cbz	w0, .L1486
	ldr	w0, [x29,152]
	cbz	w0, .L1488
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1489
.L1488:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1489:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	mov	w20, 0
	ldr	x3, [x21,16]
	ldr	x4, [x0,72]
	mov	w0, w19
	blr	x4
	mov	w28, w0
	b	.L1486
.L1554:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1486
.L1505:
	mov	w20, 0
.L1485:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1486
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x0, [x0,72]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1486:
	cmp	w28, 256
	beq	.L1510
	cmn	w28, #1
	bne	.L1490
.L1510:
	str	w28, [x26,x27]
	b	.L1492
.L1490:
	str	wzr, [x26,x27]
.L1492:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L1494
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x21,4]
	ldr	x0, [x29,128]
	ldrb	w2, [x2,1936]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L1494
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	bl	rknand_print_hex
.L1494:
	cbz	w20, .L1496
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1936]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L1497
	add	x25, x25, :lo12:.LANCHOR4
	ldr	x0, [x25,72]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L1497:
	add	w0, w24, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L1511
	cmn	w23, #1
	bne	.L1498
.L1511:
	str	w23, [x26,x0]
	b	.L1496
.L1498:
	str	wzr, [x26,x0]
.L1496:
	ldr	w0, [x29,152]
	add	w24, w24, w20
	cbz	w0, .L1466
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1466
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1466:
	add	w24, w24, 1
	b	.L1463
.L1553:
	mov	w0, 0
	b	.L1551
.L1476:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L1473
	ldr	w1, [x29,172]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L1473
.L1551:
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
.L1556:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,1845]
	cmp	w1, w19
	bls	.L1566
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1557:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1559
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
	beq	.L1558
	add	x24, x23, :lo12:.LANCHOR4
	ldr	x0, [x24,112]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1558
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	add	x24, x24, x19, sxtw 1
	bl	printk
	uxth	w28, w28
	strh	w20, [x24,144]
	b	.L1559
.L1558:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1557
.L1559:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,1845]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1556
.L1566:
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
.L1568:
	cmp	w25, w23
	beq	.L1597
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,1845]
	cmp	w0, w1
	bcc	.L1569
	str	w28, [x21]
	b	.L1570
.L1569:
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
	tbz	x0, 0, .L1573
	str	w28, [x21]
.L1573:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1570:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1568
.L1597:
	mov	w21, 0
	cbz	w27, .L1595
	adrp	x25, .LC105
	adrp	x26, .LC104
	adrp	x27, .LC103
	add	x25, x25, :lo12:.LC105
	add	x26, x26, :lo12:.LC104
	add	x27, x27, :lo12:.LC103
.L1596:
	cmp	w21, w23
	beq	.L1595
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1576
	adrp	x0, .LC102
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC102
	bl	printk
	b	.L1577
.L1576:
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
	bne	.L1578
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1578:
	ldr	x0, [x19,16]
	cbz	x0, .L1579
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,128]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1579
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1579:
	ldr	x0, [x19,8]
	cbz	x0, .L1577
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,120]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1577
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1577:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1596
.L1595:
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
.L1599:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,1970]
	cmp	w23, w1
	bcs	.L1608
	ldrh	w20, [x0,2014]
	strh	w25, [x21,12]
.L1601:
	add	x0, x22, :lo12:.LANCHOR0
	sub	w20, w20, #1
	uxth	w20, w20
	ldrh	w0, [x0,2014]
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1602
	madd	w0, w23, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1601
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1601
	strh	w20, [x21,12]
.L1602:
	add	w23, w23, 1
	add	x21, x21, 2
	b	.L1599
.L1608:
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
	bne	.L1610
	ldrh	w19, [x3,2020]
	b	.L1611
.L1610:
	ldrh	w19, [x3,2018]
.L1611:
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
	bne	.L1612
	mov	w22, 0
	mov	w25, 2
.L1613:
	cmp	w22, w19
	bgt	.L1612
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
	bne	.L1614
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1614
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1614
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1613
.L1614:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1613
.L1612:
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
	ldrh	w21, [x0,2014]
	sub	w21, w21, #1
	uxth	w21, w21
.L1621:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2014]
	sub	w0, w0, #48
	cmp	w21, w0
	ble	.L1624
	lsl	w0, w21, 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1622
	ldr	w0, [x19,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
.L1622:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1623
	ldrh	w0, [x22]
	cmp	w0, w25
	bne	.L1623
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2104
	strh	w21, [x1,2104]
	ldr	w1, [x22,4]
	str	w1, [x0,8]
	ldrh	w1, [x22,8]
	strh	w1, [x0,4]
	b	.L1624
.L1623:
	sub	w21, w21, #1
	uxth	w21, w21
	b	.L1621
.L1624:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,2104]
	cmp	w1, w2
	beq	.L1626
	ldrh	w1, [x21,2108]
	cmp	w1, w2
	beq	.L1628
	add	x25, x23, :lo12:.LANCHOR4
	lsl	w1, w1, 10
	add	x0, x25, 160
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x25,160]
	cmn	w0, #1
	beq	.L1628
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1628
	ldr	w0, [x22,4]
	ldr	w1, [x21,2112]
	cmp	w0, w1
	bls	.L1628
	str	w0, [x21,2112]
	ldrh	w1, [x21,2108]
	ldrh	w0, [x22,8]
	strh	w1, [x21,2104]
	strh	w0, [x21,2108]
.L1628:
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
.L1630:
	tbnz	w19, #31, .L1635
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
	beq	.L1631
	ldrh	w0, [x22]
	cmp	w0, w26
	bne	.L1631
.L1635:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,2110]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1632
	b	.L1633
.L1631:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1630
.L1632:
	ldr	w2, [x1,1944]
	cmp	w0, w2
	beq	.L1633
	ldrh	w1, [x1,1958]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1633
	cmp	w2, w1
	bcs	.L1633
	bl	FtlSysBlkNumInit
.L1633:
	add	x20, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x21, x20, 2104
	add	x23, x23, :lo12:.LANCHOR4
.L1636:
	ldrh	w0, [x20,1970]
	mov	w1, w19
	add	x19, x19, 1
	cmp	w1, w0
	bcs	.L1654
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x3, [x23,168]
	ldrh	w2, [x0,2248]
	add	x0, x21, x19, lsl 3
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1636
.L1654:
	mov	w0, 0
.L1626:
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
	add	x0, x21, 264
	mov	w1, 255
	mov	w2, 12
	bl	ftl_memset
	ldrh	w21, [x20,1956]
.L1656:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,1958]
	cmp	w0, w21
	bls	.L1697
	mov	x27, 0
	ldrh	w5, [x1,1948]
	ldrh	w4, [x1,2030]
	mov	w20, w27
	mov	x23, x1
.L1698:
	cmp	w5, w27, uxth
	bls	.L1731
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
	cbnz	w0, .L1657
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
.L1657:
	add	x27, x27, 1
	b	.L1698
.L1731:
	cbz	w20, .L1660
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	mov	w28, 61604
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L1661:
	cmp	w20, w23, uxth
	bls	.L1660
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
	bne	.L1663
	mov	w7, 16
.L1662:
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
	bne	.L1664
	str	w27, [x0,x6]
	b	.L1663
.L1664:
	ldr	w0, [x0,x6]
	cmn	w0, #1
	bne	.L1663
	sub	w7, w7, #1
	uxth	w7, w7
	cbnz	w7, .L1662
.L1663:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
	ldr	w1, [x1,x6]
	cmn	w1, #1
	beq	.L1666
	ldr	w0, [x0,232]
	cmn	w0, #1
	beq	.L1667
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1668
.L1667:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1668
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,232]
.L1668:
	ldrh	w0, [x26]
	cmp	w0, w28
	beq	.L1670
	bhi	.L1671
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1669
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldrh	w6, [x0,2040]
	ldrh	w0, [x0,2066]
	sub	w2, w6, #1
	ldr	x7, [x7,2200]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1684
.L1671:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1673
	cmp	w0, w22
	bne	.L1669
	mov	w0, w25
	b	.L1730
.L1673:
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
.L1675:
	cmp	w1, w2
	ble	.L1732
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bls	.L1676
	ldr	w2, [x7]
	cbnz	w2, .L1677
	cmp	w0, w6
	beq	.L1677
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,2320]
.L1677:
	mov	w0, 0
.L1678:
	cmp	w0, w1
	beq	.L1733
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
	b	.L1678
.L1733:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x2, [x0,2232]
	str	w6, [x2,x8]
	ldr	x2, [x0,2176]
	strh	w25, [x2,x9,lsl 1]
	tbz	w1, #31, .L1680
	b	.L1669
.L1676:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1675
.L1732:
	tbz	w1, #31, .L1713
	b	.L1669
.L1680:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2320]
	ldr	w2, [x2,2048]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1669
.L1713:
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldr	w4, [x26,4]
	strh	w0, [x2,2320]
	ldr	x0, [x2,2232]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2176]
	b	.L1729
.L1690:
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bhi	.L1734
	sub	w1, w1, #1
	sxth	x1, w1
.L1684:
	cmp	w1, w2
	bgt	.L1690
	b	.L1689
.L1734:
	ldr	w2, [x7]
	cbnz	w2, .L1686
	cmp	w0, w6
	beq	.L1686
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	strh	w0, [x2,2066]
.L1686:
	mov	w0, 0
.L1687:
	cmp	w0, w1
	beq	.L1735
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
	b	.L1687
.L1735:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	ldr	x0, [x0,2200]
	str	w2, [x0,x8]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2072]
	strh	w25, [x0,x9,lsl 1]
.L1689:
	tbnz	w1, #31, .L1669
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2040]
	ldrh	w6, [x2,2066]
	sub	w0, w0, #1
	sub	w0, w0, w6
	cmp	w1, w0, sxth
	bgt	.L1669
	add	x0, x24, :lo12:.LANCHOR2
	add	w6, w6, 1
	ldr	w4, [x26,4]
	strh	w6, [x2,2066]
	ldr	x0, [x0,2200]
	str	w4, [x0,x1,lsl 2]
	ldr	x0, [x2,2072]
.L1729:
	strh	w25, [x0,x1,lsl 1]
	b	.L1669
.L1670:
	add	x0, x24, :lo12:.LANCHOR2
	add	x1, x0, 264
	ldrh	w2, [x0,264]
	cmp	w2, w22
	bne	.L1691
	strh	w25, [x0,264]
	ldr	w0, [x26,4]
	str	w0, [x1,8]
	b	.L1669
.L1691:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1692
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1692:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	add	x0, x1, 264
	ldr	w6, [x0,8]
	cmp	w6, w2
	bcs	.L1693
	ldrh	w2, [x1,264]
	strh	w2, [x0,4]
	strh	w25, [x1,264]
	ldr	w1, [x26,4]
	str	w1, [x0,8]
	b	.L1669
.L1693:
	strh	w25, [x0,4]
	b	.L1669
.L1666:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,120]
	mov	w0, w25
	cbz	w1, .L1694
.L1730:
	mov	w1, 0
.L1694:
	bl	FtlFreeSysBlkQueueIn
.L1669:
	add	x23, x23, 1
	b	.L1661
.L1660:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1656
.L1697:
	add	x4, x24, :lo12:.LANCHOR2
	ldr	x2, [x4,2176]
	ldrh	w0, [x2]
	cbz	w0, .L1699
.L1702:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,2072]
	ldrh	w0, [x2]
	cbz	w0, .L1700
	b	.L1723
.L1699:
	ldrh	w4, [x4,2320]
	cbz	w4, .L1702
	ldr	w1, [x1,2048]
.L1703:
	cmp	w0, w1
	bcs	.L1702
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1704
	mov	w7, w0
.L1705:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w1, [x1,2048]
	cmp	w0, w1
	bcs	.L1702
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
	b	.L1705
.L1704:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1703
.L1700:
	ldrh	w4, [x1,2066]
	cbz	w4, .L1723
	ldrh	w1, [x1,2040]
.L1708:
	cmp	w0, w1
	mov	w7, w0
	bge	.L1723
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1709
.L1710:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2,2040]
	cmp	w0, w1
	bge	.L1723
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
	b	.L1710
.L1709:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1708
.L1723:
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
	adrp	x19, .LANCHOR2
	adrp	x23, .LANCHOR4
	stp	x21, x22, [sp,32]
	add	x22, x19, :lo12:.LANCHOR2
	add	x21, x23, :lo12:.LANCHOR4
	str	x25, [sp,64]
	add	x21, x21, 160
	adrp	x20, .LANCHOR0
	add	x25, x22, 264
	ldr	x0, [x22,2072]
	str	x0, [x21,8]
	ldr	x0, [x22,2120]
	str	x0, [x21,16]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,1956]
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x22,264]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1737
.L1745:
	mov	w19, -1
	b	.L1738
.L1737:
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w24, w0
	add	w0, w0, 1
	strh	w0, [x25,2]
	mov	w25, 61604
.L1739:
	tbnz	w24, #31, .L1744
	ldrh	w0, [x22,264]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w24, w0, lsl 10
	str	w0, [x21,4]
	ldr	x0, [x22,2072]
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1740
	ldr	x0, [x22,2072]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1740
	ldr	x0, [x22,2120]
	ldrh	w0, [x0]
	cmp	w0, w25
	bne	.L1740
.L1744:
	add	x21, x23, :lo12:.LANCHOR4
	add	x24, x19, :lo12:.LANCHOR2
	add	x21, x21, 160
	add	x22, x20, :lo12:.LANCHOR0
	mov	w2, 48
	sub	x0, x24, #64
	ldr	x1, [x21,8]
	bl	ftl_memcpy
	ldr	x1, [x21,8]
	ldrh	w2, [x22,1956]
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x22,1956]
	ldr	x3, [x21,8]
	ldr	x0, [x20,#:lo12:.LANCHOR0]
	uxth	x1, w2
	lsr	w2, w2, 3
	add	x1, x1, 24
	add	w2, w2, 4
	lsr	x1, x1, 1
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	ldrh	w0, [x22,2064]
	cbz	w0, .L1742
	ldrh	w0, [x22,1956]
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
	b	.L1742
.L1740:
	sub	w24, w24, #1
	sxth	w24, w24
	b	.L1739
.L1742:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w2, [x0,-64]
	cmp	w2, w1
	bne	.L1745
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w3, [x0,-54]
	ldrh	w4, [x0,-56]
	strh	w4, [x0,270]
	ldrh	w2, [x1,1970]
	cmp	w3, w2
	bne	.L1745
	ldrh	w3, [x1,2018]
	add	x23, x23, :lo12:.LANCHOR4
	ldrh	w2, [x1,2024]
	strh	wzr, [x0,282]
	mul	w3, w3, w4
	str	w3, [x1,2096]
	str	w4, [x23,216]
	mul	w3, w3, w2
	str	w3, [x1,2060]
	ldrh	w2, [x1,2110]
	ldr	w3, [x1,1960]
	ldrh	w1, [x1,1948]
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
	bls	.L1746
	str	w1, [x0,232]
.L1746:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,-28]
	ldr	w2, [x0,236]
	cmp	w1, w2
	bls	.L1747
	str	w1, [x0,236]
.L1747:
	mov	w0, 65535
	cmp	w3, w0
	beq	.L1748
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 40
	bl	make_superblock
.L1748:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x1, 88
	ldrh	w2, [x1,88]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1749
	bl	make_superblock
.L1749:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x1, 136
	ldrh	w2, [x1,136]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1750
	bl	make_superblock
.L1750:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x19, 280
	ldrh	w2, [x19,280]
	mov	w19, 0
	cmp	w2, w1
	beq	.L1738
	bl	make_superblock
.L1738:
	mov	w0, w19
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
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
	ldrh	w24, [x19,2018]
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
	cbnz	w25, .L1764
	ldrb	w0, [x19,120]
	cbz	w0, .L1764
	mov	w0, w23
	bl	ftl_get_blk_mode
	cmp	w0, 1
	mov	w23, w0
	bne	.L1756
	ldrh	w24, [x19,2020]
	b	.L1756
.L1764:
	mov	w23, 0
.L1756:
	add	x3, x22, :lo12:.LANCHOR0
	adrp	x0, .LC108
	adrp	x25, .LC109
	add	x0, x0, :lo12:.LC108
	mov	w1, w23
	mov	w2, w24
	ldrh	w3, [x3,2018]
	mov	w20, 0
	mov	w27, 65535
	mov	w28, 56
	mov	w26, 4
	add	x25, x25, :lo12:.LC109
	bl	printk
.L1757:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w5, [x0,1948]
	ldrh	w6, [x0,2028]
	ldrh	w7, [x0,2030]
	mov	x0, 0
	mov	w19, w0
.L1758:
	cmp	w5, w0, uxth
	bls	.L1772
	add	x1, x29, 112
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w27
	beq	.L1759
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
.L1759:
	add	x0, x0, 1
	b	.L1758
.L1772:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, w23
	ldr	x0, [x0,2048]
	bl	FlashReadPages
	mov	x8, 0
	mov	x9, 56
.L1761:
	cmp	w19, w8, uxth
	bls	.L1773
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
	b	.L1761
.L1773:
	add	w20, w20, 1
	uxth	w20, w20
	cmp	w20, w24
	bne	.L1757
.L1763:
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
.L1775:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,1958]
	cmp	w0, w20
	bls	.L1784
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
	ldrh	w6, [x19,1948]
	ldrh	w7, [x19,2028]
	mov	w9, 65535
	ldrh	w8, [x19,2030]
	mov	w10, 56
	mov	w19, w1
	mov	w5, 4
.L1776:
	cmp	w6, w1, uxth
	bls	.L1785
	add	x0, x29, 80
	add	x0, x0, x1, lsl 1
	ldrh	w2, [x0,16]
	cmp	w2, w9
	beq	.L1777
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
.L1777:
	add	x1, x1, 1
	b	.L1776
.L1785:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x25, 0
	mov	x26, 56
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L1779:
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w19, w25, uxth
	bls	.L1786
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
	b	.L1779
.L1786:
	ldr	x0, [x0,2048]
	mov	w1, w19
	mov	w2, 1
	mov	x25, 0
	mov	x26, 56
	bl	FlashReadPages
.L1781:
	cmp	w19, w25, uxth
	bls	.L1787
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
	b	.L1781
.L1787:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1775
.L1784:
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
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR1
	mov	x19, x0
	add	x0, x20, :lo12:.LANCHOR1
	str	w1, [x29,152]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	x20, [x29,144]
	ldrh	w28, [x0,3168]
	mov	w0, 65535
	cmp	w28, w0
	beq	.L1810
	cbnz	w28, .L1789
	b	.L1790
.L1810:
	mov	w28, 0
.L1789:
	adrp	x0, .LANCHOR0+2018
	ldr	w1, [x29,152]
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2018]
	cmp	w1, w0
	bne	.L1791
.L1790:
	bl	FtlGcPageVarInit
.L1791:
	mov	w0, -1
	str	w0, [x29,156]
	adrp	x0, .LC113
	mov	w21, 0
	add	x0, x0, :lo12:.LC113
	mov	w22, 65535
	adrp	x23, .LANCHOR0
	str	x0, [x29,136]
.L1804:
	ldrh	w0, [x19]
	strb	wzr, [x19,8]
	cmp	w0, w22
	beq	.L1811
.L1793:
	adrp	x27, .LANCHOR2
.L1807:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w20, 0
	mov	w9, 4
	ldrh	w5, [x0,1948]
	ldrh	w8, [x0,2030]
	add	x5, x19, x5, lsl 1
.L1794:
	cmp	x2, x5
	beq	.L1819
	ldrh	w1, [x2,16]
	cmp	w1, w22
	beq	.L1795
	add	x6, x27, :lo12:.LANCHOR2
	mov	w0, 56
	orr	w1, w28, w1, lsl 10
	add	w4, w20, 1
	umull	x0, w20, w0
	ldr	x7, [x6,2048]
	add	x7, x7, x0
	str	w1, [x7,4]
	ldr	x1, [x6,2048]
	ldr	x6, [x6,888]
	add	x0, x1, x0
	mul	w1, w20, w8
	uxth	w20, w4
	str	xzr, [x0,8]
	sdiv	w1, w1, w9
	add	x1, x6, x1, sxtw 2
	str	x1, [x0,16]
.L1795:
	add	x2, x2, 2
	b	.L1794
.L1819:
	add	x0, x27, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 0
	mov	x24, 0
	adrp	x25, .LANCHOR2
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L1797:
	cmp	w20, w24, uxth
	bls	.L1820
	add	x1, x25, :lo12:.LANCHOR2
	mov	x0, 56
	str	x1, [x29,104]
	mul	x26, x24, x0
	ldr	x10, [x1,2048]
	str	x10, [x29,112]
	add	x9, x10, x26
	str	x9, [x29,120]
	ldr	w2, [x9,4]
	str	x2, [x29,128]
	lsr	x0, x2, 10
	bl	P2V_plane
	uxth	w7, w0
	ldr	x10, [x29,112]
	ldr	x2, [x29,128]
	ldr	w0, [x10,x26]
	cbnz	w0, .L1798
	ldr	x9, [x29,120]
	add	x24, x24, 1
	ldr	x1, [x29,104]
	ldr	x5, [x9,16]
	ldrh	w0, [x5]
	cmp	w0, w22
	bne	.L1799
	mov	w0, 1
	str	w0, [x1,2344]
	b	.L1792
.L1799:
	mov	w1, w2
	ldr	w0, [x5,12]
	ldr	w2, [x5,8]
	bl	FtlGcUpdatePage
	b	.L1797
.L1798:
	ldrh	w1, [x19]
	ldr	x0, [x29,136]
	bl	printk
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	w1, [x0,1940]
	cbnz	w1, .L1800
	ldrb	w0, [x0,120]
	cbz	w0, .L1801
.L1800:
	add	x0, x25, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,-80]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 159
	bls	.L1802
.L1801:
	add	x0, x25, :lo12:.LANCHOR2
	ldr	x0, [x0,2048]
	ldr	w0, [x0,x26]
	cmn	w0, #1
	bne	.L1803
.L1802:
	add	x0, x25, :lo12:.LANCHOR2
	ldr	x0, [x0,2048]
	add	x5, x0, x26
	ldr	w0, [x5,4]
	str	w0, [x29,156]
.L1803:
	ldrh	w1, [x19]
	mov	w28, 0
	ldr	x0, [x25,#:lo12:.LANCHOR2]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	bl	FtlGcPageVarInit
	b	.L1804
.L1820:
	ldr	w0, [x29,152]
	add	w21, w21, 1
	add	w3, w28, 1
	cmp	w21, w0
	uxth	w28, w3
	bcs	.L1806
.L1808:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2018]
	cmp	w0, w28
	bhi	.L1807
	b	.L1811
.L1806:
	ldr	x0, [x29,144]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3168]
	cmp	w0, w22
	beq	.L1808
	add	w0, w0, w21
	strh	w0, [x1,3168]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2018]
	cmp	w0, w28
	bls	.L1808
	b	.L1809
.L1811:
	mov	w7, 0
.L1792:
	ldr	x0, [x29,144]
	mov	w1, w28
	strh	w28, [x19,2]
	mov	w2, w7
	add	x20, x0, :lo12:.LANCHOR1
	mov	w0, -1
	strb	w7, [x19,6]
	strh	w0, [x20,3168]
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
.L1809:
	ldr	w0, [x29,156]
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
	cbz	w4, .L1822
	bl	FlashProgSlc2KPages
	b	.L1823
.L1822:
	sub	w0, w5, #1
	mov	w27, 56
	mov	x28, 24
	str	w0, [x29,116]
.L1874:
	cmp	w22, w25
	bcs	.L1875
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
	bcc	.L1824
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1825
.L1824:
	ldrb	w1, [x2,1937]
	add	x3, x2, 1652
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x0, x1, x28, x3
	ldr	x0, [x0,8]
	cbz	x0, .L1827
	cmp	w4, 1
	bne	.L1828
	ldr	x0, [x2,128]
	bl	NandcIqrWaitFlashReady
.L1828:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1827:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	add	x0, x0, 1652
	ldr	w1, [x29,128]
	madd	x0, x2, x28, x0
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1829
	add	w1, w22, 1
	umull	x1, w1, w27
	add	x1, x19, x1
	str	x1, [x0,16]
.L1829:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, x2
	madd	x2, x2, x28, x1
	ldrb	w20, [x0,1848]
	ldrb	w0, [x1,1845]
	strb	w20, [x2,1652]
	cmp	w0, 1
	bne	.L1830
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1831
.L1830:
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
.L1831:
	ldr	w0, [x29,116]
	cmp	w0, 6
	bhi	.L1832
	add	x2, x21, :lo12:.LANCHOR0
	add	x0, x2, x20, sxtw
	ldrb	w0, [x0,1616]
	cbz	w0, .L1832
	ldrb	w1, [x2,761]
	mov	w0, w20
	add	x2, x2, 764
	mov	w3, 0
	bl	HynixSetRRPara
.L1832:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1833
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1833
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1834
.L1833:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1834:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldrb	w2, [x29,124]
	mov	w0, w20
	ldr	x3, [x26,8]
	mov	w1, 1
	ldr	x4, [x26,16]
	bl	NandcXferData
	cbz	w24, .L1835
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
.L1835:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1825:
	add	w22, w22, 1
	b	.L1874
.L1875:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	add	x22, x21, 1652
	mov	x24, 24
	ldr	x0, [x21,128]
	bl	NandcIqrWaitFlashReady
.L1837:
	ldrb	w0, [x21,1845]
	cmp	w0, w20
	bls	.L1876
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1838
	ldrb	w0, [x21,120]
	cbz	w0, .L1838
	mul	x0, x20, x24
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L1838:
	add	x20, x20, 1
	b	.L1837
.L1876:
	ldr	w0, [x29,120]
	cbnz	w0, .L1840
.L1848:
	mov	w0, 0
	b	.L1823
.L1840:
	adrp	x24, .LC105
	adrp	x26, .LC104
	adrp	x27, .LC103
	mov	w21, 0
	add	x24, x24, :lo12:.LC105
	add	x26, x26, :lo12:.LC104
	add	x27, x27, :lo12:.LC103
.L1841:
	cmp	w21, w25
	beq	.L1848
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1842
	adrp	x0, .LC102
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC102
	bl	printk
	b	.L1843
.L1842:
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
	bne	.L1844
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1844:
	ldr	x0, [x19,16]
	cbz	x0, .L1845
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,128]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1845
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1845:
	ldr	x0, [x19,8]
	cbz	x0, .L1843
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,120]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1843
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1843:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1841
.L1823:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FlashTestBlk.part.15, %function
FlashTestBlk.part.15:
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
	.size	FlashTestBlk.part.15, .-FlashTestBlk.part.15
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
	bcc	.L1879
	mov	w0, w1
	bl	FlashTestBlk.part.15
.L1879:
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
	adrp	x0, .LC114
	mov	w1, 1
	add	x0, x0, :lo12:.LC114
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
.L1883:
	ldrb	w0, [x19,1845]
	cmp	w0, w21
	bls	.L1934
	sxtw	x25, w21
	add	x0, x20, :lo12:.LANCHOR4
	add	x1, x0, x25, lsl 1
	ldrh	w22, [x1,144]
	cbnz	w22, .L1913
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
.L1885:
	uxth	w0, w26
	str	w0, [x29,144]
	cmp	w0, w23
	bcs	.L1895
	mov	w0, -1
	strb	w0, [x29,182]
	strb	w0, [x29,183]
	ldr	w0, [x29,116]
	cbz	w0, .L1887
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
	bne	.L1887
	ldr	w1, [x19,12]
	mov	w0, w27
	add	x2, x29, 183
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,182]
	ldrb	w1, [x29,183]
	and	w0, w1, w0
	strb	w0, [x29,182]
.L1887:
	ldr	w0, [x29,120]
	cbz	w0, .L1889
	ldr	x0, [x19,744]
	add	x2, x29, 183
	ldrh	w1, [x0,10]
	ldr	w0, [x28,x25,lsl 2]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w27
	add	w1, w1, w22
	bl	FlashReadSpare
.L1889:
	ldr	x0, [x19,744]
	ldrb	w1, [x29,182]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1914
	cmp	w0, 1
	bne	.L1890
.L1914:
	mov	w0, 1
	cbz	w1, .L1892
	ldrb	w0, [x29,183]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1892
.L1890:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1892
	ldrb	w0, [x29,183]
	cmp	w0, 255
	cset	w0, ne
.L1892:
	ldr	x1, [x29,128]
	tbz	x1, 2, .L1893
	ldr	w1, [x28,x25,lsl 2]
	mov	w0, w27
	add	w1, w22, w1
	bl	SandiskProgTestBadBlock
.L1893:
	cbz	w0, .L1894
	adrp	x0, .LC115
	mov	w1, w21
	mov	w2, w26
	add	x0, x0, :lo12:.LC115
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
	bgt	.L1895
.L1894:
	ldr	w0, [x29,172]
	add	w26, w26, 1
	add	w22, w22, w0
	b	.L1885
.L1895:
	adrp	x0, .LC116
	mov	w1, w21
	add	x0, x0, :lo12:.LC116
	mov	w2, w24
	bl	printk
	ldrb	w0, [x19,1845]
	ldr	w1, [x29,168]
	mul	w0, w1, w0
	cmp	w24, w0
	blt	.L1897
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w2, [x19,188]
	mov	w1, 0
	lsl	w2, w2, 9
	ldr	x0, [x0,56]
	bl	ftl_memset
.L1897:
	cbnz	w21, .L1899
	add	x0, x20, :lo12:.LANCHOR4
	adrp	x26, .LC117
	mov	w22, w21
	add	x26, x26, :lo12:.LC117
	mov	w24, 1
	ldrh	w27, [x0,92]
.L1900:
	ldrb	w0, [x19,9]
	cmp	w0, w27
	bls	.L1935
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1901
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
.L1901:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1900
.L1935:
	adrp	x26, .LC117
	ldr	w24, [x29,124]
	sub	w27, w23, #50
	add	x26, x26, :lo12:.LC117
.L1903:
	cmp	w24, w27
	ble	.L1936
	mov	w0, w24
	bl	FlashTestBlk
	cbz	w0, .L1904
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
.L1904:
	sub	w2, w24, #1
	uxth	w24, w2
	b	.L1903
.L1936:
	add	x1, x20, :lo12:.LANCHOR4
	ldrb	w0, [x19,9]
	ldr	w2, [x1,92]
	sub	w0, w0, w2
	cmp	w22, w0
	bcc	.L1899
	ldrh	w2, [x19,188]
	ldr	x0, [x1,56]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L1899:
	adrp	x24, .LC118
	ldr	w27, [x29,124]
	mul	w26, w21, w23
	add	x24, x24, :lo12:.LC118
	lsl	x25, x25, 1
	mov	w22, -3872
.L1907:
	mov	w2, w27
	mov	x0, x24
	mov	w1, w21
	bl	printk
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x2, [x0,56]
.L1908:
	ubfx	x0, x27, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w27
	and	w1, w0, 1
	tbz	x0, 0, .L1937
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1908
.L1937:
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
	cbz	w0, .L1913
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1907
.L1913:
	add	w21, w21, 1
	uxtb	w21, w21
	b	.L1883
.L1934:
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
	ldrb	w0, [x24,1937]
	ldr	w2, [x1,-96]
	str	w0, [x29,120]
	mov	w0, 0
	cbnz	w2, .L1939
	ldrb	w0, [x24,120]
	mov	w28, w2
	str	w0, [x29,124]
	mov	w21, w2
	str	w26, [x1,2036]
	mov	w19, w2
	mov	w27, 56
.L1940:
	ldrh	w0, [x24,1948]
	cmp	w0, w28
	bls	.L1984
	add	x1, x20, :lo12:.LANCHOR2
	umull	x0, w28, w27
	ldr	x1, [x1,-88]
	str	wzr, [x1,x0]
	add	x0, x24, 1976
	mov	w1, w26
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	uxth	w23, w0
	mov	w1, w23
	cbz	w25, .L1941
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L1942
.L1941:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1943
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
	ldrh	w0, [x24,2030]
	mul	w0, w21, w0
	add	w21, w21, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1942
.L1943:
	add	w19, w19, 1
	uxth	w19, w19
.L1942:
	add	w2, w28, 1
	uxth	w28, w2
	b	.L1940
.L1984:
	cbz	w21, .L1963
	ldr	w0, [x29,124]
	mov	w2, w21
	strb	wzr, [x24,1937]
	mov	x27, 56
	cmp	w0, wzr
	add	x0, x20, :lo12:.LANCHOR2
	cset	w23, ne
	mov	w1, w23
	ldr	x0, [x0,-88]
	bl	FlashEraseBlocks
	ldrb	w0, [x29,120]
	strb	w0, [x24,1937]
	mov	x24, 0
.L1946:
	cmp	w21, w24, uxth
	bls	.L1985
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x24, x27
	ldr	x1, [x1,-88]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1947
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1947:
	add	x24, x24, 1
	b	.L1946
.L1985:
	cbnz	w25, .L1949
	mov	w0, 1
	uxth	w23, w23
	mov	w28, 6
	str	w0, [x29,124]
	b	.L1950
.L1949:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,2020]
	ldrb	w0, [x0,120]
	str	w1, [x29,124]
	cbnz	w0, .L1966
	uxtw	x0, w1
	mov	w23, 1
	lsr	w28, w0, 2
	b	.L1950
.L1966:
	mov	w23, 1
	mov	w28, w23
.L1950:
	mov	w24, 0
	add	x22, x22, :lo12:.LANCHOR0
.L1959:
	mov	w27, 0
	mov	w21, w27
.L1951:
	ldrh	w0, [x22,1948]
	cmp	w0, w27
	bls	.L1986
	add	x1, x20, :lo12:.LANCHOR2
	mov	w0, 56
	umull	x0, w27, w0
	ldr	x1, [x1,-88]
	str	wzr, [x1,x0]
	add	x0, x22, 1976
	mov	w1, w26
	ldrb	w0, [x0,w27,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w25, .L1952
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L1953
.L1952:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1953
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
	ldrh	w0, [x22,2030]
	mul	w0, w21, w0
	add	w21, w21, 1
	sdiv	w0, w0, w3
	uxth	w21, w21
	add	x0, x2, x0, sxtw 2
	str	x0, [x1,16]
.L1953:
	add	w4, w27, 1
	uxth	w27, w4
	b	.L1951
.L1986:
	cbz	w21, .L1963
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w21
	mov	w2, w23
	mov	w3, 1
	strb	wzr, [x22,1937]
	ldr	x0, [x0,-88]
	bl	FlashProgPages
	ldrb	w0, [x29,120]
	mov	x1, 0
	strb	w0, [x22,1937]
.L1956:
	cmp	w21, w1, uxth
	bls	.L1987
	add	x2, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x1, x0
	ldr	x2, [x2,-88]
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L1957
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L1957:
	add	x1, x1, 1
	b	.L1956
.L1987:
	add	w24, w24, w28
	ldr	w0, [x29,124]
	uxth	w24, w24
	cmp	w24, w0
	bcc	.L1959
	mov	x22, 0
	mov	x24, 56
.L1960:
	cmp	w21, w22, uxth
	bls	.L1988
	cbz	w25, .L1961
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x22, x24
	ldr	x1, [x1,-88]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L1961
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1961:
	add	x22, x22, 1
	b	.L1960
.L1988:
	cmp	w26, 63
	bls	.L1967
	cbz	w25, .L1963
.L1967:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, w21
	ldr	x0, [x20,-88]
	bl	FlashEraseBlocks
.L1963:
	mov	w0, w19
.L1939:
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
	cbnz	w1, .L1991
	adrp	x19, .LANCHOR4
	adrp	x20, .LANCHOR0
	add	x24, x19, :lo12:.LANCHOR4
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x2,2072]
	mov	x21, 0
	ldr	x2, [x2,2120]
	add	x23, x20, 2104
	str	x2, [x24,176]
	ldrh	w2, [x20,2028]
	str	x0, [x24,168]
	bl	ftl_memset
.L1992:
	ldrh	w1, [x20,1970]
	mov	w0, w21
	add	x21, x21, 1
	cmp	w0, w1
	bge	.L2000
	add	x1, x22, :lo12:.LANCHOR2
	ldr	x3, [x24,168]
	ldrh	w2, [x1,2248]
	add	x1, x23, x21, lsl 3
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L1992
.L2000:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 255
	add	x19, x19, 160
	mov	w2, 16
	mov	w24, 0
	adrp	x26, .LC119
	ldr	x21, [x19,16]
	adrp	x27, .LC120
	mov	w25, w24
	add	x26, x26, :lo12:.LC119
	mov	x0, x21
	add	x27, x27, :lo12:.LC120
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
	ldr	w0, [x20,1944]
	strh	w0, [x21,12]
.L1994:
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
	ldrh	w0, [x20,2020]
	ldrh	w1, [x20,2106]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L1995
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
.L1995:
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
	bne	.L1996
	ldr	w1, [x19,4]
	mov	x0, x27
	add	w24, w24, 1
	bl	printk
	uxth	w24, w24
	cmp	w24, 3
	bls	.L1994
	adrp	x0, .LC121
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC121
	mov	w2, w24
	add	x22, x22, :lo12:.LANCHOR2
	bl	printk
	mov	w0, 1
	str	w0, [x22,-96]
	b	.L1991
.L1996:
	cbnz	w25, .L1991
	mov	w25, 1
	b	.L1994
.L1991:
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
	cbnz	w1, .L2002
	mov	x20, x0
	add	x23, x19, 136
	adrp	x21, .LANCHOR0
	mov	w24, 7
	add	x25, x19, 24
.L2003:
	cmp	x20, x23
	bne	.L2004
	ldrh	w2, [x19,32]
	ldr	w4, [x19,860]
	lsr	w3, w2, 1
	add	w1, w3, 1
	mul	w0, w2, w4
	add	w1, w1, w0, lsr 2
	add	x0, x21, :lo12:.LANCHOR0
	uxth	w1, w1
	ldr	w0, [x0,1940]
	cbz	w0, .L2005
	ldr	w0, [x19,256]
	cmp	w0, 39
	bhi	.L2005
	cmp	w0, 2
	mov	w1, 0
	bls	.L2006
	tbz	x2, 0, .L2033
	cbz	w4, .L2006
.L2033:
	mov	w1, w3
	b	.L2005
.L2004:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L2006
	add	x2, x21, :lo12:.LANCHOR0
	ldrh	w0, [x2,1968]
	cmp	w0, 1
	beq	.L2006
	ldrb	w0, [x2,120]
	cbnz	w0, .L2006
	ldr	w2, [x2,1940]
	ldrh	w0, [x19,32]
	lsr	w1, w0, 3
	cbz	w2, .L2005
	ldr	w2, [x19,256]
	cmp	w2, 1
	bhi	.L2005
	mul	w1, w0, w24
	lsr	w1, w1, 3
.L2005:
	cbz	w1, .L2006
	sub	w1, w1, #1
	uxth	w1, w1
.L2006:
	ldrb	w2, [x20,8]
	mov	x0, x25
	add	x27, x21, :lo12:.LANCHOR0
	bl	List_pop_index_node
	uxth	w28, w0
	ldrh	w0, [x19,32]
	sub	w0, w0, #1
	strh	w0, [x19,32]
	ldrh	w0, [x27,1956]
	cmp	w0, w28
	bls	.L2003
	ubfiz	x26, x28, 1, 16
	ldr	x0, [x19]
	ldrh	w0, [x0,x26]
	cbnz	w0, .L2003
	strh	w28, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L2058
	mov	x0, 0
	ldrh	w5, [x27,1948]
	mov	x3, 56
	mov	w27, w0
	mov	w6, 65535
.L2010:
	cmp	w5, w0, uxth
	bls	.L2061
	ldr	x4, [x19,-88]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L2011
	umull	x2, w27, w3
	add	w27, w27, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w27, w27
	str	w1, [x2,4]
.L2011:
	add	x0, x0, 1
	b	.L2010
.L2061:
	add	x0, x19, 40
	cmp	x20, x0
	bne	.L2013
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w1, [x1,1940]
	cbz	w1, .L2013
	ldr	x1, [x19,-80]
	ldrh	w1, [x1,x26]
	cmp	w1, 40
	bls	.L2013
	strb	wzr, [x19,48]
.L2013:
	ldrb	w0, [x20,8]
	cbnz	w0, .L2014
	ldr	x0, [x19,-80]
	mov	w1, 2
	ldrh	w2, [x0,x26]
	cbz	w2, .L2059
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w1, [x1,2008]
	add	w1, w2, w1
.L2059:
	strh	w1, [x0,x26]
	mov	w1, 0
	ldr	w0, [x19,240]
	add	w0, w0, 1
	str	w0, [x19,240]
	mov	w0, w28
	bl	ftl_set_blk_mode
	b	.L2017
.L2014:
	ldr	x1, [x19,-80]
	ldrh	w0, [x1,x26]
	add	w0, w0, 1
	strh	w0, [x1,x26]
	ldr	w0, [x19,244]
	add	w0, w0, 1
	str	w0, [x19,244]
	mov	w0, w28
	bl	ftl_set_blk_mode.part.9
.L2017:
	ldr	x0, [x19,-80]
	ldr	w1, [x19,252]
	ldrh	w0, [x0,x26]
	cmp	w0, w1
	bls	.L2018
	str	w0, [x19,252]
.L2018:
	add	x2, x21, :lo12:.LANCHOR0
	ldr	w3, [x19,240]
	ldr	w0, [x19,244]
	ldrh	w1, [x2,2008]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,1956]
	mov	x3, 56
	udiv	w0, w0, w1
	ldr	x1, [x19,2160]
	str	w0, [x19,248]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L2019:
	cmp	w27, w0, uxth
	bls	.L2062
	ldr	x1, [x19,-88]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L2019
.L2062:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L2021
	ldrb	w0, [x20,8]
	cmp	w0, 1
	ldr	x0, [x19,-88]
	bne	.L2022
	mov	w1, 0
	b	.L2060
.L2022:
	mov	w1, 1
.L2060:
	mov	w2, w27
	bl	FlashEraseBlocks
.L2021:
	ldrb	w1, [x20,8]
	mov	w2, w27
	ldr	x0, [x19,-88]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	x4, 56
	mov	w2, w1
.L2023:
	cmp	w27, w1, uxth
	bls	.L2063
	mul	x0, x1, x4
	ldr	x3, [x19,-88]
	add	x5, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	bne	.L2024
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
.L2024:
	add	x1, x1, 1
	b	.L2023
.L2063:
	cbz	w2, .L2026
	mov	w0, w28
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2026:
	ldrb	w0, [x20,7]
	cbnz	w0, .L2027
.L2058:
	ldr	x0, [x19]
	mov	w1, -1
	strh	w1, [x0,x26]
	b	.L2003
.L2027:
	adrp	x1, .LANCHOR0
	add	x2, x22, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x1,2018]
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
.L2002:
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
	cbz	w0, .L2066
	adrp	x22, .LANCHOR0
	adrp	x24, .LC122
	add	x22, x22, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LC122
.L2074:
	ldrh	w0, [x22,1948]
	cmp	w0, w21
	bls	.L2075
	add	x0, x22, 1976
	mov	w1, w23
	mov	w20, 0
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w25, w0
.L2067:
	ldrh	w0, [x19,944]
	cmp	w0, w20
	bls	.L2076
	add	x0, x19, 952
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w25
	bne	.L2068
	mov	w1, w25
	mov	x0, x24
	bl	printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,944]
	mov	w3, w20
.L2069:
	cmp	w3, w2
	bcs	.L2077
	add	x4, x19, 952
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2069
.L2077:
	sub	w2, w2, #1
	strh	w2, [x19,944]
.L2068:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2067
.L2076:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2074
.L2075:
	bl	FtlGcReFreshBadBlk
.L2066:
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
	cbnz	w2, .L2079
	ldrh	w0, [x1,280]
	cmp	w0, w19
	bne	.L2080
	mov	w0, -1
	strh	w0, [x1,280]
	b	.L2081
.L2080:
	ldrh	w3, [x1,40]
	mov	w0, w2
	cmp	w3, w19
	beq	.L2082
	ldrh	w3, [x1,88]
	cmp	w3, w19
	beq	.L2082
	ldrh	w1, [x1,136]
	cmp	w1, w19
	beq	.L2082
.L2081:
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
	b	.L2082
.L2079:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2082:
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
	beq	.L2087
	ubfiz	x21, x19, 1, 16
	ldr	x0, [x20,#:lo12:.LANCHOR2]
	add	x22, x20, :lo12:.LANCHOR2
	ldrh	w2, [x0,x21]
	cbnz	w2, .L2088
	adrp	x0, .LC123
	mov	w1, w19
	add	x0, x0, :lo12:.LC123
	add	x23, x22, 24
	bl	printk
	ldr	x0, [x20,#:lo12:.LANCHOR2]
	mov	w1, 32
	strh	w1, [x0,x21]
	mov	x0, x23
	mov	w1, w19
	bl	test_node_in_list
	cbz	w0, .L2089
	mov	w1, w19
	mov	x0, x23
	bl	List_remove_node
	ldrh	w0, [x22,32]
	sub	w0, w0, #1
	strh	w0, [x22,32]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x2, [x20,#:lo12:.LANCHOR2]
	adrp	x0, .LC124
	add	x0, x0, :lo12:.LC124
	mov	w1, w19
	ldrh	w2, [x2,x21]
	bl	printk
.L2089:
	mov	w0, w19
	bl	FtlGcRefreshBlock
	b	.L2096
.L2088:
	sub	w2, w2, #1
	strh	w2, [x0,x21]
.L2087:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w1, [x20,2336]
	cmp	w1, w0
	bne	.L2091
	strh	w19, [x20,2336]
.L2096:
	mov	w0, 0
	b	.L2090
.L2091:
	cmp	w1, w19
	mov	w0, 0
	beq	.L2090
	mov	w0, w1
	bl	update_vpc_list
	cmp	w0, wzr
	cset	w0, ne
	strh	w19, [x20,2336]
.L2090:
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
	cbz	w1, .L2097
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2097
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR2
	adrp	x22, .LANCHOR0
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR2
	add	x21, x21, 988
	ldrh	w0, [x19,x0,lsl 1]
.L2101:
	cmp	w0, w20
	bne	.L2108
.L2103:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1948]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2102
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2102:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2101
.L2108:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2104
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2104
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w20
	bne	.L2104
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2103
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2097
.L2104:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,120]
	cbz	w2, .L2097
	cmp	w1, 1
	bne	.L2097
	ldrh	w2, [x19,2]
	ldrh	w1, [x0,2020]
	cmp	w2, w1
	bcc	.L2097
	adrp	x2, .LANCHOR2
	ldrh	w1, [x19]
	ldrh	w4, [x19,4]
	lsl	x1, x1, 1
	ldr	x3, [x2,#:lo12:.LANCHOR2]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,2018]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2097:
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
.L2110:
	cmp	w0, w22
	adrp	x20, .LANCHOR0
	bne	.L2125
.L2111:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1948]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2112
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2112:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2110
.L2125:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2113
	add	x1, x23, :lo12:.LANCHOR0
	ldrb	w1, [x1,120]
	cbnz	w1, .L2113
	ldrh	w1, [x19,2]
	ldrh	w1, [x21,w1,sxtw 1]
	cmp	w1, w22
	bne	.L2113
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2111
.L2113:
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
.L2114:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,1948]
.L2116:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2115
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2115:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2116
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2121
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2118
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w24
	bne	.L2118
	ldrh	w1, [x19,4]
	cbz	w1, .L2118
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2114
.L2118:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,120]
	cbz	w0, .L2121
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,2020]
	cmp	w1, w0
	bcc	.L2121
	adrp	x0, .LANCHOR2
	ldrh	w1, [x19]
	ldrh	w3, [x19,4]
	lsl	x1, x1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR2]
	ldrh	w2, [x0,x1]
	sub	w2, w2, w3
	strh	w2, [x0,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x20,2018]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2121:
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
	cbnz	w0, .L2128
	adrp	x23, .LANCHOR4
	ldr	x21, [x19,2120]
	add	x20, x23, :lo12:.LANCHOR4
	ldr	x1, [x19,2072]
	ldrh	w2, [x19,264]
	adrp	x24, .LANCHOR0
	add	x25, x24, :lo12:.LANCHOR0
	sub	x26, x19, #64
	str	x1, [x20,168]
	add	x20, x20, 160
	str	x21, [x20,16]
	strh	w2, [x21,2]
	mov	w2, -3932
	strh	w2, [x21]
	str	w0, [x21,12]
	ldr	w2, [x19,272]
	str	w2, [x21,4]
	str	w0, [x21,8]
	mov	w0, 19539
	ldrh	w1, [x19,40]
	movk	w0, 0x4654, lsl 16
	strh	w1, [x19,-50]
	ldrh	w1, [x19,42]
	ldrb	w2, [x19,46]
	str	w0, [x19,-64]
	mov	w0, 80
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
	ldrh	w0, [x25,1970]
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
	ldrh	w2, [x25,2028]
	ldr	x0, [x20,8]
	strh	w1, [x19,-40]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x20,8]
	mov	x1, x26
	mov	w2, 48
	bl	ftl_memcpy
	ldr	x0, [x20,8]
	ldrh	w2, [x25,1956]
	ldr	x1, [x22,#:lo12:.LANCHOR2]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x25,1956]
	ldr	x1, [x20,8]
	uxth	x0, w2
	lsr	w2, w2, 3
	add	x0, x0, 24
	add	w2, w2, 4
	lsr	x0, x0, 1
	add	x0, x1, x0, lsl 2
	ldr	x1, [x24,#:lo12:.LANCHOR0]
	bl	ftl_memcpy
	ldrh	w0, [x25,2064]
	cbz	w0, .L2129
	ldrh	w0, [x25,1956]
	ldrh	w2, [x25,2056]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x20,8]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,2216]
	bl	ftl_memcpy
.L2129:
	mov	w0, 0
	mov	w20, 0
	mov	w27, 65535
	add	x28, x23, :lo12:.LANCHOR4
	add	x19, x22, :lo12:.LANCHOR2
	bl	FtlUpdateVaildLpn
.L2130:
	ldrh	w1, [x19,264]
	ldr	x0, [x19,2072]
	ldrh	w2, [x19,266]
	str	x0, [x28,168]
	ldr	x0, [x19,2120]
	str	x0, [x28,176]
	orr	w0, w2, w1, lsl 10
	str	w0, [x28,164]
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L2131
	ldrh	w27, [x19,268]
	strh	wzr, [x19,266]
	strh	w1, [x19,268]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,232]
	str	w1, [x19,272]
	add	w2, w1, 1
	str	w2, [x19,232]
	ubfiz	w2, w0, 10, 16
	str	w2, [x28,164]
	strh	w0, [x19,264]
	str	w1, [x21,4]
	strh	w0, [x21,2]
.L2131:
	mov	w1, 1
	add	x0, x28, 160
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,266]
	ldr	w2, [x28,160]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x19,266]
	bne	.L2132
	cmp	w1, 1
	bne	.L2133
	add	x1, x24, :lo12:.LANCHOR0
	ldrh	w1, [x1,2020]
	sub	w1, w1, #1
	strh	w1, [x19,266]
.L2133:
	add	w20, w20, 1
	uxth	w20, w20
	cmp	w20, 3
	bls	.L2130
	add	x23, x23, :lo12:.LANCHOR4
	adrp	x0, .LC125
	add	x0, x0, :lo12:.LC125
	mov	w2, w20
	add	x22, x22, :lo12:.LANCHOR2
	ldr	w1, [x23,164]
	bl	printk
	mov	w0, 1
	str	w0, [x22,-96]
	b	.L2128
.L2132:
	cmp	w1, 1
	beq	.L2130
	cmp	w2, 256
	beq	.L2130
	mov	w0, 65535
	cmp	w27, w0
	beq	.L2128
	mov	w0, w27
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L2128:
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
	cbnz	w1, .L2139
	adrp	x21, .LANCHOR0
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w22, [x2,120]
	cbz	w22, .L2154
	ldrb	w22, [x0,8]
	cmp	w22, 1
	bne	.L2150
	ldrh	w24, [x0,4]
	b	.L2141
.L2150:
	mov	w22, w1
.L2154:
	mov	w24, 12
.L2141:
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
.L2142:
	sub	w24, w24, #1
	cmn	w24, #1
	beq	.L2145
	ldrh	w0, [x19,4]
	cbnz	w0, .L2143
.L2145:
	ldrh	w0, [x19]
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x2, [x20,#:lo12:.LANCHOR2]
	lsl	x0, x0, 1
	ldrh	w3, [x19,4]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x21,2018]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	b	.L2139
.L2143:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2145
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
	b	.L2142
.L2139:
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
	blt	.L2156
	ubfiz	x0, x0, 1, 16
	ldrh	w24, [x20,x0]
	cbz	w24, .L2156
	ldr	w1, [x19,52]
	cbnz	w1, .L2156
	mov	w2, 1
	str	w2, [x19,52]
	strh	w1, [x20,x0]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	cmp	w1, w0
	bcc	.L2157
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2157:
	adrp	x20, .LANCHOR4
	mov	w21, 0
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x26, .LANCHOR2
	add	x20, x20, 160
.L2158:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L2167
	ubfiz	x0, x21, 2, 16
	add	x27, x25, x0
	ldr	w1, [x25,x0]
	cmp	w24, w1, lsr 10
	bne	.L2159
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
	bne	.L2160
.L2162:
	adrp	x1, .LANCHOR4
	str	wzr, [x27]
	add	x1, x1, :lo12:.LANCHOR4
	adrp	x0, .LC126
	ldrh	w2, [x23,8]
	add	x0, x0, :lo12:.LC126
	ldr	w1, [x1,164]
	bl	printk
	adrp	x0, .LANCHOR2
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR2
	str	w1, [x0,-96]
	b	.L2161
.L2160:
	ldrh	w0, [x23,8]
	cmp	w0, w21
	bne	.L2162
	ldrh	w1, [x23]
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L2162
	ldr	x2, [x20,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L2159:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2158
.L2167:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L2156:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x22,2020]
	cmp	w1, w0
	bcc	.L2161
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2161:
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
	cbnz	w20, .L2169
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2170
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
	b	.L2169
.L2170:
	ubfiz	x0, x0, 1, 16
	ldrh	w22, [x21,x0]
	adrp	x21, .LANCHOR4
	ldrh	w0, [x19,2]
	add	x1, x21, :lo12:.LANCHOR4
	orr	w0, w0, w22, lsl 10
	str	w0, [x1,164]
	ldr	x0, [x3,2072]
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
	ldr	x0, [x3,2072]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w1, w20
.L2171:
	ldrh	w0, [x19,6]
	cmp	w0, w1
	bls	.L2174
	ubfiz	x3, x1, 2, 32
	ldr	w0, [x24,x3]
	cmp	w22, w0, lsr 10
	bne	.L2172
	add	x2, x23, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x4, [x2,2072]
	ubfiz	x0, x20, 3, 16
	str	w1, [x4,x0]
	ldr	x2, [x2,2072]
	ldr	w3, [x24,x3]
	add	x0, x2, x0
	str	w3, [x0,4]
.L2172:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2171
.L2174:
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
.L2169:
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
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	mov	x19, x0
	stp	x25, x26, [sp,64]
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x25, .LC127
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w1
	mov	x27, x2
	mov	w22, 0
	adrp	x28, .LANCHOR2
	add	x20, x20, 160
	add	x25, x25, :lo12:.LC127
.L2176:
	add	x1, x28, :lo12:.LANCHOR2
	adrp	x23, .LANCHOR0
	str	x23, [x29,104]
	adrp	x24, .LANCHOR2
	ldr	w0, [x1,216]
	add	w0, w0, 1
	str	w0, [x1,216]
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L2177
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2178
.L2177:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L2178:
	add	x2, x24, :lo12:.LANCHOR2
	ldr	w1, [x2,-96]
	cbnz	w1, .L2179
	ldrh	w3, [x19]
	adrp	x21, .LANCHOR4
	ldr	x0, [x19,16]
	ldrh	w23, [x0,x3,lsl 1]
	ldrh	w0, [x19,2]
	str	x27, [x20,8]
	orr	w0, w0, w23, lsl 10
	str	w0, [x20,4]
	ldr	x0, [x2,2120]
	mov	w2, 16
	str	x0, [x20,16]
	bl	ftl_memset
	ldr	x0, [x20,16]
	ldr	w1, [x19,48]
	str	w1, [x0,4]
	strh	w26, [x0,8]
	ldrh	w1, [x19,4]
	strh	w1, [x0]
	mov	w1, 1
	strh	w23, [x0,2]
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
	bne	.L2180
	ldr	w1, [x20,4]
	mov	x0, x25
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	ldrh	w0, [x19,2]
	cmp	w0, 2
	bhi	.L2181
	ldr	x0, [x29,104]
	add	x23, x0, :lo12:.LANCHOR0
	ldrh	w0, [x23,2020]
	sub	w0, w0, #1
	strh	w0, [x19,2]
.L2181:
	cmp	w22, 3
	bls	.L2176
	add	x21, x21, :lo12:.LANCHOR4
	adrp	x0, .LC128
	add	x0, x0, :lo12:.LC128
	mov	w2, w22
	add	x24, x24, :lo12:.LANCHOR2
	ldr	w1, [x21,164]
	bl	printk
	mov	w0, 1
	str	w0, [x24,-96]
	b	.L2179
.L2180:
	cbz	w1, .L2183
	strh	w23, [x19,60]
.L2183:
	cmp	w0, 1
	beq	.L2184
	cmp	w1, 256
	beq	.L2184
	ldr	w0, [x19,56]
	cbz	w0, .L2185
.L2184:
	str	wzr, [x19,56]
	b	.L2176
.L2185:
	add	x21, x21, :lo12:.LANCHOR4
	ldr	x0, [x19,40]
	ldr	w1, [x21,164]
	str	w1, [x0,w26,uxtw 2]
.L2179:
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
	add	x5, x5, 160
	sub	w0, w23, #1
	ldr	x2, [x1,2072]
	ldr	x26, [x1,2120]
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
.L2191:
	cmp	w25, w23
	bge	.L2207
	ldr	w0, [x29,116]
	sxtw	x28, w25
	cmp	w25, w0
	bne	.L2192
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
	add	x21, x21, 160
.L2193:
	cmp	w19, w24
	bge	.L2207
	ldrh	w0, [x23]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w19, w0, lsl 10
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L2194
	ldrh	w0, [x26,8]
	cmp	w0, w22
	bcs	.L2194
	ldrh	w2, [x26]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2194
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x21,4]
	str	w1, [x20,x0]
.L2194:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2193
.L2207:
	mov	x0, x27
	bl	ftl_free_no_use_map_blk
	adrp	x0, .LANCHOR0+2020
	ldrh	w1, [x27,2]
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2020]
	cmp	w1, w0
	bne	.L2197
	mov	x0, x27
	bl	ftl_map_blk_alloc_new_blk
	b	.L2197
.L2192:
	ldr	x0, [x29,136]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,2072]
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
	beq	.L2208
	ldrh	w1, [x26]
	mov	w4, 0
	ldrh	w0, [x27,4]
	cmp	w1, w0
	bne	.L2217
	ldrh	w1, [x26,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2217
.L2199:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	sub	w0, w0, #1
	cmp	w4, w0
	bge	.L2202
	ldr	x0, [x29,136]
	sbfiz	x1, x4, 3, 32
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x2, [x0,2072]
	ldrh	w0, [x2,x1]
	cmp	w0, w22
	bcs	.L2200
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x1,4]
	str	w1, [x20,x0]
.L2200:
	add	w4, w4, 1
	sxth	w4, w4
	b	.L2199
.L2208:
	mov	w4, 0
.L2217:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	cmp	w4, w0
	bge	.L2202
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
	beq	.L2203
	ldrh	w0, [x26,8]
	cmp	w0, w22
	bcs	.L2203
	ldrh	w2, [x26]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2203
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x19,4]
	str	w1, [x20,x0]
.L2203:
	add	w4, w4, 1
	sxth	w4, w4
	b	.L2217
.L2202:
	add	w6, w25, 1
	sxth	w25, w6
	b	.L2191
.L2197:
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
	ldrh	w21, [x0,2026]
	mov	x25, x2
	ldrh	w0, [x0,2012]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L2221
	adrp	x24, .LANCHOR4
	lsr	w21, w26, w21
	add	x24, x24, :lo12:.LANCHOR4
	mov	w20, 0
	adrp	x27, .LANCHOR2
	add	x24, x24, 224
.L2222:
	cbz	w28, .L2221
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
	beq	.L2224
	cbz	w4, .L2224
	ldr	x0, [x2,2088]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w4, [x29,124]
	str	xzr, [x29,136]
	bl	FlashReadPages
	b	.L2225
.L2224:
	add	x0, x27, :lo12:.LANCHOR2
	add	x2, x23, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x0,2088]
	ldrh	w2, [x2,2028]
	bl	ftl_memset
.L2225:
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
	b	.L2222
.L2221:
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
	bne	.L2235
	mov	w1, 80
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
.L2235:
	ldp	x29, x30, [sp], 16
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x1, .LANCHOR0+1940
	adrp	x2, .LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w3, [x1,#:lo12:.LANCHOR0+1940]
	mov	w1, 32
	cbz	w3, .L2238
	add	x3, x2, :lo12:.LANCHOR2
	ldr	w3, [x3,256]
	cmp	w3, 39
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2238:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,288]
	cmp	w4, 31
	bhi	.L2239
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,288]
.L2239:
	cbnz	w0, .L2240
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,2160]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2241
.L2240:
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
.L2241:
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
	bhi	.L2246
	adrp	x27, .LANCHOR4
	adrp	x0, .LC129
	add	x0, x0, :lo12:.LC129
	add	x27, x27, :lo12:.LANCHOR4
	str	x0, [x29,128]
	lsr	w20, w23, w20
	add	x0, x27, 224
	mov	w21, 0
	str	x0, [x29,120]
.L2247:
	cbz	w22, .L2255
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
	cbz	w3, .L2249
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
	bne	.L2251
	ldr	x0, [x29,128]
	mov	w1, w20
	mov	w2, w3
	bl	printk
	ldr	x4, [x29,112]
	mov	w1, w20
	ldr	x0, [x29,120]
	ldr	x2, [x4,2088]
	bl	FtlMapWritePage
.L2251:
	add	x25, x25, :lo12:.LANCHOR2
	ubfiz	x24, x24, 9, 16
	mov	x0, x26
	mov	w2, w28
	ldr	x1, [x25,2088]
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L2252
.L2249:
	mov	x0, x26
	mov	w1, w3
	mov	w2, w28
	bl	ftl_memset
.L2252:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x26, x26, x28, sxtw
	b	.L2247
.L2255:
	mov	w0, w21
.L2246:
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
	beq	.L2257
	adrp	x0, .LC75
	adrp	x1, .LC130
	add	x1, x1, :lo12:.LC130
	add	x0, x0, :lo12:.LC75
	bl	printk
	ldrh	w2, [x19,2144]
	mov	w1, 0
	ldr	x0, [x19,2160]
	lsl	w2, w2, 9
	bl	ftl_memset
.L2257:
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
	beq	.L2259
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,336]
.L2259:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,336]
	cmp	w3, w2
	bne	.L2260
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
.L2260:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR0
	ldr	w1, [x0,404]
	str	wzr, [x0,2020]
	mov	w0, 34661
	movk	w0, 0x1234, lsl 16
	cmp	w1, w0
	bne	.L2261
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 1
	str	w1, [x0,1940]
	adrp	x0, .LC75
	adrp	x1, .LC131
	add	x0, x0, :lo12:.LC75
	add	x1, x1, :lo12:.LC131
	bl	printk
.L2261:
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x21, [sp,32]
	ldrh	w1, [x20,2008]
	ldr	w2, [x19,240]
	ldr	w0, [x19,244]
	madd	w0, w1, w2, w0
	ldrh	w1, [x20,1956]
	udiv	w0, w0, w1
	str	w0, [x19,248]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	ftl_vendor_read
	.type	ftl_vendor_read, %function
ftl_vendor_read:
	stp	x29, x30, [sp, -16]!
	add	w0, w0, 256
	add	x29, sp, 0
	bl	FtlVendorPartRead
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_vendor_read, .-ftl_vendor_read
	.align	2
	.global	FlashBootVendorRead
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x0, [x29,24]
	str	x1, [x29,32]
	str	x2, [x29,40]
	bl	rknand_device_lock
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	bl	ftl_vendor_read
	str	x0, [x29,40]
	bl	rknand_device_unlock
	ldr	x0, [x29,40]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashBootVendorRead, .-FlashBootVendorRead
	.align	2
	.global	ftl_sys_read
	.type	ftl_sys_read, %function
ftl_sys_read:
	stp	x29, x30, [sp, -16]!
	add	w0, w0, 512
	add	x29, sp, 0
	bl	FtlVendorPartRead
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
	cbz	w1, .L2266
	adrp	x3, .LANCHOR2
	str	wzr, [x0,56]
	add	x3, x3, :lo12:.LANCHOR2
	ldr	w1, [x3,-96]
	cbnz	w1, .L2266
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
	cbz	w3, .L2270
	mov	x0, x1
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L2271
.L2270:
	adrp	x2, .LANCHOR0+2028
	mov	w1, 255
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2028]
	bl	ftl_memset
.L2271:
	add	x2, x19, :lo12:.LANCHOR4
	mov	x0, x21
	mov	w1, w20
	ldr	x2, [x2,168]
	bl	FtlMapWritePage
.L2266:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.type	FlashReadFacBbtData.part.14, %function
FlashReadFacBbtData.part.14:
	stp	x29, x30, [sp, -32]!
	mov	w3, 0
	adrp	x4, .LANCHOR4
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w7, 1
	mov	x19, x0
	cbnz	w1, .L2275
.L2277:
	add	x0, x4, :lo12:.LANCHOR4
	ldr	w1, [x0,92]
	cmp	w3, w1
	bcs	.L2275
	ubfx	x5, x3, 5, 11
	ldr	x0, [x0,56]
	lsl	x5, x5, 2
	lsl	w6, w7, w3
	add	w3, w3, 1
	ldr	w1, [x0,x5]
	uxth	w3, w3
	orr	w6, w1, w6
	str	w6, [x0,x5]
	b	.L2277
.L2275:
	add	x4, x4, :lo12:.LANCHOR4
	mov	x0, x19
	ldr	x1, [x4,56]
	bl	ftl_memcpy
	mov	w2, 4
	adrp	x0, .LC132
	mov	x1, x19
	add	x0, x0, :lo12:.LC132
	mov	w3, w2
	bl	rknand_print_hex
	ldr	x19, [sp,16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadFacBbtData.part.14, .-FlashReadFacBbtData.part.14
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
.L2280:
	cmp	w20, w19
	ble	.L2288
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L2281
	add	x0, x21, :lo12:.LANCHOR4
	ldr	x0, [x0,112]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L2281
	mov	w0, w22
	cbz	x22, .L2282
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.14
	b	.L2282
.L2281:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2280
.L2288:
	mov	w0, -1
.L2282:
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
	bne	.L2290
.L2294:
	mov	w1, 0
	b	.L2291
.L2290:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L2292:
	cmp	w2, w19
	bge	.L2291
	add	x3, x20, :lo12:.LANCHOR4
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,104]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L2293
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L2293:
	cmp	w1, w0
	bcs	.L2294
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2292
.L2291:
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
	cbnz	w21, .L2300
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
.L2301:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22,1970]
	cmp	w21, w0
	bcs	.L2319
	add	x1, x23, :lo12:.LANCHOR2
	ldrh	w28, [x25,12]
	ldr	x0, [x1,2072]
	ldr	x27, [x1,2120]
	mov	w1, 65535
	str	x0, [x20,8]
	cmp	w28, w1
	str	x27, [x20,16]
	beq	.L2302
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
	b	.L2303
.L2302:
	mov	w1, w21
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x22,2014]
.L2305:
	sub	w22, w22, #1
	uxth	w22, w22
.L2304:
	add	x28, x19, :lo12:.LANCHOR0
	ldrh	w0, [x28,2014]
	madd	w0, w21, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2305
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
	bne	.L2303
	mov	w0, w28
	bl	FtlBbmMapBadBlock
	b	.L2304
.L2303:
	mov	w0, w28
	add	w21, w21, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L2301
.L2319:
	mov	w20, 0
.L2308:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,2032]
	cmp	w0, w20
	bls	.L2320
	mov	w0, w20
	add	w20, w20, 1
	bl	FtlBbmMapBadBlock
	uxth	w20, w20
	b	.L2308
.L2320:
	ldrh	w20, [x21,2116]
	mov	w24, 65535
	sub	w20, w20, #1
	uxth	w20, w20
.L2310:
	add	x22, x21, 2104
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L2314
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2311
	mov	w0, w20
	bl	FlashTestBlk
	cbz	w0, .L2312
	mov	w0, w20
	bl	FtlBbmMapBadBlock
	b	.L2311
.L2312:
	ldrh	w0, [x21,2104]
	cmp	w0, w24
	bne	.L2313
	strh	w20, [x21,2104]
	b	.L2311
.L2313:
	strh	w20, [x22,4]
	b	.L2314
.L2311:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2310
.L2314:
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
.L2300:
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
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR0
	add	x3, x24, :lo12:.LANCHOR0
	str	w2, [x29,104]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	mov	x27, x1
	mov	x2, 1
	ldrh	w1, [x3,2026]
	adrp	x20, .LANCHOR2
	add	w1, w1, 7
	lsr	w26, w0, w1
	lsl	x1, x2, x1
	sub	w1, w1, #1
	and	w23, w1, w0
	ldrh	w1, [x3,2058]
	uxth	w25, w26
	uxth	w0, w23
	str	w0, [x29,108]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x21, [x0,184]
	mov	x0, 0
.L2322:
	uxth	x19, w0
	cmp	w19, w1
	bcs	.L2337
	add	x0, x0, 1
	add	x2, x21, x0, lsl 4
	ldrh	w2, [x2,-16]
	cmp	w2, w25
	bne	.L2322
.L2323:
	ldr	w0, [x29,104]
	ldr	w23, [x29,108]
	cbnz	w0, .L2324
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,184]
	add	x1, x1, x19, lsl 4
	ldr	x0, [x1,8]
	ldr	w0, [x0,x23,lsl 2]
	str	w0, [x27]
	b	.L2325
.L2324:
	add	x1, x20, :lo12:.LANCHOR2
	lsl	x0, x19, 4
	ldr	w3, [x27]
	ldr	x2, [x1,184]
	add	x2, x2, x0
	ldr	x2, [x2,8]
	str	w3, [x2,x23,lsl 2]
	strh	w25, [x1,192]
	ldr	x2, [x1,184]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2325:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x20,184]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19,4]
	cmn	w0, #1
	beq	.L2334
	add	w0, w0, 1
	str	w0, [x19,4]
	b	.L2334
.L2337:
	bl	select_l2p_ram_region
	uxth	x19, w0
	ubfiz	x22, x19, 4, 16
	mov	w2, 65535
	add	x1, x21, x22
	ldrh	w3, [x21,x22]
	cmp	w3, w2
	beq	.L2328
	ldr	w1, [x1,4]
	tbz	w1, #31, .L2328
	bl	flush_l2p_region
.L2328:
	add	x21, x20, :lo12:.LANCHOR2
	ubfiz	x0, x25, 2, 16
	ldr	x1, [x21,2216]
	ldr	w23, [x1,x0]
	cbnz	w23, .L2329
	ldr	x0, [x21,184]
	add	x24, x24, :lo12:.LANCHOR0
	mov	w1, 255
	add	x0, x0, x22
	ldrh	w2, [x24,2028]
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x21,184]
	strh	w25, [x0,x22]
	ldr	x0, [x21,184]
	add	x22, x0, x22
	str	w23, [x22,4]
	b	.L2323
.L2329:
	ldr	x0, [x21,184]
	adrp	x5, .LANCHOR4
	add	x5, x5, :lo12:.LANCHOR4
	mov	w1, 1
	add	x0, x0, x22
	add	x28, x5, 160
	mov	w2, w1
	str	x5, [x29,96]
	str	w23, [x5,164]
	ldr	x0, [x0,8]
	str	x0, [x5,168]
	ldr	x0, [x21,2120]
	str	x0, [x5,176]
	mov	x0, x28
	bl	FlashReadPages
	ldr	x0, [x28,16]
	ldr	x5, [x29,96]
	ldrh	w0, [x0,8]
	cmp	w0, w25
	beq	.L2330
	adrp	x0, .LC133
	uxth	w1, w26
	mov	w2, w23
	add	x0, x0, :lo12:.LC133
	add	x24, x24, :lo12:.LANCHOR0
	bl	printk
	mov	w2, 4
	ldr	x1, [x28,16]
	adrp	x0, .LC99
	mov	w3, w2
	add	x0, x0, :lo12:.LC99
	bl	rknand_print_hex
	adrp	x0, .LC134
	ldr	x1, [x21,2216]
	ldrh	w3, [x24,2056]
	add	x0, x0, :lo12:.LC134
	mov	w2, 4
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x21,-96]
	b	.L2331
.L2330:
	ldr	w0, [x5,160]
	cmp	w0, 256
	bne	.L2331
	and	w26, w26, 65535
	adrp	x0, .LC135
	mov	w1, w26
	mov	w2, w23
	add	x0, x0, :lo12:.LC135
	bl	printk
	ldr	x2, [x21,184]
	add	x0, x21, 2256
	mov	w1, w26
	add	x2, x2, x22
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L2331:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x0, [x1,184]
	add	x0, x0, x22
	str	wzr, [x0,4]
	ldr	x0, [x1,184]
	strh	w25, [x0,x22]
	b	.L2323
.L2334:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
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
	cbnz	w0, .L2338
	ldrh	w2, [x23,44]
	cbz	w2, .L2340
	ldrb	w1, [x23,48]
	cbnz	w1, .L2340
	adrp	x21, .LANCHOR0
	ldrb	w1, [x23,47]
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w3, [x21,2018]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L2340
	ldrb	w24, [x23,50]
	ldr	w22, [x21,2096]
	ldrh	w25, [x21,1948]
	cbnz	w24, .L2338
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
	beq	.L2342
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w24
	bl	FlashReadPages
	b	.L2343
.L2342:
	ldr	x0, [x23,2072]
	mov	w1, 255
	ldrh	w2, [x21,2028]
	bl	ftl_memset
.L2343:
	add	x24, x19, :lo12:.LANCHOR2
	mov	w0, -3947
	lsl	w25, w25, 2
	strh	w0, [x20]
	mov	w23, 0
	add	x21, x24, 40
.L2344:
	cmp	w23, w25
	beq	.L2345
	ldrh	w0, [x21,4]
	cbz	w0, .L2345
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
	b	.L2344
.L2345:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,50]
	b	.L2338
.L2340:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,50]
.L2338:
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
.L2356:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2058]
	cmp	w0, w19
	bls	.L2359
	add	x1, x21, :lo12:.LANCHOR2
	ubfiz	x0, x19, 4, 16
	ldr	x1, [x1,184]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2357
	mov	w0, w19
	bl	flush_l2p_region
.L2357:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2356
.L2359:
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
	cbnz	w1, .L2361
	mov	x21, x0
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2362
	ubfiz	x1, x20, 1, 16
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2363
	mov	w0, w20
	bl	INSERT_DATA_LIST
	b	.L2362
.L2363:
	mov	w0, w20
	bl	INSERT_FREE_LIST
.L2362:
	add	x2, x19, :lo12:.LANCHOR2
	strb	wzr, [x21,8]
	add	x0, x2, 88
	cmp	x21, x0
	beq	.L2364
	adrp	x1, .LANCHOR0
	add	x4, x1, :lo12:.LANCHOR0
	ldrh	w3, [x4,1968]
	cmp	w3, 1
	beq	.L2364
	ldrb	w0, [x4,120]
	cbz	w0, .L2365
.L2364:
	mov	w0, 1
	strb	w0, [x21,8]
	b	.L2366
.L2365:
	add	x0, x2, 40
	cmp	x21, x0
	bne	.L2366
	cmp	w3, 3
	beq	.L2368
	ldr	w0, [x2,328]
	cmp	w0, 1
	bne	.L2369
.L2368:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x2,48]
.L2369:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x1,1940]
	cbz	w0, .L2366
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,256]
	cmp	w0, 39
	bhi	.L2366
	mov	w0, 1
	strb	w0, [x1,48]
.L2366:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,2336]
	mov	w0, 65535
	cmp	w1, w0
	beq	.L2371
	cmp	w20, w1
	bne	.L2372
	adrp	x0, .LANCHOR2
	ubfiz	x2, x1, 1, 16
	ldr	x0, [x0,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x2]
	cbz	w0, .L2373
.L2372:
	mov	w0, w1
	bl	update_vpc_list
.L2373:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,2336]
.L2371:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2361:
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	Ftlscanalldata
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	sub	sp, sp, #80
	adrp	x0, .LC136
	add	x0, x0, :lo12:.LC136
	mov	w1, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR4
	mov	w20, 0
	stp	x21, x22, [sp,48]
	add	x19, x19, :lo12:.LANCHOR4
	adrp	x21, .LC137
	add	x21, x21, :lo12:.LC137
	add	x19, x19, 160
	bl	printk
.L2387:
	adrp	x0, .LANCHOR0+2096
	ldr	w0, [x0,#:lo12:.LANCHOR0+2096]
	cmp	w20, w0
	bcs	.L2394
	mov	w0, w20
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	and	w0, w20, 2047
	cbnz	w0, .L2388
	ldr	w2, [x29,60]
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L2388:
	ldr	w0, [x29,60]
	cmn	w0, #1
	beq	.L2390
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
	beq	.L2391
	cmp	w0, 256
	beq	.L2391
	ldr	w0, [x22,8]
	cmp	w0, w20
	beq	.L2390
.L2391:
	adrp	x2, .LANCHOR4
	mov	w1, w20
	add	x2, x2, :lo12:.LANCHOR4
	add	x2, x2, 160
	ldr	x7, [x2,8]
	ldr	x6, [x2,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	adrp	x0, .LC138
	add	x0, x0, :lo12:.LC138
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x2,4]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
.L2390:
	add	w20, w20, 1
	b	.L2387
.L2394:
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
	cbnz	w1, .L2396
	ldr	x19, [x3,24]
	cbz	x19, .L2397
	ldr	x2, [x3,-16]
	mov	w5, 65535
	ldrh	w4, [x3,32]
	mov	x3, -6148914691236517206
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x3, x19, x19
	mov	w3, 6
	uxth	w19, w19
.L2398:
	cmp	w1, w4
	beq	.L2397
	cmp	w19, w0
	bne	.L2399
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
	b	.L2397
.L2399:
	umull	x19, w19, w3
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2397
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2398
.L2396:
	add	w1, w1, 1
	strh	w1, [x4,x22]
.L2397:
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
	beq	.L2561
	ldrh	w0, [x19,2]
	adrp	x23, .LANCHOR0
	str	w0, [x29,188]
	ldrb	w0, [x19,6]
	str	w0, [x29,176]
	add	x0, x23, :lo12:.LANCHOR0
	ldr	w1, [x29,188]
	ldrh	w0, [x0,2018]
	cmp	w0, w1
	bne	.L2407
	strh	wzr, [x19,4]
	b	.L2567
.L2407:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2408:
	cmp	w0, w2
	bne	.L2570
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2408
.L2570:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2410
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L2411
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, w20
	ldrb	w1, [x0,120]
	cbnz	w1, .L2484
	add	x0, x0, 196
	ldrh	w24, [x0,w20,sxtw 1]
	b	.L2484
.L2410:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L2411
	mov	w24, w0
.L2484:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w26, 0
	mov	w8, 65535
	mov	w9, 56
	adrp	x10, .LANCHOR2
	ldrh	w4, [x0,1948]
	mov	w11, 4
	ldrh	w7, [x0,2030]
	add	x4, x19, x4, lsl 1
	b	.L2413
.L2411:
	strh	wzr, [x19,2]
.L2567:
	strb	wzr, [x19,6]
	b	.L2561
.L2415:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2414
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
.L2414:
	add	x2, x2, 2
.L2413:
	cmp	x2, x4
	bne	.L2415
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2416
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2416:
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
	adrp	x22, .LC139
	str	w0, [x29,168]
	add	x22, x22, :lo12:.LC139
	uxth	w0, w24
	str	w0, [x29,160]
.L2417:
	uxth	w2, w27
	cmp	w2, w26
	bcs	.L2424
	add	x6, x21, :lo12:.LANCHOR2
	mul	x0, x27, x28
	ldr	x4, [x6,2048]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2418
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2419
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
	cbz	w0, .L2419
	add	w4, w4, 1
	str	w4, [x6,236]
.L2419:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2420
.L2424:
	cmp	w2, w26
	bne	.L2562
	add	x0, x21, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x0, [x0,2048]
	ldr	w0, [x0,4]
	b	.L2563
.L2418:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w25, [x29,160]
	adrp	x0, .LANCHOR4
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LANCHOR4
	strh	w1, [x0,290]
.L2420:
	add	x27, x27, 1
	b	.L2417
.L2562:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w20, w20
	umull	x2, w2, w1
	ldr	x0, [x0,2048]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2563:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2426
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w2, [x0,120]
	cbnz	w2, .L2426
	add	x0, x0, 196
	ldrh	w20, [x0,w20,sxtw 1]
.L2426:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w0, [x0,2018]
	cmp	w0, w20
	bne	.L2427
	strh	w20, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2427:
	ldrh	w0, [x29,176]
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w20, w0
	bne	.L2428
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2568
.L2428:
	ldr	w0, [x29,168]
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w25, w0
	bne	.L2429
	cbnz	w1, .L2430
.L2429:
	add	x0, x21, :lo12:.LANCHOR2
	uxth	w5, w24
	ldr	w1, [x0,2348]
	cmn	w1, #1
	bne	.L2431
	str	w22, [x0,2348]
.L2431:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w25, [x29,188]
	ldr	w6, [x0,2348]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w24, uxth
	bge	.L2432
	sub	w25, w5, #7
	uxth	w25, w25
.L2432:
	mov	w3, -1
	add	x28, x21, :lo12:.LANCHOR2
	mov	w27, w3
	mov	w4, 65535
	mov	w7, 56
	add	x8, x28, 988
.L2433:
	cmp	w25, w5
	bhi	.L2446
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, 0
	ldrh	w2, [x0,1948]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2447:
	cmp	x0, x2
	beq	.L2571
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2434
	umull	x9, w24, w7
	ldr	x10, [x28,2048]
	add	w24, w24, 1
	orr	w1, w25, w1, lsl 10
	add	x9, x10, x9
	uxth	w24, w24
	str	w1, [x9,4]
.L2434:
	add	x0, x0, 2
	b	.L2447
.L2571:
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
	ldrb	w9, [x0,120]
	ldr	x0, [x28,2048]
	ldr	x4, [x29,144]
	ldr	x7, [x29,136]
	add	x0, x0, 16
	ldr	x8, [x29,128]
.L2436:
	cmp	w1, w24
	beq	.L2572
	ldr	w2, [x0,-16]
	cbnz	w2, .L2437
	ldr	x2, [x0]
	ldrh	w11, [x2]
	cmp	w11, w4
	beq	.L2438
	ldr	w2, [x2,4]
	cmn	w2, #1
	beq	.L2438
	ldr	w27, [x28,2348]
	cmn	w3, #1
	str	w2, [x28,2348]
	bne	.L2438
	ldrh	w2, [x8,x10,lsl 1]
	cmp	w2, w4
	bne	.L2439
	cbz	w9, .L2438
.L2439:
	cmp	w27, w22
	csinv	w3, w27, wzr, ne
	b	.L2438
.L2437:
	adrp	x0, .LANCHOR4+290
	ldrh	w1, [x19]
	strh	w1, [x0,#:lo12:.LANCHOR4+290]
	ldrb	w0, [x19,8]
	cbnz	w0, .L2430
	add	x0, x21, :lo12:.LANCHOR2
	add	x1, x0, 988
	ldrh	w2, [x1,w25,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2441
	cmn	w3, #1
	beq	.L2442
	str	w3, [x0,2348]
	b	.L2430
.L2442:
	cmp	w6, w22
	beq	.L2443
	str	w6, [x0,2348]
	b	.L2430
.L2443:
	ldr	w1, [x0,2348]
	b	.L2569
.L2441:
	cmp	w27, w22
	beq	.L2444
	cmn	w27, #1
	beq	.L2430
	str	w27, [x0,2348]
	b	.L2430
.L2444:
	ldr	w1, [x0,2348]
	cmp	w1, w22
	beq	.L2430
.L2569:
	sub	w1, w1, #1
	b	.L2564
.L2438:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2436
.L2572:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2433
.L2446:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, -1
.L2564:
	str	w1, [x0,2348]
.L2430:
	adrp	x0, .LANCHOR4
	str	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR4
	adrp	x0, .LC141
	add	x0, x0, :lo12:.LC141
	ldr	w24, [x29,188]
	str	x0, [x29,128]
	adrp	x0, .LC140
	mov	w2, 1
	add	x0, x0, :lo12:.LC140
	strh	w2, [x1,292]
	str	x0, [x29,120]
.L2448:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x1, x19
	str	wzr, [x29,176]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,1948]
	ldrb	w6, [x0,120]
	add	x3, x19, x3, lsl 1
.L2449:
	cmp	x1, x3
	beq	.L2573
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2450
	ldr	w0, [x29,176]
	add	x5, x21, :lo12:.LANCHOR2
	orr	w2, w24, w2, lsl 10
	ldr	x4, [x5,2048]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2451
	cbz	w6, .L2451
	ldr	x2, [x5,2048]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2451:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2450:
	add	x1, x1, 2
	b	.L2449
.L2573:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,2048]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2453:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2574
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
	bcc	.L2454
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2488
	ldr	w2, [x29,188]
	cmp	w24, w2
	beq	.L2454
.L2488:
	cmp	w0, w26
	bne	.L2489
	cmp	w24, w20
	beq	.L2456
.L2489:
	ldr	w0, [x28,x25]
	cmn	w0, #1
	beq	.L2458
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	beq	.L2459
	ldrh	w0, [x19]
	b	.L2565
.L2459:
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2460
	add	x27, x21, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,236]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2460
	add	w0, w22, 1
	str	w0, [x27,236]
.L2460:
	ldr	w27, [x28,8]
	add	x1, x29, 200
	ldr	w0, [x28,12]
	mov	w2, 0
	str	w0, [x29,196]
	mov	w0, w27
	bl	log2phys
	add	x3, x21, :lo12:.LANCHOR2
	str	x3, [x29,168]
	ldr	w1, [x3,2348]
	cmn	w1, #1
	beq	.L2461
	mov	w0, w22
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,168]
	cbz	w0, .L2461
	ldr	w1, [x29,196]
	cmn	w1, #1
	beq	.L2462
	ldr	x0, [x3,2048]
	str	x3, [x29,112]
	add	x0, x0, x25
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x2, [x0,16]
	ldr	x0, [x3,2048]
	str	x2, [x29,168]
	mov	w2, 0
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
	bne	.L2463
	b	.L2464
.L2462:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2454
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
	b	.L2454
.L2463:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2464
	ldr	w0, [x3,2348]
	ldr	w1, [x29,144]
	str	x2, [x29,96]
	str	x4, [x29,104]
	str	x3, [x29,112]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	ldr	x2, [x29,96]
	cbz	w0, .L2464
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2469
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2464
	cmn	w0, #1
	beq	.L2467
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,2048]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	b	.L2468
.L2467:
	str	w0, [x2,x25]
.L2468:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
	ldr	w1, [x1,x25]
	cmn	w1, #1
	beq	.L2469
	ldr	x1, [x29,168]
	ldr	w0, [x0,2348]
	ldr	w25, [x1,4]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2469
	ldr	w0, [x29,144]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2464
.L2469:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2464:
	mov	w0, -1
	str	w0, [x29,196]
	b	.L2471
.L2461:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2471
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w25, [x29,200]
	cmn	w25, #1
	beq	.L2471
	ldr	w0, [x29,196]
	cmp	w25, w0
	beq	.L2471
	lsr	x0, x25, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x1, x21, :lo12:.LANCHOR2
	ldrh	w2, [x1,40]
	cmp	w2, w0
	beq	.L2474
	ldrh	w2, [x1,88]
	cmp	w2, w0
	beq	.L2474
	ldrh	w1, [x1,136]
	cmp	w1, w0
	bne	.L2471
.L2474:
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
	beq	.L2471
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2471
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2471:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2454
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x2, x1, 1, 16
	ldr	x3, [x21,#:lo12:.LANCHOR2]
	ldrh	w2, [x3,x2]
	cbz	w2, .L2475
.L2565:
	bl	decrement_vpc_count
	b	.L2454
.L2475:
	ldr	x0, [x29,120]
	bl	printk
	b	.L2454
.L2458:
	ldr	x2, [x29,136]
	ldrh	w0, [x19]
	add	x25, x2, :lo12:.LANCHOR4
	mov	w2, w22
	strh	w0, [x25,290]
	ldr	x0, [x29,128]
	bl	printk
	ldr	w0, [x25,296]
	cmp	w0, 31
	bhi	.L2476
	add	x1, x25, 304
	ldr	w2, [x29,204]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x25,296]
.L2476:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,2348]
	cmn	w1, #1
	beq	.L2566
	cmp	w1, w22
	bls	.L2454
.L2566:
	str	w22, [x0,2348]
.L2454:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2453
.L2574:
	ldrb	w0, [x19,8]
	add	w24, w24, 1
	cmp	w0, 1
	uxth	w24, w24
	bne	.L2479
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,120]
	cbz	w1, .L2479
	cmp	w20, w24
	bne	.L2479
	ldrh	w0, [x0,2020]
	cmp	w0, w24
	beq	.L2456
.L2479:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2018]
	cmp	w24, w1
	bne	.L2448
	ldrh	w2, [x0,1948]
	mov	w3, 65535
	strh	w24, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2481:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2561
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2481
	strb	w1, [x19,6]
	b	.L2561
.L2456:
	strb	w26, [x19,6]
	strh	w20, [x19,2]
.L2568:
	mov	x0, x19
	mov	w1, w20
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2561:
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
	add	x1, x1, 56
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
	ldrh	w2, [x1,1958]
	mov	w1, 0
	ldr	x0, [x0,2168]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2576:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0,2096]
	cmp	w19, w0
	bcs	.L2592
	mov	w0, w19
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2577
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x2, [x1,2168]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2577:
	add	w19, w19, 1
	b	.L2576
.L2592:
	adrp	x23, .LC142
	mov	w19, 0
	add	x20, x20, :lo12:.LANCHOR2
	mov	w24, 65535
	add	x23, x23, :lo12:.LC142
.L2579:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,1956]
	cmp	w0, w19
	bls	.L2593
	ubfiz	x21, x19, 1, 16
	ldr	x0, [x20]
	ldrh	w2, [x0,x21]
	ldr	x0, [x20,2168]
	ldrh	w3, [x0,x21]
	cmp	w2, w3
	beq	.L2582
	cmp	w2, w24
	beq	.L2582
	mov	x0, x23
	mov	w1, w19
	bl	printk
	ldrh	w0, [x20,40]
	cmp	w0, w19
	beq	.L2582
	ldrh	w0, [x20,136]
	cmp	w0, w19
	beq	.L2582
	ldrh	w0, [x20,88]
	cmp	w0, w19
	beq	.L2582
	ldr	x0, [x20]
	ldrh	w1, [x0,x21]
	cbnz	w1, .L2584
	ldr	x1, [x20,2168]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	b	.L2582
.L2584:
	ldr	x1, [x20,2168]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	mov	w0, w19
	bl	update_vpc_list
.L2582:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2579
.L2593:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LANCHOR2
	add	x0, x1, :lo12:.LANCHOR2
	add	x29, sp, 0
	add	x2, x0, 336
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x19, .LANCHOR0
	ldr	w3, [x2,80]
	cbz	w3, .L2595
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w3, [x2,84]
	ldr	w1, [x1,2096]
	cmp	w3, w1
	bcs	.L2596
	mov	w21, 2048
	mov	x20, x0
.L2601:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w0, [x20,420]
	ldr	w1, [x1,2096]
	cmp	w0, w1
	bcs	.L2598
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x20,420]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x20,420]
	cmn	w1, #1
	beq	.L2599
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
	bne	.L2598
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2598
.L2599:
	subs	w21, w21, #1
	bne	.L2601
.L2598:
	mov	w0, -1
	b	.L2603
.L2596:
	ldr	w0, [x0,200]
	str	wzr, [x2,80]
	str	wzr, [x2,84]
	str	w0, [x2,76]
	b	.L2609
.L2595:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x0,252]
	mov	w2, 4194304
	ldrb	w4, [x19,120]
	ldr	w3, [x19,1940]
	cbnz	w4, .L2604
	add	w0, w3, w0, lsr 10
	mov	w3, 33554432
	asr	w2, w3, w0
.L2604:
	add	x4, x1, :lo12:.LANCHOR2
	ldr	w3, [x4,200]
	ldr	w0, [x4,412]
	add	w5, w3, 1048576
	cmp	w0, w5
	bhi	.L2605
	add	w0, w2, w0
	cmp	w0, w3
	bcc	.L2605
	ldrb	w2, [x4,-36]
	mov	w0, 0
	cbnz	w2, .L2603
.L2605:
	add	x0, x1, :lo12:.LANCHOR2
	mov	w1, 1
	str	wzr, [x0,420]
	str	w1, [x0,416]
	str	w3, [x0,412]
.L2609:
	mov	w0, 0
.L2603:
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
	ldrh	w1, [x1,2018]
	cbnz	w3, .L2644
	ldrh	w20, [x2,136]
	mov	w4, 65535
	cmp	w20, w4
	bne	.L2613
.L2622:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x22, x20, 136
	ldrh	w1, [x20,136]
	str	wzr, [x20,2344]
	cmp	w1, w0
	beq	.L2644
	bl	FtlCacheWriteBack
	mov	w26, 12
	adrp	x0, .LANCHOR2
	add	x3, x21, :lo12:.LANCHOR0
	ldrh	w2, [x20,136]
	ldr	x1, [x0,#:lo12:.LANCHOR2]
	ldrh	w3, [x3,2018]
	ldrb	w0, [x22,7]
	mov	w22, 0
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldr	w0, [x20,220]
	ldrh	w1, [x20,920]
	add	w0, w1, w0
	str	w0, [x20,220]
	b	.L2623
.L2613:
	cbz	w0, .L2616
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3168]
	cmp	w5, w4
	beq	.L2617
.L2618:
	mov	w1, 2
	b	.L2616
.L2617:
	strh	w3, [x0,3168]
	ldrh	w0, [x2,32]
	cmp	w0, 17
	bhi	.L2618
.L2616:
	add	x22, x19, :lo12:.LANCHOR2
	add	x0, x22, 136
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L2619
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x22,-80]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L2620
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2620:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2344]
	cbnz	w1, .L2621
	ldr	w1, [x0,432]
	add	w1, w1, 1
	str	w1, [x0,432]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2621:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	str	wzr, [x19,2344]
	b	.L2612
.L2619:
	adrp	x0, .LANCHOR1+3168
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3168]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2612
	b	.L2622
.L2626:
	ldr	x25, [x20,928]
	add	x1, x29, 92
	umull	x24, w22, w26
	mov	w2, 0
	add	x23, x25, x24
	ldr	w0, [x23,8]
	bl	log2phys
	ldr	w0, [x29,92]
	ldr	w1, [x25,x24]
	cmp	w0, w1
	bne	.L2624
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
.L2643:
	bl	decrement_vpc_count
.L2625:
	add	w22, w22, 1
	uxth	w22, w22
.L2623:
	ldrh	w0, [x20,920]
	cmp	w0, w22
	bhi	.L2626
	b	.L2645
.L2624:
	ldr	w1, [x23,4]
	cmp	w0, w1
	beq	.L2625
	ldrh	w0, [x20,136]
	b	.L2643
.L2645:
	mov	w0, -1
	bl	decrement_vpc_count
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L2627
	adrp	x0, .LC143
	ldrh	w1, [x20,136]
	add	x0, x0, :lo12:.LC143
	bl	printk
.L2627:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,136]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2,x1]
	cbz	w1, .L2628
	bl	INSERT_DATA_LIST
	b	.L2629
.L2628:
	bl	INSERT_FREE_LIST
.L2629:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w22, -1
	strh	wzr, [x20,920]
	strh	w22, [x20,136]
	strh	wzr, [x20,908]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w22, [x20,280]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,1940]
	cbz	w0, .L2630
	ldr	w0, [x20,256]
	cmp	w0, 39
	bhi	.L2630
	ldrh	w0, [x20,260]
	ldrh	w1, [x20,32]
	cmp	w1, w0
	bcs	.L2644
	lsl	w0, w0, 1
	strh	w0, [x20,848]
.L2644:
	mov	w0, 0
	b	.L2612
.L2630:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,260]
	ldrh	w0, [x19,32]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L2644
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,120]
	cbz	w0, .L2631
	sub	w1, w1, #2
	strh	w1, [x19,848]
	b	.L2644
.L2631:
	mov	w1, 20
	strh	w1, [x19,848]
.L2612:
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
	ldrh	w1, [x20,2018]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,138]
	ldrh	w0, [x20,2018]
	cmp	w1, w0
	bcc	.L2646
	add	x0, x19, 2256
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19,2344]
.L2646:
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR4+296]
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
	adrp	x20, .LANCHOR4
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp,48]
	add	x0, x20, :lo12:.LANCHOR4
	add	x24, x21, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR2
	add	x23, x19, :lo12:.LANCHOR2
	strh	w1, [x0,290]
	strh	wzr, [x0,292]
	ldrh	w0, [x24,1952]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x23,264]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2650
.L2652:
	mov	w22, -1
	b	.L2651
.L2650:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L2652
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x24,2058]
	mov	x1, 0
	ldr	x0, [x23,184]
.L2653:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2657
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2653
.L2657:
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w3, w2
	ldrh	w0, [x1,-36]
	add	w0, w0, 1
	strh	w0, [x1,-36]
	blt	.L2654
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w0, [x20,292]
	cbz	w0, .L2658
.L2654:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x4, [x19,#:lo12:.LANCHOR2]
	ldrh	w2, [x1,40]
	ldrh	w5, [x1,44]
	lsl	x2, x2, 1
	ldrh	w3, [x4,x2]
	sub	w3, w3, w5
	strh	w3, [x4,x2]
	add	x3, x21, :lo12:.LANCHOR0
	strb	wzr, [x1,46]
	ldrh	w4, [x1,88]
	ldr	x5, [x19,#:lo12:.LANCHOR2]
	ldrh	w2, [x3,2018]
	lsl	x4, x4, 1
	strh	w2, [x1,42]
	strh	wzr, [x1,44]
	ldrh	w6, [x1,92]
	ldrh	w0, [x5,x4]
	sub	w0, w0, w6
	strh	w0, [x5,x4]
	strb	wzr, [x1,94]
	ldrh	w0, [x3,2018]
	strh	w0, [x1,90]
	ldrh	w0, [x1,-34]
	strh	wzr, [x1,92]
	add	w0, w0, 1
	strh	w0, [x1,-34]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2658:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,-36]
	and	w0, w0, 31
	cbnz	w0, .L2659
	bl	FtlVpcCheckAndModify
.L2659:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x21, x19, 40
	ldrh	w0, [x19,40]
	cmp	w0, w1
	beq	.L2651
	ldrh	w1, [x19,44]
	cbnz	w1, .L2651
	ldrh	w1, [x19,92]
	add	x20, x19, 88
	cbnz	w1, .L2651
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x19,88]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x21
	bl	allocate_new_data_superblock
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2651:
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
	cbnz	w22, .L2667
	adrp	x20, .LANCHOR0
	ldr	x0, [x19,2224]
	add	x23, x20, :lo12:.LANCHOR0
	mov	w1, w22
	ldrh	w2, [x23,2056]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x23,2056]
	mov	w1, w22
	ldr	x0, [x19,2216]
	lsl	w2, w2, 2
	bl	ftl_memset
	str	w22, [x19,232]
	ldrh	w0, [x23,1952]
	str	w22, [x19,236]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2668
	bl	FtlMakeBbt
.L2668:
	mov	w0, 0
.L2669:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2024]
	cmp	w0, w2, lsl 7
	bge	.L2696
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
	b	.L2669
.L2696:
	ldrh	w22, [x1,1956]
	mov	w19, 0
.L2671:
	add	x23, x20, :lo12:.LANCHOR0
	ldrh	w0, [x23,1958]
	cmp	w0, w22
	bls	.L2697
	mov	w0, w22
	mov	w1, 1
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w19, w0
	uxth	w22, w22
	uxth	w19, w0
	b	.L2671
.L2697:
	ldrh	w0, [x23,1948]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L2673
.L2677:
	mov	w19, 0
	mov	w22, w19
	b	.L2674
.L2673:
	udiv	w19, w19, w0
	ldr	w0, [x23,2052]
	add	w0, w19, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x23,1952]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x23,1956]
.L2675:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,1958]
	cmp	w0, w19
	bls	.L2677
	mov	w0, w19
	mov	w1, 1
	add	w19, w19, 1
	bl	FtlLowFormatEraseBlock
	uxth	w19, w19
	b	.L2675
.L2674:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,1956]
	cmp	w1, w22
	bls	.L2698
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w19, w0
	uxth	w22, w22
	uxth	w19, w0
	b	.L2674
.L2698:
	add	x4, x21, :lo12:.LANCHOR2
	ldrh	w1, [x0,1958]
	ldrh	w2, [x0,1948]
	str	w1, [x4,2036]
	ldr	w1, [x0,1960]
	udiv	w3, w1, w2
	str	w3, [x0,2096]
	ubfx	x6, x3, 5, 16
	add	w5, w6, 36
	strh	w5, [x4,260]
	mov	w5, 24
	mul	w5, w2, w5
	cmp	w19, w5
	ble	.L2679
	sub	w1, w1, w19
	udiv	w1, w1, w2
	str	w1, [x0,2096]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x4,260]
.L2679:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1940]
	cmp	w0, 1
	bne	.L2680
	add	x1, x21, :lo12:.LANCHOR2
	udiv	w0, w19, w2
	ldrh	w4, [x1,260]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,260]
.L2680:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L2681
	add	x1, x21, :lo12:.LANCHOR2
	udiv	w0, w19, w2
	ldrh	w4, [x1,260]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,260]
.L2681:
	add	x5, x20, :lo12:.LANCHOR0
	ldrh	w1, [x5,2010]
	cbz	w1, .L2683
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w4, [x0,260]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,260]
	mul	w4, w1, w2
	cmp	w4, w19
	ble	.L2683
	add	w1, w1, 32
	str	w3, [x5,2096]
	add	w1, w6, w1
	strh	w1, [x0,260]
.L2683:
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
	ldrh	w1, [x22,2018]
	mul	w0, w0, w1
	ldrh	w1, [x22,2024]
	str	w0, [x22,2096]
	mul	w0, w0, w1
	str	w0, [x22,2060]
	bl	FtlBbmTblFlush
	ldrh	w2, [x22,1958]
	mov	w1, 0
	ldr	x0, [x21,#:lo12:.LANCHOR2]
	add	x21, x19, 40
	lsl	w2, w2, 1
	bl	ftl_memset
	str	wzr, [x19,196]
	ldrh	w2, [x22,1956]
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
.L2685:
	mov	x0, x21
	bl	make_superblock
	ldrb	w0, [x21,7]
	cbnz	w0, .L2686
	ldrh	w1, [x21]
	ldr	x0, [x19]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L2685
.L2686:
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
.L2687:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2688
	ldrh	w1, [x20]
	ldr	x0, [x19]
	strh	w21, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2687
.L2688:
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
	cbnz	w0, .L2667
	adrp	x0, .LANCHOR1+504
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+504]
.L2667:
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
	cbz	w0, .L2700
.L2702:
	mov	w0, 0
	b	.L2701
.L2700:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,48]
	bl	FlashInit
	cbnz	w0, .L2702
	bl	FlashLoadFactorBbt
	cbz	w0, .L2703
	bl	FlashMakeFactorBbt
.L2703:
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20,104]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L2704
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L2706:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L2706
	cmp	w0, 6
	bhi	.L2707
	add	x0, x19, :lo12:.LANCHOR0
	b	.L2729
.L2707:
	mov	w1, 0
	mov	w4, 1
.L2710:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L2710
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L2729
	mov	w1, 36
.L2729:
	strb	w1, [x0,9]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,9]
	strh	w1, [x0,194]
.L2704:
	adrp	x0, .LC75
	adrp	x1, .LC76
	add	x1, x1, :lo12:.LC76
	add	x0, x0, :lo12:.LC75
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	add	x0, x19, 168
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,1952]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L2712:
	bl	FtlLoadBbt
	cbz	w0, .L2713
.L2731:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L2732
	add	w19, w19, 1
	b	.L2712
.L2732:
	mov	w0, -1
	b	.L2701
.L2713:
	bl	FtlSysBlkInit
	cbnz	w0, .L2731
	adrp	x1, .LANCHOR1+504
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+504]
.L2701:
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
	cbz	w1, .L2734
.L2737:
	mov	w0, 0
	b	.L2735
.L2734:
	adrp	x1, .LANCHOR0+120
	ldrb	w1, [x1,#:lo12:.LANCHOR0+120]
	cbz	w1, .L2736
	ldr	w1, [x0,864]
	tbz	x1, 0, .L2736
	ldrh	w0, [x0,140]
	cbnz	w0, .L2737
.L2736:
	add	x1, x21, :lo12:.LANCHOR2
	mov	w2, 0
	mov	w3, w2
	mov	w20, 0
	mov	w22, 56
	ldr	x0, [x1,2056]
	ldr	w1, [x1,864]
	bl	FlashProgPages
.L2738:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w1, [x19,864]
	cmp	w20, w1
	bcs	.L2748
	umull	x0, w20, w22
	ldr	x2, [x19,2056]
	add	x1, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L2739
	ldrh	w3, [x19,136]
	ldr	x1, [x19]
	strh	wzr, [x1,x3,lsl 1]
	strh	w2, [x19,136]
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
	b	.L2747
.L2739:
	ldr	x2, [x1,16]
	add	w20, w20, 1
	ldr	w1, [x1,4]
	uxth	w20, w20
	ldr	w0, [x2,12]
	ldr	w2, [x2,8]
	bl	FtlGcUpdatePage
	b	.L2738
.L2748:
	ldr	x0, [x19,2056]
	bl	FtlGcBufFree
	str	wzr, [x19,864]
	ldrh	w0, [x19,140]
	cbnz	w0, .L2737
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L2747:
	mov	w0, 1
.L2735:
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
	beq	.L2750
	ldrh	w0, [x1,4]
	cbnz	w0, .L2751
.L2750:
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
.L2751:
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
	cbnz	w4, .L2897
	ldr	w4, [x3,2024]
	cbnz	w4, .L2897
	ldrh	w4, [x3,16]
	cmp	w4, 47
	bls	.L2897
	mov	w23, w0
	adrp	x0, .LANCHOR1+3168
	mov	w22, w1
	ldrh	w1, [x0,#:lo12:.LANCHOR1+3168]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2754
.L2757:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0,938]
	cmp	w4, w2
	bne	.L2755
	b	.L2756
.L2754:
	ldrh	w1, [x3,136]
	cmp	w1, w0
	beq	.L2757
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2757
	b	.L2897
.L2755:
	ldrh	w1, [x0,936]
	cmp	w1, w2
	bne	.L2756
	ldrh	w3, [x0,940]
	cmp	w3, w1
	beq	.L2756
	ldrh	w2, [x0,942]
	cmp	w2, w1
	beq	.L2756
	mov	w1, -1
	strh	w4, [x0,936]
	strh	w3, [x0,938]
	strh	w2, [x0,940]
	strh	w1, [x0,942]
.L2756:
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w23, 1
	ldr	w0, [x1,852]
	add	w0, w0, 1
	add	w0, w0, w23, lsl 7
	str	w0, [x1,852]
	bne	.L2768
	adrp	x25, .LANCHOR0
	add	x1, x25, :lo12:.LANCHOR0
	ldr	w2, [x1,1940]
	cbnz	w2, .L2759
	ldrb	w1, [x1,120]
	cbz	w1, .L2768
.L2759:
	add	x20, x19, :lo12:.LANCHOR2
	ldr	w1, [x20,256]
	cmp	w1, 39
	bhi	.L2768
	adrp	x21, .LANCHOR4
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w1, [x1,432]
	add	w0, w0, w1
	str	w0, [x20,852]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x20,280]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2768
	ldrh	w1, [x20,936]
	cmp	w1, w0
	bne	.L2768
	ldr	w0, [x20,852]
	cmp	w0, 1024
	bhi	.L2760
	ldrh	w0, [x20,32]
	cmp	w0, 63
	bhi	.L2768
.L2760:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w2, [x0,850]
	ldrh	w3, [x0,32]
	add	w2, w2, 64
	strh	wzr, [x1,432]
	cmp	w3, w2
	bgt	.L2768
	str	wzr, [x0,852]
	ldr	w0, [x0,256]
	cbnz	w0, .L2761
	mov	w0, 6
	b	.L2919
.L2761:
	cmp	w0, 5
	bhi	.L2762
	mov	w0, 18
.L2919:
	strh	w0, [x1,432]
.L2762:
	mov	w0, 32
	bl	List_get_gc_head_node
	uxth	w26, w0
	mov	w3, 65535
	cmp	w26, w3
	beq	.L2767
	add	x24, x19, :lo12:.LANCHOR2
	adrp	x27, .LANCHOR2
	ldrh	w0, [x24,856]
	cbz	w0, .L2764
	add	x25, x25, :lo12:.LANCHOR0
	ldr	x28, [x27,#:lo12:.LANCHOR2]
	ubfiz	x26, x26, 1, 16
	ldrh	w1, [x25,2020]
	ldrh	w4, [x25,1948]
	ldrh	w2, [x28,x26]
	mul	w1, w1, w4
	add	w1, w1, 1
	cmp	w2, w1
	bgt	.L2767
	add	w1, w0, 1
	str	x3, [x29,112]
	str	wzr, [x24,860]
	uxth	w1, w1
	strh	w1, [x24,856]
	str	x1, [x29,120]
	bl	List_get_gc_head_node
	uxth	w20, w0
	ldr	x3, [x29,112]
	ldr	x1, [x29,120]
	cmp	w20, w3
	beq	.L2767
	ubfiz	x25, x20, 1, 16
	adrp	x0, .LC144
	ldrh	w4, [x28,x26]
	add	x0, x0, :lo12:.LC144
	mov	w2, w20
	ldrh	w3, [x28,x25]
	bl	printk
	ldrh	w0, [x24,856]
	cmp	w0, 40
	bls	.L2765
	ldr	x0, [x27,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x25]
	cmp	w0, 32
	bls	.L2765
	strh	wzr, [x24,856]
.L2765:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, 6
	strh	w0, [x21,432]
	b	.L2769
.L2764:
	mov	w0, 1
	strh	w0, [x24,856]
.L2767:
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2769
	add	x21, x21, :lo12:.LANCHOR4
	strh	wzr, [x21,432]
.L2768:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w20, [x21,280]
	cmp	w20, w0
	bne	.L2923
	ldrh	w0, [x21,136]
	cmp	w0, w20
	beq	.L2924
.L2769:
	mov	w0, 65535
	cmp	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	cset	w2, eq
	cbz	w2, .L2782
	cbnz	w23, .L2782
	ldrh	w1, [x0,32]
	mov	w21, 1
	cmp	w1, 24
	bhi	.L2783
	adrp	x0, .LANCHOR0
	cmp	w1, 16
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w21, [x0,2018]
	bls	.L2784
	lsr	w21, w21, 5
	b	.L2783
.L2784:
	cmp	w1, 12
	bls	.L2785
	lsr	w21, w21, 4
	b	.L2783
.L2785:
	cmp	w1, 8
	bls	.L2783
	lsr	w21, w21, 2
.L2783:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,848]
	cmp	w2, w1
	bcs	.L2787
	ldrh	w1, [x0,136]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L2788
	ldrh	w2, [x0,936]
	cmp	w2, w1
	bne	.L2788
	adrp	x1, .LANCHOR4+432
	ldrh	w2, [x1,#:lo12:.LANCHOR4+432]
	cbnz	w2, .L2789
	adrp	x1, .LANCHOR0+2096
	ldr	w3, [x0,196]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2096]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2790
.L2789:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,260]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,848]
	b	.L2791
.L2790:
	mov	w1, 18
	strh	w1, [x0,848]
.L2791:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,860]
	b	.L2897
.L2788:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,260]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,848]
.L2787:
	cmp	w22, 2
	bhi	.L2852
	adrp	x0, .LANCHOR0+1940
	ldr	w0, [x0,#:lo12:.LANCHOR0+1940]
	cbz	w0, .L2852
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2852
.L2782:
	ldrh	w1, [x0,136]
	mov	w3, 65535
	cmp	w1, w3
	bne	.L2794
	cbz	w2, .L2794
	ldrh	w2, [x0,936]
	cmp	w2, w1
	bne	.L2794
	ldrh	w2, [x0,280]
	cmp	w2, w1
	beq	.L2795
.L2800:
	mov	w20, 65535
	b	.L2794
.L2795:
	ldrh	w2, [x0,32]
	adrp	x20, .LANCHOR4
	ldrh	w1, [x0,848]
	str	wzr, [x0,860]
	cmp	w2, w1
	bls	.L2796
	add	x1, x20, :lo12:.LANCHOR4
	ldrh	w1, [x1,432]
	cbnz	w1, .L2797
	adrp	x1, .LANCHOR0+2096
	ldr	w2, [x0,196]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2096]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L2798
.L2797:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,260]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,848]
	b	.L2799
.L2798:
	mov	w1, 18
	strh	w1, [x0,848]
.L2799:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x1,x0]
	cmp	w0, 4
	bls	.L2796
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w2, [x20,432]
	b	.L2897
.L2796:
	add	x21, x20, :lo12:.LANCHOR4
	ldrh	w0, [x21,432]
	cbnz	w0, .L2800
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
	ldrh	w0, [x1,2020]
	ldrh	w1, [x1,1948]
	mul	w0, w0, w1
	mov	w1, 2
	sdiv	w0, w0, w1
	cmp	w2, w0
	ble	.L2801
	ldrh	w0, [x22,32]
	sub	w20, w20, #1
	cmp	w0, w20
	blt	.L2801
	bl	FtlReadRefresh
	ldrh	w2, [x21,432]
	b	.L2897
.L2801:
	cbnz	w2, .L2800
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,32]
	add	w2, w2, 1
	b	.L2897
.L2794:
	adrp	x0, .LANCHOR0+1940
	mov	w21, 2
	ldr	w0, [x0,#:lo12:.LANCHOR0+1940]
	cmp	w0, wzr
	csinc	w21, w21, wzr, ne
	b	.L2793
.L2852:
	mov	w20, 65535
.L2793:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,280]
	cmp	w1, w2
	bne	.L2803
	cmp	w20, w1
	beq	.L2804
	strh	w20, [x0,280]
	b	.L2805
.L2804:
	ldrh	w1, [x0,936]
	cmp	w1, w20
	beq	.L2805
	adrp	x2, .LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2]
	ldrh	w1, [x2,x1]
	cbnz	w1, .L2806
	mov	w1, -1
	strh	w1, [x0,936]
.L2806:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,936]
	strh	w1, [x0,280]
	mov	w1, -1
	strh	w1, [x0,936]
.L2805:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x22,280]
	strb	wzr, [x22,288]
	cmp	w0, w1
	beq	.L2803
	bl	IsBlkInGcList
	cbz	w0, .L2808
	mov	w0, -1
	strh	w0, [x22,280]
.L2808:
	adrp	x0, .LANCHOR0+120
	ldrb	w0, [x0,#:lo12:.LANCHOR0+120]
	cbz	w0, .L2809
	add	x0, x19, :lo12:.LANCHOR2
	add	x22, x0, 280
	ldrh	w0, [x0,280]
	bl	ftl_get_blk_mode
	strb	w0, [x22,8]
.L2809:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x24, x22, 280
	ldrh	w1, [x22,280]
	cmp	w1, w0
	beq	.L2803
	mov	x0, x24
	bl	make_superblock
	strh	wzr, [x22,282]
	adrp	x0, .LANCHOR4
	adrp	x1, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w2, [x22,280]
	strb	wzr, [x22,286]
	ldr	x1, [x1,#:lo12:.LANCHOR2]
	strh	wzr, [x0,434]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,436]
.L2803:
	add	x6, x19, :lo12:.LANCHOR2
	ldrh	w0, [x6,280]
	ldrh	w1, [x6,40]
	cmp	w1, w0
	beq	.L2810
	ldrh	w1, [x6,88]
	cmp	w1, w0
	beq	.L2810
.L2811:
	adrp	x0, .LC146
	mov	x28, x6
	add	x0, x0, :lo12:.LC146
	mov	w24, 65535
	str	x0, [x29,120]
	b	.L2812
.L2810:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,280]
	b	.L2922
.L2833:
	ldrh	w0, [x28,282]
	add	w21, w21, w0
	uxth	w21, w21
	strh	w21, [x28,282]
	cmp	w21, w25
	bcs	.L2925
.L2835:
	ldrh	w0, [x28,32]
	cmp	w0, 2
	bhi	.L2838
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w21, [x0,2018]
.L2812:
	ldrh	w0, [x28,280]
	cmp	w0, w24
	bne	.L2813
	str	wzr, [x28,860]
	mov	w25, 2
.L2814:
	ldrh	w22, [x28,856]
	mov	w0, w22
	bl	List_get_gc_head_node
	uxth	w26, w0
	cmp	w26, w24
	strh	w26, [x28,280]
	bne	.L2815
	strh	wzr, [x28,856]
	mov	w2, 8
	b	.L2897
.L2815:
	mov	w0, w26
	add	w22, w22, 1
	bl	IsBlkInGcList
	cbz	w0, .L2816
	strh	w22, [x28,856]
	b	.L2814
.L2816:
	adrp	x0, .LANCHOR0
	ubfiz	x1, x26, 1, 16
	add	x4, x0, :lo12:.LANCHOR0
	ldr	x2, [x28]
	uxth	w22, w22
	strh	w22, [x28,856]
	ldrh	w0, [x4,2018]
	ldrh	w4, [x4,1948]
	ldrh	w3, [x2,x1]
	mul	w0, w0, w4
	sdiv	w4, w0, w25
	cmp	w3, w4
	bgt	.L2818
	cmp	w3, 8
	bls	.L2819
	cmp	w22, 48
	bls	.L2819
	ldrh	w3, [x28,908]
	cmp	w3, 35
	bhi	.L2819
.L2818:
	strh	wzr, [x28,856]
.L2819:
	ldrh	w1, [x2,x1]
	cmp	w1, w0
	blt	.L2820
	cmp	w20, w24
	bne	.L2820
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x19,856]
	strh	w0, [x19,280]
.L2922:
	adrp	x0, .LANCHOR4+432
	ldrh	w2, [x0,#:lo12:.LANCHOR4+432]
	b	.L2897
.L2820:
	cbnz	w1, .L2821
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x28,856]
	add	w0, w0, 1
	strh	w0, [x28,856]
	b	.L2814
.L2821:
	adrp	x0, .LANCHOR0
	strb	wzr, [x28,288]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L2822
	mov	w0, w26
	bl	ftl_get_blk_mode
	strb	w0, [x28,288]
.L2822:
	add	x22, x28, 280
	mov	x0, x22
	bl	make_superblock
	adrp	x1, .LANCHOR4
	ldrh	w2, [x28,280]
	add	x1, x1, :lo12:.LANCHOR4
	ldr	x0, [x28]
	strh	wzr, [x1,434]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,436]
	strh	wzr, [x28,282]
	strb	wzr, [x28,286]
.L2813:
	cmp	w23, 1
	bne	.L2823
	bl	FtlReadRefresh
.L2823:
	mov	w0, 1
	str	w0, [x28,2024]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,120]
	ldrh	w25, [x0,2018]
	cbz	w1, .L2824
	ldrb	w1, [x28,288]
	cmp	w1, 1
	bne	.L2824
	ldrh	w25, [x0,2020]
.L2824:
	ldrh	w0, [x28,282]
	add	w1, w0, w21
	cmp	w1, w25
	ble	.L2825
	sub	w21, w25, w0
	uxth	w21, w21
.L2825:
	mov	w26, 0
.L2826:
	cmp	w21, w26, uxth
	bls	.L2833
	adrp	x0, .LANCHOR0
	add	x1, x28, 280
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w3, [x1,2]
	add	w3, w3, w26
	ldrh	w5, [x0,1948]
	mov	x0, 0
	mov	w22, w0
.L2834:
	cmp	w5, w0, uxth
	bls	.L2926
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,16]
	cmp	w2, w24
	beq	.L2827
	mov	w4, 56
	ldr	x7, [x28,896]
	orr	w2, w3, w2, lsl 10
	umull	x4, w22, w4
	add	w22, w22, 1
	add	x4, x7, x4
	uxth	w22, w22
	str	w2, [x4,4]
.L2827:
	add	x0, x0, 1
	b	.L2834
.L2926:
	ldr	x0, [x28,896]
	mov	w1, w22
	ldrb	w2, [x28,288]
	mov	x27, 0
	bl	FlashReadPages
.L2829:
	cmp	w22, w27, uxth
	bls	.L2927
	mov	x0, 56
	mul	x4, x27, x0
	ldr	x0, [x28,896]
	add	x1, x0, x4
	ldr	w0, [x0,x4]
	cmn	w0, #1
	ldr	x3, [x1,16]
	beq	.L2854
	ldrh	w0, [x3]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2854
	ldr	w0, [x3,8]
	add	x1, x29, 140
	mov	w2, 0
	str	x4, [x29,104]
	str	x3, [x29,112]
	bl	log2phys
	ldr	x2, [x28,896]
	ldr	x4, [x29,104]
	ldr	w0, [x29,140]
	add	x2, x2, x4
	ldr	x3, [x29,112]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L2854
	adrp	x0, .LANCHOR4
	str	x3, [x29,104]
	add	x1, x0, :lo12:.LANCHOR4
	mov	x3, 56
	str	x4, [x29,96]
	ldrh	w0, [x1,434]
	add	w0, w0, 1
	strh	w0, [x1,434]
	ldr	w0, [x28,864]
	ldr	x1, [x28,2056]
	madd	x1, x0, x3, x1
	ldr	w0, [x2,24]
	str	x1, [x29,112]
	str	w0, [x1,24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29,112]
	mov	x2, 56
	ldr	x4, [x29,96]
	ldr	x3, [x29,104]
	str	w0, [x1,4]
	ldr	w0, [x28,864]
	ldr	x1, [x28,2056]
	madd	x0, x0, x2, x1
	ldr	x1, [x28,896]
	add	x1, x1, x4
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	add	x2, x28, 136
	str	x2, [x29,112]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,140]
	str	w0, [x3,12]
	ldrh	w0, [x28,136]
	strh	w0, [x3,2]
	ldr	w0, [x28,236]
	str	w0, [x3,4]
	ldr	w0, [x28,864]
	add	w0, w0, 1
	str	w0, [x28,864]
	ldr	x0, [x28,896]
	add	x0, x0, x4
	bl	FtlGcBufAlloc
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2831
	ldr	x2, [x29,112]
	ldr	w1, [x28,864]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2831
	ldrh	w0, [x2,4]
	cbnz	w0, .L2854
.L2831:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2854
	adrp	x0, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x19,2024]
	ldrh	w2, [x0,432]
	b	.L2897
.L2854:
	add	x27, x27, 1
	b	.L2829
.L2927:
	add	w26, w26, 1
	b	.L2826
.L2925:
	ldr	w0, [x28,864]
	adrp	x21, .LANCHOR4
	cbz	w0, .L2836
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2836
	add	x5, x21, :lo12:.LANCHOR4
	str	wzr, [x28,2024]
	ldrh	w2, [x5,432]
	b	.L2897
.L2836:
	add	x5, x21, :lo12:.LANCHOR4
	ldrh	w21, [x5,434]
	cbnz	w21, .L2837
	ldrh	w1, [x28,280]
	ldr	x3, [x28]
	ubfiz	x0, x1, 1, 16
	ldrh	w4, [x3,x0]
	cbz	w4, .L2837
	ldrh	w2, [x28,282]
	mov	w3, w21
	ldrh	w5, [x5,436]
	ldr	x0, [x29,120]
	bl	printk
	ldrh	w1, [x28,280]
	ldr	x0, [x28]
	strh	w21, [x0,x1,lsl 1]
	ldrh	w0, [x28,280]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2837:
	mov	w0, -1
	strh	w0, [x28,280]
	b	.L2835
.L2838:
	adrp	x1, .LANCHOR4+432
	str	wzr, [x28,2024]
	ldrh	w2, [x1,#:lo12:.LANCHOR4+432]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L2897
.L2924:
	ldrh	w24, [x21,936]
	cmp	w24, w20
	bne	.L2769
	ldrh	w0, [x21,32]
	mov	w2, 1024
	ldr	w1, [x21,852]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2769
	adrp	x0, .LANCHOR4+432
	str	wzr, [x21,852]
	strh	wzr, [x0,#:lo12:.LANCHOR4+432]
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	cmp	w20, w24
	bne	.L2771
	ldrh	w1, [x21,32]
	ldrh	w0, [x21,850]
	cmp	w1, w0
	bcs	.L2772
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w20
	beq	.L2781
	ldr	w2, [x21,2020]
	adrp	x24, .LANCHOR0
	uxtw	x1, w0
	cbnz	w2, .L2774
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w2, [x0,1968]
	cmp	w2, 3
	beq	.L2774
	ldr	w2, [x21,328]
	cbnz	w2, .L2774
	ldr	w2, [x0,1940]
	cbnz	w2, .L2774
	ldrb	w0, [x0,120]
	cbz	w0, .L2775
.L2774:
	add	x3, x24, :lo12:.LANCHOR0
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x3,2020]
	ldrh	w0, [x3,1948]
	ldrh	w3, [x3,1968]
	mul	w2, w1, w0
	cmp	w3, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w2, w0
	cmp	w4, w0
	bgt	.L2777
	mov	w0, 0
	add	x24, x24, :lo12:.LANCHOR0
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x24,2096]
	ldr	w2, [x1,196]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2778
	mov	w0, 128
	b	.L2920
.L2778:
	mov	w0, 160
.L2920:
	strh	w0, [x1,850]
	b	.L2779
.L2777:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L2921
.L2775:
	adrp	x2, .LANCHOR2
	ldr	x2, [x2,#:lo12:.LANCHOR2]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2780
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0,850]
	b	.L2779
.L2780:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L2921:
	strh	w1, [x0,850]
	b	.L2781
.L2772:
	mov	w0, 80
	strh	w0, [x21,850]
	b	.L2781
.L2779:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2781
.L2771:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x20, 1, 32
	ldr	x3, [x19,#:lo12:.LANCHOR2]
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	mov	w1, w20
	ldr	x6, [x5,-80]
	ldrh	w2, [x5,32]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,848]
	ldrh	w4, [x6,x4]
	bl	printk
.L2781:
	bl	FtlGcReFreshBadBlk
	b	.L2769
.L2923:
	mov	w20, w0
	b	.L2769
.L2897:
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
	cbnz	w23, .L2930
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cbz	w1, .L2930
	ldrb	w0, [x0,120]
	cbz	w0, .L2932
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w23, eq
.L2932:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x20,9]
	adrp	x26, .LC147
	mov	w2, w23
	mov	w24, 0
	mov	w27, 56
	ldr	x0, [x0,2088]
	add	x26, x26, :lo12:.LC147
	bl	FlashProgPages
.L2933:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cmp	w24, w1
	bcs	.L2951
	umull	x21, w24, w27
	ldr	x1, [x0,2088]
	add	x0, x1, x21
	ldr	w25, [x1,x21]
	cmn	w25, #1
	beq	.L2954
	ldr	w1, [x0,4]
	cbz	w23, .L2968
	orr	w1, w1, -2147483648
.L2968:
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
	beq	.L2937
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x22,#:lo12:.LANCHOR2]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2938
	mov	x0, x26
	bl	printk
.L2938:
	mov	w0, w21
	bl	decrement_vpc_count
.L2937:
	add	w24, w24, 1
	b	.L2933
.L2970:
	mov	w20, 16386
.L2950:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w0, [x0,944]
	cbz	w0, .L2951
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w20, w20, #1
	bne	.L2950
.L2951:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19,2080]
	b	.L2930
.L2954:
	adrp	x26, .LC147
	mov	w24, 0
	mov	w27, 56
	str	w25, [x29,108]
	mov	w28, 1
	add	x26, x26, :lo12:.LC147
.L2934:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	cmp	w24, w1
	bcs	.L2970
	umull	x21, w24, w27
	ldr	x0, [x0,2088]
	ldr	w1, [x29,108]
	str	w1, [x0,x21]
.L2940:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x0, [x2,2088]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L2971
	ldr	w0, [x1,4]
	str	x2, [x29,96]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	ldr	x2, [x29,96]
	bne	.L2941
	ldr	x3, [x22,#:lo12:.LANCHOR2]
	ubfiz	x1, x1, 1, 16
	ldrh	w4, [x20,4]
	ldrh	w0, [x3,x1]
	sub	w0, w0, w4
	strh	w0, [x3,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x2,2018]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L2941:
	ldrh	w0, [x20,4]
	cbnz	w0, .L2942
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2942:
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
	bne	.L2943
	str	w28, [x5,-96]
.L2943:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w0, [x0,-96]
	cbz	w0, .L2940
	b	.L2930
.L2971:
	ldr	w0, [x1,4]
	cbz	w23, .L2969
	orr	w0, w0, -2147483648
.L2969:
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
	beq	.L2947
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x22,#:lo12:.LANCHOR2]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2948
	mov	x0, x26
	bl	printk
.L2948:
	mov	w0, w21
	bl	decrement_vpc_count
.L2947:
	add	w24, w24, 1
	b	.L2934
.L2930:
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
	cbnz	w0, .L2973
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2973:
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
	bne	.L2975
	bl	FtlSysFlush
.L2975:
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
	adrp	x0, .LC148
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC148
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
	ldr	w1, [x21,2060]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L2981
	cmp	w19, 31
	mov	w0, 0
	bls	.L2981
	adrp	x23, .LANCHOR2
	add	x1, x23, :lo12:.LANCHOR2
	ldr	w1, [x1,-96]
	cbnz	w1, .L2981
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,2024]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L2982
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L2982:
	mov	w0, -1
	adrp	x20, .LANCHOR4
	str	w0, [x29,76]
.L2983:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w19, w0
	bcc	.L2996
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L2984
	add	x1, x20, :lo12:.LANCHOR4
	mov	w2, 1
	ldr	w0, [x1,448]
	add	w0, w0, 1
	str	w0, [x1,448]
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
.L2984:
	add	x0, x22, :lo12:.LANCHOR0
	add	w21, w21, 1
	ldrh	w0, [x0,2024]
	sub	w19, w19, w0
	b	.L2983
.L2996:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,448]
	cmp	w1, 32
	bls	.L2986
	str	wzr, [x0,448]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2986:
	mov	w0, 0
.L2981:
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
	cmp	w0, 16
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	mov	w20, w1
	mov	w28, w2
	mov	x26, x3
	bne	.L2999
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L3000
.L2999:
	adrp	x21, .LANCHOR0
	add	w0, w1, w2
	add	x1, x21, :lo12:.LANCHOR0
	str	w0, [x29,168]
	mov	w0, -1
	ldr	w3, [x29,168]
	ldr	w2, [x1,2060]
	cmp	w3, w2
	bhi	.L3000
	ldrh	w0, [x1,2024]
	sub	w25, w3, #1
	adrp	x19, .LANCHOR2
	udiv	w1, w20, w0
	str	w1, [x29,172]
	udiv	w25, w25, w0
	ldr	w0, [x29,172]
	sub	w24, w25, w0
	add	x0, x19, :lo12:.LANCHOR2
	add	w24, w24, 1
	ldr	w1, [x0,228]
	add	w1, w28, w1
	str	w1, [x0,228]
	ldr	w1, [x0,200]
	add	w1, w24, w1
	str	w1, [x0,200]
	ldr	w0, [x29,172]
	mov	w1, w25
	bl	FtlCacheMetchLpa
	cbz	w0, .L3001
	bl	FtlCacheWriteBack
.L3001:
	mov	w27, 0
	adrp	x0, .LC138
	ldr	w22, [x29,172]
	add	x0, x0, :lo12:.LC138
	str	w27, [x29,156]
	mov	w23, w27
	str	w27, [x29,152]
	str	x0, [x29,112]
.L3002:
	cbz	w24, .L3045
	mov	w0, w22
	add	x1, x29, 188
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,188]
	cmn	w3, #1
	bne	.L3042
	mov	w3, 0
.L3003:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	cmp	w3, w0
	bcs	.L3007
	madd	w0, w22, w0, w3
	cmp	w0, w20
	bcc	.L3005
	ldr	w1, [x29,168]
	cmp	w0, w1
	bcs	.L3005
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x26, x0
	str	x3, [x29,160]
	bl	ftl_memset
	ldr	x3, [x29,160]
.L3005:
	add	w3, w3, 1
	b	.L3003
.L3042:
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
	bne	.L3008
	ldr	x0, [x0,2104]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	udiv	w3, w20, w0
	msub	w3, w3, w0, w20
	str	w3, [x29,128]
	sub	w3, w0, w3
	cmp	w3, w28
	csel	w3, w3, w28, ls
	str	w3, [x29,156]
	cmp	w3, w0
	bne	.L3009
	str	x26, [x2,8]
	b	.L3009
.L3008:
	cmp	w22, w25
	bne	.L3010
	ldr	x0, [x0,2112]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w4, [x29,168]
	ldrh	w3, [x0,2024]
	mul	w0, w22, w3
	sub	w27, w4, w0
	cmp	w27, w3
	bne	.L3009
	b	.L3043
.L3010:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2024]
	mul	w0, w22, w0
.L3043:
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x26, x0
	str	x0, [x2,8]
.L3009:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2048]
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
.L3007:
	subs	w24, w24, #1
	add	w22, w22, 1
	beq	.L3011
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,1948]
	cmp	w23, w0, lsl 3
	bne	.L3002
.L3011:
	cbz	w23, .L3002
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
	lsl	w0, w27, 9
	str	w0, [x29,120]
.L3013:
	ldr	w0, [x29,160]
	cmp	w23, w0
	bls	.L3046
	ldr	x0, [x29,160]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	ldr	w3, [x29,172]
	mul	x8, x0, x1
	ldr	x0, [x2,2048]
	add	x0, x0, x8
	ldr	w1, [x0,24]
	cmp	w1, w3
	bne	.L3014
	ldr	x1, [x0,8]
	ldr	x0, [x2,2104]
	cmp	x1, x0
	bne	.L3015
	ldr	x2, [x29,144]
	mov	x0, x26
	str	x8, [x29,104]
	add	x1, x1, x2
	ldr	w2, [x29,124]
	b	.L3044
.L3014:
	cmp	w1, w25
	bne	.L3015
	ldr	x1, [x0,8]
	ldr	x0, [x2,2112]
	cmp	x1, x0
	bne	.L3015
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,120]
	str	x8, [x29,104]
	ldrh	w0, [x0,2024]
	mul	w0, w25, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x26, x0
.L3044:
	bl	ftl_memcpy
	ldr	x8, [x29,104]
.L3015:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
	add	x2, x1, x8
	ldr	w3, [x1,x8]
	cmn	w3, #1
	bne	.L3016
	ldr	w1, [x0,408]
	str	w3, [x29,152]
	add	w1, w1, 1
	str	w1, [x0,408]
.L3016:
	ldr	x0, [x2,16]
	ldr	w1, [x2,24]
	ldr	w0, [x0,8]
	cmp	w1, w0
	beq	.L3017
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
.L3017:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2048]
	add	x1, x0, x8
	ldr	w0, [x0,x8]
	cmp	w0, 256
	bne	.L3018
	ldr	w0, [x1,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3018:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L3013
.L3046:
	mov	w23, 0
	b	.L3002
.L3045:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,944]
	cbz	w0, .L3021
	mov	w0, w24
	mov	w1, 1
	bl	ftl_do_gc
.L3021:
	ldr	w0, [x29,152]
.L3000:
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
	str	x21, [sp,32]
	adrp	x21, .LANCHOR1
	add	x21, x21, :lo12:.LANCHOR1
	add	x1, x1, :lo12:.LC76
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	adrp	x19, .LANCHOR0
	str	w0, [x21,504]
	adrp	x0, .LANCHOR4+452
	add	x20, x20, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x0,#:lo12:.LANCHOR4+452]
	adrp	x0, .LC75
	add	x0, x0, :lo12:.LC75
	str	wzr, [x20,-96]
	bl	printk
	add	x0, x19, 168
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,1952]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3049
	adrp	x0, .LC149
	add	x0, x0, :lo12:.LC149
	b	.L3061
.L3049:
	bl	FtlSysBlkInit
	cbz	w0, .L3051
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
.L3061:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 80
	bl	printk
	b	.L3050
.L3051:
	mov	w1, 1
	str	w1, [x21,504]
	bl	ftl_do_gc
	ldrh	w0, [x20,32]
	cmp	w0, 15
	bhi	.L3052
	mov	w19, 1024
.L3053:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3053
	b	.L3050
.L3052:
	ldrb	w0, [x19,120]
	cbz	w0, .L3050
	mov	w19, 128
.L3055:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3055
.L3050:
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
	cbz	x1, .L3063
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
	cbnz	w0, .L3064
	bl	FtlInit
.L3064:
	adrp	x0, .LC151
	mov	w1, w19
	add	x0, x0, :lo12:.LC151
	bl	printk
	mov	w0, w19
.L3063:
	ldr	x19, [sp,16]
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
	ldrb	w1, [x1,120]
	cbz	w1, .L3067
	adrp	x21, .LANCHOR4
	adrp	x19, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x20, .LC152
	add	x24, x19, :lo12:.LANCHOR2
	ldr	x2, [x19,#:lo12:.LANCHOR2]
	add	x27, x24, 40
	ldrh	w23, [x0,290]
	add	x0, x20, :lo12:.LC152
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
.L3069:
	subs	w24, w24, #1
	beq	.L3073
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3069
.L3073:
	ldr	x2, [x19,#:lo12:.LANCHOR2]
	add	x0, x20, :lo12:.LC152
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3071
	add	x0, x29, 96
	strh	w23, [x29,96]
	bl	make_superblock
	add	x0, x25, :lo12:.LANCHOR0
	mov	w6, 65535
	mov	w7, 56
	ldrh	w5, [x0,1948]
	mov	x0, 0
	mov	w20, w0
.L3074:
	cmp	w5, w0, uxth
	bls	.L3081
	add	x1, x29, 96
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L3075
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
.L3075:
	add	x0, x0, 1
	b	.L3074
.L3081:
	ldr	x2, [x19,#:lo12:.LANCHOR2]
	adrp	x0, .LC153
	add	x24, x19, :lo12:.LANCHOR2
	mov	w1, w23
	add	x0, x0, :lo12:.LC153
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
.L3071:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x21,290]
.L3067:
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
	stp	x21, x22, [sp,32]
	mov	w21, w1
	adrp	x1, .LANCHOR2
	stp	x23, x24, [sp,48]
	mov	w23, w2
	add	x2, x1, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x24, x3
	str	x1, [x29,176]
	ldr	w3, [x2,-96]
	cbnz	w3, .L3084
	cmp	w4, 16
	bne	.L3085
	add	w0, w21, 256
	mov	w1, w23
	mov	x2, x24
	bl	FtlVendorPartWrite
	b	.L3084
.L3085:
	adrp	x20, .LANCHOR0
	add	w26, w21, w23
	add	x4, x20, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	w1, [x4,2060]
	cmp	w26, w1
	bhi	.L3084
	adrp	x6, .LANCHOR4
	ldrh	w1, [x4,2024]
	add	x28, x6, :lo12:.LANCHOR4
	sub	w26, w26, #1
	mov	w0, 2048
	ldr	w3, [x4,2080]
	udiv	w25, w21, w1
	cmp	w23, w1, lsl 1
	str	w0, [x28,472]
	str	x6, [x29,152]
	udiv	w0, w26, w1
	str	w0, [x29,216]
	sub	w26, w0, w25
	add	w27, w26, 1
	ldr	w0, [x2,208]
	add	w0, w27, w0
	str	w0, [x2,208]
	ldr	w0, [x2,224]
	add	w0, w23, w0
	str	w0, [x2,224]
	cset	w0, cs
	str	w0, [x29,220]
	cbz	w3, .L3087
	mov	w0, 56
	sub	w3, w3, #1
	umull	x3, w3, w0
	ldr	x0, [x4,2088]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w25, w0
	bne	.L3088
	ldr	w0, [x2,212]
	add	w0, w0, 1
	str	w0, [x2,212]
	ldr	w0, [x28,476]
	add	w0, w0, 1
	str	w0, [x28,476]
	msub	w0, w25, w1, w21
	sub	w1, w1, w0
	ubfiz	x0, x0, 9, 23
	cmp	w1, w23
	csel	w19, w1, w23, ls
	ldr	x1, [x3,8]
	lsl	w22, w19, 9
	add	x0, x1, x0
	mov	w2, w22
	mov	x1, x24
	bl	ftl_memcpy
	cbnz	w26, .L3089
	ldr	w0, [x28,476]
	cmp	w0, 2
	bgt	.L3089
.L3122:
	mov	w0, 0
	b	.L3084
.L3089:
	sub	w23, w23, w19
	add	w21, w21, w19
	add	x24, x24, x22
	add	w25, w25, 1
	mov	w27, w26
.L3088:
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x0,476]
.L3087:
	ldr	w1, [x29,216]
	mov	w0, w25
	bl	FtlCacheMetchLpa
	cbz	w0, .L3090
	bl	FtlCacheWriteBack
.L3090:
	ldr	x0, [x29,152]
	ldr	x1, [x29,176]
	add	x0, x0, :lo12:.LANCHOR4
	str	w25, [x29,200]
	add	x22, x1, :lo12:.LANCHOR2
	add	x19, x22, 40
	str	x19, [x29,184]
	str	x19, [x0,440]
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	str	x0, [x29,112]
.L3091:
	cbz	w27, .L3151
	ldrh	w0, [x19,4]
	cbnz	w0, .L3092
	ldr	x1, [x29,184]
	adrp	x28, .LANCHOR1
	cmp	x19, x1
	bne	.L3093
	add	x0, x22, 88
	ldrh	w19, [x0,4]
	cbnz	w19, .L3094
	bl	allocate_new_data_superblock
	add	x0, x28, :lo12:.LANCHOR1
	str	w19, [x0,3172]
.L3094:
	ldr	x0, [x29,184]
	add	x28, x28, :lo12:.LANCHOR1
	add	x19, x22, 88
	bl	allocate_new_data_superblock
	ldr	w0, [x28,3172]
	cbnz	w0, .L3095
.L3096:
	ldr	x19, [x29,184]
	b	.L3095
.L3093:
	add	x28, x28, :lo12:.LANCHOR1
	str	w0, [x28,3172]
	ldr	x0, [x29,184]
	ldrh	w0, [x0,4]
	cbnz	w0, .L3096
	mov	x0, x19
	bl	allocate_new_data_superblock
.L3095:
	ldrh	w0, [x19,4]
	cbnz	w0, .L3097
	mov	x0, x19
	bl	allocate_new_data_superblock
.L3097:
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [x0,440]
.L3092:
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w0, [x22,2044]
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
	add	w0, w23, w21
	str	w0, [x29,144]
.L3098:
	ldr	w0, [x29,208]
	ldr	w2, [x29,148]
	ldr	w1, [x29,200]
	cmp	w0, w2
	add	w26, w1, w0
	bcs	.L3152
	ldrh	w1, [x19,4]
	cbz	w1, .L3128
	ldr	w1, [x29,216]
	cmp	w26, w1
	cset	w7, eq
	cbz	w0, .L3100
	ldr	w1, [x29,124]
	tst	w7, w1
	beq	.L3100
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w2, [x29,144]
	ldrh	w1, [x1,2024]
	msub	w2, w26, w1, w2
	cmp	w2, w1
	bne	.L3128
.L3100:
	add	x1, x29, 228
	mov	w2, 0
	mov	w0, w26
	str	x7, [x29,104]
	bl	log2phys
	mov	x0, x19
	bl	get_new_active_ppa
	add	x4, x20, :lo12:.LANCHOR0
	mov	w5, 56
	str	x4, [x29,192]
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
	ldr	x0, [x22,2136]
	ldr	x3, [x29,136]
	str	x0, [x29,128]
	add	x28, x0, x3
	ldrh	w0, [x4,2028]
	str	x28, [x1,16]
	str	w26, [x1,24]
	mul	w0, w0, w6
	ldr	x6, [x22,2096]
	and	x0, x0, 4294967292
	add	x0, x6, x0
	str	x0, [x1,8]
	mov	x0, x28
	mov	w1, 0
	bl	ftl_memset
	cmp	w26, w25
	ldr	x4, [x29,192]
	cset	w0, eq
	str	w0, [x29,204]
	cbnz	w0, .L3131
	ldr	x7, [x29,104]
	ldr	x5, [x29,160]
	cbz	w7, .L3101
	ldr	w0, [x29,144]
	ldrh	w2, [x4,2024]
	msub	w2, w26, w2, w0
	uxth	w0, w2
	str	w0, [x29,192]
	ldr	w0, [x29,204]
	str	w0, [x29,160]
	b	.L3104
.L3131:
	ldrh	w2, [x4,2024]
	udiv	w0, w21, w2
	msub	w0, w0, w2, w21
	str	w0, [x29,160]
	sub	w2, w2, w0
	cmp	w2, w23
	csel	w0, w2, w23, ls
	str	w0, [x29,192]
.L3104:
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w1, [x29,192]
	ldrh	w0, [x2,2024]
	cmp	w1, w0
	bne	.L3105
	ldr	w0, [x29,204]
	mov	x1, x24
	cbnz	w0, .L3106
	ldr	w0, [x29,192]
	mul	w1, w0, w26
	sub	w1, w1, w21
	ubfiz	x1, x1, 9, 23
	add	x1, x24, x1
.L3106:
	ldr	w2, [x29,220]
	mov	w0, 56
	add	x4, x20, :lo12:.LANCHOR0
	cbz	w2, .L3107
	ldr	w2, [x4,2080]
	umull	x0, w2, w0
	ldr	x2, [x4,2088]
	add	x0, x2, x0
	b	.L3148
.L3107:
	ldr	w2, [x4,2080]
	umull	x0, w2, w0
	ldr	x2, [x4,2088]
	add	x0, x2, x0
	ldr	x0, [x0,8]
	b	.L3149
.L3105:
	ldr	w0, [x29,228]
	cmn	w0, #1
	beq	.L3109
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
	bne	.L3110
	ldr	w0, [x22,408]
	add	w0, w0, 1
	str	w0, [x22,408]
	b	.L3112
.L3110:
	ldr	w0, [x28,8]
	cmp	w0, w26
	beq	.L3112
	ldr	w0, [x22,408]
	mov	w2, w26
	add	w0, w0, 1
	str	w0, [x22,408]
	ldr	x0, [x29,112]
	ldr	w1, [x28,8]
	bl	printk
	b	.L3112
.L3109:
	ldr	w0, [x2,2080]
	mov	w1, 56
	umull	x0, w0, w1
	ldr	x1, [x2,2088]
	ldrh	w2, [x2,2028]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L3112:
	ldr	w1, [x29,192]
	mov	w0, 56
	lsl	w2, w1, 9
	ldr	w1, [x29,204]
	cbz	w1, .L3113
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
	b	.L3150
.L3113:
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
	b	.L3150
.L3101:
	ldr	w0, [x29,220]
	ldrh	w1, [x4,2024]
	ldr	w2, [x4,2080]
	cbz	w0, .L3114
	mul	w1, w26, w1
	umull	x0, w2, w5
	sub	w1, w1, w21
	ldr	x2, [x4,2088]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x24, x1
.L3148:
	str	x1, [x0,8]
	b	.L3108
.L3114:
	umull	x0, w2, w5
	ldr	x2, [x4,2088]
	mul	w1, w26, w1
	add	x0, x2, x0
	sub	w1, w1, w21
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x24, x1
.L3149:
	ldrh	w2, [x4,2028]
.L3150:
	bl	ftl_memcpy
.L3108:
	ldr	x1, [x29,128]
	mov	w0, -3947
	ldr	x2, [x29,136]
	strh	w0, [x1,x2]
	add	x1, x20, :lo12:.LANCHOR0
	str	w26, [x28,8]
	ldr	w0, [x22,236]
	str	w0, [x28,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x22,236]
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
	b	.L3098
.L3152:
	str	w26, [x29,200]
	mov	x0, x2
	b	.L3099
.L3128:
	str	w26, [x29,200]
.L3099:
	sub	w27, w27, w0
	ldr	w0, [x29,220]
	cbnz	w0, .L3118
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0,2080]
	ldr	w0, [x22,2044]
	cmp	w1, w0
	bcs	.L3118
	ldrh	w0, [x19,4]
	cbz	w0, .L3118
.L3120:
	str	wzr, [x29,220]
	b	.L3091
.L3118:
	bl	FtlCacheWriteBack
	add	x0, x20, :lo12:.LANCHOR0
	cmp	w27, 1
	str	wzr, [x0,2080]
	bhi	.L3091
	b	.L3120
.L3151:
	ldr	w1, [x29,216]
	mov	w0, w27
	sub	w1, w1, w25
	bl	ftl_do_gc
	ldr	x0, [x29,176]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,32]
	cmp	w1, 31
	bhi	.L3122
	mov	w1, 128
	mov	w19, 16
	strh	w1, [x0,850]
	strh	w1, [x0,848]
.L3123:
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldr	x0, [x29,176]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,-96]
	cbnz	w0, .L3122
	subs	w19, w19, #1
	bne	.L3123
	b	.L3122
.L3084:
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
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x0, [x29,24]
	str	x1, [x29,32]
	str	x2, [x29,40]
	bl	rknand_device_lock
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	bl	ftl_vendor_write
	str	x0, [x29,40]
	bl	rknand_device_unlock
	ldr	x0, [x29,40]
	ldp	x29, x30, [sp], 48
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
	.global	FtlCheckVpc
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	stp	x29, x30, [sp, -96]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC106
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 88
	add	x0, x0, :lo12:.LC106
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR4
	adrp	x22, .LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	add	x20, x21, :lo12:.LANCHOR4
	bl	printk
	add	x0, x20, 480
	mov	w1, 0
	mov	x2, 8192
	mov	w19, 0
	bl	memset
.L3158:
	add	x24, x22, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR0
	ldr	w0, [x24,2096]
	cmp	w19, w0
	bcs	.L3175
	mov	w0, w19
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	cmn	w0, #1
	beq	.L3159
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x2, x20, 480
	ldrh	w1, [x2,w0,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x2,w0,sxtw 1]
.L3159:
	add	w19, w19, 1
	b	.L3158
.L3175:
	adrp	x20, .LANCHOR2
	ldrh	w3, [x24,1956]
	adrp	x0, .LC155
	mov	w2, 2
	add	x0, x0, :lo12:.LC155
	add	x22, x21, :lo12:.LANCHOR4
	ldr	x1, [x20,#:lo12:.LANCHOR2]
	mov	w19, 0
	bl	rknand_print_hex
	ldrh	w3, [x24,1956]
	adrp	x0, .LC156
	adrp	x24, .LC157
	add	x0, x0, :lo12:.LC156
	add	x1, x22, 480
	mov	w2, 2
	add	x24, x24, :lo12:.LC157
	bl	rknand_print_hex
.L3161:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w0, [x0,1956]
	cmp	w0, w19
	bls	.L3176
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x20,#:lo12:.LANCHOR2]
	ldrh	w2, [x1,x0]
	add	x0, x22, 480
	ldrh	w3, [x0,w19,sxtw 1]
	cmp	w2, w3
	beq	.L3162
	mov	x0, x24
	mov	w1, w19
	bl	printk
.L3162:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L3161
.L3176:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x0, [x1,24]
	cbz	x0, .L3157
	ldr	x19, [x1,-16]
	adrp	x23, .LC158
	ldrh	w24, [x1,32]
	mov	w22, 0
	sub	x19, x0, x19
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	add	x23, x23, :lo12:.LC158
	add	x21, x21, :lo12:.LANCHOR4
	mov	w25, 6
	madd	x19, x0, x19, x19
	mov	w26, 65535
	uxth	w19, w19
.L3165:
	cmp	w22, w24
	beq	.L3157
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x20,#:lo12:.LANCHOR2]
	ldrh	w2, [x1,x0]
	cbz	w2, .L3166
	add	x3, x21, 480
	mov	x0, x23
	mov	w1, w19
	ldrh	w3, [x3,w19,sxtw 1]
	bl	printk
.L3166:
	add	x0, x20, :lo12:.LANCHOR2
	umull	x19, w19, w25
	ldr	x0, [x0,-16]
	ldrh	w19, [x0,x19]
	cmp	w19, w26
	beq	.L3157
	add	w22, w22, 1
	uxth	w22, w22
	b	.L3165
.L3157:
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
	adrp	x22, .LC159
	adrp	x24, .LC160
	lsl	w25, w23, 10
	ldr	x1, [x0,2072]
	mov	w20, 0
	ldr	x0, [x0,2120]
	add	x22, x22, :lo12:.LC159
	str	x1, [x19,8]
	add	x24, x24, :lo12:.LC160
	str	x0, [x19,16]
.L3178:
	adrp	x0, .LANCHOR0+2020
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2020]
	cmp	w20, w0
	bge	.L3184
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
	beq	.L3179
	add	x1, x21, :lo12:.LANCHOR2
	mov	x0, x24
	mov	w2, 4
	mov	w3, 768
	ldr	x1, [x1,2072]
	bl	rknand_print_hex
.L3179:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L3178
.L3184:
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
	adrp	x23, .LC161
	mov	w25, 56
	ldrh	w21, [x0,1956]
	adrp	x24, .LANCHOR2
	mov	w26, 4
	add	x23, x23, :lo12:.LC161
.L3186:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,1958]
	cmp	w0, w21
	bls	.L3199
	mov	x22, 0
	ldrh	w5, [x2,1948]
	ldrh	w4, [x2,2030]
	mov	w27, w22
	mov	x20, x2
.L3195:
	cmp	w5, w22, uxth
	bls	.L3200
	add	x0, x20, 1976
	mov	w1, w21
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrb	w0, [x0,x22]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x5, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L3187
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
.L3187:
	add	x22, x22, 1
	b	.L3195
.L3200:
	cbz	w27, .L3190
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w27
	mov	w2, 1
	mov	x20, 0
	adrp	x22, .LANCHOR2
	mov	x28, 56
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L3191:
	cmp	w27, w20, uxth
	bls	.L3190
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
	b	.L3191
.L3190:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3186
.L3199:
	adrp	x20, .LANCHOR4
	adrp	x23, .LC109
	add	x20, x20, :lo12:.LANCHOR4
	mov	w22, 0
	adrp	x27, .LANCHOR2
	add	x20, x20, 160
	add	x23, x23, :lo12:.LC109
.L3194:
	add	x21, x27, :lo12:.LANCHOR2
	adrp	x26, .LANCHOR2
	ldrh	w0, [x21,2320]
	cmp	w22, w0
	bge	.L3197
	sbfiz	x24, x22, 1, 32
	mov	w21, 0
.L3198:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2020]
	cmp	w21, w0
	bge	.L3201
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
	b	.L3198
.L3201:
	add	w22, w22, 1
	sxth	w22, w22
	b	.L3194
.L3197:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x21,2176]
	adrp	x0, .LC162
	mov	w2, 2
	add	x0, x0, :lo12:.LC162
	ldr	w3, [x19,2048]
	bl	rknand_print_hex
	ldr	x1, [x21,2216]
	adrp	x0, .LC163
	ldrh	w3, [x19,2056]
	add	x0, x0, :lo12:.LC163
	mov	w2, 4
	bl	rknand_print_hex
	ldr	x1, [x21,2224]
	adrp	x0, .LC164
	ldrh	w3, [x19,2056]
	add	x0, x0, :lo12:.LC164
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
	adrp	x1, .LANCHOR0+1856
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+1856]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3202
	bl	flash_enter_slc_mode
.L3202:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	2
	.global	flash_boot_exit_slc_mode
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	adrp	x1, .LANCHOR0+1856
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x1,#:lo12:.LANCHOR0+1856]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L3204
	bl	flash_exit_slc_mode
.L3204:
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
	mov	w26, w1
	mov	x21, x2
	ldrb	w27, [x0,9]
	mov	w1, w23
	ldrh	w20, [x0,10]
	adrp	x0, .LC165
	add	x0, x0, :lo12:.LC165
	mov	w2, w26
	mov	w25, 0
	mul	w20, w27, w20
	bl	printk
	mov	w6, 4
	adrp	x7, .LANCHOR4
	uxth	w20, w20
	udiv	w24, w23, w20
	msub	w24, w24, w20, w23
	sub	w28, w23, w24
	mul	w3, w24, w27
	ubfx	x3, x3, 2, 2
.L3207:
	cmp	w25, w26
	bcs	.L3213
	add	w5, w25, w24
	add	x0, x19, 196
	ubfx	x5, x5, 2, 16
	sub	w20, w6, w3
	uxth	w20, w20
	ldrh	w4, [x0,w5,sxtw 1]
	ldrb	w0, [x19,120]
	cbz	w0, .L3208
	ldr	w1, [x19,1856]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	csel	w4, w4, w5, ne
.L3208:
	ldr	x0, [x19,744]
	add	w3, w3, w28
	str	x6, [x29,96]
	madd	w22, w4, w27, w3
	ldrb	w1, [x0,9]
	add	x0, x7, :lo12:.LANCHOR4
	ldrb	w4, [x19,1936]
	str	x7, [x29,120]
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
	ldr	x6, [x29,96]
	ldr	x7, [x29,120]
	b	.L3207
.L3213:
	adrp	x0, .LC166
	mov	w1, w23
	mov	w2, w26
	mov	w3, 0
	add	x0, x0, :lo12:.LC166
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
	ldr	x0, [x0,744]
	ldrb	w21, [x0,9]
	ldrh	w20, [x0,10]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	mov	x26, x0
	mov	w0, -1
	cbz	x26, .L3252
	add	w19, w19, 511
	lsr	w19, w19, 9
	cmp	w19, 8
	bls	.L3236
	cmp	w19, 500
	bhi	.L3252
	b	.L3216
.L3236:
	mov	w19, 8
.L3216:
	ldr	w2, [x22]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	mov	w0, -1
	cmp	w2, w1
	bne	.L3252
	mul	w21, w20, w21
	add	x3, x22, 253952
	mov	w2, 63999
	add	x3, x3, 2044
	mov	w4, 4097
	uxth	w21, w21
	add	w0, w21, w0
	add	w0, w0, w19
	udiv	w0, w0, w21
	str	w0, [x29,224]
	mov	w0, 0
.L3220:
	ldr	w1, [x3]
	cbnz	w1, .L3217
	ldr	w1, [x22,w0,uxtw 2]
	add	w0, w0, 1
	cmp	w0, w4
	sub	w2, w2, #1
	csel	w0, w0, wzr, cc
	str	w1, [x3],-4
	cmp	w2, 4096
	bne	.L3220
	b	.L3219
.L3217:
	adrp	x0, .LC167
	add	x0, x0, :lo12:.LC167
	bl	printk
.L3219:
	adrp	x0, .LC168
	mov	w3, 5
	add	x20, x28, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC168
	mov	x1, x23
	mov	w2, 4
	adrp	x24, .LANCHOR4
	bl	rknand_print_hex
	ldr	w1, [x22,512]
	adrp	x0, .LC169
	ldrb	w2, [x20,9]
	add	x0, x0, :lo12:.LC169
	bl	printk
	add	x4, x24, :lo12:.LANCHOR4
	adrp	x0, .LC170
	ldrh	w3, [x20,194]
	mov	w1, w19
	add	x0, x0, :lo12:.LC170
	mov	w2, w19
	ldr	w4, [x4,92]
	bl	printk
	str	x24, [x29,208]
	ldrb	w0, [x20,9]
	ldr	w1, [x22,512]
	cmp	w1, w0
	bls	.L3221
	str	w0, [x22,512]
.L3221:
	lsl	w0, w19, 7
	str	w0, [x29,204]
	adrp	x0, .LC171
	mov	x20, 0
	add	x0, x0, :lo12:.LC171
	str	x0, [x29,192]
	adrp	x0, .LC172
	str	w20, [x29,236]
	add	x0, x0, :lo12:.LC172
	str	x0, [x29,184]
	adrp	x0, .LC173
	add	x0, x0, :lo12:.LC173
	str	x0, [x29,176]
.L3234:
	add	x0, x28, :lo12:.LANCHOR0
	ldr	w1, [x23,x20,lsl 2]
	str	w20, [x29,232]
	ldrb	w0, [x0,9]
	cmp	w1, w0
	bcs	.L3222
	ldr	x0, [x29,208]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w0, [x0,92]
	cmp	w1, w0
	bcc	.L3222
	ldr	w0, [x29,224]
	cmp	w0, 1
	cset	w0, hi
	str	w0, [x29,228]
	uxtw	x0, w20
	cbz	w0, .L3223
	ldr	w0, [x29,228]
	cbz	w0, .L3223
	uxtw	x0, w20
	sub	w0, w0, #1
	ldr	w0, [x23,x0,lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	beq	.L3222
.L3223:
	mov	w1, 0
	mov	x2, 512
	mov	x0, x26
	bl	memset
	ldr	w24, [x23,x20,lsl 2]
	add	x0, x28, :lo12:.LANCHOR0
	mov	w2, w19
	mul	w24, w21, w24
	ldr	x0, [x0,744]
	mov	w1, w24
	ldrb	w27, [x0,9]
	ldrh	w25, [x0,10]
	ldr	x0, [x29,192]
	mul	w25, w27, w25
	bl	printk
	mov	w0, 0
	uxth	w25, w25
	bl	flash_boot_enter_slc_mode
	udiv	w1, w24, w27
	mov	w2, 0
	mov	w0, 0
	bl	FlashEraseBlock
	cmp	w19, w25
	mov	w2, 1
	bls	.L3224
	mov	w2, 0
	mov	w0, 0
	add	w1, w24, w25
	bl	FlashEraseBlock
	mov	w2, 2
.L3224:
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
.L3225:
	cmp	w6, w5
	bcs	.L3263
	add	w0, w6, w8
	lsr	w0, w0, 2
	uxth	w1, w0
	and	w0, w0, 65535
	cbz	w1, .L3226
	add	w2, w0, 1
	add	x1, x4, 196
	ldrb	w3, [x4,120]
	ldrh	w1, [x1,w2,sxtw 1]
	cbz	w3, .L3227
	ldr	w7, [x4,1856]
	mov	w3, 12336
	movk	w3, 0x5638, lsl 16
	cmp	w7, w3
	csel	w1, w1, w2, ne
.L3227:
	add	w1, w1, w10
	lsl	w1, w1, 2
	str	w1, [x29,240]
.L3226:
	add	x1, x4, 196
	str	w11, [x29,244]
	ldrh	w7, [x1,w0,sxtw 1]
	ldrb	w1, [x4,120]
	cbz	w1, .L3228
	ldr	w2, [x4,1856]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w7, w7, w0, ne
.L3228:
	ldr	w0, [x29,216]
	ldrb	w12, [x4,1936]
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
	b	.L3225
.L3263:
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
.L3230:
	ldr	w4, [x29,204]
	mov	w3, w0
	cmp	w0, w4
	bcs	.L3264
	mov	x27, x2
	mov	x25, x1
	mov	x24, x0
	add	x2, x2, 4
	add	x0, x0, 1
	add	x1, x1, 4
	ldr	w5, [x27]
	ldr	w4, [x25]
	cmp	w5, w4
	beq	.L3230
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
	adrp	x0, .LC174
	add	x1, x22, x24
	add	x0, x0, :lo12:.LC174
	mov	w2, 4
	mov	w3, 256
	bl	rknand_print_hex
	adrp	x0, .LC175
	add	x1, x26, x24
	mov	w2, 4
	mov	w3, 256
	add	x0, x0, :lo12:.LC175
	bl	rknand_print_hex
	mov	w0, 0
	bl	flash_boot_enter_slc_mode
	ldr	w1, [x23,x20,lsl 2]
	mov	w0, 0
	mov	w2, 0
	mul	w1, w21, w1
	bl	FlashEraseBlock
	ldr	w0, [x29,228]
	cbz	w0, .L3231
	ldr	w1, [x23,x20,lsl 2]
	mov	w0, 0
	mov	w2, 0
	madd	w1, w1, w21, w21
	bl	FlashEraseBlock
.L3231:
	mov	w0, 0
	bl	flash_boot_exit_slc_mode
	adrp	x0, .LC176
	ldr	w1, [x23,x20,lsl 2]
	add	x0, x0, :lo12:.LC176
	bl	printk
	b	.L3222
.L3264:
	ldr	w0, [x29,236]
	add	w0, w0, 1
	str	w0, [x29,236]
.L3222:
	add	x20, x20, 1
	cmp	x20, 5
	bne	.L3234
	mov	x0, x26
	bl	ftl_free
	ldr	w0, [x29,236]
	cmp	w0, wzr
	csetm	w0, eq
.L3252:
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
.L3266:
	cmp	w1, w2
	bls	.L3268
	ldrb	w4, [x5,x2]
	add	x6, x3, 3176
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3266
.L3268:
	ret
	.size	CRC_32, .-CRC_32
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	stp	x29, x30, [sp, -96]!
	cmp	w0, 64
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	mov	w19, w0
	mov	w21, w1
	mov	x23, x2
	adrp	x20, .LANCHOR5
	bne	.L3270
	ldr	w1, [x2]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L3270
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
.L3270:
	add	x3, x20, :lo12:.LANCHOR5
	ldrb	w0, [x3,-32]
	cbz	w0, .L3269
	sub	w0, w19, #64
	ldr	x22, [x3,-24]
	cmp	w0, 499
	bhi	.L3272
	mov	w3, 564
	ubfiz	x0, x0, 9, 25
	sub	w3, w3, w19
	add	x0, x22, x0
	cmp	w3, w21
	mov	x1, x23
	csel	w2, w3, w21, ls
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L3273
.L3272:
	cmp	w19, 563
	bhi	.L3283
.L3273:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,-16]
	cmp	w1, w19
	beq	.L3281
	strb	wzr, [x0,-32]
	cbz	x22, .L3282
	mov	x0, x22
	bl	ftl_free
.L3282:
	add	x0, x20, :lo12:.LANCHOR5
	str	xzr, [x0,-24]
.L3281:
	add	x20, x20, :lo12:.LANCHOR5
	add	w19, w19, w21
	str	w19, [x20,-16]
	b	.L3269
.L3283:
	ldr	w2, [x3,-16]
	mov	w0, 500
	sub	w2, w2, #64
	cmp	w2, 500
	csel	w2, w2, w0, ls
	adrp	x0, .LANCHOR0+744
	ldr	x0, [x0,#:lo12:.LANCHOR0+744]
	ldrb	w0, [x0,9]
	cmp	w0, 4
	beq	.L3284
	mov	w0, 2
	str	w0, [x29,64]
	mov	w0, 3
	str	w0, [x29,68]
	mov	w0, 4
	str	w0, [x29,72]
	mov	w0, 5
	str	w0, [x29,76]
	mov	w0, 6
	str	w0, [x29,80]
	b	.L3275
.L3284:
	mov	x3, 0
.L3274:
	cmp	w2, 256
	add	x0, x29, 64
	bls	.L3276
	lsl	w1, w3, 1
	str	w1, [x0,x3,lsl 2]
	b	.L3277
.L3276:
	str	w3, [x0,x3,lsl 2]
.L3277:
	add	x3, x3, 1
	cmp	x3, 5
	bne	.L3274
.L3275:
	add	x0, x20, :lo12:.LANCHOR5
	mov	w4, 63999
	ldr	x3, [x0,-8]
	add	x3, x3, 61440
	add	x3, x3, 2559
.L3280:
	ldrb	w0, [x3]
	cbz	w0, .L3278
	add	w2, w4, 1
	lsl	w0, w2, 2
	b	.L3279
.L3278:
	sub	w4, w4, #1
	sub	x3, x3, #1
	cmp	w4, 4096
	bne	.L3280
	lsl	w0, w2, 9
.L3279:
	mov	x1, x22
	add	x2, x29, 64
	add	x22, x20, :lo12:.LANCHOR5
	bl	write_idblock
	ldr	x0, [x22,-24]
	strb	wzr, [x22,-32]
	bl	ftl_free
	str	xzr, [x22,-24]
	b	.L3281
.L3269:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 96
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
	bhi	.L3294
	cbnz	w21, .L3294
	mov	w0, w1
	mov	w1, w2
	mov	x2, x3
	str	x3, [x29,56]
	bl	write_loader_lba
	ldr	x3, [x29,56]
.L3294:
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
	beq	.L3303
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3304
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3305
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3306
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3307
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3308
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3365
	bl	rknand_dev_flush
	b	.L3399
.L3308:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3310
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3311
	b	.L3365
.L3306:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3312
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3313
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3314
	b	.L3365
.L3313:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3312
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3312
	b	.L3365
.L3304:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3315
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3316
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3317
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3318
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3319
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3320
	b	.L3365
.L3316:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3321
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3322
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3323
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3324
	b	.L3365
.L3322:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3324
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3321
	b	.L3365
.L3318:
	adrp	x0, .LC177
	add	x0, x0, :lo12:.LC177
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3325
.L3330:
	mov	x0, -12
	b	.L3302
.L3325:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3406
	adrp	x0, .LC179
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC179
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3403
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
	cbz	x0, .L3409
	adrp	x0, .LC180
	add	x0, x0, :lo12:.LC180
.L3401:
	bl	printk
.L3403:
	mov	x0, x19
.L3404:
	bl	ftl_free
.L3402:
	mov	x0, -14
	b	.L3302
.L3317:
	adrp	x0, .LC181
	add	x0, x0, :lo12:.LC181
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3330
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3406
	adrp	x0, .LC182
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC182
	bl	printk
	adrp	x0, .LANCHOR5
	add	x22, x0, :lo12:.LANCHOR5
	mov	x20, x0
	ldr	x21, [x22,-8]
	cbnz	x21, .L3332
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x22,-8]
	cbz	x0, .L3403
	mov	w1, w21
	mov	x2, 260096
	bl	memset
.L3332:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3403
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3403
	add	x20, x20, :lo12:.LANCHOR5
	uxtw	x2, w2
	ldr	x1, [x20,-8]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3409:
	mov	x0, x19
	bl	ftl_free
	b	.L3399
.L3320:
	adrp	x0, .LC183
	add	x0, x0, :lo12:.LC183
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3330
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3335
.L3406:
	adrp	x0, .LC178
	add	x0, x0, :lo12:.LC178
	b	.L3401
.L3335:
	ldr	w1, [x19]
	adrp	x0, .LC184
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC184
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3403
	adrp	x20, .LANCHOR5
	add	x20, x20, :lo12:.LANCHOR5
	ldr	x0, [x20,-8]
	cbz	x0, .L3403
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3338
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3302
.L3338:
	bl	rknand_device_lock
	ldr	x1, [x20,-8]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,-8]
	bl	ftl_free
	str	xzr, [x20,-8]
	b	.L3409
.L3319:
	adrp	x0, .LC185
	add	x0, x0, :lo12:.LC185
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3330
	bl	ftl_read_flash_info
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3411
.L3303:
	adrp	x0, .LC186
	add	x0, x0, :lo12:.LC186
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3330
	mov	w1, 0
	mov	w2, 64
	bl	ftl_memset
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3411
.L3315:
	adrp	x0, .LC187
	add	x0, x0, :lo12:.LC187
	bl	printk
	adrp	x0, .LANCHOR5
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR5]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
.L3394:
	bl	rk_copy_to_user
	cbnz	x0, .L3402
	b	.L3399
.L3323:
	adrp	x0, .LC188
	add	x0, x0, :lo12:.LC188
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3330
	bl	rknand_device_lock
	mov	w1, 2
	mov	x2, x21
	mov	w0, 520
	bl	FtlVendorPartRead
	bl	rknand_device_unlock
	mov	x0, x20
	mov	x1, x21
	mov	x2, 1024
.L3411:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3404
	bl	ftl_free
	b	.L3329
.L3307:
	adrp	x0, .LC189
	add	x0, x0, :lo12:.LC189
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3343
.L3348:
	adrp	x0, .LC178
	add	x0, x0, :lo12:.LC178
	bl	printk
	b	.L3402
.L3343:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3344
.L3345:
	mov	x19, -1
	b	.L3329
.L3344:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3345
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
	beq	.L3346
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3346:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
	b	.L3408
.L3310:
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3348
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3345
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3345
	adrp	x2, .LANCHOR5
	mov	w1, 5161
	add	x0, x2, :lo12:.LANCHOR5
	movk	w1, 0xc059, lsl 16
	ldr	w3, [x0,8]
	mov	x0, -2
	cmp	w3, w1
	bne	.L3302
	ldr	w1, [x29,68]
	mov	x0, -3
	sub	w3, w1, #1
	cmp	w3, 127
	bhi	.L3302
	ldr	x19, [x2,#:lo12:.LANCHOR5]
	add	x0, x19, 64
	str	w1, [x19,12]
	add	x1, x29, 120
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3400
.L3314:
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3348
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3345
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3345
	adrp	x19, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR5
	ldr	w1, [x0,12]
	cbnz	w1, .L3349
.L3352:
	mov	x0, 0
	b	.L3302
.L3349:
	ldr	x1, [x0,16]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3350
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,16]
	str	w2, [x1,4]
	ldr	x0, [x0,16]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3350:
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
	beq	.L3351
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,#:lo12:.LANCHOR5]
	str	w1, [x0,4]
	ldr	x0, [x20,#:lo12:.LANCHOR5]
	str	wzr, [x0,8]
.L3351:
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
	b	.L3399
.L3305:
	adrp	x0, .LC192
	add	x0, x0, :lo12:.LC192
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3348
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3345
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3345
	adrp	x19, .LANCHOR5
	add	x1, x19, :lo12:.LANCHOR5
	ldr	w2, [x1,12]
	cmp	w2, 1
	beq	.L3352
	ldr	x2, [x1,16]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3353
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,16]
	str	w3, [x2,4]
	ldr	x1, [x1,16]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3353:
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
	beq	.L3354
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,#:lo12:.LANCHOR5]
	str	w1, [x0,4]
	ldr	x0, [x20,#:lo12:.LANCHOR5]
	str	wzr, [x0,8]
.L3354:
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
	b	.L3399
.L3311:
	adrp	x0, .LC193
	add	x0, x0, :lo12:.LC193
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3348
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3345
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3345
	adrp	x1, .LANCHOR5
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR5
	uxtw	x2, w2
	add	x1, x1, 24
	b	.L3407
.L3312:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3355
	adrp	x0, .LC194
	add	x0, x0, :lo12:.LC194
	b	.L3397
.L3355:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3357
	adrp	x0, .LC195
	add	x0, x0, :lo12:.LC195
	b	.L3397
.L3357:
	adrp	x0, .LC196
	add	x0, x0, :lo12:.LC196
.L3397:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3348
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3402
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR5
	bne	.L3358
	ldr	x0, [x0,#:lo12:.LANCHOR5]
	add	x1, x29, 56
	mov	x2, 16
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3302
	b	.L3402
.L3358:
	add	x20, x0, :lo12:.LANCHOR5
	ldr	w1, [x20,536]
	cmp	w1, 10
	bhi	.L3402
	ldr	x1, [x0,#:lo12:.LANCHOR5]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3359
	cbz	w3, .L3359
	adrp	x0, .LC197
	mov	w1, w2
	add	x0, x0, :lo12:.LC197
	bl	printk
	ldr	w0, [x20,536]
	add	w0, w0, 1
	str	w0, [x20,536]
	b	.L3402
.L3359:
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,536]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3360
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3361
.L3360:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3361:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3399
	b	.L3329
.L3324:
	adrp	x0, .LC198
	add	x0, x0, :lo12:.LC198
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3348
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3345
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3345
	mov	w0, 30224
	adrp	x1, .LANCHOR5
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR5
	add	x0, x29, 64
	bne	.L3362
	ldr	x1, [x1,544]
	b	.L3398
.L3362:
	ldr	x1, [x1,552]
.L3398:
	add	x1, x1, 8
.L3407:
	bl	memcpy
.L3408:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
	b	.L3394
.L3321:
	adrp	x0, .LC199
	add	x0, x0, :lo12:.LC199
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3348
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3345
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3345
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	bne	.L3364
	ldr	x0, [x19,544]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,544]
	b	.L3400
.L3364:
	ldr	x0, [x19,552]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,552]
	mov	w0, 3
.L3400:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3329
.L3399:
	mov	x19, 0
.L3329:
	adrp	x0, .LC200
	mov	x1, x19
	add	x0, x0, :lo12:.LC200
	bl	printk
	mov	x0, x19
	b	.L3302
.L3365:
	mov	x0, -22
.L3302:
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
	str	w0, [x19,-16]
	adrp	x0, .LANCHOR4+456
	strb	wzr, [x19,-32]
	str	xzr, [x19,-24]
	ldr	x1, [x0,#:lo12:.LANCHOR4+456]
	str	x1, [x19,16]
	add	x0, x1, 512
	str	x0, [x20,#:lo12:.LANCHOR5]
	add	x0, x1, 1024
	add	x1, x1, 1536
	str	x0, [x19,544]
	add	x0, x19, 24
	str	xzr, [x19,-8]
	str	x1, [x19,552]
	bl	ftl_memcpy
	ldr	x21, [x19,16]
	str	wzr, [x19,8]
	str	wzr, [x19,536]
	ldr	w0, [x21,16]
	ldr	w22, [x21,508]
	str	w0, [x19,12]
	cbz	w22, .L3414
	mov	x0, x21
	mov	w1, 508
	bl	JSHash
	cmp	w22, w0
	beq	.L3414
	adrp	x0, .LC201
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC201
	str	wzr, [x19,12]
	bl	printk
.L3414:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,12]
	cbz	w1, .L3415
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,8]
.L3415:
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
	cbz	x0, .L3424
	mov	w23, 0
	adrp	x24, .LC202
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC202
.L3428:
	add	x20, x21, :lo12:.LANCHOR5
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,560]
	bl	FlashBootVendorRead
	cbnz	w0, .L3425
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
	bne	.L3426
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3426
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L3426:
	cbnz	w22, .L3436
	mov	w22, 1
	b	.L3428
.L3436:
	cbz	w19, .L3429
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L3424
	b	.L3425
.L3429:
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
	b	.L3424
.L3425:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w19, -1
	ldr	x0, [x21,560]
	bl	kfree
	str	xzr, [x21,560]
.L3424:
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
	cbz	x5, .L3438
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3439:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3443
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3439
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
	b	.L3438
.L3443:
	mov	w3, -1
.L3438:
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
	cbz	x19, .L3445
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3446:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3461
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3446
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3447
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3445
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3448:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3462
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
	b	.L3448
.L3462:
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
	b	.L3460
.L3447:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3460
.L3461:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3445
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
.L3460:
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
.L3445:
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
	cbz	x0, .L3464
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3466
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3467
	b	.L3478
.L3466:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3478
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3469
.L3470:
	mov	x20, -1
	b	.L3465
.L3469:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3470
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3465
.L3467:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3478
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3470
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3470
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3478
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3465
.L3478:
	mov	x20, -14
.L3465:
	mov	x0, x19
	bl	kfree
.L3464:
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
	.type	__func__.19528, %object
	.size	__func__.19528, 11
__func__.19528:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.20301, %object
	.size	__func__.20301, 17
__func__.20301:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.20320, %object
	.size	__func__.20320, 16
__func__.20320:
	.string	"FtlScanAllBlock"
	.type	__func__.20568, %object
	.size	__func__.20568, 21
__func__.20568:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.19601, %object
	.size	__func__.19601, 8
__func__.19601:
	.string	"FtlInit"
	.type	__func__.20269, %object
	.size	__func__.20269, 12
__func__.20269:
	.string	"FtlCheckVpc"
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
	.string	"FTL version: 5.0.50 20180905"
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
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC114:
	.string	"FlashMakeFactorBbt %d\n"
.LC115:
	.string	"bad block:%d %d\n"
.LC116:
	.string	"FMFB:%d %d\n"
.LC117:
	.string	"E:bad block:%d\n"
.LC118:
	.string	"FMFB:Save %d %d\n"
.LC119:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC120:
	.string	"FtlBbmTblFlush error:%x\n"
.LC121:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC122:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC123:
	.string	"decrement_vpc_count %x = %d\n"
.LC124:
	.string	"decrement_vpc_count %x = %d in free list\n"
.LC125:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC126:
	.string	"page map lost: %x %x\n"
.LC127:
	.string	"FtlMapWritePage error = %x\n"
.LC128:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC129:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC130:
	.string	"no ect"
.LC131:
	.string	"slc mode"
.LC132:
	.string	"BBT:"
.LC133:
	.string	"region_id = %x phyAddr = %x\n"
.LC134:
	.string	"map_ppn:"
.LC135:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC136:
	.string	"Ftlscanalldata = %x\n"
.LC137:
	.string	"scan lpa = %x ppa= %x\n"
.LC138:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC139:
	.string	"RSB refresh addr %x\n"
.LC140:
	.string	"spuer block %x vpn is 0\n "
.LC141:
	.string	"g_recovery_ppa %x ver %x\n "
.LC142:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC143:
	.string	"GC des block %x done\n"
.LC144:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC145:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC146:
	.string	"g_gc_superblock_free %x %x %x %x %x\n"
.LC147:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC148:
	.string	"rk_ftl_de_init %x\n"
.LC149:
	.string	"...%s: no bad block mapping table, format device\n"
.LC150:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
.LC151:
	.string	"FtlInit %x\n"
.LC152:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC153:
	.string	"erase power lost blk = %x vpc=%x\n"
.LC154:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC155:
	.string	"vpc1"
.LC156:
	.string	"vpc2"
.LC157:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC158:
	.string	"free blk vpc error %x = %x  %x\n"
.LC159:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\n"
.LC160:
	.string	":"
.LC161:
	.string	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC162:
	.string	"Mblk:"
.LC163:
	.string	"L2P:"
.LC164:
	.string	"L2PC:"
.LC165:
	.string	"IdBlockReadData %x %x\n"
.LC166:
	.string	"IdBlockReadData %x %x ret= %x\n"
.LC167:
	.string	"write_idblock fix data %x %x\n"
.LC168:
	.string	"idblk:"
.LC169:
	.string	"idb reverse %x %x\n"
.LC170:
	.string	"write_idblock totle_sec %x %x %x %x\n"
.LC171:
	.string	"IDBlockWriteData %x %x\n"
.LC172:
	.string	"IDBlockWriteData %x %x ret= %x\n"
.LC173:
	.string	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x\n"
.LC174:
	.string	"write"
.LC175:
	.string	"read"
.LC176:
	.string	"write_idblock error %d\n"
.LC177:
	.string	"READ_SECTOR_IO\n"
.LC178:
	.string	"rk_copy_from_user error\n"
.LC179:
	.string	"READ_SECTOR_IO %x %x\n"
.LC180:
	.string	"rk_copy_to_user error\n"
.LC181:
	.string	"WRITE_SECTOR_IO\n"
.LC182:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC183:
	.string	"END_WRITE_SECTOR_IO\n"
.LC184:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC185:
	.string	"GET_FLASH_INFO_IO\n"
.LC186:
	.string	"GET_BAD_BLOCK_IO\n"
.LC187:
	.string	"GET_LOCK_FLAG_IO\n"
.LC188:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC189:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC190:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC191:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC192:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC193:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC194:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC195:
	.string	"RKNAND_LOADER_STATUS\n"
.LC196:
	.string	"RKNAND_LOADER_LOCK\n"
.LC197:
	.string	"LockKey not match %d\n"
.LC198:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC199:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC200:
	.string	"return ret = %lx\n"
.LC201:
	.string	"secureBootEn check error\n"
.LC202:
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
	.size	NandFlashParaTbl, 2464
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
	.type	g_nandc_version_data, %object
	.size	g_nandc_version_data, 4
g_nandc_version_data:
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
	.type	check_valid_page_count_table, %object
	.size	check_valid_page_count_table, 8192
check_valid_page_count_table:
	.zero	8192
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
