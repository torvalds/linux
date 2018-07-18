/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-07-19
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
	bl	printk
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
	bl	printk
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
	bl	printk
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
	bl	printk
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
	bl	printk
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
	bl	printk
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
	bl	printk
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
	bl	printk
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
	bl	printk
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
	bl	printk
.L251:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L252
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2709
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	printk
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
	bl	printk
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
	bl	printk
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
	bl	printk
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
	stp	x29, x30, [sp, -16]!
	mul	w3, w3, w2
	mov	x5, x1
	mov	w4, w2
	add	x29, sp, 0
	mov	x1, x0
	sxtw	x6, w3
	mov	w7, 0
	mov	w3, 16
	mov	w2, 2
	adrp	x0, .LC75
	add	x0, x0, :lo12:.LC75
	bl	print_hex_dump
	ldp	x29, x30, [sp], 16
	ret
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
.L316:
	cmp	x19, x23
	bne	.L323
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L323:
	ldr	x0, [x19, 8]
	cbz	x0, .L317
	ldr	x0, [x19, 16]
	cbnz	x0, .L318
.L317:
	mov	w2, 76
	mov	x1, x20
	mov	x0, x21
	bl	printk
.L318:
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
	bne	.L320
	ldrb	w0, [x29, 88]
	ldr	x4, [x26, 3392]
	ldp	x2, x3, [x19, 8]
	ldr	w1, [x29, 92]
	add	x3, x3, 8
	add	x2, x2, 2048
	add	w1, w25, w1
	blr	x4
	cmn	w0, #1
	beq	.L321
	ldr	x0, [x19, 16]
	ldr	w1, [x0, 12]
	cmn	w1, #1
	bne	.L320
	ldr	w1, [x0, 8]
	cmn	w1, #1
	bne	.L320
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L320
.L321:
	mov	w0, -1
	str	w0, [x19]
.L320:
	add	x19, x19, 32
	b	.L316
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
.L335:
	ldrh	w0, [x19, 58]
	cmp	w21, w0
	bcc	.L340
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L340:
	ldrh	w20, [x19, 102]
	mov	w0, -1
	strh	w0, [x22]
.L337:
	ldrh	w0, [x19, 102]
	sub	w20, w20, #1
	and	w20, w20, 65535
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L338
	madd	w0, w0, w21, w20
	mov	w2, 1
	mov	w1, w2
	lsl	w0, w0, 10
	str	w0, [x25, 4]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L337
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L337
	strh	w20, [x22]
.L338:
	add	w21, w21, 1
	add	x22, x22, 2
	b	.L335
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
	bne	.L346
	ldrh	w19, [x1, 108]
.L347:
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
	bne	.L348
	mov	w22, 0
	mov	w24, 2
.L349:
	cmp	w22, w19
	ble	.L352
.L348:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 160
	ret
.L346:
	ldrh	w19, [x1, 106]
	b	.L347
.L352:
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
	bne	.L350
	ldr	w0, [x29, 100]
	cmn	w0, #1
	bne	.L350
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L349
.L350:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L349
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
.L355:
	cmp	x19, x24
	bne	.L363
	cbnz	w26, .L364
.L382:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L363:
	ldr	x0, [x19, 8]
	cbz	x0, .L356
	ldr	x0, [x19, 16]
	cbnz	x0, .L357
.L356:
	mov	w2, 106
	mov	x1, x22
	mov	x0, x23
	bl	printk
.L357:
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
	cbnz	w0, .L358
	str	wzr, [x19]
.L359:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0, 14]
	cmp	w1, 4
	bne	.L361
	ldr	x4, [x0, 3384]
	ldrb	w0, [x29, 104]
	ldp	x2, x3, [x19, 8]
	ldr	w1, [x29, 108]
	add	x3, x3, 8
	add	x2, x2, 2048
	add	w1, w27, w1
	blr	x4
	cbz	w0, .L361
	mov	w0, -1
	str	w0, [x19]
.L361:
	add	x19, x19, 32
	b	.L355
.L358:
	mov	w0, -1
	str	w0, [x19]
	b	.L359
.L368:
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
	bne	.L365
	ldr	w1, [x20, 4]
	adrp	x0, .LC76
	add	x0, x0, :lo12:.LC76
	bl	printk
	str	w24, [x20]
.L365:
	ldr	x0, [x20, 16]
	cbz	x0, .L366
	ldr	w2, [x0]
	ldr	w3, [x21]
	cmp	w2, w3
	beq	.L366
	ldr	w1, [x20, 4]
	adrp	x0, .LC77
	add	x0, x0, :lo12:.LC77
	bl	printk
	mov	w0, -1
	str	w0, [x20]
.L366:
	ldr	x0, [x20, 8]
	cbz	x0, .L367
	ldr	w3, [x22, #:lo12:check_buf]
	ldr	w2, [x0]
	cmp	w2, w3
	beq	.L367
	ldr	w1, [x20, 4]
	adrp	x0, .LC78
	add	x0, x0, :lo12:.LC78
	bl	printk
	mov	w0, -1
	str	w0, [x20]
.L367:
	add	x20, x20, 32
.L383:
	cmp	x20, x19
	bne	.L368
	b	.L382
.L364:
	add	x21, x21, :lo12:.LANCHOR0
	adrp	x22, check_buf
	add	x21, x21, 3448
	add	x23, x22, :lo12:check_buf
	b	.L383
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
.L386:
	cmp	x20, x22
	bne	.L392
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L392:
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
	bne	.L390
	ldrb	w0, [x29, 72]
	ldr	x2, [x19, 3376]
	ldr	w1, [x29, 76]
	add	w1, w23, w1
	blr	x2
	cbz	w0, .L390
	str	w21, [x20, -4]
.L390:
	add	x20, x20, 32
	b	.L386
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
	bcc	.L397
	adrp	x0, .LANCHOR0
	add	x20, x0, :lo12:.LANCHOR0
	mov	x19, x0
	ldrh	w2, [x20, 262]
	cmp	w2, 1024
	beq	.L397
	and	w1, w1, 65535
	cbz	w1, .L399
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
.L399:
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
.L397:
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
.L408:
	ldrh	w0, [x5, 36]
	cmp	w0, w10
	bhi	.L412
	cbz	w21, .L407
	ldr	x0, [x5, 3960]
	mov	w2, w21
	mov	w1, 0
	ubfiz	x21, x21, 5, 16
	mov	x22, 0
	add	x25, x20, :lo12:.LANCHOR0
	bl	FlashEraseBlocks
.L416:
	ldr	x0, [x25, 3960]
	add	x1, x0, x22
	ldr	w0, [x0, x22]
	cmn	w0, #1
	bne	.L415
	ldr	w0, [x1, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.L415:
	add	x22, x22, 32
	cmp	x22, x21
	bne	.L416
	cbz	w23, .L429
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 1
	ldrh	w26, [x0, 108]
	lsr	w0, w26, 2
	str	w0, [x29, 124]
.L417:
	add	x27, x20, :lo12:.LANCHOR0
	mov	w22, 0
	add	x0, x27, 64
	str	x0, [x29, 112]
.L425:
	mov	w5, 0
	mov	w21, 0
.L418:
	ldrh	w0, [x27, 36]
	cmp	w0, w5
	bhi	.L421
	cbz	w21, .L407
	ldr	x0, [x27, 3960]
	mov	w1, w21
	mov	w3, 1
	mov	w2, w25
	mov	x28, 0
	bl	FlashProgPages
	ubfiz	x1, x21, 5, 16
.L424:
	ldr	x0, [x27, 3960]
	add	x3, x0, x28
	ldr	w0, [x0, x28]
	cbz	w0, .L423
	ldr	w0, [x3, 4]
	add	w19, w19, 1
	str	x1, [x29, 104]
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29, 104]
.L423:
	add	x28, x28, 32
	cmp	x1, x28
	bne	.L424
	ldr	w0, [x29, 124]
	add	w22, w22, w0
	and	w22, w22, 65535
	cmp	w26, w22
	bhi	.L425
	add	x26, x20, :lo12:.LANCHOR0
	mov	x22, 0
.L427:
	cbz	w23, .L426
	ldr	x0, [x26, 3960]
	add	x1, x0, x22
	ldr	w0, [x0, x22]
	cbnz	w0, .L426
	ldr	w0, [x1, 4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L426:
	add	x22, x22, 32
	cmp	x22, x28
	bne	.L427
	cmp	w24, 63
	ccmp	w23, 0, 0, hi
	beq	.L407
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	mov	w1, w25
	ldr	x0, [x20, 3960]
	bl	FlashEraseBlocks
.L407:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L412:
	ldr	x1, [x5, 3960]
	ubfiz	x0, x10, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w24
	ldrb	w0, [x8, w10, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w23, .L409
	bl	IsBlkInVendorPart
	cbnz	w0, .L410
.L409:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L411
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
.L410:
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L408
.L411:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L410
.L429:
	mov	w0, 6
	mov	w25, 0
	mov	w26, 1
	str	w0, [x29, 124]
	b	.L417
.L421:
	ldr	x1, [x27, 3960]
	ubfiz	x0, x5, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w24
	ldr	x0, [x29, 112]
	ldrb	w0, [x0, w5, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w23, .L419
	bl	IsBlkInVendorPart
	cbnz	w0, .L420
.L419:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L420
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
.L420:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L418
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
	adrp	x23, .LC80
	add	x23, x23, :lo12:.LC80
	stp	x21, x22, [sp, 32]
.L451:
	ldrh	w1, [x20, 6]
	cbz	w1, .L452
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
	bcs	.L456
	ldrh	w2, [x20, 6]
	mov	w1, w21
	mov	x0, x23
	bl	printk
	b	.L451
.L452:
	adrp	x0, .LC79
	mov	w1, 0
	add	x0, x0, :lo12:.LC79
	bl	printk
.L455:
	b	.L455
.L456:
	mov	w0, w21
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
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
.L459:
	cmp	w20, w1
	beq	.L463
	mov	x21, x0
	ldrh	w2, [x0], 2
	cbnz	w2, .L460
	bl	FtlFreeSysBlkQueueOut
	and	w1, w0, 65535
	strh	w0, [x21]
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L461
	adrp	x0, .LANCHOR0+262
	ldrh	w2, [x0, #:lo12:.LANCHOR0+262]
	adrp	x0, .LC81
	add	x0, x0, :lo12:.LC81
	bl	printk
.L462:
	b	.L462
.L461:
	ldr	w0, [x19, 48]
	strh	wzr, [x19, 2]
	add	w0, w0, 1
	str	w0, [x19, 48]
	ldrh	w0, [x19, 8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19, 8]
.L463:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L465
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 581
	add	x1, x1, 192
	add	x0, x0, :lo12:.LC1
	bl	printk
.L465:
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L460:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L459
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
.L470:
	cmp	w1, w6
	bcc	.L471
	mov	w2, 8
	add	x4, x0, :lo12:.LANCHOR0
	sub	w2, w2, w1
	add	x4, x4, 160
	add	x2, x2, 1
	mov	x3, 0
.L472:
	add	x3, x3, 1
	cmp	x3, x2
	bne	.L473
	add	x2, x0, :lo12:.LANCHOR0
	ldr	x1, [x2, 4088]
	cbnz	x1, .L474
.L476:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 224
	adrp	x0, .LC82
	add	x0, x0, :lo12:.LC82
	bl	printk
	mov	w0, -1
.L469:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L471:
	ldr	x3, [x19, 192]
	add	w1, w1, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x5], 8
	b	.L470
.L473:
	add	x5, x1, x3
	add	x5, x4, x5, lsl 3
	str	xzr, [x5, 24]
	b	.L472
.L474:
	add	x1, x21, :lo12:.LANCHOR3
	ldr	x3, [x1, -256]
	cbz	x3, .L476
	ldr	x3, [x1, -224]
	cbz	x3, .L476
	ldr	x3, [x1, -216]
	cbz	x3, .L476
	ldr	x3, [x2, 2568]
	cbz	x3, .L476
	ldr	x1, [x1, -208]
	cbz	x1, .L476
	ldr	x1, [x2, 2360]
	cbz	x1, .L476
	ldr	x1, [x2, 192]
	cbz	x1, .L476
	ldr	x1, [x2, 2376]
	cbz	x1, .L476
	add	x1, x0, :lo12:.LANCHOR0
	ldr	x2, [x1, 3296]
	cbz	x2, .L476
	ldr	x2, [x1, 3312]
	cbz	x2, .L476
	ldr	x2, [x1, 4016]
	cbz	x2, .L476
	ldr	x2, [x1, 4032]
	cbz	x2, .L476
	ldr	x2, [x1, 3960]
	cbz	x2, .L476
	ldr	x2, [x1, 3280]
	cbz	x2, .L476
	ldr	x2, [x1, 4024]
	cbz	x2, .L476
	ldr	x2, [x1, 3432]
	cbz	x2, .L476
	ldr	x2, [x1, 4040]
	cbz	x2, .L476
	ldr	x1, [x1, 4048]
	cbz	x1, .L476
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3264]
	cbz	x1, .L476
	ldr	x1, [x0, 3992]
	cbz	x1, .L476
	ldr	x1, [x0, 3976]
	cbz	x1, .L476
	ldr	x1, [x0, 3256]
	cbz	x1, .L476
	ldr	x1, [x0, 3440]
	cbz	x1, .L476
	ldr	x1, [x0, 3984]
	cbz	x1, .L476
	ldr	x1, [x0, 3272]
	cbz	x1, .L476
	ldr	x1, [x0, 2384]
	cbz	x1, .L476
	ldr	x1, [x0, 4064]
	cbz	x1, .L476
	ldr	x0, [x0, 152]
	cbz	x0, .L476
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x1, [x0, -248]
	cbz	x1, .L476
	ldr	x1, [x0, -240]
	cbz	x1, .L476
	ldr	x0, [x0, -232]
	cbz	x0, .L476
	mov	w0, 0
	b	.L469
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
.L574:
	ldrh	w0, [x22, x19]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L571
	ldrh	w1, [x23, 102]
	cmp	w1, w0
	bhi	.L573
	adrp	x0, .LC1
	mov	w2, 74
	mov	x1, x20
	add	x0, x0, :lo12:.LC1
	bl	printk
.L573:
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
	bne	.L574
.L571:
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
	bcs	.L583
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 492
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC1
	bl	printk
.L583:
	adrp	x22, .LANCHOR3
	add	x22, x22, :lo12:.LANCHOR3
	ubfiz	x0, x20, 2, 16
	ldr	x1, [x22, -224]
	ldr	w25, [x1, x0]
	cbnz	w25, .L584
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
.L585:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L584:
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
	beq	.L586
	mov	w2, w25
	mov	w1, w20
	adrp	x0, .LC83
	add	x0, x0, :lo12:.LC83
	bl	printk
	ldr	x1, [x23, 3416]
	mov	w3, 4
	adrp	x0, .LC84
	mov	w2, w3
	add	x0, x0, :lo12:.LC84
	bl	rknand_print_hex
	ldrh	w3, [x23, 140]
	adrp	x0, .LC85
	ldr	x1, [x22, -224]
	mov	w2, 4
	add	x0, x0, :lo12:.LC85
	bl	rknand_print_hex
.L586:
	ldrh	w0, [x26, 8]
	cmp	w0, w20
	beq	.L587
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 513
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC1
	bl	printk
.L587:
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x1, [x0, 2568]
	add	x1, x1, x19
	str	wzr, [x1, 4]
	ldr	x0, [x0, 2568]
	strh	w20, [x0, x19]
	b	.L585
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
.L590:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L594
	ldrh	w26, [x21]
	adrp	x24, .LANCHOR0
	add	x24, x24, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w20, 0
.L595:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L599
	mov	w0, w25
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L594:
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20, x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L591:
	ldrh	w3, [x19, 10]
	cmp	w3, w1
	bhi	.L593
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L590
.L593:
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x23, x3]
	cmp	w4, w2
	bne	.L592
	ldrh	w4, [x21, x3]
	add	w4, w4, 1
	strh	w4, [x21, x3]
.L592:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L591
.L599:
	ldrh	w0, [x19]
	uxtw	x22, w20
	cmp	w0, w20
	bne	.L596
	ldrh	w0, [x24, 108]
	ldrh	w1, [x19, 2]
	cmp	w1, w0
	bcs	.L596
	strh	w0, [x21, x22, lsl 1]
.L596:
	lsl	x22, x22, 1
	ldrh	w0, [x21, x22]
	cmp	w26, w0
	bls	.L597
	mov	w25, w20
	mov	w26, w0
.L597:
	cbnz	w0, .L598
	ldrh	w0, [x23, x22]
	cbz	w0, .L598
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x23, x22]
	ldrh	w0, [x19, 8]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
.L598:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L595
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
	bne	.L605
	ldrh	w0, [x19, 8]
	cbz	w0, .L606
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 641
	add	x1, x1, 272
	add	x0, x0, :lo12:.LC1
	bl	printk
.L606:
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
.L607:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L605:
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
.L608:
	ldrh	w2, [x19, 6]
	cmp	w2, w1
	bhi	.L610
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
	b	.L607
.L610:
	ubfiz	x2, x1, 2, 16
	ldr	w3, [x23, x2]
	cmp	w22, w3, lsr 10
	bne	.L609
	add	w0, w0, 1
	ldr	x4, [x20, 3432]
	and	w0, w0, 65535
	ubfiz	x3, x0, 3, 16
	str	w1, [x4, x3]
	ldr	w4, [x23, x2]
	ldr	x2, [x20, 3432]
	add	x2, x2, x3
	str	w4, [x2, 4]
.L609:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L608
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
.L616:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	w1, [x0, 2604]
	add	w1, w1, 1
	str	w1, [x0, 2604]
	ldrh	w0, [x0, 108]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L617
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L618
.L617:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L618:
	ldrh	w1, [x19]
	ldr	x0, [x19, 16]
	ldrh	w0, [x0, x1, lsl 1]
	cbnz	w0, .L619
	adrp	x0, .LC1
	mov	w2, 699
	add	x1, x22, 304
	add	x0, x0, :lo12:.LC1
	bl	printk
.L619:
	ldrh	w1, [x19]
	ldrh	w0, [x19, 10]
	cmp	w1, w0
	bcc	.L620
	adrp	x0, .LC1
	mov	w2, 700
	add	x1, x22, 304
	add	x0, x0, :lo12:.LC1
	bl	printk
.L620:
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
	bne	.L621
	ldr	w1, [x20, 4]
	adrp	x0, .LC86
	add	x0, x0, :lo12:.LC86
	add	w21, w21, 1
	and	w21, w21, 65535
	bl	printk
	ldrh	w0, [x19, 2]
	cmp	w0, 2
	bhi	.L622
	ldrh	w0, [x24, 108]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L622:
	cmp	w21, 3
	bls	.L623
	add	x25, x25, :lo12:.LANCHOR0
	adrp	x0, .LC87
	mov	w2, w21
	add	x0, x0, :lo12:.LC87
	ldr	w1, [x25, 3404]
	bl	printk
.L624:
	b	.L624
.L623:
	ldr	w0, [x19, 52]
	cbz	w0, .L616
.L633:
	b	.L633
.L621:
	cmp	w0, 1
	beq	.L616
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
	blt	.L636
	ubfiz	x0, x0, 1, 16
	ldrh	w24, [x20, x0]
	cbz	w24, .L636
	ldr	w1, [x19, 52]
	cbnz	w1, .L636
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
	bcc	.L637
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L637:
	add	x26, x22, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR1
	add	x21, x26, 3400
	add	x23, x23, 320
	mov	w20, 0
.L638:
	ldrh	w0, [x19, 6]
	cmp	w0, w20
	bhi	.L642
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19, 52]
.L636:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x22, 108]
	cmp	w1, w0
	bcc	.L643
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L643:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L642:
	ubfiz	x27, x20, 2, 16
	ldr	w0, [x25, x27]
	cmp	w24, w0, lsr 10
	bne	.L639
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
	beq	.L640
	adrp	x0, .LC1
	mov	w2, 613
	mov	x1, x23
	add	x0, x0, :lo12:.LC1
	bl	printk
