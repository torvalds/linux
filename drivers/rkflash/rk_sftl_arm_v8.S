/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-08-07
 */
	.arch armv8-a
	.file	"rk_sftl.c"
#APP
	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
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
	.type	l2p_addr_tran.isra.0, %function
l2p_addr_tran.isra.0:
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	ldrh	w3, [x4, 8]
	ldrh	w6, [x4, 10]
	ldrh	w4, [x4, 14]
	cmp	w4, 4
	bne	.L2
	lsr	w3, w3, 1
	ubfiz	w6, w6, 1, 15
.L2:
	lsr	w5, w0, 10
	and	w0, w0, 1023
	and	w4, w5, 65535
	and	w5, w5, 65535
	udiv	w4, w4, w3
	msub	w3, w3, w4, w5
	madd	w3, w3, w6, w0
	mov	w0, 0
	str	w3, [x1]
	str	w4, [x2]
	ret
	.size	l2p_addr_tran.isra.0, .-l2p_addr_tran.isra.0
	.align	2
	.type	ftl_set_blk_mode.part.7, %function
ftl_set_blk_mode.part.7:
	and	w0, w0, 65535
	adrp	x2, .LANCHOR0+24
	ubfx	x1, x0, 5, 11
	ldr	x3, [x2, #:lo12:.LANCHOR0+24]
	lsl	x1, x1, 2
	mov	w2, 1
	lsl	w2, w2, w0
	ldr	w0, [x3, x1]
	orr	w0, w0, w2
	str	w0, [x3, x1]
	ret
	.size	ftl_set_blk_mode.part.7, .-ftl_set_blk_mode.part.7
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	mov	w2, 1
	mov	w1, 0
.L5:
	cmp	w2, w0
	bls	.L6
	sub	w0, w1, #1
	ret
.L6:
	add	w1, w1, 1
	lsl	w2, w2, 1
	and	w1, w1, 65535
	b	.L5
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
	ldrh	w2, [x1, 36]
	ldrh	w3, [x1, 46]
	str	w0, [x1, 32]
	mul	w2, w2, w0
	sub	w0, w3, w0
	strh	w0, [x1, 44]
	ldr	w0, [x1, 52]
	str	w2, [x1, 40]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1, 48]
	ret
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	stp	x29, x30, [sp, -16]!
	mov	x7, x0
	adrp	x4, .LANCHOR0
	add	x1, x4, :lo12:.LANCHOR0
	add	x29, sp, 0
	ldrh	w3, [x0]
	ldrh	w6, [x0, 2]
	ldrh	w0, [x0, 4]
	ldrh	w2, [x7, 14]
	strh	w3, [x1, 56]
	strh	w6, [x1, 58]
	cmp	w2, 4
	strh	w0, [x1, 60]
	bne	.L10
	ldrh	w2, [x7, 6]
	lsr	w2, w2, 1
	strh	w2, [x1, 46]
	mov	w2, 8
	strh	w2, [x1, 62]
.L10:
	add	x2, x4, :lo12:.LANCHOR0
	mov	x1, 0
	add	x5, x2, 64
.L11:
	strb	w1, [x1, x5]
	add	x1, x1, 1
	cmp	x1, 32
	bne	.L11
	mov	w1, 5
	strh	wzr, [x2, 98]
	strh	w1, [x2, 96]
	cmp	w3, 1
	bne	.L12
	strh	w3, [x2, 96]
.L12:
	add	x3, x4, :lo12:.LANCHOR0
	mul	w6, w6, w0
	mov	w1, 640
	and	w6, w6, 65535
	ldrh	w5, [x3, 46]
	strh	w1, [x3, 100]
	strh	w6, [x3, 36]
	mul	w0, w0, w5
	and	w0, w0, 65535
	strh	w0, [x3, 102]
	bl	Ftl_log2
	ldrh	w9, [x7, 12]
	ldrh	w8, [x3, 62]
	strh	w0, [x3, 104]
	strh	w9, [x3, 106]
	mul	w0, w6, w9
	strh	w9, [x3, 108]
	strh	w0, [x3, 110]
	mov	w0, w8
	bl	Ftl_log2
	and	w2, w0, 65535
	strh	w0, [x3, 112]
	ubfiz	w0, w8, 9, 7
	strh	w0, [x3, 114]
	ubfx	w0, w0, 8, 8
	strh	w0, [x3, 116]
	ldrh	w0, [x7, 20]
	strh	w0, [x3, 118]
	mul	w0, w6, w5
	str	w0, [x3, 52]
	mul	w0, w0, w8
	mul	w8, w8, w9
	mul	w0, w0, w9
	asr	w0, w0, 11
	str	w0, [x3, 120]
	mov	w0, 5120
	sdiv	w0, w0, w8
	and	w0, w0, 65535
	cmp	w0, 4
	bls	.L13
.L19:
	add	x1, x4, :lo12:.LANCHOR0
	strh	w0, [x3, 124]
	mov	w0, 640
	lsl	w5, w5, 6
	asr	w0, w0, w2
	add	w2, w2, 9
	add	w0, w0, 2
	strh	w0, [x1, 126]
	asr	w5, w5, w2
	strh	w5, [x1, 128]
	and	w5, w5, 65535
	cmp	w6, 1
	mul	w0, w5, w6
	add	w5, w5, 8
	str	w0, [x1, 132]
	ldrh	w0, [x1, 124]
	udiv	w0, w0, w6
	add	w5, w0, w5
	beq	.L15
.L20:
	add	x4, x4, :lo12:.LANCHOR0
	str	w5, [x1, 32]
	ldrh	w0, [x4, 32]
	bl	FtlSysBlkNumInit
	ldr	w0, [x4, 32]
	str	w0, [x4, 136]
	ldr	w0, [x4, 48]
	ldrh	w1, [x4, 106]
	str	wzr, [x4, 144]
	lsl	w0, w0, 2
	mul	w0, w0, w1
	ldrh	w1, [x4, 112]
	add	w1, w1, 9
	lsr	w0, w0, w1
	add	w0, w0, 2
	strh	w0, [x4, 140]
	mov	w0, 32
	strh	w0, [x4, 142]
	ldrh	w0, [x4, 124]
	add	w0, w0, 3
	strh	w0, [x4, 124]
	ldr	w0, [x4, 132]
	add	w0, w0, 3
	str	w0, [x4, 132]
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L13:
	mov	w0, 4
	b	.L19
.L15:
	add	w5, w5, 4
	b	.L20
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w2, [x1, 148]
	cbz	w2, .L25
	ldrh	w2, [x1, 124]
	ldr	x3, [x1, 152]
	mov	x1, 0
.L23:
	cmp	w2, w1, uxth
	bhi	.L24
.L25:
	mov	w0, 0
	ret
.L24:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L23
	mov	w0, 1
	ret
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	sftl_get_density
	.type	sftl_get_density, %function
sftl_get_density:
	adrp	x0, .LANCHOR0+144
	ldr	w0, [x0, #:lo12:.LANCHOR0+144]
	ret
	.size	sftl_get_density, .-sftl_get_density
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
	add	x19, x19, 160
	ldrh	w0, [x19, -58]
	udiv	w3, w1, w0
	and	w2, w3, 65535
	msub	w3, w3, w0, w1
	add	x0, x19, x2, uxth 3
	and	w3, w3, 65535
	ldr	x0, [x0, 32]
	ubfx	x5, x3, 5, 11
	lsl	x5, x5, 2
	lsl	w4, w4, w3
	ldr	w6, [x0, x5]
	orr	w4, w4, w6
	str	w4, [x0, x5]
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	sftl_printk
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
	ldrh	w1, [x3, 102]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	and	w0, w0, 65535
	ldr	x1, [x2, 192]
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
	ldrh	w6, [x0, 102]
	ldrh	w0, [x0, 58]
	mul	w6, w6, w0
	cmp	w4, w6
	blt	.L42
	mov	w0, w5
	ret
.L42:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L35:
	mov	w0, w4
	bl	FtlBbmIsBadBlock
	cbz	w0, .L34
	add	w5, w5, 1
	and	w5, w5, 65535
.L34:
	add	w4, w4, 1
	and	w4, w4, 65535
	cmp	w4, w6
	blt	.L35
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
	ldrh	w2, [x4, 60]
	ldrh	w4, [x4, 102]
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
	ldrh	w1, [x2, 60]
	ldrh	w2, [x2, 102]
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
	ldrh	w2, [x1, 102]
	ldrh	w1, [x1, 60]
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
	bls	.L47
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	ret
.L47:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	adrp	x0, .LANCHOR0+262
	ldrh	w0, [x0, #:lo12:.LANCHOR0+262]
	cmp	w0, 0
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR0+262
	ldrh	w0, [x0, #:lo12:.LANCHOR0+262]
	cmp	w0, 1024
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	adrp	x0, .LANCHOR0
	add	x2, x0, :lo12:.LANCHOR0
	add	x1, x2, 256
	ldrh	w3, [x1, 6]
	cbz	w3, .L51
	ldrh	w5, [x2, 2340]
	mov	w6, 0
	ldrh	w3, [x1, 2]
	mov	w4, 0
	ldrh	w2, [x1, 4]
	and	w5, w5, 31
.L53:
	cmp	w5, w4
	bgt	.L54
	cbz	w6, .L51
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0, 258]
	strh	w2, [x0, 260]
.L51:
	ret
.L54:
	add	x6, x1, x3, sxtw 1
	add	w4, w4, 1
	add	w3, w3, 1
	and	w4, w4, 65535
	and	w3, w3, 1023
	ldrh	w7, [x6, 8]
	add	x6, x1, x2, sxtw 1
	strh	w7, [x6, 8]
	mov	w6, 1
	add	w2, w2, w6
	and	w2, w2, 1023
	b	.L53
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 256
	and	w0, w0, 65535
	ldrh	w4, [x1, 6]
	cmp	w4, 1024
	beq	.L64
	ldrh	w5, [x1, 2]
	mov	w3, 0
.L62:
	cmp	w3, w4
	bcc	.L63
.L64:
	mov	w0, 0
	ret
.L63:
	add	w2, w3, w5
	ubfiz	x2, x2, 1, 10
	add	x2, x1, x2
	ldrh	w2, [x2, 8]
	cmp	w2, w0
	beq	.L65
	add	w3, w3, 1
	b	.L62
.L65:
	mov	w0, 1
	ret
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x2, .LANCHOR0
	add	x3, x2, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w1, [x3, 44]
	cmp	w1, w0
	bls	.L82
	mov	w7, 6
	ldr	x12, [x3, 2360]
	mov	w1, -1
	umull	x11, w0, w7
	add	x5, x12, x11
	strh	w1, [x5, 2]
	strh	w1, [x12, x11]
	ldr	x1, [x3, 2368]
	cbnz	x1, .L69
	str	x5, [x3, 2368]
.L82:
	mov	w0, 0
	ret
.L69:
	stp	x29, x30, [sp, -16]!
	ubfiz	x8, x0, 1, 16
	mov	x4, x2
	mov	x9, -6148914691236517206
	add	x29, sp, 0
	ldr	x14, [x3, 2376]
	movk	x9, 0xaaab, lsl 0
	ldrh	w2, [x5, 4]
	mov	w10, -1
	ldr	x13, [x3, 2360]
	mov	w30, 65535
	cmp	w2, 0
	ldr	x15, [x3, 2384]
	ldrh	w6, [x14, x8]
	ldrh	w18, [x3, 44]
	mov	w3, 0
	mul	w6, w6, w2
	sub	x2, x1, x13
	asr	x2, x2, 1
	csel	w6, w6, w10, ne
	mul	x2, x2, x9
	add	x9, x15, x8
	mov	w8, w7
	and	w2, w2, 65535
.L77:
	add	w3, w3, 1
	and	w3, w3, 65535
	cmp	w3, w18
	bhi	.L68
	cmp	w0, w2
	beq	.L68
	ubfiz	x16, x2, 1, 16
	ldrh	w17, [x1, 4]
	cmp	w17, 0
	ldrh	w7, [x14, x16]
	mul	w7, w7, w17
	csel	w7, w7, w10, ne
	cmp	w6, w7
	bne	.L73
	ldrh	w16, [x15, x16]
	ldrh	w7, [x9]
	cmp	w16, w7
	bcc	.L75
.L74:
	strh	w2, [x12, x11]
	ldrh	w2, [x1, 2]
	strh	w2, [x5, 2]
	add	x2, x4, :lo12:.LANCHOR0
	ldr	x3, [x2, 2368]
	cmp	x1, x3
	bne	.L78
	strh	w0, [x1, 2]
	str	x5, [x2, 2368]
	b	.L68
.L73:
	bcc	.L74
.L75:
	ldrh	w7, [x1]
	cmp	w7, w30
	bne	.L76
	strh	w2, [x5, 2]
	add	x2, x4, :lo12:.LANCHOR0
	strh	w0, [x1]
	str	x5, [x2, 2392]
.L68:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L76:
	umaddl	x1, w7, w8, x13
	mov	w2, w7
	b	.L77
.L78:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2360]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L68
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	insert_data_list
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2400]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 2400]
	ldrh	w1, [x1, 44]
	cmp	w1, w0
	bcs	.L84
	adrp	x1, .LANCHOR1
	adrp	x0, .LC1
	mov	w2, 221
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L84:
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
	beq	.L88
	adrp	x2, .LANCHOR0
	add	x5, x2, :lo12:.LANCHOR0
	mov	w6, 6
	mov	w1, -1
	mov	x3, x2
	ldr	x9, [x5, 2360]
	umull	x8, w0, w6
	add	x4, x9, x8
	strh	w1, [x4, 2]
	strh	w1, [x9, x8]
	ldr	x1, [x5, 2408]
	cbnz	x1, .L89
	str	x4, [x5, 2408]
.L88:
	mov	w0, 0
	ret
.L89:
	ldr	x11, [x5, 2384]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5, 2360]
	mov	x5, -6148914691236517206
	movk	x5, 0xaaab, lsl 0
	ldrh	w12, [x11, x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	mul	x2, x2, x5
	and	w2, w2, 65535
.L92:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11, x5]
	cmp	w5, w12
	bcs	.L90
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L91
	strh	w2, [x4, 2]
	strh	w0, [x1]
	b	.L88
.L91:
	umaddl	x1, w5, w6, x10
	mov	w2, w5
	b	.L92
.L90:
	ldrh	w5, [x1, 2]
	strh	w5, [x4, 2]
	strh	w2, [x9, x8]
	add	x2, x3, :lo12:.LANCHOR0
	ldr	x3, [x2, 2408]
	cmp	x1, x3
	bne	.L93
	strh	w0, [x1, 2]
	str	x4, [x2, 2408]
	b	.L88
.L93:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2360]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L88
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	insert_free_list
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2416]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 2416]
	ldrh	w1, [x1, 44]
	cmp	w1, w0
	bcs	.L94
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 214
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L94:
	ldp	x29, x30, [sp], 16
	ret
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	stp	x29, x30, [sp, -64]!
	and	w1, w1, 65535
	mov	w2, 6
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	str	x23, [sp, 48]
	mov	x23, x0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	umull	x21, w1, w2
	ldr	x22, [x0, 2360]
	mov	w0, 65535
	add	x20, x22, x21
	ldrh	w1, [x20, 2]
	cmp	w1, w0
	bne	.L98
	ldr	x0, [x23]
	cmp	x20, x0
	beq	.L98
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 379
	add	x1, x1, 48
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L98:
	ldr	x0, [x23]
	mov	w1, 65535
	cmp	x20, x0
	ldrh	w0, [x22, x21]
	bne	.L99
	cmp	w0, w1
	bne	.L100
	str	xzr, [x23]
.L101:
	mov	w0, -1
	strh	w0, [x22, x21]
	strh	w0, [x20, 2]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L100:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 6
	ldr	x1, [x19, 2360]
	umaddl	x0, w0, w2, x1
	mov	w1, -1
	str	x0, [x23]
	strh	w1, [x0, 2]
	b	.L101
.L99:
	cmp	w0, w1
	ldrh	w1, [x20, 2]
	bne	.L102
	cmp	w1, w0
	beq	.L101
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 6
	mov	w2, -1
	umull	x1, w1, w0
	ldr	x0, [x19, 2360]
	strh	w2, [x0, x1]
	b	.L101
.L102:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 6
	ldr	x3, [x19, 2360]
	umaddl	x0, w0, w2, x3
	strh	w1, [x0, 2]
	ldrh	w0, [x20, 2]
	ldr	x1, [x19, 2360]
	ldrh	w3, [x22, x21]
	umull	x0, w0, w2
	strh	w3, [x1, x0]
	b	.L101
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
	ldr	x2, [x0]
	cbz	x2, .L110
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LANCHOR0+2360
	and	w1, w1, 65535
	mov	w4, 65535
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	w5, 6
	ldr	x19, [x3, #:lo12:.LANCHOR0+2360]
.L106:
	cbnz	w1, .L107
.L109:
	sub	x19, x2, x19
	mov	x2, -6148914691236517206
	asr	x19, x19, 1
	movk	x2, 0xaaab, lsl 0
	mul	x19, x19, x2
	and	w19, w19, 65535
	mov	w1, w19
	bl	List_remove_node
	mov	w0, w19
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L107:
	ldrh	w3, [x2]
	cmp	w3, w4
	beq	.L109
	sub	w1, w1, #1
	umaddl	x2, w3, w5, x19
	and	w1, w1, 65535
	b	.L106
.L110:
	mov	w0, 65535
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
	and	w2, w0, 65535
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 2368]
	cbz	x1, .L122
	ldr	x3, [x0, 2360]
	mov	w4, 6
	mov	w0, 65535
.L119:
	cbz	w2, .L120
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L121
	ret
.L121:
	sub	w2, w2, #1
	umaddl	x1, w1, w4, x3
	and	w2, w2, 65535
	b	.L119
.L122:
	mov	w0, 65535
	ret
.L120:
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 65535
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	ldrh	w1, [x0, 2424]
	cmp	w1, w20
	beq	.L125
	ldrh	w1, [x0, 2472]
	cmp	w1, w20
	beq	.L125
	ldrh	w1, [x0, 2520]
	cmp	w1, w20
	beq	.L125
	mov	w22, 6
	ldr	x24, [x0, 2360]
	ldr	x1, [x0, 2368]
	umull	x22, w20, w22
	add	x23, x24, x22
	cmp	x23, x1
	beq	.L125
	ldr	x2, [x0, 2376]
	ubfiz	x1, x20, 1, 16
	ldrh	w0, [x23, 4]
	cmp	w0, 0
	ldrh	w21, [x2, x1]
	mov	w1, 65535
	mul	w21, w21, w0
	ldrh	w0, [x23, 2]
	csinv	w21, w21, wzr, ne
	cmp	w0, w1
	bne	.L128
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	bne	.L128
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 470
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L128:
	ldrh	w0, [x23, 2]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L129
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	beq	.L125
.L129:
	mov	w1, 6
	add	x22, x19, :lo12:.LANCHOR0
	mov	x2, -6148914691236517206
	umull	x0, w0, w1
	movk	x2, 0xaaab, lsl 0
	asr	x1, x0, 1
	mul	x1, x1, x2
	ldr	x2, [x22, 2376]
	ldrh	w1, [x2, x1, lsl 1]
	ldr	x2, [x22, 2360]
	add	x0, x2, x0
	ldrh	w2, [x0, 4]
	cmp	w2, 0
	mul	w0, w1, w2
	csinv	w0, w0, wzr, ne
	cmp	w21, w0
	bcs	.L125
	mov	w1, w20
	add	x0, x22, 2368
	bl	List_remove_node
	ldrh	w0, [x22, 2400]
	cbnz	w0, .L131
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 481
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L131:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2400]
	sub	w0, w0, #1
	strh	w0, [x19, 2400]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L125:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LANCHOR0
	add	x0, x1, :lo12:.LANCHOR0
	mov	x3, 0
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	w4, 65535
	ldrh	w2, [x0, 142]
	ldr	x0, [x0, 2568]
.L136:
	and	w19, w3, 65535
	cmp	w19, w2
	bcc	.L138
	add	x4, x0, 4
	mov	w19, w2
	mov	w6, -2147483648
	mov	w3, 0
.L139:
	cmp	w3, w2
	bne	.L141
	cmp	w19, w2
	bcc	.L137
	add	x1, x1, :lo12:.LANCHOR0
	mov	w19, w2
	mov	w3, -1
	ldrh	w4, [x1, 2576]
	mov	w1, 0
.L142:
	cmp	w1, w2
	bne	.L144
	cmp	w19, w1
	bcc	.L137
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 784
	add	x1, x1, 96
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L137
.L138:
	add	x3, x3, 1
	add	x5, x0, x3, lsl 4
	ldrh	w5, [x5, -16]
	cmp	w5, w4
	bne	.L136
.L137:
	mov	w0, w19
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L141:
	ldr	w5, [x4]
	tbnz	w5, #31, .L140
	cmp	w6, w5
	bls	.L140
	mov	w6, w5
	mov	w19, w3
.L140:
	add	w3, w3, 1
	add	x4, x4, 16
	and	w3, w3, 65535
	b	.L139
.L144:
	ldr	w6, [x0, 4]
	cmp	w3, w6
	bls	.L143
	ldrh	w5, [x0]
	cmp	w5, w4
	csel	w3, w3, w6, eq
	csel	w19, w19, w1, eq
.L143:
	add	w1, w1, 1
	add	x0, x0, 16
	and	w1, w1, 65535
	b	.L142
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR0
	add	x3, x1, :lo12:.LANCHOR0
	ldrh	w2, [x3, 2578]
	cmp	w2, 4
	bhi	.L147
	cbnz	w0, .L147
	add	w2, w2, 1
	strh	w2, [x3, 2578]
	ret
.L147:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w6, 65535
	mov	x1, 0
	ldrh	w4, [x0, 44]
	ldr	x5, [x0, 2376]
	strh	wzr, [x0, 2578]
	str	wzr, [x0, 2580]
.L148:
	cmp	w4, w1, uxth
	bhi	.L150
	ret
.L150:
	ldrh	w3, [x5, x1, lsl 1]
	cmp	w3, w6
	beq	.L149
	ldr	w2, [x0, 2580]
	add	w2, w2, w3
	str	w2, [x0, 2580]
.L149:
	add	x1, x1, 1
	b	.L148
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	and	w0, w0, 65535
	cbz	w1, .L153
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_set_blk_mode.part.7
	ldp	x29, x30, [sp], 16
	ret
.L153:
	adrp	x1, .LANCHOR0+24
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1, #:lo12:.LANCHOR0+24]
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
	adrp	x0, .LANCHOR0+24
	ldr	x0, [x0, #:lo12:.LANCHOR0+24]
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
	ldrh	w3, [x1, 36]
	mov	w1, 65535
.L161:
	cmp	w3, w2, uxth
	bhi	.L163
	add	x4, x4, :lo12:.LANCHOR0
	ubfiz	x3, x3, 1, 16
	add	x3, x3, 16
	add	x2, x0, 16
	add	x3, x0, x3
	mov	w5, 65535
	ldrh	w1, [x4, 106]
	sub	w1, w1, #1
	and	w1, w1, 65535
	sub	w1, w1, w6
.L164:
	cmp	x2, x3
	bne	.L166
	ret
.L163:
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5, 16]
	cmp	w5, w1
	beq	.L162
	ldrh	w5, [x0, 4]
	add	w5, w5, 1
	strh	w5, [x0, 4]
.L162:
	add	w2, w2, 1
	b	.L161
.L166:
	ldrh	w4, [x2]
	cmp	w4, w5
	beq	.L165
	ldrh	w4, [x0, 4]
	add	w4, w1, w4
	strh	w4, [x0, 4]
.L165:
	add	x2, x2, 2
	b	.L164
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	ldrh	w1, [x0, 4]
	cbz	w1, .L167
	ldrh	w2, [x0]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L167
	ldrb	w2, [x0, 6]
	add	x2, x2, 8
	ldrh	w3, [x0, x2, lsl 1]
	adrp	x2, .LANCHOR0+36
	ldrh	w4, [x2, #:lo12:.LANCHOR0+36]
	mov	w2, w1
.L170:
	cmp	w3, w2
	beq	.L172
.L167:
	ret
.L172:
	ldrb	w1, [x0, 6]
	add	w1, w1, 1
	and	w1, w1, 255
	strb	w1, [x0, 6]
	cmp	w1, w4
	bne	.L171
	ldrh	w1, [x0, 2]
	strb	wzr, [x0, 6]
	add	w1, w1, 1
	strh	w1, [x0, 2]
.L171:
	ldrb	w1, [x0, 6]
	add	x1, x1, 8
	ldrh	w3, [x0, x1, lsl 1]
	b	.L170
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldrh	w0, [x0, 44]
	cmp	w1, w0
	bcc	.L174
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2097
	add	x1, x1, 120
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L174:
	add	x0, x20, :lo12:.LANCHOR0
	add	x6, x19, 16
	add	x7, x0, 64
	strh	wzr, [x19, 4]
	strb	wzr, [x19, 7]
	mov	x5, 0
	ldrh	w8, [x0, 36]
	mov	w9, -1
.L175:
	cmp	w8, w5, uxth
	bhi	.L177
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19, 7]
	ldrh	w1, [x20, 106]
	mul	w0, w0, w1
	strh	w0, [x19, 4]
	mov	w0, 1
	strb	w0, [x19, 9]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L177:
	ldrh	w1, [x19]
	ldrb	w0, [x7, x5]
	bl	V2P_block
	mov	w4, w0
	strh	w9, [x6]
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L176
	strh	w4, [x6]
	ldrb	w0, [x19, 7]
	add	w0, w0, 1
	strb	w0, [x19, 7]
.L176:
	add	x5, x5, 1
	add	x6, x6, 2
	b	.L175
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
	and	w8, w0, 65535
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	mov	x9, 0
	mov	w6, 0
	mov	x5, x0
	add	x7, x1, 64
	ldrh	w10, [x1, 36]
	ldrh	w11, [x1, 106]
	cmp	w10, w9, uxth
	bhi	.L191
	cbz	w6, .L189
	mov	w0, 32768
	sdiv	w6, w0, w6
.L190:
	add	x0, x5, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x0, [x0, 2360]
	umaddl	x8, w8, w1, x0
	mov	w0, 0
	strh	w6, [x8, 4]
	ret
.L184:
	mov	w6, 0
	b	.L183
.L189:
	mov	w6, 0
	b	.L190
.L191:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L182:
	ldrb	w0, [x7, x9]
	mov	w1, w8
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L181
	add	w6, w6, w11
	and	w6, w6, 65535
.L181:
	add	x9, x9, 1
	cmp	w10, w9, uxth
	bhi	.L182
	cbz	w6, .L184
	mov	w0, 32768
	sdiv	w6, w0, w6
.L183:
	add	x0, x5, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x0, [x0, 2360]
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
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1, 2408]
	cbz	x0, .L194
	ldr	x2, [x1, 2360]
	ldr	x1, [x1, 2384]
	sub	x0, x0, x2
	mov	x2, -6148914691236517206
	asr	x0, x0, 1
	movk	x2, 0xaaab, lsl 0
	mul	x0, x0, x2
	and	x0, x0, 65535
	ldrh	w0, [x1, x0, lsl 1]
	ret
.L194:
	mov	w0, 0
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR0
	add	x4, x2, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldr	x1, [x4, 2408]
	cbz	x1, .L202
	ldrh	w3, [x4, 2416]
	mov	w5, 7
	ldr	x4, [x4, 2360]
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
.L198:
	cmp	w0, w3
	beq	.L201
	umull	x5, w1, w6
	ldrh	w5, [x4, x5]
	cmp	w5, w7
	bne	.L199
.L201:
	add	x2, x2, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2, 2384]
	ldrh	w0, [x0, x1]
	ret
.L199:
	add	w3, w3, 1
	mov	w1, w5
	and	w3, w3, 65535
	b	.L198
.L202:
	mov	w0, 0
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -96]!
	adrp	x1, .LC2
	add	x1, x1, :lo12:.LC2
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR0
	add	x21, x23, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	add	x20, x0, 12
	str	x27, [sp, 80]
	bl	strcpy
	ldr	w2, [x21, 120]
	mov	x0, x20
	adrp	x1, .LC3
	add	x1, x1, :lo12:.LC3
	bl	sprintf
	add	x20, x20, x0, sxtw
	mov	x0, x20
	adrp	x1, .LC4
	add	x1, x1, :lo12:.LC4
	bl	strcpy
	ldr	w2, [x21, 2584]
	add	x20, x20, 10
	mov	x0, x20
	adrp	x1, .LC5
	add	x1, x1, :lo12:.LC5
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2580]
	mov	x0, x20
	adrp	x1, .LC6
	add	x1, x1, :lo12:.LC6
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2588]
	mov	x0, x20
	adrp	x1, .LC7
	add	x1, x1, :lo12:.LC7
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2592]
	mov	x0, x20
	adrp	x1, .LC8
	add	x1, x1, :lo12:.LC8
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2596]
	mov	x0, x20
	adrp	x1, .LC9
	add	x1, x1, :lo12:.LC9
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2600]
	mov	x0, x20
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2604]
	mov	x0, x20
	adrp	x1, .LC11
	add	x1, x1, :lo12:.LC11
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2608]
	mov	x0, x20
	adrp	x1, .LC12
	add	x1, x1, :lo12:.LC12
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2612]
	mov	x0, x20
	adrp	x1, .LC13
	add	x1, x1, :lo12:.LC13
	lsr	w2, w2, 11
	bl	sprintf
	ldr	w2, [x21, 2616]
	add	x20, x20, x0, sxtw
	mov	x0, x20
	adrp	x1, .LC14
	add	x1, x1, :lo12:.LC14
	lsr	w2, w2, 11
	bl	sprintf
	ldr	w2, [x21, 2620]
	add	x20, x20, x0, sxtw
	mov	x0, x20
	adrp	x1, .LC15
	add	x1, x1, :lo12:.LC15
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2624]
	mov	x0, x20
	adrp	x1, .LC16
	add	x1, x1, :lo12:.LC16
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 166]
	mov	x0, x20
	adrp	x1, .LC17
	add	x1, x1, :lo12:.LC17
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2416]
	mov	x0, x20
	adrp	x1, .LC18
	add	x1, x1, :lo12:.LC18
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2628]
	mov	x0, x20
	adrp	x1, .LC19
	add	x1, x1, :lo12:.LC19
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2632]
	mov	x0, x20
	adrp	x1, .LC20
	add	x1, x1, :lo12:.LC20
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2636]
	mov	x0, x20
	adrp	x1, .LC21
	add	x1, x1, :lo12:.LC21
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2640]
	mov	x0, x20
	adrp	x1, .LC22
	add	x1, x1, :lo12:.LC22
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2644]
	mov	x0, x20
	adrp	x1, .LC23
	add	x1, x1, :lo12:.LC23
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 2648]
	mov	x0, x20
	adrp	x1, .LC24
	add	x1, x1, :lo12:.LC24
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2342]
	mov	x0, x20
	adrp	x1, .LC25
	add	x1, x1, :lo12:.LC25
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2340]
	mov	x0, x20
	adrp	x1, .LC26
	add	x1, x1, :lo12:.LC26
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 144]
	mov	x0, x20
	adrp	x1, .LC27
	add	x1, x1, :lo12:.LC27
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 136]
	mov	x0, x20
	adrp	x1, .LC28
	add	x1, x1, :lo12:.LC28
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 32]
	mov	x0, x20
	adrp	x1, .LC29
	add	x1, x1, :lo12:.LC29
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 262]
	mov	x0, x20
	adrp	x1, .LC30
	add	x1, x1, :lo12:.LC30
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 44]
	mov	x0, x20
	adrp	x1, .LC31
	add	x1, x1, :lo12:.LC31
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2652]
	mov	x0, x20
	adrp	x1, .LC32
	add	x1, x1, :lo12:.LC32
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w2, [x21, 48]
	mov	x0, x20
	adrp	x1, .LC33
	add	x1, x1, :lo12:.LC33
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2656]
	mov	x0, x20
	adrp	x1, .LC34
	add	x1, x1, :lo12:.LC34
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 160]
	mov	x0, x20
	adrp	x1, .LC35
	add	x1, x1, :lo12:.LC35
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2426]
	mov	x0, x20
	adrp	x1, .LC36
	add	x1, x1, :lo12:.LC36
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrb	w2, [x21, 2430]
	mov	x0, x20
	adrp	x1, .LC37
	add	x1, x1, :lo12:.LC37
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2424]
	mov	x0, x20
	adrp	x1, .LC38
	add	x1, x1, :lo12:.LC38
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrb	w2, [x21, 2432]
	mov	x0, x20
	adrp	x1, .LC39
	add	x1, x1, :lo12:.LC39
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2428]
	mov	x0, x20
	adrp	x1, .LC40
	add	x1, x1, :lo12:.LC40
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w1, [x21, 2424]
	ldr	x0, [x21, 2376]
	ldrh	w2, [x0, x1, lsl 1]
	mov	x0, x20
	adrp	x1, .LC41
	add	x1, x1, :lo12:.LC41
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2474]
	mov	x0, x20
	adrp	x1, .LC42
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrb	w2, [x21, 2478]
	mov	x0, x20
	adrp	x1, .LC43
	add	x1, x1, :lo12:.LC43
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2472]
	mov	x0, x20
	adrp	x1, .LC44
	add	x1, x1, :lo12:.LC44
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrb	w2, [x21, 2480]
	mov	x0, x20
	adrp	x1, .LC45
	add	x1, x1, :lo12:.LC45
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2476]
	mov	x0, x20
	adrp	x1, .LC46
	add	x1, x1, :lo12:.LC46
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w1, [x21, 2472]
	ldr	x0, [x21, 2376]
	ldrh	w2, [x0, x1, lsl 1]
	mov	x0, x20
	adrp	x1, .LC47
	add	x1, x1, :lo12:.LC47
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2522]
	mov	x0, x20
	adrp	x1, .LC48
	add	x1, x1, :lo12:.LC48
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrb	w2, [x21, 2526]
	mov	x0, x20
	adrp	x1, .LC49
	add	x1, x1, :lo12:.LC49
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2520]
	mov	x0, x20
	adrp	x1, .LC50
	add	x1, x1, :lo12:.LC50
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrb	w2, [x21, 2528]
	mov	x0, x20
	adrp	x1, .LC51
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2524]
	mov	x0, x20
	adrp	x1, .LC52
	add	x1, x1, :lo12:.LC52
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2674]
	mov	x0, x20
	adrp	x1, .LC53
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrb	w2, [x21, 2678]
	mov	x0, x20
	adrp	x1, .LC54
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2672]
	mov	x0, x20
	adrp	x1, .LC55
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrb	w2, [x21, 2680]
	mov	x0, x20
	adrp	x1, .LC56
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldrh	w2, [x21, 2676]
	mov	x0, x20
	adrp	x1, .LC57
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x20, x20, x0, sxtw
	ldr	w4, [x21, 2796]
	adrp	x1, .LC58
	ldr	w3, [x21, 2804]
	add	x1, x1, :lo12:.LC58
	ldr	w2, [x21, 2800]
	mov	x0, x20
	bl	sprintf
	add	x19, x20, x0, sxtw
	ldr	w2, [x21, 2792]
	mov	x0, x19
	adrp	x1, .LC59
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21, 2816]
	mov	x0, x19
	adrp	x1, .LC60
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21, 3232]
	mov	x0, x19
	adrp	x1, .LC61
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21, 3234]
	mov	x0, x19
	adrp	x1, .LC62
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21, 3236]
	mov	x0, x19
	adrp	x1, .LC63
	add	x1, x1, :lo12:.LC63
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21, 3240]
	adrp	x1, .LC64
	add	x1, x1, :lo12:.LC64
	mov	x0, x19
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	and	w2, w0, 65535
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x21, 2416]
	bl	GetFreeBlockMaxEraseCount
	and	w2, w0, 65535
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x19, x19, x0, sxtw
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	cmp	w0, 1
	beq	.L204
