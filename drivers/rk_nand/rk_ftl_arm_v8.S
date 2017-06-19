/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2017-05-26
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
	.type	rknand_sys_storage_open, %function
rknand_sys_storage_open:
	mov	w0, 0
	ret
	.size	rknand_sys_storage_open, .-rknand_sys_storage_open
	.align	2
	.type	rknand_sys_storage_release, %function
rknand_sys_storage_release:
	mov	w0, 0
	ret
	.size	rknand_sys_storage_release, .-rknand_sys_storage_release
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
	cbz	w0, .L9
	ldrb	w4, [x5,1]
	mov	w0, 0
	ldrb	w3, [x1,1]
	cmp	w4, w3
	beq	.L6
.L9:
	mov	x3, 0
.L7:
	cmp	w3, w2
	mov	w4, w3
	bcs	.L13
	ldrb	w0, [x5,x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6,-1]
	cmp	w6, w0
	beq	.L7
	add	w0, w4, 1
	b	.L6
.L13:
	mov	w0, 0
.L6:
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
	cbz	w2, .L16
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w19, -1073741824
	csel	w19, w1, w19, ne
.L16:
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
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	mov	w20, w1
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x0,20]
	cmp	w2, 5
	bls	.L23
	adrp	x2, .LANCHOR1
	and	w20, w1, 127
	add	x2, x2, :lo12:.LANCHOR1
	ldrb	w0, [x0,16]
	ldrh	w20, [x2,w20,uxtw 1]
	cbz	w0, .L24
	mov	w0, w21
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w20, -1073741824
	csel	w20, w1, w20, ne
.L24:
	add	x19, x19, :lo12:.LANCHOR0
	add	x21, x19, x21, sxtw 4
	ldr	x0, [x21,24]
	str	w20, [x0,336]
	b	.L22
.L23:
	cmp	w2, 4
	bne	.L22
	adrp	x2, .LANCHOR1
	and	w3, w1, 127
	add	x2, x2, :lo12:.LANCHOR1
	ldrb	w0, [x0,16]
	ldrh	w22, [x2,w3,uxtw 1]
	lsl	w22, w22, 8
	cbz	w0, .L26
	mov	w0, w21
	bl	FlashRsvdBlkChk
	cbz	w0, .L26
	ubfiz	w20, w20, 1, 7
	orr	w20, w20, 1
	orr	w22, w22, w20
.L26:
	add	x19, x19, :lo12:.LANCHOR0
	add	x21, x19, x21, sxtw 4
	ldr	x0, [x21,24]
	str	w22, [x0,336]
.L22:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
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
	bne	.L40
	mov	w6, 6
	str	w6, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w5, [x2,2052]
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	b	.L42
.L40:
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
.L42:
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
	cbz	w2, .L43
	sbfiz	x0, x0, 4, 32
	add	x1, x1, 24
	add	x2, x1, x0
	ldr	x1, [x1,x0]
	ldrb	w0, [x2,8]
	add	x0, x0, 8
	add	x0, x1, x0, lsl 8
	mov	w1, 218
	str	w1, [x0,8]
.L43:
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
	cbz	w2, .L48
	sbfiz	x0, x0, 4, 32
	add	x1, x1, 24
	add	x2, x1, x0
	ldr	x1, [x1,x0]
	ldrb	w0, [x2,8]
	add	x0, x0, 8
	add	x0, x1, x0, lsl 8
	mov	w1, 223
	str	w1, [x0,8]
.L48:
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
	cbz	w2, .L55
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
.L55:
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
.L63:
	cmp	w1, w2
	bls	.L65
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L63
.L65:
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
	bne	.L71
	add	x1, x7, :lo12:.LANCHOR0
	ldrb	w5, [x1,120]
	cbnz	w5, .L71
	add	x1, x1, 196
	ldrh	w8, [x1,w8,sxtw 1]
.L71:
	add	x7, x7, :lo12:.LANCHOR0
	cmp	w4, 1
	add	x7, x7, 708
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
	beq	.L84
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
	b	.L86
.L84:
	and	w19, w19, -8193
.L86:
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
.L93:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L93
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
.L97:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,753]
	cmp	w0, w20
	bls	.L103
	add	x0, x19, 256
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	NandcDelayns
	ldrb	w0, [x25,752]
	cmp	w0, 34
	bne	.L98
	ldrsb	w0, [x24,x20]
	b	.L102
.L98:
	cmp	w0, 35
	bne	.L100
	ldrsb	w0, [x21,x20]
	b	.L102
.L100:
	ldrsb	w0, [x23,400]
.L102:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L97
.L103:
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
.L105:
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,753]
	cmp	w0, w20
	bls	.L107
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
	b	.L105
.L107:
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
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	add	x19, x22, :lo12:.LANCHOR0
	uxtb	w27, w1
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	uxtb	x23, w0
	mov	x25, x2
	ldr	x0, [x19,744]
	mov	x26, x23
	uxtb	w24, w3
	add	x19, x19, 760
	ldrb	w0, [x0,19]
	cmp	w0, 6
	bne	.L109
	add	x19, x19, x23, lsl 6
	lsl	w0, w24, 2
	add	x19, x19, 20
	add	x19, x19, x0, sxtw
	b	.L110
.L109:
	lsl	x2, x23, 3
	add	x2, x2, x24, sxtw
	add	x19, x19, x2, lsl 3
	add	x19, x19, 20
.L110:
	add	x2, x22, :lo12:.LANCHOR0
	lsl	x0, x23, 4
	add	x2, x2, 24
	uxtb	x27, w27
	add	x1, x2, x0
	ldr	x20, [x2,x0]
	mov	w0, w26
	ldrb	w21, [x1,8]
	bl	NandcFlashCs
	ubfiz	x21, x21, 8, 8
	mov	w0, 54
	add	x20, x20, x21
	mov	x21, 0
	str	w0, [x20,2056]
.L111:
	cmp	x21, x27
	beq	.L113
	ldrb	w0, [x25,x21]
	str	w0, [x20,2052]
	mov	w0, 200
	bl	NandcDelayns
	ldrb	w0, [x19,x21]
	add	x21, x21, 1
	str	w0, [x20,2048]
	b	.L111
.L113:
	add	x22, x22, :lo12:.LANCHOR0
	mov	w0, 22
	add	x23, x22, x23
	str	w0, [x20,2056]
	mov	w0, w26
	bl	NandcFlashDeCs
	strb	w24, [x23,1616]
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
	cmp	w0, 5
	bhi	.L114
	add	x21, x19, 1620
	add	x22, x19, 764
.L119:
	lsl	x1, x20, 3
	uxtb	w0, w20
	ldrb	w1, [x1,x21]
	cmp	w1, 173
	bne	.L116
	ldrb	w1, [x19,761]
	mov	x2, x22
	mov	w3, 0
	bl	HynixSetRRPara
.L116:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L119
.L114:
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
	bne	.L122
	add	x4, x4, 88
	cbnz	w2, .L123
	ldrb	w2, [x4,13]
	b	.L131
.L123:
	ldrb	w2, [x4,14]
.L131:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,103]
	cbz	w4, .L126
	add	x3, x19, 8
	lsl	x3, x3, 8
.L130:
	cmp	w2, w4
	bcs	.L126
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L130
.L122:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L126:
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
.L136:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L136
	tbz	x3, 6, .L136
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
	cbz	x0, .L141
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
	cbz	x1, .L141
	str	w0, [x1]
	str	xzr, [x19,16]
.L141:
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
.L148:
	mov	w0, 100
	bl	NandcDelayns
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L149
	subs	w19, w19, #1
	bne	.L148
	mov	w0, -1
	b	.L147
.L149:
	mov	w0, 0
.L147:
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
.L179:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,753]
	cmp	w4, w2
	bls	.L184
	ldrb	w3, [x3,752]
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
.L186:
	cmp	w22, w24
	beq	.L195
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,1845]
	cmp	w2, w0
	bcc	.L187
	mov	w0, -1
	str	w0, [x20]
	b	.L188
.L187:
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
	tbz	x0, 0, .L189
	mov	w0, -1
	str	w0, [x20]
.L189:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L190
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L190:
	mov	w0, w19
	bl	NandcFlashDeCs
.L188:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L186
.L195:
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
	strb	w3, [x1,6]
	strb	w0, [x1,7]
	bne	.L197
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+521
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+521]
	b	.L234
.L197:
	cmp	w26, 3
	bne	.L199
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
	b	.L250
.L199:
	cmp	w26, 4
	bne	.L200
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
.L250:
	mov	w21, 8
	strb	w0, [x1,11]
	mov	w20, w21
	b	.L198
.L200:
	cmp	w26, 5
	bne	.L201
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w21, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L249
.L201:
	cmp	w26, 6
	bne	.L234
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w21, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L249
.L234:
	mov	w21, 7
.L249:
	mov	w20, 4
.L198:
	sub	w0, w26, #1
	cmp	w0, 1
	bhi	.L246
	adrp	x24, .LANCHOR1
	add	x27, x19, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LANCHOR1
	mov	w23, 0
	add	x28, x27, 760
	add	x4, x27, 24
	add	x24, x24, 504
.L202:
	ldrb	w0, [x27,1845]
	cmp	w0, w23
	bls	.L209
	add	x0, x27, x23, sxtw
	ldrb	w0, [x0,1848]
	mov	x25, 0
	add	x22, x28, x0, lsl 6
	lsl	x0, x0, 4
	add	x1, x4, x0
	add	x22, x22, 20
	ldr	x26, [x4,x0]
	ldrb	w0, [x1,8]
	mov	w1, 55
	add	x26, x26, x0, lsl 8
.L204:
	add	x0, x28, x25
	str	w1, [x26,2056]
	str	x4, [x29,128]
	str	x1, [x29,136]
	ldrb	w0, [x0,4]
	str	w0, [x26,2052]
	mov	w0, 80
	bl	NandcDelayns
	ldr	w0, [x26,2048]
	strb	w0, [x22,x25]
	add	x25, x25, 1
	ldr	x1, [x29,136]
	cmp	w20, w25, uxtb
	ldr	x4, [x29,128]
	bhi	.L204
	mov	x0, 0
.L205:
	add	w2, w0, 8
	mov	x1, 0
.L206:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w5, [x3,4]
	ldrb	w3, [x22,x0]
	add	w3, w5, w3
	strb	w3, [x22,w2,sxtw]
	add	w2, w2, 8
	bne	.L206
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L205
	add	w23, w23, 1
	strb	wzr, [x22,16]
	strb	wzr, [x22,24]
	strb	wzr, [x22,32]
	uxtb	w23, w23
	strb	wzr, [x22,40]
	strb	wzr, [x22,48]
	strb	wzr, [x22,41]
	strb	wzr, [x22,49]
	b	.L202
.L246:
	sub	w0, w26, #3
	cmp	w0, 3
	bhi	.L209
	mul	w0, w20, w21
	cmp	w26, 6
	mov	w1, 4
	asr	w24, w0, 2
	lsl	w0, w0, 4
	str	w0, [x29,124]
	lsl	w0, w24, 1
	str	w0, [x29,116]
	mov	w0, 8
	csel	w0, w1, w0, eq
	add	x23, x19, :lo12:.LANCHOR0
	str	w0, [x29,120]
	add	x0, x23, 760
	str	x0, [x29,136]
	sub	w0, w20, #1
	mov	w22, 0
	sub	w27, w26, #5
	uxtb	x0, w0
	add	x0, x0, 1
	str	x0, [x29,104]
.L210:
	ldrb	w0, [x23,1845]
	cmp	w0, w22
	bhi	.L233
.L209:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	strb	w21, [x19,762]
	strb	w20, [x19,761]
	ldp	x21, x22, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L233:
	add	x0, x23, x22, sxtw
	ldrb	w0, [x0,1848]
	ldr	x1, [x29,136]
	add	x3, x23, 24
	mov	x25, x0
	add	x28, x1, x0, lsl 6
	lsl	x0, x0, 4
	add	x1, x28, 20
	str	x1, [x29,128]
	add	x1, x3, x0
	ldr	x0, [x3,x0]
	mov	w3, 255
	str	x3, [x29,96]
	ldrb	w1, [x1,8]
	add	x28, x0, x1, lsl 8
	mov	w0, w25
	str	w3, [x28,2056]
	bl	NandcWaitFlashReady
	mov	w0, 54
	cmp	w26, 4
	str	w0, [x28,2056]
	ldr	x3, [x29,96]
	bne	.L211
	mov	w0, 64
	str	w3, [x28,2052]
	str	w0, [x28,2048]
	mov	w0, 204
	b	.L251
.L211:
	cmp	w27, 1
	bhi	.L213
	ldr	x0, [x29,136]
	ldrb	w0, [x0,4]
	str	w0, [x28,2052]
	mov	w0, 82
	b	.L252
.L213:
	mov	w0, 174
	str	w0, [x28,2052]
	str	wzr, [x28,2048]
	mov	w0, 176
.L251:
	str	w0, [x28,2052]
	mov	w0, 77
.L252:
	str	w0, [x28,2048]
	mov	w0, 22
	str	w0, [x28,2056]
	mov	w0, 23
	str	w0, [x28,2056]
	mov	w0, 4
	str	w0, [x28,2056]
	mov	w0, 25
	str	w0, [x28,2056]
	cmp	w26, 6
	str	wzr, [x28,2056]
	str	wzr, [x28,2052]
	str	wzr, [x28,2052]
	bne	.L214
	mov	w0, 31
	str	w0, [x28,2052]
	b	.L215
.L214:
	str	wzr, [x28,2052]
.L215:
	mov	w3, 2
	str	w3, [x28,2052]
	str	wzr, [x28,2052]
	mov	w0, 48
	str	w0, [x28,2056]
	mov	w0, w25
	str	x3, [x29,96]
	bl	NandcWaitFlashReady
	ldr	x3, [x29,96]
	mov	w0, 16
	cmp	w27, 1
	csel	w3, w3, w0, hi
	mov	w0, 0
.L217:
	add	w0, w0, 1
	ldr	w4, [x28,2048]
	uxtb	w0, w0
	cmp	w0, w3
	bne	.L217
	ldr	x6, [x23,1856]
	mov	x0, 0
.L218:
	ldr	w1, [x29,124]
	cmp	w1, w0
	ble	.L253
	ldr	w3, [x28,2048]
	strb	w3, [x6,x0]
	add	x0, x0, 1
	b	.L218
.L253:
	ldr	x8, [x23,1856]
	mov	w5, w24
	mov	w4, 8
.L221:
	mov	w0, 0
.L220:
	add	w3, w0, w5
	add	w0, w0, 1
	sbfiz	x3, x3, 2, 32
	cmp	w0, w24
	ldr	w7, [x8,x3]
	mvn	w7, w7
	str	w7, [x8,x3]
	bne	.L220
	ldr	w0, [x29,116]
	subs	w4, w4, #1
	add	w5, w5, w0
	bne	.L221
	mov	x4, 0
	mov	w12, 1
.L222:
	mov	w3, 0
	ldr	x11, [x23,1856]
	mov	w7, w3
.L226:
	lsl	w5, w12, w7
	mov	w9, w4
	mov	w0, 16
	mov	w8, 0
.L224:
	ldr	w10, [x11,w9,sxtw 2]
	add	w9, w9, w24
	and	w10, w5, w10
	cmp	w10, w5
	csinc	w8, w8, w8, ne
	subs	w0, w0, #1
	bne	.L224
	cmp	w8, 9
	orr	w5, w3, w5
	add	w7, w7, 1
	csel	w3, w5, w3, cs
	cmp	w7, 32
	bne	.L226
	str	w3, [x11,x4,lsl 2]
	add	x4, x4, 1
	cmp	w24, w4
	bgt	.L222
	mov	w4, w0
.L229:
	mov	x3, 0
.L228:
	add	w5, w0, w3
	ldr	x1, [x29,128]
	ldrb	w7, [x6,x3]
	add	x3, x3, 1
	cmp	w20, w3, uxtb
	strb	w7, [x1,w5,sxtw]
	bhi	.L228
	ldr	x1, [x29,104]
	add	w4, w4, 1
	cmp	w4, w21
	add	x6, x6, x1
	ldr	w1, [x29,120]
	add	w0, w0, w1
	blt	.L229
	mov	w0, 255
	str	w0, [x28,2056]
	mov	w0, w25
	bl	NandcWaitFlashReady
	cmp	w27, 1
	bhi	.L231
	mov	w0, 54
	str	w0, [x28,2056]
	ldr	x0, [x29,136]
	mov	w1, -1
	ldrb	w0, [x0,4]
	str	w0, [x28,2052]
	mov	w0, 22
	str	wzr, [x28,2048]
	str	w0, [x28,2056]
	mov	w0, w22
	bl	FlashReadCmd
	b	.L232
.L231:
	mov	w0, 56
	str	w0, [x28,2056]
.L232:
	mov	w0, w25
	add	w22, w22, 1
	bl	NandcWaitFlashReady
	uxtb	w22, w22
	b	.L210
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
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
	bne	.L255
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
	b	.L256
.L255:
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
.L256:
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
	ldrb	w1, [x20,1864]
	cbz	w1, .L258
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L258
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1864]
.L258:
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
	ble	.L267
	ldr	x0, [x1,128]
	mov	w1, 8354
	b	.L274
.L267:
	cmp	w0, 220
	ble	.L269
	ldr	x0, [x1,128]
	b	.L275
.L269:
	cmp	w0, 185
	ble	.L270
	ldr	x0, [x1,128]
	mov	w1, 4226
	b	.L274
.L270:
	cmp	w0, 160
	ldr	x0, [x1,128]
	ble	.L271
	mov	w1, 4194
	b	.L274
.L271:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L274
	cmp	w19, 99
	mov	w1, 4225
	bls	.L274
.L275:
	mov	w1, 8322
.L274:
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
	bls	.L277
	mov	w1, 8322
	cmp	w0, w1
	bne	.L278
.L277:
	adrp	x1, .LANCHOR0+128
	ldr	x1, [x1,#:lo12:.LANCHOR0+128]
	str	w0, [x1,4]
.L278:
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
	str	w2, [x19,1868]
	ldr	w2, [x0,352]
	and	w1, w1, 245760
	orr	w1, w1, 256
	ubfx	x2, x2, 16, 4
	str	w2, [x19,20]
	ldr	w2, [x0,352]
	str	w2, [x19,1872]
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
	bne	.L282
.L285:
	and	w1, w1, -17
	b	.L283
.L282:
	cmp	w0, 24
	bne	.L284
	orr	w1, w1, 16
	b	.L283
.L284:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L285
.L283:
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
.L292:
	lsl	x0, x21, 3
	ldrb	w0, [x0,x19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L291
	mov	w0, w21
	bl	FlashReset
.L291:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L292
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,1864]
	cbz	w0, .L293
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,1844]
	bl	NandcSetMode
	ldr	w0, [x19,152]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L293:
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
	tbnz	x0, 9, .L303
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L302
.L303:
	mov	x0, x19
	mov	w1, 1
	bl	NandCIrqDisable
.L302:
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
	cbz	w2, .L306
	mov	w1, w23
	bl	FlashEraseSLc2KBlocks
	b	.L307
.L306:
	mov	w28, 56
	add	x25, x19, 1652
	mov	x26, 24
.L331:
	cmp	w20, w23
	bcs	.L332
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
	bcc	.L308
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L309
.L308:
	ldrb	w1, [x19,1945]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w22, w22, wzr, ne
	madd	x1, x1, x26, x25
	ldr	x1, [x1,8]
	cbz	x1, .L311
	bl	FlashWaitCmdDone
.L311:
	ldr	w0, [x29,124]
	ldr	w1, [x29,120]
	madd	x2, x0, x26, x25
	str	x21, [x2,8]
	str	xzr, [x2,16]
	str	w1, [x2,4]
	cbz	w22, .L312
	add	w1, w20, 1
	umull	x1, w1, w28
	add	x1, x24, x1
	str	x1, [x2,16]
.L312:
	add	x1, x19, x0
	mul	x0, x0, x26
	ldrb	w21, [x1,1848]
	strb	w21, [x25,x0]
	mov	w0, w21
	bl	NandcFlashCs
	cmp	w27, 1
	bne	.L313
	ldrb	w0, [x19,120]
	cbz	w0, .L313
	mov	w0, w21
	bl	flash_enter_slc_mode
.L313:
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
.L309:
	add	w20, w20, 1
	b	.L331
.L332:
	ldr	x0, [x29,104]
	mov	x20, 0
	mov	x22, 24
	add	x19, x0, :lo12:.LANCHOR0
	add	x21, x19, 1652
	ldr	x0, [x19,128]
	bl	NandcIqrWaitFlashReady
.L315:
	ldrb	w0, [x19,1845]
	cmp	w0, w20
	bls	.L333
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w27, 1
	bne	.L316
	ldrb	w0, [x19,120]
	cbz	w0, .L316
	mul	x0, x20, x22
	ldrb	w0, [x0,x21]
	bl	flash_exit_slc_mode
.L316:
	add	x20, x20, 1
	b	.L315
.L333:
	mov	w0, 0
.L307:
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
.L337:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L337
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
	ldr	w0, [x1,20]
	orr	w20, w20, 536870912
	cmp	w0, 3
	orr	w20, w20, 1024
	bfi	w20, w3, 4, 1
	bls	.L341
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w8, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w8, .L355
	cbz	x22, .L342
.L355:
	cbnz	w24, .L344
.L352:
	mov	x0, x21
	bl	rk_nandc_xfer_irq_flag_init
	mov	x0, x21
	mov	w1, 0
	bl	NandCIrqEnable
	add	w2, w25, 1
	mov	x0, x22
	asr	w2, w2, 1
	bfi	w20, w2, 22, 6
	cbnz	x22, .L346
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1888]
	b	.L346
.L344:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w6, 128
	mov	w3, 0
	lsr	w2, w25, 1
	mov	w4, w3
	mov	w9, -1
	ldr	w0, [x1,1940]
	cmp	w0, 25
	mov	w0, 64
	csel	w6, w0, w6, cc
.L348:
	cmp	w4, w2
	bcs	.L352
	lsr	w0, w3, 2
	ubfiz	x0, x0, 2, 30
	cbz	w8, .L349
	ldrh	w7, [x5,2]
	ldr	x10, [x1,1896]
	ldrh	w11, [x5],4
	orr	w7, w11, w7, lsl 16
	str	w7, [x10,x0]
	b	.L350
.L349:
	ldr	x7, [x1,1896]
	str	w9, [x7,x0]
.L350:
	add	w4, w4, 1
	add	w3, w3, w6
	b	.L348
.L346:
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x25, x20, 22, 5
	mov	w2, w24
	and	x22, x22, 3
	ldr	x1, [x19,1896]
	str	x1, [x19,1912]
	lsl	w1, w25, 10
	str	x0, [x19,1904]
	bl	rknand_dma_map_single
	str	w0, [x19,1920]
	ldr	x0, [x19,1912]
	lsl	w1, w25, 7
	mov	w2, w24
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
	cbnz	x22, .L353
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L353:
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
.L342:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L341:
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
.L360:
	cmp	w2, w0
	bhi	.L362
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L360
.L362:
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
	ldrh	w2, [x1,1952]
	ldrh	w3, [x1,1962]
	str	w0, [x1,1948]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,1960]
	ldr	w0, [x1,1968]
	str	w2, [x1,1956]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,1964]
	ret
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	stp	x29, x30, [sp, -80]!
	mov	x3, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	x23, x0
	adrp	x19, .LANCHOR0
	ldrh	w6, [x0,8]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x23,10]
	ldrh	w1, [x23,12]
	ldrh	w4, [x23,14]
	strh	w6, [x0,1972]
	strh	w2, [x0,1974]
	strh	w1, [x0,1976]
	strh	w4, [x0,1962]
.L367:
	add	x5, x0, 1984
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L367
	ldrh	w3, [x23,20]
	ldrb	w0, [x23,15]
	cmp	w3, w0
	bcs	.L368
	uxtb	w8, w1
	mov	w3, 0
	mul	w11, w1, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x19, :lo12:.LANCHOR0
.L369:
	cmp	w3, w1
	bcs	.L371
	uxtb	w0, w3
	mov	w5, w3
	mov	w7, 0
.L372:
	cmp	w7, w2
	bcs	.L385
	add	x9, x12, 1984
	add	w13, w5, w11
	add	w14, w0, w8
	add	w7, w7, 1
	strb	w0, [x9,w5,uxtw]
	add	w0, w0, w10
	strb	w14, [x9,x13]
	add	w5, w5, w1
	uxtb	w0, w0
	b	.L372
.L385:
	add	w3, w3, 1
	b	.L369
.L371:
	add	x0, x19, :lo12:.LANCHOR0
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x0,1974]
	strh	w4, [x0,1962]
.L368:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x0,2016]
	strh	wzr, [x0,2018]
	bne	.L373
	strh	w6, [x0,2016]
.L373:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 4352
	strh	w2, [x0,2020]
	ldrb	w2, [x0,8]
	cbz	w2, .L374
	mov	w2, 384
	strh	w2, [x0,2020]
.L374:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w20, [x21,1962]
	ldrh	w22, [x21,1974]
	mul	w22, w1, w22
	mul	w1, w20, w1
	uxth	w22, w22
	strh	w22, [x21,1952]
	uxth	w0, w1
	strh	w0, [x21,2022]
	bl	Ftl_log2
	ldrh	w25, [x23,16]
	ldrh	w24, [x23,20]
	strh	w0, [x21,2024]
	mul	w0, w22, w25
	strh	w0, [x21,2030]
	mov	w0, w24
	ldrh	w26, [x23,18]
	strh	w25, [x21,2026]
	strh	w26, [x21,2028]
	strh	w24, [x21,2032]
	bl	Ftl_log2
	uxth	w3, w0
	strh	w0, [x21,2034]
	lsl	w0, w24, 9
	strh	w0, [x21,2036]
	ubfx	x0, x0, 9, 7
	cmp	w20, 1024
	lsl	w0, w0, 1
	strh	w0, [x21,2038]
	ldrh	w0, [x23,26]
	strh	w0, [x21,2040]
	mul	w0, w22, w20
	str	w0, [x21,1968]
	bls	.L375
	and	w0, w20, 255
	strh	w0, [x21,2018]
.L375:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2,2018]
	ldrh	w0, [x2,2020]
	sub	w1, w20, w1
	mul	w1, w22, w1
	mul	w1, w1, w24
	mul	w24, w24, w26
	mul	w1, w1, w25
	asr	w1, w1, 11
	str	w1, [x2,2044]
	lsl	w1, w0, 3
	sdiv	w24, w1, w24
	uxth	w24, w24
	cmp	w24, 4
	bls	.L376
	strh	w24, [x2,2048]
	b	.L377
.L376:
	mov	w1, 4
	strh	w1, [x2,2048]
.L377:
	add	x1, x19, :lo12:.LANCHOR0
	asr	w0, w0, w3
	lsl	w20, w20, 6
	add	w3, w3, 9
	asr	w20, w20, w3
	add	w0, w0, 2
	strh	w20, [x1,2052]
	and	w20, w20, 65535
	strh	w0, [x1,2050]
	cmp	w22, 1
	mul	w0, w20, w22
	str	w0, [x1,2056]
	add	w20, w20, 8
	ldrh	w0, [x1,2048]
	udiv	w0, w0, w22
	add	w20, w20, w0
	bne	.L384
	add	w20, w20, 4
.L384:
	add	x19, x19, :lo12:.LANCHOR0
	str	w20, [x1,1948]
	ldrh	w0, [x19,1948]
	bl	FtlSysBlkNumInit
	str	wzr, [x19,2068]
	ldr	w0, [x19,1948]
	str	w0, [x19,2060]
	ldr	w0, [x19,1964]
	ldrh	w1, [x19,2026]
	lsl	w0, w0, 2
	ldp	x21, x22, [sp,32]
	mul	w1, w0, w1
	ldrh	w0, [x19,2034]
	ldp	x23, x24, [sp,48]
	add	w0, w0, 9
	lsr	w0, w1, w0
	ldrh	w1, [x19,2032]
	add	w0, w0, 2
	strh	w0, [x19,2064]
	mov	w0, 2048
	ldp	x25, x26, [sp,64]
	sdiv	w0, w0, w1
	strh	w0, [x19,2066]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 80
	mov	w0, 0
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
	ldrh	w2, [x1,2072]
	cbz	w2, .L387
	ldrh	w2, [x1,2048]
	ldr	x3, [x1,2080]
	mov	x1, 0
.L388:
	cmp	w2, w1, uxth
	bls	.L393
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L388
	mov	w0, 1
	b	.L387
.L393:
	mov	w0, 0
.L387:
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
	ldr	w3, [x2,2088]
	cbz	w3, .L395
	ldr	x5, [x2,2096]
	mov	x0, 56
	mov	x2, 0
.L396:
	cmp	w3, w2
	bls	.L405
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L396
	cmp	w4, w1
	bhi	.L396
	mov	w0, 1
	b	.L395
.L405:
	mov	w0, 0
.L395:
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR0+2068
	ldr	w0, [x0,#:lo12:.LANCHOR0+2068]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	FtlGetCapacity
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	adrp	x0, .LANCHOR0+2068
	ldr	w0, [x0,#:lo12:.LANCHOR0+2068]
	ret
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR0+2104
	ldr	w0, [x0,#:lo12:.LANCHOR0+2104]
	ret
	.size	FtlGetLpn, .-FtlGetLpn
	.align	2
	.global	FtlGetCurEraseBlock
	.type	FtlGetCurEraseBlock, %function
FtlGetCurEraseBlock:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w2, [x1,1952]
	ldr	w0, [x1,2108]
	mul	w0, w2, w0
	ret
	.size	FtlGetCurEraseBlock, .-FtlGetCurEraseBlock
	.align	2
	.global	FtlGetAllBlockNum
	.type	FtlGetAllBlockNum, %function
FtlGetAllBlockNum:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w2, [x1,1962]
	ldrh	w0, [x1,1952]
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
	add	x19, x19, 2112
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
	ldrh	w1, [x3,2022]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	ldr	x1, [x2,2144]
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
	ldrh	w21, [x0,1974]
	mul	w21, w1, w21
.L415:
	cmp	w20, w21
	bge	.L421
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L416
	add	w19, w19, 1
	uxth	w19, w19
.L416:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L415
.L421:
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
	ldrh	w4, [x3,1976]
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
	ldrh	w1, [x2,1976]
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
	ldrh	w1, [x2,1976]
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
	bls	.L426
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L427
.L426:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L427:
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	adrp	x0, .LANCHOR0+2214
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2214]
	cmp	w0, wzr
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR0+2214
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2214]
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
	ldrh	w0, [x0,2214]
	cmp	w0, 1024
	beq	.L430
	cbz	w1, .L432
	adrp	x20, .LANCHOR2
	mov	w0, w22
	add	x20, x20, :lo12:.LANCHOR2
	bl	P2V_block_in_plane
	uxth	w21, w0
	lsl	w1, w22, 10
	ldr	x0, [x20,-88]
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
.L432:
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 2208
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w22, [x0,8]
	strh	w2, [x1,4]
.L430:
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
	add	x20, x20, 2208
	adrp	x21, .LANCHOR2
.L440:
	ldrh	w0, [x20,6]
	cmp	w0, w19
	bls	.L448
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
	b	.L440
.L448:
	adrp	x4, .LANCHOR0
	adrp	x7, .LANCHOR2
	mov	w0, 0
	add	x4, x4, :lo12:.LANCHOR0
	mov	x8, x7
.L442:
	ldrh	w6, [x4,2214]
	sub	w1, w6, #1
	cmp	w0, w1
	bge	.L449
	add	x1, x7, :lo12:.LANCHOR2
	add	w5, w0, 1
	uxth	w5, w5
	ldr	x3, [x1,-64]
	mov	w2, w5
	mov	w1, w0
.L443:
	cmp	w2, w6
	bcs	.L450
	ubfiz	x9, x2, 2, 16
	ldr	w10, [x3,w1,uxtw 2]
	ldr	w9, [x3,x9]
	cmp	w10, w9
	csel	w1, w1, w2, ls
	add	w2, w2, 1
	uxth	w2, w2
	b	.L443
.L450:
	cmp	w0, w1
	beq	.L446
	ubfiz	x2, x0, 2, 16
	ubfiz	x6, x1, 2, 32
	ldr	w10, [x3,x2]
	ldr	w9, [x3,x6]
	str	w10, [x3,x6]
	add	x3, x8, :lo12:.LANCHOR2
	ldr	x3, [x3,-64]
	str	w9, [x3,x2]
	add	x2, x4, 2208
	ldrh	w3, [x4,2210]
	add	w1, w3, w1
	add	w0, w3, w0
	add	x1, x2, x1, sxtw 1
	add	x0, x2, x0, sxtw 1
	ldrh	w2, [x0,8]
	ldrh	w6, [x1,8]
	strh	w2, [x1,8]
	strh	w6, [x0,8]
.L446:
	mov	w0, w5
	b	.L442
.L449:
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
	add	x1, x1, 2208
	ldrh	w4, [x1,6]
	cmp	w4, 1024
	beq	.L452
	ldrh	w5, [x1,2]
	mov	w3, w0
.L453:
	cmp	w3, w4
	bcs	.L457
	add	w2, w3, w5
	ubfiz	x2, x2, 1, 10
	add	x2, x1, x2
	ldrh	w0, [x2,8]
	cmp	w0, w6
	beq	.L456
	add	w3, w3, 1
	b	.L453
.L457:
	mov	w0, 0
	b	.L452
.L456:
	mov	w0, 1
.L452:
	ret
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	adrp	x1, .LANCHOR0
	mov	w0, 0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 2208
	ldrh	w3, [x1,6]
	cbz	w3, .L459
	ldrh	w2, [x1,2]
	add	x0, x1, x2, sxtw 1
	ldrh	w0, [x0,8]
	sub	w3, w3, #1
	add	w2, w2, 1
	strh	w3, [x1,6]
	and	w2, w2, 1023
	strh	w2, [x1,2]
.L459:
	ret
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	uxth	w0, w0
	mov	w9, 65535
	cmp	w0, w9
	beq	.L463
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
	cbnz	x1, .L464
	str	x4, [x5,-48]
	b	.L463
.L464:
	ubfiz	x8, x0, 1, 16
	ldr	x13, [x5,-40]
	ldrh	w2, [x4,4]
	mov	x14, -6148914691236517206
	ldr	x12, [x5,-56]
	ldrh	w6, [x13,x8]
	cmp	w2, wzr
	mul	w6, w6, w2
	sub	x2, x1, x12
	asr	x2, x2, 1
	csinv	w6, w6, wzr, ne
	madd	x2, x14, x2, x2
	ldr	x14, [x5,-80]
	uxth	w2, w2
	add	x8, x14, x8
.L471:
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w5, [x13,x15]
	mul	w5, w5, w16
	csinv	w5, w5, wzr, ne
	cmp	w5, w6
	bne	.L467
	ldrh	w15, [x14,x15]
	ldrh	w5, [x8]
	cmp	w15, w5
	bcc	.L469
	b	.L468
.L467:
	bhi	.L468
.L469:
	ldrh	w5, [x1]
	cmp	w5, w9
	bne	.L470
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x4, [x2,-32]
	b	.L463
.L470:
	umull	x1, w5, w7
	mov	w2, w5
	add	x1, x12, x1
	b	.L471
.L468:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x4,2]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,-48]
	cmp	x1, x3
	bne	.L472
	strh	w0, [x1,2]
	str	x4, [x2,-48]
	b	.L463
.L472:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-56]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L463:
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
	beq	.L477
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
	cbnz	x1, .L478
	str	x4, [x5,-16]
	b	.L477
.L478:
	ldr	x11, [x5,-80]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,-56]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L481:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L479
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L480
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L477
.L480:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L481
.L479:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,-16]
	cmp	x1, x3
	bne	.L482
	strh	w0, [x1,2]
	str	x4, [x2,-16]
	b	.L477
.L482:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,-56]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L477:
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
	bne	.L485
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L486
	str	xzr, [x0]
	b	.L487
.L486:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L487
.L485:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L488
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L487
.L488:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,-56]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L487:
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
	cbz	x2, .L490
	adrp	x3, .LANCHOR2-56
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2-56]
.L491:
	cbnz	w1, .L492
.L494:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L490
.L492:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L494
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L491
.L490:
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
	ldr	x1, [x2,-48]
	cbz	x1, .L503
	ldr	x4, [x2,-56]
	mov	w5, 6
.L499:
	cbz	w3, .L500
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L503
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L499
.L500:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L503:
	ret
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.type	List_update_data_list, %function
List_update_data_list:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w20, w0
	adrp	x0, .LANCHOR2
	add	x19, x0, :lo12:.LANCHOR2
	ldrh	w0, [x0,#:lo12:.LANCHOR2]
	cmp	w0, w20
	beq	.L505
	ldrh	w0, [x19,48]
	cmp	w0, w20
	beq	.L505
	ldrh	w0, [x19,96]
	cmp	w0, w20
	beq	.L505
	mov	w2, 6
	ldr	x4, [x19,-56]
	ldr	x0, [x19,-48]
	umull	x3, w20, w2
	add	x3, x4, x3
	cmp	x3, x0
	beq	.L505
	ldr	x5, [x19,-40]
	ubfiz	x0, x20, 1, 16
	ldrh	w1, [x3,4]
	ldrh	w0, [x5,x0]
	mul	w1, w0, w1
	ldrh	w0, [x3,2]
	mov	x3, -6148914691236517206
	cmp	w1, wzr
	csinv	w1, w1, wzr, ne
	umull	x2, w0, w2
	asr	x0, x2, 1
	add	x2, x4, x2
	madd	x0, x3, x0, x0
	ldrh	w2, [x2,4]
	ldrh	w0, [x5,x0,lsl 1]
	mul	w0, w0, w2
	cmp	w0, wzr
	csinv	w0, w0, wzr, ne
	cmp	w1, w0
	bcs	.L505
	sub	x0, x19, #48
	mov	w1, w20
	bl	List_remove_node
	ldrh	w0, [x19,-24]
	sub	w0, w0, #1
	strh	w0, [x19,-24]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L505:
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
.L509:
	cmp	w20, w2
	beq	.L512
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L510
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	uxth	w1, w0
	strh	w0, [x22]
	cbz	w1, .L512
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L512
.L510:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L509
.L512:
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
	adrp	x0, .LANCHOR0+2066
	mov	x4, 0
	mov	w5, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR0+2066]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	mov	x3, x0
	ldr	x1, [x1,144]