.L640:
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L641
	str	wzr, [x25, x27]
.L639:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L638
.L641:
	ldr	x2, [x21, 8]
	mov	w1, w20
	mov	x0, x19
	bl	FtlMapWritePage
	b	.L639
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
	bcc	.L651
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 808
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	printk
.L651:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x0, 142]
	ldr	x2, [x0, 2568]
	mov	x0, 0
.L652:
	and	x20, x0, 65535
	cmp	w20, w1
	bcc	.L657
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
	beq	.L658
	ldr	w1, [x3, 4]
	tbz	w1, #31, .L658
	bl	flush_l2p_region
.L658:
	mov	w1, w25
	mov	w0, w21
	bl	load_l2p_region
	b	.L653
.L657:
	add	x0, x0, 1
	add	x3, x2, x0, lsl 4
	ldrh	w3, [x3, -16]
	cmp	w3, w21
	bne	.L652
.L653:
	cbnz	w23, .L654
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x0, [x0, 2568]
	add	x0, x0, x20, lsl 4
	ldr	x0, [x0, 8]
	ldr	w0, [x0, x19, lsl 2]
	str	w0, [x22]
.L655:
	add	x24, x24, :lo12:.LANCHOR0
	ldr	x0, [x24, 2568]
	add	x20, x0, x20, lsl 4
	ldr	w0, [x20, 4]
	cmn	w0, #1
	beq	.L660
	add	w0, w0, 1
	str	w0, [x20, 4]
.L660:
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L654:
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
	b	.L655
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
	cbnz	w1, .L666
	ldr	x19, [x2, 2408]
	cbz	x19, .L667
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
.L668:
	cmp	w1, w3
	beq	.L667
	cmp	w19, w0
	bne	.L669
	add	x23, x22, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x23, 2408
	bl	List_remove_node
	ldrh	w0, [x23, 2416]
	cbnz	w0, .L670
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1688
	add	x1, x1, 352
	add	x0, x0, :lo12:.LC1
	bl	printk
.L670:
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
.L667:
	add	x1, x29, 76
	mov	w2, 1
	mov	w0, w21
	bl	log2phys
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L669:
	umull	x19, w19, w4
	ldrh	w19, [x2, x19]
	cmp	w19, w5
	beq	.L667
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L668
.L666:
	add	w1, w1, 1
	strh	w1, [x3, x20]
	b	.L667
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	sub	sp, sp, #96
	adrp	x0, .LC88
	mov	w1, 0
	add	x0, x0, :lo12:.LC88
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x21, x22, [sp, 48]
	adrp	x22, .LANCHOR0
	add	x21, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 32]
	str	x23, [sp, 64]
	mov	w20, 0
	add	x19, x21, 3400
	bl	printk
.L677:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0, 2584]
	cmp	w20, w0
	bcc	.L683
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x29, x30, [sp, 16]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	ret
.L683:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w20
	bl	log2phys
	tst	x20, 2047
	bne	.L678
	ldr	w2, [x29, 76]
	adrp	x0, .LC89
	mov	w1, w20
	add	x0, x0, :lo12:.LC89
	bl	printk
.L678:
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L680
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
	beq	.L681
	ldr	w0, [x23, 8]
	cmp	w20, w0
	beq	.L680
.L681:
	ldp	x1, x0, [x19, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC90
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC90
	ldr	w2, [x19, 4]
	mov	w1, w20
	bl	printk
.L680:
	add	w20, w20, 1
	b	.L677
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x2, x1, 2720
	ldr	w3, [x2, 80]
	cbz	w3, .L686
	ldr	w0, [x1, 2584]
	ldr	w3, [x2, 84]
	cmp	w3, w0
	bcs	.L687
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x1
	mov	w20, 2048
	str	x21, [sp, 32]
.L692:
	ldr	w1, [x19, 2584]
	ldr	w0, [x19, 2804]
	cmp	w0, w1
	bcs	.L689
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x19, 2804]
	ldr	w1, [x29, 60]
	add	w0, w0, 1
	str	w0, [x19, 2804]
	cmn	w1, #1
	beq	.L690
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
	bne	.L689
	ldr	w0, [x29, 60]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L689:
	ldp	x19, x20, [sp, 16]
	mov	w0, -1
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 160
	ret
.L690:
	subs	w20, w20, #1
	bne	.L692
	b	.L689
.L687:
	ldr	w0, [x1, 2588]
	stp	w0, wzr, [x2, 76]
	str	wzr, [x2, 84]
.L696:
	mov	w0, 0
	ret
.L686:
	ldr	w3, [x1, 2588]
	ldr	w5, [x2, 76]
	add	w2, w3, 1048576
	cmp	w5, w2
	bhi	.L695
	ldr	w2, [x1, 2644]
	lsr	w4, w2, 10
	mov	w2, 33554432
	asr	w2, w2, w4
	add	w2, w2, w5
	cmp	w3, w2
	bhi	.L695
	ldrb	w1, [x1, 2340]
	cbnz	w1, .L696
.L695:
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 1
	str	w3, [x0, 2796]
	str	w1, [x0, 2800]
	str	wzr, [x0, 2804]
	b	.L696
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlMapBlkWriteDump_data
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	ldr	w1, [x0, 56]
	cbz	w1, .L710
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
	cbz	w4, .L704
	mov	w2, 1
	mov	w1, w2
	bl	FlashReadPages
.L705:
	add	x1, x21, :lo12:.LANCHOR0
	mov	x0, x19
	ldr	x2, [x1, 3408]
	mov	w1, w20
	bl	FtlMapWritePage
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L704:
	ldrh	w2, [x2, 114]
	mov	w1, 255
	mov	x0, x3
	bl	ftl_memset
	b	.L705
.L710:
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
.L714:
	ldrh	w0, [x19, 46]
	cmp	w0, w24
	bls	.L754
	ldrh	w9, [x19, 36]
	mov	x5, 0
	ldrh	w8, [x19, 116]
	mov	w21, 0
	add	x7, x19, 64
	mov	w6, 4
	b	.L755
.L716:
	ldrb	w0, [x7, x5]
	mov	w1, w24
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L715
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
.L715:
	add	x5, x5, 1
.L755:
	cmp	w9, w5, uxth
	bhi	.L716
	cbnz	w21, .L717
.L753:
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L714
.L717:
	ldr	x0, [x19, 4016]
	mov	w1, w21
	mov	w2, 1
	ubfiz	x21, x21, 5, 16
	mov	x23, 0
	add	x28, x19, 2656
	bl	FlashReadPages
.L752:
	ldr	x0, [x19, 4016]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	ldr	w26, [x1, 4]
	cmn	w0, #1
	ldr	x27, [x1, 16]
	ubfx	x26, x26, 10, 16
	bne	.L720
	mov	w3, 16
	mov	w4, 65535
.L722:
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
	bne	.L719
	mov	w1, -1
	str	w1, [x0, x23]
	ldr	x0, [x19, 4016]
	ldr	w0, [x0, x23]
	cmp	w0, w1
	bne	.L720
.L721:
	mov	w1, 0
	mov	w0, w26
	bl	FtlFreeSysBlkQueueIn
	b	.L725
.L719:
	ldr	w0, [x0, x23]
	cmn	w0, #1
	bne	.L720
	sub	w3, w3, #1
	ands	w3, w3, 65535
	bne	.L722
	b	.L721
.L720:
	ldr	w1, [x19, 2620]
	ldr	w0, [x27, 4]
	cmn	w1, #1
	beq	.L723
	cmp	w1, w0
	bhi	.L724
.L723:
	cmn	w0, #1
	beq	.L724
	add	w1, w0, 1
	str	w1, [x19, 2620]
.L724:
	ldrh	w1, [x27]
	mov	w2, 61604
	cmp	w1, w2
	beq	.L726
	bhi	.L727
	mov	w0, 61574
	cmp	w1, w0
	beq	.L728
.L725:
	add	x23, x23, 32
	cmp	x21, x23
	bne	.L752
	b	.L753
.L727:
	mov	w0, 61634
	cmp	w1, w0
	beq	.L729
	mov	w0, 65535
	cmp	w1, w0
	beq	.L721
	b	.L725
.L729:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w1, [x0, -128]
	ldr	w0, [x19, 132]
	cmp	w1, w0
	bls	.L731
	adrp	x0, .LC1
	mov	w2, 1211
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	printk
.L731:
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
.L732:
	cmp	w0, w2
	bgt	.L738
	tbz	w0, #31, .L772
	b	.L725
.L738:
	sxtw	x5, w0
	ldr	w8, [x27, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L733
	ldr	w2, [x4]
	cbnz	w2, .L734
	cmp	w3, w1
	beq	.L734
	add	x2, x20, :lo12:.LANCHOR3
	add	w1, w1, 1
	strh	w1, [x2, -128]
.L734:
	add	x7, x20, :lo12:.LANCHOR3
	mov	w1, 0
.L735:
	cmp	w1, w0
	bne	.L736
	add	x1, x20, :lo12:.LANCHOR3
	ldr	w3, [x27, 4]
	ldr	x2, [x1, -216]
	str	w3, [x2, x6]
	ldr	x2, [x19, 4088]
	strh	w26, [x2, x5, lsl 1]
	tbnz	w0, #31, .L725
	ldrh	w1, [x1, -128]
	ldr	w2, [x19, 132]
	sub	w2, w2, w1
	sub	w2, w2, #1
	cmp	w0, w2, sxth
	bgt	.L725
.L772:
	add	x2, x20, :lo12:.LANCHOR3
	add	w1, w1, 1
	ldr	w3, [x27, 4]
	strh	w1, [x2, -128]
	ldr	x1, [x2, -216]
	str	w3, [x1, x0, lsl 2]
	ldr	x1, [x19, 4088]
.L790:
	strh	w26, [x1, x0, lsl 1]
	b	.L725
.L736:
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
	b	.L735
.L733:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L732
.L728:
	ldrh	w1, [x19, 148]
	ldrh	w0, [x19, 124]
	cmp	w1, w0
	bls	.L741
	adrp	x0, .LC1
	mov	w2, 1252
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	printk
.L741:
	add	x4, x20, :lo12:.LANCHOR3
	ldrh	w3, [x19, 124]
	ldrh	w1, [x19, 148]
	sub	w2, w3, #1
	ldr	x4, [x4, -240]
	sxth	x0, w2
	sub	w2, w2, w1
.L742:
	cmp	w0, w2
	ble	.L747
	sxtw	x5, w0
	ldr	w8, [x27, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L743
	ldr	w2, [x4]
	cbnz	w2, .L744
	cmp	w3, w1
	beq	.L744
	add	w1, w1, 1
	strh	w1, [x19, 148]
.L744:
	add	x7, x20, :lo12:.LANCHOR3
	mov	w1, 0
.L745:
	cmp	w1, w0
	bne	.L746
	add	x1, x20, :lo12:.LANCHOR3
	ldr	w2, [x27, 4]
	ldr	x1, [x1, -240]
	str	w2, [x1, x6]
	ldr	x1, [x19, 152]
	strh	w26, [x1, x5, lsl 1]
.L747:
	tbnz	w0, #31, .L725
	ldrh	w1, [x19, 124]
	ldrh	w2, [x19, 148]
	sub	w1, w1, #1
	sub	w1, w1, w2
	cmp	w0, w1, sxth
	bgt	.L725
	add	x1, x20, :lo12:.LANCHOR3
	add	w2, w2, 1
	strh	w2, [x19, 148]
	ldr	w2, [x27, 4]
	ldr	x1, [x1, -240]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 152]
	b	.L790
.L746:
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
	b	.L745
.L743:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L742
.L726:
	ldrh	w2, [x28]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L749
	strh	w26, [x28]
.L791:
	str	w0, [x28, 8]
	b	.L725
.L749:
	ldrh	w0, [x28, 4]
	cmp	w0, w1
	beq	.L750
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L750:
	ldr	w0, [x27, 4]
	ldr	w1, [x28, 8]
	cmp	w1, w0
	bcs	.L751
	ldrh	w0, [x28]
	strh	w0, [x28, 4]
	strh	w26, [x28]
	ldr	w0, [x27, 4]
	b	.L791
.L751:
	strh	w26, [x28, 4]
	b	.L725
.L754:
	ldr	x1, [x19, 4088]
	ldrh	w0, [x1]
	cbz	w0, .L756
.L759:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x1, [x0, 152]
	ldrh	w2, [x1]
	cbz	w2, .L757
.L758:
	add	x20, x20, :lo12:.LANCHOR3
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x20, -128]
	ldr	w0, [x22, 132]
	cmp	w1, w0
	bls	.L788
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1377
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC1
	bl	printk
