/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
	adrp	x0, :got:kmalloc_caches
	mov	x2, 4096
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:kmalloc_caches]
	str	x19, [sp,16]
	mov	x19, x1
	mov	w1, 192
	ldr	x0, [x0,96]
	movk	w1, 0x240, lsl 16
	bl	kmem_cache_alloc_trace
	str	xzr, [x19,208]
	cbnz	x0, .L2
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	mov	w0, -12
	b	.L3
.L2:
	str	x0, [x19,208]
	mov	w0, 0
.L3:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rknand_sys_storage_open, .-rknand_sys_storage_open
	.align	2
	.type	rknand_sys_storage_release, %function
rknand_sys_storage_release:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x1,208]
	cbz	x0, .L6
	str	xzr, [x1,208]
	bl	kfree
.L6:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_sys_storage_release, .-rknand_sys_storage_release
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	adrp	x3, :got:g_slc2KBNand
	mov	x5, x0
	ldr	x3, [x3, #:got_lo12:g_slc2KBNand]
	ldrb	w0, [x3]
	cbz	w0, .L14
	ldrb	w4, [x5,1]
	mov	w0, 0
	ldrb	w3, [x1,1]
	cmp	w4, w3
	beq	.L12
.L14:
	mov	x3, 0
.L16:
	cmp	w3, w2
	mov	w4, w3
	bcs	.L18
	ldrb	w0, [x5,x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6,-1]
	cmp	w6, w0
	beq	.L16
	add	w0, w4, 1
	b	.L12
.L18:
	mov	w0, 0
.L12:
	ret
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	2
	.global	FlashRsvdBlkChk
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	adrp	x2, :got:gNandIDBResBlkNum
	adrp	x3, :got:gBlockPageAlignSize
	uxtb	w0, w0
	ldr	x2, [x2, #:got_lo12:gNandIDBResBlkNum]
	ldr	x3, [x3, #:got_lo12:gBlockPageAlignSize]
	ldrb	w2, [x2]
	ldr	w3, [x3]
	mul	w2, w2, w3
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
	adrp	x2, :got:random_seed
	and	w3, w1, 127
	stp	x29, x30, [sp, -32]!
	uxtb	w0, w0
	add	x29, sp, 0
	ldr	x2, [x2, #:got_lo12:random_seed]
	str	x19, [sp,16]
	ldrh	w19, [x2,w3,uxtw 1]
	adrp	x2, :got:gNandRandomizer
	ldr	x2, [x2, #:got_lo12:gNandRandomizer]
	ldrb	w2, [x2]
	cbz	w2, .L21
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w19, -1073741824
	csel	w19, w1, w19, ne
.L21:
	mov	w0, w19
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashGetRandomizer, .-FlashGetRandomizer
	.align	2
	.global	JSHash
	.type	JSHash, %function
JSHash:
	mov	x5, x0
	mov	w0, 42982
	mov	x2, 0
	movk	w0, 0x47c6, lsl 16
.L28:
	cmp	w1, w2
	bls	.L30
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L28
.L30:
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
	adrp	x0, :got:gpNandc
	adrp	x2, .LANCHOR0
	add	x1, x2, :lo12:.LANCHOR0
	ldr	x0, [x0, #:got_lo12:gpNandc]
	ldr	x0, [x0]
	ldr	w3, [x0]
	str	w3, [x2,#:lo12:.LANCHOR0]
	ldr	w2, [x0,4]
	str	w2, [x1,4]
	ldr	w2, [x0,8]
	str	w2, [x1,8]
	ldr	w2, [x0,12]
	str	w2, [x1,12]
	ldr	w2, [x0,304]
	str	w2, [x1,16]
	ldr	w2, [x0,308]
	str	w2, [x1,20]
	ldr	w2, [x0,336]
	ldr	w0, [x0,344]
	str	w0, [x1,28]
	mov	w0, 0
	str	w2, [x1,24]
	ret
	.size	FlashSuspend, .-FlashSuspend
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	adrp	x6, :got:gNandPhyInfo
	uxtb	w4, w4
	ldr	x6, [x6, #:got_lo12:gNandPhyInfo]
	ldrh	w5, [x6,12]
	ldrh	w6, [x6,14]
	mul	w5, w5, w6
	adrp	x6, :got:gBlockPageAlignSize
	uxth	w5, w5
	ldr	x6, [x6, #:got_lo12:gBlockPageAlignSize]
	ldrh	w8, [x6]
	adrp	x6, :got:g_slc2KBNand
	ubfiz	w7, w8, 1, 15
	ldr	x6, [x6, #:got_lo12:g_slc2KBNand]
	ldrb	w6, [x6]
	cmp	w6, 1
	csel	w8, w7, w8, eq
	ldr	w7, [x0,4]
	cmp	w1, 1
	ubfx	x6, x7, 10, 16
	and	w7, w7, 1023
	udiv	w9, w6, w5
	msub	w5, w9, w5, w6
	uxth	w6, w5
	bne	.L36
	adrp	x1, :got:slcPageToMlcPageTbl
	ldr	x1, [x1, #:got_lo12:slcPageToMlcPageTbl]
	ldrh	w7, [x1,w7,sxtw 1]
.L36:
	adrp	x1, :got:DieAddrs
	cmp	w4, 1
	ldr	x1, [x1, #:got_lo12:DieAddrs]
	ldr	w5, [x1,w9,uxtw 2]
	mov	w1, 0
	madd	w5, w6, w8, w5
	add	w5, w5, w7
	str	w5, [x2]
	str	w9, [x3]
	bls	.L37
	ldr	w1, [x0,4]
	ldr	w0, [x0,60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w1, eq
.L37:
	mov	w0, w1
	ret
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	FlashGetPageSize
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	adrp	x0, :got:gpNandParaInfo
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
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
	adrp	x1, :got:gNandChipMap
	ubfiz	x0, x0, 4, 8
	ldr	x1, [x1, #:got_lo12:gNandChipMap]
	add	x2, x1, x0
	ldr	x0, [x1,x0]
	ldrb	w2, [x2,8]
	add	x2, x2, 8
	add	x0, x0, x2, lsl 8
	ret
	.size	NandcGetChipIf, .-NandcGetChipIf
	.align	2
	.global	FlashEraseCmd
	.type	FlashEraseCmd, %function
FlashEraseCmd:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x1, [x29,16]
	str	x2, [x29,24]
	bl	NandcGetChipIf
	ldr	x2, [x29,24]
	ldr	x1, [x29,16]
	cbz	w2, .L43
	mov	w2, 96
	str	w2, [x0,8]
	and	w2, w1, 255
	str	w2, [x0,4]
	lsr	w2, w1, 8
	str	w2, [x0,4]
	lsr	w2, w1, 16
	str	w2, [x0,4]
	adrp	x2, :got:gBlockPageAlignSize
	ldr	x2, [x2, #:got_lo12:gBlockPageAlignSize]
	ldr	w2, [x2]
	add	w1, w1, w2
.L43:
	mov	w2, 96
	str	w2, [x0,8]
	and	w2, w1, 255
	str	w2, [x0,4]
	lsr	w2, w1, 8
	str	w2, [x0,4]
	lsr	w1, w1, 16
	str	w1, [x0,4]
	mov	w1, 208
	str	w1, [x0,8]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashEraseCmd, .-FlashEraseCmd
	.align	2
	.global	FlashProgSecondCmd
	.type	FlashProgSecondCmd, %function
FlashProgSecondCmd:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	NandcGetChipIf
	mov	w1, 16
	str	w1, [x0,8]
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashProgSecondCmd, .-FlashProgSecondCmd
	.align	2
	.global	FlashProgDpFirstCmd
	.type	FlashProgDpFirstCmd, %function
FlashProgDpFirstCmd:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	NandcGetChipIf
	adrp	x1, :got:gNandOptPara
	ldr	x1, [x1, #:got_lo12:gNandOptPara]
	ldrb	w1, [x1,10]
	str	w1, [x0,8]
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	2
	.global	NandcSetDdrPara
	.type	NandcSetDdrPara, %function
NandcSetDdrPara:
	adrp	x1, :got:gpNandc
	uxtb	w0, w0
	lsl	w2, w0, 8
	orr	w0, w2, w0, lsl 16
	ldr	x1, [x1, #:got_lo12:gpNandc]
	orr	w0, w0, 1
	ldr	x1, [x1]
	str	w0, [x1,304]
	ret
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrMode
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	adrp	x1, :got:gpNandc
	cmp	w0, wzr
	ldr	x1, [x1, #:got_lo12:gpNandc]
	ldr	x2, [x1]
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
	adrp	x1, :got:gpNandc
	uxtb	w0, w0
	add	x29, sp, 0
	str	x21, [sp,32]
	tst	w0, 6
	ldr	x21, [x1, #:got_lo12:gpNandc]
	stp	x19, x20, [sp,16]
	mov	x20, x1
	ldr	x2, [x21]
	ldr	w19, [x2]
	beq	.L54
	orr	w19, w19, 24576
	and	w0, w0, 4
	and	w19, w19, -32769
	cmp	w0, wzr
	orr	w19, w19, 196608
	mov	w0, 0
	orr	w1, w19, 32768
	csel	w19, w1, w19, ne
	bl	rknand_get_clk_rate
	ldr	x0, [x21]
	mov	w1, 8322
	str	w1, [x0,344]
	mov	w1, 4099
	movk	w1, 0x10, lsl 16
	str	w1, [x0,304]
	mov	w1, 38
	str	w1, [x0,308]
	mov	w1, 39
	str	w1, [x0,308]
	b	.L56
.L54:
	and	w19, w19, -8193
.L56:
	ldr	x1, [x20, #:got_lo12:gpNandc]
	ldr	x0, [x1]
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
	adrp	x1, :got:gNandChipMap
	uxtb	w0, w0
	ldr	x1, [x1, #:got_lo12:gNandChipMap]
	ldr	x2, [x1]
	ldr	w1, [x2]
	bfi	w1, w0, 0, 8
	str	w1, [x2]
	ret
	.size	NandcFlashCsDebug, .-NandcFlashCsDebug
	.align	2
	.global	NandcFlashCs
	.type	NandcFlashCs, %function
NandcFlashCs:
	adrp	x1, :got:gNandChipMap
	ubfiz	x0, x0, 4, 8
	ldr	x1, [x1, #:got_lo12:gNandChipMap]
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
	adrp	x1, :got:gNandChipMap
	ubfiz	x0, x0, 4, 8
	ldr	x1, [x1, #:got_lo12:gNandChipMap]
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
.L63:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L63
	add	sp, sp, 16
	ret
	.size	NandcDelayns, .-NandcDelayns
	.align	2
	.global	FlashReadStatus
	.type	FlashReadStatus, %function
FlashReadStatus:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	bl	NandcGetChipIf
	mov	x19, x0
	mov	w0, 112
	str	w0, [x19,8]
	mov	w0, 80
	bl	NandcDelayns
	ldr	w0, [x19]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadStatus, .-FlashReadStatus
	.align	2
	.global	ToshibaSetRRPara
	.type	ToshibaSetRRPara, %function
ToshibaSetRRPara:
	stp	x29, x30, [sp, -96]!
	adrp	x2, :got:Toshiba15RefValue
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	x23, w1
	ldr	x2, [x2, #:got_lo12:Toshiba15RefValue]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	w20, 5
	mov	x21, x0
	add	w0, w23, 1
	mov	x19, 0
	stp	x25, x26, [sp,64]
	smull	x20, w0, w20
	adrp	x0, :got:ToshibaA19RefValue
	adrp	x25, :got:g_maxRegNum
	stp	x27, x28, [sp,80]
	mov	x22, x0
	mov	w26, 85
	ldr	x24, [x0, #:got_lo12:ToshibaA19RefValue]
	adrp	x27, :got:g_retryMode
	adrp	x28, :got:ToshibaRefValue
	add	x24, x20, x24
	add	x20, x20, x2
.L67:
	ldr	x0, [x25, #:got_lo12:g_maxRegNum]
	ldrb	w0, [x0]
	cmp	w0, w19
	bls	.L73
	str	w26, [x21,8]
	ldr	x0, [x22, #:got_lo12:ToshibaA19RefValue]
	ldrsb	w0, [x19,x0]
	str	w0, [x21,4]
	mov	w0, 200
	bl	NandcDelayns
	ldr	x0, [x27, #:got_lo12:g_retryMode]
	ldrb	w0, [x0]
	cmp	w0, 34
	bne	.L68
	ldrsb	w0, [x24,x19]
	b	.L72
.L68:
	cmp	w0, 35
	bne	.L70
	ldrsb	w0, [x20,x19]
	b	.L72
.L70:
	ldr	x0, [x28, #:got_lo12:ToshibaRefValue]
	ldrsb	w0, [x0,x23]
.L72:
	str	w0, [x21]
	add	x19, x19, 1
	b	.L67
.L73:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	2
	.global	SamsungSetRRPara
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	stp	x29, x30, [sp, -64]!
	uxtb	x1, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	mov	x21, x0
	adrp	x0, :got:SamsungRefValue
	stp	x19, x20, [sp,16]
	add	x1, x1, 1
	mov	x19, 0
	ldr	x20, [x0, #:got_lo12:SamsungRefValue]
	mov	x22, x0
	stp	x23, x24, [sp,48]
	add	x20, x20, x1, lsl 2
	adrp	x23, :got:g_maxRegNum
	mov	w24, 161
.L75:
	ldr	x0, [x23, #:got_lo12:g_maxRegNum]
	ldrb	w0, [x0]
	cmp	w0, w19
	bls	.L77
	str	w24, [x21,8]
	str	wzr, [x21]
	ldr	x0, [x22, #:got_lo12:SamsungRefValue]
	ldrsb	w0, [x19,x0]
	str	w0, [x21]
	ldrsb	w0, [x20,x19]
	add	x19, x19, 1
	str	w0, [x21]
	mov	w0, 300
	bl	NandcDelayns
	b	.L75
.L77:
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
	stp	x29, x30, [sp, -80]!
	adrp	x4, :got:gReadRetryInfo
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	x22, w0
	adrp	x0, :got:gpNandParaInfo
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	mov	x21, x22
	stp	x25, x26, [sp,64]
	uxtb	w20, w1
	mov	x24, x2
	ldr	x0, [x0]
	uxtb	w23, w3
	ldrb	w0, [x0,19]
	cmp	w0, 6
	bne	.L79
	ldr	x4, [x4, #:got_lo12:gReadRetryInfo]
	lsl	w19, w23, 2
	add	x4, x4, x22, lsl 6
	add	x4, x4, 20
	add	x19, x4, x19, sxtw
	b	.L80
.L79:
	ldr	x4, [x4, #:got_lo12:gReadRetryInfo]
	lsl	x19, x22, 3
	add	x19, x19, x23, sxtw
	add	x4, x4, x19, lsl 3
	add	x19, x4, 20
.L80:
	mov	w0, w21
	uxtb	x26, w20
	mov	x20, 0
	bl	NandcGetChipIf
	mov	x25, x0
	mov	w0, w21
	bl	NandcFlashCs
	mov	w0, 54
	str	w0, [x25,8]
.L81:
	cmp	x20, x26
	beq	.L83
	ldrb	w0, [x24,x20]
	str	w0, [x25,4]
	mov	w0, 200
	bl	NandcDelayns
	ldrb	w0, [x19,x20]
	add	x20, x20, 1
	str	w0, [x25]
	b	.L81
.L83:
	mov	w0, 22
	str	w0, [x25,8]
	mov	w0, w21
	bl	NandcFlashDeCs
	adrp	x0, :got:read_retry_cur_offset
	ldr	x0, [x0, #:got_lo12:read_retry_cur_offset]
	strb	w23, [x0,x22]
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
	adrp	x0, :got:gpNandParaInfo
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x19, 0
	ldr	x0, [x0]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 5
	bhi	.L84
	adrp	x20, :got:IDByte
	adrp	x21, :got:gReadRetryInfo
.L89:
	lsl	x1, x19, 3
	ldr	x2, [x20, #:got_lo12:IDByte]
	uxtb	w0, w19
	ldrb	w1, [x1,x2]
	cmp	w1, 173
	bne	.L86
	ldr	x2, [x21, #:got_lo12:gReadRetryInfo]
	mov	w3, 0
	add	x2, x2, 4
	ldrb	w1, [x2,-3]
	bl	HynixSetRRPara
.L86:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L89
.L84:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w20, w2
	str	x1, [x29,40]
	bl	NandcGetChipIf
	mov	x19, x0
	adrp	x0, :got:gpNandParaInfo
	ldr	x1, [x29,40]
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	bne	.L92
	adrp	x3, :got:gNandOptPara
	ldr	x0, [x3, #:got_lo12:gNandOptPara]
	cbnz	w20, .L93
	ldrb	w0, [x0,13]
	b	.L101
.L93:
	ldrb	w0, [x0,14]
.L101:
	str	w0, [x19,8]
	mov	w2, 0
	ldr	x0, [x3, #:got_lo12:gNandOptPara]
	ldrb	w0, [x0,15]
	cbz	w0, .L96
.L100:
	ldr	x0, [x3, #:got_lo12:gNandOptPara]
	ldrb	w0, [x0,15]
	cmp	w2, w0
	bcs	.L96
	lsl	w0, w2, 3
	add	w2, w2, 1
	lsr	w0, w1, w0
	and	w0, w0, 255
	str	w0, [x19,4]
	b	.L100
.L92:
	mov	w0, 112
	str	w0, [x19,8]
.L96:
	mov	w0, 80
	bl	NandcDelayns
	ldr	w0, [x19]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
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
.L106:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L106
	tbz	x3, 6, .L106
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
	adrp	x2, :got:gDieOp
	uxtb	x1, w0
	mov	x0, 24
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x2, [x2, #:got_lo12:gDieOp]
	mov	x20, x1
	mul	x1, x1, x0
	str	x21, [sp,32]
	add	x19, x2, x1
	ldrb	w21, [x2,x1]
	ldr	x0, [x19,8]
	cbz	x0, .L111
	mov	w0, w21
	bl	NandcFlashCs
	adrp	x1, :got:DieAddrs
	ldr	x1, [x1, #:got_lo12:DieAddrs]
	ldr	w0, [x1,w20,sxtw 2]
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
	cbz	x1, .L111
	str	w0, [x1]
	str	xzr, [x19,16]
.L111:
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.global	NandcWaitFlashReady
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	adrp	x1, :got:gNandChipMap
	ubfiz	x0, x0, 4, 8
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:gNandChipMap]
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	ldr	x20, [x1,x0]
	movk	w19, 0x1, lsl 16
.L118:
	mov	w0, 100
	bl	NandcDelayns
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L119
	subs	w19, w19, #1
	bne	.L118
	mov	w0, -1
	b	.L117
.L119:
	mov	w0, 0
.L117:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	NandcWaitFlashReady, .-NandcWaitFlashReady
	.align	2
	.global	FlashReset
	.type	FlashReset, %function
FlashReset:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w19, w0
	mov	w0, w19
	bl	NandcGetChipIf
	mov	x20, x0
	mov	w0, w19
	bl	NandcFlashCs
	mov	w0, 255
	str	w0, [x20,8]
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashDeCs
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w20, w0
	mov	x19, 0
	adrp	x25, :got:IDByte
	adrp	x21, :got:gFlashInterfaceMode
	mov	w22, 239
	mov	w23, 128
	mov	w24, 1
	mov	w26, 35
.L134:
	mov	w0, w19
	bl	NandcGetChipIf
	lsl	x1, x19, 3
	ldr	x2, [x25, #:got_lo12:IDByte]
	ldrb	w1, [x1,x2]
	cmp	w1, 152
	beq	.L124
	cmp	w1, 69
	beq	.L124
	cmp	w1, 173
	beq	.L124
	cmp	w1, 44
	bne	.L125
.L124:
	ldr	x2, [x21, #:got_lo12:gFlashInterfaceMode]
	cmp	w20, 1
	ldrb	w2, [x2]
	bne	.L127
	tbz	x2, 0, .L125
	str	w22, [x0,8]
	cmp	w1, 173
	bne	.L128
	str	w20, [x0,4]
	b	.L145
.L128:
	cmp	w1, 44
	bne	.L130
	str	w20, [x0,4]
	mov	w1, 5
	b	.L146
.L130:
	str	w23, [x0,4]
	str	w20, [x0]
	b	.L132
.L127:
	tbz	x2, 2, .L125
	str	w22, [x0,8]
	cmp	w1, 173
	bne	.L131
	str	w24, [x0,4]
	mov	w1, 32
.L146:
	str	w1, [x0]
	b	.L132
.L131:
	cmp	w1, 44
	bne	.L133
	str	w24, [x0,4]
	str	w26, [x0]
	b	.L132
.L133:
	str	w23, [x0,4]
.L145:
	str	wzr, [x0]
.L132:
	str	wzr, [x0]
	str	wzr, [x0]
	str	wzr, [x0]
.L125:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L134
	mov	w0, 0
	bl	NandcWaitFlashReady
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	2
	.global	FlashReadSpare
	.type	FlashReadSpare, %function
FlashReadSpare:
	adrp	x3, :got:gNandParaInfo
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	ldr	x3, [x3, #:got_lo12:gNandParaInfo]
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	uxtb	w21, w0
	mov	w0, w21
	str	x2, [x29,56]
	ldrb	w20, [x3,9]
	str	x1, [x29,48]
	bl	NandcGetChipIf
	str	wzr, [x0,8]
	mov	x19, x0
	ldr	x1, [x29,48]
	lsl	w20, w20, 9
	str	w20, [x0,4]
	lsr	w20, w20, 8
	str	w20, [x0,4]
	and	w0, w1, 255
	str	w0, [x19,4]
	lsr	w0, w1, 8
	str	w0, [x19,4]
	lsr	w1, w1, 16
	mov	w0, 48
	str	w1, [x19,4]
	str	w0, [x19,8]
	mov	w0, w21
	bl	NandcWaitFlashReady
	ldr	x2, [x29,56]
	ldr	w0, [x19]
	strb	w0, [x2]
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FlashReadSpare, .-FlashReadSpare
	.align	2
	.global	SandiskProgTestBadBlock
	.type	SandiskProgTestBadBlock, %function
SandiskProgTestBadBlock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w20, w0
	mov	w0, w20
	str	x1, [x29,40]
	bl	NandcGetChipIf
	mov	x19, x0
	mov	w0, 162
	ldr	x1, [x29,40]
	str	w0, [x19,8]
	mov	w0, 128
	str	w0, [x19,8]
	and	w0, w1, 255
	str	wzr, [x19,4]
	str	wzr, [x19,4]
	str	w0, [x19,4]
	lsr	w0, w1, 8
	str	w0, [x19,4]
	lsr	w1, w1, 16
	mov	w0, 16
	str	w1, [x19,4]
	str	w0, [x19,8]
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, 112
	str	w0, [x19,8]
	mov	w0, 80
	bl	NandcDelayns
	ldr	w0, [x19]
	ldp	x19, x20, [sp,16]
	and	w0, w0, 1
	ldp	x29, x30, [sp], 48
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
	adrp	x3, :got:ToshibaA19RefValue
	adrp	x2, :got:Toshiba15RefValue
	mov	w1, 5
	adrp	x4, :got:g_maxRegNum
	adrp	x5, :got:g_retryMode
	smull	x1, w19, w1
	ldr	x2, [x2, #:got_lo12:Toshiba15RefValue]
	ldr	x3, [x3, #:got_lo12:ToshibaA19RefValue]
	add	x3, x1, x3
	add	x1, x1, x2
	mov	x2, 0
.L150:
	ldr	x0, [x4, #:got_lo12:g_maxRegNum]
	ldrb	w0, [x0]
	cmp	w0, w2
	bls	.L155
	ldr	x0, [x5, #:got_lo12:g_retryMode]
	ldrb	w0, [x0]
	cmp	w0, 67
	bne	.L151
	ldrsb	w0, [x3,x2]
	b	.L154
.L151:
	ldrsb	w0, [x1,x2]
.L154:
	str	w0, [x20]
	add	x2, x2, 1
	b	.L150
.L155:
	mov	w0, 0
	bl	NandcWaitFlashReady
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	SandiskSetRRPara, .-SandiskSetRRPara
	.align	2
	.global	NandcRandmzSel
	.type	NandcRandmzSel, %function
NandcRandmzSel:
	adrp	x2, :got:gNandChipMap
	ubfiz	x0, x0, 4, 8
	ldr	x2, [x2, #:got_lo12:gNandChipMap]
	ldr	x0, [x2,x0]
	str	w1, [x0,336]
	ret
	.size	NandcRandmzSel, .-NandcRandmzSel
	.align	2
	.global	FlashSetRandomizer
	.type	FlashSetRandomizer, %function
FlashSetRandomizer:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x21, [sp,32]
	uxtb	w21, w0
	adrp	x0, :got:gNandcVer
	stp	x19, x20, [sp,16]
	mov	w19, w1
	ldr	x0, [x0, #:got_lo12:gNandcVer]
	ldr	w0, [x0]
	cmp	w0, 5
	bls	.L158
	adrp	x0, :got:random_seed
	and	w19, w1, 127
	ldr	x0, [x0, #:got_lo12:random_seed]
	ldrh	w19, [x0,w19,uxtw 1]
	adrp	x0, :got:gNandRandomizer
	ldr	x0, [x0, #:got_lo12:gNandRandomizer]
	ldrb	w0, [x0]
	cbz	w0, .L159
	mov	w0, w21
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w19, -1073741824
	csel	w19, w1, w19, ne
.L159:
	mov	w0, w21
	mov	w1, w19
	b	.L173
.L158:
	cmp	w0, 4
	bne	.L157
	adrp	x0, :got:random_seed
	and	w2, w1, 127
	ldr	x0, [x0, #:got_lo12:random_seed]
	ldrh	w20, [x0,w2,uxtw 1]
	mov	w0, w21
	lsl	w20, w20, 8
	bl	FlashRsvdBlkChk
	cbz	w0, .L161
	adrp	x0, :got:gNandRandomizer
	ldr	x0, [x0, #:got_lo12:gNandRandomizer]
	ldrb	w0, [x0]
	cbz	w0, .L161
	ubfiz	w19, w19, 1, 7
	orr	w19, w19, 1
	orr	w20, w20, w19
.L161:
	mov	w0, w21
	mov	w1, w20
.L173:
	bl	NandcRandmzSel
.L157:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	2
	.global	FlashReadCmd
	.type	FlashReadCmd, %function
FlashReadCmd:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxtb	w19, w0
	mov	w0, w19
	str	x1, [x29,40]
	bl	NandcGetChipIf
	str	wzr, [x0,8]
	str	wzr, [x0,4]
	ldr	x1, [x29,40]
	str	wzr, [x0,4]
	and	w2, w1, 255
	str	w2, [x0,4]
	lsr	w2, w1, 8
	str	w2, [x0,4]
	lsr	w2, w1, 16
	str	w2, [x0,4]
	mov	w2, 48
	str	w2, [x0,8]
	mov	w0, w19
	bl	FlashSetRandomizer
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashReadCmd, .-FlashReadCmd
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	stp	x29, x30, [sp, -144]!
	mov	w4, -83
	mov	w3, -82
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:gReadRetryInfo
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	ldr	x1, [x19, #:got_lo12:gReadRetryInfo]
	mov	w28, w0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	strb	w0, [x1]
	mov	w0, -84
	strb	w4, [x1,5]
	cmp	w28, 2
	strb	w0, [x1,4]
	mov	w0, -81
	strb	w3, [x1,6]
	strb	w0, [x1,7]
	bne	.L176
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, :got:refValueDefault
	mov	w1, -9
	ldr	x0, [x0, #:got_lo12:refValueDefault]
	strb	w1, [x0,17]
	b	.L213
.L176:
	cmp	w28, 3
	bne	.L178
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
	b	.L229
.L178:
	cmp	w28, 4
	bne	.L179
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
.L229:
	mov	w21, 8
	strb	w0, [x1,11]
	mov	w20, w21
	b	.L177
.L179:
	cmp	w28, 5
	bne	.L180
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w21, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L228
.L180:
	cmp	w28, 6
	bne	.L213
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w21, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L228
.L213:
	mov	w21, 7
.L228:
	mov	w20, 4
.L177:
	sub	w0, w28, #1
	cmp	w0, 1
	bhi	.L225
	mov	w27, 0
	adrp	x23, :got:gNandMaxDie
	adrp	x24, :got:DieCsIndex
	mov	w25, 55
	adrp	x26, :got:refValueDefault
.L181:
	ldr	x0, [x23, #:got_lo12:gNandMaxDie]
	ldrb	w0, [x0]
	cmp	w0, w27
	bls	.L188
	ldr	x0, [x24, #:got_lo12:DieCsIndex]
	mov	x28, 0
	ldr	x22, [x19, #:got_lo12:gReadRetryInfo]
	ldrb	w0, [x0,w27,sxtw]
	sbfiz	x1, x0, 6, 32
	add	x22, x22, x1
	add	x22, x22, 20
	bl	NandcGetChipIf
	mov	x1, x0
.L183:
	str	w25, [x1,8]
	str	x1, [x29,136]
	ldr	x0, [x19, #:got_lo12:gReadRetryInfo]
	add	x0, x28, x0
	ldrb	w0, [x0,4]
	str	w0, [x1,4]
	mov	w0, 80
	bl	NandcDelayns
	ldr	x1, [x29,136]
	ldr	w0, [x1]
	strb	w0, [x22,x28]
	add	x28, x28, 1
	cmp	w20, w28, uxtb
	bhi	.L183
	mov	x0, 0
.L184:
	add	w2, w0, 8
	mov	x1, 0
.L185:
	ldr	x3, [x26, #:got_lo12:refValueDefault]
	add	x4, x1, x0
	add	x1, x1, 4
	add	x3, x4, x3
	cmp	x1, 24
	ldrb	w4, [x3,4]
	ldrb	w3, [x22,x0]
	add	w3, w4, w3
	strb	w3, [x22,w2,sxtw]
	add	w2, w2, 8
	bne	.L185
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L184
	add	w27, w27, 1
	strb	wzr, [x22,16]
	strb	wzr, [x22,24]
	strb	wzr, [x22,32]
	uxtb	w27, w27
	strb	wzr, [x22,40]
	strb	wzr, [x22,48]
	strb	wzr, [x22,41]
	strb	wzr, [x22,49]
	b	.L181
.L225:
	sub	w0, w28, #3
	cmp	w0, 3
	bhi	.L188
	mul	w0, w20, w21
	sub	w25, w20, #1
	cmp	w28, 6
	asr	w24, w0, 2
	lsl	w0, w0, 4
	str	w0, [x29,136]
	lsl	w0, w24, 1
	mov	w1, 4
	str	w0, [x29,128]
	mov	w0, 8
	uxtb	x25, w25
	csel	w0, w1, w0, eq
	mov	w22, 0
	str	w0, [x29,132]
	sub	w27, w28, #5
	add	x0, x25, 1
	str	x0, [x29,120]
.L189:
	adrp	x0, :got:gNandMaxDie
	ldr	x0, [x0, #:got_lo12:gNandMaxDie]
	ldrb	w0, [x0]
	cmp	w0, w22
	bhi	.L212
.L188:
	ldr	x19, [x19, #:got_lo12:gReadRetryInfo]
	strb	w20, [x19,1]
	strb	w21, [x19,2]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L212:
	adrp	x0, :got:DieCsIndex
	ldr	x3, [x19, #:got_lo12:gReadRetryInfo]
	str	x3, [x29,104]
	ldr	x0, [x0, #:got_lo12:DieCsIndex]
	ldrb	w23, [x0,w22,sxtw]
	mov	w0, w23
	sbfiz	x26, x23, 6, 32
	add	x26, x3, x26
	bl	NandcGetChipIf
	mov	x25, x0
	mov	w4, 255
	str	w4, [x0,8]
	mov	w0, w23
	str	x4, [x29,112]
	add	x26, x26, 20
	bl	NandcWaitFlashReady
	mov	w0, 54
	cmp	w28, 4
	str	w0, [x25,8]
	ldr	x4, [x29,112]
	ldr	x3, [x29,104]
	bne	.L190
	mov	w0, 64
	str	w4, [x25,4]
	str	w0, [x25]
	mov	w0, 204
	b	.L230
.L190:
	cmp	w27, 1
	bhi	.L192
	ldrb	w0, [x3,4]
	str	w0, [x25,4]
	mov	w0, 82
	b	.L231
.L192:
	mov	w0, 174
	str	w0, [x25,4]
	str	wzr, [x25]
	mov	w0, 176
.L230:
	str	w0, [x25,4]
	mov	w0, 77
.L231:
	str	w0, [x25]
	mov	w0, 22
	str	w0, [x25,8]
	mov	w0, 23
	str	w0, [x25,8]
	mov	w0, 4
	str	w0, [x25,8]
	mov	w0, 25
	str	w0, [x25,8]
	cmp	w28, 6
	str	wzr, [x25,8]
	str	wzr, [x25,4]
	str	wzr, [x25,4]
	bne	.L193
	mov	w0, 31
	str	w0, [x25,4]
	b	.L194
.L193:
	str	wzr, [x25,4]
.L194:
	mov	w3, 2
	str	w3, [x25,4]
	str	wzr, [x25,4]
	mov	w0, 48
	str	w0, [x25,8]
	mov	w0, w23
	str	x3, [x29,112]
	bl	NandcWaitFlashReady
	ldr	x3, [x29,112]
	mov	w0, 16
	cmp	w27, 1
	csel	w3, w3, w0, hi
	mov	w0, 0
.L196:
	add	w0, w0, 1
	ldr	w4, [x25]
	uxtb	w0, w0
	cmp	w0, w3
	bne	.L196
	adrp	x4, :got:gFlashPageBuffer0
	ldr	x0, [x4, #:got_lo12:gFlashPageBuffer0]
	ldr	x7, [x0]
	mov	x0, 0
.L197:
	ldr	w1, [x29,136]
	cmp	w1, w0
	ble	.L232
	ldr	w3, [x25]
	strb	w3, [x7,x0]
	add	x0, x0, 1
	b	.L197
.L232:
	ldr	x0, [x4, #:got_lo12:gFlashPageBuffer0]
	mov	w6, w24
	mov	w5, 8
	ldr	x9, [x0]
.L200:
	mov	w0, 0
.L199:
	add	w3, w0, w6
	add	w0, w0, 1
	sbfiz	x3, x3, 2, 32
	cmp	w0, w24
	ldr	w8, [x9,x3]
	mvn	w8, w8
	str	w8, [x9,x3]
	bne	.L199
	ldr	w0, [x29,128]
	subs	w5, w5, #1
	add	w6, w6, w0
	bne	.L200
	mov	x5, 0
	mov	w13, 1
.L201:
	ldr	x0, [x4, #:got_lo12:gFlashPageBuffer0]
	mov	w3, 0
	mov	w8, w3
	ldr	x12, [x0]
.L205:
	lsl	w6, w13, w8
	mov	w10, w5
	mov	w0, 16
	mov	w9, 0
.L203:
	ldr	w11, [x12,w10,sxtw 2]
	add	w10, w10, w24
	and	w11, w6, w11
	cmp	w11, w6
	csinc	w9, w9, w9, ne
	subs	w0, w0, #1
	bne	.L203
	cmp	w9, 9
	orr	w6, w3, w6
	add	w8, w8, 1
	csel	w3, w6, w3, cs
	cmp	w8, 32
	bne	.L205
	str	w3, [x12,x5,lsl 2]
	add	x5, x5, 1
	cmp	w24, w5
	bgt	.L201
	mov	w4, w0
.L208:
	mov	x3, 0
.L207:
	add	w5, w0, w3
	ldrb	w6, [x7,x3]
	add	x3, x3, 1
	cmp	w20, w3, uxtb
	strb	w6, [x26,w5,sxtw]
	bhi	.L207
	ldr	x1, [x29,120]
	add	w4, w4, 1
	cmp	w4, w21
	add	x7, x7, x1
	ldr	w1, [x29,132]
	add	w0, w0, w1
	blt	.L208
	mov	w0, 255
	str	w0, [x25,8]
	mov	w0, w23
	bl	NandcWaitFlashReady
	cmp	w27, 1
	bhi	.L210
	mov	w0, 54
	str	w0, [x25,8]
	mov	w1, -1
	ldr	x0, [x19, #:got_lo12:gReadRetryInfo]
	ldrb	w0, [x0,4]
	str	w0, [x25,4]
	mov	w0, 22
	str	wzr, [x25]
	str	w0, [x25,8]
	mov	w0, w22
	bl	FlashReadCmd
	b	.L211
.L210:
	mov	w0, 56
	str	w0, [x25,8]
.L211:
	mov	w0, w23
	add	w22, w22, 1
	bl	NandcWaitFlashReady
	uxtb	w22, w22
	b	.L189
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashReadDpDataOutCmd
	.type	FlashReadDpDataOutCmd, %function
FlashReadDpDataOutCmd:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxtb	w19, w0
	mov	w0, w19
	str	x1, [x29,40]
	bl	NandcGetChipIf
	adrp	x2, :got:gNandOptPara
	ldr	x1, [x29,40]
	and	w4, w1, 255
	lsr	w3, w1, 8
	ldr	x2, [x2, #:got_lo12:gNandOptPara]
	ldrb	w2, [x2,16]
	cmp	w2, 1
	lsr	w2, w1, 16
	bne	.L234
	mov	w5, 6
	str	w5, [x0,8]
	str	wzr, [x0,4]
	str	wzr, [x0,4]
	str	w4, [x0,4]
	str	w3, [x0,4]
	str	w2, [x0,4]
	b	.L236
.L234:
	str	wzr, [x0,8]
	str	wzr, [x0,4]
	str	wzr, [x0,4]
	str	w4, [x0,4]
	str	w3, [x0,4]
	str	w2, [x0,4]
	mov	w2, 5
	str	w2, [x0,8]
	str	wzr, [x0,4]
	str	wzr, [x0,4]
.L236:
	mov	w2, 224
	str	w2, [x0,8]
	mov	w0, w19
	bl	FlashSetRandomizer
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	2
	.global	FlashReadDpCmd
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	str	x23, [sp,48]
	mov	w0, w21
	str	x1, [x29,64]
	stp	x19, x20, [sp,16]
	str	x2, [x29,72]
	bl	NandcGetChipIf
	adrp	x3, :got:gNandOptPara
	mov	x19, x0
	ldr	x1, [x29,64]
	ldr	x2, [x29,72]
	ldr	x3, [x3, #:got_lo12:gNandOptPara]
	and	w5, w1, 255
	lsr	w4, w1, 8
	and	w23, w2, 255
	lsr	w22, w2, 8
	lsr	w20, w2, 16
	ldrb	w0, [x3,16]
	cmp	w0, 1
	lsr	w0, w1, 16
	bne	.L238
	ldrb	w2, [x3,8]
	str	w2, [x19,8]
	str	wzr, [x19,4]
	str	wzr, [x19,4]
	str	w5, [x19,4]
	str	w4, [x19,4]
	str	w0, [x19,4]
	str	x1, [x29,72]
	ldrb	w0, [x3,9]
	str	w0, [x19,8]
	mov	w0, w21
	bl	NandcWaitFlashReady
	str	wzr, [x19,8]
	str	wzr, [x19,4]
	mov	w0, 48
	str	wzr, [x19,4]
	str	w23, [x19,4]
	str	w22, [x19,4]
	str	w20, [x19,4]
	str	w0, [x19,8]
	ldr	x1, [x29,72]
	b	.L239
.L238:
	ldrb	w2, [x3,8]
	str	w2, [x19,8]
	str	w5, [x19,4]
	str	w4, [x19,4]
	str	w0, [x19,4]
	ldrb	w0, [x3,9]
	str	w0, [x19,8]
	mov	w0, 48
	str	w23, [x19,4]
	str	w22, [x19,4]
	str	w20, [x19,4]
	str	w0, [x19,8]
.L239:
	mov	w0, w21
	bl	FlashSetRandomizer
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	2
	.global	FlashProgFirstCmd
	.type	FlashProgFirstCmd, %function
FlashProgFirstCmd:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxtb	w19, w0
	mov	w0, w19
	str	x1, [x29,40]
	bl	NandcGetChipIf
	mov	w2, 128
	str	w2, [x0,8]
	str	wzr, [x0,4]
	ldr	x1, [x29,40]
	str	wzr, [x0,4]
	and	w2, w1, 255
	str	w2, [x0,4]
	lsr	w2, w1, 8
	str	w2, [x0,4]
	lsr	w2, w1, 16
	str	w2, [x0,4]
	mov	w0, w19
	bl	FlashSetRandomizer
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashProgFirstCmd, .-FlashProgFirstCmd
	.align	2
	.global	FlashProgDpSecondCmd
	.type	FlashProgDpSecondCmd, %function
FlashProgDpSecondCmd:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxtb	w19, w0
	mov	w0, w19
	str	x1, [x29,40]
	bl	NandcGetChipIf
	adrp	x2, :got:gNandOptPara
	ldr	x1, [x29,40]
	ldr	x2, [x2, #:got_lo12:gNandOptPara]
	ldrb	w2, [x2,11]
	str	w2, [x0,8]
	and	w2, w1, 255
	str	wzr, [x0,4]
	str	wzr, [x0,4]
	str	w2, [x0,4]
	lsr	w2, w1, 8
	str	w2, [x0,4]
	lsr	w2, w1, 16
	str	w2, [x0,4]
	mov	w0, w19
	bl	FlashSetRandomizer
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashProgDpSecondCmd, .-FlashProgDpSecondCmd
	.align	2
	.global	FlashDeInit
	.type	FlashDeInit, %function
FlashDeInit:
	stp	x29, x30, [sp, -32]!
	mov	w0, 0
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, :got:gFlashToggleModeEn
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	x19, [x19, #:got_lo12:gFlashToggleModeEn]
	ldrb	w0, [x19]
	cbz	w0, .L243
	adrp	x0, :got:gFlashInterfaceMode
	ldr	x0, [x0, #:got_lo12:gFlashInterfaceMode]
	ldrb	w0, [x0]
	tbz	x0, 0, .L243
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x19]
.L243:
	mov	w0, 0
	mov	w1, 0
	bl	NandcRandmzSel
	ldr	x19, [sp,16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashDeInit, .-FlashDeInit
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
	adrp	x1, :got:gpNandc
	movk	w2, 0xf, lsl 16
	sdiv	w0, w0, w2
	ldr	x1, [x1, #:got_lo12:gpNandc]
	cmp	w0, 250
	ble	.L251
	ldr	x0, [x1]
	mov	w1, 8354
	b	.L258
.L251:
	cmp	w0, 220
	ble	.L253
	ldr	x0, [x1]
	b	.L259
.L253:
	cmp	w0, 185
	ble	.L254
	ldr	x0, [x1]
	mov	w1, 4226
	b	.L258
.L254:
	cmp	w0, 160
	ldr	x0, [x1]
	ble	.L255
	mov	w1, 4194
	b	.L258
.L255:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L258
	cmp	w19, 99
	mov	w1, 4225
	bls	.L258
.L259:
	mov	w1, 8322
.L258:
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
	bls	.L261
	mov	w1, 8322
	cmp	w0, w1
	bne	.L262
.L261:
	adrp	x1, :got:gpNandc
	ldr	x1, [x1, #:got_lo12:gpNandc]
	ldr	x1, [x1]
	str	w0, [x1,4]
.L262:
	adrp	x0, :got:gNandParaInfo
	ldr	x0, [x0, #:got_lo12:gNandParaInfo]
	ldrb	w0, [x0,21]
	bl	NandcTimeCfg
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTimingCfg, .-FlashTimingCfg
	.align	2
	.global	NandcGetTimeCfg
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	adrp	x4, :got:gpNandc
	ldr	x4, [x4, #:got_lo12:gpNandc]
	ldr	x5, [x4]
	ldr	w5, [x5,4]
	str	w5, [x0]
	ldr	x0, [x4]
	ldr	w0, [x0]
	str	w0, [x1]
	ldr	x0, [x4]
	ldr	w0, [x0,304]
	str	w0, [x2]
	ldr	x1, [x4]
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
	adrp	x1, :got:gpNandc
	uxtb	w0, w0
	cmp	w0, 16
	ldr	x1, [x1, #:got_lo12:gpNandc]
	ldr	x2, [x1]
	mov	w1, 1
	str	w1, [x2,8]
	adrp	x1, :got:gNandcEccBits
	ldr	x1, [x1, #:got_lo12:gNandcEccBits]
	str	w0, [x1]
	mov	w1, 4096
	bne	.L265
.L268:
	and	w1, w1, -17
	b	.L266
.L265:
	cmp	w0, 24
	bne	.L267
	orr	w1, w1, 16
	b	.L266
.L267:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L268
.L266:
	orr	w1, w1, 1
	str	w1, [x2,12]
	ret
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	adrp	x1, :got:gNandFlashEccBits
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:gNandFlashEccBits]
	strb	w0, [x1]
	bl	NandcBchSel
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashBchSel, .-FlashBchSel
	.align	2
	.global	FlashResume
	.type	FlashResume, %function
FlashResume:
	adrp	x1, :got:gpNandc
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:gpNandc]
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	mov	x20, 0
	ldr	x2, [x1]
	adrp	x21, :got:IDByte
	ldr	w3, [x19,#:lo12:.LANCHOR0]
	str	w3, [x2]
	ldr	w3, [x0,4]
	ldr	x2, [x1]
	str	w3, [x2,4]
	ldr	w2, [x0,8]
	ldr	x1, [x1]
	str	w2, [x1,8]
	ldr	w2, [x0,12]
	str	w2, [x1,12]
	ldr	w2, [x0,16]
	str	w2, [x1,304]
	ldr	w2, [x0,20]
	str	w2, [x1,308]
	ldr	w2, [x0,24]
	str	w2, [x1,336]
	ldr	w0, [x0,28]
	str	w0, [x1,344]
.L275:
	lsl	x0, x20, 3
	ldr	x1, [x21, #:got_lo12:IDByte]
	ldrb	w0, [x0,x1]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L274
	mov	w0, w20
	bl	FlashReset
.L274:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L275
	adrp	x0, :got:gFlashToggleModeEn
	ldr	x0, [x0, #:got_lo12:gFlashToggleModeEn]
	ldrb	w0, [x0]
	cbz	w0, .L276
	adrp	x20, :got:gFlashInterfaceMode
	mov	w0, 1
	add	x19, x19, :lo12:.LANCHOR0
	bl	NandcSetMode
	ldr	x20, [x20, #:got_lo12:gFlashInterfaceMode]
	ldrb	w0, [x20]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20]
	bl	NandcSetMode
	ldr	w0, [x19,16]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L276:
	adrp	x0, :got:gpNandParaInfo
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
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
	tbnz	x0, 9, .L286
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L285
.L286:
	mov	x0, x19
	mov	w1, 1
	bl	NandCIrqDisable
.L285:
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
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	mov	x20, x0
	mov	w21, w1
	mov	w19, 0
	mov	w22, 56
	add	x25, x29, 120
	add	x26, x29, 124
	adrp	x27, :got:gMultiPageProgEn
	adrp	x28, :got:gDieOp
.L289:
	cmp	w19, w21
	adrp	x23, :got:gNandMaxDie
	bcs	.L304
	umull	x5, w19, w22
	mov	w1, 0
	sub	w4, w21, w19
	add	x23, x20, x5
	mov	x2, x25
	mov	x0, x23
	mov	x3, x26
	str	x5, [x29,104]
	bl	LogAddr2PhyAddr
	mov	w24, w0
	adrp	x1, :got:gNandMaxDie
	ldr	w0, [x29,124]
	ldr	x5, [x29,104]
	ldr	x1, [x1, #:got_lo12:gNandMaxDie]
	ldrb	w1, [x1]
	cmp	w0, w1
	bcc	.L290
	mov	w0, -1
	str	w0, [x20,x5]
	b	.L291
.L290:
	ldr	x1, [x27, #:got_lo12:gMultiPageProgEn]
	mov	x5, 24
	ldr	x4, [x28, #:got_lo12:gDieOp]
	ldrb	w1, [x1]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x1, x1, x5, x4
	adrp	x4, :got:gDieOp
	ldr	x1, [x1,8]
	cbz	x1, .L293
	str	x4, [x29,104]
	bl	FlashWaitCmdDone
	ldr	x4, [x29,104]
.L293:
	ldr	x2, [x4, #:got_lo12:gDieOp]
	mov	x1, 24
	ldr	w0, [x29,124]
	madd	x1, x0, x1, x2
	ldr	w2, [x29,120]
	str	x23, [x1,8]
	str	xzr, [x1,16]
	str	w2, [x1,4]
	cbz	w24, .L294
	add	w2, w19, 1
	umull	x2, w2, w22
	add	x2, x20, x2
	str	x2, [x1,16]
.L294:
	adrp	x1, :got:DieCsIndex
	ldr	x4, [x4, #:got_lo12:gDieOp]
	add	w19, w19, w24
	ldr	x1, [x1, #:got_lo12:DieCsIndex]
	ldrb	w23, [x1,x0]
	mov	x1, 24
	mul	x0, x0, x1
	strb	w23, [x4,x0]
	mov	w0, w23
	bl	NandcFlashCs
	adrp	x0, :got:DieAddrs
	ldr	w1, [x29,124]
	ldr	x0, [x0, #:got_lo12:DieAddrs]
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,120]
	cmp	w0, wzr
	mov	w0, w23
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w1, [x29,120]
	mov	w0, w23
	mov	w2, w24
	bl	FlashEraseCmd
	mov	w0, w23
	bl	NandcFlashDeCs
.L291:
	add	w19, w19, 1
	b	.L289
.L304:
	adrp	x0, :got:gpNandc
	mov	w19, 0
	ldr	x0, [x0, #:got_lo12:gpNandc]
	ldr	x0, [x0]
	bl	NandcIqrWaitFlashReady
.L296:
	ldr	x0, [x23, #:got_lo12:gNandMaxDie]
	ldrb	w0, [x0]
	cmp	w19, w0
	bcs	.L305
	mov	w0, w19
	add	w19, w19, 1
	bl	FlashWaitCmdDone
	b	.L296
.L305:
	mov	w0, 0
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
.L309:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L309
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
	str	x25, [sp,64]
	uxtb	w25, w1
	adrp	x1, :got:gNandChipMap
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	ldr	x1, [x1, #:got_lo12:gNandChipMap]
	ubfiz	w19, w25, 1, 1
	stp	x23, x24, [sp,48]
	uxtb	w23, w2
	add	x2, x1, x0
	ldr	x21, [x1,x0]
	mov	w1, 16
	orr	w19, w19, 8
	mov	x22, x4
	ldrb	w0, [x2,8]
	ldr	w24, [x21,12]
	bfi	w24, w1, 8, 8
	and	w24, w24, -9
	bfi	w24, w0, 5, 3
	mov	w0, 1
	bfi	w19, w0, 5, 2
	adrp	x0, :got:gNandcVer
	orr	w19, w19, 536870912
	orr	w19, w19, 1024
	ldr	x0, [x0, #:got_lo12:gNandcVer]
	bfi	w19, w3, 4, 1
	ldr	w0, [x0]
	cmp	w0, 3
	bls	.L313
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w6, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w6, .L327
	cbz	x4, .L314
.L327:
	adrp	x20, :got:gMasterInfo
	cbnz	w25, .L316
.L324:
	mov	x0, x21
	add	w23, w23, 1
	asr	w23, w23, 1
	bl	rk_nandc_xfer_irq_flag_init
	bfi	w19, w23, 22, 6
	mov	x0, x21
	mov	w1, 0
	bl	NandCIrqEnable
	mov	x0, x22
	cbnz	x22, .L318
	ldr	x0, [x20, #:got_lo12:gMasterInfo]
	ldr	x0, [x0]
	b	.L318
.L316:
	adrp	x0, :got:gNandcEccBits
	mov	w3, 128
	mov	w1, 0
	lsr	w7, w23, 1
	mov	w2, w1
	mov	w8, -1
	ldr	x0, [x0, #:got_lo12:gNandcEccBits]
	ldr	w0, [x0]
	cmp	w0, 25
	mov	w0, 64
	csel	w3, w0, w3, cc
.L320:
	cmp	w2, w7
	bcs	.L324
	lsr	w0, w1, 2
	ldr	x4, [x20, #:got_lo12:gMasterInfo]
	ubfiz	x0, x0, 2, 30
	cbz	w6, .L321
	ldr	x9, [x4,8]
	ldrh	w4, [x5,2]
	ldrh	w10, [x5],4
	orr	w4, w10, w4, lsl 16
	str	w4, [x9,x0]
	b	.L322
.L321:
	ldr	x4, [x4,8]
	str	w8, [x4,x0]
.L322:
	add	w2, w2, 1
	add	w1, w1, w3
	b	.L320
.L318:
	ldr	x20, [x20, #:got_lo12:gMasterInfo]
	ubfx	x23, x19, 22, 5
	mov	w2, w25
	and	x22, x22, 3
	ldr	x1, [x20,8]
	str	x1, [x20,24]
	lsl	w1, w23, 10
	str	x0, [x20,16]
	bl	rknand_dma_map_single
	str	w0, [x20,32]
	ldr	x0, [x20,24]
	lsl	w1, w23, 7
	mov	w2, w25
	bl	rknand_dma_map_single
	str	w0, [x20,36]
	mov	w0, 1
	str	w0, [x20,40]
	ldr	w0, [x20,32]
	mov	w1, 16
	str	w0, [x21,20]
	ldr	w0, [x20,36]
	str	w0, [x21,24]
	str	wzr, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 9, 5
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 448
	str	w0, [x29,88]
	cbnz	x22, .L325
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L325:
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
.L314:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L313:
	str	w24, [x21,12]
	str	w19, [x21,8]
	orr	w19, w19, 4
	str	w19, [x21,8]
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
.L332:
	cmp	w2, w0
	bhi	.L334
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L332
.L334:
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
	adrp	x2, :got:c_ftl_nand_max_sys_blks
	csel	w0, w0, w1, hi
	adrp	x1, :got:c_ftl_nand_sys_blks_per_plane
	adrp	x3, :got:c_ftl_nand_blk_pre_plane
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_sys_blks_per_plane]
	str	w0, [x1]
	adrp	x1, :got:c_ftl_nand_planes_num
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_max_sys_blks]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w1, [x1]
	mul	w1, w0, w1
	str	w1, [x2]
	adrp	x2, :got:c_ftl_nand_data_blks_per_plane
	ldr	x3, [x3, #:got_lo12:c_ftl_nand_blk_pre_plane]
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w3, [x3]
	sub	w0, w3, w0
	strh	w0, [x2]
	adrp	x2, :got:c_ftl_nand_totle_phy_blks
	adrp	x0, :got:c_ftl_nand_max_data_blks
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_totle_phy_blks]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_max_data_blks]
	ldr	w2, [x2]
	sub	w1, w2, w1
	str	w1, [x0]
	mov	w0, 0
	ret
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	stp	x29, x30, [sp, -96]!
	adrp	x3, :got:c_ftl_nand_planes_per_die
	adrp	x6, :got:p_plane_order_table
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	adrp	x20, :got:c_ftl_nand_blk_pre_plane
	ldrh	w7, [x0,8]
	adrp	x0, :got:c_ftl_nand_type
	ldrh	w2, [x19,10]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_type]
	ldrh	w5, [x19,14]
	strh	w7, [x0]
	adrp	x0, :got:c_ftl_nand_die_num
	ldr	x1, [x0, #:got_lo12:c_ftl_nand_die_num]
	strh	w2, [x1]
	ldrh	w1, [x19,12]
	ldr	x3, [x3, #:got_lo12:c_ftl_nand_planes_per_die]
	strh	w1, [x3]
	ldr	x3, [x20, #:got_lo12:c_ftl_nand_blk_pre_plane]
	strh	w5, [x3]
	mov	x3, 0
.L339:
	ldr	x4, [x6, #:got_lo12:p_plane_order_table]
	adrp	x13, :got:p_plane_order_table
	strb	w3, [x3,x4]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L339
	ldrh	w4, [x19,20]
	ldrb	w3, [x19,15]
	cmp	w4, w3
	bcs	.L340
	uxtb	w9, w1
	mov	w4, 0
	mul	w12, w1, w2
	ubfiz	w11, w9, 1, 7
.L341:
	cmp	w4, w1
	bcs	.L343
	uxtb	w3, w4
	mov	w6, w4
	mov	w8, 0
.L344:
	cmp	w8, w2
	bcs	.L357
	ldr	x10, [x13, #:got_lo12:p_plane_order_table]
	add	w14, w6, w12
	add	w15, w3, w9
	add	w8, w8, 1
	strb	w3, [x10,w6,uxtw]
	add	w3, w3, w11
	strb	w15, [x10,x14]
	add	w6, w6, w1
	uxtb	w3, w3
	b	.L344
.L357:
	add	w4, w4, 1
	b	.L341
.L343:
	ldr	x3, [x0, #:got_lo12:c_ftl_nand_die_num]
	lsl	w2, w2, 1
	lsr	w5, w5, 1
	strh	w2, [x3]
	ldr	x2, [x20, #:got_lo12:c_ftl_nand_blk_pre_plane]
	strh	w5, [x2]
.L340:
	adrp	x2, :got:c_mlc_erase_count_value
	adrp	x21, :got:c_ftl_nand_ext_blk_pre_plane
	mov	w3, 5
	cmp	w7, 1
	ldr	x2, [x2, #:got_lo12:c_mlc_erase_count_value]
	strh	w3, [x2]
	ldr	x3, [x21, #:got_lo12:c_ftl_nand_ext_blk_pre_plane]
	strh	wzr, [x3]
	bne	.L345
	strh	w7, [x2]
.L345:
	adrp	x23, :got:c_ftl_vendor_part_size
	mov	w2, 4352
	ldr	x3, [x23, #:got_lo12:c_ftl_vendor_part_size]
	strh	w2, [x3]
	adrp	x2, :got:g_slc2KBNand
	ldr	x2, [x2, #:got_lo12:g_slc2KBNand]
	ldrb	w2, [x2]
	cbz	w2, .L346
	mov	w2, 384
	strh	w2, [x3]
.L346:
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_die_num]
	adrp	x24, :got:c_ftl_nand_planes_num
	adrp	x25, :got:c_ftl_nand_page_pre_blk
	adrp	x28, :got:c_ftl_nand_page_pre_slc_blk
	adrp	x27, :got:c_ftl_nand_sec_pre_page
	adrp	x22, :got:c_ftl_nand_sec_pre_page_shift
	ldrh	w0, [x0]
	ldr	x24, [x24, #:got_lo12:c_ftl_nand_planes_num]
	mul	w0, w1, w0
	strh	w0, [x24]
	ldr	x20, [x20, #:got_lo12:c_ftl_nand_blk_pre_plane]
	ldrh	w0, [x20]
	mul	w1, w1, w0
	adrp	x0, :got:c_ftl_nand_blks_per_die
	uxth	w1, w1
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_blks_per_die]
	strh	w1, [x0]
	mov	w0, w1
	bl	Ftl_log2
	adrp	x1, :got:c_ftl_nand_blks_per_die_shift
	ldrh	w2, [x19,18]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_blks_per_die_shift]
	strh	w0, [x1]
	ldrh	w0, [x19,16]
	ldr	x1, [x25, #:got_lo12:c_ftl_nand_page_pre_blk]
	strh	w0, [x1]
	ldr	x1, [x28, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	strh	w2, [x1]
	adrp	x1, :got:c_ftl_nand_page_pre_super_blk
	ldrh	w2, [x24]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_page_pre_super_blk]
	mul	w0, w0, w2
	strh	w0, [x1]
	ldrh	w0, [x19,20]
	ldr	x26, [x27, #:got_lo12:c_ftl_nand_sec_pre_page]
	strh	w0, [x26]
	bl	Ftl_log2
	ldr	x1, [x22, #:got_lo12:c_ftl_nand_sec_pre_page_shift]
	uxth	w6, w0
	ldrh	w2, [x26]
	mov	x4, x28
	ldrh	w5, [x24]
	strh	w0, [x1]
	adrp	x0, :got:c_ftl_nand_byte_pre_page
	lsl	w1, w2, 9
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_byte_pre_page]
	strh	w1, [x0]
	adrp	x0, :got:c_ftl_nand_byte_pre_oob
	ubfx	x1, x1, 9, 7
	lsl	w1, w1, 1
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_byte_pre_oob]
	strh	w1, [x0]
	adrp	x0, :got:c_ftl_nand_reserved_blks
	ldrh	w1, [x19,26]
	mov	x19, x25
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_reserved_blks]
	strh	w1, [x0]
	adrp	x1, :got:c_ftl_nand_totle_phy_blks
	ldrh	w0, [x20]
	mov	x20, x27
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_totle_phy_blks]
	cmp	w0, 1024
	mul	w3, w5, w0
	str	w3, [x1]
	bls	.L347
	ldr	x1, [x21, #:got_lo12:c_ftl_nand_ext_blk_pre_plane]
	and	w3, w0, 255
	strh	w3, [x1]
.L347:
	ldr	x21, [x21, #:got_lo12:c_ftl_nand_ext_blk_pre_plane]
	adrp	x3, :got:DeviceCapacity
	ldr	x7, [x19, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w1, [x21]
	ldrh	w7, [x7]
	sub	w1, w0, w1
	ldr	x3, [x3, #:got_lo12:DeviceCapacity]
	mul	w1, w5, w1
	mul	w1, w1, w2
	mul	w1, w1, w7
	asr	w1, w1, 11
	str	w1, [x3]
	ldr	x4, [x4, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldr	x23, [x23, #:got_lo12:c_ftl_vendor_part_size]
	ldrh	w4, [x4]
	ldrh	w1, [x23]
	mul	w2, w2, w4
	lsl	w3, w1, 3
	sdiv	w2, w3, w2
	adrp	x3, :got:c_ftl_nand_max_vendor_blks
	uxth	w2, w2
	cmp	w2, 4
	bls	.L348
	ldr	x4, [x3, #:got_lo12:c_ftl_nand_max_vendor_blks]
	strh	w2, [x4]
	b	.L349
.L348:
	ldr	x2, [x3, #:got_lo12:c_ftl_nand_max_vendor_blks]
	mov	w4, 4
	strh	w4, [x2]
.L349:
	adrp	x2, :got:c_ftl_nand_vendor_region_num
	asr	w1, w1, w6
	add	w1, w1, 2
	lsl	w0, w0, 6
	cmp	w5, 1
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_vendor_region_num]
	strh	w1, [x2]
	add	w1, w6, 9
	asr	w0, w0, w1
	adrp	x1, :got:c_ftl_nand_map_blks_per_plane
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_map_blks_per_plane]
	strh	w0, [x1]
	adrp	x1, :got:c_ftl_nand_max_map_blks
	and	w0, w0, 65535
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_max_map_blks]
	mul	w2, w0, w5
	add	w0, w0, 8
	str	w2, [x1]
	ldr	x3, [x3, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldrh	w1, [x3]
	udiv	w1, w1, w5
	add	w0, w0, w1
	adrp	x1, :got:c_ftl_nand_sys_blks_per_plane
	ldr	x2, [x1, #:got_lo12:c_ftl_nand_sys_blks_per_plane]
	bne	.L356
	add	w0, w0, 4
.L356:
	str	w0, [x2]
	ldr	x21, [x1, #:got_lo12:c_ftl_nand_sys_blks_per_plane]
	ldrh	w0, [x21]
	bl	FtlSysBlkNumInit
	adrp	x0, :got:c_ftl_nand_init_sys_blks_per_plane
	ldr	w1, [x21]
	adrp	x2, :got:c_ftl_nand_map_region_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_init_sys_blks_per_plane]
	str	w1, [x0]
	adrp	x0, :got:c_ftl_nand_max_data_blks
	ldr	x19, [x19, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_max_data_blks]
	ldr	x22, [x22, #:got_lo12:c_ftl_nand_sec_pre_page_shift]
	ldrh	w1, [x19]
	ldr	w0, [x0]
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_map_region_num]
	lsl	w0, w0, 2
	mul	w1, w0, w1
	ldrh	w0, [x22]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	strh	w0, [x2]
	adrp	x0, :got:c_ftl_nand_l2pmap_ram_region_num
	ldr	x20, [x20, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldrh	w2, [x20]
	sdiv	w1, w1, w2
	strh	w1, [x0]
	adrp	x0, :got:g_MaxLbaSector
	ldr	x0, [x0, #:got_lo12:g_MaxLbaSector]
	str	wzr, [x0]
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
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	adrp	x1, :got:g_totle_vendor_block
	uxth	w4, w0
	mov	w0, 0
	ldr	x1, [x1, #:got_lo12:g_totle_vendor_block]
	ldrh	w1, [x1]
	cbz	w1, .L359
	adrp	x1, :got:c_ftl_nand_max_vendor_blks
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldrh	w2, [x1]
	adrp	x1, :got:p_vendor_block_table
	ldr	x1, [x1, #:got_lo12:p_vendor_block_table]
	ldr	x3, [x1]
	mov	x1, 0
.L360:
	cmp	w2, w1, uxth
	bls	.L365
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L360
	mov	w0, 1
	b	.L359
.L365:
	mov	w0, 0
.L359:
	ret
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheMetchLpa
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	adrp	x2, :got:g_wr_page_num
	mov	w6, w0
	mov	w0, 0
	ldr	x2, [x2, #:got_lo12:g_wr_page_num]
	ldr	w3, [x2]
	cbz	w3, .L367
	adrp	x2, :got:req_wr_io
	mov	x0, 56
	ldr	x2, [x2, #:got_lo12:req_wr_io]
	ldr	x5, [x2]
	mov	x2, 0
.L368:
	cmp	w3, w2
	bls	.L377
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L368
	cmp	w4, w1
	bhi	.L368
	mov	w0, 1
	b	.L367
.L377:
	mov	w0, 0
.L367:
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, :got:g_MaxLbaSector
	ldr	x0, [x0, #:got_lo12:g_MaxLbaSector]
	ldr	w0, [x0]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	FtlGetCapacity
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	adrp	x0, :got:g_MaxLbaSector
	ldr	x0, [x0, #:got_lo12:g_MaxLbaSector]
	ldr	w0, [x0]
	ret
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, :got:g_MaxLpn
	ldr	x0, [x0, #:got_lo12:g_MaxLpn]
	ldr	w0, [x0]
	ret
	.size	FtlGetLpn, .-FtlGetLpn
	.align	2
	.global	FtlGetCurEraseBlock
	.type	FtlGetCurEraseBlock, %function
FtlGetCurEraseBlock:
	adrp	x0, :got:c_ftl_nand_planes_num
	adrp	x1, :got:g_cur_erase_blk
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldr	x1, [x1, #:got_lo12:g_cur_erase_blk]
	ldrh	w2, [x0]
	ldr	w0, [x1]
	mul	w0, w2, w0
	ret
	.size	FtlGetCurEraseBlock, .-FtlGetCurEraseBlock
	.align	2
	.global	FtlGetAllBlockNum
	.type	FtlGetAllBlockNum, %function
FtlGetAllBlockNum:
	adrp	x0, :got:c_ftl_nand_blk_pre_plane
	adrp	x1, :got:c_ftl_nand_planes_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_blk_pre_plane]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w2, [x0]
	ldrh	w0, [x1]
	mul	w0, w2, w0
	ret
	.size	FtlGetAllBlockNum, .-FtlGetAllBlockNum
	.align	2
	.global	FtlBbmMapBadBlock
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	uxth	w1, w0
	adrp	x0, :got:c_ftl_nand_blks_per_die
	stp	x29, x30, [sp, -32]!
	mov	w4, 1
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_blks_per_die]
	str	x19, [sp,16]
	adrp	x19, :got:gBbtInfo
	ldrh	w3, [x0]
	ldr	x19, [x19, #:got_lo12:gBbtInfo]
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
	adrp	x2, :got:c_ftl_nand_blks_per_die
	uxth	w0, w0
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w1, [x2]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	adrp	x1, :got:gBbtInfo
	uxth	w0, w0
	ldr	x1, [x1, #:got_lo12:gBbtInfo]
	ubfx	x3, x0, 5, 11
	add	x2, x1, x2, uxth 3
	ldr	x1, [x2,32]
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
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, 0
	stp	x21, x22, [sp,32]
	mov	w20, w19
	adrp	x22, :got:c_ftl_nand_blks_per_die
	adrp	x21, :got:c_ftl_nand_die_num
.L387:
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_blks_per_die]
	ldr	x1, [x21, #:got_lo12:c_ftl_nand_die_num]
	ldrh	w0, [x0]
	ldrh	w1, [x1]
	mul	w0, w0, w1
	cmp	w20, w0
	bge	.L393
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L388
	add	w19, w19, 1
	uxth	w19, w19
.L388:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L387
.L393:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.type	V2P_block, %function
V2P_block:
	adrp	x2, :got:c_ftl_nand_planes_per_die
	uxth	w0, w0
	uxth	w1, w1
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_planes_per_die]
	ldrh	w3, [x2]
	udiv	w2, w0, w3
	msub	w0, w2, w3, w0
	madd	w0, w1, w3, w0
	adrp	x1, :got:c_ftl_nand_blks_per_die
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w1, [x1]
	madd	w0, w2, w1, w0
	ret
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.type	P2V_plane, %function
P2V_plane:
	adrp	x1, :got:c_ftl_nand_planes_per_die
	adrp	x2, :got:c_ftl_nand_blks_per_die
	uxth	w0, w0
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_planes_per_die]
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w1, [x1]
	ldrh	w3, [x2]
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
	adrp	x1, :got:c_ftl_nand_blks_per_die
	uxth	w0, w0
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w2, [x1]
	udiv	w1, w0, w2
	msub	w0, w1, w2, w0
	adrp	x1, :got:c_ftl_nand_planes_per_die
	uxth	w0, w0
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_planes_per_die]
	ldrh	w1, [x1]
	udiv	w0, w0, w1
	ret
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	cmp	w0, w1
	mov	w2, -2147483648
	bls	.L398
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L399
.L398:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L399:
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	adrp	x0, :got:gSysFreeQueue
	ldr	x0, [x0, #:got_lo12:gSysFreeQueue]
	ldrh	w0, [x0,6]
	cmp	w0, wzr
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, :got:gSysFreeQueue
	ldr	x0, [x0, #:got_lo12:gSysFreeQueue]
	ldrh	w0, [x0,6]
	cmp	w0, 1024
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w20, w0
	uxth	w19, w1
	bl	FtlFreeSysBlkQueueFull
	uxth	w0, w0
	cbnz	w0, .L402
	cbz	w19, .L404
	mov	w0, w20
	bl	P2V_block_in_plane
	uxth	w19, w0
	adrp	x1, :got:req_erase
	lsl	w2, w20, 10
	ldr	x1, [x1, #:got_lo12:req_erase]
	ldr	x0, [x1]
	str	w2, [x0,4]
	ldr	x0, [x1]
	mov	w1, 1
	bl	FlashEraseBlocks
	adrp	x0, :got:p_erase_count_table
	ubfiz	x1, x19, 1, 16
	ldr	x0, [x0, #:got_lo12:p_erase_count_table]
	ldr	x2, [x0]
	ldrh	w0, [x2,x1]
	add	w0, w0, 1
	strh	w0, [x2,x1]
	adrp	x1, :got:g_totle_sys_slc_erase_count
	ldr	x1, [x1, #:got_lo12:g_totle_sys_slc_erase_count]
	ldr	w0, [x1]
	add	w0, w0, 1
	str	w0, [x1]
.L404:
	adrp	x1, :got:gSysFreeQueue
	ldr	x1, [x1, #:got_lo12:gSysFreeQueue]
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w20, [x0,8]
	strh	w2, [x1,4]
.L402:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	w19, 0
	adrp	x20, :got:gSysFreeQueue
	adrp	x21, :got:p_sys_data_buf
	adrp	x22, :got:p_erase_count_table
.L409:
	ldr	x1, [x20, #:got_lo12:gSysFreeQueue]
	adrp	x6, :got:gSysFreeQueue
	ldrh	w0, [x1,6]
	cmp	w0, w19
	bls	.L417
	ldrh	w0, [x1,2]
	add	w0, w0, w19
	add	x0, x1, x0, sxtw 1
	ldrh	w0, [x0,8]
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	ldr	x3, [x22, #:got_lo12:p_erase_count_table]
	ubfiz	x1, x19, 2, 16
	ldr	x2, [x21, #:got_lo12:p_sys_data_buf]
	add	w19, w19, 1
	ldr	x3, [x3]
	uxth	w19, w19
	ldr	x2, [x2]
	ldrh	w0, [x3,x0]
	str	w0, [x2,x1]
	b	.L409
.L417:
	mov	w0, 0
	adrp	x7, :got:p_sys_data_buf
.L411:
	ldr	x1, [x6, #:got_lo12:gSysFreeQueue]
	ldrh	w5, [x1,6]
	sub	w1, w5, #1
	cmp	w0, w1
	bge	.L418
	ldr	x1, [x7, #:got_lo12:p_sys_data_buf]
	add	w4, w0, 1
	uxth	w4, w4
	ldr	x3, [x1]
	mov	w2, w4
	mov	w1, w0
.L412:
	cmp	w2, w5
	bcs	.L419
	ubfiz	x8, x2, 2, 16
	ldr	w9, [x3,w1,uxtw 2]
	ldr	w8, [x3,x8]
	cmp	w9, w8
	csel	w1, w1, w2, ls
	add	w2, w2, 1
	uxth	w2, w2
	b	.L412
.L419:
	cmp	w0, w1
	beq	.L415
	ubfiz	x5, x0, 2, 16
	ubfiz	x2, x1, 2, 32
	ldr	w9, [x3,x5]
	ldr	w8, [x3,x2]
	str	w9, [x3,x2]
	adrp	x2, :got:p_sys_data_buf
	ldr	x2, [x2, #:got_lo12:p_sys_data_buf]
	ldr	x2, [x2]
	str	w8, [x2,x5]
	ldr	x2, [x6, #:got_lo12:gSysFreeQueue]
	ldrh	w3, [x2,2]
	add	w1, w3, w1
	add	w0, w3, w0
	add	x1, x2, x1, sxtw 1
	add	x0, x2, x0, sxtw 1
	ldrh	w2, [x0,8]
	ldrh	w5, [x1,8]
	strh	w2, [x1,8]
	strh	w5, [x0,8]
.L415:
	mov	w0, w4
	b	.L411
.L418:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxth	w19, w0
	bl	FtlFreeSysBlkQueueFull
	uxth	w1, w0
	mov	w0, 0
	cbnz	w1, .L421
	adrp	x2, :got:gSysFreeQueue
	mov	w0, w1
	ldr	x3, [x2, #:got_lo12:gSysFreeQueue]
	ldrh	w4, [x3,6]
	ldrh	w3, [x3,2]
.L422:
	cmp	w0, w4
	bcs	.L426
	ldr	x5, [x2, #:got_lo12:gSysFreeQueue]
	add	w1, w0, w3
	ubfiz	x1, x1, 1, 10
	add	x1, x5, x1
	ldrh	w1, [x1,8]
	cmp	w1, w19
	beq	.L425
	add	w0, w0, 1
	b	.L422
.L426:
	mov	w0, 0
	b	.L421
.L425:
	mov	w0, 1
.L421:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlFreeSysBlkQueueEmpty
	uxth	w1, w0
	mov	w0, 0
	cbnz	w1, .L428
	adrp	x1, :got:gSysFreeQueue
	ldr	x1, [x1, #:got_lo12:gSysFreeQueue]
	ldrh	w2, [x1,2]
	add	x0, x1, x2, sxtw 1
	ldrh	w3, [x1,6]
	add	w2, w2, 1
	ldrh	w0, [x0,8]
	sub	w3, w3, #1
	and	w2, w2, 1023
	strh	w3, [x1,6]
	strh	w2, [x1,2]
.L428:
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	uxth	w0, w0
	mov	w9, 65535
	cmp	w0, w9
	beq	.L431
	adrp	x3, :got:p_data_block_list_table
	mov	w4, 6
	mov	w1, -1
	adrp	x2, :got:p_data_block_list_head
	umull	x11, w0, w4
	ldr	x8, [x3, #:got_lo12:p_data_block_list_table]
	mov	x10, x3
	mov	x5, x2
	ldr	x12, [x8]
	add	x6, x12, x11
	strh	w1, [x6,2]
	strh	w1, [x12,x11]
	ldr	x7, [x2, #:got_lo12:p_data_block_list_head]
	ldr	x1, [x7]
	cbnz	x1, .L432
	str	x6, [x7]
	b	.L431
.L432:
	adrp	x2, :got:p_valid_page_count_table
	ubfiz	x7, x0, 1, 16
	ldr	x13, [x8]
	mov	x8, -6148914691236517206
	ldr	x2, [x2, #:got_lo12:p_valid_page_count_table]
	ldr	x14, [x2]
	ldrh	w2, [x6,4]
	ldrh	w3, [x14,x7]
	cmp	w2, wzr
	mul	w3, w3, w2
	sub	x2, x1, x13
	asr	x2, x2, 1
	csinv	w3, w3, wzr, ne
	madd	x2, x8, x2, x2
	adrp	x8, :got:p_erase_count_table
	uxth	w2, w2
	ldr	x8, [x8, #:got_lo12:p_erase_count_table]
	ldr	x15, [x8]
	add	x8, x15, x7
	mov	w7, w4
.L439:
	ubfiz	x16, x2, 1, 16
	ldrh	w17, [x1,4]
	cmp	w17, wzr
	ldrh	w4, [x14,x16]
	mul	w4, w4, w17
	csinv	w4, w4, wzr, ne
	cmp	w4, w3
	bne	.L435
	ldrh	w16, [x15,x16]
	ldrh	w4, [x8]
	cmp	w16, w4
	bcc	.L437
	b	.L436
.L435:
	bhi	.L436
.L437:
	ldrh	w4, [x1]
	cmp	w4, w9
	bne	.L438
	strh	w2, [x6,2]
	strh	w0, [x1]
	adrp	x0, :got:p_data_block_list_tail
	ldr	x0, [x0, #:got_lo12:p_data_block_list_tail]
	str	x6, [x0]
	b	.L431
.L438:
	umull	x1, w4, w7
	mov	w2, w4
	add	x1, x13, x1
	b	.L439
.L436:
	strh	w2, [x12,x11]
	ldrh	w2, [x1,2]
	strh	w2, [x6,2]
	ldr	x2, [x5, #:got_lo12:p_data_block_list_head]
	ldr	x3, [x2]
	cmp	x1, x3
	bne	.L440
	strh	w0, [x1,2]
	str	x6, [x2]
	b	.L431
.L440:
	ldrh	w2, [x1,2]
	mov	w3, 6
	umull	x2, w2, w3
	ldr	x3, [x10, #:got_lo12:p_data_block_list_table]
	ldr	x3, [x3]
	strh	w0, [x3,x2]
	strh	w0, [x1,2]
.L431:
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
	adrp	x0, :got:g_num_data_superblocks
	ldr	x0, [x0, #:got_lo12:g_num_data_superblocks]
	ldrh	w1, [x0]
	add	w1, w1, 1
	strh	w1, [x0]
	ldp	x29, x30, [sp], 16
	ret
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.type	insert_free_list, %function
insert_free_list:
	uxth	w0, w0
	mov	w6, 65535
	cmp	w0, w6
	beq	.L445
	adrp	x3, :got:p_data_block_list_table
	mov	w5, 6
	mov	w1, -1
	adrp	x2, :got:p_free_data_block_list_head
	umull	x7, w0, w5
	ldr	x9, [x3, #:got_lo12:p_data_block_list_table]
	mov	x12, x3
	mov	x3, x2
	ldr	x8, [x9]
	add	x4, x8, x7
	strh	w1, [x4,2]
	strh	w1, [x8,x7]
	ldr	x10, [x2, #:got_lo12:p_free_data_block_list_head]
	ldr	x1, [x10]
	cbnz	x1, .L446
	str	x4, [x10]
	b	.L445
.L446:
	adrp	x2, :got:p_erase_count_table
	ldr	x10, [x9]
	mov	x9, -6148914691236517206
	ldr	x2, [x2, #:got_lo12:p_erase_count_table]
	ldr	x11, [x2]
	ubfiz	x2, x0, 1, 16
	ldrh	w13, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x9, x2, x2
	uxth	w2, w2
.L449:
	ubfiz	x9, x2, 1, 16
	ldrh	w9, [x11,x9]
	cmp	w9, w13
	bcs	.L447
	ldrh	w9, [x1]
	cmp	w9, w6
	bne	.L448
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L445
.L448:
	umull	x1, w9, w5
	mov	w2, w9
	add	x1, x10, x1
	b	.L449
.L447:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x8,x7]
	ldr	x2, [x3, #:got_lo12:p_free_data_block_list_head]
	ldr	x3, [x2]
	cmp	x1, x3
	bne	.L450
	strh	w0, [x1,2]
	str	x4, [x2]
	b	.L445
.L450:
	ldrh	w2, [x1,2]
	mov	w3, 6
	umull	x2, w2, w3
	ldr	x3, [x12, #:got_lo12:p_data_block_list_table]
	ldr	x3, [x3]
	strh	w0, [x3,x2]
	strh	w0, [x1,2]
.L445:
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
	adrp	x0, :got:g_num_free_superblocks
	ldr	x0, [x0, #:got_lo12:g_num_free_superblocks]
	ldrh	w1, [x0]
	add	w1, w1, 1
	strh	w1, [x0]
	ldp	x29, x30, [sp], 16
	ret
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	adrp	x6, :got:p_data_block_list_table
	uxth	w1, w1
	mov	w5, 6
	ldr	x3, [x0]
	mov	w7, 65535
	ldr	x6, [x6, #:got_lo12:p_data_block_list_table]
	umull	x1, w1, w5
	ldr	x2, [x6]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L453
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L454
	str	xzr, [x0]
	b	.L455
.L454:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L455
.L453:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L456
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L455
.L456:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L455:
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
	cbz	x2, .L458
	adrp	x3, :got:p_data_block_list_table
	mov	w5, w19
	mov	w6, 6
	ldr	x3, [x3, #:got_lo12:p_data_block_list_table]
	ldr	x4, [x3]
.L459:
	cbnz	w1, .L460
.L462:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L458
.L460:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L462
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L459
.L458:
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
	uxth	w2, w0
	adrp	x0, :got:p_data_block_list_head
	ldr	x0, [x0, #:got_lo12:p_data_block_list_head]
	ldr	x1, [x0]
	mov	w0, 65535
	cbz	x1, .L466
	adrp	x0, :got:p_data_block_list_table
	mov	w4, 65535
	mov	w5, 6
	ldr	x0, [x0, #:got_lo12:p_data_block_list_table]
	ldr	x0, [x0]
.L467:
	cbnz	w2, .L468
.L470:
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
	b	.L466
.L468:
	ldrh	w3, [x1]
	cmp	w3, w4
	beq	.L470
	umull	x3, w3, w5
	sub	w2, w2, #1
	add	x1, x0, x3
	uxth	w2, w2
	b	.L467
.L466:
	ret
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.type	List_update_data_list, %function
List_update_data_list:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxth	w19, w0
	adrp	x0, :got:g_active_superblock
	ldr	x0, [x0, #:got_lo12:g_active_superblock]
	ldrh	w0, [x0]
	cmp	w0, w19
	beq	.L473
	adrp	x0, :got:g_buffer_superblock
	ldr	x0, [x0, #:got_lo12:g_buffer_superblock]
	ldrh	w0, [x0]
	cmp	w0, w19
	beq	.L473
	adrp	x0, :got:g_gc_temp_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	ldrh	w0, [x0]
	cmp	w0, w19
	beq	.L473
	adrp	x0, :got:p_data_block_list_table
	mov	w3, 6
	umull	x4, w19, w3
	ldr	x0, [x0, #:got_lo12:p_data_block_list_table]
	ldr	x5, [x0]
	adrp	x0, :got:p_data_block_list_head
	add	x4, x5, x4
	ldr	x0, [x0, #:got_lo12:p_data_block_list_head]
	ldr	x1, [x0]
	cmp	x4, x1
	beq	.L473
	adrp	x1, :got:p_valid_page_count_table
	ldrh	w2, [x4,4]
	ldr	x1, [x1, #:got_lo12:p_valid_page_count_table]
	ldr	x6, [x1]
	ubfiz	x1, x19, 1, 16
	ldrh	w1, [x6,x1]
	mul	w2, w1, w2
	ldrh	w1, [x4,2]
	mov	x4, -6148914691236517206
	cmp	w2, wzr
	csinv	w2, w2, wzr, ne
	umull	x3, w1, w3
	asr	x1, x3, 1
	add	x3, x5, x3
	madd	x1, x4, x1, x1
	ldrh	w3, [x3,4]
	ldrh	w1, [x6,x1,lsl 1]
	mul	w1, w1, w3
	cmp	w1, wzr
	csinv	w1, w1, wzr, ne
	cmp	w2, w1
	bcs	.L473
	mov	w1, w19
	bl	List_remove_node
	adrp	x0, :got:g_num_data_superblocks
	ldr	x0, [x0, #:got_lo12:g_num_data_superblocks]
	ldrh	w1, [x0]
	sub	w1, w1, #1
	strh	w1, [x0]
	mov	w0, w19
	bl	INSERT_DATA_LIST
.L473:
	mov	w0, 0
	ldr	x19, [sp,16]
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
	beq	.L480
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L478
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	uxth	w1, w0
	strh	w0, [x22]
	cbz	w1, .L480
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L480
.L478:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L477
.L480:
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
	adrp	x0, :got:c_ftl_nand_l2pmap_ram_region_num
	mov	x3, 0
	mov	w4, 65535
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldrh	w2, [x0]
	adrp	x0, :got:p_l2p_ram_map
	ldr	x0, [x0, #:got_lo12:p_l2p_ram_map]
	ldr	x1, [x0]
.L484:
	uxth	w0, w3
	cmp	w0, w2
	bcs	.L500
	add	x3, x3, 1
	add	x5, x1, x3, lsl 4
	ldrh	w5, [x5,-16]
	cmp	w5, w4
	bne	.L484
	b	.L485
.L500:
	mov	w0, w2
	mov	x3, 0
	mov	w6, -2147483648
.L487:
	uxth	w5, w3
	cmp	w5, w2
	bcs	.L501
	add	x4, x1, x3, lsl 4
	ldr	w4, [x4,4]
	tbnz	w4, #31, .L496
	cmp	w4, w6
	bcc	.L488
.L496:
	mov	w4, w6
	mov	w5, w0
.L488:
	add	x3, x3, 1
	mov	w6, w4
	mov	w0, w5
	b	.L487
.L501:
	cmp	w0, w2
	bcc	.L485
	adrp	x0, :got:g_l2p_last_update_region_id
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldr	x0, [x0, #:got_lo12:g_l2p_last_update_region_id]
	ldrh	w5, [x0]
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
	adrp	x1, :got:FtlUpdateVaildLpnCount
	ldr	x3, [x1, #:got_lo12:FtlUpdateVaildLpnCount]
	ldrh	w2, [x3]
	cbnz	w0, .L503
	cmp	w2, 4
	bhi	.L503
	add	w2, w2, 1
	strh	w2, [x3]
	b	.L502
.L503:
	ldr	x1, [x1, #:got_lo12:FtlUpdateVaildLpnCount]
	mov	w7, 65535
	strh	wzr, [x1]
	adrp	x1, :got:g_VaildLpn
	ldr	x0, [x1, #:got_lo12:g_VaildLpn]
	str	wzr, [x0]
	adrp	x0, :got:c_ftl_nand_data_blks_per_plane
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w5, [x0]
	adrp	x0, :got:p_valid_page_count_table
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x6, [x0]
	mov	x0, 0
.L504:
	cmp	w5, w0, uxth
	bls	.L502
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L505
	ldr	x3, [x1, #:got_lo12:g_VaildLpn]
	ldr	w2, [x3]
	add	w2, w4, w2
	str	w2, [x3]
.L505:
	add	x0, x0, 1
	b	.L504
.L502:
	ret
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_sb_update_avl_pages
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	adrp	x3, :got:c_ftl_nand_planes_num
	strh	wzr, [x0,4]
	uxth	w1, w1
	uxth	w2, w2
	mov	w5, 65535
	ldr	x3, [x3, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w3, [x3]
.L509:
	cmp	w2, w3
	bcs	.L515
	add	x4, x0, x2, sxtw 1
	ldrh	w4, [x4,16]
	cmp	w4, w5
	beq	.L510
	ldrh	w4, [x0,4]
	add	w4, w4, 1
	strh	w4, [x0,4]
.L510:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L509
.L515:
	adrp	x2, :got:c_ftl_nand_page_pre_blk
	mov	x4, x0
	mov	w6, 65535
	add	x3, x0, x3, uxth 1
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w5, [x2]
.L512:
	cmp	x4, x3
	beq	.L516
	ldrh	w2, [x4,16]
	cmp	w2, w6
	beq	.L513
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L513:
	add	x4, x4, 2
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
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	str	x25, [sp,64]
	mov	x19, x0
	strh	wzr, [x0,4]
	mov	w20, 0
	strb	wzr, [x0,7]
	adrp	x22, :got:c_ftl_nand_planes_num
	adrp	x24, :got:p_plane_order_table
	mov	w23, -1
.L518:
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w0, w20
	bls	.L525
	ldr	x0, [x24, #:got_lo12:p_plane_order_table]
	add	x21, x19, x20, uxth 1
	ldrh	w1, [x19]
	ldrb	w0, [x0,w20,sxtw]
	bl	V2P_block
	strh	w23, [x21,16]
	mov	w25, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L519
	strh	w25, [x21,16]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L519:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L518
.L525:
	adrp	x1, :got:c_ftl_nand_page_pre_blk
	ldrb	w0, [x19,7]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_page_pre_blk]
	strb	wzr, [x19,9]
	ldrh	w1, [x1]
	mul	w0, w0, w1
	strh	w0, [x19,4]
	adrp	x0, :got:g_inkDie_check_enable
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w0, [x0]
	cbz	w0, .L521
	adrp	x0, :got:p_erase_count_table
	ldrh	w1, [x19]
	ldr	x0, [x0, #:got_lo12:p_erase_count_table]
	ldr	x0, [x0]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 59
	bhi	.L521
	mov	w0, 1
	strb	w0, [x19,9]
.L521:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, 0
	uxth	w20, w0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w21, w19
	adrp	x22, :got:c_ftl_nand_planes_num
	adrp	x23, :got:p_plane_order_table
	adrp	x24, :got:c_ftl_nand_page_pre_blk
.L527:
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w0, w21
	bls	.L534
	ldr	x0, [x23, #:got_lo12:p_plane_order_table]
	mov	w1, w20
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L528
	ldr	x0, [x24, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w0, [x0]
	add	w19, w19, w0
	uxth	w19, w19
.L528:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L527
.L534:
	cbz	w19, .L530
	mov	w0, 32768
	sdiv	w19, w0, w19
.L530:
	mov	w0, 6
	umull	x20, w20, w0
	adrp	x0, :got:p_data_block_list_table
	ldr	x0, [x0, #:got_lo12:p_data_block_list_table]
	ldr	x0, [x0]
	add	x20, x0, x20
	mov	w0, 0
	strh	w19, [x20,4]
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
	adrp	x0, :got:p_free_data_block_list_head
	ldr	x0, [x0, #:got_lo12:p_free_data_block_list_head]
	ldr	x1, [x0]
	mov	w0, w1
	cbz	x1, .L536
	adrp	x0, :got:p_data_block_list_table
	ldr	x0, [x0, #:got_lo12:p_data_block_list_table]
	ldr	x0, [x0]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	adrp	x0, :got:p_erase_count_table
	and	x1, x1, 65535
	ldr	x0, [x0, #:got_lo12:p_erase_count_table]
	ldr	x0, [x0]
	ldrh	w0, [x0,x1,lsl 1]
.L536:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	uxth	w3, w0
	adrp	x0, :got:p_free_data_block_list_head
	ldr	x0, [x0, #:got_lo12:p_free_data_block_list_head]
	ldr	x1, [x0]
	mov	w0, w1
	cbz	x1, .L539
	adrp	x0, :got:g_num_free_superblocks
	mov	w5, 6
	mov	w6, 65535
	ldr	x0, [x0, #:got_lo12:g_num_free_superblocks]
	ldrh	w2, [x0]
	mov	w0, 7
	mul	w2, w2, w0
	adrp	x0, :got:p_data_block_list_table
	asr	w2, w2, 3
	cmp	w3, w2
	ldr	x0, [x0, #:got_lo12:p_data_block_list_table]
	csel	w3, w2, w3, gt
	mov	x2, -6148914691236517206
	ldr	x0, [x0]
	sub	x1, x1, x0
	asr	x1, x1, 1
	madd	x1, x2, x1, x1
	mov	w2, 0
	uxth	w1, w1
.L541:
	cmp	w2, w3
	beq	.L544
	umull	x4, w1, w5
	ldrh	w4, [x0,x4]
	cmp	w4, w6
	bne	.L542
.L544:
	adrp	x0, :got:p_erase_count_table
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x0, #:got_lo12:p_erase_count_table]
	ldr	x0, [x0]
	ldrh	w0, [x0,x1]
	b	.L539
.L542:
	add	w2, w2, 1
	mov	w1, w4
	uxth	w2, w2
	b	.L541
.L539:
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -128]!
	adrp	x1, .LC2
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x22, x0
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	bl	strcpy
	add	x20, x22, 12
	adrp	x2, :got:gNandPhyInfo
	adrp	x1, .LC3
	mov	x0, x20
	add	x1, x1, :lo12:.LC3
	ldr	x2, [x2, #:got_lo12:gNandPhyInfo]
	ldr	w2, [x2]
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x2, :got:DeviceCapacity
	adrp	x1, .LC4
	mov	x0, x20
	add	x1, x1, :lo12:.LC4
	ldr	x2, [x2, #:got_lo12:DeviceCapacity]
	ldr	w2, [x2]
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, :got:gFtlInitStatus
	ldr	x0, [x0, #:got_lo12:gFtlInitStatus]
	ldr	w0, [x0]
	cmp	w0, 1
	beq	.L547
	sub	w0, w20, w22
	b	.L548
.L547:
	add	x0, x29, 112
	add	x1, x29, 116
	add	x2, x29, 120
	add	x3, x29, 124
	adrp	x19, :got:gBbtInfo
	adrp	x25, :got:g_num_free_superblocks
	bl	NandcGetTimeCfg
	adrp	x23, :got:p_valid_page_count_table
	ldr	w4, [x29,120]
	adrp	x1, .LC5
	ldr	w5, [x29,124]
	mov	x0, x20
	ldr	w3, [x29,116]
	add	x1, x1, :lo12:.LC5
	ldr	w2, [x29,112]
	adrp	x26, :got:g_sys_ext_data
	bl	sprintf
	add	x21, x20, x0, sxtw
	adrp	x1, .LC6
	mov	x0, x21
	add	x1, x1, :lo12:.LC6
	add	x21, x21, 10
	adrp	x20, :got:g_sys_save_data
	bl	strcpy
	adrp	x2, :got:g_MaxLpn
	adrp	x1, .LC7
	mov	x0, x21
	add	x1, x1, :lo12:.LC7
	ldr	x2, [x2, #:got_lo12:g_MaxLpn]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_VaildLpn
	adrp	x1, .LC8
	mov	x0, x21
	add	x1, x1, :lo12:.LC8
	ldr	x2, [x2, #:got_lo12:g_VaildLpn]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_read_page_count
	adrp	x1, .LC9
	mov	x0, x21
	add	x1, x1, :lo12:.LC9
	ldr	x2, [x2, #:got_lo12:g_totle_read_page_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_discard_page_count
	adrp	x1, .LC10
	mov	x0, x21
	add	x1, x1, :lo12:.LC10
	ldr	x2, [x2, #:got_lo12:g_totle_discard_page_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_write_page_count
	adrp	x1, .LC11
	mov	x0, x21
	add	x1, x1, :lo12:.LC11
	ldr	x2, [x2, #:got_lo12:g_totle_write_page_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_cache_write_count
	adrp	x1, .LC12
	mov	x0, x21
	add	x1, x1, :lo12:.LC12
	ldr	x2, [x2, #:got_lo12:g_totle_cache_write_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_l2p_write_count
	adrp	x1, .LC13
	mov	x0, x21
	add	x1, x1, :lo12:.LC13
	ldr	x2, [x2, #:got_lo12:g_totle_l2p_write_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_gc_page_count
	adrp	x1, .LC14
	mov	x0, x21
	add	x1, x1, :lo12:.LC14
	ldr	x2, [x2, #:got_lo12:g_totle_gc_page_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x0, :got:g_totle_write_sector
	adrp	x1, .LC15
	add	x1, x1, :lo12:.LC15
	ldr	x0, [x0, #:got_lo12:g_totle_write_sector]
	ldr	w2, [x0]
	mov	x0, x21
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x0, :got:g_totle_read_sector
	adrp	x1, .LC16
	add	x1, x1, :lo12:.LC16
	ldr	x0, [x0, #:got_lo12:g_totle_read_sector]
	ldr	w2, [x0]
	mov	x0, x21
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_GlobalSysVersion
	adrp	x1, .LC17
	mov	x0, x21
	add	x1, x1, :lo12:.LC17
	ldr	x2, [x2, #:got_lo12:g_GlobalSysVersion]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_GlobalDataVersion
	adrp	x1, .LC18
	add	x1, x1, :lo12:.LC18
	mov	x0, x21
	ldr	x2, [x2, #:got_lo12:g_GlobalDataVersion]
	ldr	w2, [x2]
	bl	sprintf
	ldr	x19, [x19, #:got_lo12:gBbtInfo]
	add	x21, x21, x0, sxtw
	ldrh	w2, [x19,6]
	str	x2, [x29,104]
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldr	x2, [x29,104]
	adrp	x1, .LC19
	mov	x0, x21
	add	x1, x1, :lo12:.LC19
	bl	sprintf
	ldr	x25, [x25, #:got_lo12:g_num_free_superblocks]
	adrp	x1, .LC20
	add	x21, x21, x0, sxtw
	add	x1, x1, :lo12:.LC20
	mov	x0, x21
	ldrh	w2, [x25]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_mlc_erase_count
	adrp	x1, .LC21
	mov	x0, x21
	add	x1, x1, :lo12:.LC21
	ldr	x2, [x2, #:got_lo12:g_totle_mlc_erase_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_slc_erase_count
	adrp	x1, .LC22
	mov	x0, x21
	add	x1, x1, :lo12:.LC22
	ldr	x2, [x2, #:got_lo12:g_totle_slc_erase_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_avg_erase_count
	adrp	x1, .LC23
	mov	x0, x21
	add	x1, x1, :lo12:.LC23
	ldr	x2, [x2, #:got_lo12:g_totle_avg_erase_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_totle_sys_slc_erase_count
	adrp	x1, .LC24
	mov	x0, x21
	add	x1, x1, :lo12:.LC24
	ldr	x2, [x2, #:got_lo12:g_totle_sys_slc_erase_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_max_erase_count
	adrp	x1, .LC25
	mov	x0, x21
	add	x1, x1, :lo12:.LC25
	ldr	x2, [x2, #:got_lo12:g_max_erase_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_min_erase_count
	adrp	x1, .LC26
	mov	x0, x21
	add	x1, x1, :lo12:.LC26
	ldr	x2, [x2, #:got_lo12:g_min_erase_count]
	ldr	w2, [x2]
	bl	sprintf
	ldr	x20, [x20, #:got_lo12:g_sys_save_data]
	adrp	x1, .LC27
	add	x21, x21, x0, sxtw
	add	x1, x1, :lo12:.LC27
	mov	x0, x21
	ldrh	w2, [x20,30]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,28]
	adrp	x1, .LC28
	mov	x0, x21
	add	x1, x1, :lo12:.LC28
	adrp	x20, :got:g_gc_superblock
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:g_MaxLbaSector
	adrp	x1, .LC29
	mov	x0, x21
	add	x1, x1, :lo12:.LC29
	ldr	x2, [x2, #:got_lo12:g_MaxLbaSector]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:c_ftl_nand_init_sys_blks_per_plane
	adrp	x1, .LC30
	mov	x0, x21
	add	x1, x1, :lo12:.LC30
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_init_sys_blks_per_plane]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:c_ftl_nand_sys_blks_per_plane
	adrp	x1, .LC31
	mov	x0, x21
	add	x1, x1, :lo12:.LC31
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_sys_blks_per_plane]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:gSysFreeQueue
	adrp	x1, .LC32
	mov	x0, x21
	add	x1, x1, :lo12:.LC32
	ldr	x2, [x2, #:got_lo12:gSysFreeQueue]
	ldrh	w2, [x2,6]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:c_ftl_nand_data_blks_per_plane
	adrp	x1, .LC33
	mov	x0, x21
	add	x1, x1, :lo12:.LC33
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:c_ftl_nand_data_op_blks_per_plane
	adrp	x1, .LC34
	mov	x0, x21
	add	x1, x1, :lo12:.LC34
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	ldrh	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:c_ftl_nand_max_data_blks
	adrp	x1, .LC35
	mov	x0, x21
	add	x1, x1, :lo12:.LC35
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_max_data_blks]
	ldr	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x2, :got:gSysInfo
	adrp	x1, .LC36
	mov	x0, x21
	add	x1, x1, :lo12:.LC36
	ldr	x2, [x2, #:got_lo12:gSysInfo]
	ldrh	w2, [x2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x19]
	adrp	x1, .LC37
	adrp	x19, :got:g_active_superblock
	mov	x0, x21
	add	x1, x1, :lo12:.LC37
	bl	sprintf
	ldr	x19, [x19, #:got_lo12:g_active_superblock]
	adrp	x1, .LC38
	add	x21, x21, x0, sxtw
	add	x1, x1, :lo12:.LC38
	mov	x0, x21
	ldrh	w2, [x19,2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x19,6]
	adrp	x1, .LC39
	mov	x0, x21
	add	x1, x1, :lo12:.LC39
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x19]
	adrp	x1, .LC40
	mov	x0, x21
	add	x1, x1, :lo12:.LC40
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x19,8]
	adrp	x1, .LC41
	mov	x0, x21
	add	x1, x1, :lo12:.LC41
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x19,4]
	adrp	x1, .LC42
	mov	x0, x21
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	ldr	x24, [x23, #:got_lo12:p_valid_page_count_table]
	ldrh	w3, [x19]
	adrp	x1, .LC43
	adrp	x19, :got:g_buffer_superblock
	add	x21, x21, x0, sxtw
	ldr	x2, [x24]
	mov	x0, x21
	add	x1, x1, :lo12:.LC43
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	ldr	x19, [x19, #:got_lo12:g_buffer_superblock]
	adrp	x1, .LC44
	add	x21, x21, x0, sxtw
	add	x1, x1, :lo12:.LC44
	mov	x0, x21
	ldrh	w2, [x19,2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x19,6]
	adrp	x1, .LC45
	mov	x0, x21
	add	x1, x1, :lo12:.LC45
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x19]
	adrp	x1, .LC46
	mov	x0, x21
	add	x1, x1, :lo12:.LC46
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x19,8]
	adrp	x1, .LC47
	mov	x0, x21
	add	x1, x1, :lo12:.LC47
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x19,4]
	adrp	x1, .LC48
	mov	x0, x21
	add	x1, x1, :lo12:.LC48
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x19]
	adrp	x1, .LC49
	ldr	x2, [x24]
	adrp	x19, :got:g_gc_temp_superblock
	mov	x0, x21
	add	x1, x1, :lo12:.LC49
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	ldr	x19, [x19, #:got_lo12:g_gc_temp_superblock]
	adrp	x1, .LC50
	add	x21, x21, x0, sxtw
	add	x1, x1, :lo12:.LC50
	mov	x0, x21
	ldrh	w2, [x19,2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x19,6]
	adrp	x1, .LC51
	mov	x0, x21
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x19]
	adrp	x1, .LC52
	mov	x0, x21
	add	x1, x1, :lo12:.LC52
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x19,8]
	adrp	x1, .LC53
	mov	x0, x21
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x19,4]
	adrp	x1, .LC54
	mov	x0, x21
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	ldr	x20, [x20, #:got_lo12:g_gc_superblock]
	adrp	x1, .LC55
	add	x21, x21, x0, sxtw
	add	x1, x1, :lo12:.LC55
	mov	x0, x21
	ldrh	w2, [x20,2]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,6]
	adrp	x1, .LC56
	mov	x0, x21
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20]
	adrp	x1, .LC57
	mov	x0, x21
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,8]
	adrp	x1, .LC58
	mov	x0, x21
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,4]
	adrp	x1, .LC59
	mov	x0, x21
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x21, x21, x0, sxtw
	adrp	x0, :got:g_all_blk_used_slc_mode
	ldr	x26, [x26, #:got_lo12:g_sys_ext_data]
	adrp	x1, .LC60
	add	x1, x1, :lo12:.LC60
	ldr	x0, [x0, #:got_lo12:g_all_blk_used_slc_mode]
	ldr	w4, [x26,84]
	ldr	w5, [x26,76]
	ldr	w3, [x0]
	adrp	x0, :got:g_inkDie_check_enable
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w2, [x0]
	mov	x0, x21
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x26,80]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x26,72]
	adrp	x1, .LC61
	mov	x0, x19
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x26,96]
	adrp	x1, .LC62
	mov	x0, x19
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x19, x19, x0, sxtw
	adrp	x2, :got:g_gc_free_blk_threshold
	adrp	x1, .LC63
	mov	x0, x19
	add	x1, x1, :lo12:.LC63
	ldr	x2, [x2, #:got_lo12:g_gc_free_blk_threshold]
	ldrh	w2, [x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
	adrp	x2, :got:g_gc_merge_free_blk_threshold
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	ldr	x2, [x2, #:got_lo12:g_gc_merge_free_blk_threshold]
	ldrh	w2, [x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
	adrp	x2, :got:g_gc_skip_write_count
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	ldr	x2, [x2, #:got_lo12:g_gc_skip_write_count]
	ldr	w2, [x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
	adrp	x2, :got:g_gc_blk_index
	adrp	x1, .LC66
	add	x1, x1, :lo12:.LC66
	mov	x0, x19
	ldr	x2, [x2, #:got_lo12:g_gc_blk_index]
	ldrh	w2, [x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	uxth	w2, w0
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x25]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x20]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L549
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x24]
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L549:
	mov	w0, 0
	adrp	x24, .LC70
	mov	w20, 0
	adrp	x28, :got:p_data_block_list_table
	bl	List_get_gc_head_node
	add	x24, x24, :lo12:.LC70
	uxth	w3, w0
	mov	w9, 65535
	mov	w8, 6
	adrp	x27, :got:p_erase_count_table
.L551:
	cmp	w3, w9
	str	x9, [x29,96]
	adrp	x21, :got:p_data_block_list_table
	beq	.L550
	ldr	x0, [x23, #:got_lo12:p_valid_page_count_table]
	ubfiz	x6, x3, 1, 16
	ldr	x26, [x28, #:got_lo12:p_data_block_list_table]
	mov	w2, w20
	umull	x25, w3, w8
	ldr	x4, [x0]
	mov	x1, x24
	ldr	x0, [x27, #:got_lo12:p_erase_count_table]
	add	w20, w20, 1
	ldr	x5, [x26]
	ldrh	w4, [x4,x6]
	ldr	x7, [x0]
	add	x5, x5, x25
	mov	x0, x19
	str	x8, [x29,104]
	ldrh	w5, [x5,4]
	ldrh	w6, [x7,x6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x26]
	cmp	w20, 16
	ldr	x8, [x29,104]
	ldr	x9, [x29,96]
	ldrh	w3, [x0,x25]
	bne	.L551
.L550:
	adrp	x0, :got:p_free_data_block_list_head
	ldr	x1, [x21, #:got_lo12:p_data_block_list_table]
	adrp	x23, .LC71
	mov	w20, 0
	mov	w28, 65535
	mov	w27, 6
	ldr	x0, [x0, #:got_lo12:p_free_data_block_list_head]
	add	x23, x23, :lo12:.LC71
	ldr	x3, [x1]
	adrp	x26, :got:p_erase_count_table
	ldr	x0, [x0]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L553:
	cmp	w3, w28
	beq	.L552
	ldr	x25, [x21, #:got_lo12:p_data_block_list_table]
	ubfiz	x5, x3, 1, 16
	ldr	x0, [x26, #:got_lo12:p_erase_count_table]
	mov	w2, w20
	umull	x24, w3, w27
	ldr	x4, [x25]
	mov	x1, x23
	ldr	x6, [x0]
	mov	x0, x19
	add	x4, x4, x24
	add	w20, w20, 1
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x25]
	cmp	w20, 4
	ldrh	w3, [x0,x24]
	bne	.L553
.L552:
	sub	w0, w19, w22
.L548:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
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
	adrp	x1, :got:g_totle_avg_erase_count
	adrp	x2, :got:g_min_erase_count
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	ldr	x0, [x1, #:got_lo12:g_totle_avg_erase_count]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	mov	x24, x1
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x25, x2
	ldr	w3, [x0]
	ldr	x0, [x2, #:got_lo12:g_min_erase_count]
	ldr	w0, [x0]
	cmp	w3, w0
	bcs	.L558
	adrp	x0, :got:g_totle_mlc_erase_count
	adrp	x2, :got:p_erase_count_table
	ldr	x1, [x0, #:got_lo12:g_totle_mlc_erase_count]
	str	wzr, [x1]
	adrp	x1, :got:c_ftl_nand_data_blks_per_plane
	ldr	x2, [x2, #:got_lo12:p_erase_count_table]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldr	x5, [x2]
	mov	x2, 0
	ldrh	w1, [x1]
.L559:
	cmp	w1, w2
	bls	.L602
	ldr	x4, [x0, #:got_lo12:g_totle_mlc_erase_count]
	ldrh	w6, [x5,x2,lsl 1]
	add	x2, x2, 1
	ldr	w3, [x4]
	add	w3, w6, w3
	str	w3, [x4]
	b	.L559
.L602:
	ldr	x0, [x0, #:got_lo12:g_totle_mlc_erase_count]
	ldr	x2, [x24, #:got_lo12:g_totle_avg_erase_count]
	ldr	w3, [x0]
	udiv	w1, w3, w1
	str	w1, [x2]
	adrp	x1, :got:g_totle_slc_erase_count
	ldr	x1, [x1, #:got_lo12:g_totle_slc_erase_count]
	ldr	w1, [x1]
	sub	w3, w3, w1
	adrp	x1, :got:c_mlc_erase_count_value
	ldr	x1, [x1, #:got_lo12:c_mlc_erase_count_value]
	ldrh	w1, [x1]
	udiv	w3, w3, w1
	str	w3, [x0]
	b	.L561
.L558:
	adrp	x0, :got:g_max_erase_count
	ldr	x0, [x0, #:got_lo12:g_max_erase_count]
	ldr	w1, [x0]
	cmp	w3, w1
	bls	.L561
	add	w1, w1, 1
	adrp	x4, :got:c_ftl_nand_data_blks_per_plane
	str	w1, [x0]
	adrp	x5, :got:p_erase_count_table
	mov	w0, 0
.L563:
	ldr	x1, [x4, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w1, [x1]
	cmp	w0, w1
	bcs	.L561
	ldr	x1, [x5, #:got_lo12:p_erase_count_table]
	ubfiz	x2, x0, 1, 32
	add	w0, w0, 1
	ldr	x3, [x1]
	ldrh	w1, [x3,x2]
	add	w1, w1, 1
	strh	w1, [x3,x2]
	b	.L563
.L561:
	ldr	x0, [x25, #:got_lo12:g_min_erase_count]
	ldr	x1, [x24, #:got_lo12:g_totle_avg_erase_count]
	ldr	w0, [x0]
	ldr	w1, [x1]
	add	w2, w0, 341
	cmp	w2, w1
	bls	.L566
	adrp	x2, :got:g_max_erase_count
	add	w1, w0, 1024
	ldr	x2, [x2, #:got_lo12:g_max_erase_count]
	ldr	w2, [x2]
	cmp	w1, w2
	bls	.L566
	adrp	x1, :got:g_inkDie_check_enable
	ldr	x1, [x1, #:got_lo12:g_inkDie_check_enable]
	ldr	w1, [x1]
	cbz	w1, .L569
	cmp	w0, 30
	bls	.L566
.L569:
	mov	w0, 65535
	b	.L568
.L566:
	adrp	x0, :got:g_num_free_superblocks
	ldr	x0, [x0, #:got_lo12:g_num_free_superblocks]
	ldrh	w0, [x0]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	ldr	x0, [x25, #:got_lo12:g_min_erase_count]
	ldr	w1, [x0]
	add	w0, w1, 64
	cmp	w6, w0
	bcs	.L582
	cmp	w1, 30
	bhi	.L569
.L582:
	adrp	x0, :got:p_data_block_list_head
	ldr	x0, [x0, #:got_lo12:p_data_block_list_head]
	ldr	x2, [x0]
	cbz	x2, .L569
	adrp	x0, :got:p_data_block_list_table
	adrp	x21, :got:p_erase_count_table
	mov	w22, 65535
	mov	w10, 6
	mov	w19, w22
	mov	w9, w22
	ldr	x0, [x0, #:got_lo12:p_data_block_list_table]
	ldr	x8, [x0]
	ldr	x0, [x21, #:got_lo12:p_erase_count_table]
	ldr	x5, [x0]
	mov	x0, -6148914691236517206
	add	x0, x0, 1
.L571:
	ldrh	w4, [x2]
	cmp	w4, w9
	beq	.L573
	ldrh	w3, [x2,4]
	cbz	w3, .L572
	sub	x3, x2, x8
	asr	x3, x3, 1
	mul	x3, x3, x0
	uxth	w7, w3
	and	x3, x3, 65535
	ldrh	w2, [x5,x3,lsl 1]
	cmp	w2, w1
	bls	.L581
	cmp	w2, w22
	bcs	.L572
	mov	w22, w2
	mov	w19, w7
.L572:
	umull	x2, w4, w10
	add	x2, x8, x2
	b	.L571
.L581:
	mov	w19, w7
.L573:
	mov	w0, 65535
	cmp	w19, w0
	beq	.L569
	ubfiz	x23, x19, 1, 16
	ldrh	w20, [x5,x23]
	cmp	w20, w1
	bls	.L576
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x2, [x25, #:got_lo12:g_min_erase_count]
	ldr	x6, [x29,88]
	ldr	w1, [x2]
	cmp	w1, w0, uxth
	bcs	.L576
	str	w22, [x2]
.L576:
	cmp	w20, 29
	bhi	.L578
	adrp	x0, :got:g_inkDie_check_enable
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w0, [x0]
	cbz	w0, .L578
	ldr	x1, [x24, #:got_lo12:g_totle_avg_erase_count]
	add	w0, w20, 10
	ldr	w1, [x1]
	cmp	w0, w1
	bls	.L579
	adrp	x0, :got:c_mlc_erase_count_value
	ldr	x0, [x0, #:got_lo12:c_mlc_erase_count_value]
	ldrh	w0, [x0]
	cmp	w0, w20
	bls	.L578
.L579:
	adrp	x0, :got:g_num_data_superblocks
	ldr	x0, [x0, #:got_lo12:g_num_data_superblocks]
	ldrh	w0, [x0]
	cmp	w0, 64
	bls	.L578
	ldr	x21, [x21, #:got_lo12:p_erase_count_table]
	adrp	x0, .LC74
	mov	w1, w19
	add	x0, x0, :lo12:.LC74
	ldr	x2, [x21]
	ldrh	w2, [x2,x23]
	bl	printk
	b	.L601
.L578:
	ldr	x1, [x24, #:got_lo12:g_totle_avg_erase_count]
	ldr	w2, [x1]
	cmp	w20, w2
	bcs	.L569
	add	w0, w20, 128
	cmp	w6, w0
	ble	.L569
	add	w0, w20, 341
	adrp	x1, :got:g_max_erase_count
	cmp	w0, w2
	bcc	.L580
	ldr	x0, [x1, #:got_lo12:g_max_erase_count]
	add	w20, w20, 1024
	ldr	w0, [x0]
	cmp	w20, w0
	bcs	.L569
.L580:
	adrp	x0, :got:p_valid_page_count_table
	ldr	x21, [x21, #:got_lo12:p_erase_count_table]
	ldr	x3, [x1, #:got_lo12:g_max_erase_count]
	mov	w1, w19
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x5, [x21]
	ldr	w3, [x3]
	ldr	x4, [x0]
	adrp	x0, .LC75
	ldrh	w5, [x5,x23]
	add	x0, x0, :lo12:.LC75
	ldrh	w4, [x4,x23]
	bl	printk
.L601:
	adrp	x0, :got:g_in_swl_replace
	mov	w1, 1
	ldr	x0, [x0, #:got_lo12:g_in_swl_replace]
	str	w1, [x0]
	mov	w0, w19
.L568:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
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
	beq	.L604
	adrp	x1, :got:p_valid_page_count_table
	ubfiz	x2, x0, 1, 16
	ldr	x1, [x1, #:got_lo12:p_valid_page_count_table]
	ldr	x1, [x1]
	strh	wzr, [x1,x2]
	bl	INSERT_FREE_LIST
.L604:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	adrp	x0, :got:g_gc_num_req
	mov	w1, 0
	adrp	x6, :got:c_ftl_nand_planes_num
	mov	w7, 24
	adrp	x8, :got:gp_gc_page_buf_info
	mov	w9, 1
	ldr	x0, [x0, #:got_lo12:g_gc_num_req]
	adrp	x10, :got:c_ftl_nand_byte_pre_page
	mov	w3, 4
	adrp	x11, :got:p_gc_data_buf
	adrp	x12, :got:c_ftl_nand_byte_pre_oob
	adrp	x13, :got:p_gc_spare_buf
	str	wzr, [x0]
	adrp	x14, :got:req_gc
	mov	w15, 56
.L606:
	ldr	x0, [x6, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w1, w0
	bcs	.L610
	ldr	x4, [x8, #:got_lo12:gp_gc_page_buf_info]
	umull	x2, w1, w7
	ldr	x5, [x4]
	add	x0, x5, x2
	str	w9, [x0,16]
	ldr	x0, [x10, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldr	x16, [x11, #:got_lo12:p_gc_data_buf]
	ldrh	w0, [x0]
	ldr	x16, [x16]
	mul	w0, w1, w0
	sdiv	w0, w0, w3
	add	x0, x16, x0, sxtw 2
	str	x0, [x5,x2]
	ldr	x0, [x12, #:got_lo12:c_ftl_nand_byte_pre_oob]
	ldr	x5, [x4]
	ldr	x16, [x13, #:got_lo12:p_gc_spare_buf]
	ldrh	w0, [x0]
	add	x4, x5, x2
	ldr	x16, [x16]
	mul	w0, w1, w0
	sdiv	w0, w0, w3
	add	x0, x16, x0, sxtw 2
	str	x0, [x4,8]
	umull	x0, w1, w15
	ldr	x16, [x14, #:got_lo12:req_gc]
	add	w1, w1, 1
	ldr	x2, [x5,x2]
	uxth	w1, w1
	ldr	x16, [x16]
	add	x0, x16, x0
	str	x2, [x0,8]
	ldr	x2, [x4,8]
	str	x2, [x0,16]
	b	.L606
.L610:
	adrp	x6, :got:c_gc_page_buf_num
	mov	w7, 24
	adrp	x8, :got:gp_gc_page_buf_info
	adrp	x9, :got:c_ftl_nand_byte_pre_page
	mov	w3, 4
	adrp	x10, :got:p_gc_data_buf
	adrp	x11, :got:c_ftl_nand_byte_pre_oob
	adrp	x12, :got:p_gc_spare_buf
.L608:
	ldr	x1, [x6, #:got_lo12:c_gc_page_buf_num]
	ldr	w1, [x1]
	cmp	w0, w1
	bcs	.L611
	ldr	x4, [x8, #:got_lo12:gp_gc_page_buf_info]
	umull	x2, w0, w7
	ldr	x5, [x4]
	add	x1, x5, x2
	str	wzr, [x1,16]
	ldr	x1, [x9, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldr	x13, [x10, #:got_lo12:p_gc_data_buf]
	ldrh	w1, [x1]
	ldr	x13, [x13]
	mul	w1, w0, w1
	sdiv	w1, w1, w3
	add	x1, x13, x1, sxtw 2
	str	x1, [x5,x2]
	ldr	x1, [x11, #:got_lo12:c_ftl_nand_byte_pre_oob]
	ldr	x4, [x4]
	ldrh	w1, [x1]
	add	x2, x4, x2
	ldr	x4, [x12, #:got_lo12:p_gc_spare_buf]
	mul	w1, w0, w1
	add	w0, w0, 1
	ldr	x4, [x4]
	sdiv	w1, w1, w3
	uxth	w0, w0
	add	x1, x4, x1, sxtw 2
	str	x1, [x2,8]
	b	.L608
.L611:
	ret
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	adrp	x2, :got:c_gc_page_buf_num
	mov	w3, 0
	mov	w7, 56
	mov	w9, 24
	ldr	x2, [x2, #:got_lo12:c_gc_page_buf_num]
	ldr	w8, [x2]
	adrp	x2, :got:gp_gc_page_buf_info
	ldr	x2, [x2, #:got_lo12:gp_gc_page_buf_info]
	ldr	x5, [x2]
.L613:
	cmp	w3, w1
	bcs	.L612
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L618:
	cmp	w2, w8
	bcs	.L615
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L614
	str	wzr, [x10,16]
	b	.L615
.L614:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L618
.L615:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L613
.L612:
	ret
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	adrp	x2, :got:c_gc_page_buf_num
	mov	w7, 24
	mov	w8, 1
	mov	w9, 56
	ldr	x2, [x2, #:got_lo12:c_gc_page_buf_num]
	ldr	w5, [x2]
	adrp	x2, :got:gp_gc_page_buf_info
	ldr	x2, [x2, #:got_lo12:gp_gc_page_buf_info]
	ldr	x6, [x2]
	mov	w2, 0
.L620:
	cmp	w2, w1
	bcs	.L626
	mov	w3, 0
.L624:
	cmp	w3, w5
	bcs	.L622
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L621
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L622
.L621:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L624
.L622:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L620
.L626:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, :got:g_gc_blk_num
	uxth	w0, w0
	ldr	x1, [x1, #:got_lo12:g_gc_blk_num]
	ldrh	w2, [x1]
	adrp	x1, :got:p_gc_blk_tbl
	ldr	x1, [x1, #:got_lo12:p_gc_blk_tbl]
	ldr	x3, [x1]
	mov	x1, 0
.L628:
	cmp	w2, w1, uxth
	bls	.L632
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L628
	mov	w0, 1
	b	.L629
.L632:
	mov	w0, 0
.L629:
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
	lsr	w0, w0, 10
	str	x1, [x29,32]
	str	x2, [x29,40]
	bl	P2V_block_in_plane
	adrp	x3, :got:g_gc_blk_num
	ldr	x2, [x29,40]
	ldr	x1, [x29,32]
	uxth	w8, w0
	mov	x5, 0
	ldr	x4, [x3, #:got_lo12:g_gc_blk_num]
	ldrh	w7, [x4]
	adrp	x4, :got:p_gc_blk_tbl
	ldr	x4, [x4, #:got_lo12:p_gc_blk_tbl]
	ldr	x6, [x4]
.L634:
	uxth	w4, w5
	cmp	w4, w7
	bcs	.L638
	add	x5, x5, 1
	add	x9, x6, x5, lsl 1
	ldrh	w9, [x9,-2]
	cmp	w9, w8
	bne	.L634
.L638:
	cmp	w4, w7
	bne	.L636
	ubfiz	x4, x4, 1, 16
	strh	w0, [x6,x4]
	ldr	x3, [x3, #:got_lo12:g_gc_blk_num]
	ldrh	w4, [x3]
	add	w4, w4, 1
	strh	w4, [x3]
.L636:
	adrp	x3, :got:g_gc_page_offset
	adrp	x5, :got:p_gc_page_info
	mov	w0, 12
	ldr	x3, [x3, #:got_lo12:g_gc_page_offset]
	ldr	x5, [x5, #:got_lo12:p_gc_page_info]
	ldrh	w4, [x3]
	ldr	x6, [x5]
	umull	x4, w4, w0
	add	x6, x6, x4
	str	w1, [x6,4]
	ldr	x1, [x5]
	add	x0, x1, x4
	str	w2, [x0,8]
	str	w19, [x1,x4]
	ldrh	w0, [x3]
	add	w0, w0, 1
	strh	w0, [x3]
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
	adrp	x2, :got:g_gc_next_blk
	mov	w0, 65535
	ldr	x2, [x2, #:got_lo12:g_gc_next_blk]
	ldrh	w1, [x2]
	cmp	w1, w0
	beq	.L643
	adrp	x2, :got:g_gc_next_blk_1
	ldr	x2, [x2, #:got_lo12:g_gc_next_blk_1]
	ldrh	w1, [x2]
	cmp	w1, w0
	bne	.L642
.L643:
	strh	w19, [x2]
.L642:
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w20, w0
	adrp	x19, :got:g_gc_bad_block_temp_num
	mov	w0, w20
	stp	x21, x22, [sp,32]
	bl	P2V_block_in_plane
	uxth	w21, w0
	ldr	x1, [x19, #:got_lo12:g_gc_bad_block_temp_num]
	adrp	x0, .LC77
	mov	w2, w20
	add	x0, x0, :lo12:.LC77
	ldrh	w1, [x1]
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	adrp	x0, :got:g_inkDie_check_enable
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w0, [x0]
	cbz	w0, .L645
	adrp	x1, :got:p_erase_count_table
	ubfiz	x0, x21, 1, 16
	ldr	x1, [x1, #:got_lo12:p_erase_count_table]
	ldr	x2, [x1]
	ldrh	w1, [x2,x0]
	cmp	w1, 29
	bls	.L645
	sub	w1, w1, #30
	strh	w1, [x2,x0]
.L645:
	ldr	x0, [x19, #:got_lo12:g_gc_bad_block_temp_num]
	adrp	x3, :got:g_gc_bad_block_temp_tbl
	ldrh	w1, [x0]
	mov	x0, 0
.L646:
	cmp	w1, w0, uxth
	bls	.L652
	add	x0, x0, 1
	ldr	x2, [x3, #:got_lo12:g_gc_bad_block_temp_tbl]
	add	x2, x2, x0, lsl 1
	ldrh	w2, [x2,-2]
	cmp	w2, w20
	bne	.L646
	b	.L647
.L652:
	cmp	w1, 15
	bhi	.L647
	ldr	x19, [x19, #:got_lo12:g_gc_bad_block_temp_num]
	add	w0, w1, 1
	strh	w0, [x19]
	adrp	x0, :got:g_gc_bad_block_temp_tbl
	ldr	x0, [x0, #:got_lo12:g_gc_bad_block_temp_tbl]
	strh	w20, [x0,w1,sxtw 1]
.L647:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	adrp	x0, :got:g_gc_bad_block_temp_num
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:g_gc_bad_block_temp_num]
	str	x19, [sp,16]
	ldrh	w1, [x0]
	cbz	w1, .L654
	adrp	x0, :got:g_gc_next_blk
	ldr	x0, [x0, #:got_lo12:g_gc_next_blk]
	ldrh	w2, [x0]
	mov	w0, 65535
	cmp	w2, w0
	bne	.L654
	adrp	x19, :got:g_gc_bad_block_gc_index
	ldr	x0, [x19, #:got_lo12:g_gc_bad_block_gc_index]
	ldrh	w2, [x0]
	cmp	w2, w1
	bcc	.L655
	strh	wzr, [x0]
.L655:
	ldr	x19, [x19, #:got_lo12:g_gc_bad_block_gc_index]
	adrp	x0, :got:g_gc_bad_block_temp_tbl
	ldrh	w1, [x19]
	ldr	x0, [x0, #:got_lo12:g_gc_bad_block_temp_tbl]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19]
	add	w0, w0, 1
	strh	w0, [x19]
.L654:
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	ftl_malloc
	.type	ftl_malloc, %function
ftl_malloc:
	stp	x29, x30, [sp, -16]!
	mov	w1, 193
	sxtw	x0, w0
	add	x29, sp, 0
	movk	w1, 0x240, lsl 16
	bl	__kmalloc
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_malloc, .-ftl_malloc
	.align	2
	.global	NandcInit
	.type	NandcInit, %function
NandcInit:
	adrp	x1, :got:gNandChipMap
	mov	w2, 1
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:gNandChipMap]
	str	x19, [sp,16]
	adrp	x19, :got:gpNandc
	str	w2, [x1,24]
	mov	w2, 2
	str	x0, [x1]
	str	x0, [x1,16]
	str	w2, [x1,40]
	mov	w2, 3
	str	x0, [x1,32]
	str	w2, [x1,56]
	adrp	x2, :got:gBootDdrMode
	str	x0, [x1,48]
	str	wzr, [x1,8]
	ldr	x19, [x19, #:got_lo12:gpNandc]
	str	x0, [x19]
	ldr	x2, [x2, #:got_lo12:gBootDdrMode]
	ldr	w1, [x0]
	ubfx	x3, x1, 13, 1
	str	w3, [x2]
	adrp	x2, :got:gNandcVer
	ldr	w3, [x0,352]
	and	w1, w1, 245760
	ubfx	x3, x3, 16, 4
	orr	w1, w1, 256
	ldr	x2, [x2, #:got_lo12:gNandcVer]
	str	w3, [x2]
	str	w1, [x0]
	ldr	x0, [x19]
	str	wzr, [x0,336]
	mov	w0, 40
	bl	NandcTimeCfg
	ldr	x0, [x19]
	mov	w1, 8322
	str	w1, [x0,344]
	mov	w1, 6145
	movk	w1, 0x18, lsl 16
	str	w1, [x0,304]
	mov	w0, 36864
	bl	ftl_malloc
	adrp	x1, :got:gMasterTempBuf
	ldr	x1, [x1, #:got_lo12:gMasterTempBuf]
	str	x0, [x1]
	adrp	x1, :got:gMasterInfo
	ldr	x1, [x1, #:got_lo12:gMasterInfo]
	str	x0, [x1]
	add	x0, x0, 32768
	str	x0, [x1,8]
	adrp	x0, :got:gNandcDumpWriteEn
	str	wzr, [x1,40]
	ldr	x0, [x0, #:got_lo12:gNandcDumpWriteEn]
	str	wzr, [x0]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	NandcInit, .-NandcInit
	.align	2
	.global	ftl_free
	.type	ftl_free, %function
ftl_free:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	kfree
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_free, .-ftl_free
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
	mov	w19, w1
	cbnz	w0, .L664
	mov	x0, 0
	adrp	x2, :got:slcPageToMlcPageTbl
.L665:
	ldr	x1, [x2, #:got_lo12:slcPageToMlcPageTbl]
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L665
.L671:
	adrp	x20, :got:mlcPageToSlcPageTbl
	mov	w2, 1024
	mov	w1, 255
	uxth	w19, w19
	ldr	x0, [x20, #:got_lo12:mlcPageToSlcPageTbl]
	bl	ftl_memset
	mov	x0, 0
	adrp	x2, :got:slcPageToMlcPageTbl
	b	.L666
.L664:
	cmp	w0, 1
	bne	.L667
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	adrp	x3, :got:slcPageToMlcPageTbl
.L670:
	cmp	x0, 3
	uxth	w1, w0
	mov	w2, w1
	bls	.L668
	ubfiz	w2, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w4, w5, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L668:
	ldr	x1, [x3, #:got_lo12:slcPageToMlcPageTbl]
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L670
	b	.L671
.L667:
	cmp	w0, 2
	bne	.L672
	mov	w2, 65535
	mov	x0, 0
	adrp	x4, :got:slcPageToMlcPageTbl
.L674:
	ldr	x3, [x4, #:got_lo12:slcPageToMlcPageTbl]
	cmp	x0, 1
	uxth	w1, w0
	csel	w1, w2, w1, hi
	add	w2, w2, 2
	strh	w1, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w2, w2
	bne	.L674
	b	.L671
.L672:
	cmp	w0, 3
	bne	.L675
	mov	x0, 0
	mov	w5, 5
	mov	w6, 4
	adrp	x4, :got:slcPageToMlcPageTbl
.L678:
	cmp	x0, 5
	uxth	w3, w0
	mov	w2, w3
	bls	.L676
	ubfiz	w2, w3, 1, 15
	and	w3, w3, 1
	cmp	w3, wzr
	csel	w1, w5, w6, ne
	sub	w2, w2, w1
	uxth	w2, w2
.L676:
	ldr	x1, [x4, #:got_lo12:slcPageToMlcPageTbl]
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L678
	b	.L671
.L675:
	cmp	w0, 4
	bne	.L679
	adrp	x2, :got:slcPageToMlcPageTbl
	mov	w1, 1
	mov	w5, 7
	mov	w4, 6
	ldr	x2, [x2, #:got_lo12:slcPageToMlcPageTbl]
	strh	w0, [x2,8]
	mov	w0, 5
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
	strh	wzr, [x2]
	strh	w1, [x2,6]
.L681:
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
	bne	.L681
	b	.L671
.L679:
	cmp	w0, 6
	bne	.L671
	mov	x2, 0
	mov	w5, 12
	mov	w6, 10
	adrp	x4, :got:slcPageToMlcPageTbl
.L684:
	cmp	x2, 5
	uxth	w3, w2
	mov	w0, w3
	bls	.L682
	add	w0, w3, w3, lsl 1
	and	w3, w3, 1
	cmp	w3, wzr
	uxth	w0, w0
	csel	w1, w5, w6, ne
	sub	w0, w0, w1
	uxth	w0, w0
.L682:
	ldr	x1, [x4, #:got_lo12:slcPageToMlcPageTbl]
	strh	w0, [x1,x2,lsl 1]
	add	x2, x2, 1
	cmp	x2, 256
	bne	.L684
	b	.L671
.L666:
	cmp	w19, w0, uxth
	bls	.L700
	ldr	x1, [x2, #:got_lo12:slcPageToMlcPageTbl]
	ldr	x3, [x20, #:got_lo12:mlcPageToSlcPageTbl]
	ldrh	w1, [x1,x0,lsl 1]
	add	x0, x0, 1
	strh	w1, [x3,w1,sxtw 1]
	b	.L666
.L700:
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
	stp	x19, x20, [sp,16]
	adrp	x19, :got:gNandMaxDie
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x0, [x19, #:got_lo12:gNandMaxDie]
	adrp	x23, :got:gNandMaxChip
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	strb	wzr, [x0]
	adrp	x22, :got:gBlockPageAlignSize
	ldr	x0, [x23, #:got_lo12:gNandMaxChip]
	strb	wzr, [x0]
	adrp	x0, :got:gNandParaInfo
	ldr	x0, [x0, #:got_lo12:gNandParaInfo]
	ldrh	w0, [x0,10]
	cmp	w0, 256
	bls	.L702
	ldr	x0, [x22, #:got_lo12:gBlockPageAlignSize]
	mov	w1, 512
	b	.L718
.L702:
	cmp	w0, 128
	bhi	.L704
	ldr	x1, [x22, #:got_lo12:gBlockPageAlignSize]
	str	w0, [x1]
	b	.L703
.L704:
	ldr	x0, [x22, #:got_lo12:gBlockPageAlignSize]
	mov	w1, 256
.L718:
	str	w1, [x0]
.L703:
	adrp	x20, :got:DieCsIndex
	mov	w1, 0
	mov	w2, 8
	adrp	x21, :got:DieAddrs
	mov	x25, 0
	adrp	x27, :got:gpNandParaInfo
	ldr	x0, [x20, #:got_lo12:DieCsIndex]
	adrp	x28, :got:IDByte
	bl	ftl_memset
	ldr	x0, [x21, #:got_lo12:DieAddrs]
	mov	w1, 0
	mov	w2, 32
	bl	ftl_memset
	adrp	x0, :got:gDieOp
	mov	w1, 0
	mov	w2, 192
	ldr	x0, [x0, #:got_lo12:gDieOp]
	bl	ftl_memset
.L706:
	ldr	x0, [x27, #:got_lo12:gpNandParaInfo]
	adrp	x24, :got:gpNandParaInfo
	ldr	x1, [x28, #:got_lo12:IDByte]
	adrp	x26, :got:IDByte
	ldr	x2, [x0]
	add	x1, x1, x25, lsl 3
	add	x0, x2, 1
	ldrb	w2, [x2]
	bl	FlashMemCmp8
	cbnz	w0, .L705
	ldr	x2, [x19, #:got_lo12:gNandMaxDie]
	ldr	x3, [x21, #:got_lo12:DieAddrs]
	ldrb	w1, [x2]
	str	w0, [x3,w1,sxtw 2]
	add	w0, w1, 1
	strb	w0, [x2]
	ldr	x0, [x20, #:got_lo12:DieCsIndex]
	strb	w25, [x0,w1,sxtw]
.L705:
	add	x25, x25, 1
	cmp	x25, 4
	bne	.L706
	ldr	x0, [x19, #:got_lo12:gNandMaxDie]
	ldr	x23, [x23, #:got_lo12:gNandMaxChip]
	ldrb	w0, [x0]
	strb	w0, [x23]
	ldr	x0, [x24, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	beq	.L711
.L710:
	ldr	x24, [x24, #:got_lo12:gpNandParaInfo]
	adrp	x1, :got:gTotleBlock
	ldr	x19, [x19, #:got_lo12:gNandMaxDie]
	ldr	x2, [x24]
	ldrb	w0, [x19]
	ldr	x1, [x1, #:got_lo12:gTotleBlock]
	ldrb	w3, [x2,13]
	ldrh	w2, [x2,14]
	mul	w0, w0, w3
	mul	w0, w0, w2
	strh	w0, [x1]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
.L711:
	mov	x23, 0
.L707:
	ldr	x25, [x24, #:got_lo12:gpNandParaInfo]
	ldr	x1, [x26, #:got_lo12:IDByte]
	ldr	x2, [x25]
	add	x1, x1, x23, lsl 3
	add	x0, x2, 1
	ldrb	w2, [x2]
	bl	FlashMemCmp8
	cbnz	w0, .L708
	ldr	x0, [x19, #:got_lo12:gNandMaxDie]
	ldr	x3, [x25]
	ldr	x1, [x22, #:got_lo12:gBlockPageAlignSize]
	ldrb	w4, [x0]
	ldrb	w0, [x3,13]
	ldr	w1, [x1]
	mov	x2, x4
	mul	w1, w0, w1
	ldrh	w0, [x3,14]
	and	w0, w0, 65280
	mul	w0, w1, w0
	ldr	x1, [x21, #:got_lo12:DieAddrs]
	str	w0, [x1,w4,sxtw 2]
	ldrb	w3, [x3,23]
	cbz	w3, .L709
	lsl	w0, w0, 1
	str	w0, [x1,w4,sxtw 2]
.L709:
	ldr	x0, [x19, #:got_lo12:gNandMaxDie]
	add	w2, w2, 1
	strb	w2, [x0]
	ldr	x0, [x20, #:got_lo12:DieCsIndex]
	strb	w23, [x0,x4]
.L708:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L707
	b	.L710
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
	adrp	x2, :got:gpNandParaInfo
	adrp	x1, :got:gBlockPageAlignSize
	adrp	x4, :got:DieCsIndex
	mov	w5, 1
	ldr	x2, [x2, #:got_lo12:gpNandParaInfo]
	ldr	x1, [x1, #:got_lo12:gBlockPageAlignSize]
	ldr	x0, [x2]
	ldrb	w3, [x0,9]
	ldr	w0, [x1]
	adrp	x1, :got:gNandFlashEccBits
	mul	w0, w0, w3
	uxth	w0, w0
	strh	w0, [x19,4]
	ldr	x1, [x1, #:got_lo12:gNandFlashEccBits]
	ldrb	w1, [x1]
	strb	w1, [x19,7]
	ldr	x3, [x2]
	ldrb	w1, [x3,13]
	mul	w0, w1, w0
	ldrh	w1, [x3,14]
	mul	w0, w0, w1
	ldrb	w1, [x3,8]
	mul	w0, w0, w1
	str	w0, [x19]
	ldr	x0, [x2]
	ldrb	w0, [x0,9]
	strb	w0, [x19,6]
	mov	w0, 32
	strb	w0, [x19,8]
	ldr	x0, [x2]
	ldrb	w0, [x0,7]
	strb	w0, [x19,9]
	adrp	x0, :got:gNandMaxDie
	strb	wzr, [x19,10]
	ldr	x0, [x0, #:got_lo12:gNandMaxDie]
	ldrb	w3, [x0]
	mov	x0, 0
.L720:
	cmp	w3, w0, uxtb
	bls	.L722
	ldr	x1, [x4, #:got_lo12:DieCsIndex]
	ldrb	w2, [x19,10]
	ldrb	w1, [x0,x1]
	add	x0, x0, 1
	lsl	w1, w5, w1
	orr	w1, w1, w2
	strb	w1, [x19,10]
	b	.L720
.L722:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	adrp	x0, :got:g_SlcPartLbaEndSector
	mov	w1, 65535
	stp	x29, x30, [sp, -224]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:g_SlcPartLbaEndSector]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	wzr, [x0]
	adrp	x0, :got:g_all_blk_used_slc_mode
	adrp	x20, :got:c_ftl_nand_planes_num
	ldr	x0, [x0, #:got_lo12:g_all_blk_used_slc_mode]
	str	wzr, [x0]
	adrp	x0, :got:g_GlobalSysVersion
	ldr	x0, [x0, #:got_lo12:g_GlobalSysVersion]
	str	wzr, [x0]
	adrp	x0, :got:g_GlobalDataVersion
	ldr	x0, [x0, #:got_lo12:g_GlobalDataVersion]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_gc_page_count
	ldr	x0, [x0, #:got_lo12:g_totle_gc_page_count]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_write_page_count
	ldr	x0, [x0, #:got_lo12:g_totle_write_page_count]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_discard_page_count
	ldr	x0, [x0, #:got_lo12:g_totle_discard_page_count]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_cache_write_count
	ldr	x0, [x0, #:got_lo12:g_totle_cache_write_count]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_l2p_write_count
	ldr	x0, [x0, #:got_lo12:g_totle_l2p_write_count]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_read_page_count
	ldr	x0, [x0, #:got_lo12:g_totle_read_page_count]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_mlc_erase_count
	ldr	x0, [x0, #:got_lo12:g_totle_mlc_erase_count]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_slc_erase_count
	ldr	x0, [x0, #:got_lo12:g_totle_slc_erase_count]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_sys_slc_erase_count
	ldr	x0, [x0, #:got_lo12:g_totle_sys_slc_erase_count]
	str	wzr, [x0]
	adrp	x0, :got:g_max_erase_count
	ldr	x0, [x0, #:got_lo12:g_max_erase_count]
	str	wzr, [x0]
	adrp	x0, :got:g_min_erase_count
	ldr	x0, [x0, #:got_lo12:g_min_erase_count]
	str	wzr, [x0]
	adrp	x0, :got:g_in_gc_progress
	ldr	x0, [x0, #:got_lo12:g_in_gc_progress]
	str	wzr, [x0]
	adrp	x0, :got:g_in_swl_replace
	ldr	x0, [x0, #:got_lo12:g_in_swl_replace]
	str	wzr, [x0]
	adrp	x0, :got:g_gc_head_data_block
	ldr	x0, [x0, #:got_lo12:g_gc_head_data_block]
	str	w1, [x0]
	adrp	x0, :got:g_gc_head_data_block_count
	mov	w1, -1
	ldr	x0, [x0, #:got_lo12:g_gc_head_data_block_count]
	str	wzr, [x0]
	adrp	x0, :got:g_gc_skip_write_count
	ldr	x0, [x0, #:got_lo12:g_gc_skip_write_count]
	str	wzr, [x0]
	adrp	x0, :got:g_cur_erase_blk
	ldr	x0, [x0, #:got_lo12:g_cur_erase_blk]
	str	wzr, [x0]
	adrp	x0, :got:g_gc_next_blk
	ldr	x0, [x0, #:got_lo12:g_gc_next_blk]
	strh	w1, [x0]
	adrp	x0, :got:g_gc_next_blk_1
	ldr	x0, [x0, #:got_lo12:g_gc_next_blk_1]
	strh	w1, [x0]
	adrp	x0, :got:g_gc_free_blk_threshold
	mov	w1, 32
	ldr	x0, [x0, #:got_lo12:g_gc_free_blk_threshold]
	strh	w1, [x0]
	adrp	x0, :got:g_gc_merge_free_blk_threshold
	mov	w1, 128
	ldr	x0, [x0, #:got_lo12:g_gc_merge_free_blk_threshold]
	strh	w1, [x0]
	adrp	x0, :got:g_gc_blk_index
	adrp	x1, :got:c_wr_page_buf_num
	mov	x23, x1
	ldr	x0, [x0, #:got_lo12:g_gc_blk_index]
	strh	wzr, [x0]
	adrp	x0, :got:g_gc_bad_block_temp_num
	ldr	x0, [x0, #:got_lo12:g_gc_bad_block_temp_num]
	strh	wzr, [x0]
	adrp	x0, :got:g_gc_bad_block_gc_index
	ldr	x0, [x0, #:got_lo12:g_gc_bad_block_gc_index]
	strh	wzr, [x0]
	adrp	x0, :got:c_ftl_nand_sec_pre_page
	ldr	x3, [x1, #:got_lo12:c_wr_page_buf_num]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w2, [x0]
	mov	w0, 1024
	sdiv	w2, w0, w2
	str	w2, [x3]
	ldr	x0, [x20, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	lsl	w0, w0, 2
	cmp	w2, w0
	bls	.L724
	str	w0, [x3]
.L724:
	adrp	x0, :got:g_wr_page_num
	adrp	x19, :got:c_ftl_nand_page_pre_super_blk
	mov	w21, 56
	adrp	x28, :got:req_gc_dst
	adrp	x27, :got:p_sys_data_buf
	adrp	x26, :got:p_sys_data_buf_1
	ldr	x0, [x0, #:got_lo12:g_wr_page_num]
	adrp	x25, :got:p_vendor_data_buf
	adrp	x24, :got:p_gc_data_buf
	str	wzr, [x0]
	ldr	x19, [x19, #:got_lo12:c_ftl_nand_page_pre_super_blk]
	ldrh	w0, [x19]
	lsl	w0, w0, 1
	bl	ftl_malloc
	adrp	x8, :got:p_gc_blk_tbl
	str	x8, [x29,96]
	ldr	x1, [x8, #:got_lo12:p_gc_blk_tbl]
	str	x0, [x1]
	mov	w0, 12
	ldrh	w1, [x19]
	mul	w0, w1, w0
	bl	ftl_malloc
	adrp	x7, :got:p_gc_page_info
	str	x7, [x29,104]
	ldr	x1, [x7, #:got_lo12:p_gc_page_info]
	str	x0, [x1]
	ldr	x20, [x20, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w19, [x20]
	mul	w19, w19, w21
	lsl	w22, w19, 3
	mov	w0, w22
	bl	ftl_malloc
	adrp	x6, :got:req_read
	str	x6, [x29,112]
	ldr	x1, [x6, #:got_lo12:req_read]
	str	x0, [x1]
	mov	w0, w19
	bl	ftl_malloc
	ldr	x1, [x28, #:got_lo12:req_gc_dst]
	str	x0, [x1]
	mov	w0, w22
	bl	ftl_malloc
	adrp	x5, :got:req_prgm
	str	x5, [x29,120]
	ldr	x1, [x5, #:got_lo12:req_prgm]
	str	x0, [x1]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x4, :got:req_erase
	str	x4, [x29,128]
	ldr	x1, [x4, #:got_lo12:req_erase]
	str	x0, [x1]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x3, :got:req_gc
	str	x3, [x29,136]
	ldr	x1, [x3, #:got_lo12:req_gc]
	str	x0, [x1]
	ldr	x22, [x23, #:got_lo12:c_wr_page_buf_num]
	ldr	w0, [x22]
	mul	w0, w0, w21
	bl	ftl_malloc
	adrp	x2, :got:req_wr_io
	adrp	x12, :got:c_ftl_nand_byte_pre_page
	adrp	x9, :got:c_gc_page_buf_num
	ldr	x2, [x2, #:got_lo12:req_wr_io]
	str	x0, [x2]
	ldrh	w0, [x20]
	ldr	x12, [x12, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldr	x23, [x9, #:got_lo12:c_gc_page_buf_num]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	x12, [x29,144]
	ldrh	w19, [x12]
	str	w0, [x23]
	mov	w0, w19
	bl	ftl_malloc
	ldr	x2, [x27, #:got_lo12:p_sys_data_buf]
	str	x0, [x2]
	mov	w0, w19
	bl	ftl_malloc
	ldr	x2, [x26, #:got_lo12:p_sys_data_buf_1]
	str	x0, [x2]
	mov	w0, w19
	bl	ftl_malloc
	ldr	x2, [x25, #:got_lo12:p_vendor_data_buf]
	str	x0, [x2]
	ldr	w0, [x23]
	mul	w0, w19, w0
	bl	ftl_malloc
	ldr	x2, [x24, #:got_lo12:p_gc_data_buf]
	str	x0, [x2]
	ldr	w0, [x22]
	mul	w0, w19, w0
	bl	ftl_malloc
	adrp	x2, :got:p_wr_io_data_buf
	ldr	x2, [x2, #:got_lo12:p_wr_io_data_buf]
	str	x0, [x2]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x1, :got:p_io_data_buf_0
	ldr	x2, [x1, #:got_lo12:p_io_data_buf_0]
	str	x0, [x2]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x1, :got:p_io_data_buf_1
	ldr	x2, [x1, #:got_lo12:p_io_data_buf_1]
	str	x0, [x2]
	mov	w0, 24
	ldr	w2, [x23]
	mul	w0, w2, w0
	bl	ftl_malloc
	ldrh	w19, [x20]
	adrp	x1, :got:gp_gc_page_buf_info
	ldr	x2, [x1, #:got_lo12:gp_gc_page_buf_info]
	str	x0, [x2]
	adrp	x2, :got:c_ftl_nand_byte_pre_oob
	ldr	x21, [x2, #:got_lo12:c_ftl_nand_byte_pre_oob]
	ldrh	w0, [x21]
	mul	w19, w0, w19
	mov	w0, w19
	bl	ftl_malloc
	adrp	x1, :got:p_sys_spare_buf
	ldr	x10, [x1, #:got_lo12:p_sys_spare_buf]
	str	x0, [x10]
	lsl	w0, w19, 3
	bl	ftl_malloc
	adrp	x1, :got:p_io_spare_buf
	ldr	x10, [x1, #:got_lo12:p_io_spare_buf]
	str	x0, [x10]
	ldrh	w10, [x21]
	ldr	w0, [x23]
	adrp	x23, :got:p_io_data_buf_0
	mul	w0, w10, w0
	bl	ftl_malloc
	adrp	x15, :got:p_gc_spare_buf
	ldrh	w2, [x21]
	str	x15, [x29,152]
	adrp	x21, :got:gp_gc_page_buf_info
	ldr	x9, [x15, #:got_lo12:p_gc_spare_buf]
	str	x0, [x9]
	ldr	w0, [x22]
	adrp	x22, :got:p_io_data_buf_1
	mul	w0, w2, w0
	bl	ftl_malloc
	adrp	x1, :got:p_wr_io_spare_buf
	adrp	x13, :got:c_ftl_nand_blk_pre_plane
	ldr	x1, [x1, #:got_lo12:p_wr_io_spare_buf]
	str	x0, [x1]
	adrp	x1, :got:g_ect_tbl_info_size
	ldr	x19, [x13, #:got_lo12:c_ftl_nand_blk_pre_plane]
	ldr	x20, [x1, #:got_lo12:g_ect_tbl_info_size]
	ldrh	w0, [x19]
	lsl	w0, w0, 1
	strh	w0, [x20]
	and	w0, w0, 65534
	bl	ftl_malloc
	adrp	x2, :got:p_swl_mul_table
	ldr	x2, [x2, #:got_lo12:p_swl_mul_table]
	str	x0, [x2]
	ldrh	w0, [x20]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x20]
	lsl	w0, w0, 9
	bl	ftl_malloc
	adrp	x1, :got:gp_ect_tbl_info
	adrp	x14, :got:p_erase_count_table
	str	x14, [x29,160]
	ldr	x1, [x1, #:got_lo12:gp_ect_tbl_info]
	str	x0, [x1]
	add	x0, x0, 32
	ldr	x1, [x14, #:got_lo12:p_erase_count_table]
	str	x0, [x1]
	ldrh	w1, [x19]
	lsl	w20, w1, 1
	mov	w0, w20
	bl	ftl_malloc
	adrp	x2, :got:p_valid_page_count_check_table
	ldr	x2, [x2, #:got_lo12:p_valid_page_count_check_table]
	str	x0, [x2]
	mov	w0, w20
	bl	ftl_malloc
	adrp	x9, :got:p_valid_page_count_table
	adrp	x16, :got:c_ftl_nand_max_map_blks
	str	x9, [x29,168]
	ldr	x1, [x9, #:got_lo12:p_valid_page_count_table]
	str	x0, [x1]
	ldr	x16, [x16, #:got_lo12:c_ftl_nand_max_map_blks]
	str	x16, [x29,208]
	ldr	w1, [x16]
	lsl	w20, w1, 1
	mov	w0, w20
	bl	ftl_malloc
	adrp	x11, :got:p_map_block_table
	str	x11, [x29,176]
	ldr	x2, [x11, #:got_lo12:p_map_block_table]
	str	x0, [x2]
	mov	w0, w20
	bl	ftl_malloc
	adrp	x10, :got:p_map_block_valid_page_count
	str	x10, [x29,184]
	ldr	x1, [x10, #:got_lo12:p_map_block_valid_page_count]
	str	x0, [x1]
	adrp	x1, :got:c_ftl_nand_max_vendor_blks
	ldr	x20, [x1, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldrh	w0, [x20]
	lsl	w0, w0, 1
	bl	ftl_malloc
	adrp	x2, :got:p_vendor_block_table
	ldr	x2, [x2, #:got_lo12:p_vendor_block_table]
	str	x0, [x2]
	ldrh	w0, [x20]
	lsl	w0, w0, 1
	bl	ftl_malloc
	adrp	x2, :got:p_vendor_block_valid_page_count
	ldr	x2, [x2, #:got_lo12:p_vendor_block_valid_page_count]
	str	x0, [x2]
	ldrh	w0, [x20]
	adrp	x20, :got:p_sys_spare_buf
	lsl	w0, w0, 2
	bl	ftl_malloc
	adrp	x1, :got:p_vendor_block_ver_table
	ldr	x1, [x1, #:got_lo12:p_vendor_block_ver_table]
	str	x0, [x1]
	adrp	x1, :got:c_ftl_nand_vendor_region_num
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_vendor_region_num]
	str	x1, [x29,216]
	ldrh	w0, [x1]
	lsl	w0, w0, 2
	bl	ftl_malloc
	adrp	x2, :got:p_vendor_region_ppn_table
	ldr	x1, [x29,216]
	ldr	x2, [x2, #:got_lo12:p_vendor_region_ppn_table]
	str	x0, [x2]
	ldrh	w2, [x1]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	adrp	x0, :got:c_ftl_nand_map_region_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_map_region_num]
	ldrh	w0, [x0]
	lsl	w0, w0, 2
	bl	ftl_malloc
	adrp	x2, :got:p_map_region_ppn_table
	ldr	x16, [x29,208]
	str	x2, [x29,192]
	ldr	x1, [x2, #:got_lo12:p_map_region_ppn_table]
	str	x0, [x1]
	ldr	w0, [x16]
	lsl	w0, w0, 2
	bl	ftl_malloc
	adrp	x1, :got:p_map_block_ver_table
	str	x1, [x29,200]
	ldr	x16, [x1, #:got_lo12:p_map_block_ver_table]
	str	x0, [x16]
	adrp	x16, :got:c_ftl_nand_l2pmap_ram_region_num
	ldr	x16, [x16, #:got_lo12:c_ftl_nand_l2pmap_ram_region_num]
	str	x16, [x29,216]
	ldrh	w0, [x16]
	lsl	w0, w0, 4
	bl	ftl_malloc
	adrp	x17, :got:p_l2p_ram_map
	ldr	x12, [x29,144]
	ldr	x16, [x29,216]
	ldr	x17, [x17, #:got_lo12:p_l2p_ram_map]
	ldrh	w16, [x16]
	str	x0, [x17]
	ldrh	w0, [x12]
	mul	w0, w16, w0
	bl	ftl_malloc
	adrp	x12, :got:p_l2p_map_buf
	ldr	x12, [x12, #:got_lo12:p_l2p_map_buf]
	str	x0, [x12]
	mov	w0, 6
	ldrh	w12, [x19]
	adrp	x19, :got:p_io_spare_buf
	mul	w0, w12, w0
	bl	ftl_malloc
	adrp	x12, :got:p_data_block_list_table
	adrp	x16, :got:c_ftl_nand_bbm_buf_size
	adrp	x17, :got:c_ftl_nand_die_num
	ldr	x12, [x12, #:got_lo12:p_data_block_list_table]
	str	x0, [x12]
	adrp	x0, :got:c_ftl_nand_blks_per_die
	ldr	x16, [x16, #:got_lo12:c_ftl_nand_bbm_buf_size]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_blks_per_die]
	str	x16, [x29,208]
	ldrh	w0, [x0]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x16]
	ldr	x17, [x17, #:got_lo12:c_ftl_nand_die_num]
	str	x17, [x29,216]
	ldrh	w12, [x17]
	mul	w0, w0, w12
	lsl	w0, w0, 2
	bl	ftl_malloc
	adrp	x13, :got:gBbtInfo
	ldr	x17, [x29,216]
	ldr	x16, [x29,208]
	ldr	x15, [x29,152]
	ldr	x12, [x13, #:got_lo12:gBbtInfo]
	ldrh	w18, [x17]
	ldrh	w17, [x16]
	ldr	x14, [x29,160]
	ldr	x8, [x29,96]
	mov	w16, w17
	ldr	x7, [x29,104]
	ldr	x6, [x29,112]
	ldr	x5, [x29,120]
	ldr	x4, [x29,128]
	ldr	x3, [x29,136]
	ldr	x9, [x29,168]
	ldr	x11, [x29,176]
	ldr	x10, [x29,184]
	ldr	x2, [x29,192]
	ldr	x1, [x29,200]
	str	x0, [x12,32]
	mov	w0, 1
	str	x15, [x29,216]
	mov	x15, x13
	str	x14, [x29,208]
.L725:
	cmp	w0, w18
	add	x12, x12, 8
	bcs	.L727
	ldr	x14, [x15, #:got_lo12:gBbtInfo]
	add	w0, w0, 1
	ldr	x14, [x14,32]
	add	x14, x14, x16, uxtw 2
	add	w16, w16, w17
	str	x14, [x12,32]
	b	.L725
.L727:
	cmp	w0, 8
	beq	.L825
	ldr	x12, [x13, #:got_lo12:gBbtInfo]
	add	x12, x12, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x12,32]
	b	.L727
.L825:
	ldr	x11, [x11, #:got_lo12:p_map_block_table]
	ldr	x0, [x11]
	cbnz	x0, .L729
.L731:
	adrp	x0, .LC78
	adrp	x1, .LANCHOR1
	add	x0, x0, :lo12:.LC78
	add	x1, x1, :lo12:.LANCHOR1
	bl	printk
	mov	w0, -1
	b	.L730
.L729:
	ldr	x10, [x10, #:got_lo12:p_map_block_valid_page_count]
	ldr	x0, [x10]
	cbz	x0, .L731
	ldr	x2, [x2, #:got_lo12:p_map_region_ppn_table]
	ldr	x0, [x2]
	cbz	x0, .L731
	ldr	x1, [x1, #:got_lo12:p_map_block_ver_table]
	ldr	x0, [x1]
	cbz	x0, .L731
	adrp	x0, :got:p_l2p_ram_map
	ldr	x0, [x0, #:got_lo12:p_l2p_ram_map]
	ldr	x0, [x0]
	cbz	x0, .L731
	adrp	x0, :got:p_l2p_map_buf
	ldr	x0, [x0, #:got_lo12:p_l2p_map_buf]
	ldr	x0, [x0]
	cbz	x0, .L731
	adrp	x0, :got:p_data_block_list_table
	ldr	x0, [x0, #:got_lo12:p_data_block_list_table]
	ldr	x0, [x0]
	cbz	x0, .L731
	adrp	x0, :got:gBbtInfo
	ldr	x0, [x0, #:got_lo12:gBbtInfo]
	ldr	x0, [x0,32]
	cbz	x0, .L731
	ldr	x9, [x9, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x9]
	cbz	x0, .L731
	ldr	x8, [x8, #:got_lo12:p_gc_blk_tbl]
	ldr	x0, [x8]
	cbz	x0, .L731
	ldr	x7, [x7, #:got_lo12:p_gc_page_info]
	ldr	x0, [x7]
	cbz	x0, .L731
	ldr	x6, [x6, #:got_lo12:req_read]
	ldr	x0, [x6]
	cbz	x0, .L731
	ldr	x5, [x5, #:got_lo12:req_prgm]
	ldr	x0, [x5]
	cbz	x0, .L731
	ldr	x4, [x4, #:got_lo12:req_erase]
	ldr	x0, [x4]
	cbz	x0, .L731
	ldr	x3, [x3, #:got_lo12:req_gc]
	ldr	x0, [x3]
	cbz	x0, .L731
	ldr	x28, [x28, #:got_lo12:req_gc_dst]
	ldr	x0, [x28]
	cbz	x0, .L731
	ldr	x27, [x27, #:got_lo12:p_sys_data_buf]
	ldr	x0, [x27]
	cbz	x0, .L731
	ldr	x26, [x26, #:got_lo12:p_sys_data_buf_1]
	ldr	x0, [x26]
	cbz	x0, .L731
	ldr	x25, [x25, #:got_lo12:p_vendor_data_buf]
	ldr	x0, [x25]
	cbz	x0, .L731
	ldr	x24, [x24, #:got_lo12:p_gc_data_buf]
	ldr	x0, [x24]
	cbz	x0, .L731
	ldr	x23, [x23, #:got_lo12:p_io_data_buf_0]
	ldr	x0, [x23]
	cbz	x0, .L731
	ldr	x22, [x22, #:got_lo12:p_io_data_buf_1]
	ldr	x0, [x22]
	cbz	x0, .L731
	ldr	x21, [x21, #:got_lo12:gp_gc_page_buf_info]
	ldr	x0, [x21]
	cbz	x0, .L731
	ldr	x20, [x20, #:got_lo12:p_sys_spare_buf]
	ldr	x0, [x20]
	cbz	x0, .L731
	ldr	x19, [x19, #:got_lo12:p_io_spare_buf]
	ldr	x0, [x19]
	cbz	x0, .L731
	ldr	x0, [x29,216]
	ldr	x15, [x0, #:got_lo12:p_gc_spare_buf]
	ldr	x0, [x15]
	cbz	x0, .L731
	ldr	x0, [x29,208]
	ldr	x14, [x0, #:got_lo12:p_erase_count_table]
	ldr	x0, [x14]
	cbz	x0, .L731
	adrp	x0, :got:p_swl_mul_table
	ldr	x0, [x0, #:got_lo12:p_swl_mul_table]
	ldr	x0, [x0]
	cbz	x0, .L731
	adrp	x0, :got:p_vendor_block_table
	ldr	x0, [x0, #:got_lo12:p_vendor_block_table]
	ldr	x0, [x0]
	cbz	x0, .L731
	adrp	x0, :got:p_vendor_block_valid_page_count
	ldr	x0, [x0, #:got_lo12:p_vendor_block_valid_page_count]
	ldr	x0, [x0]
	cbz	x0, .L731
	adrp	x0, :got:p_vendor_block_ver_table
	ldr	x0, [x0, #:got_lo12:p_vendor_block_ver_table]
	ldr	x0, [x0]
	cbz	x0, .L731
	adrp	x0, :got:p_vendor_region_ppn_table
	ldr	x0, [x0, #:got_lo12:p_vendor_region_ppn_table]
	ldr	x0, [x0]
	cbz	x0, .L731
	mov	w0, 0
.L730:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 224
	ret
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x19, x1
	adrp	x1, :got:c_ftl_nand_bbm_buf_size
	mov	x20, x0
	mov	x0, x19
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_bbm_buf_size]
	ldrh	w2, [x1]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	x3, 0
	mov	w0, 65535
	mov	w5, 1
.L828:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L826
	ubfx	x4, x2, 5, 11
	add	x3, x3, 2
	lsl	x4, x4, 2
	lsl	w2, w5, w2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	bne	.L828
.L826:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	adrp	x0, :got:gBbtInfo
	mov	w1, -1
	stp	x29, x30, [sp, -16]!
	mov	w2, 16
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:gBbtInfo]
	add	x0, x0, 12
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
	adrp	x1, :got:gSysFreeQueue
	mov	w2, 2048
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:gSysFreeQueue]
	strh	wzr, [x1,2]
	strh	wzr, [x1,4]
	strh	wzr, [x1,6]
	strh	w0, [x1],8
	mov	x0, x1
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
.L834:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L847
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L835:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L848
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x25,x3]
	cmp	w4, w2
	bne	.L836
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L836:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L835
.L848:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L834
.L847:
	mov	w24, 0
	ldrh	w26, [x21]
	mov	w20, w24
	adrp	x27, :got:c_ftl_nand_page_pre_slc_blk
.L839:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L849
	ldrh	w0, [x19]
	uxtw	x22, w20
	cmp	w0, w20
	bne	.L840
	ldr	x0, [x27, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w1, [x19,2]
	ldrh	w0, [x0]
	cmp	w1, w0
	bcs	.L840
	strh	w0, [x21,x22,lsl 1]
.L840:
	lsl	x22, x22, 1
	ldrh	w23, [x21,x22]
	cmp	w26, w23
	bls	.L841
	mov	w24, w20
	mov	w26, w23
.L841:
	cbnz	w23, .L842
	ldrh	w0, [x25,x22]
	cbz	w0, .L842
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	w23, [x25,x22]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L842:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L839
.L849:
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, :got:c_ftl_nand_max_map_blks
	str	x23, [sp,48]
	adrp	x23, :got:p_map_block_valid_page_count
	adrp	x21, :got:c_ftl_nand_byte_pre_page
	stp	x19, x20, [sp,16]
	ldr	x1, [x22, #:got_lo12:c_ftl_nand_max_map_blks]
	adrp	x19, :got:c_ftl_nand_l2pmap_ram_region_num
	ldr	x0, [x23, #:got_lo12:p_map_block_valid_page_count]
	adrp	x20, :got:p_l2p_map_buf
	ldr	w2, [x1]
	mov	w1, 0
	ldr	x0, [x0]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x1, [x21, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldr	x0, [x20, #:got_lo12:p_l2p_map_buf]
	ldrh	w3, [x1]
	ldr	x1, [x19, #:got_lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldr	x0, [x0]
	ldrh	w2, [x1]
	mov	w1, 255
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w1, 0
	mov	x3, x23
	adrp	x5, :got:p_l2p_ram_map
	mov	w6, -1
.L851:
	ldr	x0, [x19, #:got_lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldrh	w0, [x0]
	cmp	w0, w1
	bls	.L853
	ldr	x2, [x5, #:got_lo12:p_l2p_ram_map]
	ubfiz	x0, x1, 4, 16
	ldr	x4, [x2]
	add	x4, x4, x0
	str	wzr, [x4,4]
	ldr	x4, [x2]
	strh	w6, [x4,x0]
	ldr	x2, [x2]
	ldr	x4, [x20, #:got_lo12:p_l2p_map_buf]
	add	x2, x2, x0
	ldr	x0, [x21, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldr	x4, [x4]
	ldrh	w0, [x0]
	mul	w0, w0, w1
	sxtw	x0, w0
	and	x0, x0, -4
	add	x0, x4, x0
	str	x0, [x2,8]
	add	w0, w1, 1
	uxth	w1, w0
	b	.L851
.L853:
	adrp	x0, :got:gL2pMapInfo
	mov	w2, -1
	ldr	x0, [x0, #:got_lo12:gL2pMapInfo]
	strh	w2, [x0,2]
	strh	w2, [x0]
	ldr	x1, [x22, #:got_lo12:c_ftl_nand_max_map_blks]
	ldr	w1, [x1]
	strh	w1, [x0,10]
	mov	w1, -3902
	strh	w1, [x0,4]
	adrp	x1, :got:g_totle_map_block
	ldr	x1, [x1, #:got_lo12:g_totle_map_block]
	ldrh	w1, [x1]
	strh	w1, [x0,8]
	adrp	x1, :got:c_ftl_nand_map_region_num
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_map_region_num]
	ldrh	w1, [x1]
	strh	w1, [x0,6]
	adrp	x1, :got:p_map_block_table
	ldr	x1, [x1, #:got_lo12:p_map_block_table]
	ldr	x1, [x1]
	str	x1, [x0,16]
	adrp	x1, :got:p_map_block_ver_table
	ldr	x1, [x1, #:got_lo12:p_map_block_ver_table]
	ldr	x1, [x1]
	str	x1, [x0,24]
	ldr	x1, [x3, #:got_lo12:p_map_block_valid_page_count]
	ldr	x1, [x1]
	str	x1, [x0,32]
	adrp	x1, :got:p_map_region_ppn_table
	ldr	x1, [x1, #:got_lo12:p_map_region_ppn_table]
	ldr	x1, [x1]
	str	x1, [x0,40]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	adrp	x0, :got:g_req_cache
	mov	w1, -1
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:g_req_cache]
	str	x19, [sp,16]
	adrp	x19, :got:c_ftl_nand_blk_pre_plane
	str	xzr, [x0]
	adrp	x0, :got:g_tmp_data_superblock_id
	ldr	x0, [x0, #:got_lo12:g_tmp_data_superblock_id]
	strh	w1, [x0]
	adrp	x0, :got:g_totle_swl_count
	mov	w1, -1
	ldr	x0, [x0, #:got_lo12:g_totle_swl_count]
	str	wzr, [x0]
	adrp	x0, :got:ftl_gc_temp_power_lost_recovery_flag
	ldr	x0, [x0, #:got_lo12:ftl_gc_temp_power_lost_recovery_flag]
	str	wzr, [x0]
	adrp	x0, :got:g_recovery_page_min_ver
	ldr	x0, [x0, #:got_lo12:g_recovery_page_min_ver]
	str	w1, [x0]
	adrp	x0, :got:g_inkDie_check_enable
	adrp	x1, :got:c_ftl_nand_max_vendor_blks
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	str	wzr, [x0]
	adrp	x0, :got:g_totle_vendor_block
	ldr	x0, [x0, #:got_lo12:g_totle_vendor_block]
	strh	wzr, [x0]
	adrp	x0, :got:p_vendor_block_table
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldr	x0, [x0, #:got_lo12:p_vendor_block_table]
	ldrh	w2, [x1]
	mov	w1, 0
	ldr	x0, [x0]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x19, [x19, #:got_lo12:c_ftl_nand_blk_pre_plane]
	adrp	x0, :got:p_erase_count_table
	mov	w1, 0
	ldrh	w2, [x19]
	ldr	x0, [x0, #:got_lo12:p_erase_count_table]
	lsl	w2, w2, 1
	ldr	x0, [x0]
	bl	ftl_memset
	adrp	x0, :got:p_swl_mul_table
	ldrh	w2, [x19]
	mov	w1, 0
	lsl	w2, w2, 1
	ldr	x0, [x0, #:got_lo12:p_swl_mul_table]
	ldr	x0, [x0]
	bl	ftl_memset
	adrp	x0, :got:g_sys_save_data
	mov	w1, 0
	mov	w2, 48
	ldr	x0, [x0, #:got_lo12:g_sys_save_data]
	bl	ftl_memset
	adrp	x0, :got:g_sys_ext_data
	mov	w1, 0
	mov	w2, 512
	ldr	x0, [x0, #:got_lo12:g_sys_ext_data]
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	ldr	x19, [sp,16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	stp	x29, x30, [sp, -112]!
	adrp	x0, :got:c_ftl_nand_blk_pre_plane
	mov	w2, 6
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_blk_pre_plane]
	adrp	x23, :got:p_data_block_list_table
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x1, [x23, #:got_lo12:p_data_block_list_table]
	adrp	x21, :got:g_num_data_superblocks
	ldrh	w3, [x0]
	adrp	x20, :got:g_num_free_superblocks
	mov	w24, 0
	adrp	x25, :got:c_ftl_nand_planes_num
	ldr	x0, [x1]
	mov	w1, 0
	mul	w2, w3, w2
	mov	w22, w24
	mov	w19, w24
	adrp	x26, :got:p_plane_order_table
	adrp	x27, :got:c_ftl_nand_page_pre_blk
	bl	ftl_memset
	mov	w28, 32768
	adrp	x0, :got:p_free_data_block_list_head
	ldr	x0, [x0, #:got_lo12:p_free_data_block_list_head]
	str	xzr, [x0]
	adrp	x0, :got:p_data_block_list_head
	ldr	x0, [x0, #:got_lo12:p_data_block_list_head]
	str	xzr, [x0]
	adrp	x0, :got:p_data_block_list_tail
	ldr	x0, [x0, #:got_lo12:p_data_block_list_tail]
	str	xzr, [x0]
	ldr	x0, [x21, #:got_lo12:g_num_data_superblocks]
	strh	wzr, [x0]
	ldr	x0, [x20, #:got_lo12:g_num_free_superblocks]
	strh	wzr, [x0]
.L856:
	adrp	x0, :got:c_ftl_nand_data_blks_per_plane
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w0, [x0]
	cmp	w0, w19
	bls	.L867
	mov	w2, 0
	mov	w4, w2
.L862:
	ldr	x0, [x25, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w0, w4
	bls	.L868
	ldr	x0, [x26, #:got_lo12:p_plane_order_table]
	mov	w1, w19
	str	x2, [x29,96]
	str	x4, [x29,104]
	ldrb	w0, [x0,w4,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x4, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L857
	ldr	x0, [x27, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w0, [x0]
	add	w2, w2, w0
	uxth	w2, w2
.L857:
	add	w4, w4, 1
	uxth	w4, w4
	b	.L862
.L868:
	cbz	w2, .L859
	sdiv	w2, w28, w2
.L859:
	ldr	x1, [x23, #:got_lo12:p_data_block_list_table]
	mov	w0, 6
	umull	x0, w19, w0
	ldr	x1, [x1]
	add	x0, x1, x0
	strh	w2, [x0,4]
	adrp	x0, :got:g_active_superblock
	ldr	x0, [x0, #:got_lo12:g_active_superblock]
	ldrh	w0, [x0]
	cmp	w0, w19
	beq	.L860
	adrp	x0, :got:g_buffer_superblock
	ldr	x0, [x0, #:got_lo12:g_buffer_superblock]
	ldrh	w0, [x0]
	cmp	w0, w19
	beq	.L860
	adrp	x0, :got:g_gc_temp_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	ldrh	w0, [x0]
	cmp	w0, w19
	beq	.L860
	adrp	x1, :got:p_valid_page_count_table
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x1, #:got_lo12:p_valid_page_count_table]
	ldr	x1, [x1]
	ldrh	w0, [x1,x0]
	cbnz	w0, .L861
	add	w3, w24, 1
	mov	w0, w19
	uxth	w24, w3
	bl	INSERT_FREE_LIST
	b	.L860
.L861:
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_DATA_LIST
.L860:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L856
.L867:
	ldr	x21, [x21, #:got_lo12:g_num_data_superblocks]
	mov	w0, 0
	strh	w22, [x21]
	ldr	x20, [x20, #:got_lo12:g_num_free_superblocks]
	strh	w24, [x20]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	adrp	x0, :got:g_gc_blk_num
	mov	w1, 255
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:g_gc_blk_num]
	str	x19, [sp,16]
	adrp	x19, :got:c_ftl_nand_page_pre_super_blk
	strh	wzr, [x0]
	adrp	x0, :got:g_gc_page_offset
	ldr	x0, [x0, #:got_lo12:g_gc_page_offset]
	strh	wzr, [x0]
	adrp	x0, :got:p_gc_blk_tbl
	ldr	x19, [x19, #:got_lo12:c_ftl_nand_page_pre_super_blk]
	ldr	x0, [x0, #:got_lo12:p_gc_blk_tbl]
	ldrh	w2, [x19]
	ldr	x0, [x0]
	lsl	w2, w2, 1
	bl	ftl_memset
	adrp	x0, :got:p_gc_page_info
	ldrh	w3, [x19]
	mov	w2, 12
	mov	w1, 255
	ldr	x0, [x0, #:got_lo12:p_gc_page_info]
	mul	w2, w3, w2
	ldr	x0, [x0]
	bl	ftl_memset
	bl	FtlGcBufInit
	ldr	x19, [sp,16]
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
	cbz	w0, .L871
	sub	w2, w0, #1
	cmp	w2, 5
	bhi	.L873
	bl	HynixGetReadRetryDefault
	b	.L871
.L873:
	cmp	w0, 49
	bne	.L874
	adrp	x0, :got:gReadRetryInfo
	mov	w2, 64
	ldr	x0, [x0, #:got_lo12:gReadRetryInfo]
	add	x0, x0, 4
	strb	w1, [x0,-4]
	mov	w1, 4
	strb	w1, [x0,-3]
	mov	w1, 15
	strb	w1, [x0,-2]
	adrp	x1, :got:SamsungRefValue
	ldr	x1, [x1, #:got_lo12:SamsungRefValue]
	b	.L889
.L874:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L880
	cmp	w1, 33
	bne	.L875
.L880:
	adrp	x0, :got:gReadRetryInfo
	ldr	x0, [x0, #:got_lo12:gReadRetryInfo]
	strb	w1, [x0]
	mov	w1, 4
	b	.L890
.L875:
	cmp	w1, 67
	beq	.L881
	cmp	w1, 34
	bne	.L877
.L881:
	adrp	x0, :got:gReadRetryInfo
	ldr	x0, [x0, #:got_lo12:gReadRetryInfo]
	strb	w1, [x0]
	mov	w1, 5
.L890:
	strb	w1, [x0,1]
	mov	w1, 7
	strb	w1, [x0,2]
	adrp	x1, :got:ToshibaA19RefValue
	add	x0, x0, 4
	mov	w2, 45
	ldr	x1, [x1, #:got_lo12:ToshibaA19RefValue]
	b	.L889
.L877:
	cmp	w1, 68
	beq	.L882
	cmp	w1, 35
	bne	.L871
.L882:
	adrp	x0, :got:gReadRetryInfo
	mov	w2, 95
	ldr	x0, [x0, #:got_lo12:gReadRetryInfo]
	add	x0, x0, 4
	strb	w1, [x0,-4]
	mov	w1, 5
	strb	w1, [x0,-3]
	mov	w1, 17
	strb	w1, [x0,-2]
	adrp	x1, :got:Toshiba15RefValue
	ldr	x1, [x1, #:got_lo12:Toshiba15RefValue]
.L889:
	bl	ftl_memcpy
.L871:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	FlashReadIdbData
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	adrp	x1, :got:gNandIDataBuf
	mov	w2, 2048
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:gNandIDataBuf]
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
	adrp	x19, :got:NandFlashParaTbl
	str	x23, [sp,48]
	stp	x21, x22, [sp,32]
	mov	w20, 0
	adrp	x23, :got:IDByte
	ldr	x22, [x19, #:got_lo12:NandFlashParaTbl]
.L896:
	ldr	x1, [x23, #:got_lo12:IDByte]
	add	x0, x22, 1
	ldrb	w2, [x22]
	bl	FlashMemCmp8
	mov	w21, w0
	cbnz	w0, .L893
	ldr	x19, [x19, #:got_lo12:NandFlashParaTbl]
	ubfiz	x20, x20, 5, 32
	adds	x19, x20, x19
	beq	.L899
	ldrb	w3, [x19,22]
	mov	x2, 0
	adrp	x4, .LANCHOR2
	b	.L898
.L893:
	add	w20, w20, 1
	add	x22, x22, 32
	cmp	w20, 66
	bne	.L896
	b	.L899
.L898:
	lsl	x0, x2, 5
	add	x5, x4, :lo12:.LANCHOR2
	mov	w1, w2
	ldrb	w0, [x0,x5]
	cmp	w0, w3
	beq	.L897
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L898
	mov	w1, w2
.L897:
	adrp	x0, :got:gNandOptPara
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 5, 32
	add	x1, x2, x1
	mov	w2, 32
	ldr	x0, [x0, #:got_lo12:gNandOptPara]
	bl	ftl_memcpy
	adrp	x0, :got:gNandParaInfo
	mov	x1, x19
	mov	w2, 32
	ldr	x0, [x0, #:got_lo12:gNandParaInfo]
	bl	ftl_memcpy
	b	.L894
.L899:
	mov	w21, -1
.L894:
	mov	w0, w21
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
	adrp	x4, :got:RK29_NANDC_REG_BASE
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4, #:got_lo12:RK29_NANDC_REG_BASE]
	ldr	x4, [x4]
	add	x4, x4, 4096
	cbnz	w2, .L921
	add	x1, x4, x1
	b	.L923
.L921:
	add	x0, x4, x1
	mov	x1, x6
.L923:
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
	stp	x29, x30, [sp, -16]!
	mov	w0, -1
	add	x29, sp, 0
	bl	FtlGetCapacity
	ldp	x29, x30, [sp], 16
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
.L930:
	cmp	x19, x26
	beq	.L938
	cbnz	w20, .L931
	mov	x0, x23
	mov	x1, x27
	mov	w2, w19
	bl	printk
.L931:
	cmp	w25, 4
	mov	x0, x21
	bne	.L932
	ldr	w1, [x22,x19,lsl 2]
	b	.L937
.L932:
	cmp	w25, 2
	bne	.L934
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L937
.L934:
	ldrb	w1, [x22,x19]
.L937:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L935
	adrp	x1, .LC81
	mov	x0, x24
	add	x1, x1, :lo12:.LC81
	mov	w20, 0
	bl	printk
.L935:
	add	x19, x19, 1
	b	.L930
.L938:
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
	adrp	x1, :got:gNandChipMap
	ubfiz	x0, x0, 4, 8
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	ldr	x1, [x1, #:got_lo12:gNandChipMap]
	adrp	x21, :got:gNandcVer
	stp	x19, x20, [sp,16]
	str	x23, [sp,48]
	ldr	x19, [x1,x0]
	ldr	x0, [x21, #:got_lo12:gNandcVer]
	ldr	w0, [x0]
	cmp	w0, 3
	bls	.L970
	ldr	w0, [x19,16]
	tbz	x0, 2, .L970
	mov	x0, x19
	bl	wait_for_nandc_xfer_completed
	ldr	w20, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x20, x20, 1, 1
	str	w0, [x29,64]
	cbz	w20, .L941
	adrp	x22, .LC82
	adrp	x23, .LC83
	mov	w20, 0
	add	x22, x22, :lo12:.LC82
	add	x23, x23, :lo12:.LC83
.L942:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L950
	ldr	x0, [x21, #:got_lo12:gNandcVer]
	ldr	w0, [x0]
	cmp	w0, 5
	bhi	.L943
.L946:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L942
	ldr	w2, [x19,28]
	mov	w1, w20
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
	b	.L942
.L943:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L946
	ldr	w0, [x29,72]
	tbz	x0, 17, .L946
.L950:
	adrp	x19, :got:gMasterInfo
	ldr	x19, [x19, #:got_lo12:gMasterInfo]
	ldr	w0, [x19,40]
	cbz	w0, .L951
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
	b	.L951
.L941:
	adrp	x21, .LC84
	adrp	x22, .LC83
	add	x21, x21, :lo12:.LC84
	add	x22, x22, :lo12:.LC83
.L952:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L978
	ldr	w0, [x19,8]
	add	w20, w20, 1
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L952
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x19,28]
	mov	x0, x21
	ubfx	x3, x3, 16, 5
	bl	printk
	mov	x0, x22
	mov	x1, x19
	mov	w2, 4
	mov	w3, 512
	bl	rknand_print_hex
	b	.L952
.L978:
	adrp	x0, :got:gNandcDumpWriteEn
	mov	x20, x0
	ldr	x1, [x0, #:got_lo12:gNandcDumpWriteEn]
	ldr	w1, [x1]
	cbz	w1, .L955
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L955:
	adrp	x21, :got:gMasterInfo
	ldr	x21, [x21, #:got_lo12:gMasterInfo]
	ldr	w0, [x21,40]
	cbz	w0, .L956
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
.L956:
	ldr	x0, [x20, #:got_lo12:gNandcDumpWriteEn]
	ldr	w0, [x0]
	cbz	w0, .L951
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L951:
	adrp	x0, :got:gMasterInfo
	ldr	x0, [x0, #:got_lo12:gMasterInfo]
	str	wzr, [x0,40]
	b	.L939
.L970:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L970
.L939:
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
	stp	x23, x24, [sp,48]
	uxtb	w24, w0
	adrp	x0, :got:gNandChipMap
	stp	x19, x20, [sp,16]
	uxtb	w20, w1
	sbfiz	x1, x24, 4, 32
	ldr	x0, [x0, #:got_lo12:gNandChipMap]
	uxtb	w23, w2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x25, x3
	mov	x21, x4
	ldr	x19, [x0,x1]
	and	x0, x3, 63
	cbnz	x0, .L980
	cbnz	x4, .L981
	add	x21, x29, 112
	mov	w1, 255
	mov	x0, x21
	mov	w2, 64
	bl	ftl_memset
.L981:
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
	cbnz	w20, .L982
	adrp	x0, :got:gNandcEccBits
	ubfx	x2, x23, 1, 7
	mov	w3, 128
	adrp	x5, :got:gMasterInfo
	add	x2, x21, x2, lsl 2
	ldr	x1, [x0, #:got_lo12:gNandcEccBits]
	ldr	w1, [x1]
	cmp	w1, 25
	mov	w1, 64
	csel	w3, w1, w3, cc
	mov	w1, w20
.L984:
	cmp	x21, x2
	add	w4, w1, w3
	beq	.L1026
	ldr	x6, [x5, #:got_lo12:gMasterInfo]
	and	x1, x1, 4294967292
	add	x21, x21, 4
	ldr	x6, [x6,8]
	ldr	w1, [x6,x1]
	strb	w1, [x21,-4]
	lsr	w6, w1, 8
	strb	w6, [x21,-3]
	lsr	w6, w1, 16
	strb	w6, [x21,-2]
	lsr	w1, w1, 24
	strb	w1, [x21,-1]
	mov	w1, w4
	b	.L984
.L1026:
	ldr	x0, [x0, #:got_lo12:gNandcEccBits]
	lsr	w23, w23, 2
	ldr	w5, [x0]
	adrp	x0, :got:gNandcVer
	ldr	x0, [x0, #:got_lo12:gNandcVer]
	ldr	w4, [x0]
	mov	w0, 0
	mov	w22, w0
.L986:
	cmp	w0, w23
	bcs	.L982
	cbz	w5, .L982
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x19,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L1010
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L1010
	cmp	w4, 5
	bls	.L988
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
	bls	.L989
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L990
.L989:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L1025
.L988:
	cmp	w4, 3
	bls	.L990
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
	bls	.L991
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L1025
.L991:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L1025:
	orr	w3, w1, w3, lsl 5
.L990:
	cmp	w22, w3
	csel	w22, w22, w3, cs
	b	.L987
.L1010:
	mov	w22, -1
.L987:
	add	w0, w0, 1
	b	.L986
.L982:
	str	wzr, [x19,16]
	b	.L993
.L980:
	cmp	w20, 1
	bne	.L1024
	mov	w22, 0
	mov	w27, 2
.L994:
	cmp	w22, w23
	bcs	.L1027
	and	w26, w22, 3
	mov	x3, x25
	cbz	x25, .L996
	ubfiz	x0, x22, 9, 23
	add	x3, x25, x0
.L996:
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
	b	.L994
.L1027:
	mov	w22, 0
	b	.L993
.L1024:
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
.L999:
	cmp	w26, w23
	bcs	.L993
	mov	w0, w24
	add	w28, w26, 2
	bl	NandcXferComp
	ldr	w0, [x19,32]
	cmp	w28, w23
	str	w0, [x29,104]
	bcs	.L1000
	mov	x4, 0
	mov	w0, w24
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L1000:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L1015
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w22, w0
	csel	w22, w22, w0, cs
	b	.L1001
.L1015:
	mov	w22, -1
.L1001:
	and	w2, w26, 3
	mov	x3, x25
	cbz	x25, .L1002
	ubfiz	x3, x26, 9, 23
	add	x3, x25, x3
.L1002:
	cmp	x21, xzr
	mov	x0, x19
	csel	w4, w27, wzr, ne
	mov	w1, 0
	mul	w4, w4, w26
	mov	w26, w28
	add	x4, x21, x4
	bl	NandcCopy1KB
	b	.L999
.L993:
	cbnz	w20, .L1005
	adrp	x0, :got:gNandcVer
	ldr	x0, [x0, #:got_lo12:gNandcVer]
	ldr	w0, [x0]
	cmp	w0, 5
	bls	.L1005
	ldr	w0, [x19]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L1005
	orr	w0, w0, 131072
	mov	w22, -1
	str	w0, [x19]
.L1005:
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
	adrp	x0, :got:gNandParaInfo
	str	x21, [sp,32]
	mov	x21, x2
	ldr	x0, [x0, #:got_lo12:gNandParaInfo]
	ldrb	w20, [x0,9]
	cbnz	w19, .L1029
	adrp	x0, :got:gNandIDBResBlkNum
	adrp	x5, :got:gBlockPageAlignSize
	ldr	x0, [x0, #:got_lo12:gNandIDBResBlkNum]
	ldr	x5, [x5, #:got_lo12:gBlockPageAlignSize]
	ldrb	w0, [x0]
	ldr	w2, [x5]
	mul	w0, w0, w2
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L1029:
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
	stp	x23, x24, [sp,48]
	uxtb	w24, w0
	adrp	x0, :got:gpNandc
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	ldr	x0, [x0, #:got_lo12:gpNandc]
	mov	w22, 6
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w26, w1
	mov	x25, x2
	ldr	x0, [x0]
	mov	x23, x3
	mov	w27, w4
	mov	w21, 1024
	ldr	w0, [x0,304]
	str	w0, [x29,124]
	adrp	x0, :got:gNandcVer
	ldr	x0, [x0, #:got_lo12:gNandcVer]
	ldr	w0, [x0]
	cmp	w0, 8
	mov	w0, 12
	csel	w22, w22, w0, cc
	cbz	w4, .L1032
	mov	w0, 1
	adrp	x19, :got:gFlashInterfaceMode
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	mov	w0, w24
	bl	FlashReset
	mov	w1, w26
	mov	x2, x25
	mov	x3, x23
	mov	w0, w24
	bl	FlashReadRawPage
	ldr	x19, [x19, #:got_lo12:gFlashInterfaceMode]
	mov	w21, w0
	ldrb	w0, [x19]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19]
	bl	NandcSetMode
	cmn	w21, #1
	bne	.L1033
.L1042:
	mov	w21, -1
	b	.L1034
.L1033:
	adrp	x0, .LC85
	mov	w1, w26
	add	x0, x0, :lo12:.LC85
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L1035
	adrp	x1, :got:IDByte
	sxtw	x0, w24
	lsl	x2, x0, 3
	ldr	x1, [x1, #:got_lo12:IDByte]
	ldrb	w1, [x1,x2]
	cmp	w1, 173
	bne	.L1035
	adrp	x1, :got:gNandChipMap
	lsl	x0, x0, 4
	ldr	x1, [x1, #:got_lo12:gNandChipMap]
	ldr	x0, [x1,x0]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L1035:
	adrp	x0, :got:FlashDdrTunningReadCount
	ldr	x0, [x0, #:got_lo12:FlashDdrTunningReadCount]
	ldr	w1, [x0]
	add	w1, w1, 1
	str	w1, [x0]
	cmp	w1, 2047
	bls	.L1034
	mov	x23, 0
	str	wzr, [x0]
	mov	x25, x23
.L1032:
	mov	w19, 0
	mov	w28, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L1040:
	mov	w0, w22
	str	x5, [x29,104]
	str	x6, [x29,112]
	bl	NandcSetDdrPara
	mov	w1, w26
	mov	w0, w24
	mov	x2, x25
	mov	x3, x23
	bl	FlashReadRawPage
	add	w1, w21, 1
	ldr	x6, [x29,112]
	cmp	w0, w1
	ldr	x5, [x29,104]
	bhi	.L1036
	cmp	w0, 2
	bhi	.L1046
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L1046
	sub	w19, w22, w20
	mov	w21, w0
	mov	w28, 0
	b	.L1038
.L1036:
	cmp	w6, w20
	bcs	.L1047
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L1048
	mov	w6, w20
	b	.L1047
.L1046:
	mov	x23, 0
	mov	w19, w22
	mov	w21, w0
	mov	w28, 0
	mov	x25, x23
	b	.L1037
.L1047:
	mov	w20, 0
.L1037:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L1040
.L1038:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L1039
.L1048:
	mov	w19, w5
.L1039:
	cbz	w19, .L1041
	adrp	x0, .LC86
	mov	w1, w19
	add	x0, x0, :lo12:.LC86
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L1041:
	cbz	w28, .L1034
	adrp	x0, .LC87
	mov	w1, w24
	add	x0, x0, :lo12:.LC87
	mov	w2, w26
	bl	printk
	cbz	w27, .L1042
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L1034:
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
	str	x25, [sp,64]
	mov	w0, w20
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	mov	x24, x2
	mov	w22, w1
	mov	x23, x3
	bl	FlashReadRawPage
	cmn	w0, #1
	mov	w19, w0
	bne	.L1064
	adrp	x21, :got:gNandRandomizer
	ldr	x21, [x21, #:got_lo12:gNandRandomizer]
	ldrb	w25, [x21]
	cbnz	w25, .L1065
.L1067:
	adrp	x0, :got:gFlashToggleModeEn
	ldr	x0, [x0, #:got_lo12:gFlashToggleModeEn]
	ldrb	w0, [x0]
	cbz	w0, .L1064
	b	.L1082
.L1065:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	strb	wzr, [x21]
	bl	FlashReadRawPage
	strb	w25, [x21]
	cmn	w0, #1
	beq	.L1067
	mov	w19, w0
	b	.L1064
.L1082:
	adrp	x0, :got:gpNandc
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w4, 1
	ldr	x0, [x0, #:got_lo12:gpNandc]
	ldr	x0, [x0]
	ldr	w21, [x0,304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w19, w0
	beq	.L1068
	adrp	x1, :got:gNandFlashEccBits
	ldr	x1, [x1, #:got_lo12:gNandFlashEccBits]
	ldrb	w0, [x1]
	cmp	w19, w0, lsr 1
	bls	.L1064
.L1068:
	lsr	w0, w21, 8
	bl	NandcSetDdrPara
.L1064:
	adrp	x0, :got:gpReadRetrial
	cmn	w19, #1
	ldr	x0, [x0, #:got_lo12:gpReadRetrial]
	ldr	x4, [x0]
	bne	.L1069
	cbz	x4, .L1069
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w0, w20
	blr	x4
	mov	w19, w0
	adrp	x0, .LC88
	mov	w1, w19
	add	x0, x0, :lo12:.LC88
	mov	w2, w20
	mov	w3, w22
	bl	printk
.L1069:
	mov	w0, w19
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	adrp	x19, :got:gFlashInterfaceMode
	mov	w20, w1
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	ldr	x22, [x19, #:got_lo12:gFlashInterfaceMode]
	ldrb	w0, [x22]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22]
	bl	NandcSetMode
	mov	x2, 0
	mov	w1, w20
	mov	x3, x2
	mov	w4, w2
	mov	w0, w21
	bl	FlashDdrTunningRead
	mov	w22, w0
	mov	x2, 0
	mov	w1, w20
	mov	w0, w21
	mov	x3, x2
	bl	FlashReadRawPage
	cmn	w0, #1
	mov	x1, x19
	adrp	x19, :got:gFlashToggleModeEn
	beq	.L1087
	cmn	w22, #1
	bne	.L1084
.L1087:
	ldr	x0, [x1, #:got_lo12:gFlashInterfaceMode]
	ldrb	w0, [x0]
	tbz	x0, 0, .L1084
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x19, [x19, #:got_lo12:gFlashToggleModeEn]
	strb	wzr, [x19]
	b	.L1086
.L1084:
	ldr	x19, [x19, #:got_lo12:gFlashToggleModeEn]
	mov	w0, 1
	strb	w0, [x19]
.L1086:
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
	stp	x29, x30, [sp, -144]!
	mov	w0, 60
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, :got:gNandParaInfo
	mov	w19, 0
	strb	w0, [x29,136]
	mov	w0, 40
	strb	w0, [x29,137]
	mov	w0, 24
	strb	w0, [x29,138]
	mov	w0, 16
	stp	x21, x22, [sp,32]
	strb	w0, [x29,139]
	adrp	x22, :got:gFlashPageBuffer0
	ldr	x0, [x20, #:got_lo12:gNandParaInfo]
	adrp	x21, :got:gpFlashSaveInfo
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x1, [x22, #:got_lo12:gFlashPageBuffer0]
	adrp	x23, :got:gNandFlashInfoBlockAddr
	ldrh	w0, [x0,10]
	mov	w24, 4
	str	w0, [x29,124]
	mov	w25, -1
	ldr	x0, [x21, #:got_lo12:gpFlashSaveInfo]
	adrp	x26, :got:gNandOptPara
	ldr	x1, [x1]
	adrp	x27, :got:gReadRetryInfo
	str	x21, [x29,112]
	str	x1, [x0]
	ldr	x0, [x23, #:got_lo12:gNandFlashInfoBlockAddr]
	str	wzr, [x0]
.L1095:
	add	w28, w19, 1
	mov	x21, 0
.L1097:
	add	x0, x29, 136
	ldrb	w0, [x21,x0]
	bl	FlashBchSel
	ldr	x6, [x22, #:got_lo12:gFlashPageBuffer0]
	mov	w0, 0
	mov	w1, w19
	mov	x3, 0
	str	x6, [x29,104]
	ldr	x2, [x6]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1096
	ldr	x6, [x29,104]
	mov	w0, 0
	mov	w1, w28
	mov	x3, 0
	ldr	x2, [x6]
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L1096
	add	x21, x21, 1
	cmp	x21, 4
	beq	.L1098
	b	.L1097
.L1099:
	ldr	w21, [x0,8]
	mov	w1, 2036
	add	x0, x0, 12
	mov	w25, -1
	bl	JSHash
	cmp	w21, w0
	beq	.L1109
.L1098:
	ldr	w0, [x29,124]
	subs	w24, w24, #1
	add	w19, w19, w0
	bne	.L1095
	b	.L1106
.L1096:
	ldr	x0, [x29,112]
	mov	w1, 20036
	movk	w1, 0x4e41, lsl 16
	ldr	x28, [x0, #:got_lo12:gpFlashSaveInfo]
	ldr	x0, [x28]
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1098
	cbnz	w25, .L1099
	ldr	x20, [x20, #:got_lo12:gNandParaInfo]
	adrp	x0, :got:gNandFlashIdbBlockAddr
	ldrh	w1, [x20,10]
	ldr	x0, [x0, #:got_lo12:gNandFlashIdbBlockAddr]
	udiv	w19, w19, w1
	add	w19, w19, 1
	str	w19, [x0]
	b	.L1106
.L1109:
	ldr	x21, [x20, #:got_lo12:gNandParaInfo]
	mov	w2, 32
	ldr	x1, [x28]
	mov	x0, x21
	add	x1, x1, 160
	bl	ftl_memcpy
	ldr	x1, [x28]
	mov	w2, 32
	ldr	x0, [x26, #:got_lo12:gNandOptPara]
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x28]
	mov	w2, 852
	ldr	x0, [x27, #:got_lo12:gReadRetryInfo]
	add	x1, x1, 224
	bl	ftl_memcpy
	adrp	x1, :got:gFlashToggleModeEn
	ldr	x2, [x28]
	ldr	x1, [x1, #:got_lo12:gFlashToggleModeEn]
	ldr	w0, [x2,1076]
	strb	w0, [x1]
	ldrh	w1, [x21,10]
	ldr	x0, [x23, #:got_lo12:gNandFlashInfoBlockAddr]
	udiv	w1, w19, w1
	str	w19, [x0]
	adrp	x0, :got:gNandFlashIdbBlockAddr
	add	w3, w1, 1
	ldr	x0, [x0, #:got_lo12:gNandFlashIdbBlockAddr]
	cbz	w1, .L1101
	str	w3, [x0]
	b	.L1102
.L1101:
	mov	w1, 2
	str	w1, [x0]
.L1102:
	adrp	x0, :got:gNandIDBResBlkNumSaveInFlash
	ldrh	w1, [x2,14]
	mov	w25, 0
	ldr	x0, [x0, #:got_lo12:gNandIDBResBlkNumSaveInFlash]
	strb	w1, [x0]
	b	.L1098
.L1106:
	mov	w0, w25
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	FlashLoadPhyInfo, .-FlashLoadPhyInfo
	.align	2
	.global	ToshibaReadRetrial
	.type	ToshibaReadRetrial, %function
ToshibaReadRetrial:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	mov	w0, w23
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	x26, x2
	mov	w27, w1
	mov	x25, x3
	bl	NandcWaitFlashReady
	mov	w0, w23
	adrp	x20, :got:g_retryMode
	mov	w22, 0
	bl	NandcGetChipIf
	mov	x19, x0
	ldr	x0, [x20, #:got_lo12:g_retryMode]
	ldrb	w0, [x0]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1111
	adrp	x0, :got:gFlashToggleModeEn
	ldr	x0, [x0, #:got_lo12:gFlashToggleModeEn]
	ldrb	w0, [x0]
	cbz	w0, .L1112
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L1112:
	mov	w0, 92
	str	w0, [x19,8]
	mov	w0, 197
	str	w0, [x19,8]
.L1111:
	mov	w21, 1
	mov	w24, -1
.L1113:
	adrp	x0, :got:g_maxRetryCount
	ldr	x0, [x0, #:got_lo12:g_maxRetryCount]
	ldrb	w0, [x0]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L1138
	ldr	x0, [x20, #:got_lo12:g_retryMode]
	mov	w1, w21
	ldrb	w0, [x0]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L1114
	bl	SandiskSetRRPara
	b	.L1115
.L1114:
	bl	ToshibaSetRRPara
.L1115:
	ldr	x0, [x20, #:got_lo12:g_retryMode]
	ldrb	w0, [x0]
	cmp	w0, 34
	bne	.L1116
	adrp	x0, :got:g_maxRetryCount
	ldr	x0, [x0, #:got_lo12:g_maxRetryCount]
	ldrb	w0, [x0]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L1116
	mov	w0, 179
	str	w0, [x19,8]
.L1116:
	mov	w0, 38
	str	w0, [x19,8]
	mov	w0, 93
	str	w0, [x19,8]
	cbz	w22, .L1117
	mov	w0, 4
	bl	NandcSetDdrMode
	mov	w0, w23
	mov	w1, w27
	mov	x2, x26
	mov	x3, x25
	bl	FlashReadRawPage
	mov	w28, w0
	mov	w0, 0
	bl	NandcSetDdrMode
	b	.L1118
.L1117:
	mov	w0, w23
	mov	w1, w27
	mov	x2, x26
	mov	x3, x25
	bl	FlashReadRawPage
	mov	w28, w0
.L1118:
	cmn	w28, #1
	beq	.L1119
	adrp	x0, :got:gNandFlashEccBits
	cmn	w24, #1
	csel	w24, w24, w28, ne
	ldr	x0, [x0, #:got_lo12:gNandFlashEccBits]
	ldrb	w0, [x0]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1121
	mov	x25, 0
	mov	x26, x25
.L1119:
	add	w21, w21, 1
	b	.L1113
.L1138:
	mov	w28, w24
.L1121:
	ldr	x20, [x20, #:got_lo12:g_retryMode]
	mov	w1, 0
	ldrb	w0, [x20]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L1123
	bl	SandiskSetRRPara
	b	.L1124
.L1123:
	bl	ToshibaSetRRPara
.L1124:
	mov	w0, 255
	str	w0, [x19,8]
	adrp	x0, :got:gNandFlashEccBits
	ldr	x0, [x0, #:got_lo12:gNandFlashEccBits]
	ldrb	w0, [x0]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1125
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1125:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L1126
	mov	w0, 4
	bl	NandcSetDdrMode
.L1126:
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	ToshibaReadRetrial, .-ToshibaReadRetrial
	.align	2
	.global	SamsungReadRetrial
	.type	SamsungReadRetrial, %function
SamsungReadRetrial:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	str	x27, [sp,80]
	mov	w0, w21
	mov	x22, x3
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w23, w1
	mov	x25, x2
	bl	NandcWaitFlashReady
	mov	w20, 1
	mov	w0, w21
	mov	w19, -1
	adrp	x26, :got:g_maxRetryCount
	adrp	x27, :got:gNandFlashEccBits
	bl	NandcGetChipIf
	mov	x24, x0
.L1140:
	ldr	x0, [x26, #:got_lo12:g_maxRetryCount]
	ldrb	w4, [x0]
	add	w4, w4, 1
	cmp	w20, w4
	bcs	.L1143
	mov	x0, x24
	mov	w1, w20
	bl	SamsungSetRRPara
	mov	w0, w21
	mov	w1, w23
	mov	x2, x25
	mov	x3, x22
	bl	FlashReadRawPage
	cmn	w0, #1
	mov	w4, w0
	beq	.L1141
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x0, [x27, #:got_lo12:gNandFlashEccBits]
	ldrb	w2, [x0]
	add	w2, w2, w2, lsl 1
	cmp	w4, w2, lsr 2
	bcc	.L1146
	mov	x22, 0
	mov	x25, x22
.L1141:
	add	w20, w20, 1
	b	.L1140
.L1146:
	mov	w19, w4
.L1143:
	mov	x0, x24
	mov	w1, 0
	bl	SamsungSetRRPara
	adrp	x0, :got:gNandFlashEccBits
	ldr	x0, [x0, #:got_lo12:gNandFlashEccBits]
	ldrb	w0, [x0]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1145
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1145:
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	mov	w21, 0
	mov	w0, w22
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w25, w1
	mov	x24, x2
	mov	x23, x3
	bl	NandcWaitFlashReady
	mov	w0, w22
	mov	w20, -1
	mov	w26, 239
	mov	w27, 137
	bl	NandcGetChipIf
	adrp	x28, :got:gNandFlashEccBits
	mov	x19, x0
.L1156:
	str	w26, [x19,8]
	mov	w0, 200
	str	w27, [x19,4]
	add	w21, w21, 1
	bl	NandcDelayns
	str	w21, [x19]
	str	wzr, [x19]
	mov	w0, w22
	str	wzr, [x19]
	mov	w1, w25
	str	wzr, [x19]
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1153
	ldr	x1, [x28, #:got_lo12:gNandFlashEccBits]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	ldrb	w2, [x1]
	add	w2, w2, w2, lsl 1
	cmp	w0, w2, lsr 2
	bcc	.L1158
	mov	x23, 0
	mov	x24, x23
.L1153:
	cmp	w21, 7
	bne	.L1156
	b	.L1155
.L1158:
	mov	w20, w0
.L1155:
	mov	w0, 239
	str	w0, [x19,8]
	mov	w0, 137
	str	w0, [x19,4]
	mov	w0, 200
	bl	NandcDelayns
	str	wzr, [x19]
	str	wzr, [x19]
	adrp	x0, :got:gNandFlashEccBits
	str	wzr, [x19]
	str	wzr, [x19]
	ldr	x0, [x0, #:got_lo12:gNandFlashEccBits]
	ldrb	w0, [x0]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L1157
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1157:
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	MicronReadRetrial, .-MicronReadRetrial
	.align	2
	.global	HynixReadRetrial
	.type	HynixReadRetrial, %function
HynixReadRetrial:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, :got:gReadRetryInfo
	uxtb	x22, w0
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	ldr	x0, [x21, #:got_lo12:gReadRetryInfo]
	mov	w27, w1
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	add	x1, x0, x22
	mov	x26, x2
	ldrb	w25, [x0,2]
	mov	w0, w22
	mov	x24, x3
	mov	x23, x22
	ldrb	w20, [x1,12]
	bl	NandcWaitFlashReady
	mov	w28, 0
	mov	w19, -1
	adrp	x4, :got:gNandFlashEccBits
.L1166:
	cmp	w28, w25
	bcs	.L1170
	add	w20, w20, 1
	ldr	x2, [x21, #:got_lo12:gReadRetryInfo]
	mov	w0, w23
	str	x4, [x29,104]
	uxtb	w20, w20
	add	x2, x2, 4
	cmp	w20, w25
	ldrb	w1, [x2,-3]
	csel	w20, w20, wzr, cc
	mov	w3, w20
	bl	HynixSetRRPara
	mov	w0, w23
	mov	w1, w27
	mov	x2, x26
	mov	x3, x24
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x4, [x29,104]
	beq	.L1168
	ldr	x1, [x4, #:got_lo12:gNandFlashEccBits]
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1173
	mov	x24, 0
	mov	x26, x24
.L1168:
	add	w28, w28, 1
	b	.L1166
.L1173:
	mov	w19, w0
.L1170:
	ldr	x21, [x21, #:got_lo12:gReadRetryInfo]
	adrp	x0, :got:gNandFlashEccBits
	add	x21, x21, x22
	strb	w20, [x21,12]
	ldr	x0, [x0, #:got_lo12:gNandFlashEccBits]
	ldrb	w0, [x0]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1172
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1172:
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
	adrp	x0, :got:gNandParaInfo
	stp	x21, x22, [sp,32]
	mov	w20, w1
	mov	x22, x2
	ldr	x0, [x0, #:got_lo12:gNandParaInfo]
	ldrb	w21, [x0,9]
	cbnz	w19, .L1180
	adrp	x0, :got:gNandIDBResBlkNum
	adrp	x1, :got:gBlockPageAlignSize
	ldr	x0, [x0, #:got_lo12:gNandIDBResBlkNum]
	ldr	x1, [x1, #:got_lo12:gBlockPageAlignSize]
	ldrb	w0, [x0]
	ldr	w1, [x1]
	mul	w0, w0, w1
	cmp	w20, w0
	bcs	.L1180
	adrp	x0, :got:g_slc2KBNand
	ldr	x0, [x0, #:got_lo12:g_slc2KBNand]
	ldrb	w0, [x0]
	cbnz	w0, .L1181
	sub	w21, w21, #2
	b	.L1180
.L1181:
	mov	w21, 4
.L1180:
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
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, :got:gFlashPageBuffer0
	adrp	x24, :got:gFlashPageBuffer1
	stp	x21, x22, [sp,32]
	adrp	x22, :got:gpFlashSaveInfo
	stp	x19, x20, [sp,16]
	ldr	x20, [x23, #:got_lo12:gFlashPageBuffer0]
	ldr	x19, [x22, #:got_lo12:gpFlashSaveInfo]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x25, :got:gBlockPageAlignSize
	adrp	x26, :got:gNandFlashIdbBlockAddr
	ldr	x0, [x20]
	adrp	x27, :got:gNandFlashInfoBlockAddr
	str	x0, [x19]
	adrp	x0, :got:gNandFlashIDBEccBits
	ldr	x0, [x0, #:got_lo12:gNandFlashIDBEccBits]
	ldrb	w0, [x0]
	bl	FlashBchSel
	ldr	x0, [x20]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	mov	w2, 32
	str	w0, [x1]
	adrp	x1, :got:gNandMaxDie
	ldr	x0, [x19]
	ldr	x1, [x1, #:got_lo12:gNandMaxDie]
	add	x0, x0, 16
	ldrb	w1, [x1]
	strh	w1, [x0,-4]
	adrp	x1, :got:gNandIDBResBlkNum
	ldr	x1, [x1, #:got_lo12:gNandIDBResBlkNum]
	ldrb	w1, [x1]
	strh	w1, [x0,-2]
	adrp	x1, :got:gFlashToggleModeEn
	ldr	x1, [x1, #:got_lo12:gFlashToggleModeEn]
	ldrb	w1, [x1]
	str	w1, [x0,1060]
	adrp	x1, :got:IDByte
	ldr	x1, [x1, #:got_lo12:IDByte]
	bl	ftl_memcpy
	adrp	x1, :got:DieCsIndex
	ldr	x0, [x19]
	mov	w2, 8
	add	x0, x0, 80
	ldr	x1, [x1, #:got_lo12:DieCsIndex]
	bl	ftl_memcpy
	adrp	x1, :got:DieAddrs
	ldr	x0, [x19]
	mov	w2, 32
	add	x0, x0, 96
	ldr	x1, [x1, #:got_lo12:DieAddrs]
	bl	ftl_memcpy
	adrp	x1, :got:gNandParaInfo
	ldr	x0, [x19]
	mov	w2, 32
	add	x0, x0, 160
	ldr	x1, [x1, #:got_lo12:gNandParaInfo]
	bl	ftl_memcpy
	adrp	x1, :got:gNandOptPara
	ldr	x0, [x19]
	mov	w2, 32
	add	x0, x0, 192
	ldr	x1, [x1, #:got_lo12:gNandOptPara]
	bl	ftl_memcpy
	adrp	x1, :got:gReadRetryInfo
	ldr	x0, [x19]
	mov	w2, 852
	add	x0, x0, 224
	ldr	x1, [x1, #:got_lo12:gReadRetryInfo]
	bl	ftl_memcpy
	ldr	x20, [x19]
	mov	w1, 2036
	add	x0, x20, 12
	bl	JSHash
	str	w0, [x20,8]
	ldr	x0, [x19]
	mov	w1, 1592
	str	w1, [x0,4]
	ldr	x0, [x24, #:got_lo12:gFlashPageBuffer1]
	ldr	x0, [x0]
	str	x0, [x19]
	mov	w19, 0
	mov	w20, w19
.L1185:
	ldr	x21, [x25, #:got_lo12:gBlockPageAlignSize]
	mov	w2, 0
	mov	w0, 0
	ldr	w1, [x21]
	mul	w1, w20, w1
	bl	FlashEraseBlock
	ldr	x28, [x23, #:got_lo12:gFlashPageBuffer0]
	ldr	w1, [x21]
	mov	x3, 0
	mov	w0, 0
	ldr	x2, [x28]
	mul	w1, w20, w1
	bl	FlashProgPage
	ldr	w1, [x21]
	mov	x3, 0
	ldr	x2, [x28]
	mov	w0, 0
	add	w28, w20, 1
	mul	w1, w20, w1
	add	w1, w1, 1
	bl	FlashProgPage
	ldr	w1, [x21]
	mov	w0, 0
	ldr	x2, [x24, #:got_lo12:gFlashPageBuffer1]
	mov	x3, 0
	mul	w1, w20, w1
	ldr	x2, [x2]
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L1186
	ldr	x0, [x22, #:got_lo12:gpFlashSaveInfo]
	mov	w1, 20036
	movk	w1, 0x4e41, lsl 16
	ldr	x0, [x0]
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1186
	ldr	w2, [x0,8]
	mov	w1, 2036
	add	x0, x0, 12
	str	x2, [x29,104]
	bl	JSHash
	ldr	x2, [x29,104]
	cmp	w2, w0
	bne	.L1186
	ldr	x0, [x26, #:got_lo12:gNandFlashIdbBlockAddr]
	str	w28, [x0]
	ldr	w0, [x21]
	ldr	x1, [x27, #:got_lo12:gNandFlashInfoBlockAddr]
	mul	w20, w20, w0
	str	w20, [x1]
	cbnz	w19, .L1187
	mov	w19, 1
.L1186:
	cmp	w28, 4
	mov	w20, w28
	bne	.L1185
	eor	w0, w19, 1
	neg	w0, w0
	b	.L1184
.L1187:
	mov	w0, 0
.L1184:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	2
	.global	FlashReadIdbDataRaw
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	stp	x29, x30, [sp, -128]!
	mov	w1, 60
	mov	w2, 2048
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	strb	w1, [x29,120]
	mov	w1, 40
	strb	w1, [x29,121]
	mov	w1, 24
	strb	w1, [x29,122]
	mov	w1, 16
	strb	w1, [x29,123]
	adrp	x1, :got:gNandFlashEccBits
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	ldr	x1, [x1, #:got_lo12:gNandFlashEccBits]
	adrp	x21, .LC89
	stp	x27, x28, [sp,80]
	mov	w19, 2
	mov	w28, -1
	ldrb	w22, [x1]
	adrp	x24, :got:gNandIDBResBlkNum
	mov	w1, 0
	adrp	x25, :got:gBlockPageAlignSize
	adrp	x26, :got:gFlashPageBuffer0
	add	x21, x21, :lo12:.LC89
	mov	x23, x0
	bl	ftl_memset
.L1193:
	ldr	x0, [x24, #:got_lo12:gNandIDBResBlkNum]
	ldrb	w0, [x0]
	cmp	w19, w0
	bcs	.L1197
	mov	x27, 0
.L1195:
	add	x0, x29, 120
	ldrb	w4, [x27,x0]
	str	x4, [x29,104]
	mov	w0, w4
	bl	FlashBchSel
	ldr	x20, [x26, #:got_lo12:gFlashPageBuffer0]
	ldr	x0, [x25, #:got_lo12:gBlockPageAlignSize]
	mov	x3, 0
	ldr	x2, [x20]
	ldr	w1, [x0]
	mov	w0, 0
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x4, [x29,104]
	bne	.L1194
	add	x27, x27, 1
	cmp	x27, 4
	bne	.L1195
	b	.L1196
.L1194:
	ldr	x0, [x20]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L1196
	mov	w1, w4
	mov	x0, x21
	bl	printk
	ldr	x1, [x20]
	mov	x0, x23
	mov	w2, 2048
	bl	ftl_memcpy
	adrp	x0, :got:gNandIDBResBlkNum
	ldr	x1, [x20]
	ldr	x0, [x0, #:got_lo12:gNandIDBResBlkNum]
	ldr	w1, [x1,512]
	strb	w1, [x0]
	adrp	x1, :got:gNandFlashIdbBlockAddr
	ldr	x1, [x1, #:got_lo12:gNandFlashIdbBlockAddr]
	ldr	w0, [x1]
	cmp	w0, w19
	bls	.L1199
	mov	w28, 0
	str	w19, [x1]
	bl	FlashSavePhyInfo
.L1196:
	add	w19, w19, 1
	b	.L1193
.L1199:
	mov	w28, 0
.L1197:
	mov	w0, w22
	bl	FlashBchSel
	mov	w0, w28
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
	stp	x19, x20, [sp,16]
	mov	x19, x0
	mov	w0, 32768
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	bl	ftl_malloc
	adrp	x20, :got:gNandIDBResBlkNum
	adrp	x1, :got:gFlashPageBuffer0
	adrp	x24, :got:gNandIDBResBlkNumSaveInFlash
	adrp	x23, :got:gFlashToggleModeEn
	adrp	x22, :got:g_slc2KBNand
	adrp	x25, :got:gNandFlashIDBEccBits
	adrp	x21, :got:IDByte
	ldr	x1, [x1, #:got_lo12:gFlashPageBuffer0]
	adrp	x27, .LC90
	mov	w28, 0
	add	x27, x27, :lo12:.LC90
	str	x0, [x1]
	mov	w0, 32768
	bl	ftl_malloc
	adrp	x1, :got:gFlashPageBuffer1
	ldr	x1, [x1, #:got_lo12:gFlashPageBuffer1]
	str	x0, [x1]
	mov	w0, 4096
	bl	ftl_malloc
	adrp	x1, :got:gFlashSpareBuffer
	ldr	x1, [x1, #:got_lo12:gFlashSpareBuffer]
	str	x0, [x1]
	mov	w0, 32768
	bl	ftl_malloc
	adrp	x1, :got:gFlashProgCheckBuffer
	ldr	x1, [x1, #:got_lo12:gFlashProgCheckBuffer]
	str	x0, [x1]
	mov	w0, 4096
	bl	ftl_malloc
	adrp	x1, :got:gFlashProgCheckSpareBuffer
	ldr	x1, [x1, #:got_lo12:gFlashProgCheckSpareBuffer]
	str	x0, [x1]
	mov	w1, 50
	ldr	x0, [x20, #:got_lo12:gNandIDBResBlkNum]
	strb	w1, [x0]
	ldr	x0, [x24, #:got_lo12:gNandIDBResBlkNumSaveInFlash]
	strb	w1, [x0]
	adrp	x1, :got:gNandFlashIdbBlockAddr
	mov	w0, 128
	ldr	x1, [x1, #:got_lo12:gNandFlashIdbBlockAddr]
	str	wzr, [x1]
	adrp	x1, :got:gBlockPageAlignSize
	ldr	x1, [x1, #:got_lo12:gBlockPageAlignSize]
	str	w0, [x1]
	adrp	x1, :got:FlashDdrTunningReadCount
	ldr	x0, [x23, #:got_lo12:gFlashToggleModeEn]
	strb	wzr, [x0]
	ldr	x1, [x1, #:got_lo12:FlashDdrTunningReadCount]
	str	wzr, [x1]
	mov	w1, 60
	ldr	x0, [x22, #:got_lo12:g_slc2KBNand]
	strb	wzr, [x0]
	ldr	x0, [x25, #:got_lo12:gNandFlashIDBEccBits]
	strb	w1, [x0]
	mov	x0, x19
	bl	NandcInit
	ldr	x19, [x21, #:got_lo12:IDByte]
	str	x24, [x29,104]
.L1207:
	uxtb	w26, w28
	mov	w0, w26
	bl	FlashReset
	mov	w0, w26
	bl	NandcGetChipIf
	mov	x24, x0
	mov	w0, w26
	bl	NandcFlashCs
	mov	w0, 144
	str	w0, [x24,8]
	str	wzr, [x24,4]
	mov	w0, 200
	bl	NandcDelayns
	ldr	w0, [x24]
	strb	w0, [x19]
	ldr	w0, [x24]
	strb	w0, [x19,1]
	ldr	w0, [x24]
	strb	w0, [x19,2]
	ldr	w0, [x24]
	strb	w0, [x19,3]
	ldr	w0, [x24]
	strb	w0, [x19,4]
	ldr	w0, [x24]
	strb	w0, [x19,5]
	ldr	w0, [x24]
	strb	w0, [x19,6]
	ldr	w0, [x24]
	strb	w0, [x19,7]
	mov	w0, w26
	bl	NandcFlashDeCs
	ldrb	w2, [x19]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1204
	ldrb	w3, [x19,1]
	mov	x0, x27
	ldrb	w4, [x19,2]
	add	w1, w28, 1
	ldrb	w5, [x19,3]
	ldrb	w6, [x19,4]
	ldrb	w7, [x19,5]
	bl	printk
.L1204:
	cbnz	w28, .L1205
	ldr	x1, [x21, #:got_lo12:IDByte]
	ldrb	w0, [x1]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L1243
	ldrb	w0, [x1,1]
	cmp	w0, 255
	beq	.L1243
	bl	FlashCs123Init
.L1205:
	add	w28, w28, 1
	add	x19, x19, 8
	cmp	w28, 4
	bne	.L1207
	ldr	x0, [x21, #:got_lo12:IDByte]
	ldrb	w0, [x0]
	cmp	w0, 173
	beq	.L1208
	adrp	x0, :got:gBootDdrMode
	ldr	x0, [x0, #:got_lo12:gBootDdrMode]
	ldr	w0, [x0]
	bl	NandcSetDdrMode
.L1208:
	adrp	x26, :got:gReadRetryInfo
	mov	w1, 0
	mov	w2, 852
	adrp	x19, :got:gpNandParaInfo
	adrp	x27, :got:gNandRandomizer
	ldr	x0, [x26, #:got_lo12:gReadRetryInfo]
	bl	ftl_memset
	adrp	x0, :got:gNandParaInfo
	ldr	x1, [x19, #:got_lo12:gpNandParaInfo]
	mov	x28, x0
	ldr	x2, [x0, #:got_lo12:gNandParaInfo]
	str	x2, [x1]
	ldr	x1, [x27, #:got_lo12:gNandRandomizer]
	strb	wzr, [x1]
	ldr	x1, [x21, #:got_lo12:IDByte]
	ldrb	w2, [x1,1]
	cmp	w2, 218
	cset	w1, eq
	cbnz	w1, .L1244
	cmp	w2, 241
	bne	.L1209
.L1244:
	ldr	x0, [x22, #:got_lo12:g_slc2KBNand]
	mov	w2, 1
	strb	w2, [x0]
	mov	w2, 16
	ldr	x0, [x20, #:got_lo12:gNandIDBResBlkNum]
	strb	w2, [x0]
	ldr	x25, [x25, #:got_lo12:gNandFlashIDBEccBits]
	strb	w2, [x25]
	ldr	x0, [x21, #:got_lo12:IDByte]
	ldrb	w0, [x0]
	cmp	w0, 152
	bne	.L1211
	mov	w0, 24
	strb	w0, [x25]
.L1211:
	adrp	x25, :got:gSlcNandParaInfo
	cbz	w1, .L1212
	ldr	x0, [x25, #:got_lo12:gSlcNandParaInfo]
	mov	w1, 2048
	strh	w1, [x0,14]
.L1212:
	adrp	x0, :got:gNandOptPara
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	mov	w2, 32
	add	x1, x1, 32
	ldr	x0, [x0, #:got_lo12:gNandOptPara]
	bl	ftl_memcpy
	ldr	x0, [x28, #:got_lo12:gNandParaInfo]
	mov	w2, 32
	ldr	x1, [x25, #:got_lo12:gSlcNandParaInfo]
	bl	ftl_memcpy
.L1209:
	ldr	x0, [x22, #:got_lo12:g_slc2KBNand]
	ldrb	w0, [x0]
	cbnz	w0, .L1213
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L1215
	ldr	x0, [x19, #:got_lo12:gpNandParaInfo]
	adrp	x25, :got:gFlashInterfaceMode
	ldr	x0, [x0]
	ldr	x25, [x25, #:got_lo12:gFlashInterfaceMode]
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x25]
	tbnz	x1, 0, .L1215
	ldr	x1, [x23, #:got_lo12:gFlashToggleModeEn]
	mov	w2, 1
	strb	w2, [x1]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x25]
	bl	NandcSetMode
.L1215:
	bl	FlashLoadPhyInfo
	cbz	w0, .L1213
	adrp	x0, :got:gBootDdrMode
	ldr	x0, [x0, #:got_lo12:gBootDdrMode]
	ldr	w0, [x0]
	cbz	w0, .L1218
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L1275
.L1218:
	adrp	x25, :got:gFlashInterfaceMode
	ldr	x25, [x25, #:got_lo12:gFlashInterfaceMode]
	ldrb	w0, [x25]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x25]
.L1275:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L1213
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x25, [x19, #:got_lo12:gpNandParaInfo]
	adrp	x0, .LC91
	add	x0, x0, :lo12:.LC91
	ldr	x1, [x25]
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L1206
	bl	FlashDieInfoInit
	ldr	x0, [x25]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	adrp	x1, :got:gTotleBlock
	ldr	x0, [x25]
	ldr	x1, [x1, #:got_lo12:gTotleBlock]
	ldrb	w2, [x0,9]
	ldrh	w1, [x1]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L1220
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L1221
.L1220:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L1221:
	adrp	x0, :got:gFlashInterfaceMode
	ldr	x0, [x0, #:got_lo12:gFlashInterfaceMode]
	ldrb	w0, [x0]
	tst	w0, 6
	beq	.L1222
	bl	FlashSavePhyInfo
	adrp	x1, :got:gNandFlashInfoBlockAddr
	mov	w0, 0
	ldr	x1, [x1, #:got_lo12:gNandFlashInfoBlockAddr]
	ldr	w1, [x1]
	bl	FlashDdrParaScan
.L1222:
	bl	FlashSavePhyInfo
.L1213:
	ldr	x0, [x19, #:got_lo12:gpNandParaInfo]
	adrp	x25, :got:gFlashInterfaceMode
	ldr	x27, [x27, #:got_lo12:gNandRandomizer]
	adrp	x3, :got:gpReadRetrial
	ldr	x4, [x0]
	ldrh	w0, [x4,16]
	ubfx	x1, x0, 7, 1
	strb	w1, [x27]
	adrp	x1, :got:gMultiPageReadEn
	ubfx	x2, x0, 3, 1
	ldr	x1, [x1, #:got_lo12:gMultiPageReadEn]
	strb	w2, [x1]
	adrp	x1, :got:gMultiPageProgEn
	ubfx	x2, x0, 4, 1
	ldr	x1, [x1, #:got_lo12:gMultiPageProgEn]
	strb	w2, [x1]
	ubfx	x2, x0, 8, 3
	ldr	x1, [x25, #:got_lo12:gFlashInterfaceMode]
	strb	w2, [x1]
	ldr	x6, [x3, #:got_lo12:gpReadRetrial]
	str	xzr, [x6]
	tbz	x0, 6, .L1224
	adrp	x1, :got:g_retryMode
	ldrb	w0, [x4,19]
	ldr	x1, [x1, #:got_lo12:g_retryMode]
	strb	w0, [x1]
	adrp	x1, :got:g_maxRegNum
	ldr	x26, [x26, #:got_lo12:gReadRetryInfo]
	ldr	x2, [x1, #:got_lo12:g_maxRegNum]
	ldrb	w5, [x26,1]
	strb	w5, [x2]
	adrp	x2, :got:g_maxRetryCount
	ldrb	w7, [x26,2]
	ldr	x5, [x2, #:got_lo12:g_maxRetryCount]
	strb	w7, [x5]
	sub	w5, w0, #1
	uxtb	w5, w5
	cmp	w5, 5
	bhi	.L1225
	adrp	x1, :got:HynixReadRetrial
	sub	w0, w0, #5
	uxtb	w0, w0
	ldr	x1, [x1, #:got_lo12:HynixReadRetrial]
	cmp	w0, 1
	str	x1, [x6]
	bhi	.L1224
	adrp	x0, :got:gNandcDumpWriteEn
	mov	w1, 1
	ldr	x0, [x0, #:got_lo12:gNandcDumpWriteEn]
	str	w1, [x0]
	b	.L1224
.L1225:
	sub	w5, w0, #17
	uxtb	w5, w5
	cmp	w5, 1
	bhi	.L1227
	adrp	x0, :got:MicronReadRetrial
	ldr	x0, [x0, #:got_lo12:MicronReadRetrial]
	b	.L1277
.L1227:
	sub	w5, w0, #65
	uxtb	w5, w5
	cmp	w5, 1
	bls	.L1245
	cmp	w0, 33
	bne	.L1228
.L1245:
	adrp	x0, :got:ToshibaReadRetrial
	ldr	x3, [x3, #:got_lo12:gpReadRetrial]
	ldr	x0, [x0, #:got_lo12:ToshibaReadRetrial]
	str	x0, [x3]
	mov	w0, 4
	ldr	x1, [x1, #:got_lo12:g_maxRegNum]
	strb	w0, [x1]
	mov	w0, 7
	ldr	x2, [x2, #:got_lo12:g_maxRetryCount]
	strb	w0, [x2]
	b	.L1224
.L1228:
	sub	w5, w0, #67
	uxtb	w5, w5
	cmp	w5, 1
	sub	w5, w0, #34
	cset	w7, ls
	uxtb	w5, w5
	cmp	w5, 1
	bls	.L1246
	cbz	w7, .L1230
.L1246:
	adrp	x5, :got:ToshibaReadRetrial
	ldr	x3, [x3, #:got_lo12:gpReadRetrial]
	cmp	w0, 35
	ldr	x5, [x5, #:got_lo12:ToshibaReadRetrial]
	str	x5, [x3]
	ldr	x2, [x2, #:got_lo12:g_maxRetryCount]
	beq	.L1232
	cmp	w0, 68
	beq	.L1232
	mov	w0, 7
	b	.L1276
.L1232:
	mov	w0, 17
.L1276:
	strb	w0, [x2]
	mov	w0, 4
	ldr	x1, [x1, #:got_lo12:g_maxRegNum]
	cbnz	w7, .L1278
	mov	w0, 5
.L1278:
	strb	w0, [x1]
	b	.L1224
.L1230:
	cmp	w0, 49
	bne	.L1224
	adrp	x0, :got:SamsungReadRetrial
	ldr	x0, [x0, #:got_lo12:SamsungReadRetrial]
.L1277:
	str	x0, [x6]
.L1224:
	ldrh	w2, [x4,10]
	ldrb	w1, [x4,12]
	ldrb	w0, [x4,18]
	sdiv	w1, w2, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x21, #:got_lo12:IDByte]
	ldrb	w0, [x0]
	cmp	w0, 44
	bne	.L1235
	ldr	x0, [x23, #:got_lo12:gFlashToggleModeEn]
	ldrb	w1, [x0]
	cbz	w1, .L1235
	strb	wzr, [x0]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L1235:
	ldr	x25, [x25, #:got_lo12:gFlashInterfaceMode]
	ldrb	w0, [x25]
	tst	w0, 6
	beq	.L1236
	ldr	x23, [x23, #:got_lo12:gFlashToggleModeEn]
	ldrb	w1, [x23]
	cbnz	w1, .L1237
	tbnz	x0, 0, .L1236
.L1237:
	adrp	x1, :got:gNandFlashInfoBlockAddr
	mov	w0, 0
	ldr	x1, [x1, #:got_lo12:gNandFlashInfoBlockAddr]
	ldr	w1, [x1]
	bl	FlashDdrParaScan
.L1236:
	ldr	x0, [x19, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	adrp	x0, :got:gNandIDataBuf
	ldr	x0, [x0, #:got_lo12:gNandIDataBuf]
	bl	FlashReadIdbDataRaw
	cbnz	w0, .L1238
	ldr	x0, [x29,104]
	ldr	x24, [x0, #:got_lo12:gNandIDBResBlkNumSaveInFlash]
	ldr	x0, [x20, #:got_lo12:gNandIDBResBlkNum]
	ldrb	w1, [x24]
	ldrb	w2, [x0]
	cmp	w2, w1
	bls	.L1239
	strb	w1, [x0]
.L1239:
	ldr	x0, [x20, #:got_lo12:gNandIDBResBlkNum]
	ldrb	w0, [x0]
	cmp	w0, 15
	bhi	.L1240
.L1238:
	ldr	x0, [x20, #:got_lo12:gNandIDBResBlkNum]
	mov	w1, 16
	strb	w1, [x0]
.L1240:
	mov	w0, 18928
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	bl	FlashPrintInfo
	ldr	x19, [x19, #:got_lo12:gpNandParaInfo]
	adrp	x0, :got:gNandPhyInfo
	ldr	x4, [x19]
	ldr	x0, [x0, #:got_lo12:gNandPhyInfo]
	ldrb	w1, [x4,12]
	strh	w1, [x0,8]
	ldrh	w3, [x4,14]
	ldrb	w1, [x4,7]
	str	w1, [x0,4]
	ldr	x21, [x21, #:got_lo12:IDByte]
	ldr	w1, [x21]
	str	w1, [x0]
	adrp	x1, :got:gNandMaxDie
	ldr	x1, [x1, #:got_lo12:gNandMaxDie]
	ldrb	w1, [x1]
	strh	w1, [x0,10]
	ldrb	w1, [x4,13]
	strh	w1, [x0,12]
	strh	w3, [x0,14]
	ldrh	w1, [x4,10]
	strh	w1, [x0,16]
	ldrb	w2, [x4,12]
	ldrh	w1, [x4,10]
	sdiv	w1, w1, w2
	strh	w1, [x0,18]
	ldrb	w2, [x4,9]
	strh	w2, [x0,20]
	ldrh	w5, [x4,10]
	ldrb	w1, [x4,9]
	mul	w1, w1, w5
	mov	w5, 512
	strh	w5, [x0,24]
	uxth	w1, w1
	strh	w1, [x0,22]
	ldr	x20, [x20, #:got_lo12:gNandIDBResBlkNum]
	ldrb	w5, [x20]
	strh	w5, [x0,26]
	ldr	x22, [x22, #:got_lo12:g_slc2KBNand]
	ldrb	w5, [x22]
	cmp	w5, 1
	bne	.L1241
	lsl	w1, w1, 1
	mov	w5, 16
	lsr	w3, w3, 1
	lsl	w2, w2, 1
	strh	w1, [x0,22]
	mov	w1, 8
	strb	w5, [x20]
	strh	w3, [x0,14]
	strh	w2, [x0,20]
	strh	w1, [x0,26]
.L1241:
	ldrb	w0, [x4,20]
	bl	FlashBchSel
	bl	FlashSuspend
	mov	w0, 0
	b	.L1206
.L1243:
	mov	w0, -2
.L1206:
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
	adrp	x3, :got:gpNandParaInfo
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	ldr	x0, [x3, #:got_lo12:gpNandParaInfo]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	uxtb	w19, w2
	mov	x20, x3
	ldr	x0, [x0]
	ldrb	w2, [x0,19]
	cmp	w2, 35
	cset	w4, eq
	cmp	w2, 68
	cset	w0, eq
	orr	w4, w4, w0
	cbnz	w4, .L1284
	sub	w2, w2, #5
	uxtb	w2, w2
	cmp	w2, 1
	bhi	.L1279
.L1284:
	mov	w21, w1
	adrp	x24, :got:mlcPageToSlcPageTbl
	mov	w25, 65535
	adrp	x26, :got:gFlashPageBuffer1
.L1282:
	ldr	x0, [x20, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L1279
	ldr	x0, [x24, #:got_lo12:mlcPageToSlcPageTbl]
	ldrh	w0, [x0,w19,sxtw 1]
	cmp	w0, w25
	bne	.L1279
	ldr	x23, [x26, #:got_lo12:gFlashPageBuffer1]
	mov	w1, 255
	mov	w2, 32768
	ldr	x0, [x23]
	bl	ftl_memset
	ldr	x2, [x23]
	add	w1, w19, w21
	mov	w0, w22
	mov	x3, 0
	add	w19, w19, 1
	bl	FlashProgPage
	uxtb	w19, w19
	b	.L1282
.L1279:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashProgPageDp
	.type	FlashProgPageDp, %function
FlashProgPageDp:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w19, w0
	adrp	x0, :got:gNandParaInfo
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	ldr	x0, [x0, #:got_lo12:gNandParaInfo]
	mov	w20, w1
	str	x5, [x29,72]
	mov	x24, x2
	mov	x21, x3
	mov	x23, x4
	ldrb	w22, [x0,9]
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	mov	w0, w19
	mov	w1, w20
	bl	FlashProgFirstCmd
	mov	w2, w22
	mov	x3, x24
	mov	x4, x21
	mov	w0, w19
	mov	w1, 1
	adrp	x21, :got:gBlockPageAlignSize
	bl	NandcXferData
	mov	w1, w20
	mov	w0, w19
	bl	FlashProgDpFirstCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	x21, [x21, #:got_lo12:gBlockPageAlignSize]
	mov	w0, w19
	ldr	w1, [x21]
	add	w1, w20, w1
	bl	FlashProgDpSecondCmd
	ldr	x5, [x29,72]
	mov	w2, w22
	mov	x3, x23
	mov	w0, w19
	mov	x4, x5
	mov	w1, 1
	bl	NandcXferData
	ldr	w1, [x21]
	mov	w0, w19
	add	w1, w20, w1
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
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashProgPageDp, .-FlashProgPageDp
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	mov	x24, x0
	adrp	x0, :got:gNandParaInfo
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	ldr	x0, [x0, #:got_lo12:gNandParaInfo]
	adrp	x26, :got:gNandRandomizer
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w23, 0
	mov	w27, w1
	ldrb	w0, [x0,9]
	mov	w22, w23
	str	w0, [x29,140]
	ldr	x0, [x26, #:got_lo12:gNandRandomizer]
	str	w2, [x29,132]
	ldrb	w0, [x0]
	str	w0, [x29,136]
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	str	x0, [x29,112]
	adrp	x0, .LC94
	add	x0, x0, :lo12:.LC94
	str	x0, [x29,104]
	adrp	x0, .LC92
	add	x0, x0, :lo12:.LC92
	str	x0, [x29,96]
.L1290:
	cmp	w22, w27
	bcs	.L1358
	mov	w25, 56
	ldr	w1, [x29,132]
	sub	w4, w27, w22
	add	x2, x29, 156
	umull	x25, w22, w25
	add	x3, x29, 152
	add	x21, x24, x25
	mov	x0, x21
	bl	LogAddr2PhyAddr
	mov	w19, w0
	adrp	x0, :got:gNandMaxDie
	ldr	w1, [x29,152]
	ldr	x0, [x0, #:got_lo12:gNandMaxDie]
	ldrb	w0, [x0]
	cmp	w1, w0
	bcc	.L1291
	mov	w0, -1
	str	w0, [x24,x25]
	b	.L1292
.L1291:
	adrp	x0, :got:DieCsIndex
	ldr	x0, [x0, #:got_lo12:DieCsIndex]
	ldrb	w20, [x0,w1,uxtw]
	adrp	x0, :got:gMultiPageReadEn
	ldr	x0, [x0, #:got_lo12:gMultiPageReadEn]
	ldrb	w0, [x0]
	cmp	w0, wzr
	mov	w0, w20
	csel	w19, w19, wzr, ne
	bl	NandcWaitFlashReady
	adrp	x1, :got:gpNandParaInfo
	str	x1, [x29,120]
	ldr	x0, [x1, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 5
	bhi	.L1294
	adrp	x2, :got:gReadRetryInfo
	sxtw	x1, w20
	ldr	x2, [x2, #:got_lo12:gReadRetryInfo]
	add	x0, x2, x1
	ldrb	w3, [x0,12]
	adrp	x0, :got:read_retry_cur_offset
	ldr	x0, [x0, #:got_lo12:read_retry_cur_offset]
	ldrb	w0, [x0,x1]
	cmp	w0, w3
	beq	.L1294
	ldrb	w1, [x2,1]
	mov	w0, w20
	add	x2, x2, 4
	bl	HynixSetRRPara
.L1294:
	mov	w0, w20
	bl	NandcFlashCs
.L1295:
	cmp	w20, 255
	ldr	w1, [x29,156]
	bne	.L1327
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1323
.L1327:
	cbz	w19, .L1298
	adrp	x0, :got:gBlockPageAlignSize
	ldr	x0, [x0, #:got_lo12:gBlockPageAlignSize]
	ldr	w2, [x0]
	mov	w0, w20
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1299
.L1298:
	mov	w0, w20
	bl	FlashReadCmd
	b	.L1299
.L1323:
	mov	w19, w0
.L1296:
	ldrb	w2, [x29,140]
	mov	w1, 0
	ldr	x3, [x21,8]
	mov	w0, w20
	ldr	x4, [x21,16]
	bl	NandcXferData
	mov	w28, w0
	mov	w0, 0
	bl	NandcReadDontCaseBusyEn
	cmn	w28, #1
	bne	.L1300
	ldr	x0, [x26, #:got_lo12:gNandRandomizer]
	ldrb	w1, [x0]
	cbz	w1, .L1300
	strb	wzr, [x0]
	mov	w19, 0
	b	.L1295
.L1300:
	cbz	w19, .L1301
	adrp	x0, :got:gBlockPageAlignSize
	ldr	w1, [x29,156]
	ldr	x0, [x0, #:got_lo12:gBlockPageAlignSize]
	ldr	w2, [x0]
	mov	w0, w20
	add	w1, w2, w1
	bl	FlashReadDpDataOutCmd
	add	w0, w22, 1
	mov	w4, 56
	ldrb	w2, [x29,140]
	mov	w1, 0
	umull	x4, w0, w4
	mov	w0, w20
	add	x4, x24, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w23, w0
	csel	w19, w19, wzr, ne
.L1301:
	mov	w0, w20
	bl	NandcFlashDeCs
	ldr	x0, [x26, #:got_lo12:gNandRandomizer]
	cmn	w28, #1
	ldrb	w1, [x29,136]
	strb	w1, [x0]
	bne	.L1308
	adrp	x0, :got:gFlashToggleModeEn
	ldr	x0, [x0, #:got_lo12:gFlashToggleModeEn]
	ldrb	w0, [x0]
	cbnz	w0, .L1303
.L1307:
	adrp	x0, :got:gpReadRetrial
	ldr	x0, [x0, #:got_lo12:gpReadRetrial]
	ldr	x19, [x0]
	cbnz	x19, .L1304
	b	.L1359
.L1303:
	adrp	x0, :got:gpNandc
	ldr	w1, [x29,156]
	ldr	x2, [x21,8]
	mov	w4, 1
	ldr	x3, [x21,16]
	ldr	x0, [x0, #:got_lo12:gpNandc]
	ldr	x0, [x0]
	ldr	w19, [x0,304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w28, w0
	beq	.L1306
	adrp	x1, :got:gNandFlashEccBits
	ldr	x1, [x1, #:got_lo12:gNandFlashEccBits]
	ldrb	w0, [x1]
	cmp	w28, w0, lsr 1
	bls	.L1325
.L1306:
	lsr	w0, w19, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1307
	b	.L1325
.L1304:
	ldr	w1, [x29,156]
	mov	w0, w20
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x19
	cmn	w0, #1
	mov	w28, w0
	mov	w19, 0
	bne	.L1309
	ldr	x0, [x29,120]
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 5
	bhi	.L1310
	adrp	x2, :got:gReadRetryInfo
	mov	w0, w20
	mov	w3, w19
	ldr	x2, [x2, #:got_lo12:gReadRetryInfo]
	add	x2, x2, 4
	ldrb	w1, [x2,-3]
	bl	HynixSetRRPara
.L1310:
	ldr	w1, [x29,156]
	mov	w0, w20
	ldr	x2, [x21,8]
	mov	w19, 0
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	adrp	x2, :got:gNandFlashEccBits
	ldr	x0, [x29,96]
	ldr	w1, [x21,4]
	mov	w3, w28
	ldr	x2, [x2, #:got_lo12:gNandFlashEccBits]
	ldrb	w2, [x2]
	bl	printk
	b	.L1309
.L1359:
	ldr	w1, [x29,156]
	mov	w0, w20
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1309
.L1325:
	mov	w19, 0
.L1308:
	adrp	x0, :got:gNandFlashEccBits
	ldr	x0, [x0, #:got_lo12:gNandFlashEccBits]
	ldrb	w0, [x0]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1309
	adrp	x0, :got:gpReadRetrial
	ldr	x0, [x0, #:got_lo12:gpReadRetrial]
	ldr	x0, [x0]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1309:
	cmp	w28, 256
	beq	.L1328
	cmn	w28, #1
	bne	.L1311
.L1328:
	str	w28, [x24,x25]
	b	.L1313
.L1311:
	str	wzr, [x24,x25]
.L1313:
	ldr	w3, [x24,x25]
	cmn	w3, #1
	bne	.L1315
	adrp	x2, :got:gNandFlashEccBits
	ldr	w1, [x21,4]
	ldr	x0, [x29,112]
	ldr	x2, [x2, #:got_lo12:gNandFlashEccBits]
	ldrb	w2, [x2]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L1315
	mov	w2, 4
	ldr	x0, [x29,104]
	mov	w3, w2
	bl	rknand_print_hex
.L1315:
	cbz	w19, .L1317
	adrp	x0, :got:gNandFlashEccBits
	ldr	x0, [x0, #:got_lo12:gNandFlashEccBits]
	ldrb	w0, [x0]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L1318
	adrp	x0, :got:gpReadRetrial
	ldr	x0, [x0, #:got_lo12:gpReadRetrial]
	ldr	x0, [x0]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L1318:
	add	w0, w22, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L1329
	cmn	w23, #1
	bne	.L1319
.L1329:
	str	w23, [x24,x0]
	b	.L1317
.L1319:
	str	wzr, [x24,x0]
.L1317:
	add	w22, w22, w19
.L1292:
	add	w22, w22, 1
	b	.L1290
.L1299:
	mov	w0, w20
	bl	NandcWaitFlashReady
	cbz	w19, .L1296
	ldr	w1, [x29,156]
	mov	w0, w20
	bl	FlashReadDpDataOutCmd
	b	.L1296
.L1358:
	mov	w0, 0
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
	adrp	x0, :got:gNandPhyInfo
	mov	w2, 16
	stp	x29, x30, [sp, -192]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:gNandPhyInfo]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x23, :got:FbbtBlk
	adrp	x24, :got:gFlashSpareBuffer
	ldrh	w1, [x0,12]
	mov	w27, -1
	ldrh	w21, [x0,14]
	adrp	x26, .LC95
	ldr	x0, [x23, #:got_lo12:FbbtBlk]
	add	x28, x29, 136
	add	x26, x26, :lo12:.LC95
	mul	w21, w1, w21
	mov	w1, 0
	bl	ftl_memset
	uxth	w21, w21
	ldr	x0, [x24, #:got_lo12:gFlashSpareBuffer]
	add	w25, w21, w27
	mov	w3, 0
	str	xzr, [x29,144]
	mov	w19, w3
	uxth	w25, w25
	ldr	x0, [x0]
	str	x0, [x29,152]
	sub	w0, w21, #16
	str	w0, [x29,124]
.L1361:
	adrp	x22, :got:gNandMaxDie
	ldr	x1, [x22, #:got_lo12:gNandMaxDie]
	ldrb	w1, [x1]
	cmp	w1, w19
	bls	.L1371
	mul	w4, w19, w21
	mov	w20, w25
	mov	w5, 61664
.L1362:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1364
	add	w1, w4, w20
	mov	x0, x28
	lsl	w1, w1, 10
	str	w1, [x28,4]
	mov	w1, 1
	str	x5, [x29,96]
	mov	w2, w1
	str	x3, [x29,104]
	str	x4, [x29,112]
	bl	FlashReadPages
	ldr	w0, [x28]
	ldr	x4, [x29,112]
	cmn	w0, #1
	ldr	x3, [x29,104]
	ldr	x5, [x29,96]
	beq	.L1363
	ldr	x0, [x24, #:got_lo12:gFlashSpareBuffer]
	ldr	x0, [x0]
	ldrh	w0, [x0]
	cmp	w0, w5
	bne	.L1363
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	str	x3, [x29,112]
	bl	printk
	ldr	x3, [x29,112]
	ldr	x0, [x23, #:got_lo12:FbbtBlk]
	add	w3, w3, 1
	strh	w20, [x0,w19,sxtw 1]
	uxth	w3, w3
	b	.L1364
.L1363:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1362
.L1364:
	ldr	x22, [x22, #:got_lo12:gNandMaxDie]
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x22]
	cmp	w1, w3
	csel	w27, w27, wzr, ne
	b	.L1361
.L1371:
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
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	adrp	x1, :got:p_sys_data_buf
	adrp	x0, :got:req_sys
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:p_sys_data_buf]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	x2, [x0, #:got_lo12:req_sys]
	adrp	x20, :got:gBbtInfo
	ldr	x1, [x1]
	mov	w21, 0
	mov	x22, x0
	adrp	x25, :got:c_ftl_nand_die_num
	str	x1, [x2,8]
	adrp	x1, :got:p_sys_spare_buf
	mov	w26, -1
	adrp	x27, :got:c_ftl_nand_blks_per_die
	mov	w28, 61664
	ldr	x1, [x1, #:got_lo12:p_sys_spare_buf]
	ldr	x24, [x1]
	str	x24, [x2,16]
	ldr	x20, [x20, #:got_lo12:gBbtInfo]
.L1373:
	ldr	x0, [x25, #:got_lo12:c_ftl_nand_die_num]
	ldrh	w0, [x0]
	cmp	w21, w0
	bcs	.L1382
	strh	w26, [x20,12]
	adrp	x3, :got:c_ftl_nand_blks_per_die
	ldr	x0, [x27, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w19, [x0]
	sub	w19, w19, #1
	uxth	w19, w19
.L1374:
	ldr	x0, [x3, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w1, [x0]
	sub	w0, w1, #16
	cmp	w19, w0
	ble	.L1376
	ldr	x23, [x22, #:got_lo12:req_sys]
	madd	w1, w21, w1, w19
	mov	x0, x23
	str	x3, [x29,104]
	lsl	w1, w1, 10
	str	w1, [x23,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x23]
	ldr	x3, [x29,104]
	cmn	w0, #1
	beq	.L1375
	ldrh	w0, [x24]
	cmp	w0, w28
	bne	.L1375
	strh	w19, [x20,12]
	b	.L1376
.L1375:
	sub	w19, w19, #1
	uxth	w19, w19
	b	.L1374
.L1376:
	add	w21, w21, 1
	add	x20, x20, 2
	b	.L1373
.L1382:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	stp	x29, x30, [sp, -208]!
	cmp	w1, 1
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w24, w1
	bne	.L1384
	adrp	x1, :got:c_ftl_nand_page_pre_slc_blk
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	b	.L1394
.L1384:
	adrp	x1, :got:c_ftl_nand_page_pre_blk
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_page_pre_blk]
.L1394:
	ldrh	w19, [x1]
	lsl	w21, w0, 10
	add	x22, x29, 88
	mov	w1, 1
	sub	w19, w19, #1
	mov	w2, w24
	str	xzr, [x29,96]
	add	x25, x29, 144
	sxth	w19, w19
	str	x25, [x29,104]
	orr	w0, w19, w21
	str	w0, [x29,92]
	mov	x0, x22
	bl	FlashReadPages
	ldr	w0, [x29,144]
	cmn	w0, #1
	bne	.L1386
	mov	w23, 0
	mov	w26, 2
.L1387:
	cmp	w23, w19
	bgt	.L1386
	add	w3, w23, w19
	mov	w1, 1
	mov	w2, w24
	sdiv	w20, w3, w26
	sxth	w0, w20
	orr	w0, w0, w21
	str	w0, [x22,4]
	mov	x0, x22
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	bne	.L1388
	ldr	w0, [x25,4]
	cmn	w0, #1
	bne	.L1388
	ldr	w0, [x22]
	cmn	w0, #1
	beq	.L1388
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1387
.L1388:
	add	w20, w20, 1
	sxth	w23, w20
	b	.L1387
.L1386:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 208
	ret
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	stp	x29, x30, [sp, -64]!
	adrp	x0, :got:p_sys_spare_buf
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, :got:req_sys
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	ldr	x1, [x21, #:got_lo12:req_sys]
	adrp	x23, :got:c_ftl_nand_blks_per_die
	mov	w24, 61649
	str	xzr, [x1,8]
	ldr	x0, [x0, #:got_lo12:p_sys_spare_buf]
	ldr	x20, [x0]
	str	x20, [x1,16]
	bl	FtlBbtMemInit
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w19, [x0]
	sub	w19, w19, #1
	uxth	w19, w19
.L1396:
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w0, [x0]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1399
	ldr	x22, [x21, #:got_lo12:req_sys]
	lsl	w0, w19, 10
	mov	w1, 1
	mov	w2, w1
	str	w0, [x22,4]
	mov	x0, x22
	bl	FlashReadPages
	ldr	w0, [x22]
	cmn	w0, #1
	bne	.L1397
	ldr	w0, [x22,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x22,4]
	mov	x0, x22
	bl	FlashReadPages
.L1397:
	ldr	x0, [x21, #:got_lo12:req_sys]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1398
	ldrh	w0, [x20]
	cmp	w0, w24
	bne	.L1398
	adrp	x0, :got:gBbtInfo
	ldr	w1, [x20,4]
	ldr	x0, [x0, #:got_lo12:gBbtInfo]
	str	w1, [x0,8]
	strh	w19, [x0]
	ldrh	w1, [x20,8]
	strh	w1, [x0,4]
	b	.L1399
.L1398:
	sub	w19, w19, #1
	uxth	w19, w19
	b	.L1396
.L1399:
	adrp	x19, :got:gBbtInfo
	mov	w2, 65535
	mov	w0, -1
	ldr	x22, [x19, #:got_lo12:gBbtInfo]
	ldrh	w1, [x22]
	cmp	w1, w2
	beq	.L1401
	ldrh	w1, [x22,4]
	cmp	w1, w2
	beq	.L1403
	ldr	x23, [x21, #:got_lo12:req_sys]
	lsl	w1, w1, 10
	mov	x0, x23
	str	w1, [x23,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1403
	ldrh	w1, [x20]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1403
	ldr	w0, [x20,4]
	ldr	w1, [x22,8]
	cmp	w0, w1
	bls	.L1403
	str	w0, [x22,8]
	ldrh	w1, [x22,4]
	ldrh	w0, [x20,8]
	strh	w1, [x22]
	strh	w0, [x22,4]
.L1403:
	ldr	x23, [x19, #:got_lo12:gBbtInfo]
	mov	w1, 1
	adrp	x24, :got:p_sys_data_buf
	ldrh	w0, [x23]
	bl	FtlGetLastWrittenPage
	sxth	w22, w0
	add	w0, w0, 1
	strh	w0, [x23,2]
.L1405:
	tbnz	w22, #31, .L1410
	ldr	x0, [x19, #:got_lo12:gBbtInfo]
	mov	w1, 1
	ldr	x23, [x21, #:got_lo12:req_sys]
	mov	w2, w1
	ldrh	w0, [x0]
	orr	w0, w22, w0, lsl 10
	str	w0, [x23,4]
	ldr	x0, [x24, #:got_lo12:p_sys_data_buf]
	ldr	x0, [x0]
	str	x0, [x23,8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1406
.L1410:
	ldr	x0, [x19, #:got_lo12:gBbtInfo]
	ldrh	w1, [x20,10]
	strh	w1, [x0,6]
	mov	w1, 65535
	ldrh	w0, [x20,12]
	cmp	w0, w1
	bne	.L1407
	b	.L1408
.L1406:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1405
.L1407:
	adrp	x1, :got:c_ftl_nand_sys_blks_per_plane
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_sys_blks_per_plane]
	ldr	w2, [x1]
	cmp	w0, w2
	beq	.L1408
	adrp	x1, :got:c_ftl_nand_blk_pre_plane
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_blk_pre_plane]
	ldrh	w1, [x1]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1408
	cmp	w2, w1
	bcs	.L1408
	bl	FtlSysBlkNumInit
.L1408:
	mov	x20, 0
	adrp	x22, :got:c_ftl_nand_die_num
	adrp	x23, :got:c_ftl_nand_bbm_buf_size
.L1411:
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_die_num]
	mov	w1, w20
	add	x20, x20, 1
	ldrh	w0, [x0]
	cmp	w1, w0
	bcs	.L1426
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_bbm_buf_size]
	ldr	x3, [x21, #:got_lo12:req_sys]
	ldrh	w2, [x0]
	ldr	x0, [x19, #:got_lo12:gBbtInfo]
	ldr	x3, [x3,8]
	add	x0, x0, x20, lsl 3
	mul	w1, w1, w2
	lsl	w2, w2, 2
	ldr	x0, [x0,24]
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1411
.L1426:
	mov	w0, 0
.L1401:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -48]!
	uxth	x1, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	w21, w0
	adrp	x0, :got:p_map_region_ppn_table
	stp	x19, x20, [sp,16]
	ubfiz	x2, x21, 2, 16
	adrp	x20, :got:p_l2p_ram_map
	ldr	x0, [x0, #:got_lo12:p_map_region_ppn_table]
	ldr	x0, [x0]
	ldr	w22, [x0,x2]
	cbnz	w22, .L1428
	ldr	x20, [x20, #:got_lo12:p_l2p_ram_map]
	adrp	x2, :got:c_ftl_nand_byte_pre_page
	lsl	x19, x1, 4
	mov	w1, 255
	ldr	x0, [x20]
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_byte_pre_page]
	add	x0, x0, x19
	ldrh	w2, [x2]
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x20]
	strh	w21, [x0,x19]
	ldr	x1, [x20]
	add	x19, x1, x19
	str	w22, [x19,4]
	b	.L1429
.L1428:
	adrp	x0, :got:req_sys
	lsl	x19, x1, 4
	ldr	x0, [x0, #:got_lo12:req_sys]
	str	w22, [x0,4]
	ldr	x20, [x20, #:got_lo12:p_l2p_ram_map]
	ldr	x1, [x20]
	add	x1, x1, x19
	ldr	x1, [x1,8]
	str	x1, [x0,8]
	adrp	x1, :got:p_sys_spare_buf
	ldr	x1, [x1, #:got_lo12:p_sys_spare_buf]
	ldr	x1, [x1]
	str	x1, [x0,16]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	x0, [x20]
	add	x0, x0, x19
	str	wzr, [x0,4]
	ldr	x0, [x20]
	strh	w21, [x0,x19]
.L1429:
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
	stp	x21, x22, [sp,32]
	mov	w22, w0
	adrp	x0, :got:c_ftl_nand_sec_pre_page_shift
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sec_pre_page_shift]
	mov	w21, w1
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	add	w1, w22, w1
	mov	x23, x2
	ldrh	w3, [x0]
	adrp	x0, :got:c_ftl_vendor_part_size
	mov	w20, -1
	ldr	x0, [x0, #:got_lo12:c_ftl_vendor_part_size]
	ldrh	w0, [x0]
	cmp	w1, w0
	bhi	.L1431
	lsr	w24, w22, w3
	mov	w20, 0
	add	x25, x29, 120
	adrp	x26, :got:p_vendor_data_buf
.L1432:
	cbz	w21, .L1431
	adrp	x0, :got:p_vendor_region_ppn_table
	ldr	x0, [x0, #:got_lo12:p_vendor_region_ppn_table]
	ldr	x0, [x0]
	ldr	w1, [x0,w24,uxtw 2]
	adrp	x0, :got:c_ftl_nand_sec_pre_page
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w19, [x0]
	uxth	w0, w21
	udiv	w27, w22, w19
	msub	w27, w27, w19, w22
	sub	w19, w19, w27
	uxth	w19, w19
	cmp	w19, w21
	csel	w19, w0, w19, hi
	lsl	w28, w19, 9
	cbz	w1, .L1434
	ldr	x5, [x26, #:got_lo12:p_vendor_data_buf]
	ubfiz	x27, x27, 9, 16
	str	w1, [x25,4]
	mov	w1, 1
	mov	w2, w1
	str	x5, [x29,104]
	ldr	x0, [x5]
	str	x0, [x25,8]
	mov	x0, x25
	str	xzr, [x25,16]
	bl	FlashReadPages
	ldr	x5, [x29,104]
	mov	w2, w28
	ldr	w0, [x25]
	ldr	x1, [x5]
	cmn	w0, #1
	mov	x0, x23
	csinv	w20, w20, wzr, ne
	add	x1, x1, x27
	bl	ftl_memcpy
	b	.L1436
.L1434:
	mov	x0, x23
	mov	w2, w28
	bl	ftl_memset
.L1436:
	add	w24, w24, 1
	sub	w21, w21, w19
	add	w22, w22, w19
	add	x23, x23, x28, sxtw
	b	.L1432
.L1431:
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
	stp	x19, x20, [sp,16]
	adrp	x20, :got:g_ect_tbl_info_size
	adrp	x19, :got:gp_ect_tbl_info
	ldr	x20, [x20, #:got_lo12:g_ect_tbl_info_size]
	ldr	x19, [x19, #:got_lo12:gp_ect_tbl_info]
	ldrh	w1, [x20]
	ldr	x2, [x19]
	bl	FtlVendorPartRead
	ldr	x0, [x19]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1440
	adrp	x0, .LC72
	adrp	x1, .LC96
	add	x1, x1, :lo12:.LC96
	add	x0, x0, :lo12:.LC72
	bl	printk
	ldrh	w2, [x20]
	mov	w1, 0
	ldr	x0, [x19]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1440:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	stp	x29, x30, [sp, -32]!
	mov	w1, 1
	mov	w0, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:g_sys_ext_data
	ldr	x20, [x19, #:got_lo12:g_sys_ext_data]
	mov	x2, x20
	bl	FtlVendorPartRead
	ldr	w1, [x20]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L1442
	mov	x0, x20
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20]
.L1442:
	ldr	x0, [x19, #:got_lo12:g_sys_ext_data]
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	adrp	x20, :got:g_totle_slc_erase_count
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1443
	adrp	x1, :got:g_totle_write_sector
	ldr	w2, [x0,88]
	ldr	x1, [x1, #:got_lo12:g_totle_write_sector]
	str	w2, [x1]
	adrp	x1, :got:g_totle_read_sector
	ldr	w2, [x0,92]
	ldr	x1, [x1, #:got_lo12:g_totle_read_sector]
	str	w2, [x1]
	adrp	x1, :got:g_totle_gc_page_count
	ldr	w2, [x0,8]
	ldr	x1, [x1, #:got_lo12:g_totle_gc_page_count]
	str	w2, [x1]
	adrp	x1, :got:g_totle_write_page_count
	ldr	w2, [x0,12]
	ldr	x1, [x1, #:got_lo12:g_totle_write_page_count]
	str	w2, [x1]
	adrp	x1, :got:g_totle_read_page_count
	ldr	w2, [x0,16]
	ldr	x1, [x1, #:got_lo12:g_totle_read_page_count]
	str	w2, [x1]
	adrp	x1, :got:g_totle_l2p_write_count
	ldr	w2, [x0,20]
	ldr	x1, [x1, #:got_lo12:g_totle_l2p_write_count]
	str	w2, [x1]
	ldr	w2, [x0,28]
	ldr	x1, [x20, #:got_lo12:g_totle_slc_erase_count]
	str	w2, [x1]
	adrp	x1, :got:g_totle_sys_slc_erase_count
	ldr	w2, [x0,32]
	ldr	x1, [x1, #:got_lo12:g_totle_sys_slc_erase_count]
	str	w2, [x1]
	adrp	x1, :got:g_totle_discard_page_count
	ldr	w2, [x0,36]
	ldr	x1, [x1, #:got_lo12:g_totle_discard_page_count]
	str	w2, [x1]
	adrp	x1, :got:g_totle_cache_write_count
	ldr	w2, [x0,40]
	ldr	x1, [x1, #:got_lo12:g_totle_cache_write_count]
	str	w2, [x1]
	adrp	x1, :got:g_max_erase_count
	ldr	w2, [x0,44]
	ldr	x1, [x1, #:got_lo12:g_max_erase_count]
	str	w2, [x1]
	adrp	x1, :got:g_min_erase_count
	ldr	x1, [x1, #:got_lo12:g_min_erase_count]
	ldr	w2, [x0,48]
	ldr	w0, [x0,60]
	str	w2, [x1]
	adrp	x1, :got:g_all_blk_used_slc_mode
	ldr	x1, [x1, #:got_lo12:g_all_blk_used_slc_mode]
	str	w0, [x1]
.L1443:
	adrp	x0, :got:g_SlcPartLbaEndSector
	ldr	x0, [x0, #:got_lo12:g_SlcPartLbaEndSector]
	str	wzr, [x0]
	mov	w0, 34661
	movk	w0, 0x1234, lsl 16
	ldr	x19, [x19, #:got_lo12:g_sys_ext_data]
	ldr	w1, [x19,68]
	cmp	w1, w0
	bne	.L1444
	adrp	x0, :got:g_inkDie_check_enable
	mov	w1, 1
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	str	w1, [x0]
	adrp	x0, .LC72
	adrp	x1, .LC97
	add	x0, x0, :lo12:.LC72
	add	x1, x1, :lo12:.LC97
	bl	printk
.L1444:
	adrp	x0, :got:c_mlc_erase_count_value
	ldr	x2, [x20, #:got_lo12:g_totle_slc_erase_count]
	adrp	x3, :got:g_totle_avg_erase_count
	ldr	x0, [x0, #:got_lo12:c_mlc_erase_count_value]
	ldr	x3, [x3, #:got_lo12:g_totle_avg_erase_count]
	ldrh	w1, [x0]
	adrp	x0, :got:g_totle_mlc_erase_count
	ldr	x0, [x0, #:got_lo12:g_totle_mlc_erase_count]
	ldr	w4, [x0]
	ldr	w0, [x2]
	madd	w0, w1, w4, w0
	adrp	x1, :got:c_ftl_nand_data_blks_per_plane
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w1, [x1]
	udiv	w0, w0, w1
	str	w0, [x3]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	stp	x29, x30, [sp, -160]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:g_totle_map_block
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x0, [x19, #:got_lo12:g_totle_map_block]
	adrp	x24, :got:p_map_block_ver_table
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	adrp	x28, :got:g_totle_vendor_block
	strh	wzr, [x0]
	adrp	x25, :got:c_ftl_nand_max_map_blks
	adrp	x21, :got:p_map_block_table
	adrp	x27, :got:c_ftl_nand_max_vendor_blks
	adrp	x23, :got:p_vendor_block_ver_table
	ldr	x0, [x28, #:got_lo12:g_totle_vendor_block]
	strh	wzr, [x0]
	ldr	x20, [x25, #:got_lo12:c_ftl_nand_max_map_blks]
	ldr	x0, [x24, #:got_lo12:p_map_block_ver_table]
	ldr	w2, [x20]
	ldr	x0, [x0]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x21, #:got_lo12:p_map_block_table]
	mov	w1, 0
	ldr	w2, [x20]
	adrp	x20, :got:p_vendor_block_table
	ldr	x0, [x0]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x22, [x27, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldr	x0, [x23, #:got_lo12:p_vendor_block_ver_table]
	mov	w1, 0
	ldrh	w2, [x22]
	ldr	x0, [x0]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x20, #:got_lo12:p_vendor_block_table]
	mov	w1, 0
	ldrh	w2, [x22]
	adrp	x22, :got:gSysInfo
	ldr	x0, [x0]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x22, #:got_lo12:gSysInfo]
	mov	w1, 255
	mov	w2, 12
	bl	ftl_memset
	str	x28, [x29,144]
	adrp	x0, :got:c_ftl_nand_data_blks_per_plane
	str	x25, [x29,136]
	str	x27, [x29,128]
	str	x22, [x29,120]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w0, [x0]
	str	w0, [x29,156]
.L1446:
	adrp	x0, :got:c_ftl_nand_blk_pre_plane
	ldr	w1, [x29,156]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_blk_pre_plane]
	ldrh	w0, [x0]
	cmp	w0, w1
	bls	.L1522
	mov	w22, 0
	adrp	x25, :got:c_ftl_nand_byte_pre_oob
	mov	w27, w22
	mov	w26, 4
.L1489:
	adrp	x0, :got:c_ftl_nand_planes_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w0, w27
	bls	.L1523
	adrp	x0, :got:p_plane_order_table
	ldrh	w1, [x29,156]
	ldr	x0, [x0, #:got_lo12:p_plane_order_table]
	ldrb	w0, [x0,w27,sxtw]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1447
	adrp	x1, :got:req_read
	mov	w0, 56
	lsl	w28, w28, 10
	umull	x0, w22, w0
	ldr	x2, [x1, #:got_lo12:req_read]
	ldr	x1, [x2]
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x2]
	adrp	x2, :got:p_gc_spare_buf
	add	x1, x1, x0
	str	xzr, [x1,8]
	ldr	x0, [x25, #:got_lo12:c_ftl_nand_byte_pre_oob]
	ldr	x2, [x2, #:got_lo12:p_gc_spare_buf]
	ldrh	w0, [x0]
	ldr	x2, [x2]
	mul	w0, w22, w0
	add	w22, w22, 1
	sdiv	w0, w0, w26
	uxth	w22, w22
	add	x0, x2, x0, sxtw 2
	str	x0, [x1,16]
.L1447:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1489
.L1523:
	adrp	x0, :got:req_read
	mov	w1, w22
	mov	w2, 1
	mov	x27, 0
	mov	w28, 65535
	ldr	x0, [x0, #:got_lo12:req_read]
	ldr	x0, [x0]
	bl	FlashReadPages
.L1449:
	cmp	w22, w27, uxth
	bls	.L1524
	mov	x0, 56
	mul	x5, x27, x0
	adrp	x0, :got:req_read
	ldr	x6, [x0, #:got_lo12:req_read]
	ldr	x2, [x6]
	add	x1, x2, x5
	ldr	w7, [x2,x5]
	ldr	w0, [x1,4]
	cmn	w7, #1
	ldr	x26, [x1,16]
	ubfx	x25, x0, 10, 16
	bne	.L1451
	add	w0, w0, 1
	str	w0, [x1,4]
	mov	w1, 1
	str	x7, [x29,96]
	ldr	x0, [x6]
	mov	w2, w1
	str	x6, [x29,104]
	add	x0, x0, x5
	str	x5, [x29,112]
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	x5, [x29,112]
	cmp	w0, w28
	ldr	x6, [x29,104]
	ldr	x7, [x29,96]
	bne	.L1451
	ldr	x0, [x6]
	str	w7, [x0,x5]
.L1451:
	adrp	x0, :got:req_read
	ldr	x0, [x0, #:got_lo12:req_read]
	ldr	x0, [x0]
	ldr	w0, [x0,x5]
	cmn	w0, #1
	beq	.L1453
	adrp	x0, :got:g_GlobalSysVersion
	ldr	x0, [x0, #:got_lo12:g_GlobalSysVersion]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1454
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1455
.L1454:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1455
	adrp	x1, :got:g_GlobalSysVersion
	add	w0, w0, 1
	ldr	x1, [x1, #:got_lo12:g_GlobalSysVersion]
	str	w0, [x1]
.L1455:
	ldrh	w0, [x26]
	mov	w1, 61604
	cmp	w0, w1
	beq	.L1457
	bhi	.L1458
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1456
	ldr	x0, [x29,128]
	ldr	x6, [x23, #:got_lo12:p_vendor_block_ver_table]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldr	x6, [x6]
	ldrh	w5, [x0]
	ldr	x0, [x29,144]
	sub	w1, w5, #1
	ldr	x0, [x0, #:got_lo12:g_totle_vendor_block]
	sxth	w2, w1
	ldrh	w0, [x0]
	sub	w1, w1, w0
	b	.L1474
.L1458:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1460
	cmp	w0, w28
	bne	.L1456
	mov	w0, w25
	mov	w1, 0
	b	.L1519
.L1460:
	ldr	x0, [x29,136]
	ldr	x6, [x24, #:got_lo12:p_map_block_ver_table]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_max_map_blks]
	ldr	x6, [x6]
	ldr	w5, [x0]
	ldr	x0, [x19, #:got_lo12:g_totle_map_block]
	uxth	w1, w5
	ldrh	w0, [x0]
	sub	w2, w1, #1
	sub	w1, w1, w0
	sxth	w2, w2
	sub	w1, w1, #1
	sxth	w1, w1
.L1462:
	cmp	w2, w1
	ble	.L1525
	sbfiz	x7, x2, 2, 32
	ldr	w10, [x26,4]
	sxth	x8, w2
	ldr	w9, [x6,x7]
	cmp	w10, w9
	bls	.L1463
	ldr	w1, [x6]
	cbnz	w1, .L1464
	cmp	w0, w5
	bne	.L1465
.L1464:
	ldr	x0, [x21, #:got_lo12:p_map_block_table]
	mov	w1, 1
	str	x8, [x29,96]
	str	x2, [x29,104]
	ldr	x0, [x0]
	str	x7, [x29,112]
	ldrh	w0, [x0]
	bl	FtlFreeSysBlkQueueIn
	ldr	x7, [x29,112]
	ldr	x2, [x29,104]
	ldr	x8, [x29,96]
	b	.L1466
.L1465:
	ldr	x1, [x19, #:got_lo12:g_totle_map_block]
	add	w0, w0, 1
	strh	w0, [x1]
.L1466:
	mov	w0, 0
.L1467:
	cmp	w0, w2
	beq	.L1526
	ldr	x1, [x24, #:got_lo12:p_map_block_ver_table]
	ldr	x6, [x1]
	sxtw	x1, w0
	lsl	x5, x1, 2
	lsl	x1, x1, 1
	add	x9, x6, x5
	add	w0, w0, 1
	sxth	w0, w0
	ldr	w9, [x9,4]
	str	w9, [x6,x5]
	ldr	x5, [x21, #:got_lo12:p_map_block_table]
	ldr	x5, [x5]
	add	x6, x5, x1
	ldrh	w6, [x6,2]
	strh	w6, [x5,x1]
	b	.L1467
.L1526:
	ldr	x0, [x24, #:got_lo12:p_map_block_ver_table]
	ldr	w1, [x26,4]
	ldr	x0, [x0]
	str	w1, [x0,x7]
	ldr	x0, [x21, #:got_lo12:p_map_block_table]
	ldr	x0, [x0]
	strh	w25, [x0,x8,lsl 1]
	tbz	w2, #31, .L1518
	b	.L1453
.L1463:
	sub	w2, w2, #1
	sxth	w2, w2
	b	.L1462
.L1525:
	tbz	w2, #31, .L1473
	b	.L1453
.L1518:
	ldr	x1, [x29,136]
	ldr	x0, [x19, #:got_lo12:g_totle_map_block]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_max_map_blks]
	ldrh	w0, [x0]
	ldr	w1, [x1]
	sub	w1, w1, w0
	sub	w1, w1, #1
	cmp	w2, w1, sxth
	bgt	.L1456
.L1473:
	ldr	x1, [x19, #:got_lo12:g_totle_map_block]
	add	w0, w0, 1
	sxtw	x2, w2
	strh	w0, [x1]
	ldr	w1, [x26,4]
	ldr	x0, [x24, #:got_lo12:p_map_block_ver_table]
	ldr	x0, [x0]
	str	w1, [x0,x2,lsl 2]
	ldr	x0, [x21, #:got_lo12:p_map_block_table]
	b	.L1520
.L1482:
	sbfiz	x7, x2, 2, 32
	ldr	w10, [x26,4]
	sxth	x8, w2
	ldr	w9, [x6,x7]
	cmp	w10, w9
	bhi	.L1527
	sub	w2, w2, #1
	sxth	w2, w2
.L1474:
	cmp	w2, w1
	bgt	.L1482
	b	.L1481
.L1527:
	ldr	w1, [x6]
	cbnz	w1, .L1476
	cmp	w0, w5
	bne	.L1477
.L1476:
	ldr	x0, [x20, #:got_lo12:p_vendor_block_table]
	mov	w1, 1
	str	x8, [x29,96]
	str	x7, [x29,104]
	ldr	x0, [x0]
	str	x2, [x29,112]
	ldrh	w0, [x0]
	bl	FtlFreeSysBlkQueueIn
	ldr	x2, [x29,112]
	ldr	x7, [x29,104]
	ldr	x8, [x29,96]
	b	.L1478
.L1477:
	ldr	x1, [x29,144]
	add	w0, w0, 1
	ldr	x1, [x1, #:got_lo12:g_totle_vendor_block]
	strh	w0, [x1]
.L1478:
	mov	w0, 0
.L1479:
	cmp	w0, w2
	beq	.L1528
	ldr	x1, [x23, #:got_lo12:p_vendor_block_ver_table]
	ldr	x6, [x1]
	sxtw	x1, w0
	lsl	x5, x1, 2
	lsl	x1, x1, 1
	add	x9, x6, x5
	add	w0, w0, 1
	sxth	w0, w0
	ldr	w9, [x9,4]
	str	w9, [x6,x5]
	ldr	x5, [x20, #:got_lo12:p_vendor_block_table]
	ldr	x5, [x5]
	add	x6, x5, x1
	ldrh	w6, [x6,2]
	strh	w6, [x5,x1]
	b	.L1479
.L1528:
	ldr	x0, [x23, #:got_lo12:p_vendor_block_ver_table]
	ldr	w1, [x26,4]
	ldr	x0, [x0]
	str	w1, [x0,x7]
	ldr	x0, [x20, #:got_lo12:p_vendor_block_table]
	ldr	x0, [x0]
	strh	w25, [x0,x8,lsl 1]
.L1481:
	tbnz	w2, #31, .L1453
	ldr	x0, [x29,144]
	ldr	x5, [x0, #:got_lo12:g_totle_vendor_block]
	ldr	x0, [x29,128]
	ldrh	w1, [x5]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldrh	w0, [x0]
	sub	w0, w0, #1
	sub	w0, w0, w1
	cmp	w2, w0, sxth
	bgt	.L1456
	add	w1, w1, 1
	strh	w1, [x5]
	sxtw	x2, w2
	ldr	w1, [x26,4]
	ldr	x0, [x23, #:got_lo12:p_vendor_block_ver_table]
	ldr	x0, [x0]
	str	w1, [x0,x2,lsl 2]
	ldr	x0, [x20, #:got_lo12:p_vendor_block_table]
.L1520:
	ldr	x0, [x0]
	strh	w25, [x0,x2,lsl 1]
	b	.L1456
.L1457:
	ldr	x0, [x29,120]
	ldr	x0, [x0, #:got_lo12:gSysInfo]
	ldrh	w1, [x0]
	cmp	w1, w28
	beq	.L1521
	ldrh	w0, [x0,4]
	cmp	w0, w28
	beq	.L1486
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1486:
	ldr	x0, [x29,120]
	ldr	w1, [x26,4]
	ldr	x0, [x0, #:got_lo12:gSysInfo]
	ldr	w2, [x0,8]
	cmp	w2, w1
	bcs	.L1487
	ldrh	w1, [x0]
	strh	w1, [x0,4]
.L1521:
	ldr	w1, [x26,4]
	strh	w25, [x0]
	str	w1, [x0,8]
	b	.L1456
.L1487:
	strh	w25, [x0,4]
	b	.L1456
.L1453:
	mov	w0, w25
	mov	w1, 1
.L1519:
	bl	FtlFreeSysBlkQueueIn
.L1456:
	add	x27, x27, 1
	b	.L1449
.L1524:
	ldr	w0, [x29,156]
	add	w26, w0, 1
	uxth	w0, w26
	str	w0, [x29,156]
	b	.L1446
.L1522:
	ldr	x0, [x21, #:got_lo12:p_map_block_table]
	ldr	x1, [x0]
	ldrh	w0, [x1]
	cbz	w0, .L1490
.L1493:
	ldr	x0, [x20, #:got_lo12:p_vendor_block_table]
	ldr	x2, [x0]
	ldrh	w0, [x2]
	cbz	w0, .L1491
	b	.L1513
.L1490:
	ldr	x19, [x19, #:got_lo12:g_totle_map_block]
	ldrh	w2, [x19]
	cbz	w2, .L1493
	ldr	x2, [x29,136]
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_max_map_blks]
	ldr	w2, [x2]
.L1494:
	cmp	w0, w2
	bcs	.L1493
	ldrh	w3, [x1,w0,sxtw 1]
	cbz	w3, .L1495
	mov	w6, w0
.L1496:
	ldr	x1, [x29,136]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_max_map_blks]
	ldr	w1, [x1]
	cmp	w0, w1
	bcs	.L1493
	ldr	x3, [x21, #:got_lo12:p_map_block_table]
	sxtw	x5, w0
	sub	w1, w0, w6
	lsl	x2, x5, 1
	add	w0, w0, 1
	ldr	x4, [x3]
	sxtw	x1, w1
	sxth	w0, w0
	ldrh	w7, [x4,x2]
	strh	w7, [x4,x1,lsl 1]
	ldr	x4, [x24, #:got_lo12:p_map_block_ver_table]
	ldr	x4, [x4]
	ldr	w5, [x4,x5,lsl 2]
	str	w5, [x4,x1,lsl 2]
	ldr	x1, [x3]
	strh	wzr, [x1,x2]
	b	.L1496
.L1495:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1494
.L1491:
	ldr	x1, [x29,144]
	ldr	x1, [x1, #:got_lo12:g_totle_vendor_block]
	ldrh	w1, [x1]
	cbz	w1, .L1513
	ldr	x1, [x29,128]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldrh	w1, [x1]
.L1499:
	cmp	w0, w1
	mov	w6, w0
	bge	.L1513
	ldrh	w3, [x2,w0,sxtw 1]
	cbz	w3, .L1500
.L1501:
	ldr	x1, [x29,128]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldrh	w1, [x1]
	cmp	w0, w1
	bge	.L1513
	ldr	x3, [x20, #:got_lo12:p_vendor_block_table]
	sxtw	x5, w0
	sub	w1, w0, w6
	lsl	x2, x5, 1
	add	w0, w0, 1
	ldr	x4, [x3]
	sxtw	x1, w1
	sxth	w0, w0
	ldrh	w7, [x4,x2]
	strh	w7, [x4,x1,lsl 1]
	ldr	x4, [x23, #:got_lo12:p_vendor_block_ver_table]
	ldr	x4, [x4]
	ldr	w5, [x4,x5,lsl 2]
	str	w5, [x4,x1,lsl 2]
	ldr	x1, [x3]
	strh	wzr, [x1,x2]
	b	.L1501
.L1500:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1499
.L1513:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	stp	x29, x30, [sp, -80]!
	adrp	x0, :got:p_sys_spare_buf
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, :got:req_sys
	str	x25, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x1, [x20, #:got_lo12:req_sys]
	adrp	x22, :got:c_ftl_nand_data_blks_per_plane
	adrp	x21, :got:p_valid_page_count_table
	adrp	x23, :got:gSysInfo
	str	xzr, [x1,8]
	ldr	x0, [x0, #:got_lo12:p_sys_spare_buf]
	ldr	x0, [x0]
	str	x0, [x1,16]
	ldr	x1, [x22, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldr	x0, [x21, #:got_lo12:p_valid_page_count_table]
	ldrh	w2, [x1]
	mov	w1, 0
	ldr	x0, [x0]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x24, [x23, #:got_lo12:gSysInfo]
	mov	w1, 65535
	ldrh	w0, [x24]
	cmp	w0, w1
	bne	.L1530
.L1534:
	mov	w19, -1
	b	.L1531
.L1530:
	mov	w1, 1
	adrp	x25, :got:p_sys_data_buf
	bl	FtlGetLastWrittenPage
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x24,2]
.L1532:
	tbnz	w19, #31, .L1537
	ldr	x0, [x23, #:got_lo12:gSysInfo]
	mov	w1, 1
	ldr	x24, [x20, #:got_lo12:req_sys]
	mov	w2, w1
	ldrh	w0, [x0]
	orr	w0, w19, w0, lsl 10
	str	w0, [x24,4]
	ldr	x0, [x25, #:got_lo12:p_sys_data_buf]
	ldr	x0, [x0]
	str	x0, [x24,8]
	mov	x0, x24
	bl	FlashReadPages
	ldr	w0, [x24]
	cmn	w0, #1
	beq	.L1533
.L1537:
	adrp	x24, :got:g_sys_save_data
	ldr	x20, [x20, #:got_lo12:req_sys]
	mov	w2, 48
	ldr	x19, [x24, #:got_lo12:g_sys_save_data]
	ldr	x1, [x20,8]
	mov	x0, x19
	bl	ftl_memcpy
	ldr	x22, [x22, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldr	x21, [x21, #:got_lo12:p_valid_page_count_table]
	ldr	x1, [x20,8]
	ldrh	w2, [x22]
	ldr	x0, [x21]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldr	w1, [x19]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	mov	x2, x24
	cmp	w1, w0
	beq	.L1544
	b	.L1534
.L1533:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1532
.L1544:
	ldr	x23, [x23, #:got_lo12:gSysInfo]
	adrp	x0, :got:c_ftl_nand_die_num
	ldrh	w3, [x19,8]
	ldrb	w1, [x19,10]
	strh	w3, [x23,6]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_die_num]
	ldrh	w0, [x0]
	cmp	w1, w0
	bne	.L1534
	adrp	x0, :got:g_MaxLbn
	adrp	x4, :got:c_ftl_nand_sec_pre_page
	ldrh	w6, [x19,14]
	adrp	x20, :got:g_gc_superblock
	ldr	x0, [x0, #:got_lo12:g_MaxLbn]
	str	w3, [x0]
	adrp	x0, :got:c_ftl_nand_page_pre_blk
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w1, [x0]
	adrp	x0, :got:g_MaxLpn
	ldr	x0, [x0, #:got_lo12:g_MaxLpn]
	mul	w1, w1, w3
	str	w1, [x0]
	adrp	x0, :got:g_MaxLbaSector
	ldr	x4, [x4, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldr	x0, [x0, #:got_lo12:g_MaxLbaSector]
	ldrh	w4, [x4]
	mul	w1, w1, w4
	str	w1, [x0]
	adrp	x1, :got:c_ftl_nand_max_data_blks
	adrp	x0, :got:gBbtInfo
	adrp	x4, :got:c_ftl_nand_data_op_blks_per_plane
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_max_data_blks]
	ldr	x0, [x0, #:got_lo12:gBbtInfo]
	ldr	x4, [x4, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	ldr	w1, [x1]
	ldrh	w0, [x0,6]
	sub	w0, w1, w0
	adrp	x1, :got:c_ftl_nand_planes_num
	sub	w0, w0, w3
	adrp	x3, :got:g_active_superblock
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w1, [x1]
	udiv	w0, w0, w1
	ldrh	w1, [x19,16]
	strh	w0, [x4]
	lsr	w4, w1, 6
	and	w1, w1, 63
	ldr	x0, [x3, #:got_lo12:g_active_superblock]
	strb	w1, [x0,6]
	ldrb	w1, [x19,11]
	strh	w6, [x0]
	strh	w4, [x0,2]
	strb	w1, [x0,8]
	mov	w0, -1
	ldr	x1, [x20, #:got_lo12:g_gc_superblock]
	strh	w0, [x1]
	strh	wzr, [x1,2]
	strb	wzr, [x1,6]
	strb	wzr, [x1,8]
	adrp	x1, :got:g_buffer_superblock
	ldrh	w0, [x19,18]
	ldr	x4, [x1, #:got_lo12:g_buffer_superblock]
	strh	w0, [x4]
	ldrh	w5, [x19,20]
	lsr	w0, w5, 6
	strh	w0, [x4,2]
	ldrb	w0, [x19,12]
	and	w5, w5, 63
	strb	w0, [x4,8]
	adrp	x0, :got:g_gc_temp_superblock
	strb	w5, [x4,6]
	mov	x21, x0
	ldrh	w5, [x19,22]
	ldr	x4, [x0, #:got_lo12:g_gc_temp_superblock]
	strh	w5, [x4]
	ldrh	w5, [x19,24]
	lsr	w7, w5, 6
	and	w5, w5, 63
	strb	w5, [x4,6]
	ldrb	w5, [x19,13]
	strh	w7, [x4,2]
	strb	w5, [x4,8]
	adrp	x4, :got:g_totle_gc_page_count
	ldr	w5, [x19,32]
	ldr	x4, [x4, #:got_lo12:g_totle_gc_page_count]
	str	wzr, [x4]
	adrp	x4, :got:g_totle_write_page_count
	ldr	x4, [x4, #:got_lo12:g_totle_write_page_count]
	str	wzr, [x4]
	adrp	x4, :got:g_totle_read_page_count
	ldr	x4, [x4, #:got_lo12:g_totle_read_page_count]
	str	wzr, [x4]
	adrp	x4, :got:g_totle_l2p_write_count
	ldr	x4, [x4, #:got_lo12:g_totle_l2p_write_count]
	str	wzr, [x4]
	adrp	x4, :got:g_totle_mlc_erase_count
	ldr	x4, [x4, #:got_lo12:g_totle_mlc_erase_count]
	str	w5, [x4]
	adrp	x4, :got:g_totle_slc_erase_count
	ldr	w5, [x19,40]
	mov	x19, x1
	ldr	x4, [x4, #:got_lo12:g_totle_slc_erase_count]
	str	wzr, [x4]
	adrp	x4, :got:g_max_erase_count
	ldr	x4, [x4, #:got_lo12:g_max_erase_count]
	str	wzr, [x4]
	adrp	x4, :got:g_totle_cache_write_count
	ldr	x4, [x4, #:got_lo12:g_totle_cache_write_count]
	str	wzr, [x4]
	adrp	x4, :got:g_GlobalSysVersion
	ldr	x4, [x4, #:got_lo12:g_GlobalSysVersion]
	ldr	w7, [x4]
	cmp	w5, w7
	bls	.L1538
	str	w5, [x4]
.L1538:
	adrp	x0, :got:g_GlobalDataVersion
	ldr	x2, [x2, #:got_lo12:g_sys_save_data]
	ldr	x0, [x0, #:got_lo12:g_GlobalDataVersion]
	ldr	w1, [x2,36]
	ldr	w2, [x0]
	cmp	w1, w2
	bls	.L1539
	str	w1, [x0]
.L1539:
	mov	w0, 65535
	cmp	w6, w0
	beq	.L1540
	ldr	x0, [x3, #:got_lo12:g_active_superblock]
	bl	make_superblock
.L1540:
	ldr	x0, [x19, #:got_lo12:g_buffer_superblock]
	mov	w1, 65535
	ldrh	w2, [x0]
	cmp	w2, w1
	beq	.L1541
	bl	make_superblock
.L1541:
	ldr	x0, [x21, #:got_lo12:g_gc_temp_superblock]
	mov	w1, 65535
	ldrh	w2, [x0]
	cmp	w2, w1
	beq	.L1542
	bl	make_superblock
.L1542:
	ldr	x0, [x20, #:got_lo12:g_gc_superblock]
	mov	w1, 65535
	mov	w19, 0
	ldrh	w2, [x0]
	cmp	w2, w1
	beq	.L1531
	bl	make_superblock
.L1531:
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
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	stp	x25, x26, [sp,64]
	mov	x26, x0
	add	x0, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	w23, w1
	ldrh	w25, [x0,128]
	mov	w0, 65535
	cmp	w25, w0
	beq	.L1566
	cbnz	w25, .L1546
	b	.L1547
.L1566:
	mov	w25, 0
.L1546:
	adrp	x0, :got:c_ftl_nand_page_pre_blk
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w0, [x0]
	cmp	w23, w0
	bne	.L1548
.L1547:
	bl	FtlGcPageVarInit
.L1548:
	mov	w22, -1
	mov	w20, 0
	mov	w21, 65535
	adrp	x24, :got:req_read
.L1560:
	ldrh	w0, [x26]
	strb	wzr, [x26,8]
	cmp	w0, w21
	beq	.L1567
.L1550:
.L1563:
	adrp	x0, :got:c_ftl_nand_planes_num
	adrp	x1, :got:c_ftl_nand_byte_pre_oob
	mov	x2, x26
	mov	w27, 0
	mov	w10, 4
	adrp	x11, :got:p_gc_spare_buf
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_byte_pre_oob]
	ldrh	w6, [x0]
	ldrh	w9, [x1]
	add	x6, x26, x6, lsl 1
.L1551:
	cmp	x2, x6
	beq	.L1575
	ldrh	w1, [x2,16]
	cmp	w1, w21
	beq	.L1552
	ldr	x8, [x24, #:got_lo12:req_read]
	mov	w0, 56
	orr	w1, w25, w1, lsl 10
	add	w5, w27, 1
	umull	x0, w27, w0
	ldr	x7, [x8]
	add	x7, x7, x0
	str	w1, [x7,4]
	ldr	x1, [x8]
	add	x0, x1, x0
	mul	w1, w27, w9
	uxth	w27, w5
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	ldr	x7, [x11, #:got_lo12:p_gc_spare_buf]
	ldr	x7, [x7]
	add	x1, x7, x1, sxtw 2
	str	x1, [x0,16]
.L1552:
	add	x2, x2, 2
	b	.L1551
.L1575:
	ldr	x0, [x24, #:got_lo12:req_read]
	mov	w1, w27
	mov	w2, 0
	mov	x28, 0
	ldr	x0, [x0]
	bl	FlashReadPages
.L1554:
	cmp	w27, w28, uxth
	bls	.L1576
	mov	x0, 56
	mul	x6, x28, x0
	adrp	x0, :got:req_read
	str	x6, [x29,112]
	ldr	x8, [x0, #:got_lo12:req_read]
	str	x8, [x29,104]
	ldr	x0, [x8]
	add	x0, x0, x6
	ldr	w1, [x0,4]
	str	x1, [x29,120]
	lsr	w0, w1, 10
	bl	P2V_plane
	uxth	w2, w0
	ldr	x8, [x29,104]
	ldr	x6, [x29,112]
	ldr	x0, [x8]
	add	x8, x0, x6
	ldr	w6, [x0,x6]
	cbnz	w6, .L1555
	ldr	x6, [x8,16]
	add	x28, x28, 1
	ldr	x1, [x29,120]
	ldrh	w0, [x6]
	cmp	w0, w21
	bne	.L1556
	adrp	x0, :got:ftl_gc_temp_power_lost_recovery_flag
	mov	w1, 1
	ldr	x0, [x0, #:got_lo12:ftl_gc_temp_power_lost_recovery_flag]
	str	w1, [x0]
	b	.L1549
.L1556:
	ldr	w0, [x6,12]
	ldr	w2, [x6,8]
	bl	FtlGcUpdatePage
	b	.L1554
.L1555:
	adrp	x0, :got:g_inkDie_check_enable
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w0, [x0]
	cbz	w0, .L1557
	adrp	x0, :got:p_erase_count_table
	ldrh	w1, [x26]
	ldr	x0, [x0, #:got_lo12:p_erase_count_table]
	ldr	x0, [x0]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 119
	bls	.L1558
.L1557:
	cmn	w6, #1
	bne	.L1559
.L1558:
	ldr	w22, [x8,4]
.L1559:
	adrp	x0, :got:p_valid_page_count_table
	ldrh	w1, [x26]
	mov	w25, 0
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x0]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x26]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x26]
	bl	FtlGcPageVarInit
	b	.L1560
.L1576:
	add	w20, w20, 1
	add	w3, w25, 1
	cmp	w20, w23
	adrp	x0, :got:c_ftl_nand_page_pre_blk
	uxth	w25, w3
	bcs	.L1562
.L1564:
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w0, [x0]
	cmp	w0, w25
	bhi	.L1563
	b	.L1567
.L1562:
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w1, [x2,128]
	cmp	w1, w21
	beq	.L1564
	add	w1, w1, w20
	strh	w1, [x2,128]
	ldr	x1, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w1, [x1]
	cmp	w1, w25
	bls	.L1564
	b	.L1565
.L1567:
	mov	w2, 0
.L1549:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w25, [x26,2]
	mov	w1, w25
	strb	w2, [x26,6]
	strh	w0, [x19,128]
	mov	x0, x26
	bl	ftl_sb_update_avl_pages
.L1565:
	mov	w0, w22
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	stp	x29, x30, [sp, -192]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x19, x0
	adrp	x0, :got:gpNandParaInfo
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	mov	w24, w1
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w26, w2
	str	w3, [x29,120]
	ldr	x0, [x0]
	mov	w21, 0
	mov	w27, 56
	ldrb	w1, [x0,19]
	adrp	x0, :got:gNandParaInfo
	ldr	x0, [x0, #:got_lo12:gNandParaInfo]
	ldrb	w28, [x0,9]
	add	x0, x29, 128
	str	x0, [x29,112]
	add	x0, x29, 132
	str	x0, [x29,104]
	sub	w0, w1, #1
	str	w0, [x29,100]
.L1578:
	cmp	w21, w24
	adrp	x20, :got:gNandMaxDie
	bcs	.L1620
	umull	x20, w21, w27
	ldr	x2, [x29,112]
	mov	w1, w26
	add	x25, x19, x20
	ldr	x3, [x29,104]
	sub	w4, w24, w21
	mov	x0, x25
	bl	LogAddr2PhyAddr
	mov	w23, w0
	adrp	x1, :got:gNandMaxDie
	ldr	w0, [x29,132]
	ldr	x1, [x1, #:got_lo12:gNandMaxDie]
	ldrb	w2, [x1]
	cmp	w0, w2
	bcc	.L1579
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1580
.L1579:
	adrp	x1, :got:gMultiPageProgEn
	adrp	x22, :got:gDieOp
	ldr	x1, [x1, #:got_lo12:gMultiPageProgEn]
	ldr	x3, [x22, #:got_lo12:gDieOp]
	ldrb	w1, [x1]
	cmp	w1, wzr
	uxtw	x1, w0
	mov	x0, 24
	csel	w23, w23, wzr, ne
	madd	x0, x1, x0, x3
	ldr	x0, [x0,8]
	cbz	x0, .L1582
	cmp	w2, 1
	bne	.L1583
	adrp	x0, :got:gpNandc
	ldr	x0, [x0, #:got_lo12:gpNandc]
	ldr	x0, [x0]
	bl	NandcIqrWaitFlashReady
.L1583:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1582:
	ldr	x2, [x22, #:got_lo12:gDieOp]
	mov	x0, 24
	ldr	w1, [x29,132]
	madd	x0, x1, x0, x2
	ldr	w2, [x29,128]
	str	x25, [x0,8]
	str	xzr, [x0,16]
	str	w2, [x0,4]
	cbz	w23, .L1584
	add	w2, w21, 1
	umull	x2, w2, w27
	add	x2, x19, x2
	str	x2, [x0,16]
.L1584:
	adrp	x0, :got:DieCsIndex
	ldr	x22, [x22, #:got_lo12:gDieOp]
	ldr	x0, [x0, #:got_lo12:DieCsIndex]
	ldrb	w20, [x0,x1]
	mov	x0, 24
	mul	x1, x1, x0
	adrp	x0, :got:gNandMaxDie
	strb	w20, [x22,x1]
	ldr	x0, [x0, #:got_lo12:gNandMaxDie]
	ldrb	w0, [x0]
	cmp	w0, 1
	mov	w0, w20
	bne	.L1585
	bl	NandcWaitFlashReady
	b	.L1586
.L1585:
	bl	NandcFlashCs
	adrp	x0, :got:DieAddrs
	ldr	w1, [x29,132]
	ldr	x0, [x0, #:got_lo12:DieAddrs]
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
.L1586:
	ldr	w0, [x29,100]
	cmp	w0, 5
	bhi	.L1587
	adrp	x0, :got:read_retry_cur_offset
	ldr	x0, [x0, #:got_lo12:read_retry_cur_offset]
	ldrb	w0, [x0,w20,sxtw]
	cbz	w0, .L1587
	adrp	x2, :got:gReadRetryInfo
	mov	w0, w20
	mov	w3, 0
	ldr	x2, [x2, #:got_lo12:gReadRetryInfo]
	add	x2, x2, 4
	ldrb	w1, [x2,-3]
	bl	HynixSetRRPara
.L1587:
	mov	w0, w20
	bl	NandcFlashCs
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldr	x3, [x25,8]
	mov	w0, w20
	ldr	x4, [x25,16]
	mov	w1, 1
	mov	w2, w28
	bl	NandcXferData
	cbz	w23, .L1588
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgDpFirstCmd
	adrp	x0, :got:DieAddrs
	ldr	w1, [x29,132]
	ldr	x0, [x0, #:got_lo12:DieAddrs]
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	adrp	x0, :got:gBlockPageAlignSize
	ldr	w1, [x29,128]
	ldr	x0, [x0, #:got_lo12:gBlockPageAlignSize]
	ldr	w2, [x0]
	mov	w0, w20
	add	w1, w2, w1
	bl	FlashProgDpSecondCmd
	add	w4, w21, 1
	mov	w0, w20
	mov	w1, 1
	mov	w2, w28
	umull	x4, w4, w27
	add	x4, x19, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
.L1588:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w21, w21, w23
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1580:
	add	w21, w21, 1
	b	.L1578
.L1620:
	adrp	x0, :got:gpNandc
	mov	w21, 0
	ldr	x0, [x0, #:got_lo12:gpNandc]
	ldr	x0, [x0]
	bl	NandcIqrWaitFlashReady
.L1590:
	ldr	x0, [x20, #:got_lo12:gNandMaxDie]
	ldrb	w0, [x0]
	cmp	w21, w0
	bcs	.L1621
	mov	w0, w21
	add	w21, w21, 1
	bl	FlashWaitCmdDone
	b	.L1590
.L1621:
	ldr	w0, [x29,120]
	cbz	w0, .L1619
	adrp	x23, .LC101
	adrp	x25, .LC100
	adrp	x27, .LC99
	mov	w22, 0
	add	x23, x23, :lo12:.LC101
	add	x25, x25, :lo12:.LC100
	add	x27, x27, :lo12:.LC99
.L1593:
	cmp	w22, w24
	beq	.L1619
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1594
	adrp	x0, .LC98
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC98
	bl	printk
	b	.L1595
.L1594:
	sub	w4, w24, w22
	mov	w1, w26
	add	x2, x29, 128
	add	x3, x29, 132
	mov	x0, x19
	adrp	x20, :got:gFlashProgCheckBuffer
	adrp	x21, :got:gFlashProgCheckSpareBuffer
	bl	LogAddr2PhyAddr
	ldr	x4, [x20, #:got_lo12:gFlashProgCheckBuffer]
	add	x3, x29, 136
	mov	x1, x19
	mov	x2, 56
	str	x4, [x29,120]
	ldr	x0, [x4]
	str	wzr, [x0]
	ldr	x28, [x21, #:got_lo12:gFlashProgCheckSpareBuffer]
	ldr	x0, [x28]
	str	wzr, [x0]
	mov	x0, x3
	bl	memcpy
	mov	x3, x0
	ldr	x4, [x29,120]
	mov	w1, 1
	mov	w2, w26
	ldr	x0, [x4]
	str	x0, [x29,144]
	ldr	x0, [x28]
	str	x0, [x29,152]
	mov	x0, x3
	bl	FlashReadPages
	ldr	w28, [x29,136]
	cmn	w28, #1
	bne	.L1596
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w28, [x19]
.L1596:
	ldr	x0, [x19,16]
	cbz	x0, .L1597
	ldr	x21, [x21, #:got_lo12:gFlashProgCheckSpareBuffer]
	ldr	w2, [x0]
	ldr	x0, [x21]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1597
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1597:
	ldr	x0, [x19,8]
	cbz	x0, .L1595
	ldr	x20, [x20, #:got_lo12:gFlashProgCheckBuffer]
	ldr	w2, [x0]
	ldr	x0, [x20]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1595
	ldr	w1, [x19,4]
	mov	x0, x23
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1595:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L1593
.L1619:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FlashTestBlk.part.19, %function
FlashTestBlk.part.19:
	stp	x29, x30, [sp, -176]!
	mov	w1, 165
	mov	w2, 32
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, :got:gFlashPageBuffer1
	str	x21, [sp,32]
	uxth	w21, w0
	add	x19, x29, 56
	lsl	w21, w21, 10
	ldr	x20, [x20, #:got_lo12:gFlashPageBuffer1]
	ldr	x0, [x20]
	str	x0, [x29,64]
	add	x0, x29, 112
	str	x0, [x29,72]
	bl	ftl_memset
	ldr	x0, [x20]
	mov	w2, 8
	mov	w1, 90
	bl	ftl_memset
	str	w21, [x29,60]
	mov	x0, x19
	mov	w1, 1
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x19
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x29,56]
	mov	w1, 1
	cmn	w0, #1
	mov	x0, x19
	csetm	w20, eq
	bl	FlashEraseBlocks
	ldr	x21, [sp,32]
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 176
	ret
	.size	FlashTestBlk.part.19, .-FlashTestBlk.part.19
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	adrp	x1, :got:gNandFlashIdbBlockAddr
	uxth	w2, w0
	stp	x29, x30, [sp, -16]!
	mov	w0, 0
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:gNandFlashIdbBlockAddr]
	ldr	w1, [x1]
	cmp	w2, w1
	bcc	.L1624
	mov	w0, w2
	bl	FlashTestBlk.part.19
.L1624:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	adrp	x5, :got:gFlashSpareBuffer
	adrp	x4, :got:gNandPhyInfo
	stp	x29, x30, [sp, -256]!
	adrp	x3, :got:gpNandParaInfo
	adrp	x2, :got:gBlockPageAlignSize
	add	x29, sp, 0
	ldr	x0, [x5, #:got_lo12:gFlashSpareBuffer]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x1, [x4, #:got_lo12:gNandPhyInfo]
	mov	w19, 0
	ldr	x0, [x0]
	str	x0, [x29,184]
	ldrh	w22, [x1,14]
	ldrh	w0, [x1,12]
	adrp	x1, :got:g_slc2KBNand
	str	x1, [x29,128]
	str	x5, [x29,160]
	mul	w22, w0, w22
	ldr	x0, [x3, #:got_lo12:gpNandParaInfo]
	str	x4, [x29,168]
	str	x3, [x29,152]
	uxth	w22, w22
	ldr	x0, [x0]
	str	x2, [x29,136]
	ldrb	w0, [x0,24]
	str	w0, [x29,180]
	ldr	x0, [x2, #:got_lo12:gBlockPageAlignSize]
	ldrh	w25, [x0]
	ldr	x0, [x1, #:got_lo12:g_slc2KBNand]
	ubfiz	w1, w25, 1, 15
	ldrb	w0, [x0]
	cmp	w0, 1
	adrp	x0, .LC102
	csel	w25, w1, w25, eq
	add	x0, x0, :lo12:.LC102
	mov	w1, 1
	bl	printk
	ldr	x0, [x29,160]
	mov	w1, 0
	mov	w2, 4096
	ldr	x0, [x0, #:got_lo12:gFlashSpareBuffer]
	ldr	x0, [x0]
	bl	ftl_memset
	lsr	w0, w22, 4
	str	w0, [x29,176]
	ldr	w0, [x29,180]
	and	w0, w0, 1
	str	w0, [x29,112]
	ldr	w0, [x29,180]
	and	w0, w0, 2
	uxtb	w0, w0
	str	w0, [x29,108]
	sub	w0, w22, #1
	uxth	w0, w0
	str	w0, [x29,104]
.L1628:
	adrp	x0, :got:gNandMaxDie
	str	x0, [x29,144]
	ldr	x1, [x0, #:got_lo12:gNandMaxDie]
	ldrb	w1, [x1]
	cmp	w1, w19
	bls	.L1673
	adrp	x0, :got:FbbtBlk
	str	x0, [x29,120]
	sxtw	x24, w19
	ldr	x1, [x0, #:got_lo12:FbbtBlk]
	ldrh	w20, [x1,w19,sxtw 1]
	cbnz	w20, .L1655
	ldr	x1, [x29,168]
	adrp	x21, :got:gFlashPageBuffer0
	mov	w26, w20
	mov	w23, w20
	ldr	x1, [x1, #:got_lo12:gNandPhyInfo]
	ldr	x0, [x21, #:got_lo12:gFlashPageBuffer0]
	ldrh	w2, [x1,20]
	mov	w1, w20
	ldr	x0, [x0]
	lsl	w2, w2, 9
	bl	ftl_memset
	adrp	x0, :got:DieCsIndex
	ldr	x0, [x0, #:got_lo12:DieCsIndex]
	ldrb	w27, [x0,x24]
	ldr	w0, [x29,180]
	and	w0, w0, 4
	uxtb	w0, w0
	str	w0, [x29,116]
.L1630:
	uxth	w28, w26
	cmp	w28, w22
	bcs	.L1640
	mov	w0, -1
	strb	w0, [x29,198]
	strb	w0, [x29,199]
	ldr	w0, [x29,112]
	cbz	w0, .L1632
	adrp	x0, :got:DieAddrs
	add	x2, x29, 198
	ldr	x0, [x0, #:got_lo12:DieAddrs]
	ldr	w3, [x0,x24,lsl 2]
	mov	w0, w27
	add	w3, w20, w3
	str	x3, [x29,96]
	mov	w1, w3
	bl	FlashReadSpare
	ldr	x0, [x29,128]
	ldr	x3, [x29,96]
	ldr	x0, [x0, #:got_lo12:g_slc2KBNand]
	ldrb	w0, [x0]
	cmp	w0, 1
	bne	.L1632
	ldr	x0, [x29,136]
	add	x2, x29, 199
	ldr	x0, [x0, #:got_lo12:gBlockPageAlignSize]
	ldr	w1, [x0]
	mov	w0, w27
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,198]
	ldrb	w1, [x29,199]
	and	w0, w1, w0
	strb	w0, [x29,198]
.L1632:
	ldr	w0, [x29,108]
	cbz	w0, .L1634
	ldr	x0, [x29,152]
	add	x2, x29, 199
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
	ldrh	w1, [x0,10]
	adrp	x0, :got:DieAddrs
	sub	w1, w1, #1
	ldr	x0, [x0, #:got_lo12:DieAddrs]
	ldr	w0, [x0,x24,lsl 2]
	add	w1, w1, w0
	mov	w0, w27
	add	w1, w1, w20
	bl	FlashReadSpare
.L1634:
	ldr	x0, [x29,152]
	ldrb	w1, [x29,198]
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	ldr	x0, [x0]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1656
	cmp	w0, 1
	bne	.L1635
.L1656:
	mov	w0, 1
	cbz	w1, .L1637
	ldrb	w0, [x29,199]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1637
.L1635:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1637
	ldrb	w0, [x29,199]
	cmp	w0, 255
	cset	w0, ne
.L1637:
	ldr	w1, [x29,116]
	cbz	w1, .L1638
	adrp	x0, :got:DieAddrs
	ldr	x0, [x0, #:got_lo12:DieAddrs]
	ldr	w1, [x0,x24,lsl 2]
	mov	w0, w27
	add	w1, w20, w1
	bl	SandiskProgTestBadBlock
.L1638:
	cbz	w0, .L1639
	adrp	x0, .LC103
	mov	w1, w19
	mov	w2, w26
	add	x0, x0, :lo12:.LC103
	add	w23, w23, 1
	bl	printk
	ldr	x0, [x21, #:got_lo12:gFlashPageBuffer0]
	ubfx	x1, x28, 5, 11
	lsl	x1, x1, 2
	uxth	w23, w23
	ldr	x2, [x0]
	mov	w0, 1
	lsl	w28, w0, w28
	ldr	w0, [x2,x1]
	orr	w28, w0, w28
	ldr	x0, [x29,144]
	str	w28, [x2,x1]
	ldr	w1, [x29,176]
	ldr	x0, [x0, #:got_lo12:gNandMaxDie]
	ldrb	w0, [x0]
	mul	w0, w1, w0
	cmp	w23, w0
	bgt	.L1640
.L1639:
	add	w26, w26, 1
	add	w20, w20, w25
	b	.L1630
.L1640:
	adrp	x0, .LC104
	mov	w1, w19
	add	x0, x0, :lo12:.LC104
	mov	w2, w23
	bl	printk
	ldr	x0, [x29,144]
	ldr	w1, [x29,176]
	ldr	x0, [x0, #:got_lo12:gNandMaxDie]
	ldrb	w0, [x0]
	mul	w0, w1, w0
	cmp	w23, w0
	blt	.L1642
	ldr	x1, [x29,168]
	ldr	x0, [x21, #:got_lo12:gFlashPageBuffer0]
	ldr	x1, [x1, #:got_lo12:gNandPhyInfo]
	ldr	x0, [x0]
	ldrh	w2, [x1,20]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L1642:
	cbnz	w19, .L1644
	adrp	x27, :got:gNandFlashIdbBlockAddr
	adrp	x23, .LC105
	mov	w20, w19
	adrp	x26, :got:gNandIDBResBlkNum
	add	x23, x23, :lo12:.LC105
	ldr	x0, [x27, #:got_lo12:gNandFlashIdbBlockAddr]
	ldrh	w28, [x0]
.L1645:
	ldr	x0, [x26, #:got_lo12:gNandIDBResBlkNum]
	ldrb	w0, [x0]
	cmp	w28, w0
	bcs	.L1674
	mov	w0, w28
	bl	FlashTestBlk
	cbz	w0, .L1646
	mov	w1, w28
	mov	x0, x23
	bl	printk
	ldr	x0, [x21, #:got_lo12:gFlashPageBuffer0]
	ubfx	x1, x28, 5, 11
	lsl	x1, x1, 2
	add	w2, w20, 1
	ldr	x3, [x0]
	mov	w0, 1
	lsl	w0, w0, w28
	uxth	w20, w2
	ldr	w4, [x3,x1]
	orr	w0, w4, w0
	str	w0, [x3,x1]
.L1646:
	add	w28, w28, 1
	uxth	w28, w28
	b	.L1645
.L1674:
	ldr	x27, [x27, #:got_lo12:gNandFlashIdbBlockAddr]
	ldr	w1, [x27]
	sub	w0, w0, w1
	cmp	w20, w0
	bcc	.L1644
	ldr	x1, [x29,168]
	ldr	x0, [x21, #:got_lo12:gFlashPageBuffer0]
	ldr	x1, [x1, #:got_lo12:gNandPhyInfo]
	ldr	x0, [x0]
	ldrh	w2, [x1,20]
	mov	w1, 0
	lsl	w2, w2, 9
	bl	ftl_memset
.L1644:
	adrp	x23, .LC106
	ldr	w27, [x29,104]
	mul	w26, w19, w22
	add	x23, x23, :lo12:.LC106
	mov	w20, -3872
	add	x28, x29, 200
.L1649:
	mov	w2, w27
	mov	x0, x23
	mov	w1, w19
	bl	printk
	ldr	x0, [x21, #:got_lo12:gFlashPageBuffer0]
	ldr	x2, [x0]
.L1650:
	ubfx	x0, x27, 5, 11
	ldr	w1, [x2,x0,lsl 2]
	lsr	w1, w1, w27
	and	w0, w1, 1
	tbz	x1, 0, .L1675
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1650
.L1675:
	ldr	x1, [x29,120]
	ldr	x1, [x1, #:got_lo12:FbbtBlk]
	strh	w27, [x1,x24,lsl 1]
	ldr	x1, [x29,184]
	strh	w20, [x1]
	strh	w27, [x1,2]
	strh	w0, [x1,8]
	add	w1, w27, w26
	lsl	w1, w1, 10
	ldr	x0, [x21, #:got_lo12:gFlashPageBuffer0]
	ldr	x0, [x0]
	str	x0, [x28,8]
	ldr	x0, [x29,160]
	ldr	x0, [x0, #:got_lo12:gFlashSpareBuffer]
	str	w1, [x28,4]
	mov	w1, 1
	ldr	x0, [x0]
	str	x0, [x28,16]
	mov	x0, x28
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x28
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x28]
	cbz	w0, .L1655
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1649
.L1655:
	add	w19, w19, 1
	uxtb	w19, w19
	b	.L1628
.L1673:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 256
	ret
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	w21, w0
	adrp	x0, :got:g_cur_erase_blk
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	x0, [x0, #:got_lo12:g_cur_erase_blk]
	uxtb	w23, w1
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w25, 0
	str	w21, [x0]
	mov	w20, w25
	mov	w19, w25
	mov	w24, 56
	adrp	x26, :got:p_plane_order_table
	adrp	x27, :got:c_ftl_nand_byte_pre_oob
	mov	w28, 4
.L1677:
	adrp	x0, :got:c_ftl_nand_planes_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w0, w25
	bls	.L1715
	adrp	x1, :got:req_erase
	umull	x0, w25, w24
	ldr	x1, [x1, #:got_lo12:req_erase]
	ldr	x1, [x1]
	str	wzr, [x1,x0]
	mov	w1, w21
	ldr	x0, [x26, #:got_lo12:p_plane_order_table]
	ldrb	w0, [x0,w25,sxtw]
	bl	V2P_block
	uxth	w22, w0
	mov	w1, w22
	cbnz	w23, .L1678
.L1682:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1716
	adrp	x1, :got:req_erase
	lsl	w22, w22, 10
	umull	x0, w20, w24
	ldr	x1, [x1, #:got_lo12:req_erase]
	ldr	x3, [x1]
	add	x3, x3, x0
	str	w22, [x3,4]
	adrp	x3, :got:p_io_spare_buf
	ldr	x1, [x1]
	add	x1, x1, x0
	str	xzr, [x1,8]
	ldr	x0, [x27, #:got_lo12:c_ftl_nand_byte_pre_oob]
	ldr	x3, [x3, #:got_lo12:p_io_spare_buf]
	ldrh	w0, [x0]
	ldr	x3, [x3]
	mul	w0, w20, w0
	add	w20, w20, 1
	sdiv	w0, w0, w28
	uxth	w20, w20
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1681
.L1678:
	str	x1, [x29,120]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,120]
	cbnz	w0, .L1681
	b	.L1682
.L1716:
	add	w19, w19, 1
	uxth	w19, w19
.L1681:
	add	w2, w25, 1
	uxth	w25, w2
	b	.L1677
.L1715:
	cbz	w20, .L1685
	adrp	x22, :got:req_erase
	mov	w1, w20
	mov	x24, 0
	mov	x25, 56
	ldr	x0, [x22, #:got_lo12:req_erase]
	ldr	x0, [x0]
	bl	FlashEraseBlocks
.L1686:
	cmp	w20, w24, uxth
	bls	.L1717
	ldr	x1, [x22, #:got_lo12:req_erase]
	mul	x0, x24, x25
	ldr	x1, [x1]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1687
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1687:
	add	x24, x24, 1
	b	.L1686
.L1717:
	cmp	w23, wzr
	cset	w26, ne
	cbz	w26, .L1703
	adrp	x0, :got:g_inkDie_check_enable
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w0, [x0]
	cbz	w0, .L1703
	adrp	x0, :got:c_ftl_nand_page_pre_slc_blk
	mov	w23, 5
	mov	w27, 1
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w25, [x0]
	udiv	w23, w25, w23
	sub	w23, w23, #1
	uxth	w0, w23
	str	w0, [x29,120]
	b	.L1689
.L1703:
	mov	w0, 6
	mov	w27, 0
	str	w0, [x29,120]
	mov	w25, 1
.L1689:
	mov	w24, 0
	mov	w28, 56
.L1701:
	mov	w23, 0
	mov	w20, w23
.L1690:
	adrp	x0, :got:c_ftl_nand_planes_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w0, w23
	bls	.L1718
	ldr	x1, [x22, #:got_lo12:req_erase]
	umull	x0, w23, w28
	ldr	x1, [x1]
	str	wzr, [x1,x0]
	adrp	x0, :got:p_plane_order_table
	mov	w1, w21
	ldr	x0, [x0, #:got_lo12:p_plane_order_table]
	ldrb	w0, [x0,w23,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,116]
	mov	w1, w2
	cbnz	w26, .L1691
.L1694:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1693
	ldr	x4, [x22, #:got_lo12:req_erase]
	umull	x0, w20, w28
	ldr	w2, [x29,116]
	ldr	x1, [x4]
	add	x1, x1, x0
	add	w3, w24, w2, lsl 10
	mov	w2, 4
	str	w3, [x1,4]
	ldr	x1, [x4]
	add	x1, x1, x0
	adrp	x0, :got:p_io_data_buf_0
	ldr	x0, [x0, #:got_lo12:p_io_data_buf_0]
	ldr	x0, [x0]
	str	x0, [x1,8]
	adrp	x0, :got:c_ftl_nand_byte_pre_oob
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_byte_pre_oob]
	ldrh	w0, [x0]
	mul	w0, w20, w0
	add	w20, w20, 1
	sdiv	w0, w0, w2
	adrp	x2, :got:p_io_data_buf_1
	uxth	w20, w20
	ldr	x3, [x2, #:got_lo12:p_io_data_buf_1]
	ldr	x3, [x3]
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1693
.L1691:
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbz	w0, .L1694
.L1693:
	add	w2, w23, 1
	uxth	w23, w2
	b	.L1690
.L1718:
	cbz	w20, .L1685
	ldr	x0, [x22, #:got_lo12:req_erase]
	mov	w1, w20
	mov	w2, w27
	mov	w3, 1
	mov	x23, 0
	ldr	x0, [x0]
	bl	FlashProgPages
.L1697:
	cmp	w20, w23, uxth
	bls	.L1719
	ldr	x1, [x22, #:got_lo12:req_erase]
	mov	x0, 56
	mul	x0, x23, x0
	ldr	x3, [x1]
	add	x1, x3, x0
	ldr	w0, [x3,x0]
	cmn	w0, #1
	bne	.L1698
	ldr	w0, [x1,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	b	.L1699
.L1698:
	cbz	w26, .L1699
	ldr	w0, [x1,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1699:
	add	x23, x23, 1
	b	.L1697
.L1719:
	ldr	w0, [x29,120]
	add	w24, w24, w0
	uxth	w24, w24
	cmp	w24, w25
	bcc	.L1701
	cmp	w21, 63
	bhi	.L1685
	ldr	x22, [x22, #:got_lo12:req_erase]
	mov	w1, w20
	ldr	x0, [x22]
	bl	FlashEraseBlocks
.L1685:
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
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, :got:p_sys_data_buf
	adrp	x22, :got:p_sys_spare_buf
	stp	x19, x20, [sp,16]
	adrp	x20, :got:req_sys
	mov	x19, 0
	ldr	x0, [x21, #:got_lo12:p_sys_data_buf]
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	ldr	x1, [x20, #:got_lo12:req_sys]
	adrp	x24, :got:c_ftl_nand_die_num
	ldr	x0, [x0]
	adrp	x25, :got:gBbtInfo
	adrp	x26, :got:c_ftl_nand_bbm_buf_size
	str	x0, [x1,8]
	ldr	x2, [x22, #:got_lo12:p_sys_spare_buf]
	ldr	x2, [x2]
	str	x2, [x1,16]
	adrp	x2, :got:c_ftl_nand_byte_pre_page
	mov	w1, 0
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldrh	w2, [x2]
	bl	ftl_memset
	str	x21, [x29,104]
	str	x22, [x29,96]
.L1721:
	ldr	x1, [x24, #:got_lo12:c_ftl_nand_die_num]
	mov	w0, w19
	adrp	x23, :got:gBbtInfo
	add	x19, x19, 1
	ldrh	w1, [x1]
	cmp	w0, w1
	bge	.L1728
	ldr	x1, [x26, #:got_lo12:c_ftl_nand_bbm_buf_size]
	ldrh	w2, [x1]
	ldr	x1, [x20, #:got_lo12:req_sys]
	mul	w0, w0, w2
	ldr	x3, [x1,8]
	lsl	w2, w2, 2
	ldr	x1, [x25, #:got_lo12:gBbtInfo]
	add	x0, x3, x0, sxtw 2
	add	x1, x1, x19, lsl 3
	ldr	x1, [x1,24]
	bl	ftl_memcpy
	b	.L1721
.L1728:
	ldr	x0, [x20, #:got_lo12:req_sys]
	mov	w1, 255
	mov	w2, 16
	adrp	x24, .LC107
	adrp	x25, .LC108
	mov	w26, 0
	ldr	x21, [x0,16]
	add	x24, x24, :lo12:.LC107
	adrp	x27, :got:c_ftl_nand_page_pre_slc_blk
	adrp	x28, :got:req_erase
	mov	x0, x21
	add	x25, x25, :lo12:.LC108
	bl	ftl_memset
	mov	w0, -3887
	strh	w0, [x21]
	ldr	x0, [x23, #:got_lo12:gBbtInfo]
	ldr	w1, [x0,8]
	str	w1, [x21,4]
	ldrh	w1, [x0]
	strh	w1, [x21,2]
	ldrh	w1, [x0,4]
	ldrh	w0, [x0,6]
	strh	w0, [x21,10]
	adrp	x0, :got:c_ftl_nand_sys_blks_per_plane
	strh	w1, [x21,8]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sys_blks_per_plane]
	ldr	w0, [x0]
	strh	w0, [x21,12]
.L1723:
	ldr	x0, [x29,104]
	ldr	x22, [x20, #:got_lo12:req_sys]
	ldr	x0, [x0, #:got_lo12:p_sys_data_buf]
	ldr	x0, [x0]
	str	x0, [x22,8]
	ldr	x0, [x29,96]
	ldr	x0, [x0, #:got_lo12:p_sys_spare_buf]
	str	wzr, [x22]
	ldr	x0, [x0]
	str	x0, [x22,16]
	ldr	x19, [x23, #:got_lo12:gBbtInfo]
	ldrh	w4, [x21,10]
	ldrh	w1, [x19]
	ldrh	w2, [x19,2]
	ldrh	w3, [x19,4]
	orr	w0, w2, w1, lsl 10
	str	w0, [x22,4]
	mov	x0, x24
	bl	printk
	mov	w1, 1
	mov	x0, x22
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	x0, [x27, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w1, [x19,2]
	ldrh	w0, [x0]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L1724
	ldr	w0, [x19,8]
	ldrh	w1, [x19]
	add	w0, w0, 1
	str	w0, [x19,8]
	str	w0, [x21,4]
	strh	w1, [x21,8]
	ldrh	w0, [x19,4]
	strh	w0, [x19]
	strh	w1, [x19,4]
	lsl	w0, w0, 10
	strh	wzr, [x19,2]
	str	w0, [x22,4]
	ldr	x1, [x28, #:got_lo12:req_erase]
	ldr	x2, [x1]
	str	w0, [x2,4]
	ldr	x0, [x1]
	mov	w1, 1
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x22
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
.L1724:
	bl	FtlBbtInfoPrint
	ldr	x1, [x23, #:got_lo12:gBbtInfo]
	ldrh	w0, [x1,2]
	add	w0, w0, 1
	strh	w0, [x1,2]
	ldr	x1, [x20, #:got_lo12:req_sys]
	ldr	w0, [x1]
	cmn	w0, #1
	bne	.L1725
	ldr	w1, [x1,4]
	mov	x0, x25
	bl	printk
	b	.L1723
.L1725:
	cbnz	w26, .L1729
	mov	w26, 1
	b	.L1723
.L1729:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	adrp	x20, :got:g_num_free_superblocks
	adrp	x24, :got:c_ftl_nand_type
	adrp	x21, :got:g_inkDie_check_enable
	adrp	x22, :got:g_min_erase_count
.L1731:
	adrp	x0, :got:g_gc_temp_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	cmp	x19, x0
	bne	.L1732
	ldr	x0, [x20, #:got_lo12:g_num_free_superblocks]
	ldrh	w2, [x0]
	adrp	x0, :got:g_in_swl_replace
	lsr	w3, w2, 1
	ldr	x0, [x0, #:got_lo12:g_in_swl_replace]
	add	w1, w3, 1
	ldr	w4, [x0]
	mul	w0, w2, w4
	add	w1, w1, w0, lsr 2
	ldr	x0, [x21, #:got_lo12:g_inkDie_check_enable]
	uxth	w1, w1
	ldr	w0, [x0]
	cbz	w0, .L1733
	ldr	x0, [x22, #:got_lo12:g_min_erase_count]
	ldr	w0, [x0]
	cmp	w0, 29
	bhi	.L1733
	cmp	w0, 2
	mov	w1, 0
	bls	.L1734
	tbz	x2, 0, .L1758
	cbz	w4, .L1734
.L1758:
	mov	w1, w3
	b	.L1733
.L1732:
	ldrb	w0, [x19,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L1734
	ldr	x0, [x24, #:got_lo12:c_ftl_nand_type]
	ldrh	w0, [x0]
	cmp	w0, 1
	beq	.L1734
	ldr	x2, [x21, #:got_lo12:g_inkDie_check_enable]
	ldr	x0, [x20, #:got_lo12:g_num_free_superblocks]
	ldr	w2, [x2]
	ldrh	w0, [x0]
	lsr	w1, w0, 3
	cbz	w2, .L1733
	ldr	x2, [x22, #:got_lo12:g_min_erase_count]
	ldr	w2, [x2]
	cmp	w2, 1
	bhi	.L1733
	mov	w1, 7
	mul	w1, w0, w1
	lsr	w1, w1, 3
.L1733:
	cbz	w1, .L1734
	sub	w1, w1, #1
	uxth	w1, w1
.L1734:
	adrp	x0, :got:p_free_data_block_list_head
	ldr	x0, [x0, #:got_lo12:p_free_data_block_list_head]
	bl	List_pop_index_node
	uxth	w26, w0
	ldr	x1, [x20, #:got_lo12:g_num_free_superblocks]
	ldrh	w0, [x1]
	sub	w0, w0, #1
	strh	w0, [x1]
	strh	w26, [x19]
	mov	x0, x19
	bl	make_superblock
	ldrb	w0, [x19,7]
	cbz	w0, .L1736
	adrp	x0, :got:c_ftl_nand_planes_num
	adrp	x6, :got:req_erase
	mov	x3, 56
	mov	w7, 65535
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w5, [x0]
	mov	x0, 0
	mov	w25, w0
	b	.L1737
.L1736:
	adrp	x0, :got:p_valid_page_count_table
	ubfiz	x1, x26, 1, 16
	mov	w2, -1
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x0]
	strh	w2, [x0,x1]
	b	.L1780
.L1740:
	ldr	x1, [x6, #:got_lo12:req_erase]
	ldr	x4, [x1]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x19, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w7
	beq	.L1739
	umull	x2, w25, w3
	add	w25, w25, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w25, w25
	str	w1, [x2,4]
.L1739:
	add	x0, x0, 1
.L1737:
	cmp	w5, w0, uxth
	adrp	x23, :got:req_erase
	bhi	.L1740
	adrp	x1, :got:g_active_superblock
	uxtw	x27, w26
	adrp	x0, :got:p_erase_count_table
	ldr	x1, [x1, #:got_lo12:g_active_superblock]
	cmp	x19, x1
	bne	.L1741
	ldr	x2, [x21, #:got_lo12:g_inkDie_check_enable]
	ldr	w2, [x2]
	cbz	w2, .L1741
	ldr	x2, [x0, #:got_lo12:p_erase_count_table]
	ldr	x2, [x2]
	ldrh	w2, [x2,x27,lsl 1]
	cmp	w2, 30
	bls	.L1741
	strb	wzr, [x1,8]
.L1741:
	ldrb	w5, [x19,8]
	adrp	x1, :got:c_mlc_erase_count_value
	adrp	x3, :got:g_totle_mlc_erase_count
	adrp	x2, :got:g_totle_slc_erase_count
	ldr	x6, [x0, #:got_lo12:p_erase_count_table]
	cbnz	w5, .L1742
	lsl	x5, x27, 1
	ldr	x6, [x6]
	mov	w7, 2
	ldrh	w8, [x6,x5]
	cbz	w8, .L1781
	ldr	x7, [x1, #:got_lo12:c_mlc_erase_count_value]
	ldrh	w7, [x7]
	add	w7, w8, w7
.L1781:
	strh	w7, [x6,x5]
	ldr	x6, [x3, #:got_lo12:g_totle_mlc_erase_count]
	b	.L1782
.L1742:
	lsl	x5, x27, 1
	ldr	x7, [x6]
	ldrh	w6, [x7,x5]
	add	w6, w6, 1
	strh	w6, [x7,x5]
	ldr	x6, [x2, #:got_lo12:g_totle_slc_erase_count]
.L1782:
	ldr	w5, [x6]
	lsl	x27, x27, 1
	add	w5, w5, 1
	str	w5, [x6]
	ldr	x0, [x0, #:got_lo12:p_erase_count_table]
	ldr	x0, [x0]
	ldrh	w5, [x0,x27]
	adrp	x0, :got:g_max_erase_count
	ldr	x0, [x0, #:got_lo12:g_max_erase_count]
	ldr	w6, [x0]
	cmp	w5, w6
	bls	.L1746
	str	w5, [x0]
.L1746:
	ldr	x1, [x1, #:got_lo12:c_mlc_erase_count_value]
	adrp	x5, :got:g_totle_avg_erase_count
	ldr	x3, [x3, #:got_lo12:g_totle_mlc_erase_count]
	ldr	x2, [x2, #:got_lo12:g_totle_slc_erase_count]
	ldrh	w1, [x1]
	ldr	w3, [x3]
	ldr	w0, [x2]
	ldr	x5, [x5, #:got_lo12:g_totle_avg_erase_count]
	madd	w0, w1, w3, w0
	adrp	x1, :got:c_ftl_nand_data_blks_per_plane
	mov	x3, 56
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w1, [x1]
	udiv	w0, w0, w1
	str	w0, [x5]
	adrp	x0, :got:gp_ect_tbl_info
	ldr	x0, [x0, #:got_lo12:gp_ect_tbl_info]
	ldr	x1, [x0]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L1747:
	cmp	w25, w0, uxth
	bls	.L1783
	ldr	x1, [x23, #:got_lo12:req_erase]
	ldr	x1, [x1]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L1747
.L1783:
	ldr	x0, [x23, #:got_lo12:req_erase]
	mov	w1, w25
	mov	x28, 0
	ldr	x0, [x0]
	bl	FlashEraseBlocks
	mov	w1, w28
	mov	x3, 56
.L1749:
	cmp	w25, w28, uxth
	bls	.L1784
	ldr	x2, [x23, #:got_lo12:req_erase]
	mul	x0, x28, x3
	ldr	x2, [x2]
	add	x5, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L1750
	ldr	w0, [x5,4]
	add	w1, w1, 1
	str	x3, [x29,96]
	lsr	w0, w0, 10
	str	x2, [x29,104]
	str	x1, [x29,112]
	str	x1, [x29,120]
	bl	FtlBbmMapBadBlock
	add	x0, x19, x28, lsl 1
	ldr	x2, [x29,104]
	ldr	x3, [x29,96]
	ldr	x1, [x29,120]
	strh	w2, [x0,16]
	ldrb	w0, [x19,7]
	sub	w0, w0, #1
	strb	w0, [x19,7]
.L1750:
	add	x28, x28, 1
	b	.L1749
.L1784:
	cbz	w1, .L1752
	mov	w0, w26
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1752:
	ldrb	w1, [x19,7]
	adrp	x0, :got:p_valid_page_count_table
	cbnz	w1, .L1753
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	mov	w1, -1
	ldr	x0, [x0]
	strh	w1, [x0,x27]
.L1780:
	mov	w0, w26
	bl	INSERT_DATA_LIST
	b	.L1731
.L1753:
	adrp	x2, :got:c_ftl_nand_page_pre_blk
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_page_pre_blk]
	strh	wzr, [x19,2]
	strb	wzr, [x19,6]
	ldrh	w2, [x2]
	strh	w26, [x19]
	mul	w1, w1, w2
	adrp	x2, :got:g_GlobalSysVersion
	uxth	w1, w1
	strh	w1, [x19,4]
	ldr	x2, [x2, #:got_lo12:g_GlobalSysVersion]
	ldr	w3, [x2]
	str	w3, [x19,12]
	add	w3, w3, 1
	str	w3, [x2]
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldrh	w2, [x19]
	ldr	x0, [x0]
	strh	w1, [x0,x2,lsl 1]
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
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, :got:g_gc_bad_block_temp_num
	stp	x23, x24, [sp,48]
	uxth	w24, w0
	ldr	x0, [x21, #:got_lo12:g_gc_bad_block_temp_num]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w20, 0
	ldrh	w0, [x0]
	cbz	w0, .L1787
	adrp	x22, .LC109
	adrp	x23, :got:c_ftl_nand_planes_num
	adrp	x25, :got:p_plane_order_table
	adrp	x26, :got:g_gc_bad_block_temp_tbl
	add	x22, x22, :lo12:.LC109
.L1795:
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w0, w20
	bls	.L1796
	ldr	x0, [x25, #:got_lo12:p_plane_order_table]
	mov	w1, w24
	mov	w19, 0
	ldrb	w0, [x0,w20,sxtw]
	bl	V2P_block
	uxth	w27, w0
.L1788:
	ldr	x28, [x21, #:got_lo12:g_gc_bad_block_temp_num]
	ldrh	w0, [x28]
	cmp	w0, w19
	bls	.L1797
	ldr	x0, [x26, #:got_lo12:g_gc_bad_block_temp_tbl]
	adrp	x4, :got:g_gc_bad_block_temp_tbl
	ldrh	w0, [x0,w19,sxtw 1]
	cmp	w0, w27
	bne	.L1789
	mov	w1, w27
	mov	x0, x22
	str	x4, [x29,104]
	bl	printk
	mov	w0, w27
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x28]
	mov	w3, w19
	ldr	x4, [x29,104]
.L1790:
	cmp	w3, w2
	bcs	.L1798
	ldr	x1, [x4, #:got_lo12:g_gc_bad_block_temp_tbl]
	add	w0, w3, 1
	ldrh	w5, [x1,w0,sxtw 1]
	strh	w5, [x1,w3,sxtw 1]
	uxth	w3, w0
	b	.L1790
.L1798:
	ldr	x0, [x21, #:got_lo12:g_gc_bad_block_temp_num]
	sub	w2, w2, #1
	strh	w2, [x0]
.L1789:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1788
.L1797:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1795
.L1796:
	bl	FtlGcReFreshBadBlk
.L1787:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxth	w19, w0
	adrp	x0, :got:p_valid_page_count_table
	ubfiz	x1, x19, 1, 16
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x0]
	ldrh	w1, [x0,x1]
	cbnz	w1, .L1800
	adrp	x0, :got:g_gc_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_superblock]
	ldrh	w2, [x0]
	cmp	w2, w19
	bne	.L1801
	mov	w1, -1
	strh	w1, [x0]
	b	.L1802
.L1801:
	adrp	x0, :got:g_active_superblock
	ldr	x0, [x0, #:got_lo12:g_active_superblock]
	ldrh	w2, [x0]
	mov	w0, w1
	cmp	w2, w19
	beq	.L1803
	adrp	x0, :got:g_buffer_superblock
	ldr	x0, [x0, #:got_lo12:g_buffer_superblock]
	ldrh	w2, [x0]
	mov	w0, w1
	cmp	w2, w19
	beq	.L1803
	adrp	x0, :got:g_gc_temp_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	ldrh	w2, [x0]
	mov	w0, w1
	cmp	w2, w19
	beq	.L1803
.L1802:
	adrp	x0, :got:p_data_block_list_head
	mov	w1, w19
	ldr	x0, [x0, #:got_lo12:p_data_block_list_head]
	bl	List_remove_node
	adrp	x0, :got:g_num_data_superblocks
	ldr	x0, [x0, #:got_lo12:g_num_data_superblocks]
	ldrh	w1, [x0]
	sub	w1, w1, #1
	strh	w1, [x0]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L1803
.L1800:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L1803:
	ldr	x19, [sp,16]
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
	beq	.L1808
	adrp	x0, :got:p_valid_page_count_table
	ubfiz	x2, x1, 1, 16
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x0]
	ldrh	w19, [x0,x2]
	cbnz	w19, .L1809
	adrp	x0, .LC110
	mov	w2, w19
	add	x0, x0, :lo12:.LC110
	bl	printk
	b	.L1813
.L1809:
	sub	w19, w19, #1
	strh	w19, [x0,x2]
.L1808:
	adrp	x19, :got:g_tmp_data_superblock_id
	mov	w0, 65535
	ldr	x19, [x19, #:got_lo12:g_tmp_data_superblock_id]
	ldrh	w2, [x19]
	cmp	w2, w0
	bne	.L1811
	strh	w1, [x19]
.L1813:
	mov	w0, 0
	b	.L1810
.L1811:
	cmp	w2, w1
	str	x1, [x29,40]
	mov	w0, 0
	beq	.L1810
	mov	w0, w2
	bl	update_vpc_list
	cmp	w0, wzr
	ldr	x1, [x29,40]
	cset	w0, ne
	strh	w1, [x19]
.L1810:
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
	cbz	w1, .L1814
	mov	x19, x0
	ldrb	w0, [x0,6]
	mov	w20, 65535
	adrp	x21, :got:mlcPageToSlcPageTbl
	add	x0, x0, 8
	adrp	x22, :got:c_ftl_nand_planes_num
	ldrh	w0, [x19,x0,lsl 1]
.L1817:
	cmp	w0, w20
	bne	.L1821
.L1819:
	ldrb	w0, [x19,6]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,6]
	ldr	x1, [x22, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L1818
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1818:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L1817
.L1821:
	ldrb	w0, [x19,8]
	cmp	w0, 1
	bne	.L1814
	ldr	x0, [x21, #:got_lo12:mlcPageToSlcPageTbl]
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,w1,sxtw 1]
	cmp	w0, w20
	bne	.L1814
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L1819
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1814:
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
	mov	w20, 65535
	adrp	x21, :got:c_ftl_nand_planes_num
	adrp	x23, :got:mlcPageToSlcPageTbl
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
.L1823:
	cmp	w0, w20
	adrp	x22, :got:c_ftl_nand_planes_num
	bne	.L1835
.L1824:
	ldrb	w0, [x19,6]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,6]
	ldr	x1, [x21, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L1825
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1825:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L1823
.L1835:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L1826
	ldr	x1, [x23, #:got_lo12:mlcPageToSlcPageTbl]
	ldrh	w2, [x19,2]
	ldrh	w1, [x1,w2,sxtw 1]
	cmp	w1, w20
	bne	.L1826
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L1824
.L1826:
	ldrh	w20, [x19,2]
	mov	w21, 65535
	mov	w23, w21
	adrp	x24, :got:mlcPageToSlcPageTbl
	orr	w20, w20, w0, lsl 10
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
.L1827:
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_planes_num]
	ldrb	w1, [x19,6]
	ldrh	w2, [x0]
.L1829:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L1828
	ldrh	w0, [x19,2]
	mov	w1, 0
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1828:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w21
	beq	.L1829
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L1830
	ldr	x0, [x24, #:got_lo12:mlcPageToSlcPageTbl]
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,w1,sxtw 1]
	cmp	w0, w23
	bne	.L1830
	ldrh	w0, [x19,4]
	cbz	w0, .L1830
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L1827
.L1830:
	mov	w0, w20
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
	adrp	x23, :got:p_sys_data_buf
	adrp	x24, :got:p_sys_spare_buf
	stp	x21, x22, [sp,32]
	adrp	x21, :got:req_sys
	adrp	x22, :got:gSysInfo
	ldr	x0, [x23, #:got_lo12:p_sys_data_buf]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	ldr	x26, [x21, #:got_lo12:req_sys]
	adrp	x19, :got:g_sys_save_data
	ldr	x0, [x0]
	adrp	x25, :got:g_GlobalSysVersion
	mov	w28, 65535
	str	x0, [x26,8]
	ldr	x0, [x24, #:got_lo12:p_sys_spare_buf]
	ldr	x20, [x0]
	str	x20, [x26,16]
	ldr	x0, [x22, #:got_lo12:gSysInfo]
	str	wzr, [x20,12]
	ldrh	w1, [x0]
	strh	w1, [x20,2]
	mov	w1, -3932
	strh	w1, [x20]
	ldr	w1, [x0,8]
	str	w1, [x20,4]
	mov	w1, 19539
	str	wzr, [x20,8]
	movk	w1, 0x4654, lsl 16
	ldrh	w0, [x0,6]
	ldr	x19, [x19, #:got_lo12:g_sys_save_data]
	str	w1, [x19]
	mov	w1, 39
	strh	w0, [x19,8]
	adrp	x0, :got:c_ftl_nand_die_num
	movk	w1, 0x5000, lsl 16
	str	w1, [x19,4]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_die_num]
	ldrh	w0, [x0]
	strb	w0, [x19,10]
	adrp	x0, :got:g_active_superblock
	ldr	x0, [x0, #:got_lo12:g_active_superblock]
	ldrh	w1, [x0]
	strh	w1, [x19,14]
	ldrh	w1, [x0,2]
	ldrb	w2, [x0,6]
	ldrb	w0, [x0,8]
	strb	w0, [x19,11]
	adrp	x0, :got:g_buffer_superblock
	orr	w1, w2, w1, lsl 6
	strh	w1, [x19,16]
	ldr	x0, [x0, #:got_lo12:g_buffer_superblock]
	ldrh	w1, [x0]
	strh	w1, [x19,18]
	ldrh	w1, [x0,2]
	ldrb	w2, [x0,6]
	ldrb	w0, [x0,8]
	strb	w0, [x19,12]
	adrp	x0, :got:g_gc_temp_superblock
	orr	w1, w2, w1, lsl 6
	strh	w1, [x19,20]
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	ldrh	w1, [x0]
	strh	w1, [x19,22]
	ldrh	w1, [x0,2]
	ldrb	w2, [x0,6]
	ldrb	w0, [x0,8]
	strb	w0, [x19,13]
	adrp	x0, :got:g_totle_mlc_erase_count
	orr	w1, w2, w1, lsl 6
	strh	w1, [x19,24]
	adrp	x2, :got:c_ftl_nand_byte_pre_page
	mov	w1, 255
	ldr	x0, [x0, #:got_lo12:g_totle_mlc_erase_count]
	ldr	w0, [x0]
	str	w0, [x19,32]
	ldr	x0, [x25, #:got_lo12:g_GlobalSysVersion]
	ldr	w0, [x0]
	str	w0, [x19,40]
	adrp	x0, :got:g_GlobalDataVersion
	ldr	x0, [x0, #:got_lo12:g_GlobalDataVersion]
	ldr	w0, [x0]
	str	w0, [x19,36]
	ldr	x0, [x26,8]
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldrh	w2, [x2]
	bl	ftl_memset
	ldr	x0, [x26,8]
	mov	x1, x19
	mov	w2, 48
	bl	ftl_memcpy
	adrp	x2, :got:c_ftl_nand_data_blks_per_plane
	adrp	x1, :got:p_valid_page_count_table
	ldr	x0, [x26,8]
	adrp	x26, :got:c_ftl_nand_page_pre_slc_blk
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	add	x0, x0, 48
	ldr	x1, [x1, #:got_lo12:p_valid_page_count_table]
	ldrh	w2, [x2]
	ldr	x1, [x1]
	lsl	w2, w2, 1
	bl	ftl_memcpy
	mov	w0, 0
	bl	FtlUpdateVaildLpn
.L1837:
	ldr	x0, [x23, #:got_lo12:p_sys_data_buf]
	ldr	x27, [x21, #:got_lo12:req_sys]
	ldr	x0, [x0]
	str	x0, [x27,8]
	ldr	x0, [x24, #:got_lo12:p_sys_spare_buf]
	ldr	x0, [x0]
	str	x0, [x27,16]
	ldr	x19, [x22, #:got_lo12:gSysInfo]
	ldrh	w0, [x19]
	ldrh	w1, [x19,2]
	orr	w0, w1, w0, lsl 10
	mov	w1, 1
	str	w0, [x27,4]
	mov	w2, w1
	mov	w3, w1
	mov	x0, x27
	bl	FlashProgPages
	ldr	x0, [x26, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w1, [x19,2]
	ldrh	w0, [x0]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L1838
	ldrh	w0, [x19]
	ldrh	w28, [x19,4]
	strh	wzr, [x19,2]
	strh	w0, [x19,4]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19]
	ldr	x2, [x25, #:got_lo12:g_GlobalSysVersion]
	ldr	w1, [x2]
	str	w1, [x19,8]
	add	w3, w1, 1
	str	w3, [x2]
	ubfiz	w2, w0, 10, 16
	str	w2, [x27,4]
	str	w1, [x20,4]
	mov	w1, 1
	strh	w0, [x20,2]
	mov	w2, w1
	mov	x0, x27
	mov	w3, w1
	bl	FlashProgPages
.L1838:
	ldr	x0, [x22, #:got_lo12:gSysInfo]
	ldrh	w1, [x0,2]
	add	w1, w1, 1
	uxth	w1, w1
	strh	w1, [x0,2]
	ldr	x2, [x21, #:got_lo12:req_sys]
	ldr	w2, [x2]
	cmn	w2, #1
	bne	.L1839
	cmp	w1, 1
	bne	.L1837
	adrp	x1, :got:c_ftl_nand_page_pre_slc_blk
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w1, [x1]
	sub	w1, w1, #1
	strh	w1, [x0,2]
	b	.L1837
.L1839:
	cmp	w1, 1
	beq	.L1837
	mov	w0, 65535
	cmp	w28, w0
	beq	.L1841
	mov	w0, w28
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1841:
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
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	adrp	x22, :got:c_ftl_nand_page_pre_slc_blk
	ldr	x20, [x0,16]
	ldr	x24, [x0,40]
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19,10]
	ldrh	w2, [x19,8]
	sub	w1, w1, #1
	cmp	w2, w1
	blt	.L1846
	ubfiz	x1, x0, 1, 16
	ldrh	w23, [x20,x1]
	cbz	w23, .L1846
	ldr	w0, [x19,52]
	cbnz	w0, .L1846
	mov	w2, 1
	str	w2, [x19,52]
	strh	w0, [x20,x1]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w0, [x0]
	cmp	w1, w0
	bcc	.L1847
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1847:
	mov	w20, 0
	adrp	x25, :got:req_sys
	adrp	x26, :got:p_sys_data_buf_1
	adrp	x27, :got:p_sys_spare_buf
.L1848:
	ldrh	w0, [x19,6]
	cmp	w0, w20
	bls	.L1856
	ubfiz	x28, x20, 2, 16
	ldr	w0, [x24,x28]
	cmp	w23, w0, lsr 10
	bne	.L1849
	ldr	x0, [x26, #:got_lo12:p_sys_data_buf_1]
	mov	w1, 1
	ldr	x21, [x25, #:got_lo12:req_sys]
	mov	w2, w1
	ldr	x0, [x0]
	str	x0, [x21,8]
	ldr	x0, [x27, #:got_lo12:p_sys_spare_buf]
	ldr	x0, [x0]
	str	x0, [x21,16]
	ldr	w0, [x24,x28]
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1850
	str	wzr, [x24,x28]
	b	.L1849
.L1850:
	ldr	x2, [x21,8]
	mov	x0, x19
	mov	w1, w20
	bl	FtlMapWritePage
.L1849:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1848
.L1856:
	mov	w0, w23
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L1846:
	ldr	x22, [x22, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w1, [x19,2]
	ldrh	w0, [x22]
	cmp	w1, w0
	bcc	.L1852
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1852:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
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
	bne	.L1858
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
	b	.L1859
.L1858:
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x19,2]
	adrp	x21, :got:p_sys_data_buf
	ldrh	w22, [x20,x0]
	adrp	x20, :got:req_sys
	ldr	x0, [x20, #:got_lo12:req_sys]
	orr	w1, w1, w22, lsl 10
	str	w1, [x0,4]
	ldr	x3, [x21, #:got_lo12:p_sys_data_buf]
	ldr	x1, [x3]
	str	x1, [x0,8]
	adrp	x1, :got:p_sys_spare_buf
	ldr	x1, [x1, #:got_lo12:p_sys_spare_buf]
	ldr	x1, [x1]
	str	x1, [x0,16]
	ldr	w0, [x19,48]
	str	w0, [x1,4]
	mov	w0, -1291
	strh	w0, [x1,8]
	ldrh	w0, [x19,4]
	strh	w0, [x1]
	adrp	x0, :got:c_ftl_nand_page_pre_slc_blk
	strh	w22, [x1,2]
	mov	w1, 255
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w2, [x0]
	ldr	x0, [x3]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w2, 0
	mov	w1, w2
.L1860:
	ldrh	w0, [x19,6]
	cmp	w0, w1
	bls	.L1863
	ubfiz	x4, x1, 2, 16
	ldr	w0, [x23,x4]
	cmp	w22, w0, lsr 10
	bne	.L1861
	ldr	x3, [x21, #:got_lo12:p_sys_data_buf]
	add	w2, w2, 1
	uxth	w2, w2
	ldr	x5, [x3]
	ubfiz	x0, x2, 3, 16
	str	w1, [x5,x0]
	ldr	x3, [x3]
	ldr	w4, [x23,x4]
	add	x0, x3, x0
	str	w4, [x0,4]
.L1861:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1860
.L1863:
	mov	w1, 1
	ldr	x0, [x20, #:got_lo12:req_sys]
	mov	w2, w1
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	strh	w0, [x19,2]
	mov	x0, x19
	bl	ftl_map_blk_gc
.L1859:
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
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w21, w1
	mov	x22, x2
	adrp	x23, :got:g_totle_l2p_write_count
	adrp	x24, :got:c_ftl_nand_page_pre_slc_blk
	mov	w25, 65535
	adrp	x26, :got:req_sys
	adrp	x27, :got:p_sys_spare_buf
.L1870:
	ldr	x1, [x23, #:got_lo12:g_totle_l2p_write_count]
	ldr	w0, [x1]
	add	w0, w0, 1
	str	w0, [x1]
	ldr	x0, [x24, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w1, [x19,2]
	ldrh	w0, [x0]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L1865
	ldrh	w0, [x19]
	cmp	w0, w25
	bne	.L1866
.L1865:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L1866:
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19,16]
	ldr	x20, [x26, #:got_lo12:req_sys]
	ldrh	w28, [x0,x1,lsl 1]
	mov	w1, 0
	ldrh	w0, [x19,2]
	str	x22, [x20,8]
	orr	w0, w0, w28, lsl 10
	str	w0, [x20,4]
	ldr	x0, [x27, #:got_lo12:p_sys_spare_buf]
	ldr	x0, [x0]
	str	x0, [x20,16]
	bl	ftl_memset
	ldr	x0, [x20,16]
	ldr	w1, [x19,48]
	str	w1, [x0,4]
	strh	w21, [x0,8]
	ldrh	w1, [x19,4]
	strh	w1, [x0]
	mov	w1, 1
	strh	w28, [x0,2]
	mov	w2, w1
	mov	x0, x20
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	uxth	w0, w0
	strh	w0, [x19,2]
	cmp	w0, 1
	beq	.L1870
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L1870
	ldr	x0, [x19,40]
	ldr	w1, [x20,4]
	str	w1, [x0,w21,uxtw 2]
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
	adrp	x20, :got:p_l2p_ram_map
	ubfiz	x19, x0, 4, 16
	adrp	x0, :got:gL2pMapInfo
	ldr	x20, [x20, #:got_lo12:p_l2p_ram_map]
	ldr	x0, [x0, #:got_lo12:gL2pMapInfo]
	ldr	x1, [x20]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20]
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
	stp	x29, x30, [sp, -80]!
	mov	x4, 1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	mov	x21, x1
	adrp	x1, :got:c_ftl_nand_sec_pre_page_shift
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_sec_pre_page_shift]
	adrp	x23, :got:p_l2p_ram_map
	mov	w22, w2
	ldrh	w3, [x1]
	ldr	x1, [x23, #:got_lo12:p_l2p_ram_map]
	add	w3, w3, 7
	lsr	w20, w0, w3
	lsl	x3, x4, x3
	sub	w3, w3, #1
	ldr	x1, [x1]
	and	w0, w3, w0
	uxth	w20, w20
	mov	x4, 0
	uxth	x24, w0
	adrp	x0, :got:c_ftl_nand_l2pmap_ram_region_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldrh	w0, [x0]
.L1875:
	uxth	x19, w4
	cmp	w19, w0
	bcs	.L1887
	add	x4, x4, 1
	add	x2, x1, x4, lsl 4
	ldrh	w2, [x2,-16]
	cmp	w2, w20
	bne	.L1875
.L1876:
	cbnz	w22, .L1877
	ldr	x0, [x23, #:got_lo12:p_l2p_ram_map]
	ldr	x1, [x0]
	add	x1, x1, x19, lsl 4
	ldr	x0, [x1,8]
	ldr	w0, [x0,x24,lsl 2]
	str	w0, [x21]
	b	.L1878
.L1877:
	ldr	x2, [x23, #:got_lo12:p_l2p_ram_map]
	lsl	x0, x19, 4
	ldr	w3, [x21]
	ldr	x1, [x2]
	add	x1, x1, x0
	ldr	x1, [x1,8]
	str	w3, [x1,x24,lsl 2]
	ldr	x1, [x2]
	add	x0, x1, x0
	ldr	w1, [x0,4]
	orr	w1, w1, -2147483648
	str	w1, [x0,4]
	adrp	x0, :got:g_l2p_last_update_region_id
	ldr	x0, [x0, #:got_lo12:g_l2p_last_update_region_id]
	strh	w20, [x0]
.L1878:
	ldr	x23, [x23, #:got_lo12:p_l2p_ram_map]
	ldr	x0, [x23]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19,4]
	cmn	w0, #1
	beq	.L1884
	add	w0, w0, 1
	str	w0, [x19,4]
	b	.L1884
.L1887:
	bl	select_l2p_ram_region
	uxth	x19, w0
	ldr	x3, [x23, #:got_lo12:p_l2p_ram_map]
	ubfiz	x2, x19, 4, 16
	mov	w1, w0
	ldr	x3, [x3]
	add	x4, x3, x2
	ldrh	w3, [x3,x2]
	mov	w2, 65535
	cmp	w3, w2
	beq	.L1881
	ldr	w2, [x4,4]
	tbz	w2, #31, .L1881
	str	x1, [x29,72]
	bl	flush_l2p_region
	ldr	x1, [x29,72]
.L1881:
	mov	w0, w20
	bl	load_l2p_region
	b	.L1876
.L1884:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlReUsePrevPpa
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	lsr	w0, w1, 10
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	str	w1, [x29,76]
	bl	P2V_block_in_plane
	uxth	w4, w0
	adrp	x0, :got:p_valid_page_count_table
	ubfiz	x22, x4, 1, 16
	mov	x23, x0
	ldr	x1, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x2, [x1]
	ldrh	w1, [x2,x22]
	cbnz	w1, .L1889
	adrp	x0, :got:p_free_data_block_list_head
	ldr	x2, [x0, #:got_lo12:p_free_data_block_list_head]
	ldr	x19, [x2]
	cbz	x19, .L1890
	adrp	x2, :got:g_num_free_superblocks
	mov	x6, -6148914691236517206
	mov	x21, x2
	ldr	x3, [x2, #:got_lo12:g_num_free_superblocks]
	mov	w2, 6
	ldrh	w5, [x3]
	adrp	x3, :got:p_data_block_list_table
	ldr	x3, [x3, #:got_lo12:p_data_block_list_table]
	ldr	x3, [x3]
	sub	x19, x19, x3
	asr	x19, x19, 1
	madd	x19, x6, x19, x19
	mov	w6, 65535
	uxth	w19, w19
.L1891:
	cmp	w1, w5
	beq	.L1890
	cmp	w19, w4
	bne	.L1892
	ldr	x0, [x0, #:got_lo12:p_free_data_block_list_head]
	mov	w1, w19
	bl	List_remove_node
	ldr	x0, [x21, #:got_lo12:g_num_free_superblocks]
	ldrh	w1, [x0]
	sub	w1, w1, #1
	strh	w1, [x0]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x0, [x23, #:got_lo12:p_valid_page_count_table]
	ldr	x1, [x0]
	ldrh	w0, [x1,x22]
	add	w0, w0, 1
	strh	w0, [x1,x22]
	b	.L1890
.L1892:
	umull	x19, w19, w2
	ldrh	w19, [x3,x19]
	cmp	w19, w6
	beq	.L1890
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1891
.L1889:
	add	w1, w1, 1
	strh	w1, [x2,x22]
.L1890:
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
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	stp	x29, x30, [sp, -144]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	mov	x26, x0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	ldrh	w23, [x26,6]
	adrp	x20, :got:p_sys_data_buf
	ldr	x0, [x0,40]
	adrp	x19, :got:req_sys
	lsl	w2, w23, 2
	ldrh	w24, [x26,8]
	str	x0, [x29,136]
	mov	w27, 0
	ldr	x22, [x26,16]
	ldr	x25, [x26,24]
	bl	ftl_memset
	ldr	x0, [x20, #:got_lo12:p_sys_data_buf]
	ldr	x1, [x19, #:got_lo12:req_sys]
	ldr	x0, [x0]
	str	x0, [x1,8]
	adrp	x0, :got:p_sys_spare_buf
	ldr	x0, [x0, #:got_lo12:p_sys_spare_buf]
	str	wzr, [x26,52]
	str	wzr, [x26,48]
	ldr	x21, [x0]
	mov	w0, -1
	str	x21, [x1,16]
	strh	w0, [x26]
	strh	w0, [x26,2]
	mov	w0, 1
	str	w0, [x26,56]
	sub	w0, w24, #1
	str	w0, [x29,132]
.L1898:
	cmp	w27, w24
	bge	.L1915
	ldr	w0, [x29,132]
	sxtw	x28, w27
	cmp	w27, w0
	bne	.L1899
	lsl	x0, x28, 1
	mov	w1, 1
	add	x20, x22, x0
	ldrh	w0, [x22,x0]
	mov	w22, 0
	bl	FtlGetLastWrittenPage
	strh	w27, [x26]
	sxth	w1, w0
	add	w0, w0, 1
	strh	w0, [x26,2]
	ldr	w0, [x25,x28,lsl 2]
	add	w25, w1, 1
	str	w0, [x26,48]
	ldr	x0, [x19, #:got_lo12:req_sys]
	str	xzr, [x0,8]
.L1900:
	cmp	w22, w25
	bge	.L1915
	ldrh	w0, [x20]
	mov	w1, 1
	ldr	x24, [x19, #:got_lo12:req_sys]
	mov	w2, w1
	orr	w0, w22, w0, lsl 10
	str	w0, [x24,4]
	mov	x0, x24
	bl	FlashReadPages
	ldr	w0, [x24]
	cmn	w0, #1
	beq	.L1901
	ldrh	w1, [x21,8]
	cmp	w1, w23
	bcs	.L1901
	ldrh	w2, [x21]
	ldrh	w0, [x26,4]
	cmp	w2, w0
	bne	.L1901
	ubfiz	x1, x1, 2, 16
	ldr	x2, [x29,136]
	ldr	w0, [x24,4]
	str	w0, [x2,x1]
.L1901:
	add	w22, w22, 1
	sxth	w22, w22
	b	.L1900
.L1915:
	mov	x0, x26
	bl	ftl_free_no_use_map_blk
	adrp	x0, :got:c_ftl_nand_page_pre_slc_blk
	ldrh	w1, [x26,2]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w0, [x0]
	cmp	w1, w0
	bne	.L1904
	mov	x0, x26
	bl	ftl_map_blk_alloc_new_blk
	b	.L1904
.L1899:
	ldr	x0, [x20, #:got_lo12:p_sys_data_buf]
	lsl	x1, x28, 1
	ldr	x6, [x19, #:got_lo12:req_sys]
	add	x28, x22, x1
	str	x6, [x29,120]
	ldr	x0, [x0]
	str	x0, [x6,8]
	adrp	x0, :got:c_ftl_nand_page_pre_slc_blk
	ldrh	w1, [x22,x1]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w0, [x0]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x6,4]
	mov	w2, w1
	mov	x0, x6
	bl	FlashReadPages
	ldr	x6, [x29,120]
	adrp	x8, :got:c_ftl_nand_page_pre_slc_blk
	ldr	w0, [x6]
	cmn	w0, #1
	beq	.L1905
	ldrh	w1, [x21]
	ldrh	w0, [x26,4]
	cmp	w1, w0
	bne	.L1905
	ldrh	w0, [x21,8]
	mov	w1, 64245
	cmp	w0, w1
	bne	.L1905
	mov	w0, 0
.L1906:
	ldr	x1, [x8, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w1, [x1]
	sub	w1, w1, #1
	cmp	w0, w1
	bge	.L1909
	ldr	x1, [x20, #:got_lo12:p_sys_data_buf]
	sbfiz	x2, x0, 3, 32
	ldr	x6, [x1]
	ldrh	w1, [x6,x2]
	cmp	w1, w23
	bcs	.L1907
	add	x2, x6, x2
	ubfiz	x1, x1, 2, 16
	ldr	x3, [x29,136]
	ldr	w2, [x2,4]
	str	w2, [x3,x1]
.L1907:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1906
.L1905:
	ldr	x0, [x19, #:got_lo12:req_sys]
	mov	w6, 0
	str	xzr, [x0,8]
.L1910:
	ldr	x0, [x8, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w0, [x0]
	cmp	w6, w0
	bge	.L1909
	ldrh	w0, [x28]
	mov	w1, 1
	ldr	x7, [x19, #:got_lo12:req_sys]
	mov	w2, w1
	str	x8, [x29,104]
	orr	w0, w6, w0, lsl 10
	str	x6, [x29,112]
	str	w0, [x7,4]
	mov	x0, x7
	str	x7, [x29,120]
	bl	FlashReadPages
	ldr	x7, [x29,120]
	ldr	x6, [x29,112]
	ldr	x8, [x29,104]
	ldr	w0, [x7]
	cmn	w0, #1
	beq	.L1911
	ldrh	w1, [x21,8]
	cmp	w1, w23
	bcs	.L1911
	ldrh	w2, [x21]
	ldrh	w0, [x26,4]
	cmp	w2, w0
	bne	.L1911
	ubfiz	x1, x1, 2, 16
	ldr	x2, [x29,136]
	ldr	w0, [x7,4]
	str	w0, [x2,x1]
.L1911:
	add	w6, w6, 1
	sxth	w6, w6
	b	.L1910
.L1909:
	add	w4, w27, 1
	sxth	w27, w4
	b	.L1898
.L1904:
	ldrh	w1, [x26,8]
	ldrh	w0, [x26,10]
	cmp	w1, w0
	bcc	.L1916
	mov	x0, x26
	bl	ftl_map_blk_gc
.L1916:
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
	adrp	x1, :got:c_ftl_nand_max_vendor_blks
	adrp	x0, :got:gVendorBlkInfo
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_max_vendor_blks]
	ldr	x0, [x0, #:got_lo12:gVendorBlkInfo]
	ldrh	w1, [x1]
	strh	w1, [x0,10]
	mov	w1, -3962
	strh	w1, [x0,4]
	adrp	x1, :got:g_totle_vendor_block
	ldr	x1, [x1, #:got_lo12:g_totle_vendor_block]
	ldrh	w1, [x1]
	strh	w1, [x0,8]
	adrp	x1, :got:c_ftl_nand_vendor_region_num
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_vendor_region_num]
	ldrh	w1, [x1]
	strh	w1, [x0,6]
	adrp	x1, :got:p_vendor_block_table
	ldr	x1, [x1, #:got_lo12:p_vendor_block_table]
	ldr	x1, [x1]
	str	x1, [x0,16]
	adrp	x1, :got:p_vendor_block_ver_table
	ldr	x1, [x1, #:got_lo12:p_vendor_block_ver_table]
	ldr	x1, [x1]
	str	x1, [x0,24]
	adrp	x1, :got:p_vendor_block_valid_page_count
	ldr	x1, [x1, #:got_lo12:p_vendor_block_valid_page_count]
	ldr	x1, [x1]
	str	x1, [x0,32]
	adrp	x1, :got:p_vendor_region_ppn_table
	ldr	x1, [x1, #:got_lo12:p_vendor_region_ppn_table]
	ldr	x1, [x1]
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
	adrp	x0, :got:gL2pMapInfo
	ldr	x0, [x0, #:got_lo12:gL2pMapInfo]
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:g_sys_ext_data
	str	x23, [sp,48]
	stp	x21, x22, [sp,32]
	ldr	x1, [x19, #:got_lo12:g_sys_ext_data]
	ldr	w0, [x1,80]
	cbz	w0, .L1930
	adrp	x0, :got:g_MaxLpn
	ldr	w3, [x1,84]
	mov	x20, x0
	ldr	x2, [x0, #:got_lo12:g_MaxLpn]
	ldr	w2, [x2]
	cmp	w3, w2
	bcs	.L1931
	mov	w21, 2048
	add	x23, x29, 68
.L1936:
	ldr	x22, [x19, #:got_lo12:g_sys_ext_data]
	ldr	x1, [x20, #:got_lo12:g_MaxLpn]
	ldr	w0, [x22,84]
	ldr	w1, [x1]
	cmp	w0, w1
	bcs	.L1933
	mov	w2, 0
	mov	x1, x23
	bl	log2phys
	ldr	w0, [x22,84]
	ldr	w2, [x29,68]
	add	w0, w0, 1
	str	w0, [x22,84]
	cmn	w2, #1
	beq	.L1934
	str	w2, [x29,76]
	mov	w1, 1
	str	w0, [x29,96]
	mov	w2, 0
	add	x0, x29, 72
	str	xzr, [x29,80]
	str	xzr, [x29,88]
	str	wzr, [x29,72]
	bl	FlashReadPages
	ldr	w0, [x29,72]
	cmp	w0, 256
	bne	.L1933
	ldr	w0, [x29,68]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1933
.L1934:
	subs	w21, w21, #1
	bne	.L1936
.L1933:
	mov	w0, -1
	b	.L1938
.L1931:
	adrp	x0, :got:g_totle_read_page_count
	str	wzr, [x1,80]
	str	wzr, [x1,84]
	ldr	x0, [x0, #:got_lo12:g_totle_read_page_count]
	ldr	w0, [x0]
	str	w0, [x1,76]
	b	.L1942
.L1930:
	adrp	x2, :got:g_max_erase_count
	ldr	w1, [x1,76]
	ldr	x2, [x2, #:got_lo12:g_max_erase_count]
	ldr	w3, [x2]
	adrp	x2, :got:g_inkDie_check_enable
	ldr	x2, [x2, #:got_lo12:g_inkDie_check_enable]
	ldr	w4, [x2]
	adrp	x2, :got:g_totle_read_page_count
	ldr	x2, [x2, #:got_lo12:g_totle_read_page_count]
	ldr	w2, [x2]
	add	w5, w2, 1048576
	cmp	w1, w5
	bhi	.L1939
	add	w3, w4, w3, lsr 10
	mov	w4, 33554432
	asr	w3, w4, w3
	add	w1, w3, w1
	cmp	w1, w2
	bcs	.L1938
.L1939:
	ldr	x0, [x19, #:got_lo12:g_sys_ext_data]
	mov	w1, 1
	str	wzr, [x0,84]
	str	w1, [x0,80]
	str	w2, [x0,76]
.L1942:
	mov	w0, 0
.L1938:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 128
	ret
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	mov	w26, w0
	adrp	x0, :got:c_ftl_nand_sec_pre_page_shift
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sec_pre_page_shift]
	mov	w25, w1
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	add	w1, w26, w1
	mov	x23, x2
	ldrh	w20, [x0]
	adrp	x0, :got:c_ftl_vendor_part_size
	mov	w19, -1
	ldr	x0, [x0, #:got_lo12:c_ftl_vendor_part_size]
	ldrh	w0, [x0]
	cmp	w1, w0
	bhi	.L1944
	lsr	w20, w26, w20
	mov	w19, 0
	adrp	x24, :got:p_vendor_data_buf
	add	x22, x29, 120
	adrp	x28, :got:gVendorBlkInfo
.L1945:
	cbz	w25, .L1944
	adrp	x0, :got:p_vendor_region_ppn_table
	ldr	x0, [x0, #:got_lo12:p_vendor_region_ppn_table]
	ldr	x0, [x0]
	ldr	w2, [x0,w20,uxtw 2]
	adrp	x0, :got:c_ftl_nand_sec_pre_page
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w1, [x0]
	uxth	w0, w25
	udiv	w21, w26, w1
	msub	w21, w21, w1, w26
	sub	w3, w1, w21
	uxth	w27, w3
	cmp	w27, w25
	csel	w27, w0, w27, hi
	cmp	w27, w1
	beq	.L1947
	cbz	w2, .L1947
	ldr	x0, [x24, #:got_lo12:p_vendor_data_buf]
	mov	w1, 1
	str	w2, [x22,4]
	mov	w2, w1
	str	xzr, [x22,16]
	ldr	x0, [x0]
	str	x0, [x22,8]
	mov	x0, x22
	bl	FlashReadPages
	b	.L1948
.L1947:
	adrp	x1, :got:c_ftl_nand_byte_pre_page
	ldr	x0, [x24, #:got_lo12:p_vendor_data_buf]
	ldr	x2, [x1, #:got_lo12:c_ftl_nand_byte_pre_page]
	mov	w1, 0
	ldr	x0, [x0]
	ldrh	w2, [x2]
	bl	ftl_memset
.L1948:
	ldr	x7, [x24, #:got_lo12:p_vendor_data_buf]
	lsl	w6, w27, 9
	ubfiz	x21, x21, 9, 16
	mov	w2, w6
	mov	x1, x23
	str	x6, [x29,104]
	ldr	x0, [x7]
	sub	w25, w25, w27
	str	x7, [x29,96]
	add	w26, w26, w27
	add	x0, x0, x21
	bl	ftl_memcpy
	ldr	x7, [x29,96]
	mov	w1, w20
	ldr	x0, [x28, #:got_lo12:gVendorBlkInfo]
	add	w20, w20, 1
	ldr	x2, [x7]
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	x6, [x29,104]
	csinv	w19, w19, wzr, ne
	add	x23, x23, x6, sxtw
	b	.L1945
.L1944:
	mov	w0, w19
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
	adrp	x2, :got:g_sys_ext_data
	mov	w0, 19539
	stp	x29, x30, [sp, -16]!
	movk	w0, 0x4654, lsl 16
	add	x29, sp, 0
	ldr	x2, [x2, #:got_lo12:g_sys_ext_data]
	ldr	w1, [x2]
	cmp	w1, w0
	bne	.L1958
	mov	w0, 39
	mov	w1, 1
	movk	w0, 0x5000, lsl 16
	str	w0, [x2,4]
	adrp	x0, :got:g_totle_write_sector
	ldr	x0, [x0, #:got_lo12:g_totle_write_sector]
	ldr	w0, [x0]
	str	w0, [x2,88]
	adrp	x0, :got:g_totle_read_sector
	ldr	x0, [x0, #:got_lo12:g_totle_read_sector]
	ldr	w0, [x0]
	str	w0, [x2,92]
	adrp	x0, :got:g_totle_gc_page_count
	ldr	x0, [x0, #:got_lo12:g_totle_gc_page_count]
	ldr	w0, [x0]
	str	w0, [x2,8]
	adrp	x0, :got:g_totle_write_page_count
	ldr	x0, [x0, #:got_lo12:g_totle_write_page_count]
	ldr	w0, [x0]
	str	w0, [x2,12]
	adrp	x0, :got:g_totle_read_page_count
	ldr	x0, [x0, #:got_lo12:g_totle_read_page_count]
	ldr	w0, [x0]
	str	w0, [x2,16]
	adrp	x0, :got:g_totle_l2p_write_count
	ldr	x0, [x0, #:got_lo12:g_totle_l2p_write_count]
	ldr	w0, [x0]
	str	w0, [x2,20]
	adrp	x0, :got:g_totle_slc_erase_count
	ldr	x0, [x0, #:got_lo12:g_totle_slc_erase_count]
	ldr	w0, [x0]
	str	w0, [x2,28]
	adrp	x0, :got:g_totle_sys_slc_erase_count
	ldr	x0, [x0, #:got_lo12:g_totle_sys_slc_erase_count]
	ldr	w0, [x0]
	str	w0, [x2,32]
	adrp	x0, :got:g_totle_discard_page_count
	ldr	x0, [x0, #:got_lo12:g_totle_discard_page_count]
	ldr	w0, [x0]
	str	w0, [x2,36]
	adrp	x0, :got:g_totle_cache_write_count
	ldr	x0, [x0, #:got_lo12:g_totle_cache_write_count]
	ldr	w0, [x0]
	str	w0, [x2,40]
	adrp	x0, :got:g_max_erase_count
	ldr	x0, [x0, #:got_lo12:g_max_erase_count]
	ldr	w0, [x0]
	str	w0, [x2,44]
	adrp	x0, :got:g_min_erase_count
	ldr	x0, [x0, #:got_lo12:g_min_erase_count]
	ldr	w0, [x0]
	str	w0, [x2,48]
	adrp	x0, :got:g_all_blk_used_slc_mode
	ldr	x0, [x0, #:got_lo12:g_all_blk_used_slc_mode]
	ldr	w0, [x0]
	str	w0, [x2,60]
	adrp	x0, :got:g_SlcPartLbaEndSector
	ldr	x0, [x0, #:got_lo12:g_SlcPartLbaEndSector]
	ldr	w0, [x0]
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L1958:
	ldp	x29, x30, [sp], 16
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x1, :got:g_inkDie_check_enable
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1, #:got_lo12:g_inkDie_check_enable]
	ldr	w2, [x1]
	mov	w1, 32
	cbz	w2, .L1961
	adrp	x2, :got:g_min_erase_count
	ldr	x2, [x2, #:got_lo12:g_min_erase_count]
	ldr	w2, [x2]
	cmp	w2, 29
	mov	w2, 4
	csel	w1, w1, w2, hi
.L1961:
	adrp	x2, :got:g_ect_tbl_power_up_flush
	ldr	x2, [x2, #:got_lo12:g_ect_tbl_power_up_flush]
	ldrh	w3, [x2]
	cmp	w3, 31
	bhi	.L1962
	add	w3, w3, 1
	mov	w1, 1
	strh	w3, [x2]
.L1962:
	adrp	x2, :got:gp_ect_tbl_info
	cbnz	w0, .L1963
	ldr	x0, [x2, #:got_lo12:gp_ect_tbl_info]
	ldr	x3, [x0]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L1964
.L1963:
	ldr	x2, [x2, #:got_lo12:gp_ect_tbl_info]
	ldr	x0, [x2]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	adrp	x1, :got:g_ect_tbl_info_size
	ldr	x0, [x2]
	ldr	x1, [x1, #:got_lo12:g_ect_tbl_info_size]
	str	wzr, [x0,4]
	ldrh	w1, [x1]
	ldr	x2, [x2]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L1964:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlEctTblFlush, .-FtlEctTblFlush
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
	cbz	w1, .L1968
	str	wzr, [x0,56]
	mov	x21, x0
	adrp	x0, :got:p_sys_data_buf_1
	adrp	x2, :got:req_sys
	adrp	x3, :got:p_sys_spare_buf
	sub	w20, w20, #1
	mov	x19, x2
	ldr	x0, [x0, #:got_lo12:p_sys_data_buf_1]
	uxth	w20, w20
	ldr	x1, [x2, #:got_lo12:req_sys]
	ldr	x0, [x0]
	str	x0, [x1,8]
	ldr	x3, [x3, #:got_lo12:p_sys_spare_buf]
	ldr	x3, [x3]
	str	x3, [x1,16]
	ubfiz	x3, x20, 2, 16
	ldr	w3, [x4,x3]
	str	w3, [x1,4]
	cbz	w3, .L1970
	mov	x0, x1
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L1971
.L1970:
	adrp	x2, :got:c_ftl_nand_byte_pre_page
	mov	w1, 255
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldrh	w2, [x2]
	bl	ftl_memset
.L1971:
	ldr	x2, [x19, #:got_lo12:req_sys]
	mov	x0, x21
	mov	w1, w20
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L1968:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	ldrh	w25, [x0,2]
	ldrb	w22, [x0,6]
	adrp	x0, :got:c_ftl_nand_page_pre_blk
	str	x0, [x29,160]
	ldr	x1, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w1, [x1]
	cmp	w1, w25
	bne	.L1976
	strh	wzr, [x19,4]
	b	.L2119
.L1976:
	ldrh	w0, [x19,16]
	mov	w1, 0
	mov	w2, 65535
.L1978:
	cmp	w0, w2
	bne	.L2122
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L1978
.L2122:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L1980
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L1981
	adrp	x0, :got:slcPageToMlcPageTbl
	ldr	x0, [x0, #:got_lo12:slcPageToMlcPageTbl]
	ldrh	w23, [x0,w20,sxtw 1]
	b	.L1982
.L1980:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w20, w0
	beq	.L1981
	mov	w23, w0
.L1982:
	adrp	x1, :got:c_ftl_nand_planes_num
	adrp	x0, :got:c_ftl_nand_byte_pre_oob
	mov	w26, 0
	str	x1, [x29,192]
	mov	w7, 65535
	mov	w8, 56
	ldr	x2, [x1, #:got_lo12:c_ftl_nand_planes_num]
	adrp	x9, :got:req_read
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_byte_pre_oob]
	mov	w10, 4
	adrp	x11, :got:p_gc_spare_buf
	ldrh	w3, [x2]
	mov	x2, x19
	ldrh	w6, [x0]
	add	x3, x19, x3, lsl 1
	b	.L1983
.L1981:
	strh	wzr, [x19,2]
.L2119:
	strb	wzr, [x19,6]
	b	.L1977
.L1985:
	ldrh	w1, [x2,16]
	cmp	w1, w7
	beq	.L1984
	ldr	x5, [x9, #:got_lo12:req_read]
	orr	w1, w23, w1, lsl 10
	umull	x0, w26, w8
	ldr	x4, [x5]
	add	x4, x4, x0
	str	w1, [x4,4]
	ldr	x1, [x5]
	add	x0, x1, x0
	mul	w1, w26, w6
	add	w26, w26, 1
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	uxth	w26, w26
	ldr	x4, [x11, #:got_lo12:p_gc_spare_buf]
	ldr	x4, [x4]
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L1984:
	add	x2, x2, 2
.L1983:
	cmp	x2, x3
	bne	.L1985
	adrp	x21, :got:req_read
	mov	w1, w26
	mov	w2, 0
	mov	w24, 65535
	ldr	x0, [x21, #:got_lo12:req_read]
	ldr	x0, [x0]
	bl	FlashReadPages
	adrp	x0, :got:g_GlobalDataVersion
	mov	x3, 0
	str	x0, [x29,152]
	mov	x6, 56
	uxth	w7, w23
	ldr	x1, [x0, #:got_lo12:g_GlobalDataVersion]
	ldr	w27, [x1]
.L1986:
	uxth	w28, w3
	cmp	w28, w26
	bcs	.L1995
	ldr	x0, [x21, #:got_lo12:req_read]
	mul	x2, x3, x6
	ldr	x0, [x0]
	add	x1, x0, x2
	ldr	w0, [x0,x2]
	cbnz	w0, .L1987
	ldr	x5, [x1,16]
	str	x5, [x29,168]
	ldr	w0, [x5,4]
	cmn	w0, #1
	beq	.L1989
	ldr	x1, [x29,152]
	str	x7, [x29,176]
	str	x6, [x29,184]
	ldr	x4, [x1, #:got_lo12:g_GlobalDataVersion]
	str	x2, [x29,200]
	str	x3, [x29,208]
	ldr	w1, [x4]
	str	x4, [x29,216]
	bl	ftl_cmp_data_ver
	ldr	x4, [x29,216]
	ldr	x3, [x29,208]
	ldr	x2, [x29,200]
	ldr	x6, [x29,184]
	ldr	x7, [x29,176]
	ldr	x5, [x29,168]
	cbz	w0, .L1989
	ldr	w0, [x5,4]
	add	w0, w0, 1
	str	w0, [x4]
.L1989:
	ldr	x0, [x21, #:got_lo12:req_read]
	ldr	x0, [x0]
	add	x2, x0, x2
	ldr	x0, [x2,16]
	ldr	w0, [x0]
	cmn	w0, #1
	bne	.L1991
.L1995:
	cmp	w28, w26
	bne	.L2114
	ldr	x0, [x21, #:got_lo12:req_read]
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x0, [x0]
	ldr	w0, [x0,4]
	b	.L2115
.L1987:
	mov	w24, w7
.L1991:
	add	x3, x3, 1
	b	.L1986
.L2114:
	mov	w0, 56
	uxth	w20, w20
	umull	x28, w28, w0
	ldr	x0, [x21, #:got_lo12:req_read]
	ldr	x0, [x0]
	add	x28, x0, x28
	ldr	w0, [x28,4]
.L2115:
	lsr	w0, w0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L1997
	adrp	x0, :got:slcPageToMlcPageTbl
	ldr	x0, [x0, #:got_lo12:slcPageToMlcPageTbl]
	ldrh	w20, [x0,w20,sxtw 1]
.L1997:
	ldr	x0, [x29,160]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w0, [x0]
	cmp	w0, w20
	bne	.L1998
	strh	w20, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L1998:
	uxth	w0, w22
	cmp	w20, w25
	str	w0, [x29,184]
	bne	.L1999
	cmp	w26, w0
	beq	.L2120
.L1999:
	mov	w0, 65535
	sub	w22, w27, #1
	cmp	w24, w0
	bne	.L2000
	cbnz	w1, .L2001
.L2000:
	adrp	x24, :got:g_recovery_page_min_ver
	uxth	w5, w23
	ldr	x0, [x24, #:got_lo12:g_recovery_page_min_ver]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L2002
	str	w22, [x0]
.L2002:
	ldr	x0, [x24, #:got_lo12:g_recovery_page_min_ver]
	mov	w27, w25
	ldr	w6, [x0]
	add	w0, w25, 7
	cmp	w0, w23, uxth
	bge	.L2003
	sub	w27, w5, #7
	uxth	w27, w27
.L2003:
	mov	w3, -1
	mov	w4, 65535
	mov	w28, w3
	mov	w7, 56
	adrp	x8, :got:mlcPageToSlcPageTbl
.L2004:
	cmp	w27, w5
	bhi	.L2017
	ldr	x0, [x29,192]
	mov	w23, 0
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w2, [x0]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2018:
	cmp	x0, x2
	beq	.L2123
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2005
	ldr	x10, [x21, #:got_lo12:req_read]
	orr	w1, w27, w1, lsl 10
	umull	x9, w23, w7
	add	w23, w23, 1
	ldr	x10, [x10]
	uxth	w23, w23
	add	x9, x10, x9
	str	w1, [x9,4]
.L2005:
	add	x0, x0, 2
	b	.L2018
.L2123:
	ldr	x9, [x21, #:got_lo12:req_read]
	mov	w1, w23
	mov	w2, 0
	str	x8, [x29,136]
	str	x7, [x29,144]
	ldr	x0, [x9]
	str	x4, [x29,168]
	str	x6, [x29,176]
	str	x5, [x29,200]
	str	x3, [x29,208]
	str	x9, [x29,216]
	bl	FlashReadPages
	ldr	x9, [x29,216]
	mov	w1, 0
	ldr	x3, [x29,208]
	sxtw	x10, w27
	ldr	x5, [x29,200]
	ldr	x0, [x9]
	ldr	x6, [x29,176]
	ldr	x4, [x29,168]
	add	x0, x0, 16
	ldr	x7, [x29,144]
	ldr	x8, [x29,136]
.L2007:
	cmp	w1, w23
	beq	.L2124
	ldr	w2, [x0,-16]
	cbnz	w2, .L2008
	ldr	x2, [x0]
	ldrh	w9, [x2]
	cmp	w9, w4
	beq	.L2009
	ldr	w9, [x2,4]
	cmn	w9, #1
	beq	.L2009
	ldr	x2, [x24, #:got_lo12:g_recovery_page_min_ver]
	cmn	w3, #1
	ldr	w28, [x2]
	str	w9, [x2]
	bne	.L2009
	ldr	x2, [x8, #:got_lo12:mlcPageToSlcPageTbl]
	ldrh	w2, [x2,x10,lsl 1]
	cmp	w2, w4
	beq	.L2009
	cmp	w28, w22
	csinv	w3, w28, wzr, ne
	b	.L2009
.L2008:
	ldrb	w0, [x19,8]
	cbnz	w0, .L2001
	adrp	x0, :got:mlcPageToSlcPageTbl
	ldr	x0, [x0, #:got_lo12:mlcPageToSlcPageTbl]
	ldrh	w1, [x0,w27,sxtw 1]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2012
	cmn	w3, #1
	ldr	x24, [x24, #:got_lo12:g_recovery_page_min_ver]
	beq	.L2013
	str	w3, [x24]
	b	.L2001
.L2013:
	cmp	w6, w22
	beq	.L2014
	str	w6, [x24]
	b	.L2001
.L2014:
	ldr	w0, [x24]
	b	.L2121
.L2012:
	cmp	w28, w22
	beq	.L2015
	cmn	w28, #1
	beq	.L2001
	ldr	x24, [x24, #:got_lo12:g_recovery_page_min_ver]
	str	w28, [x24]
	b	.L2001
.L2015:
	ldr	x24, [x24, #:got_lo12:g_recovery_page_min_ver]
	ldr	w0, [x24]
	cmp	w0, w22
	beq	.L2001
.L2121:
	sub	w0, w0, #1
	b	.L2116
.L2009:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2007
.L2124:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L2004
.L2017:
	ldr	x24, [x24, #:got_lo12:g_recovery_page_min_ver]
	mov	w0, -1
.L2116:
	str	w0, [x24]
.L2001:
	adrp	x0, :got:g_power_lost_recovery_flag
	mov	w1, 1
	mov	w23, w25
	ldr	x0, [x0, #:got_lo12:g_power_lost_recovery_flag]
	strh	w1, [x0]
	adrp	x0, :got:gL2pMapInfo
	ldr	x0, [x0, #:got_lo12:gL2pMapInfo]
	bl	FtlMapBlkWriteDumpData
	add	x0, x29, 232
	str	x0, [x29,144]
	adrp	x0, .LC111
	add	x0, x0, :lo12:.LC111
	str	x0, [x29,128]
	add	x0, x29, 236
	str	x0, [x29,120]
	add	x0, x29, 228
	str	x0, [x29,112]
.L2019:
	ldr	x0, [x29,192]
	mov	w4, 65535
	str	wzr, [x29,216]
	mov	w5, 56
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w2, [x0]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2020:
	cmp	x0, x2
	beq	.L2125
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2021
	ldr	w3, [x29,216]
	orr	w1, w23, w1, lsl 10
	ldr	x6, [x21, #:got_lo12:req_read]
	umull	x3, w3, w5
	ldr	x6, [x6]
	add	x3, x6, x3
	str	w1, [x3,4]
	ldr	w1, [x29,216]
	add	w1, w1, 1
	uxth	w1, w1
	str	w1, [x29,216]
.L2021:
	add	x0, x0, 2
	b	.L2020
.L2125:
	ldr	x0, [x21, #:got_lo12:req_read]
	mov	w2, 0
	ldr	w1, [x29,216]
	ldr	x0, [x0]
	bl	FlashReadPages
	str	xzr, [x29,200]
.L2023:
	ldrh	w0, [x29,200]
	ldr	w1, [x29,216]
	cmp	w1, w0, uxth
	bls	.L2126
	ldr	x0, [x29,200]
	mov	x1, 56
	mul	x24, x0, x1
	ldr	x0, [x21, #:got_lo12:req_read]
	ldr	x0, [x0]
	add	x0, x0, x24
	ldr	w0, [x0,4]
	str	w0, [x29,236]
	lsr	w0, w0, 10
	bl	P2V_plane
	uxth	w0, w0
	cmp	w23, w25
	bcc	.L2024
	ldr	w1, [x29,184]
	cmp	w0, w1
	bcs	.L2056
	cmp	w23, w25
	beq	.L2024
.L2056:
	cmp	w0, w26
	bne	.L2057
	cmp	w23, w20
	beq	.L2026
.L2057:
	ldr	x0, [x21, #:got_lo12:req_read]
	ldr	x0, [x0]
	add	x1, x0, x24
	ldr	w0, [x0,x24]
	cmn	w0, #1
	beq	.L2028
	ldr	x28, [x1,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	bne	.L2037
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2031
	ldr	x0, [x29,152]
	ldr	x27, [x0, #:got_lo12:g_GlobalDataVersion]
	mov	w0, w22
	ldr	w1, [x27]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2031
	ldr	w0, [x28,4]
	add	w0, w0, 1
	str	w0, [x27]
.L2031:
	ldr	w27, [x28,8]
	mov	w2, 0
	ldr	w0, [x28,12]
	ldr	x1, [x29,144]
	str	w0, [x29,228]
	mov	w0, w27
	bl	log2phys
	adrp	x0, :got:g_recovery_page_min_ver
	str	x0, [x29,136]
	ldr	x1, [x0, #:got_lo12:g_recovery_page_min_ver]
	str	x1, [x29,168]
	ldr	w1, [x1]
	cmn	w1, #1
	beq	.L2033
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2033
	ldr	w1, [x29,228]
	cmn	w1, #1
	beq	.L2034
	ldr	x3, [x21, #:got_lo12:req_read]
	str	x3, [x29,104]
	ldr	x0, [x3]
	add	x0, x0, x24
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x2, [x0,16]
	ldr	x0, [x3]
	str	x2, [x29,208]
	mov	w2, 0
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x0, [x29,208]
	ldr	x3, [x29,104]
	ldr	w0, [x0,4]
	str	w0, [x29,176]
	ldr	x0, [x3]
	ldr	w0, [x0,x24]
	cmn	w0, #1
	bne	.L2035
	b	.L2036
.L2034:
	ldr	w0, [x29,236]
	ldr	w1, [x29,232]
	cmp	w1, w0
	bne	.L2037
	ldr	x1, [x29,112]
	mov	w0, w27
	mov	w2, 1
	bl	log2phys
.L2037:
	ldrh	w0, [x19]
	b	.L2118
.L2035:
	ldr	x0, [x29,208]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2036
	ldr	x0, [x29,168]
	ldr	w1, [x29,176]
	str	x3, [x29,104]
	ldr	w0, [x0]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29,104]
	cbz	w0, .L2036
	ldr	w0, [x29,232]
	ldr	w1, [x29,236]
	cmp	w0, w1
	beq	.L2042
	ldr	w1, [x29,228]
	cmp	w0, w1
	beq	.L2036
	cmn	w0, #1
	ldr	x1, [x3]
	beq	.L2040
	add	x1, x1, x24
	str	w0, [x1,4]
	ldr	x2, [x1,16]
	mov	w1, 1
	ldr	x0, [x3]
	str	x2, [x29,208]
	mov	w2, 0
	add	x0, x0, x24
	bl	FlashReadPages
	b	.L2041
.L2040:
	str	w0, [x1,x24]
.L2041:
	ldr	x0, [x21, #:got_lo12:req_read]
	ldr	x0, [x0]
	ldr	w0, [x0,x24]
	cmn	w0, #1
	beq	.L2042
	ldr	x0, [x29,208]
	ldr	w24, [x0,4]
	ldr	x0, [x29,136]
	mov	w1, w24
	ldr	x0, [x0, #:got_lo12:g_recovery_page_min_ver]
	ldr	w0, [x0]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2042
	ldr	w0, [x29,176]
	mov	w1, w24
	bl	ftl_cmp_data_ver
	cbz	w0, .L2036
.L2042:
	ldr	w1, [x29,228]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2036:
	mov	w0, -1
	str	w0, [x29,228]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2044
.L2033:
	ldr	w0, [x29,236]
	ldr	w1, [x29,232]
	cmp	w1, w0
	beq	.L2044
	ldr	x1, [x29,120]
	mov	w0, w27
	mov	w2, 1
	bl	log2phys
	ldr	w0, [x29,232]
	cmn	w0, #1
	beq	.L2044
	ldr	w1, [x29,228]
	cmp	w0, w1
	beq	.L2044
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	adrp	x1, :got:g_active_superblock
	ldr	x1, [x1, #:got_lo12:g_active_superblock]
	ldrh	w1, [x1]
	cmp	w1, w0
	beq	.L2047
	adrp	x1, :got:g_buffer_superblock
	ldr	x1, [x1, #:got_lo12:g_buffer_superblock]
	ldrh	w1, [x1]
	cmp	w1, w0
	beq	.L2047
	adrp	x1, :got:g_gc_temp_superblock
	ldr	x1, [x1, #:got_lo12:g_gc_temp_superblock]
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L2044
.L2047:
	ldr	x24, [x21, #:got_lo12:req_read]
	mov	w2, 0
	ldr	w1, [x29,232]
	ldr	x0, [x24]
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x28, [x0,16]
	ldr	x0, [x24]
	bl	FlashReadPages
	ldr	x0, [x24]
	ldr	w1, [x28,4]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2044
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2044
	ldr	x1, [x29,144]
	mov	w0, w27
	mov	w2, 1
	bl	log2phys
.L2044:
	ldr	w0, [x29,228]
	cmn	w0, #1
	beq	.L2024
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	adrp	x2, :got:p_valid_page_count_table
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2, #:got_lo12:p_valid_page_count_table]
	ldr	x2, [x2]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2048
.L2118:
	bl	decrement_vpc_count
	b	.L2024
.L2048:
	ldr	x0, [x29,128]
	bl	printk
	b	.L2024
.L2028:
	adrp	x1, :got:g_recovery_page_num
	ldr	x1, [x1, #:got_lo12:g_recovery_page_num]
	ldr	w0, [x1]
	cmp	w0, 31
	bhi	.L2049
	adrp	x2, :got:g_recovery_ppa_tbl
	ldr	w3, [x29,236]
	ldr	x2, [x2, #:got_lo12:g_recovery_ppa_tbl]
	str	w3, [x2,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x1]
.L2049:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	adrp	x0, :got:g_recovery_page_min_ver
	ldr	x0, [x0, #:got_lo12:g_recovery_page_min_ver]
	ldr	w1, [x0]
	cmn	w1, #1
	beq	.L2117
	cmp	w1, w22
	bls	.L2024
.L2117:
	str	w22, [x0]
.L2024:
	ldr	x0, [x29,200]
	add	x0, x0, 1
	str	x0, [x29,200]
	b	.L2023
.L2126:
	ldr	x0, [x29,160]
	add	w23, w23, 1
	uxth	w23, w23
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w0, [x0]
	cmp	w23, w0
	bne	.L2019
	ldr	x0, [x29,192]
	mov	w3, 65535
	strh	w23, [x19,2]
	strh	wzr, [x19,4]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w2, [x0]
	mov	x0, 0
.L2052:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L1977
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2052
	strb	w1, [x19,6]
	b	.L1977
.L2026:
	strb	w26, [x19,6]
	strh	w20, [x19,2]
.L2120:
	mov	x0, x19
	mov	w1, w20
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L1977:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 240
	ret
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:g_active_superblock
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x0, [x19, #:got_lo12:g_active_superblock]
	stp	x25, x26, [sp,64]
	ldrh	w3, [x0,4]
	cbz	w3, .L2128
	ldrb	w1, [x0,8]
	cbnz	w1, .L2128
	adrp	x2, :got:c_ftl_nand_page_pre_blk
	ldrb	w1, [x0,7]
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w2, [x2]
	mul	w1, w1, w2
	cmp	w3, w1
	beq	.L2128
	adrp	x1, :got:g_MaxLpn
	ldrb	w23, [x0,10]
	ldr	x1, [x1, #:got_lo12:g_MaxLpn]
	ldr	w21, [x1]
	adrp	x1, :got:c_ftl_nand_planes_num
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w24, [x1]
	cbnz	w23, .L2127
	sub	w21, w21, #1
	add	x1, x29, 84
	mov	w2, w23
	mov	w0, w21
	add	x22, x29, 88
	bl	log2phys
	str	w21, [x29,112]
	adrp	x0, :got:p_sys_data_buf
	ldr	w2, [x29,84]
	str	w2, [x29,92]
	cmn	w2, #1
	ldr	x0, [x0, #:got_lo12:p_sys_data_buf]
	ldr	x1, [x0]
	str	x1, [x29,96]
	adrp	x1, :got:p_sys_spare_buf
	ldr	x1, [x1, #:got_lo12:p_sys_spare_buf]
	ldr	x20, [x1]
	str	x20, [x29,104]
	str	w23, [x20,4]
	beq	.L2130
	mov	x0, x22
	mov	w1, 1
	mov	w2, w23
	bl	FlashReadPages
	b	.L2131
.L2130:
	adrp	x2, :got:c_ftl_nand_byte_pre_page
	ldr	x0, [x0]
	mov	w1, 255
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldrh	w2, [x2]
	bl	ftl_memset
.L2131:
	mov	w0, 6
	mov	w26, 0
	adrp	x25, :got:g_GlobalDataVersion
	mul	w24, w24, w0
	mov	w0, -3947
	strh	w0, [x20]
.L2132:
	cmp	w26, w24
	beq	.L2133
	ldr	x23, [x19, #:got_lo12:g_active_superblock]
	ldrh	w0, [x23,4]
	cbz	w0, .L2133
	ldr	w0, [x22,4]
	add	w26, w26, 1
	str	w0, [x20,12]
	ldrh	w0, [x23]
	strh	w0, [x20,2]
	mov	x0, x23
	str	w21, [x20,8]
	bl	get_new_active_ppa
	str	w0, [x22,4]
	ldr	x0, [x25, #:got_lo12:g_GlobalDataVersion]
	mov	w2, 0
	mov	w3, w2
	ldr	w1, [x0]
	str	w1, [x20,4]
	add	w1, w1, 1
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x0]
	mov	w1, 1
	mov	x0, x22
	bl	FlashProgPages
	ldrh	w0, [x23]
	bl	decrement_vpc_count
	b	.L2132
.L2133:
	ldr	x19, [x19, #:got_lo12:g_active_superblock]
	mov	w0, 1
	strb	w0, [x19,10]
	b	.L2127
.L2128:
	ldr	x19, [x19, #:got_lo12:g_active_superblock]
	strb	wzr, [x19,10]
.L2127:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	adrp	x20, :got:c_ftl_nand_l2pmap_ram_region_num
	adrp	x21, :got:p_l2p_ram_map
	bl	FtlWriteDumpData
.L2145:
	ldr	x0, [x20, #:got_lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldrh	w0, [x0]
	cmp	w0, w19
	bls	.L2148
	ldr	x1, [x21, #:got_lo12:p_l2p_ram_map]
	ubfiz	x0, x19, 4, 16
	ldr	x1, [x1]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2146
	mov	w0, w19
	bl	flush_l2p_region
.L2146:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2145
.L2148:
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
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LANCHOR1
	adrp	x0, .LC112
	add	x1, x1, :lo12:.LANCHOR1
	add	x29, sp, 0
	add	x1, x1, 16
	add	x0, x0, :lo12:.LC112
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	bl	printk
	adrp	x20, :got:p_valid_page_count_check_table
	adrp	x0, :got:c_ftl_nand_blk_pre_plane
	mov	w19, 0
	adrp	x21, :got:g_MaxLpn
	add	x22, x29, 108
	ldr	x1, [x20, #:got_lo12:p_valid_page_count_check_table]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_blk_pre_plane]
	ldrh	w2, [x0]
	ldr	x0, [x1]
	mov	w1, 0
	lsl	w2, w2, 1
	bl	ftl_memset
.L2150:
	ldr	x0, [x21, #:got_lo12:g_MaxLpn]
	ldr	w0, [x0]
	cmp	w19, w0
	bcs	.L2165
	mov	w0, w19
	mov	x1, x22
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,108]
	cmn	w0, #1
	beq	.L2151
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x20, #:got_lo12:p_valid_page_count_check_table]
	ldr	x3, [x1]
	ldrh	w2, [x3,x0]
	add	w2, w2, 1
	strh	w2, [x3,x0]
.L2151:
	add	w19, w19, 1
	b	.L2150
.L2165:
	adrp	x22, .LC113
	mov	w19, 0
	adrp	x23, :got:c_ftl_nand_data_blks_per_plane
	adrp	x24, :got:p_valid_page_count_table
	mov	w25, 65535
	add	x22, x22, :lo12:.LC113
	adrp	x26, :got:g_active_superblock
.L2153:
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w0, [x0]
	cmp	w0, w19
	bls	.L2166
	ldr	x27, [x24, #:got_lo12:p_valid_page_count_table]
	ubfiz	x21, x19, 1, 16
	ldr	x28, [x20, #:got_lo12:p_valid_page_count_check_table]
	ldr	x0, [x27]
	ldrh	w2, [x0,x21]
	ldr	x0, [x28]
	ldrh	w3, [x0,x21]
	cmp	w2, w3
	beq	.L2156
	cmp	w2, w25
	beq	.L2156
	mov	x0, x22
	mov	w1, w19
	bl	printk
	ldr	x0, [x26, #:got_lo12:g_active_superblock]
	ldrh	w0, [x0]
	cmp	w0, w19
	beq	.L2156
	adrp	x0, :got:g_gc_temp_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	ldrh	w0, [x0]
	cmp	w0, w19
	beq	.L2156
	adrp	x0, :got:g_buffer_superblock
	ldr	x0, [x0, #:got_lo12:g_buffer_superblock]
	ldrh	w0, [x0]
	cmp	w0, w19
	beq	.L2156
	ldr	x0, [x28]
	ldrh	w1, [x0,x21]
	ldr	x0, [x27]
	strh	w1, [x0,x21]
	mov	w0, w19
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2156:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2153
.L2166:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
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
	mov	x19, x0
	ldrh	w21, [x0]
	mov	w0, 65535
	cmp	w21, w0
	beq	.L2168
	adrp	x1, :got:p_valid_page_count_table
	ubfiz	x0, x21, 1, 16
	ldr	x1, [x1, #:got_lo12:p_valid_page_count_table]
	ldr	x1, [x1]
	ldrh	w0, [x1,x0]
	cbz	w0, .L2169
	mov	w0, w21
	bl	INSERT_DATA_LIST
	b	.L2168
.L2169:
	mov	w0, w21
	bl	INSERT_FREE_LIST
.L2168:
	adrp	x0, :got:g_buffer_superblock
	strb	wzr, [x19,8]
	ldr	x0, [x0, #:got_lo12:g_buffer_superblock]
	cmp	x19, x0
	beq	.L2170
	adrp	x0, :got:c_ftl_nand_type
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_type]
	ldrh	w0, [x0]
	cmp	w0, 1
	bne	.L2171
.L2170:
	mov	w0, 1
	strb	w0, [x19,8]
	b	.L2172
.L2171:
	adrp	x1, :got:g_active_superblock
	ldr	x2, [x1, #:got_lo12:g_active_superblock]
	cmp	x19, x2
	bne	.L2172
	cmp	w0, 3
	beq	.L2174
	adrp	x0, :got:g_all_blk_used_slc_mode
	ldr	x0, [x0, #:got_lo12:g_all_blk_used_slc_mode]
	ldr	w0, [x0]
	cmp	w0, 1
	bne	.L2175
.L2174:
	ldr	x0, [x1, #:got_lo12:g_active_superblock]
	mov	w2, 1
	strb	w2, [x0,8]
.L2175:
	adrp	x0, :got:g_inkDie_check_enable
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w0, [x0]
	cbz	w0, .L2172
	adrp	x0, :got:g_min_erase_count
	ldr	x0, [x0, #:got_lo12:g_min_erase_count]
	ldr	w0, [x0]
	cmp	w0, 29
	bhi	.L2172
	ldr	x1, [x1, #:got_lo12:g_active_superblock]
	mov	w0, 1
	strb	w0, [x1,8]
.L2172:
	adrp	x1, :got:g_tmp_data_superblock_id
	mov	w2, 65535
	mov	x20, x1
	ldr	x0, [x1, #:got_lo12:g_tmp_data_superblock_id]
	ldrh	w0, [x0]
	cmp	w0, w2
	beq	.L2177
	cmp	w21, w0
	bne	.L2178
	adrp	x2, :got:p_valid_page_count_table
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2, #:got_lo12:p_valid_page_count_table]
	ldr	x2, [x2]
	ldrh	w1, [x2,x1]
	cbz	w1, .L2179
.L2178:
	bl	update_vpc_list
.L2179:
	ldr	x1, [x20, #:got_lo12:g_tmp_data_superblock_id]
	mov	w0, -1
	strh	w0, [x1]
.L2177:
	mov	x0, x19
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
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	adrp	x0, :got:gp_last_act_superblock
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:gp_last_act_superblock]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x19, [x0]
	adrp	x0, :got:g_wr_page_num
	str	x0, [x29,120]
	ldr	x1, [x0, #:got_lo12:g_wr_page_num]
	ldr	w1, [x1]
	cbz	w1, .L2190
	adrp	x21, :got:req_wr_io
	ldrb	w3, [x19,9]
	mov	w2, 0
	mov	w23, 0
	adrp	x24, .LC114
	mov	w22, w23
	ldr	x0, [x21, #:got_lo12:req_wr_io]
	mov	w25, 56
	adrp	x26, :got:p_valid_page_count_table
	add	x24, x24, :lo12:.LC114
	ldr	x0, [x0]
	bl	FlashProgPages
	add	x0, x29, 140
	str	x0, [x29,112]
.L2191:
	ldr	x0, [x29,120]
	ldr	x0, [x0, #:got_lo12:g_wr_page_num]
	ldr	w1, [x0]
	cmp	w22, w1
	bcs	.L2209
	cbz	w23, .L2192
	ldr	x1, [x21, #:got_lo12:req_wr_io]
	mov	w2, -1
	umull	x0, w22, w25
	ldr	x1, [x1]
	str	w2, [x1,x0]
.L2192:
	umull	x20, w22, w25
	adrp	x27, :got:c_ftl_nand_page_pre_blk
	adrp	x28, :got:g_sys_ext_data
.L2193:
	ldr	x3, [x21, #:got_lo12:req_wr_io]
	ldr	x1, [x3]
	add	x0, x1, x20
	ldr	w1, [x1,x20]
	cmn	w1, #1
	bne	.L2210
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19]
	cmp	w1, w0, uxth
	bne	.L2194
	ldr	x0, [x26, #:got_lo12:p_valid_page_count_table]
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x19,4]
	ldr	x2, [x0]
	ldrh	w0, [x2,x1]
	sub	w0, w0, w3
	strh	w0, [x2,x1]
	ldr	x0, [x27, #:got_lo12:c_ftl_nand_page_pre_blk]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
	ldrh	w0, [x0]
	strh	w0, [x19,2]
.L2194:
	ldrh	w0, [x19,4]
	cbnz	w0, .L2195
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2195:
	ldr	x1, [x28, #:got_lo12:g_sys_ext_data]
	ldr	w0, [x1,96]
	add	w0, w0, 1
	str	w0, [x1,96]
	ldr	x23, [x21, #:got_lo12:req_wr_io]
	ldr	x0, [x23]
	add	x0, x0, x20
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,140]
	ldr	x1, [x23]
	mov	w2, 0
	add	x1, x1, x20
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x23]
	mov	w23, 1
	ldrb	w3, [x19,9]
	add	x0, x0, x20
	bl	FlashProgPages
	b	.L2193
.L2210:
	ldr	w1, [x0,4]
	mov	w2, 1
	str	w1, [x29,140]
	ldr	w0, [x0,24]
	ldr	x1, [x29,112]
	str	x3, [x29,104]
	bl	log2phys
	ldr	x3, [x29,104]
	ldr	x0, [x3]
	add	x20, x0, x20
	ldr	x0, [x20,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2197
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ldr	x2, [x26, #:got_lo12:p_valid_page_count_table]
	ubfiz	x0, x1, 1, 16
	mov	w20, w1
	ldr	x2, [x2]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2198
	mov	x0, x24
	bl	printk
.L2198:
	mov	w0, w20
	bl	decrement_vpc_count
.L2197:
	add	w22, w22, 1
	b	.L2191
.L2209:
	str	wzr, [x0]
.L2190:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
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
	adrp	x0, :got:gFtlInitStatus
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:gFtlInitStatus]
	ldr	w0, [x0]
	cmp	w0, 1
	bne	.L2214
	bl	FtlSysFlush
.L2214:
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
	bl	rk_nand_de_init
	bl	FtlDeInit
	bl	rk_nand_de_init
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	stp	x29, x30, [sp, -96]!
	add	w2, w0, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	adrp	x0, :got:g_MaxLbaSector
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x0, [x0, #:got_lo12:g_MaxLbaSector]
	mov	w19, w1
	str	x25, [sp,64]
	ldr	w1, [x0]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L2217
	cmp	w19, 31
	bls	.L2231
	adrp	x22, :got:c_ftl_nand_sec_pre_page
	bl	FtlCacheWriteBack
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w1, [x0]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L2219
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L2219:
	adrp	x20, .LANCHOR0
	mov	w0, -1
	add	x23, x29, 88
	str	w0, [x29,92]
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x24, :got:g_totle_discard_page_count
	add	x25, x29, 92
.L2220:
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w0, [x0]
	cmp	w19, w0
	bcc	.L2232
	mov	w0, w21
	mov	x1, x23
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L2221
	ldr	w0, [x20,32]
	mov	w2, 1
	add	w0, w0, 1
	str	w0, [x20,32]
	ldr	x1, [x24, #:got_lo12:g_totle_discard_page_count]
	ldr	w0, [x1]
	add	w0, w0, 1
	str	w0, [x1]
	mov	x1, x25
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,88]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2221:
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_sec_pre_page]
	add	w21, w21, 1
	ldrh	w0, [x0]
	sub	w19, w19, w0
	b	.L2220
.L2232:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	w1, [x0,32]
	cmp	w1, 32
	bls	.L2231
	str	wzr, [x0,32]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2231:
	mov	w0, 0
.L2217:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:g_gc_temp_superblock
	adrp	x20, :got:c_ftl_nand_page_pre_blk
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x2, [x19, #:got_lo12:g_gc_temp_superblock]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x1, [x20, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w21, [x2]
	mov	w2, 65535
	cmp	w21, w2
	ldrh	w1, [x1]
	bne	.L2234
.L2244:
	adrp	x0, :got:ftl_gc_temp_power_lost_recovery_flag
	ldr	x0, [x0, #:got_lo12:ftl_gc_temp_power_lost_recovery_flag]
	str	wzr, [x0]
	mov	w0, 65535
	ldr	x22, [x19, #:got_lo12:g_gc_temp_superblock]
	ldrh	w1, [x22]
	cmp	w1, w0
	beq	.L2262
	adrp	x21, :got:p_valid_page_count_table
	bl	FtlCacheWriteBack
	ldr	x20, [x20, #:got_lo12:c_ftl_nand_page_pre_blk]
	adrp	x25, :got:p_gc_page_info
	ldrh	w2, [x22]
	mov	w26, 12
	ldr	x0, [x21, #:got_lo12:p_valid_page_count_table]
	add	x27, x29, 108
	ldrh	w3, [x20]
	adrp	x20, :got:g_gc_page_offset
	ldr	x1, [x0]
	ldrb	w0, [x22,7]
	mov	w22, 0
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	adrp	x0, :got:g_totle_gc_page_count
	ldr	x1, [x20, #:got_lo12:g_gc_page_offset]
	ldr	x0, [x0, #:got_lo12:g_totle_gc_page_count]
	ldrh	w2, [x1]
	ldr	w1, [x0]
	add	w1, w2, w1
	str	w1, [x0]
	b	.L2245
.L2234:
	cbz	w0, .L2237
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w3, [x0,128]
	cmp	w3, w2
	beq	.L2238
.L2239:
	mov	w1, 2
	b	.L2237
.L2238:
	strh	wzr, [x0,128]
	adrp	x0, :got:g_num_free_superblocks
	ldr	x0, [x0, #:got_lo12:g_num_free_superblocks]
	ldrh	w0, [x0]
	cmp	w0, 17
	bhi	.L2239
.L2237:
	ldr	x0, [x19, #:got_lo12:g_gc_temp_superblock]
	bl	FtlGcScanTempBlk
	str	w0, [x29,108]
	cmn	w0, #1
	beq	.L2240
	adrp	x0, :got:g_sys_ext_data
	ubfiz	x21, x21, 1, 16
	ldr	x0, [x0, #:got_lo12:g_sys_ext_data]
	ldr	w1, [x0,96]
	add	w1, w1, 1
	str	w1, [x0,96]
	adrp	x0, :got:p_erase_count_table
	ldr	x0, [x0, #:got_lo12:p_erase_count_table]
	ldr	x1, [x0]
	ldrh	w0, [x1,x21]
	cmp	w0, 4
	bls	.L2241
	sub	w0, w0, #5
	strh	w0, [x1,x21]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2241:
	adrp	x0, :got:ftl_gc_temp_power_lost_recovery_flag
	mov	x19, x0
	ldr	x1, [x0, #:got_lo12:ftl_gc_temp_power_lost_recovery_flag]
	ldr	w1, [x1]
	cbnz	w1, .L2242
	ldr	w0, [x29,108]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2242:
	ldr	x0, [x19, #:got_lo12:ftl_gc_temp_power_lost_recovery_flag]
	str	wzr, [x0]
	mov	w0, 1
	b	.L2243
.L2240:
	adrp	x0, .LANCHOR2+128
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR2+128]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2243
	b	.L2244
.L2248:
	ldr	x0, [x25, #:got_lo12:p_gc_page_info]
	mov	x1, x27
	umull	x24, w22, w26
	mov	w2, 0
	ldr	x28, [x0]
	add	x23, x28, x24
	ldr	w0, [x23,8]
	bl	log2phys
	ldr	w0, [x29,108]
	ldr	w1, [x28,x24]
	cmp	w0, w1
	bne	.L2246
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
.L2261:
	bl	decrement_vpc_count
.L2247:
	add	w22, w22, 1
	uxth	w22, w22
.L2245:
	ldr	x0, [x20, #:got_lo12:g_gc_page_offset]
	ldrh	w0, [x0]
	cmp	w0, w22
	bhi	.L2248
	b	.L2264
.L2246:
	ldr	w1, [x23,4]
	cmp	w0, w1
	beq	.L2247
	ldr	x0, [x19, #:got_lo12:g_gc_temp_superblock]
	ldrh	w0, [x0]
	b	.L2261
.L2264:
	mov	w0, -1
	bl	decrement_vpc_count
	ldr	x21, [x21, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x19, #:got_lo12:g_gc_temp_superblock]
	ldr	x2, [x21]
	ldrh	w0, [x0]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2,x1]
	cbz	w1, .L2249
	bl	INSERT_DATA_LIST
	b	.L2250
.L2249:
	bl	INSERT_FREE_LIST
.L2250:
	ldr	x19, [x19, #:got_lo12:g_gc_temp_superblock]
	mov	w0, -1
	strh	w0, [x19]
	adrp	x0, :got:g_gc_blk_num
	ldr	x20, [x20, #:got_lo12:g_gc_page_offset]
	strh	wzr, [x20]
	ldr	x0, [x0, #:got_lo12:g_gc_blk_num]
	strh	wzr, [x0]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	adrp	x0, :got:g_inkDie_check_enable
	adrp	x1, :got:g_num_free_superblocks
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w2, [x0]
	adrp	x0, :got:c_ftl_nand_data_op_blks_per_plane
	cbz	w2, .L2251
	adrp	x2, :got:g_min_erase_count
	ldr	x2, [x2, #:got_lo12:g_min_erase_count]
	ldr	w2, [x2]
	cmp	w2, 29
	bhi	.L2251
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	ldr	x1, [x1, #:got_lo12:g_num_free_superblocks]
	ldrh	w0, [x0]
	ldrh	w1, [x1]
	cmp	w1, w0
	bcs	.L2252
	adrp	x1, :got:g_gc_free_blk_threshold
	lsl	w0, w0, 1
	ldr	x1, [x1, #:got_lo12:g_gc_free_blk_threshold]
	strh	w0, [x1]
.L2252:
	adrp	x0, :got:g_gc_superblock
	mov	w1, -1
	ldr	x0, [x0, #:got_lo12:g_gc_superblock]
	b	.L2263
.L2251:
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	ldr	x1, [x1, #:got_lo12:g_num_free_superblocks]
	ldrh	w0, [x0]
	ldrh	w1, [x1]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	ble	.L2262
	adrp	x0, :got:g_gc_superblock
	mov	w1, -1
	ldr	x0, [x0, #:got_lo12:g_gc_superblock]
	strh	w1, [x0]
	adrp	x0, :got:g_gc_free_blk_threshold
	mov	w1, 20
	ldr	x0, [x0, #:got_lo12:g_gc_free_blk_threshold]
.L2263:
	strh	w1, [x0]
.L2262:
	mov	w0, 0
.L2243:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:c_ftl_nand_page_pre_blk
	adrp	x20, :got:g_gc_temp_superblock
	ldr	x19, [x19, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldr	x20, [x20, #:got_lo12:g_gc_temp_superblock]
	ldrh	w1, [x19]
	mov	x0, x20
	bl	FtlGcScanTempBlk
	ldrh	w1, [x20,2]
	ldrh	w0, [x19]
	cmp	w1, w0
	bcc	.L2265
	adrp	x0, :got:gL2pMapInfo
	ldr	x0, [x0, #:got_lo12:gL2pMapInfo]
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	adrp	x0, :got:ftl_gc_temp_power_lost_recovery_flag
	ldr	x0, [x0, #:got_lo12:ftl_gc_temp_power_lost_recovery_flag]
	str	wzr, [x0]
.L2265:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	adrp	x0, :got:g_power_lost_recovery_flag
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:g_power_lost_recovery_flag]
	str	x19, [sp,16]
	adrp	x19, :got:g_active_superblock
	strh	wzr, [x0]
	adrp	x0, :got:g_recovery_page_num
	ldr	x0, [x0, #:got_lo12:g_recovery_page_num]
	str	wzr, [x0]
	ldr	x19, [x19, #:got_lo12:g_active_superblock]
	mov	x0, x19
	bl	FtlRecoverySuperblock
	mov	x0, x19
	adrp	x19, :got:g_buffer_superblock
	bl	FtlSlcSuperblockCheck
	ldr	x19, [x19, #:got_lo12:g_buffer_superblock]
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
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	stp	x29, x30, [sp, -64]!
	mov	w2, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x19, :got:req_gc_dst
	adrp	x21, :got:g_gc_num_req
	str	x23, [sp,48]
	mov	w3, w2
	mov	w20, 0
	mov	w23, 56
	ldr	x0, [x19, #:got_lo12:req_gc_dst]
	ldr	x1, [x21, #:got_lo12:g_gc_num_req]
	ldr	x0, [x0]
	ldr	w1, [x1]
	bl	FlashProgPages
.L2269:
	ldr	x22, [x21, #:got_lo12:g_gc_num_req]
	ldr	w1, [x22]
	cmp	w20, w1
	bcs	.L2275
	ldr	x3, [x19, #:got_lo12:req_gc_dst]
	umull	x0, w20, w23
	ldr	x2, [x3]
	add	x1, x2, x0
	ldr	w4, [x2,x0]
	cmn	w4, #1
	bne	.L2270
	adrp	x1, :got:g_gc_temp_superblock
	adrp	x2, :got:p_valid_page_count_table
	ldr	x1, [x1, #:got_lo12:g_gc_temp_superblock]
	ldr	x2, [x2, #:got_lo12:p_valid_page_count_table]
	ldrh	w5, [x1]
	ldr	x2, [x2]
	strh	wzr, [x2,x5,lsl 1]
	strh	w4, [x1]
	adrp	x1, :got:g_sys_ext_data
	ldr	x1, [x1, #:got_lo12:g_sys_ext_data]
	ldr	w2, [x1,96]
	add	w2, w2, 1
	str	w2, [x1,96]
	ldr	x1, [x3]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2274
.L2270:
	ldr	x2, [x1,16]
	add	w20, w20, 1
	ldr	w1, [x1,4]
	uxth	w20, w20
	ldr	w0, [x2,12]
	ldr	w2, [x2,8]
	bl	FtlGcUpdatePage
	b	.L2269
.L2275:
	ldr	x19, [x19, #:got_lo12:req_gc_dst]
	ldr	x0, [x19]
	bl	FtlGcBufFree
	str	wzr, [x22]
	adrp	x0, :got:g_gc_temp_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	ldrh	w1, [x0,4]
	mov	w0, 0
	cbnz	w1, .L2271
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L2274:
	mov	w0, 1
.L2271:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	adrp	x0, :got:c_ftl_nand_max_sys_blks
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_max_sys_blks]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	ldrh	w0, [x0]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	adrp	x0, :got:gSysInfo
	ldr	x0, [x0, #:got_lo12:gSysInfo]
	ldrh	w1, [x0]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2277
.L2279:
	mov	w22, -1
	b	.L2278
.L2277:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L2279
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	adrp	x0, :got:c_ftl_nand_l2pmap_ram_region_num
	mov	x1, 0
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldrh	w2, [x0]
	adrp	x0, :got:p_l2p_ram_map
	ldr	x0, [x0, #:got_lo12:p_l2p_ram_map]
	ldr	x0, [x0]
.L2280:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2284
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2280
.L2284:
	adrp	x19, :got:g_sys_save_data
	cmp	w3, w2
	adrp	x20, :got:g_active_superblock
	ldr	x1, [x19, #:got_lo12:g_sys_save_data]
	ldrh	w0, [x1,28]
	add	w0, w0, 1
	strh	w0, [x1,28]
	blt	.L2281
	adrp	x0, :got:g_power_lost_recovery_flag
	ldr	x0, [x0, #:got_lo12:g_power_lost_recovery_flag]
	ldrh	w0, [x0]
	cbz	w0, .L2285
.L2281:
	adrp	x3, :got:p_valid_page_count_table
	ldr	x1, [x20, #:got_lo12:g_active_superblock]
	ldr	x3, [x3, #:got_lo12:p_valid_page_count_table]
	ldrh	w0, [x1]
	ldrh	w5, [x1,4]
	ldr	x4, [x3]
	lsl	x0, x0, 1
	ldrh	w2, [x4,x0]
	sub	w2, w2, w5
	strh	w2, [x4,x0]
	adrp	x0, :got:c_ftl_nand_page_pre_blk
	ldr	x4, [x3]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	strh	wzr, [x1,4]
	strb	wzr, [x1,6]
	ldrh	w2, [x0]
	strh	w2, [x1,2]
	adrp	x1, :got:g_buffer_superblock
	ldr	x1, [x1, #:got_lo12:g_buffer_superblock]
	ldrh	w2, [x1]
	ldrh	w5, [x1,4]
	lsl	x2, x2, 1
	ldrh	w3, [x4,x2]
	sub	w3, w3, w5
	strh	w3, [x4,x2]
	strb	wzr, [x1,6]
	ldrh	w0, [x0]
	strh	w0, [x1,2]
	adrp	x0, :got:gL2pMapInfo
	strh	wzr, [x1,4]
	ldr	x0, [x0, #:got_lo12:gL2pMapInfo]
	bl	FtlMapBlkWriteDumpData
	adrp	x0, :got:gVendorBlkInfo
	ldr	x0, [x0, #:got_lo12:gVendorBlkInfo]
	bl	FtlMapBlkWriteDumpData
	ldr	x1, [x19, #:got_lo12:g_sys_save_data]
	ldrh	w0, [x1,30]
	add	w0, w0, 1
	strh	w0, [x1,30]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2285:
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldr	x20, [x20, #:got_lo12:g_active_superblock]
	mov	w1, 65535
	ldrh	w0, [x20]
	cmp	w0, w1
	beq	.L2286
	ldrh	w1, [x20,4]
	cbnz	w1, .L2286
	adrp	x21, :got:g_buffer_superblock
	ldr	x21, [x21, #:got_lo12:g_buffer_superblock]
	ldrh	w1, [x21,4]
	cbnz	w1, .L2286
	bl	FtlGcRefreshBlock
	ldrh	w0, [x21]
	bl	FtlGcRefreshBlock
	mov	x0, x20
	bl	allocate_new_data_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
	adrp	x0, :got:gVendorBlkInfo
	ldr	x0, [x0, #:got_lo12:gVendorBlkInfo]
	bl	FtlMapBlkWriteDumpData
.L2286:
	ldr	x19, [x19, #:got_lo12:g_sys_save_data]
	ldrh	w0, [x19,28]
	and	w0, w0, 31
	cbnz	w0, .L2278
	bl	FtlVpcCheckAndModify
.L2278:
	mov	w0, w22
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	stp	x29, x30, [sp, -32]!
	adrp	x1, :got:g_LowFormat
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:gFtlInitStatus
	mov	x20, x0
	mov	w0, -1
	ldr	x19, [x19, #:got_lo12:gFtlInitStatus]
	str	w0, [x19]
	adrp	x0, .LC72
	add	x0, x0, :lo12:.LC72
	ldr	x1, [x1, #:got_lo12:g_LowFormat]
	str	wzr, [x1]
	adrp	x1, .LC73
	add	x1, x1, :lo12:.LC73
	bl	printk
	mov	x0, x20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	adrp	x0, :got:c_ftl_nand_max_sys_blks
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_max_sys_blks]
	ldrh	w0, [x0]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	w0, .L2294
	bl	FtlSysBlkInit
	cbnz	w0, .L2294
	mov	w0, 1
	str	w0, [x19]
.L2294:
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
	stp	x19, x20, [sp,16]
	adrp	x20, :got:gLoaderBootInfo
	bl	ftl_malloc
	adrp	x19, :got:RK29_NANDC_REG_BASE
	adrp	x1, :got:RK29_NANDC1_REG_BASE
	ldr	x20, [x20, #:got_lo12:gLoaderBootInfo]
	str	x0, [x20]
	ldr	x19, [x19, #:got_lo12:RK29_NANDC_REG_BASE]
	mov	x0, x19
	str	xzr, [x19]
	ldr	x1, [x1, #:got_lo12:RK29_NANDC1_REG_BASE]
	str	xzr, [x1]
	bl	rknand_get_reg_addr
	ldr	x1, [x19]
	mov	w0, -1
	cbz	x1, .L2297
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x20]
	mov	w2, w1
	mov	w3, 2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	x0, [x19]
	bl	FlashInit
	mov	w19, w0
	cbnz	w0, .L2298
	adrp	x0, :got:gNandPhyInfo
	ldr	x0, [x0, #:got_lo12:gNandPhyInfo]
	bl	FtlInit
.L2298:
	adrp	x0, .LC115
	mov	w1, w19
	add	x0, x0, :lo12:.LC115
	bl	printk
	mov	w0, w19
.L2297:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	stp	x29, x30, [sp, -32]!
	mov	w1, 65535
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, :got:g_gc_temp_superblock
	ldr	x0, [x19, #:got_lo12:g_gc_temp_superblock]
	ldrh	w2, [x0]
	cmp	w2, w1
	beq	.L2302
	ldrh	w0, [x0,4]
	cbnz	w0, .L2303
.L2302:
	bl	FtlCacheWriteBack
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	ldr	x0, [x19, #:got_lo12:g_gc_temp_superblock]
	strb	wzr, [x0,8]
	bl	allocate_data_superblock
	adrp	x0, :got:g_gc_blk_num
	ldr	x0, [x0, #:got_lo12:g_gc_blk_num]
	strh	wzr, [x0]
	adrp	x0, :got:g_gc_page_offset
	ldr	x0, [x0, #:got_lo12:g_gc_page_offset]
	strh	wzr, [x0]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	mov	w0, 0
	bl	FtlEctTblFlush
.L2303:
	ldr	x0, [x19, #:got_lo12:g_gc_temp_superblock]
	bl	get_new_active_ppa
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, :got:g_in_gc_progress
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	x2, [x22, #:got_lo12:g_in_gc_progress]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w3, [x2]
	mov	w2, 0
	cbnz	w3, .L2431
	mov	w24, w0
	adrp	x0, .LANCHOR2+128
	mov	w23, w1
	mov	w1, 65535
	ldrh	w0, [x0,#:lo12:.LANCHOR2+128]
	cmp	w0, w1
	bne	.L2306
.L2309:
	adrp	x0, :got:g_gc_next_blk_1
	mov	w3, 65535
	ldr	x0, [x0, #:got_lo12:g_gc_next_blk_1]
	ldrh	w2, [x0]
	cmp	w2, w3
	bne	.L2307
	b	.L2308
.L2306:
	adrp	x0, :got:g_gc_temp_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	ldrh	w0, [x0]
	cmp	w0, w1
	beq	.L2309
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2309
	b	.L2431
.L2307:
	adrp	x1, :got:g_gc_next_blk
	ldr	x1, [x1, #:got_lo12:g_gc_next_blk]
	ldrh	w4, [x1]
	cmp	w4, w3
	bne	.L2308
	strh	w2, [x1]
	mov	w1, -1
	strh	w1, [x0]
.L2308:
	adrp	x26, :got:g_gc_skip_write_count
	cmp	w24, 1
	adrp	x19, :got:g_gc_superblock
	ldr	x20, [x26, #:got_lo12:g_gc_skip_write_count]
	ldr	w0, [x20]
	add	w0, w0, 1
	add	w0, w0, w24, lsl 7
	str	w0, [x20]
	beq	.L2310
.L2312:
	mov	w21, 65535
	b	.L2311
.L2310:
	adrp	x1, :got:g_inkDie_check_enable
	ldr	x1, [x1, #:got_lo12:g_inkDie_check_enable]
	ldr	w1, [x1]
	cbz	w1, .L2312
	adrp	x27, :got:g_min_erase_count
	ldr	x27, [x27, #:got_lo12:g_min_erase_count]
	ldr	w1, [x27]
	cmp	w1, 29
	bhi	.L2312
	adrp	x25, :got:gc_ink_free_return_value
	ldr	x21, [x25, #:got_lo12:gc_ink_free_return_value]
	ldrh	w1, [x21]
	add	w0, w0, w1
	str	w0, [x20]
	bl	FtlGcReFreshBadBlk
	ldr	x0, [x19, #:got_lo12:g_gc_superblock]
	ldrh	w1, [x0]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2312
	adrp	x0, :got:g_gc_next_blk
	ldr	x0, [x0, #:got_lo12:g_gc_next_blk]
	ldrh	w0, [x0]
	cmp	w0, w1
	bne	.L2312
	ldr	w0, [x20]
	cmp	w0, 1024
	bls	.L2312
	ldr	w0, [x27]
	str	wzr, [x20]
	strh	wzr, [x21]
	cbnz	w0, .L2313
	mov	w0, 6
	b	.L2450
.L2313:
	cmp	w0, 5
	bhi	.L2314
	mov	w0, 18
.L2450:
	strh	w0, [x21]
.L2314:
	mov	w0, 32
	bl	List_get_gc_head_node
	uxth	w28, w0
	mov	w3, 65535
	cmp	w28, w3
	beq	.L2318
	adrp	x20, :got:g_gc_blk_index
	ldr	x20, [x20, #:got_lo12:g_gc_blk_index]
	ldrh	w0, [x20]
	cbz	w0, .L2316
	adrp	x27, :got:p_valid_page_count_table
	ubfiz	x28, x28, 1, 16
	adrp	x2, :got:c_ftl_nand_planes_num
	ldr	x27, [x27, #:got_lo12:p_valid_page_count_table]
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_planes_num]
	ldr	x1, [x27]
	ldrh	w2, [x2]
	ldrh	w4, [x1,x28]
	adrp	x1, :got:c_ftl_nand_page_pre_slc_blk
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w1, [x1]
	mul	w1, w1, w2
	add	w1, w1, 1
	cmp	w4, w1
	bgt	.L2318
	adrp	x1, :got:g_in_swl_replace
	str	x3, [x29,136]
	ldr	x1, [x1, #:got_lo12:g_in_swl_replace]
	str	wzr, [x1]
	add	w1, w0, 1
	strh	w1, [x20]
	bl	List_get_gc_head_node
	uxth	w21, w0
	ldr	x3, [x29,136]
	cmp	w21, w3
	beq	.L2318
	ubfiz	x5, x21, 1, 16
	ldr	x4, [x27]
	adrp	x0, .LC116
	ldrh	w1, [x20]
	add	x0, x0, :lo12:.LC116
	mov	w2, w21
	ldrh	w3, [x4,x5]
	ldrh	w4, [x4,x28]
	str	x5, [x29,136]
	bl	printk
	ldrh	w0, [x20]
	ldr	x5, [x29,136]
	cmp	w0, 40
	bls	.L2317
	ldr	x0, [x27]
	ldrh	w0, [x0,x5]
	cmp	w0, 32
	bls	.L2317
	strh	wzr, [x20]
.L2317:
	ldr	x25, [x25, #:got_lo12:gc_ink_free_return_value]
	mov	w0, 6
	strh	w0, [x25]
	b	.L2311
.L2316:
	mov	w0, 1
	strh	w0, [x20]
.L2318:
	bl	GetSwlReplaceBlock
	uxth	w21, w0
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2311
	ldr	x25, [x25, #:got_lo12:gc_ink_free_return_value]
	strh	wzr, [x25]
.L2311:
	ldr	x0, [x19, #:got_lo12:g_gc_superblock]
	ldrh	w2, [x0]
	mov	w0, 65535
	cmp	w2, w0
	bne	.L2319
	adrp	x0, :got:g_gc_temp_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	ldrh	w1, [x0]
	cmp	w1, w2
	bne	.L2319
	adrp	x0, :got:g_gc_next_blk
	ldr	x0, [x0, #:got_lo12:g_gc_next_blk]
	ldrh	w20, [x0]
	cmp	w20, w1
	bne	.L2319
	adrp	x25, :got:g_num_free_superblocks
	ldr	x26, [x26, #:got_lo12:g_gc_skip_write_count]
	mov	w2, 1024
	ldr	x27, [x25, #:got_lo12:g_num_free_superblocks]
	ldr	w1, [x26]
	ldrh	w0, [x27]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2319
	adrp	x0, :got:gc_ink_free_return_value
	str	wzr, [x26]
	ldr	x0, [x0, #:got_lo12:gc_ink_free_return_value]
	strh	wzr, [x0]
	bl	GetSwlReplaceBlock
	uxth	w21, w0
	cmp	w21, w20
	bne	.L2321
	adrp	x20, :got:g_gc_merge_free_blk_threshold
	ldrh	w1, [x27]
	ldr	x26, [x20, #:got_lo12:g_gc_merge_free_blk_threshold]
	ldrh	w0, [x26]
	cmp	w1, w0
	bcs	.L2322
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w21
	beq	.L2331
	adrp	x1, :got:g_SlcPartLbaEndSector
	adrp	x4, :got:c_ftl_nand_type
	uxtw	x2, w0
	ldr	x1, [x1, #:got_lo12:g_SlcPartLbaEndSector]
	ldr	w3, [x1]
	adrp	x1, :got:p_valid_page_count_table
	cbnz	w3, .L2324
	ldr	x0, [x4, #:got_lo12:c_ftl_nand_type]
	ldrh	w0, [x0]
	cmp	w0, 3
	beq	.L2324
	adrp	x0, :got:g_all_blk_used_slc_mode
	ldr	x0, [x0, #:got_lo12:g_all_blk_used_slc_mode]
	ldr	w0, [x0]
	cbnz	w0, .L2324
	adrp	x0, :got:g_inkDie_check_enable
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w0, [x0]
	cbz	w0, .L2325
.L2324:
	ldr	x1, [x1, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x1]
	ldrh	w5, [x0,x2,lsl 1]
	adrp	x0, :got:c_ftl_nand_page_pre_slc_blk
	ldr	x2, [x4, #:got_lo12:c_ftl_nand_type]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w2, [x2]
	ldrh	w1, [x0]
	adrp	x0, :got:c_ftl_nand_planes_num
	cmp	w2, 3
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	mul	w3, w1, w0
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w3, w0
	cmp	w5, w0
	bgt	.L2327
	mov	w0, 0
	bl	List_get_gc_head_node
	uxth	w21, w0
	adrp	x0, :got:g_MaxLpn
	adrp	x1, :got:g_VaildLpn
	ldr	x20, [x20, #:got_lo12:g_gc_merge_free_blk_threshold]
	ldr	x0, [x0, #:got_lo12:g_MaxLpn]
	ldr	x1, [x1, #:got_lo12:g_VaildLpn]
	ldr	w0, [x0]
	ldr	w1, [x1]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bls	.L2328
	mov	w0, 128
	b	.L2451
.L2328:
	mov	w0, 160
.L2451:
	strh	w0, [x20]
	b	.L2329
.L2327:
	ldr	x20, [x20, #:got_lo12:g_gc_merge_free_blk_threshold]
	mov	w0, 128
	strh	w0, [x20]
	b	.L2331
.L2325:
	ldr	x1, [x1, #:got_lo12:p_valid_page_count_table]
	ldr	x1, [x1]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, 7
	bhi	.L2330
	bl	List_get_gc_head_node
	uxth	w21, w0
	mov	w0, 128
	strh	w0, [x26]
	b	.L2329
.L2330:
	mov	w0, 64
	b	.L2452
.L2322:
	mov	w0, 80
.L2452:
	strh	w0, [x26]
	b	.L2331
.L2329:
	mov	w0, 65535
	cmp	w21, w0
	beq	.L2331
.L2321:
	adrp	x0, :got:p_valid_page_count_table
	adrp	x5, :got:g_gc_free_blk_threshold
	ubfiz	x4, x21, 1, 16
	ldr	x25, [x25, #:got_lo12:g_num_free_superblocks]
	mov	w1, w21
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x5, [x5, #:got_lo12:g_gc_free_blk_threshold]
	ldrh	w2, [x25]
	ldr	x3, [x0]
	adrp	x0, :got:p_erase_count_table
	ldrh	w5, [x5]
	ldr	x0, [x0, #:got_lo12:p_erase_count_table]
	ldrh	w3, [x3,x4]
	ldr	x6, [x0]
	adrp	x0, .LC117
	add	x0, x0, :lo12:.LC117
	ldrh	w4, [x6,x4]
	bl	printk
.L2331:
	bl	FtlGcReFreshBadBlk
.L2319:
	mov	w0, 65535
	cmp	w21, w0
	cset	w1, eq
	cbz	w1, .L2332
	cbnz	w24, .L2332
	adrp	x0, :got:g_num_free_superblocks
	mov	w20, 1
	ldr	x0, [x0, #:got_lo12:g_num_free_superblocks]
	ldrh	w1, [x0]
	cmp	w1, 24
	bhi	.L2333
	adrp	x0, :got:c_ftl_nand_page_pre_blk
	cmp	w1, 16
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w20, [x0]
	bls	.L2334
	lsr	w20, w20, 5
	b	.L2333
.L2334:
	cmp	w1, 12
	bls	.L2335
	lsr	w20, w20, 4
	b	.L2333
.L2335:
	cmp	w1, 8
	bls	.L2333
	lsr	w20, w20, 2
.L2333:
	adrp	x0, :got:g_gc_free_blk_threshold
	ldr	x4, [x0, #:got_lo12:g_gc_free_blk_threshold]
	ldrh	w2, [x4]
	cmp	w2, w1
	bcs	.L2337
	adrp	x1, :got:g_gc_temp_superblock
	ldr	x1, [x1, #:got_lo12:g_gc_temp_superblock]
	ldrh	w2, [x1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2338
	adrp	x1, :got:g_gc_next_blk
	ldr	x1, [x1, #:got_lo12:g_gc_next_blk]
	ldrh	w1, [x1]
	cmp	w1, w2
	bne	.L2338
	adrp	x1, :got:gc_ink_free_return_value
	ldr	x1, [x1, #:got_lo12:gc_ink_free_return_value]
	ldrh	w2, [x1]
	cbnz	w2, .L2339
	adrp	x1, :got:g_MaxLpn
	adrp	x3, :got:g_VaildLpn
	ldr	x1, [x1, #:got_lo12:g_MaxLpn]
	ldr	x3, [x3, #:got_lo12:g_VaildLpn]
	ldr	w1, [x1]
	ldr	w3, [x3]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2340
.L2339:
	adrp	x1, :got:c_ftl_nand_data_op_blks_per_plane
	ldr	x0, [x0, #:got_lo12:g_gc_free_blk_threshold]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	ldrh	w1, [x1]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x0]
	b	.L2341
.L2340:
	mov	w0, 18
	strh	w0, [x4]
.L2341:
	adrp	x0, :got:g_in_swl_replace
	ldr	x0, [x0, #:got_lo12:g_in_swl_replace]
	str	wzr, [x0]
	b	.L2431
.L2338:
	adrp	x1, :got:c_ftl_nand_data_op_blks_per_plane
	ldr	x0, [x0, #:got_lo12:g_gc_free_blk_threshold]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	ldrh	w1, [x1]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x0]
.L2337:
	cmp	w23, 2
	bhi	.L2392
	adrp	x0, :got:g_inkDie_check_enable
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w0, [x0]
	cbz	w0, .L2392
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2392
.L2332:
	adrp	x0, :got:g_gc_temp_superblock
	mov	w2, 65535
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	ldrh	w0, [x0]
	cmp	w0, w2
	bne	.L2344
	cbz	w1, .L2344
	adrp	x1, :got:g_gc_next_blk
	ldr	x1, [x1, #:got_lo12:g_gc_next_blk]
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L2344
	ldr	x1, [x19, #:got_lo12:g_gc_superblock]
	ldrh	w1, [x1]
	cmp	w1, w0
	beq	.L2345
.L2350:
	mov	w21, 65535
	b	.L2344
.L2345:
	adrp	x0, :got:g_in_swl_replace
	adrp	x20, :got:g_num_free_superblocks
	adrp	x21, :got:gc_ink_free_return_value
	ldr	x0, [x0, #:got_lo12:g_in_swl_replace]
	str	wzr, [x0]
	adrp	x0, :got:g_gc_free_blk_threshold
	ldr	x24, [x20, #:got_lo12:g_num_free_superblocks]
	ldr	x2, [x0, #:got_lo12:g_gc_free_blk_threshold]
	ldrh	w3, [x24]
	ldrh	w1, [x2]
	cmp	w3, w1
	bls	.L2346
	ldr	x1, [x21, #:got_lo12:gc_ink_free_return_value]
	ldrh	w1, [x1]
	cbnz	w1, .L2347
	adrp	x1, :got:g_MaxLpn
	adrp	x3, :got:g_VaildLpn
	ldr	x1, [x1, #:got_lo12:g_MaxLpn]
	ldr	x3, [x3, #:got_lo12:g_VaildLpn]
	ldr	w1, [x1]
	ldr	w3, [x3]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2348
.L2347:
	adrp	x1, :got:c_ftl_nand_data_op_blks_per_plane
	ldr	x0, [x0, #:got_lo12:g_gc_free_blk_threshold]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	ldrh	w1, [x1]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x0]
	b	.L2349
.L2348:
	mov	w0, 18
	strh	w0, [x2]
.L2349:
	bl	FtlReadRefresh
	ldr	x21, [x21, #:got_lo12:gc_ink_free_return_value]
	b	.L2453
.L2346:
	ldr	x21, [x21, #:got_lo12:gc_ink_free_return_value]
	ldrh	w0, [x21]
	cbnz	w0, .L2350
	adrp	x23, :got:c_ftl_nand_data_op_blks_per_plane
	ldr	x23, [x23, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	ldrh	w1, [x23]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	adrp	x1, :got:p_valid_page_count_table
	ldr	x1, [x1, #:got_lo12:p_valid_page_count_table]
	ldr	x1, [x1]
	ldrh	w2, [x1,x0]
	adrp	x0, :got:c_ftl_nand_page_pre_slc_blk
	adrp	x1, :got:c_ftl_nand_planes_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_slc_blk]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	ldrh	w1, [x1]
	mul	w0, w0, w1
	mov	w1, 2
	sdiv	w0, w0, w1
	cmp	w2, w0
	ble	.L2351
	ldrh	w0, [x23]
	ldrh	w1, [x24]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2351
	bl	FtlReadRefresh
.L2453:
	ldrh	w2, [x21]
	b	.L2431
.L2351:
	cbnz	w2, .L2350
	mov	w0, -1
	bl	decrement_vpc_count
	ldr	x20, [x20, #:got_lo12:g_num_free_superblocks]
	ldrh	w2, [x20]
	add	w2, w2, 1
	b	.L2431
.L2344:
	adrp	x0, :got:g_inkDie_check_enable
	mov	w20, 2
	ldr	x0, [x0, #:got_lo12:g_inkDie_check_enable]
	ldr	w0, [x0]
	cmp	w0, wzr
	csinc	w20, w20, wzr, ne
	b	.L2343
.L2392:
	mov	w21, 65535
.L2343:
	ldr	x1, [x19, #:got_lo12:g_gc_superblock]
	mov	w2, 65535
	ldrh	w0, [x1]
	cmp	w0, w2
	bne	.L2353
	cmp	w21, w0
	beq	.L2354
	strh	w21, [x1]
	b	.L2355
.L2354:
	adrp	x0, :got:g_gc_next_blk
	ldr	x0, [x0, #:got_lo12:g_gc_next_blk]
	ldrh	w2, [x0]
	cmp	w2, w21
	beq	.L2355
	strh	w2, [x1]
	mov	w1, -1
	strh	w1, [x0]
.L2355:
	ldr	x23, [x19, #:got_lo12:g_gc_superblock]
	ldrh	w0, [x23]
	bl	IsBlkInGcList
	cbz	w0, .L2356
	mov	w0, -1
	strh	w0, [x23]
.L2356:
	ldr	x23, [x19, #:got_lo12:g_gc_superblock]
	mov	w0, 65535
	ldrh	w1, [x23]
	cmp	w1, w0
	beq	.L2353
	mov	x0, x23
	bl	make_superblock
	strh	wzr, [x23,2]
	adrp	x0, :got:g_gc_cur_blk_valid_pages
	strb	wzr, [x23,6]
	ldrh	w1, [x23]
	ldr	x0, [x0, #:got_lo12:g_gc_cur_blk_valid_pages]
	strh	wzr, [x0]
	adrp	x0, :got:p_valid_page_count_table
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x0]
	ldrh	w1, [x0,x1,lsl 1]
	adrp	x0, :got:g_gc_cur_blk_max_valid_pages
	ldr	x0, [x0, #:got_lo12:g_gc_cur_blk_max_valid_pages]
	strh	w1, [x0]
.L2353:
	ldr	x0, [x19, #:got_lo12:g_gc_superblock]
	ldrh	w1, [x0]
	adrp	x0, :got:g_active_superblock
	ldr	x0, [x0, #:got_lo12:g_active_superblock]
	ldrh	w0, [x0]
	cmp	w0, w1
	beq	.L2358
	adrp	x0, :got:g_buffer_superblock
	ldr	x0, [x0, #:got_lo12:g_buffer_superblock]
	ldrh	w0, [x0]
	cmp	w0, w1
	beq	.L2358
.L2359:
	add	x0, x29, 156
	mov	w24, 65535
	str	x0, [x29,136]
	b	.L2360
.L2358:
	ldr	x19, [x19, #:got_lo12:g_gc_superblock]
	mov	w0, -1
	strh	w0, [x19]
	b	.L2454
.L2378:
	ldr	x1, [x19, #:got_lo12:g_gc_superblock]
	ldrh	w0, [x1,2]
	add	w20, w20, w0
	adrp	x0, :got:c_ftl_nand_page_pre_blk
	uxth	w20, w20
	strh	w20, [x1,2]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w0, [x0]
	cmp	w0, w20
	bls	.L2380
	adrp	x1, :got:g_gc_cur_blk_valid_pages
	adrp	x0, :got:g_gc_cur_blk_max_valid_pages
	ldr	x1, [x1, #:got_lo12:g_gc_cur_blk_valid_pages]
	ldr	x0, [x0, #:got_lo12:g_gc_cur_blk_max_valid_pages]
	ldrh	w1, [x1]
	ldrh	w0, [x0]
	cmp	w1, w0
	beq	.L2380
.L2381:
	adrp	x0, :got:g_num_free_superblocks
	ldr	x0, [x0, #:got_lo12:g_num_free_superblocks]
	ldrh	w0, [x0]
	cmp	w0, 2
	bhi	.L2384
	adrp	x0, :got:c_ftl_nand_page_pre_blk
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w20, [x0]
.L2360:
	ldr	x0, [x19, #:got_lo12:g_gc_superblock]
	ldrh	w0, [x0]
	cmp	w0, w24
	bne	.L2361
	adrp	x0, :got:g_in_swl_replace
	adrp	x23, :got:c_ftl_nand_planes_num
	mov	w25, 2
	ldr	x0, [x0, #:got_lo12:g_in_swl_replace]
	str	wzr, [x0]
.L2362:
	adrp	x0, :got:g_gc_blk_index
	adrp	x26, :got:g_gc_blk_index
	ldr	x27, [x0, #:got_lo12:g_gc_blk_index]
	ldrh	w0, [x27]
	bl	List_get_gc_head_node
	ldr	x28, [x19, #:got_lo12:g_gc_superblock]
	uxth	w0, w0
	cmp	w0, w24
	strh	w0, [x28]
	bne	.L2363
	strh	wzr, [x27]
	mov	w2, 8
	b	.L2431
.L2363:
	bl	IsBlkInGcList
	cbz	w0, .L2364
	ldrh	w0, [x27]
	add	w0, w0, 1
	strh	w0, [x27]
	b	.L2362
.L2364:
	ldrh	w0, [x27]
	adrp	x1, :got:p_valid_page_count_table
	ldrh	w2, [x28]
	add	w0, w0, 1
	lsl	x2, x2, 1
	uxth	w0, w0
	strh	w0, [x27]
	ldr	x1, [x1, #:got_lo12:p_valid_page_count_table]
	ldr	x5, [x23, #:got_lo12:c_ftl_nand_planes_num]
	ldr	x3, [x1]
	adrp	x1, :got:c_ftl_nand_page_pre_blk
	ldrh	w5, [x5]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w4, [x3,x2]
	ldrh	w1, [x1]
	mul	w1, w1, w5
	sdiv	w5, w1, w25
	cmp	w4, w5
	bgt	.L2366
	cmp	w4, 8
	bls	.L2367
	cmp	w0, 48
	bls	.L2367
	adrp	x0, :got:g_gc_blk_num
	ldr	x0, [x0, #:got_lo12:g_gc_blk_num]
	ldrh	w0, [x0]
	cmp	w0, 35
	bhi	.L2367
.L2366:
	ldr	x0, [x26, #:got_lo12:g_gc_blk_index]
	strh	wzr, [x0]
.L2367:
	cmp	w21, w24
	ldrh	w0, [x3,x2]
	bne	.L2368
	cmp	w0, w1
	blt	.L2368
	ldr	x19, [x19, #:got_lo12:g_gc_superblock]
	mov	w0, -1
	strh	w0, [x19]
	ldr	x26, [x26, #:got_lo12:g_gc_blk_index]
	strh	wzr, [x26]
	b	.L2454
.L2368:
	cbnz	w0, .L2369
	mov	w0, -1
	bl	decrement_vpc_count
	ldr	x26, [x26, #:got_lo12:g_gc_blk_index]
	ldrh	w0, [x26]
	add	w0, w0, 1
	strh	w0, [x26]
	b	.L2362
.L2369:
	ldr	x23, [x19, #:got_lo12:g_gc_superblock]
	mov	x0, x23
	strb	wzr, [x23,8]
	bl	make_superblock
	adrp	x0, :got:g_gc_cur_blk_valid_pages
	ldrh	w1, [x23]
	ldr	x0, [x0, #:got_lo12:g_gc_cur_blk_valid_pages]
	strh	wzr, [x0]
	adrp	x0, :got:p_valid_page_count_table
	ldr	x0, [x0, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x0]
	ldrh	w1, [x0,x1,lsl 1]
	adrp	x0, :got:g_gc_cur_blk_max_valid_pages
	ldr	x0, [x0, #:got_lo12:g_gc_cur_blk_max_valid_pages]
	strh	wzr, [x23,2]
	strb	wzr, [x23,6]
	strh	w1, [x0]
.L2361:
	bl	FtlReadRefresh
	ldr	x0, [x22, #:got_lo12:g_in_gc_progress]
	mov	w1, 1
	str	w1, [x0]
	ldr	x0, [x19, #:got_lo12:g_gc_superblock]
	ldrh	w1, [x0,2]
	adrp	x0, :got:c_ftl_nand_page_pre_blk
	add	w2, w1, w20
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w0, [x0]
	cmp	w2, w0
	ble	.L2370
	sub	w20, w0, w1
	uxth	w20, w20
.L2370:
	mov	w25, 0
.L2371:
	cmp	w20, w25, uxth
	bls	.L2378
	adrp	x0, :got:c_ftl_nand_planes_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w4, [x0]
	ldr	x0, [x19, #:got_lo12:g_gc_superblock]
	ldrh	w2, [x0,2]
	mov	x0, 0
	mov	w23, w0
	add	w2, w2, w25
.L2379:
	cmp	w4, w0, uxth
	bls	.L2456
	ldr	x1, [x19, #:got_lo12:g_gc_superblock]
	add	x1, x1, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w24
	beq	.L2372
	adrp	x5, :got:req_gc
	mov	w3, 56
	orr	w1, w2, w1, lsl 10
	umull	x3, w23, w3
	ldr	x5, [x5, #:got_lo12:req_gc]
	add	w23, w23, 1
	uxth	w23, w23
	ldr	x5, [x5]
	add	x3, x5, x3
	str	w1, [x3,4]
.L2372:
	add	x0, x0, 1
	b	.L2379
.L2456:
	adrp	x0, :got:req_gc
	mov	w1, w23
	mov	w2, 0
	mov	x26, 0
	mov	x27, 56
	ldr	x0, [x0, #:got_lo12:req_gc]
	ldr	x0, [x0]
	bl	FlashReadPages
.L2374:
	cmp	w23, w26, uxth
	bls	.L2457
	adrp	x0, :got:req_gc
	mul	x4, x26, x27
	ldr	x6, [x0, #:got_lo12:req_gc]
	ldr	x0, [x6]
	add	x1, x0, x4
	ldr	w0, [x0,x4]
	cmn	w0, #1
	ldr	x5, [x1,16]
	beq	.L2394
	ldrh	w0, [x5]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2394
	ldr	w0, [x5,8]
	mov	w2, 0
	ldr	x1, [x29,136]
	str	x6, [x29,112]
	str	x4, [x29,120]
	str	x5, [x29,128]
	bl	log2phys
	ldr	x6, [x29,112]
	ldr	x4, [x29,120]
	ldr	w1, [x29,156]
	ldr	x2, [x6]
	ldr	x5, [x29,128]
	add	x2, x2, x4
	ldr	w0, [x2,4]
	cmp	w1, w0
	bne	.L2394
	adrp	x0, :got:g_gc_cur_blk_valid_pages
	str	x6, [x29,96]
	str	x4, [x29,104]
	str	x5, [x29,112]
	ldr	x1, [x0, #:got_lo12:g_gc_cur_blk_valid_pages]
	ldrh	w0, [x1]
	add	w0, w0, 1
	strh	w0, [x1]
	adrp	x0, :got:g_gc_num_req
	ldr	x28, [x0, #:got_lo12:g_gc_num_req]
	adrp	x0, :got:req_gc_dst
	ldr	x7, [x0, #:got_lo12:req_gc_dst]
	ldr	w0, [x28]
	str	x7, [x29,120]
	ldr	x1, [x7]
	madd	x1, x0, x27, x1
	ldr	w0, [x2,24]
	str	x1, [x29,128]
	str	w0, [x1,24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29,128]
	ldr	x7, [x29,120]
	ldr	x6, [x29,96]
	str	w0, [x1,4]
	ldr	w0, [x28]
	ldr	x1, [x7]
	ldr	x4, [x29,104]
	ldr	x5, [x29,112]
	madd	x0, x0, x27, x1
	ldr	x1, [x6]
	add	x1, x1, x4
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	adrp	x2, :got:g_gc_temp_superblock
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,156]
	str	w0, [x5,12]
	ldr	x2, [x2, #:got_lo12:g_gc_temp_superblock]
	str	x2, [x29,128]
	ldrh	w0, [x2]
	strh	w0, [x5,2]
	adrp	x0, :got:g_GlobalDataVersion
	ldr	x0, [x0, #:got_lo12:g_GlobalDataVersion]
	ldr	w0, [x0]
	str	w0, [x5,4]
	ldr	w0, [x28]
	add	w0, w0, 1
	str	w0, [x28]
	ldr	x0, [x6]
	add	x0, x0, x4
	bl	FtlGcBufAlloc
	ldr	x2, [x29,128]
	ldr	w1, [x28]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2376
	ldrh	w0, [x2,4]
	cbnz	w0, .L2394
.L2376:
	bl	Ftl_gc_temp_data_write_back
	cbnz	w0, .L2455
.L2394:
	add	x26, x26, 1
	b	.L2374
.L2457:
	add	w25, w25, 1
	b	.L2371
.L2380:
	adrp	x0, :got:g_gc_num_req
	ldr	x0, [x0, #:got_lo12:g_gc_num_req]
	ldr	w0, [x0]
	cbz	w0, .L2382
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2382
.L2455:
	ldr	x22, [x22, #:got_lo12:g_in_gc_progress]
	str	wzr, [x22]
.L2454:
	adrp	x0, :got:gc_ink_free_return_value
	ldr	x0, [x0, #:got_lo12:gc_ink_free_return_value]
	ldrh	w2, [x0]
	b	.L2431
.L2382:
	adrp	x0, :got:g_gc_cur_blk_valid_pages
	ldr	x0, [x0, #:got_lo12:g_gc_cur_blk_valid_pages]
	ldrh	w3, [x0]
	cbnz	w3, .L2383
	adrp	x0, :got:p_valid_page_count_table
	ldr	x1, [x19, #:got_lo12:g_gc_superblock]
	ldr	x2, [x0, #:got_lo12:p_valid_page_count_table]
	ldrh	w0, [x1]
	ldr	x2, [x2]
	lsl	x0, x0, 1
	ldrh	w4, [x2,x0]
	cbz	w4, .L2383
	strh	w3, [x2,x0]
	ldrh	w0, [x1]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2383:
	ldr	x0, [x19, #:got_lo12:g_gc_superblock]
	mov	w1, -1
	strh	w1, [x0]
	b	.L2381
.L2384:
	ldr	x22, [x22, #:got_lo12:g_in_gc_progress]
	adrp	x1, :got:gc_ink_free_return_value
	str	wzr, [x22]
	ldr	x1, [x1, #:got_lo12:gc_ink_free_return_value]
	ldrh	w2, [x1]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
.L2431:
	mov	w0, w2
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	stp	x29, x30, [sp, -192]!
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	cmp	w0, 16
	mov	w19, w1
	mov	w28, w2
	mov	x25, x3
	bne	.L2459
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L2460
.L2459:
	add	w0, w1, w2
	str	w0, [x29,172]
	adrp	x0, :got:g_MaxLbaSector
	ldr	w2, [x29,172]
	ldr	x0, [x0, #:got_lo12:g_MaxLbaSector]
	ldr	w1, [x0]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L2460
	adrp	x23, :got:c_ftl_nand_sec_pre_page
	sub	w24, w2, #1
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w0, [x0]
	udiv	w27, w19, w0
	udiv	w24, w24, w0
	adrp	x0, :got:g_totle_read_sector
	sub	w22, w24, w27
	ldr	x0, [x0, #:got_lo12:g_totle_read_sector]
	add	w22, w22, 1
	ldr	w1, [x0]
	add	w1, w28, w1
	str	w1, [x0]
	adrp	x0, :got:g_totle_read_page_count
	ldr	x0, [x0, #:got_lo12:g_totle_read_page_count]
	ldr	w1, [x0]
	add	w1, w22, w1
	str	w1, [x0]
	mov	w0, w27
	mov	w1, w24
	bl	FtlCacheMetchLpa
	cbz	w0, .L2461
	bl	FtlCacheWriteBack
.L2461:
	mov	w26, 0
	add	x0, x29, 188
	mov	w20, w27
	str	w26, [x29,168]
	mov	w21, w26
	str	w26, [x29,156]
	str	x0, [x29,112]
.L2462:
	cbz	w22, .L2504
	ldr	x1, [x29,112]
	mov	w0, w20
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,188]
	cmn	w3, #1
	bne	.L2501
	mov	w3, 0
.L2463:
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w0, [x0]
	cmp	w3, w0
	bcs	.L2467
	madd	w0, w20, w0, w3
	cmp	w0, w19
	bcc	.L2465
	ldr	w1, [x29,172]
	cmp	w0, w1
	bcs	.L2465
	sub	w0, w0, w19
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x25, x0
	str	x3, [x29,160]
	bl	ftl_memset
	ldr	x3, [x29,160]
.L2465:
	add	w3, w3, 1
	b	.L2463
.L2501:
	mov	w0, 56
	cmp	w20, w27
	umull	x1, w21, w0
	adrp	x0, :got:req_read
	ldr	x0, [x0, #:got_lo12:req_read]
	ldr	x2, [x0]
	add	x2, x2, x1
	str	w3, [x2,4]
	bne	.L2468
	adrp	x2, :got:p_io_data_buf_0
	ldr	x0, [x0]
	add	x0, x0, x1
	ldr	x2, [x2, #:got_lo12:p_io_data_buf_0]
	ldr	x2, [x2]
	str	x2, [x0,8]
	ldr	x2, [x23, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w2, [x2]
	udiv	w3, w19, w2
	msub	w3, w3, w2, w19
	str	w3, [x29,128]
	sub	w3, w2, w3
	cmp	w3, w28
	csel	w3, w3, w28, ls
	str	w3, [x29,168]
	cmp	w3, w2
	bne	.L2469
	str	x25, [x0,8]
	b	.L2469
.L2468:
	ldr	x2, [x0]
	cmp	w20, w24
	add	x2, x2, x1
	bne	.L2470
	adrp	x0, :got:p_io_data_buf_1
	ldr	w4, [x29,172]
	ldr	x0, [x0, #:got_lo12:p_io_data_buf_1]
	ldr	x0, [x0]
	str	x0, [x2,8]
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w3, [x0]
	mul	w0, w20, w3
	sub	w26, w4, w0
	cmp	w26, w3
	bne	.L2469
	b	.L2502
.L2470:
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w0, [x0]
	mul	w0, w20, w0
.L2502:
	sub	w0, w0, w19
	ubfiz	x0, x0, 9, 23
	add	x0, x25, x0
	str	x0, [x2,8]
.L2469:
	adrp	x0, :got:req_read
	adrp	x2, :got:p_io_spare_buf
	ldr	x0, [x0, #:got_lo12:req_read]
	ldr	x2, [x2, #:got_lo12:p_io_spare_buf]
	ldr	x0, [x0]
	ldr	x2, [x2]
	add	x1, x0, x1
	adrp	x0, :got:c_ftl_nand_byte_pre_oob
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_byte_pre_oob]
	str	w20, [x1,24]
	ldrh	w0, [x0]
	mul	w0, w21, w0
	add	w21, w21, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L2467:
	subs	w22, w22, #1
	add	w20, w20, 1
	beq	.L2471
	adrp	x0, :got:c_ftl_nand_planes_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w21, w0, lsl 3
	bne	.L2462
.L2471:
	cbz	w21, .L2462
	adrp	x0, :got:req_read
	mov	w1, w21
	mov	w2, 0
	ldr	x0, [x0, #:got_lo12:req_read]
	ldr	x0, [x0]
	bl	FlashReadPages
	str	xzr, [x29,160]
	ldr	x0, [x29,128]
	ubfiz	x0, x0, 9, 23
	str	x0, [x29,144]
	ldr	w0, [x29,168]
	lsl	w0, w0, 9
	str	w0, [x29,152]
	lsl	w0, w26, 9
	str	w0, [x29,124]
.L2473:
	ldr	w0, [x29,160]
	cmp	w21, w0
	bls	.L2505
	ldr	x0, [x29,160]
	mov	x1, 56
	mul	x3, x0, x1
	adrp	x0, :got:req_read
	ldr	x0, [x0, #:got_lo12:req_read]
	ldr	x0, [x0]
	add	x0, x0, x3
	ldr	w1, [x0,24]
	cmp	w1, w27
	bne	.L2474
	ldr	x1, [x0,8]
	adrp	x0, :got:p_io_data_buf_0
	ldr	x0, [x0, #:got_lo12:p_io_data_buf_0]
	ldr	x0, [x0]
	cmp	x1, x0
	bne	.L2475
	ldr	x2, [x29,144]
	mov	x0, x25
	str	x3, [x29,104]
	add	x1, x1, x2
	ldr	w2, [x29,152]
	b	.L2503
.L2474:
	cmp	w1, w24
	bne	.L2475
	ldr	x1, [x0,8]
	adrp	x0, :got:p_io_data_buf_1
	ldr	x0, [x0, #:got_lo12:p_io_data_buf_1]
	ldr	x0, [x0]
	cmp	x1, x0
	bne	.L2475
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldr	w2, [x29,124]
	str	x3, [x29,104]
	ldrh	w0, [x0]
	mul	w0, w24, w0
	sub	w0, w0, w19
	ubfiz	x0, x0, 9, 23
	add	x0, x25, x0
.L2503:
	bl	ftl_memcpy
	ldr	x3, [x29,104]
.L2475:
	adrp	x0, :got:req_read
	ldr	x0, [x0, #:got_lo12:req_read]
	ldr	x0, [x0]
	add	x5, x0, x3
	ldr	w2, [x0,x3]
	cmn	w2, #1
	bne	.L2476
	adrp	x1, :got:g_sys_ext_data
	str	w2, [x29,156]
	ldr	x4, [x1, #:got_lo12:g_sys_ext_data]
	ldr	w1, [x4,72]
	add	w1, w1, 1
	str	w1, [x4,72]
.L2476:
	ldr	w0, [x0,x3]
	cmp	w0, 256
	bne	.L2477
	ldr	w0, [x5,4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2477:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2473
.L2505:
	mov	w21, 0
	b	.L2462
.L2504:
	adrp	x0, :got:g_gc_bad_block_temp_num
	ldr	x0, [x0, #:got_lo12:g_gc_bad_block_temp_num]
	ldrh	w0, [x0]
	cbz	w0, .L2480
	mov	w0, w22
	mov	w1, 1
	bl	rk_ftl_garbage_collect
.L2480:
	ldr	w0, [x29,156]
.L2460:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	StorageSysDataLoad
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	stp	x29, x30, [sp, -48]!
	mov	x2, 512
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w19, w0
	mov	x0, x1
	str	x1, [x29,40]
	mov	w1, 0
	bl	memset
	bl	rknand_device_lock
	ldr	x3, [x29,40]
	add	w1, w19, 256
	mov	w2, 1
	mov	w0, 16
	bl	FtlRead
	str	x0, [x29,40]
	bl	rknand_device_unlock
	ldr	x19, [sp,16]
	ldr	x0, [x29,40]
	ldp	x29, x30, [sp], 48
	ret
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	stp	x29, x30, [sp, -320]!
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
	bne	.L2508
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartWrite
	b	.L2509
.L2508:
	adrp	x0, :got:g_MaxLbaSector
	add	w2, w1, w2
	ldr	x0, [x0, #:got_lo12:g_MaxLbaSector]
	ldr	w1, [x0]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L2509
	adrp	x0, :got:g_ftl_nand_free_count
	adrp	x3, :got:c_ftl_nand_sec_pre_page
	mov	w1, 2048
	sub	w2, w2, #1
	str	x3, [x29,248]
	ldr	x0, [x0, #:got_lo12:g_ftl_nand_free_count]
	str	w1, [x0]
	ldr	x0, [x3, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w1, [x0]
	udiv	w0, w2, w1
	cmp	w24, w1, lsl 1
	str	w0, [x29,220]
	udiv	w26, w23, w1
	sub	w27, w0, w26
	adrp	x0, :got:g_totle_write_page_count
	add	w21, w27, 1
	ldr	x0, [x0, #:got_lo12:g_totle_write_page_count]
	ldr	w2, [x0]
	add	w2, w21, w2
	str	w2, [x0]
	adrp	x0, :got:g_totle_write_sector
	ldr	x0, [x0, #:got_lo12:g_totle_write_sector]
	ldr	w2, [x0]
	add	w2, w24, w2
	str	w2, [x0]
	cset	w0, cs
	cmp	w24, 8
	str	w0, [x29,236]
	bhi	.L2547
	adrp	x19, :got:g_buffer_superblock
	ldr	x19, [x19, #:got_lo12:g_buffer_superblock]
	b	.L2510
.L2547:
	adrp	x19, :got:g_active_superblock
	ldr	x19, [x19, #:got_lo12:g_active_superblock]
.L2510:
	adrp	x22, :got:g_wr_page_num
	ldr	x0, [x22, #:got_lo12:g_wr_page_num]
	ldr	w3, [x0]
	cbz	w3, .L2511
	adrp	x0, :got:req_wr_io
	sub	w3, w3, #1
	mov	w2, 56
	adrp	x20, :got:last_cache_match_count
	ldr	x0, [x0, #:got_lo12:req_wr_io]
	umull	x3, w3, w2
	ldr	x0, [x0]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w26, w0
	bne	.L2512
	adrp	x0, :got:g_totle_cache_write_count
	ldr	x0, [x0, #:got_lo12:g_totle_cache_write_count]
	ldr	w2, [x0]
	add	w2, w2, 1
	str	w2, [x0]
	ldr	x28, [x20, #:got_lo12:last_cache_match_count]
	ldr	w0, [x28]
	add	w0, w0, 1
	str	w0, [x28]
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
	cbnz	w27, .L2513
	ldr	w1, [x28]
	mov	w0, w27
	cmp	w1, 2
	ble	.L2509
.L2513:
	add	x25, x25, x21
	sub	w24, w24, w19
	add	w23, w23, w19
	add	w26, w26, 1
	mov	w21, w27
.L2512:
	ldr	x20, [x20, #:got_lo12:last_cache_match_count]
	adrp	x0, :got:gp_last_act_superblock
	str	wzr, [x20]
	ldr	x0, [x0, #:got_lo12:gp_last_act_superblock]
	ldr	x19, [x0]
.L2511:
	ldr	w1, [x29,220]
	mov	w0, w26
	bl	FtlCacheMetchLpa
	cbz	w0, .L2514
	bl	FtlCacheWriteBack
.L2514:
	adrp	x0, :got:gp_last_act_superblock
	str	x0, [x29,168]
	str	w26, [x29,232]
	ldr	x1, [x0, #:got_lo12:gp_last_act_superblock]
	add	x0, x29, 260
	str	x0, [x29,144]
	add	x0, x29, 264
	str	x0, [x29,240]
	adrp	x0, .LC118
	add	x0, x0, :lo12:.LC118
	str	x19, [x1]
	str	x0, [x29,136]
.L2515:
	cbz	w21, .L2572
	ldrh	w1, [x19,4]
	cbnz	w1, .L2516
	adrp	x0, :got:g_active_superblock
	adrp	x20, :got:g_active_superblock
	ldr	x0, [x0, #:got_lo12:g_active_superblock]
	cmp	x19, x0
	bne	.L2517
	adrp	x19, :got:g_buffer_superblock
	ldr	x0, [x19, #:got_lo12:g_buffer_superblock]
	ldrh	w27, [x0,4]
	cbnz	w27, .L2518
	bl	allocate_new_data_superblock
	adrp	x0, :got:power_up_flag
	ldr	x0, [x0, #:got_lo12:power_up_flag]
	str	w27, [x0]
.L2518:
	ldr	x0, [x20, #:got_lo12:g_active_superblock]
	bl	allocate_new_data_superblock
	adrp	x0, :got:power_up_flag
	ldr	x0, [x0, #:got_lo12:power_up_flag]
	ldr	w0, [x0]
	cbnz	w0, .L2549
.L2520:
	ldr	x19, [x20, #:got_lo12:g_active_superblock]
	b	.L2519
.L2517:
	adrp	x2, :got:power_up_flag
	ldrh	w0, [x0,4]
	ldr	x2, [x2, #:got_lo12:power_up_flag]
	str	w1, [x2]
	cbnz	w0, .L2520
	mov	x0, x19
	bl	allocate_new_data_superblock
	b	.L2519
.L2549:
	ldr	x19, [x19, #:got_lo12:g_buffer_superblock]
.L2519:
	ldrh	w0, [x19,4]
	cbnz	w0, .L2521
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2521:
	ldr	x0, [x29,168]
	ldr	x0, [x0, #:got_lo12:gp_last_act_superblock]
	str	x19, [x0]
.L2516:
	adrp	x2, :got:c_wr_page_buf_num
	ldr	x3, [x22, #:got_lo12:g_wr_page_num]
	ldrh	w1, [x19,4]
	str	xzr, [x29,224]
	ldr	x0, [x2, #:got_lo12:c_wr_page_buf_num]
	ldr	w3, [x3]
	str	x2, [x29,160]
	ldr	w0, [x0]
	sub	w0, w0, w3
	cmp	w1, w0
	csel	w0, w1, w0, ls
	cmp	w0, w21
	csel	w0, w0, w21, ls
	str	w0, [x29,212]
	ldr	w0, [x29,236]
	and	w0, w0, 1
	str	w0, [x29,156]
	add	w0, w24, w23
	str	w0, [x29,208]
.L2522:
	ldr	w0, [x29,224]
	ldr	w2, [x29,212]
	ldr	w1, [x29,232]
	cmp	w0, w2
	add	w28, w1, w0
	bcs	.L2573
	ldrh	w1, [x19,4]
	cbz	w1, .L2551
	ldr	w1, [x29,220]
	cmp	w28, w1
	cset	w10, eq
	cbz	w0, .L2524
	ldr	w1, [x29,156]
	tst	w10, w1
	beq	.L2524
	ldr	x1, [x29,248]
	ldr	w2, [x29,208]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w1, [x1]
	msub	w2, w28, w1, w2
	cmp	w2, w1
	bne	.L2551
.L2524:
	ldr	x1, [x29,144]
	mov	w2, 0
	mov	w0, w28
	str	x10, [x29,96]
	bl	log2phys
	mov	x0, x19
	bl	get_new_active_ppa
	ldr	x8, [x22, #:got_lo12:g_wr_page_num]
	adrp	x4, :got:req_wr_io
	mov	w7, 56
	str	x4, [x29,120]
	str	x8, [x29,104]
	ldr	x6, [x4, #:got_lo12:req_wr_io]
	ldr	w9, [x8]
	str	x7, [x29,112]
	ldr	x2, [x6]
	umull	x1, w9, w7
	str	x6, [x29,128]
	add	x2, x2, x1
	str	w0, [x2,4]
	ldr	x0, [x6]
	add	x1, x0, x1
	adrp	x0, :got:c_ftl_nand_byte_pre_oob
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_byte_pre_oob]
	ldrh	w2, [x0]
	adrp	x0, :got:p_wr_io_spare_buf
	ldr	x0, [x0, #:got_lo12:p_wr_io_spare_buf]
	mul	w5, w2, w9
	ldr	x0, [x0]
	and	x3, x5, 4294967292
	adrp	x5, :got:c_ftl_nand_byte_pre_page
	str	x0, [x29,176]
	add	x27, x0, x3
	str	x27, [x1,16]
	str	x5, [x29,192]
	ldr	x20, [x5, #:got_lo12:c_ftl_nand_byte_pre_page]
	str	x3, [x29,184]
	ldrh	w0, [x20]
	mul	w0, w0, w9
	adrp	x9, :got:p_wr_io_data_buf
	and	x0, x0, 4294967292
	ldr	x9, [x9, #:got_lo12:p_wr_io_data_buf]
	str	w28, [x1,24]
	ldr	x9, [x9]
	add	x0, x9, x0
	str	x0, [x1,8]
	mov	x0, x27
	mov	w1, 0
	bl	ftl_memset
	cmp	w28, w26
	ldr	x4, [x29,120]
	cset	w0, eq
	str	w0, [x29,216]
	ldr	x5, [x29,192]
	cbnz	w0, .L2554
	ldr	x10, [x29,96]
	ldr	x6, [x29,128]
	ldr	x7, [x29,112]
	ldr	x8, [x29,104]
	cbz	w10, .L2525
	ldr	x0, [x29,248]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w20, [x0]
	ldr	w0, [x29,208]
	msub	w20, w28, w20, w0
	ldr	w0, [x29,216]
	str	w0, [x29,192]
	uxth	w20, w20
	b	.L2528
.L2554:
	ldr	x0, [x29,248]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w20, [x0]
	udiv	w0, w23, w20
	msub	w0, w0, w20, w23
	str	w0, [x29,192]
	sub	w20, w20, w0
	cmp	w20, w24
	csel	w20, w20, w24, ls
.L2528:
	ldr	x0, [x29,248]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w0, [x0]
	cmp	w20, w0
	bne	.L2529
	ldr	w0, [x29,216]
	mov	x1, x25
	cbnz	w0, .L2530
	mul	w1, w20, w28
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x25, x1
.L2530:
	ldr	w2, [x29,236]
	mov	w0, 56
	cbz	w2, .L2531
	ldr	x2, [x22, #:got_lo12:g_wr_page_num]
	ldr	x4, [x4, #:got_lo12:req_wr_io]
	ldr	w2, [x2]
	umull	x0, w2, w0
	ldr	x2, [x4]
	add	x0, x2, x0
	str	x1, [x0,8]
	b	.L2532
.L2531:
	ldr	x2, [x22, #:got_lo12:g_wr_page_num]
	ldr	x4, [x4, #:got_lo12:req_wr_io]
	ldr	x5, [x5, #:got_lo12:c_ftl_nand_byte_pre_page]
	ldr	w2, [x2]
	umull	x0, w2, w0
	ldr	x2, [x4]
	add	x0, x2, x0
	ldrh	w2, [x5]
	ldr	x0, [x0,8]
	b	.L2571
.L2529:
	ldr	w0, [x29,260]
	cmn	w0, #1
	beq	.L2533
	ldr	x1, [x29,240]
	mov	w2, 56
	str	x4, [x29,128]
	str	w28, [x1,24]
	str	w0, [x1,4]
	ldr	x0, [x22, #:got_lo12:g_wr_page_num]
	ldr	x1, [x4, #:got_lo12:req_wr_io]
	ldr	w0, [x0]
	ldr	x1, [x1]
	umull	x0, w0, w2
	ldr	x2, [x29,240]
	add	x0, x1, x0
	ldr	x1, [x0,8]
	ldr	x0, [x0,16]
	str	x1, [x2,8]
	mov	w1, 1
	str	x0, [x2,16]
	mov	x0, x2
	mov	w2, 0
	bl	FlashReadPages
	ldr	x0, [x29,240]
	ldr	x4, [x29,128]
	ldr	w0, [x0]
	cmn	w0, #1
	bne	.L2534
	adrp	x0, :got:g_sys_ext_data
	ldr	x0, [x0, #:got_lo12:g_sys_ext_data]
	ldr	w1, [x0,72]
	add	w1, w1, 1
	str	w1, [x0,72]
	b	.L2536
.L2534:
	ldr	w0, [x27,8]
	cmp	w0, w28
	beq	.L2536
	adrp	x0, :got:g_sys_ext_data
	mov	w2, w28
	str	x4, [x29,128]
	ldr	x0, [x0, #:got_lo12:g_sys_ext_data]
	ldr	w1, [x0,72]
	add	w1, w1, 1
	str	w1, [x0,72]
	ldr	x0, [x29,136]
	ldr	w1, [x27,8]
	bl	printk
	b	.L2570
.L2533:
	ldr	x0, [x22, #:got_lo12:g_wr_page_num]
	mov	w1, 56
	ldr	x5, [x5, #:got_lo12:c_ftl_nand_byte_pre_page]
	str	x4, [x29,128]
	ldr	w0, [x0]
	ldrh	w2, [x5]
	umull	x0, w0, w1
	ldr	x1, [x4, #:got_lo12:req_wr_io]
	ldr	x1, [x1]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L2570:
	ldr	x4, [x29,128]
.L2536:
	ldr	w1, [x29,216]
	mov	w0, 56
	lsl	w2, w20, 9
	cbz	w1, .L2537
	ldr	x1, [x22, #:got_lo12:g_wr_page_num]
	ldr	x4, [x4, #:got_lo12:req_wr_io]
	ldr	w5, [x1]
	ldr	x4, [x4]
	umull	x0, w5, w0
	add	x4, x4, x0
	ldr	x0, [x29,192]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x4,8]
	add	x0, x1, x0
	mov	x1, x25
	b	.L2571
.L2537:
	ldr	x1, [x22, #:got_lo12:g_wr_page_num]
	ldr	x4, [x4, #:got_lo12:req_wr_io]
	ldr	w5, [x1]
	ldr	x1, [x4]
	umull	x0, w5, w0
	add	x0, x1, x0
	ldr	x1, [x29,248]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldr	x0, [x0,8]
	ldrh	w1, [x1]
	mul	w1, w28, w1
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x25, x1
	b	.L2571
.L2525:
	ldr	w0, [x29,236]
	ldr	w1, [x8]
	cbz	w0, .L2538
	umull	x0, w1, w7
	ldr	x1, [x6]
	add	x1, x1, x0
	ldr	x0, [x29,248]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w0, [x0]
	mul	w0, w28, w0
	sub	w0, w0, w23
	ubfiz	x0, x0, 9, 23
	add	x0, x25, x0
	str	x0, [x1,8]
	b	.L2532
.L2538:
	umull	x0, w1, w7
	ldr	x1, [x6]
	ldrh	w2, [x20]
	add	x0, x1, x0
	ldr	x1, [x29,248]
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldr	x0, [x0,8]
	ldrh	w1, [x1]
	mul	w1, w28, w1
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x25, x1
.L2571:
	bl	ftl_memcpy
.L2532:
	ldr	x1, [x29,176]
	mov	w0, -3947
	ldr	x2, [x29,184]
	strh	w0, [x1,x2]
	adrp	x1, :got:g_GlobalDataVersion
	ldr	x1, [x1, #:got_lo12:g_GlobalDataVersion]
	str	w28, [x27,8]
	ldr	w0, [x1]
	str	w0, [x27,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1]
	ldr	w0, [x29,260]
	str	w0, [x27,12]
	ldrh	w0, [x19]
	strh	w0, [x27,2]
	ldr	x1, [x22, #:got_lo12:g_wr_page_num]
	ldr	w0, [x1]
	add	w0, w0, 1
	str	w0, [x1]
	ldr	x0, [x29,224]
	add	x0, x0, 1
	str	x0, [x29,224]
	b	.L2522
.L2573:
	str	w28, [x29,232]
	mov	x0, x2
	b	.L2523
.L2551:
	str	w28, [x29,232]
.L2523:
	sub	w21, w21, w0
	ldr	w0, [x29,236]
	cbnz	w0, .L2542
	ldr	x0, [x29,160]
	ldr	x1, [x22, #:got_lo12:g_wr_page_num]
	ldr	x0, [x0, #:got_lo12:c_wr_page_buf_num]
	ldr	w1, [x1]
	ldr	w0, [x0]
	cmp	w1, w0
	bcs	.L2542
	ldrh	w0, [x19,4]
	cbz	w0, .L2542
.L2544:
	str	wzr, [x29,236]
	b	.L2543
.L2542:
	bl	FtlCacheWriteBack
	ldr	x0, [x22, #:got_lo12:g_wr_page_num]
	cmp	w21, 3
	str	wzr, [x0]
	bls	.L2544
.L2543:
	bl	rknand_queue_cond_resched
	b	.L2515
.L2572:
	ldr	w1, [x29,220]
	mov	w0, w21
	sub	w1, w1, w26
	bl	rk_ftl_garbage_collect
	mov	w0, w21
.L2509:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 320
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
	.type	FlashReadFacBbtData.part.16, %function
FlashReadFacBbtData.part.16:
	stp	x29, x30, [sp, -32]!
	mov	w3, 0
	adrp	x4, :got:gFlashPageBuffer0
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	cbnz	w1, .L2578
	adrp	x1, :got:gNandFlashIdbBlockAddr
	mov	w7, 1
.L2581:
	ldr	x0, [x1, #:got_lo12:gNandFlashIdbBlockAddr]
	ldr	w0, [x0]
	cmp	w3, w0
	bcs	.L2578
	ldr	x0, [x4, #:got_lo12:gFlashPageBuffer0]
	ubfx	x5, x3, 5, 11
	lsl	x5, x5, 2
	lsl	w6, w7, w3
	add	w3, w3, 1
	ldr	x0, [x0]
	uxth	w3, w3
	ldr	w8, [x0,x5]
	orr	w6, w8, w6
	str	w6, [x0,x5]
	b	.L2581
.L2578:
	ldr	x4, [x4, #:got_lo12:gFlashPageBuffer0]
	mov	x0, x19
	ldr	x1, [x4]
	bl	ftl_memcpy
	mov	w2, 4
	adrp	x0, .LC119
	mov	x1, x19
	add	x0, x0, :lo12:.LC119
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
	adrp	x4, :got:gNandPhyInfo
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	ldr	x4, [x4, #:got_lo12:gNandPhyInfo]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x22, x0
	mov	w24, w1
	mov	w26, w2
	ldrh	w0, [x4,14]
	add	x23, x29, 104
	ldrh	w3, [x4,12]
	adrp	x4, :got:gFlashSpareBuffer
	mov	x21, x4
	mov	w27, 61664
	mul	w3, w3, w0
	adrp	x0, :got:gFlashPageBuffer0
	uxth	w3, w3
	ldr	x0, [x0, #:got_lo12:gFlashPageBuffer0]
	sub	w20, w3, #1
	mul	w25, w1, w3
	uxth	w20, w20
	sub	w19, w3, #16
	ldr	x0, [x0]
	str	x0, [x29,112]
	ldr	x0, [x4, #:got_lo12:gFlashSpareBuffer]
	ldr	x0, [x0]
	str	x0, [x29,120]
.L2583:
	cmp	w20, w19
	ble	.L2591
	add	w0, w20, w25
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x23,4]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L2584
	ldr	x0, [x21, #:got_lo12:gFlashSpareBuffer]
	ldr	x0, [x0]
	ldrh	w0, [x0]
	cmp	w0, w27
	bne	.L2584
	mov	w0, w22
	cbz	x22, .L2585
	mov	x0, x22
	mov	w1, w24
	mov	w2, w26
	bl	FlashReadFacBbtData.part.16
	b	.L2585
.L2584:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2583
.L2591:
	mov	w0, -1
.L2585:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	adrp	x2, :got:gpNandParaInfo
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	ldr	x2, [x2, #:got_lo12:gpNandParaInfo]
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x21, x0
	adrp	x20, :got:gFlashPageBuffer1
	ldr	x0, [x2]
	ldrb	w2, [x0,13]
	ldrh	w19, [x0,14]
	ldr	x0, [x20, #:got_lo12:gFlashPageBuffer1]
	mul	w19, w2, w19
	ldr	x0, [x0]
	uxth	w19, w19
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L2593
.L2597:
	mov	w1, 0
	b	.L2594
.L2593:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L2595:
	cmp	w2, w19
	bge	.L2594
	ldr	x3, [x20, #:got_lo12:gFlashPageBuffer1]
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L2596
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L2596:
	cmp	w1, w0
	bcs	.L2597
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2595
.L2594:
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:gBbtInfo
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	bl	FtlBbtMemInit
	mov	w21, 0
	bl	FtlLoadFactoryBbt
	ldr	x22, [x19, #:got_lo12:gBbtInfo]
	adrp	x25, :got:c_ftl_nand_blks_per_die
	mov	x23, x22
.L2603:
	adrp	x0, :got:c_ftl_nand_die_num
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_die_num]
	ldrh	w0, [x0]
	cmp	w21, w0
	bcs	.L2621
	adrp	x0, :got:p_sys_data_buf
	adrp	x1, :got:req_sys
	ldrh	w26, [x23,12]
	ldr	x0, [x0, #:got_lo12:p_sys_data_buf]
	ldr	x20, [x1, #:got_lo12:req_sys]
	adrp	x1, :got:p_sys_spare_buf
	ldr	x0, [x0]
	str	x0, [x20,8]
	ldr	x1, [x1, #:got_lo12:p_sys_spare_buf]
	ldr	x24, [x1]
	mov	w1, 65535
	str	x24, [x20,16]
	cmp	w26, w1
	beq	.L2604
	ldr	x24, [x25, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w1, [x24]
	madd	w26, w21, w1, w26
	mov	w1, 1
	lsl	w0, w26, 10
	mov	w2, w1
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x24]
	ldr	x0, [x22,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L2605
.L2604:
	mov	w1, w21
	adrp	x28, :got:c_ftl_nand_bbm_buf_size
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x22,32]
	bl	FtlBbt2Bitmap
	ldr	x0, [x25, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w20, [x0]
	sub	w20, w20, #1
	uxth	w20, w20
.L2606:
	ldr	x26, [x25, #:got_lo12:c_ftl_nand_blks_per_die]
	ldrh	w0, [x26]
	madd	w0, w21, w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	bne	.L2622
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2606
.L2622:
	adrp	x0, :got:p_sys_spare_buf
	strh	w20, [x23,12]
	mov	w1, 0
	mov	w2, 16
	ldr	x0, [x0, #:got_lo12:p_sys_spare_buf]
	ldr	x0, [x0]
	bl	ftl_memset
	str	wzr, [x24,4]
	mov	w0, -3872
	strh	w0, [x24]
	ldrh	w0, [x23,12]
	strh	w0, [x24,2]
	ldrh	w0, [x26]
	ldrh	w1, [x23,12]
	madd	w26, w21, w0, w1
	adrp	x0, :got:req_sys
	ldr	x1, [x22,32]
	ldr	x27, [x0, #:got_lo12:req_sys]
	lsl	w0, w26, 10
	str	w0, [x27,4]
	ldr	x0, [x28, #:got_lo12:c_ftl_nand_bbm_buf_size]
	ldrh	w2, [x0]
	ldr	x0, [x27,8]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	mov	w1, 1
	mov	x0, x27
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x27
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x27]
	cmn	w0, #1
	bne	.L2605
	mov	w0, w26
	bl	FtlBbmMapBadBlock
	b	.L2606
.L2605:
	mov	w0, w26
	add	w21, w21, 1
	add	x22, x22, 8
	add	x23, x23, 2
	bl	FtlBbmMapBadBlock
	b	.L2603
.L2621:
	mov	w20, 0
	adrp	x21, :got:c_ftl_nand_reserved_blks
.L2610:
	ldr	x0, [x21, #:got_lo12:c_ftl_nand_reserved_blks]
	ldrh	w0, [x0]
	cmp	w0, w20
	bls	.L2623
	mov	w0, w20
	add	w20, w20, 1
	bl	FtlBbmMapBadBlock
	uxth	w20, w20
	b	.L2610
.L2623:
	ldr	x0, [x19, #:got_lo12:gBbtInfo]
	mov	w22, 65535
	ldrh	w20, [x0,12]
	sub	w20, w20, #1
	uxth	w20, w20
.L2612:
	ldr	x21, [x19, #:got_lo12:gBbtInfo]
	ldrh	w0, [x21,12]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L2616
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L2613
	mov	w0, w20
	bl	FlashTestBlk
	cbz	w0, .L2614
	mov	w0, w20
	bl	FtlBbmMapBadBlock
	b	.L2613
.L2614:
	ldrh	w0, [x21]
	cmp	w0, w22
	bne	.L2615
	strh	w20, [x21]
	b	.L2613
.L2615:
	strh	w20, [x21,4]
	b	.L2616
.L2613:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L2612
.L2616:
	ldr	x19, [x19, #:got_lo12:gBbtInfo]
	adrp	x0, :got:req_erase
	str	wzr, [x19,8]
	strh	wzr, [x19,2]
	ldrh	w1, [x19]
	ldr	x0, [x0, #:got_lo12:req_erase]
	lsl	w1, w1, 10
	ldr	x2, [x0]
	str	w1, [x2,4]
	ldrh	w1, [x19,4]
	ldr	x0, [x0]
	lsl	w1, w1, 10
	str	w1, [x0,60]
	mov	w1, 2
	bl	FlashEraseBlocks
	ldrh	w0, [x19]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19,4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19,2]
	ldr	w0, [x19,8]
	ldrh	w1, [x19,4]
	add	w0, w0, 1
	str	w0, [x19,8]
	ldrh	w0, [x19]
	strh	w0, [x19,4]
	strh	w1, [x19]
	bl	FtlBbmTblFlush
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
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, :got:g_GlobalSysVersion
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	ldr	x0, [x19, #:got_lo12:g_GlobalSysVersion]
	adrp	x23, :got:c_ftl_nand_max_sys_blks
	str	wzr, [x0]
	adrp	x0, :got:g_GlobalDataVersion
	ldr	x0, [x0, #:got_lo12:g_GlobalDataVersion]
	str	wzr, [x0]
	ldr	x0, [x23, #:got_lo12:c_ftl_nand_max_sys_blks]
	ldrh	w0, [x0]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2625
	bl	FtlMakeBbt
.L2625:
	mov	w0, 0
	adrp	x3, :got:c_ftl_nand_sec_pre_page
	adrp	x4, :got:p_io_data_buf_0
	adrp	x5, :got:p_io_data_buf_1
.L2626:
	ldr	x1, [x3, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldrh	w1, [x1]
	cmp	w0, w1, lsl 7
	bge	.L2649
	ldr	x1, [x4, #:got_lo12:p_io_data_buf_0]
	ubfiz	x2, x0, 2, 16
	ldr	x6, [x1]
	mvn	w1, w0
	orr	w1, w0, w1, lsl 16
	add	w0, w0, 1
	str	w1, [x6,x2]
	uxth	w0, w0
	ldr	x1, [x5, #:got_lo12:p_io_data_buf_1]
	ldr	x6, [x1]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	str	w1, [x6,x2]
	b	.L2626
.L2649:
	adrp	x24, :got:c_ftl_nand_data_blks_per_plane
	mov	w21, 0
	adrp	x25, :got:c_ftl_nand_blk_pre_plane
	ldr	x0, [x24, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w22, [x0]
.L2628:
	ldr	x0, [x25, #:got_lo12:c_ftl_nand_blk_pre_plane]
	adrp	x20, :got:c_ftl_nand_blk_pre_plane
	ldrh	w0, [x0]
	cmp	w0, w22
	bls	.L2650
	mov	w0, w22
	mov	w1, 1
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w21, w0
	uxth	w22, w22
	uxth	w21, w0
	b	.L2628
.L2650:
	adrp	x22, :got:c_ftl_nand_planes_num
	sub	w1, w21, #3
	ldr	x0, [x22, #:got_lo12:c_ftl_nand_planes_num]
	ldrh	w0, [x0]
	cmp	w1, w0, lsl 1
	bge	.L2630
.L2634:
	mov	w21, 0
	mov	w23, w21
	b	.L2631
.L2630:
	udiv	w21, w21, w0
	adrp	x0, :got:c_ftl_nand_init_sys_blks_per_plane
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_init_sys_blks_per_plane]
	ldr	w0, [x0]
	add	w0, w21, w0
	bl	FtlSysBlkNumInit
	ldr	x23, [x23, #:got_lo12:c_ftl_nand_max_sys_blks]
	ldrh	w0, [x23]
	bl	FtlFreeSysBlkQueueInit
	ldr	x0, [x24, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w21, [x0]
.L2632:
	ldr	x0, [x20, #:got_lo12:c_ftl_nand_blk_pre_plane]
	ldrh	w0, [x0]
	cmp	w0, w21
	bls	.L2634
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	uxth	w21, w21
	b	.L2632
.L2631:
	ldr	x0, [x24, #:got_lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w0, [x0]
	cmp	w0, w23
	bls	.L2651
	mov	w0, w23
	mov	w1, 0
	add	w23, w23, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w21, w0
	uxth	w23, w23
	uxth	w21, w0
	b	.L2631
.L2651:
	adrp	x0, :got:g_cur_erase_blk
	ldr	x1, [x20, #:got_lo12:c_ftl_nand_blk_pre_plane]
	adrp	x2, :got:g_MaxLpn
	adrp	x5, :got:c_ftl_nand_data_op_blks_per_plane
	ldr	x0, [x0, #:got_lo12:g_cur_erase_blk]
	ldrh	w1, [x1]
	str	w1, [x0]
	adrp	x0, :got:c_ftl_nand_max_data_blks
	ldr	x22, [x22, #:got_lo12:c_ftl_nand_planes_num]
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_max_data_blks]
	ldr	x8, [x2, #:got_lo12:g_MaxLpn]
	ldrh	w6, [x22]
	ldr	w1, [x0]
	udiv	w0, w1, w6
	str	w0, [x8]
	ubfx	x7, x0, 5, 16
	ldr	x4, [x5, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	add	w3, w7, 36
	strh	w3, [x4]
	mov	w3, 24
	mul	w3, w6, w3
	cmp	w21, w3
	ble	.L2636
	sub	w1, w1, w21
	udiv	w1, w1, w6
	str	w1, [x8]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x4]
.L2636:
	adrp	x3, :got:g_inkDie_check_enable
	ldr	x3, [x3, #:got_lo12:g_inkDie_check_enable]
	ldr	w1, [x3]
	cmp	w1, 1
	bne	.L2637
	ldr	x1, [x5, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	udiv	w3, w21, w6
	ldrh	w4, [x1]
	add	w3, w4, w3
	add	w3, w4, w3, asr 2
	strh	w3, [x1]
.L2637:
	adrp	x3, :got:c_ftl_nand_ext_blk_pre_plane
	ldr	x3, [x3, #:got_lo12:c_ftl_nand_ext_blk_pre_plane]
	ldrh	w1, [x3]
	cbz	w1, .L2639
	ldr	x3, [x5, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	ldrh	w4, [x3]
	add	w4, w4, w1, lsr 1
	strh	w4, [x3]
	mul	w4, w1, w6
	cmp	w4, w21
	ble	.L2639
	ldr	x4, [x2, #:got_lo12:g_MaxLpn]
	add	w1, w1, 32
	add	w1, w7, w1
	strh	w1, [x3]
	str	w0, [x4]
.L2639:
	ldr	x1, [x5, #:got_lo12:c_ftl_nand_data_op_blks_per_plane]
	adrp	x22, :got:g_MaxLbn
	ldr	x2, [x2, #:got_lo12:g_MaxLpn]
	adrp	x21, :got:p_valid_page_count_table
	ldrh	w0, [x1]
	ldr	w1, [x2]
	sub	w0, w1, w0
	ldr	x1, [x22, #:got_lo12:g_MaxLbn]
	mul	w0, w0, w6
	str	w0, [x1]
	adrp	x1, :got:c_ftl_nand_page_pre_blk
	ldr	x1, [x1, #:got_lo12:c_ftl_nand_page_pre_blk]
	ldrh	w1, [x1]
	mul	w0, w0, w1
	str	w0, [x2]
	adrp	x2, :got:c_ftl_nand_sec_pre_page
	adrp	x1, :got:g_MaxLbaSector
	ldr	x2, [x2, #:got_lo12:c_ftl_nand_sec_pre_page]
	ldr	x1, [x1, #:got_lo12:g_MaxLbaSector]
	ldrh	w2, [x2]
	mul	w0, w0, w2
	str	w0, [x1]
	bl	FtlBbmTblFlush
	ldr	x0, [x20, #:got_lo12:c_ftl_nand_blk_pre_plane]
	mov	x20, x21
	ldr	x1, [x21, #:got_lo12:p_valid_page_count_table]
	ldrh	w2, [x0]
	ldr	x0, [x1]
	mov	w1, 0
	lsl	w2, w2, 1
	bl	ftl_memset
	adrp	x0, :got:g_VaildLpn
	mov	w1, -1
	mov	w23, w1
	ldr	x0, [x0, #:got_lo12:g_VaildLpn]
	str	wzr, [x0]
	adrp	x0, :got:g_gc_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_superblock]
	strh	w1, [x0]
	strh	wzr, [x0,2]
	strb	wzr, [x0,6]
	strb	wzr, [x0,8]
	adrp	x0, :got:g_active_superblock
	mov	x24, x0
	ldr	x2, [x0, #:got_lo12:g_active_superblock]
	strh	wzr, [x2,2]
	strb	wzr, [x2,6]
	strh	wzr, [x2]
	strb	wzr, [x2,8]
.L2641:
	ldr	x21, [x24, #:got_lo12:g_active_superblock]
	mov	x0, x21
	bl	make_superblock
	ldrb	w0, [x21,7]
	cbnz	w0, .L2642
	ldr	x0, [x20, #:got_lo12:p_valid_page_count_table]
	ldrh	w1, [x21]
	ldr	x0, [x0]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L2641
.L2642:
	ldr	x1, [x19, #:got_lo12:g_GlobalSysVersion]
	mov	w24, -1
	ldrh	w2, [x21,4]
	ldr	w0, [x1]
	str	w0, [x21,12]
	add	w0, w0, 1
	str	w0, [x1]
	ldrh	w1, [x21]
	ldr	x0, [x20, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x0]
	strh	w2, [x0,x1,lsl 1]
	adrp	x0, :got:g_buffer_superblock
	mov	x23, x0
	ldrh	w2, [x21]
	ldr	x1, [x0, #:got_lo12:g_buffer_superblock]
	add	w2, w2, 1
	strh	wzr, [x1,2]
	strh	w2, [x1]
	mov	w2, 1
	strb	wzr, [x1,6]
	strb	w2, [x1,8]
.L2643:
	ldr	x21, [x23, #:got_lo12:g_buffer_superblock]
	mov	x0, x21
	bl	make_superblock
	ldrb	w0, [x21,7]
	cbnz	w0, .L2644
	ldr	x0, [x20, #:got_lo12:p_valid_page_count_table]
	ldrh	w1, [x21]
	ldr	x0, [x0]
	strh	w24, [x0,x1,lsl 1]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L2643
.L2644:
	ldr	x19, [x19, #:got_lo12:g_GlobalSysVersion]
	ldrh	w1, [x21]
	ldrh	w2, [x21,4]
	ldr	w0, [x19]
	str	w0, [x21,12]
	add	w0, w0, 1
	str	w0, [x19]
	ldr	x20, [x20, #:got_lo12:p_valid_page_count_table]
	ldr	x0, [x20]
	mov	w20, -1
	strh	w2, [x0,x1,lsl 1]
	adrp	x0, :got:g_gc_temp_superblock
	ldr	x0, [x0, #:got_lo12:g_gc_temp_superblock]
	strh	w20, [x0]
	bl	FtlFreeSysBlkQueueOut
	adrp	x1, :got:gSysInfo
	ldr	x1, [x1, #:got_lo12:gSysInfo]
	strh	w0, [x1]
	strh	wzr, [x1,2]
	strh	w20, [x1,4]
	ldr	x22, [x22, #:got_lo12:g_MaxLbn]
	ldr	w0, [x22]
	strh	w0, [x1,6]
	ldr	w0, [x19]
	str	w0, [x1,8]
	add	w0, w0, 1
	str	w0, [x19]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L2645
	adrp	x0, :got:gFtlInitStatus
	mov	w1, 1
	ldr	x0, [x0, #:got_lo12:gFtlInitStatus]
	str	w1, [x0]
.L2645:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	adrp	x0, :got:RK29_NANDC_REG_BASE
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:RK29_NANDC_REG_BASE]
	str	x19, [sp,16]
	ldr	x0, [x0]
	bl	FlashInit
	mov	w1, 0
	cbnz	w0, .L2653
	bl	FlashLoadFactorBbt
	cbz	w0, .L2654
	bl	FlashMakeFactorBbt
.L2654:
	adrp	x0, :got:gFlashPageBuffer1
	ldr	x0, [x0, #:got_lo12:gFlashPageBuffer1]
	ldr	x0, [x0]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L2655
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L2657:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L2657
	cmp	w0, 6
	bhi	.L2658
	adrp	x0, :got:gNandIDBResBlkNum
	ldr	x0, [x0, #:got_lo12:gNandIDBResBlkNum]
	b	.L2682
.L2658:
	mov	w1, 0
	mov	w4, 1
.L2661:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L2661
	cmp	w0, 17
	adrp	x0, :got:gNandIDBResBlkNum
	ldr	x0, [x0, #:got_lo12:gNandIDBResBlkNum]
	bls	.L2682
	mov	w1, 36
.L2682:
	strb	w1, [x0]
	adrp	x1, :got:gNandIDBResBlkNum
	adrp	x0, :got:gNandPhyInfo
	ldr	x1, [x1, #:got_lo12:gNandIDBResBlkNum]
	ldr	x0, [x0, #:got_lo12:gNandPhyInfo]
	ldrb	w1, [x1]
	strh	w1, [x0,26]
.L2655:
	adrp	x0, .LC72
	adrp	x1, .LC73
	add	x1, x1, :lo12:.LC73
	add	x0, x0, :lo12:.LC72
	mov	w19, 1
	bl	printk
	adrp	x0, :got:gNandPhyInfo
	ldr	x0, [x0, #:got_lo12:gNandPhyInfo]
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	adrp	x0, :got:c_ftl_nand_max_sys_blks
	ldr	x0, [x0, #:got_lo12:c_ftl_nand_max_sys_blks]
	ldrh	w0, [x0]
	bl	FtlFreeSysBlkQueueInit
.L2663:
	bl	FtlLoadBbt
	cbz	w0, .L2664
.L2684:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L2685
	add	w19, w19, 1
	b	.L2663
.L2685:
	mov	w1, -1
	b	.L2653
.L2664:
	bl	FtlSysBlkInit
	cbnz	w0, .L2684
	adrp	x1, :got:gFtlInitStatus
	mov	w2, 1
	ldr	x1, [x1, #:got_lo12:gFtlInitStatus]
	str	w2, [x1]
	mov	w1, w0
.L2653:
	mov	w0, w1
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	IdBlockReadData
	.type	IdBlockReadData, %function
IdBlockReadData:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	mov	w20, w0
	adrp	x23, :got:gpNandParaInfo
	adrp	x0, :got:gBlockPageAlignSize
	stp	x21, x22, [sp,32]
	ldr	x21, [x23, #:got_lo12:gpNandParaInfo]
	mov	x22, x2
	ldr	x0, [x0, #:got_lo12:gBlockPageAlignSize]
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	mov	w27, w1
	mov	w1, w20
	ldr	w3, [x0]
	mov	w2, w27
	ldr	x0, [x21]
	mov	x26, x23
	ldrb	w19, [x0,9]
	adrp	x0, .LC120
	add	x0, x0, :lo12:.LC120
	mul	w19, w3, w19
	bl	printk
	ldr	x0, [x21]
	mov	w5, 4
	uxth	w19, w19
	mov	w21, 0
	adrp	x6, :got:slcPageToMlcPageTbl
	adrp	x7, :got:gFlashPageBuffer1
	ldrb	w28, [x0,9]
	adrp	x8, :got:gNandFlashEccBits
	udiv	w0, w20, w19
	adrp	x9, :got:gNandFlashIDBEccBits
	msub	w19, w0, w19, w20
	sub	w4, w20, w19
	mul	w3, w19, w28
	ubfx	x3, x3, 2, 2
.L2687:
	cmp	w21, w27
	bcs	.L2689
	add	w0, w21, w19
	ldr	x1, [x6, #:got_lo12:slcPageToMlcPageTbl]
	ubfx	x0, x0, 2, 16
	ldr	x25, [x7, #:got_lo12:gFlashPageBuffer1]
	sub	w23, w5, w3
	add	w3, w3, w4
	str	x5, [x29,104]
	ldrh	w24, [x1,w0,sxtw 1]
	uxth	w23, w23
	ldr	x0, [x26, #:got_lo12:gpNandParaInfo]
	ldr	x2, [x25]
	str	x4, [x29,112]
	ldr	x0, [x0]
	str	x6, [x29,120]
	str	x7, [x29,128]
	ldrb	w1, [x0,9]
	ldr	x0, [x8, #:got_lo12:gNandFlashEccBits]
	str	x8, [x29,152]
	str	x9, [x29,168]
	ldrb	w10, [x0]
	ldr	x0, [x9, #:got_lo12:gNandFlashIDBEccBits]
	madd	w24, w24, w28, w3
	str	x10, [x29,160]
	ldrb	w0, [x0]
	str	x2, [x29,136]
	str	x1, [x29,144]
	bl	FlashBchSel
	ldr	x1, [x29,144]
	mov	x3, 0
	ldr	x2, [x29,136]
	mov	w0, 0
	udiv	w1, w24, w1
	bl	FlashReadPage
	ldr	x10, [x29,160]
	mov	w0, w10
	bl	FlashBchSel
	ubfiz	x0, x21, 9, 16
	ldr	x1, [x25]
	add	x0, x22, x0
	mov	w2, 2048
	add	w21, w23, w21
	bl	ftl_memcpy
	uxth	w21, w21
	mov	w3, 0
	ldr	x5, [x29,104]
	ldr	x4, [x29,112]
	ldr	x6, [x29,120]
	ldr	x7, [x29,128]
	ldr	x8, [x29,152]
	ldr	x9, [x29,168]
	b	.L2687
.L2689:
	adrp	x0, .LC121
	mov	w1, w20
	mov	w2, w27
	mov	w3, 0
	add	x0, x0, :lo12:.LC121
	bl	printk
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
	ret
	.size	IdBlockReadData, .-IdBlockReadData
	.align	2
	.global	IDBlockWriteData
	.type	IDBlockWriteData, %function
IDBlockWriteData:
	stp	x29, x30, [sp, -224]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	adrp	x21, :got:gpNandParaInfo
	mov	w25, w0
	adrp	x0, :got:gBlockPageAlignSize
	stp	x23, x24, [sp,48]
	ldr	x22, [x21, #:got_lo12:gpNandParaInfo]
	mov	w24, 0
	ldr	x0, [x0, #:got_lo12:gBlockPageAlignSize]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w27, w1
	mov	x20, x2
	ldr	w3, [x0]
	mov	w1, w25
	ldr	x0, [x22]
	mov	w2, w27
	add	x28, x29, 160
	ldrb	w19, [x0,9]
	adrp	x0, .LC122
	add	x0, x0, :lo12:.LC122
	mul	w19, w3, w19
	bl	printk
	adrp	x0, :got:gNandPhyInfo
	mov	w2, 0
	uxth	w19, w19
	ldr	x0, [x0, #:got_lo12:gNandPhyInfo]
	ldrh	w1, [x0,20]
	mov	w0, 0
	udiv	w1, w25, w1
	bl	FlashEraseBlock
	udiv	w26, w25, w19
	ldr	x0, [x22]
	adrp	x5, :got:slcPageToMlcPageTbl
	adrp	x6, :got:gNandFlashEccBits
	adrp	x7, :got:gNandFlashIDBEccBits
	ldrb	w22, [x0,9]
	msub	w26, w26, w19, w25
	sub	w23, w25, w26
.L2691:
	cmp	w24, w27
	bcs	.L2697
	add	w4, w24, w26
	lsr	w4, w4, 2
	and	w19, w4, 65535
	uxth	w0, w4
	cbz	w0, .L2692
	ldr	x0, [x5, #:got_lo12:slcPageToMlcPageTbl]
	add	w1, w19, 1
	str	wzr, [x28,4]
	ldrh	w4, [x0,w1,sxtw 1]
	sub	w4, w4, #1
	lsl	w4, w4, 2
	str	w4, [x28]
.L2692:
	ldr	x4, [x5, #:got_lo12:slcPageToMlcPageTbl]
	ubfiz	x2, x24, 9, 16
	ldr	x0, [x6, #:got_lo12:gNandFlashEccBits]
	add	x2, x20, x2
	str	x5, [x29,104]
	add	w24, w24, 4
	ldrh	w1, [x4,w19,sxtw 1]
	ldrb	w8, [x0]
	uxth	w24, w24
	ldr	x0, [x7, #:got_lo12:gNandFlashIDBEccBits]
	madd	w1, w1, w22, w23
	str	x6, [x29,136]
	ldrb	w0, [x0]
	str	x7, [x29,152]
	str	x4, [x29,112]
	str	x8, [x29,144]
	str	x1, [x29,120]
	str	x2, [x29,128]
	bl	FlashBchSel
	ldr	x0, [x21, #:got_lo12:gpNandParaInfo]
	ldr	x1, [x29,120]
	ldr	x2, [x29,128]
	ldr	x0, [x0]
	ldrb	w3, [x0,9]
	mov	w0, 0
	udiv	w1, w1, w3
	mov	x3, x28
	bl	FlashProgPage
	ldr	x8, [x29,144]
	mov	w0, w8
	bl	FlashBchSel
	udiv	w1, w23, w22
	ldr	x4, [x29,112]
	mov	w0, 0
	ldrh	w2, [x4,w19,sxtw 1]
	add	w2, w2, 1
	bl	FlashPageProgMsbFFData
	ldr	x5, [x29,104]
	ldr	x6, [x29,136]
	ldr	x7, [x29,152]
	b	.L2691
.L2697:
	adrp	x0, .LC123
	mov	w1, w25
	mov	w2, w27
	mov	w3, 0
	add	x0, x0, :lo12:.LC123
	bl	printk
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 224
	ret
	.size	IDBlockWriteData, .-IDBlockWriteData
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	mov	w24, w0
	adrp	x0, :got:gpNandParaInfo
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	ldr	x0, [x0, #:got_lo12:gpNandParaInfo]
	mov	x21, x1
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w1, 192
	mov	x23, x2
	ldr	x0, [x0]
	movk	w1, 0x240, lsl 16
	mov	w2, 6
	ldrb	w22, [x0,9]
	adrp	x0, :got:gBlockPageAlignSize
	ldr	x0, [x0, #:got_lo12:gBlockPageAlignSize]
	ldr	w25, [x0]
	mov	x0, 59392
	movk	x0, 0x3, lsl 16
	bl	kmalloc_order_trace
	mov	x20, x0
	mov	w0, -1
	cbz	x20, .L2708
	add	w19, w24, 511
	lsr	w19, w19, 9
	cmp	w19, 255
	bhi	.L2700
	ubfiz	x0, x19, 9, 23
	mov	w2, 256
	add	x0, x21, x0
	mov	x1, x21
	sub	w2, w2, w19
	bl	memcpy
.L2700:
	add	w19, w19, 128
	mov	w0, 256
	cmp	w19, 256
	adrp	x26, :got:gNandIDBResBlkNum
	csel	w19, w19, w0, ls
	adrp	x0, .LC124
	add	x0, x0, :lo12:.LC124
	mov	x1, x23
	mov	w2, 4
	mov	w3, 5
	bl	rknand_print_hex
	ldr	x26, [x26, #:got_lo12:gNandIDBResBlkNum]
	ldr	w1, [x21,512]
	adrp	x0, .LC125
	add	x0, x0, :lo12:.LC125
	ldrb	w2, [x26]
	bl	printk
	ldrb	w0, [x26]
	ldr	w1, [x21,512]
	cmp	w1, w0
	bls	.L2701
	str	w0, [x21,512]
.L2701:
	adrp	x0, .LC126
	mov	w2, w24
	mul	w22, w22, w25
	add	x0, x0, :lo12:.LC126
	mov	w1, w19
	mov	x28, 0
	adrp	x25, .LC127
	bl	printk
	adrp	x26, .LC128
	adrp	x27, .LC129
	lsl	w0, w19, 7
	uxth	w22, w22
	str	w0, [x29,108]
	mov	w24, w28
	add	x25, x25, :lo12:.LC127
	add	x26, x26, :lo12:.LC128
	add	x27, x27, :lo12:.LC129
.L2706:
	adrp	x0, :got:gNandPhyInfo
	ldr	w2, [x23,x28,lsl 2]
	mov	w8, w28
	ldr	x0, [x0, #:got_lo12:gNandPhyInfo]
	ldrh	w0, [x0,26]
	cmp	w2, w0
	bcs	.L2702
	adrp	x0, :got:gNandFlashIdbBlockAddr
	ldr	x0, [x0, #:got_lo12:gNandFlashIdbBlockAddr]
	ldr	w0, [x0]
	cmp	w2, w0
	bcc	.L2702
	mov	w1, 0
	mov	x2, 512
	mov	x0, x20
	str	x8, [x29,96]
	bl	memset
	ldr	w0, [x23,x28,lsl 2]
	mov	w1, w19
	mov	x2, x21
	mul	w0, w22, w0
	bl	IDBlockWriteData
	ldr	w0, [x23,x28,lsl 2]
	mov	w1, w19
	mov	x2, x20
	mul	w0, w22, w0
	bl	IdBlockReadData
	ldr	x8, [x29,96]
	mov	x0, 0
.L2703:
	ldr	w1, [x29,108]
	mov	w3, w0
	cmp	w0, w1
	bcs	.L2713
	ldr	w4, [x20,x0,lsl 2]
	mov	x7, x0
	add	x0, x0, 1
	add	x1, x21, x0, lsl 2
	ldr	w5, [x1,-4]
	cmp	w4, w5
	beq	.L2703
	ldr	w2, [x23,x28,lsl 2]
	mov	w1, w8
	mov	x0, x25
	str	x7, [x29,96]
	bl	printk
	ldr	x7, [x29,96]
	mov	x0, x26
	mov	w2, 4
	mov	w3, 256
	and	x4, x7, -256
	lsl	x4, x4, 2
	str	x4, [x29,96]
	add	x1, x21, x4
	bl	rknand_print_hex
	ldr	x4, [x29,96]
	mov	w3, 256
	mov	x0, x27
	mov	w2, 4
	add	x1, x20, x4
	bl	rknand_print_hex
	mov	x0, x20
	mov	w1, 0
	mov	x2, 512
	bl	memset
	ldr	w0, [x23,x28,lsl 2]
	mov	w1, 4
	mov	x2, x20
	mul	w0, w22, w0
	bl	IDBlockWriteData
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
	bl	printk
	b	.L2702
.L2713:
	add	w24, w24, 1
.L2702:
	add	x28, x28, 1
	cmp	x28, 5
	bne	.L2706
	mov	x0, x20
	bl	kfree
	cmp	w24, wzr
	csetm	w0, eq
.L2708:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	write_idblock, .-write_idblock
	.align	2
	.global	CRC_32
	.type	CRC_32, %function
CRC_32:
	mov	x2, 0
	mov	x4, x0
	adrp	x5, :got:gTable_Crc32
	mov	w0, w2
.L2715:
	cmp	w1, w2
	bls	.L2717
	ldrb	w3, [x4,x2]
	add	x2, x2, 1
	ldr	x6, [x5, #:got_lo12:gTable_Crc32]
	eor	w3, w3, w0, lsr 24
	ldr	w3, [x6,w3,uxtw 2]
	eor	w0, w3, w0, lsl 8
	b	.L2715
.L2717:
	ret
	.size	CRC_32, .-CRC_32
	.align	2
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	sub	sp, sp, #528
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	w19, w1
	mov	x21, x2
	ldr	x20, [x0,208]
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2720
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2721
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2722
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2723
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2724
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2725
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L2773
	bl	rknand_dev_flush
	b	.L2796
.L2725:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2727
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2728
	b	.L2773
.L2723:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2729
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2730
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2731
	b	.L2773
.L2730:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2729
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2729
	b	.L2773
.L2721:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2732
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2733
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2734
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L2735
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2736
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2737
	b	.L2773
.L2733:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2738
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L2739
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2740
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2741
	b	.L2773
.L2739:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2741
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L2738
	b	.L2773
.L2735:
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	bl	printk
	mov	x0, x20
	mov	x1, x21
	mov	x2, 512
	bl	rk_copy_from_user
	cbz	x0, .L2742
.L2746:
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	b	.L2800
.L2742:
	adrp	x0, .LC133
	ldr	w1, [x20]
	ldr	w2, [x20,4]
	add	x0, x0, :lo12:.LC133
	bl	printk
	ldr	w19, [x20,4]
	cmp	w19, 8
	bhi	.L2799
	bl	rknand_device_unlock
	ldr	w0, [x20]
	mov	w1, w19
	mov	x2, x20
	bl	IdBlockReadData
	bl	rknand_device_unlock
	mov	x0, x21
	mov	x1, x20
	ubfiz	x2, x19, 9, 23
	bl	rk_copy_to_user
	cbz	x0, .L2796
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
.L2800:
	bl	printk
	b	.L2799
.L2734:
	adrp	x0, .LC135
	add	x0, x0, :lo12:.LC135
	bl	printk
	mov	x0, x20
	mov	x1, x21
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L2746
	ldr	w1, [x20]
	adrp	x0, .LC136
	ldr	w2, [x20,4]
	add	x0, x0, :lo12:.LC136
	bl	printk
	adrp	x0, :got:g_idb_buffer
	mov	x19, x0
	ldr	x21, [x0, #:got_lo12:g_idb_buffer]
	ldr	x1, [x21]
	cbz	x1, .L2747
.L2750:
	ldr	w2, [x20,4]
	cmp	w2, 4088
	bls	.L2748
	b	.L2799
.L2747:
	mov	w1, 192
	mov	x0, 260096
	movk	w1, 0x240, lsl 16
	mov	w2, 6
	bl	kmalloc_order_trace
	str	x0, [x21]
	cbnz	x0, .L2750
	b	.L2799
.L2748:
	ldr	w0, [x20]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L2799
	ldr	x19, [x19, #:got_lo12:g_idb_buffer]
	uxtw	x2, w2
	ldr	x1, [x19]
	add	x0, x1, x0, uxtw
	add	x1, x20, 8
	bl	memcpy
	b	.L2796
.L2737:
	adrp	x0, .LC137
	add	x0, x0, :lo12:.LC137
	bl	printk
	mov	x0, x20
	mov	x1, x21
	mov	x2, 28
	bl	rk_copy_from_user
	cbnz	x0, .L2746
	ldr	w1, [x20]
	adrp	x0, .LC138
	ldr	w2, [x20,4]
	add	x0, x0, :lo12:.LC138
	bl	printk
	ldr	w1, [x20]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L2799
	adrp	x19, :got:g_idb_buffer
	ldr	x19, [x19, #:got_lo12:g_idb_buffer]
	ldr	x0, [x19]
	cbz	x0, .L2799
	ldr	w21, [x20,4]
	bl	CRC_32
	cmp	w21, w0
	beq	.L2751
.L2756:
	mov	x0, -2
	b	.L2719
.L2751:
	bl	rknand_device_unlock
	ldr	x1, [x19]
	add	x2, x20, 8
	ldr	w0, [x20]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x19]
	bl	kfree
	str	xzr, [x19]
	b	.L2796
.L2736:
	adrp	x0, .LC139
	add	x0, x0, :lo12:.LC139
	bl	printk
	mov	x0, x20
	bl	ReadFlashInfo
	mov	x0, x21
	mov	x1, x20
	mov	x2, 11
	b	.L2791
.L2720:
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	bl	printk
	bl	rknand_device_unlock
	bl	FtlReInitForSDUpdata
	mov	w19, w0
	bl	rknand_device_unlock
	cbnz	w19, .L2799
	bl	nand_blk_add_whole_disk
	bl	rknand_device_unlock
	mov	w1, w19
	mov	w2, 64
	mov	x0, x20
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	adrp	x0, .LC141
	mov	x1, x20
	add	x0, x0, :lo12:.LC141
	mov	w2, 4
	mov	w3, 8
	bl	rknand_print_hex
	mov	x0, x21
	mov	x1, x20
	mov	x2, 64
	b	.L2791
.L2732:
	adrp	x0, .LC142
	add	x0, x0, :lo12:.LC142
	bl	printk
	adrp	x0, :got:gpDrmKeyInfo
	mov	x1, x20
	mov	x2, 4
	ldr	x0, [x0, #:got_lo12:gpDrmKeyInfo]
	ldr	x0, [x0]
	ldr	w0, [x0,20]
	str	w0, [x20]
	mov	x0, x21
	b	.L2791
.L2740:
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	bl	printk
	bl	rknand_device_unlock
	mov	w1, 264
	mov	w2, 2
	mov	x3, x20
	mov	w0, 16
	bl	FtlRead
	bl	rknand_device_unlock
	mov	x0, x21
	mov	x1, x20
	mov	x2, 1024
	b	.L2791
.L2724:
	adrp	x0, .LC144
	add	x19, x29, 56
	add	x0, x0, :lo12:.LC144
	bl	printk
	mov	x0, x19
	mov	x1, x21
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x20, x0
	cbnz	x0, .L2746
	ldr	w1, [x19]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L2753
.L2754:
	mov	x19, -1
	b	.L2745
.L2753:
	ldr	w0, [x19,4]
	cmp	w0, 512
	bhi	.L2754
	adrp	x1, :got:gpDrmKeyInfo
	mov	x0, x19
	mov	x2, 512
	ldr	x1, [x1, #:got_lo12:gpDrmKeyInfo]
	ldr	x1, [x1]
	bl	memcpy
	adrp	x0, :got:SecureBootCheckOK
	ldr	x0, [x0, #:got_lo12:SecureBootCheckOK]
	ldr	w1, [x0]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L2755
	str	w20, [x19,8]
	add	x0, x19, 64
	str	w20, [x19,12]
	mov	w1, w20
	mov	x2, 128
	bl	memset
.L2755:
	str	wzr, [x19,16]
	add	x0, x19, 256
	mov	w1, 0
	mov	x2, 256
	bl	memset
	b	.L2795
.L2727:
	adrp	x0, .LC145
	add	x19, x29, 56
	add	x0, x0, :lo12:.LC145
	bl	printk
	mov	x0, x19
	mov	x1, x21
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2746
	ldr	w1, [x19]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L2754
	ldr	w0, [x19,4]
	cmp	w0, 512
	bhi	.L2754
	adrp	x0, :got:SecureBootCheckOK
	ldr	x0, [x0, #:got_lo12:SecureBootCheckOK]
	ldr	w1, [x0]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	bne	.L2756
	ldr	w1, [x19,12]
	mov	x0, -3
	sub	w2, w1, #1
	cmp	w2, 127
	bhi	.L2719
	adrp	x0, :got:gpDrmKeyInfo
	ldr	x0, [x0, #:got_lo12:gpDrmKeyInfo]
	ldr	x20, [x0]
	add	x0, x20, 64
	str	w1, [x20,12]
	add	x1, x19, 64
	ldr	w2, [x19,12]
	bl	memcpy
	mov	w0, 1
	mov	x1, x20
	b	.L2798
.L2731:
	adrp	x0, .LC146
	add	x19, x29, 56
	add	x0, x0, :lo12:.LC146
	bl	printk
	mov	x0, x19
	mov	x1, x21
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2746
	ldr	w1, [x19]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L2754
	ldr	w0, [x19,4]
	cmp	w0, 512
	bhi	.L2754
	adrp	x19, :got:SecureBootEn
	ldr	x0, [x19, #:got_lo12:SecureBootEn]
	ldr	w0, [x0]
	cbnz	w0, .L2757
.L2760:
	mov	x0, 0
	b	.L2719
.L2757:
	adrp	x0, :got:gpBootConfig
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	x1, [x0, #:got_lo12:gpBootConfig]
	ldr	x2, [x1]
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L2758
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1]
	str	w3, [x2,4]
	ldr	x1, [x1]
	str	wzr, [x1,8]
	str	wzr, [x1,12]
.L2758:
	ldr	x0, [x0, #:got_lo12:gpBootConfig]
	ldr	x1, [x0]
	mov	w0, 0
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	adrp	x0, :got:gpDrmKeyInfo
	mov	w3, 21060
	movk	w3, 0x4b4d, lsl 16
	ldr	x1, [x0, #:got_lo12:gpDrmKeyInfo]
	ldr	x2, [x1]
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L2759
	mov	w3, 21060
	movk	w3, 0x4b4d, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1]
	str	w3, [x2,4]
	ldr	x1, [x1]
	str	wzr, [x1,8]
.L2759:
	ldr	x0, [x0, #:got_lo12:gpDrmKeyInfo]
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x0]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	ldr	x19, [x19, #:got_lo12:SecureBootEn]
	adrp	x0, :got:SecureBootCheckOK
	str	wzr, [x19]
	ldr	x0, [x0, #:got_lo12:SecureBootCheckOK]
	str	wzr, [x0]
	b	.L2796
.L2722:
	adrp	x0, .LC147
	add	x19, x29, 56
	add	x0, x0, :lo12:.LC147
	bl	printk
	mov	x0, x19
	mov	x1, x21
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2746
	ldr	w2, [x19]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L2754
	ldr	w1, [x19,4]
	cmp	w1, 512
	bhi	.L2754
	adrp	x19, :got:SecureBootEn
	ldr	x1, [x19, #:got_lo12:SecureBootEn]
	ldr	w1, [x1]
	cmp	w1, 1
	beq	.L2760
	adrp	x1, :got:gpBootConfig
	mov	w4, 22867
	movk	w4, 0x4453, lsl 16
	ldr	x2, [x1, #:got_lo12:gpBootConfig]
	ldr	x3, [x2]
	ldr	w5, [x3]
	cmp	w5, w4
	beq	.L2761
	mov	w4, 22867
	movk	w4, 0x4453, lsl 16
	str	w4, [x3]
	mov	w4, 504
	ldr	x3, [x2]
	str	w4, [x3,4]
	ldr	x2, [x2]
	str	w0, [x2,8]
	str	w0, [x2,12]
.L2761:
	ldr	x1, [x1, #:got_lo12:gpBootConfig]
	mov	w0, 1
	ldr	x1, [x1]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	adrp	x0, :got:gpDrmKeyInfo
	mov	w3, 21060
	movk	w3, 0x4b4d, lsl 16
	ldr	x1, [x0, #:got_lo12:gpDrmKeyInfo]
	ldr	x2, [x1]
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L2762
	mov	w3, 21060
	movk	w3, 0x4b4d, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1]
	str	w3, [x2,4]
	ldr	x1, [x1]
	str	wzr, [x1,8]
.L2762:
	ldr	x0, [x0, #:got_lo12:gpDrmKeyInfo]
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x0]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	ldr	x19, [x19, #:got_lo12:SecureBootEn]
	mov	w0, 1
	str	w0, [x19]
	b	.L2796
.L2728:
	adrp	x0, .LC148
	add	x19, x29, 56
	add	x0, x0, :lo12:.LC148
	bl	printk
	mov	x0, x19
	mov	x1, x21
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2746
	ldr	w1, [x19]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L2754
	ldr	w2, [x19,4]
	cmp	w2, 512
	bhi	.L2754
	adrp	x1, :got:gSnSectorData
	add	x0, x19, 8
	uxtw	x2, w2
	ldr	x1, [x1, #:got_lo12:gSnSectorData]
	bl	memcpy
.L2795:
	mov	x0, x21
	mov	x1, x19
	b	.L2792
.L2729:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L2763
	adrp	x0, .LC149
	add	x0, x0, :lo12:.LC149
	b	.L2793
.L2763:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L2765
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	b	.L2793
.L2765:
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
.L2793:
	bl	printk
	add	x20, x29, 56
	mov	x0, x20
	mov	x1, x21
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2746
	ldr	w1, [x20]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L2799
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L2766
	adrp	x0, :got:gpDrmKeyInfo
	mov	x1, x20
	mov	x2, 16
	ldr	x0, [x0, #:got_lo12:gpDrmKeyInfo]
	ldr	x0, [x0]
	ldr	w0, [x0,20]
	str	w0, [x20,4]
	strb	w0, [x20,8]
	mov	x0, x21
	bl	rk_copy_to_user
	cbz	x0, .L2719
	b	.L2799
.L2766:
	adrp	x2, :got:SecureBootUnlockTryCount
	mov	x3, x2
	ldr	x21, [x2, #:got_lo12:SecureBootUnlockTryCount]
	ldr	w0, [x21]
	cmp	w0, 10
	bhi	.L2799
	adrp	x0, :got:gpDrmKeyInfo
	ldr	w1, [x20,4]
	ldr	x0, [x0, #:got_lo12:gpDrmKeyInfo]
	ldr	x2, [x0]
	ldr	w0, [x2,24]
	cmp	w0, w1
	beq	.L2767
	cbz	w0, .L2767
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
	bl	printk
	ldr	w0, [x21]
	add	w0, w0, 1
	str	w0, [x21]
.L2799:
	mov	x0, -14
	b	.L2719
.L2767:
	ldr	x3, [x3, #:got_lo12:SecureBootUnlockTryCount]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	str	wzr, [x3]
	bne	.L2768
	str	wzr, [x2,20]
	str	wzr, [x2,24]
	b	.L2769
.L2768:
	mov	w0, 1
	str	w1, [x2,24]
	str	w0, [x2,20]
.L2769:
	mov	w0, 1
	mov	x1, x2
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L2796
	b	.L2745
.L2741:
	adrp	x0, .LC153
	add	x20, x29, 56
	add	x0, x0, :lo12:.LC153
	bl	printk
	mov	x0, x20
	mov	x1, x21
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2746
	ldr	w1, [x20]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L2754
	ldr	w2, [x20,4]
	cmp	w2, 504
	bhi	.L2754
	mov	w0, 30224
	uxtw	x2, w2
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	add	x0, x20, 8
	bne	.L2770
	adrp	x1, :got:gpVendor0Info
	ldr	x1, [x1, #:got_lo12:gpVendor0Info]
	b	.L2794
.L2770:
	adrp	x1, :got:gpVendor1Info
	ldr	x1, [x1, #:got_lo12:gpVendor1Info]
.L2794:
	ldr	x1, [x1]
	add	x1, x1, 8
	bl	memcpy
	mov	x0, x21
	mov	x1, x20
.L2792:
	mov	x2, 520
.L2791:
	bl	rk_copy_to_user
	cbnz	x0, .L2799
	b	.L2796
.L2738:
	adrp	x0, .LC154
	add	x20, x29, 56
	add	x0, x0, :lo12:.LC154
	bl	printk
	mov	x0, x20
	mov	x1, x21
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L2746
	ldr	w1, [x20]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L2754
	ldr	w2, [x20,4]
	cmp	w2, 504
	bhi	.L2754
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L2772
	adrp	x19, :got:gpVendor0Info
	mov	x1, x20
	ldr	x19, [x19, #:got_lo12:gpVendor0Info]
	ldr	x0, [x19]
	bl	memcpy
	mov	w0, 2
	b	.L2797
.L2772:
	adrp	x19, :got:gpVendor1Info
	mov	x1, x20
	ldr	x19, [x19, #:got_lo12:gpVendor1Info]
	ldr	x0, [x19]
	bl	memcpy
	mov	w0, 3
.L2797:
	ldr	x1, [x19]
.L2798:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L2745
.L2796:
	mov	x19, 0
.L2745:
	adrp	x0, .LC155
	mov	x1, x19
	add	x0, x0, :lo12:.LC155
	bl	printk
	mov	x0, x19
	b	.L2719
.L2773:
	mov	x0, -22
.L2719:
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
	adrp	x0, :got:g_idb_buffer
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	ldr	x0, [x0, #:got_lo12:g_idb_buffer]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	adrp	x21, :got:gpBootConfig
	str	xzr, [x0]
	adrp	x0, :got:gLoaderBootInfo
	adrp	x20, :got:gpVendor0Info
	adrp	x19, :got:gpVendor1Info
	ldr	x21, [x21, #:got_lo12:gpBootConfig]
	ldr	x0, [x0, #:got_lo12:gLoaderBootInfo]
	ldr	x1, [x0]
	adrp	x0, :got:gpDrmKeyInfo
	str	x1, [x21]
	add	x2, x1, 512
	ldr	x0, [x0, #:got_lo12:gpDrmKeyInfo]
	str	x2, [x0]
	add	x2, x1, 1024
	add	x1, x1, 1536
	ldr	x0, [x20, #:got_lo12:gpVendor0Info]
	str	x2, [x0]
	mov	w2, 512
	ldr	x0, [x19, #:got_lo12:gpVendor1Info]
	str	x1, [x0]
	adrp	x0, :got:gSnSectorData
	ldr	x0, [x0, #:got_lo12:gSnSectorData]
	bl	ftl_memcpy
	adrp	x1, :got:SecureBootEn
	ldr	x0, [x21]
	adrp	x3, :got:SecureBootCheckOK
	mov	x25, x1
	mov	x24, x3
	ldr	x22, [x1, #:got_lo12:SecureBootEn]
	ldr	w2, [x0,16]
	ldr	w23, [x0,508]
	str	w2, [x22]
	ldr	x2, [x3, #:got_lo12:SecureBootCheckOK]
	str	wzr, [x2]
	adrp	x2, :got:SecureBootUnlockTryCount
	ldr	x2, [x2, #:got_lo12:SecureBootUnlockTryCount]
	str	wzr, [x2]
	cbz	w23, .L2803
	mov	w1, 508
	bl	JSHash
	cmp	w23, w0
	beq	.L2803
	ldr	x0, [x21]
	str	wzr, [x22]
	str	wzr, [x0,16]
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
	bl	printk
.L2803:
	ldr	x1, [x25, #:got_lo12:SecureBootEn]
	ldr	w0, [x1]
	cbz	w0, .L2805
	ldr	x0, [x24, #:got_lo12:SecureBootCheckOK]
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0]
.L2805:
	ldr	x20, [x20, #:got_lo12:gpVendor0Info]
	mov	w0, 2
	ldr	x1, [x20]
	bl	StorageSysDataLoad
	ldr	x19, [x19, #:got_lo12:gpVendor1Info]
	mov	w0, 3
	ldr	x1, [x19]
	bl	StorageSysDataLoad
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	bl	misc_register
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	2
	.global	StorageSysDataDeInit
	.type	StorageSysDataDeInit, %function
StorageSysDataDeInit:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR3
	add	x29, sp, 0
	add	x0, x0, :lo12:.LANCHOR3
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
	.global	gNandcVer
	.global	gNandChipMap
	.global	gNandIDataBuf
	.global	FlashDdrTunningReadCount
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
.LANCHOR1 = . + 0
	.type	__func__.18162, %object
	.size	__func__.18162, 11
__func__.18162:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.19016, %object
	.size	__func__.19016, 21
__func__.19016:
	.string	"FtlVpcCheckAndModify"
	.section	.data.rel.local,"aw",%progbits
	.align	3
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
	.data
	.align	3
.LANCHOR2 = . + 0
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
	.type	ftl_gc_temp_block_bops_scan_page_addr, %object
	.size	ftl_gc_temp_block_bops_scan_page_addr, 2
ftl_gc_temp_block_bops_scan_page_addr:
	.hword	-1
	.zero	6
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
	.type	power_up_flag, %object
	.size	power_up_flag, 4
power_up_flag:
	.word	1
	.type	gFtlInitStatus, %object
	.size	gFtlInitStatus, 4
gFtlInitStatus:
	.word	-1
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
	.zero	5
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
	.zero	5
	.type	NandFlashParaTbl, %object
	.size	NandFlashParaTbl, 2112
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.hword	1497
	.byte	2
	.byte	6
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.zero	5
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
	.hword	1497
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
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
	.zero	5
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"Context allocation failed\n"
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
	.string	"FTL version: 5.0.31 20160315"
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
	.string	"ReadRetry pageadd=%x  ecc=%x err=%x\n"
.LC93:
	.string	"Read pageadd=%x  ecc=%x err=%x\n"
.LC94:
	.string	"spare:"
.LC95:
	.string	"FLFB:%d %d\n"
.LC96:
	.string	"no ect"
.LC97:
	.string	"slc mode"
.LC98:
	.string	"prog error: = %x\n"
.LC99:
	.string	"prog read error: = %x\n"
.LC100:
	.string	"prog read s error: = %x %x %x\n"
.LC101:
	.string	"prog read d error: = %x %x %x\n"
.LC102:
	.string	"FlashMakeFactorBbt %d\n"
.LC103:
	.string	"bad block:%d %d\n"
.LC104:
	.string	"FMFB:%d %d\n"
.LC105:
	.string	"E:bad block:%d\n"
.LC106:
	.string	"FMFB:Save %d %d\n"
.LC107:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC108:
	.string	"FtlBbmTblFlush error:%x\n"
.LC109:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC110:
	.string	"decrement_vpc_count %x = %d\n"
.LC111:
	.string	"spuer block %x vpn is 0\n "
.LC112:
	.string	"...%s enter...\n"
.LC113:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC114:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC115:
	.string	"FtlInit %x\n"
.LC116:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC117:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC118:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC119:
	.string	"BBT:"
.LC120:
	.string	"IdBlockReadData %x %x\n"
.LC121:
	.string	"IdBlockReadData %x %x ret= %x\n"
.LC122:
	.string	"IDBlockWriteData %x %x\n"
.LC123:
	.string	"IDBlockWriteData %x %x ret= %x\n"
.LC124:
	.string	"idblk:"
.LC125:
	.string	"idb reverse %x %x\n"
.LC126:
	.string	"write_idblock totle_sec %x %x\n"
.LC127:
	.string	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x \n"
.LC128:
	.string	"write"
.LC129:
	.string	"read"
.LC130:
	.string	"write_idblock error\n"
.LC131:
	.string	"READ_SECTOR_IO\n"
.LC132:
	.string	"rk_copy_from_user error \n"
.LC133:
	.string	"READ_SECTOR_IO %x %x\n"
.LC134:
	.string	"rk_copy_to_user error\n"
.LC135:
	.string	"WRITE_SECTOR_IO\n"
.LC136:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC137:
	.string	"END_WRITE_SECTOR_IO\n"
.LC138:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC139:
	.string	"GET_FLASH_INFO_IO\n"
.LC140:
	.string	"GET_BAD_BLOCK_IO\n"
.LC141:
	.string	"bbt:"
.LC142:
	.string	"GET_LOCK_FLAG_IO\n"
.LC143:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC144:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC145:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC146:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC147:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC148:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC149:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC150:
	.string	"RKNAND_LOADER_STATUS\n"
.LC151:
	.string	"RKNAND_LOADER_LOCK\n"
.LC152:
	.string	"LockKey not match %d\n"
.LC153:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC154:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC155:
	.string	"return ret = %lx\n"
.LC156:
	.string	"secureBootEn check error\n"
.LC157:
	.string	"rknand_sys_storage"
	.bss
	.align	3
.LANCHOR0 = . + 0
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
	.type	gc_discard_updated, %object
	.size	gc_discard_updated, 4
gc_discard_updated:
	.zero	4
	.type	SecureBootUnlockTryCount, %object
	.size	SecureBootUnlockTryCount, 4
SecureBootUnlockTryCount:
	.zero	4
	.type	SecureBootCheckOK, %object
	.size	SecureBootCheckOK, 4
SecureBootCheckOK:
	.zero	4
	.type	SecureBootEn, %object
	.size	SecureBootEn, 4
SecureBootEn:
	.zero	4
	.type	gpVendor1Info, %object
	.size	gpVendor1Info, 8
gpVendor1Info:
	.zero	8
	.type	gpVendor0Info, %object
	.size	gpVendor0Info, 8
gpVendor0Info:
	.zero	8
	.type	g_idb_buffer, %object
	.size	g_idb_buffer, 8
g_idb_buffer:
	.zero	8
	.type	gSnSectorData, %object
	.size	gSnSectorData, 512
gSnSectorData:
	.zero	512
	.type	gpDrmKeyInfo, %object
	.size	gpDrmKeyInfo, 8
gpDrmKeyInfo:
	.zero	8
	.type	gpBootConfig, %object
	.size	gpBootConfig, 8
gpBootConfig:
	.zero	8
	.type	gLoaderBootInfo, %object
	.size	gLoaderBootInfo, 8
gLoaderBootInfo:
	.zero	8
	.type	RK29_NANDC1_REG_BASE, %object
	.size	RK29_NANDC1_REG_BASE, 8
RK29_NANDC1_REG_BASE:
	.zero	8
	.type	RK29_NANDC_REG_BASE, %object
	.size	RK29_NANDC_REG_BASE, 8
RK29_NANDC_REG_BASE:
	.zero	8
	.type	gc_ink_free_return_value, %object
	.size	gc_ink_free_return_value, 2
gc_ink_free_return_value:
	.zero	2
	.type	FtlUpdateVaildLpnCount, %object
	.size	FtlUpdateVaildLpnCount, 2
FtlUpdateVaildLpnCount:
	.zero	2
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.zero	2
	.zero	2
	.type	last_cache_match_count, %object
	.size	last_cache_match_count, 4
last_cache_match_count:
	.zero	4
	.type	g_LowFormat, %object
	.size	g_LowFormat, 4
g_LowFormat:
	.zero	4
	.type	DeviceCapacity, %object
	.size	DeviceCapacity, 4
DeviceCapacity:
	.zero	4
	.zero	4
	.type	FbbtBlk, %object
	.size	FbbtBlk, 16
FbbtBlk:
	.zero	16
	.type	g_power_lost_recovery_flag, %object
	.size	g_power_lost_recovery_flag, 2
g_power_lost_recovery_flag:
	.zero	2
	.type	c_mlc_erase_count_value, %object
	.size	c_mlc_erase_count_value, 2
c_mlc_erase_count_value:
	.zero	2
	.zero	4
	.type	g_recovery_ppa_tbl, %object
	.size	g_recovery_ppa_tbl, 128
g_recovery_ppa_tbl:
	.zero	128
	.type	g_recovery_page_min_ver, %object
	.size	g_recovery_page_min_ver, 4
g_recovery_page_min_ver:
	.zero	4
	.type	g_recovery_page_num, %object
	.size	g_recovery_page_num, 4
g_recovery_page_num:
	.zero	4
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
	.zero	4
	.type	g_gc_skip_write_count, %object
	.size	g_gc_skip_write_count, 4
g_gc_skip_write_count:
	.zero	4
	.type	g_gc_head_data_block_count, %object
	.size	g_gc_head_data_block_count, 4
g_gc_head_data_block_count:
	.zero	4
	.type	g_gc_head_data_block, %object
	.size	g_gc_head_data_block, 4
g_gc_head_data_block:
	.zero	4
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.zero	4
	.type	g_in_swl_replace, %object
	.size	g_in_swl_replace, 4
g_in_swl_replace:
	.zero	4
	.type	g_in_gc_progress, %object
	.size	g_in_gc_progress, 4
g_in_gc_progress:
	.zero	4
	.type	g_all_blk_used_slc_mode, %object
	.size	g_all_blk_used_slc_mode, 4
g_all_blk_used_slc_mode:
	.zero	4
	.type	g_max_erase_count, %object
	.size	g_max_erase_count, 4
g_max_erase_count:
	.zero	4
	.type	g_totle_sys_slc_erase_count, %object
	.size	g_totle_sys_slc_erase_count, 4
g_totle_sys_slc_erase_count:
	.zero	4
	.type	g_totle_slc_erase_count, %object
	.size	g_totle_slc_erase_count, 4
g_totle_slc_erase_count:
	.zero	4
	.type	g_min_erase_count, %object
	.size	g_min_erase_count, 4
g_min_erase_count:
	.zero	4
	.type	g_totle_avg_erase_count, %object
	.size	g_totle_avg_erase_count, 4
g_totle_avg_erase_count:
	.zero	4
	.type	g_totle_mlc_erase_count, %object
	.size	g_totle_mlc_erase_count, 4
g_totle_mlc_erase_count:
	.zero	4
	.type	g_totle_l2p_write_count, %object
	.size	g_totle_l2p_write_count, 4
g_totle_l2p_write_count:
	.zero	4
	.type	g_totle_cache_write_count, %object
	.size	g_totle_cache_write_count, 4
g_totle_cache_write_count:
	.zero	4
	.type	g_tmp_data_superblock_id, %object
	.size	g_tmp_data_superblock_id, 2
g_tmp_data_superblock_id:
	.zero	2
	.zero	2
	.type	g_totle_read_page_count, %object
	.size	g_totle_read_page_count, 4
g_totle_read_page_count:
	.zero	4
	.type	g_totle_discard_page_count, %object
	.size	g_totle_discard_page_count, 4
g_totle_discard_page_count:
	.zero	4
	.type	g_totle_read_sector, %object
	.size	g_totle_read_sector, 4
g_totle_read_sector:
	.zero	4
	.type	g_totle_write_sector, %object
	.size	g_totle_write_sector, 4
g_totle_write_sector:
	.zero	4
	.type	g_totle_write_page_count, %object
	.size	g_totle_write_page_count, 4
g_totle_write_page_count:
	.zero	4
	.type	g_totle_gc_page_count, %object
	.size	g_totle_gc_page_count, 4
g_totle_gc_page_count:
	.zero	4
	.type	g_gc_blk_index, %object
	.size	g_gc_blk_index, 2
g_gc_blk_index:
	.zero	2
	.type	g_gc_merge_free_blk_threshold, %object
	.size	g_gc_merge_free_blk_threshold, 2
g_gc_merge_free_blk_threshold:
	.zero	2
	.type	g_gc_free_blk_threshold, %object
	.size	g_gc_free_blk_threshold, 2
g_gc_free_blk_threshold:
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
	.type	g_gc_bad_block_temp_num, %object
	.size	g_gc_bad_block_temp_num, 2
g_gc_bad_block_temp_num:
	.zero	2
	.type	g_gc_next_blk_1, %object
	.size	g_gc_next_blk_1, 2
g_gc_next_blk_1:
	.zero	2
	.type	g_gc_next_blk, %object
	.size	g_gc_next_blk, 2
g_gc_next_blk:
	.zero	2
	.type	g_gc_cur_blk_max_valid_pages, %object
	.size	g_gc_cur_blk_max_valid_pages, 2
g_gc_cur_blk_max_valid_pages:
	.zero	2
	.type	g_gc_cur_blk_valid_pages, %object
	.size	g_gc_cur_blk_valid_pages, 2
g_gc_cur_blk_valid_pages:
	.zero	2
	.type	g_gc_page_offset, %object
	.size	g_gc_page_offset, 2
g_gc_page_offset:
	.zero	2
	.type	g_gc_blk_num, %object
	.size	g_gc_blk_num, 2
g_gc_blk_num:
	.zero	2
	.zero	6
	.type	p_gc_blk_tbl, %object
	.size	p_gc_blk_tbl, 8
p_gc_blk_tbl:
	.zero	8
	.type	p_gc_page_info, %object
	.size	p_gc_page_info, 8
p_gc_page_info:
	.zero	8
	.type	g_sys_ext_data, %object
	.size	g_sys_ext_data, 512
g_sys_ext_data:
	.zero	512
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.zero	48
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 8
gp_last_act_superblock:
	.zero	8
	.type	g_gc_superblock, %object
	.size	g_gc_superblock, 48
g_gc_superblock:
	.zero	48
	.type	g_gc_temp_superblock, %object
	.size	g_gc_temp_superblock, 48
g_gc_temp_superblock:
	.zero	48
	.type	g_buffer_superblock, %object
	.size	g_buffer_superblock, 48
g_buffer_superblock:
	.zero	48
	.type	g_active_superblock, %object
	.size	g_active_superblock, 48
g_active_superblock:
	.zero	48
	.type	g_num_data_superblocks, %object
	.size	g_num_data_superblocks, 2
g_num_data_superblocks:
	.zero	2
	.type	g_num_free_superblocks, %object
	.size	g_num_free_superblocks, 2
g_num_free_superblocks:
	.zero	2
	.zero	4
	.type	p_data_block_list_tail, %object
	.size	p_data_block_list_tail, 8
p_data_block_list_tail:
	.zero	8
	.type	p_data_block_list_head, %object
	.size	p_data_block_list_head, 8
p_data_block_list_head:
	.zero	8
	.type	p_free_data_block_list_head, %object
	.size	p_free_data_block_list_head, 8
p_free_data_block_list_head:
	.zero	8
	.type	p_data_block_list_table, %object
	.size	p_data_block_list_table, 8
p_data_block_list_table:
	.zero	8
	.type	g_l2p_last_update_region_id, %object
	.size	g_l2p_last_update_region_id, 2
g_l2p_last_update_region_id:
	.zero	2
	.zero	6
	.type	p_l2p_map_buf, %object
	.size	p_l2p_map_buf, 8
p_l2p_map_buf:
	.zero	8
	.type	p_l2p_ram_map, %object
	.size	p_l2p_ram_map, 8
p_l2p_ram_map:
	.zero	8
	.type	g_totle_vendor_block, %object
	.size	g_totle_vendor_block, 2
g_totle_vendor_block:
	.zero	2
	.zero	6
	.type	p_vendor_region_ppn_table, %object
	.size	p_vendor_region_ppn_table, 8
p_vendor_region_ppn_table:
	.zero	8
	.type	p_vendor_block_ver_table, %object
	.size	p_vendor_block_ver_table, 8
p_vendor_block_ver_table:
	.zero	8
	.type	p_vendor_block_valid_page_count, %object
	.size	p_vendor_block_valid_page_count, 8
p_vendor_block_valid_page_count:
	.zero	8
	.type	p_vendor_block_table, %object
	.size	p_vendor_block_table, 8
p_vendor_block_table:
	.zero	8
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.zero	2
	.zero	6
	.type	p_map_region_ppn_table, %object
	.size	p_map_region_ppn_table, 8
p_map_region_ppn_table:
	.zero	8
	.type	p_map_block_ver_table, %object
	.size	p_map_block_ver_table, 8
p_map_block_ver_table:
	.zero	8
	.type	p_map_block_valid_page_count, %object
	.size	p_map_block_valid_page_count, 8
p_map_block_valid_page_count:
	.zero	8
	.type	p_map_block_table, %object
	.size	p_map_block_table, 8
p_map_block_table:
	.zero	8
	.type	p_valid_page_count_check_table, %object
	.size	p_valid_page_count_check_table, 8
p_valid_page_count_check_table:
	.zero	8
	.type	p_valid_page_count_table, %object
	.size	p_valid_page_count_table, 8
p_valid_page_count_table:
	.zero	8
	.type	g_totle_swl_count, %object
	.size	g_totle_swl_count, 4
g_totle_swl_count:
	.zero	4
	.zero	4
	.type	p_swl_mul_table, %object
	.size	p_swl_mul_table, 8
p_swl_mul_table:
	.zero	8
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 8
p_erase_count_table:
	.zero	8
	.type	g_ect_tbl_info_size, %object
	.size	g_ect_tbl_info_size, 2
g_ect_tbl_info_size:
	.zero	2
	.zero	6
	.type	gp_ect_tbl_info, %object
	.size	gp_ect_tbl_info, 8
gp_ect_tbl_info:
	.zero	8
	.type	g_gc_num_req, %object
	.size	g_gc_num_req, 4
g_gc_num_req:
	.zero	4
	.type	c_gc_page_buf_num, %object
	.size	c_gc_page_buf_num, 4
c_gc_page_buf_num:
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
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 8
p_io_spare_buf:
	.zero	8
	.type	p_io_data_buf_1, %object
	.size	p_io_data_buf_1, 8
p_io_data_buf_1:
	.zero	8
	.type	p_io_data_buf_0, %object
	.size	p_io_data_buf_0, 8
p_io_data_buf_0:
	.zero	8
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 8
p_sys_spare_buf:
	.zero	8
	.type	p_vendor_data_buf, %object
	.size	p_vendor_data_buf, 8
p_vendor_data_buf:
	.zero	8
	.type	p_sys_data_buf_1, %object
	.size	p_sys_data_buf_1, 8
p_sys_data_buf_1:
	.zero	8
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 8
p_sys_data_buf:
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
	.type	c_wr_page_buf_num, %object
	.size	c_wr_page_buf_num, 4
c_wr_page_buf_num:
	.zero	4
	.zero	4
	.type	p_wr_io_data_buf, %object
	.size	p_wr_io_data_buf, 8
p_wr_io_data_buf:
	.zero	8
	.type	p_wr_io_spare_buf, %object
	.size	p_wr_io_spare_buf, 8
p_wr_io_spare_buf:
	.zero	8
	.type	p_plane_order_table, %object
	.size	p_plane_order_table, 32
p_plane_order_table:
	.zero	32
	.type	g_req_cache, %object
	.size	g_req_cache, 8
g_req_cache:
	.zero	8
	.type	req_gc_dst, %object
	.size	req_gc_dst, 8
req_gc_dst:
	.zero	8
	.type	req_gc, %object
	.size	req_gc, 8
req_gc:
	.zero	8
	.type	req_erase, %object
	.size	req_erase, 8
req_erase:
	.zero	8
	.type	req_prgm, %object
	.size	req_prgm, 8
req_prgm:
	.zero	8
	.type	req_read, %object
	.size	req_read, 8
req_read:
	.zero	8
	.type	req_sys, %object
	.size	req_sys, 56
req_sys:
	.zero	56
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 64
gVendorBlkInfo:
	.zero	64
	.type	gL2pMapInfo, %object
	.size	gL2pMapInfo, 64
gL2pMapInfo:
	.zero	64
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.zero	2056
	.type	gSysInfo, %object
	.size	gSysInfo, 12
gSysInfo:
	.zero	12
	.zero	4
	.type	gBbtInfo, %object
	.size	gBbtInfo, 96
gBbtInfo:
	.zero	96
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
	.zero	4
	.type	g_SlcPartLbaEndSector, %object
	.size	g_SlcPartLbaEndSector, 4
g_SlcPartLbaEndSector:
	.zero	4
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.zero	4
	.type	g_VaildLpn, %object
	.size	g_VaildLpn, 4
g_VaildLpn:
	.zero	4
	.type	g_MaxLpn, %object
	.size	g_MaxLpn, 4
g_MaxLpn:
	.zero	4
	.type	g_MaxLbaSector, %object
	.size	g_MaxLbaSector, 4
g_MaxLbaSector:
	.zero	4
	.type	g_GlobalDataVersion, %object
	.size	g_GlobalDataVersion, 4
g_GlobalDataVersion:
	.zero	4
	.type	g_GlobalSysVersion, %object
	.size	g_GlobalSysVersion, 4
g_GlobalSysVersion:
	.zero	4
	.type	ftl_gc_temp_power_lost_recovery_flag, %object
	.size	ftl_gc_temp_power_lost_recovery_flag, 4
ftl_gc_temp_power_lost_recovery_flag:
	.zero	4
	.type	c_ftl_nand_max_data_blks, %object
	.size	c_ftl_nand_max_data_blks, 4
c_ftl_nand_max_data_blks:
	.zero	4
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.zero	2
	.type	c_ftl_nand_data_blks_per_plane, %object
	.size	c_ftl_nand_data_blks_per_plane, 2
c_ftl_nand_data_blks_per_plane:
	.zero	2
	.type	c_ftl_nand_max_sys_blks, %object
	.size	c_ftl_nand_max_sys_blks, 4
c_ftl_nand_max_sys_blks:
	.zero	4
	.type	c_ftl_nand_init_sys_blks_per_plane, %object
	.size	c_ftl_nand_init_sys_blks_per_plane, 4
c_ftl_nand_init_sys_blks_per_plane:
	.zero	4
	.type	c_ftl_nand_sys_blks_per_plane, %object
	.size	c_ftl_nand_sys_blks_per_plane, 4
c_ftl_nand_sys_blks_per_plane:
	.zero	4
	.type	c_ftl_vendor_part_size, %object
	.size	c_ftl_vendor_part_size, 2
c_ftl_vendor_part_size:
	.zero	2
	.type	c_ftl_nand_max_vendor_blks, %object
	.size	c_ftl_nand_max_vendor_blks, 2
c_ftl_nand_max_vendor_blks:
	.zero	2
	.type	c_ftl_nand_max_map_blks, %object
	.size	c_ftl_nand_max_map_blks, 4
c_ftl_nand_max_map_blks:
	.zero	4
	.type	c_ftl_nand_map_blks_per_plane, %object
	.size	c_ftl_nand_map_blks_per_plane, 2
c_ftl_nand_map_blks_per_plane:
	.zero	2
	.type	c_ftl_nand_vendor_region_num, %object
	.size	c_ftl_nand_vendor_region_num, 2
c_ftl_nand_vendor_region_num:
	.zero	2
	.type	c_ftl_nand_l2pmap_ram_region_num, %object
	.size	c_ftl_nand_l2pmap_ram_region_num, 2
c_ftl_nand_l2pmap_ram_region_num:
	.zero	2
	.type	c_ftl_nand_map_region_num, %object
	.size	c_ftl_nand_map_region_num, 2
c_ftl_nand_map_region_num:
	.zero	2
	.type	c_ftl_nand_totle_phy_blks, %object
	.size	c_ftl_nand_totle_phy_blks, 4
c_ftl_nand_totle_phy_blks:
	.zero	4
	.type	c_ftl_nand_reserved_blks, %object
	.size	c_ftl_nand_reserved_blks, 2
c_ftl_nand_reserved_blks:
	.zero	2
	.type	c_ftl_nand_byte_pre_oob, %object
	.size	c_ftl_nand_byte_pre_oob, 2
c_ftl_nand_byte_pre_oob:
	.zero	2
	.type	c_ftl_nand_byte_pre_page, %object
	.size	c_ftl_nand_byte_pre_page, 2
c_ftl_nand_byte_pre_page:
	.zero	2
	.type	c_ftl_nand_sec_pre_page_shift, %object
	.size	c_ftl_nand_sec_pre_page_shift, 2
c_ftl_nand_sec_pre_page_shift:
	.zero	2
	.type	c_ftl_nand_sec_pre_page, %object
	.size	c_ftl_nand_sec_pre_page, 2
c_ftl_nand_sec_pre_page:
	.zero	2
	.type	c_ftl_nand_page_pre_super_blk, %object
	.size	c_ftl_nand_page_pre_super_blk, 2
c_ftl_nand_page_pre_super_blk:
	.zero	2
	.type	c_ftl_nand_page_pre_slc_blk, %object
	.size	c_ftl_nand_page_pre_slc_blk, 2
c_ftl_nand_page_pre_slc_blk:
	.zero	2
	.type	c_ftl_nand_page_pre_blk, %object
	.size	c_ftl_nand_page_pre_blk, 2
c_ftl_nand_page_pre_blk:
	.zero	2
	.type	c_ftl_nand_bbm_buf_size, %object
	.size	c_ftl_nand_bbm_buf_size, 2
c_ftl_nand_bbm_buf_size:
	.zero	2
	.type	c_ftl_nand_ext_blk_pre_plane, %object
	.size	c_ftl_nand_ext_blk_pre_plane, 2
c_ftl_nand_ext_blk_pre_plane:
	.zero	2
	.type	c_ftl_nand_blk_pre_plane, %object
	.size	c_ftl_nand_blk_pre_plane, 2
c_ftl_nand_blk_pre_plane:
	.zero	2
	.type	c_ftl_nand_planes_num, %object
	.size	c_ftl_nand_planes_num, 2
c_ftl_nand_planes_num:
	.zero	2
	.type	c_ftl_nand_blks_per_die_shift, %object
	.size	c_ftl_nand_blks_per_die_shift, 2
c_ftl_nand_blks_per_die_shift:
	.zero	2
	.type	c_ftl_nand_blks_per_die, %object
	.size	c_ftl_nand_blks_per_die, 2
c_ftl_nand_blks_per_die:
	.zero	2
	.type	c_ftl_nand_planes_per_die, %object
	.size	c_ftl_nand_planes_per_die, 2
c_ftl_nand_planes_per_die:
	.zero	2
	.type	c_ftl_nand_die_num, %object
	.size	c_ftl_nand_die_num, 2
c_ftl_nand_die_num:
	.zero	2
	.type	c_ftl_nand_type, %object
	.size	c_ftl_nand_type, 2
c_ftl_nand_type:
	.zero	2
	.zero	2
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
	.type	gToggleModeClkDiv, %object
	.size	gToggleModeClkDiv, 4
gToggleModeClkDiv:
	.zero	4
	.type	gBootDdrMode, %object
	.size	gBootDdrMode, 4
gBootDdrMode:
	.zero	4
	.type	gNandcEccBits, %object
	.size	gNandcEccBits, 4
gNandcEccBits:
	.zero	4
	.type	gpNandc1, %object
	.size	gpNandc1, 8
gpNandc1:
	.zero	8
	.type	gpNandc, %object
	.size	gpNandc, 8
gpNandc:
	.zero	8
	.type	gNandcVer, %object
	.size	gNandcVer, 4
gNandcVer:
	.zero	4
	.zero	4
	.type	gNandChipMap, %object
	.size	gNandChipMap, 64
gNandChipMap:
	.zero	64
	.type	gNandIDataBuf, %object
	.size	gNandIDataBuf, 2048
gNandIDataBuf:
	.zero	2048
	.type	FlashDdrTunningReadCount, %object
	.size	FlashDdrTunningReadCount, 4
FlashDdrTunningReadCount:
	.zero	4
	.zero	4
	.type	gNandPhyInfo, %object
	.size	gNandPhyInfo, 28
gNandPhyInfo:
	.zero	28
	.zero	4
	.type	gFlashProgCheckSpareBuffer, %object
	.size	gFlashProgCheckSpareBuffer, 8
gFlashProgCheckSpareBuffer:
	.zero	8
	.type	gFlashProgCheckBuffer, %object
	.size	gFlashProgCheckBuffer, 8
gFlashProgCheckBuffer:
	.zero	8
	.type	gFlashSpareBuffer, %object
	.size	gFlashSpareBuffer, 8
gFlashSpareBuffer:
	.zero	8
	.type	gFlashPageBuffer1, %object
	.size	gFlashPageBuffer1, 8
gFlashPageBuffer1:
	.zero	8
	.type	gFlashPageBuffer0, %object
	.size	gFlashPageBuffer0, 8
gFlashPageBuffer0:
	.zero	8
	.type	gpFlashSaveInfo, %object
	.size	gpFlashSaveInfo, 8
gpFlashSaveInfo:
	.zero	8
	.type	gReadRetryInfo, %object
	.size	gReadRetryInfo, 852
gReadRetryInfo:
	.zero	852
	.zero	4
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 8
gpNandParaInfo:
	.zero	8
	.type	gNandOptPara, %object
	.size	gNandOptPara, 32
gNandOptPara:
	.zero	32
	.type	g_slc2KBNand, %object
	.size	g_slc2KBNand, 1
g_slc2KBNand:
	.zero	1
	.type	g_maxRetryCount, %object
	.size	g_maxRetryCount, 1
g_maxRetryCount:
	.zero	1
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.zero	1
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
	.zero	1
	.type	gNandIDBResBlkNumSaveInFlash, %object
	.size	gNandIDBResBlkNumSaveInFlash, 1
gNandIDBResBlkNumSaveInFlash:
	.zero	1
	.type	gNandIDBResBlkNum, %object
	.size	gNandIDBResBlkNum, 1
gNandIDBResBlkNum:
	.zero	1
	.zero	2
	.type	gNandFlashResEndPageAddr, %object
	.size	gNandFlashResEndPageAddr, 4
gNandFlashResEndPageAddr:
	.zero	4
	.type	gNandFlashInfoBlockAddr, %object
	.size	gNandFlashInfoBlockAddr, 4
gNandFlashInfoBlockAddr:
	.zero	4
	.type	gNandFlashIdbBlockAddr, %object
	.size	gNandFlashIdbBlockAddr, 4
gNandFlashIdbBlockAddr:
	.zero	4
	.type	gNandFlashInfoBlockEcc, %object
	.size	gNandFlashInfoBlockEcc, 1
gNandFlashInfoBlockEcc:
	.zero	1
	.type	gNandFlashIDBEccBits, %object
	.size	gNandFlashIDBEccBits, 1
gNandFlashIDBEccBits:
	.zero	1
	.type	gNandFlashEccBits, %object
	.size	gNandFlashEccBits, 1
gNandFlashEccBits:
	.zero	1
	.type	gNandRandomizer, %object
	.size	gNandRandomizer, 1
gNandRandomizer:
	.zero	1
	.type	gBlockPageAlignSize, %object
	.size	gBlockPageAlignSize, 4
gBlockPageAlignSize:
	.zero	4
	.type	gTotleBlock, %object
	.size	gTotleBlock, 2
gTotleBlock:
	.zero	2
	.type	gNandMaxChip, %object
	.size	gNandMaxChip, 1
gNandMaxChip:
	.zero	1
	.type	gNandMaxDie, %object
	.size	gNandMaxDie, 1
gNandMaxDie:
	.zero	1
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.zero	1
	.type	gFlashOnfiModeEn, %object
	.size	gFlashOnfiModeEn, 1
gFlashOnfiModeEn:
	.zero	1
	.type	gFlashToggleModeEn, %object
	.size	gFlashToggleModeEn, 1
gFlashToggleModeEn:
	.zero	1
	.type	gFlashSdrModeEn, %object
	.size	gFlashSdrModeEn, 1
gFlashSdrModeEn:
	.zero	1
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.zero	1
	.type	gMultiPageReadEn, %object
	.size	gMultiPageReadEn, 1
gMultiPageReadEn:
	.zero	1
	.zero	2
	.type	gpReadRetrial, %object
	.size	gpReadRetrial, 8
gpReadRetrial:
	.zero	8
	.type	mlcPageToSlcPageTbl, %object
	.size	mlcPageToSlcPageTbl, 1024
mlcPageToSlcPageTbl:
	.zero	1024
	.type	slcPageToMlcPageTbl, %object
	.size	slcPageToMlcPageTbl, 512
slcPageToMlcPageTbl:
	.zero	512
	.type	DieAddrs, %object
	.size	DieAddrs, 32
DieAddrs:
	.zero	32
	.type	gDieOp, %object
	.size	gDieOp, 192
gDieOp:
	.zero	192
	.type	DieCsIndex, %object
	.size	DieCsIndex, 8
DieCsIndex:
	.zero	8
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
	.zero	32
	.type	read_retry_cur_offset, %object
	.size	read_retry_cur_offset, 4
read_retry_cur_offset:
	.zero	4
	.section	.data.rel,"aw",%progbits
	.align	3
.LANCHOR3 = . + 0
	.type	rknand_sys_storage_dev, %object
	.size	rknand_sys_storage_dev, 80
rknand_sys_storage_dev:
	.word	255
	.zero	4
	.xword	.LC157
	.xword	rknand_sys_storage_fops
	.zero	56