.L209:
	sub	w0, w19, w22
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L204:
	ldrh	w0, [x21, 2672]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L206
	ldr	x1, [x21, 2376]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x1, x0]
	mov	x0, x19
	adrp	x1, .LC67
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, x0, sxtw
.L206:
	mov	w0, 0
	adrp	x24, .LC68
	bl	List_get_gc_head_node
	add	x21, x23, :lo12:.LANCHOR0
	and	w3, w0, 65535
	add	x24, x24, :lo12:.LC68
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
.L208:
	cmp	w3, w27
	beq	.L207
	umull	x25, w3, w26
	ldr	x0, [x21, 2360]
	ldr	x2, [x21, 2376]
	ubfiz	x1, x3, 1, 16
	ldr	x4, [x21, 2384]
	add	x0, x0, x25
	ldrh	w5, [x0, 4]
	mov	x0, x19
	ldrh	w6, [x4, x1]
	ldrh	w4, [x2, x1]
	mov	w2, w20
	mov	x1, x24
	add	w20, w20, 1
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21, 2360]
	cmp	w20, 16
	ldrh	w3, [x0, x25]
	bne	.L208
.L207:
	add	x23, x23, :lo12:.LANCHOR0
	adrp	x21, .LC69
	add	x21, x21, :lo12:.LC69
	mov	w20, 0
	mov	w25, 65535
	mov	w26, 6
	ldr	x0, [x23, 2360]
	ldr	x3, [x23, 2408]
	sub	x3, x3, x0
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	movk	x0, 0xaaab, lsl 0
	mul	x3, x3, x0
	and	w3, w3, 65535
.L210:
	cmp	w3, w25
	beq	.L209
	umull	x24, w3, w26
	ldr	x0, [x23, 2360]
	ldr	x2, [x23, 2384]
	ubfiz	x1, x3, 1, 16
	add	x0, x0, x24
	ldrh	w5, [x2, x1]
	mov	w2, w20
	ldrh	w4, [x0, 4]
	mov	x1, x21
	mov	x0, x19
	add	w20, w20, 1
	bl	sprintf
	add	x19, x19, x0, sxtw
	cmp	w20, 4
	beq	.L209
	ldr	x0, [x23, 2360]
	ldrh	w3, [x0, x24]
	b	.L210
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LC70
	adrp	x1, .LC71
	add	x2, x2, :lo12:.LC70
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC71
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	bl	sprintf
	add	x19, x20, x0, sxtw
	mov	x0, x19
	bl	FtlPrintInfo2buf
	add	x0, x19, x0, sxtw
	sub	w0, w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	stp	x29, x30, [sp, -32]!
	adrp	x8, .LANCHOR0
	add	x0, x8, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	ldr	w2, [x0, 2636]
	ldr	w1, [x0, 2648]
	cmp	w2, w1
	bcs	.L219
	ldrh	w2, [x0, 44]
	mov	x1, 0
	ldr	x4, [x0, 2384]
	str	wzr, [x0, 2628]
.L220:
	cmp	w2, w1
	bhi	.L221
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w0, [x1, 2628]
	udiv	w2, w0, w2
	str	w2, [x1, 2636]
	ldr	w2, [x1, 2632]
	sub	w0, w0, w2
	ldrh	w2, [x1, 96]
	udiv	w0, w0, w2
	str	w0, [x1, 2628]
.L222:
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w9, [x1, 2648]
	ldr	w11, [x1, 2636]
	add	w0, w9, 256
	cmp	w0, w11
	bls	.L227
	ldr	w1, [x1, 2644]
	add	w0, w9, 768
	cmp	w0, w1
	bls	.L227
.L229:
	mov	w20, 65535
.L228:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L221:
	ldrh	w3, [x4, x1, lsl 1]
	add	x1, x1, 1
	ldr	w5, [x0, 2628]
	add	w3, w3, w5
	str	w3, [x0, 2628]
	b	.L220
.L219:
	ldr	w1, [x0, 2644]
	cmp	w2, w1
	bls	.L222
	add	w1, w1, 1
	str	w1, [x0, 2644]
	mov	w1, 0
.L224:
	ldrh	w2, [x0, 44]
	cmp	w1, w2
	bcs	.L222
	ldr	x4, [x0, 2384]
	ubfiz	x3, x1, 1, 32
	add	w1, w1, 1
	ldrh	w2, [x4, x3]
	add	w2, w2, 1
	strh	w2, [x4, x3]
	b	.L224
.L227:
	add	x10, x8, :lo12:.LANCHOR0
	ldrh	w0, [x10, 2416]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	and	w6, w0, 65535
	add	w0, w9, 64
	cmp	w6, w0
	bcc	.L229
	ldr	x0, [x10, 2368]
	cbz	x0, .L229
	mov	w4, 65535
	ldrh	w12, [x10, 44]
	mov	x13, -6148914691236517206
	ldr	x7, [x10, 2360]
	ldr	x5, [x10, 2384]
	mov	w2, w4
	mov	w10, w4
	mov	w1, 0
	movk	x13, 0xaaab, lsl 0
	mov	w14, 6
.L230:
	ldrh	w3, [x0]
	cmp	w3, w10
	bne	.L233
	mov	w20, w2
.L232:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L229
	ubfiz	x7, x20, 1, 32
	ldrh	w3, [x5, x7]
	cmp	w9, w3
	bcs	.L234
	bl	GetFreeBlockMinEraseCount
	cmp	w9, w0, uxth
	bcs	.L234
	add	x0, x8, :lo12:.LANCHOR0
	str	w4, [x0, 2648]
.L234:
	cmp	w11, w3
	bls	.L229
	add	w0, w3, 128
	cmp	w6, w0
	ble	.L229
	add	w0, w3, 256
	cmp	w11, w0
	bhi	.L235
	add	x0, x8, :lo12:.LANCHOR0
	add	w3, w3, 768
	ldr	w0, [x0, 2644]
	cmp	w3, w0
	bcs	.L229
.L235:
	add	x19, x8, :lo12:.LANCHOR0
	ldrh	w5, [x5, x7]
	mov	w2, w11
	mov	w1, w20
	ldr	x0, [x19, 2376]
	ldr	w3, [x19, 2644]
	ldrh	w4, [x0, x7]
	adrp	x0, .LC72
	add	x0, x0, :lo12:.LC72
	bl	sftl_printk
	mov	w0, 1
	str	w0, [x19, 3244]
	b	.L228
.L233:
	add	w1, w1, 1
	and	w1, w1, 65535
	cmp	w1, w12
	bhi	.L229
	ldrh	w15, [x0, 4]
	cbz	w15, .L231
	sub	x0, x0, x7
	asr	x0, x0, 1
	mul	x0, x0, x13
	and	w20, w0, 65535
	and	x0, x0, 65535
	ldrh	w0, [x5, x0, lsl 1]
	cmp	w9, w0
	bcs	.L232
	cmp	w4, w0
	bls	.L231
	mov	w4, w0
	mov	w2, w20
.L231:
	umaddl	x0, w3, w14, x7
	b	.L230
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
	and	w0, w0, 65535
	mov	w1, 65535
	cmp	w0, w1
	beq	.L246
	stp	x29, x30, [sp, -16]!
	adrp	x2, .LANCHOR0+2376
	ubfiz	x1, x0, 1, 16
	add	x29, sp, 0
	ldr	x2, [x2, #:lo12:.LANCHOR0+2376]
	strh	wzr, [x2, x1]
	bl	INSERT_FREE_LIST
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L246:
	mov	w0, 0
	ret
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	get_new_active_ppa
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	ldrh	w1, [x0]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L250
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2707
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L250:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x0, 106]
	cmp	w1, w0
	bne	.L251
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2708
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L251:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L252
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2709
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L252:
	ldrb	w0, [x19, 6]
	add	x1, x20, :lo12:.LANCHOR0
	strb	wzr, [x19, 10]
	add	x0, x0, 8
	ldrh	w2, [x1, 36]
	mov	w1, 65535
	ldrh	w0, [x19, x0, lsl 1]
.L253:
	cmp	w0, w1
	beq	.L255
	ldrh	w21, [x19, 2]
	mov	w3, 65535
	ldrh	w1, [x19, 4]
	orr	w21, w21, w0, lsl 10
	ldrb	w0, [x19, 6]
	sub	w1, w1, #1
	and	w1, w1, 65535
	strh	w1, [x19, 4]
.L257:
	add	w0, w0, 1
	and	w0, w0, 255
	cmp	w2, w0
	bne	.L256
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	w0, 0
.L256:
	add	x4, x19, x0, sxtw 1
	ldrh	w4, [x4, 16]
	cmp	w4, w3
	beq	.L257
	add	x20, x20, :lo12:.LANCHOR0
	strb	w0, [x19, 6]
	ldrh	w2, [x19, 2]
	ldrh	w0, [x20, 106]
	cmp	w2, w0
	bne	.L249
	cbz	w1, .L249
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2730
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L249:
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L255:
	ldrb	w0, [x19, 6]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	cmp	w0, w2
	bne	.L254
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L254:
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
	b	.L253
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	mov	w3, 0
	mov	w8, 24
	mov	w9, 1
	mov	w5, 4
	str	wzr, [x1, 3248]
.L265:
	ldrh	w2, [x1, 36]
	cmp	w3, w2
	bcc	.L266
	add	x0, x0, :lo12:.LANCHOR0
	mov	w6, 24
	mov	w5, 4
.L267:
	ldr	w1, [x0, 3288]
	cmp	w2, w1
	bcc	.L268
	ret
.L266:
	umull	x4, w3, w8
	ldr	x6, [x1, 3256]
	ldr	x7, [x1, 3264]
	add	x2, x6, x4
	str	w9, [x2, 16]
	ldrh	w2, [x1, 114]
	mul	w2, w2, w3
	sdiv	w2, w2, w5
	add	x2, x7, x2, sxtw 2
	str	x2, [x6, x4]
	ldrh	w2, [x1, 116]
	ldr	x7, [x1, 3256]
	ldr	x10, [x1, 3272]
	add	x6, x7, x4
	mul	w2, w2, w3
	sdiv	w2, w2, w5
	add	x2, x10, x2, sxtw 2
	str	x2, [x6, 8]
	ldr	x2, [x1, 3280]
	ubfiz	x10, x3, 5, 16
	ldr	x4, [x7, x4]
	add	w3, w3, 1
	add	x2, x2, x10
	and	w3, w3, 65535
	str	x4, [x2, 8]
	ldr	x4, [x6, 8]
	str	x4, [x2, 16]
	b	.L265
.L268:
	umull	x4, w2, w6
	ldr	x3, [x0, 3256]
	ldr	x7, [x0, 3264]
	add	x1, x3, x4
	str	wzr, [x1, 16]
	ldrh	w1, [x0, 114]
	mul	w1, w1, w2
	sdiv	w1, w1, w5
	add	x1, x7, x1, sxtw 2
	str	x1, [x3, x4]
	ldrh	w1, [x0, 116]
	ldr	x3, [x0, 3256]
	add	x3, x3, x4
	ldr	x4, [x0, 3272]
	mul	w1, w1, w2
	add	w2, w2, 1
	and	w2, w2, 65535
	sdiv	w1, w1, w5
	add	x1, x4, x1, sxtw 2
	str	x1, [x3, 8]
	b	.L267
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	mov	w3, 0
	mov	w8, 24
	ldr	w7, [x2, 3288]
	ldr	x5, [x2, 3256]
.L270:
	cmp	w3, w1
	bcs	.L269
	ubfiz	x4, x3, 5, 16
	mov	w2, 0
	add	x4, x0, x4
	b	.L275
.L271:
	add	w2, w2, 1
	and	w2, w2, 65535
.L275:
	cmp	w2, w7
	bcs	.L272
	umull	x6, w2, w8
	add	x9, x5, x6
	ldr	x10, [x5, x6]
	ldr	x6, [x4, 8]
	cmp	x10, x6
	bne	.L271
	str	wzr, [x9, 16]
.L272:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L270
.L269:
	ret
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	mov	w3, 0
	mov	w7, 24
	mov	w8, 1
	ldr	w5, [x2, 3288]
	ldr	x6, [x2, 3256]
.L277:
	cmp	w3, w1
	bcs	.L276
	mov	w2, 0
	b	.L282
.L278:
	add	w2, w2, 1
	and	w2, w2, 65535
.L282:
	cmp	w2, w5
	bcs	.L279
	umaddl	x4, w2, w7, x6
	ldr	w9, [x4, 16]
	cbnz	w9, .L278
	ubfiz	x2, x3, 5, 16
	ldr	x9, [x4]
	add	x2, x0, x2
	str	w8, [x4, 16]
	str	x9, [x2, 8]
	ldr	x4, [x4, 8]
	str	x4, [x2, 16]
.L279:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L277
.L276:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w2, [x1, 3292]
	ldr	x3, [x1, 3296]
	mov	x1, 0
.L284:
	cmp	w2, w1, uxth
	bhi	.L286
	mov	w0, 0
	ret
.L286:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L284
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
	adrp	x3, .LANCHOR0
	add	x4, x3, :lo12:.LANCHOR0
	ldrh	w1, [x4, 3292]
	ldr	x6, [x4, 3296]
	mov	x4, 0
.L289:
	and	w2, w4, 65535
	cmp	w2, w1
	bcc	.L291
	bne	.L290
	and	x4, x4, 65535
	strh	w0, [x6, x4, lsl 1]
	add	x4, x3, :lo12:.LANCHOR0
	ldrh	w0, [x4, 3292]
	add	w0, w0, 1
	strh	w0, [x4, 3292]
	b	.L290
.L291:
	add	x4, x4, 1
	add	x2, x6, x4, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w9
	bne	.L289
.L290:
	add	x0, x3, :lo12:.LANCHOR0
	mov	w1, 12
	ldrh	w3, [x0, 3304]
	ldr	x4, [x0, 3312]
	umull	x3, w3, w1
	add	x4, x4, x3
	str	w8, [x4, 4]
	ldr	x1, [x0, 3312]
	add	x4, x1, x3
	str	w7, [x4, 8]
	str	w5, [x1, x3]
	ldrh	w1, [x0, 3304]
	add	w1, w1, 1
	strh	w1, [x0, 3304]
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshBlock
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	and	w19, w0, 65535
	mov	w1, w19
	adrp	x0, .LC73
	add	x0, x0, :lo12:.LC73
	bl	sftl_printk
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w3, [x0, 3320]
	cmp	w19, w3
	beq	.L294
	ldrh	w2, [x0, 3322]
	cmp	w19, w2
	beq	.L294
	mov	w1, 65535
	cmp	w3, w1
	bne	.L295
	strh	w19, [x0, 3320]
.L294:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L295:
	cmp	w2, w1
	bne	.L294
	strh	w19, [x0, 3322]
	b	.L294
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	and	w21, w0, 65535
	add	x20, x20, 3328
	mov	w0, w21
	bl	P2V_block_in_plane
	ldrh	w1, [x20, -4]
	mov	w2, w21
	mov	w22, w0
	adrp	x0, .LC74
	add	x0, x0, :lo12:.LC74
	bl	sftl_printk
	mov	w0, w22
	bl	FtlGcRefreshBlock
	ldrh	w0, [x20, -4]
	mov	x1, 0
.L298:
	cmp	w0, w1, uxth
	bhi	.L300
	cmp	w0, 15
	bhi	.L299
	add	x19, x19, :lo12:.LANCHOR0
	add	w1, w0, 1
	add	x19, x19, 3328
	strh	w1, [x19, -4]
	strh	w21, [x19, w0, sxtw 1]
	b	.L299
.L300:
	add	x1, x1, 1
	add	x2, x20, x1, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w21
	bne	.L298
.L299:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3324]
	cbz	w1, .L303
	ldrh	w3, [x0, 3320]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L303
	ldrh	w2, [x0, 3362]
	cmp	w2, w1
	bcc	.L304
	strh	wzr, [x0, 3362]
.L304:
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x19, 3328
	ldrh	w1, [x19, 3362]
	ldrh	w0, [x0, x1, lsl 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 3362]
	add	w0, w0, 1
	strh	w0, [x19, 3362]
.L303:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	ftl_malloc
	.type	ftl_malloc, %function
ftl_malloc:
	stp	x29, x30, [sp, -16]!
	mov	w1, 193
	movk	w1, 0x240, lsl 16
	sxtw	x0, w0
	add	x29, sp, 0
	bl	__kmalloc
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_malloc, .-ftl_malloc
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
	.global	rknand_print_hex
	.type	rknand_print_hex, %function
rknand_print_hex:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x21, x1
	stp	x23, x24, [sp, 48]
	adrp	x24, .LC75
	stp	x25, x26, [sp, 64]
	mov	w23, w2
	stp	x27, x28, [sp, 80]
	adrp	x25, .LC78
	mov	x27, x0
	mov	w26, w3
	add	x24, x24, :lo12:.LC75
	add	x25, x25, :lo12:.LC78
	stp	x19, x20, [sp, 16]
	mov	w22, 0
	mov	x19, 0
	mov	w20, 0
	adrp	x28, .LC77
.L314:
	cmp	w26, w19
	bhi	.L320
	adrp	x0, .LC79
	add	x0, x0, :lo12:.LC79
	bl	sftl_printk
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L320:
	cbnz	w20, .L315
	mov	w3, w22
	mov	x2, x21
	mov	x1, x27
	mov	x0, x24
	bl	sftl_printk
.L315:
	cmp	w23, 4
	bne	.L316
	ldr	w1, [x21, x19, lsl 2]
	adrp	x0, .LC76
	add	x0, x0, :lo12:.LC76
.L322:
	bl	sftl_printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L319
	mov	w20, 0
	adrp	x0, .LC79
	add	x0, x0, :lo12:.LC79
	bl	sftl_printk
.L319:
	add	x19, x19, 1
	add	w22, w22, w23
	b	.L314
.L316:
	cmp	w23, 2
	bne	.L318
	ldrh	w1, [x21, x19, lsl 1]
	add	x0, x28, :lo12:.LC77
	b	.L322
.L318:
	ldrb	w1, [x21, x19]
	mov	x0, x25
	b	.L322
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	stp	x29, x30, [sp, -96]!
	ubfiz	x1, x1, 5, 32
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	add	x2, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	adrp	x20, .LANCHOR1
	stp	x25, x26, [sp, 64]
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x21, .LC1
	mov	x19, x0
	ldrh	w25, [x2, 12]
	add	x23, x0, x1
	add	x20, x20, 160
	add	x21, x21, :lo12:.LC1
.L324:
	cmp	x19, x23
	bne	.L331
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L331:
	ldr	x0, [x19, 8]
	cbz	x0, .L325
	ldr	x0, [x19, 16]
	cbnz	x0, .L326
.L325:
	mov	w2, 101
	mov	x1, x20
	mov	x0, x21
	bl	sftl_printk
.L326:
	ldr	w0, [x19, 4]
	add	x26, x22, :lo12:.LANCHOR0
	add	x2, x29, 88
	add	x1, x29, 92
	bl	l2p_addr_tran.isra.0
	ldrb	w0, [x29, 88]
	ldr	w1, [x29, 92]
	ldp	x2, x3, [x19, 8]
	ldr	x4, [x26, 3392]
	blr	x4
	str	w0, [x19]
	ldrh	w0, [x26, 14]
	cmp	w0, 4
	bne	.L328
	ldrb	w0, [x29, 88]
	ldr	x4, [x26, 3392]
	ldp	x2, x3, [x19, 8]
	ldr	w1, [x29, 92]
	add	x3, x3, 8
	add	x2, x2, 2048
	add	w1, w25, w1
	blr	x4
	cmn	w0, #1
	beq	.L329
	ldr	x0, [x19, 16]
	ldr	w1, [x0, 12]
	cmn	w1, #1
	bne	.L328
	ldr	w1, [x0, 8]
	cmn	w1, #1
	bne	.L328
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L328
.L329:
	mov	w0, -1
	str	w0, [x19]
.L328:
	add	x19, x19, 32
	b	.L324
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	stp	x29, x30, [sp, -80]!
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	add	x22, x0, 172
	stp	x25, x26, [sp, 64]
	mov	w21, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x23, x24, [sp, 48]
	add	x23, x0, 3400
	mov	x25, x23
	mov	w26, 61664
	ldr	x1, [x0, 3432]
	ldr	x24, [x0, 3440]
	stp	x1, x24, [x23, 8]
.L343:
	ldrh	w0, [x19, 58]
	cmp	w21, w0
	bcc	.L348
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L348:
	ldrh	w20, [x19, 102]
	mov	w0, -1
	strh	w0, [x22]
.L345:
	ldrh	w0, [x19, 102]
	sub	w20, w20, #1
	and	w20, w20, 65535
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L346
	madd	w0, w0, w21, w20
	mov	w2, 1
	mov	w1, w2
	lsl	w0, w0, 10
	str	w0, [x25, 4]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L345
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L345
	strh	w20, [x22]
.L346:
	add	w21, w21, 1
	add	x22, x22, 2
	b	.L343
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	stp	x29, x30, [sp, -160]!
	cmp	w1, 1
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, w1
	stp	x19, x20, [sp, 16]
	adrp	x1, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x1, :lo12:.LANCHOR0
	bne	.L354
	ldrh	w19, [x1, 108]
.L355:
	sub	w19, w19, #1
	adrp	x1, ftl_temp_buf
	sxth	w19, w19
	add	x1, x1, :lo12:ftl_temp_buf
	lsl	w21, w0, 10
	str	x1, [x29, 72]
	orr	w0, w19, w21
	add	x1, x29, 96
	str	w0, [x29, 68]
	mov	w2, w23
	str	x1, [x29, 80]
	add	x0, x29, 64
	mov	w1, 1
	bl	FlashReadPages
	ldr	w0, [x29, 96]
	cmn	w0, #1
	bne	.L356
	mov	w22, 0
	mov	w24, 2
.L357:
	cmp	w22, w19
	ble	.L360
.L356:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 160
	ret
.L354:
	ldrh	w19, [x1, 106]
	b	.L355
.L360:
	add	w20, w22, w19
	mov	w2, w23
	mov	w1, 1
	sdiv	w20, w20, w24
	sxth	w0, w20
	orr	w0, w0, w21
	str	w0, [x29, 68]
	add	x0, x29, 64
	bl	FlashReadPages
	ldr	w0, [x29, 96]
	cmn	w0, #1
	bne	.L358
	ldr	w0, [x29, 100]
	cmn	w0, #1
	bne	.L358
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L357
.L358:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L357
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	stp	x29, x30, [sp, -144]!
	ubfiz	x1, x1, 5, 32
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x25, x26, [sp, 64]
	mov	w25, w2
	add	x2, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	adrp	x22, .LANCHOR1
	str	x27, [sp, 80]
	add	x22, x22, :lo12:.LANCHOR1
	adrp	x23, .LC1
	mov	w26, w3
	ldrh	w27, [x2, 12]
	mov	x20, x0
	add	x24, x0, x1
	mov	x19, x0
	add	x22, x22, 176
	add	x23, x23, :lo12:.LC1
.L363:
	cmp	x19, x24
	bne	.L371
	cbnz	w26, .L372
.L390:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L371:
	ldr	x0, [x19, 8]
	cbz	x0, .L364
	ldr	x0, [x19, 16]
	cbnz	x0, .L365
.L364:
	mov	w2, 131
	mov	x1, x22
	mov	x0, x23
	bl	sftl_printk
.L365:
	ldr	w0, [x19, 4]
	add	x2, x29, 104
	add	x1, x29, 108
	bl	l2p_addr_tran.isra.0
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29, 108]
	ldp	x2, x3, [x19, 8]
	ldr	x4, [x0, 3384]
	ldrb	w0, [x29, 104]
	blr	x4
	cbnz	w0, .L366
	str	wzr, [x19]
.L367:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0, 14]
	cmp	w1, 4
	bne	.L369
	ldr	x4, [x0, 3384]
	ldrb	w0, [x29, 104]
	ldp	x2, x3, [x19, 8]
	ldr	w1, [x29, 108]
	add	x3, x3, 8
	add	x2, x2, 2048
	add	w1, w27, w1
	blr	x4
	cbz	w0, .L369
	mov	w0, -1
	str	w0, [x19]
.L369:
	add	x19, x19, 32
	b	.L363
.L366:
	mov	w0, -1
	str	w0, [x19]
	b	.L367
.L376:
	str	wzr, [x21]
	mov	w2, w25
	str	wzr, [x23]
	mov	w1, 1
	stp	x23, x21, [x29, 120]
	ldr	w0, [x20, 4]
	str	w0, [x29, 116]
	add	x0, x29, 112
	bl	FlashReadPages
	ldr	w24, [x29, 112]
	cmn	w24, #1
	bne	.L373
	ldr	w1, [x20, 4]
	adrp	x0, .LC80
	add	x0, x0, :lo12:.LC80
	bl	sftl_printk
	str	w24, [x20]
.L373:
	ldr	x0, [x20, 16]
	cbz	x0, .L374
	ldr	w2, [x0]
	ldr	w3, [x21]
	cmp	w2, w3
	beq	.L374
	ldr	w1, [x20, 4]
	adrp	x0, .LC81
	add	x0, x0, :lo12:.LC81
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x20]
.L374:
	ldr	x0, [x20, 8]
	cbz	x0, .L375
	ldr	w3, [x22, #:lo12:check_buf]
	ldr	w2, [x0]
	cmp	w2, w3
	beq	.L375
	ldr	w1, [x20, 4]
	adrp	x0, .LC82
	add	x0, x0, :lo12:.LC82
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x20]
.L375:
	add	x20, x20, 32
.L391:
	cmp	x20, x19
	bne	.L376
	b	.L390
.L372:
	add	x21, x21, :lo12:.LANCHOR0
	adrp	x22, check_buf
	add	x21, x21, 3448
	add	x23, x22, :lo12:check_buf
	b	.L391
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	stp	x29, x30, [sp, -80]!
	ubfiz	x2, x2, 5, 32
	add	x2, x2, 4
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	x23, [sp, 48]
	stp	x21, x22, [sp, 32]
	add	x20, x0, 4
	add	x22, x0, x2
	mov	w21, -1
	ldrh	w23, [x19, 12]
.L394:
	cmp	x20, x22
	bne	.L400
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L400:
	ldr	w0, [x20]
	add	x2, x29, 72
	add	x1, x29, 76
	bl	l2p_addr_tran.isra.0
	ldrb	w0, [x29, 72]
	ldr	w1, [x29, 76]
	ldr	x2, [x19, 3376]
	blr	x2
	cmp	w0, 0
	csel	w0, w21, wzr, ne
	str	w0, [x20, -4]
	ldrh	w0, [x19, 14]
	cmp	w0, 4
	bne	.L398
	ldrb	w0, [x29, 72]
	ldr	x2, [x19, 3376]
	ldr	w1, [x29, 76]
	add	w1, w23, w1
	blr	x2
	cbz	w0, .L398
	str	w21, [x20, -4]
.L398:
	add	x20, x20, 32
	b	.L394
	.size	FlashEraseBlocks, .-FlashEraseBlocks
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
	bcc	.L405
	adrp	x0, .LANCHOR0
	add	x20, x0, :lo12:.LANCHOR0
	mov	x19, x0
	ldrh	w2, [x20, 262]
	cmp	w2, 1024
	beq	.L405
	and	w1, w1, 65535
	cbz	w1, .L407
	mov	w0, w21
	bl	P2V_block_in_plane
	and	w22, w0, 65535
	ldr	x0, [x20, 3960]
	lsl	w1, w21, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x20, 3960]
	bl	FlashEraseBlocks
	ldr	x2, [x20, 2384]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x20, 2640]
	add	w0, w0, 1
	str	w0, [x20, 2640]
.L407:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 256
	ldrh	w1, [x0, 6]
	add	w1, w1, 1
	strh	w1, [x0, 6]
	ldrh	w1, [x0, 4]
	add	x2, x0, x1, sxtw 1
	add	w1, w1, 1
	and	w1, w1, 1023
	strh	w1, [x0, 4]
	strh	w21, [x2, 8]
.L405:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	stp	x29, x30, [sp, -128]!
	mov	w10, 0
	mov	w9, 4
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x5, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x21, x22, [sp, 32]
	and	w23, w1, 255
	stp	x25, x26, [sp, 64]
	add	x8, x5, 64
	stp	x27, x28, [sp, 80]
	and	w24, w0, 65535
	mov	w21, 0
	mov	w19, 0
	str	w24, [x5, 3968]
.L416:
	ldrh	w0, [x5, 36]
	cmp	w0, w10
	bhi	.L420
	cbz	w21, .L415
	ldr	x0, [x5, 3960]
	mov	w2, w21
	mov	w1, 0
	ubfiz	x21, x21, 5, 16
	mov	x22, 0
	add	x25, x20, :lo12:.LANCHOR0
	bl	FlashEraseBlocks
.L424:
	ldr	x0, [x25, 3960]
	add	x1, x0, x22
	ldr	w0, [x0, x22]
	cmn	w0, #1
	bne	.L423
	ldr	w0, [x1, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.L423:
	add	x22, x22, 32
	cmp	x22, x21
	bne	.L424
	cbz	w23, .L437
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 1
	ldrh	w26, [x0, 108]
	lsr	w0, w26, 2
	str	w0, [x29, 124]
.L425:
	add	x27, x20, :lo12:.LANCHOR0
	mov	w22, 0
	add	x0, x27, 64
	str	x0, [x29, 112]
.L433:
	mov	w5, 0
	mov	w21, 0
.L426:
	ldrh	w0, [x27, 36]
	cmp	w0, w5
	bhi	.L429
	cbz	w21, .L415
	ldr	x0, [x27, 3960]
	mov	w1, w21
	mov	w3, 1
	mov	w2, w25
	mov	x28, 0
	bl	FlashProgPages
	ubfiz	x1, x21, 5, 16
.L432:
	ldr	x0, [x27, 3960]
	add	x3, x0, x28
	ldr	w0, [x0, x28]
	cbz	w0, .L431
	ldr	w0, [x3, 4]
	add	w19, w19, 1
	str	x1, [x29, 104]
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29, 104]
.L431:
	add	x28, x28, 32
	cmp	x1, x28
	bne	.L432
	ldr	w0, [x29, 124]
	add	w22, w22, w0
	and	w22, w22, 65535
	cmp	w26, w22
	bhi	.L433
	add	x26, x20, :lo12:.LANCHOR0
	mov	x22, 0