.L788:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L756:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0, -128]
	cbz	w0, .L759
	ldr	w2, [x19, 132]
	mov	w0, 0
.L760:
	cmp	w0, w2
	bcs	.L759
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L761
	mov	w1, w0
	add	x3, x22, :lo12:.LANCHOR0
	add	x7, x20, :lo12:.LANCHOR3
.L762:
	ldr	w2, [x3, 132]
	cmp	w1, w2
	bcs	.L759
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
	b	.L762
.L761:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L760
.L757:
	ldrh	w2, [x0, 148]
	cbz	w2, .L758
	ldrh	w2, [x0, 124]
	mov	w0, 0
.L767:
	mov	w6, w0
	cmp	w0, w2
	bge	.L758
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L768
	add	x2, x22, :lo12:.LANCHOR0
	add	x7, x20, :lo12:.LANCHOR3
.L769:
	ldrh	w1, [x2, 124]
	cmp	w0, w1
	bge	.L758
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
	b	.L769
.L768:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L767
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
.L793:
	ldr	w0, [x29, 116]
	cmp	w27, w0
	bge	.L810
	ldr	w0, [x29, 112]
	sxtw	x28, w27
	cmp	w27, w0
	bne	.L794
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
.L795:
	cmp	w23, w26
	blt	.L797
.L810:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x0, x19
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 2]
	ldrh	w0, [x21, 108]
	cmp	w1, w0
	bne	.L799
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L799:
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
.L797:
	ldrh	w0, [x20]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w23, w0, lsl 10
	str	w0, [x25, 4]
	mov	x0, x25
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L796
	ldrh	w0, [x22, 8]
	cmp	w24, w0
	bls	.L796
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L796
	ldr	x2, [x29, 120]
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x25, 4]
	str	w1, [x2, x0]
.L796:
	add	w23, w23, 1
	sxth	w23, w23
	b	.L795
.L794:
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
	beq	.L812
	ldrh	w1, [x22]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L812
	ldrh	w1, [x22, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L801
.L812:
	mov	w5, 0
.L802:
	ldrh	w0, [x25, 108]
	cmp	w5, w0
	bge	.L808
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
	beq	.L806
	ldrh	w0, [x22, 8]
	cmp	w24, w0
	bls	.L806
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L806
	ldr	x2, [x29, 120]
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x20, 4]
	str	w1, [x2, x0]
.L806:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L802
.L801:
	mov	w0, 0
.L803:
	ldrh	w1, [x25, 108]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L805
.L808:
	add	w4, w27, 1
	sxth	w27, w4
	b	.L793
.L805:
	ldr	x2, [x25, 3432]
	sbfiz	x5, x0, 3, 32
	ldrh	w1, [x2, x5]
	cmp	w24, w1
	bls	.L804
	add	x2, x2, x5
	ldr	x3, [x29, 120]
	ubfiz	x1, x1, 2, 16
	ldr	w2, [x2, 4]
	str	w2, [x3, x1]
.L804:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L803
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
.L823:
	ldrh	w2, [x1, 142]
	cmp	w2, w0
	bhi	.L824
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
.L824:
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
	b	.L823
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
.L831:
	ldrh	w0, [x19, 44]
	cmp	w20, w0
	bcs	.L837
	ldrh	w8, [x19, 36]
	mov	x6, 0
	ldrh	w7, [x19, 106]
	mov	w5, 0
	b	.L838
.L833:
	ldrb	w0, [x23, x6]
	mov	w1, w20
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L832
	add	w5, w5, w7
	and	w5, w5, 65535
.L832:
	add	x6, x6, 1
.L838:
	cmp	w8, w6, uxth
	bhi	.L833
	cbz	w5, .L840
	mov	w0, 32768
	sdiv	w5, w0, w5
.L834:
	ldr	x1, [x19, 2360]
	mov	w0, 6
	umaddl	x0, w20, w0, x1
	strh	w5, [x0, 4]
	ldrh	w0, [x19, 2424]
	cmp	w0, w20
	beq	.L835
	ldrh	w0, [x19, 2472]
	cmp	w0, w20
	beq	.L835
	ldrh	w0, [x19, 2520]
	cmp	w0, w20
	beq	.L835
	ldr	x1, [x19, 2376]
	ubfiz	x0, x20, 1, 16
	ldrh	w0, [x1, x0]
	cbnz	w0, .L836
	add	w22, w22, 1
	mov	w0, w20
	and	w22, w22, 65535
	bl	INSERT_FREE_LIST
.L835:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L831
.L840:
	mov	w5, 0
	b	.L834
.L836:
	add	w21, w21, 1
	mov	w0, w20
	and	w21, w21, 65535
	bl	INSERT_DATA_LIST
	b	.L835
.L837:
	strh	w21, [x19, 2400]
	add	w21, w21, w22
	strh	w22, [x19, 2416]
	cmp	w21, w0
	ble	.L839
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2154
	add	x1, x1, 384
	add	x0, x0, :lo12:.LC1
	bl	printk
.L839:
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
	adrp	x0, .LC91
	add	x0, x0, :lo12:.LC91
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
	bl	printk
	add	x19, x22, :lo12:check_vpc_table
	mov	w2, 8192
	mov	w1, 0
	mov	x0, x19
	bl	ftl_memset
.L843:
	ldr	w0, [x24, 2584]
	cmp	w23, w0
	bcc	.L845
	adrp	x25, .LC92
	add	x23, x20, :lo12:.LANCHOR0
	add	x26, x22, :lo12:check_vpc_table
	add	x25, x25, :lo12:.LC92
	mov	w24, 0
	mov	w19, 0
	mov	w4, 65535
.L846:
	ldrh	w0, [x23, 44]
	cmp	w0, w19
	bhi	.L848
	ldr	x19, [x23, 2408]
	cbz	x19, .L849
	ldr	x0, [x23, 2360]
	adrp	x25, .LC93
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:check_vpc_table
	sub	x19, x19, x0
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	movk	x0, 0xaaab, lsl 0
	add	x25, x25, :lo12:.LC93
	ldrh	w26, [x23, 2416]
	mov	w27, 6
	mov	w23, 0
	mul	x19, x19, x0
	and	w19, w19, 65535
.L850:
	cmp	w23, w26
	bne	.L852
.L849:
	cbz	w24, .L842
	add	x1, x21, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2321
	add	x1, x1, 408
	add	x0, x0, :lo12:.LC1
	bl	printk
.L842:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L845:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w23
	bl	log2phys
	ldr	w0, [x29, 124]
	cmn	w0, #1
	beq	.L844
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	and	x0, x0, 65535
	ldrh	w1, [x19, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x19, x0, lsl 1]
.L844:
	add	w23, w23, 1
	b	.L843
.L848:
	ldr	x0, [x23, 2376]
	ubfiz	x28, x19, 1, 16
	sxtw	x27, w19
	ldrh	w2, [x0, x28]
	ldrh	w3, [x26, x27, lsl 1]
	cmp	w2, w3
	beq	.L847
	str	w4, [x29, 108]
	mov	w1, w19
	mov	x0, x25
	bl	printk
	ldr	x0, [x23, 2376]
	ldr	w4, [x29, 108]
	ldrh	w0, [x0, x28]
	cmp	w0, w4
	beq	.L847
	ldrh	w1, [x26, x27, lsl 1]
	cmp	w1, w0
	csinc	w24, w24, wzr, ls
.L847:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L846
.L852:
	ldr	x1, [x20, 2376]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L851
	ldrh	w3, [x22, w19, sxtw 1]
	mov	w24, 1
	mov	w1, w19
	mov	x0, x25
	bl	printk
.L851:
	ldr	x0, [x20, 2360]
	umull	x19, w19, w27
	ldrh	w19, [x0, x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L849
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L850
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
	beq	.L886
	cbnz	w19, .L870
.L871:
	bl	FtlGcPageVarInit
	b	.L872
.L886:
	mov	w19, 0
.L870:
	adrp	x0, .LANCHOR0+106
	ldrh	w0, [x0, #:lo12:.LANCHOR0+106]
	cmp	w0, w25
	beq	.L871
.L872:
	adrp	x26, .LANCHOR0
	add	x22, x26, :lo12:.LANCHOR0
	mov	w24, 0
.L880:
	ldrh	w1, [x21]
	mov	w0, 65535
	strb	wzr, [x21, 8]
	cmp	w1, w0
	beq	.L887
.L883:
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
.L874:
	cmp	x0, x2
	bne	.L876
	ldr	x0, [x22, 4016]
	mov	w1, w20
	mov	w2, 0
	ubfiz	x20, x20, 5, 16
	bl	FlashReadPages
	mov	x10, 0
	mov	w11, 65535
.L877:
	cmp	x10, x20
	bne	.L881
	add	w19, w19, 1
	add	w24, w24, 1
	and	w19, w19, 65535
	cmp	w25, w24
	bls	.L882
.L884:
	ldrh	w0, [x22, 106]
	cmp	w0, w19
	bhi	.L883
.L887:
	mov	w2, 0
	b	.L873
.L876:
	ldrh	w1, [x2]
	cmp	w1, w9
	beq	.L875
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
.L875:
	add	x2, x2, 2
	b	.L874
.L881:
	ldr	x6, [x22, 4016]
	add	x5, x6, x10
	ldr	w4, [x5, 4]
	lsr	w0, w4, 10
	bl	P2V_plane
	and	w2, w0, 65535
	ldr	w0, [x6, x10]
	cbnz	w0, .L878
	ldr	x0, [x5, 16]
	add	x10, x10, 32
	ldrh	w1, [x0]
	cmp	w1, w11
	bne	.L879
	adrp	x0, .LANCHOR3-40
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR3-40]
.L873:
	add	x23, x23, :lo12:.LANCHOR2
	strh	w19, [x21, 2]
	strb	w2, [x21, 6]
	mov	w0, -1
	mov	w1, w19
	strh	w0, [x23, 4]
	mov	x0, x21
	bl	ftl_sb_update_avl_pages
	b	.L885
.L879:
	ldp	w2, w0, [x0, 8]
	mov	w1, w4
	bl	FtlGcUpdatePage
	b	.L877
.L878:
	ldr	x0, [x22, 2376]
	mov	w19, 0
	ldrh	w1, [x21]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x21]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x21]
	bl	FtlGcPageVarInit
	b	.L880
.L882:
	add	x1, x23, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x1, 4]
	cmp	w0, w2
	beq	.L884
	add	w0, w0, w24
	strh	w0, [x1, 4]
	ldrh	w0, [x22, 106]
	cmp	w0, w19
	bls	.L884
.L885:
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
	bls	.L895
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
.L893:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 128
	ret
.L895:
	mov	w19, 0
	b	.L893
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
	bls	.L898
	mov	w2, 256
	mov	w1, 255
	mov	x0, x20
	bl	ftl_memset
	mov	w0, 0
.L898:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19, 14]
	cmp	w1, 4
	bne	.L902
	mov	x1, 0
.L900:
	cmp	w0, w1, uxth
	bhi	.L901
.L902:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L901:
	ldrh	w2, [x20, x1, lsl 1]
	lsr	w2, w2, 1
	strh	w2, [x20, x1, lsl 1]
	add	x1, x1, 1
	b	.L900
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
.L907:
	ldrh	w0, [x19, 58]
	add	x20, x19, 3400
	cmp	w21, w0
	blt	.L908
	ldr	x24, [x20, 16]
	mov	w1, 255
	mov	w2, 16
	adrp	x22, .LC94
	mov	x0, x24
	bl	ftl_memset
	mov	w0, -3887
	add	x1, x19, 160
	strh	w0, [x24]
	add	x25, x25, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC94
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
.L909:
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
	bl	printk
	mov	w3, 1
	mov	x0, x20
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x25, 108]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L910
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
.L910:
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L911
	ldr	w1, [x20, 4]
	add	w21, w21, 1
	adrp	x0, .LC95
	and	w21, w21, 65535
	add	x0, x0, :lo12:.LC95
	bl	printk
	cmp	w21, 3
	bls	.L909
	ldr	w1, [x20, 4]
	adrp	x0, .LC96
	mov	w2, w21
	add	x0, x0, :lo12:.LC96
	bl	printk
.L913:
	b	.L913
.L908:
	ldrh	w2, [x22, -200]
	ldr	x1, [x23], 8
	ldr	x0, [x20, 8]
	mul	w3, w21, w2
	lsl	w2, w2, 2
	add	w21, w21, 1
	add	x0, x0, x3, sxtw 2
	bl	ftl_memcpy
	b	.L907
.L914:
	mov	w23, 1
	b	.L909
.L911:
	cbz	w23, .L914
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
.L917:
	ldrh	w1, [x22, 2400]
	ldrh	w0, [x22, 2416]
	add	w0, w0, w1
	ldrh	w1, [x22, 44]
	cmp	w0, w1
	ble	.L918
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2585
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.L918:
	cmp	x20, x23
	bne	.L943
	ldrh	w0, [x19, 2416]
	ldr	w1, [x19, 3244]
	mul	w1, w0, w1
	lsr	w0, w0, 1
	add	w0, w0, 1
	add	w1, w0, w1, lsr 2
	ands	w1, w1, 65535
	beq	.L919
	sub	w1, w1, #1
	and	w1, w1, 65535
.L919:
	add	x0, x19, 2408
	bl	List_pop_index_node
	and	w24, w0, 65535
	ldrh	w0, [x19, 2416]
	cbnz	w0, .L920
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2594
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.L920:
	ldrh	w0, [x19, 2416]
	sub	w0, w0, #1
	strh	w0, [x19, 2416]
	strh	w24, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20, 7]
	cbnz	w0, .L921
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
	ble	.L917
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2605
	add	x1, x1, 424
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
	b	.L917
.L943:
	mov	w1, 0
	b	.L919
.L921:
	ldrh	w1, [x19, 2400]
	ldrh	w0, [x19, 2416]
	add	w0, w0, w1
	ldrh	w1, [x19, 44]
	cmp	w0, w1
	ble	.L923
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2608
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.L923:
	add	x27, x20, 16
	ldrh	w4, [x19, 36]
	mov	x3, x27
	mov	x0, 0
	mov	w25, 0
	mov	w5, 65535
.L924:
	cmp	w4, w0, uxth
	bhi	.L926
	cbnz	w25, .L927
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2619
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.L927:
	ldrh	w0, [x19, 2672]
	cmp	w0, w24
	bne	.L928
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2621
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.L928:
	ldrb	w0, [x20, 8]
	uxtw	x26, w24
	cbnz	w0, .L929
	ldr	x2, [x19, 2384]
	lsl	x1, x26, 1
	ldrh	w0, [x2, x1]
	cbz	w0, .L930
	ldrh	w3, [x19, 96]
	add	w0, w0, w3
.L951:
	strh	w0, [x2, x1]
	mov	w1, 0
	ldr	w0, [x19, 2628]
	add	w0, w0, 1
	str	w0, [x19, 2628]
	mov	w0, w24
	bl	ftl_set_blk_mode
.L932:
	ldr	x0, [x19, 2384]
	lsl	x26, x26, 1
	ldr	w1, [x19, 2644]
	ldrh	w0, [x0, x26]
	cmp	w0, w1
	bls	.L933
	str	w0, [x19, 2644]
.L933:
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
.L934:
	cmp	x3, x0
	bne	.L935
	ldrb	w1, [x20, 8]
	mov	w2, w25
	ldr	x0, [x19, 3960]
	mov	x28, 0
	bl	FlashEraseBlocks
	mov	w1, 0
.L936:
	cmp	w25, w28, uxth
	bhi	.L938
	cmp	w1, 0
	ble	.L939
	mov	w0, w24
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L939:
	ldrb	w0, [x20, 7]
	cbnz	w0, .L940
	ldr	x0, [x19, 2376]
	mov	w1, -1
	strh	w1, [x0, x26]
	mov	w0, w24
	bl	INSERT_DATA_LIST
	b	.L917