.L516:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L532
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L516
	b	.L517
.L532:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L519:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L533
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L528
	cmp	w5, w7
	bcc	.L520
.L528:
	mov	w5, w7
	mov	w6, w0
.L520:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L519
.L533:
	cmp	w0, w2
	bcc	.L517
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,152]
	mov	w0, w2
.L523:
	cmp	w3, w2
	beq	.L517
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L524
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L524:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L523
.L517:
	ret
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3,154]
	cbnz	w0, .L535
	cmp	w2, 4
	bhi	.L535
	add	w2, w2, 1
	strh	w2, [x3,154]
	b	.L534
.L535:
	adrp	x2, .LANCHOR0+1960
	add	x0, x1, :lo12:.LANCHOR2
	mov	w7, 65535
	ldrh	w5, [x2,#:lo12:.LANCHOR0+1960]
	strh	wzr, [x0,154]
	str	wzr, [x0,156]
	ldr	x6, [x0,-40]
	mov	x0, 0
.L536:
	cmp	w5, w0, uxth
	bls	.L534
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L537
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,156]
	add	w2, w4, w2
	str	w2, [x3,156]
.L537:
	add	x0, x0, 1
	b	.L536
.L534:
	ret
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	stp	x29, x30, [sp, -16]!
	uxth	w0, w0
	add	x29, sp, 0
	cbz	w1, .L541
	bl	ftl_set_blk_mode.part.8
	b	.L540
.L541:
	adrp	x1, .LANCHOR0
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L540:
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
	ldrh	w4, [x4,1952]
.L545:
	cmp	w2, w4
	bcs	.L551
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L546
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L546:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L545
.L551:
	add	x3, x3, :lo12:.LANCHOR0
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,2026]
	mov	x3, x0
.L548:
	cmp	x3, x4
	beq	.L552
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L549
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L549:
	add	x3, x3, 2
	b	.L548
.L552:
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
	mov	x19, x0
	strh	wzr, [x0,4]
	strb	wzr, [x0,7]
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x22, x19, 16
	mov	x21, 0
	mov	x20, x0
	mov	x23, x1
	mov	w25, -1
	ldrh	w24, [x1,1952]
.L554:
	cmp	w24, w21, uxth
	bls	.L561
	add	x0, x23, 1984
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L555
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L555:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L554
.L561:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w0, [x0,2026]
	mul	w0, w1, w0
	strh	w0, [x19,4]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w1, [x0,160]
	cbz	w1, .L557
	ldrh	w1, [x19]
	ldr	x0, [x0,-80]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 59
	bhi	.L557
	mov	w0, 1
	strb	w0, [x19,9]
.L557:
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
	ldrh	w23, [x1,1952]
	mov	w19, w22
	ldrh	w24, [x1,2026]
	mov	x20, x1
.L563:
	cmp	w23, w22, uxth
	bls	.L570
	add	x0, x20, 1984
	mov	w1, w21
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L564
	add	w19, w19, w24
	uxth	w19, w19
.L564:
	add	x22, x22, 1
	b	.L563
.L570:
	cbz	w19, .L566
	mov	w0, 32768
	sdiv	w19, w0, w19
.L566:
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
	cbz	x1, .L572
	ldr	x0, [x2,-56]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,-80]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L572:
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
	cbz	x1, .L575
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
.L577:
	cmp	w3, w4
	beq	.L580
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L578
.L580:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,-80]
	ldrh	w0, [x0,x1]
	b	.L575
.L578:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L577
.L575:
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
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	mov	x23, x0
	bl	strcpy
	add	x20, x23, 12
	ldr	w2, [x25,168]
	adrp	x1, .LC3
	mov	x0, x20
	add	x1, x1, :lo12:.LC3
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC4
	ldr	w2, [x25,2044]
	mov	x0, x20
	add	x1, x1, :lo12:.LC4
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+532
	ldr	w0, [x0,#:lo12:.LANCHOR1+532]
	cmp	w0, 1
	beq	.L583
	sub	w0, w20, w23
	b	.L584
.L583:
	add	x0, x29, 96
	add	x1, x29, 100
	add	x2, x29, 104
	add	x3, x29, 108
	adrp	x24, .LANCHOR2
	bl	NandcGetTimeCfg
	add	x22, x24, :lo12:.LANCHOR2
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
	ldr	w2, [x25,2104]
	adrp	x1, .LC7
	mov	x0, x21
	add	x1, x1, :lo12:.LC7
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,156]
	adrp	x1, .LC8
	mov	x0, x21
	add	x1, x1, :lo12:.LC8
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,164]
	adrp	x1, .LC9
	mov	x0, x21
	add	x1, x1, :lo12:.LC9
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,168]
	adrp	x1, .LC10
	mov	x0, x21
	add	x1, x1, :lo12:.LC10
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,172]
	adrp	x1, .LC11
	mov	x0, x21
	add	x1, x1, :lo12:.LC11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,176]
	adrp	x1, .LC12
	mov	x0, x21
	add	x1, x1, :lo12:.LC12
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,180]
	adrp	x1, .LC13
	mov	x0, x21
	add	x1, x1, :lo12:.LC13
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,184]
	adrp	x1, .LC14
	mov	x0, x21
	add	x1, x1, :lo12:.LC14
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,188]
	adrp	x1, .LC15
	mov	x0, x21
	add	x1, x1, :lo12:.LC15
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,192]
	adrp	x1, .LC16
	mov	x0, x21
	add	x1, x1, :lo12:.LC16
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,196]
	adrp	x1, .LC17
	mov	x0, x21
	add	x1, x1, :lo12:.LC17
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,200]
	adrp	x1, .LC18
	add	x1, x1, :lo12:.LC18
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x25,2118]
	adrp	x1, .LC19
	mov	x0, x21
	add	x1, x1, :lo12:.LC19
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,-8]
	adrp	x1, .LC20
	mov	x0, x21
	add	x1, x1, :lo12:.LC20
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,204]
	adrp	x1, .LC21
	mov	x0, x21
	add	x1, x1, :lo12:.LC21
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,208]
	adrp	x1, .LC22
	mov	x0, x21
	add	x1, x1, :lo12:.LC22
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,212]
	adrp	x1, .LC23
	mov	x0, x21
	add	x1, x1, :lo12:.LC23
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,-72]
	adrp	x1, .LC24
	mov	x0, x21
	add	x1, x1, :lo12:.LC24
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,216]
	adrp	x1, .LC25
	mov	x0, x21
	add	x1, x1, :lo12:.LC25
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,220]
	adrp	x1, .LC26
	mov	x0, x21
	add	x1, x1, :lo12:.LC26
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,254]
	adrp	x1, .LC27
	mov	x0, x21
	add	x1, x1, :lo12:.LC27
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,252]
	adrp	x1, .LC28
	mov	x0, x21
	add	x1, x1, :lo12:.LC28
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,2068]
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
	ldr	w2, [x25,1948]
	adrp	x1, .LC31
	mov	x0, x21
	add	x1, x1, :lo12:.LC31
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,2214]
	adrp	x1, .LC32
	mov	x0, x21
	add	x1, x1, :lo12:.LC32
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,1960]
	adrp	x1, .LC33
	mov	x0, x21
	add	x1, x1, :lo12:.LC33
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,272]
	adrp	x1, .LC34
	mov	x0, x21
	add	x1, x1, :lo12:.LC34
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,1964]
	adrp	x1, .LC35
	mov	x0, x21
	add	x1, x1, :lo12:.LC35
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,280]
	adrp	x1, .LC36
	mov	x0, x21
	add	x1, x1, :lo12:.LC36
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,2112]
	adrp	x1, .LC37
	mov	x0, x21
	add	x1, x1, :lo12:.LC37
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,2]
	adrp	x1, .LC38
	mov	x0, x21
	add	x1, x1, :lo12:.LC38
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,6]
	adrp	x1, .LC39
	mov	x0, x21
	add	x1, x1, :lo12:.LC39
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x24,#:lo12:.LANCHOR2]
	adrp	x1, .LC40
	mov	x0, x21
	add	x1, x1, :lo12:.LC40
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,8]
	adrp	x1, .LC41
	mov	x0, x21
	add	x1, x1, :lo12:.LC41
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,4]
	adrp	x1, .LC42
	mov	x0, x21
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x24,#:lo12:.LANCHOR2]
	adrp	x1, .LC43
	ldr	x2, [x22,-40]
	mov	x0, x21
	add	x1, x1, :lo12:.LC43
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,50]
	adrp	x1, .LC44
	mov	x0, x21
	add	x1, x1, :lo12:.LC44
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,54]
	adrp	x1, .LC45
	mov	x0, x21
	add	x1, x1, :lo12:.LC45
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,48]
	adrp	x1, .LC46
	mov	x0, x21
	add	x1, x1, :lo12:.LC46
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,56]
	adrp	x1, .LC47
	mov	x0, x21
	add	x1, x1, :lo12:.LC47
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,52]
	adrp	x1, .LC48
	mov	x0, x21
	add	x1, x1, :lo12:.LC48
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x22,48]
	adrp	x1, .LC49
	ldr	x2, [x22,-40]
	mov	x0, x21
	add	x1, x1, :lo12:.LC49
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,98]
	adrp	x1, .LC50
	mov	x0, x21
	add	x1, x1, :lo12:.LC50
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,102]
	adrp	x1, .LC51
	mov	x0, x21
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,96]
	adrp	x1, .LC52
	mov	x0, x21
	add	x1, x1, :lo12:.LC52
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,104]
	adrp	x1, .LC53
	mov	x0, x21
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,100]
	adrp	x1, .LC54
	mov	x0, x21
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,298]
	adrp	x1, .LC55
	mov	x0, x21
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,302]
	adrp	x1, .LC56
	mov	x0, x21
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,296]
	adrp	x1, .LC57
	mov	x0, x21
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,304]
	adrp	x1, .LC58
	mov	x0, x21
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,300]
	adrp	x1, .LC59
	mov	x0, x21
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x22,344]
	adrp	x1, .LC60
	ldr	w2, [x22,160]
	add	x1, x1, :lo12:.LC60
	ldr	w4, [x22,436]
	mov	x0, x21
	ldr	w5, [x22,428]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x22,432]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x22,424]
	adrp	x1, .LC61
	mov	x0, x19
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,448]
	adrp	x1, .LC62
	mov	x0, x19
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,864]
	adrp	x1, .LC63
	mov	x0, x19
	add	x1, x1, :lo12:.LC63
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,866]
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,868]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,872]
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
	ldrh	w0, [x22,-8]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x22,296]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L585
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x22,-40]
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L585:
	mov	w0, 0
	adrp	x22, .LC70
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x22, x22, :lo12:.LC70
.L587:
	cmp	w3, w27
	beq	.L586
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
	bne	.L587
.L586:
	add	x1, x24, :lo12:.LANCHOR2
	adrp	x22, .LC71
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
	add	x22, x22, :lo12:.LC71
	ldr	x0, [x1,-16]
	ldr	x3, [x1,-56]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L589:
	cmp	w3, w27
	beq	.L588
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
	bne	.L589
.L588:
	sub	w0, w19, w23
.L584:
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
	bcs	.L594
	adrp	x1, .LANCHOR0
	str	wzr, [x0,204]
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x5, [x0,-80]
	mov	x0, 0
	ldrh	w3, [x2,1960]
.L595:
	cmp	w3, w0
	bls	.L636
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x0,lsl 1]
	add	x0, x0, 1
	ldr	w2, [x4,204]
	add	w2, w6, w2
	str	w2, [x4,204]
	b	.L595
.L636:
	add	x2, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w0, [x2,204]
	ldrh	w1, [x1,2016]
	udiv	w3, w0, w3
	str	w3, [x2,212]
	ldr	w3, [x2,208]
	sub	w0, w0, w3
	udiv	w0, w0, w1
	str	w0, [x2,204]
	b	.L597
.L594:
	ldr	w1, [x0,216]
	cmp	w2, w1
	bls	.L597
	add	w1, w1, 1
	adrp	x4, .LANCHOR0
	str	w1, [x0,216]
	mov	w0, 0
.L599:
	add	x1, x4, :lo12:.LANCHOR0
	ldrh	w1, [x1,1960]
	cmp	w0, w1
	bcs	.L597
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x2, x0, 1, 32
	add	w0, w0, 1
	ldr	x3, [x1,-80]
	ldrh	w1, [x3,x2]
	add	w1, w1, 1
	strh	w1, [x3,x2]
	b	.L599
.L597:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w22, [x0,220]
	ldr	w23, [x0,212]
	add	w1, w22, 256
	cmp	w1, w23
	bls	.L602
	ldr	w2, [x0,216]
	add	w1, w22, 768
	cmp	w1, w2
	bls	.L602
	ldr	w0, [x0,160]
	cbz	w0, .L605
	cmp	w22, 30
	bls	.L602
.L605:
	mov	w0, 65535
	b	.L604
.L602:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,-8]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w22, 64
	cmp	w6, w0
	bcs	.L616
	cmp	w22, 30
	bhi	.L605
.L616:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x3, [x0,-48]
	cbz	x3, .L605
	mov	w24, 65535
	ldr	x5, [x0,-56]
	ldr	x26, [x0,-80]
	mov	x0, -6148914691236517206
	mov	w20, w24
	mov	w7, w24
	add	x0, x0, 1
	mov	w8, 6
.L607:
	ldrh	w1, [x3]
	cmp	w1, w7
	beq	.L609
	ldrh	w2, [x3,4]
	cbz	w2, .L608
	sub	x4, x3, x5
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w3, w4
	and	x4, x4, 65535
	ldrh	w2, [x26,x4,lsl 1]
	cmp	w2, w22
	bls	.L615
	cmp	w2, w24
	bcs	.L608
	mov	w24, w2
	mov	w20, w3
.L608:
	umull	x3, w1, w8
	add	x3, x5, x3
	b	.L607
.L615:
	mov	w20, w3
.L609:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L605
	ubfiz	x25, x20, 1, 16
	ldrh	w21, [x26,x25]
	cmp	w21, w22
	bls	.L611
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w22, w0, uxth
	bcs	.L611
	add	x0, x19, :lo12:.LANCHOR2
	str	w24, [x0,220]
.L611:
	cmp	w21, 29
	bhi	.L612
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,160]
	cbz	w0, .L612
	add	w0, w21, 10
	cmp	w0, w23
	bls	.L613
	adrp	x0, .LANCHOR0+2016
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2016]
	cmp	w0, w21
	bls	.L612
.L613:
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,-24]
	cmp	w0, 64
	bls	.L612
	adrp	x0, .LC74
	ldrh	w2, [x26,x25]
	add	x0, x0, :lo12:.LC74
	mov	w1, w20
	bl	printk
	mov	w0, 1
	str	w0, [x22,876]
	b	.L635
.L612:
	cmp	w21, w23
	bcs	.L605
	add	w0, w21, 128
	cmp	w6, w0
	ble	.L605
	add	w0, w21, 256
	cmp	w0, w23
	bcc	.L614
	add	x0, x19, :lo12:.LANCHOR2
	add	w21, w21, 768
	ldr	w0, [x0,216]
	cmp	w21, w0
	bcs	.L605
.L614:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC75
	ldrh	w5, [x26,x25]
	add	x0, x0, :lo12:.LC75
	mov	w1, w20
	mov	w2, w23
	ldr	x4, [x19,-40]
	ldr	w3, [x19,216]
	ldrh	w4, [x4,x25]
	bl	printk
	mov	w0, 1
	str	w0, [x19,876]
.L635:
	mov	w0, w20
.L604:
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
	beq	.L638
	adrp	x2, .LANCHOR2-40
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2-40]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L638:
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
.L640:
	add	x5, x10, :lo12:.LANCHOR0
	adrp	x9, .LANCHOR0
	ldrh	w0, [x5,1952]
	cmp	w1, w0
	bcs	.L644
	add	x2, x3, :lo12:.LANCHOR2
	umull	x4, w1, w11
	ldr	x7, [x2,888]
	ldr	x8, [x2,896]
	add	x0, x7, x4
	str	w12, [x0,16]
	ldrh	w0, [x5,2036]
	mul	w0, w1, w0
	sdiv	w0, w0, w6
	add	x0, x8, x0, sxtw 2
	str	x0, [x7,x4]
	ldrh	w0, [x5,2038]
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
	b	.L640
.L644:
	mov	w8, 24
	mov	w5, 4
.L642:
	add	x2, x3, :lo12:.LANCHOR2
	ldr	w1, [x2,920]
	cmp	w0, w1
	bcs	.L645
	umull	x4, w0, w8
	ldr	x7, [x2,888]
	add	x6, x9, :lo12:.LANCHOR0
	ldr	x10, [x2,896]
	add	x1, x7, x4
	str	wzr, [x1,16]
	ldrh	w1, [x6,2036]
	mul	w1, w0, w1
	sdiv	w1, w1, w5
	add	x1, x10, x1, sxtw 2
	str	x1, [x7,x4]
	ldrh	w1, [x6,2038]
	ldr	x6, [x2,888]
	ldr	x2, [x2,904]
	mul	w1, w0, w1
	add	x4, x6, x4
	add	w0, w0, 1
	sdiv	w1, w1, w5
	uxth	w0, w0
	add	x1, x2, x1, sxtw 2
	str	x1, [x4,8]
	b	.L642
.L645:
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
.L647:
	cmp	w3, w1
	bcs	.L646
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L652:
	cmp	w2, w8
	bcs	.L649
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L648
	str	wzr, [x10,16]
	b	.L649
.L648:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L652
.L649:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L647
.L646:
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
.L654:
	cmp	w2, w1
	bcs	.L660
	mov	w3, 0
.L658:
	cmp	w3, w5
	bcs	.L656
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L655
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L656
.L655:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L658
.L656:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L654
.L660:
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
.L662:
	cmp	w2, w1, uxth
	bls	.L666
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L662
	mov	w0, 1
	b	.L663
.L666:
	mov	w0, 0
.L663:
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
.L668:
	uxth	w4, w5
	cmp	w4, w7
	bcs	.L672
	add	x5, x5, 1
	add	x9, x6, x5, lsl 1
	ldrh	w9, [x9,-2]
	cmp	w9, w8
	bne	.L668
.L672:
	cmp	w4, w7
	bne	.L670
	ubfiz	x4, x4, 1, 16
	strh	w0, [x6,x4]
	add	x0, x3, :lo12:.LANCHOR2
	ldrh	w4, [x0,924]
	add	w4, w4, 1
	strh	w4, [x0,924]
.L670:
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
	mov	w0, 65535
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w1, [x2,952]
	cmp	w1, w0
	bne	.L675
	strh	w19, [x2,952]
	b	.L676
.L675:
	ldrh	w1, [x2,954]
	cmp	w1, w0
	bne	.L676
	strh	w19, [x2,954]
.L676:
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
	cbz	w0, .L678
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,-80]
	ldrh	w1, [x2,x0]
	cmp	w1, 29
	bls	.L678
	sub	w1, w1, #30
	strh	w1, [x2,x0]
.L678:
	add	x2, x19, :lo12:.LANCHOR2
	mov	x0, 0
	add	x2, x2, 960
	ldrh	w1, [x2,-4]
.L679:
	cmp	w1, w0, uxth
	bls	.L685
	add	x0, x0, 1
	add	x3, x2, x0, lsl 1
	ldrh	w3, [x3,-2]
	cmp	w3, w20
	bne	.L679
	b	.L680
.L685:
	cmp	w1, 15
	bhi	.L680
	add	x19, x19, :lo12:.LANCHOR2
	add	w0, w1, 1
	add	x19, x19, 960
	strh	w0, [x19,-4]
	strh	w20, [x19,w1,sxtw 1]
.L680:
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
	cbz	w1, .L687
	ldrh	w3, [x0,952]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L687
	ldrh	w2, [x0,994]
	cmp	w2, w1
	bcc	.L688
	strh	wzr, [x0,994]
.L688:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x19, 960
	ldrh	w1, [x19,994]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,994]
	add	w0, w0, 1
	strh	w0, [x19,994]
.L687:
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
	cbnz	w0, .L694
	adrp	x1, .LANCHOR0
	mov	x0, 0
	add	x1, x1, :lo12:.LANCHOR0
.L695:
	add	x2, x1, 196
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L695
.L701:
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
	b	.L696
.L694:
	cmp	w0, 1
	bne	.L697
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x3, :lo12:.LANCHOR0
.L700:
	cmp	x0, 3
	uxth	w1, w0
	mov	w2, w1
	bls	.L698
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L698:
	add	x1, x3, 196
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L700
	b	.L701
.L697:
	cmp	w0, 2
	bne	.L702
	adrp	x3, .LANCHOR0
	mov	w1, 65535
	mov	x0, 0
	add	x3, x3, :lo12:.LANCHOR0
.L704:
	add	x4, x3, 196
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L704
	b	.L701
.L702:
	cmp	w0, 3
	bne	.L705
	adrp	x3, .LANCHOR0
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x3, :lo12:.LANCHOR0
.L708:
	cmp	x0, 5
	uxth	w1, w0
	mov	w2, w1
	bls	.L706
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L706:
	add	x1, x3, 196
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L708
	b	.L701
.L705:
	cmp	w0, 4
	bne	.L709
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
.L711:
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
	bne	.L711
	b	.L701
.L709:
	cmp	w0, 5
	bne	.L712
	adrp	x2, .LANCHOR0
	mov	x0, 0
	add	x2, x2, :lo12:.LANCHOR0
.L713:
	add	x1, x2, 196
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L713
	mov	x0, 0
.L714:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L714
	b	.L701
.L712:
	cmp	w0, 6
	bne	.L701
	adrp	x1, .LANCHOR0
	mov	x2, 0
	mov	w4, 12
	mov	w5, 10
	add	x1, x1, :lo12:.LANCHOR0
.L717:
	cmp	x2, 5
	uxth	w3, w2
	mov	w0, w3
	bls	.L715
	add	w0, w3, w3, lsl 1
	and	w3, w3, 1
	cmp	w3, wzr
	uxth	w0, w0
	csel	w3, w4, w5, ne
	sub	w0, w0, w3
	uxth	w0, w0
.L715:
	add	x3, x1, 196
	strh	w0, [x3,x2,lsl 1]
	add	x2, x2, 1
	cmp	x2, 256
	bne	.L717
	b	.L701
.L696:
	cmp	w20, w0, uxth
	bls	.L735
	add	x2, x1, 196
	ldrh	w2, [x2,x0,lsl 1]
	add	x0, x0, 1
	strh	w2, [x19,w2,sxtw 1]
	b	.L696
.L735:
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
	bls	.L737
	mov	w1, 512
	b	.L753
.L737:
	cmp	w1, 128
	bls	.L753
	mov	w1, 256
.L753:
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
.L741:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x26, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L740
	ldrb	w1, [x19,1845]
	str	w0, [x25,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x19, x1, sxtw
	strb	w0, [x19,1845]
	strb	w24, [x1,1848]
.L740:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L741
	add	x19, x21, :lo12:.LANCHOR0
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w1, [x19,1845]
	strb	w1, [x0,2020]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L742
.L746:
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
.L742:
	ldr	w26, [x19,12]
	mov	x24, 0
	add	x27, x19, 1620
	add	x25, x19, 708
.L745:
	ldrb	w2, [x20]
	mov	x0, x23
	add	x1, x27, x24, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L743
	ldrb	w1, [x20,13]
	ldrb	w3, [x19,1845]
	mul	w0, w1, w26
	ldrh	w1, [x20,14]
	mov	x2, x3
	and	w1, w1, 65280
	mul	w1, w0, w1
	str	w1, [x25,w3,sxtw 2]
	ldrb	w0, [x20,23]
	cbz	w0, .L744
	lsl	w1, w1, 1
	str	w1, [x25,w3,sxtw 2]
.L744:
	add	x0, x19, x3
	add	w2, w2, 1
	strb	w2, [x19,1845]
	strb	w24, [x0,1848]
.L743:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L745
	b	.L746
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
	ldrb	w2, [x1,1944]
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
.L755:
	cmp	w4, w0, uxtb
	bls	.L757
	add	x2, x1, 1848
	ldrb	w3, [x19,10]
	ldrb	w2, [x0,x2]
	add	x0, x0, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L755
.L757:
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
	str	w1, [x0,2032]
	mov	w1, -1
	str	wzr, [x0,2024]
	strh	w1, [x0,952]
	strh	w1, [x0,954]
	mov	w1, 32
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
	str	wzr, [x0,2028]
	str	wzr, [x0,876]
	str	wzr, [x0,2036]
	str	wzr, [x0,868]
	str	wzr, [x3,2108]
	strh	w1, [x0,864]
	mov	w1, 128
	ldrh	w2, [x3,2032]
	strh	w1, [x0,866]
	mov	w1, 1024
	strh	wzr, [x0,872]
	sdiv	w2, w1, w2
	ldrh	w1, [x3,1952]
	strh	wzr, [x0,956]
	lsl	w1, w1, 2
	strh	wzr, [x0,994]
	str	w2, [x0,2040]
	cmp	w2, w1
	bls	.L759
	str	w1, [x0,2040]
.L759:
	add	x20, x22, :lo12:.LANCHOR0
	add	x19, x21, :lo12:.LANCHOR2
	mov	w24, 56
	ldrh	w0, [x20,2030]
	str	wzr, [x20,2088]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,928]
	ldrh	w1, [x20,2030]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,1952]
	str	x0, [x19,944]
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
	str	x0, [x19,912]
	ldr	w0, [x19,2040]
	mul	w0, w0, w24
	bl	ftl_malloc
	str	x0, [x20,2096]
	ldrh	w0, [x20,1952]
	ldrh	w23, [x20,2036]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,920]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,-64]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2072]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2080]
	ldr	w0, [x19,920]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,896]
	ldr	w0, [x19,2040]
	mul	w0, w23, w0
	bl	ftl_malloc
	str	x0, [x19,2088]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2096]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2104]
	ldr	w1, [x19,920]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w23, [x20,1952]
	str	x0, [x19,888]
	ldrh	w0, [x20,2038]
	mul	w23, w0, w23
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2112]
	lsl	w0, w23, 3
	bl	ftl_malloc
	str	x0, [x19,2120]
	ldrh	w1, [x20,2038]
	ldr	w0, [x19,920]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,904]
	ldrh	w1, [x20,2038]
	ldr	w0, [x19,2040]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2128]
	ldrh	w0, [x20,1962]
	lsl	w0, w0, 1
	strh	w0, [x19,2136]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19,2144]
	ldrh	w0, [x19,2136]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19,2136]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w23, [x20,1962]
	str	x0, [x19,2152]
	add	x0, x0, 32
	lsl	w23, w23, 1
	str	x0, [x19,-80]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2160]
	mov	w0, w23
	bl	ftl_malloc
	ldr	w23, [x20,2056]
	str	x0, [x19,-40]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2168]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2176]
	ldrh	w0, [x20,1962]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x22,#:lo12:.LANCHOR0]
	ldrh	w0, [x20,2048]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x20,2080]
	ldrh	w0, [x20,2048]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,2184]
	ldrh	w0, [x20,2048]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2192]
	ldrh	w0, [x20,2050]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2200]
	ldrh	w2, [x20,2050]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x20,2064]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2208]
	ldr	w0, [x20,2056]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2216]
	ldrh	w0, [x20,2066]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,144]
	ldrh	w1, [x20,2066]
	ldrh	w0, [x20,2036]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2224]
	ldrh	w1, [x20,1962]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,-56]
	ldrh	w0, [x20,2022]
	ldrh	w1, [x20,1974]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,2232]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,2144]
	add	x1, x20, 2112
	ldrh	w4, [x19,2232]
	ldrh	w5, [x20,1974]
	mov	w0, 1
	mov	w2, w4
.L760:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L860
	ldr	x3, [x20,2144]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L760
.L860:
	add	x1, x22, :lo12:.LANCHOR0
.L762:
	cmp	w0, 8
	beq	.L861
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,2144]
	b	.L762
.L861:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,2168]
	cbnz	x1, .L764
.L766:
	adrp	x0, .LC78
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC78
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L765
.L764:
	ldr	x1, [x0,2176]
	cbz	x1, .L766
	ldr	x1, [x0,2208]
	cbz	x1, .L766
	ldr	x1, [x0,2216]
	cbz	x1, .L766
	ldr	x1, [x0,144]
	cbz	x1, .L766
	ldr	x1, [x0,2224]
	cbz	x1, .L766
	ldr	x1, [x0,-56]
	cbz	x1, .L766
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x1, [x22,2144]
	cbz	x1, .L766
	ldr	x0, [x0,-40]
	cbz	x0, .L766
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,928]
	cbz	x1, .L766
	ldr	x1, [x0,944]
	cbz	x1, .L766
	ldr	x1, [x0,2048]
	cbz	x1, .L766
	ldr	x1, [x0,2064]
	cbz	x1, .L766
	ldr	x1, [x0,-88]
	cbz	x1, .L766
	ldr	x1, [x0,912]
	cbz	x1, .L766
	ldr	x1, [x0,2056]
	cbz	x1, .L766
	ldr	x1, [x0,-64]
	cbz	x1, .L766
	ldr	x1, [x0,2072]
	cbz	x1, .L766
	ldr	x0, [x0,2080]
	cbz	x0, .L766
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,896]
	cbz	x1, .L766
	ldr	x1, [x0,2096]
	cbz	x1, .L766
	ldr	x1, [x0,2104]
	cbz	x1, .L766
	ldr	x1, [x0,888]
	cbz	x1, .L766
	ldr	x1, [x0,2112]
	cbz	x1, .L766
	ldr	x1, [x0,2120]
	cbz	x1, .L766
	ldr	x1, [x0,904]
	cbz	x1, .L766
	ldr	x1, [x0,-80]
	cbz	x1, .L766
	ldr	x0, [x0,2144]
	cbz	x0, .L766
	adrp	x0, .LANCHOR0+2080
	ldr	x0, [x0,#:lo12:.LANCHOR0+2080]
	cbz	x0, .L766
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,2184]
	cbz	x1, .L766
	ldr	x1, [x0,2192]
	cbz	x1, .L766
	ldr	x0, [x0,2200]
	cbz	x0, .L766
	mov	w0, 0
.L765:
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
	adrp	x0, .LANCHOR2+2232
	mov	x19, x1
	ldrh	w2, [x0,#:lo12:.LANCHOR2+2232]
	mov	x0, x1
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	x3, 0
	mov	w0, 65535
	mov	w5, 1
.L864:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L862
	ubfx	x4, x2, 5, 11
	add	x3, x3, 2
	lsl	x4, x4, 2
	lsl	w2, w5, w2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	bne	.L864
.L862:
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
	add	x0, x0, 2124
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
	strh	w0, [x1,2208]
	add	x0, x1, 2216
	strh	wzr, [x1,2210]
	strh	wzr, [x1,2212]
	strh	wzr, [x1,2214]
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
.L870:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L883
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L871:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L884
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x25,x3]
	cmp	w4, w2
	bne	.L872
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L872:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L871
.L884:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L870
.L883:
	mov	w24, 0
	ldrh	w26, [x21]
	mov	w20, w24
	adrp	x27, .LANCHOR0
.L875:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L885
	ldrh	w0, [x19]
	uxtw	x22, w20
	cmp	w0, w20
	bne	.L876
	add	x0, x27, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,2028]
	cmp	w1, w0
	bcs	.L876
	strh	w0, [x21,x22,lsl 1]
.L876:
	lsl	x22, x22, 1
	ldrh	w23, [x21,x22]
	cmp	w26, w23
	bls	.L877
	mov	w24, w20
	mov	w26, w23
.L877:
	cbnz	w23, .L878
	ldrh	w0, [x25,x22]
	cbz	w0, .L878
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	w23, [x25,x22]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L878:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L875
.L885:
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
	ldr	w2, [x21,2056]
	ldr	x0, [x22,2176]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x21,2036]
	mov	w1, 255
	ldrh	w2, [x21,2066]
	ldr	x0, [x22,2224]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w1, 0
	mov	w5, -1
.L887:
	add	x3, x20, :lo12:.LANCHOR0
	ldrh	w0, [x3,2066]
	cmp	w0, w1
	bls	.L889
	add	x4, x19, :lo12:.LANCHOR2
	ubfiz	x0, x1, 4, 16
	ldr	x2, [x4,144]
	add	x2, x2, x0
	str	wzr, [x2,4]
	ldr	x2, [x4,144]
	strh	w5, [x2,x0]
	ldr	x2, [x4,144]
	add	x2, x2, x0
	ldrh	w0, [x3,2036]
	ldr	x3, [x4,2224]
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
	add	x1, x0, 2240
	strh	w2, [x0,2242]
	strh	w2, [x0,2240]
	ldr	w2, [x3,2056]
	strh	w2, [x0,2250]
	mov	w2, -3902
	strh	w2, [x0,2244]
	ldrh	w2, [x0,2304]
	strh	w2, [x0,2248]
	ldrh	w2, [x3,2064]
	strh	w2, [x0,2246]
	ldr	x2, [x0,2168]
	str	x2, [x0,2256]
	ldr	x2, [x0,2216]
	str	x2, [x0,2264]
	ldr	x2, [x0,2176]
	str	x2, [x0,2272]
	ldr	x0, [x0,2208]
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
	strh	w0, [x19,2320]
	mov	w0, -1
	ldrh	w2, [x20,2048]
	str	w0, [x19,2332]
	ldr	x0, [x20,2080]
	lsl	w2, w2, 1
	str	xzr, [x19,2312]
	str	wzr, [x19,2324]
	str	wzr, [x19,2328]
	str	wzr, [x19,160]
	strh	wzr, [x20,2072]
	bl	ftl_memset
	ldrh	w2, [x20,1962]
	mov	w1, 0
	ldr	x0, [x19,-80]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,1962]
	mov	w1, 0
	ldr	x0, [x19,2144]
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
	adrp	x24, .LANCHOR0
	add	x0, x24, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x22, .LANCHOR2
	mov	w26, 6
	add	x20, x22, :lo12:.LANCHOR2
	ldrh	w2, [x0,1962]
	mov	w25, 0
	mov	x21, x20
	mov	w23, w25
	mov	w19, w25
	ldr	x0, [x20,-56]
	mul	w2, w2, w26
	bl	ftl_memset
	str	xzr, [x20,-16]
	str	xzr, [x20,-48]
	str	xzr, [x20,-32]
	strh	wzr, [x20,-24]
	strh	wzr, [x20,-8]
.L892:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x0,1960]
	cmp	w1, w19
	bls	.L898
	mov	x28, 0
	ldrh	w3, [x0,1952]
	ldrh	w2, [x0,2026]
	mov	w20, w28
	mov	x27, x0
.L899:
	cmp	w3, w28, uxth
	bls	.L903
	add	x0, x27, 1984
	mov	w1, w19
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,x28]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L893
	add	w20, w20, w2
	uxth	w20, w20
.L893:
	add	x28, x28, 1
	b	.L899
.L903:
	cbz	w20, .L895
	mov	w0, 32768
	sdiv	w20, w0, w20
.L895:
	ldr	x1, [x21,-56]
	umull	x0, w19, w26
	add	x0, x1, x0
	strh	w20, [x0,4]
	ldrh	w0, [x21]
	cmp	w0, w19
	beq	.L896
	ldrh	w0, [x21,48]
	cmp	w0, w19
	beq	.L896
	ldrh	w0, [x21,96]
	cmp	w0, w19
	beq	.L896
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x21,-40]
	ldrh	w0, [x1,x0]
	cbnz	w0, .L897
	add	w4, w25, 1
	mov	w0, w19
	uxth	w25, w4
	bl	INSERT_FREE_LIST
	b	.L896
.L897:
	add	w23, w23, 1
	mov	w0, w19
	uxth	w23, w23
	bl	INSERT_DATA_LIST
.L896:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L892
.L898:
	add	x22, x22, :lo12:.LANCHOR2
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x27, x28, [sp,80]
	strh	w23, [x22,-24]
	strh	w25, [x22,-8]
	ldp	x23, x24, [sp,48]
	ldp	x21, x22, [sp,32]
	ldp	x25, x26, [sp,64]
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
	ldrh	w2, [x20,2030]
	ldr	x0, [x19,928]
	lsl	w2, w2, 1
	strh	wzr, [x19,924]
	strh	wzr, [x19,936]
	bl	ftl_memset
	ldrh	w3, [x20,2030]
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
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L906
	sub	w2, w0, #1
	cmp	w2, 5
	bhi	.L908
	bl	HynixGetReadRetryDefault
	b	.L906
.L908:
	cmp	w0, 49
	bne	.L909
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
	b	.L924
.L909:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L915
	cmp	w1, 33
	bne	.L910
.L915:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 4
	b	.L925
.L910:
	cmp	w1, 67
	beq	.L916
	cmp	w1, 34
	bne	.L912
.L916:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,760]
	mov	w1, 5
.L925:
	strb	w1, [x0,761]
	mov	w1, 7
	strb	w1, [x0,762]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 764
	add	x1, x1, 256
	mov	w2, 45
	b	.L924
.L912:
	cmp	w1, 68
	beq	.L917
	cmp	w1, 35
	bne	.L906
.L917:
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
.L924:
	bl	ftl_memcpy
.L906:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	FlashReadIdbData
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR2
	mov	w2, 2048
	add	x1, x1, :lo12:.LANCHOR2
	add	x29, sp, 0
	add	x1, x1, 2336
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
	add	x21, x21, 536
	mov	w19, 0
	add	x22, x22, 1620