.L435:
	cbz	w23, .L434
	ldr	x0, [x26, 3960]
	add	x1, x0, x22
	ldr	w0, [x0, x22]
	cbnz	w0, .L434
	ldr	w0, [x1, 4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L434:
	add	x22, x22, 32
	cmp	x22, x28
	bne	.L435
	cmp	w24, 63
	ccmp	w23, 0, 0, hi
	beq	.L415
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	mov	w1, w25
	ldr	x0, [x20, 3960]
	bl	FlashEraseBlocks
.L415:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L420:
	ldr	x1, [x5, 3960]
	ubfiz	x0, x10, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w24
	ldrb	w0, [x8, w10, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w23, .L417
	bl	IsBlkInVendorPart
	cbnz	w0, .L418
.L417:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L419
	ldr	x0, [x5, 3960]
	ubfiz	x2, x21, 5, 16
	lsl	w6, w6, 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	ldr	x1, [x5, 3960]
	ldr	x0, [x5, 3976]
	add	x1, x1, x2
	ldr	x2, [x5, 3984]
	str	x0, [x1, 8]
	ldrh	w0, [x5, 116]
	mul	w0, w0, w21
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w0, w0, w9
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L418:
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L416
.L419:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L418
.L437:
	mov	w0, 6
	mov	w25, 0
	mov	w26, 1
	str	w0, [x29, 124]
	b	.L425
.L429:
	ldr	x1, [x27, 3960]
	ubfiz	x0, x5, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w24
	ldr	x0, [x29, 112]
	ldrb	w0, [x0, w5, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w23, .L427
	bl	IsBlkInVendorPart
	cbnz	w0, .L428
.L427:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L428
	ldr	x0, [x27, 3960]
	ubfiz	x2, x21, 5, 16
	add	w6, w22, w6, lsl 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	ldr	x1, [x27, 3960]
	ldr	x0, [x27, 3992]
	add	x1, x1, x2
	mov	w2, 4
	str	x0, [x1, 8]
	ldrh	w0, [x27, 116]
	mul	w0, w0, w21
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w0, w0, w2
	ldr	x2, [x27, 3976]
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L428:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L426
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	x23, [sp, 48]
	add	x20, x19, 256
	adrp	x23, .LC84
	add	x23, x23, :lo12:.LC84
	stp	x21, x22, [sp, 32]
.L459:
	ldrh	w1, [x20, 6]
	cbz	w1, .L460
	ldrh	w0, [x20, 2]
	sub	w1, w1, #1
	strh	w1, [x20, 6]
	add	x2, x20, x0, sxtw 1
	add	w0, w0, 1
	and	w0, w0, 1023
	strh	w0, [x20, 2]
	ldrh	w21, [x2, 8]
	mov	w0, w21
	bl	P2V_block_in_plane
	and	w22, w0, 65535
	ldr	x0, [x19, 3960]
	lsl	w1, w21, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 3960]
	bl	FlashEraseBlocks
	ldr	x2, [x19, 2384]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	sub	w1, w21, #1
	ldr	w0, [x19, 2640]
	add	w0, w0, 1
	str	w0, [x19, 2640]
	mov	w0, 65533
	cmp	w0, w1, uxth
	bcc	.L461
	mov	w0, w21
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L460:
	adrp	x0, .LC83
	mov	w1, 0
	add	x0, x0, :lo12:.LC83
	bl	sftl_printk
.L463:
	b	.L463
.L461:
	ldrh	w2, [x20, 6]
	mov	w1, w21
	mov	x0, x23
	bl	sftl_printk
	b	.L459
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	mov	w20, 0
	ldrh	w1, [x0, 10]
	ldr	x0, [x0, 16]
.L467:
	cmp	w20, w1
	beq	.L471
	mov	x21, x0
	ldrh	w2, [x0], 2
	cbnz	w2, .L468
	bl	FtlFreeSysBlkQueueOut
	and	w1, w0, 65535
	strh	w0, [x21]
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L469
	adrp	x0, .LANCHOR0+262
	ldrh	w2, [x0, #:lo12:.LANCHOR0+262]
	adrp	x0, .LC85
	add	x0, x0, :lo12:.LC85
	bl	sftl_printk
.L470:
	b	.L470
.L469:
	ldr	w0, [x19, 48]
	strh	wzr, [x19, 2]
	add	w0, w0, 1
	str	w0, [x19, 48]
	ldrh	w0, [x19, 8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19, 8]
.L471:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L473
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 581
	add	x1, x1, 192
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L473:
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L468:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L467
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
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
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	stp	x29, x30, [sp, -48]!
	mov	w0, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x19, 2560
	add	x2, x19, 2560
	strh	wzr, [x19, 3240]
	strh	wzr, [x19, 3324]
	stp	wzr, wzr, [x1, 28]
	stp	wzr, wzr, [x1, 36]
	str	wzr, [x19, 2604]
	str	wzr, [x19, 2608]
	stp	wzr, wzr, [x2, 60]
	stp	wzr, wzr, [x2, 68]
	str	w0, [x19, 4004]
	mov	w0, -1
	strh	w0, [x19, 3320]
	strh	w0, [x19, 3322]
	mov	w0, 32
	str	wzr, [x19, 2640]
	strh	w0, [x19, 3232]
	mov	w0, 128
	str	wzr, [x19, 2644]
	strh	w0, [x19, 3234]
	str	wzr, [x19, 2648]
	str	wzr, [x19, 3236]
	str	wzr, [x19, 3244]
	str	wzr, [x19, 3968]
	str	wzr, [x19, 4000]
	str	wzr, [x19, 4008]
	strh	wzr, [x19, 3362]
	ldrh	w0, [x19, 110]
	lsl	w0, w0, 1
	bl	ftl_malloc
	ldrh	w1, [x19, 110]
	str	x0, [x19, 3296]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19, 36]
	str	x0, [x19, 3312]
	lsl	w22, w21, 5
	lsl	w21, w21, 7
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4016]
	mov	w0, w22
	bl	ftl_malloc
	str	x0, [x19, 4024]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4032]
	mov	w0, w22
	bl	ftl_malloc
	str	x0, [x19, 3960]
	mov	w0, w22
	bl	ftl_malloc
	str	x0, [x19, 3280]
	ldrh	w0, [x19, 36]
	ldrh	w21, [x19, 114]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19, 3288]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3432]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4040]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4048]
	ldr	w0, [x19, 3288]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19, 3264]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3992]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3976]
	ldr	w1, [x19, 3288]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19, 116]
	str	x0, [x19, 3256]
	ldrh	w0, [x19, 36]
	mul	w21, w21, w0
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3440]
	lsl	w0, w21, 2
	bl	ftl_malloc
	str	x0, [x19, 3984]
	ldrh	w1, [x19, 116]
	ldr	w0, [x19, 3288]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3272]
	ldrh	w0, [x19, 46]
	ubfiz	w0, w0, 1, 15
	strh	w0, [x19, 4056]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19, 4064]
	ldrh	w0, [x19, 4056]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19, 4056]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w21, [x19, 46]
	str	x0, [x19, 4072]
	add	x0, x0, 32
	str	x0, [x19, 2384]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4080]
	mov	w0, w21
	bl	ftl_malloc
	ldr	w21, [x19, 132]
	str	x0, [x19, 2376]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4088]
	mov	w0, w21
	adrp	x21, .LANCHOR3
	add	x22, x21, :lo12:.LANCHOR3
	bl	ftl_malloc
	str	x0, [x22, -256]
	ldrh	w0, [x19, 46]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19, 24]
	ldrh	w0, [x19, 124]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19, 152]
	ldrh	w0, [x19, 124]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x22, -248]
	ldrh	w0, [x19, 124]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x22, -240]
	ldrh	w0, [x19, 126]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w2, [x19, 126]
	mov	w1, 0
	str	x0, [x22, -232]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x19, 140]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x22, -224]
	ldr	w0, [x19, 132]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x22, -216]
	ldrh	w0, [x19, 142]
	lsl	w0, w0, 4
	bl	ftl_malloc
	ldrh	w1, [x19, 142]
	str	x0, [x19, 2568]
	ldrh	w0, [x19, 114]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x22, -208]
	ldrh	w1, [x19, 46]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 2360]
	ldrh	w0, [x19, 102]
	ldrh	w1, [x19, 58]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x22, -200]
	mul	w0, w1, w0
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w4, [x22, -200]
	add	x5, x19, 200
	ldrh	w6, [x19, 58]
	mov	x1, 1
	str	x0, [x19, 192]
	mov	w2, w4
	mov	x0, x20
.L478:
	cmp	w1, w6
	bcc	.L479
	mov	w2, 8
	add	x4, x0, :lo12:.LANCHOR0
	sub	w2, w2, w1
	add	x4, x4, 160
	add	x2, x2, 1
	mov	x3, 0
.L480:
	add	x3, x3, 1
	cmp	x3, x2
	bne	.L481
	add	x2, x0, :lo12:.LANCHOR0
	ldr	x1, [x2, 4088]
	cbnz	x1, .L482
.L484:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 224
	adrp	x0, .LC86
	add	x0, x0, :lo12:.LC86
	bl	sftl_printk
	mov	w0, -1
.L477:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L479:
	ldr	x3, [x19, 192]
	add	w1, w1, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x5], 8
	b	.L478
.L481:
	add	x5, x1, x3
	add	x5, x4, x5, lsl 3
	str	xzr, [x5, 24]
	b	.L480
.L482:
	add	x1, x21, :lo12:.LANCHOR3
	ldr	x3, [x1, -256]
	cbz	x3, .L484
	ldr	x3, [x1, -224]
	cbz	x3, .L484
	ldr	x3, [x1, -216]
	cbz	x3, .L484
	ldr	x3, [x2, 2568]
	cbz	x3, .L484
	ldr	x1, [x1, -208]
	cbz	x1, .L484
	ldr	x1, [x2, 2360]
	cbz	x1, .L484
	ldr	x1, [x2, 192]
	cbz	x1, .L484
	ldr	x1, [x2, 2376]
	cbz	x1, .L484
	add	x1, x0, :lo12:.LANCHOR0
	ldr	x2, [x1, 3296]
	cbz	x2, .L484
	ldr	x2, [x1, 3312]
	cbz	x2, .L484
	ldr	x2, [x1, 4016]
	cbz	x2, .L484
	ldr	x2, [x1, 4032]
	cbz	x2, .L484
	ldr	x2, [x1, 3960]
	cbz	x2, .L484
	ldr	x2, [x1, 3280]
	cbz	x2, .L484
	ldr	x2, [x1, 4024]
	cbz	x2, .L484
	ldr	x2, [x1, 3432]
	cbz	x2, .L484
	ldr	x2, [x1, 4040]
	cbz	x2, .L484
	ldr	x1, [x1, 4048]
	cbz	x1, .L484
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3264]
	cbz	x1, .L484
	ldr	x1, [x0, 3992]
	cbz	x1, .L484
	ldr	x1, [x0, 3976]
	cbz	x1, .L484
	ldr	x1, [x0, 3256]
	cbz	x1, .L484
	ldr	x1, [x0, 3440]
	cbz	x1, .L484
	ldr	x1, [x0, 3984]
	cbz	x1, .L484
	ldr	x1, [x0, 3272]
	cbz	x1, .L484
	ldr	x1, [x0, 2384]
	cbz	x1, .L484
	ldr	x1, [x0, 4064]
	cbz	x1, .L484
	ldr	x0, [x0, 152]
	cbz	x0, .L484
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x1, [x0, -248]
	cbz	x1, .L484
	ldr	x1, [x0, -240]
	cbz	x1, .L484
	ldr	x0, [x0, -232]
	cbz	x0, .L484
	mov	w0, 0
	b	.L477
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	adrp	x0, .LANCHOR3-200
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	ldrh	w2, [x0, #:lo12:.LANCHOR3-200]
	add	x20, x20, 240
	str	x23, [sp, 48]
	adrp	x23, .LANCHOR0
	add	x23, x23, :lo12:.LANCHOR0
	mov	x19, 0
	mov	x21, x1
	lsl	w2, w2, 2
	mov	w1, 0
	mov	x0, x21
	bl	ftl_memset
.L582:
	ldrh	w0, [x22, x19]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L579
	ldrh	w1, [x23, 102]
	cmp	w1, w0
	bhi	.L581
	adrp	x0, .LC1
	mov	w2, 74
	mov	x1, x20
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L581:
	ldrh	w2, [x22, x19]
	mov	w1, 1
	add	x19, x19, 2
	cmp	x19, 1024
	ubfx	x0, x2, 5, 11
	lsl	w2, w1, w2
	lsl	x0, x0, 2
	ldr	w1, [x21, x0]
	orr	w1, w1, w2
	str	w1, [x21, x0]
	bne	.L582
.L579:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
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
	add	x0, x0, 172
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
	strh	w0, [x3, 256]
	add	x0, x3, 264
	strh	wzr, [x3, 258]
	strh	wzr, [x3, 260]
	strh	wzr, [x3, 262]
	bl	ftl_memset
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 65535
	and	x19, x1, 65535
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x21, x22, [sp, 32]
	mov	x24, x0
	stp	x25, x26, [sp, 64]
	ldrh	w1, [x1, 140]
	cmp	w1, w20
	bcs	.L591
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 492
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L591:
	adrp	x22, .LANCHOR3
	add	x22, x22, :lo12:.LANCHOR3
	ubfiz	x0, x20, 2, 16
	ldr	x1, [x22, -224]
	ldr	w25, [x1, x0]
	cbnz	w25, .L592
	add	x24, x24, :lo12:.LANCHOR0
	lsl	x19, x19, 4
	mov	w1, 255
	ldr	x0, [x24, 2568]
	ldrh	w2, [x24, 114]
	add	x0, x0, x19
	ldr	x0, [x0, 8]
	bl	ftl_memset
	ldr	x0, [x24, 2568]
	strh	w20, [x0, x19]
	ldr	x0, [x24, 2568]
	add	x19, x0, x19
	str	wzr, [x19, 4]
.L593:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L592:
	add	x23, x24, :lo12:.LANCHOR0
	lsl	x19, x19, 4
	add	x21, x23, 3400
	mov	w2, 1
	mov	w1, w2
	ldr	x0, [x23, 2568]
	str	w25, [x23, 3404]
	add	x0, x0, x19
	ldr	x0, [x0, 8]
	str	x0, [x23, 3408]
	ldr	x0, [x23, 3440]
	str	x0, [x23, 3416]
	mov	x0, x21
	bl	FlashReadPages
	ldr	x26, [x23, 3416]
	ldrh	w0, [x26, 8]
	cmp	w0, w20
	beq	.L594
	mov	w2, w25
	mov	w1, w20
	adrp	x0, .LC87
	add	x0, x0, :lo12:.LC87
	bl	sftl_printk
	ldr	x1, [x23, 3416]
	mov	w3, 4
	adrp	x0, .LC88
	mov	w2, w3
	add	x0, x0, :lo12:.LC88
	bl	rknand_print_hex
	ldrh	w3, [x23, 140]
	adrp	x0, .LC89
	ldr	x1, [x22, -224]
	mov	w2, 4
	add	x0, x0, :lo12:.LC89
	bl	rknand_print_hex
.L594:
	ldrh	w0, [x26, 8]
	cmp	w0, w20
	beq	.L595
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 513
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L595:
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x1, [x0, 2568]
	add	x1, x1, x19
	str	wzr, [x1, 4]
	ldr	x0, [x0, 2568]
	strh	w20, [x0, x19]
	b	.L593
	.size	load_l2p_region, .-load_l2p_region
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
	stp	x25, x26, [sp, 64]
	ldrh	w2, [x0, 10]
	ldp	x21, x20, [x0, 32]
	ldr	x23, [x0, 16]
	lsl	w2, w2, 1
	mov	x0, x21
	bl	ftl_memset
	mov	w0, 0
.L598:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L602
	ldrh	w26, [x21]
	adrp	x24, .LANCHOR0
	add	x24, x24, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w20, 0
.L603:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L607
	mov	w0, w25
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L602:
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20, x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L599:
	ldrh	w3, [x19, 10]
	cmp	w3, w1
	bhi	.L601
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L598
.L601:
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x23, x3]
	cmp	w4, w2
	bne	.L600
	ldrh	w4, [x21, x3]
	add	w4, w4, 1
	strh	w4, [x21, x3]
.L600:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L599
.L607:
	ldrh	w0, [x19]
	uxtw	x22, w20
	cmp	w0, w20
	bne	.L604
	ldrh	w0, [x24, 108]
	ldrh	w1, [x19, 2]
	cmp	w1, w0
	bcs	.L604
	strh	w0, [x21, x22, lsl 1]
.L604:
	lsl	x22, x22, 1
	ldrh	w0, [x21, x22]
	cmp	w26, w0
	bls	.L605
	mov	w25, w20
	mov	w26, w0
.L605:
	cbnz	w0, .L606
	ldrh	w0, [x23, x22]
	cbz	w0, .L606
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x23, x22]
	ldrh	w0, [x19, 8]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
.L606:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L603
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	stp	x29, x30, [sp, -64]!
	mov	w1, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	str	x23, [sp, 48]
	ldr	x20, [x0, 16]
	ldrh	w0, [x0]
	cmp	w0, w1
	bne	.L613
	ldrh	w0, [x19, 8]
	cbz	w0, .L614
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 641
	add	x1, x1, 272
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L614:
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	strh	w0, [x19, 8]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x20]
	strh	wzr, [x19, 2]
	ldr	w0, [x19, 48]
	strh	wzr, [x19]
	add	w0, w0, 1
	str	w0, [x19, 48]
.L615:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L613:
	ubfiz	x0, x0, 1, 16
	adrp	x21, .LANCHOR0
	ldr	w1, [x19, 48]
	ldr	x23, [x19, 40]
	ldrh	w22, [x20, x0]
	add	x20, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2]
	orr	w0, w0, w22, lsl 10
	str	w0, [x20, 3404]
	ldr	x0, [x20, 3432]
	str	x0, [x20, 3408]
	ldr	x0, [x20, 3440]
	str	x0, [x20, 3416]
	str	w1, [x0, 4]
	mov	w1, -1291
	strh	w1, [x0, 8]
	ldrh	w1, [x19, 4]
	strh	w1, [x0]
	strh	w22, [x0, 2]
	mov	w1, 255
	ldrh	w2, [x20, 108]
	ldr	x0, [x20, 3432]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, 0
	mov	w1, 0
.L616:
	ldrh	w2, [x19, 6]
	cmp	w2, w1
	bhi	.L618
	add	x0, x21, :lo12:.LANCHOR0
	mov	w2, 1
	mov	w3, 0
	mov	w1, w2
	add	x0, x0, 3400
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	x0, x19
	bl	ftl_map_blk_gc
	b	.L615
.L618:
	ubfiz	x2, x1, 2, 16
	ldr	w3, [x23, x2]
	cmp	w22, w3, lsr 10
	bne	.L617
	add	w0, w0, 1
	ldr	x4, [x20, 3432]
	and	w0, w0, 65535
	ubfiz	x3, x0, 3, 16
	str	w1, [x4, x3]
	ldr	w4, [x23, x2]
	ldr	x2, [x20, 3432]
	add	x2, x2, x3
	str	w4, [x2, 4]
.L617:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L616
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR0
	add	x24, x23, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	mov	x19, x0
	stp	x25, x26, [sp, 64]
	adrp	x22, .LANCHOR1
	stp	x27, x28, [sp, 80]
	mov	w26, w1
	mov	x27, x2
	mov	x25, x23
	add	x22, x22, :lo12:.LANCHOR1
	add	x20, x24, 3400
	mov	w21, 0
.L624:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	w1, [x0, 2604]
	add	w1, w1, 1
	str	w1, [x0, 2604]
	ldrh	w0, [x0, 108]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L625
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L626
.L625:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L626:
	ldrh	w1, [x19]
	ldr	x0, [x19, 16]
	ldrh	w0, [x0, x1, lsl 1]
	cbnz	w0, .L627
	adrp	x0, .LC1
	mov	w2, 699
	add	x1, x22, 304
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L627:
	ldrh	w1, [x19]
	ldrh	w0, [x19, 10]
	cmp	w1, w0
	bcc	.L628
	adrp	x0, .LC1
	mov	w2, 700
	add	x1, x22, 304
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L628:
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19, 16]
	ldrh	w28, [x0, x1, lsl 1]
	mov	w1, 0
	ldrh	w0, [x19, 2]
	str	x27, [x20, 8]
	orr	w0, w0, w28, lsl 10
	str	w0, [x20, 4]
	ldr	x0, [x24, 3440]
	str	x0, [x20, 16]
	bl	ftl_memset
	ldr	x0, [x20, 16]
	mov	w3, 1
	ldr	w1, [x19, 48]
	mov	w2, w3
	strh	w26, [x0, 8]
	str	w1, [x0, 4]
	ldrh	w1, [x19, 4]
	strh	w1, [x0]
	mov	w1, w3
	strh	w28, [x0, 2]
	mov	x0, x20
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2]
	ldr	w1, [x20]
	cmn	w1, #1
	bne	.L629
	ldr	w1, [x20, 4]
	adrp	x0, .LC90
	add	x0, x0, :lo12:.LC90
	add	w21, w21, 1
	and	w21, w21, 65535
	bl	sftl_printk
	ldrh	w0, [x19, 2]
	cmp	w0, 2
	bhi	.L630
	ldrh	w0, [x24, 108]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L630:
	cmp	w21, 3
	bls	.L631
	add	x25, x25, :lo12:.LANCHOR0
	adrp	x0, .LC91
	mov	w2, w21
	add	x0, x0, :lo12:.LC91
	ldr	w1, [x25, 3404]
	bl	sftl_printk
.L632:
	b	.L632
.L631:
	ldr	w0, [x19, 52]
	cbz	w0, .L624
.L641:
	b	.L641
.L629:
	cmp	w0, 1
	beq	.L624
	ldr	x0, [x19, 40]
	ldr	w1, [x20, 4]
	str	w1, [x0, w26, uxtw 2]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
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
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x20, [x0, 16]
	ldr	x25, [x0, 40]
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 10]
	ldrh	w2, [x19, 8]
	sub	w1, w1, #4
	cmp	w2, w1
	blt	.L644
	ubfiz	x0, x0, 1, 16
	ldrh	w24, [x20, x0]
	cbz	w24, .L644
	ldr	w1, [x19, 52]
	cbnz	w1, .L644
	mov	w1, 1
	str	w1, [x19, 52]
	strh	wzr, [x20, x0]
	ldrh	w0, [x19, 8]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0, 108]
	cmp	w1, w0
	bcc	.L645
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L645:
	add	x26, x22, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR1
	add	x21, x26, 3400
	add	x23, x23, 320
	mov	w20, 0
.L646:
	ldrh	w0, [x19, 6]
	cmp	w0, w20
	bhi	.L650
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19, 52]
.L644:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x22, 108]
	cmp	w1, w0
	bcc	.L651
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L651:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L650:
	ubfiz	x27, x20, 2, 16
	ldr	w0, [x25, x27]
	cmp	w24, w0, lsr 10
	bne	.L647
	ldr	x28, [x26, 3440]
	mov	w2, 1
	ldr	x0, [x26, 4040]
	mov	w1, w2
	stp	x0, x28, [x21, 8]
	ldr	w0, [x25, x27]
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
	ldrh	w0, [x28, 8]
	cmp	w0, w20
	beq	.L648
	adrp	x0, .LC1
	mov	w2, 613
	mov	x1, x23
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L648:
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L649
	str	wzr, [x25, x27]
.L647:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L646
.L649:
	ldr	x2, [x21, 8]
	mov	w1, w20
	mov	x0, x19
	bl	FtlMapWritePage
	b	.L647
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	flush_l2p_region
	.type	flush_l2p_region, %function
flush_l2p_region:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x20, x0, 4, 16
	ldr	x0, [x19, 2568]
	add	x1, x0, x20
	ldr	x2, [x1, 8]
	ldrh	w1, [x0, x20]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	sub	x0, x0, #192
	bl	FtlMapWritePage
	ldr	x0, [x19, 2568]
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
	.global	log2phys
	.type	log2phys, %function
log2phys:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, w2
	adrp	x24, .LANCHOR0
	add	x2, x24, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	x22, x1
	stp	x19, x20, [sp, 16]
	str	x25, [sp, 64]
	ldrh	w19, [x2, 112]
	add	w1, w19, 7
	mov	x19, 1
	lsr	w21, w0, w1
	and	w21, w21, 65535
	lsl	x19, x19, x1
	ldr	w1, [x2, 2584]
	sub	w19, w19, #1
	and	w19, w19, w0
	cmp	w0, w1
	and	x19, x19, 65535
	bcc	.L659
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 808
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L659:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x0, 142]
	ldr	x2, [x0, 2568]
	mov	x0, 0
.L660:
	and	x20, x0, 65535
	cmp	w20, w1
	bcc	.L665
	bl	select_l2p_ram_region
	and	x20, x0, 65535
	add	x2, x24, :lo12:.LANCHOR0
	ubfiz	x1, x20, 4, 16
	mov	w25, w0
	ldr	x2, [x2, 2568]
	add	x3, x2, x1
	ldrh	w2, [x2, x1]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L666
	ldr	w1, [x3, 4]
	tbz	w1, #31, .L666
	bl	flush_l2p_region
.L666:
	mov	w1, w25
	mov	w0, w21
	bl	load_l2p_region
	b	.L661
.L665:
	add	x0, x0, 1
	add	x3, x2, x0, lsl 4
	ldrh	w3, [x3, -16]
	cmp	w3, w21
	bne	.L660
.L661:
	cbnz	w23, .L662
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x0, [x0, 2568]
	add	x0, x0, x20, lsl 4
	ldr	x0, [x0, 8]
	ldr	w0, [x0, x19, lsl 2]
	str	w0, [x22]
.L663:
	add	x24, x24, :lo12:.LANCHOR0
	ldr	x0, [x24, 2568]
	add	x20, x0, x20, lsl 4
	ldr	w0, [x20, 4]
	cmn	w0, #1
	beq	.L668
	add	w0, w0, 1
	str	w0, [x20, 4]
.L668:
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L662:
	add	x2, x24, :lo12:.LANCHOR0
	lsl	x0, x20, 4
	mov	x1, x0
	ldr	w4, [x22]
	ldr	x3, [x2, 2568]
	add	x0, x3, x0
	ldr	x0, [x0, 8]
	str	w4, [x0, x19, lsl 2]
	strh	w21, [x2, 2576]
	ldr	x0, [x2, 2568]
	add	x0, x0, x1
	ldr	w1, [x0, 4]
	orr	w1, w1, -2147483648
	str	w1, [x0, 4]
	b	.L663
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlReUsePrevPpa
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w21, w0
	stp	x19, x20, [sp, 16]
	lsr	w0, w1, 10
	str	w1, [x29, 76]
	adrp	x22, .LANCHOR0
	str	x23, [sp, 48]
	bl	P2V_block_in_plane
	add	x2, x22, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ubfiz	x20, x0, 1, 16
	ldr	x3, [x2, 2376]
	ldrh	w1, [x3, x20]
	cbnz	w1, .L674
	ldr	x19, [x2, 2408]
	cbz	x19, .L675
	ldrh	w3, [x2, 2416]
	mov	x4, -6148914691236517206
	ldr	x2, [x2, 2360]
	movk	x4, 0xaaab, lsl 0
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	mul	x19, x19, x4
	mov	w4, 6
	and	w19, w19, 65535
.L676:
	cmp	w1, w3
	beq	.L675
	cmp	w19, w0
	bne	.L677
	add	x23, x22, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x23, 2408
	bl	List_remove_node
	ldrh	w0, [x23, 2416]
	cbnz	w0, .L678
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1688
	add	x1, x1, 352
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L678:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w0, [x22, 2416]
	sub	w0, w0, #1
	strh	w0, [x22, 2416]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x1, [x22, 2376]
	ldrh	w0, [x1, x20]
	add	w0, w0, 1
	strh	w0, [x1, x20]
.L675:
	add	x1, x29, 76
	mov	w2, 1
	mov	w0, w21
	bl	log2phys
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L677:
	umull	x19, w19, w4
	ldrh	w19, [x2, x19]
	cmp	w19, w5
	beq	.L675
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L676
.L674:
	add	w1, w1, 1
	strh	w1, [x3, x20]
	b	.L675
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	sub	sp, sp, #96
	adrp	x0, .LC92
	mov	w1, 0
	add	x0, x0, :lo12:.LC92
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x21, x22, [sp, 48]
	adrp	x22, .LANCHOR0
	add	x21, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 32]
	str	x23, [sp, 64]
	mov	w20, 0
	add	x19, x21, 3400
	bl	sftl_printk
.L685:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0, 2584]
	cmp	w20, w0
	bcc	.L691
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x29, x30, [sp, 16]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	ret
.L691:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w20
	bl	log2phys
	tst	x20, 2047
	bne	.L686
	ldr	w2, [x29, 76]
	adrp	x0, .LC93
	mov	w1, w20
	add	x0, x0, :lo12:.LC93
	bl	sftl_printk
.L686:
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L688
	ldr	x23, [x21, 3440]
	mov	w2, 0
	stp	wzr, w0, [x19]
	mov	w1, 1
	ldr	x0, [x21, 3432]
	str	x0, [x19, 8]
	mov	x0, x19
	str	x23, [x19, 16]
	str	w20, [x19, 24]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmp	w0, 256
	ccmn	w0, #1, 4, ne
	beq	.L689
	ldr	w0, [x23, 8]
	cmp	w20, w0
	beq	.L688
.L689:
	ldp	x1, x0, [x19, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC94
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC94
	ldr	w2, [x19, 4]
	mov	w1, w20
	bl	sftl_printk
.L688:
	add	w20, w20, 1
	b	.L685
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x2, x1, 2720
	ldr	w3, [x2, 80]
	cbz	w3, .L694
	ldr	w0, [x1, 2584]
	ldr	w3, [x2, 84]
	cmp	w3, w0
	bcs	.L695
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x1
	mov	w20, 2048
	str	x21, [sp, 32]
.L700:
	ldr	w1, [x19, 2584]
	ldr	w0, [x19, 2804]
	cmp	w0, w1
	bcs	.L697
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x19, 2804]
	ldr	w1, [x29, 60]
	add	w0, w0, 1
	str	w0, [x19, 2804]
	cmn	w1, #1
	beq	.L698
	str	w0, [x29, 88]
	adrp	x0, ftl_temp_buf
	add	x0, x0, :lo12:ftl_temp_buf
	str	x0, [x29, 72]
	add	x0, x29, 96
	str	x0, [x29, 80]
	add	x0, x29, 160
	str	w1, [x29, 68]
	mov	w2, 0
	mov	w1, 1
	str	wzr, [x0, -96]!
	bl	FlashReadPages
	ldr	w0, [x29, 64]
	cmp	w0, 256
	bne	.L697
	ldr	w0, [x29, 60]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L697:
	ldp	x19, x20, [sp, 16]
	mov	w0, -1
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 160
	ret
.L698:
	subs	w20, w20, #1
	bne	.L700
	b	.L697
.L695:
	ldr	w0, [x1, 2588]
	stp	w0, wzr, [x2, 76]
	str	wzr, [x2, 84]
.L704:
	mov	w0, 0
	ret
.L694:
	ldr	w3, [x1, 2588]
	ldr	w5, [x2, 76]
	add	w2, w3, 1048576
	cmp	w5, w2
	bhi	.L703
	ldr	w2, [x1, 2644]
	lsr	w4, w2, 10
	mov	w2, 33554432
	asr	w2, w2, w4
	add	w2, w2, w5
	cmp	w3, w2
	bhi	.L703
	ldrb	w1, [x1, 2340]
	cbnz	w1, .L704
.L703:
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 1
	str	w3, [x0, 2796]
	str	w1, [x0, 2800]
	str	wzr, [x0, 2804]
	b	.L704
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlMapBlkWriteDump_data
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	ldr	w1, [x0, 56]
	cbz	w1, .L718
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x2, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	add	x0, x2, 3400
	mov	x21, x1
	ldrh	w20, [x19, 6]
	ldr	x4, [x2, 3440]
	sub	w20, w20, #1
	ldr	x3, [x2, 4040]
	and	w20, w20, 65535
	ldr	x5, [x19, 40]
	stp	x3, x4, [x0, 8]
	ubfiz	x4, x20, 2, 16
	str	wzr, [x19, 56]
	ldr	w4, [x5, x4]
	str	w4, [x0, 4]
	cbz	w4, .L712
	mov	w2, 1
	mov	w1, w2
	bl	FlashReadPages
.L713:
	add	x1, x21, :lo12:.LANCHOR0
	mov	x0, x19
	ldr	x2, [x1, 3408]
	mov	w1, w20
	bl	FtlMapWritePage
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L712:
	ldrh	w2, [x2, 114]
	mov	w1, 255
	mov	x0, x3
	bl	ftl_memset
	b	.L713
.L718:
	ret
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	stp	x29, x30, [sp, -112]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR3
	stp	x21, x22, [sp, 32]
	add	x21, x20, :lo12:.LANCHOR3
	stp	x23, x24, [sp, 48]
	adrp	x22, .LANCHOR0
	stp	x25, x26, [sp, 64]
	add	x19, x22, :lo12:.LANCHOR0
	stp	x27, x28, [sp, 80]
	adrp	x25, .LANCHOR1
	strh	wzr, [x21, -128]
	add	x25, x25, :lo12:.LANCHOR1
	ldr	x0, [x21, -216]
	add	x25, x25, 368
	ldr	w2, [x19, 132]
	strh	wzr, [x19, 148]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x19, 4088]
	mov	w1, 0
	ldr	w2, [x19, 132]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x21, -240]
	mov	w1, 0
	ldrh	w2, [x19, 124]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x19, 152]
	mov	w1, 0
	ldrh	w2, [x19, 124]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w2, 12
	mov	w1, 255
	add	x0, x19, 2656
	bl	ftl_memset
	ldrh	w24, [x19, 44]
.L722:
	ldrh	w0, [x19, 46]
	cmp	w0, w24
	bls	.L762
	ldrh	w9, [x19, 36]
	mov	x5, 0
	ldrh	w8, [x19, 116]
	mov	w21, 0
	add	x7, x19, 64
	mov	w6, 4
	b	.L763
.L724:
	ldrb	w0, [x7, x5]
	mov	w1, w24
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L723
	ldr	x0, [x19, 4016]
	ubfiz	x2, x21, 5, 16
	lsl	w4, w4, 10
	add	x0, x0, x2
	str	w4, [x0, 4]
	ldr	x1, [x19, 4016]
	ldr	x0, [x19, 3264]
	add	x1, x1, x2
	ldr	x2, [x19, 3272]
	str	x0, [x1, 8]
	mul	w0, w21, w8
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w0, w0, w6
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L723:
	add	x5, x5, 1
.L763:
	cmp	w9, w5, uxth
	bhi	.L724
	cbnz	w21, .L725
.L761:
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L722
.L725:
	ldr	x0, [x19, 4016]
	mov	w1, w21
	mov	w2, 1
	ubfiz	x21, x21, 5, 16
	mov	x23, 0
	add	x28, x19, 2656
	bl	FlashReadPages
.L760:
	ldr	x0, [x19, 4016]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	ldr	w26, [x1, 4]
	cmn	w0, #1
	ldr	x27, [x1, 16]
	ubfx	x26, x26, 10, 16
	bne	.L728
	mov	w3, 16
	mov	w4, 65535
.L730:
	ldr	x0, [x19, 4016]
	mov	w2, 1
	stp	w4, w3, [x29, 104]
	add	x0, x0, x23
	ldr	w1, [x0, 4]
	add	w1, w1, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 4016]
	add	x0, x0, x23
	bl	FlashReadPages
	ldp	w4, w3, [x29, 104]
	ldrh	w0, [x27]
	cmp	w0, w4
	ldr	x0, [x19, 4016]
	bne	.L727
	mov	w1, -1
	str	w1, [x0, x23]
	ldr	x0, [x19, 4016]
	ldr	w0, [x0, x23]
	cmp	w0, w1
	bne	.L728
.L729:
	mov	w1, 0
	mov	w0, w26
	bl	FtlFreeSysBlkQueueIn
	b	.L733
.L727:
	ldr	w0, [x0, x23]
	cmn	w0, #1
	bne	.L728
	sub	w3, w3, #1
	ands	w3, w3, 65535
	bne	.L730
	b	.L729
.L728:
	ldr	w1, [x19, 2620]
	ldr	w0, [x27, 4]
	cmn	w1, #1
	beq	.L731
	cmp	w1, w0
	bhi	.L732
.L731:
	cmn	w0, #1
	beq	.L732
	add	w1, w0, 1
	str	w1, [x19, 2620]
.L732:
	ldrh	w1, [x27]
	mov	w2, 61604
	cmp	w1, w2
	beq	.L734
	bhi	.L735
	mov	w0, 61574
	cmp	w1, w0
	beq	.L736
.L733:
	add	x23, x23, 32
	cmp	x21, x23
	bne	.L760
	b	.L761
.L735:
	mov	w0, 61634
	cmp	w1, w0
	beq	.L737
	mov	w0, 65535
	cmp	w1, w0
	beq	.L729
	b	.L733
.L737:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w1, [x0, -128]
	ldr	w0, [x19, 132]
	cmp	w1, w0
	bls	.L739
	adrp	x0, .LC1
	mov	w2, 1211
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L739:
	add	x4, x20, :lo12:.LANCHOR3
	ldr	w3, [x19, 132]
	and	w2, w3, 65535
	ldrh	w1, [x4, -128]
	sub	w0, w2, #1
	ldr	x4, [x4, -216]
	sxth	x0, w0
	sub	w2, w2, w1
	sub	w2, w2, #1
	sxth	w2, w2