.L926:
	ldr	x1, [x19, 3960]
	add	x2, x1, x0, lsl 5
	stp	xzr, xzr, [x2, 8]
	ldrh	w2, [x3]
	cmp	w2, w5
	beq	.L925
	ubfiz	x6, x25, 5, 16
	add	w25, w25, 1
	add	x1, x1, x6
	and	w25, w25, 65535
	lsl	w2, w2, 10
	str	w2, [x1, 4]
.L925:
	add	x0, x0, 1
	add	x3, x3, 2
	b	.L924
.L930:
	mov	w0, 2
	b	.L951
.L929:
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
	b	.L932
.L935:
	ldr	x1, [x19, 3960]
	add	x1, x1, x0
	add	x0, x0, 32
	ldr	w2, [x1, 4]
	and	w2, w2, -1024
	str	w2, [x1, 4]
	b	.L934
.L938:
	ldr	x2, [x19, 3960]
	lsl	x0, x28, 5
	add	x3, x2, x0
	ldr	w2, [x2, x0]
	cmn	w2, #1
	bne	.L937
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
.L937:
	add	x28, x28, 1
	add	x27, x27, 2
	b	.L936
.L940:
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
	cbz	w0, .L941
	ldrb	w0, [x20, 7]
	cbnz	w0, .L942
.L941:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2674
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.L942:
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
	cbz	w0, .L953
	add	x23, x19, 64
	add	x24, x19, 3328
	mov	w21, 0
.L954:
	ldrh	w0, [x19, 36]
	cmp	w0, w21
	bhi	.L960
	bl	FtlGcReFreshBadBlk
.L953:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L960:
	ldrb	w0, [x23, w21, sxtw]
	mov	w1, w22
	adrp	x26, .LC97
	add	x26, x26, :lo12:.LC97
	mov	w20, 0
	bl	V2P_block
	and	w25, w0, 65535
.L955:
	ldrh	w0, [x19, 3324]
	cmp	w0, w20
	bhi	.L959
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L954
.L959:
	ldrh	w0, [x24, w20, sxtw 1]
	add	w27, w20, 1
	cmp	w0, w25
	bne	.L956
	mov	w1, w25
	mov	x0, x26
	bl	printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w0, [x19, 3324]
	mov	w1, w27
.L957:
	cmp	w20, w0
	bcc	.L958
	sub	w0, w0, #1
	strh	w0, [x19, 3324]
.L956:
	and	w20, w27, 65535
	b	.L955
.L958:
	sub	w2, w1, #1
	ldrh	w3, [x24, w1, sxtw 1]
	add	w20, w20, 1
	add	w1, w1, 1
	and	w20, w20, 65535
	strh	w3, [x24, w2, sxtw 1]
	b	.L957
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
	cbnz	w1, .L966
	ldrh	w1, [x0, 2672]
	cmp	w1, w19
	bne	.L967
	mov	w1, -1
	strh	w1, [x0, 2672]
.L968:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x21, 2368
	bl	List_remove_node
	ldrh	w0, [x21, 2400]
	cbnz	w0, .L970
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2744
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	printk
.L970:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20, 2400]
	sub	w0, w0, #1
	strh	w0, [x20, 2400]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	ldrh	w1, [x20, 2400]
	mov	w19, 1
	ldrh	w0, [x20, 2416]
	add	w0, w0, w1
	ldrh	w1, [x20, 44]
	cmp	w0, w1
	ble	.L965
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2747
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	printk
.L965:
	mov	w0, w19
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L967:
	ldrh	w1, [x0, 2424]
	cmp	w1, w19
	beq	.L973
	ldrh	w1, [x0, 2472]
	cmp	w1, w19
	beq	.L973
	ldrh	w0, [x0, 2520]
	cmp	w0, w19
	bne	.L968
.L973:
	mov	w19, 0
	b	.L965
.L966:
	mov	w0, w19
	bl	List_update_data_list
	b	.L973
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
	beq	.L977
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	ubfiz	x21, x19, 1, 16
	ldr	x1, [x20, 2376]
	ldrh	w0, [x1, x21]
	cbnz	w0, .L978
	mov	w2, 0
	mov	w1, w19
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	bl	printk
	ldr	x0, [x20, 2376]
	ldrh	w0, [x0, x21]
	cbz	w0, .L979
.L984:
	mov	w21, 0
.L976:
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L979:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2762
	add	x1, x1, 472
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
	b	.L984
.L978:
	sub	w0, w0, #1
	strh	w0, [x1, x21]
.L977:
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	mov	w1, 65535
	ldrh	w0, [x20, -48]
	cmp	w0, w1
	bne	.L981
	strh	w19, [x20, -48]
	b	.L984
.L981:
	cmp	w19, w0
	beq	.L984
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
	cbnz	w1, .L976
	cmp	w19, w0, uxth
	beq	.L976
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2778
	add	x1, x1, 472
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
	b	.L976
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
	cbz	w2, .L986
	ldrb	w1, [x20, 2432]
	cbnz	w1, .L986
	ldrb	w1, [x20, 2431]
	ldrh	w3, [x20, 106]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L986
	ldrb	w0, [x20, 2434]
	cbnz	w0, .L985
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
	beq	.L988
	mov	w2, 0
	mov	w1, 1
	add	x0, x29, 96
	bl	FlashReadPages
.L989:
	add	x25, x19, :lo12:.LANCHOR0
	lsl	w24, w24, 2
	add	x20, x25, 2424
	mov	w0, -3947
	mov	w23, 0
	strh	w0, [x21]
.L990:
	cmp	w24, w23
	bne	.L994
.L991:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19, 2434]
.L985:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 128
	ret
.L988:
	ldrh	w2, [x20, 114]
	mov	w1, 255
	ldr	x0, [x20, 3432]
	bl	ftl_memset
	b	.L989
.L994:
	ldrh	w0, [x20, 4]
	cbz	w0, .L991
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
	b	.L990
.L986:
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x19, 2434]
	b	.L985
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
.L1004:
	ldrh	w0, [x20, 142]
	cmp	w0, w19
	bhi	.L1006
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1006:
	ldr	x1, [x20, 2568]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L1005
	mov	w0, w19
	bl	flush_l2p_region
.L1005:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1004
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
	beq	.L1126
	ldrb	w0, [x19, 6]
	adrp	x20, .LANCHOR0
	str	w0, [x29, 156]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w27, [x19, 2]
	ldrh	w0, [x0, 106]
	cmp	w0, w27
	bne	.L1011
	strh	wzr, [x19, 4]
.L1132:
	strb	wzr, [x19, 6]
.L1126:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1011:
	ldrh	w0, [x19, 16]
	mov	w21, 0
.L1012:
	cmp	w0, w1
	beq	.L1013
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	mov	w24, w0
	cmn	w0, #1
	beq	.L1014
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
.L1015:
	cmp	x5, x1
	bne	.L1019
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
.L1020:
	cmp	w21, w3
	bne	.L1025
	add	w23, w24, 1
	ldr	w0, [x4, 4]
	and	w23, w23, 65535
.L1128:
	lsr	w0, w0, 10
	bl	P2V_plane
	and	w28, w0, 65535
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0, 106]
	cmp	w0, w23
	bne	.L1027
	strh	w23, [x19, 2]
	strb	wzr, [x19, 6]
	strh	wzr, [x19, 4]
.L1027:
	ldr	w0, [x29, 156]
	cmp	w23, w27
	ccmp	w28, w0, 0, eq
	bne	.L1028
.L1133:
	mov	w2, w28
	mov	w1, w23
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
	b	.L1126
.L1013:
	add	w21, w21, 1
	and	w21, w21, 65535
	add	x0, x19, x21, sxtw 1
	ldrh	w0, [x0, 16]
	b	.L1012
.L1014:
	cbz	w27, .L1016
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1755
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1016:
	ldr	w0, [x29, 156]
	cmp	w0, 0
	ccmp	w21, w0, 4, ne
	beq	.L1017
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1756
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1017:
	strh	wzr, [x19, 2]
	b	.L1132
.L1019:
	ldrh	w2, [x5]
	cmp	w2, w10
	beq	.L1018
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
.L1018:
	add	x5, x5, 2
	b	.L1015
.L1025:
	ldr	w0, [x6]
	cbnz	w0, .L1021
	ldr	x8, [x6, 16]
	ldr	w5, [x8, 4]
	cmn	w5, #1
	beq	.L1022
	ldr	w1, [x23, 2624]
	mov	w0, w5
	bl	ftl_cmp_data_ver
	cbz	w0, .L1022
	add	w5, w5, 1
	str	w5, [x23, 2624]
.L1022:
	ldr	w0, [x8]
	cmn	w0, #1
	bne	.L1024
	ubfiz	x3, x3, 5, 16
	and	w23, w24, 65535
	add	x3, x4, x3
	ldr	w0, [x3, 4]
	b	.L1128
.L1021:
	mov	w7, w9
.L1024:
	add	w3, w3, 1
	add	x6, x6, 32
	and	w3, w3, 65535
	b	.L1020
.L1028:
	mov	w0, 65535
	adrp	x21, .LANCHOR3
	cmp	w7, w0
	bne	.L1029
	ldrb	w0, [x19, 8]
	cbnz	w0, .L1030
.L1029:
	add	x0, x21, :lo12:.LANCHOR3
	and	w3, w24, 65535
	ldr	w1, [x0, -36]
	cmn	w1, #1
	bne	.L1031
	str	w22, [x0, -36]
.L1031:
	add	w0, w27, 7
	cmp	w0, w24, uxth
	bge	.L1079
	sub	w25, w3, #7
	and	w25, w25, 65535
.L1032:
	add	x26, x20, :lo12:.LANCHOR0
	mov	w4, 65535
.L1033:
	cmp	w25, w3
	bhi	.L1040
	ldrh	w0, [x26, 36]
	mov	w24, 0
	ldr	x1, [x29, 144]
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
	b	.L1041
.L1079:
	mov	w25, w27
	b	.L1032
.L1035:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1034
	ldr	x5, [x26, 4016]
	ubfiz	x6, x24, 5, 16
	add	w24, w24, 1
	orr	w2, w25, w2, lsl 10
	add	x5, x5, x6
	and	w24, w24, 65535
	str	w2, [x5, 4]
.L1034:
	add	x1, x1, 2
.L1041:
	cmp	x0, x1
	bne	.L1035
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
.L1036:
	cmp	x24, x0
	bne	.L1039
	add	w25, w25, 1
	and	w25, w25, 65535
	b	.L1033
.L1039:
	ldr	w1, [x0, -16]
	cbnz	w1, .L1030
	ldr	x1, [x0]
	ldrh	w5, [x1]
	cmp	w5, w4
	beq	.L1038
	ldr	w1, [x1, 4]
	cmn	w1, #1
	beq	.L1038
	str	w1, [x2, -36]
.L1038:
	add	x0, x0, 32
	b	.L1036
.L1040:
	add	x0, x21, :lo12:.LANCHOR3
	mov	w1, -1
	str	w1, [x0, -36]
.L1030:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 1
	mov	w25, w27
	add	x20, x20, :lo12:.LANCHOR0
	strh	w0, [x21, -32]
	sub	x0, x21, #192
	bl	FtlMapBlkWriteDump_data
	sub	x0, x21, #24
	str	x0, [x29, 112]
.L1042:
	ldrh	w0, [x20, 36]
	mov	w24, 0
	ldr	x1, [x29, 144]
	mov	w4, 65535
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
.L1043:
	cmp	x0, x1
	bne	.L1045
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
.L1046:
	ldr	x0, [x29, 120]
	cmp	x0, x24
	bne	.L1073
	add	w25, w25, 1
	ldrh	w0, [x20, 106]
	and	w25, w25, 65535
	cmp	w0, w25
	bne	.L1042
	ldrh	w1, [x20, 36]
	mov	w0, 0
	strh	w25, [x19, 2]
	mov	w2, 65535
	strh	wzr, [x19, 4]
.L1074:
	cmp	w0, w1
	beq	.L1126
	ldr	x4, [x29, 144]
	ldrh	w3, [x4], 2
	str	x4, [x29, 144]
	cmp	w3, w2
	beq	.L1075
	strb	w0, [x19, 6]
	b	.L1126
.L1045:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1044
	ldr	x3, [x20, 4016]
	ubfiz	x5, x24, 5, 16
	add	w24, w24, 1
	orr	w2, w25, w2, lsl 10
	add	x3, x3, x5
	and	w24, w24, 65535
	str	w2, [x3, 4]
.L1044:
	add	x1, x1, 2
	b	.L1043
.L1073:
	ldr	x4, [x20, 4016]
	add	x4, x4, x24
	ldr	w5, [x4, 4]
	str	w5, [x29, 172]
	lsr	w0, w5, 10
	bl	P2V_plane
	and	w0, w0, 65535
	cmp	w25, w27
	bcc	.L1047
	ldr	w1, [x29, 156]
	ccmp	w1, w0, 0, eq
	bhi	.L1047
	cmp	w25, w23
	ccmp	w28, w0, 0, eq
	beq	.L1048
	ldr	w0, [x4]
	cmn	w0, #1
	beq	.L1049
	ldr	x3, [x4, 16]
	mov	w0, 61589
	ldrh	w1, [x3]
	cmp	w1, w0
	beq	.L1050
.L1057:
	ldrh	w0, [x19]
.L1131:
	bl	decrement_vpc_count
.L1047:
	add	x24, x24, 32
	b	.L1046
.L1050:
	ldr	w22, [x3, 4]
	cmn	w22, #1
	beq	.L1051
	ldr	w1, [x20, 2624]
	mov	w0, w22
	str	x3, [x29, 136]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29, 136]
	cbz	w0, .L1051
	add	w0, w22, 1
	str	w0, [x20, 2624]
.L1051:
	ldrh	w1, [x3]
	mov	w0, 61589
	cmp	w1, w0
	beq	.L1052
	ldr	x1, [x29, 128]
	mov	w2, 1905
	str	x3, [x29, 136]
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
	ldr	x3, [x29, 136]
.L1052:
	ldp	w26, w0, [x3, 8]
	add	x1, x29, 168
	str	w0, [x29, 164]
	mov	w2, 0
	mov	w0, w26
	bl	log2phys
	ldr	w1, [x21, -36]
	cmn	w1, #1
	beq	.L1053
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L1053
	ldr	w1, [x29, 164]
	cmn	w1, #1
	beq	.L1054
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
	bne	.L1055
.L1056:
	mov	w0, -1
	str	w0, [x29, 164]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
.L1064:
	ldr	w4, [x29, 164]
	cmn	w4, #1
	beq	.L1047
.L1078:
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	and	w26, w0, 65535
	ldrh	w0, [x20, 44]
	mov	w3, w26
	cmp	w0, w26
	bhi	.L1069
	ldr	x1, [x29, 128]
	mov	w2, 2006
	adrp	x0, .LC1
	str	w26, [x29, 136]
	add	x0, x0, :lo12:.LC1
	bl	printk
	ldr	w3, [x29, 136]
.L1069:
	ldr	x1, [x20, 2376]
	ubfiz	x0, x26, 1, 16
	ldrh	w0, [x1, x0]
	cbz	w0, .L1070
	mov	w0, w3
	b	.L1131
.L1054:
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	bne	.L1057
	mov	w2, 1
	add	x1, x29, 164
	mov	w0, w26
	bl	log2phys
	b	.L1057
.L1055:
	ldr	w0, [x4, 8]
	cmp	w26, w0
	bne	.L1056
	ldr	w0, [x4, 4]
	str	w0, [x29, 136]
	str	x4, [x29, 104]
	uxtw	x1, w0
	ldr	w0, [x21, -36]
	bl	ftl_cmp_data_ver
	cbz	w0, .L1056
	ldp	w0, w1, [x29, 168]
	ldr	x4, [x29, 104]
	cmp	w0, w1
	ldr	w1, [x29, 164]
	bne	.L1059
.L1129:
	mov	w0, w26
	bl	FtlReUsePrevPpa
	b	.L1056