.L931:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x22
	bl	FlashMemCmp8
	mov	w23, w0
	cbnz	w0, .L928
	add	x0, x20, :lo12:.LANCHOR1
	ubfiz	x19, x19, 5, 32
	add	x1, x0, 536
	adds	x19, x1, x19
	beq	.L934
	ldrb	w3, [x19,22]
	mov	x2, 0
	mov	x1, x0
	b	.L933
.L928:
	add	w19, w19, 1
	add	x21, x21, 32
	cmp	w19, 72
	bne	.L931
	b	.L934
.L933:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,2840]
	cmp	w4, w3
	beq	.L932
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L933
	mov	w0, w2
.L932:
	add	x20, x20, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x20, 2840
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x2, x1
	add	x0, x0, 88
	mov	w2, 32
	bl	ftl_memcpy
	add	x0, x20, 472
	mov	x1, x19
	mov	w2, 32
	bl	ftl_memcpy
	b	.L929
.L934:
	mov	w23, -1
.L929:
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
	bne	.L939
	cbz	x3, .L940
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L940:
	cbz	x20, .L938
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
	b	.L938
.L939:
	cbz	x3, .L943
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L943:
	cbz	x20, .L938
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
.L938:
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
	adrp	x4, .LANCHOR4+32
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+32]
	add	x4, x4, 4096
	cbnz	w2, .L956
	add	x1, x4, x1
	b	.L958
.L956:
	add	x0, x4, x1
	mov	x1, x6
.L958:
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
	adrp	x0, .LANCHOR0+2068
	ldr	w0, [x0,#:lo12:.LANCHOR0+2068]
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
.L965:
	cmp	x19, x26
	beq	.L973
	cbnz	w20, .L966
	mov	x0, x23
	mov	x1, x27
	mov	w2, w19
	bl	printk
.L966:
	cmp	w25, 4
	mov	x0, x21
	bne	.L967
	ldr	w1, [x22,x19,lsl 2]
	b	.L972
.L967:
	cmp	w25, 2
	bne	.L969
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L972
.L969:
	ldrb	w1, [x22,x19]
.L972:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L970
	adrp	x1, .LC81
	mov	x0, x24
	add	x1, x1, :lo12:.LC81
	mov	w20, 0
	bl	printk
.L970:
	add	x19, x19, 1
	b	.L965
.L973:
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
	ldr	w0, [x1,20]
	cmp	w0, 3
	bls	.L1005
	ldr	w0, [x19,16]
	tbz	x0, 2, .L1005
	mov	x0, x19
	bl	wait_for_nandc_xfer_completed
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L976
	adrp	x22, .LC82
	adrp	x23, .LC83
	mov	w21, 0
	add	x22, x22, :lo12:.LC82
	add	x23, x23, :lo12:.LC83
.L977:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L985
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,20]
	cmp	w0, 5
	bhi	.L978
.L981:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L977
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
	b	.L977
.L978:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L981
	ldr	w0, [x29,72]
	tbz	x0, 17, .L981
.L985:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 1888
	ldr	w0, [x19,40]
	cbz	w0, .L986
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
	b	.L986
.L976:
	adrp	x22, .LC84
	adrp	x23, .LC83
	add	x22, x22, :lo12:.LC84
	add	x23, x23, :lo12:.LC83
.L987:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L1013
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L987
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
	b	.L987
.L1013:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1936]
	cbz	w0, .L990
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L990:
	add	x21, x20, :lo12:.LANCHOR0
	add	x21, x21, 1888
	ldr	w0, [x21,40]
	cbz	w0, .L991
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
.L991:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w0, [x0,1936]
	cbz	w0, .L986
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L986:
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x20,1928]
	b	.L974
.L1005:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1005
.L974:
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
	cbnz	x0, .L1015
	cbnz	x4, .L1016
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x22, x29, 112
	bl	ftl_memset
.L1016:
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
	cbnz	w21, .L1017
	add	x1, x19, :lo12:.LANCHOR0
	ubfx	x2, x24, 1, 7
	mov	w3, 128
	add	x2, x22, x2, lsl 2
	ldr	w0, [x1,1940]
	cmp	w0, 25
	mov	w0, 64
	csel	w3, w0, w3, cc
	mov	w0, w21
.L1019:
	cmp	x22, x2
	add	w4, w0, w3
	beq	.L1061
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
	b	.L1019
.L1061:
	add	x0, x19, :lo12:.LANCHOR0
	lsr	w24, w24, 2
	ldr	w5, [x0,1940]
	ldr	w4, [x0,20]
	mov	w0, 0
	mov	w23, w0
.L1021:
	cmp	w0, w24
	bcs	.L1017
	cbz	w5, .L1017
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x20,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L1045
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L1045
	cmp	w4, 5
	bls	.L1023
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
	bls	.L1024
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L1025
.L1024:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L1060
.L1023:
	cmp	w4, 3
	bls	.L1025
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
	bls	.L1026
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L1060
.L1026:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L1060:
	orr	w3, w1, w3, lsl 5
.L1025:
	cmp	w23, w3
	csel	w23, w23, w3, cs
	b	.L1022
.L1045:
	mov	w23, -1
.L1022:
	add	w0, w0, 1
	b	.L1021
.L1017:
	str	wzr, [x20,16]
	b	.L1028
.L1015:
	cmp	w21, 1
	bne	.L1059
	mov	w23, 0
	mov	w28, 2
.L1029:
	cmp	w23, w24
	bcs	.L1062
	and	w27, w23, 3
	mov	x3, x26
	cbz	x26, .L1031
	ubfiz	x0, x23, 9, 23
	add	x3, x26, x0
.L1031:
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
	b	.L1029
.L1062:
	mov	w23, 0
	b	.L1028
.L1059:
	mov	w1, 0
	mov	x4, 0
	mov	w27, 0
	mov	w0, w25
	mov	w2, 2
	mov	w3, w1
	mov	x5, x4
	mov	w23, w27
	bl	NandcXferStart
.L1034:
	cmp	w27, w24
	bcs	.L1028
	mov	w0, w25
	add	w28, w27, 2
	bl	NandcXferComp
	ldr	w0, [x20,32]
	cmp	w28, w24
	str	w0, [x29,104]
	bcs	.L1035
	mov	x4, 0
	mov	w0, w25
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L1035:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L1050
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w23, w0
	csel	w23, w23, w0, cs
	b	.L1036
.L1050:
	mov	w23, -1
.L1036:
	and	w2, w27, 3
	mov	x3, x26
	cbz	x26, .L1037
	ubfiz	x3, x27, 9, 23
	add	x3, x26, x3
.L1037:
	cmp	x22, xzr
	mov	w0, 2
	csel	w4, w0, wzr, ne
	mov	w1, 0
	mov	x0, x20
	mul	w4, w4, w27
	mov	w27, w28
	add	x4, x22, x4
	bl	NandcCopy1KB
	b	.L1034
.L1028:
	cbnz	w21, .L1040
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19,20]
	cmp	w0, 5
	bls	.L1040
	ldr	w0, [x20]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L1040
	orr	w0, w0, 131072
	mov	w23, -1
	str	w0, [x20]
.L1040:
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
	cbnz	w19, .L1064
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,9]
	ldr	w0, [x0,12]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L1064:
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
	ldr	w0, [x19,20]
	cmp	w0, 8
	mov	w0, 12
	csel	w22, w22, w0, cc
	cbz	w4, .L1067
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
	bne	.L1068
.L1077:
	mov	w21, -1
	b	.L1069
.L1068:
	adrp	x0, .LC85
	mov	w1, w24
	add	x0, x0, :lo12:.LC85
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L1070
	add	x19, x19, x23, sxtw 4
	ldr	x0, [x19,24]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L1070:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,40]
	add	w1, w1, 1
	str	w1, [x0,40]
	cmp	w1, 2047
	bls	.L1069
	mov	x27, 0
	str	wzr, [x0,40]
	mov	x28, x27
.L1067:
	mov	w19, 0
	mov	w26, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L1075:
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
	bhi	.L1071
	cmp	w0, 2
	bhi	.L1081
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L1081
	sub	w19, w22, w20
	mov	w21, w0
	mov	w26, 0
	b	.L1073
.L1071:
	cmp	w6, w20
	bcs	.L1082
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L1083
	mov	w6, w20
	b	.L1082
.L1081:
	mov	x27, 0
	mov	w19, w22
	mov	w21, w0
	mov	w26, 0
	mov	x28, x27
	b	.L1072
.L1082:
	mov	w20, 0
.L1072:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L1075
.L1073:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L1074
.L1083:
	mov	w19, w5
.L1074:
	cbz	w19, .L1076
	adrp	x0, .LC86
	mov	w1, w19
	add	x0, x0, :lo12:.LC86
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L1076:
	cbz	w26, .L1069
	adrp	x0, .LC87
	mov	w1, w23
	add	x0, x0, :lo12:.LC87
	mov	w2, w24
	bl	printk
	cbz	w25, .L1077
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L1069:
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
	bne	.L1093
	adrp	x20, .LANCHOR0
	add	x25, x20, :lo12:.LANCHOR0
	ldrb	w26, [x25,16]
	cbnz	w26, .L1094
.L1096:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,1864]
	cbz	w0, .L1093
	b	.L1111
.L1094:
	mov	w0, w21
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	strb	wzr, [x25,16]
	bl	FlashReadRawPage
	strb	w26, [x25,16]
	cmn	w0, #1
	beq	.L1096
	mov	w19, w0
	b	.L1093
.L1111:
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
	beq	.L1097
	ldrb	w0, [x20,1944]
	cmp	w19, w0, lsr 1
	bls	.L1093
.L1097:
	lsr	w0, w25, 8
	bl	NandcSetDdrPara
.L1093:
	adrp	x0, .LANCHOR4+48
	cmn	w19, #1
	ldr	x4, [x0,#:lo12:.LANCHOR4+48]
	bne	.L1098
	cbz	x4, .L1098
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w0, w21
	blr	x4
	mov	w19, w0
	adrp	x0, .LC88
	mov	w1, w19
	add	x0, x0, :lo12:.LC88
	mov	w2, w21
	mov	w3, w22
	bl	printk
.L1098:
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
	beq	.L1116
	cmn	w22, #1
	bne	.L1113
.L1116:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,1844]
	tbz	x0, 0, .L1113
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,1864]
	b	.L1115
.L1113:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,1864]
.L1115:
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
	adrp	x24, .LANCHOR0
	adrp	x23, .LANCHOR4
	strb	w0, [x29,120]
	mov	w0, 40
	strb	w0, [x29,121]
	mov	w0, 24
	add	x1, x24, :lo12:.LANCHOR0
	strb	w0, [x29,122]
	mov	w0, 16
	strb	w0, [x29,123]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	add	x0, x23, :lo12:.LANCHOR4
	adrp	x22, .LANCHOR1
	add	x21, x22, :lo12:.LANCHOR1
	ldr	x1, [x1,1856]
	add	x21, x21, 472
	mov	w20, 0
	str	x1, [x0,56]
	mov	w26, 4
	str	wzr, [x0,64]
	mov	w25, -1
	mov	w0, 0
	ldrh	w27, [x21,10]
	bl	flash_enter_slc_mode
.L1124:
	add	w28, w20, 1
	mov	x19, 0
.L1126:
	add	x0, x29, 120
	ldrb	w0, [x19,x0]
	bl	FlashBchSel
	add	x4, x24, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w1, w20
	mov	x3, 0
	str	x4, [x29,104]
	ldr	x2, [x4,1856]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1125
	ldr	x4, [x29,104]
	mov	w0, 0
	mov	w1, w28
	mov	x3, 0
	ldr	x2, [x4,1856]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1125
	add	x19, x19, 1
	cmp	x19, 4
	beq	.L1127
	b	.L1126
.L1128:
	mov	w1, 2036
	add	x0, x28, 12
	mov	w25, -1
	bl	JSHash
	ldr	w1, [x28,8]
	cmp	w1, w0
	beq	.L1137
.L1127:
	subs	w26, w26, #1
	add	w20, w20, w27
	bne	.L1124
	mov	w0, w26
	bl	flash_exit_slc_mode
	b	.L1136
.L1125:
	add	x19, x23, :lo12:.LANCHOR4
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x28, [x19,56]
	ldr	w1, [x28]
	cmp	w1, w0
	bne	.L1127
	cbnz	w25, .L1128
	add	x22, x22, :lo12:.LANCHOR1
	ldrh	w0, [x22,482]
	udiv	w20, w20, w0
	add	w20, w20, 1
	str	w20, [x19,68]
	b	.L1136
.L1137:
	add	x1, x28, 160
	mov	w2, 32
	mov	x0, x21
	add	x25, x24, :lo12:.LANCHOR0
	bl	ftl_memcpy
	ldr	x1, [x19,56]
	mov	w2, 32
	add	x0, x25, 88
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
	strb	w0, [x25,1864]
	ldrh	w0, [x21,10]
	udiv	w0, w20, w0
	add	w2, w0, 1
	cbz	w0, .L1130
	str	w2, [x19,68]
	b	.L1131
.L1130:
	mov	w0, 2
	str	w0, [x19,68]
.L1131:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w1, [x1,14]
	mov	w25, 0
	strb	w1, [x0,72]
	b	.L1127
.L1136:
	mov	w0, w25
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
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
	bls	.L1139
	ldrb	w0, [x2,1864]
	cbz	w0, .L1140
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L1140:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L1139:
	ldr	x0, [x29,120]
	mov	w21, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x28, 8, 8
	str	x0, [x29,96]
.L1141:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,73]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L1166
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w21
	ldrb	w0, [x0,752]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1142
	bl	SandiskSetRRPara
	b	.L1143
.L1142:
	bl	ToshibaSetRRPara
.L1143:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,752]
	cmp	w0, 34
	bne	.L1144
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,73]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L1144
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L1144:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L1145
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
	b	.L1146
.L1145:
	ldr	w1, [x29,116]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L1146:
	cmn	w28, #1
	beq	.L1147
	add	x0, x19, :lo12:.LANCHOR0
	cmn	w25, #1
	csel	w25, w25, w28, ne
	ldrb	w0, [x0,1944]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1149
	mov	x26, 0
	mov	x27, x26
.L1147:
	add	w21, w21, 1
	b	.L1141
.L1166:
	mov	w28, w25
.L1149:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,752]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x20
	bhi	.L1151
	bl	SandiskSetRRPara
	b	.L1152
.L1151:
	bl	ToshibaSetRRPara
.L1152:
	ldr	x0, [x29,120]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	ldrb	w0, [x19,1944]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1153
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1153:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L1154
	mov	w0, 4
	bl	NandcSetDdrMode
.L1154:
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
.L1168:
	add	x0, x27, :lo12:.LANCHOR4
	ldrb	w0, [x0,73]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L1171
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1169
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,1944]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1174
	mov	x23, 0
	mov	x24, x23
.L1169:
	add	w26, w26, 1
	b	.L1168
.L1174:
	mov	w19, w0
.L1171:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,1944]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1173
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1173:
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
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	w24, w0
	mov	w23, 0
	mov	w0, w24
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	mov	w25, w1
	mov	x27, x2
	mov	x26, x3
	adrp	x21, .LANCHOR0
	bl	NandcWaitFlashReady
	mov	w19, -1
	add	x0, x21, :lo12:.LANCHOR0
	sbfiz	x1, x24, 4, 32
	add	x0, x0, 24
	adrp	x4, .LANCHOR4
	add	x2, x0, x1
	mov	w6, 239
	mov	w7, 137
	ldr	x28, [x0,x1]
	ldrb	w20, [x2,8]
	lsl	x5, x20, 8
.L1181:
	add	x0, x4, :lo12:.LANCHOR4
	ldrb	w0, [x0,73]
	cmp	w23, w0
	bcs	.L1184
	add	x22, x28, x5
	mov	w0, 200
	str	x4, [x29,96]
	add	w23, w23, 1
	str	x5, [x29,104]
	str	w6, [x22,2056]
	str	w7, [x22,2052]
	str	x6, [x29,112]
	str	x7, [x29,120]
	bl	NandcDelayns
	str	w23, [x22,2048]
	mov	w0, w24
	str	wzr, [x22,2048]
	mov	w1, w25
	str	wzr, [x22,2048]
	mov	x2, x27
	str	wzr, [x22,2048]
	mov	x3, x26
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x7, [x29,120]
	ldr	x6, [x29,112]
	ldr	x5, [x29,104]
	ldr	x4, [x29,96]
	beq	.L1181
	add	x1, x21, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w2, [x1,1944]
	add	w2, w2, w2, lsl 1
	cmp	w0, w2, lsr 2
	bcc	.L1187
	mov	x26, 0
	mov	x27, x26
	b	.L1181
.L1187:
	mov	w19, w0
.L1184:
	add	x20, x28, x20, lsl 8
	mov	w0, 239
	add	x21, x21, :lo12:.LANCHOR0
	str	w0, [x20,2056]
	mov	w0, 137
	str	w0, [x20,2052]
	mov	w0, 200
	bl	NandcDelayns
	str	wzr, [x20,2048]
	ldrb	w0, [x21,1944]
	str	wzr, [x20,2048]
	str	wzr, [x20,2048]
	add	w0, w0, w0, lsl 1
	str	wzr, [x20,2048]
	cmp	w19, w0, lsr 2
	bcc	.L1186
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1186:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
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
	add	x28, x21, :lo12:.LANCHOR0
	mov	w27, w1
	add	x0, x28, 760
	add	x1, x0, x22
	mov	w0, w22
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	x26, x2
	mov	x24, x3
	ldrb	w20, [x1,12]
	mov	x23, x22
	ldrb	w25, [x28,762]
	bl	NandcWaitFlashReady
	mov	w4, 0
	mov	w19, -1
	add	x5, x28, 764
.L1194:
	cmp	w4, w25
	bcs	.L1198
	add	w20, w20, 1
	ldrb	w1, [x28,761]
	mov	x2, x5
	mov	w0, w23
	uxtb	w20, w20
	str	x4, [x29,96]
	cmp	w20, w25
	str	x5, [x29,104]
	csel	w20, w20, wzr, cc
	mov	w3, w20
	bl	HynixSetRRPara
	mov	w0, w23
	mov	w1, w27
	mov	x2, x26
	mov	x3, x24
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x5, [x29,104]
	ldr	x4, [x29,96]
	beq	.L1196
	ldrb	w1, [x28,1944]
	cmn	w19, #1
	csel	w19, w19, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1201
	mov	x24, 0
	mov	x26, x24
.L1196:
	add	w4, w4, 1
	b	.L1194
.L1201:
	mov	w19, w0
.L1198:
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x21, x22
	ldrb	w0, [x21,1944]
	strb	w20, [x22,772]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1200
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1200:
	mov	w0, w19
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
	cbnz	w19, .L1208
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,9]
	ldr	w2, [x0,12]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L1208
	ldrb	w0, [x0,8]
	cbnz	w0, .L1209
	sub	w21, w21, #2
	b	.L1208
.L1209:
	mov	w21, 4
.L1208:
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
	adrp	x23, .LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	add	x20, x23, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR4
	add	x19, x22, :lo12:.LANCHOR4
	mov	w21, 0
	stp	x25, x26, [sp,64]
	ldr	x0, [x20,1856]
	str	x0, [x19,56]
	ldrb	w0, [x19,74]
	bl	FlashBchSel
	ldr	x0, [x20,1856]
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
	ldrb	w1, [x20,1864]
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
.L1213:
	add	x20, x23, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w0, 0
	add	x24, x22, :lo12:.LANCHOR4
	add	w26, w19, 1
	ldr	w1, [x20,12]
	mul	w1, w19, w1
	bl	FlashEraseBlock
	ldr	w1, [x20,12]
	mov	x3, 0
	ldr	x2, [x20,1856]
	mov	w0, 0
	mul	w1, w19, w1
	bl	FlashProgPage
	ldr	w1, [x20,12]
	mov	x3, 0
	ldr	x2, [x20,1856]
	mov	w0, 0
	mul	w1, w19, w1
	add	w1, w1, 1
	bl	FlashProgPage
	ldr	w1, [x20,12]
	mov	w0, 0
	ldr	x2, [x24,80]
	mov	x3, 0
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1211
	ldr	x25, [x24,56]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L1211
	mov	w1, 2036
	add	x0, x25, 12
	bl	JSHash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L1211
	ldr	w0, [x20,12]
	cmp	w21, 1
	str	w26, [x24,68]
	mul	w19, w19, w0
	str	w19, [x24,64]
	beq	.L1214
	mov	w21, 1
.L1211:
	cmp	w26, 4
	mov	w19, w26
	bne	.L1213
	b	.L1212
.L1214:
	mov	w21, 2
.L1212:
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
	stp	x25, x26, [sp,64]
	mov	x26, x0
	mov	w0, 60
	strb	w0, [x29,104]
	mov	w0, 40
	strb	w0, [x29,105]
	mov	w0, 24
	stp	x19, x20, [sp,16]
	strb	w0, [x29,106]
	adrp	x19, .LANCHOR0
	mov	w0, 16
	strb	w0, [x29,107]
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	ldr	w1, [x0,1872]
	ldrb	w27, [x0,1944]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L1220
	mov	w0, 0
	bl	flash_enter_slc_mode
.L1220:
	adrp	x24, .LC89
	mov	x0, x26
	mov	w1, 0
	mov	w2, 2048
	mov	w23, -1
	mov	w20, 2
	add	x24, x24, :lo12:.LC89
	adrp	x28, .LANCHOR4
	bl	ftl_memset
.L1221:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,9]
	cmp	w20, w0
	bcs	.L1225
	mov	x22, 0
.L1223:
	add	x0, x29, 104
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w25, [x22,x0]
	mov	w0, w25
	bl	FlashBchSel
	ldr	w1, [x21,12]
	mov	w0, 0
	ldr	x2, [x21,1856]
	mov	x3, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1222
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L1223
	b	.L1224
.L1222:
	ldr	x0, [x21,1856]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L1224
	mov	w1, w25
	mov	x0, x24
	bl	printk
	ldr	x1, [x21,1856]
	mov	x0, x26
	mov	w2, 2048
	bl	ftl_memcpy
	ldr	x0, [x21,1856]
	ldr	w0, [x0,512]
	strb	w0, [x21,9]
	add	x0, x28, :lo12:.LANCHOR4
	ldr	w1, [x0,68]
	cmp	w1, w20
	bls	.L1228
	mov	w23, 0
	str	w20, [x0,68]
	bl	FlashSavePhyInfo
.L1224:
	add	w20, w20, 1
	b	.L1221
.L1228:
	mov	w23, 0
.L1225:
	mov	w0, w27
	add	x19, x19, :lo12:.LANCHOR0
	bl	FlashBchSel
	ldr	w1, [x19,1872]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L1230
	mov	w0, 0
	bl	flash_exit_slc_mode
.L1230:
	mov	w0, w23
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
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	mov	x22, x0
	mov	w0, 32768
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x23, .LANCHOR4
	bl	ftl_malloc
	add	x21, x23, :lo12:.LANCHOR4
	str	x0, [x20,1856]
	mov	w0, 32768
	add	x24, x20, 24
	adrp	x25, .LC90
	mov	w28, 0
	bl	ftl_malloc
	str	x0, [x21,80]
	mov	w0, 4096
	mov	w27, 144
	bl	ftl_malloc
	str	x0, [x21,88]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x21,96]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x21,104]
	mov	w0, 128
	str	w0, [x20,12]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x21,74]
	mov	x0, x22
	strb	wzr, [x20,1864]
	strb	w1, [x20,9]
	strb	wzr, [x20,8]
	add	x20, x20, 1620
	strb	w1, [x21,72]
	mov	x26, x20
	str	wzr, [x21,68]
	str	wzr, [x21,40]
	bl	NandcInit
	add	x0, x25, :lo12:.LC90
	str	x0, [x29,104]
.L1236:
	uxtb	w25, w28
	mov	w0, w25
	bl	FlashReset
	ldrb	w22, [x24,8]
	ldr	x21, [x24]
	mov	w0, w25
	ubfiz	x22, x22, 8, 8
	add	x21, x21, x22
	bl	NandcFlashCs
	mov	w0, 200
	str	w27, [x21,2056]
	str	wzr, [x21,2052]
	bl	NandcDelayns
	ldr	w0, [x21,2048]
	strb	w0, [x20]
	ldr	w0, [x21,2048]
	strb	w0, [x20,1]
	ldr	w0, [x21,2048]
	strb	w0, [x20,2]
	ldr	w0, [x21,2048]
	strb	w0, [x20,3]
	ldr	w0, [x21,2048]
	strb	w0, [x20,4]
	ldr	w0, [x21,2048]
	strb	w0, [x20,5]
	ldr	w0, [x21,2048]
	strb	w0, [x20,6]
	ldr	w0, [x21,2048]
	strb	w0, [x20,7]
	mov	w0, w25
	bl	NandcFlashDeCs
	ldrb	w2, [x20]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1233
	ldr	x0, [x29,104]
	add	w1, w28, 1
	ldrb	w3, [x20,1]
	ldrb	w4, [x20,2]
	ldrb	w5, [x20,3]
	ldrb	w6, [x20,4]
	ldrb	w7, [x20,5]
	bl	printk
.L1233:
	cbnz	w28, .L1234
	ldrb	w0, [x26]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1276
	ldrb	w0, [x26,1]
	cmp	w0, 255
	beq	.L1276
.L1234:
	add	w28, w28, 1
	add	x24, x24, 16
	cmp	w28, 4
	add	x20, x20, 8
	bne	.L1236
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 173
	beq	.L1237
	ldr	w0, [x0,1868]
	bl	NandcSetDdrMode
.L1237:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 0
	add	x0, x20, 760
	mov	w2, 852
	bl	ftl_memset
	strb	wzr, [x20,16]
	ldrb	w2, [x20,1621]
	adrp	x1, .LANCHOR1
	add	x0, x1, :lo12:.LANCHOR1
	cmp	w2, 218
	add	x0, x0, 472
	cset	w3, eq
	str	x0, [x20,744]
	add	x20, x20, 1620
	cbnz	w3, .L1238
	cmp	w2, 241
	beq	.L1238
	cmp	w2, 220
	bne	.L1239
	ldrb	w0, [x20,3]
	cmp	w0, 149
	bne	.L1239
.L1238:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w4, 1
	mov	w5, 16
	strb	w4, [x0,8]
	add	x4, x23, :lo12:.LANCHOR4
	strb	w5, [x0,9]
	ldrb	w0, [x0,1620]
	strb	w5, [x4,74]
	add	x5, x1, :lo12:.LANCHOR1
	cmp	w0, 152
	strb	w0, [x5,2969]
	bne	.L1240
	mov	w0, 24
	strb	w0, [x4,74]
.L1240:
	cbz	w3, .L1241
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,2982]
	mov	w2, -38
	b	.L1307
.L1241:
	cmp	w2, 220
	bne	.L1242
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,2982]
	mov	w2, -36
.L1307:
	strb	w2, [x0,2970]
.L1242:
	add	x20, x1, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x20, 2872
	mov	w2, 32
	add	x0, x0, 88
	bl	ftl_memcpy
	add	x0, x20, 472
	add	x1, x20, 2968
	mov	w2, 32
	bl	ftl_memcpy
.L1239:
	add	x20, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20,8]
	cbnz	w0, .L1243
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L1245
	ldr	x0, [x20,744]
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x20,1844]
	tbnz	x1, 0, .L1245
	mov	w1, 1
	strb	w1, [x20,1864]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
	bl	NandcSetMode
.L1245:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,744]
	ldrb	w0, [x0,26]
	strb	w0, [x20,120]
	bl	FlashLoadPhyInfo
	cbz	w0, .L1243
	ldr	w0, [x20,1868]
	cbz	w0, .L1248
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L1308
.L1248:
	ldrb	w0, [x20,1844]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,1844]
.L1308:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L1243
	mov	w0, 1
	add	x20, x19, :lo12:.LANCHOR0
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x1, [x20,744]
	adrp	x0, .LC91
	add	x0, x0, :lo12:.LC91
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L1235
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
	blt	.L1250
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L1251
.L1250:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L1251:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1844]
	tst	w0, 6
	beq	.L1252
	bl	FlashSavePhyInfo
	add	x1, x23, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x1,64]
	bl	FlashDdrParaScan
.L1252:
	bl	FlashSavePhyInfo
.L1243:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x3, [x1,744]
	ldrh	w0, [x3,16]
	ldrb	w4, [x3,26]
	ubfx	x2, x0, 7, 1
	strb	w2, [x1,16]
	add	x2, x23, :lo12:.LANCHOR4
	ubfx	x5, x0, 3, 1
	strb	w4, [x1,120]
	strb	w5, [x2,112]
	ubfx	x5, x0, 4, 1
	str	xzr, [x2,48]
	strb	w5, [x1,1945]
	ubfx	x5, x0, 8, 3
	strb	w5, [x1,1844]
	tbz	x0, 6, .L1254
	ldrb	w0, [x3,19]
	ldrb	w5, [x1,762]
	strb	w5, [x2,73]
	sub	w5, w0, #1
	ldrb	w6, [x1,761]
	uxtb	w5, w5
	strb	w0, [x1,752]
	strb	w6, [x1,753]
	cmp	w5, 5
	bhi	.L1255
	sub	w0, w0, #5
	adrp	x5, HynixReadRetrial
	add	x5, x5, :lo12:HynixReadRetrial
	str	x5, [x2,48]
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L1254
	mov	w0, 1
	str	w0, [x1,1936]
	b	.L1254
.L1255:
	sub	w1, w0, #17
	uxtb	w1, w1
	cmp	w1, 2
	bhi	.L1257
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x2,48]
	beq	.L1258
	mov	w0, 7
	b	.L1309
.L1258:
	mov	w0, 15
.L1309:
	strb	w0, [x2,73]
	b	.L1254
.L1257:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1277
	cmp	w0, 33
	bne	.L1259
.L1277:
	add	x0, x23, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	mov	w2, 4
	str	x1, [x0,48]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,753]
	mov	w1, 7
	strb	w1, [x0,73]
	b	.L1254
.L1259:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w5, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L1278
	cbz	w5, .L1261
.L1278:
	add	x2, x23, :lo12:.LANCHOR4
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x1, [x2,48]
	beq	.L1263
	cmp	w0, 68
	beq	.L1263
	mov	w0, 7
	strb	w0, [x2,73]
	b	.L1264
.L1263:
	add	x0, x23, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,73]
.L1264:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w5, .L1310
	mov	w1, 5
.L1310:
	strb	w1, [x0,753]
	b	.L1254
.L1261:
	cmp	w0, 49
	bne	.L1254
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x2,48]
.L1254:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,1872]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L1266
	cbz	w4, .L1266
	strb	wzr, [x3,18]
.L1266:
	ldrh	w2, [x3,10]
	ldrb	w1, [x3,12]
	ldrb	w0, [x3,18]
	sdiv	w1, w2, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1620]
	cmp	w1, 44
	bne	.L1267
	ldrb	w1, [x0,1864]
	cbz	w1, .L1267
	ldr	w2, [x0,1872]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L1268
	ldrb	w0, [x0,120]
	cbnz	w0, .L1267
.L1268:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,1864]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L1267:
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w0, [x1,1844]
	tst	w0, 6
	beq	.L1269
	ldrb	w1, [x1,1864]
	cbnz	w1, .L1270
	tbnz	x0, 0, .L1269
.L1270:
	add	x1, x23, :lo12:.LANCHOR4
	mov	w0, 0
	ldr	w1, [x1,64]
	bl	FlashDdrParaScan
.L1269:
	mov	w0, 0
	add	x20, x19, :lo12:.LANCHOR0
	bl	flash_exit_slc_mode
	ldr	x0, [x20,744]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2336
	bl	FlashReadIdbDataRaw
	cbnz	w0, .L1271
	add	x23, x23, :lo12:.LANCHOR4
	ldrb	w1, [x20,9]
	ldrb	w0, [x23,72]
	cmp	w1, w0
	bls	.L1272
	strb	w0, [x20,9]
.L1272:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,9]
	cmp	w0, 15
	bhi	.L1273
.L1271:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 16
	strb	w1, [x0,9]
.L1273:
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
	bne	.L1274
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
.L1274:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	FlashSuspend
	mov	w0, 0
	b	.L1235
.L1276:
	mov	w0, -2
.L1235:
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
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x23, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxth	w22, w2
	ldr	x2, [x0,744]
	ldrb	w24, [x2,19]
	ldrb	w2, [x0,120]
	cbz	w2, .L1312
	ldr	w2, [x0,1872]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w2, w0
	beq	.L1311
.L1312:
	sub	w0, w24, #5
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1314
	cmp	w24, 68
	beq	.L1314
	cmp	w24, 35
	beq	.L1314
	sub	w0, w24, #17
	uxtb	w0, w0
	cmp	w0, 2
	bhi	.L1311
.L1314:
	mov	w27, w1
	sub	w24, w24, #17
	add	x23, x23, :lo12:.LANCHOR0
	add	x26, x23, 24
	add	x26, x26, x21, sxtw 4
.L1317:
	ldr	x0, [x23,744]
	ldrh	w0, [x0,10]
	cmp	w0, w22
	bls	.L1311
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 996
	ldrh	w1, [x0,w22,sxtw 1]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1311
	adrp	x25, .LANCHOR4
	mov	w1, 255
	add	x19, x25, :lo12:.LANCHOR4
	mov	w2, 32768
	ldr	x0, [x19,80]
	bl	ftl_memset
	uxtb	w0, w24
	cmp	w0, 1
	bhi	.L1318
	adrp	x28, .LANCHOR1
	add	x28, x28, :lo12:.LANCHOR1
	ldr	w1, [x28,3000]
	cbz	w1, .L1319
	ldrb	w20, [x26,8]
	mov	w0, w21
	ldr	x19, [x26]
	ubfiz	x20, x20, 8, 8
	str	x1, [x29,104]
	add	x19, x19, x20
	bl	NandcFlashCs
	mov	w0, 239
	mov	w20, 146
	str	w0, [x19,2056]
	mov	w0, w21
	str	w20, [x19,2052]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	bl	NandcFlashDeCs
	ldr	x1, [x29,104]
	cmp	w1, 1
	bne	.L1320
	mov	w0, w21
	bl	NandcFlashCs
	mov	w0, 238
	str	w0, [x19,2056]
	str	w20, [x19,2052]
	mov	w0, 2000
	bl	NandcDelayns
	ldr	w20, [x19,2048]
	mov	w0, w21
	uxtb	w20, w20
	bl	NandcFlashDeCs
	cbnz	w20, .L1321
	mov	w0, 2
	str	w0, [x28,3000]
	b	.L1320
.L1321:
	str	wzr, [x28,3000]
	b	.L1311
.L1320:
	add	x25, x25, :lo12:.LANCHOR4
	add	w1, w22, w27
	mov	x3, 0
	mov	w0, w21
	ldr	x2, [x25,80]
	bl	FlashProgPage
	mov	w0, w21
	bl	NandcFlashCs
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 146
	str	w0, [x19,2052]
	mov	w0, 1
	str	w0, [x19,2048]
	str	wzr, [x19,2048]
	mov	w0, w21
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	bl	NandcFlashDeCs
	b	.L1319
.L1318:
	ldr	x2, [x19,80]
	mov	w0, w21
	add	w1, w22, w27
	mov	x3, 0
	bl	FlashProgPage
.L1319:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L1317
.L1311:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
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
	adrp	x23, .LC92
	adrp	x24, .LC94
	ldrb	w26, [x1,#:lo12:.LANCHOR1+481]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w27, w2
	mov	w22, 0
	add	x21, x21, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC92
	add	x24, x24, :lo12:.LC94
.L1333:
	cmp	w22, w25
	beq	.L1367
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
	bcc	.L1334
	mov	w0, -1
	str	w0, [x19]
	b	.L1335
.L1334:
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
	ldrb	w0, [x0,1944]
	add	w0, w0, w0, lsl 1
	cmp	w3, w0, lsr 2
	bls	.L1338
	cmn	w3, #1
	mov	w0, 256
	csel	w3, w3, w0, eq
.L1338:
	cmp	w3, 256
	beq	.L1349
	cmn	w3, #1
	bne	.L1339
.L1349:
	str	w3, [x19]
	b	.L1341
.L1339:
	str	wzr, [x19]
.L1341:
	ldr	x0, [x19,16]
	cbz	x0, .L1342
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L1342
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1342
	str	w1, [x19]
.L1342:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1335
	add	x5, x5, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	mov	x0, x23
	ldrb	w2, [x5,1944]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L1344
	adrp	x0, .LC93
	mov	w2, 4
	add	x0, x0, :lo12:.LC93
	mov	w3, 8
	bl	rknand_print_hex
.L1344:
	ldr	x1, [x19,16]
	cbz	x1, .L1335
	mov	w2, 4
	mov	x0, x24
	mov	w3, w2
	bl	rknand_print_hex
.L1335:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L1333
.L1367:
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
	str	w3, [x29,156]
	add	x3, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	str	w1, [x29,152]
	mov	x25, x0
	ldrb	w4, [x3,16]
	ldrb	w23, [x3,8]
	str	w2, [x29,140]
	str	w4, [x29,136]
	cbz	w23, .L1404
	bl	FlashReadSlc2KPages
	b	.L1450
.L1404:
	adrp	x0, .LC92
	mov	w24, w23
	add	x0, x0, :lo12:.LC92
	str	x0, [x29,128]
	adrp	x0, .LC94
	add	x0, x0, :lo12:.LC94
	str	x0, [x29,120]
	adrp	x0, .LC95
	add	x0, x0, :lo12:.LC95
	str	x0, [x29,112]
.L1369:
	ldr	w0, [x29,152]
	cmp	w24, w0
	bcs	.L1452
	mov	w27, 56
	ldr	w0, [x29,152]
	ldr	w1, [x29,140]
	add	x2, x29, 172
	umull	x27, w24, w27
	sub	w4, w0, w24
	add	x3, x29, 168
	add	x21, x25, x27
	mov	x0, x21
	ldr	w28, [x21,4]
	bl	LogAddr2PhyAddr
	add	x2, x22, :lo12:.LANCHOR0
	mov	w19, w0
	ldr	w0, [x29,168]
	ldrb	w1, [x2,1845]
	cmp	w0, w1
	bcc	.L1371
	mov	w0, -1
	str	w0, [x25,x27]
	b	.L1372
.L1371:
	add	x0, x2, x0, uxtw
	adrp	x26, .LANCHOR4
	ldrb	w20, [x0,1848]
	add	x0, x26, :lo12:.LANCHOR4
	str	x2, [x29,144]
	ldrb	w0, [x0,112]
	cmp	w0, wzr
	mov	w0, w20
	csel	w19, w19, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x2, [x29,144]
	ldr	x0, [x2,744]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 5
	bhi	.L1374
	sxtw	x0, w20
	add	x1, x2, 760
	add	x3, x1, x0
	add	x0, x2, x0
	ldrb	w3, [x3,12]
	ldrb	w0, [x0,1616]
	cmp	w0, w3
	beq	.L1374
	ldrb	w1, [x2,761]
	mov	w0, w20
	add	x2, x2, 764
	bl	HynixSetRRPara
.L1374:
	mov	w0, w20
	bl	NandcFlashCs
	ldr	w0, [x29,140]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,144]
	cbz	w0, .L1381
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1381
	mov	w0, w20
	bl	flash_enter_slc_mode