.L740:
	cmp	w0, w2
	bgt	.L746
	tbz	w0, #31, .L780
	b	.L733
.L746:
	sxtw	x5, w0
	ldr	w8, [x27, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L741
	ldr	w2, [x4]
	cbnz	w2, .L742
	cmp	w3, w1
	beq	.L742
	add	x2, x20, :lo12:.LANCHOR3
	add	w1, w1, 1
	strh	w1, [x2, -128]
.L742:
	add	x7, x20, :lo12:.LANCHOR3
	mov	w1, 0
.L743:
	cmp	w1, w0
	bne	.L744
	add	x1, x20, :lo12:.LANCHOR3
	ldr	w3, [x27, 4]
	ldr	x2, [x1, -216]
	str	w3, [x2, x6]
	ldr	x2, [x19, 4088]
	strh	w26, [x2, x5, lsl 1]
	tbnz	w0, #31, .L733
	ldrh	w1, [x1, -128]
	ldr	w2, [x19, 132]
	sub	w2, w2, w1
	sub	w2, w2, #1
	cmp	w0, w2, sxth
	bgt	.L733
.L780:
	add	x2, x20, :lo12:.LANCHOR3
	add	w1, w1, 1
	ldr	w3, [x27, 4]
	strh	w1, [x2, -128]
	ldr	x1, [x2, -216]
	str	w3, [x1, x0, lsl 2]
	ldr	x1, [x19, 4088]
.L798:
	strh	w26, [x1, x0, lsl 1]
	b	.L733
.L744:
	ldr	x4, [x7, -216]
	sxtw	x2, w1
	lsl	x3, x2, 2
	lsl	x2, x2, 1
	add	x8, x4, x3
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w8, [x8, 4]
	str	w8, [x4, x3]
	ldr	x3, [x19, 4088]
	add	x4, x3, x2
	ldrh	w4, [x4, 2]
	strh	w4, [x3, x2]
	b	.L743
.L741:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L740
.L736:
	ldrh	w1, [x19, 148]
	ldrh	w0, [x19, 124]
	cmp	w1, w0
	bls	.L749
	adrp	x0, .LC1
	mov	w2, 1252
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L749:
	add	x4, x20, :lo12:.LANCHOR3
	ldrh	w3, [x19, 124]
	ldrh	w1, [x19, 148]
	sub	w2, w3, #1
	ldr	x4, [x4, -240]
	sxth	x0, w2
	sub	w2, w2, w1
.L750:
	cmp	w0, w2
	ble	.L755
	sxtw	x5, w0
	ldr	w8, [x27, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L751
	ldr	w2, [x4]
	cbnz	w2, .L752
	cmp	w3, w1
	beq	.L752
	add	w1, w1, 1
	strh	w1, [x19, 148]
.L752:
	add	x7, x20, :lo12:.LANCHOR3
	mov	w1, 0
.L753:
	cmp	w1, w0
	bne	.L754
	add	x1, x20, :lo12:.LANCHOR3
	ldr	w2, [x27, 4]
	ldr	x1, [x1, -240]
	str	w2, [x1, x6]
	ldr	x1, [x19, 152]
	strh	w26, [x1, x5, lsl 1]
.L755:
	tbnz	w0, #31, .L733
	ldrh	w1, [x19, 124]
	ldrh	w2, [x19, 148]
	sub	w1, w1, #1
	sub	w1, w1, w2
	cmp	w0, w1, sxth
	bgt	.L733
	add	x1, x20, :lo12:.LANCHOR3
	add	w2, w2, 1
	strh	w2, [x19, 148]
	ldr	w2, [x27, 4]
	ldr	x1, [x1, -240]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 152]
	b	.L798
.L754:
	ldr	x4, [x7, -240]
	sxtw	x2, w1
	lsl	x3, x2, 2
	lsl	x2, x2, 1
	add	x8, x4, x3
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w8, [x8, 4]
	str	w8, [x4, x3]
	ldr	x3, [x19, 152]
	add	x4, x3, x2
	ldrh	w4, [x4, 2]
	strh	w4, [x3, x2]
	b	.L753
.L751:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L750
.L734:
	ldrh	w2, [x28]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L757
	strh	w26, [x28]
.L799:
	str	w0, [x28, 8]
	b	.L733
.L757:
	ldrh	w0, [x28, 4]
	cmp	w0, w1
	beq	.L758
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L758:
	ldr	w0, [x27, 4]
	ldr	w1, [x28, 8]
	cmp	w1, w0
	bcs	.L759
	ldrh	w0, [x28]
	strh	w0, [x28, 4]
	strh	w26, [x28]
	ldr	w0, [x27, 4]
	b	.L799
.L759:
	strh	w26, [x28, 4]
	b	.L733
.L762:
	ldr	x1, [x19, 4088]
	ldrh	w0, [x1]
	cbz	w0, .L764
.L767:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x1, [x0, 152]
	ldrh	w2, [x1]
	cbz	w2, .L765
.L766:
	add	x20, x20, :lo12:.LANCHOR3
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x20, -128]
	ldr	w0, [x22, 132]
	cmp	w1, w0
	bls	.L796
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1377
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L796:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L764:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0, -128]
	cbz	w0, .L767
	ldr	w2, [x19, 132]
	mov	w0, 0
.L768:
	cmp	w0, w2
	bcs	.L767
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L769
	mov	w1, w0
	add	x3, x22, :lo12:.LANCHOR0
	add	x7, x20, :lo12:.LANCHOR3
.L770:
	ldr	w2, [x3, 132]
	cmp	w1, w2
	bcs	.L767
	ldr	x5, [x3, 4088]
	sxtw	x6, w1
	lsl	x4, x6, 1
	sub	w2, w1, w0
	sxtw	x2, w2
	add	w1, w1, 1
	sxth	w1, w1
	ldrh	w8, [x5, x4]
	strh	w8, [x5, x2, lsl 1]
	ldr	x5, [x7, -216]
	ldr	w6, [x5, x6, lsl 2]
	str	w6, [x5, x2, lsl 2]
	ldr	x2, [x3, 4088]
	strh	wzr, [x2, x4]
	b	.L770
.L769:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L768
.L765:
	ldrh	w2, [x0, 148]
	cbz	w2, .L766
	ldrh	w2, [x0, 124]
	mov	w0, 0
.L775:
	mov	w6, w0
	cmp	w0, w2
	bge	.L766
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L776
	add	x2, x22, :lo12:.LANCHOR0
	add	x7, x20, :lo12:.LANCHOR3
.L777:
	ldrh	w1, [x2, 124]
	cmp	w0, w1
	bge	.L766
	ldr	x4, [x2, 152]
	sxtw	x5, w0
	lsl	x3, x5, 1
	sub	w1, w0, w6
	sxtw	x1, w1
	add	w0, w0, 1
	sxth	w0, w0
	ldrh	w8, [x4, x3]
	strh	w8, [x4, x1, lsl 1]
	ldr	x4, [x7, -240]
	ldr	w5, [x4, x5, lsl 2]
	str	w5, [x4, x1, lsl 2]
	ldr	x1, [x2, 152]
	strh	wzr, [x1, x3]
	b	.L777
.L776:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L775
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	mov	w27, 0
	stp	x23, x24, [sp, 48]
	ldrh	w24, [x0, 6]
	ldr	x0, [x0, 40]
	str	x0, [x29, 120]
	ldrh	w0, [x19, 8]
	lsl	w2, w24, 2
	str	w0, [x29, 116]
	ldr	x0, [x29, 120]
	ldp	x23, x26, [x19, 16]
	bl	ftl_memset
	stp	wzr, wzr, [x19, 48]
	add	x3, x21, :lo12:.LANCHOR0
	add	x20, x3, 3400
	mov	x25, x3
	ldr	x0, [x3, 3432]
	ldr	x22, [x3, 3440]
	stp	x0, x22, [x20, 8]
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x19, 2]
	mov	w0, 1
	str	w0, [x19, 56]
	ldr	w0, [x29, 116]
	sub	w0, w0, #1
	str	w0, [x29, 112]
.L801:
	ldr	w0, [x29, 116]
	cmp	w27, w0
	bge	.L818
	ldr	w0, [x29, 112]
	sxtw	x28, w27
	cmp	w27, w0
	bne	.L802
	lsl	x0, x28, 1
	mov	w1, 1
	add	x20, x23, x0
	add	x25, x21, :lo12:.LANCHOR0
	add	x25, x25, 3400
	ldrh	w0, [x23, x0]
	mov	w23, 0
	bl	FtlGetLastWrittenPage
	strh	w27, [x19]
	sxth	w1, w0
	add	w0, w0, 1
	strh	w0, [x19, 2]
	ldr	w0, [x26, x28, lsl 2]
	add	w26, w1, 1
	str	w0, [x19, 48]
.L803:
	cmp	w23, w26
	blt	.L805
.L818:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x0, x19
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 2]
	ldrh	w0, [x21, 108]
	cmp	w1, w0
	bne	.L807
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L807:
	mov	x0, x19
	bl	ftl_map_blk_gc
	mov	x0, x19
	bl	ftl_map_blk_gc
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L805:
	ldrh	w0, [x20]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w23, w0, lsl 10
	str	w0, [x25, 4]
	mov	x0, x25
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L804
	ldrh	w0, [x22, 8]
	cmp	w24, w0
	bls	.L804
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L804
	ldr	x2, [x29, 120]
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x25, 4]
	str	w1, [x2, x0]
.L804:
	add	w23, w23, 1
	sxth	w23, w23
	b	.L803
.L802:
	ldr	x0, [x25, 3432]
	mov	w2, 1
	str	x0, [x20, 8]
	lsl	x0, x28, 1
	ldrh	w1, [x25, 108]
	add	x28, x23, x0
	ldrh	w0, [x23, x0]
	sub	w1, w1, #1
	orr	w0, w1, w0, lsl 10
	mov	w1, w2
	str	w0, [x20, 4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L820
	ldrh	w1, [x22]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L820
	ldrh	w1, [x22, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L809
.L820:
	mov	w5, 0
.L810:
	ldrh	w0, [x25, 108]
	cmp	w5, w0
	bge	.L816
	ldrh	w0, [x28]
	mov	w2, 1
	str	w5, [x29, 108]
	mov	w1, w2
	orr	w0, w5, w0, lsl 10
	str	w0, [x20, 4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	ldr	w5, [x29, 108]
	cmn	w0, #1
	beq	.L814
	ldrh	w0, [x22, 8]
	cmp	w24, w0
	bls	.L814
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L814
	ldr	x2, [x29, 120]
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x20, 4]
	str	w1, [x2, x0]
.L814:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L810
.L809:
	mov	w0, 0
.L811:
	ldrh	w1, [x25, 108]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L813
.L816:
	add	w4, w27, 1
	sxth	w27, w4
	b	.L801
.L813:
	ldr	x2, [x25, 3432]
	sbfiz	x5, x0, 3, 32
	ldrh	w1, [x2, x5]
	cmp	w24, w1
	bls	.L812
	add	x2, x2, x5
	ldr	x3, [x29, 120]
	ubfiz	x1, x1, 2, 16
	ldr	w2, [x2, 4]
	str	w2, [x3, x1]
.L812:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L811
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x29, sp, 0
	ldrh	w2, [x1, 124]
	sub	x0, x0, #120
	strh	w2, [x0, 10]
	mov	w2, -3962
	strh	w2, [x0, 4]
	ldrh	w2, [x1, 148]
	strh	w2, [x0, 8]
	ldrh	w2, [x1, 126]
	ldr	x1, [x1, 152]
	str	x1, [x0, 16]
	ldr	x1, [x0, -120]
	str	x1, [x0, 24]
	ldr	x1, [x0, -128]
	str	x1, [x0, 32]
	ldr	x1, [x0, -112]
	strh	w2, [x0, 6]
	str	x1, [x0, 40]
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR3
	str	x21, [sp, 32]
	add	x21, x19, :lo12:.LANCHOR3
	add	x20, x1, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x21, -256]
	ldr	w2, [x20, 132]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x20, 142]
	mov	w1, 255
	ldrh	w2, [x20, 114]
	mul	w2, w2, w0
	ldr	x0, [x21, -208]
	bl	ftl_memset
	mov	x1, x20
	mov	w0, 0
	mov	w5, -1
.L831:
	ldrh	w2, [x1, 142]
	cmp	w2, w0
	bhi	.L832
	add	x0, x19, :lo12:.LANCHOR3
	mov	w2, -1
	strh	w2, [x0, -190]
	strh	w2, [x0, -192]
	ldr	w2, [x1, 132]
	strh	w2, [x0, -182]
	mov	w2, -3902
	strh	w2, [x0, -188]
	ldrh	w2, [x0, -128]
	strh	w2, [x0, -184]
	ldrh	w2, [x1, 140]
	ldr	x1, [x1, 4088]
	str	x1, [x0, -176]
	ldr	x1, [x0, -216]
	str	x1, [x0, -168]
	ldr	x1, [x0, -256]
	str	x1, [x0, -160]
	ldr	x1, [x0, -224]
	strh	w2, [x0, -186]
	str	x1, [x0, -152]
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L832:
	ldr	x3, [x1, 2568]
	ubfiz	x2, x0, 4, 16
	add	x3, x3, x2
	str	wzr, [x3, 4]
	ldr	x3, [x1, 2568]
	strh	w5, [x3, x2]
	ldr	x3, [x1, 2568]
	ldr	x4, [x21, -208]
	add	x3, x3, x2
	ldrh	w2, [x1, 114]
	mul	w2, w2, w0
	add	w0, w0, 1
	and	w0, w0, 65535
	sxtw	x2, w2
	and	x2, x2, -4
	add	x2, x4, x2
	str	x2, [x3, 8]
	b	.L831
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlLoadMapInfo
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlL2PDataInit
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	sub	x0, x0, #192
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	mov	w1, -1
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	strh	w1, [x0, -48]
	str	xzr, [x0, -56]
	mov	w1, -1
	stp	wzr, wzr, [x0, -44]
	str	w1, [x0, -36]
	mov	w1, 0
	ldr	x0, [x19, 152]
	ldrh	w2, [x19, 124]
	strh	wzr, [x19, 148]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 2384]
	mov	w1, 0
	ldrh	w2, [x19, 46]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 4064]
	mov	w1, 0
	ldrh	w2, [x19, 46]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w2, 48
	mov	w1, 0
	add	x0, x19, 2312
	bl	ftl_memset
	mov	w2, 512
	mov	w1, 0
	add	x0, x19, 2720
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	ldr	x19, [sp, 16]
	mov	w0, 0
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
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	x23, [sp, 48]
	add	x23, x19, 64
	stp	x21, x22, [sp, 32]
	mov	w20, 0
	mov	w22, 0
	ldrh	w2, [x19, 46]
	mov	w21, 0
	mul	w2, w2, w0
	ldr	x0, [x19, 2360]
	bl	ftl_memset
	strh	wzr, [x19, 2400]
	strh	wzr, [x19, 2416]
	str	xzr, [x19, 2368]
	str	xzr, [x19, 2392]
	str	xzr, [x19, 2408]
.L839:
	ldrh	w0, [x19, 44]
	cmp	w20, w0
	bcs	.L845
	ldrh	w8, [x19, 36]
	mov	x6, 0
	ldrh	w7, [x19, 106]
	mov	w5, 0
	b	.L846
.L841:
	ldrb	w0, [x23, x6]
	mov	w1, w20
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L840
	add	w5, w5, w7
	and	w5, w5, 65535
.L840:
	add	x6, x6, 1
.L846:
	cmp	w8, w6, uxth
	bhi	.L841
	cbz	w5, .L848
	mov	w0, 32768
	sdiv	w5, w0, w5
.L842:
	ldr	x1, [x19, 2360]
	mov	w0, 6
	umaddl	x0, w20, w0, x1
	strh	w5, [x0, 4]
	ldrh	w0, [x19, 2424]
	cmp	w0, w20
	beq	.L843
	ldrh	w0, [x19, 2472]
	cmp	w0, w20
	beq	.L843
	ldrh	w0, [x19, 2520]
	cmp	w0, w20
	beq	.L843
	ldr	x1, [x19, 2376]
	ubfiz	x0, x20, 1, 16
	ldrh	w0, [x1, x0]
	cbnz	w0, .L844
	add	w22, w22, 1
	mov	w0, w20
	and	w22, w22, 65535
	bl	INSERT_FREE_LIST
.L843:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L839
.L848:
	mov	w5, 0
	b	.L842
.L844:
	add	w21, w21, 1
	mov	w0, w20
	and	w21, w21, 65535
	bl	INSERT_DATA_LIST
	b	.L843
.L845:
	strh	w21, [x19, 2400]
	add	w21, w21, w22
	strh	w22, [x19, 2416]
	cmp	w21, w0
	ble	.L847
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2154
	add	x1, x1, 384
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L847:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	ftl_check_vpc
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	stp	x29, x30, [sp, -128]!
	adrp	x0, .LC95
	add	x0, x0, :lo12:.LC95
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x24, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	w23, 0
	adrp	x21, .LANCHOR1
	add	x1, x21, :lo12:.LANCHOR1
	add	x1, x1, 408
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	adrp	x22, check_vpc_table
	bl	sftl_printk
	add	x19, x22, :lo12:check_vpc_table
	mov	w2, 8192
	mov	w1, 0
	mov	x0, x19
	bl	ftl_memset
.L851:
	ldr	w0, [x24, 2584]
	cmp	w23, w0
	bcc	.L853
	adrp	x25, .LC96
	add	x23, x20, :lo12:.LANCHOR0
	add	x26, x22, :lo12:check_vpc_table
	add	x25, x25, :lo12:.LC96
	mov	w24, 0
	mov	w19, 0
	mov	w4, 65535
.L854:
	ldrh	w0, [x23, 44]
	cmp	w0, w19
	bhi	.L856
	ldr	x19, [x23, 2408]
	cbz	x19, .L857
	ldr	x0, [x23, 2360]
	adrp	x25, .LC97
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:check_vpc_table
	sub	x19, x19, x0
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	movk	x0, 0xaaab, lsl 0
	add	x25, x25, :lo12:.LC97
	ldrh	w26, [x23, 2416]
	mov	w27, 6
	mov	w23, 0
	mul	x19, x19, x0
	and	w19, w19, 65535
.L858:
	cmp	w23, w26
	bne	.L860
.L857:
	cbz	w24, .L850
	add	x1, x21, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2321
	add	x1, x1, 408
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L850:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L853:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w23
	bl	log2phys
	ldr	w0, [x29, 124]
	cmn	w0, #1
	beq	.L852
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	and	x0, x0, 65535
	ldrh	w1, [x19, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x19, x0, lsl 1]
.L852:
	add	w23, w23, 1
	b	.L851
.L856:
	ldr	x0, [x23, 2376]
	ubfiz	x28, x19, 1, 16
	sxtw	x27, w19
	ldrh	w2, [x0, x28]
	ldrh	w3, [x26, x27, lsl 1]
	cmp	w2, w3
	beq	.L855
	mov	x0, x25
	str	w4, [x29, 108]
	mov	w1, w19
	bl	sftl_printk
	ldr	x0, [x23, 2376]
	ldr	w4, [x29, 108]
	ldrh	w0, [x0, x28]
	cmp	w0, w4
	beq	.L855
	ldrh	w1, [x26, x27, lsl 1]
	cmp	w1, w0
	csinc	w24, w24, wzr, ls
.L855:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L854
.L860:
	ldr	x1, [x20, 2376]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L859
	ldrh	w3, [x22, w19, sxtw 1]
	mov	w24, 1
	mov	w1, w19
	mov	x0, x25
	bl	sftl_printk
.L859:
	ldr	x0, [x20, 2360]
	umull	x19, w19, w27
	ldrh	w19, [x0, x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L857
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L858
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	stp	x29, x30, [sp, -32]!
	mov	w1, 255
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19, 3296]
	ldrh	w2, [x19, 110]
	strh	wzr, [x19, 3292]
	strh	wzr, [x19, 3304]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x19, 110]
	mov	w0, 12
	mov	w1, 255
	mul	w2, w2, w0
	ldr	x0, [x19, 3312]
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x21, x0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR2
	add	x0, x23, :lo12:.LANCHOR2
	stp	x25, x26, [sp, 64]
	stp	x19, x20, [sp, 16]
	mov	w25, w1
	ldrh	w19, [x0, 4]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L894
	cbnz	w19, .L878
.L879:
	bl	FtlGcPageVarInit
	b	.L880
.L894:
	mov	w19, 0
.L878:
	adrp	x0, .LANCHOR0+106
	ldrh	w0, [x0, #:lo12:.LANCHOR0+106]
	cmp	w0, w25
	beq	.L879
.L880:
	adrp	x26, .LANCHOR0
	add	x22, x26, :lo12:.LANCHOR0
	mov	w24, 0
.L888:
	ldrh	w1, [x21]
	mov	w0, 65535
	strb	wzr, [x21, 8]
	cmp	w1, w0
	beq	.L895
.L891:
	add	x0, x26, :lo12:.LANCHOR0
	add	x2, x21, 16
	mov	w20, 0
	mov	w9, 65535
	mov	w5, 4
	ldrh	w7, [x0, 114]
	ldrh	w8, [x0, 116]
	ldrh	w0, [x0, 36]
	add	x0, x0, 8
	add	x0, x21, x0, lsl 1
.L882:
	cmp	x0, x2
	bne	.L884
	ldr	x0, [x22, 4016]
	mov	w1, w20
	mov	w2, 0
	ubfiz	x20, x20, 5, 16
	bl	FlashReadPages
	mov	x10, 0
	mov	w11, 65535
.L885:
	cmp	x10, x20
	bne	.L889
	add	w19, w19, 1
	add	w24, w24, 1
	and	w19, w19, 65535
	cmp	w25, w24
	bls	.L890
.L892:
	ldrh	w0, [x22, 106]
	cmp	w0, w19
	bhi	.L891
.L895:
	mov	w2, 0
	b	.L881
.L884:
	ldrh	w1, [x2]
	cmp	w1, w9
	beq	.L883
	ldr	x4, [x22, 4016]
	ubfiz	x6, x20, 5, 16
	orr	w1, w19, w1, lsl 10
	add	x4, x4, x6
	str	w1, [x4, 4]
	mul	w1, w20, w7
	ldr	x3, [x22, 4016]
	ldr	x4, [x22, 3264]
	sdiv	w1, w1, w5
	add	x3, x3, x6
	add	x1, x4, x1, sxtw 2
	str	x1, [x3, 8]
	mul	w1, w20, w8
	ldr	x4, [x22, 3272]
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w1, w1, w5
	add	x1, x4, x1, sxtw 2
	str	x1, [x3, 16]
.L883:
	add	x2, x2, 2
	b	.L882
.L889:
	ldr	x6, [x22, 4016]
	add	x5, x6, x10
	ldr	w4, [x5, 4]
	lsr	w0, w4, 10
	bl	P2V_plane
	and	w2, w0, 65535
	ldr	w0, [x6, x10]
	cbnz	w0, .L886
	ldr	x0, [x5, 16]
	add	x10, x10, 32
	ldrh	w1, [x0]
	cmp	w1, w11
	bne	.L887
	adrp	x0, .LANCHOR3-40
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR3-40]
.L881:
	add	x23, x23, :lo12:.LANCHOR2
	strh	w19, [x21, 2]
	strb	w2, [x21, 6]
	mov	w0, -1
	mov	w1, w19
	strh	w0, [x23, 4]
	mov	x0, x21
	bl	ftl_sb_update_avl_pages
	b	.L893
.L887:
	ldp	w2, w0, [x0, 8]
	mov	w1, w4
	bl	FtlGcUpdatePage
	b	.L885
.L886:
	ldr	x0, [x22, 2376]
	mov	w19, 0
	ldrh	w1, [x21]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x21]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x21]
	bl	FtlGcPageVarInit
	b	.L888
.L890:
	add	x1, x23, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x1, 4]
	cmp	w0, w2
	beq	.L892
	add	w0, w0, w24
	strh	w0, [x1, 4]
	ldrh	w0, [x22, 106]
	cmp	w0, w19
	bls	.L892
.L893:
	mov	w0, -1
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 65535
	cmp	w19, 11
	bls	.L903
	add	x0, x29, 64
	mov	w2, 32
	mov	w1, 165
	adrp	x20, ftl_temp_buf
	add	x20, x20, :lo12:ftl_temp_buf
	stp	x20, x0, [x29, 40]
	bl	ftl_memset
	lsl	w19, w19, 10
	mov	w2, 8
	mov	w1, 90
	mov	x0, x20
	bl	ftl_memset
	mov	w2, 1
	str	w19, [x29, 36]
	mov	w1, w2
	add	x0, x29, 32
	bl	FlashEraseBlocks
	mov	w3, 1
	add	x0, x29, 32
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldr	w0, [x29, 32]
	mov	w2, 1
	mov	w1, 0
	cmp	w0, 0
	add	x0, x29, 32
	csetm	w19, ne
	bl	FlashEraseBlocks
.L901:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 128
	ret
.L903:
	mov	w19, 0
	b	.L901
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	stp	x29, x30, [sp, -48]!
	mov	w2, 256
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	str	x21, [sp, 32]
	mov	w21, w1
	mov	w1, 255
	bl	ftl_memset
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w21
	ldr	x2, [x0, 3368]
	mov	x0, x20
	blr	x2
	and	w0, w0, 65535
	cmp	w0, 50
	bls	.L906
	mov	w2, 256
	mov	w1, 255
	mov	x0, x20
	bl	ftl_memset
	mov	w0, 0
.L906:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19, 14]
	cmp	w1, 4
	bne	.L910
	mov	x1, 0
.L908:
	cmp	w0, w1, uxth
	bhi	.L909
.L910:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L909:
	ldrh	w2, [x20, x1, lsl 1]
	lsr	w2, w2, 1
	strh	w2, [x20, x1, lsl 1]
	add	x1, x1, 1
	b	.L908
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
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
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	stp	x29, x30, [sp, -80]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	str	x25, [sp, 64]
	adrp	x25, .LANCHOR0
	add	x19, x25, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	adrp	x22, .LANCHOR3
	mov	w21, 0
	add	x22, x22, :lo12:.LANCHOR3
	ldr	x2, [x19, 3440]
	add	x23, x19, 192
	ldr	x0, [x19, 3432]
	str	x2, [x19, 3416]
	ldrh	w2, [x19, 114]
	str	x0, [x19, 3408]
	bl	ftl_memset
.L915:
	ldrh	w0, [x19, 58]
	add	x20, x19, 3400
	cmp	w21, w0
	blt	.L916
	ldr	x24, [x20, 16]
	mov	w1, 255
	mov	w2, 16
	adrp	x22, .LC98
	mov	x0, x24
	bl	ftl_memset
	mov	w0, -3887
	add	x1, x19, 160
	strh	w0, [x24]
	add	x25, x25, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC98
	mov	w21, 0
	ldr	w0, [x19, 168]
	mov	w23, 0
	str	w0, [x24, 4]
	ldrh	w0, [x19, 160]
	strh	w0, [x24, 2]
	ldrh	w0, [x19, 164]
	strh	w0, [x24, 8]
	ldrh	w0, [x19, 166]
	strh	w0, [x24, 10]
	ldr	w0, [x19, 32]
	mov	x19, x1
	strh	w0, [x24, 12]
.L917:
	ldr	x0, [x25, 3432]
	str	x0, [x20, 8]
	ldr	x0, [x25, 3440]
	str	x0, [x20, 16]
	ldrh	w1, [x19]
	ldrh	w2, [x19, 2]
	ldrh	w3, [x19, 4]
	ldrh	w4, [x24, 10]
	orr	w0, w2, w1, lsl 10
	str	wzr, [x20]
	str	w0, [x20, 4]
	mov	x0, x22
	bl	sftl_printk
	mov	w3, 1
	mov	x0, x20
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x25, 108]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L918
	ldr	w0, [x19, 8]
	mov	w2, 1
	ldrh	w1, [x19]
	add	w0, w0, 1
	str	w0, [x19, 8]
	str	w0, [x24, 4]
	strh	w1, [x24, 8]
	ldrh	w0, [x19, 4]
	strh	w1, [x19, 4]
	ldr	x1, [x25, 3960]
	strh	w0, [x19]
	lsl	w0, w0, 10
	str	w0, [x20, 4]
	strh	wzr, [x19, 2]
	str	w0, [x1, 4]
	mov	w1, w2
	ldr	x0, [x25, 3960]
	bl	FlashEraseBlocks
	mov	w3, 1
	mov	x0, x20
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
.L918:
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L919
	ldr	w1, [x20, 4]
	add	w21, w21, 1
	adrp	x0, .LC99
	and	w21, w21, 65535
	add	x0, x0, :lo12:.LC99
	bl	sftl_printk
	cmp	w21, 3
	bls	.L917
	ldr	w1, [x20, 4]
	adrp	x0, .LC100
	mov	w2, w21
	add	x0, x0, :lo12:.LC100
	bl	sftl_printk
.L921:
	b	.L921
.L916:
	ldrh	w2, [x22, -200]
	ldr	x1, [x23], 8
	ldr	x0, [x20, 8]
	mul	w3, w21, w2
	lsl	w2, w2, 2
	add	w21, w21, 1
	add	x0, x0, x3, sxtw 2
	bl	ftl_memcpy
	b	.L915
.L922:
	mov	w23, 1
	b	.L917
.L919:
	cbz	w23, .L922
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x22, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	mov	x20, x0
	mov	x19, x22
	add	x23, x22, 2520
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
.L925:
	ldrh	w1, [x22, 2400]
	ldrh	w0, [x22, 2416]
	add	w0, w0, w1
	ldrh	w1, [x22, 44]
	cmp	w0, w1
	ble	.L926
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2585
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L926:
	cmp	x20, x23
	bne	.L951
	ldrh	w0, [x19, 2416]
	ldr	w1, [x19, 3244]
	mul	w1, w0, w1
	lsr	w0, w0, 1
	add	w0, w0, 1
	add	w1, w0, w1, lsr 2
	ands	w1, w1, 65535
	beq	.L927
	sub	w1, w1, #1
	and	w1, w1, 65535
.L927:
	add	x0, x19, 2408
	bl	List_pop_index_node
	and	w24, w0, 65535
	ldrh	w0, [x19, 2416]
	cbnz	w0, .L928
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2594
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L928:
	ldrh	w0, [x19, 2416]
	sub	w0, w0, #1
	strh	w0, [x19, 2416]
	strh	w24, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20, 7]
	cbnz	w0, .L929
	ldr	x1, [x19, 2376]
	ubfiz	x0, x24, 1, 16
	mov	w2, -1
	strh	w2, [x1, x0]
	mov	w0, w24
	bl	INSERT_DATA_LIST
	ldrh	w1, [x19, 2400]
	ldrh	w0, [x19, 2416]
	add	w0, w0, w1
	ldrh	w1, [x19, 44]
	cmp	w0, w1
	ble	.L925
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2605
	add	x1, x1, 424
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L925
.L951:
	mov	w1, 0
	b	.L927
.L929:
	ldrh	w1, [x19, 2400]
	ldrh	w0, [x19, 2416]
	add	w0, w0, w1
	ldrh	w1, [x19, 44]
	cmp	w0, w1
	ble	.L931
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2608
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L931:
	add	x27, x20, 16
	ldrh	w4, [x19, 36]
	mov	x3, x27
	mov	x0, 0
	mov	w25, 0
	mov	w5, 65535
.L932:
	cmp	w4, w0, uxth
	bhi	.L934
	cbnz	w25, .L935
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2619
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L935:
	ldrh	w0, [x19, 2672]
	cmp	w0, w24
	bne	.L936
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2621
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L936:
	ldrb	w0, [x20, 8]
	uxtw	x26, w24
	cbnz	w0, .L937
	ldr	x2, [x19, 2384]
	lsl	x1, x26, 1
	ldrh	w0, [x2, x1]
	cbz	w0, .L938
	ldrh	w3, [x19, 96]
	add	w0, w0, w3
.L959:
	strh	w0, [x2, x1]
	mov	w1, 0
	ldr	w0, [x19, 2628]
	add	w0, w0, 1
	str	w0, [x19, 2628]
	mov	w0, w24
	bl	ftl_set_blk_mode
.L940:
	ldr	x0, [x19, 2384]
	lsl	x26, x26, 1
	ldr	w1, [x19, 2644]
	ldrh	w0, [x0, x26]
	cmp	w0, w1
	bls	.L941
	str	w0, [x19, 2644]
.L941:
	ldr	w1, [x19, 2632]
	ubfiz	x3, x25, 5, 16
	ldrh	w0, [x19, 96]
	ldr	w2, [x19, 2628]
	madd	w0, w0, w2, w1
	ldrh	w1, [x19, 44]
	udiv	w0, w0, w1
	ldr	x1, [x19, 4072]
	str	w0, [x19, 2636]
	ldr	w0, [x1, 16]
	add	w0, w0, 1
	str	w0, [x1, 16]
	mov	x0, 0
.L942:
	cmp	x3, x0
	bne	.L943
	ldrb	w1, [x20, 8]
	mov	w2, w25
	ldr	x0, [x19, 3960]
	mov	x28, 0
	bl	FlashEraseBlocks
	mov	w1, 0
.L944:
	cmp	w25, w28, uxth
	bhi	.L946
	cmp	w1, 0
	ble	.L947
	mov	w0, w24
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L947:
	ldrb	w0, [x20, 7]
	cbnz	w0, .L948
	ldr	x0, [x19, 2376]
	mov	w1, -1
	strh	w1, [x0, x26]
	mov	w0, w24
	bl	INSERT_DATA_LIST
	b	.L925
.L934:
	ldr	x1, [x19, 3960]
	add	x2, x1, x0, lsl 5
	stp	xzr, xzr, [x2, 8]
	ldrh	w2, [x3]
	cmp	w2, w5
	beq	.L933
	ubfiz	x6, x25, 5, 16
	add	w25, w25, 1
	add	x1, x1, x6
	and	w25, w25, 65535
	lsl	w2, w2, 10
	str	w2, [x1, 4]
.L933:
	add	x0, x0, 1
	add	x3, x3, 2
	b	.L932
.L938:
	mov	w0, 2
	b	.L959
.L937:
	ldr	x2, [x19, 2384]
	lsl	x0, x26, 1
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x19, 2632]
	add	w0, w0, 1
	str	w0, [x19, 2632]
	mov	w0, w24
	bl	ftl_set_blk_mode.part.7
	b	.L940