.L1059:
	cmp	w0, w1
	beq	.L1056
	cmn	w0, #1
	beq	.L1060
	ldr	x4, [x3, 16]
	mov	w2, 0
	str	w0, [x3, 4]
	mov	w1, 1
	str	x4, [x29, 104]
	ldr	x0, [x20, 4016]
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x4, [x29, 104]
.L1061:
	adrp	x0, .LANCHOR0+4016
	ldr	x0, [x0, #:lo12:.LANCHOR0+4016]
	ldr	w0, [x0, x24]
	cmn	w0, #1
	beq	.L1062
	ldr	w3, [x4, 4]
	ldr	w0, [x21, -36]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1062
	ldr	w0, [x29, 136]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1056
.L1062:
	ldr	w1, [x29, 164]
	b	.L1129
.L1060:
	str	w0, [x3]
	b	.L1061
.L1053:
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	beq	.L1064
	mov	w2, 1
	add	x1, x29, 172
	mov	w0, w26
	bl	log2phys
	ldr	w4, [x29, 168]
	cmn	w4, #1
	beq	.L1064
	ldr	w0, [x29, 164]
	cmp	w4, w0
	beq	.L1078
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20, 2424]
	and	w0, w0, 65535
	cmp	w1, w0
	beq	.L1068
	ldrh	w1, [x20, 2472]
	cmp	w1, w0
	beq	.L1068
	ldrh	w1, [x20, 2520]
	cmp	w1, w0
	bne	.L1064
.L1068:
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
	beq	.L1064
	ldr	x3, [x29, 136]
	mov	w0, w22
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
	cbnz	w0, .L1064
	mov	w2, 1
	add	x1, x29, 168
	mov	w0, w26
	bl	log2phys
	b	.L1064
.L1070:
	mov	w1, w26
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	bl	printk
	b	.L1047
.L1049:
	ldr	w0, [x21, -28]
	cmp	w0, 31
	bhi	.L1071
	ldr	x1, [x29, 112]
	str	w5, [x1, w0, uxtw 2]
	add	w0, w0, 1
	str	w0, [x21, -28]
.L1071:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldr	w0, [x21, -36]
	cmn	w0, #1
	bne	.L1072
.L1130:
	str	w22, [x21, -36]
	b	.L1047
.L1072:
	cmp	w22, w0
	bcs	.L1047
	b	.L1130
.L1075:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1074
.L1048:
	strb	w28, [x19, 6]
	strh	w23, [x19, 2]
	b	.L1133
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
.L1135:
	subs	w22, w22, #1
	beq	.L1137
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1136
.L1137:
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
.L1136:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29, 68]
	cmn	w0, #1
	beq	.L1137
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
	b	.L1135
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
.L1147:
	ldrh	w0, [x23, 102]
	sub	w0, w0, #16
	cmp	w20, w0
	ble	.L1150
	lsl	w0, w20, 10
	mov	w2, 1
	str	w0, [x21, 4]
	mov	w1, w2
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1148
	ldr	w0, [x21, 4]
	mov	w2, 1
	mov	w1, w2
	add	w0, w0, 1
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
.L1148:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1149
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1149
	ldr	w1, [x22, 4]
	str	w1, [x23, 168]
	strh	w20, [x23, 160]
	ldrh	w1, [x22, 8]
	strh	w1, [x23, 164]
.L1150:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21, 160]
	cmp	w1, w0
	beq	.L1164
	ldrh	w1, [x21, 164]
	cmp	w1, w0
	beq	.L1154
	add	x0, x21, 3400
	lsl	w1, w1, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x21, 3400]
	cmn	w0, #1
	beq	.L1154
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1154
	ldr	w1, [x21, 168]
	ldr	w0, [x22, 4]
	cmp	w0, w1
	bls	.L1154
	str	w0, [x21, 168]
	ldrh	w1, [x21, 164]
	ldrh	w0, [x22, 8]
	strh	w1, [x21, 160]
	strh	w0, [x21, 164]
.L1154:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x23, x20, 3400
	mov	w24, 61649
	ldrh	w0, [x20, 160]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20, 162]
.L1156:
	tbz	w21, #31, .L1159
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 254
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1158:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 10]
	mov	w2, 65535
	strh	w0, [x1, 166]
	ldrh	w0, [x22, 12]
	cmp	w0, w2
	beq	.L1161
	ldr	w2, [x1, 32]
	cmp	w0, w2
	beq	.L1161
	ldrh	w1, [x1, 46]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L1161
	cmp	w0, w1
	bcs	.L1161
	bl	FtlSysBlkNumInit
.L1161:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	add	x22, x19, 192
	add	x21, x21, :lo12:.LANCHOR3
	mov	w20, 0
.L1162:
	ldrh	w0, [x19, 58]
	cmp	w20, w0
	bcc	.L1163
	mov	w0, 0
.L1146:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1149:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L1147
.L1159:
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
	beq	.L1157
	ldrh	w0, [x22]
	cmp	w0, w24
	beq	.L1158
.L1157:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L1156
.L1163:
	ldrh	w2, [x21, -200]
	ldr	x0, [x19, 3408]
	mul	w1, w2, w20
	lsl	w2, w2, 2
	add	w20, w20, 1
	add	x1, x0, x1, lsl 2
	ldr	x0, [x22], 8
	bl	ftl_memcpy
	b	.L1162
.L1164:
	mov	w0, -1
	b	.L1146
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
.L1177:
	ldrh	w0, [x19, 58]
	cmp	w24, w0
	bcc	.L1183
	add	x20, x21, :lo12:.LANCHOR0
	mov	w19, 0
.L1184:
	ldrh	w0, [x20, 118]
	cmp	w0, w19
	bhi	.L1185
	add	x20, x20, 160
	ldrh	w19, [x20, 12]
	mov	w22, 65535
	sub	w19, w19, #1
	and	w19, w19, 65535
.L1186:
	ldrh	w0, [x20, 12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1190
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1187
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L1188
	mov	w0, w19
	bl	FtlBbmMapBadBlock
.L1187:
	sub	w19, w19, #1
	and	w19, w19, 65535
	b	.L1186
.L1183:
	ldr	x0, [x19, 3432]
	mov	w2, 65535
	ldr	x27, [x19, 3440]
	ldrh	w1, [x25]
	stp	x0, x27, [x20, 8]
	cmp	w1, w2
	beq	.L1178
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
.L1179:
	mov	w0, w23
	add	w24, w24, 1
	bl	FtlBbmMapBadBlock
	add	x26, x26, 8
	add	x25, x25, 2
	b	.L1177
.L1178:
	mov	w1, w24
	bl	FlashGetBadBlockList
	ldr	x0, [x20, 8]
	ldr	x1, [x26]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x19, 102]
.L1181:
	sub	w22, w22, #1
	and	w22, w22, 65535
.L1180:
	ldrh	w0, [x19, 102]
	madd	w0, w24, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1181
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
	bne	.L1179
	mov	w0, w23
	bl	FtlBbmMapBadBlock
	b	.L1180
.L1185:
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	and	w19, w19, 65535
	b	.L1184
.L1188:
	ldrh	w0, [x20]
	cmp	w0, w22
	bne	.L1189
	strh	w19, [x20]
	b	.L1187
.L1189:
	strh	w19, [x20, 4]
.L1190:
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
	bhi	.L1204
	ldrh	w21, [x0, 112]
	adrp	x26, .LANCHOR3
	add	x25, x26, :lo12:.LANCHOR3
	mov	w23, 0
	sub	x25, x25, #120
	lsr	w21, w27, w21
.L1198:
	cbnz	w28, .L1203
.L1196:
	mov	w0, w23
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1203:
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
	cbz	w4, .L1200
	cmp	w19, w1
	beq	.L1200
	ldr	x0, [x2, 4048]
	mov	w2, 1
	str	x0, [x29, 120]
	add	x0, x29, 144
	mov	w1, w2
	str	x0, [x29, 128]
	str	w4, [x29, 116]
	add	x0, x29, 112
	bl	FlashReadPages
.L1201:
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
	b	.L1198
.L1200:
	add	x0, x22, :lo12:.LANCHOR0
	mov	w1, 0
	ldrh	w2, [x0, 114]
	ldr	x0, [x0, 4048]
	bl	ftl_memset
	b	.L1201
.L1204:
	mov	w23, -1
	b	.L1196
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
	bne	.L1212
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
.L1212:
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
	bhi	.L1219
	add	w2, w2, 1
	strh	w2, [x1, 104]
	mov	w2, 1
.L1216:
	adrp	x1, .LANCHOR0
	cbnz	w0, .L1217
	add	x0, x1, :lo12:.LANCHOR0
	ldr	x0, [x0, 4072]
	ldr	w3, [x0, 20]
	ldr	w0, [x0, 16]
	add	w2, w2, w3
	cmp	w0, w2
	bcc	.L1221
.L1217:
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
.L1219:
	mov	w2, 32
	b	.L1216
.L1221:
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
	bhi	.L1233
	ldrh	w3, [x20, 112]
	adrp	x24, .LANCHOR3
	mov	x23, x2
	add	x24, x24, :lo12:.LANCHOR3
	mov	w22, 0
	lsr	w25, w28, w3
.L1227:
	cbnz	w27, .L1232
.L1225:
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 192
	ret
.L1232:
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
	cbz	w1, .L1229
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
.L1231:
	add	w25, w25, 1
	sub	w27, w27, w19
	add	w28, w28, w19
	add	x23, x23, x26, sxtw
	b	.L1227
.L1229:
	mov	w2, w26
	mov	w1, 0
	mov	x0, x23
	bl	ftl_memset
	b	.L1231
.L1233:
	mov	w22, -1
	b	.L1225
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
	beq	.L1236
	adrp	x1, .LC100
	adrp	x0, .LC71
	add	x1, x1, :lo12:.LC100
	add	x0, x0, :lo12:.LC71
	bl	printk
	ldr	x0, [x19, 4072]
	mov	w1, 0
	ldrh	w2, [x19, 4056]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1236:
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
	beq	.L1239
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	str	w21, [x20, 2720]
.L1239:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0, 2720]
	cmp	w3, w2
	bne	.L1240
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
.L1240:
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
.L1245:
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
	blt	.L1246
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
.L1246:
	ldrh	w0, [x19, 2658]
	ldr	w1, [x20]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2658]
	cmn	w1, #1
	bne	.L1247
	cmp	w0, 1
	bne	.L1248
	adrp	x0, .LC1
	mov	w2, 1124
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1248:
	ldrh	w0, [x19, 2658]
	cmp	w0, 1
	bne	.L1249
	ldrh	w0, [x19, 108]
	sub	w0, w0, #1
	strh	w0, [x19, 2658]
.L1249:
	add	w22, w22, 1
	and	w22, w22, 65535
	cmp	w22, 3
	bls	.L1245
	add	x23, x23, :lo12:.LANCHOR0
	adrp	x0, .LC101
	mov	w2, w22
	add	x0, x0, :lo12:.LC101
	ldr	w1, [x23, 3404]
	bl	printk
.L1251:
	b	.L1251
.L1247:
	cmp	w0, 1
	beq	.L1245
	mov	w0, 65535
	cmp	w24, w0
	beq	.L1252
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
.L1252:
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
	adrp	x0, .LC91
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x1, 552
	add	x0, x0, :lo12:.LC91
	str	x23, [sp, 48]
	bl	printk
	ldr	x0, [x19, 4080]
	mov	w21, 0
	ldrh	w2, [x19, 46]
	mov	w1, 0
	lsl	w2, w2, 1
	bl	ftl_memset
.L1258:
	ldr	w0, [x19, 2584]
	cmp	w21, w0
	bcc	.L1260
	adrp	x22, .LC102
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC102
	mov	w19, 0
	mov	w23, 65535
.L1261:
	ldrh	w0, [x20, 44]
	cmp	w0, w19
	bhi	.L1265
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L1260:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1259
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x19, 4080]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L1259:
	add	w21, w21, 1
	b	.L1258
.L1265:
	ldr	x0, [x20, 2376]
	ubfiz	x21, x19, 1, 16
	ldrh	w2, [x0, x21]
	ldr	x0, [x20, 4080]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L1263
	cmp	w2, w23
	beq	.L1263
	mov	w1, w19
	mov	x0, x22
	bl	printk
	ldrh	w0, [x20, 2424]
	cmp	w0, w19
	beq	.L1263
	ldrh	w0, [x20, 2520]
	cmp	w0, w19
	beq	.L1263
	ldrh	w0, [x20, 2472]
	cmp	w0, w19
	beq	.L1263
	ldr	x0, [x20, 4080]
	ldrh	w1, [x0, x21]
	ldr	x0, [x20, 2376]
	strh	w1, [x0, x21]
	mov	w0, w19
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1263:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1261
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
	bcs	.L1271
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2681
	add	x1, x1, 576
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1271:
	mov	w0, 65535
	cmp	w21, w0
	beq	.L1272
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x21, 1, 16
	ldr	x1, [x1, 2376]
	ldrh	w0, [x1, x0]
	cbz	w0, .L1273
	mov	w0, w21
	bl	INSERT_DATA_LIST
.L1272:
	mov	w0, 1
	adrp	x1, .LANCHOR3
	strb	w0, [x22, 8]
	add	x0, x1, :lo12:.LANCHOR3
	mov	w2, 65535
	mov	x20, x1
	ldrh	w0, [x0, -48]
	cmp	w0, w2
	beq	.L1274
	cmp	w21, w0
	bne	.L1275
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x19, 2376]
	ldrh	w1, [x2, x1]
	cbz	w1, .L1276
.L1275:
	bl	update_vpc_list
.L1276:
	add	x1, x20, :lo12:.LANCHOR3
	mov	w0, -1
	strh	w0, [x1, -48]
.L1274:
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
.L1273:
	mov	w0, w21
	bl	INSERT_FREE_LIST
	b	.L1272
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
.L1282:
	cmp	x20, x24
	beq	.L1289
	sub	x23, x20, #4
	b	.L1290
.L1285:
	ldr	w0, [x20]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19]
	cmp	w1, w0, uxth
	bne	.L1283
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
.L1283:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1284
	mov	x0, x19
	bl	allocate_new_data_superblock
.L1284:
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
.L1290:
	ldr	w0, [x20, -4]
	cmn	w0, #1
	beq	.L1285
	ldrb	w1, [x19, 6]
	ldrh	w0, [x21, 36]
	cmp	w1, w0
	bcc	.L1286
	adrp	x0, .LC1
	mov	w2, 933
	add	x1, x25, 608
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1286:
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
	beq	.L1287
	ldr	x2, [x21, 2376]
	ubfiz	x0, x1, 1, 16
	ldrh	w0, [x2, x0]
	cbnz	w0, .L1288
	adrp	x0, .LC103
	mov	w2, 0
	add	x0, x0, :lo12:.LC103
	bl	printk
.L1288:
	mov	w0, w23
	bl	decrement_vpc_count
.L1287:
	add	x20, x20, 32
	b	.L1282
.L1289:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x22, 36]
	cmp	w1, w0
	bcc	.L1281
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 948
	add	x1, x1, 608
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1281:
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
	cbz	x0, .L1297
	ldr	x3, [x19, 112]
	mov	w2, 0
	mov	w1, 1
	bl	FtlProgPages
	str	xzr, [x19, -56]
.L1297:
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
	bne	.L1307
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlSysFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1307:
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
	bhi	.L1318
	cmp	w19, 31
	bhi	.L1312
.L1326:
	mov	w0, 0
.L1310:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1312:
	bl	FtlCacheWriteBack
	ldrh	w1, [x22, 62]
	udiv	w22, w21, w1
	msub	w21, w1, w22, w21
	ands	w21, w21, 65535
	beq	.L1313
	sub	w1, w1, w21
	add	w22, w22, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L1313:
	adrp	x21, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, -1
	str	w0, [x29, 60]
.L1314:
	ldrh	w0, [x20, 62]
	cmp	w19, w0
	bcs	.L1316
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	w1, [x0, 120]
	cmp	w1, 32
	bls	.L1326
	str	wzr, [x0, 120]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1326
.L1316:
	mov	w2, 0
	add	x1, x29, 56
	mov	w0, w22
	bl	log2phys
	ldr	w0, [x29, 56]
	cmn	w0, #1
	beq	.L1315
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
.L1315:
	ldrh	w0, [x20, 62]
	add	w22, w22, 1
	sub	w19, w19, w0
	b	.L1314