.L1381:
	cmp	w20, 255
	ldr	w1, [x29,172]
	bne	.L1409
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1405
.L1409:
	cbz	w19, .L1378
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x0,12]
	mov	w0, w20
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1379
.L1378:
	mov	w0, w20
	bl	FlashReadCmd
	b	.L1379
.L1405:
	mov	w19, w0
.L1376:
	ldrb	w2, [x29,156]
	mov	w0, w20
	ldr	x3, [x21,8]
	mov	w1, 0
	ldr	x4, [x21,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1380
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0,16]
	cbz	w1, .L1380
	strb	wzr, [x0,16]
	mov	w19, 0
	b	.L1381
.L1380:
	cbz	w19, .L1382
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x29,172]
	ldr	w2, [x0,12]
	mov	w0, w20
	add	w1, w2, w1
	bl	FlashReadDpDataOutCmd
	add	w0, w24, 1
	mov	w4, 56
	ldrb	w2, [x29,156]
	mov	w1, 0
	umull	x4, w0, w4
	mov	w0, w20
	add	x4, x25, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w23, w0
	csel	w19, w19, wzr, ne
.L1382:
	mov	w0, w20
	bl	NandcFlashDeCs
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w0, [x29,136]
	cmn	w28, #1
	strb	w0, [x6,16]
	bne	.L1389
	ldrb	w0, [x6,1864]
	cbnz	w0, .L1384
.L1388:
	add	x0, x26, :lo12:.LANCHOR4
	ldr	x19, [x0,48]
	cbnz	x19, .L1385
	b	.L1453
.L1384:
	ldr	x0, [x6,128]
	mov	w4, 1
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	ldr	w19, [x0,304]
	mov	w0, w20
	str	x6, [x29,104]
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w28, w0
	beq	.L1387
	ldr	x6, [x29,104]
	ldrb	w0, [x6,1944]
	cmp	w28, w0, lsr 1
	bls	.L1407
.L1387:
	lsr	w0, w19, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1388
	b	.L1407
.L1385:
	ldr	w1, [x29,172]
	mov	w0, w20
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x19
	cmn	w0, #1
	mov	w28, w0
	mov	w19, 0
	bne	.L1390
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,744]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 5
	bhi	.L1391
	ldrb	w1, [x2,761]
	mov	w0, w20
	add	x2, x2, 764
	mov	w3, w19
	bl	HynixSetRRPara
.L1391:
	ldr	w1, [x29,172]
	mov	w0, w20
	ldr	x2, [x21,8]
	mov	w19, 0
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x29,112]
	ldr	w1, [x21,4]
	mov	w3, w28
	ldrb	w2, [x2,1944]
	bl	printk
	b	.L1390
.L1453:
	ldr	w1, [x29,172]
	mov	w0, w20
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1390
.L1407:
	mov	w19, 0
.L1389:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1944]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1390
	add	x0, x26, :lo12:.LANCHOR4
	ldr	x0, [x0,48]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1390:
	cmp	w28, 256
	beq	.L1410
	cmn	w28, #1
	bne	.L1392
.L1410:
	str	w28, [x25,x27]
	b	.L1394
.L1392:
	str	wzr, [x25,x27]
.L1394:
	ldr	w3, [x25,x27]
	cmn	w3, #1
	bne	.L1396
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x21,4]
	ldr	x0, [x29,128]
	ldrb	w2, [x2,1944]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L1396
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	bl	rknand_print_hex
.L1396:
	cbz	w19, .L1398
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,1944]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L1399
	add	x26, x26, :lo12:.LANCHOR4
	ldr	x0, [x26,48]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L1399:
	add	w0, w24, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L1411
	cmn	w23, #1
	bne	.L1400
.L1411:
	str	w23, [x25,x0]
	b	.L1398
.L1400:
	str	wzr, [x25,x0]
.L1398:
	ldr	w0, [x29,144]
	add	w24, w24, w19
	cbz	w0, .L1372
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1372
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1372:
	add	w24, w24, 1
	b	.L1369
.L1452:
	mov	w0, 0
	b	.L1450
.L1379:
	mov	w0, w20
	bl	NandcWaitFlashReady
	cbz	w19, .L1376
	ldr	w1, [x29,172]
	mov	w0, w20
	bl	FlashReadDpDataOutCmd
	b	.L1376
.L1450:
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
	adrp	x26, .LC96
	add	x26, x26, :lo12:.LC96
	mul	w21, w1, w21
	mov	w1, 0
	uxth	w21, w21
	bl	ftl_memset
	add	w25, w21, w27
	ldr	x0, [x19,88]
	str	x0, [x29,152]
	mov	w19, w28
	sub	w0, w21, #8
	str	xzr, [x29,144]
	uxth	w25, w25
	str	w0, [x29,124]
.L1455:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,1845]
	cmp	w1, w19
	bls	.L1465
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1456:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1458
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
	beq	.L1457
	add	x24, x23, :lo12:.LANCHOR4
	ldr	x0, [x24,88]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1457
	add	x24, x24, 120
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	bl	printk
	strh	w20, [x24,w19,sxtw 1]
	uxth	w28, w28
	b	.L1458
.L1457:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1456
.L1458:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,1845]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1455
.L1465:
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
.L1467:
	cmp	w25, w23
	beq	.L1496
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,1845]
	cmp	w0, w1
	bcc	.L1468
	str	w28, [x21]
	b	.L1469
.L1468:
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
	tbz	x0, 0, .L1472
	str	w28, [x21]
.L1472:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1469:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1467
.L1496:
	mov	w21, 0
	cbz	w27, .L1494
	adrp	x25, .LC100
	adrp	x26, .LC99
	adrp	x27, .LC98
	add	x25, x25, :lo12:.LC100
	add	x26, x26, :lo12:.LC99
	add	x27, x27, :lo12:.LC98
.L1495:
	cmp	w21, w23
	beq	.L1494
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1475
	adrp	x0, .LC97
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC97
	bl	printk
	b	.L1476
.L1475:
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
	bne	.L1477
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1477:
	ldr	x0, [x19,16]
	cbz	x0, .L1478
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,104]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1478
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1478:
	ldr	x0, [x19,8]
	cbz	x0, .L1476
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,96]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1476
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1476:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1495
.L1494:
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
	ldr	x1, [x0,-64]
	add	x21, x22, :lo12:.LANCHOR0
	ldr	x24, [x0,2112]
	add	x21, x21, 2112
	str	x1, [x19,8]
	mov	w23, 0
	str	x24, [x19,16]
	mov	w25, -1
	mov	w26, 61664
.L1498:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x0,1974]
	cmp	w23, w1
	bcs	.L1507
	ldrh	w20, [x0,2022]
	strh	w25, [x21,12]
.L1500:
	add	x0, x22, :lo12:.LANCHOR0
	sub	w20, w20, #1
	uxth	w20, w20
	ldrh	w0, [x0,2022]
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1501
	madd	w0, w23, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1500
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1500
	strh	w20, [x21,12]
.L1501:
	add	w23, w23, 1
	add	x21, x21, 2
	b	.L1498
.L1507:
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
	bne	.L1509
	ldrh	w19, [x3,2028]
	b	.L1510
.L1509:
	ldrh	w19, [x3,2026]
.L1510:
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
	bne	.L1511
	mov	w22, 0
	mov	w25, 2
.L1512:
	cmp	w22, w19
	bgt	.L1511
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
	bne	.L1513
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1513
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1513
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1512
.L1513:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1512
.L1511:
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
	str	x25, [sp,64]
	add	x19, x19, 136
	adrp	x20, .LANCHOR0
	str	xzr, [x19,8]
	ldr	x22, [x0,2112]
	mov	w25, 61649
	str	x22, [x19,16]
	bl	FtlBbtMemInit
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w21, [x0,2022]
	sub	w21, w21, #1
	uxth	w21, w21
.L1520:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2022]
	sub	w0, w0, #48
	cmp	w21, w0
	ble	.L1523
	lsl	w0, w21, 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1521
	ldr	w0, [x19,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
.L1521:
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1522
	ldrh	w0, [x22]
	cmp	w0, w25
	bne	.L1522
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2112
	strh	w21, [x1,2112]
	ldr	w1, [x22,4]
	str	w1, [x0,8]
	ldrh	w1, [x22,8]
	strh	w1, [x0,4]
	b	.L1523
.L1522:
	sub	w21, w21, #1
	uxth	w21, w21
	b	.L1520
.L1523:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,2112]
	cmp	w1, w2
	beq	.L1525
	ldrh	w1, [x21,2116]
	cmp	w1, w2
	beq	.L1527
	add	x25, x23, :lo12:.LANCHOR4
	lsl	w1, w1, 10
	add	x0, x25, 136
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x25,136]
	cmn	w0, #1
	beq	.L1527
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1527
	ldr	w0, [x22,4]
	ldr	w1, [x21,2120]
	cmp	w0, w1
	bls	.L1527
	str	w0, [x21,2120]
	ldrh	w1, [x21,2116]
	ldrh	w0, [x22,8]
	strh	w1, [x21,2112]
	strh	w0, [x21,2116]
.L1527:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 1
	add	x21, x23, :lo12:.LANCHOR4
	add	x21, x21, 136
	ldrh	w0, [x25,2112]
	bl	FtlGetLastWrittenPage
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x25,2114]
.L1529:
	tbnz	w19, #31, .L1534
	ldrh	w0, [x25,2112]
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
	beq	.L1530
.L1534:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,2118]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1531
	b	.L1532
.L1530:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1529
.L1531:
	ldr	w2, [x1,1948]
	cmp	w0, w2
	beq	.L1532
	ldrh	w1, [x1,1962]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1532
	cmp	w2, w1
	bcs	.L1532
	bl	FtlSysBlkNumInit
.L1532:
	add	x20, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x21, x20, 2112
	add	x23, x23, :lo12:.LANCHOR4
.L1535:
	ldrh	w0, [x20,1974]
	mov	w1, w19
	add	x19, x19, 1
	cmp	w1, w0
	bcs	.L1550
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x3, [x23,144]
	ldrh	w2, [x0,2232]
	add	x0, x21, x19, lsl 3
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1535
.L1550:
	mov	w0, 0
.L1525:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
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
	ldr	x2, [x20,2208]
	ldr	w22, [x2,x0]
	cbnz	w22, .L1552
	ldr	x0, [x20,144]
	lsl	x19, x1, 4
	adrp	x2, .LANCHOR0+2036
	mov	w1, 255
	add	x0, x0, x19
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2036]
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x20,144]
	strh	w21, [x0,x19]
	ldr	x1, [x20,144]
	add	x19, x1, x19
	str	w22, [x19,4]
	b	.L1553
.L1552:
	lsl	x19, x1, 4
	ldr	x1, [x20,144]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x1, x1, x19
	add	x0, x0, 136
	ldr	x1, [x1,8]
	str	x1, [x0,8]
	ldr	x1, [x20,2112]
	str	x1, [x0,16]
	mov	w1, 1
	mov	w2, w1
	str	w22, [x0,4]
	bl	FlashReadPages
	ldr	x0, [x20,144]
	add	x0, x0, x19
	str	wzr, [x0,4]
	ldr	x0, [x20,144]
	strh	w21, [x0,x19]
.L1553:
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
	ldrh	w28, [x0,2034]
	mov	x24, x2
	ldrh	w0, [x0,2020]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L1555
	lsr	w28, w22, w28
	mov	w20, 0
	adrp	x26, .LANCHOR2
.L1556:
	cbz	w21, .L1555
	add	x3, x26, :lo12:.LANCHOR2
	ldr	x0, [x3,2200]
	ldr	w1, [x0,w28,uxtw 2]
	add	x0, x25, :lo12:.LANCHOR0
	ldrh	w19, [x0,2032]
	uxth	w0, w21
	udiv	w23, w22, w19
	msub	w23, w23, w19, w22
	sub	w19, w19, w23
	uxth	w19, w19
	cmp	w19, w21
	csel	w19, w0, w19, hi
	lsl	w27, w19, 9
	cbz	w1, .L1558
	ldr	x0, [x3,2080]
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
	ldr	x1, [x3,2080]
	cmn	w0, #1
	mov	x0, x24
	csinv	w20, w20, wzr, ne
	add	x1, x1, x23
	bl	ftl_memcpy
	b	.L1560
.L1558:
	mov	x0, x24
	mov	w2, w27
	bl	ftl_memset
.L1560:
	add	w28, w28, 1
	sub	w21, w21, w19
	add	w22, w22, w19
	add	x24, x24, x27, sxtw
	b	.L1556
.L1555:
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
	ldrh	w1, [x19,2136]
	ldr	x2, [x19,2152]
	bl	FtlVendorPartRead
	ldr	x0, [x19,2152]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1564
	adrp	x0, .LC72
	adrp	x1, .LC101
	add	x1, x1, :lo12:.LC101
	add	x0, x0, :lo12:.LC72
	bl	printk
	ldrh	w2, [x19,2136]
	mov	w1, 0
	ldr	x0, [x19,2152]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1564:
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
	beq	.L1566
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,352]
.L1566:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,352]
	cmp	w3, w2
	bne	.L1567
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
.L1567:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 34661
	movk	w1, 0x1234, lsl 16
	ldr	w2, [x0,420]
	str	wzr, [x0,2024]
	cmp	w2, w1
	bne	.L1568
	mov	w1, 1
	str	w1, [x0,160]
	adrp	x0, .LC72
	adrp	x1, .LC102
	add	x0, x0, :lo12:.LC72
	add	x1, x1, :lo12:.LC102
	bl	printk
.L1568:
	adrp	x2, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR2
	add	x2, x2, :lo12:.LANCHOR0
	ldr	x21, [sp,32]
	ldr	w3, [x19,204]
	ldrh	w1, [x2,2016]
	ldr	w0, [x19,208]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,1960]
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
	strh	wzr, [x21,2304]
	mov	x24, x22
	ldr	w2, [x20,2056]
	mov	w22, 65535
	ldr	x0, [x21,2216]
	lsl	w2, w2, 2
	strh	wzr, [x20,2072]
	bl	ftl_memset
	ldr	w2, [x20,2056]
	mov	w1, 0
	ldr	x0, [x21,2168]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2048]
	mov	w1, 0
	ldr	x0, [x21,2192]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2048]
	mov	w1, 0
	ldr	x0, [x20,2080]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x21, 280
	mov	w1, 255
	mov	w2, 12
	bl	ftl_memset
	ldrh	w21, [x20,1960]
.L1571:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,1962]
	cmp	w0, w21
	bls	.L1614
	mov	x27, 0
	ldrh	w5, [x1,1952]
	ldrh	w4, [x1,2038]
	mov	w20, w27
	mov	x23, x1
.L1615:
	cmp	w5, w27, uxth
	bls	.L1646
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
	cbnz	w0, .L1572
	add	x2, x24, :lo12:.LANCHOR2
	mov	w0, 56
	lsl	w28, w28, 10
	mov	w3, 4
	umull	x0, w20, w0
	ldr	x1, [x2,2048]
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x2,2048]
	ldr	x2, [x2,904]
	add	x0, x1, x0
	mul	w1, w20, w4
	add	w20, w20, 1
	str	xzr, [x0,8]
	sdiv	w1, w1, w3
	uxth	w20, w20
	add	x1, x2, x1, sxtw 2
	str	x1, [x0,16]
.L1572:
	add	x27, x27, 1
	b	.L1615
.L1646:
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, 61634
	mov	w28, 61574
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L1574:
	cmp	w20, w23, uxth
	bls	.L1647
	add	x7, x24, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x6, x23, x0
	ldr	x2, [x7,2048]
	add	x1, x2, x6
	ldr	w8, [x2,x6]
	ldr	w0, [x1,4]
	cmn	w8, #1
	ldr	x26, [x1,16]
	ubfx	x25, x0, 10, 16
	bne	.L1576
	add	w0, w0, 1
	str	w0, [x1,4]
	mov	w1, 1
	str	x8, [x29,104]
	ldr	x0, [x7,2048]
	mov	w2, w1
	str	x7, [x29,112]
	add	x0, x0, x6
	str	x6, [x29,120]
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	x6, [x29,120]
	cmp	w0, w22
	ldr	x7, [x29,112]
	ldr	x8, [x29,104]
	bne	.L1576
	ldr	x0, [x7,2048]
	str	w8, [x0,x6]
.L1576:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
	ldr	w1, [x1,x6]
	cmn	w1, #1
	beq	.L1578
	ldr	w0, [x0,196]
	cmn	w0, #1
	beq	.L1579
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1580
.L1579:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1580
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,196]
.L1580:
	ldrh	w0, [x26]
	mov	w1, 61604
	cmp	w0, w1
	beq	.L1582
	bhi	.L1583
	cmp	w0, w28
	bne	.L1581
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldrh	w6, [x0,2048]
	ldrh	w0, [x0,2072]
	sub	w1, w6, #1
	ldr	x7, [x7,2192]
	sxth	w2, w1
	sub	w1, w1, w0
	b	.L1599
.L1583:
	cmp	w0, w27
	beq	.L1585
	cmp	w0, w22
	bne	.L1581
	mov	w0, w25
	mov	w1, 0
	b	.L1644
.L1585:
	add	x0, x19, :lo12:.LANCHOR0
	add	x7, x24, :lo12:.LANCHOR2
	ldr	w6, [x0,2056]
	ldrh	w0, [x7,2304]
	ldr	x7, [x7,2216]
	uxth	w1, w6
	sub	w2, w1, #1
	sub	w1, w1, w0
	sub	w1, w1, #1
	sxth	w2, w2
	sxth	w1, w1
.L1587:
	cmp	w2, w1
	ble	.L1648
	sbfiz	x8, x2, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w2
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bls	.L1588
	ldr	w1, [x7]
	cbnz	w1, .L1589
	cmp	w0, w6
	bne	.L1590
.L1589:
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, 1
	str	x9, [x29,104]
	str	x2, [x29,112]
	ldr	x0, [x0,2168]
	str	x8, [x29,120]
	ldrh	w0, [x0]
	bl	FtlFreeSysBlkQueueIn
	ldr	x8, [x29,120]
	ldr	x2, [x29,112]
	ldr	x9, [x29,104]
	b	.L1591
.L1590:
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x1,2304]
.L1591:
	mov	w0, 0
.L1592:
	cmp	w0, w2
	beq	.L1649
	add	x6, x24, :lo12:.LANCHOR2
	sxtw	x1, w0
	lsl	x7, x1, 2
	lsl	x1, x1, 1
	add	w0, w0, 1
	ldr	x10, [x6,2216]
	sxth	w0, w0
	add	x11, x10, x7
	ldr	w11, [x11,4]
	str	w11, [x10,x7]
	ldr	x6, [x6,2168]
	add	x7, x6, x1
	ldrh	w7, [x7,2]
	strh	w7, [x6,x1]
	b	.L1592
.L1649:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x1, [x0,2216]
	str	w6, [x1,x8]
	ldr	x1, [x0,2168]
	strh	w25, [x1,x9,lsl 1]
	tbz	w2, #31, .L1643
	b	.L1578
.L1588:
	sub	w2, w2, #1
	sxth	w2, w2
	b	.L1587
.L1648:
	tbz	w2, #31, .L1598
	b	.L1578
.L1643:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2304]
	ldr	w1, [x1,2056]
	sub	w1, w1, w0
	sub	w1, w1, #1
	cmp	w2, w1, sxth
	bgt	.L1581
.L1598:
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	sxtw	x2, w2
	ldr	w4, [x26,4]
	strh	w0, [x1,2304]
	ldr	x0, [x1,2216]
	str	w4, [x0,x2,lsl 2]
	ldr	x0, [x1,2168]
	b	.L1645
.L1607:
	sbfiz	x8, x2, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w2
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bhi	.L1650
	sub	w2, w2, #1
	sxth	w2, w2
.L1599:
	cmp	w2, w1
	bgt	.L1607
	b	.L1606
.L1650:
	ldr	w1, [x7]
	cbnz	w1, .L1601
	cmp	w0, w6
	bne	.L1602
.L1601:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	str	x9, [x29,104]
	str	x8, [x29,112]
	ldr	x0, [x0,2080]
	str	x2, [x29,120]
	ldrh	w0, [x0]
	bl	FtlFreeSysBlkQueueIn
	ldr	x2, [x29,120]
	ldr	x8, [x29,112]
	ldr	x9, [x29,104]
	b	.L1603
.L1602:
	add	x1, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	strh	w0, [x1,2072]
.L1603:
	mov	w0, 0
.L1604:
	cmp	w0, w2
	beq	.L1651
	add	x1, x24, :lo12:.LANCHOR2
	ldr	x7, [x1,2192]
	sxtw	x1, w0
	lsl	x6, x1, 2
	lsl	x1, x1, 1
	add	x10, x7, x6
	add	w0, w0, 1
	sxth	w0, w0
	ldr	w10, [x10,4]
	str	w10, [x7,x6]
	add	x6, x19, :lo12:.LANCHOR0
	ldr	x6, [x6,2080]
	add	x7, x6, x1
	ldrh	w7, [x7,2]
	strh	w7, [x6,x1]
	b	.L1604
.L1651:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w1, [x26,4]
	ldr	x0, [x0,2192]
	str	w1, [x0,x8]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2080]
	strh	w25, [x0,x9,lsl 1]
.L1606:
	tbnz	w2, #31, .L1578
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,2048]
	ldrh	w6, [x1,2072]
	sub	w0, w0, #1
	sub	w0, w0, w6
	cmp	w2, w0, sxth
	bgt	.L1581
	add	x0, x24, :lo12:.LANCHOR2
	sxtw	x2, w2
	add	w6, w6, 1
	ldr	w4, [x26,4]
	strh	w6, [x1,2072]
	ldr	x0, [x0,2192]
	str	w4, [x0,x2,lsl 2]
	ldr	x0, [x1,2080]
.L1645:
	strh	w25, [x0,x2,lsl 1]
	b	.L1581
.L1582:
	add	x0, x24, :lo12:.LANCHOR2
	add	x1, x0, 280
	ldrh	w2, [x0,280]
	cmp	w2, w22
	bne	.L1610
	strh	w25, [x0,280]
	ldr	w0, [x26,4]
	str	w0, [x1,8]
	b	.L1581
.L1610:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1611
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1611:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	w2, [x26,4]
	add	x0, x1, 280
	ldr	w6, [x0,8]
	cmp	w6, w2
	bcs	.L1612
	ldrh	w2, [x1,280]
	strh	w2, [x0,4]
	strh	w25, [x1,280]
	ldr	w1, [x26,4]
	str	w1, [x0,8]
	b	.L1581
.L1612:
	strh	w25, [x0,4]
	b	.L1581
.L1578:
	mov	w0, w25
	mov	w1, 1
.L1644:
	bl	FtlFreeSysBlkQueueIn
.L1581:
	add	x23, x23, 1
	b	.L1574
.L1647:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1571
.L1614:
	add	x4, x24, :lo12:.LANCHOR2
	ldr	x2, [x4,2168]
	ldrh	w0, [x2]
	cbz	w0, .L1616
.L1619:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,2080]
	ldrh	w0, [x2]
	cbz	w0, .L1617
	b	.L1638
.L1616:
	ldrh	w4, [x4,2304]
	cbz	w4, .L1619
	ldr	w1, [x1,2056]
.L1620:
	cmp	w0, w1
	bcs	.L1619
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1621
	mov	w7, w0
.L1622:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w1, [x1,2056]
	cmp	w0, w1
	bcs	.L1619
	add	x2, x24, :lo12:.LANCHOR2
	sxtw	x6, w0
	sub	w1, w0, w7
	lsl	x4, x6, 1
	add	w0, w0, 1
	ldr	x5, [x2,2168]
	sxtw	x1, w1
	sxth	w0, w0
	ldrh	w8, [x5,x4]
	strh	w8, [x5,x1,lsl 1]
	ldr	x5, [x2,2216]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,2168]
	strh	wzr, [x1,x4]
	b	.L1622
.L1621:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1620
.L1617:
	ldrh	w4, [x1,2072]
	cbz	w4, .L1638
	ldrh	w1, [x1,2048]
.L1625:
	cmp	w0, w1
	mov	w7, w0
	bge	.L1638
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1626
.L1627:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2,2048]
	cmp	w0, w1
	bge	.L1638
	sxtw	x6, w0
	sub	w1, w0, w7
	ldr	x5, [x2,2080]
	lsl	x4, x6, 1
	sxtw	x1, w1
	add	w0, w0, 1
	ldrh	w8, [x5,x4]
	sxth	w0, w0
	strh	w8, [x5,x1,lsl 1]
	add	x5, x24, :lo12:.LANCHOR2
	ldr	x5, [x5,2192]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x1,lsl 2]
	ldr	x1, [x2,2080]
	strh	wzr, [x1,x4]
	b	.L1627
.L1626:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1625
.L1638:
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
	ldr	x0, [x20,2112]
	add	x25, x20, 280
	str	x0, [x19,16]
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w2, [x0,1960]
	ldr	x0, [x20,-40]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x20,280]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1653
.L1657:
	mov	w19, -1
	b	.L1654
.L1653:
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w24, w0
	add	w0, w0, 1
	strh	w0, [x25,2]
.L1655:
	tbnz	w24, #31, .L1660
	ldrh	w0, [x20,280]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w24, w0, lsl 10
	str	w0, [x19,4]
	ldr	x0, [x20,-64]
	str	x0, [x19,8]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1656
.L1660:
	add	x24, x22, :lo12:.LANCHOR4
	add	x19, x21, :lo12:.LANCHOR2
	add	x20, x19, 224
	mov	w2, 48
	add	x22, x23, :lo12:.LANCHOR0
	mov	x0, x20
	ldr	x1, [x24,144]
	bl	ftl_memcpy
	ldr	x1, [x24,144]
	ldrh	w2, [x22,1960]
	ldr	x0, [x19,-40]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x22,1960]
	ldr	x3, [x24,144]
	ldr	x0, [x23,#:lo12:.LANCHOR0]
	uxth	x1, w2
	lsr	w2, w2, 3
	add	x1, x1, 24
	add	w2, w2, 4
	lsr	x1, x1, 1
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	ldr	w1, [x19,224]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L1667
	b	.L1657
.L1656:
	sub	w24, w24, #1
	sxth	w24, w24
	b	.L1655
.L1667:
	ldrb	w1, [x20,10]
	ldrh	w0, [x22,1974]
	ldrh	w2, [x20,8]
	strh	w2, [x19,286]
	cmp	w1, w0
	bne	.L1657
	ldrh	w1, [x22,2026]
	ldrh	w0, [x22,2032]
	str	w2, [x24,192]
	mul	w1, w1, w2
	str	w1, [x22,2104]
	strh	wzr, [x19,298]
	mul	w1, w1, w0
	str	w1, [x22,2068]
	ldrh	w0, [x22,2118]
	ldr	w1, [x22,1964]
	strb	wzr, [x19,302]
	sub	w0, w1, w0
	ldrh	w1, [x22,1952]
	sub	w0, w0, w2
	ldrh	w2, [x20,14]
	strb	wzr, [x19,304]
	udiv	w0, w0, w1
	strh	w0, [x19,272]
	adrp	x0, .LANCHOR2
	strh	w2, [x0,#:lo12:.LANCHOR2]
	ldrh	w0, [x20,16]
	lsr	w1, w0, 6
	strh	w1, [x19,2]
	mov	w1, -1
	strh	w1, [x19,296]
	ldrh	w1, [x20,18]
	and	w0, w0, 63
	strh	w1, [x19,48]
	ldrh	w1, [x20,20]
	strb	w0, [x19,6]
	lsr	w3, w1, 6
	and	w1, w1, 63
	strb	w1, [x19,54]
	ldrb	w1, [x20,12]
	strb	w1, [x19,56]
	ldrh	w1, [x20,22]
	strh	w1, [x19,96]
	ldrh	w1, [x20,24]
	ldrb	w0, [x20,11]
	strh	w3, [x19,50]
	lsr	w3, w1, 6
	and	w1, w1, 63
	strb	w0, [x19,8]
	strb	w1, [x19,102]
	strh	w3, [x19,98]
	ldrb	w1, [x20,13]
	strb	w1, [x19,104]
	ldr	w0, [x20,32]
	str	w0, [x19,204]
	ldr	w1, [x19,196]
	ldr	w0, [x20,40]
	str	wzr, [x19,184]
	str	wzr, [x19,172]
	cmp	w0, w1
	str	wzr, [x19,164]
	str	wzr, [x19,180]
	str	wzr, [x19,208]
	str	wzr, [x19,216]
	str	wzr, [x19,176]
	bls	.L1661
	str	w0, [x19,196]
.L1661:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,260]
	ldr	w3, [x0,200]
	cmp	w1, w3
	bls	.L1662
	str	w1, [x0,200]
.L1662:
	mov	w0, 65535
	cmp	w2, w0
	beq	.L1663
	add	x0, x21, :lo12:.LANCHOR2
	bl	make_superblock
.L1663:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 48
	ldrh	w2, [x1,48]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1664
	bl	make_superblock
.L1664:
	add	x1, x21, :lo12:.LANCHOR2
	add	x0, x1, 96
	ldrh	w2, [x1,96]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1665
	bl	make_superblock
.L1665:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x21, 296
	mov	w19, 0
	ldrh	w2, [x21,296]
	cmp	w2, w1
	beq	.L1654
	bl	make_superblock
.L1654:
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
	adrp	x19, .LANCHOR1
	stp	x23, x24, [sp,48]
	mov	x24, x0
	add	x0, x19, :lo12:.LANCHOR1
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	str	x19, [x29,136]
	mov	w25, w1
	ldrh	w28, [x0,3004]
	mov	w0, 65535
	cmp	w28, w0
	beq	.L1688
	cbnz	w28, .L1669
	b	.L1670
.L1688:
	mov	w28, 0
.L1669:
	adrp	x0, .LANCHOR0+2026
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2026]
	cmp	w25, w0
	bne	.L1671
.L1670:
	bl	FtlGcPageVarInit
.L1671:
	mov	w23, -1
	mov	w20, 0
	mov	w21, 65535
	adrp	x26, .LANCHOR0
	adrp	x27, .LANCHOR2
.L1682:
	ldrh	w0, [x24]
	strb	wzr, [x24,8]
	cmp	w0, w21
	beq	.L1689
.L1673:
.L1685:
	add	x0, x26, :lo12:.LANCHOR0
	mov	x2, x24
	mov	w19, 0
	mov	w10, 4
	ldrh	w6, [x0,1952]
	ldrh	w9, [x0,2038]
	add	x6, x24, x6, lsl 1
.L1674:
	cmp	x2, x6
	beq	.L1699
	ldrh	w1, [x2,16]
	cmp	w1, w21
	beq	.L1675
	add	x7, x27, :lo12:.LANCHOR2
	mov	w0, 56
	orr	w1, w28, w1, lsl 10
	add	w5, w19, 1
	umull	x0, w19, w0
	ldr	x8, [x7,2048]
	add	x8, x8, x0
	str	w1, [x8,4]
	ldr	x1, [x7,2048]
	ldr	x7, [x7,904]
	add	x0, x1, x0
	mul	w1, w19, w9
	uxth	w19, w5
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	add	x1, x7, x1, sxtw 2
	str	x1, [x0,16]
.L1675:
	add	x2, x2, 2
	b	.L1674
.L1699:
	add	x0, x27, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x22, 0
	ldr	x0, [x0,2048]
	bl	FlashReadPages
.L1677:
	cmp	w19, w22, uxth
	bls	.L1700
	adrp	x0, .LANCHOR2
	add	x7, x0, :lo12:.LANCHOR2
	mov	x0, 56
	str	x7, [x29,96]
	mul	x8, x22, x0
	ldr	x10, [x7,2048]
	str	x8, [x29,104]
	add	x9, x10, x8
	str	x10, [x29,112]
	str	x9, [x29,120]
	ldr	w1, [x9,4]
	str	x1, [x29,128]
	lsr	x0, x1, 10
	bl	P2V_plane
	uxth	w2, w0
	ldr	x10, [x29,112]
	ldr	x8, [x29,104]
	ldr	x1, [x29,128]
	ldr	x7, [x29,96]
	ldr	w0, [x10,x8]
	cbnz	w0, .L1678
	ldr	x9, [x29,120]
	add	x22, x22, 1
	ldr	x8, [x9,16]
	ldrh	w0, [x8]
	cmp	w0, w21
	bne	.L1679
	mov	w0, 1
	str	w0, [x7,2328]
	b	.L1672
.L1679:
	ldr	w0, [x8,12]
	ldr	w2, [x8,8]
	bl	FtlGcUpdatePage
	b	.L1677
.L1678:
	ldr	w2, [x7,160]
	cbz	w2, .L1680
	ldrh	w3, [x24]
	ldr	x2, [x7,-80]
	ldrh	w2, [x2,x3,lsl 1]
	cmp	w2, 119
	bls	.L1681
.L1680:
	cmn	w0, #1
	csel	w1, w1, w23, eq
.L1681:
	adrp	x0, .LANCHOR2
	ldrh	w2, [x24]
	add	x22, x0, :lo12:.LANCHOR2
	str	x1, [x29,128]
	mov	w28, 0
	ldr	x0, [x22,-40]
	strh	wzr, [x0,x2,lsl 1]
	ldrh	w0, [x24]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x24]
	bl	FtlGcPageVarInit
	ldr	x1, [x29,128]
	mov	w23, w1
	b	.L1682
.L1700:
	add	w20, w20, 1
	add	w3, w28, 1
	cmp	w20, w25
	uxth	w28, w3
	bcs	.L1684
.L1686:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2026]
	cmp	w0, w28
	bhi	.L1685
	b	.L1689
.L1684:
	ldr	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3004]
	cmp	w0, w21
	beq	.L1686
	add	w0, w0, w20
	strh	w0, [x1,3004]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2026]
	cmp	w0, w28
	bls	.L1686
	b	.L1687
.L1689:
	mov	w2, 0
.L1672:
	ldr	x0, [x29,136]
	mov	w1, w28
	strh	w28, [x24,2]
	add	x19, x0, :lo12:.LANCHOR1
	mov	w0, -1
	strb	w2, [x24,6]
	strh	w0, [x19,3004]
	mov	x0, x24
	bl	ftl_sb_update_avl_pages