.L943:
	ldr	x1, [x19, 3960]
	add	x1, x1, x0
	add	x0, x0, 32
	ldr	w2, [x1, 4]
	and	w2, w2, -1024
	str	w2, [x1, 4]
	b	.L942
.L946:
	ldr	x2, [x19, 3960]
	lsl	x0, x28, 5
	add	x3, x2, x0
	ldr	w2, [x2, x0]
	cmn	w2, #1
	bne	.L945
	add	w1, w1, 1
	ldr	w0, [x3, 4]
	stp	w2, w1, [x29, 104]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldp	w2, w1, [x29, 104]
	strh	w2, [x27]
	ldrb	w0, [x20, 7]
	sub	w0, w0, #1
	strb	w0, [x20, 7]
.L945:
	add	x28, x28, 1
	add	x27, x27, 2
	b	.L944
.L948:
	add	x21, x21, :lo12:.LANCHOR0
	strh	wzr, [x20, 2]
	strb	wzr, [x20, 6]
	ldrh	w1, [x21, 106]
	strh	w24, [x20]
	mul	w0, w0, w1
	ldr	w1, [x21, 2620]
	str	w1, [x20, 12]
	and	w0, w0, 65535
	add	w1, w1, 1
	strh	w0, [x20, 4]
	str	w1, [x21, 2620]
	ldr	x1, [x21, 2376]
	ldrh	w2, [x20]
	strh	w0, [x1, x2, lsl 1]
	ldrh	w0, [x20, 4]
	cbz	w0, .L949
	ldrb	w0, [x20, 7]
	cbnz	w0, .L950
.L949:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2674
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L950:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 65535
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	ldrh	w0, [x19, 3324]
	str	x27, [sp, 80]
	cbz	w0, .L961
	add	x23, x19, 64
	add	x24, x19, 3328
	mov	w21, 0
.L962:
	ldrh	w0, [x19, 36]
	cmp	w0, w21
	bhi	.L968
	bl	FtlGcReFreshBadBlk
.L961:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L968:
	ldrb	w0, [x23, w21, sxtw]
	mov	w1, w22
	adrp	x26, .LC101
	add	x26, x26, :lo12:.LC101
	mov	w20, 0
	bl	V2P_block
	and	w25, w0, 65535
.L963:
	ldrh	w0, [x19, 3324]
	cmp	w0, w20
	bhi	.L967
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L962
.L967:
	ldrh	w0, [x24, w20, sxtw 1]
	add	w27, w20, 1
	cmp	w0, w25
	bne	.L964
	mov	w1, w25
	mov	x0, x26
	bl	sftl_printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w0, [x19, 3324]
	mov	w1, w27
.L965:
	cmp	w20, w0
	bcc	.L966
	sub	w0, w0, #1
	strh	w0, [x19, 3324]
.L964:
	and	w20, w27, 65535
	b	.L963
.L966:
	sub	w2, w1, #1
	ldrh	w3, [x24, w1, sxtw 1]
	add	w20, w20, 1
	add	w1, w1, 1
	and	w20, w20, 65535
	strh	w3, [x24, w2, sxtw 1]
	b	.L965
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 65535
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	ubfiz	x1, x19, 1, 16
	ldr	x2, [x0, 2376]
	ldrh	w1, [x2, x1]
	cbnz	w1, .L974
	ldrh	w1, [x0, 2672]
	cmp	w1, w19
	bne	.L975
	mov	w1, -1
	strh	w1, [x0, 2672]
.L976:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x21, 2368
	bl	List_remove_node
	ldrh	w0, [x21, 2400]
	cbnz	w0, .L978
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2744
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L978:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20, 2400]
	sub	w0, w0, #1
	strh	w0, [x20, 2400]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	ldrh	w1, [x20, 2400]
	ldrh	w0, [x20, 2416]
	add	w0, w0, w1
	ldrh	w1, [x20, 44]
	cmp	w0, w1
	ble	.L982
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2747
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L982:
	mov	w0, 1
	b	.L973
.L975:
	ldrh	w1, [x0, 2424]
	cmp	w1, w19
	beq	.L981
	ldrh	w1, [x0, 2472]
	cmp	w1, w19
	beq	.L981
	ldrh	w0, [x0, 2520]
	cmp	w0, w19
	bne	.L976
.L981:
	mov	w0, 0
.L973:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L974:
	mov	w0, w19
	bl	List_update_data_list
	b	.L981
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 65535
	str	x21, [sp, 32]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L985
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	ubfiz	x21, x19, 1, 16
	ldr	x1, [x20, 2376]
	ldrh	w0, [x1, x21]
	cbnz	w0, .L986
	mov	w2, 0
	mov	w1, w19
	adrp	x0, .LC102
	add	x0, x0, :lo12:.LC102
	bl	sftl_printk
	ldr	x0, [x20, 2376]
	ldrh	w0, [x0, x21]
	cbz	w0, .L987
.L992:
	mov	w21, 0
.L984:
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L987:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2762
	add	x1, x1, 472
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L992
.L986:
	sub	w0, w0, #1
	strh	w0, [x1, x21]
.L985:
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	mov	w1, 65535
	ldrh	w0, [x20, -48]
	cmp	w0, w1
	bne	.L989
	strh	w19, [x20, -48]
	b	.L992
.L989:
	cmp	w19, w0
	beq	.L992
	bl	update_vpc_list
	cmp	w0, 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	strh	w19, [x20, -48]
	cset	w21, ne
	ldr	x2, [x1, 2360]
	ldr	x0, [x1, 2368]
	ldr	x1, [x1, 2376]
	sub	x0, x0, x2
	mov	x2, -6148914691236517206
	asr	x0, x0, 1
	movk	x2, 0xaaab, lsl 0
	mul	x0, x0, x2
	and	x2, x0, 65535
	ldrh	w1, [x1, x2, lsl 1]
	cbnz	w1, .L984
	cmp	w19, w0, uxth
	beq	.L984
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2778
	add	x1, x1, 472
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L984
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlWriteDump_data
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldrh	w2, [x20, 2428]
	cbz	w2, .L994
	ldrb	w1, [x20, 2432]
	cbnz	w1, .L994
	ldrb	w1, [x20, 2431]
	ldrh	w3, [x20, 106]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L994
	ldrb	w0, [x20, 2434]
	cbnz	w0, .L993
	ldr	w22, [x20, 2584]
	add	x1, x29, 92
	ldrh	w24, [x20, 36]
	mov	w2, 0
	sub	w22, w22, #1
	mov	w0, w22
	bl	log2phys
	ldr	x21, [x20, 3440]
	ldr	w0, [x29, 92]
	ldr	x1, [x20, 3432]
	str	w0, [x29, 100]
	cmn	w0, #1
	stp	x1, x21, [x29, 104]
	str	w22, [x29, 120]
	str	wzr, [x21, 4]
	beq	.L996
	mov	w2, 0
	mov	w1, 1
	add	x0, x29, 96
	bl	FlashReadPages
.L997:
	add	x25, x19, :lo12:.LANCHOR0
	lsl	w24, w24, 2
	add	x20, x25, 2424
	mov	w0, -3947
	mov	w23, 0
	strh	w0, [x21]
.L998:
	cmp	w24, w23
	bne	.L1002
.L999:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19, 2434]
.L993:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 128
	ret
.L996:
	ldrh	w2, [x20, 114]
	mov	w1, 255
	ldr	x0, [x20, 3432]
	bl	ftl_memset
	b	.L997
.L1002:
	ldrh	w0, [x20, 4]
	cbz	w0, .L999
	ldr	w0, [x29, 100]
	add	w23, w23, 1
	stp	w22, w0, [x21, 8]
	ldrh	w0, [x20]
	strh	w0, [x21, 2]
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29, 100]
	ldr	w0, [x25, 2624]
	mov	w3, 0
	str	w0, [x21, 4]
	mov	w2, 0
	add	w0, w0, 1
	mov	w1, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x25, 2624]
	add	x0, x29, 96
	bl	FlashProgPages
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	b	.L998
.L994:
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x19, 2434]
	b	.L993
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	l2p_flush
	.type	l2p_flush, %function
l2p_flush:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w19, 0
	bl	FtlWriteDump_data
.L1012:
	ldrh	w0, [x20, 142]
	cmp	w0, w19
	bhi	.L1014
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1014:
	ldr	x1, [x20, 2568]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L1013
	mov	w0, w19
	bl	flush_l2p_region
.L1013:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1012
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	stp	x29, x30, [sp, -176]!
	mov	w1, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w0, [x0]
	cmp	w0, w1
	beq	.L1134
	ldrb	w0, [x19, 6]
	adrp	x20, .LANCHOR0
	str	w0, [x29, 156]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w27, [x19, 2]
	ldrh	w0, [x0, 106]
	cmp	w0, w27
	bne	.L1019
	strh	wzr, [x19, 4]
.L1140:
	strb	wzr, [x19, 6]
.L1134:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1019:
	ldrh	w0, [x19, 16]
	mov	w21, 0
.L1020:
	cmp	w0, w1
	beq	.L1021
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	mov	w24, w0
	cmn	w0, #1
	beq	.L1022
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x19, 16
	str	x1, [x29, 144]
	add	x5, x19, 16
	mov	w21, 0
	mov	w10, 65535
	ldrh	w1, [x0, 36]
	mov	w6, 4
	ldrh	w8, [x0, 114]
	add	x1, x1, 8
	ldrh	w9, [x0, 116]
	add	x1, x19, x1, lsl 1
.L1023:
	cmp	x5, x1
	bne	.L1027
	add	x23, x20, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w1, w21
	ldr	x0, [x23, 4016]
	bl	FlashReadPages
	ldr	w22, [x23, 2624]
	ldr	x4, [x23, 4016]
	and	w9, w24, 65535
	sub	w22, w22, #1
	mov	w7, 65535
	mov	x6, x4
	mov	w3, 0
.L1028:
	cmp	w21, w3
	bne	.L1033
	add	w23, w24, 1
	ldr	w0, [x4, 4]
	and	w23, w23, 65535
.L1136:
	lsr	w0, w0, 10
	bl	P2V_plane
	and	w28, w0, 65535
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0, 106]
	cmp	w0, w23
	bne	.L1035
	strh	w23, [x19, 2]
	strb	wzr, [x19, 6]
	strh	wzr, [x19, 4]
.L1035:
	ldr	w0, [x29, 156]
	cmp	w23, w27
	ccmp	w28, w0, 0, eq
	bne	.L1036
.L1141:
	mov	w2, w28
	mov	w1, w23
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
	b	.L1134
.L1021:
	add	w21, w21, 1
	and	w21, w21, 65535
	add	x0, x19, x21, sxtw 1
	ldrh	w0, [x0, 16]
	b	.L1020
.L1022:
	cbz	w27, .L1024
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1755
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1024:
	ldr	w0, [x29, 156]
	cmp	w0, 0
	ccmp	w21, w0, 4, ne
	beq	.L1025
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1756
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1025:
	strh	wzr, [x19, 2]
	b	.L1140
.L1027:
	ldrh	w2, [x5]
	cmp	w2, w10
	beq	.L1026
	ldr	x4, [x0, 4016]
	ubfiz	x7, x21, 5, 16
	orr	w2, w24, w2, lsl 10
	add	x4, x4, x7
	str	w2, [x4, 4]
	mul	w2, w21, w8
	ldr	x3, [x0, 4016]
	ldr	x4, [x0, 3264]
	sdiv	w2, w2, w6
	add	x3, x3, x7
	add	x2, x4, x2, sxtw 2
	str	x2, [x3, 8]
	mul	w2, w21, w9
	ldr	x4, [x0, 3272]
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w2, w2, w6
	add	x2, x4, x2, sxtw 2
	str	x2, [x3, 16]
.L1026:
	add	x5, x5, 2
	b	.L1023
.L1033:
	ldr	w0, [x6]
	cbnz	w0, .L1029
	ldr	x8, [x6, 16]
	ldr	w5, [x8, 4]
	cmn	w5, #1
	beq	.L1030
	ldr	w1, [x23, 2624]
	mov	w0, w5
	bl	ftl_cmp_data_ver
	cbz	w0, .L1030
	add	w5, w5, 1
	str	w5, [x23, 2624]
.L1030:
	ldr	w0, [x8]
	cmn	w0, #1
	bne	.L1032
	ubfiz	x3, x3, 5, 16
	and	w23, w24, 65535
	add	x3, x4, x3
	ldr	w0, [x3, 4]
	b	.L1136
.L1029:
	mov	w7, w9
.L1032:
	add	w3, w3, 1
	add	x6, x6, 32
	and	w3, w3, 65535
	b	.L1028
.L1036:
	mov	w0, 65535
	adrp	x21, .LANCHOR3
	cmp	w7, w0
	bne	.L1037
	ldrb	w0, [x19, 8]
	cbnz	w0, .L1038
.L1037:
	add	x0, x21, :lo12:.LANCHOR3
	and	w3, w24, 65535
	ldr	w1, [x0, -36]
	cmn	w1, #1
	bne	.L1039
	str	w22, [x0, -36]
.L1039:
	add	w0, w27, 7
	cmp	w0, w24, uxth
	bge	.L1087
	sub	w25, w3, #7
	and	w25, w25, 65535
.L1040:
	add	x26, x20, :lo12:.LANCHOR0
	mov	w4, 65535
.L1041:
	cmp	w25, w3
	bhi	.L1048
	ldrh	w0, [x26, 36]
	mov	w24, 0
	ldr	x1, [x29, 144]
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
	b	.L1049
.L1087:
	mov	w25, w27
	b	.L1040
.L1043:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1042
	ldr	x5, [x26, 4016]
	ubfiz	x6, x24, 5, 16
	add	w24, w24, 1
	orr	w2, w25, w2, lsl 10
	add	x5, x5, x6
	and	w24, w24, 65535
	str	w2, [x5, 4]
.L1042:
	add	x1, x1, 2
.L1049:
	cmp	x0, x1
	bne	.L1043
	ldr	x0, [x26, 4016]
	mov	w1, w24
	mov	w2, 0
	str	w4, [x29, 128]
	str	w3, [x29, 136]
	ubfiz	x24, x24, 5, 16
	add	x24, x24, 16
	bl	FlashReadPages
	ldr	x1, [x26, 4016]
	add	x2, x21, :lo12:.LANCHOR3
	ldr	w4, [x29, 128]
	ldr	w3, [x29, 136]
	add	x0, x1, 16
	add	x24, x1, x24
.L1044:
	cmp	x24, x0
	bne	.L1047
	add	w25, w25, 1
	and	w25, w25, 65535
	b	.L1041
.L1047:
	ldr	w1, [x0, -16]
	cbnz	w1, .L1038
	ldr	x1, [x0]
	ldrh	w5, [x1]
	cmp	w5, w4
	beq	.L1046
	ldr	w1, [x1, 4]
	cmn	w1, #1
	beq	.L1046
	str	w1, [x2, -36]
.L1046:
	add	x0, x0, 32
	b	.L1044
.L1048:
	add	x0, x21, :lo12:.LANCHOR3
	mov	w1, -1
	str	w1, [x0, -36]
.L1038:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 1
	mov	w25, w27
	add	x20, x20, :lo12:.LANCHOR0
	strh	w0, [x21, -32]
	sub	x0, x21, #192
	bl	FtlMapBlkWriteDump_data
	sub	x0, x21, #24
	str	x0, [x29, 112]
.L1050:
	ldrh	w0, [x20, 36]
	mov	w24, 0
	ldr	x1, [x29, 144]
	mov	w4, 65535
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
.L1051:
	cmp	x0, x1
	bne	.L1053
	ldr	x0, [x20, 4016]
	mov	w1, w24
	mov	w2, 0
	bl	FlashReadPages
	ubfiz	x0, x24, 5, 16
	str	x0, [x29, 120]
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 496
	mov	x24, 0
	str	x0, [x29, 128]
.L1054:
	ldr	x0, [x29, 120]
	cmp	x0, x24
	bne	.L1081
	add	w25, w25, 1
	ldrh	w0, [x20, 106]
	and	w25, w25, 65535
	cmp	w0, w25
	bne	.L1050
	ldrh	w1, [x20, 36]
	mov	w0, 0
	strh	w25, [x19, 2]
	mov	w2, 65535
	strh	wzr, [x19, 4]
.L1082:
	cmp	w0, w1
	beq	.L1134
	ldr	x4, [x29, 144]
	ldrh	w3, [x4], 2
	str	x4, [x29, 144]
	cmp	w3, w2
	beq	.L1083
	strb	w0, [x19, 6]
	b	.L1134
.L1053:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1052
	ldr	x3, [x20, 4016]
	ubfiz	x5, x24, 5, 16
	add	w24, w24, 1
	orr	w2, w25, w2, lsl 10
	add	x3, x3, x5
	and	w24, w24, 65535
	str	w2, [x3, 4]
.L1052:
	add	x1, x1, 2
	b	.L1051
.L1081:
	ldr	x4, [x20, 4016]
	add	x4, x4, x24
	ldr	w5, [x4, 4]
	str	w5, [x29, 172]
	lsr	w0, w5, 10
	bl	P2V_plane
	and	w0, w0, 65535
	cmp	w25, w27
	bcc	.L1055
	ldr	w1, [x29, 156]
	ccmp	w1, w0, 0, eq
	bhi	.L1055
	cmp	w25, w23
	ccmp	w28, w0, 0, eq
	beq	.L1056
	ldr	w0, [x4]
	cmn	w0, #1
	beq	.L1057
	ldr	x3, [x4, 16]
	mov	w0, 61589
	ldrh	w1, [x3]
	cmp	w1, w0
	beq	.L1058
.L1065:
	ldrh	w0, [x19]
.L1139:
	bl	decrement_vpc_count
.L1055:
	add	x24, x24, 32
	b	.L1054
.L1058:
	ldr	w22, [x3, 4]
	cmn	w22, #1
	beq	.L1059
	ldr	w1, [x20, 2624]
	mov	w0, w22
	str	x3, [x29, 136]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29, 136]
	cbz	w0, .L1059
	add	w0, w22, 1
	str	w0, [x20, 2624]
.L1059:
	ldrh	w1, [x3]
	mov	w0, 61589
	cmp	w1, w0
	beq	.L1060
	ldr	x1, [x29, 128]
	mov	w2, 1905
	str	x3, [x29, 136]
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	ldr	x3, [x29, 136]
.L1060:
	ldp	w26, w0, [x3, 8]
	add	x1, x29, 168
	str	w0, [x29, 164]
	mov	w2, 0
	mov	w0, w26
	bl	log2phys
	ldr	w1, [x21, -36]
	cmn	w1, #1
	beq	.L1061
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L1061
	ldr	w1, [x29, 164]
	cmn	w1, #1
	beq	.L1062
	ldr	x0, [x20, 4016]
	mov	w2, 0
	add	x0, x0, x24
	ldr	x4, [x0, 16]
	str	w1, [x0, 4]
	mov	w1, 1
	str	x4, [x29, 136]
	ldr	x0, [x20, 4016]
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x0, [x20, 4016]
	ldr	x4, [x29, 136]
	add	x3, x0, x24
	ldr	w0, [x0, x24]
	cmn	w0, #1
	bne	.L1063
.L1064:
	mov	w0, -1
	str	w0, [x29, 164]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
.L1072:
	ldr	w4, [x29, 164]
	cmn	w4, #1
	beq	.L1055
.L1086:
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	and	w26, w0, 65535
	ldrh	w0, [x20, 44]
	mov	w3, w26
	cmp	w0, w26
	bhi	.L1077
	ldr	x1, [x29, 128]
	mov	w2, 2006
	adrp	x0, .LC1
	str	w26, [x29, 136]
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	ldr	w3, [x29, 136]
.L1077:
	ldr	x1, [x20, 2376]
	ubfiz	x0, x26, 1, 16
	ldrh	w0, [x1, x0]
	cbz	w0, .L1078
	mov	w0, w3
	b	.L1139
.L1062:
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	bne	.L1065
	mov	w2, 1
	add	x1, x29, 164
	mov	w0, w26
	bl	log2phys
	b	.L1065
.L1063:
	ldr	w0, [x4, 8]
	cmp	w26, w0
	bne	.L1064
	ldr	w0, [x4, 4]
	str	w0, [x29, 136]
	str	x4, [x29, 104]
	uxtw	x1, w0
	ldr	w0, [x21, -36]
	bl	ftl_cmp_data_ver
	cbz	w0, .L1064
	ldp	w0, w1, [x29, 168]
	ldr	x4, [x29, 104]
	cmp	w0, w1
	ldr	w1, [x29, 164]
	bne	.L1067
.L1137:
	mov	w0, w26
	bl	FtlReUsePrevPpa
	b	.L1064
.L1067:
	cmp	w0, w1
	beq	.L1064
	cmn	w0, #1
	beq	.L1068
	ldr	x4, [x3, 16]
	mov	w2, 0
	str	w0, [x3, 4]
	mov	w1, 1
	str	x4, [x29, 104]
	ldr	x0, [x20, 4016]
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x4, [x29, 104]
.L1069:
	adrp	x0, .LANCHOR0+4016
	ldr	x0, [x0, #:lo12:.LANCHOR0+4016]
	ldr	w0, [x0, x24]
	cmn	w0, #1
	beq	.L1070
	ldr	w3, [x4, 4]
	ldr	w0, [x21, -36]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1070
	ldr	w0, [x29, 136]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1064
.L1070:
	ldr	w1, [x29, 164]
	b	.L1137
.L1068:
	str	w0, [x3]
	b	.L1069
.L1061:
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	beq	.L1072
	mov	w2, 1
	add	x1, x29, 172
	mov	w0, w26
	bl	log2phys
	ldr	w4, [x29, 168]
	cmn	w4, #1
	beq	.L1072
	ldr	w0, [x29, 164]
	cmp	w4, w0
	beq	.L1086
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20, 2424]
	and	w0, w0, 65535
	cmp	w1, w0
	beq	.L1076
	ldrh	w1, [x20, 2472]
	cmp	w1, w0
	beq	.L1076
	ldrh	w1, [x20, 2520]
	cmp	w1, w0
	bne	.L1072
.L1076:
	ldr	x0, [x20, 4016]
	mov	w2, 0
	mov	w1, 1
	str	w4, [x0, 4]
	ldr	x3, [x0, 16]
	ldr	x0, [x20, 4016]
	str	x3, [x29, 136]
	bl	FlashReadPages
	ldr	x0, [x20, 4016]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1072
	ldr	x3, [x29, 136]
	mov	w0, w22
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
	cbnz	w0, .L1072
	mov	w2, 1
	add	x1, x29, 168
	mov	w0, w26
	bl	log2phys
	b	.L1072
.L1078:
	mov	w1, w26
	adrp	x0, .LC103
	add	x0, x0, :lo12:.LC103
	bl	sftl_printk
	b	.L1055
.L1057:
	ldr	w0, [x21, -28]
	cmp	w0, 31
	bhi	.L1079
	ldr	x1, [x29, 112]
	str	w5, [x1, w0, uxtw 2]
	add	w0, w0, 1
	str	w0, [x21, -28]
.L1079:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldr	w0, [x21, -36]
	cmn	w0, #1
	bne	.L1080
.L1138:
	str	w22, [x21, -36]
	b	.L1055
.L1080:
	cmp	w22, w0
	bcs	.L1055
	b	.L1138
.L1083:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1082
.L1056:
	strb	w28, [x19, 6]
	strh	w23, [x19, 2]
	b	.L1141
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	stp	x29, x30, [sp, -96]!
	adrp	x2, .LANCHOR0
	mov	w3, 61589
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w0, -1
	stp	x21, x22, [sp, 32]
	str	w0, [x29, 88]
	add	x0, x2, :lo12:.LANCHOR0
	str	x23, [sp, 48]
	mov	x20, x0
	mov	x23, x2
	mov	w22, 7
	ldr	x21, [x0, 3440]
	ldr	x1, [x0, 3432]
	stp	x1, x21, [x29, 72]
	mov	w1, -3
	str	w1, [x21, 8]
	mov	w1, -2
	str	w1, [x21, 12]
	ldrh	w1, [x19]
	strh	w1, [x21, 2]
	strh	wzr, [x21]
	ldr	x1, [x0, 3432]
	str	w3, [x1]
	mov	w3, 22136
	movk	w3, 0x1234, lsl 16
	ldr	x1, [x0, 3432]
	str	w3, [x1, 4]
.L1143:
	subs	w22, w22, #1
	beq	.L1145
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1144
.L1145:
	add	x2, x23, :lo12:.LANCHOR0
	ldrh	w0, [x19]
	ldrh	w4, [x19, 4]
	ldr	x3, [x2, 2376]
	lsl	x0, x0, 1
	ldrh	w1, [x3, x0]
	sub	w1, w1, w4
	strh	w1, [x3, x0]
	strb	wzr, [x19, 6]
	ldrh	w0, [x2, 106]
	strh	w0, [x19, 2]
	strh	wzr, [x19, 4]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 96
	ret
.L1144:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29, 68]
	cmn	w0, #1
	beq	.L1145
	ldr	w0, [x20, 2624]
	mov	w3, 0
	str	w0, [x21, 4]
	mov	w2, 0
	add	w0, w0, 1
	mov	w1, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x20, 2624]
	add	x0, x29, 64
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L1143
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x23, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x21, x23, 3400
	mov	w24, 61649
	ldr	x0, [x23, 3432]
	ldr	x22, [x23, 3440]
	stp	x0, x22, [x21, 8]
	bl	FtlBbtMemInit
	ldrh	w20, [x23, 102]
	sub	w20, w20, #1
	and	w20, w20, 65535
.L1155:
	ldrh	w0, [x23, 102]
	sub	w0, w0, #16
	cmp	w20, w0
	ble	.L1158
	lsl	w0, w20, 10
	mov	w2, 1
	str	w0, [x21, 4]
	mov	w1, w2
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1156
	ldr	w0, [x21, 4]
	mov	w2, 1
	mov	w1, w2
	add	w0, w0, 1
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
.L1156:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1157
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1157
	ldr	w1, [x22, 4]
	str	w1, [x23, 168]
	strh	w20, [x23, 160]
	ldrh	w1, [x22, 8]
	strh	w1, [x23, 164]
.L1158:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21, 160]
	cmp	w1, w0
	beq	.L1172
	ldrh	w1, [x21, 164]
	cmp	w1, w0
	beq	.L1162
	add	x0, x21, 3400
	lsl	w1, w1, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x21, 3400]
	cmn	w0, #1
	beq	.L1162
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1162
	ldr	w1, [x21, 168]
	ldr	w0, [x22, 4]
	cmp	w0, w1
	bls	.L1162
	str	w0, [x21, 168]
	ldrh	w1, [x21, 164]
	ldrh	w0, [x22, 8]
	strh	w1, [x21, 160]
	strh	w0, [x21, 164]
.L1162:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x23, x20, 3400
	mov	w24, 61649
	ldrh	w0, [x20, 160]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20, 162]
.L1164:
	tbz	w21, #31, .L1167
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 254
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1166:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 10]
	mov	w2, 65535
	strh	w0, [x1, 166]
	ldrh	w0, [x22, 12]
	cmp	w0, w2
	beq	.L1169
	ldr	w2, [x1, 32]
	cmp	w0, w2
	beq	.L1169
	ldrh	w1, [x1, 46]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L1169
	cmp	w0, w1
	bcs	.L1169
	bl	FtlSysBlkNumInit
.L1169:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	add	x22, x19, 192
	add	x21, x21, :lo12:.LANCHOR3
	mov	w20, 0
.L1170:
	ldrh	w0, [x19, 58]
	cmp	w20, w0
	bcc	.L1171
	mov	w0, 0
.L1154:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1157:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L1155
.L1167:
	ldrh	w0, [x20, 160]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w21, w0, lsl 10
	str	w0, [x23, 4]
	ldr	x0, [x20, 3432]
	str	x0, [x23, 8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1165
	ldrh	w0, [x22]
	cmp	w0, w24
	beq	.L1166
.L1165:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L1164
.L1171:
	ldrh	w2, [x21, -200]
	ldr	x0, [x19, 3408]
	mul	w1, w2, w20
	lsl	w2, w2, 2
	add	w20, w20, 1
	add	x1, x0, x1, lsl 2
	ldr	x0, [x22], 8
	bl	ftl_memcpy
	b	.L1170
.L1172:
	mov	w0, -1
	b	.L1154
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x25, x26, [sp, 64]
	stp	x21, x22, [sp, 32]
	mov	x21, x19
	stp	x23, x24, [sp, 48]
	mov	w24, 0
	stp	x27, x28, [sp, 80]
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
	adrp	x28, .LANCHOR3
	add	x0, x19, :lo12:.LANCHOR0
	add	x28, x28, :lo12:.LANCHOR3
	add	x26, x0, 192
	add	x25, x0, 172
	mov	x19, x0
	add	x20, x0, 3400
.L1185:
	ldrh	w0, [x19, 58]
	cmp	w24, w0
	bcc	.L1191
	add	x20, x21, :lo12:.LANCHOR0
	mov	w19, 0
.L1192:
	ldrh	w0, [x20, 118]
	cmp	w0, w19
	bhi	.L1193
	add	x20, x20, 160
	ldrh	w19, [x20, 12]
	mov	w22, 65535
	sub	w19, w19, #1
	and	w19, w19, 65535
.L1194:
	ldrh	w0, [x20, 12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1198
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1195
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L1196
	mov	w0, w19
	bl	FtlBbmMapBadBlock
.L1195:
	sub	w19, w19, #1
	and	w19, w19, 65535
	b	.L1194
.L1191:
	ldr	x0, [x19, 3432]
	mov	w2, 65535
	ldr	x27, [x19, 3440]
	ldrh	w1, [x25]
	stp	x0, x27, [x20, 8]
	cmp	w1, w2
	beq	.L1186
	ldrh	w23, [x19, 102]
	mov	w2, 1
	madd	w23, w23, w24, w1
	mov	w1, w2
	lsl	w0, w23, 10
	str	w0, [x20, 4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	x1, [x20, 8]
	ldr	x0, [x26]
	ldrh	w2, [x19, 102]
	add	w2, w2, 7
	lsr	w2, w2, 3
	bl	ftl_memcpy
.L1187:
	mov	w0, w23
	add	w24, w24, 1
	bl	FtlBbmMapBadBlock
	add	x26, x26, 8
	add	x25, x25, 2
	b	.L1185
.L1186:
	mov	w1, w24
	bl	FlashGetBadBlockList
	ldr	x0, [x20, 8]
	ldr	x1, [x26]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x19, 102]
.L1189:
	sub	w22, w22, #1
	and	w22, w22, 65535
.L1188:
	ldrh	w0, [x19, 102]
	madd	w0, w24, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1189
	ldr	x0, [x19, 3440]
	mov	w2, 16
	strh	w22, [x25]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x19, 3432]
	mov	w2, 4096
	mov	w1, 0
	bl	ftl_memset
	str	wzr, [x27, 4]
	mov	w0, -3872
	strh	w0, [x27]
	ldrh	w0, [x25]
	strh	w0, [x27, 2]
	ldrh	w23, [x19, 102]
	ldrh	w0, [x25]
	ldr	x1, [x26]
	ldrh	w2, [x28, -200]
	madd	w23, w23, w24, w0
	lsl	w2, w2, 2
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
	bne	.L1187
	mov	w0, w23
	bl	FtlBbmMapBadBlock
	b	.L1188
.L1193:
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	and	w19, w19, 65535
	b	.L1192
.L1196:
	ldrh	w0, [x20]
	cmp	w0, w22
	bne	.L1197
	strh	w19, [x20]
	b	.L1195
.L1197:
	strh	w19, [x20, 4]
.L1198:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, 2
	ldr	x1, [x21, 3960]
	ldrh	w0, [x21, 160]
	str	wzr, [x21, 168]
	strh	wzr, [x21, 162]
	lsl	w0, w0, 10
	str	w0, [x1, 4]
	ldr	x0, [x21, 3960]
	ldrh	w1, [x21, 164]
	lsl	w1, w1, 10
	str	w1, [x0, 36]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x21, 160]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x21, 164]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x21, 162]
	ldr	w0, [x21, 168]
	ldrh	w1, [x21, 164]
	add	w0, w0, 1
	str	w0, [x21, 168]
	ldrh	w0, [x21, 160]
	strh	w0, [x21, 164]
	strh	w1, [x21, 160]
	bl	FtlBbmTblFlush
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
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	stp	x29, x30, [sp, -208]!
	add	x29, sp, 0
	stp	x27, x28, [sp, 80]
	mov	w27, w0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	x24, x2
	add	w2, w0, w1
	add	x0, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	w28, w1
	stp	x25, x26, [sp, 64]
	ldrh	w1, [x0, 100]
	cmp	w2, w1
	bhi	.L1212
	ldrh	w21, [x0, 112]
	adrp	x26, .LANCHOR3
	add	x25, x26, :lo12:.LANCHOR3
	mov	w23, 0
	sub	x25, x25, #120
	lsr	w21, w27, w21
.L1206:
	cbnz	w28, .L1211
.L1204:
	mov	w0, w23
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1211:
	add	x2, x22, :lo12:.LANCHOR0
	add	x0, x26, :lo12:.LANCHOR3
	ldrh	w1, [x2, 62]
	ldr	x0, [x0, -232]
	udiv	w20, w27, w1
	ldr	w4, [x0, w21, uxtw 2]
	and	w0, w28, 65535
	msub	w20, w20, w1, w27
	sub	w19, w1, w20
	and	w19, w19, 65535
	cmp	w28, w19
	csel	w19, w0, w19, cc
	cbz	w4, .L1208
	cmp	w19, w1
	beq	.L1208
	ldr	x0, [x2, 4048]
	mov	w2, 1
	str	x0, [x29, 120]
	add	x0, x29, 144
	mov	w1, w2
	str	x0, [x29, 128]
	str	w4, [x29, 116]
	add	x0, x29, 112
	bl	FlashReadPages
.L1209:
	add	x5, x22, :lo12:.LANCHOR0
	lsl	w4, w19, 9
	lsl	w20, w20, 9
	mov	w2, w4
	asr	w20, w20, 2
	mov	x1, x24
	ldr	x0, [x5, 4048]
	sub	w28, w28, w19
	str	w4, [x29, 108]
	add	w27, w27, w19
	str	x5, [x29, 96]
	add	x0, x0, x20, sxtw 2
	bl	ftl_memcpy
	ldr	x5, [x29, 96]
	mov	w1, w21
	mov	x0, x25
	add	w21, w21, 1
	ldr	x2, [x5, 4048]
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	w4, [x29, 108]
	csinv	w23, w23, wzr, ne
	add	x24, x24, x4, sxtw
	b	.L1206