.L1318:
	mov	w0, -1
	b	.L1310
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
	bne	.L1328
.L1338:
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3-40
	add	x21, x20, 2520
	str	wzr, [x0, #:lo12:.LANCHOR3-40]
	mov	w0, 65535
	ldrh	w1, [x20, 2520]
	cmp	w1, w0
	bne	.L1351
.L1354:
	mov	w0, 0
.L1327:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1328:
	cbz	w0, .L1331
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w4, [x0, 4]
	cmp	w4, w3
	beq	.L1332
.L1333:
	mov	w1, 2
.L1331:
	add	x21, x19, :lo12:.LANCHOR0
	add	x0, x21, 2520
	bl	FtlGcScanTempBlk
	str	w0, [x29, 92]
	cmn	w0, #1
	beq	.L1334
	ldr	x1, [x21, 2384]
	ubfiz	x20, x20, 1, 16
	ldrh	w0, [x1, x20]
	cmp	w0, 4
	bls	.L1335
	sub	w0, w0, #5
	strh	w0, [x1, x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L1335:
	adrp	x0, .LANCHOR3
	add	x1, x0, :lo12:.LANCHOR3
	mov	x20, x0
	ldr	w1, [x1, -40]
	cbnz	w1, .L1336
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19, 2816]
	add	w0, w0, 1
	str	w0, [x19, 2816]
	ldr	w0, [x29, 92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1336:
	add	x0, x20, :lo12:.LANCHOR3
	str	wzr, [x0, -40]
.L1347:
	mov	w0, 1
	b	.L1327
.L1332:
	strh	wzr, [x0, 4]
	ldrh	w0, [x2, 2416]
	cmp	w0, 17
	bhi	.L1333
	b	.L1331
.L1334:
	adrp	x0, .LANCHOR2+4
	ldrh	w1, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1347
	b	.L1338
.L1351:
	bl	FtlCacheWriteBack
	ldrb	w0, [x21, 7]
	ldrh	w2, [x20, 106]
	ldrh	w1, [x20, 3304]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L1339
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 162
	add	x1, x1, 624
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1339:
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
.L1340:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21, 3304]
	cmp	w0, w20
	bhi	.L1344
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x21, 2520]
	ldr	x2, [x21, 2376]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1345
	bl	INSERT_DATA_LIST
.L1346:
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
	ble	.L1354
	mov	w0, 20
	strh	w20, [x19, 2672]
	strh	w0, [x19, 3232]
	b	.L1354
.L1344:
	mov	w24, 12
	ldr	x26, [x21, 3312]
	ldr	w0, [x21, 2584]
	umull	x24, w20, w24
	add	x23, x26, x24
	ldr	w1, [x23, 8]
	cmp	w1, w0
	bcc	.L1341
	mov	w2, 168
	mov	x1, x22
	mov	x0, x25
	bl	printk
.L1341:
	ldr	w0, [x23, 8]
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x26, x24]
	ldr	w1, [x29, 92]
	cmp	w0, w1
	bne	.L1342
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	mov	w21, w0
	ldr	w0, [x23, 8]
	mov	w2, 1
	add	x1, x23, 4
	bl	log2phys
	mov	w0, w21
.L1353:
	bl	decrement_vpc_count
.L1343:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1340
.L1342:
	ldr	w0, [x23, 4]
	cmp	w1, w0
	beq	.L1343
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2520]
	b	.L1353
.L1345:
	bl	INSERT_FREE_LIST
	b	.L1346
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
	bcc	.L1355
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	sub	x0, x19, #192
	bl	FtlMapBlkWriteDump_data
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19, -40]
.L1355:
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
.L1361:
	ldr	w1, [x19, 3248]
	cmp	w10, w1
	bcc	.L1364
	ldr	x0, [x19, 4024]
	bl	FtlGcBufFree
	str	wzr, [x19, 3248]
	ldrh	w0, [x19, 2524]
	cbnz	w0, .L1365
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	b	.L1367
.L1364:
	ldr	x0, [x19, 4024]
	ubfiz	x1, x10, 5, 16
	add	x3, x0, x1
	ldr	w0, [x0, x1]
	cmn	w0, #1
	bne	.L1362
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
.L1367:
	mov	w0, 1
.L1360:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1362:
	ldr	x0, [x3, 16]
	ldr	w1, [x3, 4]
	ldp	w2, w0, [x0, 8]
	bl	FtlGcUpdatePage
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L1361
.L1365:
	mov	w0, 0
	b	.L1360
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
	beq	.L1369
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1370
.L1369:
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
.L1370:
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
	cbnz	w2, .L1444
	ldrh	w2, [x1, 2400]
	cmp	w2, 47
	bls	.L1444
	mov	w23, w0
	adrp	x0, .LANCHOR2+4
	ldrh	w2, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1374
.L1377:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 3322]
	cmp	w1, w2
	bne	.L1375
.L1376:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w20, 65535
	ldr	w1, [x21, 3236]
	ldrh	w0, [x21, 2672]
	add	w1, w1, 1
	add	w1, w1, w23, lsl 7
	str	w1, [x21, 3236]
	cmp	w0, w20
	bne	.L1378
	ldrh	w20, [x21, 2520]
	cmp	w20, w0
	bne	.L1447
	ldrh	w22, [x21, 3320]
	cmp	w22, w20
	bne	.L1378
	ldrh	w0, [x21, 2416]
	mov	w2, 1024
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L1450
	adrp	x0, .LANCHOR3+124
	str	wzr, [x21, 3236]
	strh	wzr, [x0, #:lo12:.LANCHOR3+124]
	bl	GetSwlReplaceBlock
	and	w20, w0, 65535
	cmp	w20, w22
	bne	.L1451
	ldrh	w1, [x21, 2416]
	ldrh	w0, [x21, 3234]
	cmp	w1, w0
	bcs	.L1381
	mov	w0, 64
	bl	List_get_gc_head_node
	and	w0, w0, 65535
	cmp	w0, w20
	beq	.L1383
	ldr	x1, [x21, 2376]
	ubfiz	x0, x0, 1, 16
	ldrh	w0, [x1, x0]
	cmp	w0, 7
	bhi	.L1384
	mov	w0, 0
	bl	List_get_gc_head_node
	and	w22, w0, 65535
	mov	w0, 128
	strh	w0, [x21, 3234]
	cmp	w22, w20
	beq	.L1383
.L1380:
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
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	bl	printk
	b	.L1383
.L1374:
	ldrh	w1, [x1, 2520]
	cmp	w1, w0
	beq	.L1377
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	cbz	w0, .L1377
	mov	w0, 1
	b	.L1372
.L1375:
	ldrh	w3, [x0, 3320]
	cmp	w3, w2
	bne	.L1376
	strh	w1, [x0, 3320]
	mov	w1, -1
	strh	w1, [x0, 3322]
	b	.L1376
.L1384:
	mov	w0, 64
.L1476:
	strh	w0, [x21, 3234]
.L1383:
	bl	FtlGcReFreshBadBlk
.L1378:
	cmp	w23, 0
	mov	w0, 65535
	ccmp	w20, w0, 0, eq
	bne	.L1386
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2416]
	cmp	w0, 24
	bhi	.L1452
	cmp	w0, 16
	ldrh	w21, [x1, 106]
	bls	.L1388
	lsr	w21, w21, 5
.L1387:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1, 3232]
	cmp	w2, w0
	bcs	.L1391
	ldrh	w0, [x1, 2520]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1392
	ldrh	w2, [x1, 3320]
	cmp	w2, w0
	bne	.L1392
	adrp	x0, .LANCHOR3+124
	ldrh	w0, [x0, #:lo12:.LANCHOR3+124]
	cbnz	w0, .L1393
	ldr	w2, [x1, 2584]
	ldr	w3, [x1, 2580]
	add	w2, w2, w2, lsl 1
	cmp	w3, w2, lsr 2
	bcs	.L1394
.L1393:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2, 2652]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2, 3232]
.L1395:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19, 3244]
.L1372:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1381:
	mov	w0, 80
	b	.L1476
.L1451:
	mov	w22, w20
	b	.L1380
.L1447:
	mov	w20, w0
	b	.L1378
.L1450:
	mov	w20, w22
	b	.L1378
.L1388:
	cmp	w0, 12
	bls	.L1389
	lsr	w21, w21, 4
	b	.L1387
.L1389:
	mov	w1, w21
	cmp	w0, 9
	lsr	w21, w21, 2
	csel	w21, w21, w1, cs
	b	.L1387
.L1452:
	mov	w21, 1
	b	.L1387
.L1394:
	mov	w2, 18
	strh	w2, [x1, 3232]
	b	.L1395
.L1392:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2652]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 3232]
.L1391:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 3324]
	cbz	w0, .L1453
	add	w21, w21, 32
	and	w21, w21, 65535
.L1453:
	mov	w20, 65535
.L1397:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 2672]
	cmp	w1, w2
	bne	.L1406
	cmp	w20, w1
	beq	.L1407
	strh	w20, [x0, 2672]
.L1408:
	add	x5, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldrh	w0, [x5, 2672]
	strb	wzr, [x5, 2680]
	cmp	w0, w1
	beq	.L1406
	bl	IsBlkInGcList
	cbz	w0, .L1411
	mov	w0, -1
	strh	w0, [x5, 2672]
.L1411:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	add	x23, x22, 2672
	ldrh	w1, [x22, 2672]
	cmp	w1, w0
	beq	.L1406
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
.L1406:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w2, [x0, 2424]
	cmp	w2, w1
	beq	.L1412
	ldrh	w0, [x0, 2472]
	cmp	w0, w1
	beq	.L1412
	adrp	x24, .LANCHOR1
	add	x24, x24, :lo12:.LANCHOR1
	add	x24, x24, 648
.L1413:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w23, [x22, 2672]
	cmp	w23, w0
	bne	.L1414
	mov	w25, 2
	str	wzr, [x22, 3244]
.L1415:
	ldrh	w5, [x22, 3240]
	mov	w0, w5
	bl	List_get_gc_head_node
	and	w6, w0, 65535
	strh	w6, [x22, 2672]
	cmp	w6, w23
	bne	.L1416
	strh	wzr, [x22, 3240]
	mov	w0, 8
	b	.L1372
.L1386:
	add	x5, x19, :lo12:.LANCHOR0
	ldrh	w1, [x5, 2520]
	cmp	w1, w0
	bne	.L1456
	ldrh	w0, [x5, 3320]
	cmp	w0, w1
	bne	.L1456
	cmp	w20, w0
	bne	.L1456
	ldrh	w0, [x5, 2672]
	cmp	w0, w20
	beq	.L1398
.L1456:
	mov	w21, 1
	b	.L1397
.L1398:
	ldrh	w7, [x5, 2416]
	adrp	x21, .LANCHOR3
	ldrh	w0, [x5, 3232]
	str	wzr, [x5, 3244]
	cmp	w0, w7
	bcs	.L1399
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, 124]
	cbnz	w0, .L1400
	ldr	w0, [x5, 2584]
	ldr	w1, [x5, 2580]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bcs	.L1401
.L1400:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2652]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x19, 3232]
.L1402:
	bl	FtlReadRefresh
.L1477:
	add	x21, x21, :lo12:.LANCHOR3
	b	.L1478
.L1401:
	mov	w0, 18
	strh	w0, [x5, 3232]
	b	.L1402
.L1399:
	add	x21, x21, :lo12:.LANCHOR3
	ldrh	w0, [x21, 124]
	cbnz	w0, .L1456
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
	ble	.L1404
	sub	w6, w6, #1
	cmp	w7, w6
	blt	.L1404
	bl	FtlReadRefresh
.L1478:
	ldrh	w0, [x21, 124]
	b	.L1372
.L1404:
	cbnz	w1, .L1456
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x19, 2416]
	add	w0, w0, 1
	b	.L1372
.L1407:
	ldrh	w1, [x0, 3320]
	cmp	w1, w20
	beq	.L1408
	ldr	x2, [x0, 2376]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2, x1]
	cbnz	w1, .L1409
	mov	w1, -1
	strh	w1, [x0, 3320]
.L1409:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3320]
	strh	w1, [x0, 2672]
	mov	w1, -1
	strh	w1, [x0, 3320]
	b	.L1408
.L1412:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19, 2672]
.L1479:
	adrp	x0, .LANCHOR3+124
	ldrh	w0, [x0, #:lo12:.LANCHOR3+124]
	b	.L1372
.L1416:
	mov	w0, w6
	bl	IsBlkInGcList
	add	w5, w5, 1
	cbz	w0, .L1417
	strh	w5, [x22, 3240]
	b	.L1415
.L1417:
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
	bgt	.L1419
	cmp	w5, 48
	bls	.L1420
	cmp	w3, 8
	bls	.L1420
	ldrh	w3, [x22, 3292]
	cmp	w3, 35
	bhi	.L1420
.L1419:
	strh	wzr, [x22, 3240]
.L1420:
	ldrh	w1, [x2, x1]
	cmp	w0, w1
	bgt	.L1421
	cmp	w20, w23
	bne	.L1421
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19, 3240]
	strh	w0, [x19, 2672]
	b	.L1479
.L1421:
	cbnz	w1, .L1422
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x22, 3240]
	add	w0, w0, 1
	strh	w0, [x22, 3240]
	b	.L1415
.L1422:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0, 2680]
	ldrh	w0, [x0, 2424]
	cmp	w0, w6
	bne	.L1423
	adrp	x0, .LC1
	mov	w2, 658
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1423:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w0, [x0, 2472]
	cmp	w1, w0
	bne	.L1424
	adrp	x0, .LC1
	mov	w2, 659
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1424:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w0, [x0, 2520]
	cmp	w1, w0
	bne	.L1425
	adrp	x0, .LC1
	mov	w2, 660
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1425:
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
.L1414:
	bl	FtlReadRefresh
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	ldrh	w26, [x0, 106]
	str	w1, [x0, 4000]
	ldrh	w0, [x0, 2674]
	add	w1, w0, w21
	cmp	w1, w26
	ble	.L1426
	sub	w21, w26, w0
	and	w21, w21, 65535
.L1426:
	mov	w25, 0
.L1427:
	cmp	w21, w25, uxth
	bls	.L1435
	add	x0, x19, :lo12:.LANCHOR0
	mov	w23, 0
	add	x3, x0, 2688
	mov	w1, 0
	mov	w6, 65535
	ldrh	w4, [x0, 2674]
	ldrh	w7, [x0, 36]
	add	w4, w4, w25
	b	.L1436
.L1429:
	ldrh	w2, [x3]
	cmp	w2, w6
	beq	.L1428
	ldr	x5, [x0, 3280]
	ubfiz	x8, x23, 5, 16
	add	w23, w23, 1
	orr	w2, w4, w2, lsl 10
	add	x5, x5, x8
	and	w23, w23, 65535
	str	w2, [x5, 4]
.L1428:
	add	w1, w1, 1
	add	x3, x3, 2
	and	w1, w1, 65535
.L1436:
	cmp	w1, w7
	bne	.L1429
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, w23
	mov	x27, 0
	ldrb	w2, [x22, 2680]
	ldr	x0, [x22, 3280]
	bl	FlashReadPages
	ubfiz	x0, x23, 5, 16
	str	x0, [x29, 104]
.L1430:
	ldr	x0, [x29, 104]
	cmp	x27, x0
	bne	.L1434
	add	w25, w25, 1
	b	.L1427
.L1434:
	ldr	x0, [x22, 3280]
	add	x1, x0, x27
	ldr	w0, [x0, x27]
	cmn	w0, #1
	beq	.L1431
	ldr	x23, [x1, 16]
	mov	w0, 61589
	ldrh	w1, [x23]
	cmp	w1, w0
	bne	.L1431
	ldr	w28, [x23, 8]
	cmn	w28, #1
	bne	.L1432
	adrp	x0, .LC1
	mov	w2, 696
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1432:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w28
	bl	log2phys
	ldr	x0, [x22, 3280]
	ldr	w1, [x29, 124]
	add	x0, x0, x27
	ldr	w2, [x0, 4]
	cmp	w2, w1
	bne	.L1431
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
	beq	.L1433
	ldrh	w0, [x22, 2524]
	cbnz	w0, .L1431