.L1687:
	mov	w0, w23
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
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	ldrb	w6, [x6,#:lo12:.LANCHOR1+481]
	add	x4, x20, :lo12:.LANCHOR0
	str	w3, [x29,120]
	stp	x21, x22, [sp,32]
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
	cbz	w4, .L1702
	bl	FlashProgSlc2KPages
	b	.L1703
.L1702:
	sub	w0, w5, #1
	mov	w27, 56
	mov	x28, 24
	str	w0, [x29,116]
.L1753:
	cmp	w22, w25
	bcs	.L1754
	umull	x21, w22, w27
	sub	w4, w25, w22
	add	x2, x29, 128
	add	x26, x19, x21
	mov	w1, w23
	mov	x0, x26
	add	x3, x29, 132
	bl	LogAddr2PhyAddr
	mov	w24, w0
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w0, [x29,132]
	ldrb	w4, [x2,1845]
	cmp	w0, w4
	bcc	.L1704
	mov	w0, -1
	str	w0, [x19,x21]
	b	.L1705
.L1704:
	ldrb	w1, [x2,1945]
	add	x3, x2, 1652
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x0, x1, x28, x3
	ldr	x0, [x0,8]
	cbz	x0, .L1707
	cmp	w4, 1
	bne	.L1708
	ldr	x0, [x2,128]
	bl	NandcIqrWaitFlashReady
.L1708:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1707:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	add	x0, x0, 1652
	ldr	w1, [x29,128]
	madd	x0, x2, x28, x0
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1709
	add	w1, w22, 1
	umull	x1, w1, w27
	add	x1, x19, x1
	str	x1, [x0,16]
.L1709:
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, x2
	madd	x2, x2, x28, x1
	ldrb	w21, [x0,1848]
	ldrb	w0, [x1,1845]
	strb	w21, [x2,1652]
	cmp	w0, 1
	bne	.L1710
	mov	w0, w21
	bl	NandcWaitFlashReady
	b	.L1711
.L1710:
	mov	w0, w21
	str	x1, [x29,104]
	bl	NandcFlashCs
	ldr	x1, [x29,104]
	ldr	w0, [x29,132]
	add	x1, x1, 708
	ldr	w0, [x1,x0,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w21
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w21
	bl	NandcFlashDeCs
.L1711:
	ldr	w0, [x29,116]
	cmp	w0, 5
	bhi	.L1712
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x2, x21, sxtw
	ldrb	w0, [x0,1616]
	cbz	w0, .L1712
	ldrb	w1, [x2,761]
	mov	w0, w21
	add	x2, x2, 764
	mov	w3, 0
	bl	HynixSetRRPara
.L1712:
	mov	w0, w21
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1713
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L1713
	mov	w0, w21
	bl	flash_enter_slc_mode
.L1713:
	ldr	w1, [x29,128]
	mov	w0, w21
	bl	FlashProgFirstCmd
	ldrb	w2, [x29,124]
	mov	w0, w21
	ldr	x3, [x26,8]
	mov	w1, 1
	ldr	x4, [x26,16]
	bl	NandcXferData
	cbz	w24, .L1714
	ldr	w1, [x29,128]
	mov	w0, w21
	add	x26, x20, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29,132]
	add	x0, x26, 708
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w21
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w2, [x26,12]
	mov	w0, w21
	ldr	w1, [x29,128]
	add	w1, w2, w1
	bl	FlashProgDpSecondCmd
	add	w4, w22, 1
	ldrb	w2, [x29,124]
	mov	w0, w21
	mov	w1, 1
	umull	x4, w4, w27
	add	x4, x19, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
.L1714:
	ldr	w1, [x29,128]
	mov	w0, w21
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w21
	bl	NandcFlashDeCs
.L1705:
	add	w22, w22, 1
	b	.L1753
.L1754:
	add	x20, x20, :lo12:.LANCHOR0
	mov	x21, 0
	add	x22, x20, 1652
	mov	x24, 24
	ldr	x0, [x20,128]
	bl	NandcIqrWaitFlashReady
.L1716:
	ldrb	w0, [x20,1845]
	cmp	w0, w21
	bls	.L1755
	mov	w0, w21
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1717
	ldrb	w0, [x20,120]
	cbz	w0, .L1717
	mul	x0, x21, x24
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L1717:
	add	x21, x21, 1
	b	.L1716
.L1755:
	ldr	w0, [x29,120]
	cbnz	w0, .L1719
.L1727:
	mov	w0, 0
	b	.L1703
.L1719:
	adrp	x24, .LC100
	adrp	x26, .LC99
	adrp	x27, .LC98
	mov	w21, 0
	add	x24, x24, :lo12:.LC100
	add	x26, x26, :lo12:.LC99
	add	x27, x27, :lo12:.LC98
.L1720:
	cmp	w21, w25
	beq	.L1727
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1721
	adrp	x0, .LC97
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC97
	bl	printk
	b	.L1722
.L1721:
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
	bne	.L1723
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1723:
	ldr	x0, [x19,16]
	cbz	x0, .L1724
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR4
	ldr	x0, [x0,104]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1724
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1724:
	ldr	x0, [x19,8]
	cbz	x0, .L1722
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x0]
	ldr	x0, [x20,96]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1722
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1722:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1720
.L1703:
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
	cmn	w0, #1
	add	x0, x29, 40
	csetm	w19, eq
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
	adrp	x0, .LANCHOR4+68
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x0,#:lo12:.LANCHOR4+68]
	mov	w0, 0
	cmp	w1, w2
	bcc	.L1758
	mov	w0, w1
	bl	FlashTestBlk.part.17
.L1758:
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
	adrp	x22, .LANCHOR4
	add	x0, x22, :lo12:.LANCHOR4
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	mov	x20, x4
	mov	w19, 0
	ldr	x0, [x0,88]
	add	x25, x20, :lo12:.LANCHOR0
	str	x0, [x29,160]
	add	x0, x4, :lo12:.LANCHOR0
	add	x28, x25, 708
	ldrh	w1, [x0,180]
	ldrh	w21, [x0,182]
	mul	w21, w1, w21
	ldr	x1, [x0,744]
	uxth	w21, w21
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
	adrp	x0, .LC103
	mov	w1, 1
	add	x0, x0, :lo12:.LC103
	bl	printk
	add	x0, x22, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w2, 4096
	ldr	x0, [x0,88]
	bl	ftl_memset
	lsr	w0, w21, 4
	str	w0, [x29,168]
	ldr	w0, [x29,128]
	and	w0, w0, 1
	str	w0, [x29,116]
	sub	w0, w21, #1
	uxth	w0, w0
	str	w0, [x29,124]
.L1762:
	ldrb	w0, [x25,1845]
	cmp	w0, w19
	bls	.L1813
	add	x0, x22, :lo12:.LANCHOR4
	sxtw	x24, w19
	add	x0, x0, 120
	ldrh	w20, [x0,w19,sxtw 1]
	cbnz	w20, .L1792
	ldrh	w2, [x25,188]
	mov	w1, w20
	ldr	x0, [x25,1856]
	mov	w26, w20
	lsl	w2, w2, 9
	mov	w23, w20
	bl	ftl_memset
	add	x0, x25, x24
	ldrb	w27, [x0,1848]
	ldr	w0, [x29,128]
	and	w0, w0, 2
	uxtb	w0, w0
	str	w0, [x29,120]
.L1764:
	uxth	w0, w26
	str	w0, [x29,144]
	cmp	w0, w21
	bcs	.L1774
	mov	w0, -1
	strb	w0, [x29,182]
	strb	w0, [x29,183]
	ldr	w0, [x29,116]
	cbz	w0, .L1766
	ldr	w3, [x28,x24,lsl 2]
	mov	w0, w27
	add	x2, x29, 182
	add	w3, w20, w3
	str	x3, [x29,104]
	mov	w1, w3
	bl	FlashReadSpare
	ldrb	w0, [x25,8]
	ldr	x3, [x29,104]
	cmp	w0, 1
	bne	.L1766
	ldr	w1, [x25,12]
	mov	w0, w27
	add	x2, x29, 183
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,182]
	ldrb	w1, [x29,183]
	and	w0, w1, w0
	strb	w0, [x29,182]
.L1766:
	ldr	w0, [x29,120]
	cbz	w0, .L1768
	ldr	x0, [x25,744]
	add	x2, x29, 183
	ldrh	w1, [x0,10]
	ldr	w0, [x28,x24,lsl 2]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w27
	add	w1, w1, w20
	bl	FlashReadSpare
.L1768:
	ldr	x0, [x25,744]
	ldrb	w1, [x29,182]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1793
	cmp	w0, 1
	bne	.L1769
.L1793:
	mov	w0, 1
	cbz	w1, .L1771
	ldrb	w0, [x29,183]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1771
.L1769:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1771
	ldrb	w0, [x29,183]
	cmp	w0, 255
	cset	w0, ne
.L1771:
	ldr	x1, [x29,128]
	tbz	x1, 2, .L1772
	ldr	w1, [x28,x24,lsl 2]
	mov	w0, w27
	add	w1, w20, w1
	bl	SandiskProgTestBadBlock
.L1772:
	cbz	w0, .L1773
	adrp	x0, .LC104
	mov	w1, w19
	mov	w2, w26
	add	x0, x0, :lo12:.LC104
	add	w23, w23, 1
	bl	printk
	ldr	x0, [x29,144]
	uxth	w23, w23
	ldr	x2, [x25,1856]
	ubfx	x1, x0, 5, 11
	ldrb	w3, [x29,144]
	lsl	x1, x1, 2
	mov	w0, 1
	lsl	w0, w0, w3
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
	ldr	w1, [x29,168]
	ldrb	w0, [x25,1845]
	mul	w0, w1, w0
	cmp	w23, w0
	bgt	.L1774
.L1773:
	ldr	w0, [x29,172]
	add	w26, w26, 1
	add	w20, w20, w0
	b	.L1764
.L1774:
	adrp	x0, .LC105
	mov	w1, w19
	add	x0, x0, :lo12:.LC105
	mov	w2, w23
	bl	printk
	ldrb	w0, [x25,1845]
	ldr	w1, [x29,168]
	mul	w0, w1, w0
	cmp	w23, w0
	blt	.L1776
	ldrh	w2, [x25,188]
	mov	w1, 0
	ldr	x0, [x25,1856]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1776:
	cbnz	w19, .L1778
	add	x0, x22, :lo12:.LANCHOR4
	adrp	x26, .LC106
	mov	w20, w19
	add	x26, x26, :lo12:.LC106
	mov	w23, 1
	ldrh	w27, [x0,68]
.L1779:
	ldrb	w0, [x25,9]
	cmp	w0, w27
	bls	.L1814
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1780
	mov	w1, w27
	mov	x0, x26
	add	w20, w20, 1
	bl	printk
	ubfx	x1, x27, 5, 11
	ldr	x2, [x25,1856]
	lsl	x1, x1, 2
	lsl	w0, w23, w27
	uxth	w20, w20
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1780:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1779
.L1814:
	adrp	x26, .LC106
	ldr	w23, [x29,124]
	sub	w27, w21, #50
	add	x26, x26, :lo12:.LC106
.L1782:
	cmp	w23, w27
	ble	.L1815
	mov	w0, w23
	bl	FlashTestBlk
	cbz	w0, .L1783
	mov	w1, w23
	mov	x0, x26
	bl	printk
	ubfx	x1, x23, 5, 11
	ldr	x3, [x25,1856]
	lsl	x1, x1, 2
	mov	w0, 1
	lsl	w0, w0, w23
	ldr	w5, [x3,x1]
	orr	w0, w5, w0
	str	w0, [x3,x1]
.L1783:
	sub	w2, w23, #1
	uxth	w23, w2
	b	.L1782
.L1815:
	add	x1, x22, :lo12:.LANCHOR4
	ldrb	w0, [x25,9]
	ldr	w1, [x1,68]
	sub	w0, w0, w1
	cmp	w20, w0
	bcc	.L1778
	ldrh	w2, [x25,188]
	mov	w1, 0
	ldr	x0, [x25,1856]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1778:
	adrp	x23, .LC107
	ldr	w27, [x29,124]
	mul	w26, w19, w21
	add	x23, x23, :lo12:.LC107
	add	x20, x22, :lo12:.LANCHOR4
.L1786:
	mov	w2, w27
	mov	x0, x23
	mov	w1, w19
	bl	printk
	ldr	x2, [x25,1856]
.L1787:
	ubfx	x0, x27, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w27
	and	w1, w0, 1
	tbz	x0, 0, .L1816
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1787
.L1816:
	add	x0, x20, 120
	mov	w2, -3872
	strh	w27, [x0,x24,lsl 1]
	ldr	x0, [x29,160]
	strh	w2, [x0]
	strh	w27, [x0,2]
	strh	w1, [x0,8]
	mov	w1, 1
	ldr	x0, [x25,1856]
	mov	w2, w1
	str	x0, [x29,192]
	ldr	x0, [x20,88]
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
	cbz	w0, .L1792
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1786
.L1792:
	add	w19, w19, 1
	uxtb	w19, w19
	b	.L1762
.L1813:
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
	uxth	w25, w0
	mov	w28, 0
	ldrb	w0, [x20,120]
	uxtb	w26, w1
	str	w0, [x29,124]
	mov	w21, w28
	str	w25, [x20,2108]
	mov	w19, w28
	adrp	x24, .LANCHOR2
	mov	w27, 56
.L1818:
	ldrh	w0, [x20,1952]
	cmp	w0, w28
	bls	.L1851
	add	x1, x24, :lo12:.LANCHOR2
	umull	x0, w28, w27
	ldr	x1, [x1,-88]
	str	wzr, [x1,x0]
	add	x0, x20, 1984
	mov	w1, w25
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	uxth	w23, w0
	mov	w1, w23
	cbz	w26, .L1819
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L1820
.L1819:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1821
	adrp	x1, .LANCHOR2
	lsl	w23, w23, 10
	add	x3, x1, :lo12:.LANCHOR2
	mov	w2, 4
	umull	x0, w21, w27
	ldr	x1, [x3,-88]
	add	x1, x1, x0
	str	w23, [x1,4]
	ldr	x1, [x3,-88]
	ldr	x3, [x3,2120]
	add	x1, x1, x0
	ldrh	w0, [x20,2038]
	mul	w0, w21, w0
	add	w21, w21, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1820
.L1821:
	add	w19, w19, 1
	uxth	w19, w19
.L1820:
	add	w2, w28, 1
	uxth	w28, w2
	b	.L1818
.L1851:
	cbz	w21, .L1824
	ldr	w0, [x29,124]
	adrp	x20, .LANCHOR2
	mov	w2, w21
	mov	x24, 0
	cmp	w0, wzr
	add	x0, x20, :lo12:.LANCHOR2
	cset	w23, ne
	mov	x27, 56
	mov	w1, w23
	ldr	x0, [x0,-88]
	bl	FlashEraseBlocks
.L1825:
	cmp	w21, w24, uxth
	bls	.L1852
	add	x1, x20, :lo12:.LANCHOR2
	mul	x0, x24, x27
	ldr	x1, [x1,-88]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1826
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1826:
	add	x24, x24, 1
	b	.L1825
.L1852:
	cbnz	w26, .L1828
	mov	w0, 6
	uxth	w23, w23
	str	w0, [x29,124]
	mov	w27, 1
	b	.L1829
.L1828:
	add	x0, x22, :lo12:.LANCHOR0
	mov	w23, 1
	ldrh	w27, [x0,2028]
	lsr	w0, w27, 2
	str	w0, [x29,124]
.L1829:
	mov	w24, 0
	add	x22, x22, :lo12:.LANCHOR0
.L1839:
	mov	w28, 0
	mov	w21, w28
.L1830:
	ldrh	w0, [x22,1952]
	cmp	w0, w28
	bls	.L1853
	add	x1, x20, :lo12:.LANCHOR2
	mov	w0, 56
	umull	x0, w28, w0
	ldr	x1, [x1,-88]
	str	wzr, [x1,x0]
	add	x0, x22, 1984
	mov	w1, w25
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w26, .L1831
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L1832
.L1831:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1832
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
	ldr	x0, [x3,2096]
	ldr	x3, [x3,2104]
	str	x0, [x1,8]
	ldrh	w0, [x22,2038]
	mul	w0, w21, w0
	add	w21, w21, 1
	sdiv	w0, w0, w2
	uxth	w21, w21
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
.L1832:
	add	w2, w28, 1
	uxth	w28, w2
	b	.L1830
.L1853:
	cbz	w21, .L1824
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w21
	mov	w2, w23
	mov	w3, 1
	mov	x28, 0
	ldr	x0, [x0,-88]
	bl	FlashProgPages
.L1835:
	cmp	w21, w28, uxth
	bls	.L1854
	add	x1, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x28, x0
	ldr	x3, [x1,-88]
	add	x1, x3, x0
	ldr	w0, [x3,x0]
	cmn	w0, #1
	bne	.L1836
	ldr	w0, [x1,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	b	.L1837
.L1836:
	cbz	w26, .L1837
	ldr	w0, [x1,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1837:
	add	x28, x28, 1
	b	.L1835
.L1854:
	ldr	w0, [x29,124]
	add	w24, w24, w0
	uxth	w24, w24
	cmp	w24, w27
	bcc	.L1839
	cmp	w25, 63
	bhi	.L1824
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, w21
	ldr	x0, [x20,-88]
	bl	FlashEraseBlocks
.L1824:
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
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	add	x2, x22, :lo12:.LANCHOR2
	adrp	x19, .LANCHOR4
	adrp	x20, .LANCHOR0
	mov	x21, 0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	add	x24, x19, :lo12:.LANCHOR4
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x2,-64]
	add	x23, x20, 2112
	ldr	x2, [x2,2112]
	str	x2, [x24,152]
	ldrh	w2, [x20,2036]
	str	x0, [x24,144]
	bl	ftl_memset
.L1856:
	ldrh	w1, [x20,1974]
	mov	w0, w21
	add	x21, x21, 1
	cmp	w0, w1
	bge	.L1863
	add	x1, x22, :lo12:.LANCHOR2
	ldr	x3, [x24,144]
	ldrh	w2, [x1,2232]
	add	x1, x23, x21, lsl 3
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L1856
.L1863:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 255
	add	x19, x19, 136
	mov	w2, 16
	adrp	x25, .LC108
	adrp	x26, .LC109
	ldr	x21, [x19,16]
	mov	w24, 0
	add	x25, x25, :lo12:.LC108
	add	x26, x26, :lo12:.LC109
	mov	x0, x21
	bl	ftl_memset
	mov	w0, -3887
	strh	w0, [x21]
	ldr	w0, [x23,8]
	str	w0, [x21,4]
	ldrh	w0, [x20,2112]
	strh	w0, [x21,2]
	ldrh	w0, [x23,4]
	strh	w0, [x21,8]
	ldrh	w0, [x23,6]
	strh	w0, [x21,10]
	ldr	w0, [x20,1948]
	strh	w0, [x21,12]
.L1858:
	add	x27, x22, :lo12:.LANCHOR2
	ldrh	w1, [x20,2112]
	ldrh	w2, [x20,2114]
	ldrh	w3, [x20,2116]
	ldr	x0, [x27,-64]
	str	x0, [x19,8]
	ldr	x0, [x27,2112]
	str	x0, [x19,16]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19,4]
	mov	x0, x25
	ldrh	w4, [x21,10]
	str	wzr, [x19]
	bl	printk
	mov	w1, 1
	mov	x0, x19
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w0, [x20,2028]
	ldrh	w1, [x20,2114]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L1859
	ldr	w0, [x20,2120]
	strh	wzr, [x20,2114]
	add	w0, w0, 1
	str	w0, [x20,2120]
	str	w0, [x21,4]
	ldrh	w0, [x20,2112]
	ldrh	w1, [x20,2116]
	strh	w0, [x21,8]
	strh	w0, [x20,2116]
	ldr	x0, [x27,-88]
	strh	w1, [x20,2112]
	lsl	w1, w1, 10
	str	w1, [x19,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x27,-88]
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x19
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
.L1859:
	ldrh	w0, [x20,2114]
	add	w0, w0, 1
	strh	w0, [x20,2114]
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1860
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	b	.L1858
.L1860:
	cbnz	w24, .L1864
	mov	w24, 1
	b	.L1858
.L1864:
	mov	w0, 0
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	add	x23, x21, 96
	mov	x19, x21
	sub	x24, x21, #16
.L1866:
	cmp	x20, x23
	bne	.L1867
	ldrh	w0, [x21,-8]
	ldr	w3, [x21,876]
	lsr	w2, w0, 1
	add	w4, w2, 1
	mul	w1, w0, w3
	add	w1, w4, w1, lsr 2
	ldr	w4, [x21,160]
	uxth	w1, w1
	cbz	w4, .L1868
	ldr	w4, [x21,220]
	cmp	w4, 29
	bhi	.L1868
	cmp	w4, 2
	mov	w1, 0
	bls	.L1869
	tbz	x0, 0, .L1894
	cbz	w3, .L1869
.L1894:
	mov	w1, w2
	b	.L1868
.L1867:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L1869
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w2, [x0,1972]
	cmp	w2, 1
	beq	.L1869
	ldrb	w0, [x0,120]
	cbnz	w0, .L1869
	ldr	w2, [x21,160]
	ldrh	w0, [x21,-8]
	lsr	w1, w0, 3
	cbz	w2, .L1868
	ldr	w2, [x21,220]
	cmp	w2, 1
	bhi	.L1868
	mov	w1, 7
	mul	w1, w0, w1
	lsr	w1, w1, 3
.L1868:
	cbz	w1, .L1869
	sub	w1, w1, #1
	uxth	w1, w1
.L1869:
	mov	x0, x24
	bl	List_pop_index_node
	uxth	w22, w0
	ldrh	w0, [x19,-8]
	sub	w0, w0, #1
	strh	w0, [x19,-8]
	strh	w22, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L1871
	adrp	x25, .LANCHOR0
	mov	x3, 56
	add	x0, x25, :lo12:.LANCHOR0
	mov	w6, 65535
	ldrh	w5, [x0,1952]
	mov	x0, 0
	mov	w26, w0
	b	.L1872
.L1871:
	ubfiz	x0, x22, 1, 16
	ldr	x1, [x19,-40]
	mov	w2, -1
	strh	w2, [x1,x0]
	b	.L1916
.L1875:
	ldr	x4, [x19,-88]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L1874
	umull	x2, w26, w3
	add	w26, w26, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w26, w26
	str	w1, [x2,4]
.L1874:
	add	x0, x0, 1
.L1872:
	cmp	w5, w0, uxth
	bhi	.L1875
	cmp	x20, x19
	uxtw	x27, w22
	bne	.L1876
	ldr	w0, [x19,160]
	cbz	w0, .L1876
	ldr	x0, [x19,-80]
	ldrh	w0, [x0,x27,lsl 1]
	cmp	w0, 30
	bls	.L1876
	strb	wzr, [x19,8]
.L1876:
	ldrb	w0, [x20,8]
	cbnz	w0, .L1877
	lsl	x0, x27, 1
	ldr	x1, [x19,-80]
	mov	w2, 2
	ldrh	w3, [x1,x0]
	cbz	w3, .L1917
	add	x2, x25, :lo12:.LANCHOR0
	ldrh	w2, [x2,2016]
	add	w2, w3, w2
.L1917:
	strh	w2, [x1,x0]
	mov	w1, 0
	ldr	w0, [x19,204]
	add	w0, w0, 1
	str	w0, [x19,204]
	mov	w0, w22
	bl	ftl_set_blk_mode
	b	.L1880
.L1877:
	lsl	x0, x27, 1
	ldr	x2, [x19,-80]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x19,208]
	add	w0, w0, 1
	str	w0, [x19,208]
	mov	w0, w22
	bl	ftl_set_blk_mode.part.8
.L1880:
	lsl	x27, x27, 1
	ldr	x0, [x19,-80]
	ldr	w1, [x19,216]
	ldrh	w0, [x0,x27]
	cmp	w0, w1
	bls	.L1881
	str	w0, [x19,216]
.L1881:
	add	x2, x25, :lo12:.LANCHOR0
	ldr	w3, [x19,204]
	ldr	w0, [x19,208]
	ldrh	w1, [x2,2016]
	madd	w0, w1, w3, w0
	ldrh	w1, [x2,1960]
	mov	x3, 56
	udiv	w0, w0, w1
	ldr	x1, [x19,2152]
	str	w0, [x19,212]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L1882:
	cmp	w26, w0, uxth
	bls	.L1918
	ldr	x1, [x19,-88]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L1882
.L1918:
	ldrb	w1, [x20,8]
	mov	w2, w26
	ldr	x0, [x19,-88]
	mov	x28, 0
	bl	FlashEraseBlocks
	mov	w1, w28
	mov	x3, 56
.L1884:
	cmp	w26, w28, uxth
	bls	.L1919
	mul	x0, x28, x3
	ldr	x2, [x19,-88]
	add	x4, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L1885
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
.L1885:
	add	x28, x28, 1
	b	.L1884
.L1919:
	cbz	w1, .L1887
	mov	w0, w22
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1887:
	ldrb	w0, [x20,7]
	cbnz	w0, .L1888
	ldr	x0, [x19,-40]
	mov	w1, -1
	strh	w1, [x0,x27]
.L1916:
	mov	w0, w22
	bl	INSERT_DATA_LIST
	b	.L1866
.L1888:
	add	x25, x25, :lo12:.LANCHOR0
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x25,2026]
	strh	w22, [x20]
	mul	w0, w0, w1
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	uxth	w0, w0
	strh	w0, [x20,4]
	ldr	w2, [x1,196]
	str	w2, [x20,12]
	add	w2, w2, 1
	str	w2, [x1,196]
	ldr	x1, [x1,-40]
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
	ldrh	w0, [x19,956]
	stp	x21, x22, [sp,32]
	mov	w21, 0
	cbz	w0, .L1922
	adrp	x22, .LANCHOR0
	adrp	x24, .LC110
	add	x22, x22, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LC110
.L1930:
	ldrh	w0, [x22,1952]
	cmp	w0, w21
	bls	.L1931
	add	x0, x22, 1984
	mov	w1, w23
	mov	w20, 0
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w25, w0
.L1923:
	ldrh	w0, [x19,956]
	cmp	w0, w20
	bls	.L1932
	add	x0, x19, 960
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w25
	bne	.L1924
	mov	w1, w25
	mov	x0, x24
	bl	printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,956]
	mov	w3, w20
.L1925:
	cmp	w3, w2
	bcs	.L1933
	add	x4, x19, 960
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L1925
.L1933:
	sub	w2, w2, #1
	strh	w2, [x19,956]
.L1924:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1923
.L1932:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1930
.L1931:
	bl	FtlGcReFreshBadBlk
.L1922:
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
	cbnz	w2, .L1935
	ldrh	w3, [x1,296]
	cmp	w3, w19
	bne	.L1936
	mov	w0, -1
	strh	w0, [x1,296]
	b	.L1937
.L1936:
	ldrh	w3, [x0,#:lo12:.LANCHOR2]
	mov	w0, w2
	cmp	w3, w19
	beq	.L1938
	ldrh	w3, [x1,48]
	cmp	w3, w19
	beq	.L1938
	ldrh	w1, [x1,96]
	cmp	w1, w19
	beq	.L1938
.L1937:
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
	b	.L1938
.L1935:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L1938:
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
	beq	.L1943
	add	x0, x2, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x0, [x0,-40]
	ldrh	w19, [x0,x3]
	cbnz	w19, .L1944
	adrp	x0, .LC111
	mov	w2, w19
	add	x0, x0, :lo12:.LC111
	bl	printk
	b	.L1948
.L1944:
	sub	w19, w19, #1
	strh	w19, [x0,x3]
.L1943:
	add	x19, x2, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w2, [x19,2320]
	cmp	w2, w0
	bne	.L1946
	strh	w1, [x19,2320]
.L1948:
	mov	w0, 0
	b	.L1945
.L1946:
	cmp	w2, w1
	str	x1, [x29,40]
	mov	w0, 0
	beq	.L1945
	mov	w0, w2
	bl	update_vpc_list
	cmp	w0, wzr
	ldr	x1, [x29,40]
	cset	w0, ne
	strh	w1, [x19,2320]
.L1945:
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
	cbz	w1, .L1949
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x20, .LANCHOR2
	mov	w21, 65535
	add	x0, x0, 8
	add	x20, x20, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR0
	add	x20, x20, 996
	ldrh	w0, [x19,x0,lsl 1]
.L1952:
	cmp	w0, w21
	bne	.L1960
.L1954:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1952]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L1953
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1953:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L1952
.L1960:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L1955
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L1955
	ldrh	w0, [x19,2]
	ldrh	w0, [x20,w0,sxtw 1]
	cmp	w0, w21
	bne	.L1955
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L1954
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L1949
.L1955:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,120]
	cbz	w2, .L1949
	cmp	w1, 1
	bne	.L1949
	ldrh	w2, [x19,2]
	ldrh	w1, [x0,2028]
	cmp	w2, w1
	bcc	.L1949
	adrp	x2, .LANCHOR2-40
	ldrh	w1, [x19]
	ldrh	w4, [x19,4]
	lsl	x1, x1, 1
	ldr	x3, [x2,#:lo12:.LANCHOR2-40]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,2026]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L1949:
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
.L1962:
	cmp	w0, w22
	adrp	x20, .LANCHOR0
	bne	.L1977
.L1963:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR0
	add	w0, w0, 1
	ldrh	w1, [x1,1952]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L1964
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1964:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L1962
.L1977:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L1965
	add	x1, x23, :lo12:.LANCHOR0
	ldrb	w1, [x1,120]
	cbnz	w1, .L1965
	ldrh	w1, [x19,2]
	ldrh	w1, [x21,w1,sxtw 1]
	cmp	w1, w22
	bne	.L1965
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L1963
.L1965:
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
.L1966:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,1952]
.L1968:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L1967
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L1967:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L1968
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L1973
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L1970
	ldrh	w0, [x19,2]
	ldrh	w0, [x21,w0,sxtw 1]
	cmp	w0, w24
	bne	.L1970
	ldrh	w1, [x19,4]
	cbz	w1, .L1970
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L1966
.L1970:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,120]
	cbz	w0, .L1973
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,2028]
	cmp	w1, w0
	bcc	.L1973
	adrp	x0, .LANCHOR2-40
	ldrh	w1, [x19]
	ldrh	w3, [x19,4]
	lsl	x1, x1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR2-40]
	ldrh	w2, [x0,x1]
	sub	w2, w2, w3
	strh	w2, [x0,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x20,2026]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L1973:
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
	adrp	x0, .LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	add	x19, x0, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	str	x25, [sp,64]
	add	x20, x20, 136
	mov	w25, 65535
	stp	x21, x22, [sp,32]
	ldr	x22, [x19,2112]
	add	x21, x19, 224
	stp	x23, x24, [sp,48]
	ldr	x1, [x19,-64]
	adrp	x24, .LANCHOR0
	str	x1, [x20,8]
	add	x23, x24, :lo12:.LANCHOR0
	str	x22, [x20,16]
	ldrh	w2, [x19,280]
	strh	w2, [x22,2]
	mov	w2, -3932
	strh	w2, [x22]
	str	wzr, [x22,12]
	ldr	w2, [x19,288]
	str	w2, [x22,4]
	mov	w2, 19539
	str	wzr, [x22,8]
	movk	w2, 0x4654, lsl 16
	ldrh	w1, [x19,286]
	ldrh	w0, [x0,#:lo12:.LANCHOR2]
	strh	w1, [x19,232]
	strh	w0, [x19,238]
	ldrh	w1, [x23,1974]
	ldrh	w0, [x19,2]
	strb	w1, [x19,234]
	ldrb	w1, [x19,6]
	str	w2, [x19,224]
	mov	w2, 54
	orr	w0, w1, w0, lsl 6
	movk	w2, 0x5000, lsl 16
	strh	w0, [x19,240]
	str	w2, [x19,228]
	ldrb	w0, [x19,8]
	strb	w0, [x19,235]
	ldrh	w1, [x19,48]
	strh	w1, [x19,242]
	ldrh	w1, [x19,50]
	ldrb	w2, [x19,54]
	ldrb	w0, [x19,56]
	orr	w1, w2, w1, lsl 6
	strb	w0, [x19,236]
	strh	w1, [x19,244]
	ldrb	w0, [x19,104]
	ldrh	w1, [x19,96]
	strh	w1, [x19,246]
	strb	w0, [x19,237]
	ldrh	w1, [x19,98]
	ldr	w0, [x19,204]
	ldrb	w2, [x19,102]
	str	w0, [x19,256]
	ldr	w0, [x19,196]
	orr	w1, w2, w1, lsl 6
	str	w0, [x19,264]
	ldr	w0, [x19,200]
	str	w0, [x19,260]
	ldrh	w2, [x23,2036]
	ldr	x0, [x20,8]
	strh	w1, [x19,248]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x20,8]
	mov	x1, x21
	mov	w2, 48
	mov	x21, x24
	bl	ftl_memcpy
	ldr	x0, [x20,8]
	ldrh	w2, [x23,1960]
	ldr	x1, [x19,-40]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x23,1960]
	ldr	x1, [x20,8]
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
.L1979:
	ldr	x0, [x19,-64]
	str	x0, [x20,8]
	ldrh	w1, [x19,280]
	ldr	x0, [x19,2112]
	str	x0, [x20,16]
	ldrh	w0, [x19,282]
	orr	w1, w0, w1, lsl 10
	str	w1, [x20,4]
	mov	w1, 1
	mov	x0, x20
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19,282]
	ldrh	w1, [x1,2028]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1980
	ldrh	w0, [x19,280]
	ldrh	w25, [x19,284]
	strh	wzr, [x19,282]
	strh	w0, [x19,284]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,196]
	str	w1, [x19,288]
	add	w2, w1, 1
	str	w2, [x19,196]
	ubfiz	w2, w0, 10, 16
	str	w2, [x20,4]
	str	w1, [x22,4]
	mov	w1, 1
	strh	w0, [x19,280]
	mov	w2, w1
	strh	w0, [x22,2]
	mov	w3, w1
	mov	x0, x20
	bl	FlashProgPages
.L1980:
	ldrh	w1, [x19,282]
	ldr	w0, [x20]
	add	w1, w1, 1
	cmn	w0, #1
	uxth	w1, w1
	strh	w1, [x19,282]
	bne	.L1981
	cmp	w1, 1
	bne	.L1979
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,2028]
	sub	w1, w1, #1
	strh	w1, [x19,282]
	b	.L1979
.L1981:
	cmp	w1, 1
	beq	.L1979
	mov	w0, 65535
	cmp	w25, w0
	beq	.L1983
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1983:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	mov	x19, x0
	add	x0, x20, :lo12:.LANCHOR2
	str	x25, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w23, 0
	ldr	x22, [x0,2112]
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	mov	x21, x0
	ldrb	w1, [x1,120]
	cbz	w1, .L1988
	ldrb	w0, [x19,8]
	cmp	w0, 1
	cset	w23, eq
.L1988:
	mov	w24, 7
	mov	w25, -1
.L1989:
	subs	w24, w24, #1
	beq	.L1990
	ldrh	w0, [x19,4]
	cbz	w0, .L1990
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,92]
	add	x1, x20, :lo12:.LANCHOR2
	str	w25, [x29,112]
	mov	w2, w23
	mov	w3, 0
	ldr	x0, [x1,-64]
	str	x0, [x29,96]
	ldr	x0, [x1,2112]
	str	x0, [x29,104]
	str	w25, [x22,8]
	str	w25, [x22,12]
	ldrh	w0, [x19]
	strh	w0, [x22,2]
	strh	wzr, [x22]
	ldr	x22, [x1,2112]
	ldr	w0, [x1,200]
	str	w0, [x22,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,200]
	add	x0, x29, 88
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L1989
.L1990:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19]
	ldrh	w3, [x19,4]
	lsl	x0, x0, 1
	ldr	x2, [x20,-40]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	add	x0, x21, :lo12:.LANCHOR0
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
	ldp	x21, x22, [sp,32]
	ldrh	w0, [x0,2026]
	strh	w0, [x19,2]
	ldp	x23, x24, [sp,48]
	ldp	x19, x20, [sp,16]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 144
	ret
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
	sub	w1, w1, #1
	cmp	w2, w1
	blt	.L2000
	ubfiz	x1, x0, 1, 16
	ldrh	w24, [x20,x1]
	cbz	w24, .L2000
	ldr	w0, [x19,52]
	cbnz	w0, .L2000
	mov	w2, 1
	str	w2, [x19,52]
	strh	w0, [x20,x1]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2028]
	cmp	w1, w0
	bcc	.L2001
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2001:
	adrp	x20, .LANCHOR4
	mov	w21, 0
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x26, .LANCHOR2
	add	x20, x20, 136
.L2002:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L2010
	ubfiz	x25, x21, 2, 16
	ldr	w0, [x23,x25]
	cmp	w24, w0, lsr 10
	bne	.L2003
	add	x0, x26, :lo12:.LANCHOR2
	ldr	x1, [x0,2072]
	ldr	x0, [x0,2112]
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
	bne	.L2004
	str	wzr, [x23,x25]
	b	.L2003
.L2004:
	ldr	x2, [x20,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L2003:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2002
.L2010:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L2000:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x22,2028]
	cmp	w1, w0
	bcc	.L2006
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L2006:
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
	bne	.L2012
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
	b	.L2013
.L2012:
	ubfiz	x0, x0, 1, 16
	adrp	x21, .LANCHOR2
	add	x3, x21, :lo12:.LANCHOR2
	ldrh	w22, [x20,x0]
	adrp	x20, .LANCHOR4
	ldrh	w0, [x19,2]
	add	x1, x20, :lo12:.LANCHOR4
	orr	w0, w0, w22, lsl 10
	str	w0, [x1,140]
	ldr	x0, [x3,-64]
	str	x0, [x1,144]
	ldr	x0, [x3,2112]
	str	x0, [x1,152]
	ldr	w1, [x19,48]
	str	w1, [x0,4]
	mov	w1, -1291
	strh	w1, [x0,8]
	ldrh	w1, [x19,4]
	strh	w1, [x0]
	mov	w1, 255
	strh	w22, [x0,2]
	adrp	x0, .LANCHOR0+2028
	ldrh	w2, [x0,#:lo12:.LANCHOR0+2028]
	ldr	x0, [x3,-64]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, 0
	mov	w1, w0
.L2014:
	ldrh	w2, [x19,6]
	cmp	w2, w1
	bls	.L2017
	ubfiz	x4, x1, 2, 16
	ldr	w2, [x23,x4]
	cmp	w22, w2, lsr 10
	bne	.L2015
	add	x3, x21, :lo12:.LANCHOR2
	add	w0, w0, 1
	uxth	w0, w0
	ldr	x5, [x3,-64]
	ubfiz	x2, x0, 3, 16
	str	w1, [x5,x2]
	ldr	x3, [x3,-64]
	ldr	w4, [x23,x4]
	add	x2, x3, x2
	str	w4, [x2,4]
.L2015:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2014
.L2017:
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
.L2013:
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
	str	x27, [sp,80]
	stp	x21, x22, [sp,32]
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x22, .LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x19, x0
	mov	w21, w1
	mov	x23, x2
	adrp	x24, .LANCHOR0
	mov	x25, x22
	mov	w26, 65535
	add	x20, x20, 136
.L2024:
	add	x1, x22, :lo12:.LANCHOR2
	ldr	w0, [x1,180]
	add	w0, w0, 1
	str	w0, [x1,180]
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,2028]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L2019
	ldrh	w0, [x19]
	cmp	w0, w26
	bne	.L2020
.L2019:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L2020:
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19,16]
	ldrh	w27, [x0,x1,lsl 1]
	mov	w1, 0
	ldrh	w0, [x19,2]
	str	x23, [x20,8]
	orr	w0, w0, w27, lsl 10
	str	w0, [x20,4]
	add	x0, x25, :lo12:.LANCHOR2
	ldr	x0, [x0,2112]
	str	x0, [x20,16]
	bl	ftl_memset
	ldr	x0, [x20,16]
	ldr	w1, [x19,48]
	str	w1, [x0,4]
	strh	w21, [x0,8]
	ldrh	w1, [x19,4]
	strh	w1, [x0]
	mov	w1, 1
	strh	w27, [x0,2]
	mov	w2, w1
	mov	x0, x20
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	uxth	w0, w0
	strh	w0, [x19,2]
	cmp	w0, 1
	beq	.L2024
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L2024
	ldr	x0, [x19,40]
	ldr	w1, [x20,4]
	str	w1, [x0,w21,uxtw 2]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
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
	add	x0, x20, 2240
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
	ldrh	w3, [x5,2034]
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
	ldrh	w0, [x5,2066]