.L1208:
	add	x0, x22, :lo12:.LANCHOR0
	mov	w1, 0
	ldrh	w2, [x0, 114]
	ldr	x0, [x0, 4048]
	bl	ftl_memset
	b	.L1209
.L1212:
	mov	w23, -1
	b	.L1204
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w3, [x0, 2720]
	cmp	w3, w1
	bne	.L1220
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 2720
	mov	w1, 68
	add	x29, sp, 0
	movk	w1, 0x5000, lsl 16
	str	w1, [x2, 4]
	ldr	w1, [x0, 2612]
	str	w1, [x2, 88]
	ldr	w1, [x0, 2616]
	str	w1, [x2, 92]
	ldr	w1, [x0, 2608]
	str	w1, [x2, 8]
	ldr	w1, [x0, 2596]
	str	w1, [x2, 12]
	ldr	w1, [x0, 2588]
	str	w1, [x2, 16]
	ldr	w1, [x0, 2604]
	str	w1, [x2, 20]
	ldr	w1, [x0, 2632]
	str	w1, [x2, 28]
	ldr	w1, [x0, 2640]
	str	w1, [x2, 32]
	ldr	w1, [x0, 2592]
	str	w1, [x2, 36]
	ldr	w1, [x0, 2600]
	str	w1, [x2, 40]
	ldr	w1, [x0, 2644]
	ldr	w0, [x0, 2648]
	stp	w1, w0, [x2, 44]
	mov	w1, 1
	mov	w0, 0
	bl	FtlVendorPartWrite
	ldp	x29, x30, [sp], 16
	ret
.L1220:
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	ldrh	w2, [x1, 104]
	cmp	w2, 31
	bhi	.L1227
	add	w2, w2, 1
	strh	w2, [x1, 104]
	mov	w2, 1
.L1224:
	adrp	x1, .LANCHOR0
	cbnz	w0, .L1225
	add	x0, x1, :lo12:.LANCHOR0
	ldr	x0, [x0, 4072]
	ldr	w3, [x0, 20]
	ldr	w0, [x0, 16]
	add	w2, w2, w3
	cmp	w0, w2
	bcc	.L1229
.L1225:
	add	x0, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x0, 4072]
	ldr	w2, [x1, 16]
	str	w2, [x1, 20]
	mov	w2, 17221
	movk	w2, 0x4254, lsl 16
	str	w2, [x1]
	ldr	x2, [x0, 4072]
	ldrh	w1, [x0, 4056]
	lsl	w3, w1, 9
	str	wzr, [x2, 4]
	str	w3, [x2, 12]
	ldr	w3, [x2, 8]
	add	w3, w3, 1
	str	w3, [x2, 8]
	ldr	x2, [x0, 4072]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1227:
	mov	w2, 32
	b	.L1224
.L1229:
	mov	w0, 0
	ret
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	sftl_vendor_write
	.type	sftl_vendor_write, %function
sftl_vendor_write:
	stp	x29, x30, [sp, -16]!
	add	w0, w0, 256
	add	x29, sp, 0
	bl	FtlVendorPartWrite
	ldp	x29, x30, [sp], 16
	ret
	.size	sftl_vendor_write, .-sftl_vendor_write
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	stp	x29, x30, [sp, -192]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x27, x28, [sp, 80]
	stp	x21, x22, [sp, 32]
	mov	w28, w0
	mov	w27, w1
	stp	x23, x24, [sp, 48]
	add	w1, w0, w1
	stp	x25, x26, [sp, 64]
	ldrh	w0, [x20, 100]
	cmp	w1, w0
	bhi	.L1241
	ldrh	w3, [x20, 112]
	adrp	x24, .LANCHOR3
	mov	x23, x2
	add	x24, x24, :lo12:.LANCHOR3
	mov	w22, 0
	lsr	w25, w28, w3
.L1235:
	cbnz	w27, .L1240
.L1233:
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 192
	ret
.L1240:
	ldrh	w19, [x20, 62]
	ldr	x0, [x24, -232]
	udiv	w21, w28, w19
	ldr	w1, [x0, w25, uxtw 2]
	and	w0, w27, 65535
	msub	w21, w21, w19, w28
	sub	w19, w19, w21
	and	w19, w19, 65535
	cmp	w27, w19
	csel	w19, w0, w19, cc
	lsl	w26, w19, 9
	cbz	w1, .L1237
	ldr	x0, [x20, 4048]
	mov	w2, 1
	str	x0, [x29, 104]
	add	x0, x29, 128
	str	w1, [x29, 100]
	mov	w1, w2
	str	x0, [x29, 112]
	add	x0, x29, 96
	bl	FlashReadPages
	lsl	w21, w21, 9
	ldr	w0, [x29, 96]
	asr	w21, w21, 2
	ldr	x1, [x20, 4048]
	mov	w2, w26
	cmn	w0, #1
	mov	x0, x23
	csinv	w22, w22, wzr, ne
	add	x1, x1, x21, sxtw 2
	bl	ftl_memcpy
.L1239:
	add	w25, w25, 1
	sub	w27, w27, w19
	add	w28, w28, w19
	add	x23, x23, x26, sxtw
	b	.L1235
.L1237:
	mov	w2, w26
	mov	w1, 0
	mov	x0, x23
	bl	ftl_memset
	b	.L1239
.L1241:
	mov	w22, -1
	b	.L1233
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlLoadEctTbl
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	stp	x29, x30, [sp, -32]!
	mov	w0, 64
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19, 4056]
	ldr	x2, [x19, 4072]
	bl	FtlVendorPartRead
	ldr	x0, [x19, 4072]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1244
	adrp	x1, .LC104
	adrp	x0, .LC71
	add	x1, x1, :lo12:.LC104
	add	x0, x0, :lo12:.LC71
	bl	sftl_printk
	ldr	x0, [x19, 4072]
	mov	w1, 0
	ldrh	w2, [x19, 4056]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1244:
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
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x22, x20, 2720
	mov	w21, 19539
	mov	x2, x22
	bl	FtlVendorPartRead
	ldr	w0, [x20, 2720]
	movk	w21, 0x4654, lsl 16
	cmp	w0, w21
	beq	.L1247
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	str	w21, [x20, 2720]
.L1247:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0, 2720]
	cmp	w3, w2
	bne	.L1248
	ldr	w2, [x0, 2808]
	str	w2, [x0, 2612]
	ldr	w2, [x0, 2812]
	str	w2, [x0, 2616]
	ldr	w2, [x0, 2728]
	str	w2, [x0, 2608]
	ldr	w2, [x0, 2732]
	str	w2, [x0, 2596]
	ldr	w2, [x0, 2736]
	str	w2, [x0, 2588]
	ldr	w2, [x0, 2740]
	str	w2, [x0, 2604]
	ldr	w2, [x0, 2748]
	str	w2, [x0, 2632]
	ldr	w2, [x0, 2752]
	str	w2, [x0, 2640]
	ldr	w2, [x0, 2756]
	str	w2, [x0, 2592]
	ldr	w2, [x0, 2760]
	ldr	w1, [x0, 2768]
	str	w2, [x0, 2600]
	ldr	w2, [x0, 2764]
	str	w1, [x0, 2648]
	str	w2, [x0, 2644]
.L1248:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x0, 2632]
	ldrh	w1, [x0, 96]
	ldr	w3, [x0, 2628]
	madd	w1, w1, w3, w2
	ldrh	w2, [x0, 44]
	udiv	w1, w1, w2
	str	w1, [x0, 2636]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	sftl_vendor_read
	.type	sftl_vendor_read, %function
sftl_vendor_read:
	stp	x29, x30, [sp, -16]!
	add	w0, w0, 256
	add	x29, sp, 0
	bl	FtlVendorPartRead
	ldp	x29, x30, [sp], 16
	ret
	.size	sftl_vendor_read, .-sftl_vendor_read
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR0
	add	x19, x23, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	add	x20, x19, 2312
	add	x25, x19, 3400
	mov	w22, 0
	ldr	x21, [x19, 3440]
	mov	w24, 65535
	ldr	x0, [x19, 3432]
	ldrh	w1, [x19, 2656]
	str	x0, [x19, 3408]
	str	x21, [x19, 3416]
	strh	w1, [x21, 2]
	mov	w1, -3932
	strh	w1, [x21]
	str	wzr, [x21, 12]
	ldr	w1, [x19, 2664]
	stp	w1, wzr, [x21, 4]
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	str	w1, [x19, 2312]
	mov	w1, 68
	ldrb	w2, [x19, 2430]
	movk	w1, 0x5000, lsl 16
	str	w1, [x19, 2316]
	ldrh	w1, [x19, 2424]
	strh	w1, [x19, 2326]
	ldrh	w1, [x19, 2426]
	ldrh	w0, [x19, 2662]
	strh	w0, [x19, 2320]
	ldrh	w0, [x19, 58]
	orr	w1, w2, w1, lsl 6
	strb	w0, [x19, 2322]
	strh	w1, [x19, 2328]
	ldrh	w1, [x19, 2472]
	ldrb	w2, [x19, 2478]
	strh	w1, [x19, 2330]
	ldrh	w1, [x19, 2474]
	ldrb	w0, [x19, 2432]
	strb	w0, [x19, 2323]
	orr	w1, w2, w1, lsl 6
	ldrb	w0, [x19, 2480]
	strh	w1, [x19, 2332]
	strb	w0, [x19, 2324]
	ldrh	w1, [x19, 2520]
	ldrb	w0, [x19, 2528]
	ldrb	w2, [x19, 2526]
	strh	w1, [x19, 2334]
	strb	w0, [x19, 2325]
	ldrh	w1, [x19, 2522]
	ldr	w0, [x19, 2628]
	str	w0, [x19, 2344]
	ldr	w0, [x19, 2620]
	orr	w1, w2, w1, lsl 6
	str	w0, [x19, 2352]
	ldr	w0, [x19, 2624]
	ldrh	w2, [x19, 114]
	str	w0, [x19, 2348]
	ldr	x0, [x19, 3408]
	strh	w1, [x19, 2336]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x19, 3408]
	mov	x1, x20
	mov	w2, 48
	mov	x20, x25
	adrp	x25, .LANCHOR1
	add	x25, x25, :lo12:.LANCHOR1
	add	x25, x25, 536
	bl	ftl_memcpy
	ldr	x1, [x19, 2376]
	ldrh	w2, [x19, 44]
	ldr	x0, [x19, 3408]
	lsl	w2, w2, 1
	add	x0, x0, 48
	bl	ftl_memcpy
	ldrh	w0, [x19, 44]
	ldr	x3, [x19, 3408]
	ldr	x1, [x19, 24]
	lsr	w2, w0, 3
	ubfiz	x0, x0, 1, 16
	add	x0, x0, 48
	add	w2, w2, 4
	and	x0, x0, -4
	add	x0, x3, x0
	bl	ftl_memcpy
	mov	w0, 0
	bl	FtlUpdateVaildLpn
.L1253:
	ldr	x0, [x19, 3432]
	mov	w3, 1
	str	x0, [x20, 8]
	mov	w2, w3
	ldr	x0, [x19, 3440]
	ldrh	w1, [x19, 2658]
	str	x0, [x20, 16]
	ldrh	w0, [x19, 2656]
	orr	w0, w1, w0, lsl 10
	mov	w1, w3
	str	w0, [x20, 4]
	mov	x0, x20
	bl	FlashProgPages
	ldrh	w0, [x19, 108]
	ldrh	w1, [x19, 2658]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L1254
	ldrh	w0, [x19, 2656]
	ldrh	w24, [x19, 2660]
	strh	wzr, [x19, 2658]
	strh	w0, [x19, 2660]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19, 2656]
	ldr	w1, [x19, 2620]
	mov	w3, 1
	str	w1, [x19, 2664]
	add	w2, w1, 1
	str	w2, [x19, 2620]
	ubfiz	w2, w0, 10, 16
	str	w2, [x20, 4]
	strh	w0, [x21, 2]
	mov	w2, w3
	str	w1, [x21, 4]
	mov	x0, x20
	mov	w1, w3
	bl	FlashProgPages
.L1254:
	ldrh	w0, [x19, 2658]
	ldr	w1, [x20]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2658]
	cmn	w1, #1
	bne	.L1255
	cmp	w0, 1
	bne	.L1256
	adrp	x0, .LC1
	mov	w2, 1124
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1256:
	ldrh	w0, [x19, 2658]
	cmp	w0, 1
	bne	.L1257
	ldrh	w0, [x19, 108]
	sub	w0, w0, #1
	strh	w0, [x19, 2658]
.L1257:
	add	w22, w22, 1
	and	w22, w22, 65535
	cmp	w22, 3
	bls	.L1253
	add	x23, x23, :lo12:.LANCHOR0
	adrp	x0, .LC105
	mov	w2, w22
	add	x0, x0, :lo12:.LC105
	ldr	w1, [x23, 3404]
	bl	sftl_printk
.L1259:
	b	.L1259
.L1255:
	cmp	w0, 1
	beq	.L1253
	mov	w0, 65535
	cmp	w24, w0
	beq	.L1260
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
.L1260:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC95
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x1, 552
	add	x0, x0, :lo12:.LC95
	str	x23, [sp, 48]
	bl	sftl_printk
	ldr	x0, [x19, 4080]
	mov	w21, 0
	ldrh	w2, [x19, 46]
	mov	w1, 0
	lsl	w2, w2, 1
	bl	ftl_memset
.L1266:
	ldr	w0, [x19, 2584]
	cmp	w21, w0
	bcc	.L1268
	adrp	x22, .LC106
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC106
	mov	w19, 0
	mov	w23, 65535
.L1269:
	ldrh	w0, [x20, 44]
	cmp	w0, w19
	bhi	.L1273
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L1268:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1267
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x19, 4080]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L1267:
	add	w21, w21, 1
	b	.L1266
.L1273:
	ldr	x0, [x20, 2376]
	ubfiz	x21, x19, 1, 16
	ldrh	w2, [x0, x21]
	ldr	x0, [x20, 4080]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L1271
	cmp	w2, w23
	beq	.L1271
	mov	x0, x22
	mov	w1, w19
	bl	sftl_printk
	ldrh	w0, [x20, 2424]
	cmp	w0, w19
	beq	.L1271
	ldrh	w0, [x20, 2520]
	cmp	w0, w19
	beq	.L1271
	ldrh	w0, [x20, 2472]
	cmp	w0, w19
	beq	.L1271
	ldr	x0, [x20, 4080]
	ldrh	w1, [x0, x21]
	ldr	x0, [x20, 2376]
	strh	w1, [x0, x21]
	mov	w0, w19
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1271:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1269
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	ldrh	w21, [x0]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 44]
	cmp	w0, w21
	bcs	.L1279
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2681
	add	x1, x1, 576
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1279:
	mov	w0, 65535
	cmp	w21, w0
	beq	.L1280
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x21, 1, 16
	ldr	x1, [x1, 2376]
	ldrh	w0, [x1, x0]
	cbz	w0, .L1281
	mov	w0, w21
	bl	INSERT_DATA_LIST
.L1280:
	mov	w0, 1
	adrp	x1, .LANCHOR3
	strb	w0, [x22, 8]
	add	x0, x1, :lo12:.LANCHOR3
	mov	w2, 65535
	mov	x20, x1
	ldrh	w0, [x0, -48]
	cmp	w0, w2
	beq	.L1282
	cmp	w21, w0
	bne	.L1283
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x19, 2376]
	ldrh	w1, [x2, x1]
	cbz	w1, .L1284
.L1283:
	bl	update_vpc_list
.L1284:
	add	x1, x20, :lo12:.LANCHOR3
	mov	w0, -1
	strh	w0, [x1, -48]
.L1282:
	mov	x0, x22
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L1281:
	mov	w0, w21
	bl	INSERT_FREE_LIST
	b	.L1280
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.type	FtlProgPages, %function
FtlProgPages:
	stp	x29, x30, [sp, -96]!
	mov	w2, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x3
	stp	x21, x22, [sp, 32]
	mov	w21, w1
	stp	x23, x24, [sp, 48]
	ubfiz	x21, x21, 5, 32
	str	x25, [sp, 64]
	add	x21, x21, 4
	add	x24, x0, x21
	adrp	x22, .LANCHOR0
	ldrb	w3, [x3, 9]
	adrp	x25, .LANCHOR1
	add	x21, x22, :lo12:.LANCHOR0
	add	x25, x25, :lo12:.LANCHOR1
	add	x20, x0, 4
	bl	FlashProgPages
.L1290:
	cmp	x20, x24
	beq	.L1297
	sub	x23, x20, #4
	b	.L1298
.L1293:
	ldr	w0, [x20]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19]
	cmp	w1, w0, uxth
	bne	.L1291
	ldr	x2, [x21, 2376]
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x19, 4]
	ldrh	w0, [x2, x1]
	sub	w0, w0, w3
	strh	w0, [x2, x1]
	strb	wzr, [x19, 6]
	ldrh	w0, [x21, 106]
	strh	w0, [x19, 2]
	strh	wzr, [x19, 4]
.L1291:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1292
	mov	x0, x19
	bl	allocate_new_data_superblock
.L1292:
	ldr	w0, [x21, 2816]
	add	w0, w0, 1
	str	w0, [x21, 2816]
	ldr	w0, [x20]
	lsr	w0, w0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x20]
	mov	w2, 0
	str	w0, [x29, 92]
	mov	w1, 1
	ldrb	w3, [x19, 9]
	mov	x0, x23
	bl	FlashProgPages
.L1298:
	ldr	w0, [x20, -4]
	cmn	w0, #1
	beq	.L1293
	ldrb	w1, [x19, 6]
	ldrh	w0, [x21, 36]
	cmp	w1, w0
	bcc	.L1294
	adrp	x0, .LC1
	mov	w2, 933
	add	x1, x25, 608
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1294:
	add	x1, x29, 96
	ldr	w0, [x23, 4]
	mov	w2, 1
	str	w0, [x1, -4]!
	ldr	w0, [x23, 24]
	bl	log2phys
	ldr	x0, [x23, 16]
	ldr	w4, [x0, 12]
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	and	w1, w0, 65535
	mov	w23, w1
	cmn	w4, #1
	beq	.L1295
	ldr	x2, [x21, 2376]
	ubfiz	x0, x1, 1, 16
	ldrh	w0, [x2, x0]
	cbnz	w0, .L1296
	adrp	x0, .LC107
	mov	w2, 0
	add	x0, x0, :lo12:.LC107
	bl	sftl_printk
.L1296:
	mov	w0, w23
	bl	decrement_vpc_count
.L1295:
	add	x20, x20, 32
	b	.L1290
.L1297:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x22, 36]
	cmp	w1, w0
	bcc	.L1289
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 948
	add	x1, x1, 608
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1289:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlProgPages, .-FtlProgPages
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	ldr	x0, [x19, -56]
	cbz	x0, .L1305
	ldr	x3, [x19, 112]
	mov	w2, 0
	mov	w1, 1
	bl	FtlProgPages
	str	xzr, [x19, -56]
.L1305:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
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
	.global	sftl_deinit
	.type	sftl_deinit, %function
sftl_deinit:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	cmp	w0, 1
	bne	.L1315
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlSysFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1315:
	mov	w0, 0
	ret
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x22, x20, :lo12:.LANCHOR0
	mov	w21, w0
	mov	w19, w1
	add	w1, w0, w1
	ldr	w0, [x22, 144]
	cmp	w1, w0
	bhi	.L1326
	cmp	w19, 31
	bhi	.L1320
.L1334:
	mov	w0, 0
.L1318:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1320:
	bl	FtlCacheWriteBack
	ldrh	w1, [x22, 62]
	udiv	w22, w21, w1
	msub	w21, w1, w22, w21
	ands	w21, w21, 65535
	beq	.L1321
	sub	w1, w1, w21
	add	w22, w22, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L1321:
	adrp	x21, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, -1
	str	w0, [x29, 60]
.L1322:
	ldrh	w0, [x20, 62]
	cmp	w19, w0
	bcs	.L1324
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	w1, [x0, 120]
	cmp	w1, 32
	bls	.L1334
	str	wzr, [x0, 120]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1334
.L1324:
	mov	w2, 0
	add	x1, x29, 56
	mov	w0, w22
	bl	log2phys
	ldr	w0, [x29, 56]
	cmn	w0, #1
	beq	.L1323
	ldr	w0, [x21, 120]
	mov	w2, 1
	add	x1, x29, 60
	add	w0, w0, 1
	str	w0, [x21, 120]
	ldr	w0, [x20, 2592]
	add	w0, w0, 1
	str	w0, [x20, 2592]
	mov	w0, w22
	bl	log2phys
	ldr	w0, [x29, 56]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1323:
	ldrh	w0, [x20, 62]
	add	w22, w22, 1
	sub	w19, w19, w0
	b	.L1322
.L1326:
	mov	w0, -1
	b	.L1318
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	stp	x29, x30, [sp, -96]!
	mov	w3, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x2, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	ldrh	w20, [x2, 2520]
	ldrh	w1, [x2, 106]
	cmp	w20, w3
	bne	.L1336
.L1346:
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3-40
	add	x21, x20, 2520
	str	wzr, [x0, #:lo12:.LANCHOR3-40]
	mov	w0, 65535
	ldrh	w1, [x20, 2520]
	cmp	w1, w0
	bne	.L1359
.L1362:
	mov	w0, 0
.L1335:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1336:
	cbz	w0, .L1339
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w4, [x0, 4]
	cmp	w4, w3
	beq	.L1340
.L1341:
	mov	w1, 2
.L1339:
	add	x21, x19, :lo12:.LANCHOR0
	add	x0, x21, 2520
	bl	FtlGcScanTempBlk
	str	w0, [x29, 92]
	cmn	w0, #1
	beq	.L1342
	ldr	x1, [x21, 2384]
	ubfiz	x20, x20, 1, 16
	ldrh	w0, [x1, x20]
	cmp	w0, 4
	bls	.L1343
	sub	w0, w0, #5
	strh	w0, [x1, x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L1343:
	adrp	x0, .LANCHOR3
	add	x1, x0, :lo12:.LANCHOR3
	mov	x20, x0
	ldr	w1, [x1, -40]
	cbnz	w1, .L1344
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19, 2816]
	add	w0, w0, 1
	str	w0, [x19, 2816]
	ldr	w0, [x29, 92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1344:
	add	x0, x20, :lo12:.LANCHOR3
	str	wzr, [x0, -40]
.L1355:
	mov	w0, 1
	b	.L1335
.L1340:
	strh	wzr, [x0, 4]
	ldrh	w0, [x2, 2416]
	cmp	w0, 17
	bhi	.L1341
	b	.L1339
.L1342:
	adrp	x0, .LANCHOR2+4
	ldrh	w1, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1355
	b	.L1346
.L1359:
	bl	FtlCacheWriteBack
	ldrb	w0, [x21, 7]
	ldrh	w2, [x20, 106]
	ldrh	w1, [x20, 3304]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L1347
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 162
	add	x1, x1, 624
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1347:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR1
	add	x22, x22, :lo12:.LANCHOR1
	adrp	x25, .LC1
	add	x22, x22, 624
	add	x25, x25, :lo12:.LC1
	ldrb	w1, [x0, 2527]
	mov	w20, 0
	ldrh	w4, [x0, 106]
	ldr	x2, [x0, 2376]
	ldrh	w3, [x0, 2520]
	mul	w1, w1, w4
	strh	w1, [x2, x3, lsl 1]
	ldr	w2, [x0, 2608]
	ldrh	w1, [x0, 3304]
	add	w1, w1, w2
	str	w1, [x0, 2608]
.L1348:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21, 3304]
	cmp	w0, w20
	bhi	.L1352
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x21, 2520]
	ldr	x2, [x21, 2376]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1353
	bl	INSERT_DATA_LIST
.L1354:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, -1
	strh	wzr, [x19, 3304]
	strh	w20, [x19, 2520]
	strh	wzr, [x19, 3292]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldrh	w0, [x19, 2652]
	ldrh	w1, [x19, 2416]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	ble	.L1362
	mov	w0, 20
	strh	w20, [x19, 2672]
	strh	w0, [x19, 3232]
	b	.L1362
.L1352:
	mov	w24, 12
	ldr	x26, [x21, 3312]
	ldr	w0, [x21, 2584]
	umull	x24, w20, w24
	add	x23, x26, x24
	ldr	w1, [x23, 8]
	cmp	w1, w0
	bcc	.L1349
	mov	w2, 168
	mov	x1, x22
	mov	x0, x25
	bl	sftl_printk
.L1349:
	ldr	w0, [x23, 8]
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x26, x24]
	ldr	w1, [x29, 92]
	cmp	w0, w1
	bne	.L1350
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	mov	w21, w0
	ldr	w0, [x23, 8]
	mov	w2, 1
	add	x1, x23, 4
	bl	log2phys
	mov	w0, w21
.L1361:
	bl	decrement_vpc_count
.L1351:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1348
.L1350:
	ldr	w0, [x23, 4]
	cmp	w1, w0
	beq	.L1351
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2520]
	b	.L1361
.L1353:
	bl	INSERT_FREE_LIST
	b	.L1354
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 2520
	mov	x0, x20
	ldrh	w1, [x19, 106]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19, 2522]
	ldrh	w0, [x19, 106]
	cmp	w1, w0
	bcc	.L1363
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	sub	x0, x19, #192
	bl	FtlMapBlkWriteDump_data
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19, -40]
.L1363:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR3-28
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x0, #:lo12:.LANCHOR3-28]
	add	x20, x19, 2424
	add	x19, x19, 2472
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
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	stp	x29, x30, [sp, -32]!
	adrp	x11, .LANCHOR0
	mov	w3, 0
	mov	w2, 0
	add	x29, sp, 0
	str	x19, [sp, 16]
	add	x19, x11, :lo12:.LANCHOR0
	ldr	w1, [x19, 3248]
	ldr	x0, [x19, 4024]
	bl	FlashProgPages
	mov	w10, 0
.L1369:
	ldr	w1, [x19, 3248]
	cmp	w10, w1
	bcc	.L1372
	ldr	x0, [x19, 4024]
	bl	FtlGcBufFree
	str	wzr, [x19, 3248]
	ldrh	w0, [x19, 2524]
	cbnz	w0, .L1373
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	b	.L1375
.L1372:
	ldr	x0, [x19, 4024]
	ubfiz	x1, x10, 5, 16
	add	x3, x0, x1
	ldr	w0, [x0, x1]
	cmn	w0, #1
	bne	.L1370
	ldrh	w3, [x19, 2520]
	ldr	x2, [x19, 2376]
	strh	wzr, [x2, x3, lsl 1]
	strh	w0, [x19, 2520]
	ldr	w0, [x19, 2816]
	add	w0, w0, 1
	str	w0, [x19, 2816]
	ldr	x0, [x19, 4024]
	add	x0, x0, x1
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L1375:
	mov	w0, 1
.L1368:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1370:
	ldr	x0, [x3, 16]
	ldr	w1, [x3, 4]
	ldp	w2, w0, [x0, 8]
	bl	FtlGcUpdatePage
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L1369
.L1373:
	mov	w0, 0
	b	.L1368
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 2520
	ldrh	w2, [x0, 2520]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L1377
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1378
.L1377:
	bl	FtlCacheWriteBack
	add	x20, x19, :lo12:.LANCHOR0
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x20, 2520
	strb	wzr, [x0, 8]
	bl	allocate_data_superblock
	strh	wzr, [x20, 3292]
	strh	wzr, [x20, 3304]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1378:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2520
	bl	get_new_active_ppa
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w2, [x1, 4000]
	cbnz	w2, .L1452
	ldrh	w2, [x1, 2400]
	cmp	w2, 47
	bls	.L1452
	mov	w23, w0
	adrp	x0, .LANCHOR2+4
	ldrh	w2, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1382
.L1385:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 3322]
	cmp	w1, w2
	bne	.L1383
.L1384:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w20, 65535
	ldr	w1, [x21, 3236]
	ldrh	w0, [x21, 2672]
	add	w1, w1, 1
	add	w1, w1, w23, lsl 7
	str	w1, [x21, 3236]
	cmp	w0, w20
	bne	.L1386
	ldrh	w20, [x21, 2520]
	cmp	w20, w0
	bne	.L1455
	ldrh	w22, [x21, 3320]
	cmp	w22, w20
	bne	.L1386
	ldrh	w0, [x21, 2416]
	mov	w2, 1024
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L1458
	adrp	x0, .LANCHOR3+124
	str	wzr, [x21, 3236]
	strh	wzr, [x0, #:lo12:.LANCHOR3+124]
	bl	GetSwlReplaceBlock
	and	w20, w0, 65535
	cmp	w20, w22
	bne	.L1459
	ldrh	w1, [x21, 2416]
	ldrh	w0, [x21, 3234]
	cmp	w1, w0
	bcs	.L1389
	mov	w0, 64
	bl	List_get_gc_head_node
	and	w0, w0, 65535
	cmp	w0, w20
	beq	.L1391
	ldr	x1, [x21, 2376]
	ubfiz	x0, x0, 1, 16
	ldrh	w0, [x1, x0]
	cmp	w0, 7
	bhi	.L1392
	mov	w0, 0
	bl	List_get_gc_head_node
	and	w22, w0, 65535
	mov	w0, 128
	strh	w0, [x21, 3234]
	cmp	w22, w20
	bne	.L1388
.L1391:
	bl	FtlGcReFreshBadBlk
.L1386:
	cmp	w23, 0
	mov	w0, 65535
	ccmp	w20, w0, 0, eq
	bne	.L1394
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2416]
	cmp	w0, 24
	bhi	.L1460
	cmp	w0, 16
	ldrh	w21, [x1, 106]
	bls	.L1396
	lsr	w21, w21, 5
.L1395:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1, 3232]
	cmp	w2, w0
	bcs	.L1399
	ldrh	w0, [x1, 2520]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1400
	ldrh	w2, [x1, 3320]
	cmp	w2, w0
	bne	.L1400
	adrp	x0, .LANCHOR3+124
	ldrh	w0, [x0, #:lo12:.LANCHOR3+124]
	cbnz	w0, .L1401
	ldr	w2, [x1, 2584]
	ldr	w3, [x1, 2580]
	add	w2, w2, w2, lsl 1
	cmp	w3, w2, lsr 2
	bcs	.L1402
.L1401:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2, 2652]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2, 3232]
.L1403:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19, 3244]
.L1380:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1382:
	ldrh	w1, [x1, 2520]
	cmp	w1, w0
	beq	.L1385
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	cbz	w0, .L1385
	mov	w0, 1
	b	.L1380
.L1383:
	ldrh	w3, [x0, 3320]
	cmp	w3, w2
	bne	.L1384
	strh	w1, [x0, 3320]
	mov	w1, -1
	strh	w1, [x0, 3322]
	b	.L1384
.L1392:
	mov	w0, 64
.L1484:
	strh	w0, [x21, 3234]
	b	.L1391
.L1389:
	mov	w0, 80
	b	.L1484
.L1459:
	mov	w22, w20
.L1388:
	add	x0, x19, :lo12:.LANCHOR0
	ubfiz	x1, x22, 1, 32
	mov	w20, w22
	ldr	x2, [x0, 2376]
	ldr	x3, [x0, 2384]
	ldrh	w5, [x0, 3232]
	ldrh	w4, [x3, x1]
	ldrh	w3, [x2, x1]
	mov	w1, w22
	ldrh	w2, [x0, 2416]
	adrp	x0, .LC108
	add	x0, x0, :lo12:.LC108
	bl	sftl_printk
	b	.L1391
.L1455:
	mov	w20, w0
	b	.L1386
.L1458:
	mov	w20, w22
	b	.L1386
.L1396:
	cmp	w0, 12
	bls	.L1397
	lsr	w21, w21, 4
	b	.L1395
.L1397:
	mov	w1, w21
	cmp	w0, 9
	lsr	w21, w21, 2
	csel	w21, w21, w1, cs
	b	.L1395
.L1460:
	mov	w21, 1
	b	.L1395
.L1402:
	mov	w2, 18
	strh	w2, [x1, 3232]
	b	.L1403
.L1400:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2652]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 3232]
.L1399:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 3324]
	cbz	w0, .L1461
	add	w21, w21, 32
	and	w21, w21, 65535
.L1461:
	mov	w20, 65535
.L1405:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 2672]
	cmp	w1, w2
	bne	.L1414
	cmp	w20, w1
	beq	.L1415
	strh	w20, [x0, 2672]
.L1416:
	add	x5, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldrh	w0, [x5, 2672]
	strb	wzr, [x5, 2680]
	cmp	w0, w1
	beq	.L1414
	bl	IsBlkInGcList
	cbz	w0, .L1419
	mov	w0, -1
	strh	w0, [x5, 2672]
.L1419:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	add	x23, x22, 2672
	ldrh	w1, [x22, 2672]
	cmp	w1, w0
	beq	.L1414
	mov	x0, x23
	bl	make_superblock
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrh	w2, [x22, 2672]
	ldr	x1, [x22, 2376]
	strh	wzr, [x22, 2674]
	strb	wzr, [x22, 2678]
	strh	wzr, [x0, 126]
	ldrh	w1, [x1, x2, lsl 1]
	strh	w1, [x0, 128]
.L1414:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w2, [x0, 2424]
	cmp	w2, w1
	beq	.L1420
	ldrh	w0, [x0, 2472]
	cmp	w0, w1
	beq	.L1420
	adrp	x24, .LANCHOR1
	add	x24, x24, :lo12:.LANCHOR1
	add	x24, x24, 648
.L1421:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w23, [x22, 2672]
	cmp	w23, w0
	bne	.L1422
	mov	w25, 2
	str	wzr, [x22, 3244]
.L1423:
	ldrh	w5, [x22, 3240]
	mov	w0, w5
	bl	List_get_gc_head_node
	and	w6, w0, 65535
	strh	w6, [x22, 2672]
	cmp	w6, w23
	bne	.L1424
	strh	wzr, [x22, 3240]
	mov	w0, 8
	b	.L1380
.L1394:
	add	x5, x19, :lo12:.LANCHOR0
	ldrh	w1, [x5, 2520]
	cmp	w1, w0
	bne	.L1464
	ldrh	w0, [x5, 3320]
	cmp	w0, w1
	bne	.L1464
	cmp	w20, w0
	bne	.L1464
	ldrh	w0, [x5, 2672]
	cmp	w0, w20
	beq	.L1406
.L1464:
	mov	w21, 1
	b	.L1405
.L1406:
	ldrh	w7, [x5, 2416]
	adrp	x21, .LANCHOR3
	ldrh	w0, [x5, 3232]
	str	wzr, [x5, 3244]
	cmp	w0, w7
	bcs	.L1407
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, 124]
	cbnz	w0, .L1408
	ldr	w0, [x5, 2584]
	ldr	w1, [x5, 2580]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bcs	.L1409