.L1433:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1431
	add	x19, x19, :lo12:.LANCHOR0
	add	x28, x28, :lo12:.LANCHOR3
	str	wzr, [x19, 4000]
	ldrh	w0, [x28, 124]
	b	.L1372
.L1431:
	add	x27, x27, 32
	b	.L1430
.L1435:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2674]
	add	w21, w21, w0
	and	w21, w21, 65535
	strh	w21, [x1, 2674]
	cmp	w26, w21
	adrp	x21, .LANCHOR3
	bls	.L1437
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w1, [x0, 126]
	ldrh	w0, [x0, 128]
	cmp	w1, w0
	bne	.L1438
.L1437:
	add	x22, x19, :lo12:.LANCHOR0
	ldr	w0, [x22, 3248]
	cbz	w0, .L1439
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1439
	str	wzr, [x22, 4000]
	b	.L1477
.L1439:
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, 126]
	cbnz	w0, .L1440
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2672]
	ldr	x2, [x1, 2376]
	lsl	x0, x0, 1
	ldrh	w3, [x2, x0]
	cbz	w3, .L1440
	strh	wzr, [x2, x0]
	ldrh	w0, [x1, 2672]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1440:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 2672]
.L1438:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2416]
	cmp	w0, 2
	bhi	.L1441
	ldrh	w21, [x1, 106]
	b	.L1413
.L1441:
	add	x21, x21, :lo12:.LANCHOR3
	str	wzr, [x1, 4000]
	ldrh	w1, [x21, 124]
	cmp	w1, 0
	csinc	w0, w1, w0, ne
	b	.L1372
.L1444:
	mov	w0, 0
	b	.L1372
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
	bne	.L1483
	mov	x2, x3
	mov	w1, w27
	add	w0, w19, 256
	bl	FtlVendorPartRead
	mov	w21, w0
.L1482:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1483:
	add	w0, w1, w2
	str	w0, [x29, 128]
	adrp	x0, .LANCHOR0
	add	x25, x0, :lo12:.LANCHOR0
	add	w2, w1, w2
	str	x0, [x29, 96]
	ldr	w1, [x25, 144]
	cmp	w2, w1
	bhi	.L1503
	adrp	x0, .LANCHOR2
	ldr	w21, [x0, #:lo12:.LANCHOR2]
	cmn	w21, #1
	beq	.L1482
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
.L1485:
	cbnz	w22, .L1501
	ldr	x0, [x29, 96]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3324]
	cbnz	w1, .L1502
	ldrh	w0, [x0, 2416]
	cmp	w0, 31
	bhi	.L1482
.L1502:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	b	.L1482
.L1501:
	add	x1, x29, 156
	mov	w2, 0
	mov	w0, w20
	bl	log2phys
	ldr	w1, [x29, 156]
	cmn	w1, #1
	bne	.L1486
	mov	w5, 0
.L1487:
	ldrh	w0, [x25, 62]
	cmp	w5, w0
	bcc	.L1489
.L1490:
	add	w20, w20, 1
	subs	w22, w22, #1
	beq	.L1494
	ldrh	w0, [x25, 36]
	cmp	w28, w0, lsl 2
	bne	.L1485
.L1494:
	cbz	w28, .L1485
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
.L1500:
	ldr	x0, [x25, 4016]
	add	x0, x0, x28
	ldr	w1, [x0, 24]
	cmp	w26, w1
	bne	.L1496
	ldr	x1, [x0, 8]
	ldr	x0, [x25, 3992]
	cmp	x1, x0
	bne	.L1497
	ldr	x0, [x29, 120]
	ldr	w2, [x29, 116]
	add	x1, x1, x0
	mov	x0, x23
.L1513:
	bl	ftl_memcpy
.L1497:
	ldr	x0, [x25, 4016]
	add	x1, x0, x28
	ldr	w2, [x0, x28]
	cmn	w2, #1
	bne	.L1498
	ldr	w0, [x25, 2792]
	mov	w21, w2
	add	w0, w0, 1
	str	w0, [x25, 2792]
.L1498:
	ldr	w0, [x1]
	cmp	w0, 256
	bne	.L1499
	ldr	w0, [x1, 4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1499:
	ldr	x0, [x29, 104]
	add	x28, x28, 32
	cmp	x0, x28
	bne	.L1500
	mov	w28, 0
	b	.L1485
.L1489:
	madd	w0, w20, w0, w5
	cmp	w19, w0
	bhi	.L1488
	ldr	w1, [x29, 128]
	cmp	w1, w0
	bls	.L1488
	sub	w0, w0, w19
	str	w5, [x29, 120]
	lsl	w0, w0, 9
	mov	w2, 512
	mov	w1, 0
	add	x0, x23, x0
	bl	ftl_memset
	ldr	w5, [x29, 120]
.L1488:
	add	w5, w5, 1
	b	.L1487
.L1486:
	ldr	x0, [x25, 4016]
	ubfiz	x2, x28, 5, 32
	cmp	w20, w26
	add	x0, x0, x2
	str	w1, [x0, 4]
	ldrh	w0, [x25, 62]
	bne	.L1491
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
	bne	.L1492
	str	x23, [x1, 8]
.L1492:
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
	b	.L1490
.L1491:
	cmp	w20, w24
	bne	.L1493
	ldr	x5, [x25, 4016]
	ldr	x1, [x25, 3976]
	add	x5, x5, x2
	ldr	w3, [x29, 128]
	str	x1, [x5, 8]
	mul	w1, w20, w0
	sub	w3, w3, w1
	str	w3, [x29, 132]
	cmp	w0, w3
	bne	.L1492
	sub	w1, w1, w19
	lsl	w1, w1, 9
	add	x1, x23, x1
	str	x1, [x5, 8]
	b	.L1492
.L1493:
	ldr	x1, [x25, 4016]
	mul	w0, w0, w20
	add	x1, x1, x2
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	str	x0, [x1, 8]
	b	.L1492
.L1496:
	cmp	w24, w1
	bne	.L1497
	ldr	x1, [x0, 8]
	ldr	x0, [x25, 3976]
	cmp	x1, x0
	bne	.L1497
	ldrh	w0, [x25, 62]
	ldr	w2, [x29, 112]
	mul	w0, w0, w24
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	b	.L1513
.L1503:
	mov	w21, -1
	b	.L1482
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
	bne	.L1517
	mov	x2, x3
	mov	w1, w26
	add	w0, w23, 256
	bl	FtlVendorPartWrite
.L1516:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	ret
.L1517:
	add	w3, w1, w2
	adrp	x20, .LANCHOR0
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w0, [x2, 144]
	cmp	w3, w0
	bhi	.L1559
	adrp	x1, .LANCHOR2
	str	x1, [x29, 136]
	ldr	w0, [x1, #:lo12:.LANCHOR2]
	cmn	w0, #1
	beq	.L1516
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
	cbz	x3, .L1519
	ldr	w0, [x3, 24]
	cmp	w22, w0
	beq	.L1520
	bl	FtlCacheWriteBack
.L1519:
	ldr	x0, [x29, 136]
	add	x19, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	cbz	w0, .L1561
	ldrh	w1, [x19, 2428]
	add	x0, x19, 2424
	add	x19, x19, 2472
	cmp	w1, 0
	csel	x19, x19, x0, eq
.L1522:
	adrp	x0, .LANCHOR1
	mov	w21, w22
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 672
	str	x0, [x29, 160]
.L1523:
	cbz	w25, .L1557
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1524
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1012
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1524:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1525
	bl	FtlCacheWriteBack
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, 2424
	cmp	x19, x1
	bne	.L1526
	add	x0, x0, 2472
	ldrh	w1, [x0, 4]
	cbnz	w1, .L1527
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
.L1527:
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
.L1528:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1525
	mov	x0, x19
	bl	allocate_new_data_superblock
.L1525:
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
	bcc	.L1529
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1045
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1529:
	str	xzr, [x29, 168]
.L1530:
	ldr	w1, [x29, 132]
	ldr	w28, [x29, 168]
	cmp	w28, w1
	bcc	.L1551
	mov	x28, x1
.L1531:
	ldr	x0, [x29, 104]
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x0, [x0, -56]
	cbz	x0, .L1552
	sub	w25, w25, #1
	subs	w28, w28, #1
	bne	.L1552
.L1557:
	ldr	w0, [x29, 148]
	add	x20, x20, :lo12:.LANCHOR0
	sub	w1, w0, w22
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x20, 2416]
	cmp	w0, 15
	bhi	.L1554
	mov	w21, 65535
	mov	w19, 128
.L1577:
	ldrh	w0, [x20, 2672]
	cmp	w0, w21
	bne	.L1558
	ldrh	w0, [x20, 3320]
	cmp	w0, w21
	bne	.L1558
	mov	w0, 0
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L1558:
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
	bls	.L1577
	b	.L1554
.L1520:
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
	cbnz	w24, .L1521
.L1554:
	mov	w0, 0
	b	.L1516
.L1521:
	sub	w26, w26, w19
	add	w23, w23, w19
	add	x27, x27, x21
	add	w22, w22, 1
	bl	FtlCacheWriteBack
	mov	w25, w24
	b	.L1519
.L1561:
	add	x19, x19, 2424
	b	.L1522
.L1526:
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1564
	mov	x0, x19
	bl	allocate_new_data_superblock
	b	.L1528
.L1564:
	mov	x19, x1
	b	.L1525
.L1551:
	ldrh	w0, [x19, 4]
	cbz	w0, .L1531
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
	bne	.L1532
	cmp	w21, w22
	bne	.L1533
	ldrh	w2, [x3, 62]
	udiv	w0, w23, w2
	msub	w0, w0, w2, w23
	str	w0, [x29, 144]
	sub	w2, w2, w0
	cmp	w2, w26
	csel	w0, w2, w26, ls
	str	w0, [x29, 152]
.L1534:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x29, 152]
	ldrh	w1, [x0, 62]
	cmp	w2, w1
	bne	.L1535
	ldr	x0, [x0, 4032]
	cmp	w21, w22
	add	x24, x0, x24
	bne	.L1536
	str	x27, [x24, 8]
.L1537:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1548
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1119
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1548:
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
	b	.L1530
.L1533:
	ldrh	w0, [x3, 62]
	add	w2, w23, w26
	str	wzr, [x29, 144]
	msub	w2, w21, w0, w2
	and	w0, w2, 65535
	str	w0, [x29, 152]
	b	.L1534
.L1536:
	ldr	w0, [x29, 152]
.L1580:
	mul	w0, w0, w21
	sub	w0, w0, w23
	lsl	w0, w0, 9
	add	x0, x27, x0
	str	x0, [x24, 8]
	b	.L1537
.L1535:
	ldr	x1, [x0, 4032]
	cmp	w21, w22
	add	x1, x1, x24
	bne	.L1538
	ldr	x0, [x0, 3992]
.L1579:
	str	x0, [x1, 8]
	ldr	w0, [x29, 188]
	cmn	w0, #1
	beq	.L1540
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
	bne	.L1541
	ldr	w0, [x3, 2792]
	add	w0, w0, 1
	str	w0, [x3, 2792]
.L1544:
	ldr	w0, [x29, 152]
	cmp	w21, w22
	lsl	w2, w0, 9
	add	x0, x20, :lo12:.LANCHOR0
	bne	.L1545
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
	bne	.L1537
.L1546:
	ldrh	w0, [x19, 4]
	cbz	w0, .L1537
	ldr	x0, [x29, 104]
	add	x1, x0, :lo12:.LANCHOR3
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0, 4032]
	str	x19, [x1, 112]
	add	x24, x0, x24
	str	x24, [x1, -56]
	b	.L1537
.L1538:
	ldr	x0, [x0, 3976]
	b	.L1579
.L1541:
	ldr	w0, [x28, 8]
	cmp	w21, w0
	beq	.L1543
	ldr	w0, [x3, 2792]
	mov	w2, w21
	add	w0, w0, 1
	str	w0, [x3, 2792]
	adrp	x0, .LC105
	add	x0, x0, :lo12:.LC105
	ldr	w1, [x28, 8]
	bl	printk
.L1543:
	ldr	w0, [x28, 8]
	cmp	w21, w0
	beq	.L1544
	ldr	x1, [x29, 160]
	mov	w2, 1098
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
	b	.L1544
.L1540:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1, 4032]
	ldrh	w2, [x1, 114]
	mov	w1, 0
	add	x0, x0, x24
	ldr	x0, [x0, 8]
	bl	ftl_memset
	b	.L1544
.L1545:
	ldrh	w1, [x0, 62]
	ldr	x0, [x0, 4032]
	add	x0, x0, x24
	mul	w1, w1, w21
	ldr	x0, [x0, 8]
	sub	w1, w1, w23
	lsl	w1, w1, 9
	add	x1, x27, x1
	bl	ftl_memcpy
	b	.L1546
.L1532:
	ldr	x0, [x3, 4032]
	add	x24, x0, x24
	ldrh	w0, [x3, 62]
	b	.L1580
.L1552:
	add	x0, x20, :lo12:.LANCHOR0
	mov	x3, x19
	mov	w2, 0
	mov	w1, w28
	ldr	x0, [x0, 4032]
	bl	FtlProgPages
	cmp	w25, w28
	bcs	.L1555
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1134
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1555:
	sub	w25, w25, w28
	b	.L1523
.L1559:
	mov	w0, -1
	b	.L1516
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
	bne	.L1584
.L1592:
	mov	w0, -1
.L1583:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1584:
	add	x23, x19, 2656
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w22, w0
	add	w0, w0, 1
	mov	w24, 61604
	strh	w0, [x23, 2]
	mov	w23, 19539
	movk	w23, 0x4654, lsl 16
.L1586:
	tbz	w22, #31, .L1589
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1437
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1588:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x1, 44]
	ldrh	w1, [x1, 114]
	add	x0, x0, 24
	cmp	x1, x0, lsl 1
	bcs	.L1591
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1439
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1591:
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
	bne	.L1592
	ldrh	w1, [x19, 2320]
	ldrb	w2, [x19, 2322]
	ldrh	w0, [x19, 58]
	strh	w1, [x19, 2662]
	cmp	w2, w0
	bne	.L1592
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
	bls	.L1593
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1461
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	printk
.L1593:
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
	bls	.L1594
	str	w1, [x0, 2620]
.L1594:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0, 2348]
	ldr	w2, [x0, 2624]
	cmp	w1, w2
	bls	.L1595
	str	w1, [x0, 2624]
.L1595:
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1596
	add	x0, x20, :lo12:.LANCHOR0
	add	x0, x0, 2424
	bl	make_superblock
.L1596:
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2472
	ldrh	w2, [x1, 2472]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1597
	bl	make_superblock
.L1597:
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2520
	ldrh	w2, [x1, 2520]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1598
	bl	make_superblock
.L1598:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x0, x20, 2672
	ldrh	w2, [x20, 2672]
	cmp	w2, w1
	beq	.L1599
	bl	make_superblock
.L1599:
	mov	w0, 0
	b	.L1583
.L1589:
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
	beq	.L1587
	ldr	x0, [x19, 3432]
	ldr	w0, [x0]
	cmp	w0, w23
	bne	.L1587
	ldr	x0, [x19, 3440]
	ldrh	w0, [x0]
	cmp	w0, w24
	beq	.L1588
.L1587:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1586
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
	bne	.L1606
.L1608:
	mov	w24, -1
.L1605:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1606:
	bl	FtlLoadSysInfo
	mov	w24, w0
	cbnz	w0, .L1608
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
.L1609:
	cmp	w0, w3
	bge	.L1614
	ldr	w2, [x1], 16
	tbz	w2, #31, .L1610
.L1614:
	add	x1, x20, :lo12:.LANCHOR0
	cmp	w0, w3
	ldrh	w2, [x1, 2340]
	add	w2, w2, 1
	strh	w2, [x1, 2340]
	bge	.L1620
.L1611:
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
	b	.L1615
.L1610:
	add	w0, w0, 1
	b	.L1609
.L1620:
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, -32]
	cbnz	w0, .L1611