.L2029:
	uxth	x19, w4
	cmp	w19, w0
	bcs	.L2041
	add	x4, x4, 1
	add	x1, x25, x4, lsl 4
	ldrh	w1, [x1,-16]
	cmp	w1, w20
	bne	.L2029
.L2030:
	cbnz	w22, .L2031
	add	x0, x23, :lo12:.LANCHOR2
	ldr	x1, [x0,144]
	add	x1, x1, x19, lsl 4
	ldr	x0, [x1,8]
	ldr	w0, [x0,x24,lsl 2]
	str	w0, [x21]
	b	.L2032
.L2031:
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
.L2032:
	add	x23, x23, :lo12:.LANCHOR2
	ldr	x0, [x23,144]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19,4]
	cmn	w0, #1
	beq	.L2038
	add	w0, w0, 1
	str	w0, [x19,4]
	b	.L2038
.L2041:
	bl	select_l2p_ram_region
	uxth	x19, w0
	ubfiz	x2, x19, 4, 16
	mov	w1, w0
	add	x3, x25, x2
	ldrh	w4, [x25,x2]
	mov	w2, 65535
	cmp	w4, w2
	beq	.L2035
	ldr	w2, [x3,4]
	tbz	w2, #31, .L2035
	str	x1, [x29,88]
	bl	flush_l2p_region
	ldr	x1, [x29,88]
.L2035:
	mov	w0, w20
	bl	load_l2p_region
	b	.L2030
.L2038:
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
	cbnz	w1, .L2043
	ldr	x19, [x2,-16]
	cbz	x19, .L2044
	ldrh	w3, [x2,-8]
	mov	x4, -6148914691236517206
	ldr	x2, [x2,-56]
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L2045:
	cmp	w1, w3
	beq	.L2044
	cmp	w19, w0
	bne	.L2046
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
	b	.L2044
.L2046:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2044
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2045
.L2043:
	add	w1, w1, 1
	strh	w1, [x3,x22]
.L2044:
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
	add	x4, x0, :lo12:.LANCHOR4
	str	x0, [x29,120]
	add	x4, x4, 136
	sub	w0, w23, #1
	ldr	x2, [x1,-64]
	ldr	x26, [x1,2112]
	mov	w1, -1
	str	x2, [x4,8]
	str	x26, [x4,16]
	mov	x19, x4
	strh	w1, [x27]
	strh	w1, [x27,2]
	mov	w1, 1
	str	wzr, [x27,52]
	str	wzr, [x27,48]
	str	w1, [x27,56]
	str	w0, [x29,116]
.L2052:
	cmp	w25, w23
	bge	.L2069
	ldr	w0, [x29,116]
	sxtw	x28, w25
	cmp	w25, w0
	bne	.L2053
	lsl	x0, x28, 1
	mov	w1, 1
	add	x23, x21, x0
	ldrh	w0, [x21,x0]
	mov	w21, 0
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
	add	x19, x0, :lo12:.LANCHOR4
	add	x19, x19, 136
	str	xzr, [x19,8]
.L2054:
	cmp	w21, w24
	bge	.L2069
	ldrh	w0, [x23]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w21, w0, lsl 10
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2055
	ldrh	w0, [x26,8]
	cmp	w0, w22
	bcs	.L2055
	ldrh	w2, [x26]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2055
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x19,4]
	str	w1, [x20,x0]
.L2055:
	add	w21, w21, 1
	sxth	w21, w21
	b	.L2054
.L2069:
	mov	x0, x27
	bl	ftl_free_no_use_map_blk
	adrp	x0, .LANCHOR0+2028
	ldrh	w1, [x27,2]
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2028]
	cmp	w1, w0
	bne	.L2058
	mov	x0, x27
	bl	ftl_map_blk_alloc_new_blk
	b	.L2058
.L2053:
	ldr	x0, [x29,136]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,-64]
	str	x0, [x19,8]
	lsl	x0, x28, 1
	add	x28, x21, x0
	ldrh	w1, [x21,x0]
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w0, [x0,2028]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x19,4]
	mov	w2, w1
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2059
	ldrh	w1, [x26]
	ldrh	w0, [x27,4]
	cmp	w1, w0
	bne	.L2059
	ldrh	w1, [x26,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2059
	mov	w0, 0
.L2060:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w1, [x1,2028]
	sub	w1, w1, #1
	cmp	w0, w1
	bge	.L2063
	ldr	x1, [x29,136]
	sbfiz	x2, x0, 3, 32
	add	x1, x1, :lo12:.LANCHOR2
	ldr	x5, [x1,-64]
	ldrh	w1, [x5,x2]
	cmp	w1, w22
	bcs	.L2061
	add	x2, x5, x2
	ubfiz	x1, x1, 2, 16
	ldr	w2, [x2,4]
	str	w2, [x20,x1]
.L2061:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L2060
.L2059:
	str	xzr, [x19,8]
	mov	w5, 0
.L2064:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,2028]
	cmp	w5, w0
	bge	.L2063
	ldrh	w0, [x28]
	mov	w1, 1
	mov	w2, w1
	str	x5, [x29,104]
	orr	w0, w5, w0, lsl 10
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	w0, [x19]
	ldr	x5, [x29,104]
	cmn	w0, #1
	beq	.L2065
	ldrh	w0, [x26,8]
	cmp	w0, w22
	bcs	.L2065
	ldrh	w2, [x26]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2065
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x19,4]
	str	w1, [x20,x0]
.L2065:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L2064
.L2063:
	add	w6, w25, 1
	sxth	w25, w6
	b	.L2052
.L2058:
	ldrh	w1, [x27,8]
	ldrh	w0, [x27,10]
	cmp	w1, w0
	bcc	.L2070
	mov	x0, x27
	bl	ftl_map_blk_gc
.L2070:
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
	ldrh	w2, [x1,2048]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,2072]
	strh	w2, [x0,8]
	ldrh	w2, [x1,2050]
	ldr	x1, [x1,2080]
	str	x1, [x0,16]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	strh	w2, [x0,6]
	ldr	x2, [x1,2192]
	str	x2, [x0,24]
	ldr	x2, [x1,2184]
	ldr	x1, [x1,2200]
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
	add	x0, x0, 2240
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
	cbz	w3, .L2084
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w3, [x2,84]
	ldr	w1, [x1,2104]
	cmp	w3, w1
	bcs	.L2085
	mov	w21, 2048
	mov	x19, x0
.L2090:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x19,436]
	ldr	w1, [x1,2104]
	cmp	w0, w1
	bcs	.L2087
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x19,436]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x19,436]
	cmn	w1, #1
	beq	.L2088
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
	bne	.L2087
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2087
.L2088:
	subs	w21, w21, #1
	bne	.L2090
.L2087:
	mov	w0, -1
	b	.L2092
.L2085:
	ldr	w0, [x0,164]
	str	wzr, [x2,80]
	str	wzr, [x2,84]
	str	w0, [x2,76]
	b	.L2098
.L2084:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	w3, [x0,216]
	mov	w2, 1048576
	ldr	w0, [x0,160]
	ldrb	w4, [x20,120]
	cbnz	w4, .L2093
	add	w2, w0, w3, lsr 10
	mov	w0, 33554432
	asr	w2, w0, w2
.L2093:
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w0, [x3,428]
	ldr	w3, [x3,164]
	add	w4, w3, 1048576
	cmp	w0, w4
	bhi	.L2094
	add	w2, w2, w0
	mov	w0, 0
	cmp	w2, w3
	bcs	.L2092
.L2094:
	add	x0, x1, :lo12:.LANCHOR2
	mov	w1, 1
	str	wzr, [x0,436]
	str	w1, [x0,432]
	str	w3, [x0,428]
.L2098:
	mov	w0, 0
.L2092:
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
	ldrh	w21, [x0,2034]
	mov	x25, x2
	ldrh	w0, [x0,2020]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L2100
	adrp	x24, .LANCHOR4
	lsr	w21, w26, w21
	add	x24, x24, :lo12:.LANCHOR4
	mov	w20, 0
	adrp	x27, .LANCHOR2
	add	x24, x24, 200
.L2101:
	cbz	w28, .L2100
	adrp	x0, .LANCHOR2
	add	x2, x0, :lo12:.LANCHOR2
	ldr	x0, [x2,2200]
	ldr	w4, [x0,w21,uxtw 2]
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w1, [x0,2032]
	uxth	w0, w28
	udiv	w22, w26, w1
	msub	w22, w22, w1, w26
	sub	w19, w1, w22
	uxth	w19, w19
	cmp	w19, w28
	csel	w19, w0, w19, hi
	cmp	w19, w1
	beq	.L2103
	cbz	w4, .L2103
	ldr	x0, [x2,2080]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w4, [x29,124]
	str	xzr, [x29,136]
	bl	FlashReadPages
	b	.L2104
.L2103:
	add	x0, x27, :lo12:.LANCHOR2
	add	x2, x23, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x0,2080]
	ldrh	w2, [x2,2036]
	bl	ftl_memset
.L2104:
	add	x5, x27, :lo12:.LANCHOR2
	lsl	w4, w19, 9
	ubfiz	x22, x22, 9, 16
	mov	w2, w4
	mov	x1, x25
	str	x4, [x29,104]
	ldr	x0, [x5,2080]
	sub	w28, w28, w19
	str	x5, [x29,96]
	add	w26, w26, w19
	add	x0, x0, x22
	bl	ftl_memcpy
	ldr	x5, [x29,96]
	mov	w1, w21
	mov	x0, x24
	add	w21, w21, 1
	ldr	x2, [x5,2080]
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	x4, [x29,104]
	csinv	w20, w20, wzr, ne
	add	x25, x25, x4, sxtw
	b	.L2101
.L2100:
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
	bne	.L2114
	mov	w1, 54
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
	ldr	w0, [x0,2024]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2114:
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
	cbz	w4, .L2117
	ldr	w3, [x3,220]
	cmp	w3, 29
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2117:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,264]
	cmp	w4, 31
	bhi	.L2118
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,264]
.L2118:
	cbnz	w0, .L2119
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,2152]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2120
.L2119:
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x0, [x2,2152]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	ldr	x0, [x2,2152]
	ldrh	w1, [x2,2136]
	str	wzr, [x0,4]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	ldr	x2, [x2,2152]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2120:
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
	cbz	w1, .L2125
	adrp	x3, .LANCHOR2
	adrp	x2, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR2
	add	x1, x2, :lo12:.LANCHOR4
	sub	w20, w20, #1
	str	wzr, [x0,56]
	mov	x21, x0
	add	x1, x1, 136
	ldr	x0, [x3,2072]
	uxth	w20, w20
	ldr	x3, [x3,2112]
	mov	x19, x2
	str	x3, [x1,16]
	ubfiz	x3, x20, 2, 16
	str	x0, [x1,8]
	ldr	w3, [x4,x3]
	str	w3, [x1,4]
	cbz	w3, .L2127
	mov	x0, x1
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L2128
.L2127:
	adrp	x2, .LANCHOR0+2036
	mov	w1, 255
	ldrh	w2, [x2,#:lo12:.LANCHOR0+2036]
	bl	ftl_memset
.L2128:
	add	x2, x19, :lo12:.LANCHOR4
	mov	x0, x21
	mov	w1, w20
	ldr	x2, [x2,144]
	bl	FtlMapWritePage
.L2125:
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
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	x19, x0
	adrp	x23, .LANCHOR0
	ldrh	w0, [x0,2]
	str	w0, [x29,188]
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w27, [x19,6]
	ldr	w1, [x29,188]
	ldrh	w0, [x0,2026]
	cmp	w0, w1
	bne	.L2133
	strh	wzr, [x19,4]
	b	.L2284
.L2133:
	ldrh	w0, [x19,16]
	mov	w1, 0
	mov	w2, 65535
.L2135:
	cmp	w0, w2
	bne	.L2287
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2135
.L2287:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2137
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2138
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, w21
	ldrb	w1, [x0,120]
	cbnz	w1, .L2207
	add	x0, x0, 196
	ldrh	w24, [x0,w21,sxtw 1]
	b	.L2207
.L2137:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2138
	mov	w24, w0
.L2207:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w28, 0
	mov	w7, 65535
	mov	w8, 56
	adrp	x9, .LANCHOR2
	ldrh	w3, [x0,1952]
	mov	w10, 4
	ldrh	w6, [x0,2038]
	add	x3, x19, x3, lsl 1
	b	.L2140
.L2138:
	strh	wzr, [x19,2]
.L2284:
	strb	wzr, [x19,6]
	b	.L2278
.L2142:
	ldrh	w1, [x2,16]
	cmp	w1, w7
	beq	.L2141
	add	x4, x9, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x0, w28, w8
	ldr	x5, [x4,2048]
	add	x5, x5, x0
	str	w1, [x5,4]
	ldr	x1, [x4,2048]
	ldr	x4, [x4,904]
	add	x0, x1, x0
	mul	w1, w28, w6
	add	w28, w28, 1
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	uxth	w28, w28
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L2141:
	add	x2, x2, 2
.L2140:
	cmp	x2, x3
	bne	.L2142
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2143
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2143:
	adrp	x20, .LANCHOR2
	ldr	w2, [x29,184]
	add	x25, x20, :lo12:.LANCHOR2
	mov	w1, w28
	mov	w26, 0
	ldr	x0, [x25,2048]
	bl	FlashReadPages
	ldr	w22, [x25,200]
	ldr	x3, [x25,2048]
	uxth	w6, w24
	mov	w25, 65535
.L2144:
	cmp	w26, w28
	beq	.L2288
	ldr	w0, [x3]
	cbnz	w0, .L2145
	ldr	x4, [x3,16]
	ldr	w2, [x4,4]
	cmn	w2, #1
	beq	.L2146
	add	x5, x20, :lo12:.LANCHOR2
	mov	w0, w2
	str	x6, [x29,144]
	str	x4, [x29,152]
	ldr	w1, [x5,200]
	str	x3, [x29,160]
	str	x2, [x29,168]
	str	x5, [x29,176]
	bl	ftl_cmp_data_ver
	ldr	x5, [x29,176]
	ldr	x2, [x29,168]
	ldr	x3, [x29,160]
	ldr	x4, [x29,152]
	ldr	x6, [x29,144]
	cbz	w0, .L2146
	add	w2, w2, 1
	str	w2, [x5,200]
.L2146:
	ldr	w0, [x4]
	cmn	w0, #1
	bne	.L2147
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w21, w21
	umull	x26, w26, w1
	ldr	x0, [x0,2048]
	add	x26, x0, x26
	ldr	w0, [x26,4]
	b	.L2280
.L2145:
	mov	w25, w6
.L2147:
	add	w26, w26, 1
	add	x3, x3, 56
	uxth	w26, w26
	b	.L2144
.L2288:
	add	x0, x20, :lo12:.LANCHOR2
	add	w21, w21, 1
	uxth	w21, w21
	ldr	x0, [x0,2048]
	ldr	w0, [x0,4]
.L2280:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2151
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w2, [x0,120]
	cbnz	w2, .L2151
	add	x0, x0, 196
	ldrh	w21, [x0,w21,sxtw 1]
.L2151:
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w0, [x0,2026]
	cmp	w0, w21
	bne	.L2152
	strh	w21, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2152:
	uxth	w0, w27
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w21, w0
	bne	.L2153
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2285
.L2153:
	mov	w0, 65535
	sub	w22, w22, #1
	cmp	w25, w0
	bne	.L2154
	cbnz	w1, .L2155
.L2154:
	add	x0, x20, :lo12:.LANCHOR2
	uxth	w5, w24
	ldr	w1, [x0,2332]
	cmn	w1, #1
	bne	.L2156
	str	w22, [x0,2332]
.L2156:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w25, [x29,188]
	ldr	w6, [x0,2332]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w24, uxth
	bge	.L2157
	sub	w25, w5, #7
	uxth	w25, w25
.L2157:
	mov	w3, -1
	add	x28, x20, :lo12:.LANCHOR2
	mov	w27, w3
	mov	w4, 65535
	mov	w7, 56
	add	x8, x28, 996
.L2158:
	cmp	w25, w5
	bhi	.L2171
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, 0
	ldrh	w2, [x0,1952]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2172:
	cmp	x0, x2
	beq	.L2289
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2159
	umull	x9, w24, w7
	ldr	x10, [x28,2048]
	add	w24, w24, 1
	orr	w1, w25, w1, lsl 10
	add	x9, x10, x9
	uxth	w24, w24
	str	w1, [x9,4]
.L2159:
	add	x0, x0, 2
	b	.L2172
.L2289:
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
.L2161:
	cmp	w1, w24
	beq	.L2290
	ldr	w2, [x0,-16]
	cbnz	w2, .L2162
	ldr	x2, [x0]
	ldrh	w11, [x2]
	cmp	w11, w4
	beq	.L2163
	ldr	w2, [x2,4]
	cmn	w2, #1
	beq	.L2163
	ldr	w27, [x28,2332]
	cmn	w3, #1
	str	w2, [x28,2332]
	bne	.L2163
	ldrh	w2, [x8,x10,lsl 1]
	cmp	w2, w4
	bne	.L2164
	cbz	w9, .L2163
.L2164:
	cmp	w27, w22
	csinv	w3, w27, wzr, ne
	b	.L2163
.L2162:
	ldrb	w0, [x19,8]
	cbnz	w0, .L2155
	add	x0, x20, :lo12:.LANCHOR2
	add	x1, x0, 996
	ldrh	w2, [x1,w25,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2166
	cmn	w3, #1
	beq	.L2167
	str	w3, [x0,2332]
	b	.L2155
.L2167:
	cmp	w6, w22
	beq	.L2168
	str	w6, [x0,2332]
	b	.L2155
.L2168:
	ldr	w1, [x0,2332]
	b	.L2286
.L2166:
	cmp	w27, w22
	beq	.L2169
	cmn	w27, #1
	beq	.L2155
	str	w27, [x0,2332]
	b	.L2155
.L2169:
	ldr	w1, [x0,2332]
	cmp	w1, w22
	beq	.L2155
.L2286:
	sub	w1, w1, #1
	b	.L2281
.L2163:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2161
.L2290:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2158
.L2171:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, -1
.L2281:
	str	w1, [x0,2332]
.L2155:
	adrp	x25, .LANCHOR4
	mov	w1, 1
	add	x0, x25, :lo12:.LANCHOR4
	strh	w1, [x0,266]
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 2240
	bl	FtlMapBlkWriteDumpData
	ldr	w24, [x29,188]
	adrp	x0, .LC112
	str	x25, [x29,136]
	add	x0, x0, :lo12:.LC112
	str	x0, [x29,128]
.L2173:
	add	x0, x23, :lo12:.LANCHOR0
	mov	x1, x19
	str	wzr, [x29,176]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,1952]
	ldrb	w6, [x0,120]
	add	x3, x19, x3, lsl 1
.L2174:
	cmp	x1, x3
	beq	.L2291
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2175
	ldr	w0, [x29,176]
	add	x5, x20, :lo12:.LANCHOR2
	orr	w2, w24, w2, lsl 10
	ldr	x4, [x5,2048]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2176
	cbz	w6, .L2176
	ldr	x2, [x5,2048]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2176:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2175:
	add	x1, x1, 2
	b	.L2174
.L2291:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,2048]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2178:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2292
	ldr	x0, [x29,160]
	mov	x1, 56
	mul	x25, x0, x1
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
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
	bcc	.L2179
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2211
	ldr	w2, [x29,188]
	cmp	w24, w2
	beq	.L2179
.L2211:
	cmp	w0, w26
	bne	.L2212
	cmp	w24, w21
	beq	.L2181
.L2212:
	ldr	w0, [x1,x25]
	cmn	w0, #1
	beq	.L2183
	ldr	x28, [x28,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	bne	.L2190
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2185
	add	x27, x20, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,200]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2185
	add	w0, w22, 1
	str	w0, [x27,200]
.L2185:
	ldr	w27, [x28,8]
	add	x1, x29, 200
	ldr	w0, [x28,12]
	mov	w2, 0
	str	w0, [x29,196]
	mov	w0, w27
	bl	log2phys
	add	x3, x20, :lo12:.LANCHOR2
	str	x3, [x29,168]
	ldr	w1, [x3,2332]
	cmn	w1, #1
	beq	.L2186
	mov	w0, w22
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,168]
	cbz	w0, .L2186
	ldr	w1, [x29,196]
	cmn	w1, #1
	beq	.L2187
	ldr	x0, [x3,2048]
	str	x3, [x29,120]
	add	x0, x0, x25
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x2, [x0,16]
	ldr	x0, [x3,2048]
	str	x2, [x29,168]
	mov	w2, 0
	add	x0, x0, x25
	bl	FlashReadPages
	ldr	x3, [x29,120]
	ldr	x0, [x29,168]
	ldr	x2, [x3,2048]
	ldr	w0, [x0,4]
	str	w0, [x29,144]
	add	x4, x2, x25
	ldr	w0, [x2,x25]
	cmn	w0, #1
	bne	.L2188
	b	.L2189
.L2187:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2190
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
.L2190:
	ldrh	w0, [x19]
	b	.L2283
.L2188:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2189
	ldr	w0, [x3,2332]
	ldr	w1, [x29,144]
	str	x2, [x29,104]
	str	x4, [x29,112]
	str	x3, [x29,120]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,120]
	ldr	x4, [x29,112]
	ldr	x2, [x29,104]
	cbz	w0, .L2189
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2195
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2189
	cmn	w0, #1
	beq	.L2193
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,2048]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x25
	bl	FlashReadPages
	b	.L2194
.L2193:
	str	w0, [x2,x25]
.L2194:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
	ldr	w1, [x1,x25]
	cmn	w1, #1
	beq	.L2195
	ldr	x1, [x29,168]
	ldr	w0, [x0,2332]
	ldr	w25, [x1,4]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2195
	ldr	w0, [x29,144]
	mov	w1, w25
	bl	ftl_cmp_data_ver
	cbz	w0, .L2189
.L2195:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2189:
	mov	w0, -1
	str	w0, [x29,196]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2197
.L2186:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2197
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w25, [x29,200]
	cmn	w25, #1
	beq	.L2197
	ldr	w0, [x29,196]
	cmp	w25, w0
	beq	.L2197
	lsr	x0, x25, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	ldrh	w2, [x20,#:lo12:.LANCHOR2]
	add	x1, x20, :lo12:.LANCHOR2
	cmp	w2, w0
	beq	.L2200
	ldrh	w2, [x1,48]
	cmp	w2, w0
	beq	.L2200
	ldrh	w1, [x1,96]
	cmp	w1, w0
	bne	.L2197
.L2200:
	add	x28, x20, :lo12:.LANCHOR2
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
	beq	.L2197
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2197
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2197:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2179
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,-40]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2201
.L2283:
	bl	decrement_vpc_count
	b	.L2179
.L2201:
	ldr	x0, [x29,128]
	bl	printk
	b	.L2179
.L2183:
	ldr	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR4
	ldr	w0, [x1,268]
	cmp	w0, 31
	bhi	.L2202
	add	x2, x1, 272
	str	w27, [x2,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x1,268]
.L2202:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,2332]
	cmn	w1, #1
	beq	.L2282
	cmp	w1, w22
	bls	.L2179
.L2282:
	str	w22, [x0,2332]
.L2179:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2178
.L2292:
	add	x0, x23, :lo12:.LANCHOR0
	add	w24, w24, 1
	uxth	w24, w24
	ldrh	w1, [x0,2026]
	cmp	w24, w1
	bne	.L2173
	ldrh	w2, [x0,1952]
	mov	w3, 65535
	strh	w24, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2205:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2278
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2205
	strb	w1, [x19,6]
	b	.L2278
.L2181:
	strb	w26, [x19,6]
	strh	w21, [x19,2]
.L2285:
	mov	x0, x19
	mov	w1, w21
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2278:
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
	add	x21, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	ldrh	w1, [x21,4]
	cbz	w1, .L2294
	ldrb	w0, [x21,8]
	cbnz	w0, .L2294
	adrp	x22, .LANCHOR0
	ldrb	w0, [x21,7]
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w2, [x22,2026]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2294
	ldrb	w24, [x21,10]
	ldr	w23, [x22,2104]
	ldrh	w25, [x22,1952]
	cbnz	w24, .L2293
	sub	w23, w23, #1
	add	x1, x29, 84
	mov	w0, w23
	mov	w2, w24
	bl	log2phys
	ldr	x20, [x21,2112]
	ldr	w0, [x29,84]
	ldr	x1, [x21,-64]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w23, [x29,112]
	str	x1, [x29,96]
	str	x20, [x29,104]
	str	w24, [x20,4]
	beq	.L2296
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w24
	bl	FlashReadPages
	b	.L2297
.L2296:
	ldr	x0, [x21,-64]
	mov	w1, 255
	ldrh	w2, [x22,2036]
	bl	ftl_memset
.L2297:
	mov	w0, -3947
	lsl	w25, w25, 2
	strh	w0, [x20]
	mov	w22, 0
	add	x21, x19, :lo12:.LANCHOR2
.L2298:
	cmp	w22, w25
	beq	.L2299
	ldrh	w0, [x21,4]
	cbz	w0, .L2299
	ldr	w0, [x29,92]
	add	w22, w22, 1
	str	w0, [x20,12]
	ldrh	w0, [x21]
	strh	w0, [x20,2]
	mov	x0, x21
	str	w23, [x20,8]
	bl	get_new_active_ppa
	str	w0, [x29,92]
	ldr	w1, [x21,200]
	mov	w2, 0
	str	w1, [x20,4]
	mov	w3, w2
	add	w1, w1, 1
	add	x0, x29, 88
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x21,200]
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x21]
	bl	decrement_vpc_count
	b	.L2298
.L2299:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,10]
	b	.L2293
.L2294:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,10]
.L2293:
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
.L2311:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,2066]
	cmp	w0, w19
	bls	.L2314
	add	x1, x21, :lo12:.LANCHOR2
	ubfiz	x0, x19, 4, 16
	ldr	x1, [x1,144]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2312
	mov	w0, w19
	bl	flush_l2p_region
.L2312:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2311
.L2314:
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
	adrp	x0, .LC113
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 16
	add	x0, x0, :lo12:.LC113
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR0
	bl	printk
	add	x1, x21, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	mov	w19, 0
	ldrh	w2, [x1,1962]
	mov	w1, 0
	ldr	x0, [x0,2160]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2316:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,2104]
	cmp	w19, w0
	bcs	.L2331
	mov	w0, w19
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2317
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	add	x1, x20, :lo12:.LANCHOR2
	ldr	x2, [x1,2160]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2317:
	add	w19, w19, 1
	b	.L2316
.L2331:
	adrp	x23, .LC114
	mov	w19, 0
	add	x20, x20, :lo12:.LANCHOR2
	mov	w24, 65535
	add	x23, x23, :lo12:.LC114
.L2319:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,1960]
	cmp	w0, w19
	bls	.L2332
	ubfiz	x22, x19, 1, 16
	ldr	x0, [x20,-40]
	ldrh	w2, [x0,x22]
	ldr	x0, [x20,2160]
	ldrh	w3, [x0,x22]
	cmp	w2, w3
	beq	.L2322
	cmp	w2, w24
	beq	.L2322
	mov	x0, x23
	mov	w1, w19
	bl	printk
	ldrh	w0, [x20]
	cmp	w0, w19
	beq	.L2322
	ldrh	w0, [x20,96]
	cmp	w0, w19
	beq	.L2322
	ldrh	w0, [x20,48]
	cmp	w0, w19
	beq	.L2322
	ldr	x0, [x20,2160]
	ldrh	w1, [x0,x22]
	ldr	x0, [x20,-40]
	strh	w1, [x0,x22]
	mov	w0, w19
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2322:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2319
.L2332:
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
	beq	.L2334
	add	x1, x19, :lo12:.LANCHOR2
	ubfiz	x0, x21, 1, 16
	ldr	x1, [x1,-40]
	ldrh	w0, [x1,x0]
	cbz	w0, .L2335
	mov	w0, w21
	bl	INSERT_DATA_LIST
	b	.L2334
.L2335:
	mov	w0, w21
	bl	INSERT_FREE_LIST
.L2334:
	add	x2, x19, :lo12:.LANCHOR2
	strb	wzr, [x20,8]
	add	x0, x2, 48
	cmp	x20, x0
	beq	.L2336
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w1, [x0,1972]
	cmp	w1, 1
	beq	.L2336
	ldrb	w0, [x0,120]
	cbz	w0, .L2337
.L2336:
	mov	w0, 1
	strb	w0, [x20,8]
	b	.L2338
.L2337:
	cmp	x20, x2
	bne	.L2338
	cmp	w1, 3
	beq	.L2340
	ldr	w0, [x20,344]
	cmp	w0, 1
	bne	.L2341
.L2340:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 1
	strb	w1, [x0,8]
.L2341:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,160]
	cbz	w1, .L2338
	ldr	w1, [x0,220]
	cmp	w1, 29
	bhi	.L2338
	mov	w1, 1
	strb	w1, [x0,8]
.L2338:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x2,2320]
	cmp	w0, w1
	beq	.L2343
	cmp	w21, w0
	bne	.L2344
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,-40]
	ldrh	w1, [x2,x1]
	cbz	w1, .L2345
.L2344:
	bl	update_vpc_list
.L2345:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,2320]
.L2343:
	mov	x0, x20
	bl	allocate_data_superblock
	bl	l2p_flush
	bl	FtlVpcTblFlush
	mov	w0, 0
	bl	FtlEctTblFlush
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
	ldr	w4, [x3,2028]
	cbnz	w4, .L2495
	ldrh	w4, [x3,-24]
	cmp	w4, 47
	bls	.L2495
	mov	w24, w0
	adrp	x0, .LANCHOR1+3004
	mov	w22, w1
	ldrh	w1, [x0,#:lo12:.LANCHOR1+3004]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2360
.L2363:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,954]
	cmp	w1, w2
	bne	.L2361
	b	.L2362
.L2360:
	ldrh	w1, [x3,96]
	cmp	w1, w0
	beq	.L2363
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2363
	b	.L2495
.L2361:
	ldrh	w3, [x0,952]
	cmp	w3, w2
	bne	.L2362
	strh	w1, [x0,952]
	mov	w1, -1
	strh	w1, [x0,954]
.L2362:
	add	x20, x19, :lo12:.LANCHOR2
	cmp	w24, 1
	ldr	w0, [x20,868]
	add	w0, w0, 1
	add	w0, w0, w24, lsl 7
	str	w0, [x20,868]
	beq	.L2364
.L2366:
	mov	w21, 65535
	b	.L2365
.L2364:
	ldr	w1, [x20,160]
	cbz	w1, .L2366
	ldr	w1, [x20,220]
	cmp	w1, 29
	bhi	.L2366
	adrp	x23, .LANCHOR4
	add	x21, x23, :lo12:.LANCHOR4
	ldrh	w1, [x21,400]
	add	w0, w0, w1
	str	w0, [x20,868]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x20,296]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2366
	ldrh	w1, [x20,952]
	cmp	w1, w0
	bne	.L2366
	ldr	w0, [x20,868]
	cmp	w0, 1024
	bls	.L2366
	ldr	w0, [x20,220]
	str	wzr, [x20,868]
	strh	wzr, [x21,400]
	cbnz	w0, .L2367
	mov	w0, 6
	b	.L2517
.L2367:
	cmp	w0, 5
	bhi	.L2368
	mov	w0, 18
.L2517:
	strh	w0, [x21,400]
.L2368:
	mov	w0, 32
	mov	w27, 65535
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w27
	beq	.L2372
	add	x20, x19, :lo12:.LANCHOR2
	ldrh	w0, [x20,872]
	cbz	w0, .L2370
	adrp	x2, .LANCHOR0
	ldr	x26, [x20,-40]
	add	x2, x2, :lo12:.LANCHOR0
	ubfiz	x25, x25, 1, 16
	ldrh	w1, [x2,2028]
	ldrh	w2, [x2,1952]
	ldrh	w3, [x26,x25]
	mul	w1, w1, w2
	add	w1, w1, 1
	cmp	w3, w1
	bgt	.L2372
	add	w1, w0, 1
	str	wzr, [x20,876]
	uxth	w1, w1
	strh	w1, [x20,872]
	str	x1, [x29,120]
	bl	List_get_gc_head_node
	uxth	w21, w0
	cmp	w21, w27
	ldr	x1, [x29,120]
	beq	.L2372
	ubfiz	x27, x21, 1, 16
	adrp	x0, .LC115
	ldrh	w4, [x26,x25]
	add	x0, x0, :lo12:.LC115
	mov	w2, w21
	ldrh	w3, [x26,x27]
	bl	printk
	ldrh	w0, [x20,872]
	cmp	w0, 40
	bls	.L2371
	ldr	x0, [x20,-40]
	ldrh	w0, [x0,x27]
	cmp	w0, 32
	bls	.L2371
	strh	wzr, [x20,872]
.L2371:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 6
	strh	w0, [x23,400]
	b	.L2365
.L2370:
	mov	w0, 1
	strh	w0, [x20,872]
.L2372:
	bl	GetSwlReplaceBlock
	uxth	w21, w0
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2365
	add	x23, x23, :lo12:.LANCHOR4
	strh	wzr, [x23,400]
.L2365:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w1, [x20,296]
	cmp	w1, w0
	bne	.L2373
	ldrh	w0, [x20,96]
	cmp	w0, w1
	bne	.L2373
	ldrh	w25, [x20,952]
	cmp	w25, w0
	bne	.L2373
	ldrh	w0, [x20,-8]
	mov	w2, 1024
	ldr	w1, [x20,868]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2373
	adrp	x0, .LANCHOR4+400
	str	wzr, [x20,868]
	mov	x23, x20
	strh	wzr, [x0,#:lo12:.LANCHOR4+400]
	bl	GetSwlReplaceBlock
	uxth	w21, w0
	cmp	w21, w25
	bne	.L2375
	ldrh	w1, [x20,-8]
	ldrh	w0, [x20,866]
	cmp	w1, w0
	bcs	.L2376
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w21
	beq	.L2385
	ldr	w2, [x20,2024]
	uxtw	x1, w0
	adrp	x20, .LANCHOR0
	cbnz	w2, .L2378
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,1972]
	cmp	w2, 3
	beq	.L2378
	ldr	w2, [x23,344]
	cbnz	w2, .L2378
	ldr	w2, [x23,160]
	cbnz	w2, .L2378
	ldrb	w0, [x0,120]
	cbz	w0, .L2379
.L2378:
	add	x0, x19, :lo12:.LANCHOR2
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,-40]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x3,2028]
	ldrh	w0, [x3,1952]
	ldrh	w3, [x3,1972]
	mul	w2, w1, w0
	cmp	w3, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w2, w0
	cmp	w4, w0
	bgt	.L2381
	mov	w0, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	List_get_gc_head_node
	uxth	w21, w0
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x20,2104]
	ldr	w2, [x1,156]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2382
	mov	w0, 128
	b	.L2518
.L2382:
	mov	w0, 160
.L2518:
	strh	w0, [x1,866]
	b	.L2383
.L2381:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L2519
.L2379:
	ldr	x2, [x23,-40]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2384
	bl	List_get_gc_head_node
	uxth	w21, w0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0,866]
	b	.L2383
.L2384:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L2519:
	strh	w1, [x0,866]
	b	.L2385
.L2376:
	mov	w0, 80
	strh	w0, [x20,866]
	b	.L2385
.L2383:
	mov	w0, 65535
	cmp	w21, w0
	beq	.L2385
.L2375:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x21, 1, 16
	adrp	x0, .LC116
	mov	w1, w21
	add	x0, x0, :lo12:.LC116
	ldr	x3, [x5,-40]
	ldr	x6, [x5,-80]
	ldrh	w2, [x5,-8]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,864]
	ldrh	w4, [x6,x4]
	bl	printk
.L2385:
	bl	FtlGcReFreshBadBlk
.L2373:
	mov	w0, 65535
	cmp	w21, w0
	cset	w1, eq
	cbz	w1, .L2386
	cbnz	w24, .L2386
	add	x0, x19, :lo12:.LANCHOR2
	mov	w20, 1
	ldrh	w1, [x0,-8]
	cmp	w1, 24
	bhi	.L2387
	adrp	x0, .LANCHOR0
	cmp	w1, 16
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w20, [x0,2026]
	bls	.L2388
	lsr	w20, w20, 5
	b	.L2387
.L2388:
	cmp	w1, 12
	bls	.L2389
	lsr	w20, w20, 4
	b	.L2387
.L2389:
	cmp	w1, 8
	bls	.L2387
	lsr	w20, w20, 2
.L2387:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,864]
	cmp	w2, w1
	bcs	.L2391
	ldrh	w1, [x0,96]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L2392
	ldrh	w2, [x0,952]
	cmp	w2, w1
	bne	.L2392
	adrp	x1, .LANCHOR4+400
	ldrh	w2, [x1,#:lo12:.LANCHOR4+400]
	cbnz	w2, .L2393
	adrp	x1, .LANCHOR0+2104
	ldr	w3, [x0,156]
	ldr	w1, [x1,#:lo12:.LANCHOR0+2104]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2394
.L2393:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,272]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,864]
	b	.L2395
.L2394:
	mov	w1, 18
	strh	w1, [x0,864]
.L2395:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,876]
	b	.L2495
.L2392:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,272]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,864]
.L2391:
	cmp	w22, 2
	bhi	.L2450
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,160]
	cbz	w0, .L2450
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2450
.L2386:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x20,96]
	cmp	w0, w2
	bne	.L2398
	cbz	w1, .L2398
	ldrh	w1, [x20,952]
	cmp	w1, w0
	bne	.L2398
	ldrh	w1, [x20,296]
	cmp	w1, w0
	beq	.L2399
.L2404:
	mov	w21, 65535
	b	.L2398