.L1408:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2652]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x19, 3232]
.L1410:
	bl	FtlReadRefresh
.L1485:
	add	x21, x21, :lo12:.LANCHOR3
	b	.L1486
.L1409:
	mov	w0, 18
	strh	w0, [x5, 3232]
	b	.L1410
.L1407:
	add	x21, x21, :lo12:.LANCHOR3
	ldrh	w0, [x21, 124]
	cbnz	w0, .L1464
	ldrh	w6, [x5, 2652]
	add	w0, w6, w6, lsl 1
	asr	w0, w0, 2
	strh	w0, [x5, 3232]
	mov	w0, 0
	bl	List_get_gc_head_node
	ldr	x1, [x5, 2376]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x5, 36]
	ldrh	w1, [x1, x0]
	ldrh	w0, [x5, 108]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L1412
	sub	w6, w6, #1
	cmp	w7, w6
	blt	.L1412
	bl	FtlReadRefresh
.L1486:
	ldrh	w0, [x21, 124]
	b	.L1380
.L1412:
	cbnz	w1, .L1464
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x19, 2416]
	add	w0, w0, 1
	b	.L1380
.L1415:
	ldrh	w1, [x0, 3320]
	cmp	w1, w20
	beq	.L1416
	ldr	x2, [x0, 2376]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2, x1]
	cbnz	w1, .L1417
	mov	w1, -1
	strh	w1, [x0, 3320]
.L1417:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3320]
	strh	w1, [x0, 2672]
	mov	w1, -1
	strh	w1, [x0, 3320]
	b	.L1416
.L1420:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19, 2672]
.L1487:
	adrp	x0, .LANCHOR3+124
	ldrh	w0, [x0, #:lo12:.LANCHOR3+124]
	b	.L1380
.L1424:
	mov	w0, w6
	bl	IsBlkInGcList
	add	w5, w5, 1
	cbz	w0, .L1425
	strh	w5, [x22, 3240]
	b	.L1423
.L1425:
	ldrh	w4, [x22, 36]
	ubfiz	x1, x6, 1, 16
	ldrh	w0, [x22, 106]
	and	w5, w5, 65535
	ldr	x2, [x22, 2376]
	strh	w5, [x22, 3240]
	mul	w0, w0, w4
	ldrh	w3, [x2, x1]
	sdiv	w4, w0, w25
	cmp	w3, w4
	bgt	.L1427
	cmp	w5, 48
	bls	.L1428
	cmp	w3, 8
	bls	.L1428
	ldrh	w3, [x22, 3292]
	cmp	w3, 35
	bhi	.L1428
.L1427:
	strh	wzr, [x22, 3240]
.L1428:
	ldrh	w1, [x2, x1]
	cmp	w0, w1
	bgt	.L1429
	cmp	w20, w23
	bne	.L1429
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19, 3240]
	strh	w0, [x19, 2672]
	b	.L1487
.L1429:
	cbnz	w1, .L1430
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x22, 3240]
	add	w0, w0, 1
	strh	w0, [x22, 3240]
	b	.L1423
.L1430:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0, 2680]
	ldrh	w0, [x0, 2424]
	cmp	w0, w6
	bne	.L1431
	adrp	x0, .LC1
	mov	w2, 658
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1431:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w0, [x0, 2472]
	cmp	w1, w0
	bne	.L1432
	adrp	x0, .LC1
	mov	w2, 659
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1432:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w0, [x0, 2520]
	cmp	w1, w0
	bne	.L1433
	adrp	x0, .LC1
	mov	w2, 660
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1433:
	add	x23, x19, :lo12:.LANCHOR0
	add	x22, x23, 2672
	mov	x0, x22
	bl	make_superblock
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrh	w2, [x23, 2672]
	ldr	x1, [x23, 2376]
	strh	wzr, [x0, 126]
	ldrh	w1, [x1, x2, lsl 1]
	strh	w1, [x0, 128]
	strh	wzr, [x23, 2674]
	strb	wzr, [x23, 2678]
.L1422:
	bl	FtlReadRefresh
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	ldrh	w26, [x0, 106]
	str	w1, [x0, 4000]
	ldrh	w0, [x0, 2674]
	add	w1, w0, w21
	cmp	w1, w26
	ble	.L1434
	sub	w21, w26, w0
	and	w21, w21, 65535
.L1434:
	mov	w25, 0
.L1435:
	cmp	w21, w25, uxth
	bls	.L1443
	add	x0, x19, :lo12:.LANCHOR0
	mov	w23, 0
	add	x3, x0, 2688
	mov	w1, 0
	mov	w6, 65535
	ldrh	w4, [x0, 2674]
	ldrh	w7, [x0, 36]
	add	w4, w4, w25
	b	.L1444
.L1437:
	ldrh	w2, [x3]
	cmp	w2, w6
	beq	.L1436
	ldr	x5, [x0, 3280]
	ubfiz	x8, x23, 5, 16
	add	w23, w23, 1
	orr	w2, w4, w2, lsl 10
	add	x5, x5, x8
	and	w23, w23, 65535
	str	w2, [x5, 4]
.L1436:
	add	w1, w1, 1
	add	x3, x3, 2
	and	w1, w1, 65535
.L1444:
	cmp	w1, w7
	bne	.L1437
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, w23
	mov	x27, 0
	ldrb	w2, [x22, 2680]
	ldr	x0, [x22, 3280]
	bl	FlashReadPages
	ubfiz	x0, x23, 5, 16
	str	x0, [x29, 104]
.L1438:
	ldr	x0, [x29, 104]
	cmp	x27, x0
	bne	.L1442
	add	w25, w25, 1
	b	.L1435
.L1442:
	ldr	x0, [x22, 3280]
	add	x1, x0, x27
	ldr	w0, [x0, x27]
	cmn	w0, #1
	beq	.L1439
	ldr	x23, [x1, 16]
	mov	w0, 61589
	ldrh	w1, [x23]
	cmp	w1, w0
	bne	.L1439
	ldr	w28, [x23, 8]
	cmn	w28, #1
	bne	.L1440
	adrp	x0, .LC1
	mov	w2, 696
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1440:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w28
	bl	log2phys
	ldr	x0, [x22, 3280]
	ldr	w1, [x29, 124]
	add	x0, x0, x27
	ldr	w2, [x0, 4]
	cmp	w2, w1
	bne	.L1439
	adrp	x28, .LANCHOR3
	add	x2, x28, :lo12:.LANCHOR3
	ldr	w0, [x0, 24]
	ldrh	w1, [x2, 126]
	add	w1, w1, 1
	strh	w1, [x2, 126]
	ldr	x2, [x22, 4024]
	ldr	w1, [x22, 3248]
	add	x1, x2, x1, lsl 5
	str	x1, [x29, 96]
	str	w0, [x1, 24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29, 96]
	str	w0, [x1, 4]
	ldr	w0, [x22, 3248]
	ldr	x1, [x22, 4024]
	add	x0, x1, x0, lsl 5
	ldr	x1, [x22, 3280]
	add	x1, x1, x27
	ldr	x2, [x1, 8]
	str	x2, [x0, 8]
	ldr	x1, [x1, 16]
	str	x1, [x0, 16]
	ldr	w0, [x29, 124]
	mov	w1, 1
	str	w0, [x23, 12]
	ldrh	w0, [x22, 2520]
	strh	w0, [x23, 2]
	ldr	w0, [x22, 2624]
	str	w0, [x23, 4]
	ldr	w0, [x22, 3248]
	add	w0, w0, 1
	str	w0, [x22, 3248]
	ldr	x0, [x22, 3280]
	add	x0, x0, x27
	bl	FtlGcBufAlloc
	ldrb	w1, [x22, 2527]
	ldr	w0, [x22, 3248]
	cmp	w1, w0
	beq	.L1441
	ldrh	w0, [x22, 2524]
	cbnz	w0, .L1439
.L1441:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1439
	add	x19, x19, :lo12:.LANCHOR0
	add	x28, x28, :lo12:.LANCHOR3
	str	wzr, [x19, 4000]
	ldrh	w0, [x28, 124]
	b	.L1380
.L1439:
	add	x27, x27, 32
	b	.L1438
.L1443:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2674]
	add	w21, w21, w0
	and	w21, w21, 65535
	strh	w21, [x1, 2674]
	cmp	w26, w21
	adrp	x21, .LANCHOR3
	bls	.L1445
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w1, [x0, 126]
	ldrh	w0, [x0, 128]
	cmp	w1, w0
	bne	.L1446
.L1445:
	add	x22, x19, :lo12:.LANCHOR0
	ldr	w0, [x22, 3248]
	cbz	w0, .L1447
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1447
	str	wzr, [x22, 4000]
	b	.L1485
.L1447:
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, 126]
	cbnz	w0, .L1448
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2672]
	ldr	x2, [x1, 2376]
	lsl	x0, x0, 1
	ldrh	w3, [x2, x0]
	cbz	w3, .L1448
	strh	wzr, [x2, x0]
	ldrh	w0, [x1, 2672]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1448:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 2672]
.L1446:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2416]
	cmp	w0, 2
	bhi	.L1449
	ldrh	w21, [x1, 106]
	b	.L1421
.L1449:
	add	x21, x21, :lo12:.LANCHOR3
	str	wzr, [x1, 4000]
	ldrh	w1, [x21, 124]
	cmp	w1, 0
	csinc	w0, w1, w0, ne
	b	.L1380
.L1452:
	mov	w0, 0
	b	.L1380
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	sftl_gc
	.type	sftl_gc, %function
sftl_gc:
	stp	x29, x30, [sp, -16]!
	mov	w1, 1
	mov	w0, w1
	add	x29, sp, 0
	bl	rk_ftl_garbage_collect
	ldp	x29, x30, [sp], 16
	ret
	.size	sftl_gc, .-sftl_gc
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	stp	x29, x30, [sp, -160]!
	and	w0, w0, 255
	cmp	w0, 16
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w1
	stp	x23, x24, [sp, 48]
	mov	x23, x3
	stp	x27, x28, [sp, 80]
	mov	w27, w2
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	bne	.L1491
	mov	x2, x3
	mov	w1, w27
	add	w0, w19, 256
	bl	FtlVendorPartRead
	mov	w21, w0
.L1490:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1491:
	add	w0, w1, w2
	str	w0, [x29, 128]
	adrp	x0, .LANCHOR0
	add	x25, x0, :lo12:.LANCHOR0
	add	w2, w1, w2
	str	x0, [x29, 96]
	ldr	w1, [x25, 144]
	cmp	w2, w1
	bhi	.L1511
	adrp	x0, .LANCHOR2
	ldr	w21, [x0, #:lo12:.LANCHOR2]
	cmn	w21, #1
	beq	.L1490
	bl	FtlCacheWriteBack
	mov	w28, 0
	ldrh	w0, [x25, 62]
	add	w1, w19, w27
	sub	w24, w1, #1
	mov	w21, 0
	stp	wzr, wzr, [x29, 132]
	str	wzr, [x29, 140]
	udiv	w26, w19, w0
	udiv	w24, w24, w0
	ldr	w0, [x25, 2588]
	mov	w20, w26
	sub	w22, w24, w26
	add	w22, w22, 1
	add	w0, w0, w22
	str	w0, [x25, 2588]
.L1493:
	cbnz	w22, .L1509
	ldr	x0, [x29, 96]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3324]
	cbnz	w1, .L1510
	ldrh	w0, [x0, 2416]
	cmp	w0, 31
	bhi	.L1490
.L1510:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	b	.L1490
.L1509:
	add	x1, x29, 156
	mov	w2, 0
	mov	w0, w20
	bl	log2phys
	ldr	w1, [x29, 156]
	cmn	w1, #1
	bne	.L1494
	mov	w5, 0
.L1495:
	ldrh	w0, [x25, 62]
	cmp	w5, w0
	bcc	.L1497
.L1498:
	add	w20, w20, 1
	subs	w22, w22, #1
	beq	.L1502
	ldrh	w0, [x25, 36]
	cmp	w28, w0, lsl 2
	bne	.L1493
.L1502:
	cbz	w28, .L1493
	ldr	x0, [x25, 4016]
	mov	w1, w28
	mov	w2, 0
	bl	FlashReadPages
	ldr	w0, [x29, 132]
	lsl	w0, w0, 9
	str	w0, [x29, 112]
	ldr	w0, [x29, 140]
	lsl	w0, w0, 9
	str	x0, [x29, 120]
	ldr	w0, [x29, 136]
	lsl	w0, w0, 9
	str	w0, [x29, 116]
	ubfiz	x0, x28, 5, 32
	mov	x28, 0
	str	x0, [x29, 104]
.L1508:
	ldr	x0, [x25, 4016]
	add	x0, x0, x28
	ldr	w1, [x0, 24]
	cmp	w26, w1
	bne	.L1504
	ldr	x1, [x0, 8]
	ldr	x0, [x25, 3992]
	cmp	x1, x0
	bne	.L1505
	ldr	x0, [x29, 120]
	ldr	w2, [x29, 116]
	add	x1, x1, x0
	mov	x0, x23
.L1521:
	bl	ftl_memcpy
.L1505:
	ldr	x0, [x25, 4016]
	add	x1, x0, x28
	ldr	w2, [x0, x28]
	cmn	w2, #1
	bne	.L1506
	ldr	w0, [x25, 2792]
	mov	w21, w2
	add	w0, w0, 1
	str	w0, [x25, 2792]
.L1506:
	ldr	w0, [x1]
	cmp	w0, 256
	bne	.L1507
	ldr	w0, [x1, 4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1507:
	ldr	x0, [x29, 104]
	add	x28, x28, 32
	cmp	x0, x28
	bne	.L1508
	mov	w28, 0
	b	.L1493
.L1497:
	madd	w0, w20, w0, w5
	cmp	w19, w0
	bhi	.L1496
	ldr	w1, [x29, 128]
	cmp	w1, w0
	bls	.L1496
	sub	w0, w0, w19
	str	w5, [x29, 120]
	lsl	w0, w0, 9
	mov	w2, 512
	mov	w1, 0
	add	x0, x23, x0
	bl	ftl_memset
	ldr	w5, [x29, 120]
.L1496:
	add	w5, w5, 1
	b	.L1495
.L1494:
	ldr	x0, [x25, 4016]
	ubfiz	x2, x28, 5, 32
	cmp	w20, w26
	add	x0, x0, x2
	str	w1, [x0, 4]
	ldrh	w0, [x25, 62]
	bne	.L1499
	ldr	x1, [x25, 4016]
	ldr	x5, [x25, 3992]
	add	x1, x1, x2
	str	x5, [x1, 8]
	udiv	w5, w19, w0
	msub	w3, w5, w0, w19
	str	w3, [x29, 140]
	sub	w5, w0, w3
	cmp	w27, w5
	csel	w3, w27, w5, ls
	str	w3, [x29, 136]
	cmp	w3, w0
	bne	.L1500
	str	x23, [x1, 8]
.L1500:
	ldr	x0, [x25, 4016]
	ldr	x1, [x25, 3984]
	add	x2, x0, x2
	ldrh	w0, [x25, 116]
	str	w20, [x2, 24]
	mul	w0, w0, w28
	add	w28, w28, 1
	and	x0, x0, 4294967292
	add	x0, x1, x0
	str	x0, [x2, 16]
	b	.L1498
.L1499:
	cmp	w20, w24
	bne	.L1501
	ldr	x5, [x25, 4016]
	ldr	x1, [x25, 3976]
	add	x5, x5, x2
	ldr	w3, [x29, 128]
	str	x1, [x5, 8]
	mul	w1, w20, w0
	sub	w3, w3, w1
	str	w3, [x29, 132]
	cmp	w0, w3
	bne	.L1500
	sub	w1, w1, w19
	lsl	w1, w1, 9
	add	x1, x23, x1
	str	x1, [x5, 8]
	b	.L1500
.L1501:
	ldr	x1, [x25, 4016]
	mul	w0, w0, w20
	add	x1, x1, x2
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	str	x0, [x1, 8]
	b	.L1500
.L1504:
	cmp	w24, w1
	bne	.L1505
	ldr	x1, [x0, 8]
	ldr	x0, [x25, 3976]
	cmp	x1, x0
	bne	.L1505
	ldrh	w0, [x25, 62]
	ldr	w2, [x29, 112]
	mul	w0, w0, w24
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	b	.L1521
.L1511:
	mov	w21, -1
	b	.L1490
	.size	FtlRead, .-FtlRead
	.align	2
	.global	sftl_read
	.type	sftl_read, %function
sftl_read:
	stp	x29, x30, [sp, -16]!
	mov	x3, x2
	mov	w2, w1
	mov	w1, w0
	add	x29, sp, 0
	mov	w0, 0
	bl	FtlRead
	ldp	x29, x30, [sp], 16
	ret
	.size	sftl_read, .-sftl_read
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	stp	x29, x30, [sp, -224]!
	and	w0, w0, 255
	cmp	w0, 16
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, w1
	stp	x25, x26, [sp, 64]
	mov	w26, w2
	stp	x27, x28, [sp, 80]
	mov	x27, x3
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	bne	.L1525
	mov	x2, x3
	mov	w1, w26
	add	w0, w23, 256
	bl	FtlVendorPartWrite
.L1524:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	ret
.L1525:
	add	w3, w1, w2
	adrp	x20, .LANCHOR0
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w0, [x2, 144]
	cmp	w3, w0
	bhi	.L1567
	adrp	x1, .LANCHOR2
	str	x1, [x29, 136]
	ldr	w0, [x1, #:lo12:.LANCHOR2]
	cmn	w0, #1
	beq	.L1524
	ldrh	w1, [x2, 62]
	adrp	x4, .LANCHOR3
	sub	w3, w3, #1
	add	x5, x4, :lo12:.LANCHOR3
	mov	w0, 2048
	str	x4, [x29, 104]
	udiv	w22, w23, w1
	str	w0, [x5, 132]
	udiv	w0, w3, w1
	ldr	x3, [x5, -56]
	sub	w24, w0, w22
	str	w0, [x29, 148]
	ldr	w0, [x2, 2596]
	add	w25, w24, 1
	add	w0, w0, w25
	str	w0, [x2, 2596]
	cbz	x3, .L1527
	ldr	w0, [x3, 24]
	cmp	w22, w0
	beq	.L1528
	bl	FtlCacheWriteBack
.L1527:
	ldr	x0, [x29, 136]
	add	x19, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	cbz	w0, .L1569
	ldrh	w1, [x19, 2428]
	add	x0, x19, 2424
	add	x19, x19, 2472
	cmp	w1, 0
	csel	x19, x19, x0, eq
.L1530:
	adrp	x0, .LANCHOR1
	mov	w21, w22
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 672
	str	x0, [x29, 160]
.L1531:
	cbz	w25, .L1565
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1532
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1012
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1532:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1533
	bl	FtlCacheWriteBack
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, 2424
	cmp	x19, x1
	bne	.L1534
	add	x0, x0, 2472
	ldrh	w1, [x0, 4]
	cbnz	w1, .L1535
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
.L1535:
	add	x19, x20, :lo12:.LANCHOR0
	add	x24, x19, 2424
	add	x19, x19, 2472
	mov	x0, x24
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	cmp	w0, 0
	csel	x19, x19, x24, ne
.L1536:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1533
	mov	x0, x19
	bl	allocate_new_data_superblock
.L1533:
	ldrh	w0, [x19, 4]
	ldrb	w1, [x19, 7]
	cmp	w0, w25
	csel	w0, w0, w25, ls
	lsl	w1, w1, 2
	cmp	w0, w1
	csel	w0, w0, w1, ls
	str	w0, [x29, 132]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1537
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1045
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1537:
	str	xzr, [x29, 168]
.L1538:
	ldr	w1, [x29, 132]
	ldr	w28, [x29, 168]
	cmp	w28, w1
	bcc	.L1559
	mov	x28, x1
.L1539:
	ldr	x0, [x29, 104]
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x0, [x0, -56]
	cbz	x0, .L1560
	sub	w25, w25, #1
	subs	w28, w28, #1
	bne	.L1560
.L1565:
	ldr	w0, [x29, 148]
	add	x20, x20, :lo12:.LANCHOR0
	sub	w1, w0, w22
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x20, 2416]
	cmp	w0, 15
	bhi	.L1562
	mov	w21, 65535
	mov	w19, 128
.L1585:
	ldrh	w0, [x20, 2672]
	cmp	w0, w21
	bne	.L1566
	ldrh	w0, [x20, 3320]
	cmp	w0, w21
	bne	.L1566
	mov	w0, 0
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L1566:
	mov	w1, 1
	strh	w19, [x20, 3234]
	mov	w0, w1
	strh	w19, [x20, 3232]
	bl	rk_ftl_garbage_collect
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x20, 2416]
	cmp	w0, 8
	bls	.L1585
	b	.L1562
.L1528:
	ldr	w0, [x2, 2600]
	ldr	x3, [x3, 8]
	add	w0, w0, 1
	str	w0, [x2, 2600]
	msub	w0, w22, w1, w23
	sub	w1, w1, w0
	cmp	w26, w1
	lsl	w0, w0, 9
	csel	w19, w26, w1, ls
	add	x0, x3, x0
	lsl	w21, w19, 9
	mov	x1, x27
	mov	w2, w21
	bl	ftl_memcpy
	cbnz	w24, .L1529
.L1562:
	mov	w0, 0
	b	.L1524
.L1529:
	sub	w26, w26, w19
	add	w23, w23, w19
	add	x27, x27, x21
	add	w22, w22, 1
	bl	FtlCacheWriteBack
	mov	w25, w24
	b	.L1527
.L1569:
	add	x19, x19, 2424
	b	.L1530
.L1534:
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1572
	mov	x0, x19
	bl	allocate_new_data_superblock
	b	.L1536
.L1572:
	mov	x19, x1
	b	.L1533
.L1559:
	ldrh	w0, [x19, 4]
	cbz	w0, .L1539
	add	x1, x29, 188
	mov	w2, 0
	mov	w0, w21
	bl	log2phys
	mov	x0, x19
	bl	get_new_active_ppa
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x1, [x29, 168]
	str	x3, [x29, 152]
	lsl	x24, x1, 5
	ldr	x1, [x3, 4032]
	ldrh	w2, [x3, 116]
	add	x1, x1, x24
	str	w0, [x1, 4]
	mul	w28, w28, w2
	ldr	x0, [x3, 4032]
	and	x1, x28, 4294967292
	str	x1, [x29, 120]
	add	x0, x0, x24
	ldr	x1, [x3, 3984]
	ldr	x3, [x29, 120]
	str	x1, [x29, 112]
	add	x28, x1, x3
	str	w21, [x0, 24]
	str	x28, [x0, 16]
	mov	w1, 0
	mov	x0, x28
	bl	ftl_memset
	ldr	w0, [x29, 148]
	cmp	w21, w22
	ldr	x3, [x29, 152]
	ccmp	w21, w0, 4, ne
	bne	.L1540
	cmp	w21, w22
	bne	.L1541
	ldrh	w2, [x3, 62]
	udiv	w0, w23, w2
	msub	w0, w0, w2, w23
	str	w0, [x29, 144]
	sub	w2, w2, w0
	cmp	w2, w26
	csel	w0, w2, w26, ls
	str	w0, [x29, 152]
.L1542:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x29, 152]
	ldrh	w1, [x0, 62]
	cmp	w2, w1
	bne	.L1543
	ldr	x0, [x0, 4032]
	cmp	w21, w22
	add	x24, x0, x24
	bne	.L1544
	str	x27, [x24, 8]
.L1545:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1556
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1119
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1556:
	ldp	x1, x2, [x29, 112]
	mov	w0, -3947
	strh	w0, [x1, x2]
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x1, 2624]
	stp	w0, w21, [x28, 4]
	add	w21, w21, 1
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1, 2624]
	ldr	w0, [x29, 188]
	str	w0, [x28, 12]
	ldrh	w0, [x19]
	strh	w0, [x28, 2]
	ldr	x0, [x29, 168]
	add	x0, x0, 1
	str	x0, [x29, 168]
	b	.L1538
.L1541:
	ldrh	w0, [x3, 62]
	add	w2, w23, w26
	str	wzr, [x29, 144]
	msub	w2, w21, w0, w2
	and	w0, w2, 65535
	str	w0, [x29, 152]
	b	.L1542
.L1544:
	ldr	w0, [x29, 152]
.L1588:
	mul	w0, w0, w21
	sub	w0, w0, w23
	lsl	w0, w0, 9
	add	x0, x27, x0
	str	x0, [x24, 8]
	b	.L1545
.L1543:
	ldr	x1, [x0, 4032]
	cmp	w21, w22
	add	x1, x1, x24
	bne	.L1546
	ldr	x0, [x0, 3992]
.L1587:
	str	x0, [x1, 8]
	ldr	w0, [x29, 188]
	cmn	w0, #1
	beq	.L1548
	add	x3, x20, :lo12:.LANCHOR0
	str	w0, [x29, 196]
	str	w21, [x29, 216]
	mov	w2, 0
	str	x3, [x29, 96]
	ldr	x0, [x3, 4032]
	add	x0, x0, x24
	ldp	x1, x0, [x0, 8]
	stp	x1, x0, [x29, 200]
	mov	w1, 1
	add	x0, x29, 192
	bl	FlashReadPages
	ldr	w0, [x29, 192]
	ldr	x3, [x29, 96]
	cmn	w0, #1
	bne	.L1549
	ldr	w0, [x3, 2792]
	add	w0, w0, 1
	str	w0, [x3, 2792]
.L1552:
	ldr	w0, [x29, 152]
	cmp	w21, w22
	lsl	w2, w0, 9
	add	x0, x20, :lo12:.LANCHOR0
	bne	.L1553
	ldr	x1, [x0, 4032]
	ldr	w0, [x29, 144]
	add	x1, x1, x24
	lsl	w0, w0, 9
	ldr	x3, [x1, 8]
	mov	x1, x27
	add	x0, x3, x0
	bl	ftl_memcpy
	ldr	w0, [x29, 148]
	cmp	w21, w0
	bne	.L1545
.L1554:
	ldrh	w0, [x19, 4]
	cbz	w0, .L1545
	ldr	x0, [x29, 104]
	add	x1, x0, :lo12:.LANCHOR3
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0, 4032]
	str	x19, [x1, 112]
	add	x24, x0, x24
	str	x24, [x1, -56]
	b	.L1545
.L1546:
	ldr	x0, [x0, 3976]
	b	.L1587
.L1549:
	ldr	w0, [x28, 8]
	cmp	w21, w0
	beq	.L1551
	ldr	w0, [x3, 2792]
	mov	w2, w21
	add	w0, w0, 1
	str	w0, [x3, 2792]
	adrp	x0, .LC109
	add	x0, x0, :lo12:.LC109
	ldr	w1, [x28, 8]
	bl	sftl_printk
.L1551:
	ldr	w0, [x28, 8]
	cmp	w21, w0
	beq	.L1552
	ldr	x1, [x29, 160]
	mov	w2, 1098
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L1552
.L1548:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1, 4032]
	ldrh	w2, [x1, 114]
	mov	w1, 0
	add	x0, x0, x24
	ldr	x0, [x0, 8]
	bl	ftl_memset
	b	.L1552
.L1553:
	ldrh	w1, [x0, 62]
	ldr	x0, [x0, 4032]
	add	x0, x0, x24
	mul	w1, w1, w21
	ldr	x0, [x0, 8]
	sub	w1, w1, w23
	lsl	w1, w1, 9
	add	x1, x27, x1
	bl	ftl_memcpy
	b	.L1554
.L1540:
	ldr	x0, [x3, 4032]
	add	x24, x0, x24
	ldrh	w0, [x3, 62]
	b	.L1588
.L1560:
	add	x0, x20, :lo12:.LANCHOR0
	mov	x3, x19
	mov	w2, 0
	mov	w1, w28
	ldr	x0, [x0, 4032]
	bl	FtlProgPages
	cmp	w25, w28
	bcs	.L1563
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1134
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1563:
	sub	w25, w25, w28
	b	.L1531
.L1567:
	mov	w0, -1
	b	.L1524
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_write
	.type	sftl_write, %function
sftl_write:
	stp	x29, x30, [sp, -16]!
	mov	x3, x2
	mov	w2, w1
	mov	w1, w0
	add	x29, sp, 0
	mov	w0, 0
	bl	FtlWrite
	ldp	x29, x30, [sp], 16
	ret
	.size	sftl_write, .-sftl_write
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	stp	x29, x30, [sp, -64]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x21, x19, 3400
	stp	x23, x24, [sp, 48]
	ldr	x0, [x19, 3432]
	str	x0, [x21, 8]
	ldr	x0, [x19, 3440]
	str	x0, [x21, 16]
	ldr	x0, [x19, 2376]
	ldrh	w2, [x19, 44]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x19, 2656]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1592
.L1600:
	mov	w0, -1
.L1591:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1592:
	add	x23, x19, 2656
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w22, w0
	add	w0, w0, 1
	mov	w24, 61604
	strh	w0, [x23, 2]
	mov	w23, 19539
	movk	w23, 0x4654, lsl 16
.L1594:
	tbz	w22, #31, .L1597
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1437
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1596:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x1, 44]
	ldrh	w1, [x1, 114]
	add	x0, x0, 24
	cmp	x1, x0, lsl 1
	bcs	.L1599
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1439
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1599:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w2, 48
	add	x21, x19, 2312
	mov	x0, x21
	ldr	x1, [x19, 3408]
	bl	ftl_memcpy
	ldr	x0, [x19, 2376]
	ldrh	w2, [x19, 44]
	ldr	x1, [x19, 3408]
	lsl	w2, w2, 1
	add	x1, x1, 48
	bl	ftl_memcpy
	ldrh	w1, [x19, 44]
	ldr	x0, [x19, 3408]
	lsr	w2, w1, 3
	ubfiz	x1, x1, 1, 16
	add	x1, x1, 48
	add	w2, w2, 4
	and	x1, x1, -4
	add	x1, x0, x1
	ldr	x0, [x19, 24]
	bl	ftl_memcpy
	ldr	w1, [x19, 2312]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1600
	ldrh	w1, [x19, 2320]
	ldrb	w2, [x19, 2322]
	ldrh	w0, [x19, 58]
	strh	w1, [x19, 2662]
	cmp	w2, w0
	bne	.L1600
	adrp	x0, .LANCHOR3+136
	ldrh	w2, [x19, 62]
	ldrh	w3, [x19, 36]
	str	w1, [x0, #:lo12:.LANCHOR3+136]
	ldrh	w0, [x19, 106]
	mul	w0, w0, w1
	str	w0, [x19, 2584]
	mul	w0, w2, w0
	ldr	w2, [x19, 48]
	str	w0, [x19, 144]
	ldrh	w0, [x19, 166]
	cmp	w1, w2
	sub	w0, w2, w0
	sub	w0, w0, w1
	udiv	w0, w0, w3
	strh	w0, [x19, 2652]
	bls	.L1601
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1461
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1601:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w3, -1
	ldrh	w2, [x0, 2328]
	ldrh	w4, [x0, 2326]
	strh	w4, [x0, 2424]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0, 2430]
	ldrb	w2, [x0, 2323]
	strb	w2, [x0, 2432]
	ldrh	w2, [x0, 2330]
	strh	w2, [x0, 2472]
	ldrh	w2, [x0, 2332]
	strh	w5, [x0, 2426]
	strh	w3, [x0, 2672]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0, 2478]
	ldrb	w2, [x0, 2324]
	strb	w2, [x0, 2480]
	ldrh	w2, [x0, 2334]
	strh	w2, [x0, 2520]
	ldrh	w2, [x0, 2336]
	strh	w5, [x0, 2474]
	strh	wzr, [x0, 2674]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0, 2526]
	ldrb	w2, [x0, 2325]
	strb	w2, [x0, 2528]
	ldr	w2, [x0, 2344]
	str	w2, [x0, 2628]
	strb	wzr, [x0, 2678]
	strb	wzr, [x0, 2680]
	strh	w5, [x0, 2522]
	str	wzr, [x0, 2588]
	str	wzr, [x0, 2596]
	str	wzr, [x0, 2604]
	str	wzr, [x0, 2608]
	str	wzr, [x0, 2632]
	str	wzr, [x0, 2644]
	ldr	w1, [x0, 2352]
	ldr	w2, [x0, 2620]
	str	wzr, [x0, 2600]
	cmp	w1, w2
	bls	.L1602
	str	w1, [x0, 2620]
.L1602:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0, 2348]
	ldr	w2, [x0, 2624]
	cmp	w1, w2
	bls	.L1603
	str	w1, [x0, 2624]
.L1603:
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1604
	add	x0, x20, :lo12:.LANCHOR0
	add	x0, x0, 2424
	bl	make_superblock
.L1604:
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2472
	ldrh	w2, [x1, 2472]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1605
	bl	make_superblock
.L1605:
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2520
	ldrh	w2, [x1, 2520]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1606
	bl	make_superblock
.L1606:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x0, x20, 2672
	ldrh	w2, [x20, 2672]
	cmp	w2, w1
	beq	.L1607
	bl	make_superblock
.L1607:
	mov	w0, 0
	b	.L1591
.L1597:
	ldrh	w0, [x19, 2656]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w22, w0, lsl 10
	str	w0, [x21, 4]
	ldr	x0, [x19, 3432]
	str	x0, [x21, 8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1595
	ldr	x0, [x19, 3432]
	ldr	w0, [x0]
	cmp	w0, w23
	bne	.L1595
	ldr	x0, [x19, 3440]
	ldrh	w0, [x0]
	cmp	w0, w24
	beq	.L1596
.L1595:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1594
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x19, x20, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	add	x0, x21, :lo12:.LANCHOR3
	stp	x23, x24, [sp, 48]
	strh	wzr, [x0, -32]
	ldrh	w0, [x19, 40]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x19, 2656]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1614
.L1616:
	mov	w24, -1
.L1613:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1614:
	bl	FtlLoadSysInfo
	mov	w24, w0
	cbnz	w0, .L1616
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldr	x1, [x19, 2568]
	mov	w0, 0
	ldrh	w3, [x19, 142]
	add	x1, x1, 4
.L1617:
	cmp	w0, w3
	bge	.L1622
	ldr	w2, [x1], 16
	tbz	w2, #31, .L1618
.L1622:
	add	x1, x20, :lo12:.LANCHOR0
	cmp	w0, w3
	ldrh	w2, [x1, 2340]
	add	w2, w2, 1
	strh	w2, [x1, 2340]
	bge	.L1628