.L1615:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x22, x19, 2424
	ldrh	w0, [x19, 2424]
	cmp	w0, w1
	beq	.L1616
	ldrh	w1, [x19, 2428]
	cbnz	w1, .L1616
	ldrh	w1, [x19, 2476]
	add	x21, x19, 2472
	cbnz	w1, .L1616
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 2472]
	bl	FtlGcRefreshBlock
	mov	x0, x22
	bl	allocate_new_data_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
.L1616:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20, 2340]
	tst	x0, 31
	bne	.L1605
	bl	FtlVpcCheckAndModify
	b	.L1605
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
	cbz	w0, .L1623
	bl	FtlMakeBbt
.L1623:
	mov	w4, 23752
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 0
	movk	w4, 0xa0f, lsl 16
.L1624:
	ldrh	w2, [x1, 62]
	cmp	w0, w2, lsl 7
	blt	.L1625
	ldrh	w21, [x1, 44]
	add	x22, x20, :lo12:.LANCHOR0
	mov	w19, 0
.L1626:
	ldrh	w0, [x22, 46]
	cmp	w0, w21
	bhi	.L1627
	ldrh	w0, [x22, 36]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L1628
.L1632:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w21, 0
	mov	w22, 0
.L1629:
	ldrh	w0, [x19, 44]
	cmp	w0, w22
	bhi	.L1633
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
	ble	.L1634
	sub	w1, w1, w21
	udiv	w1, w1, w2
	str	w1, [x19, 2584]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x19, 2652]
.L1634:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 98]
	cbz	w1, .L1636
	ldrh	w5, [x0, 2652]
	add	w5, w5, w1, lsr 1
	strh	w5, [x0, 2652]
	mul	w5, w1, w2
	cmp	w21, w5
	bge	.L1636
	add	w1, w1, 32
	str	w4, [x0, 2584]
	add	w1, w3, w1
	strh	w1, [x0, 2652]
.L1636:
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
.L1638:
	mov	x0, x21
	bl	make_superblock
	ldrb	w1, [x21, 7]
	ldrh	w0, [x21]
	cbnz	w1, .L1639
	ldr	x1, [x19, 2376]
	ubfiz	x0, x0, 1, 16
	strh	w23, [x1, x0]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L1638
.L1625:
	ldr	x5, [x1, 3992]
	ubfiz	x3, x0, 2, 16
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w2, [x5, x3]
	ldr	x2, [x1, 3976]
	str	w4, [x2, x3]
	b	.L1624
.L1627:
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w19, w19, w0
	and	w21, w21, 65535
	and	w19, w19, 65535
	b	.L1626
.L1628:
	udiv	w0, w19, w0
	ldr	w19, [x22, 136]
	add	x21, x20, :lo12:.LANCHOR0
	add	w0, w0, w19
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22, 40]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x22, 44]
.L1630:
	ldrh	w0, [x21, 46]
	cmp	w0, w19
	bls	.L1632
	mov	w0, w19
	add	w19, w19, 1
	mov	w1, 1
	and	w19, w19, 65535
	bl	FtlLowFormatEraseBlock
	b	.L1630
.L1633:
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w21, w21, w0
	and	w22, w22, 65535
	and	w21, w21, 65535
	b	.L1629
.L1639:
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
.L1640:
	mov	x0, x20
	bl	make_superblock
	ldrb	w1, [x20, 7]
	ldrh	w0, [x20]
	cbnz	w1, .L1641
	ldr	x1, [x19, 2376]
	ubfiz	x0, x0, 1, 16
	strh	w21, [x1, x0]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L1640
.L1641:
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
	cbnz	w0, .L1642
	adrp	x0, .LANCHOR2
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR2]
.L1642:
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
	bl	printk
	mov	x0, x19
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x19, 40]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	w0, .L1651
	bl	FtlSysBlkInit
	cbnz	w0, .L1651
	mov	w0, 1
	str	w0, [x20, #:lo12:.LANCHOR2]
	ldrh	w0, [x19, 2416]
	cmp	w0, 15
	bhi	.L1651
	mov	w19, 8129
.L1650:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L1650
.L1651:
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
	cbnz	x3, .L1658
	stp	x0, x1, [x2, 144]
	mov	w0, 0
	ret
.L1658:
	mov	w0, -1
	ret
	.size	rk_sftl_vendor_dev_ops_register, .-rk_sftl_vendor_dev_ops_register
	.align	2
	.global	rk_sftl_vendor_storage_init
	.type	rk_sftl_vendor_storage_init, %function
rk_sftl_vendor_storage_init:
	stp	x29, x30, [sp, -96]!
	mov	w0, 65536
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR3
	stp	x21, x22, [sp, 32]
	add	x21, x20, :lo12:.LANCHOR3
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	bl	ftl_malloc
	str	x0, [x21, 160]
	cbz	x0, .L1665
	adrp	x25, .LC106
	mov	w27, 22084
	add	x25, x25, :lo12:.LC106
	mov	w26, 0
	mov	w24, 0
	mov	w23, 0
	movk	w27, 0x524b, lsl 16
.L1663:
	ldr	x3, [x21, 144]
	mov	w1, 128
	ldr	x2, [x21, 160]
	lsl	w0, w23, 7
	blr	x3
	mov	w22, w0
	cbnz	w0, .L1661
	ldr	x0, [x21, 160]
	add	x1, x0, 61440
	ldr	w3, [x0, 4]
	ldr	w2, [x1, 4092]
	ldr	w1, [x0]
	mov	x0, x25
	bl	printk
	ldr	x19, [x21, 160]
	ldr	w0, [x19]
	cmp	w0, w27
	bne	.L1662
	add	x0, x19, 61440
	ldr	w1, [x19, 4]
	ldr	w0, [x0, 4092]
	cmp	w0, w1
	bne	.L1662
	cmp	w0, w24
	bls	.L1662
	mov	w26, w23
	mov	w24, w0
.L1662:
	add	w23, w23, 1
	cmp	w23, 2
	bne	.L1663
	cbz	w24, .L1664
	add	x0, x20, :lo12:.LANCHOR3
	mov	x2, x19
	mov	w1, 128
	ldr	x3, [x0, 144]
	lsl	w0, w26, 7
	blr	x3
	mov	w22, w0
	cbz	w0, .L1659
.L1661:
	add	x20, x20, :lo12:.LANCHOR3
	mov	w22, -1
	ldr	x0, [x20, 160]
	bl	kfree
	str	xzr, [x20, 160]
	b	.L1659
.L1664:
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
.L1659:
	mov	w0, w22
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1665:
	mov	w22, -12
	b	.L1659
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	adrp	x3, .LANCHOR3+160
	ldr	x4, [x3, #:lo12:.LANCHOR3+160]
	cbz	x4, .L1673
	ldrh	w6, [x4, 10]
	add	x5, x4, 16
	mov	w3, 0
.L1670:
	cmp	w3, w6
	bcc	.L1672
.L1673:
	mov	w0, -1
	ret
.L1672:
	ldrh	w7, [x5], 8
	cmp	w7, w0
	bne	.L1671
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
.L1671:
	add	w3, w3, 1
	b	.L1670
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
	cbz	x19, .L1693
	add	w21, w2, 63
	ldrh	w3, [x19, 10]
	ldrh	w24, [x19, 8]
	mov	x4, x1
	mov	w28, w2
	and	w21, w21, -64
	add	x1, x19, 16
	mov	w20, 0
.L1680:
	cmp	w20, w3
	bcc	.L1688
	ldrh	w1, [x19, 14]
	cmp	w21, w1
	bhi	.L1693
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
	b	.L1695
.L1688:
	ldrh	w6, [x1], 8
	cmp	w6, w0
	bne	.L1681
	uxtw	x27, w20
	add	x5, x19, 1024
	add	x22, x19, x27, lsl 3
	ldrh	w25, [x22, 20]
	add	w25, w25, 63
	and	w25, w25, -64
	cmp	w28, w25
	bls	.L1682
	ldrh	w0, [x19, 14]
	cmp	w21, w0
	bhi	.L1693
	add	x27, x27, 2
	ldrh	w22, [x22, 18]
	add	x27, x19, x27, lsl 3
	sub	w26, w3, #1
.L1683:
	cmp	w20, w26
	bcc	.L1684
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
.L1695:
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
.L1678:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1684:
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
	b	.L1683
.L1682:
	ldrh	w0, [x22, 18]
	uxtw	x2, w28
	mov	x1, x4
	add	x0, x5, x0
	bl	memcpy
	strh	w28, [x22, 20]
	b	.L1695
.L1681:
	add	w20, w20, 1
	b	.L1680
.L1693:
	mov	w0, -1
	b	.L1678
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
	cbz	x0, .L1710
	mov	w1, 30209
	mov	x19, x0
	movk	w1, 0x4004, lsl 16
	cmp	w21, w1
	beq	.L1699
	add	w1, w1, 1
	cmp	w21, w1
	beq	.L1700
.L1720:
	mov	x20, -14
	b	.L1698
.L1699:
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
	cbz	x3, .L1706
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbnz	x0, .L1720
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L1704
.L1705:
	mov	x20, -1
.L1698:
	mov	x0, x19
	bl	kfree
.L1696:
	mov	x0, x20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1704:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_read
	cmn	w0, #1
	beq	.L1705
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
	cbz	x3, .L1720
	mov	x0, x20
	mov	x1, x19
	mov	x20, 0
	bl	__arch_copy_to_user
	cbz	x0, .L1698
	b	.L1720
.L1700:
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
	cbz	x3, .L1706
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbnz	x0, .L1720
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L1705
	ldrh	w2, [x19, 6]
	cmp	w2, 4087
	bhi	.L1705
	add	w2, w2, 8
	mov	x0, x20
	sxtw	x2, w2
	ldr	x1, [x21, 8]
#APP
// 410 "./arch/arm64/include/asm/uaccess.h" 1
	adds x0, x0, x2; ccmp x0, x1, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1708
	mov	x1, x20
	mov	x0, x19
	bl	__arch_copy_from_user
	cbnz	x0, .L1720
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_write
	sxtw	x20, w0
	b	.L1698
.L1706:
	str	xzr, [x19]
	b	.L1720
.L1708:
	mov	w1, 0
	mov	x0, x19
	bl	memset
	b	.L1720
.L1710:
	mov	x20, -1
	b	.L1696
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
	.type	__func__.3767, %object
	.size	__func__.3767, 17
__func__.3767:
	.string	"INSERT_DATA_LIST"
	.zero	7
	.type	__func__.3762, %object
	.size	__func__.3762, 17
__func__.3762:
	.string	"INSERT_FREE_LIST"
	.zero	7
	.type	__func__.3798, %object
	.size	__func__.3798, 17
__func__.3798:
	.string	"List_remove_node"
	.zero	7
	.type	__func__.3830, %object
	.size	__func__.3830, 22
__func__.3830:
	.string	"List_update_data_list"
	.zero	2
	.type	__func__.3936, %object
	.size	__func__.3936, 22
__func__.3936:
	.string	"select_l2p_ram_region"
	.zero	2
	.type	__func__.4259, %object
	.size	__func__.4259, 16
__func__.4259:
	.string	"make_superblock"
	.type	__func__.4434, %object
	.size	__func__.4434, 19
__func__.4434:
	.string	"get_new_active_ppa"
	.zero	5
	.type	__func__.16008, %object
	.size	__func__.16008, 15
__func__.16008:
	.string	"FlashReadPages"
	.zero	1
	.type	__func__.16025, %object
	.size	__func__.16025, 15
__func__.16025:
	.string	"FlashProgPages"
	.zero	1
	.type	__func__.3872, %object
	.size	__func__.3872, 26
__func__.3872:
	.string	"ftl_map_blk_alloc_new_blk"
	.zero	6
	.type	__func__.3376, %object
	.size	__func__.3376, 11
__func__.3376:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.3602, %object
	.size	__func__.3602, 14
__func__.3602:
	.string	"FtlBbt2Bitmap"
	.zero	2
	.type	__func__.3839, %object
	.size	__func__.3839, 16
__func__.3839:
	.string	"load_l2p_region"
	.type	__func__.3897, %object
	.size	__func__.3897, 31
__func__.3897:
	.string	"Ftl_write_map_blk_to_last_page"
	.zero	1
	.type	__func__.3911, %object
	.size	__func__.3911, 16
__func__.3911:
	.string	"FtlMapWritePage"
	.type	__func__.3883, %object
	.size	__func__.3883, 15
__func__.3883:
	.string	"ftl_map_blk_gc"
	.zero	1
	.type	__func__.3953, %object
	.size	__func__.3953, 9
__func__.3953:
	.string	"log2phys"
	.zero	7
	.type	__func__.4168, %object
	.size	__func__.4168, 16
__func__.4168:
	.string	"FtlReUsePrevPpa"
	.type	__func__.4048, %object
	.size	__func__.4048, 14
__func__.4048:
	.string	"FtlScanSysBlk"
	.zero	2
	.type	__func__.4280, %object
	.size	__func__.4280, 18
__func__.4280:
	.string	"SupperBlkListInit"
	.zero	6
	.type	__func__.4321, %object
	.size	__func__.4321, 14
__func__.4321:
	.string	"ftl_check_vpc"
	.zero	2
	.type	__func__.4406, %object
	.size	__func__.4406, 25
__func__.4406:
	.string	"allocate_data_superblock"
	.zero	7
	.type	__func__.4447, %object
	.size	__func__.4447, 16
__func__.4447:
	.string	"update_vpc_list"
	.type	__func__.4454, %object
	.size	__func__.4454, 20
__func__.4454:
	.string	"decrement_vpc_count"
	.zero	4
	.type	__func__.4202, %object
	.size	__func__.4202, 22
__func__.4202:
	.string	"FtlRecoverySuperblock"
	.zero	2
	.type	__func__.3645, %object
	.size	__func__.3645, 11
__func__.3645:
	.string	"FtlLoadBbt"
	.zero	5
	.type	__func__.4026, %object
	.size	__func__.4026, 15
__func__.4026:
	.string	"FtlVpcTblFlush"
	.zero	1
	.type	__func__.4305, %object
	.size	__func__.4305, 21
__func__.4305:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.4427, %object
	.size	__func__.4427, 29
__func__.4427:
	.string	"allocate_new_data_superblock"
	.zero	3
	.type	__func__.3500, %object
	.size	__func__.3500, 13
__func__.3500:
	.string	"FtlProgPages"
	.zero	3
	.type	__func__.4524, %object
	.size	__func__.4524, 19
__func__.4524:
	.string	"FtlGcFreeTempBlock"
	.zero	5
	.type	__func__.4630, %object
	.size	__func__.4630, 23
__func__.4630:
	.string	"rk_ftl_garbage_collect"
	.zero	1
	.type	__func__.3528, %object
	.size	__func__.3528, 9
__func__.3528:
	.string	"FtlWrite"
	.zero	7
	.type	__func__.4105, %object
	.size	__func__.4105, 15
__func__.4105:
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
	.xword	.LC107
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
	.string	"\0014"
.LC76:
	.string	"prog read error: = %x\n"
.LC77:
	.string	"prog read s error: = %x %x %x\n"
.LC78:
	.string	"prog read d error: = %x %x %x\n"
.LC79:
	.string	"FtlFreeSysBlkQueueOut free count = %d\n"
.LC80:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
.LC81:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
.LC82:
	.string	"%s error allocating memory. return -1\n"
.LC83:
	.string	"region_id = %x phyAddr = %x\n"
.LC84:
	.string	"spare:"
.LC85:
	.string	"map_ppn:"
.LC86:
	.string	"FtlMapWritePage error = %x \n"
.LC87:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC88:
	.string	"ftl_scan_all_data = %x\n"
.LC89:
	.string	"scan lpa = %x ppa= %x\n"
.LC90:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC91:
	.string	"...%s enter...\n"
.LC92:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC93:
	.string	"free blk vpc error %x = %x  %x\n"
.LC94:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC95:
	.string	"FtlBbmTblFlush error:%x\n"
.LC96:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC97:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC98:
	.string	"decrement_vpc_count %x = %d\n"
.LC99:
	.string	"spuer block %x vpn is 0\n "
.LC100:
	.string	"no ect"
.LC101:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC102:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC103:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC104:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC105:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC106:
	.string	"\0013vendor storage %x,%x,%x\n"
.LC107:
	.string	"vendor_storage"