.L2399:
	ldrh	w23, [x20,-8]
	adrp	x21, .LANCHOR4
	ldrh	w0, [x20,864]
	str	wzr, [x20,876]
	cmp	w0, w23
	bcs	.L2400
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w0, [x0,400]
	cbnz	w0, .L2401
	adrp	x0, .LANCHOR0+2104
	ldr	w1, [x20,156]
	ldr	w0, [x0,#:lo12:.LANCHOR0+2104]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bcs	.L2402
.L2401:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,272]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x19,864]
	b	.L2403
.L2402:
	mov	w0, 18
	strh	w0, [x20,864]
.L2403:
	add	x21, x21, :lo12:.LANCHOR4
	bl	FtlReadRefresh
	ldrh	w2, [x21,400]
	b	.L2495
.L2400:
	add	x22, x21, :lo12:.LANCHOR4
	ldrh	w0, [x22,400]
	cbnz	w0, .L2404
	ldrh	w21, [x20,272]
	add	w1, w21, w21, lsl 1
	asr	w1, w1, 2
	strh	w1, [x20,864]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x20,-40]
	ldrh	w2, [x1,x0]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1,2028]
	ldrh	w1, [x1,1952]
	mul	w0, w0, w1
	mov	w1, 2
	sdiv	w0, w0, w1
	cmp	w2, w0
	ble	.L2405
	sub	w21, w21, #1
	cmp	w23, w21
	blt	.L2405
	bl	FtlReadRefresh
	ldrh	w2, [x22,400]
	b	.L2495
.L2405:
	cbnz	w2, .L2404
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,-8]
	add	w2, w2, 1
	b	.L2495
.L2398:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w20, 2
	ldr	w0, [x0,160]
	cmp	w0, wzr
	csinc	w20, w20, wzr, ne
	b	.L2397
.L2450:
	mov	w21, 65535
.L2397:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,296]
	cmp	w1, w2
	bne	.L2407
	cmp	w21, w1
	beq	.L2408
	strh	w21, [x0,296]
	b	.L2409
.L2408:
	ldrh	w1, [x0,952]
	cmp	w1, w21
	beq	.L2409
	strh	w1, [x0,296]
	mov	w1, -1
	strh	w1, [x0,952]
.L2409:
	adrp	x1, .LANCHOR0+120
	add	x0, x19, :lo12:.LANCHOR2
	add	x22, x0, 296
	ldrb	w1, [x1,#:lo12:.LANCHOR0+120]
	strb	wzr, [x0,304]
	cbz	w1, .L2410
	ldrh	w0, [x0,296]
	bl	ftl_get_blk_mode
	strb	w0, [x22,8]
.L2410:
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,296]
	bl	IsBlkInGcList
	cbz	w0, .L2411
	mov	w0, -1
	strh	w0, [x22,296]
.L2411:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x23, x22, 296
	ldrh	w1, [x22,296]
	cmp	w1, w0
	beq	.L2407
	mov	x0, x23
	bl	make_superblock
	strh	wzr, [x22,298]
	adrp	x0, .LANCHOR4
	ldrh	w2, [x22,296]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x1, [x22,-40]
	strb	wzr, [x22,302]
	strh	wzr, [x0,402]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,404]
.L2407:
	add	x3, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,#:lo12:.LANCHOR2]
	ldrh	w0, [x3,296]
	cmp	w1, w0
	beq	.L2413
	ldrh	w1, [x3,48]
	cmp	w1, w0
	beq	.L2413
.L2414:
	mov	x28, x3
	mov	w24, 65535
	b	.L2415
.L2413:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,296]
	b	.L2520
.L2435:
	ldrh	w0, [x28,298]
	add	w20, w20, w0
	uxth	w20, w20
	strh	w20, [x28,298]
	cmp	w20, w25
	adrp	x20, .LANCHOR4
	bcs	.L2437
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w1, [x0,402]
	ldrh	w0, [x0,404]
	cmp	w1, w0
	beq	.L2437
.L2438:
	ldrh	w0, [x28,-8]
	cmp	w0, 2
	bhi	.L2441
	add	x23, x23, :lo12:.LANCHOR0
	ldrh	w20, [x23,2026]
.L2415:
	ldrh	w0, [x28,296]
	cmp	w0, w24
	bne	.L2416
	str	wzr, [x28,876]
.L2417:
	ldrh	w22, [x28,872]
	mov	w0, w22
	bl	List_get_gc_head_node
	uxth	w23, w0
	cmp	w23, w24
	strh	w23, [x28,296]
	bne	.L2418
	strh	wzr, [x28,872]
	mov	w2, 8
	b	.L2495
.L2418:
	mov	w0, w23
	add	w22, w22, 1
	bl	IsBlkInGcList
	cbz	w0, .L2419
	strh	w22, [x28,872]
	b	.L2417
.L2419:
	adrp	x0, .LANCHOR0
	ubfiz	x1, x23, 1, 16
	add	x5, x0, :lo12:.LANCHOR0
	ldr	x2, [x28,-40]
	uxth	w22, w22
	mov	w3, 2
	strh	w22, [x28,872]
	ldrh	w0, [x5,2026]
	ldrh	w5, [x5,1952]
	ldrh	w4, [x2,x1]
	mul	w0, w0, w5
	sdiv	w5, w0, w3
	cmp	w4, w5
	bgt	.L2421
	cmp	w4, 8
	bls	.L2422
	cmp	w22, 48
	bls	.L2422
	ldrh	w4, [x28,924]
	cmp	w4, 35
	bhi	.L2422
.L2421:
	strh	wzr, [x28,872]
.L2422:
	ldrh	w1, [x2,x1]
	cmp	w1, w0
	blt	.L2423
	cmp	w21, w24
	bne	.L2423
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x19,872]
	strh	w0, [x19,296]
.L2520:
	adrp	x0, .LANCHOR4+400
	ldrh	w2, [x0,#:lo12:.LANCHOR4+400]
	b	.L2495
.L2423:
	cbnz	w1, .L2424
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x28,872]
	add	w0, w0, 1
	strh	w0, [x28,872]
	b	.L2417
.L2424:
	adrp	x0, .LANCHOR0
	strb	wzr, [x28,304]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbz	w0, .L2425
	mov	w0, w23
	bl	ftl_get_blk_mode
	strb	w0, [x28,304]
.L2425:
	add	x22, x28, 296
	mov	x0, x22
	bl	make_superblock
	adrp	x1, .LANCHOR4
	ldrh	w2, [x28,296]
	add	x1, x1, :lo12:.LANCHOR4
	ldr	x0, [x28,-40]
	strh	wzr, [x1,402]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,404]
	strh	wzr, [x28,298]
	strb	wzr, [x28,302]
.L2416:
	bl	FtlReadRefresh
	adrp	x23, .LANCHOR0
	mov	w0, 1
	str	w0, [x28,2028]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,120]
	ldrh	w25, [x0,2026]
	cbz	w1, .L2426
	ldrb	w1, [x28,304]
	cmp	w1, 1
	bne	.L2426
	ldrh	w25, [x0,2028]
.L2426:
	ldrh	w0, [x28,298]
	add	w1, w0, w20
	cmp	w1, w25
	ble	.L2427
	sub	w20, w25, w0
	uxth	w20, w20
.L2427:
	mov	w26, 0
.L2428:
	cmp	w20, w26, uxth
	bls	.L2435
	add	x1, x28, 296
	add	x0, x23, :lo12:.LANCHOR0
	ldrh	w4, [x1,2]
	ldrh	w6, [x0,1952]
	mov	x0, 0
	add	w4, w4, w26
	mov	w22, w0
.L2436:
	cmp	w6, w0, uxth
	bls	.L2521
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,16]
	cmp	w2, w24
	beq	.L2429
	mov	w3, 56
	ldr	x7, [x28,912]
	orr	w2, w4, w2, lsl 10
	umull	x5, w22, w3
	add	w22, w22, 1
	add	x5, x7, x5
	uxth	w22, w22
	str	w2, [x5,4]
.L2429:
	add	x0, x0, 1
	b	.L2436
.L2521:
	ldr	x0, [x28,912]
	mov	w1, w22
	ldrb	w2, [x28,304]
	mov	x27, 0
	bl	FlashReadPages
.L2431:
	cmp	w22, w27, uxth
	bls	.L2522
	mov	x0, 56
	mul	x5, x27, x0
	ldr	x0, [x28,912]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	cmn	w0, #1
	ldr	x4, [x1,16]
	beq	.L2452
	ldrh	w0, [x4]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2452
	ldr	w0, [x4,8]
	add	x1, x29, 140
	mov	w2, 0
	str	x5, [x29,112]
	str	x4, [x29,120]
	bl	log2phys
	ldr	x2, [x28,912]
	ldr	x5, [x29,112]
	ldr	w0, [x29,140]
	add	x2, x2, x5
	ldr	x4, [x29,120]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L2452
	adrp	x0, .LANCHOR4
	mov	x3, 56
	add	x1, x0, :lo12:.LANCHOR4
	str	x5, [x29,104]
	str	x4, [x29,112]
	ldrh	w0, [x1,402]
	add	w0, w0, 1
	strh	w0, [x1,402]
	ldr	w0, [x28,880]
	ldr	x1, [x28,2056]
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
	ldr	w0, [x28,880]
	ldr	x1, [x28,2056]
	madd	x0, x0, x2, x1
	ldr	x1, [x28,912]
	add	x1, x1, x5
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	add	x2, x28, 96
	str	x2, [x29,120]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,140]
	str	w0, [x4,12]
	ldrh	w0, [x28,96]
	strh	w0, [x4,2]
	ldr	w0, [x28,200]
	str	w0, [x4,4]
	ldr	w0, [x28,880]
	add	w0, w0, 1
	str	w0, [x28,880]
	ldr	x0, [x28,912]
	add	x0, x0, x5
	bl	FtlGcBufAlloc
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,120]
	cbnz	w0, .L2433
	ldr	x2, [x29,120]
	ldr	w1, [x28,880]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2433
	ldrh	w0, [x2,4]
	cbnz	w0, .L2452
.L2433:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2452
	adrp	x0, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x19,2028]
	ldrh	w2, [x0,400]
	b	.L2495
.L2452:
	add	x27, x27, 1
	b	.L2431
.L2522:
	add	w26, w26, 1
	b	.L2428
.L2437:
	ldr	w0, [x28,880]
	cbz	w0, .L2439
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2439
	add	x20, x20, :lo12:.LANCHOR4
	str	wzr, [x28,2028]
	ldrh	w2, [x20,400]
	b	.L2495
.L2439:
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w2, [x0,402]
	cbnz	w2, .L2440
	ldrh	w0, [x28,296]
	ldr	x1, [x28,-40]
	lsl	x0, x0, 1
	ldrh	w4, [x1,x0]
	cbz	w4, .L2440
	strh	w2, [x1,x0]
	ldrh	w0, [x28,296]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2440:
	mov	w0, -1
	strh	w0, [x28,296]
	b	.L2438
.L2441:
	add	x20, x20, :lo12:.LANCHOR4
	str	wzr, [x28,2028]
	ldrh	w2, [x20,400]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
.L2495:
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
	ldr	w1, [x0,2088]
	cbz	w1, .L2524
	ldrb	w0, [x0,120]
	mov	w22, 0
	cbz	w0, .L2525
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w22, eq
.L2525:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x20,9]
	adrp	x24, .LC117
	mov	w2, w22
	mov	w23, 0
	mov	w25, 56
	ldr	x0, [x0,2096]
	adrp	x26, .LANCHOR2
	add	x24, x24, :lo12:.LC117
	bl	FlashProgPages
.L2526:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2088]
	cmp	w23, w1
	bcs	.L2543
	umull	x21, w23, w25
	ldr	x1, [x0,2096]
	add	x0, x1, x21
	ldr	w1, [x1,x21]
	cmn	w1, #1
	beq	.L2546
	ldr	w1, [x0,4]
	cbz	w22, .L2560
	orr	w1, w1, -2147483648
.L2560:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,108]
	add	x1, x29, 108
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2096]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2530
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x26, :lo12:.LANCHOR2
	ubfiz	x0, x1, 1, 16
	mov	w21, w1
	ldr	x2, [x2,-40]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2531
	mov	x0, x24
	bl	printk
.L2531:
	mov	w0, w21
	bl	decrement_vpc_count
.L2530:
	add	w23, w23, 1
	b	.L2526
.L2562:
	mov	w20, 16386
	adrp	x21, .LANCHOR2
.L2542:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w0, [x0,956]
	cbz	w0, .L2543
	mov	w0, 1
	mov	w1, w0
	bl	rk_ftl_garbage_collect
	subs	w20, w20, #1
	bne	.L2542
.L2543:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19,2088]
	b	.L2524
.L2546:
	adrp	x23, .LANCHOR2
	adrp	x25, .LC117
	mov	w24, 0
	mov	w26, 56
	add	x23, x23, :lo12:.LANCHOR2
	add	x25, x25, :lo12:.LC117
.L2527:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0,2088]
	cmp	w24, w1
	bcs	.L2562
	umull	x21, w24, w26
	ldr	x0, [x0,2096]
	mov	w1, -1
	str	w1, [x0,x21]
.L2533:
	add	x27, x19, :lo12:.LANCHOR0
	ldr	x0, [x27,2096]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	ldr	w0, [x1,4]
	bne	.L2563
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	bne	.L2534
	ldr	x2, [x23,-40]
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x20,4]
	ldrh	w0, [x2,x1]
	sub	w0, w0, w3
	strh	w0, [x2,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x27,2026]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L2534:
	ldrh	w0, [x20,4]
	cbnz	w0, .L2535
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2535:
	ldr	w0, [x23,448]
	add	x27, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x23,448]
	ldr	x0, [x27,2096]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,108]
	ldr	x1, [x27,2096]
	mov	w2, w22
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x27,2096]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	b	.L2533
.L2563:
	cbz	w22, .L2561
	orr	w0, w0, -2147483648
.L2561:
	str	w0, [x29,108]
	mov	w2, 1
	ldr	w0, [x1,24]
	add	x1, x29, 108
	bl	log2phys
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2096]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2539
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x23,-40]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2540
	mov	x0, x25
	bl	printk
.L2540:
	mov	w0, w21
	bl	decrement_vpc_count
.L2539:
	add	w24, w24, 1
	b	.L2527
.L2524:
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
	bl	FtlVpcTblFlush
	mov	w0, 1
	bl	FtlEctTblFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	adrp	x0, .LANCHOR1+532
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR1+532]
	cmp	w0, 1
	bne	.L2566
	bl	FtlSysFlush
.L2566:
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
	ldr	w1, [x21,2068]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L2569
	cmp	w19, 31
	bls	.L2583
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,2032]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L2571
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L2571:
	mov	w0, -1
	adrp	x20, .LANCHOR4
	str	w0, [x29,76]
	adrp	x23, .LANCHOR2
.L2572:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,2032]
	cmp	w19, w0
	bcc	.L2584
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L2573
	add	x1, x20, :lo12:.LANCHOR4
	mov	w2, 1
	ldr	w0, [x1,416]
	add	w0, w0, 1
	str	w0, [x1,416]
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
.L2573:
	add	x0, x22, :lo12:.LANCHOR0
	add	w21, w21, 1
	ldrh	w0, [x0,2032]
	sub	w19, w19, w0
	b	.L2572
.L2584:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,416]
	cmp	w1, 32
	bls	.L2583
	str	wzr, [x0,416]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2583:
	mov	w0, 0
.L2569:
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
	str	x25, [sp,64]
	add	x2, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR0
	ldrh	w20, [x2,96]
	add	x1, x21, :lo12:.LANCHOR0
	cmp	w20, w3
	ldrh	w1, [x1,2026]
	bne	.L2586
.L2596:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x22, x20, 96
	ldrh	w1, [x20,96]
	str	wzr, [x20,2328]
	cmp	w1, w0
	beq	.L2614
	add	x21, x21, :lo12:.LANCHOR0
	bl	FtlCacheWriteBack
	ldrb	w0, [x22,7]
	mov	w25, 12
	ldr	x1, [x20,-40]
	ldrh	w3, [x21,2026]
	mov	w21, 0
	ldrh	w2, [x20,96]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldr	w0, [x20,184]
	ldrh	w1, [x20,936]
	add	w0, w1, w0
	str	w0, [x20,184]
	b	.L2597
.L2586:
	cbz	w0, .L2589
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w4, [x0,3004]
	cmp	w4, w3
	beq	.L2590
.L2591:
	mov	w1, 2
	b	.L2589
.L2590:
	strh	wzr, [x0,3004]
	ldrh	w0, [x2,-8]
	cmp	w0, 17
	bhi	.L2591
.L2589:
	add	x22, x19, :lo12:.LANCHOR2
	add	x0, x22, 96
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L2592
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x22,-80]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L2593
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2593:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2328]
	cbnz	w1, .L2594
	ldr	w1, [x0,448]
	add	w1, w1, 1
	str	w1, [x0,448]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2594:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	str	wzr, [x19,2328]
	b	.L2595
.L2592:
	adrp	x0, .LANCHOR1+3004
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3004]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2595
	b	.L2596
.L2600:
	ldr	x24, [x20,944]
	add	x1, x29, 92
	umull	x23, w21, w25
	mov	w2, 0
	add	x22, x24, x23
	ldr	w0, [x22,8]
	bl	log2phys
	ldr	w0, [x29,92]
	ldr	w1, [x24,x23]
	cmp	w0, w1
	bne	.L2598
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w23, w0
	ldr	w0, [x22,8]
	add	x1, x22, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w23
.L2613:
	bl	decrement_vpc_count
.L2599:
	add	w21, w21, 1
	uxth	w21, w21
.L2597:
	ldrh	w0, [x20,936]
	cmp	w0, w21
	bhi	.L2600
	b	.L2615
.L2598:
	ldr	w1, [x22,4]
	cmp	w0, w1
	beq	.L2599
	ldrh	w0, [x20,96]
	b	.L2613
.L2615:
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x20,96]
	ldr	x2, [x20,-40]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2,x1]
	cbz	w1, .L2601
	bl	INSERT_DATA_LIST
	b	.L2602
.L2601:
	bl	INSERT_FREE_LIST
.L2602:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x20,936]
	strh	w0, [x20,96]
	strh	wzr, [x20,924]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	w0, [x20,160]
	cbz	w0, .L2603
	ldr	w0, [x20,220]
	cmp	w0, 29
	bhi	.L2603
	ldrh	w0, [x20,272]
	ldrh	w1, [x20,-8]
	cmp	w1, w0
	bcs	.L2604
	lsl	w0, w0, 1
	strh	w0, [x20,864]
.L2604:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,296]
	b	.L2614
.L2603:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,272]
	ldrh	w1, [x19,-8]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	ble	.L2614
	mov	w0, -1
	strh	w0, [x19,296]
	mov	w0, 20
	strh	w0, [x19,864]
.L2614:
	mov	w0, 0
.L2595:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
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
	cbz	w0, .L2617
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w1, [x0,880]
	tbz	x1, 0, .L2617
	ldrh	w0, [x0,100]
	cbz	w0, .L2617
.L2622:
	mov	w0, 0
	b	.L2618
.L2617:
	add	x1, x21, :lo12:.LANCHOR2
	mov	w2, 0
	mov	w3, w2
	mov	w20, 0
	mov	w22, 56
	ldr	x0, [x1,2056]
	ldr	w1, [x1,880]
	bl	FlashProgPages
.L2619:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w1, [x19,880]
	cmp	w20, w1
	bcs	.L2633
	umull	x0, w20, w22
	ldr	x2, [x19,2056]
	add	x1, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L2620
	ldrh	w3, [x19,96]
	ldr	x1, [x19,-40]
	strh	wzr, [x1,x3,lsl 1]
	strh	w2, [x19,96]
	ldr	w1, [x19,448]
	add	w1, w1, 1
	str	w1, [x19,448]
	ldr	x1, [x19,2056]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2632
.L2620:
	ldr	x2, [x1,16]
	add	w20, w20, 1
	ldr	w1, [x1,4]
	uxth	w20, w20
	ldr	w0, [x2,12]
	ldr	w2, [x2,8]
	bl	FtlGcUpdatePage
	b	.L2619
.L2633:
	ldr	x0, [x19,2056]
	bl	FtlGcBufFree
	str	wzr, [x19,880]
	ldrh	w0, [x19,100]
	cbnz	w0, .L2622
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L2632:
	mov	w0, 1
.L2618:
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
	ldrh	w1, [x20,2026]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,98]
	ldrh	w0, [x20,2026]
	cmp	w1, w0
	bcc	.L2634
	add	x0, x19, 2240
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19,2328]
.L2634:
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
	adrp	x0, .LANCHOR4
	add	x29, sp, 0
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	strh	wzr, [x0,266]
	str	wzr, [x0,268]
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
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	stp	x21, x22, [sp,32]
	add	x21, x23, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ldrh	w0, [x21,1956]
	add	x19, x20, :lo12:.LANCHOR2
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x19,280]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2638
.L2640:
	mov	w22, -1
	b	.L2639
.L2638:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L2640
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	ldrh	w2, [x21,2066]
	mov	x1, 0
	ldr	x0, [x19,144]
.L2641:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2645
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2641
.L2645:
	add	x0, x20, :lo12:.LANCHOR2
	cmp	w3, w2
	adrp	x24, .LANCHOR4
	ldrh	w1, [x0,252]
	add	w1, w1, 1
	strh	w1, [x0,252]
	blt	.L2642
	add	x0, x24, :lo12:.LANCHOR4
	ldrh	w0, [x0,266]
	cbz	w0, .L2646
.L2642:
	add	x19, x20, :lo12:.LANCHOR2
	mov	x0, x19
	add	x21, x19, 48
	bl	FtlSuperblockPowerLostFix
	mov	x0, x21
	bl	FtlSuperblockPowerLostFix
	ldrh	w0, [x20,#:lo12:.LANCHOR2]
	ldr	x2, [x19,-40]
	lsl	x0, x0, 1
	ldrh	w3, [x19,4]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	add	x2, x23, :lo12:.LANCHOR0
	strb	wzr, [x19,6]
	ldr	x3, [x19,-40]
	strh	wzr, [x19,4]
	ldrh	w0, [x2,2026]
	strh	w0, [x19,2]
	ldrh	w0, [x19,48]
	ldrh	w4, [x19,52]
	lsl	x0, x0, 1
	ldrh	w1, [x3,x0]
	sub	w1, w1, w4
	strh	w1, [x3,x0]
	strb	wzr, [x19,54]
	ldrh	w0, [x2,2026]
	strh	w0, [x19,50]
	add	x0, x19, 2240
	strh	wzr, [x19,52]
	bl	FtlMapBlkWriteDumpData
	add	x0, x24, :lo12:.LANCHOR4
	add	x0, x0, 200
	bl	FtlMapBlkWriteDumpData
	ldrh	w0, [x19,254]
	add	w0, w0, 1
	strh	w0, [x19,254]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2646:
	mov	w0, 1
	add	x19, x20, :lo12:.LANCHOR2
	bl	FtlUpdateVaildLpn
	ldrh	w0, [x20,#:lo12:.LANCHOR2]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2647
	ldrh	w1, [x19,4]
	cbnz	w1, .L2647
	ldrh	w1, [x19,52]
	add	x21, x19, 48
	cbnz	w1, .L2647
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,48]
	bl	FtlGcRefreshBlock
	mov	x0, x19
	bl	allocate_new_data_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
	add	x0, x24, :lo12:.LANCHOR4
	add	x0, x0, 200
	bl	FtlMapBlkWriteDumpData
.L2647:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x20,252]
	and	w0, w0, 31
	cbnz	w0, .L2639
	bl	FtlVpcCheckAndModify
.L2639:
	mov	w0, w22
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlSysBlkInit, .-FtlSysBlkInit
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
	str	w0, [x19,532]
	adrp	x0, .LANCHOR4+420
	str	wzr, [x0,#:lo12:.LANCHOR4+420]
	adrp	x0, .LC72
	add	x0, x0, :lo12:.LC72
	bl	printk
	mov	x0, x20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	adrp	x0, .LANCHOR0+1956
	ldrh	w0, [x0,#:lo12:.LANCHOR0+1956]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	w0, .L2655
	bl	FtlSysBlkInit
	cbnz	w0, .L2655
	mov	w0, 1
	str	w0, [x19,532]
.L2655:
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
	add	x0, x19, 32
	str	xzr, [x19,32]
	str	xzr, [x19,432]
	bl	rknand_get_reg_addr
	ldr	x1, [x19,32]
	mov	w0, -1
	cbz	x1, .L2658
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x19,424]
	mov	w2, w1
	mov	w3, 2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	x0, [x19,32]
	bl	FlashInit
	mov	w19, w0
	cbnz	w0, .L2659
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 168
	bl	FtlInit
.L2659:
	adrp	x0, .LC118
	mov	w1, w19
	add	x0, x0, :lo12:.LC118
	bl	printk
	mov	w0, w19
.L2658:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rk_ftl_init, .-rk_ftl_init
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
	beq	.L2663
	ldrh	w0, [x1,4]
	cbnz	w0, .L2664
.L2663:
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
	bl	FtlVpcTblFlush
	mov	w0, 0
	bl	FtlEctTblFlush
.L2664:
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
	bne	.L2667
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L2668
.L2667:
	adrp	x21, .LANCHOR0
	add	w0, w1, w2
	add	x1, x21, :lo12:.LANCHOR0
	str	w0, [x29,152]
	mov	w0, -1
	ldr	w3, [x29,152]
	ldr	w2, [x1,2068]
	cmp	w3, w2
	bhi	.L2668
	ldrh	w0, [x1,2032]
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
	cbz	w0, .L2669
	bl	FtlCacheWriteBack
.L2669:
	mov	w27, 0
	ldr	w22, [x29,156]
	str	w27, [x29,140]
	mov	w23, w27
	str	w27, [x29,136]
.L2670:
	cbz	w24, .L2712
	mov	w0, w22
	add	x1, x29, 172
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,172]
	cmn	w3, #1
	bne	.L2709
	mov	w3, 0
.L2671:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2032]
	cmp	w3, w0
	bcs	.L2675
	madd	w0, w22, w0, w3
	cmp	w0, w20
	bcc	.L2673
	ldr	w1, [x29,152]
	cmp	w0, w1
	bcs	.L2673
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x26, x0
	str	x3, [x29,144]
	bl	ftl_memset
	ldr	x3, [x29,144]
.L2673:
	add	w3, w3, 1
	b	.L2671
.L2709:
	mov	w0, 56
	umull	x1, w23, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x0,2048]
	add	x2, x2, x1
	str	w3, [x2,4]
	ldr	w2, [x29,156]
	cmp	w22, w2
	ldr	x2, [x0,2048]
	add	x2, x2, x1
	bne	.L2676
	ldr	x0, [x0,2096]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2032]
	udiv	w3, w20, w0
	msub	w3, w3, w0, w20
	str	w3, [x29,112]
	sub	w3, w0, w3
	cmp	w3, w28
	csel	w3, w3, w28, ls
	str	w3, [x29,140]
	cmp	w3, w0
	bne	.L2677
	str	x26, [x2,8]
	b	.L2677
.L2676:
	cmp	w22, w25
	bne	.L2678
	ldr	x0, [x0,2104]
	str	x0, [x2,8]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w4, [x29,152]
	ldrh	w3, [x0,2032]
	mul	w0, w22, w3
	sub	w27, w4, w0
	cmp	w27, w3
	bne	.L2677
	b	.L2710
.L2678:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2032]
	mul	w0, w22, w0
.L2710:
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x26, x0
	str	x0, [x2,8]
.L2677:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2048]
	ldr	x2, [x2,2120]
	add	x1, x0, x1
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,2038]
	str	w22, [x1,24]
	mul	w0, w23, w0
	add	w23, w23, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L2675:
	subs	w24, w24, #1
	add	w22, w22, 1
	beq	.L2679
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,1952]
	cmp	w23, w0, lsl 3
	bne	.L2670
.L2679:
	cbz	w23, .L2670
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, 0
	ldr	x0, [x0,2048]
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
.L2681:
	ldr	w0, [x29,144]
	cmp	w23, w0
	bls	.L2713
	ldr	x0, [x29,144]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	ldr	w4, [x29,156]
	mul	x3, x0, x1
	ldr	x0, [x2,2048]
	add	x0, x0, x3
	ldr	w1, [x0,24]
	cmp	w1, w4
	bne	.L2682
	ldr	x1, [x0,8]
	ldr	x0, [x2,2096]
	cmp	x1, x0
	bne	.L2683
	ldr	x2, [x29,128]
	mov	x0, x26
	str	x3, [x29,96]
	add	x1, x1, x2
	ldr	w2, [x29,108]
	b	.L2711
.L2682:
	cmp	w1, w25
	bne	.L2683
	ldr	x1, [x0,8]
	ldr	x0, [x2,2104]
	cmp	x1, x0
	bne	.L2683
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,104]
	str	x3, [x29,96]
	ldrh	w0, [x0,2032]
	mul	w0, w25, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x26, x0
.L2711:
	bl	ftl_memcpy
	ldr	x3, [x29,96]
.L2683:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,2048]
	add	x5, x1, x3
	ldr	w4, [x1,x3]
	cmn	w4, #1
	bne	.L2684
	ldr	w2, [x0,424]
	str	w4, [x29,136]
	add	w2, w2, 1
	str	w2, [x0,424]
.L2684:
	ldr	w0, [x1,x3]
	cmp	w0, 256
	bne	.L2685
	ldr	w0, [x5,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2685:
	ldr	x0, [x29,144]
	add	x0, x0, 1
	str	x0, [x29,144]
	b	.L2681
.L2713:
	mov	w23, 0
	b	.L2670
.L2712:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,956]
	cbz	w0, .L2688
	mov	w0, w24
	mov	w1, 1
	bl	rk_ftl_garbage_collect
.L2688:
	ldr	w0, [x29,136]
.L2668:
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
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	cmp	w0, 16
	mov	w23, w1
	mov	w24, w2
	mov	x25, x3
	bne	.L2715
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartWrite
	b	.L2716
.L2715:
	adrp	x22, .LANCHOR0
	add	w27, w1, w2
	add	x2, x22, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	w1, [x2,2068]
	cmp	w27, w1
	bhi	.L2716
	adrp	x5, .LANCHOR4
	ldrh	w1, [x2,2032]
	add	x4, x5, :lo12:.LANCHOR4
	sub	w27, w27, #1
	mov	w0, 2048
	adrp	x20, .LANCHOR2
	udiv	w26, w23, w1
	cmp	w24, w1, lsl 1
	str	w0, [x4,440]
	str	x5, [x29,152]
	udiv	w0, w27, w1
	str	w0, [x29,180]
	sub	w28, w0, w26
	add	w21, w28, 1
	add	x0, x20, :lo12:.LANCHOR2
	add	x19, x0, 48
	ldr	w3, [x0,172]
	add	w3, w21, w3
	str	w3, [x0,172]
	ldr	w3, [x0,188]
	add	w3, w24, w3
	str	w3, [x0,188]
	cset	w3, cs
	str	w3, [x29,204]
	ldr	w3, [x2,2088]
	cmp	w24, 8
	csel	x19, x0, x19, hi
	cbz	w3, .L2718
	sub	w3, w3, #1
	mov	w5, 56
	ldr	x2, [x2,2096]
	umull	x3, w3, w5
	add	x3, x2, x3
	ldr	w2, [x3,24]
	cmp	w26, w2
	bne	.L2719
	ldr	w2, [x0,176]
	str	x4, [x29,192]
	add	w2, w2, 1
	str	w2, [x0,176]
	ldr	w0, [x4,444]
	add	w0, w0, 1
	str	w0, [x4,444]
	msub	w0, w26, w1, w23
	sub	w1, w1, w0
	ubfiz	x0, x0, 9, 23
	cmp	w1, w24
	csel	w19, w1, w24, ls
	ldr	x1, [x3,8]
	lsl	w21, w19, 9
	add	x0, x1, x0
	mov	w2, w21
	mov	x1, x25
	bl	ftl_memcpy
	ldr	x4, [x29,192]
	cbnz	w28, .L2720
	ldr	w1, [x4,444]
	mov	w0, w28
	cmp	w1, 2
	ble	.L2716
.L2720:
	add	x25, x25, x21
	sub	w24, w24, w19
	add	w23, w23, w19
	add	w26, w26, 1
	mov	w21, w28
.L2719:
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x19, [x0,408]
	str	wzr, [x0,444]
.L2718:
	ldr	w1, [x29,180]
	mov	w0, w26
	bl	FtlCacheMetchLpa
	cbz	w0, .L2721
	bl	FtlCacheWriteBack
.L2721:
	ldr	x0, [x29,152]
	add	x20, x20, :lo12:.LANCHOR2
	str	w26, [x29,200]
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [x0,408]
	adrp	x0, .LC119
	add	x0, x0, :lo12:.LC119
	str	x0, [x29,112]
.L2722:
	cbz	w21, .L2780
	ldrh	w0, [x19,4]
	cbnz	w0, .L2723
	cmp	x19, x20
	bne	.L2724
	add	x0, x20, 48
	ldrh	w19, [x0,4]
	cbnz	w19, .L2725
	bl	allocate_new_data_superblock
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	str	w19, [x0,3008]
.L2725:
	mov	x0, x20
	add	x19, x20, 48
	bl	allocate_new_data_superblock
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldr	w0, [x0,3008]
	cbnz	w0, .L2726
.L2727:
	mov	x19, x20
	b	.L2726
.L2724:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	str	w0, [x1,3008]
	ldrh	w0, [x20,4]
	cbnz	w0, .L2727
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2726:
	ldrh	w0, [x19,4]
	cbnz	w0, .L2728
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2728:
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [x0,408]
.L2723:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w0, [x20,2040]
	ldrh	w1, [x19,4]
	str	xzr, [x29,192]
	ldr	w2, [x2,2088]
	sub	w0, w0, w2
	cmp	w1, w0
	csel	w0, w1, w0, ls
	cmp	w0, w21
	csel	w0, w0, w21, ls
	str	w0, [x29,148]
	ldr	w0, [x29,204]
	and	w0, w0, 1
	str	w0, [x29,124]
	add	w0, w24, w23
	str	w0, [x29,144]
.L2729:
	ldr	w0, [x29,192]
	ldr	w2, [x29,148]
	ldr	w1, [x29,200]
	cmp	w0, w2
	add	w27, w1, w0
	bcs	.L2781
	ldrh	w1, [x19,4]
	cbz	w1, .L2758
	ldr	w1, [x29,180]
	cmp	w27, w1
	cset	w7, eq
	cbz	w0, .L2731
	ldr	w1, [x29,124]
	tst	w7, w1
	beq	.L2731
	add	x1, x22, :lo12:.LANCHOR0
	ldr	w2, [x29,144]
	ldrh	w1, [x1,2032]
	msub	w2, w27, w1, w2
	cmp	w2, w1
	bne	.L2758
.L2731:
	add	x1, x29, 212
	mov	w2, 0
	mov	w0, w27
	str	x7, [x29,104]
	bl	log2phys
	mov	x0, x19
	bl	get_new_active_ppa
	add	x4, x22, :lo12:.LANCHOR0
	mov	w5, 56
	str	x4, [x29,184]
	str	x5, [x29,160]
	ldr	w6, [x4,2088]
	ldr	x2, [x4,2096]
	umull	x1, w6, w5
	add	x2, x2, x1
	str	w0, [x2,4]
	ldrh	w2, [x4,2038]
	ldr	x0, [x4,2096]
	add	x1, x0, x1
	mul	w0, w2, w6
	and	x0, x0, 4294967292
	str	x0, [x29,136]
	ldr	x0, [x20,2128]
	ldr	x3, [x29,136]
	str	x0, [x29,128]
	add	x28, x0, x3
	ldrh	w0, [x4,2036]
	str	x28, [x1,16]
	str	w27, [x1,24]
	mul	w0, w0, w6
	ldr	x6, [x20,2088]
	and	x0, x0, 4294967292
	add	x0, x6, x0
	str	x0, [x1,8]
	mov	x0, x28
	mov	w1, 0
	bl	ftl_memset
	cmp	w27, w26
	ldr	x4, [x29,184]
	cset	w0, eq
	str	w0, [x29,176]
	cbnz	w0, .L2761
	ldr	x7, [x29,104]
	ldr	x5, [x29,160]
	cbz	w7, .L2732
	ldr	w0, [x29,144]
	ldrh	w2, [x4,2032]
	msub	w2, w27, w2, w0
	uxth	w0, w2
	str	w0, [x29,184]
	ldr	w0, [x29,176]
	str	w0, [x29,160]
	b	.L2735
.L2761:
	ldrh	w2, [x4,2032]
	udiv	w0, w23, w2
	msub	w0, w0, w2, w23
	str	w0, [x29,160]
	sub	w2, w2, w0
	cmp	w2, w24
	csel	w0, w2, w24, ls
	str	w0, [x29,184]
.L2735:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x29,184]
	ldrh	w0, [x2,2032]
	cmp	w1, w0
	bne	.L2736
	ldr	w0, [x29,176]
	mov	x1, x25
	cbnz	w0, .L2737
	ldr	w0, [x29,184]
	mul	w1, w0, w27
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x25, x1
.L2737:
	ldr	w2, [x29,204]
	mov	w0, 56
	add	x4, x22, :lo12:.LANCHOR0
	cbz	w2, .L2738
	ldr	w2, [x4,2088]
	umull	x0, w2, w0
	ldr	x2, [x4,2096]
	add	x0, x2, x0
	b	.L2779
.L2738:
	ldr	w2, [x4,2088]
	umull	x0, w2, w0
	ldr	x2, [x4,2096]
	add	x0, x2, x0
	ldr	x0, [x0,8]
	b	.L2777
.L2736:
	ldr	w0, [x29,212]
	cmn	w0, #1
	beq	.L2740
	str	w0, [x29,220]
	mov	w1, 56
	ldr	w0, [x2,2088]
	str	w27, [x29,240]
	umull	x0, w0, w1
	ldr	x1, [x2,2096]
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
	bne	.L2741
	ldr	w0, [x20,424]
	add	w0, w0, 1
	str	w0, [x20,424]
	b	.L2743