.L1619:
	add	x19, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	add	x23, x19, 2424
	add	x22, x19, 2472
	mov	x0, x23
	bl	FtlSuperblockPowerLostFix
	mov	x0, x22
	bl	FtlSuperblockPowerLostFix
	ldrh	w0, [x19, 2424]
	ldr	x2, [x19, 2376]
	ldrh	w3, [x19, 2428]
	lsl	x0, x0, 1
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strb	wzr, [x19, 2430]
	ldrh	w0, [x19, 106]
	strh	w0, [x19, 2426]
	ldrh	w0, [x19, 2472]
	ldr	x2, [x19, 2376]
	strh	wzr, [x19, 2428]
	lsl	x0, x0, 1
	ldrh	w3, [x19, 2476]
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strb	wzr, [x19, 2478]
	ldrh	w0, [x19, 106]
	strh	w0, [x19, 2474]
	sub	x0, x21, #192
	strh	wzr, [x19, 2476]
	bl	FtlMapBlkWriteDump_data
	sub	x0, x21, #120
	bl	FtlMapBlkWriteDump_data
	ldrh	w0, [x19, 2342]
	add	w0, w0, 1
	strh	w0, [x19, 2342]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1623
.L1618:
	add	w0, w0, 1
	b	.L1617
.L1628:
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, -32]
	cbnz	w0, .L1619
.L1623:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x22, x19, 2424
	ldrh	w0, [x19, 2424]
	cmp	w0, w1
	beq	.L1624
	ldrh	w1, [x19, 2428]
	cbnz	w1, .L1624
	ldrh	w1, [x19, 2476]
	add	x21, x19, 2472
	cbnz	w1, .L1624
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 2472]
	bl	FtlGcRefreshBlock
	mov	x0, x22
	bl	allocate_new_data_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
.L1624:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20, 2340]
	tst	x0, 31
	bne	.L1613
	bl	FtlVpcCheckAndModify
	b	.L1613
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	ftl_low_format
	.type	ftl_low_format, %function
ftl_low_format:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	str	x23, [sp, 48]
	str	wzr, [x0, 2620]
	str	wzr, [x0, 2624]
	ldrh	w0, [x0, 40]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L1631
	bl	FtlMakeBbt
.L1631:
	mov	w4, 23752
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 0
	movk	w4, 0xa0f, lsl 16
.L1632:
	ldrh	w2, [x1, 62]
	cmp	w0, w2, lsl 7
	blt	.L1633
	ldrh	w21, [x1, 44]
	add	x22, x20, :lo12:.LANCHOR0
	mov	w19, 0
.L1634:
	ldrh	w0, [x22, 46]
	cmp	w0, w21
	bhi	.L1635
	ldrh	w0, [x22, 36]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L1636
.L1640:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w21, 0
	mov	w22, 0
.L1637:
	ldrh	w0, [x19, 44]
	cmp	w0, w22
	bhi	.L1641
	ldrh	w2, [x19, 36]
	ldr	w1, [x19, 48]
	ldrh	w0, [x19, 46]
	str	w0, [x19, 3968]
	udiv	w4, w1, w2
	ubfx	x3, x4, 5, 16
	str	w4, [x19, 2584]
	add	w0, w3, 36
	strh	w0, [x19, 2652]
	mov	w0, 24
	mul	w0, w2, w0
	cmp	w21, w0
	ble	.L1642
	sub	w1, w1, w21
	udiv	w1, w1, w2
	str	w1, [x19, 2584]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x19, 2652]
.L1642:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 98]
	cbz	w1, .L1644
	ldrh	w5, [x0, 2652]
	add	w5, w5, w1, lsr 1
	strh	w5, [x0, 2652]
	mul	w5, w1, w2
	cmp	w21, w5
	bge	.L1644
	add	w1, w1, 32
	str	w4, [x0, 2584]
	add	w1, w3, w1
	strh	w1, [x0, 2652]
.L1644:
	add	x19, x20, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR3
	add	x21, x19, 2424
	mov	w23, -1
	ldrh	w1, [x19, 2652]
	ldr	w0, [x19, 2584]
	sub	w0, w0, w1
	add	x1, x22, :lo12:.LANCHOR3
	mul	w0, w0, w2
	str	w0, [x1, 136]
	ldrh	w1, [x19, 106]
	mul	w0, w1, w0
	ldrh	w1, [x19, 62]
	str	w0, [x19, 2584]
	mul	w0, w1, w0
	str	w0, [x19, 144]
	bl	FtlBbmTblFlush
	ldr	x0, [x19, 2376]
	mov	w1, 0
	ldrh	w2, [x19, 46]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w0, 1
	strb	w0, [x21, 8]
	ldr	x0, [x19, 24]
	mov	w1, 255
	ldrh	w2, [x19, 44]
	strh	w23, [x19, 2672]
	strh	wzr, [x19, 2674]
	strb	wzr, [x19, 2678]
	lsr	w2, w2, 3
	strb	wzr, [x19, 2680]
	strh	wzr, [x21, 2]
	strb	wzr, [x21, 6]
	strh	wzr, [x19, 2424]
	str	wzr, [x19, 2580]
	bl	ftl_memset
.L1646:
	mov	x0, x21
	bl	make_superblock
	ldrb	w1, [x21, 7]
	ldrh	w0, [x21]
	cbnz	w1, .L1647
	ldr	x1, [x19, 2376]
	ubfiz	x0, x0, 1, 16
	strh	w23, [x1, x0]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L1646
.L1633:
	ldr	x5, [x1, 3992]
	ubfiz	x3, x0, 2, 16
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w2, [x5, x3]
	ldr	x2, [x1, 3976]
	str	w4, [x2, x3]
	b	.L1632
.L1635:
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w19, w19, w0
	and	w21, w21, 65535
	and	w19, w19, 65535
	b	.L1634
.L1636:
	udiv	w0, w19, w0
	ldr	w19, [x22, 136]
	add	x21, x20, :lo12:.LANCHOR0
	add	w0, w0, w19
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22, 40]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x22, 44]
.L1638:
	ldrh	w0, [x21, 46]
	cmp	w0, w19
	bls	.L1640
	mov	w0, w19
	add	w19, w19, 1
	mov	w1, 1
	and	w19, w19, 65535
	bl	FtlLowFormatEraseBlock
	b	.L1638
.L1641:
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w21, w21, w0
	and	w22, w22, 65535
	and	w21, w21, 65535
	b	.L1637
.L1647:
	ldr	w1, [x19, 2620]
	ubfiz	x0, x0, 1, 16
	str	w1, [x21, 12]
	add	w1, w1, 1
	str	w1, [x19, 2620]
	ldr	x1, [x19, 2376]
	ldrh	w2, [x21, 4]
	strh	w2, [x1, x0]
	add	x0, x19, 2472
	strh	wzr, [x19, 2474]
	ldrh	w1, [x21]
	mov	w21, -1
	strb	wzr, [x19, 2478]
	add	w1, w1, 1
	strh	w1, [x19, 2472]
	mov	w1, 1
	strb	w1, [x19, 2480]
	add	x19, x20, :lo12:.LANCHOR0
	mov	x20, x0
.L1648:
	mov	x0, x20
	bl	make_superblock
	ldrb	w1, [x20, 7]
	ldrh	w0, [x20]
	cbnz	w1, .L1649
	ldr	x1, [x19, 2376]
	ubfiz	x0, x0, 1, 16
	strh	w21, [x1, x0]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L1648
.L1649:
	ldr	w1, [x19, 2620]
	ubfiz	x0, x0, 1, 16
	str	w1, [x20, 12]
	add	x22, x22, :lo12:.LANCHOR3
	add	w1, w1, 1
	str	w1, [x19, 2620]
	ldr	x1, [x19, 2376]
	ldrh	w2, [x20, 4]
	mov	w20, -1
	strh	w2, [x1, x0]
	strh	w20, [x19, 2520]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19, 2656]
	ldr	w0, [x22, 136]
	strh	w0, [x19, 2662]
	ldr	w0, [x19, 2620]
	str	w0, [x19, 2664]
	add	w0, w0, 1
	strh	wzr, [x19, 2658]
	strh	w20, [x19, 2660]
	str	w0, [x19, 2620]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L1650
	adrp	x0, .LANCHOR2
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR2]
.L1650:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	sftl_init
	.type	sftl_init, %function
sftl_init:
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LC70
	add	x1, x1, :lo12:.LC70
	mov	w0, -1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	w0, [x20, #:lo12:.LANCHOR2]
	adrp	x0, .LC71
	add	x0, x0, :lo12:.LC71
	bl	sftl_printk
	mov	x0, x19
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x19, 40]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	w0, .L1659
	bl	FtlSysBlkInit
	cbnz	w0, .L1659
	mov	w0, 1
	str	w0, [x20, #:lo12:.LANCHOR2]
	ldrh	w0, [x19, 2416]
	cmp	w0, 15
	bhi	.L1659
	mov	w19, 8129
.L1658:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L1658
.L1659:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	sftl_init, .-sftl_init
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
	.global	rk_sftl_vendor_dev_ops_register
	.type	rk_sftl_vendor_dev_ops_register, %function
rk_sftl_vendor_dev_ops_register:
	adrp	x2, .LANCHOR3
	add	x2, x2, :lo12:.LANCHOR3
	ldr	x3, [x2, 144]
	cbnz	x3, .L1666
	stp	x0, x1, [x2, 144]
	mov	w0, 0
	ret
.L1666:
	mov	w0, -1
	ret
	.size	rk_sftl_vendor_dev_ops_register, .-rk_sftl_vendor_dev_ops_register
	.align	2
	.global	rk_sftl_vendor_storage_init
	.type	rk_sftl_vendor_storage_init, %function
rk_sftl_vendor_storage_init:
	stp	x29, x30, [sp, -80]!
	mov	w0, 65536
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR3
	stp	x21, x22, [sp, 32]
	add	x22, x20, :lo12:.LANCHOR3
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	bl	ftl_malloc
	str	x0, [x22, 160]
	cbz	x0, .L1673
	mov	w26, 22084
	mov	w25, 0
	mov	w24, 0
	mov	w23, 0
	movk	w26, 0x524b, lsl 16
.L1671:
	ldr	x3, [x22, 144]
	mov	w1, 128
	ldr	x2, [x22, 160]
	lsl	w0, w23, 7
	blr	x3
	mov	w21, w0
	cbnz	w0, .L1669
	ldr	x19, [x22, 160]
	ldr	w0, [x19]
	cmp	w0, w26
	bne	.L1670
	add	x0, x19, 61440
	ldr	w1, [x19, 4]
	ldr	w0, [x0, 4092]
	cmp	w0, w1
	bne	.L1670
	cmp	w0, w24
	bls	.L1670
	mov	w25, w23
	mov	w24, w0
.L1670:
	add	w23, w23, 1
	cmp	w23, 2
	bne	.L1671
	cbz	w24, .L1672
	add	x0, x20, :lo12:.LANCHOR3
	mov	x2, x19
	mov	w1, 128
	ldr	x3, [x0, 144]
	lsl	w0, w25, 7
	blr	x3
	mov	w21, w0
	cbz	w0, .L1667
.L1669:
	add	x20, x20, :lo12:.LANCHOR3
	mov	w21, -1
	ldr	x0, [x20, 160]
	bl	kfree
	str	xzr, [x20, 160]
	b	.L1667
.L1672:
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
.L1667:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1673:
	mov	w21, -12
	b	.L1667
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	adrp	x3, .LANCHOR3+160
	ldr	x4, [x3, #:lo12:.LANCHOR3+160]
	cbz	x4, .L1681
	ldrh	w6, [x4, 10]
	add	x5, x4, 16
	mov	w3, 0
.L1678:
	cmp	w3, w6
	bcc	.L1680
.L1681:
	mov	w0, -1
	ret
.L1680:
	ldrh	w7, [x5], 8
	cmp	w7, w0
	bne	.L1679
	stp	x29, x30, [sp, -32]!
	add	x3, x4, x3, uxtw 3
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
.L1679:
	add	w3, w3, 1
	b	.L1678
	.size	rk_sftl_vendor_read, .-rk_sftl_vendor_read
	.align	2
	.global	rk_sftl_vendor_write
	.type	rk_sftl_vendor_write, %function
rk_sftl_vendor_write:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR3
	add	x3, x23, :lo12:.LANCHOR3
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x19, [x3, 160]
	cbz	x19, .L1701
	add	w21, w2, 63
	ldrh	w3, [x19, 10]
	ldrh	w24, [x19, 8]
	mov	x4, x1
	mov	w28, w2
	and	w21, w21, -64
	add	x1, x19, 16
	mov	w20, 0
.L1688:
	cmp	w20, w3
	bcc	.L1696
	ldrh	w1, [x19, 14]
	cmp	w21, w1
	bhi	.L1701
	add	x3, x19, x3, uxth 3
	and	w21, w21, 65535
	uxtw	x2, w28
	strh	w0, [x3, 16]
	ldrh	w0, [x19, 12]
	strh	w0, [x3, 18]
	strh	w28, [x3, 20]
	add	w0, w0, w21
	sub	w21, w1, w21
	strh	w0, [x19, 12]
	strh	w21, [x19, 14]
	mov	x1, x4
	ldrh	w0, [x3, 18]
	add	x0, x0, 1024
	add	x0, x19, x0
	bl	memcpy
	ldrh	w0, [x19, 10]
	add	w0, w0, 1
	strh	w0, [x19, 10]
	b	.L1703
.L1696:
	ldrh	w6, [x1], 8
	cmp	w6, w0
	bne	.L1689
	uxtw	x27, w20
	add	x5, x19, 1024
	add	x22, x19, x27, lsl 3
	ldrh	w25, [x22, 20]
	add	w25, w25, 63
	and	w25, w25, -64
	cmp	w28, w25
	bls	.L1690
	ldrh	w0, [x19, 14]
	cmp	w21, w0
	bhi	.L1701
	add	x27, x27, 2
	ldrh	w22, [x22, 18]
	add	x27, x19, x27, lsl 3
	sub	w26, w3, #1
.L1691:
	cmp	w20, w26
	bcc	.L1692
	add	x20, x19, x20, uxtw 3
	and	w22, w22, 65535
	add	x0, x5, x22, uxth
	uxtw	x2, w28
	mov	x1, x4
	and	w21, w21, 65535
	strh	w22, [x20, 18]
	add	w22, w22, w21
	strh	w6, [x20, 16]
	strh	w28, [x20, 20]
	bl	memcpy
	strh	w22, [x19, 12]
	ldrh	w0, [x19, 14]
	sub	w21, w0, w21
	add	w25, w21, w25
	strh	w25, [x19, 14]
.L1703:
	ldr	w0, [x19, 4]
	add	x1, x19, 61440
	add	x23, x23, :lo12:.LANCHOR3
	mov	x2, x19
	add	w0, w0, 1
	str	w0, [x19, 4]
	str	w0, [x1, 4092]
	mov	w1, 128
	ldr	x3, [x23, 152]
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	and	w0, w0, 65535
	cmp	w0, 1
	csel	w0, w0, wzr, ls
	strh	w0, [x19, 8]
	lsl	w0, w24, 7
	blr	x3
	mov	w0, 0
.L1686:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1692:
	add	w20, w20, 1
	str	x4, [x29, 104]
	add	x0, x19, x20, uxtw 3
	str	x5, [x29, 120]
	add	x27, x27, 8
	ldrh	w1, [x0, 16]
	strh	w1, [x27, -8]
	ldrh	w1, [x0, 20]
	strh	w1, [x27, -4]
	strh	w22, [x27, -6]
	ldrh	w3, [x0, 20]
	ldrh	w1, [x0, 18]
	add	x0, x5, x22, uxtw
	add	w3, w3, 63
	and	w3, w3, -64
	add	x1, x5, x1
	and	x2, x3, 131008
	stp	w6, w3, [x29, 112]
	bl	memcpy
	ldp	w6, w3, [x29, 112]
	ldr	x4, [x29, 104]
	add	w22, w22, w3
	ldr	x5, [x29, 120]
	b	.L1691
.L1690:
	ldrh	w0, [x22, 18]
	uxtw	x2, w28
	mov	x1, x4
	add	x0, x5, x0
	bl	memcpy
	strh	w28, [x22, 20]
	b	.L1703
.L1689:
	add	w20, w20, 1
	b	.L1688
.L1701:
	mov	w0, -1
	b	.L1686
	.size	rk_sftl_vendor_write, .-rk_sftl_vendor_write
	.align	2
	.global	rk_sftl_vendor_storage_ioctl
	.type	rk_sftl_vendor_storage_ioctl, %function
rk_sftl_vendor_storage_ioctl:
	stp	x29, x30, [sp, -48]!
	mov	w0, 4096
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x2
	str	x21, [sp, 32]
	mov	w21, w1
	bl	ftl_malloc
	cbz	x0, .L1718
	mov	w1, 30209
	mov	x19, x0
	movk	w1, 0x4004, lsl 16
	cmp	w21, w1
	beq	.L1707
	add	w1, w1, 1
	cmp	w21, w1
	beq	.L1708
.L1728:
	mov	x20, -14
	b	.L1706
.L1707:
#APP
// 21 "./arch/arm64/include/asm/current.h" 1
	mrs x21, sp_el0
// 0 "" 2
#NO_APP
	mov	x1, x20
	ldr	x2, [x21, 8]
#APP
// 410 "./arch/arm64/include/asm/uaccess.h" 1
	adds x1, x1, 8; ccmp x1, x2, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1714
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbnz	x0, .L1728
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L1712
.L1713:
	mov	x20, -1
.L1706:
	mov	x0, x19
	bl	kfree
.L1704:
	mov	x0, x20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1712:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_read
	cmn	w0, #1
	beq	.L1713
	strh	w0, [x19, 6]
	and	x2, x0, 65535
	add	x2, x2, 8
	mov	x0, x20
	ldr	x1, [x21, 8]
#APP
// 420 "./arch/arm64/include/asm/uaccess.h" 1
	adds x0, x0, x2; ccmp x0, x1, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1728
	mov	x0, x20
	mov	x1, x19
	mov	x20, 0
	bl	__arch_copy_to_user
	cbz	x0, .L1706
	b	.L1728
.L1708:
#APP
// 21 "./arch/arm64/include/asm/current.h" 1
	mrs x21, sp_el0
// 0 "" 2
#NO_APP
	mov	x1, x20
	ldr	x2, [x21, 8]
#APP
// 410 "./arch/arm64/include/asm/uaccess.h" 1
	adds x1, x1, 8; ccmp x1, x2, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1714
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbnz	x0, .L1728
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L1713
	ldrh	w2, [x19, 6]
	cmp	w2, 4087
	bhi	.L1713
	add	w2, w2, 8
	mov	x0, x20
	sxtw	x2, w2
	ldr	x1, [x21, 8]
#APP
// 410 "./arch/arm64/include/asm/uaccess.h" 1
	adds x0, x0, x2; ccmp x0, x1, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1716
	mov	x1, x20
	mov	x0, x19
	bl	__arch_copy_from_user
	cbnz	x0, .L1728
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_write
	sxtw	x20, w0
	b	.L1706
.L1714:
	str	xzr, [x19]
	b	.L1728
.L1716:
	mov	w1, 0
	mov	x0, x19
	bl	memset
	b	.L1728
.L1718:
	mov	x20, -1
	b	.L1704
	.size	rk_sftl_vendor_storage_ioctl, .-rk_sftl_vendor_storage_ioctl
	.align	2
	.global	rk_sftl_vendor_register
	.type	rk_sftl_vendor_register, %function
rk_sftl_vendor_register:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x29, sp, 0
	add	x0, x0, 16
	bl	misc_register
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_sftl_vendor_register, .-rk_sftl_vendor_register
	.global	ftl_temp_buf
	.global	g_nand_ops
	.global	g_nand_phy_info
	.global	gc_ink_free_return_value
	.global	check_vpc_table
	.global	FtlUpdateVaildLpnCount
	.global	g_ect_tbl_power_up_flush
	.global	power_up_flag
	.global	gFtlInitStatus
	.global	DeviceCapacity
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
	.section	.rodata
	.align	3
	.set	.LANCHOR1,. + 0
	.type	__func__.3769, %object
	.size	__func__.3769, 17
__func__.3769:
	.string	"INSERT_DATA_LIST"
	.zero	7
	.type	__func__.3764, %object
	.size	__func__.3764, 17
__func__.3764:
	.string	"INSERT_FREE_LIST"
	.zero	7
	.type	__func__.3800, %object
	.size	__func__.3800, 17
__func__.3800:
	.string	"List_remove_node"
	.zero	7
	.type	__func__.3832, %object
	.size	__func__.3832, 22
__func__.3832:
	.string	"List_update_data_list"
	.zero	2
	.type	__func__.3938, %object
	.size	__func__.3938, 22
__func__.3938:
	.string	"select_l2p_ram_region"
	.zero	2
	.type	__func__.4261, %object
	.size	__func__.4261, 16
__func__.4261:
	.string	"make_superblock"
	.type	__func__.4436, %object
	.size	__func__.4436, 19
__func__.4436:
	.string	"get_new_active_ppa"
	.zero	5
	.type	__func__.16018, %object
	.size	__func__.16018, 15
__func__.16018:
	.string	"FlashReadPages"
	.zero	1
	.type	__func__.16035, %object
	.size	__func__.16035, 15
__func__.16035:
	.string	"FlashProgPages"
	.zero	1
	.type	__func__.3874, %object
	.size	__func__.3874, 26
__func__.3874:
	.string	"ftl_map_blk_alloc_new_blk"
	.zero	6
	.type	__func__.3378, %object
	.size	__func__.3378, 11
__func__.3378:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.3604, %object
	.size	__func__.3604, 14
__func__.3604:
	.string	"FtlBbt2Bitmap"
	.zero	2
	.type	__func__.3841, %object
	.size	__func__.3841, 16
__func__.3841:
	.string	"load_l2p_region"
	.type	__func__.3899, %object
	.size	__func__.3899, 31
__func__.3899:
	.string	"Ftl_write_map_blk_to_last_page"
	.zero	1
	.type	__func__.3913, %object
	.size	__func__.3913, 16
__func__.3913:
	.string	"FtlMapWritePage"
	.type	__func__.3885, %object
	.size	__func__.3885, 15
__func__.3885:
	.string	"ftl_map_blk_gc"
	.zero	1
	.type	__func__.3955, %object
	.size	__func__.3955, 9
__func__.3955:
	.string	"log2phys"
	.zero	7
	.type	__func__.4170, %object
	.size	__func__.4170, 16
__func__.4170:
	.string	"FtlReUsePrevPpa"
	.type	__func__.4050, %object
	.size	__func__.4050, 14
__func__.4050:
	.string	"FtlScanSysBlk"
	.zero	2
	.type	__func__.4282, %object
	.size	__func__.4282, 18
__func__.4282:
	.string	"SupperBlkListInit"
	.zero	6
	.type	__func__.4323, %object
	.size	__func__.4323, 14
__func__.4323:
	.string	"ftl_check_vpc"
	.zero	2
	.type	__func__.4408, %object
	.size	__func__.4408, 25
__func__.4408:
	.string	"allocate_data_superblock"
	.zero	7
	.type	__func__.4449, %object
	.size	__func__.4449, 16
__func__.4449:
	.string	"update_vpc_list"
	.type	__func__.4456, %object
	.size	__func__.4456, 20
__func__.4456:
	.string	"decrement_vpc_count"
	.zero	4
	.type	__func__.4204, %object
	.size	__func__.4204, 22
__func__.4204:
	.string	"FtlRecoverySuperblock"
	.zero	2
	.type	__func__.3647, %object
	.size	__func__.3647, 11
__func__.3647:
	.string	"FtlLoadBbt"
	.zero	5
	.type	__func__.4028, %object
	.size	__func__.4028, 15
__func__.4028:
	.string	"FtlVpcTblFlush"
	.zero	1
	.type	__func__.4307, %object
	.size	__func__.4307, 21
__func__.4307:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.4429, %object
	.size	__func__.4429, 29
__func__.4429:
	.string	"allocate_new_data_superblock"
	.zero	3
	.type	__func__.3502, %object
	.size	__func__.3502, 13
__func__.3502:
	.string	"FtlProgPages"
	.zero	3
	.type	__func__.4526, %object
	.size	__func__.4526, 19
__func__.4526:
	.string	"FtlGcFreeTempBlock"
	.zero	5
	.type	__func__.4632, %object
	.size	__func__.4632, 23
__func__.4632:
	.string	"rk_ftl_garbage_collect"
	.zero	1
	.type	__func__.3530, %object
	.size	__func__.3530, 9
__func__.3530:
	.string	"FtlWrite"
	.zero	7
	.type	__func__.4107, %object
	.size	__func__.4107, 15
__func__.4107:
	.string	"FtlLoadSysInfo"
	.zero	1
	.type	rk_sftl_vendor_storage_fops, %object
	.size	rk_sftl_vendor_storage_fops, 216
rk_sftl_vendor_storage_fops:
	.zero	64
	.xword	rk_sftl_vendor_storage_ioctl
	.xword	rk_sftl_vendor_storage_ioctl
	.zero	136
	.data
	.align	3
	.set	.LANCHOR2,. + 0
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
	.zero	4
	.type	rkflash_vender_storage_dev, %object
	.size	rkflash_vender_storage_dev, 80
rkflash_vender_storage_dev:
	.word	255
	.zero	4
	.xword	.LC110
	.xword	rk_sftl_vendor_storage_fops
	.zero	56
	.bss
	.align	3
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR3,. + 4352
	.type	g_nand_phy_info, %object
	.size	g_nand_phy_info, 24
g_nand_phy_info:
	.zero	24
	.type	p_blk_mode_table, %object
	.size	p_blk_mode_table, 8
p_blk_mode_table:
	.zero	8
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
	.type	c_ftl_nand_sec_pre_page, %object
	.size	c_ftl_nand_sec_pre_page, 2
c_ftl_nand_sec_pre_page:
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
	.type	g_totle_vendor_block, %object
	.size	g_totle_vendor_block, 2
g_totle_vendor_block:
	.zero	2
	.zero	2
	.type	p_vendor_block_table, %object
	.size	p_vendor_block_table, 8
p_vendor_block_table:
	.zero	8
	.type	gBbtInfo, %object
	.size	gBbtInfo, 96
gBbtInfo:
	.zero	96
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.zero	2056
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
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 8
p_erase_count_table:
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
	.type	g_MaxLpn, %object
	.size	g_MaxLpn, 4
g_MaxLpn:
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
	.zero	4
	.type	g_nand_ops, %object
	.size	g_nand_ops, 32
g_nand_ops:
	.zero	32
	.type	req_sys, %object
	.size	req_sys, 32
req_sys:
	.zero	32
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 8
p_sys_data_buf:
	.zero	8
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 8
p_sys_spare_buf:
	.zero	8
	.type	check_spare_buf, %object
	.size	check_spare_buf, 512
check_spare_buf:
	.zero	512
	.type	req_erase, %object
	.size	req_erase, 8
req_erase:
	.zero	8
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
	.zero	4
	.zero	4
	.type	p_io_data_buf_1, %object
	.size	p_io_data_buf_1, 8
p_io_data_buf_1:
	.zero	8
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 8
p_io_spare_buf:
	.zero	8
	.type	p_io_data_buf_0, %object
	.size	p_io_data_buf_0, 8
p_io_data_buf_0:
	.zero	8
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
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 64
gVendorBlkInfo:
	.zero	64
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
	.type	g_power_lost_recovery_flag, %object
	.size	g_power_lost_recovery_flag, 2
g_power_lost_recovery_flag:
	.zero	2
	.zero	2
	.type	g_recovery_page_num, %object
	.size	g_recovery_page_num, 4
g_recovery_page_num:
	.zero	4
	.type	g_recovery_ppa_tbl, %object
	.size	g_recovery_ppa_tbl, 128
g_recovery_ppa_tbl:
	.zero	128
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.zero	2
	.zero	6
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 8
gp_last_act_superblock:
	.zero	8
	.type	gc_discard_updated, %object
	.size	gc_discard_updated, 4
gc_discard_updated:
	.zero	4
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
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.zero	4
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.zero	4
	.zero	4
	.type	_flash_read, %object
	.size	_flash_read, 8
_flash_read:
	.zero	8
	.type	_flash_write, %object
	.size	_flash_write, 8
_flash_write:
	.zero	8
	.type	g_vendor, %object
	.size	g_vendor, 8
g_vendor:
	.zero	8
	.type	ftl_temp_buf, %object
	.size	ftl_temp_buf, 4096
ftl_temp_buf:
	.zero	4096
	.type	check_buf, %object
	.size	check_buf, 4096
check_buf:
	.zero	4096
	.type	check_vpc_table, %object
	.size	check_vpc_table, 16384
check_vpc_table:
	.zero	16384
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\n"
.LC1:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC2:
	.string	"FLASH INFO:\n"
.LC3:
	.string	"Device Capacity: %d MB\n"
.LC4:
	.string	"FTL INFO:\n"
.LC5:
	.string	"g_MaxLpn = 0x%x\n"
.LC6:
	.string	"g_VaildLpn = 0x%x\n"
.LC7:
	.string	"read_page_count = 0x%x\n"
.LC8:
	.string	"discard_page_count = 0x%x\n"
.LC9:
	.string	"write_page_count = 0x%x\n"
.LC10:
	.string	"cache_write_count = 0x%x\n"
.LC11:
	.string	"l2p_write_count = 0x%x\n"
.LC12:
	.string	"gc_page_count = 0x%x\n"
.LC13:
	.string	"totle_write = %d MB\n"
.LC14:
	.string	"totle_read = %d MB\n"
.LC15:
	.string	"GSV = 0x%x\n"
.LC16:
	.string	"GDV = 0x%x\n"
.LC17:
	.string	"bad blk num = %d\n"
.LC18:
	.string	"free_superblocks = 0x%x\n"
.LC19:
	.string	"mlc_EC = 0x%x\n"
.LC20:
	.string	"slc_EC = 0x%x\n"
.LC21:
	.string	"avg_EC = 0x%x\n"
.LC22:
	.string	"sys_EC = 0x%x\n"
.LC23:
	.string	"max_EC = 0x%x\n"
.LC24:
	.string	"min_EC = 0x%x\n"
.LC25:
	.string	"PLT = 0x%x\n"
.LC26:
	.string	"POT = 0x%x\n"
.LC27:
	.string	"MaxSector = 0x%x\n"
.LC28:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC29:
	.string	"sys_blks_pp = 0x%x\n"
.LC30:
	.string	"free sysblock = 0x%x\n"
.LC31:
	.string	"data_blks_pp = 0x%x\n"
.LC32:
	.string	"data_op_blks_pp = 0x%x\n"
.LC33:
	.string	"max_data_blks = 0x%x\n"
.LC34:
	.string	"Sys.id = 0x%x\n"
.LC35:
	.string	"Bbt.id = 0x%x\n"
.LC36:
	.string	"ACT.page = 0x%x\n"
.LC37:
	.string	"ACT.plane = 0x%x\n"
.LC38:
	.string	"ACT.id = 0x%x\n"
.LC39:
	.string	"ACT.mode = 0x%x\n"
.LC40:
	.string	"ACT.a_pages = 0x%x\n"
.LC41:
	.string	"ACT VPC = 0x%x\n"
.LC42:
	.string	"BUF.page = 0x%x\n"
.LC43:
	.string	"BUF.plane = 0x%x\n"
.LC44:
	.string	"BUF.id = 0x%x\n"
.LC45:
	.string	"BUF.mode = 0x%x\n"
.LC46:
	.string	"BUF.a_pages = 0x%x\n"
.LC47:
	.string	"BUF VPC = 0x%x\n"
.LC48:
	.string	"TMP.page = 0x%x\n"
.LC49:
	.string	"TMP.plane = 0x%x\n"
.LC50:
	.string	"TMP.id = 0x%x\n"
.LC51:
	.string	"TMP.mode = 0x%x\n"
.LC52:
	.string	"TMP.a_pages = 0x%x\n"
.LC53:
	.string	"GC.page = 0x%x\n"
.LC54:
	.string	"GC.plane = 0x%x\n"
.LC55:
	.string	"GC.id = 0x%x\n"
.LC56:
	.string	"GC.mode = 0x%x\n"
.LC57:
	.string	"GC.a_pages = 0x%x\n"
.LC58:
	.string	"WR_CHK = %x %x %x\n"
.LC59:
	.string	"Read Err Cnt = 0x%x\n"
.LC60:
	.string	"Prog Err Cnt = 0x%x\n"
.LC61:
	.string	"gc_free_blk_th= 0x%x\n"
.LC62:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC63:
	.string	"gc_skip_write_count= 0x%x\n"
.LC64:
	.string	"gc_blk_index= 0x%x\n"
.LC65:
	.string	"free min EC= 0x%x\n"
.LC66:
	.string	"free max EC= 0x%x\n"
.LC67:
	.string	"GC__SB VPC = 0x%x\n"
.LC68:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC69:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC70:
	.string	"SFTL version: 5.0.44 20180713"
.LC71:
	.string	"%s\n"
.LC72:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC73:
	.string	"FtlGcRefreshBlock  0x%x\n"
.LC74:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
.LC75:
	.string	"%s %p + 0x%x:"
.LC76:
	.string	"0x%08x,"
.LC77:
	.string	"0x%04x,"
.LC78:
	.string	"0x%02x,"
.LC79:
	.string	"\n"
.LC80:
	.string	"prog read error: = %x\n"
.LC81:
	.string	"prog read s error: = %x %x %x\n"
.LC82:
	.string	"prog read d error: = %x %x %x\n"
.LC83:
	.string	"FtlFreeSysBlkQueueOut free count = %d\n"
.LC84:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
.LC85:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
.LC86:
	.string	"%s error allocating memory. return -1\n"
.LC87:
	.string	"region_id = %x phyAddr = %x\n"
.LC88:
	.string	"spare:"
.LC89:
	.string	"map_ppn:"
.LC90:
	.string	"FtlMapWritePage error = %x \n"
.LC91:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC92:
	.string	"ftl_scan_all_data = %x\n"
.LC93:
	.string	"scan lpa = %x ppa= %x\n"
.LC94:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC95:
	.string	"...%s enter...\n"
.LC96:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC97:
	.string	"free blk vpc error %x = %x  %x\n"
.LC98:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC99:
	.string	"FtlBbmTblFlush error:%x\n"
.LC100:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC101:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC102:
	.string	"decrement_vpc_count %x = %d\n"
.LC103:
	.string	"spuer block %x vpn is 0\n "
.LC104:
	.string	"no ect"
.LC105:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC106:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC107:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC108:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC109:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC110:
	.string	"vendor_storage"
	.ident	"GCC: (Linaro GCC 6.3-2017.05) 6.3.1 20170404"
	.section	.note.GNU-stack,"",@progbits