.L2741:
	ldr	w0, [x28,8]
	cmp	w0, w27
	beq	.L2743
	ldr	w0, [x20,424]
	mov	w2, w27
	add	w0, w0, 1
	str	w0, [x20,424]
	ldr	x0, [x29,112]
	ldr	w1, [x28,8]
	bl	printk
	b	.L2743
.L2740:
	ldr	w0, [x2,2088]
	mov	w1, 56
	umull	x0, w0, w1
	ldr	x1, [x2,2096]
	ldrh	w2, [x2,2036]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L2743:
	ldr	w1, [x29,184]
	mov	w0, 56
	lsl	w2, w1, 9
	ldr	w1, [x29,176]
	cbz	w1, .L2744
	add	x1, x22, :lo12:.LANCHOR0
	ldr	w4, [x1,2088]
	umull	x0, w4, w0
	ldr	x4, [x1,2096]
	add	x4, x4, x0
	ldr	x0, [x29,160]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x4,8]
	add	x0, x1, x0
	mov	x1, x25
	b	.L2778
.L2744:
	add	x5, x22, :lo12:.LANCHOR0
	ldr	w4, [x5,2088]
	ldr	x1, [x5,2096]
	umull	x0, w4, w0
	add	x0, x1, x0
	ldrh	w1, [x5,2032]
	mul	w1, w27, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x25, x1
	b	.L2778
.L2732:
	ldr	w0, [x29,204]
	ldrh	w1, [x4,2032]
	ldr	w2, [x4,2088]
	cbz	w0, .L2745
	mul	w1, w27, w1
	umull	x0, w2, w5
	sub	w1, w1, w23
	ldr	x2, [x4,2096]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x25, x1
.L2779:
	str	x1, [x0,8]
	b	.L2739
.L2745:
	umull	x0, w2, w5
	ldr	x2, [x4,2096]
	mul	w1, w27, w1
	add	x0, x2, x0
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x25, x1
.L2777:
	ldrh	w2, [x4,2036]
.L2778:
	bl	ftl_memcpy
.L2739:
	ldr	x1, [x29,128]
	mov	w0, -3947
	ldr	x2, [x29,136]
	strh	w0, [x1,x2]
	add	x1, x22, :lo12:.LANCHOR0
	str	w27, [x28,8]
	ldr	w0, [x20,200]
	str	w0, [x28,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x20,200]
	ldr	w0, [x29,212]
	str	w0, [x28,12]
	ldrh	w0, [x19]
	strh	w0, [x28,2]
	ldr	w0, [x1,2088]
	add	w0, w0, 1
	str	w0, [x1,2088]
	ldr	x0, [x29,192]
	add	x0, x0, 1
	str	x0, [x29,192]
	b	.L2729
.L2781:
	str	w27, [x29,200]
	mov	x0, x2
	b	.L2730
.L2758:
	str	w27, [x29,200]
.L2730:
	sub	w21, w21, w0
	ldr	w0, [x29,204]
	cbnz	w0, .L2749
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x0,2088]
	ldr	w0, [x20,2040]
	cmp	w1, w0
	bcs	.L2749
	ldrh	w0, [x19,4]
	cbz	w0, .L2749
.L2751:
	str	wzr, [x29,204]
	b	.L2722
.L2749:
	bl	FtlCacheWriteBack
	add	x0, x22, :lo12:.LANCHOR0
	cmp	w21, 3
	str	wzr, [x0,2088]
	bls	.L2751
	b	.L2722
.L2780:
	ldr	w1, [x29,180]
	mov	w0, w21
	sub	w1, w1, w26
	bl	rk_ftl_garbage_collect
	mov	w0, w21
.L2716:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 272
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.type	FlashReadFacBbtData.part.14, %function
FlashReadFacBbtData.part.14:
	stp	x29, x30, [sp, -32]!
	mov	w3, 0
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	cbnz	w1, .L2785
	adrp	x1, .LANCHOR4
	mov	w7, 1
.L2788:
	add	x0, x1, :lo12:.LANCHOR4
	ldr	w0, [x0,68]
	cmp	w3, w0
	bcs	.L2785
	add	x6, x4, :lo12:.LANCHOR0
	ubfx	x5, x3, 5, 11
	lsl	x5, x5, 2
	ldr	x0, [x6,1856]
	lsl	w6, w7, w3
	add	w3, w3, 1
	ldr	w8, [x0,x5]
	uxth	w3, w3
	orr	w6, w8, w6
	str	w6, [x0,x5]
	b	.L2788
.L2785:
	add	x4, x4, :lo12:.LANCHOR0
	mov	x0, x19
	ldr	x1, [x4,1856]
	bl	ftl_memcpy
	mov	w2, 4
	adrp	x0, .LC120
	mov	x1, x19
	add	x0, x0, :lo12:.LC120
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
	add	x4, x4, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x22, x0
	mov	w23, w1
	ldrh	w0, [x4,182]
	mov	w25, w2
	ldrh	w3, [x4,180]
	mov	w26, 61664
	mul	w3, w3, w0
	ldr	x0, [x4,1856]
	adrp	x4, .LANCHOR4
	str	x0, [x29,96]
	add	x0, x4, :lo12:.LANCHOR4
	uxth	w3, w3
	mov	x21, x4
	sub	w20, w3, #1
	sub	w19, w3, #16
	ldr	x0, [x0,88]
	mul	w24, w1, w3
	str	x0, [x29,104]
	uxth	w20, w20
.L2790:
	cmp	w20, w19
	ble	.L2798
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L2791
	add	x0, x21, :lo12:.LANCHOR4
	ldr	x0, [x0,88]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L2791
	mov	w0, w22
	cbz	x22, .L2792
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.14
	b	.L2792
.L2791:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2790
.L2798:
	mov	w0, -1
.L2792:
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
	bne	.L2800
.L2804:
	mov	w1, 0
	b	.L2801
.L2800:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L2802:
	cmp	w2, w19
	bge	.L2801
	add	x3, x20, :lo12:.LANCHOR4
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,80]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L2803
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L2803:
	cmp	w1, w0
	bcs	.L2804
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2802
.L2801:
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
	add	x22, x22, 2112
	add	x20, x20, 136
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x24, x22
	mov	w23, 0
	mov	x25, x20
	stp	x27, x28, [sp,80]
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L2810:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,1974]
	cmp	w23, w0
	bcs	.L2828
	adrp	x1, .LANCHOR2
	ldrh	w27, [x24,12]
	add	x2, x1, :lo12:.LANCHOR2
	str	x1, [x29,104]
	ldr	x0, [x2,-64]
	ldr	x26, [x2,2112]
	mov	w2, 65535
	str	x0, [x20,8]
	cmp	w27, w2
	str	x26, [x20,16]
	beq	.L2811
	ldrh	w0, [x21,2022]
	mov	w1, 1
	mov	w2, w1
	madd	w27, w23, w0, w27
	lsl	w0, w27, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x21,2022]
	ldr	x0, [x22,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L2812
.L2811:
	mov	w1, w23
	mov	w28, -3872
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x22,32]
	bl	FtlBbt2Bitmap
	ldrh	w21, [x21,2022]
	sub	w21, w21, #1
	uxth	w21, w21
.L2813:
	add	x27, x19, :lo12:.LANCHOR0
	ldrh	w0, [x27,2022]
	madd	w0, w23, w0, w21
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	bne	.L2829
	sub	w21, w21, #1
	uxth	w21, w21
	b	.L2813
.L2829:
	ldr	x0, [x29,104]
	mov	w1, 0
	mov	w2, 16
	strh	w21, [x24,12]
	add	x3, x0, :lo12:.LANCHOR2
	str	x3, [x29,96]
	ldr	x0, [x3,2112]
	bl	ftl_memset
	strh	w28, [x26]
	str	wzr, [x26,4]
	ldr	x3, [x29,96]
	ldrh	w0, [x24,12]
	strh	w0, [x26,2]
	ldrh	w0, [x27,2022]
	ldrh	w1, [x24,12]
	ldrh	w2, [x3,2232]
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
	bne	.L2812
	mov	w0, w27
	bl	FtlBbmMapBadBlock
	b	.L2813
.L2812:
	mov	w0, w27
	add	w23, w23, 1
	add	x22, x22, 8
	add	x24, x24, 2
	bl	FtlBbmMapBadBlock
	b	.L2810
.L2828:
	mov	w20, 0
.L2817:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,2040]
	cmp	w0, w20
	bls	.L2830
	mov	w0, w20
	add	w20, w20, 1
	bl	FtlBbmMapBadBlock
	uxth	w20, w20
	b	.L2817
.L2830:
	ldrh	w20, [x21,2124]
	mov	w23, 65535
	sub	w20, w20, #1
	uxth	w20, w20
.L2819:
	add	x22, x21, 2112
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L2823
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2820
	mov	w0, w20
	bl	FlashTestBlk
	cbz	w0, .L2821
	mov	w0, w20
	bl	FtlBbmMapBadBlock
	b	.L2820
.L2821:
	ldrh	w0, [x21,2112]
	cmp	w0, w23
	bne	.L2822
	strh	w20, [x21,2112]
	b	.L2820
.L2822:
	strh	w20, [x22,4]
	b	.L2823
.L2820:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2819
.L2823:
	adrp	x0, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x19,2112]
	ldr	x2, [x0,-88]
	str	wzr, [x19,2120]
	lsl	w1, w1, 10
	strh	wzr, [x19,2114]
	str	w1, [x2,4]
	mov	w2, 2
	ldrh	w1, [x19,2116]
	ldr	x0, [x0,-88]
	lsl	w1, w1, 10
	str	w1, [x0,60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x19,2112]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19,2116]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19,2114]
	ldr	w0, [x19,2120]
	ldrh	w1, [x19,2116]
	add	w0, w0, 1
	str	w0, [x19,2120]
	ldrh	w0, [x19,2112]
	strh	w0, [x19,2116]
	strh	w1, [x19,2112]
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
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x20, .LANCHOR0
	str	wzr, [x0,196]
	str	wzr, [x0,200]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,1956]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2832
	bl	FtlMakeBbt
.L2832:
	mov	w0, 0
.L2833:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2032]
	cmp	w0, w2, lsl 7
	bge	.L2856
	add	x3, x21, :lo12:.LANCHOR2
	ubfiz	x2, x0, 2, 16
	mvn	w1, w0
	ldr	x4, [x3,2096]
	orr	w1, w0, w1, lsl 16
	add	w0, w0, 1
	str	w1, [x4,x2]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	uxth	w0, w0
	ldr	x3, [x3,2104]
	str	w1, [x3,x2]
	b	.L2833
.L2856:
	ldrh	w22, [x1,1960]
	mov	w19, 0
.L2835:
	add	x23, x20, :lo12:.LANCHOR0
	ldrh	w0, [x23,1962]
	cmp	w0, w22
	bls	.L2857
	mov	w0, w22
	mov	w1, 1
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w19, w0
	uxth	w22, w22
	uxth	w19, w0
	b	.L2835
.L2857:
	ldrh	w0, [x23,1952]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L2837
.L2841:
	mov	w19, 0
	mov	w22, w19
	b	.L2838
.L2837:
	udiv	w19, w19, w0
	ldr	w0, [x23,2060]
	add	w0, w19, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x23,1956]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x23,1960]
.L2839:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,1962]
	cmp	w0, w19
	bls	.L2841
	mov	w0, w19
	mov	w1, 1
	add	w19, w19, 1
	bl	FtlLowFormatEraseBlock
	uxth	w19, w19
	b	.L2839
.L2838:
	add	x2, x20, :lo12:.LANCHOR0
	ldrh	w0, [x2,1960]
	cmp	w0, w22
	bls	.L2858
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w19, w0
	uxth	w22, w22
	uxth	w19, w0
	b	.L2838
.L2858:
	ldrh	w3, [x2,1952]
	add	x6, x21, :lo12:.LANCHOR2
	ldr	w1, [x2,1964]
	ldrh	w0, [x2,1962]
	str	w0, [x2,2108]
	udiv	w4, w1, w3
	str	w4, [x2,2104]
	ubfx	x5, x4, 5, 16
	add	w0, w5, 36
	strh	w0, [x6,272]
	mov	w0, 24
	mul	w0, w3, w0
	cmp	w19, w0
	ble	.L2843
	sub	w1, w1, w19
	udiv	w1, w1, w3
	str	w1, [x2,2104]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x6,272]
.L2843:
	add	x2, x21, :lo12:.LANCHOR2
	ldr	w0, [x2,160]
	cmp	w0, 1
	bne	.L2844
	ldrh	w1, [x2,272]
	udiv	w0, w19, w3
	add	w0, w1, w0
	add	w0, w1, w0, asr 2
	strh	w0, [x2,272]
.L2844:
	add	x6, x20, :lo12:.LANCHOR0
	ldrh	w1, [x6,2018]
	cbz	w1, .L2846
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w2, [x0,272]
	add	w2, w2, w1, lsr 1
	strh	w2, [x0,272]
	mul	w2, w1, w3
	cmp	w2, w19
	ble	.L2846
	add	w1, w1, 32
	str	w4, [x6,2104]
	add	w1, w5, w1
	strh	w1, [x0,272]
.L2846:
	add	x19, x21, :lo12:.LANCHOR2
	add	x22, x20, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR4
	mov	w23, -1
	ldr	w1, [x22,2104]
	ldrh	w0, [x19,272]
	sub	w0, w1, w0
	add	x1, x24, :lo12:.LANCHOR4
	mul	w0, w0, w3
	str	w0, [x1,192]
	ldrh	w1, [x22,2026]
	mul	w0, w0, w1
	ldrh	w1, [x22,2032]
	str	w0, [x22,2104]
	mul	w0, w0, w1
	str	w0, [x22,2068]
	bl	FtlBbmTblFlush
	ldrh	w2, [x22,1962]
	mov	w1, 0
	ldr	x0, [x19,-40]
	lsl	w2, w2, 1
	bl	ftl_memset
	strh	wzr, [x21,#:lo12:.LANCHOR2]
	mov	w0, 1
	ldrh	w2, [x22,1960]
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
.L2848:
	mov	x0, x19
	bl	make_superblock
	ldrb	w0, [x19,7]
	cbnz	w0, .L2849
	ldrh	w1, [x19]
	ldr	x0, [x19,-40]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	add	w0, w0, 1
	strh	w0, [x19]
	b	.L2848
.L2849:
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
.L2850:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2851
	ldrh	w1, [x20]
	ldr	x0, [x19,-40]
	strh	w22, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2850
.L2851:
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
	ldr	w0, [x21,192]
	strh	w0, [x19,286]
	ldr	w0, [x19,196]
	str	w0, [x19,288]
	add	w0, w0, 1
	strh	wzr, [x19,282]
	strh	w20, [x19,284]
	str	w0, [x19,196]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L2852
	adrp	x0, .LANCHOR1+532
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+532]
.L2852:
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
	adrp	x19, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR4
	ldr	x0, [x0,32]
	bl	FlashInit
	mov	w1, 0
	cbnz	w0, .L2860
	bl	FlashLoadFactorBbt
	cbz	w0, .L2861
	bl	FlashMakeFactorBbt
.L2861:
	add	x19, x19, :lo12:.LANCHOR4
	ldr	x0, [x19,80]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L2862
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L2864:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L2864
	cmp	w0, 6
	bhi	.L2865
	adrp	x0, .LANCHOR0+9
	strb	w1, [x0,#:lo12:.LANCHOR0+9]
	b	.L2866
.L2865:
	mov	w1, 0
	mov	w4, 1
.L2868:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L2868
	cmp	w0, 17
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	bls	.L2889
	mov	w1, 36
.L2889:
	strb	w1, [x0,9]
.L2866:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,9]
	strh	w1, [x0,194]
.L2862:
	adrp	x0, .LC72
	adrp	x1, .LC73
	add	x1, x1, :lo12:.LC73
	add	x0, x0, :lo12:.LC72
	adrp	x19, .LANCHOR0
	bl	printk
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x19, 168
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,1956]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L2870:
	bl	FtlLoadBbt
	cbz	w0, .L2871
.L2891:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L2892
	add	w19, w19, 1
	b	.L2870
.L2892:
	mov	w1, -1
	b	.L2860
.L2871:
	bl	FtlSysBlkInit
	cbnz	w0, .L2891
	adrp	x1, .LANCHOR1+532
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+532]
	mov	w1, w0
.L2860:
	mov	w0, w1
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
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
	bne	.L2893
	bl	flash_enter_slc_mode
.L2893:
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
	bne	.L2895
	bl	flash_exit_slc_mode
.L2895:
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
	adrp	x0, .LC121
	add	x0, x0, :lo12:.LC121
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
.L2898:
	cmp	w23, w26
	bcs	.L2904
	ldrb	w1, [x19,120]
	sub	w24, w6, w3
	add	w4, w23, w21
	lsr	w4, w4, 2
	uxth	w24, w24
	add	x0, x19, x4, uxth 1
	ldrh	w0, [x0,196]
	cbz	w1, .L2899
	ldr	w2, [x19,1872]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	uxth	w4, w4
	cmp	w2, w1
	csel	w0, w4, w0, eq
.L2899:
	add	w3, w3, w5
	add	x27, x7, :lo12:.LANCHOR4
	ldrb	w4, [x19,1944]
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
	b	.L2898
.L2904:
	adrp	x0, .LC122
	mov	w1, w22
	mov	w2, w26
	mov	w3, 0
	add	x0, x0, :lo12:.LC122
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
	adrp	x0, .LC123
	add	x0, x0, :lo12:.LC123
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
.L2906:
	cmp	w23, w27
	bcs	.L2920
	add	w4, w23, w26
	lsr	w4, w4, 2
	uxth	w0, w4
	and	w4, w4, 65535
	cbz	w0, .L2907
	add	x1, x19, 196
	add	w2, w4, 1
	ldrh	w3, [x1,w2,sxtw 1]
	ldrb	w1, [x19,120]
	cbz	w1, .L2908
	ldr	w2, [x19,1872]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L2908
	add	w3, w0, 1
	uxth	w3, w3
.L2908:
	sub	w3, w3, #1
	str	wzr, [x29,132]
	lsl	w3, w3, 2
	str	w3, [x29,128]
.L2907:
	add	x1, x19, 196
	ldrh	w25, [x1,w4,sxtw 1]
	ldrb	w1, [x19,120]
	cbz	w1, .L2909
	ldr	w2, [x19,1872]
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	csel	w25, w25, w0, ne
.L2909:
	add	x0, x28, :lo12:.LANCHOR4
	ldrb	w4, [x19,1944]
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
	b	.L2906
.L2920:
	adrp	x0, .LC124
	mov	w1, w24
	mov	w2, w27
	mov	w3, 0
	add	x0, x0, :lo12:.LC124
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
	mov	w2, 6
	ldr	x1, [x0,744]
	ldr	w26, [x0,12]
	mov	x0, 59392
	movk	x0, 0x3, lsl 16
	ldrb	w22, [x1,9]
	mov	w1, 192
	movk	w1, 0x240, lsl 16
	bl	kmalloc_order_trace
	str	x24, [x29,120]
	mov	x20, x0
	mov	w0, -1
	cbz	x20, .L2931
	add	w19, w25, 511
	lsr	w19, w19, 9
	cmp	w19, 255
	bhi	.L2923
	ubfiz	x0, x19, 9, 23
	mov	w2, 256
	add	x0, x21, x0
	mov	x1, x21
	sub	w2, w2, w19
	bl	memcpy
.L2923:
	add	w19, w19, 128
	mov	w0, 256
	cmp	w19, 256
	mov	x1, x23
	csel	w19, w19, w0, ls
	adrp	x0, .LC125
	add	x0, x0, :lo12:.LC125
	mov	w2, 4
	mov	w3, 5
	bl	rknand_print_hex
	ldr	x0, [x29,120]
	ldr	w1, [x21,512]
	add	x27, x0, :lo12:.LANCHOR0
	adrp	x0, .LC126
	add	x0, x0, :lo12:.LC126
	ldrb	w2, [x27,9]
	bl	printk
	ldrb	w0, [x27,9]
	ldr	w1, [x21,512]
	cmp	w1, w0
	bls	.L2924
	str	w0, [x21,512]
.L2924:
	adrp	x0, .LC127
	mov	w2, w25
	mul	w22, w22, w26
	add	x0, x0, :lo12:.LC127
	mov	w1, w19
	mov	x24, 0
	adrp	x26, .LC128
	bl	printk
	adrp	x27, .LC129
	adrp	x28, .LC130
	lsl	w0, w19, 7
	uxth	w22, w22
	str	w0, [x29,116]
	mov	w25, w24
	add	x26, x26, :lo12:.LC128
	add	x27, x27, :lo12:.LC129
	add	x28, x28, :lo12:.LC130
.L2929:
	ldr	x1, [x29,120]
	mov	w8, w24
	ldr	w0, [x23,x24,lsl 2]
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w1, [x1,194]
	cmp	w0, w1
	bcs	.L2925
	adrp	x1, .LANCHOR4+68
	ldr	w1, [x1,#:lo12:.LANCHOR4+68]
	cmp	w0, w1
	bcc	.L2925
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
.L2926:
	ldr	w1, [x29,116]
	mov	w3, w0
	cmp	w0, w1
	bcs	.L2936
	ldr	w4, [x20,x0,lsl 2]
	mov	x7, x0
	add	x0, x0, 1
	add	x1, x21, x0, lsl 2
	ldr	w5, [x1,-4]
	cmp	w4, w5
	beq	.L2926
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
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	bl	printk
	b	.L2925
.L2936:
	add	w25, w25, 1
.L2925:
	add	x24, x24, 1
	cmp	x24, 5
	bne	.L2929
	mov	x0, x20
	bl	kfree
	cmp	w25, wzr
	csetm	w0, eq
.L2931:
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
.L2938:
	cmp	w1, w2
	bls	.L2940
	ldrb	w4, [x5,x2]
	add	x6, x3, 3016
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L2938
.L2940:
	ret
	.size	CRC_32, .-CRC_32
	.align	2
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
	beq	.L2943
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2944
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2945
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2946
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2947
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2948
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3005
	bl	rknand_dev_flush
	b	.L3037
.L2948:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2950
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2951
	b	.L3005
.L2946:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2952
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2953
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2954
	b	.L3005
.L2953:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2952
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2952
	b	.L3005
.L2944:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2955
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2956
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2957
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L2958
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2959
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2960
	b	.L3005
.L2956:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2961
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2962
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2963
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2964
	b	.L3005
.L2962:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2964
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2961
	b	.L3005
.L2958:
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	bl	printk
	adrp	x0, kmalloc_caches+96
	mov	w1, 192
	movk	w1, 0x240, lsl 16
	mov	x2, 4096
	ldr	x0, [x0,#:lo12:kmalloc_caches+96]
	bl	kmem_cache_alloc_trace
	mov	x19, x0
	cbnz	x0, .L2965
.L2970:
	mov	x0, -12
	b	.L2942
.L2965:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3044
	adrp	x0, .LC134
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC134
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3040
	bl	rknand_device_unlock
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
	cbz	x0, .L3046
	adrp	x0, .LC135
	add	x0, x0, :lo12:.LC135
.L3039:
	bl	printk
.L3040:
	mov	x0, x19
.L3041:
	bl	kfree
.L3042:
	mov	x0, -14
	b	.L2942
.L2957:
	adrp	x0, .LC136
	add	x0, x0, :lo12:.LC136
	bl	printk
	adrp	x0, kmalloc_caches+96
	mov	w1, 192
	movk	w1, 0x240, lsl 16
	mov	x2, 4096
	ldr	x0, [x0,#:lo12:kmalloc_caches+96]
	bl	kmem_cache_alloc_trace
	mov	x19, x0
	cbz	x0, .L2970
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3044
	ldr	w1, [x19]
	adrp	x0, .LC137
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC137
	bl	printk
	adrp	x0, .LANCHOR4
	add	x21, x0, :lo12:.LANCHOR4
	mov	x20, x0
	ldr	x1, [x21,448]
	cbnz	x1, .L2972
	mov	w1, 192
	mov	x0, 260096
	movk	w1, 0x240, lsl 16
	mov	w2, 6
	bl	kmalloc_order_trace
	str	x0, [x21,448]
	cbz	x0, .L3040
.L2972:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3040
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3040
	add	x20, x20, :lo12:.LANCHOR4
	uxtw	x2, w2
	ldr	x1, [x20,448]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3046:
	mov	x0, x19
	bl	kfree
	b	.L3037
.L2960:
	adrp	x0, .LC138
	add	x0, x0, :lo12:.LC138
	bl	printk
	adrp	x0, kmalloc_caches+96
	mov	w1, 192
	movk	w1, 0x240, lsl 16
	mov	x2, 4096
	ldr	x0, [x0,#:lo12:kmalloc_caches+96]
	bl	kmem_cache_alloc_trace
	mov	x19, x0
	cbz	x0, .L2970
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L2974
.L3044:
	adrp	x0, .LC133
	add	x0, x0, :lo12:.LC133
	b	.L3039
.L2974:
	ldr	w1, [x19]
	adrp	x0, .LC139
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC139
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3040
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x20,448]
	cbz	x0, .L3040
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L2977
	mov	x0, x19
	bl	kfree
	mov	x0, -2
	b	.L2942
.L2977:
	bl	rknand_device_unlock
	ldr	x1, [x20,448]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,448]
	bl	kfree
	str	xzr, [x20,448]
	b	.L3046
.L2959:
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	bl	printk
	adrp	x0, kmalloc_caches+96
	mov	w1, 192
	movk	w1, 0x240, lsl 16
	mov	x2, 4096
	ldr	x0, [x0,#:lo12:kmalloc_caches+96]
	bl	kmem_cache_alloc_trace
	mov	x21, x0
	cbz	x0, .L2970
	bl	ReadFlashInfo
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3048
.L2943:
	adrp	x0, .LC141
	add	x0, x0, :lo12:.LC141
	bl	printk
	bl	rknand_device_unlock
	bl	FtlReInitForSDUpdata
	mov	w19, w0
	bl	rknand_device_unlock
	cbnz	w19, .L3042
	bl	nand_blk_add_whole_disk
	adrp	x0, kmalloc_caches+96
	mov	w1, 192
	movk	w1, 0x240, lsl 16
	mov	x2, 4096
	ldr	x0, [x0,#:lo12:kmalloc_caches+96]
	bl	kmem_cache_alloc_trace
	mov	x21, x0
	cbz	x0, .L2970
	bl	rknand_device_unlock
	mov	w1, w19
	mov	w2, 64
	mov	x0, x21
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	adrp	x0, .LC142
	mov	x1, x21
	add	x0, x0, :lo12:.LC142
	mov	w2, 4
	mov	w3, 8
	bl	rknand_print_hex
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3048
.L2955:
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	bl	printk
	adrp	x0, .LANCHOR4+456
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR4+456]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
	b	.L3033
.L2963:
	adrp	x0, .LC144
	add	x0, x0, :lo12:.LC144
	bl	printk
	adrp	x0, kmalloc_caches+96
	mov	w1, 192
	movk	w1, 0x240, lsl 16
	mov	x2, 4096
	ldr	x0, [x0,#:lo12:kmalloc_caches+96]
	bl	kmem_cache_alloc_trace
	mov	x21, x0
	cbz	x0, .L2970
	bl	rknand_device_unlock
	mov	w1, 2
	mov	x2, x21
	mov	w0, 520
	bl	FtlVendorPartRead
	bl	rknand_device_unlock
	mov	x0, x20
	mov	x1, x21
	mov	x2, 1024
.L3048:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3041
	bl	kfree
	b	.L2969
.L2947:
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L2983
.L2988:
	adrp	x0, .LC133
	add	x0, x0, :lo12:.LC133
	bl	printk
	b	.L3042
.L2983:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L2984
.L2985:
	mov	x19, -1
	b	.L2969
.L2984:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L2985
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
	beq	.L2986
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L2986:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
.L3034:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
.L3033:
	bl	rk_copy_to_user
	cbnz	x0, .L3042
	b	.L3037
.L2950:
	adrp	x0, .LC146
	add	x0, x0, :lo12:.LC146
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2988
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L2985
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L2985
	adrp	x1, .LANCHOR4
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR4
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,464]
	cmp	w3, w2
	bne	.L2942
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L2942
	ldr	x19, [x1,456]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3038
.L2954:
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2988
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L2985
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L2985
	adrp	x19, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR4
	ldr	w1, [x0,468]
	cbnz	w1, .L2989
.L2992:
	mov	x0, 0
	b	.L2942
.L2989:
	ldr	x1, [x0,472]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L2990
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,472]
	str	w2, [x1,4]
	ldr	x0, [x0,472]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L2990:
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
	beq	.L2991
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,456]
	str	w1, [x0,4]
	ldr	x0, [x20,456]
	str	wzr, [x0,8]
.L2991:
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
	b	.L3037
.L2945:
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2988
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L2985
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L2985
	adrp	x19, .LANCHOR4
	add	x1, x19, :lo12:.LANCHOR4
	ldr	w2, [x1,468]
	cmp	w2, 1
	beq	.L2992
	ldr	x2, [x1,472]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L2993
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,472]
	str	w3, [x2,4]
	ldr	x1, [x1,472]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L2993:
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
	beq	.L2994
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,456]
	str	w1, [x0,4]
	ldr	x0, [x20,456]
	str	wzr, [x0,8]
.L2994:
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
	b	.L3037
.L2951:
	adrp	x0, .LC149
	add	x0, x0, :lo12:.LC149
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2988
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L2985
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L2985
	adrp	x1, .LANCHOR4
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR4
	uxtw	x2, w2
	add	x1, x1, 480
	b	.L3035
.L2952:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L2995
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	b	.L3036
.L2995:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L2997
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	b	.L3036
.L2997:
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
.L3036:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2988
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3042
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR4
	bne	.L2998
	add	x0, x0, :lo12:.LANCHOR4
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,456]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L2942
	b	.L3042
.L2998:
	add	x20, x0, :lo12:.LANCHOR4
	ldr	w1, [x20,992]
	cmp	w1, 10
	bhi	.L3042
	ldr	x1, [x20,456]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L2999
	cbz	w3, .L2999
	adrp	x0, .LC153
	mov	w1, w2
	add	x0, x0, :lo12:.LC153
	bl	printk
	ldr	w0, [x20,992]
	add	w0, w0, 1
	str	w0, [x20,992]
	b	.L3042
.L2999:
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x0,992]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3000
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3001
.L3000:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3001:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3037
	b	.L2969
.L2964:
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2988
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L2985
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L2985
	mov	w0, 30224
	adrp	x1, .LANCHOR4
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x29, 64
	bne	.L3002
	ldr	x1, [x1,1000]
	b	.L3045
.L3002:
	ldr	x1, [x1,1008]
.L3045:
	add	x1, x1, 8
.L3035:
	bl	memcpy
	b	.L3034
.L2961:
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2988
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L2985
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L2985
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR4
	bne	.L3004
	ldr	x0, [x19,1000]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,1000]
	b	.L3038
.L3004:
	ldr	x0, [x19,1008]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,1008]
	mov	w0, 3
.L3038:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L2969
.L3037:
	mov	x19, 0
.L2969:
	adrp	x0, .LC156
	mov	x1, x19
	add	x0, x0, :lo12:.LC156
	bl	printk
	mov	x0, x19
	b	.L2942
.L3005:
	mov	x0, -22
.L2942:
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
	cbz	w22, .L3051
	mov	x0, x21
	mov	w1, 508
	bl	JSHash
	cmp	w22, w0
	beq	.L3051
	adrp	x0, .LC157
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC157
	str	wzr, [x19,468]
	bl	printk
.L3051:
	add	x0, x20, :lo12:.LANCHOR4
	ldr	w1, [x0,468]
	cbz	w1, .L3052
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,464]
.L3052:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 2
	ldr	x1, [x20,1000]
	bl	StorageSysDataLoad
	ldr	x1, [x20,1008]
	mov	w0, 3
	bl	StorageSysDataLoad
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 4040
	bl	misc_register
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	2
	.global	StorageSysDataDeInit
	.type	StorageSysDataDeInit, %function
StorageSysDataDeInit:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x29, sp, 0
	add	x0, x0, 4040
	bl	misc_deregister
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	StorageSysDataDeInit, .-StorageSysDataDeInit
	.global	rknand_sys_storage_fops
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
	.type	__func__.19115, %object
	.size	__func__.19115, 11
__func__.19115:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.19991, %object
	.size	__func__.19991, 21
__func__.19991:
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
	.string	"FTL version: 5.0.36 20170512"
.LC74:
	.string	"GetSwlReplaceBlock min_ec_id =%x %x\n"
.LC75:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC76:
	.string	"FtlGcRefreshBlock  0x%x \n"
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
	.string	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\n"
.LC83:
	.string	"nandc:"
.LC84:
	.string	"%d flReg.d32=%x %x\n"
.LC85:
	.string	"sdr read ok %x ecc=%d\n"
.LC86:
	.string	"sync para %d\n"
.LC87:
	.string	"TOG mode Read error %x %x\n"
.LC88:
	.string	"read retry status %x %x %x\n"
.LC89:
	.string	"ECC:%d\n"
.LC90:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC91:
	.string	"FlashLoadPhyInfo fail %x!!\n"
.LC92:
	.string	"Read pageadd=%x  ecc=%x err=%x\n"
.LC93:
	.string	"data:"
.LC94:
	.string	"spare:"
.LC95:
	.string	"ReadRetry pageadd=%x  ecc=%x err=%x\n"
.LC96:
	.string	"FLFB:%d %d\n"
.LC97:
	.string	"prog error: = %x\n"
.LC98:
	.string	"prog read error: = %x\n"
.LC99:
	.string	"prog read s error: = %x %x %x\n"
.LC100:
	.string	"prog read d error: = %x %x %x\n"
.LC101:
	.string	"no ect"
.LC102:
	.string	"slc mode"
.LC103:
	.string	"FlashMakeFactorBbt %d\n"
.LC104:
	.string	"bad block:%d %d\n"
.LC105:
	.string	"FMFB:%d %d\n"
.LC106:
	.string	"E:bad block:%d\n"
.LC107:
	.string	"FMFB:Save %d %d\n"
.LC108:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC109:
	.string	"FtlBbmTblFlush error:%x\n"
.LC110:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC111:
	.string	"decrement_vpc_count %x = %d\n"
.LC112:
	.string	"spuer block %x vpn is 0\n "
.LC113:
	.string	"...%s enter...\n"
.LC114:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC115:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC116:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC117:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC118:
	.string	"FtlInit %x\n"
.LC119:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC120:
	.string	"BBT:"
.LC121:
	.string	"IdBlockReadData %x %x\n"
.LC122:
	.string	"IdBlockReadData %x %x ret= %x\n"
.LC123:
	.string	"IDBlockWriteData %x %x\n"
.LC124:
	.string	"IDBlockWriteData %x %x ret= %x\n"
.LC125:
	.string	"idblk:"
.LC126:
	.string	"idb reverse %x %x\n"
.LC127:
	.string	"write_idblock totle_sec %x %x\n"
.LC128:
	.string	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x\n"
.LC129:
	.string	"write"
.LC130:
	.string	"read"
.LC131:
	.string	"write_idblock error\n"
.LC132:
	.string	"READ_SECTOR_IO\n"
.LC133:
	.string	"rk_copy_from_user error\n"
.LC134:
	.string	"READ_SECTOR_IO %x %x\n"
.LC135:
	.string	"rk_copy_to_user error\n"
.LC136:
	.string	"WRITE_SECTOR_IO\n"
.LC137:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC138:
	.string	"END_WRITE_SECTOR_IO\n"
.LC139:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC140:
	.string	"GET_FLASH_INFO_IO\n"
.LC141:
	.string	"GET_BAD_BLOCK_IO\n"
.LC142:
	.string	"bbt:"
.LC143:
	.string	"GET_LOCK_FLAG_IO\n"
.LC144:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC145:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC146:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC147:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC148:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC149:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC150:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC151:
	.string	"RKNAND_LOADER_STATUS\n"
.LC152:
	.string	"RKNAND_LOADER_LOCK\n"
.LC153:
	.string	"LockKey not match %d\n"
.LC154:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC155:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC156:
	.string	"return ret = %lx\n"
.LC157:
	.string	"secureBootEn check error\n"
.LC158:
	.string	"rknand_sys_storage"
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
	.type	gFtlInitStatus, %object
	.size	gFtlInitStatus, 4
gFtlInitStatus:
	.word	-1
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
	.byte	6
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
	.byte	-75
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
	.hword	1088
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
	.type	micron_flash_randomizer_flag, %object
	.size	micron_flash_randomizer_flag, 4
micron_flash_randomizer_flag:
	.word	1
	.type	ftl_gc_temp_block_bops_scan_page_addr, %object
	.size	ftl_gc_temp_block_bops_scan_page_addr, 2
ftl_gc_temp_block_bops_scan_page_addr:
	.hword	-1
	.zero	2
	.type	power_up_flag, %object
	.size	power_up_flag, 4
power_up_flag:
	.word	1
	.zero	4
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
	.type	rknand_sys_storage_dev, %object
	.size	rknand_sys_storage_dev, 80
rknand_sys_storage_dev:
	.word	255
	.zero	4
	.xword	.LC158
	.xword	rknand_sys_storage_fops
	.zero	56
	.type	rknand_sys_storage_fops, %object
	.size	rknand_sys_storage_fops, 216
rknand_sys_storage_fops:
	.zero	64
	.xword	rknand_sys_storage_ioctl
	.xword	rknand_sys_storage_ioctl
	.zero	8
	.xword	rknand_sys_storage_open
	.zero	8
	.xword	rknand_sys_storage_release
	.zero	104
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
	.zero	3
	.type	gNandcVer, %object
	.size	gNandcVer, 4
gNandcVer:
	.zero	4
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
	.type	gFlashPageBuffer0, %object
	.size	gFlashPageBuffer0, 8
gFlashPageBuffer0:
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
