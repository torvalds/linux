/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-12-27
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
	mov	x7, x0
	stp	x29, x30, [sp, -16]!
	adrp	x4, .LANCHOR0
	add	x1, x4, :lo12:.LANCHOR0
	add	x29, sp, 0
	ldrh	w3, [x0]
	ldrh	w6, [x0, 2]
	ldrh	w5, [x7, 14]
	ldrh	w0, [x0, 4]
	ldrh	w2, [x7, 6]
	cmp	w5, 4
	strh	w3, [x1, 56]
	strh	w6, [x1, 58]
	strh	w0, [x1, 60]
	strh	w2, [x1, 46]
	strh	w5, [x1, 62]
	bne	.L10
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
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	mov	w0, 0
	ret
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
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
	blt	.L43
	mov	w0, w5
	ret
.L43:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L36:
	mov	w0, w4
	bl	FtlBbmIsBadBlock
	cbz	w0, .L35
	add	w5, w5, 1
	and	w5, w5, 65535
.L35:
	add	w4, w4, 1
	and	w4, w4, 65535
	cmp	w4, w6
	blt	.L36
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
	bls	.L48
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	ret
.L48:
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
	cbz	w3, .L52
	ldrh	w5, [x2, 2340]
	mov	w6, 0
	ldrh	w3, [x1, 2]
	mov	w4, 0
	ldrh	w2, [x1, 4]
	and	w5, w5, 31
.L54:
	cmp	w5, w4
	bgt	.L55
	cbz	w6, .L52
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0, 258]
	strh	w2, [x0, 260]
.L52:
	ret
.L55:
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
	b	.L54
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
	beq	.L65
	ldrh	w5, [x1, 2]
	mov	w3, 0
.L63:
	cmp	w3, w4
	bcc	.L64
.L65:
	mov	w0, 0
	ret
.L64:
	add	w2, w3, w5
	ubfiz	x2, x2, 1, 10
	add	x2, x1, x2
	ldrh	w2, [x2, 8]
	cmp	w2, w0
	beq	.L66
	add	w3, w3, 1
	b	.L63
.L66:
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
	bls	.L83
	mov	w7, 6
	ldr	x12, [x3, 2360]
	mov	w1, -1
	umull	x11, w0, w7
	add	x5, x12, x11
	strh	w1, [x5, 2]
	strh	w1, [x12, x11]
	ldr	x1, [x3, 2368]
	cbnz	x1, .L70
	str	x5, [x3, 2368]
.L83:
	mov	w0, 0
	ret
.L70:
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
.L78:
	add	w3, w3, 1
	and	w3, w3, 65535
	cmp	w3, w18
	bhi	.L69
	cmp	w0, w2
	beq	.L69
	ubfiz	x16, x2, 1, 16
	ldrh	w17, [x1, 4]
	cmp	w17, 0
	ldrh	w7, [x14, x16]
	mul	w7, w7, w17
	csel	w7, w7, w10, ne
	cmp	w6, w7
	bne	.L74
	ldrh	w16, [x15, x16]
	ldrh	w7, [x9]
	cmp	w16, w7
	bcc	.L76
.L75:
	strh	w2, [x12, x11]
	ldrh	w2, [x1, 2]
	strh	w2, [x5, 2]
	add	x2, x4, :lo12:.LANCHOR0
	ldr	x3, [x2, 2368]
	cmp	x1, x3
	bne	.L79
	strh	w0, [x1, 2]
	str	x5, [x2, 2368]
	b	.L69
.L74:
	bcc	.L75
.L76:
	ldrh	w7, [x1]
	cmp	w7, w30
	bne	.L77
	strh	w2, [x5, 2]
	add	x2, x4, :lo12:.LANCHOR0
	strh	w0, [x1]
	str	x5, [x2, 2392]
.L69:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L77:
	umaddl	x1, w7, w8, x13
	mov	w2, w7
	b	.L78
.L79:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2360]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L69
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
	bcs	.L85
	adrp	x1, .LANCHOR1
	adrp	x0, .LC1
	mov	w2, 214
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L85:
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
	beq	.L89
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
	cbnz	x1, .L90
	str	x4, [x5, 2408]
.L89:
	mov	w0, 0
	ret
.L90:
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
.L93:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11, x5]
	cmp	w5, w12
	bcs	.L91
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L92
	strh	w2, [x4, 2]
	strh	w0, [x1]
	b	.L89
.L92:
	umaddl	x1, w5, w6, x10
	mov	w2, w5
	b	.L93
.L91:
	ldrh	w5, [x1, 2]
	strh	w5, [x4, 2]
	strh	w2, [x9, x8]
	add	x2, x3, :lo12:.LANCHOR0
	ldr	x3, [x2, 2408]
	cmp	x1, x3
	bne	.L94
	strh	w0, [x1, 2]
	str	x4, [x2, 2408]
	b	.L89
.L94:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2360]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L89
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
	bcs	.L95
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 207
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L95:
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
	bne	.L99
	ldr	x0, [x23]
	cmp	x20, x0
	beq	.L99
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 372
	add	x1, x1, 48
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L99:
	ldr	x0, [x23]
	mov	w1, 65535
	cmp	x20, x0
	ldrh	w0, [x22, x21]
	bne	.L100
	cmp	w0, w1
	bne	.L101
	str	xzr, [x23]
.L102:
	mov	w0, -1
	strh	w0, [x22, x21]
	strh	w0, [x20, 2]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L101:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 6
	ldr	x1, [x19, 2360]
	umaddl	x0, w0, w2, x1
	mov	w1, -1
	str	x0, [x23]
	strh	w1, [x0, 2]
	b	.L102
.L100:
	cmp	w0, w1
	ldrh	w1, [x20, 2]
	bne	.L103
	cmp	w1, w0
	beq	.L102
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 6
	mov	w2, -1
	umull	x1, w1, w0
	ldr	x0, [x19, 2360]
	strh	w2, [x0, x1]
	b	.L102
.L103:
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
	b	.L102
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
	ldr	x2, [x0]
	cbz	x2, .L111
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LANCHOR0+2360
	and	w1, w1, 65535
	mov	w4, 65535
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	w5, 6
	ldr	x19, [x3, #:lo12:.LANCHOR0+2360]
.L107:
	cbnz	w1, .L108
.L110:
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
.L108:
	ldrh	w3, [x2]
	cmp	w3, w4
	beq	.L110
	sub	w1, w1, #1
	umaddl	x2, w3, w5, x19
	and	w1, w1, 65535
	b	.L107
.L111:
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
	cbz	x1, .L123
	ldr	x3, [x0, 2360]
	mov	w4, 6
	mov	w0, 65535
.L120:
	cbz	w2, .L121
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L122
	ret
.L122:
	sub	w2, w2, #1
	umaddl	x1, w1, w4, x3
	and	w2, w2, 65535
	b	.L120
.L123:
	mov	w0, 65535
	ret
.L121:
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
	beq	.L126
	ldrh	w1, [x0, 2472]
	cmp	w1, w20
	beq	.L126
	ldrh	w1, [x0, 2520]
	cmp	w1, w20
	beq	.L126
	mov	w22, 6
	ldr	x24, [x0, 2360]
	ldr	x1, [x0, 2368]
	umull	x22, w20, w22
	add	x23, x24, x22
	cmp	x23, x1
	beq	.L126
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
	bne	.L129
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	bne	.L129
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 463
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L129:
	ldrh	w0, [x23, 2]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L130
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	beq	.L126
.L130:
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
	bcs	.L126
	mov	w1, w20
	add	x0, x22, 2368
	bl	List_remove_node
	ldrh	w0, [x22, 2400]
	cbnz	w0, .L132
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 474
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L132:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2400]
	sub	w0, w0, #1
	strh	w0, [x19, 2400]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L126:
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
.L137:
	and	w19, w3, 65535
	cmp	w19, w2
	bcc	.L139
	add	x4, x0, 4
	mov	w19, w2
	mov	w6, -2147483648
	mov	w3, 0
.L140:
	cmp	w3, w2
	bne	.L142
	cmp	w19, w2
	bcc	.L138
	add	x1, x1, :lo12:.LANCHOR0
	mov	w19, w2
	mov	w3, -1
	ldrh	w4, [x1, 2576]
	mov	w1, 0
.L143:
	cmp	w1, w2
	bne	.L145
	cmp	w19, w1
	bcc	.L138
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 789
	add	x1, x1, 96
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L138
.L139:
	add	x3, x3, 1
	add	x5, x0, x3, lsl 4
	ldrh	w5, [x5, -16]
	cmp	w5, w4
	bne	.L137
.L138:
	mov	w0, w19
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L142:
	ldr	w5, [x4]
	tbnz	w5, #31, .L141
	cmp	w6, w5
	bls	.L141
	mov	w6, w5
	mov	w19, w3
.L141:
	add	w3, w3, 1
	add	x4, x4, 16
	and	w3, w3, 65535
	b	.L140
.L145:
	ldr	w6, [x0, 4]
	cmp	w3, w6
	bls	.L144
	ldrh	w5, [x0]
	cmp	w5, w4
	csel	w3, w3, w6, eq
	csel	w19, w19, w1, eq
.L144:
	add	w1, w1, 1
	add	x0, x0, 16
	and	w1, w1, 65535
	b	.L143
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR0
	add	x3, x1, :lo12:.LANCHOR0
	ldrh	w2, [x3, 2578]
	cmp	w2, 4
	bhi	.L148
	cbnz	w0, .L148
	add	w2, w2, 1
	strh	w2, [x3, 2578]
	ret
.L148:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w6, 65535
	mov	x1, 0
	ldrh	w4, [x0, 44]
	ldr	x5, [x0, 2376]
	strh	wzr, [x0, 2578]
	str	wzr, [x0, 2580]
.L149:
	cmp	w4, w1, uxth
	bhi	.L151
	ret
.L151:
	ldrh	w3, [x5, x1, lsl 1]
	cmp	w3, w6
	beq	.L150
	ldr	w2, [x0, 2580]
	add	w2, w2, w3
	str	w2, [x0, 2580]
.L150:
	add	x1, x1, 1
	b	.L149
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	and	w0, w0, 65535
	cbz	w1, .L154
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_set_blk_mode.part.7
	ldp	x29, x30, [sp], 16
	ret
.L154:
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
.L162:
	cmp	w3, w2, uxth
	bhi	.L164
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
.L165:
	cmp	x2, x3
	bne	.L167
	ret
.L164:
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5, 16]
	cmp	w5, w1
	beq	.L163
	ldrh	w5, [x0, 4]
	add	w5, w5, 1
	strh	w5, [x0, 4]
.L163:
	add	w2, w2, 1
	b	.L162
.L167:
	ldrh	w4, [x2]
	cmp	w4, w5
	beq	.L166
	ldrh	w4, [x0, 4]
	add	w4, w1, w4
	strh	w4, [x0, 4]
.L166:
	add	x2, x2, 2
	b	.L165
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	ldrh	w1, [x0, 4]
	cbz	w1, .L168
	ldrh	w2, [x0]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L168
	ldrb	w2, [x0, 6]
	add	x2, x2, 8
	ldrh	w3, [x0, x2, lsl 1]
	adrp	x2, .LANCHOR0+36
	ldrh	w4, [x2, #:lo12:.LANCHOR0+36]
	mov	w2, w1
.L171:
	cmp	w3, w2
	beq	.L173
.L168:
	ret
.L173:
	ldrb	w1, [x0, 6]
	add	w1, w1, 1
	and	w1, w1, 255
	strb	w1, [x0, 6]
	cmp	w1, w4
	bne	.L172
	ldrh	w1, [x0, 2]
	strb	wzr, [x0, 6]
	add	w1, w1, 1
	strh	w1, [x0, 2]
.L172:
	ldrb	w1, [x0, 6]
	add	x1, x1, 8
	ldrh	w3, [x0, x1, lsl 1]
	b	.L171
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
	bcc	.L175
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2156
	add	x1, x1, 120
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L175:
	add	x0, x20, :lo12:.LANCHOR0
	add	x6, x19, 16
	add	x7, x0, 64
	strh	wzr, [x19, 4]
	strb	wzr, [x19, 7]
	mov	x5, 0
	ldrh	w8, [x0, 36]
	mov	w9, -1
.L176:
	cmp	w8, w5, uxth
	bhi	.L178
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19, 7]
	strb	wzr, [x19, 9]
	ldrh	w1, [x20, 106]
	mul	w0, w0, w1
	strh	w0, [x19, 4]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L178:
	ldrh	w1, [x19]
	ldrb	w0, [x7, x5]
	bl	V2P_block
	mov	w4, w0
	strh	w9, [x6]
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L177
	strh	w4, [x6]
	ldrb	w0, [x19, 7]
	add	w0, w0, 1
	strb	w0, [x19, 7]
.L177:
	add	x5, x5, 1
	add	x6, x6, 2
	b	.L176
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
	bhi	.L192
	cbz	w6, .L190
	mov	w0, 32768
	sdiv	w6, w0, w6
.L191:
	add	x0, x5, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x0, [x0, 2360]
	umaddl	x8, w8, w1, x0
	mov	w0, 0
	strh	w6, [x8, 4]
	ret
.L185:
	mov	w6, 0
	b	.L184
.L190:
	mov	w6, 0
	b	.L191
.L192:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L183:
	ldrb	w0, [x7, x9]
	mov	w1, w8
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L182
	add	w6, w6, w11
	and	w6, w6, 65535
.L182:
	add	x9, x9, 1
	cmp	w10, w9, uxth
	bhi	.L183
	cbz	w6, .L185
	mov	w0, 32768
	sdiv	w6, w0, w6
.L184:
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
	cbz	x0, .L195
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
.L195:
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
	cbz	x1, .L203
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
.L199:
	cmp	w0, w3
	beq	.L202
	umull	x5, w1, w6
	ldrh	w5, [x4, x5]
	cmp	w5, w7
	bne	.L200
.L202:
	add	x2, x2, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2, 2384]
	ldrh	w0, [x0, x1]
	ret
.L200:
	add	w3, w3, 1
	mov	w1, w5
	and	w3, w3, 65535
	b	.L199
.L203:
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
	ldr	w3, [x21, 120]
	adrp	x2, .LC3
	add	x2, x2, :lo12:.LC3
	mov	x1, 64
	mov	x0, x20
	bl	snprintf
	add	x20, x20, x0, sxtw
	mov	x0, x20
	adrp	x1, .LC4
	add	x1, x1, :lo12:.LC4
	bl	strcpy
	ldr	w3, [x21, 2584]
	add	x20, x20, 10
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC5
	add	x2, x2, :lo12:.LC5
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2580]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC6
	add	x2, x2, :lo12:.LC6
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2588]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC7
	add	x2, x2, :lo12:.LC7
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2592]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC8
	add	x2, x2, :lo12:.LC8
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2596]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC9
	add	x2, x2, :lo12:.LC9
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2600]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC10
	add	x2, x2, :lo12:.LC10
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2604]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC11
	add	x2, x2, :lo12:.LC11
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2608]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC12
	add	x2, x2, :lo12:.LC12
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2612]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC13
	add	x2, x2, :lo12:.LC13
	lsr	w3, w3, 11
	bl	snprintf
	ldr	w3, [x21, 2616]
	add	x20, x20, x0, sxtw
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC14
	add	x2, x2, :lo12:.LC14
	lsr	w3, w3, 11
	bl	snprintf
	ldr	w3, [x21, 2620]
	add	x20, x20, x0, sxtw
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC15
	add	x2, x2, :lo12:.LC15
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2624]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC16
	add	x2, x2, :lo12:.LC16
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 166]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC17
	add	x2, x2, :lo12:.LC17
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2416]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC18
	add	x2, x2, :lo12:.LC18
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2628]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC19
	add	x2, x2, :lo12:.LC19
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2632]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC20
	add	x2, x2, :lo12:.LC20
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2636]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC21
	add	x2, x2, :lo12:.LC21
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2640]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC22
	add	x2, x2, :lo12:.LC22
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2644]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC23
	add	x2, x2, :lo12:.LC23
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2648]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC24
	add	x2, x2, :lo12:.LC24
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2342]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC25
	add	x2, x2, :lo12:.LC25
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2340]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC26
	add	x2, x2, :lo12:.LC26
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 144]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC27
	add	x2, x2, :lo12:.LC27
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 136]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC28
	add	x2, x2, :lo12:.LC28
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 32]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC29
	add	x2, x2, :lo12:.LC29
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 262]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC30
	add	x2, x2, :lo12:.LC30
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 44]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC31
	add	x2, x2, :lo12:.LC31
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2652]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC32
	add	x2, x2, :lo12:.LC32
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 48]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC33
	add	x2, x2, :lo12:.LC33
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2656]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC34
	add	x2, x2, :lo12:.LC34
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 160]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC35
	add	x2, x2, :lo12:.LC35
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2426]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC36
	add	x2, x2, :lo12:.LC36
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 2430]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC37
	add	x2, x2, :lo12:.LC37
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2424]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC38
	add	x2, x2, :lo12:.LC38
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 2432]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC39
	add	x2, x2, :lo12:.LC39
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2428]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC40
	add	x2, x2, :lo12:.LC40
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w1, [x21, 2424]
	adrp	x2, .LC41
	ldr	x0, [x21, 2376]
	add	x2, x2, :lo12:.LC41
	ldrh	w3, [x0, x1, lsl 1]
	mov	x1, 64
	mov	x0, x20
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2474]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC42
	add	x2, x2, :lo12:.LC42
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 2478]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC43
	add	x2, x2, :lo12:.LC43
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2472]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC44
	add	x2, x2, :lo12:.LC44
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 2480]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC45
	add	x2, x2, :lo12:.LC45
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2476]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC46
	add	x2, x2, :lo12:.LC46
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w1, [x21, 2472]
	adrp	x2, .LC47
	ldr	x0, [x21, 2376]
	add	x2, x2, :lo12:.LC47
	ldrh	w3, [x0, x1, lsl 1]
	mov	x1, 64
	mov	x0, x20
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2522]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC48
	add	x2, x2, :lo12:.LC48
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 2526]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC49
	add	x2, x2, :lo12:.LC49
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2520]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC50
	add	x2, x2, :lo12:.LC50
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 2528]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC51
	add	x2, x2, :lo12:.LC51
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2524]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC52
	add	x2, x2, :lo12:.LC52
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2674]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC53
	add	x2, x2, :lo12:.LC53
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 2678]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC54
	add	x2, x2, :lo12:.LC54
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2672]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC55
	add	x2, x2, :lo12:.LC55
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 2680]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC56
	add	x2, x2, :lo12:.LC56
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2676]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC57
	add	x2, x2, :lo12:.LC57
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w5, [x21, 2796]
	mov	x1, 64
	ldr	w4, [x21, 2804]
	adrp	x2, .LC58
	ldr	w3, [x21, 2800]
	add	x2, x2, :lo12:.LC58
	mov	x0, x20
	bl	snprintf
	add	x19, x20, x0, sxtw
	ldr	w3, [x21, 2792]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC59
	add	x2, x2, :lo12:.LC59
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldr	w3, [x21, 2816]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC60
	add	x2, x2, :lo12:.LC60
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldrh	w3, [x21, 3232]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC61
	add	x2, x2, :lo12:.LC61
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldrh	w3, [x21, 3234]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC62
	add	x2, x2, :lo12:.LC62
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldr	w3, [x21, 3236]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC63
	add	x2, x2, :lo12:.LC63
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldrh	w3, [x21, 3240]
	adrp	x2, .LC64
	add	x2, x2, :lo12:.LC64
	mov	x1, 64
	mov	x0, x19
	bl	snprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	and	w3, w0, 65535
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC65
	add	x2, x2, :lo12:.LC65
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x21, 2416]
	bl	GetFreeBlockMaxEraseCount
	and	w3, w0, 65535
	adrp	x2, .LC66
	mov	x0, x19
	add	x2, x2, :lo12:.LC66
	mov	x1, 64
	bl	snprintf
	add	x19, x19, x0, sxtw
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	cmp	w0, 1
	beq	.L205
.L210:
	sub	w0, w19, w22
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L205:
	ldrh	w0, [x21, 2672]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L207
	ldr	x1, [x21, 2376]
	ubfiz	x0, x0, 1, 16
	adrp	x2, .LC67
	add	x2, x2, :lo12:.LC67
	ldrh	w3, [x1, x0]
	mov	x0, x19
	mov	x1, 64
	bl	snprintf
	add	x19, x19, x0, sxtw
.L207:
	mov	w0, 0
	adrp	x24, .LC68
	bl	List_get_gc_head_node
	add	x21, x23, :lo12:.LANCHOR0
	and	w4, w0, 65535
	add	x24, x24, :lo12:.LC68
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
.L209:
	cmp	w4, w27
	beq	.L208
	umull	x25, w4, w26
	ldr	x0, [x21, 2360]
	ldr	x2, [x21, 2376]
	ubfiz	x1, x4, 1, 16
	ldr	x3, [x21, 2384]
	add	x0, x0, x25
	ldrh	w5, [x2, x1]
	mov	x2, x24
	ldrh	w7, [x3, x1]
	mov	w3, w20
	ldrh	w6, [x0, 4]
	mov	x1, 64
	mov	x0, x19
	add	w20, w20, 1
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21, 2360]
	cmp	w20, 16
	ldrh	w4, [x0, x25]
	bne	.L209
.L208:
	add	x23, x23, :lo12:.LANCHOR0
	adrp	x21, .LC69
	add	x21, x21, :lo12:.LC69
	mov	w20, 0
	mov	w25, 65535
	mov	w26, 6
	ldr	x0, [x23, 2360]
	ldr	x4, [x23, 2408]
	sub	x4, x4, x0
	mov	x0, -6148914691236517206
	asr	x4, x4, 1
	movk	x0, 0xaaab, lsl 0
	mul	x4, x4, x0
	and	w4, w4, 65535
.L211:
	cmp	w4, w25
	beq	.L210
	umull	x24, w4, w26
	ldr	x0, [x23, 2360]
	ldr	x2, [x23, 2384]
	ubfiz	x1, x4, 1, 16
	add	x0, x0, x24
	mov	w3, w20
	add	w20, w20, 1
	ldrh	w6, [x2, x1]
	mov	x2, x21
	ldrh	w5, [x0, 4]
	mov	x1, 64
	mov	x0, x19
	bl	snprintf
	add	x19, x19, x0, sxtw
	cmp	w20, 4
	beq	.L210
	ldr	x0, [x23, 2360]
	ldrh	w4, [x0, x24]
	b	.L211
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	cmp	w0, 2047
	ble	.L219
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LC70
	adrp	x2, .LC71
	add	x3, x3, :lo12:.LC70
	add	x29, sp, 0
	add	x2, x2, :lo12:.LC71
	stp	x19, x20, [sp, 16]
	mov	x20, x1
	mov	x1, 64
	mov	x0, x20
	bl	snprintf
	add	x19, x20, x0, sxtw
	mov	x0, x19
	bl	FtlPrintInfo2buf
	add	x0, x19, x0, sxtw
	sub	w0, w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L219:
	mov	w0, 0
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
	bcs	.L225
	ldrh	w2, [x0, 44]
	mov	x1, 0
	ldr	x4, [x0, 2384]
	str	wzr, [x0, 2628]
.L226:
	cmp	w2, w1
	bhi	.L227
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w0, [x1, 2628]
	udiv	w2, w0, w2
	str	w2, [x1, 2636]
	ldr	w2, [x1, 2632]
	sub	w0, w0, w2
	ldrh	w2, [x1, 96]
	udiv	w0, w0, w2
	str	w0, [x1, 2628]
.L228:
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w9, [x1, 2648]
	ldr	w11, [x1, 2636]
	add	w0, w9, 256
	cmp	w0, w11
	bls	.L233
	ldr	w1, [x1, 2644]
	add	w0, w9, 768
	cmp	w0, w1
	bls	.L233
.L235:
	mov	w20, 65535
.L234:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L227:
	ldrh	w3, [x4, x1, lsl 1]
	add	x1, x1, 1
	ldr	w5, [x0, 2628]
	add	w3, w3, w5
	str	w3, [x0, 2628]
	b	.L226
.L225:
	ldr	w1, [x0, 2644]
	cmp	w2, w1
	bls	.L228
	add	w1, w1, 1
	str	w1, [x0, 2644]
	mov	w1, 0
.L230:
	ldrh	w2, [x0, 44]
	cmp	w1, w2
	bcs	.L228
	ldr	x4, [x0, 2384]
	ubfiz	x3, x1, 1, 32
	add	w1, w1, 1
	ldrh	w2, [x4, x3]
	add	w2, w2, 1
	strh	w2, [x4, x3]
	b	.L230
.L233:
	add	x10, x8, :lo12:.LANCHOR0
	ldrh	w0, [x10, 2416]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	and	w6, w0, 65535
	add	w0, w9, 64
	cmp	w6, w0
	bcc	.L235
	ldr	x0, [x10, 2368]
	cbz	x0, .L235
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
.L236:
	ldrh	w3, [x0]
	cmp	w3, w10
	bne	.L239
	mov	w20, w2
.L238:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L235
	ubfiz	x7, x20, 1, 32
	ldrh	w3, [x5, x7]
	cmp	w9, w3
	bcs	.L240
	bl	GetFreeBlockMinEraseCount
	cmp	w9, w0, uxth
	bcs	.L240
	add	x0, x8, :lo12:.LANCHOR0
	str	w4, [x0, 2648]
.L240:
	cmp	w11, w3
	bls	.L235
	add	w0, w3, 128
	cmp	w6, w0
	ble	.L235
	add	w0, w3, 256
	cmp	w11, w0
	bhi	.L241
	add	x0, x8, :lo12:.LANCHOR0
	add	w3, w3, 768
	ldr	w0, [x0, 2644]
	cmp	w3, w0
	bcs	.L235
.L241:
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
	b	.L234
.L239:
	add	w1, w1, 1
	and	w1, w1, 65535
	cmp	w1, w12
	bhi	.L235
	ldrh	w15, [x0, 4]
	cbz	w15, .L237
	sub	x0, x0, x7
	asr	x0, x0, 1
	mul	x0, x0, x13
	and	w20, w0, 65535
	and	x0, x0, 65535
	ldrh	w0, [x5, x0, lsl 1]
	cmp	w9, w0
	bcs	.L238
	cmp	w4, w0
	bls	.L237
	mov	w4, w0
	mov	w2, w20
.L237:
	umaddl	x0, w3, w14, x7
	b	.L236
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
	and	w0, w0, 65535
	mov	w1, 65535
	cmp	w0, w1
	beq	.L252
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
.L252:
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
	bne	.L256
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2786
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L256:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x0, 106]
	cmp	w1, w0
	bne	.L257
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2787
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L257:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L258
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2788
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L258:
	ldrb	w0, [x19, 6]
	add	x1, x20, :lo12:.LANCHOR0
	strb	wzr, [x19, 10]
	add	x0, x0, 8
	ldrh	w2, [x1, 36]
	mov	w1, 65535
	ldrh	w0, [x19, x0, lsl 1]
.L259:
	cmp	w0, w1
	beq	.L261
	ldrh	w21, [x19, 2]
	mov	w3, 65535
	ldrh	w1, [x19, 4]
	orr	w21, w21, w0, lsl 10
	ldrb	w0, [x19, 6]
	sub	w1, w1, #1
	and	w1, w1, 65535
	strh	w1, [x19, 4]
.L263:
	add	w0, w0, 1
	and	w0, w0, 255
	cmp	w2, w0
	bne	.L262
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	w0, 0
.L262:
	add	x4, x19, x0, sxtw 1
	ldrh	w4, [x4, 16]
	cmp	w4, w3
	beq	.L263
	add	x20, x20, :lo12:.LANCHOR0
	strb	w0, [x19, 6]
	ldrh	w2, [x19, 2]
	ldrh	w0, [x20, 106]
	cmp	w2, w0
	bne	.L255
	cbz	w1, .L255
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2809
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L255:
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L261:
	ldrb	w0, [x19, 6]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	cmp	w0, w2
	bne	.L260
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L260:
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
	b	.L259
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
.L271:
	ldrh	w2, [x1, 36]
	cmp	w3, w2
	bcc	.L272
	add	x0, x0, :lo12:.LANCHOR0
	mov	w6, 24
	mov	w5, 4
.L273:
	ldr	w1, [x0, 3288]
	cmp	w2, w1
	bcc	.L274
	ret
.L272:
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
	b	.L271
.L274:
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
	b	.L273
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
.L276:
	cmp	w3, w1
	bcs	.L275
	ubfiz	x4, x3, 5, 16
	mov	w2, 0
	add	x4, x0, x4
	b	.L281
.L277:
	add	w2, w2, 1
	and	w2, w2, 65535
.L281:
	cmp	w2, w7
	bcs	.L278
	umull	x6, w2, w8
	add	x9, x5, x6
	ldr	x10, [x5, x6]
	ldr	x6, [x4, 8]
	cmp	x10, x6
	bne	.L277
	str	wzr, [x9, 16]
.L278:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L276
.L275:
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
.L283:
	cmp	w3, w1
	bcs	.L282
	mov	w2, 0
	b	.L288
.L284:
	add	w2, w2, 1
	and	w2, w2, 65535
.L288:
	cmp	w2, w5
	bcs	.L285
	umaddl	x4, w2, w7, x6
	ldr	w9, [x4, 16]
	cbnz	w9, .L284
	ubfiz	x2, x3, 5, 16
	ldr	x9, [x4]
	add	x2, x0, x2
	str	w8, [x4, 16]
	str	x9, [x2, 8]
	ldr	x4, [x4, 8]
	str	x4, [x2, 16]
.L285:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L283
.L282:
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
.L290:
	cmp	w2, w1, uxth
	bhi	.L292
	mov	w0, 0
	ret
.L292:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L290
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
.L295:
	and	w2, w4, 65535
	cmp	w2, w1
	bcc	.L297
	bne	.L296
	and	x4, x4, 65535
	strh	w0, [x6, x4, lsl 1]
	add	x4, x3, :lo12:.LANCHOR0
	ldrh	w0, [x4, 3292]
	add	w0, w0, 1
	strh	w0, [x4, 3292]
	b	.L296
.L297:
	add	x4, x4, 1
	add	x2, x6, x4, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w9
	bne	.L295
.L296:
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
	beq	.L300
	ldrh	w2, [x0, 3322]
	cmp	w19, w2
	beq	.L300
	mov	w1, 65535
	cmp	w3, w1
	bne	.L301
	strh	w19, [x0, 3320]
.L300:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L301:
	cmp	w2, w1
	bne	.L300
	strh	w19, [x0, 3322]
	b	.L300
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
.L304:
	cmp	w0, w1, uxth
	bhi	.L306
	cmp	w0, 15
	bhi	.L305
	add	x19, x19, :lo12:.LANCHOR0
	add	w1, w0, 1
	add	x19, x19, 3328
	strh	w1, [x19, -4]
	strh	w21, [x19, w0, sxtw 1]
	b	.L305
.L306:
	add	x1, x1, 1
	add	x2, x20, x1, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w21
	bne	.L304
.L305:
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
	cbz	w1, .L309
	ldrh	w3, [x0, 3320]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L309
	ldrh	w2, [x0, 3362]
	cmp	w2, w1
	bcc	.L310
	strh	wzr, [x0, 3362]
.L310:
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x19, 3328
	ldrh	w1, [x19, 3362]
	ldrh	w0, [x0, x1, lsl 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 3362]
	add	w0, w0, 1
	strh	w0, [x19, 3362]
.L309:
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
.L320:
	cmp	w26, w19
	bhi	.L326
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
.L326:
	cbnz	w20, .L321
	mov	w3, w22
	mov	x2, x21
	mov	x1, x27
	mov	x0, x24
	bl	sftl_printk
.L321:
	cmp	w23, 4
	bne	.L322
	ldr	w1, [x21, x19, lsl 2]
	adrp	x0, .LC76
	add	x0, x0, :lo12:.LC76
.L328:
	bl	sftl_printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L325
	mov	w20, 0
	adrp	x0, .LC79
	add	x0, x0, :lo12:.LC79
	bl	sftl_printk
.L325:
	add	x19, x19, 1
	add	w22, w22, w23
	b	.L320
.L322:
	cmp	w23, 2
	bne	.L324
	ldrh	w1, [x21, x19, lsl 1]
	add	x0, x28, :lo12:.LC77
	b	.L328
.L324:
	ldrb	w1, [x21, x19]
	mov	x0, x25
	b	.L328
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	ubfiz	x22, x2, 5, 32
	stp	x23, x24, [sp, 48]
	add	x23, x22, 4
	mov	x20, x0
	add	x21, x0, 4
	ldrh	w24, [x19, 12]
	add	x23, x0, x23
	mov	w25, -1
	lsl	w26, w24, 3
.L330:
	cmp	x21, x23
	beq	.L334
	ldr	w0, [x21]
	add	x2, x29, 88
	add	x1, x29, 92
	bl	l2p_addr_tran.isra.0
	ldr	w0, [x29, 88]
	cbnz	w0, .L331
	ldr	w1, [x29, 92]
	cmp	w26, w1
	bls	.L331
	mov	x19, x20
	add	x22, x20, x22
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x21, .LC80
	add	x20, x20, 160
	add	x21, x21, :lo12:.LC80
.L332:
	cmp	x19, x22
	bne	.L333
	bl	dump_stack
.L334:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L333:
	ldr	w2, [x29, 92]
	mov	w0, -1
	str	w0, [x19]
	mov	x1, x20
	mov	x0, x21
	add	x19, x19, 32
	bl	sftl_printk
	ldr	x1, [x19, -24]
	mov	w3, 16
	mov	w2, 4
	adrp	x0, .LC81
	add	x0, x0, :lo12:.LC81
	bl	rknand_print_hex
	ldr	x1, [x19, -16]
	mov	w3, 4
	adrp	x0, .LC82
	mov	w2, w3
	add	x0, x0, :lo12:.LC82
	bl	rknand_print_hex
	b	.L332
.L331:
	ldr	w1, [x29, 92]
	ldr	x2, [x19, 3376]
	blr	x2
	cmp	w0, 0
	csel	w0, w25, wzr, ne
	str	w0, [x21, -4]
	ldrh	w0, [x19, 14]
	cmp	w0, 4
	bne	.L338
	ldrb	w0, [x29, 88]
	ldr	x2, [x19, 3376]
	ldr	w1, [x29, 92]
	add	w1, w24, w1
	blr	x2
	cbz	w0, .L338
	str	w25, [x21, -4]
.L338:
	add	x21, x21, 32
	b	.L330
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
	bcc	.L345
	adrp	x0, .LANCHOR0
	add	x20, x0, :lo12:.LANCHOR0
	mov	x19, x0
	ldrh	w2, [x20, 262]
	cmp	w2, 1024
	beq	.L345
	and	w1, w1, 65535
	cbz	w1, .L347
	mov	w0, w21
	bl	P2V_block_in_plane
	and	w22, w0, 65535
	ldr	x0, [x20, 3416]
	lsl	w1, w21, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x20, 3416]
	bl	FlashEraseBlocks
	ldr	x2, [x20, 2384]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x20, 2640]
	add	w0, w0, 1
	str	w0, [x20, 2640]
.L347:
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
.L345:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR0
	add	x29, sp, 0
	str	x21, [sp, 32]
	add	x21, x0, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	ldrh	w2, [x21, 262]
	cbz	w2, .L359
	ldrh	w0, [x21, 258]
	add	x1, x21, 256
	sub	w2, w2, #1
	strh	w2, [x21, 262]
	mov	w2, 1
	add	x3, x1, x0, sxtw 1
	add	w0, w0, 1
	and	w0, w0, 1023
	strh	w0, [x21, 258]
	ldr	x0, [x21, 3416]
	ldrh	w20, [x3, 8]
	lsl	w1, w20, 10
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x21, 3416]
	bl	FlashEraseBlocks
	ldr	w0, [x21, 2640]
	add	w0, w0, 1
	str	w0, [x21, 2640]
.L356:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L357
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrh	w2, [x0, 262]
	adrp	x0, .LC83
	add	x0, x0, :lo12:.LC83
	bl	sftl_printk
.L358:
	b	.L358
.L359:
	mov	w20, 65535
	b	.L356
.L357:
	mov	w0, w20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
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
.L362:
	cmp	w20, w1
	beq	.L366
	mov	x21, x0
	ldrh	w2, [x0], 2
	cbnz	w2, .L363
	bl	FtlFreeSysBlkQueueOut
	and	w1, w0, 65535
	strh	w0, [x21]
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L364
	adrp	x0, .LANCHOR0+262
	ldrh	w2, [x0, #:lo12:.LANCHOR0+262]
	adrp	x0, .LC84
	add	x0, x0, :lo12:.LC84
	bl	sftl_printk
.L365:
	b	.L365
.L364:
	ldr	w0, [x19, 48]
	strh	wzr, [x19, 2]
	add	w0, w0, 1
	str	w0, [x19, 48]
	ldrh	w0, [x19, 8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19, 8]
.L366:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L368
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 578
	add	x1, x1, 184
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L368:
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L363:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L362
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
	add	x3, x19, 3328
	strh	wzr, [x19, 3240]
	strh	wzr, [x19, 3324]
	stp	wzr, wzr, [x1, 28]
	stp	wzr, wzr, [x1, 36]
	str	wzr, [x19, 2604]
	str	wzr, [x19, 2608]
	stp	wzr, wzr, [x2, 60]
	stp	wzr, wzr, [x2, 68]
	str	wzr, [x19, 2640]
	str	wzr, [x19, 2644]
	str	wzr, [x19, 2648]
	str	wzr, [x19, 3236]
	str	wzr, [x19, 3244]
	stp	wzr, w0, [x3, 96]
	mov	w0, -1
	stp	wzr, wzr, [x3, 104]
	strh	w0, [x19, 3320]
	strh	w0, [x19, 3322]
	mov	w0, 32
	strh	w0, [x19, 3232]
	mov	w0, 128
	strh	w0, [x19, 3234]
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
	str	x0, [x19, 3440]
	mov	w0, w22
	bl	ftl_malloc
	str	x0, [x19, 3448]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3456]
	mov	w0, w22
	bl	ftl_malloc
	str	x0, [x19, 3416]
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
	str	x0, [x19, 3464]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3472]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3480]
	ldr	w0, [x19, 3288]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19, 3264]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3488]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3496]
	ldr	w1, [x19, 3288]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3256]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3504]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3512]
	ldrh	w0, [x19, 62]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w21, [x19, 116]
	str	x0, [x19, 3520]
	ldrh	w0, [x19, 36]
	mul	w21, w21, w0
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3528]
	lsl	w0, w21, 2
	bl	ftl_malloc
	str	x0, [x19, 3536]
	ldrh	w1, [x19, 116]
	ldr	w0, [x19, 3288]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3272]
	ldrh	w0, [x19, 46]
	ubfiz	w0, w0, 1, 15
	strh	w0, [x19, 3544]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19, 3552]
	ldrh	w0, [x19, 3544]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19, 3544]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w21, [x19, 46]
	str	x0, [x19, 3560]
	add	x0, x0, 32
	str	x0, [x19, 2384]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3568]
	mov	w0, w21
	bl	ftl_malloc
	ldr	w21, [x19, 132]
	str	x0, [x19, 2376]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3576]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3584]
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
	str	x0, [x19, 3592]
	ldrh	w0, [x19, 124]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3600]
	ldrh	w0, [x19, 126]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w2, [x19, 126]
	mov	w1, 0
	str	x0, [x19, 3608]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x19, 140]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3616]
	ldr	w0, [x19, 132]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3624]
	ldrh	w0, [x19, 142]
	lsl	w0, w0, 4
	bl	ftl_malloc
	ldrh	w1, [x19, 142]
	str	x0, [x19, 2568]
	ldrh	w0, [x19, 114]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3632]
	ldrh	w1, [x19, 46]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 2360]
	ldrh	w0, [x19, 102]
	ldrh	w1, [x19, 58]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19, 3640]
	mul	w0, w1, w0
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w4, [x19, 3640]
	add	x5, x19, 200
	ldrh	w6, [x19, 58]
	mov	x1, 1
	str	x0, [x19, 192]
	mov	w2, w4
	mov	x0, x20
.L373:
	cmp	w1, w6
	bcc	.L374
	mov	w2, 8
	add	x4, x0, :lo12:.LANCHOR0
	sub	w2, w2, w1
	add	x4, x4, 160
	add	x2, x2, 1
	mov	x3, 0
.L375:
	add	x3, x3, 1
	cmp	x3, x2
	bne	.L376
	add	x1, x0, :lo12:.LANCHOR0
	ldr	x2, [x1, 3576]
	cbnz	x2, .L377
.L379:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 216
	adrp	x0, .LC85
	add	x0, x0, :lo12:.LC85
	bl	sftl_printk
	mov	w0, -1
.L372:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L374:
	ldr	x3, [x19, 192]
	add	w1, w1, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x5], 8
	b	.L373
.L376:
	add	x5, x1, x3
	add	x5, x4, x5, lsl 3
	str	xzr, [x5, 24]
	b	.L375
.L377:
	ldr	x2, [x1, 3584]
	cbz	x2, .L379
	ldr	x2, [x1, 3616]
	cbz	x2, .L379
	ldr	x2, [x1, 3624]
	cbz	x2, .L379
	ldr	x2, [x1, 2568]
	cbz	x2, .L379
	ldr	x2, [x1, 3632]
	cbz	x2, .L379
	ldr	x2, [x1, 2360]
	cbz	x2, .L379
	ldr	x2, [x1, 192]
	cbz	x2, .L379
	ldr	x1, [x1, 2376]
	cbz	x1, .L379
	add	x1, x0, :lo12:.LANCHOR0
	ldr	x2, [x1, 3296]
	cbz	x2, .L379
	ldr	x2, [x1, 3312]
	cbz	x2, .L379
	ldr	x2, [x1, 3440]
	cbz	x2, .L379
	ldr	x2, [x1, 3456]
	cbz	x2, .L379
	ldr	x2, [x1, 3416]
	cbz	x2, .L379
	ldr	x2, [x1, 3280]
	cbz	x2, .L379
	ldr	x2, [x1, 3448]
	cbz	x2, .L379
	ldr	x2, [x1, 3464]
	cbz	x2, .L379
	ldr	x2, [x1, 3472]
	cbz	x2, .L379
	ldr	x1, [x1, 3480]
	cbz	x1, .L379
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3264]
	cbz	x1, .L379
	ldr	x1, [x0, 3488]
	cbz	x1, .L379
	ldr	x1, [x0, 3496]
	cbz	x1, .L379
	ldr	x1, [x0, 3256]
	cbz	x1, .L379
	ldr	x1, [x0, 3528]
	cbz	x1, .L379
	ldr	x1, [x0, 3536]
	cbz	x1, .L379
	ldr	x1, [x0, 3272]
	cbz	x1, .L379
	ldr	x1, [x0, 2384]
	cbz	x1, .L379
	ldr	x1, [x0, 3552]
	cbz	x1, .L379
	ldr	x0, [x0, 152]
	cbz	x0, .L379
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3592]
	cbz	x1, .L379
	ldr	x1, [x0, 3600]
	cbz	x1, .L379
	ldr	x0, [x0, 3608]
	cbz	x0, .L379
	mov	w0, 0
	b	.L372
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	x23, x0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x24, .LC1
	add	x20, x20, 232
	ldrh	w2, [x0, 3640]
	add	x24, x24, :lo12:.LC1
	mov	x19, 0
	mov	x22, x1
	mov	x0, x22
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
.L477:
	ldrh	w0, [x23, x19]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L474
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w1, [x1, 102]
	cmp	w1, w0
	bhi	.L476
	mov	w2, 74
	mov	x1, x20
	mov	x0, x24
	bl	sftl_printk
.L476:
	ldrh	w2, [x23, x19]
	mov	w1, 1
	add	x19, x19, 2
	cmp	x19, 1024
	ubfx	x0, x2, 5, 11
	lsl	w2, w1, w2
	lsl	x0, x0, 2
	ldr	w1, [x22, x0]
	orr	w1, w1, w2
	str	w1, [x22, x0]
	bne	.L477
.L474:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
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
	str	x25, [sp, 64]
	ldrh	w2, [x0, 10]
	ldp	x21, x20, [x0, 32]
	ldr	x22, [x0, 16]
	lsl	w2, w2, 1
	mov	x0, x21
	bl	ftl_memset
	mov	w0, 0
.L486:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L490
	adrp	x0, .LANCHOR0+108
	mov	w23, 0
	mov	w20, 0
	ldrh	w1, [x0, #:lo12:.LANCHOR0+108]
	ldrh	w0, [x19]
	strh	w1, [x21, x0, lsl 1]
	ldrh	w24, [x21]
.L491:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L495
	mov	w0, w23
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L490:
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20, x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L487:
	ldrh	w3, [x19, 10]
	cmp	w3, w1
	bhi	.L489
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L486
.L489:
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22, x3]
	cmp	w4, w2
	bne	.L488
	cbz	w2, .L488
	ldrh	w4, [x21, x3]
	add	w4, w4, 1
	strh	w4, [x21, x3]
.L488:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L487
.L495:
	ubfiz	x0, x20, 1, 16
	ldrh	w1, [x21, x0]
	cmp	w24, w1
	bls	.L492
	add	x25, x22, x0
	ldrh	w0, [x22, x0]
	cbnz	w0, .L493
.L494:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L491
.L492:
	cbnz	w1, .L494
	add	x25, x22, x0
	ldrh	w0, [x22, x0]
	cbz	w0, .L494
.L496:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19, 8]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	b	.L494
.L497:
	mov	w24, 0
	b	.L496
.L493:
	mov	w23, w20
	cbz	w1, .L497
	mov	w24, w1
	b	.L494
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR0
	mov	w1, 0
	add	x29, sp, 0
	str	x19, [sp, 16]
	add	x19, x0, :lo12:.LANCHOR0
	ldr	x0, [x19, 3584]
	ldr	w2, [x19, 132]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x19, 142]
	mov	w1, 255
	ldrh	w2, [x19, 114]
	mul	w2, w2, w0
	ldr	x0, [x19, 3632]
	bl	ftl_memset
	mov	x0, x19
	mov	w1, 0
	mov	w5, -1
.L507:
	ldrh	w2, [x0, 142]
	cmp	w2, w1
	bhi	.L508
	mov	w2, -1
	strh	w2, [x0, 3650]
	strh	w2, [x0, 3648]
	add	x1, x0, 3648
	ldr	w2, [x0, 132]
	strh	w2, [x0, 3658]
	mov	w2, -3902
	strh	w2, [x0, 3652]
	ldrh	w2, [x0, 3712]
	strh	w2, [x0, 3656]
	ldrh	w2, [x0, 140]
	strh	w2, [x0, 3654]
	ldr	x2, [x0, 3576]
	str	x2, [x0, 3664]
	ldr	x2, [x0, 3624]
	str	x2, [x0, 3672]
	ldr	x2, [x0, 3584]
	str	x2, [x0, 3680]
	ldr	x0, [x0, 3616]
	str	x0, [x1, 40]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L508:
	ldr	x3, [x0, 2568]
	ubfiz	x2, x1, 4, 16
	add	x3, x3, x2
	str	wzr, [x3, 4]
	ldr	x3, [x0, 2568]
	strh	w5, [x3, x2]
	ldr	x3, [x0, 2568]
	ldr	x4, [x0, 3632]
	add	x3, x3, x2
	ldrh	w2, [x0, 114]
	mul	w2, w2, w1
	add	w1, w1, 1
	and	w1, w1, 65535
	sxtw	x2, w2
	and	x2, x2, -4
	add	x2, x4, x2
	str	x2, [x3, 8]
	b	.L507
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	stp	x29, x30, [sp, -32]!
	mov	w0, -1
	mov	w1, 0
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	strh	w0, [x19, 3714]
	mov	w0, -1
	str	w0, [x19, 3724]
	ldr	x0, [x19, 152]
	ldrh	w2, [x19, 124]
	strh	wzr, [x19, 148]
	str	wzr, [x19, 3716]
	lsl	w2, w2, 1
	str	wzr, [x19, 3720]
	bl	ftl_memset
	ldr	x0, [x19, 2384]
	mov	w1, 0
	ldrh	w2, [x19, 46]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 3552]
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
.L513:
	ldrh	w0, [x19, 44]
	cmp	w20, w0
	bcs	.L520
	ldrh	w8, [x19, 36]
	mov	x6, 0
	ldrh	w7, [x19, 106]
	mov	w5, 0
	b	.L521
.L515:
	ldrb	w0, [x23, x6]
	mov	w1, w20
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L514
	add	w5, w5, w7
	and	w5, w5, 65535
.L514:
	add	x6, x6, 1
.L521:
	cmp	w8, w6, uxth
	bhi	.L515
	cbz	w5, .L516
	mov	w0, 32768
	sdiv	w5, w0, w5
.L517:
	ldr	x1, [x19, 2360]
	mov	w0, 6
	umaddl	x0, w20, w0, x1
	strh	w5, [x0, 4]
	ldrh	w0, [x19, 2424]
	cmp	w0, w20
	beq	.L518
	ldrh	w0, [x19, 2472]
	cmp	w0, w20
	beq	.L518
	ldrh	w0, [x19, 2520]
	cmp	w0, w20
	beq	.L518
	ldr	x1, [x19, 2376]
	ubfiz	x0, x20, 1, 16
	ldrh	w0, [x1, x0]
	cbnz	w0, .L519
	add	w22, w22, 1
	mov	w0, w20
	and	w22, w22, 65535
	bl	INSERT_FREE_LIST
.L518:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L513
.L516:
	ldr	x1, [x19, 2376]
	ubfiz	x0, x20, 1, 16
	mov	w2, -1
	strh	w2, [x1, x0]
	b	.L517
.L519:
	add	w21, w21, 1
	mov	w0, w20
	and	w21, w21, 65535
	bl	INSERT_DATA_LIST
	b	.L518
.L520:
	strh	w21, [x19, 2400]
	add	w21, w21, w22
	strh	w22, [x19, 2416]
	cmp	w21, w0
	ble	.L522
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2216
	add	x1, x1, 248
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L522:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
	.size	SupperBlkListInit, .-SupperBlkListInit
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
	bls	.L527
	mov	w2, 256
	mov	w1, 255
	mov	x0, x20
	bl	ftl_memset
	mov	w0, 0
.L527:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19, 14]
	cmp	w1, 4
	bne	.L531
	mov	x1, 0
.L529:
	cmp	w0, w1, uxth
	bhi	.L530
.L531:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L530:
	ldrh	w2, [x20, x1, lsl 1]
	lsr	w2, w2, 1
	strh	w2, [x20, x1, lsl 1]
	add	x1, x1, 1
	b	.L529
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
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	stp	x29, x30, [sp, -112]!
	ubfiz	x1, x1, 5, 32
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x2, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x19, x20, [sp, 16]
	adrp	x22, .LANCHOR1
	stp	x25, x26, [sp, 64]
	add	x22, x22, :lo12:.LANCHOR1
	str	x27, [sp, 80]
	adrp	x23, .LC1
	mov	x19, x0
	add	x24, x0, x1
	ldrh	w25, [x2, 12]
	add	x22, x22, 272
	add	x23, x23, :lo12:.LC1
.L536:
	cmp	x24, x19
	bne	.L547
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L547:
	ldr	x0, [x19, 8]
	cbz	x0, .L537
	ldr	x0, [x19, 16]
	cbnz	x0, .L538
.L537:
	mov	w2, 96
	mov	x1, x22
	mov	x0, x23
	bl	sftl_printk
.L538:
	ldr	w0, [x19, 4]
	add	x2, x29, 104
	add	x1, x29, 108
	bl	l2p_addr_tran.isra.0
	ldr	w0, [x29, 104]
	cmp	w0, 3
	bls	.L539
	mov	w0, -1
	str	w0, [x19]
.L540:
	add	x19, x19, 32
	b	.L536
.L539:
	ldr	x20, [x19, 8]
	tst	x20, 63
	beq	.L541
	add	x1, x21, :lo12:.LANCHOR0
	ldr	x20, [x1, 3504]
.L541:
	add	x27, x21, :lo12:.LANCHOR0
	ldr	w1, [x29, 108]
	ldr	x3, [x19, 16]
	mov	x2, x20
	ldr	x4, [x27, 3392]
	blr	x4
	str	w0, [x19]
	ldrh	w0, [x27, 14]
	cmp	w0, 4
	bne	.L543
	ldrb	w0, [x29, 104]
	add	x2, x20, 2048
	ldr	x4, [x27, 3392]
	ldr	w1, [x29, 108]
	ldr	x3, [x19, 16]
	add	w1, w25, w1
	add	x3, x3, 8
	blr	x4
	cmn	w0, #1
	beq	.L544
	ldr	x2, [x19, 16]
	ldr	w1, [x2, 12]
	cmn	w1, #1
	bne	.L545
	ldr	w1, [x2, 8]
	cmn	w1, #1
	bne	.L545
	ldr	w1, [x2]
	cmn	w1, #1
	beq	.L545
.L544:
	mov	w1, -1
	str	w1, [x19]
.L545:
	ldr	w1, [x19]
	cmn	w1, #1
	beq	.L543
	cmp	w0, 256
	bne	.L543
	str	w0, [x19]
.L543:
	add	x1, x21, :lo12:.LANCHOR0
	ldr	x0, [x1, 3504]
	cmp	x20, x0
	bne	.L540
	ldr	x0, [x19, 8]
	cmp	x20, x0
	beq	.L540
	ldrh	w2, [x1, 62]
	mov	x1, x20
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L540
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
	add	x23, x0, 3728
	mov	x25, x23
	mov	w26, 61664
	ldr	x1, [x0, 3464]
	ldr	x24, [x0, 3528]
	stp	x1, x24, [x23, 8]
.L565:
	ldrh	w0, [x19, 58]
	cmp	w21, w0
	bcc	.L570
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L570:
	ldrh	w20, [x19, 102]
	mov	w0, -1
	strh	w0, [x22]
.L567:
	ldrh	w0, [x19, 102]
	sub	w20, w20, #1
	and	w20, w20, 65535
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L568
	madd	w0, w0, w21, w20
	mov	w2, 1
	mov	w1, w2
	lsl	w0, w0, 10
	str	w0, [x25, 4]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L567
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L567
	strh	w20, [x22]
.L568:
	add	w21, w21, 1
	add	x22, x22, 2
	b	.L565
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
	add	x2, x1, :lo12:.LANCHOR0
	bne	.L576
	ldrh	w19, [x2, 108]
.L577:
	add	x1, x1, :lo12:.LANCHOR0
	sub	w19, w19, #1
	sxth	w19, w19
	lsl	w21, w0, 10
	orr	w0, w19, w21
	mov	w2, w23
	ldr	x1, [x1, 3512]
	str	x1, [x29, 72]
	add	x1, x29, 96
	str	w0, [x29, 68]
	add	x0, x29, 64
	str	x1, [x29, 80]
	mov	w1, 1
	bl	FlashReadPages
	ldr	w0, [x29, 96]
	cmn	w0, #1
	bne	.L578
	mov	w22, 0
	mov	w24, 2
.L579:
	cmp	w22, w19
	ble	.L582
.L578:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 160
	ret
.L576:
	ldrh	w19, [x2, 106]
	b	.L577
.L582:
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
	bne	.L580
	ldr	w0, [x29, 100]
	cmn	w0, #1
	bne	.L580
	ldr	w0, [x29, 64]
	cmn	w0, #1
	beq	.L580
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L579
.L580:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L579
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	stp	x29, x30, [sp, -112]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x19, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR1
	stp	x27, x28, [sp, 80]
	add	x25, x25, :lo12:.LANCHOR1
	ldr	x0, [x19, 3624]
	add	x25, x25, 288
	ldr	w2, [x19, 132]
	strh	wzr, [x19, 3712]
	strh	wzr, [x19, 148]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x19, 3576]
	mov	w1, 0
	ldr	w2, [x19, 132]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 3600]
	mov	w1, 0
	ldrh	w2, [x19, 124]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x19, 152]
	mov	w1, 0
	ldrh	w2, [x19, 124]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w2, 16
	mov	w1, 255
	add	x0, x19, 2656
	bl	ftl_memset
	ldrh	w24, [x19, 44]
.L588:
	ldrh	w0, [x19, 46]
	cmp	w0, w24
	bls	.L628
	ldrh	w9, [x19, 36]
	mov	x5, 0
	ldrh	w8, [x19, 116]
	mov	w20, 0
	add	x7, x19, 64
	mov	w6, 4
	b	.L629
.L590:
	ldrb	w0, [x7, x5]
	mov	w1, w24
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L589
	ldr	x0, [x19, 3440]
	ubfiz	x2, x20, 5, 16
	lsl	w4, w4, 10
	add	x0, x0, x2
	str	w4, [x0, 4]
	ldr	x1, [x19, 3440]
	ldr	x0, [x19, 3264]
	add	x1, x1, x2
	ldr	x2, [x19, 3272]
	str	x0, [x1, 8]
	mul	w0, w20, w8
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w0, w0, w6
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L589:
	add	x5, x5, 1
.L629:
	cmp	w9, w5, uxth
	bhi	.L590
	cbnz	w20, .L591
.L627:
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L588
.L591:
	ldr	x0, [x19, 3440]
	mov	w1, w20
	mov	w2, 1
	ubfiz	x20, x20, 5, 16
	mov	x23, 0
	add	x27, x19, 2656
	bl	FlashReadPages
.L626:
	ldr	x0, [x19, 3440]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	ldr	w22, [x1, 4]
	cmn	w0, #1
	ldr	x26, [x1, 16]
	ubfx	x22, x22, 10, 16
	bne	.L594
	mov	w28, 16
	mov	w3, 65535
.L596:
	ldr	x0, [x19, 3440]
	mov	w2, 1
	str	w3, [x29, 108]
	add	x0, x0, x23
	ldr	w1, [x0, 4]
	add	w1, w1, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 3440]
	add	x0, x0, x23
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	w3, [x29, 108]
	cmp	w0, w3
	ldr	x0, [x19, 3440]
	bne	.L593
	mov	w1, -1
	str	w1, [x0, x23]
	ldr	x0, [x19, 3440]
	ldr	w0, [x0, x23]
	cmp	w0, w1
	bne	.L594
.L595:
	mov	w1, 1
	b	.L666
.L593:
	ldr	w0, [x0, x23]
	cmn	w0, #1
	bne	.L594
	sub	w28, w28, #1
	ands	w28, w28, 65535
	bne	.L596
	b	.L595
.L594:
	ldr	w1, [x19, 2620]
	ldr	w0, [x26, 4]
	cmn	w1, #1
	beq	.L597
	cmp	w1, w0
	bhi	.L598
.L597:
	cmn	w0, #1
	beq	.L598
	add	w1, w0, 1
	str	w1, [x19, 2620]
.L598:
	ldrh	w1, [x26]
	mov	w2, 61604
	cmp	w1, w2
	beq	.L600
	bhi	.L601
	mov	w0, 61574
	cmp	w1, w0
	beq	.L602
.L599:
	add	x23, x23, 32
	cmp	x20, x23
	bne	.L626
	b	.L627
.L601:
	mov	w0, 61634
	cmp	w1, w0
	beq	.L603
	mov	w0, 65535
	cmp	w1, w0
	bne	.L599
	mov	w1, 0
.L666:
	mov	w0, w22
	bl	FtlFreeSysBlkQueueIn
	b	.L599
.L603:
	ldrh	w1, [x19, 3712]
	ldr	w0, [x19, 132]
	cmp	w1, w0
	bls	.L605
	adrp	x0, .LC1
	mov	w2, 1225
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L605:
	ldr	w3, [x19, 132]
	ldrh	w1, [x19, 3712]
	and	w2, w3, 65535
	ldr	x4, [x19, 3624]
	sub	w0, w2, #1
	sub	w2, w2, w1
	sub	w2, w2, #1
	sxth	x0, w0
	sxth	w2, w2
.L606:
	cmp	w0, w2
	bgt	.L612
	tbz	w0, #31, .L646
	b	.L599
.L612:
	sxtw	x5, w0
	ldr	w8, [x26, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L607
	ldr	w2, [x4]
	cbnz	w2, .L608
	cmp	w3, w1
	beq	.L608
	add	w1, w1, 1
	strh	w1, [x19, 3712]
.L608:
	mov	w1, 0
.L609:
	cmp	w1, w0
	bne	.L610
	ldr	x1, [x19, 3624]
	ldr	w2, [x26, 4]
	str	w2, [x1, x6]
	ldr	x1, [x19, 3576]
	strh	w22, [x1, x5, lsl 1]
	tbnz	w0, #31, .L599
	ldrh	w1, [x19, 3712]
	ldr	w2, [x19, 132]
	sub	w2, w2, w1
	sub	w2, w2, #1
	cmp	w0, w2, sxth
	bgt	.L599
.L646:
	add	w1, w1, 1
	strh	w1, [x19, 3712]
	ldr	x1, [x19, 3624]
	ldr	w2, [x26, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 3576]
.L664:
	strh	w22, [x1, x0, lsl 1]
	b	.L599
.L610:
	ldr	x4, [x19, 3624]
	sxtw	x2, w1
	lsl	x3, x2, 2
	lsl	x2, x2, 1
	add	x7, x4, x3
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w7, [x7, 4]
	str	w7, [x4, x3]
	ldr	x3, [x19, 3576]
	add	x4, x3, x2
	ldrh	w4, [x4, 2]
	strh	w4, [x3, x2]
	b	.L609
.L607:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L606
.L602:
	ldrh	w1, [x19, 148]
	ldrh	w0, [x19, 124]
	cmp	w1, w0
	bls	.L615
	adrp	x0, .LC1
	mov	w2, 1266
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L615:
	ldrh	w3, [x19, 124]
	ldrh	w1, [x19, 148]
	sub	w2, w3, #1
	ldr	x4, [x19, 3600]
	sxth	x0, w2
	sub	w2, w2, w1
.L616:
	cmp	w0, w2
	ble	.L621
	sxtw	x5, w0
	ldr	w8, [x26, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L617
	ldr	w2, [x4]
	cbnz	w2, .L618
	cmp	w3, w1
	beq	.L618
	add	w1, w1, 1
	strh	w1, [x19, 148]
.L618:
	mov	w1, 0
.L619:
	cmp	w1, w0
	bne	.L620
	ldr	x1, [x19, 3600]
	ldr	w2, [x26, 4]
	str	w2, [x1, x6]
	ldr	x1, [x19, 152]
	strh	w22, [x1, x5, lsl 1]
.L621:
	tbnz	w0, #31, .L599
	ldrh	w1, [x19, 124]
	ldrh	w2, [x19, 148]
	sub	w1, w1, #1
	sub	w1, w1, w2
	cmp	w0, w1, sxth
	bgt	.L599
	ldr	x1, [x19, 3600]
	add	w2, w2, 1
	strh	w2, [x19, 148]
	ldr	w2, [x26, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 152]
	b	.L664
.L620:
	ldr	x4, [x19, 3600]
	sxtw	x2, w1
	lsl	x3, x2, 2
	lsl	x2, x2, 1
	add	x7, x4, x3
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w7, [x7, 4]
	str	w7, [x4, x3]
	ldr	x3, [x19, 152]
	add	x4, x3, x2
	ldrh	w4, [x4, 2]
	strh	w4, [x3, x2]
	b	.L619
.L617:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L616
.L600:
	ldrh	w2, [x27]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L623
	strh	w22, [x27]
.L665:
	str	w0, [x27, 8]
	b	.L599
.L623:
	ldrh	w0, [x27, 4]
	cmp	w0, w1
	beq	.L624
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L624:
	ldr	w0, [x26, 4]
	ldr	w1, [x27, 8]
	cmp	w1, w0
	bcs	.L625
	ldrh	w0, [x27]
	strh	w0, [x27, 4]
	strh	w22, [x27]
	ldr	w0, [x26, 4]
	b	.L665
.L625:
	strh	w22, [x27, 4]
	b	.L599
.L628:
	ldr	x1, [x19, 3576]
	ldrh	w0, [x1]
	cbz	w0, .L630
.L633:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0, 152]
	ldrh	w2, [x1]
	cbz	w2, .L631
.L632:
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w1, [x21, 3712]
	ldr	w0, [x21, 132]
	cmp	w1, w0
	bls	.L662
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1391
	add	x1, x1, 288
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L662:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L630:
	ldrh	w0, [x19, 3712]
	cbz	w0, .L633
	ldr	w2, [x19, 132]
	mov	w0, 0
.L634:
	cmp	w0, w2
	bcs	.L633
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L635
	mov	w1, w0
	add	x2, x21, :lo12:.LANCHOR0
.L636:
	ldr	w3, [x2, 132]
	cmp	w1, w3
	bcs	.L633
	ldr	x5, [x2, 3576]
	sxtw	x6, w1
	lsl	x4, x6, 1
	sub	w3, w1, w0
	sxtw	x3, w3
	add	w1, w1, 1
	sxth	w1, w1
	ldrh	w7, [x5, x4]
	strh	w7, [x5, x3, lsl 1]
	ldr	x5, [x2, 3624]
	ldr	w6, [x5, x6, lsl 2]
	str	w6, [x5, x3, lsl 2]
	ldr	x3, [x2, 3576]
	strh	wzr, [x3, x4]
	b	.L636
.L635:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L634
.L631:
	ldrh	w2, [x0, 148]
	cbz	w2, .L632
	ldrh	w2, [x0, 124]
	mov	w0, 0
.L641:
	mov	w6, w0
	cmp	w0, w2
	bge	.L632
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L642
	add	x1, x21, :lo12:.LANCHOR0
.L643:
	ldrh	w2, [x1, 124]
	cmp	w0, w2
	bge	.L632
	ldr	x4, [x1, 152]
	sxtw	x5, w0
	lsl	x3, x5, 1
	sub	w2, w0, w6
	sxtw	x2, w2
	add	w0, w0, 1
	sxth	w0, w0
	ldrh	w7, [x4, x3]
	strh	w7, [x4, x2, lsl 1]
	ldr	x4, [x1, 3600]
	ldr	w5, [x4, x5, lsl 2]
	str	w5, [x4, x2, lsl 2]
	ldr	x2, [x1, 152]
	strh	wzr, [x2, x3]
	b	.L643
.L642:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L641
	.size	FtlScanSysBlk, .-FtlScanSysBlk
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
	add	x21, x23, 3728
	mov	w24, 61649
	ldr	x0, [x23, 3464]
	ldr	x22, [x23, 3528]
	stp	x0, x22, [x21, 8]
	bl	FtlBbtMemInit
	ldrh	w20, [x23, 102]
	sub	w20, w20, #1
	and	w20, w20, 65535
.L668:
	ldrh	w0, [x23, 102]
	sub	w0, w0, #16
	cmp	w20, w0
	ble	.L671
	lsl	w0, w20, 10
	mov	w2, 1
	str	w0, [x21, 4]
	mov	w1, w2
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L669
	ldr	w0, [x21, 4]
	mov	w2, 1
	mov	w1, w2
	add	w0, w0, 1
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
.L669:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L670
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L670
	ldr	w1, [x22, 4]
	str	w1, [x23, 168]
	strh	w20, [x23, 160]
	ldrh	w1, [x22, 8]
	strh	w1, [x23, 164]
.L671:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21, 160]
	cmp	w1, w0
	beq	.L685
	ldrh	w1, [x21, 164]
	cmp	w1, w0
	beq	.L675
	add	x0, x21, 3728
	lsl	w1, w1, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x21, 3728]
	cmn	w0, #1
	beq	.L675
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L675
	ldr	w1, [x21, 168]
	ldr	w0, [x22, 4]
	cmp	w0, w1
	bls	.L675
	str	w0, [x21, 168]
	ldrh	w1, [x21, 164]
	ldrh	w0, [x22, 8]
	strh	w1, [x21, 160]
	strh	w0, [x21, 164]
.L675:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x23, x20, 3728
	mov	w24, 61649
	ldrh	w0, [x20, 160]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20, 162]
.L677:
	tbz	w21, #31, .L680
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 253
	add	x1, x1, 304
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L679:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 10]
	mov	w2, 65535
	strh	w0, [x1, 166]
	ldrh	w0, [x22, 12]
	cmp	w0, w2
	beq	.L682
	ldr	w2, [x1, 32]
	cmp	w0, w2
	beq	.L682
	ldrh	w1, [x1, 46]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L682
	cmp	w0, w1
	bcs	.L682
	bl	FtlSysBlkNumInit
.L682:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 0
	add	x21, x19, 192
.L683:
	ldrh	w0, [x19, 58]
	cmp	w20, w0
	bcc	.L684
	mov	w0, 0
.L667:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L670:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L668
.L680:
	ldrh	w0, [x20, 160]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w21, w0, lsl 10
	str	w0, [x23, 4]
	ldr	x0, [x20, 3464]
	str	x0, [x23, 8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L678
	ldrh	w0, [x22]
	cmp	w0, w24
	beq	.L679
.L678:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L677
.L684:
	ldrh	w2, [x19, 3640]
	ldr	x0, [x19, 3736]
	mul	w1, w2, w20
	lsl	w2, w2, 2
	add	w20, w20, 1
	add	x1, x0, x1, lsl 2
	ldr	x0, [x21], 8
	bl	ftl_memcpy
	b	.L683
.L685:
	mov	w0, -1
	b	.L667
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	stp	x29, x30, [sp, -160]!
	ubfiz	x1, x1, 5, 32
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp, 16]
	add	x22, x0, x1
	str	w2, [x29, 108]
	add	x2, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR1
	stp	x27, x28, [sp, 80]
	add	x23, x23, :lo12:.LANCHOR1
	stp	x25, x26, [sp, 64]
	adrp	x24, .LC1
	mov	w27, w3
	mov	x19, x0
	ldrh	w25, [x2, 12]
	mov	x20, x0
	add	x23, x23, 320
	add	x24, x24, :lo12:.LC1
	lsl	w28, w25, 3
.L698:
	cmp	x20, x22
	bne	.L712
	cbz	w27, .L706
	adrp	x20, .LC86
	adrp	x23, .LC87
	add	x20, x20, :lo12:.LC86
	add	x23, x23, :lo12:.LC87
	b	.L733
.L712:
	ldr	x0, [x20, 8]
	cbz	x0, .L699
	ldr	x0, [x20, 16]
	cbnz	x0, .L700
.L699:
	mov	w2, 140
	mov	x1, x23
	mov	x0, x24
	bl	sftl_printk
.L700:
	ldr	w0, [x20, 4]
	add	x2, x29, 120
	add	x1, x29, 124
	bl	l2p_addr_tran.isra.0
	ldr	w0, [x29, 120]
	cmp	w0, 3
	bls	.L701
.L735:
	mov	w0, -1
	str	w0, [x20]
	b	.L702
.L701:
	cbnz	w0, .L703
	ldr	w0, [x29, 124]
	cmp	w28, w0
	bls	.L703
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x21, .LC80
	add	x20, x20, 320
	add	x21, x21, :lo12:.LC80
	b	.L732
.L705:
	ldr	w2, [x19, 4]
	mov	w0, -1
	str	w0, [x19]
	mov	x1, x20
	mov	x0, x21
	add	x19, x19, 32
	bl	sftl_printk
	ldr	x1, [x19, -24]
	mov	w3, 16
	mov	w2, 4
	adrp	x0, .LC81
	add	x0, x0, :lo12:.LC81
	bl	rknand_print_hex
	ldr	x1, [x19, -16]
	mov	w3, 4
	adrp	x0, .LC82
	mov	w2, w3
	add	x0, x0, :lo12:.LC82
	bl	rknand_print_hex
.L732:
	cmp	x19, x22
	bne	.L705
	bl	dump_stack
.L706:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L703:
	ldr	x1, [x20, 8]
	tst	x1, 63
	beq	.L719
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x26, [x0, 3504]
	cmp	x1, x26
	beq	.L707
	ldrh	w2, [x0, 62]
	mov	x0, x26
	lsl	w2, w2, 9
	bl	ftl_memcpy
.L707:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29, 124]
	ldr	x3, [x20, 16]
	mov	x2, x26
	ldr	x5, [x0, 3384]
	ldrb	w0, [x29, 120]
	blr	x5
	cbnz	w0, .L708
	str	wzr, [x20]
.L709:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0, 14]
	cmp	w1, 4
	bne	.L702
	ldr	x5, [x0, 3384]
	add	x2, x26, 2048
	ldrb	w0, [x29, 120]
	ldr	w1, [x29, 124]
	ldr	x3, [x20, 16]
	add	w1, w25, w1
	add	x3, x3, 8
	blr	x5
	cbnz	w0, .L735
.L702:
	add	x20, x20, 32
	b	.L698
.L719:
	mov	x26, x1
	b	.L707
.L708:
	mov	w0, -1
	str	w0, [x20]
	b	.L709
.L717:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29, 108]
	ldr	x1, [x0, 3504]
	str	wzr, [x1]
	ldr	x1, [x0, 3520]
	str	wzr, [x1]
	ldr	w1, [x19, 4]
	str	w1, [x29, 132]
	ldr	x1, [x0, 3504]
	ldr	x0, [x0, 3520]
	stp	x1, x0, [x29, 136]
	mov	w1, 1
	add	x0, x29, 128
	bl	FlashReadPages
	ldr	w24, [x29, 128]
	cmn	w24, #1
	bne	.L714
	ldr	w1, [x19, 4]
	mov	x0, x20
	bl	sftl_printk
	str	w24, [x19]
.L714:
	ldr	x0, [x19, 16]
	cbz	x0, .L715
	ldr	w2, [x0]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0, 3520]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L715
	ldr	w1, [x19, 4]
	mov	x0, x23
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x19]
.L715:
	ldr	x0, [x19, 8]
	cbz	x0, .L716
	ldr	w2, [x0]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0, 3504]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L716
	ldr	w1, [x19, 4]
	adrp	x0, .LC88
	add	x0, x0, :lo12:.LC88
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x19]
.L716:
	add	x19, x19, 32
.L733:
	cmp	x19, x22
	bne	.L717
	b	.L706
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	stp	x29, x30, [sp, -112]!
	mov	w10, 0
	mov	w9, 4
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x5, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	and	w22, w1, 255
	stp	x25, x26, [sp, 64]
	add	x8, x5, 64
	stp	x27, x28, [sp, 80]
	and	w23, w0, 65535
	mov	w20, 0
	mov	w19, 0
	str	w23, [x5, 3436]
.L737:
	ldrh	w0, [x5, 36]
	cmp	w0, w10
	bhi	.L741
	cbz	w20, .L736
	ldr	x0, [x5, 3416]
	mov	w2, w20
	mov	w1, 0
	ubfiz	x20, x20, 5, 16
	mov	x24, 0
	add	x25, x21, :lo12:.LANCHOR0
	bl	FlashEraseBlocks
.L745:
	ldr	x0, [x25, 3416]
	add	x1, x0, x24
	ldr	w0, [x0, x24]
	cmn	w0, #1
	bne	.L744
	ldr	w0, [x1, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.L744:
	add	x24, x24, 32
	cmp	x24, x20
	bne	.L745
	cbz	w22, .L758
	add	x0, x21, :lo12:.LANCHOR0
	mov	w25, 1
	ldrh	w26, [x0, 108]
.L746:
	add	x28, x21, :lo12:.LANCHOR0
	mov	w24, 0
	add	x0, x28, 64
	str	x0, [x29, 104]
.L754:
	mov	w5, 0
	mov	w20, 0
.L747:
	ldrh	w0, [x28, 36]
	cmp	w0, w5
	bhi	.L750
	cbz	w20, .L736
	ldr	x0, [x28, 3416]
	mov	w1, w20
	mov	w3, 1
	mov	w2, w25
	mov	x27, 0
	bl	FlashProgPages
	ubfiz	x1, x20, 5, 16
.L753:
	ldr	x0, [x28, 3416]
	add	x3, x0, x27
	ldr	w0, [x0, x27]
	cbz	w0, .L752
	ldr	w0, [x3, 4]
	add	w19, w19, 1
	str	x1, [x29, 96]
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29, 96]
.L752:
	add	x27, x27, 32
	cmp	x1, x27
	bne	.L753
	add	w24, w24, 1
	cmp	w26, w24, uxth
	bhi	.L754
	add	x26, x21, :lo12:.LANCHOR0
	mov	x24, 0
.L756:
	cbz	w22, .L755
	ldr	x0, [x26, 3416]
	add	x1, x0, x24
	ldr	w0, [x0, x24]
	cbnz	w0, .L755
	ldr	w0, [x1, 4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L755:
	add	x24, x24, 32
	cmp	x24, x27
	bne	.L756
	cmp	w23, 63
	ccmp	w22, 0, 0, hi
	beq	.L736
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, w20
	mov	w1, w25
	ldr	x0, [x21, 3416]
	bl	FlashEraseBlocks
.L736:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L741:
	ldr	x1, [x5, 3416]
	ubfiz	x0, x10, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w23
	ldrb	w0, [x8, w10, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w22, .L738
	bl	IsBlkInVendorPart
	cbnz	w0, .L739
.L738:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L740
	ldr	x0, [x5, 3416]
	ubfiz	x2, x20, 5, 16
	lsl	w6, w6, 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	ldr	x1, [x5, 3416]
	ldr	x0, [x5, 3496]
	add	x1, x1, x2
	ldr	x2, [x5, 3536]
	str	x0, [x1, 8]
	ldrh	w0, [x5, 116]
	mul	w0, w0, w20
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w0, w0, w9
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L739:
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L737
.L740:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L739
.L758:
	mov	w25, 0
	mov	w26, 2
	b	.L746
.L750:
	ldr	x1, [x28, 3416]
	ubfiz	x0, x5, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w23
	ldr	x0, [x29, 104]
	ldrb	w0, [x0, w5, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w22, .L748
	bl	IsBlkInVendorPart
	cbnz	w0, .L749
.L748:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L749
	ldr	x0, [x28, 3416]
	ubfiz	x2, x20, 5, 16
	add	w6, w24, w6, lsl 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	ldr	x1, [x28, 3416]
	ldr	x0, [x28, 3488]
	add	x1, x1, x2
	mov	w2, 4
	str	x0, [x1, 8]
	ldrh	w0, [x28, 116]
	mul	w0, w0, w20
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w0, w0, w2
	ldr	x2, [x28, 3496]
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L749:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L747
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 65535
	cmp	w19, 11
	bls	.L782
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, 32
	mov	w1, 165
	lsl	w19, w19, 10
	ldr	x0, [x20, 3512]
	str	x0, [x29, 40]
	add	x0, x29, 64
	str	x0, [x29, 48]
	bl	ftl_memset
	ldr	x0, [x20, 3512]
	mov	w2, 8
	mov	w1, 90
	bl	ftl_memset
	str	w19, [x29, 36]
	mov	w2, 1
	add	x0, x29, 32
	mov	w1, w2
	bl	FlashEraseBlocks
	mov	w3, 1
	add	x0, x29, 32
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldr	w0, [x29, 32]
	cbnz	w0, .L783
	add	w0, w19, 1
	mov	w3, 1
	str	w0, [x29, 36]
	mov	w2, w3
	mov	w1, w3
	add	x0, x29, 32
	bl	FlashProgPages
	ldr	w0, [x29, 32]
	cmp	w0, 0
	csetm	w20, ne
.L781:
	mov	w2, 1
	mov	w1, 0
	add	x0, x29, 32
	str	w19, [x29, 36]
	bl	FlashEraseBlocks
.L779:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 128
	ret
.L783:
	mov	w20, -1
	b	.L781
.L782:
	mov	w20, 0
	b	.L779
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	stp	x29, x30, [sp, -80]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, 0
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR0
	add	x20, x25, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	add	x21, x20, 192
	ldr	x2, [x20, 3528]
	ldr	x0, [x20, 3464]
	str	x2, [x20, 3744]
	ldrh	w2, [x20, 114]
	str	x0, [x20, 3736]
	bl	ftl_memset
.L786:
	ldrh	w0, [x20, 58]
	add	x1, x20, 3728
	cmp	w19, w0
	blt	.L787
	ldr	x26, [x1, 16]
	add	x19, x20, 160
	mov	w2, 16
	mov	w1, 255
	mov	x0, x26
	bl	ftl_memset
	mov	w0, -3887
	adrp	x22, .LC89
	strh	w0, [x26]
	add	x25, x25, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC89
	mov	w21, 0
	ldr	w0, [x19, 8]
	mov	w23, 0
	str	w0, [x26, 4]
	adrp	x24, .LC90
	ldrh	w0, [x20, 160]
	strh	w0, [x26, 2]
	ldrh	w0, [x19, 4]
	strh	w0, [x26, 8]
	ldrh	w0, [x19, 6]
	strh	w0, [x26, 10]
	ldr	w0, [x20, 32]
	strh	w0, [x26, 12]
.L788:
	ldr	x0, [x25, 3464]
	str	x0, [x25, 3736]
	ldr	x0, [x25, 3528]
	str	x0, [x25, 3744]
	ldrh	w1, [x19]
	ldrh	w2, [x19, 2]
	ldrh	w3, [x19, 4]
	ldrh	w4, [x26, 10]
	orr	w0, w2, w1, lsl 10
	str	wzr, [x25, 3728]
	str	w0, [x25, 3732]
	mov	x0, x22
	bl	sftl_printk
	ldrh	w0, [x25, 108]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L789
	ldr	w0, [x19, 8]
	mov	w2, 1
	ldrh	w1, [x19]
	add	w0, w0, 1
	str	w0, [x19, 8]
	str	w0, [x26, 4]
	strh	w1, [x26, 8]
	ldrh	w0, [x19, 4]
	strh	w1, [x19, 4]
	ldr	x1, [x25, 3416]
	strh	w0, [x19]
	lsl	w0, w0, 10
	str	w0, [x25, 3732]
	strh	wzr, [x19, 2]
	str	w0, [x1, 4]
	mov	w1, w2
	ldr	x0, [x25, 3416]
	bl	FlashEraseBlocks
.L789:
	add	x20, x25, 3728
	mov	w3, 1
	mov	x0, x20
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	ldr	w0, [x25, 3728]
	cmn	w0, #1
	bne	.L790
	ldr	w1, [x25, 3732]
	add	w21, w21, 1
	add	x0, x24, :lo12:.LC90
	and	w21, w21, 65535
	bl	sftl_printk
	cmp	w21, 3
	bls	.L788
	ldr	w1, [x25, 3732]
	adrp	x0, .LC91
	mov	w2, w21
	add	x0, x0, :lo12:.LC91
	bl	sftl_printk
.L792:
	b	.L792
.L787:
	ldrh	w2, [x20, 3640]
	ldr	x0, [x1, 8]
	ldr	x1, [x21], 8
	mul	w3, w19, w2
	lsl	w2, w2, 2
	add	w19, w19, 1
	add	x0, x0, x3, sxtw 2
	bl	ftl_memcpy
	b	.L786
.L793:
	mov	w23, 1
	b	.L788
.L790:
	cbz	w23, .L793
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
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
.L796:
	ldrh	w1, [x22, 2400]
	ldrh	w0, [x22, 2416]
	add	w0, w0, w1
	ldrh	w1, [x22, 44]
	cmp	w0, w1
	ble	.L797
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2661
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L797:
	cmp	x20, x23
	bne	.L823
	ldrh	w0, [x19, 2416]
	ldr	w1, [x19, 3244]
	mul	w1, w0, w1
	lsr	w0, w0, 1
	add	w0, w0, 1
	add	w1, w0, w1, lsr 2
	ands	w1, w1, 65535
	beq	.L798
	sub	w1, w1, #1
	and	w1, w1, 65535
.L798:
	add	x0, x19, 2408
	bl	List_pop_index_node
	and	w25, w0, 65535
	ldrh	w0, [x19, 2416]
	cbnz	w0, .L799
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2670
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L799:
	ldrh	w0, [x19, 2416]
	sub	w0, w0, #1
	strh	w0, [x19, 2416]
	ldrh	w0, [x19, 44]
	cmp	w0, w25
	bls	.L796
	ldr	x0, [x19, 2376]
	ubfiz	x26, x25, 1, 16
	ldrh	w24, [x0, x26]
	cbnz	w24, .L796
	strh	w25, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20, 7]
	cbnz	w0, .L801
	ldr	x0, [x19, 2376]
	mov	w1, -1
	strh	w1, [x0, x26]
	mov	w0, w25
	bl	INSERT_DATA_LIST
	ldrh	w1, [x19, 2400]
	ldrh	w0, [x19, 2416]
	add	w0, w0, w1
	ldrh	w1, [x19, 44]
	cmp	w0, w1
	ble	.L796
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2684
	add	x1, x1, 336
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L796
.L823:
	mov	w1, 0
	b	.L798
.L801:
	ldrh	w1, [x19, 2400]
	ldrh	w0, [x19, 2416]
	add	w0, w0, w1
	ldrh	w1, [x19, 44]
	cmp	w0, w1
	ble	.L803
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2687
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L803:
	add	x27, x20, 16
	ldrh	w4, [x19, 36]
	mov	x3, x27
	mov	x0, 0
	mov	w5, 65535
.L804:
	cmp	w4, w0, uxth
	bhi	.L806
	cbnz	w24, .L807
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2698
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L807:
	ldrh	w0, [x19, 2672]
	cmp	w0, w25
	bne	.L808
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2700
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L808:
	ldrb	w0, [x20, 8]
	ldr	x1, [x19, 2384]
	cbnz	w0, .L809
	ldrh	w0, [x1, x26]
	cbz	w0, .L810
	ldrh	w2, [x19, 96]
	add	w0, w0, w2
.L831:
	strh	w0, [x1, x26]
	mov	w1, 0
	ldr	w0, [x19, 2628]
	add	w0, w0, 1
	str	w0, [x19, 2628]
	mov	w0, w25
	bl	ftl_set_blk_mode
.L812:
	ldr	x0, [x19, 2384]
	ldr	w1, [x19, 2644]
	ldrh	w0, [x0, x26]
	cmp	w0, w1
	bls	.L813
	str	w0, [x19, 2644]
.L813:
	ldr	w1, [x19, 2632]
	ubfiz	x3, x24, 5, 16
	ldrh	w0, [x19, 96]
	ldr	w2, [x19, 2628]
	madd	w0, w0, w2, w1
	ldrh	w1, [x19, 44]
	udiv	w0, w0, w1
	ldr	x1, [x19, 3560]
	str	w0, [x19, 2636]
	ldr	w0, [x1, 16]
	add	w0, w0, 1
	str	w0, [x1, 16]
	mov	x0, 0
.L814:
	cmp	x3, x0
	bne	.L815
	ldrb	w1, [x20, 8]
	mov	w2, w24
	ldr	x0, [x19, 3416]
	mov	x28, 0
	bl	FlashEraseBlocks
	mov	w1, 0
.L816:
	cmp	w24, w28, uxth
	bhi	.L818
	cmp	w1, 0
	ble	.L819
	mov	w0, w25
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L819:
	ldrb	w0, [x20, 7]
	cbnz	w0, .L820
	ldr	x0, [x19, 2376]
	mov	w1, -1
	strh	w1, [x0, x26]
	b	.L796
.L806:
	ldr	x1, [x19, 3416]
	add	x2, x1, x0, lsl 5
	stp	xzr, xzr, [x2, 8]
	ldrh	w2, [x3]
	cmp	w2, w5
	beq	.L805
	ubfiz	x6, x24, 5, 16
	add	w24, w24, 1
	add	x1, x1, x6
	and	w24, w24, 65535
	lsl	w2, w2, 10
	str	w2, [x1, 4]
.L805:
	add	x0, x0, 1
	add	x3, x3, 2
	b	.L804
.L810:
	mov	w0, 2
	b	.L831
.L809:
	ldrh	w0, [x1, x26]
	add	w0, w0, 1
	strh	w0, [x1, x26]
	ldr	w0, [x19, 2632]
	add	w0, w0, 1
	str	w0, [x19, 2632]
	mov	w0, w25
	bl	ftl_set_blk_mode.part.7
	b	.L812
.L815:
	ldr	x1, [x19, 3416]
	add	x1, x1, x0
	add	x0, x0, 32
	ldr	w2, [x1, 4]
	and	w2, w2, -1024
	str	w2, [x1, 4]
	b	.L814
.L818:
	ldr	x2, [x19, 3416]
	lsl	x0, x28, 5
	add	x3, x2, x0
	ldr	w2, [x2, x0]
	cmn	w2, #1
	bne	.L817
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
.L817:
	add	x28, x28, 1
	add	x27, x27, 2
	b	.L816
.L820:
	add	x21, x21, :lo12:.LANCHOR0
	strh	wzr, [x20, 2]
	strb	wzr, [x20, 6]
	ldrh	w1, [x21, 106]
	strh	w25, [x20]
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
	cbz	w0, .L821
	ldrb	w0, [x20, 7]
	cbnz	w0, .L822
.L821:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2753
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L822:
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
	cbz	w0, .L833
	add	x23, x19, 64
	add	x24, x19, 3328
	mov	w21, 0
.L834:
	ldrh	w0, [x19, 36]
	cmp	w0, w21
	bhi	.L840
	bl	FtlGcReFreshBadBlk
.L833:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L840:
	ldrb	w0, [x23, w21, sxtw]
	mov	w1, w22
	adrp	x26, .LC92
	add	x26, x26, :lo12:.LC92
	mov	w20, 0
	bl	V2P_block
	and	w25, w0, 65535
.L835:
	ldrh	w0, [x19, 3324]
	cmp	w0, w20
	bhi	.L839
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L834
.L839:
	ldrh	w0, [x24, w20, sxtw 1]
	add	w27, w20, 1
	cmp	w0, w25
	bne	.L836
	mov	w1, w25
	mov	x0, x26
	bl	sftl_printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w0, [x19, 3324]
	mov	w1, w27
.L837:
	cmp	w20, w0
	bcc	.L838
	sub	w0, w0, #1
	strh	w0, [x19, 3324]
.L836:
	and	w20, w27, 65535
	b	.L835
.L838:
	sub	w2, w1, #1
	ldrh	w3, [x24, w1, sxtw 1]
	add	w20, w20, 1
	add	w1, w1, 1
	and	w20, w20, 65535
	strh	w3, [x24, w2, sxtw 1]
	b	.L837
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
	cbnz	w1, .L846
	ldrh	w1, [x0, 2672]
	cmp	w1, w19
	bne	.L847
	mov	w1, -1
	strh	w1, [x0, 2672]
.L848:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x21, 2368
	bl	List_remove_node
	ldrh	w0, [x21, 2400]
	cbnz	w0, .L850
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2823
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L850:
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
	ble	.L854
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2826
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L854:
	mov	w0, 1
	b	.L845
.L847:
	ldrh	w1, [x0, 2424]
	cmp	w1, w19
	beq	.L853
	ldrh	w1, [x0, 2472]
	cmp	w1, w19
	beq	.L853
	ldrh	w0, [x0, 2520]
	cmp	w0, w19
	bne	.L848
.L853:
	mov	w0, 0
.L845:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L846:
	mov	w0, w19
	bl	List_update_data_list
	b	.L853
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 65535
	stp	x21, x22, [sp, 32]
	mov	w0, 65535
	adrp	x19, .LANCHOR0
	cmp	w20, w0
	beq	.L857
	add	x22, x19, :lo12:.LANCHOR0
	ubfiz	x21, x20, 1, 16
	ldr	x1, [x22, 2376]
	ldrh	w0, [x1, x21]
	cbnz	w0, .L858
	mov	w2, 0
	mov	w1, w20
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	bl	sftl_printk
	ldr	x0, [x22, 2376]
	ldrh	w0, [x0, x21]
	cbz	w0, .L859
.L864:
	mov	w21, 0
.L856:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L859:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2841
	add	x1, x1, 384
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L864
.L858:
	sub	w0, w0, #1
	strh	w0, [x1, x21]
.L857:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldrh	w0, [x19, 3714]
	cmp	w0, w1
	bne	.L861
	strh	w20, [x19, 3714]
	b	.L864
.L861:
	cmp	w20, w0
	beq	.L864
	bl	update_vpc_list
	cmp	w0, 0
	ldr	x1, [x19, 2360]
	cset	w21, ne
	ldr	x0, [x19, 2368]
	strh	w20, [x19, 3714]
	sub	x0, x0, x1
	mov	x1, -6148914691236517206
	asr	x0, x0, 1
	movk	x1, 0xaaab, lsl 0
	mul	x0, x0, x1
	ldr	x1, [x19, 2376]
	and	x2, x0, 65535
	ldrh	w1, [x1, x2, lsl 1]
	cbnz	w1, .L856
	cmp	w20, w0, uxth
	beq	.L856
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2857
	add	x1, x1, 384
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L856
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	stp	x29, x30, [sp, -96]!
	mov	w2, 61589
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w0, -1
	stp	x21, x22, [sp, 32]
	str	w0, [x29, 88]
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	str	x23, [sp, 48]
	mov	x20, x0
	ldr	x23, [x0, 3528]
	ldr	x1, [x0, 3464]
	stp	x1, x23, [x29, 72]
	mov	w1, -3
	str	w1, [x23, 8]
	mov	w1, -2
	str	w1, [x23, 12]
	ldrh	w1, [x19]
	strh	w1, [x23, 2]
	strh	wzr, [x23]
	ldr	x1, [x0, 3464]
	str	w2, [x1]
	mov	w2, 22136
	movk	w2, 0x1234, lsl 16
	ldr	x1, [x0, 3464]
	str	w2, [x1, 4]
	ldrh	w22, [x19, 4]
	and	w22, w22, 1
	add	w22, w22, 6
.L871:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L867
.L868:
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19]
	ldrh	w3, [x19, 4]
	ldr	x2, [x21, 2376]
	lsl	x0, x0, 1
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strb	wzr, [x19, 6]
	ldrh	w0, [x21, 106]
	strh	w0, [x19, 2]
	strh	wzr, [x19, 4]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 96
	ret
.L867:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29, 68]
	cmn	w0, #1
	beq	.L868
	ldr	w0, [x20, 2624]
	mov	w3, 0
	str	w0, [x23, 4]
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
	subs	w22, w22, #1
	bne	.L871
	b	.L868
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	mov	w28, -3872
	add	x0, x19, :lo12:.LANCHOR0
	add	x26, x0, 192
	add	x25, x0, 172
	mov	x19, x0
	add	x20, x0, 3728
.L880:
	ldrh	w0, [x19, 58]
	cmp	w24, w0
	bcc	.L886
	add	x20, x21, :lo12:.LANCHOR0
	mov	w19, 0
.L887:
	ldrh	w0, [x20, 118]
	cmp	w0, w19
	bhi	.L888
	add	x20, x20, 160
	ldrh	w19, [x20, 12]
	mov	w22, 65535
	sub	w19, w19, #1
	and	w19, w19, 65535
.L889:
	ldrh	w0, [x20, 12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L893
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L890
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L891
	mov	w0, w19
	bl	FtlBbmMapBadBlock
.L890:
	sub	w19, w19, #1
	and	w19, w19, 65535
	b	.L889
.L886:
	ldr	x0, [x19, 3464]
	mov	w2, 65535
	ldr	x27, [x19, 3528]
	ldrh	w1, [x25]
	stp	x0, x27, [x20, 8]
	cmp	w1, w2
	beq	.L881
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
.L882:
	mov	w0, w23
	add	w24, w24, 1
	bl	FtlBbmMapBadBlock
	add	x26, x26, 8
	add	x25, x25, 2
	b	.L880
.L881:
	mov	w1, w24
	bl	FlashGetBadBlockList
	ldr	x0, [x20, 8]
	ldr	x1, [x26]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x19, 102]
.L884:
	sub	w22, w22, #1
	and	w22, w22, 65535
.L883:
	ldrh	w0, [x19, 102]
	madd	w0, w24, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L884
	ldr	x0, [x19, 3528]
	mov	w2, 16
	strh	w22, [x25]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x19, 3464]
	mov	w2, 4096
	mov	w1, 0
	bl	ftl_memset
	strh	w28, [x27]
	str	wzr, [x27, 4]
	ldrh	w23, [x19, 102]
	ldrh	w0, [x25]
	strh	w0, [x27, 2]
	ldrh	w2, [x19, 3640]
	ldrh	w0, [x25]
	ldr	x1, [x26]
	lsl	w2, w2, 2
	madd	w23, w23, w24, w0
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
	bne	.L882
	mov	w0, w23
	bl	FtlBbmMapBadBlock
	b	.L883
.L888:
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	and	w19, w19, 65535
	b	.L887
.L891:
	ldrh	w0, [x20]
	cmp	w0, w22
	bne	.L892
	strh	w19, [x20]
	b	.L890
.L892:
	strh	w19, [x20, 4]
.L893:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, 2
	ldr	x1, [x21, 3416]
	ldrh	w0, [x21, 160]
	str	wzr, [x21, 168]
	strh	wzr, [x21, 162]
	lsl	w0, w0, 10
	str	w0, [x1, 4]
	ldr	x0, [x21, 3416]
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
	mov	x4, x0
	mov	w0, 42982
	mov	x3, 0
	movk	w0, 0x47c6, lsl 16
.L902:
	cmp	w1, w3
	bhi	.L903
	ret
.L903:
	lsr	w2, w0, 2
	ldrb	w5, [x4, x3]
	add	w2, w2, w0, lsl 5
	add	x3, x3, 1
	add	w2, w2, w5
	eor	w0, w0, w2
	b	.L902
	.size	js_hash, .-js_hash
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
	stp	x23, x24, [sp, 48]
	ldr	x20, [x0, 16]
	ldrh	w0, [x0]
	cmp	w0, w1
	bne	.L905
	ldrh	w0, [x19, 8]
	cbz	w0, .L906
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 641
	add	x1, x1, 408
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L906:
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
.L907:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L905:
	ubfiz	x0, x0, 1, 16
	adrp	x21, .LANCHOR0
	ldrh	w1, [x19, 2]
	ldr	x24, [x19, 40]
	ldrh	w23, [x20, x0]
	add	x20, x21, :lo12:.LANCHOR0
	ldr	w0, [x19, 48]
	ldr	x22, [x20, 3528]
	orr	w1, w1, w23, lsl 10
	str	w1, [x20, 3732]
	ldr	x1, [x20, 3464]
	str	x1, [x20, 3736]
	str	x22, [x20, 3744]
	mov	w1, 255
	str	w0, [x22, 4]
	mov	w0, -1291
	strh	w0, [x22, 8]
	ldrh	w0, [x19, 4]
	strh	w0, [x22]
	strh	w23, [x22, 2]
	ldrh	w2, [x20, 108]
	ldr	x0, [x20, 3464]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w1, 0
	mov	w0, 0
.L908:
	ldrh	w2, [x19, 6]
	cmp	w2, w0
	bhi	.L910
	add	x21, x21, :lo12:.LANCHOR0
	add	x6, x21, 3728
	ldrh	w1, [x21, 114]
	ldr	x0, [x21, 3736]
	bl	js_hash
	str	w0, [x22, 12]
	mov	w2, 1
	mov	w3, 0
	mov	w1, w2
	mov	x0, x6
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	x0, x19
	bl	ftl_map_blk_gc
	b	.L907
.L910:
	ubfiz	x2, x0, 2, 16
	ldr	w3, [x24, x2]
	cmp	w23, w3, lsr 10
	bne	.L909
	add	w1, w1, 1
	ldr	x4, [x20, 3464]
	and	w1, w1, 65535
	ubfiz	x3, x1, 3, 16
	str	w0, [x4, x3]
	ldr	w4, [x24, x2]
	ldr	x2, [x20, 3464]
	add	x2, x2, x3
	str	w4, [x2, 4]
.L909:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L908
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w21, 0
	stp	x23, x24, [sp, 48]
	adrp	x24, .LANCHOR0
	add	x22, x24, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	adrp	x23, .LANCHOR1
	stp	x27, x28, [sp, 80]
	mov	x19, x0
	mov	w26, w1
	mov	x27, x2
	mov	x25, x24
	add	x23, x23, :lo12:.LANCHOR1
	add	x20, x22, 3728
.L916:
	add	x0, x24, :lo12:.LANCHOR0
	ldr	w1, [x0, 2604]
	add	w1, w1, 1
	str	w1, [x0, 2604]
	ldrh	w0, [x0, 108]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L917
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L918
.L917:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L918:
	ldrh	w1, [x19]
	ldr	x0, [x19, 16]
	ldrh	w0, [x0, x1, lsl 1]
	cbnz	w0, .L919
	adrp	x0, .LC1
	mov	w2, 700
	add	x1, x23, 440
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L919:
	ldrh	w1, [x19]
	ldrh	w0, [x19, 10]
	cmp	w1, w0
	bcc	.L920
	adrp	x0, .LC1
	mov	w2, 701
	add	x1, x23, 440
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L920:
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19, 16]
	ldrh	w28, [x0, x1, lsl 1]
	mov	w1, 0
	ldrh	w0, [x19, 2]
	str	x27, [x20, 8]
	orr	w0, w0, w28, lsl 10
	str	w0, [x20, 4]
	ldr	x0, [x22, 3528]
	str	x0, [x20, 16]
	bl	ftl_memset
	ldr	x6, [x20, 16]
	ldr	w0, [x19, 48]
	str	w0, [x6, 4]
	strh	w26, [x6, 8]
	ldrh	w0, [x19, 4]
	strh	w0, [x6]
	strh	w28, [x6, 2]
	ldrh	w1, [x22, 114]
	ldr	x0, [x20, 8]
	bl	js_hash
	str	w0, [x6, 12]
	mov	w3, 1
	mov	x0, x20
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2]
	ldr	w1, [x20]
	cmn	w1, #1
	bne	.L921
	ldr	w1, [x20, 4]
	adrp	x0, .LC94
	add	x0, x0, :lo12:.LC94
	add	w21, w21, 1
	and	w21, w21, 65535
	bl	sftl_printk
	ldrh	w0, [x19, 2]
	cmp	w0, 2
	bhi	.L922
	ldrh	w0, [x22, 108]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L922:
	cmp	w21, 3
	bls	.L923
	add	x25, x25, :lo12:.LANCHOR0
	adrp	x0, .LC95
	mov	w2, w21
	add	x0, x0, :lo12:.LC95
	ldr	w1, [x25, 3732]
	bl	sftl_printk
.L924:
	b	.L924
.L923:
	ldr	w0, [x19, 52]
	cbz	w0, .L916
.L938:
	b	.L938
.L921:
	cmp	w0, 1
	beq	.L927
	cmp	w1, 256
	beq	.L927
	ldr	w0, [x19, 56]
	cbz	w0, .L928
.L927:
	str	wzr, [x19, 56]
	b	.L916
.L928:
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
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 65535
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	and	x19, x1, 65535
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldrh	w0, [x0, 140]
	cmp	w0, w22
	bcs	.L941
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 485
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L941:
	add	x20, x21, :lo12:.LANCHOR0
	ubfiz	x0, x22, 2, 16
	ldr	x1, [x20, 3616]
	ldr	w24, [x1, x0]
	cbnz	w24, .L942
	ldr	x0, [x20, 2568]
	lsl	x19, x19, 4
	ldrh	w2, [x20, 114]
	mov	w1, 255
	add	x0, x0, x19
	ldr	x0, [x0, 8]
	bl	ftl_memset
	ldr	x0, [x20, 2568]
	strh	w22, [x0, x19]
	ldr	x0, [x20, 2568]
	add	x19, x0, x19
	str	wzr, [x19, 4]
.L943:
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L942:
	ldr	x0, [x20, 2568]
	lsl	x19, x19, 4
	add	x23, x20, 3728
	mov	w2, 1
	add	x0, x0, x19
	mov	w1, w2
	str	w24, [x20, 3732]
	ldr	x0, [x0, 8]
	str	x0, [x20, 3736]
	ldr	x0, [x20, 3528]
	str	x0, [x20, 3744]
	mov	x0, x23
	bl	FlashReadPages
	ldr	x25, [x20, 3744]
	ldrh	w0, [x25, 8]
	cmp	w0, w22
	beq	.L944
	mov	w2, w24
	mov	w1, w22
	adrp	x0, .LC96
	add	x0, x0, :lo12:.LC96
	bl	sftl_printk
	ldr	x1, [x20, 3744]
	mov	w3, 4
	adrp	x0, .LC97
	mov	w2, w3
	add	x0, x0, :lo12:.LC97
	bl	rknand_print_hex
	ldrh	w3, [x20, 140]
	adrp	x0, .LC98
	ldr	x1, [x20, 3616]
	mov	w2, 4
	add	x0, x0, :lo12:.LC98
	bl	rknand_print_hex
.L945:
	ldrh	w0, [x25, 8]
	cmp	w0, w22
	beq	.L946
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 508
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L946:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21, 2568]
	add	x0, x0, x19
	str	wzr, [x0, 4]
	ldr	x0, [x21, 2568]
	strh	w22, [x0, x19]
	b	.L943
.L944:
	ldr	w0, [x20, 3728]
	cmp	w0, 256
	bne	.L945
	mov	w2, w24
	mov	w1, w22
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	bl	sftl_printk
	ldr	x0, [x20, 2568]
	mov	w1, w22
	add	x0, x0, x19
	ldr	x2, [x0, 8]
	add	x0, x20, 3648
	bl	FtlMapWritePage
	b	.L945
	.size	load_l2p_region, .-load_l2p_region
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
	sub	w1, w1, #5
	cmp	w2, w1
	blt	.L949
	ubfiz	x0, x0, 1, 16
	ldrh	w26, [x20, x0]
	cbz	w26, .L949
	ldr	w1, [x19, 52]
	cbnz	w1, .L949
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
	bcc	.L950
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L950:
	add	x27, x22, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR1
	add	x21, x27, 3728
	add	x23, x23, 472
	mov	w20, 0
.L951:
	ldrh	w0, [x19, 6]
	cmp	w0, w20
	bhi	.L958
	mov	w1, 1
	mov	w0, w26
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19, 52]
.L949:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x22, 108]
	cmp	w1, w0
	bcc	.L959
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L959:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L958:
	ubfiz	x0, x20, 2, 16
	add	x28, x25, x0
	ldr	w1, [x25, x0]
	cmp	w26, w1, lsr 10
	bne	.L952
	ldr	x1, [x27, 3472]
	mov	w2, 1
	ldr	x24, [x27, 3528]
	stp	x1, x24, [x21, 8]
	mov	w1, w2
	ldr	w0, [x25, x0]
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
	ldrh	w0, [x24, 8]
	cmp	w0, w20
	beq	.L953
	adrp	x0, .LC1
	mov	w2, 611
	mov	x1, x23
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L953:
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L954
.L956:
	str	wzr, [x28]
.L955:
	b	.L955
.L954:
	ldrh	w0, [x24, 8]
	cmp	w0, w20
	bne	.L956
	ldrh	w1, [x24]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L956
	ldr	x2, [x21, 8]
	mov	w1, w20
	mov	x0, x19
	bl	FtlMapWritePage
.L952:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L951
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
	add	x0, x19, 3648
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
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x25, x26, [sp, 64]
	mov	w25, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	mov	x22, x1
	mov	w23, w2
	ldrh	w24, [x0, 112]
	ldr	w0, [x0, 2584]
	cmp	w25, w0
	bcc	.L968
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 813
	add	x1, x1, 488
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L968:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0, 2584]
	cmp	w25, w1
	bcs	.L969
	add	w24, w24, 7
	ldrh	w1, [x0, 142]
	ldr	x2, [x0, 2568]
	mov	x0, 0
	lsr	w21, w25, w24
	and	w21, w21, 65535
.L970:
	and	x19, x0, 65535
	cmp	w19, w1
	bcc	.L975
	bl	select_l2p_ram_region
	and	x19, x0, 65535
	add	x2, x20, :lo12:.LANCHOR0
	ubfiz	x1, x19, 4, 16
	mov	w26, w0
	ldr	x2, [x2, 2568]
	add	x3, x2, x1
	ldrh	w2, [x2, x1]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L976
	ldr	w1, [x3, 4]
	tbz	w1, #31, .L976
	bl	flush_l2p_region
.L976:
	mov	w1, w26
	mov	w0, w21
	bl	load_l2p_region
	b	.L972
.L969:
	mov	w0, -1
	cbnz	w23, .L967
	str	w0, [x22]
.L967:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L975:
	add	x0, x0, 1
	add	x3, x2, x0, lsl 4
	ldrh	w3, [x3, -16]
	cmp	w3, w21
	bne	.L970
.L972:
	mov	x0, 1
	lsl	x0, x0, x24
	sub	w0, w0, #1
	and	w0, w0, w25
	and	x0, x0, 65535
	cbnz	w23, .L973
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x1, [x1, 2568]
	add	x1, x1, x19, lsl 4
	ldr	x1, [x1, 8]
	ldr	w0, [x1, x0, lsl 2]
	str	w0, [x22]
.L974:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20, 2568]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19, 4]
	cmn	w0, #1
	beq	.L978
	add	w0, w0, 1
	str	w0, [x19, 4]
.L978:
	mov	w0, 0
	b	.L967
.L973:
	add	x2, x20, :lo12:.LANCHOR0
	lsl	x1, x19, 4
	ldr	w4, [x22]
	ldr	x3, [x2, 2568]
	add	x3, x3, x1
	ldr	x3, [x3, 8]
	str	w4, [x3, x0, lsl 2]
	strh	w21, [x2, 2576]
	ldr	x0, [x2, 2568]
	add	x0, x0, x1
	ldr	w1, [x0, 4]
	orr	w1, w1, -2147483648
	str	w1, [x0, 4]
	b	.L974
	.size	log2phys, .-log2phys
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
	cbz	w2, .L982
	ldrb	w1, [x20, 2432]
	cbnz	w1, .L982
	ldrb	w1, [x20, 2431]
	ldrh	w3, [x20, 106]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L982
	ldrb	w0, [x20, 2434]
	cbnz	w0, .L981
	ldr	w22, [x20, 2584]
	add	x1, x29, 92
	ldrh	w24, [x20, 36]
	mov	w2, 0
	sub	w22, w22, #1
	mov	w0, w22
	bl	log2phys
	ldr	x21, [x20, 3528]
	ldr	w0, [x29, 92]
	ldr	x1, [x20, 3464]
	str	w0, [x29, 100]
	cmn	w0, #1
	stp	x1, x21, [x29, 104]
	str	w22, [x29, 120]
	str	wzr, [x21, 4]
	beq	.L984
	mov	w2, 0
	mov	w1, 1
	add	x0, x29, 96
	bl	FlashReadPages
.L985:
	add	x25, x19, :lo12:.LANCHOR0
	lsl	w24, w24, 2
	add	x20, x25, 2424
	mov	w0, -3947
	mov	w23, 0
	strh	w0, [x21]
.L986:
	cmp	w24, w23
	bne	.L990
.L987:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19, 2434]
.L981:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 128
	ret
.L984:
	ldrh	w2, [x20, 114]
	mov	w1, 255
	ldr	x0, [x20, 3464]
	bl	ftl_memset
	b	.L985
.L990:
	ldrh	w0, [x20, 4]
	cbz	w0, .L987
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
	b	.L986
.L982:
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x19, 2434]
	b	.L981
	.size	FtlWriteDump_data, .-FtlWriteDump_data
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
	cbnz	w1, .L1000
	ldr	x19, [x2, 2408]
	cbz	x19, .L1001
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
.L1002:
	cmp	w1, w3
	beq	.L1001
	cmp	w19, w0
	bne	.L1003
	add	x23, x22, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x23, 2408
	bl	List_remove_node
	ldrh	w0, [x23, 2416]
	cbnz	w0, .L1004
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1733
	add	x1, x1, 504
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1004:
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
.L1001:
	add	x1, x29, 76
	mov	w2, 1
	mov	w0, w21
	bl	log2phys
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L1003:
	umull	x19, w19, w4
	ldrh	w19, [x2, x19]
	cmp	w19, w5
	beq	.L1001
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L1002
.L1000:
	add	w1, w1, 1
	strh	w1, [x3, x20]
	b	.L1001
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	stp	x29, x30, [sp, -160]!
	mov	w1, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w0, [x0]
	cmp	w0, w1
	beq	.L1127
	ldrb	w0, [x20, 6]
	adrp	x19, .LANCHOR0
	str	w0, [x29, 140]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w26, [x20, 2]
	ldrh	w0, [x0, 106]
	cmp	w0, w26
	bne	.L1013
	strh	wzr, [x20, 4]
.L1135:
	strb	wzr, [x20, 6]
.L1127:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1013:
	ldrh	w0, [x20, 16]
	mov	w21, 0
.L1014:
	cmp	w0, w1
	beq	.L1015
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	mov	w23, w0
	cmn	w0, #1
	beq	.L1016
	add	x0, x19, :lo12:.LANCHOR0
	add	x28, x20, 16
	mov	x3, x28
	mov	w22, 0
	mov	w10, 65535
	mov	w6, 4
	ldrh	w1, [x0, 36]
	ldrh	w8, [x0, 114]
	add	x1, x1, 8
	ldrh	w9, [x0, 116]
	add	x1, x20, x1, lsl 1
.L1017:
	cmp	x1, x3
	bne	.L1021
	add	x24, x19, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w1, w22
	ldr	x0, [x24, 3440]
	bl	FlashReadPages
	ldr	w21, [x24, 2624]
	ldr	x4, [x24, 3440]
	and	w9, w23, 65535
	sub	w21, w21, #1
	mov	w7, 65535
	mov	x6, x4
	mov	w3, 0
.L1022:
	cmp	w22, w3
	bne	.L1027
	add	w22, w23, 1
	ldr	w0, [x4, 4]
	and	w22, w22, 65535
.L1129:
	lsr	w0, w0, 10
	bl	P2V_plane
	and	w27, w0, 65535
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 106]
	cmp	w0, w22
	bne	.L1029
	strh	w22, [x20, 2]
	strb	wzr, [x20, 6]
	strh	wzr, [x20, 4]
.L1029:
	ldr	w0, [x29, 140]
	cmp	w22, w26
	ccmp	w27, w0, 0, eq
	bne	.L1030
.L1136:
	mov	w2, w27
	mov	w1, w22
	mov	x0, x20
	bl	ftl_sb_update_avl_pages
	b	.L1127
.L1015:
	add	w21, w21, 1
	and	w21, w21, 65535
	add	x0, x20, x21, sxtw 1
	ldrh	w0, [x0, 16]
	b	.L1014
.L1016:
	cbz	w26, .L1018
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1800
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1018:
	ldr	w0, [x29, 140]
	cmp	w0, 0
	ccmp	w21, w0, 4, ne
	beq	.L1019
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1801
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1019:
	strh	wzr, [x20, 2]
	b	.L1135
.L1021:
	ldrh	w2, [x3]
	cmp	w2, w10
	beq	.L1020
	ldr	x5, [x0, 3440]
	ubfiz	x7, x22, 5, 16
	orr	w2, w23, w2, lsl 10
	add	x5, x5, x7
	str	w2, [x5, 4]
	mul	w2, w22, w8
	ldr	x4, [x0, 3440]
	ldr	x5, [x0, 3264]
	sdiv	w2, w2, w6
	add	x4, x4, x7
	add	x2, x5, x2, sxtw 2
	str	x2, [x4, 8]
	mul	w2, w22, w9
	ldr	x5, [x0, 3272]
	add	w22, w22, 1
	and	w22, w22, 65535
	sdiv	w2, w2, w6
	add	x2, x5, x2, sxtw 2
	str	x2, [x4, 16]
.L1020:
	add	x3, x3, 2
	b	.L1017
.L1027:
	ldr	w0, [x6]
	cbnz	w0, .L1023
	ldr	x8, [x6, 16]
	ldr	w5, [x8, 4]
	cmn	w5, #1
	beq	.L1024
	ldr	w1, [x24, 2624]
	mov	w0, w5
	bl	ftl_cmp_data_ver
	cbz	w0, .L1024
	add	w5, w5, 1
	str	w5, [x24, 2624]
.L1024:
	ldr	w0, [x8]
	cmn	w0, #1
	bne	.L1026
	ubfiz	x3, x3, 5, 16
	and	w22, w23, 65535
	add	x3, x4, x3
	ldr	w0, [x3, 4]
	b	.L1129
.L1023:
	mov	w7, w9
.L1026:
	add	w3, w3, 1
	add	x6, x6, 32
	and	w3, w3, 65535
	b	.L1022
.L1030:
	mov	w0, 65535
	cmp	w7, w0
	bne	.L1031
	ldrb	w0, [x20, 8]
	cbnz	w0, .L1032
.L1031:
	add	x0, x19, :lo12:.LANCHOR0
	and	w3, w23, 65535
	ldr	w1, [x0, 3724]
	cmn	w1, #1
	bne	.L1033
	str	w21, [x0, 3724]
.L1033:
	add	w0, w26, 7
	cmp	w0, w23, uxth
	bge	.L1083
	sub	w24, w3, #7
	and	w24, w24, 65535
.L1034:
	add	x25, x19, :lo12:.LANCHOR0
	mov	w4, 65535
.L1035:
	cmp	w24, w3
	bhi	.L1042
	ldrh	w0, [x25, 36]
	mov	x1, x28
	mov	w23, 0
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
	b	.L1043
.L1083:
	mov	w24, w26
	b	.L1034
.L1037:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1036
	ldr	x5, [x25, 3440]
	ubfiz	x6, x23, 5, 16
	add	w23, w23, 1
	orr	w2, w24, w2, lsl 10
	add	x5, x5, x6
	and	w23, w23, 65535
	str	w2, [x5, 4]
.L1036:
	add	x1, x1, 2
.L1043:
	cmp	x0, x1
	bne	.L1037
	ldr	x0, [x25, 3440]
	mov	w1, w23
	str	w4, [x29, 120]
	mov	w2, 0
	str	w3, [x29, 128]
	ubfiz	x23, x23, 5, 16
	add	x23, x23, 16
	bl	FlashReadPages
	ldr	x1, [x25, 3440]
	ldr	w4, [x29, 120]
	ldr	w3, [x29, 128]
	add	x0, x1, 16
	add	x23, x1, x23
.L1038:
	cmp	x23, x0
	bne	.L1041
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L1035
.L1041:
	ldr	w1, [x0, -16]
	cbnz	w1, .L1032
	ldr	x1, [x0]
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1040
	ldr	w1, [x1, 4]
	cmn	w1, #1
	beq	.L1040
	str	w1, [x25, 3724]
.L1040:
	add	x0, x0, 32
	b	.L1038
.L1042:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	str	w1, [x0, 3724]
.L1032:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	mov	w25, w26
	strh	w0, [x19, 3760]
	add	x0, x19, 3768
	str	x0, [x29, 104]
.L1044:
	ldrh	w0, [x19, 36]
	mov	x1, x28
	mov	w23, 0
	mov	w4, 65535
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
.L1045:
	cmp	x0, x1
	bne	.L1047
	ldr	x0, [x19, 3440]
	mov	w1, w23
	mov	w2, 0
	bl	FlashReadPages
	ubfiz	x0, x23, 5, 16
	str	x0, [x29, 112]
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 520
	mov	x23, 0
	str	x0, [x29, 120]
.L1048:
	ldr	x0, [x29, 112]
	cmp	x0, x23
	bne	.L1077
	add	w25, w25, 1
	ldrh	w0, [x19, 106]
	and	w25, w25, 65535
	cmp	w0, w25
	bne	.L1044
	ldrh	w1, [x19, 36]
	mov	w0, 0
	strh	w25, [x20, 2]
	mov	w2, 65535
	strh	wzr, [x20, 4]
.L1078:
	cmp	w0, w1
	beq	.L1127
	ldrh	w3, [x28], 2
	cmp	w3, w2
	beq	.L1079
	strb	w0, [x20, 6]
	b	.L1127
.L1047:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1046
	ldr	x3, [x19, 3440]
	ubfiz	x5, x23, 5, 16
	add	w23, w23, 1
	orr	w2, w25, w2, lsl 10
	add	x3, x3, x5
	and	w23, w23, 65535
	str	w2, [x3, 4]
.L1046:
	add	x1, x1, 2
	b	.L1045
.L1077:
	ldr	x4, [x19, 3440]
	add	x4, x4, x23
	ldr	w5, [x4, 4]
	str	w5, [x29, 156]
	lsr	w0, w5, 10
	bl	P2V_plane
	and	w0, w0, 65535
	cmp	w25, w26
	bcc	.L1049
	ldr	w1, [x29, 140]
	ccmp	w1, w0, 0, eq
	bhi	.L1049
	cmp	w25, w22
	ccmp	w27, w0, 0, eq
	beq	.L1050
	ldr	w0, [x4]
	cmn	w0, #1
	beq	.L1051
	ldr	x3, [x4, 16]
	mov	w0, 61589
	ldrh	w1, [x3]
	cmp	w1, w0
	beq	.L1052
	ldrh	w0, [x20]
.L1133:
	bl	decrement_vpc_count
	b	.L1049
.L1052:
	ldr	w21, [x3, 4]
	cmn	w21, #1
	beq	.L1053
	ldr	w1, [x19, 2624]
	mov	w0, w21
	str	x3, [x29, 128]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29, 128]
	cbz	w0, .L1053
	add	w0, w21, 1
	str	w0, [x19, 2624]
.L1053:
	ldrh	w1, [x3]
	mov	w0, 61589
	cmp	w1, w0
	beq	.L1054
	ldr	x1, [x29, 120]
	mov	w2, 1952
	str	x3, [x29, 128]
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	ldr	x3, [x29, 128]
.L1054:
	ldp	w24, w0, [x3, 8]
	add	x1, x29, 152
	str	w0, [x29, 148]
	mov	w2, 0
	mov	w0, w24
	bl	log2phys
	ldr	w1, [x19, 3724]
	ldr	w3, [x29, 148]
	cmn	w1, #1
	beq	.L1055
	mov	w0, w21
	bl	ftl_cmp_data_ver
	cbz	w0, .L1055
	cmn	w3, #1
	beq	.L1056
	ldr	x0, [x19, 3440]
	mov	w2, 0
	mov	w1, 1
	add	x0, x0, x23
	ldr	x4, [x0, 16]
	str	w3, [x0, 4]
	str	x4, [x29, 128]
	ldr	x0, [x19, 3440]
	add	x0, x0, x23
	bl	FlashReadPages
	ldr	x0, [x19, 3440]
	ldr	x4, [x29, 128]
	add	x3, x0, x23
	ldr	w0, [x0, x23]
	cmn	w0, #1
	bne	.L1057
.L1058:
	mov	w0, -1
	str	w0, [x29, 148]
.L1065:
	ldr	w4, [x29, 148]
	cmn	w4, #1
	beq	.L1049
.L1082:
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	and	w24, w0, 65535
	ldrh	w0, [x19, 44]
	mov	w3, w24
	cmp	w0, w24
	bhi	.L1073
	ldr	x1, [x29, 120]
	mov	w2, 2065
	adrp	x0, .LC1
	str	w24, [x29, 128]
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	ldr	w3, [x29, 128]
.L1073:
	ldr	x1, [x19, 2376]
	ubfiz	x0, x24, 1, 16
	ldrh	w0, [x1, x0]
	cbz	w0, .L1074
	mov	w0, w3
	b	.L1133
.L1056:
	ldp	w1, w0, [x29, 152]
	cmp	w1, w0
	bne	.L1049
	mov	w2, 1
	add	x1, x29, 148
	mov	w0, w24
	bl	log2phys
.L1049:
	add	x23, x23, 32
	b	.L1048
.L1057:
	ldr	w0, [x4, 8]
	cmp	w24, w0
	bne	.L1058
	ldr	w0, [x4, 4]
	str	w0, [x29, 128]
	str	x4, [x29, 96]
	uxtw	x1, w0
	ldr	w0, [x19, 3724]
	bl	ftl_cmp_data_ver
	cbz	w0, .L1058
	ldp	w0, w1, [x29, 152]
	ldr	x4, [x29, 96]
	cmp	w0, w1
	ldr	w1, [x29, 148]
	bne	.L1060
.L1130:
	mov	w0, w24
	bl	FtlReUsePrevPpa
	b	.L1058
.L1060:
	cmp	w0, w1
	beq	.L1058
	cmn	w0, #1
	beq	.L1061
	ldr	x4, [x3, 16]
	mov	w2, 0
	str	w0, [x3, 4]
	mov	w1, 1
	str	x4, [x29, 96]
	ldr	x0, [x19, 3440]
	add	x0, x0, x23
	bl	FlashReadPages
	ldr	x4, [x29, 96]
.L1062:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3440]
	ldr	w1, [x1, x23]
	cmn	w1, #1
	beq	.L1063
	ldr	w3, [x4, 4]
	ldr	w0, [x0, 3724]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1063
	ldr	w0, [x29, 128]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1058
.L1063:
	ldr	w1, [x29, 148]
	b	.L1130
.L1061:
	str	w0, [x3]
	b	.L1062
.L1055:
	ldp	w1, w0, [x29, 152]
	cmp	w1, w0
	beq	.L1065
	ldr	w0, [x19, 52]
	cmp	w0, w3, lsr 10
	bhi	.L1067
	cmn	w3, #1
.L1132:
	bne	.L1049
	mov	w2, 1
	add	x1, x29, 156
	mov	w0, w24
	bl	log2phys
	ldr	w4, [x29, 152]
	cmn	w4, #1
	beq	.L1065
	ldr	w0, [x29, 148]
	cmp	w4, w0
	beq	.L1082
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19, 2424]
	and	w0, w0, 65535
	cmp	w1, w0
	beq	.L1072
	ldrh	w1, [x19, 2472]
	cmp	w1, w0
	beq	.L1072
	ldrh	w1, [x19, 2520]
	cmp	w1, w0
	bne	.L1065
.L1072:
	ldr	x0, [x19, 3440]
	mov	w2, 0
	mov	w1, 1
	str	w4, [x0, 4]
	ldr	x3, [x0, 16]
	ldr	x0, [x19, 3440]
	str	x3, [x29, 128]
	bl	FlashReadPages
	ldr	x0, [x19, 3440]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1065
	ldr	x3, [x29, 128]
	mov	w0, w21
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
	cbnz	w0, .L1065
	mov	w2, 1
	add	x1, x29, 152
	mov	w0, w24
	bl	log2phys
	b	.L1065
.L1067:
	ldr	x0, [x19, 3440]
	mov	w2, 0
	mov	w1, 1
	str	w3, [x0, 4]
	ldr	x4, [x0, 16]
	ldr	x0, [x19, 3440]
	str	x4, [x29, 128]
	bl	FlashReadPages
	ldr	x4, [x29, 128]
	ldr	w0, [x4, 8]
	cmp	w24, w0
	bne	.L1049
	ldrh	w1, [x4]
	mov	w0, 61589
	cmp	w1, w0
	b	.L1132
.L1074:
	mov	w1, w24
	adrp	x0, .LC100
	add	x0, x0, :lo12:.LC100
	bl	sftl_printk
	b	.L1049
.L1051:
	ldr	w0, [x19, 3764]
	cmp	w0, 31
	bhi	.L1075
	ldr	x1, [x29, 104]
	str	w5, [x1, w0, uxtw 2]
	add	w0, w0, 1
	str	w0, [x19, 3764]
.L1075:
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	ldr	w0, [x19, 3724]
	cmn	w0, #1
	bne	.L1076
.L1134:
	str	w21, [x19, 3724]
	b	.L1049
.L1076:
	cmp	w21, w0
	bcs	.L1049
	b	.L1134
.L1079:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1078
.L1050:
	strb	w27, [x20, 6]
	strh	w22, [x20, 2]
	b	.L1136
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	ftl_check_vpc
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	stp	x29, x30, [sp, -128]!
	adrp	x0, .LC101
	add	x0, x0, :lo12:.LC101
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x24, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	w23, 0
	adrp	x21, .LANCHOR1
	add	x1, x21, :lo12:.LANCHOR1
	add	x1, x1, 544
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	adrp	x22, check_vpc_table
	bl	sftl_printk
	add	x19, x22, :lo12:check_vpc_table
	mov	w2, 8192
	mov	w1, 0
	mov	x0, x19
	bl	ftl_memset
.L1138:
	ldr	w0, [x24, 2584]
	cmp	w23, w0
	bcc	.L1140
	adrp	x25, .LC102
	add	x23, x20, :lo12:.LANCHOR0
	add	x26, x22, :lo12:check_vpc_table
	add	x25, x25, :lo12:.LC102
	mov	w24, 0
	mov	w19, 0
	mov	w4, 65535
.L1141:
	ldrh	w0, [x23, 44]
	cmp	w0, w19
	bhi	.L1143
	ldr	x19, [x23, 2408]
	cbz	x19, .L1144
	ldr	x0, [x23, 2360]
	adrp	x25, .LC103
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:check_vpc_table
	sub	x19, x19, x0
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	movk	x0, 0xaaab, lsl 0
	add	x25, x25, :lo12:.LC103
	ldrh	w26, [x23, 2416]
	mov	w27, 6
	mov	w23, 0
	mul	x19, x19, x0
	and	w19, w19, 65535
.L1145:
	cmp	w23, w26
	bne	.L1147
.L1144:
	cbz	w24, .L1137
	add	x1, x21, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2389
	add	x1, x1, 544
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1137:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1140:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w23
	bl	log2phys
	ldr	w0, [x29, 124]
	cmn	w0, #1
	beq	.L1139
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	and	x0, x0, 65535
	ldrh	w1, [x19, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x19, x0, lsl 1]
.L1139:
	add	w23, w23, 1
	b	.L1138
.L1143:
	ldr	x0, [x23, 2376]
	ubfiz	x28, x19, 1, 16
	sxtw	x27, w19
	ldrh	w2, [x0, x28]
	ldrh	w3, [x26, x27, lsl 1]
	cmp	w2, w3
	beq	.L1142
	mov	x0, x25
	str	w4, [x29, 108]
	mov	w1, w19
	bl	sftl_printk
	ldr	x0, [x23, 2376]
	ldr	w4, [x29, 108]
	ldrh	w0, [x0, x28]
	cmp	w0, w4
	beq	.L1142
	ldrh	w1, [x26, x27, lsl 1]
	cmp	w1, w0
	csinc	w24, w24, wzr, ls
.L1142:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1141
.L1147:
	ldr	x1, [x20, 2376]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L1146
	ldrh	w3, [x22, w19, sxtw 1]
	mov	w24, 1
	mov	w1, w19
	mov	x0, x25
	bl	sftl_printk
.L1146:
	ldr	x0, [x20, 2360]
	umull	x19, w19, w27
	ldrh	w19, [x0, x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L1144
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L1145
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	sub	sp, sp, #96
	adrp	x0, .LC104
	mov	w1, 0
	add	x0, x0, :lo12:.LC104
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x21, x22, [sp, 48]
	adrp	x22, .LANCHOR0
	add	x21, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 32]
	str	x23, [sp, 64]
	mov	w20, 0
	add	x19, x21, 3728
	bl	sftl_printk
.L1163:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0, 2584]
	cmp	w20, w0
	bcc	.L1169
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x29, x30, [sp, 16]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	ret
.L1169:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w20
	bl	log2phys
	tst	x20, 2047
	bne	.L1164
	ldr	w2, [x29, 76]
	adrp	x0, .LC105
	mov	w1, w20
	add	x0, x0, :lo12:.LC105
	bl	sftl_printk
.L1164:
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1166
	ldr	x23, [x21, 3528]
	mov	w2, 0
	stp	wzr, w0, [x19]
	mov	w1, 1
	ldr	x0, [x21, 3464]
	str	x0, [x19, 8]
	mov	x0, x19
	str	x23, [x19, 16]
	str	w20, [x19, 24]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmp	w0, 256
	ccmn	w0, #1, 4, ne
	beq	.L1167
	ldr	w0, [x23, 8]
	cmp	w20, w0
	beq	.L1166
.L1167:
	ldp	x1, x0, [x19, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC106
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC106
	ldr	w2, [x19, 4]
	mov	w1, w20
	bl	sftl_printk
.L1166:
	add	w20, w20, 1
	b	.L1163
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR2
	add	x0, x23, :lo12:.LANCHOR2
	stp	x25, x26, [sp, 64]
	stp	x21, x22, [sp, 32]
	mov	w25, w1
	stp	x27, x28, [sp, 80]
	ldrh	w21, [x0, 4]
	mov	w0, 65535
	cmp	w21, w0
	beq	.L1195
	cbnz	w21, .L1172
.L1173:
	bl	FtlGcPageVarInit
	b	.L1174
.L1195:
	mov	w21, 0
.L1172:
	adrp	x0, .LANCHOR0+106
	ldrh	w0, [x0, #:lo12:.LANCHOR0+106]
	cmp	w0, w25
	beq	.L1173
.L1174:
	adrp	x26, .LANCHOR0
	add	x20, x26, :lo12:.LANCHOR0
	mov	w24, 0
.L1175:
	ldrh	w1, [x19]
	mov	w0, 65535
	strb	wzr, [x19, 8]
	cmp	w1, w0
	beq	.L1176
.L1192:
	add	x0, x26, :lo12:.LANCHOR0
	add	x2, x19, 16
	mov	w22, 0
	mov	w9, 65535
	mov	w5, 4
	ldrh	w7, [x0, 114]
	ldrh	w8, [x0, 116]
	ldrh	w0, [x0, 36]
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
.L1177:
	cmp	x0, x2
	bne	.L1179
	ldr	x0, [x20, 3440]
	mov	w1, w22
	mov	w2, 0
	ubfiz	x22, x22, 5, 16
	mov	x27, 0
	bl	FlashReadPages
.L1180:
	cmp	x22, x27
	bne	.L1190
	add	w21, w21, 1
	add	w24, w24, 1
	and	w21, w21, 65535
	cmp	w25, w24
	bls	.L1191
.L1193:
	ldrh	w0, [x20, 106]
	cmp	w0, w21
	bhi	.L1192
.L1176:
	add	x23, x23, :lo12:.LANCHOR2
	strh	w21, [x19, 2]
	strb	wzr, [x19, 6]
	mov	w0, -1
	mov	w2, 0
	mov	w1, w21
	strh	w0, [x23, 4]
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
	b	.L1194
.L1179:
	ldrh	w1, [x2]
	cmp	w1, w9
	beq	.L1178
	ldr	x4, [x20, 3440]
	ubfiz	x6, x22, 5, 16
	orr	w1, w21, w1, lsl 10
	add	x4, x4, x6
	str	w1, [x4, 4]
	mul	w1, w22, w7
	ldr	x3, [x20, 3440]
	ldr	x4, [x20, 3264]
	sdiv	w1, w1, w5
	add	x3, x3, x6
	add	x1, x4, x1, sxtw 2
	str	x1, [x3, 8]
	mul	w1, w22, w8
	ldr	x4, [x20, 3272]
	add	w22, w22, 1
	and	w22, w22, 65535
	sdiv	w1, w1, w5
	add	x1, x4, x1, sxtw 2
	str	x1, [x3, 16]
.L1178:
	add	x2, x2, 2
	b	.L1177
.L1190:
	ldr	x1, [x20, 3440]
	add	x0, x1, x27
	ldr	w1, [x1, x27]
	ldr	w2, [x0, 4]
	str	w2, [x29, 108]
	cbnz	w1, .L1181
	ldr	x28, [x0, 16]
	mov	w1, 65535
	ldrh	w0, [x28]
	cmp	w0, w1
	bne	.L1182
.L1181:
	ldrh	w1, [x19]
	mov	w21, 0
	ldr	x0, [x20, 2376]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	bl	FtlGcPageVarInit
	b	.L1175
.L1182:
	ldr	w1, [x20, 2584]
	ldr	w0, [x28, 8]
	cmp	w0, w1
	bhi	.L1181
	add	x1, x29, 124
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x28, 12]
	ldr	w1, [x29, 124]
	cmp	w0, w1
	beq	.L1185
.L1186:
	ldp	w2, w0, [x28, 8]
	add	x27, x27, 32
	ldr	w1, [x29, 108]
	bl	FtlGcUpdatePage
	b	.L1180
.L1185:
	cmn	w0, #1
	beq	.L1186
	str	w0, [x29, 132]
	mov	w2, 0
	ldr	x0, [x20, 3496]
	mov	w1, 1
	str	x0, [x29, 136]
	ldr	x0, [x20, 3536]
	str	x0, [x29, 144]
	add	x0, x29, 128
	bl	FlashReadPages
	ldrh	w1, [x20, 62]
	mov	x0, 0
	ldr	x2, [x20, 3440]
	ldr	x5, [x29, 136]
	ubfiz	x1, x1, 9, 16
	add	x2, x2, x27
.L1187:
	cmp	x0, x1
	beq	.L1186
	ldr	x6, [x2, 8]
	ldr	w7, [x6, x0]
	add	x0, x0, 4
	add	x6, x5, x0
	ldr	w6, [x6, -4]
	cmp	w7, w6
	beq	.L1187
	ldrh	w1, [x19]
	adrp	x0, .LC107
	ldr	w2, [x29, 132]
	add	x0, x0, :lo12:.LC107
	bl	sftl_printk
	b	.L1181
.L1191:
	add	x1, x23, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x1, 4]
	cmp	w0, w2
	beq	.L1193
	add	w0, w0, w24
	strh	w0, [x1, 4]
	ldrh	w0, [x20, 106]
	cmp	w0, w21
	bls	.L1193
.L1194:
	ldp	x19, x20, [sp, 16]
	mov	w0, -1
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	adrp	x1, .LANCHOR0
	add	x0, x1, :lo12:.LANCHOR0
	add	x2, x0, 2720
	ldr	w3, [x2, 80]
	cbz	w3, .L1206
	ldr	w1, [x0, 2584]
	ldr	w3, [x2, 84]
	cmp	w3, w1
	bcs	.L1207
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w20, 2048
	str	x21, [sp, 32]
.L1212:
	ldr	w1, [x19, 2584]
	ldr	w0, [x19, 2804]
	cmp	w0, w1
	bcs	.L1209
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x19, 2804]
	ldr	w1, [x29, 60]
	add	w0, w0, 1
	str	w0, [x19, 2804]
	cmn	w1, #1
	beq	.L1210
	str	w0, [x29, 88]
	mov	w2, 0
	ldr	x0, [x19, 3512]
	str	x0, [x29, 72]
	add	x0, x29, 96
	str	x0, [x29, 80]
	add	x0, x29, 160
	str	w1, [x29, 68]
	mov	w1, 1
	str	wzr, [x0, -96]!
	bl	FlashReadPages
	ldr	w0, [x29, 64]
	cmp	w0, 256
	bne	.L1209
	ldr	w0, [x29, 60]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1209:
	ldp	x19, x20, [sp, 16]
	mov	w0, -1
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 160
	ret
.L1210:
	subs	w20, w20, #1
	bne	.L1212
	b	.L1209
.L1207:
	ldr	w0, [x0, 2588]
	stp	w0, wzr, [x2, 76]
	str	wzr, [x2, 84]
.L1216:
	mov	w0, 0
	ret
.L1206:
	ldr	w3, [x0, 2588]
	ldr	w5, [x2, 76]
	add	w2, w3, 1048576
	cmp	w5, w2
	bhi	.L1215
	ldr	w2, [x0, 2644]
	lsr	w4, w2, 10
	mov	w2, 33554432
	asr	w2, w2, w4
	add	w2, w2, w5
	cmp	w3, w2
	bhi	.L1215
	ldrb	w0, [x0, 2340]
	cbnz	w0, .L1216
.L1215:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w1, 1
	str	w3, [x0, 2796]
	str	w1, [x0, 2800]
	str	wzr, [x0, 2804]
	b	.L1216
	.size	FtlReadRefresh, .-FtlReadRefresh
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
.L1223:
	ldrh	w0, [x20, 142]
	cmp	w0, w19
	bhi	.L1225
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1225:
	ldr	x1, [x20, 2568]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L1224
	mov	w0, w19
	bl	flush_l2p_region
.L1224:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1223
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	stp	x29, x30, [sp, -208]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x27, x28, [sp, 80]
	mov	w22, w1
	mov	w28, w0
	add	w1, w0, w1
	stp	x23, x24, [sp, 48]
	ldrh	w0, [x19, 100]
	stp	x25, x26, [sp, 64]
	cmp	w1, w0
	bhi	.L1235
	ldrh	w23, [x19, 112]
	mov	x25, x2
	add	x26, x29, 144
	add	x27, x19, 3896
	mov	w24, 0
	lsr	w23, w28, w23
.L1229:
	cbnz	w22, .L1234
.L1227:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1234:
	ldrh	w1, [x19, 62]
	ldr	x0, [x19, 3608]
	udiv	w21, w28, w1
	ldr	w2, [x0, w23, uxtw 2]
	and	w0, w22, 65535
	msub	w21, w21, w1, w28
	sub	w20, w1, w21
	and	w20, w20, 65535
	cmp	w22, w20
	csel	w20, w0, w20, cc
	cbz	w2, .L1231
	cmp	w20, w1
	beq	.L1231
	ldr	x0, [x19, 3480]
	str	w2, [x29, 116]
	mov	w2, 1
	stp	x0, x26, [x29, 120]
	mov	w1, w2
	add	x0, x29, 112
	bl	FlashReadPages
.L1232:
	lsl	w3, w20, 9
	ldr	x0, [x19, 3480]
	lsl	w21, w21, 9
	mov	w2, w3
	asr	w21, w21, 2
	mov	x1, x25
	str	w3, [x29, 108]
	add	x0, x0, x21, sxtw 2
	bl	ftl_memcpy
	sub	w22, w22, w20
	ldr	x2, [x19, 3480]
	mov	w1, w23
	mov	x0, x27
	add	w28, w28, w20
	add	w23, w23, 1
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	w3, [x29, 108]
	csinv	w24, w24, wzr, ne
	add	x25, x25, x3, sxtw
	b	.L1229
.L1231:
	ldrh	w2, [x19, 114]
	mov	w1, 0
	ldr	x0, [x19, 3480]
	bl	ftl_memset
	b	.L1232
.L1235:
	mov	w24, -1
	b	.L1227
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
	bne	.L1243
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 2720
	mov	w1, 72
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
.L1243:
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x1, .LANCHOR0
	add	x3, x1, :lo12:.LANCHOR0
	ldrh	w2, [x3, 3960]
	cmp	w2, 31
	bhi	.L1250
	add	w2, w2, 1
	strh	w2, [x3, 3960]
	mov	w2, 1
.L1247:
	cbnz	w0, .L1248
	add	x0, x1, :lo12:.LANCHOR0
	ldr	x0, [x0, 3560]
	ldr	w3, [x0, 20]
	ldr	w0, [x0, 16]
	add	w2, w2, w3
	cmp	w0, w2
	bcc	.L1252
.L1248:
	add	x0, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x0, 3560]
	ldr	w2, [x1, 16]
	str	w2, [x1, 20]
	mov	w2, 17221
	movk	w2, 0x4254, lsl 16
	str	w2, [x1]
	ldr	x2, [x0, 3560]
	ldrh	w1, [x0, 3544]
	lsl	w3, w1, 9
	str	wzr, [x2, 4]
	str	w3, [x2, 12]
	ldr	w3, [x2, 8]
	add	w3, w3, 1
	str	w3, [x2, 8]
	ldr	x2, [x0, 3560]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1250:
	mov	w2, 32
	b	.L1247
.L1252:
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
	stp	x29, x30, [sp, -208]!
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w22, w1
	stp	x23, x24, [sp, 48]
	add	w1, w0, w1
	stp	x19, x20, [sp, 16]
	mov	w23, w0
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w0, [x3, 100]
	cmp	w1, w0
	bhi	.L1265
	ldrh	w21, [x3, 112]
	adrp	x26, .LC108
	mov	x25, x2
	mov	x28, x3
	add	x26, x26, :lo12:.LC108
	mov	w24, 0
	lsr	w21, w23, w21
.L1258:
	cbnz	w22, .L1264
.L1256:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1264:
	ldrh	w19, [x28, 62]
	ldr	x0, [x28, 3608]
	udiv	w20, w23, w19
	ldr	w4, [x0, w21, uxtw 2]
	and	w0, w22, 65535
	msub	w20, w20, w19, w23
	sub	w19, w19, w20
	and	w19, w19, 65535
	cmp	w22, w19
	csel	w19, w0, w19, cc
	lsl	w27, w19, 9
	cbz	w4, .L1260
	ldr	x0, [x28, 3480]
	mov	w2, 1
	str	x0, [x29, 120]
	add	x0, x29, 144
	str	w4, [x29, 108]
	mov	w1, w2
	str	w4, [x29, 116]
	str	x0, [x29, 128]
	add	x0, x29, 112
	bl	FlashReadPages
	ldp	w4, w0, [x29, 108]
	cmn	w0, #1
	ldr	w0, [x28, 3728]
	csinv	w24, w24, wzr, ne
	cmp	w0, 256
	bne	.L1262
	mov	w2, w4
	mov	w1, w21
	mov	x0, x26
	bl	sftl_printk
	ldr	x2, [x28, 3480]
	mov	w1, w21
	add	x0, x28, 3896
	bl	FtlMapWritePage
.L1262:
	ldr	x1, [x28, 3480]
	lsl	w20, w20, 9
	asr	w20, w20, 2
	mov	w2, w27
	mov	x0, x25
	add	x1, x1, x20, sxtw 2
	bl	ftl_memcpy
.L1263:
	add	w21, w21, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x25, x25, x27, sxtw
	b	.L1258
.L1260:
	mov	w2, w27
	mov	w1, 0
	mov	x0, x25
	bl	ftl_memset
	b	.L1263
.L1265:
	mov	w24, -1
	b	.L1256
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
	ldrh	w1, [x19, 3544]
	ldr	x2, [x19, 3560]
	bl	FtlVendorPartRead
	ldr	x0, [x19, 3560]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1268
	adrp	x1, .LC109
	adrp	x0, .LC71
	add	x1, x1, :lo12:.LC109
	add	x0, x0, :lo12:.LC71
	bl	sftl_printk
	ldr	x0, [x19, 3560]
	mov	w1, 0
	ldrh	w2, [x19, 3544]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1268:
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
	beq	.L1271
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	str	w21, [x20, 2720]
.L1271:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0, 2720]
	cmp	w3, w2
	bne	.L1272
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
.L1272:
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
	.global	FtlMapBlkWriteDump_data
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	ldr	w1, [x0, 56]
	cbz	w1, .L1284
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x2, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	add	x0, x2, 3728
	mov	x21, x1
	ldrh	w20, [x19, 6]
	ldr	x4, [x2, 3528]
	sub	w20, w20, #1
	ldr	x3, [x2, 3472]
	and	w20, w20, 65535
	ldr	x5, [x19, 40]
	stp	x3, x4, [x0, 8]
	ubfiz	x4, x20, 2, 16
	str	wzr, [x19, 56]
	ldr	w4, [x5, x4]
	str	w4, [x0, 4]
	cbz	w4, .L1278
	mov	w2, 1
	mov	w1, w2
	bl	FlashReadPages
.L1279:
	add	x1, x21, :lo12:.LANCHOR0
	mov	x0, x19
	ldr	x2, [x1, 3736]
	mov	w1, w20
	bl	FtlMapWritePage
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1278:
	ldrh	w2, [x2, 114]
	mov	w1, 255
	mov	x0, x3
	bl	ftl_memset
	b	.L1279
.L1284:
	ret
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
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
	add	x25, x19, 2656
	mov	w22, 0
	ldr	x21, [x19, 3528]
	mov	w24, 65535
	ldr	x0, [x19, 3464]
	str	x0, [x19, 3736]
	ldrh	w0, [x19, 2656]
	str	x21, [x19, 3744]
	strh	w0, [x21, 2]
	mov	w0, -3932
	strh	w0, [x21]
	str	wzr, [x21, 12]
	ldr	w0, [x19, 2664]
	stp	w0, wzr, [x21, 4]
	mov	w0, 19539
	ldrh	w1, [x19, 2424]
	movk	w0, 0x4654, lsl 16
	str	w0, [x19, 2312]
	mov	w0, 72
	ldrb	w2, [x19, 2430]
	strh	w1, [x19, 2326]
	movk	w0, 0x5000, lsl 16
	ldrh	w1, [x19, 2426]
	str	w0, [x19, 2316]
	ldrh	w0, [x19, 2662]
	strh	w0, [x19, 2320]
	orr	w1, w2, w1, lsl 6
	ldrh	w0, [x19, 58]
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
	ldr	x0, [x19, 3736]
	strh	w1, [x19, 2336]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x19, 3736]
	mov	x1, x20
	mov	w2, 48
	mov	x20, x25
	adrp	x25, .LANCHOR1
	add	x25, x25, :lo12:.LANCHOR1
	add	x25, x25, 560
	bl	ftl_memcpy
	ldr	x1, [x19, 2376]
	ldrh	w2, [x19, 44]
	ldr	x0, [x19, 3736]
	lsl	w2, w2, 1
	add	x0, x0, 48
	bl	ftl_memcpy
	ldrh	w0, [x19, 44]
	ldr	x3, [x19, 3736]
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
.L1288:
	ldrh	w2, [x20, 2]
	ldrh	w1, [x20]
	ldr	x0, [x19, 3464]
	str	x0, [x19, 3736]
	ldr	x0, [x19, 3528]
	str	x0, [x19, 3744]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19, 3732]
	ldrh	w0, [x19, 108]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L1289
	ldrh	w24, [x20, 4]
	strh	wzr, [x20, 2]
	strh	w1, [x20, 4]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19, 2620]
	str	w1, [x20, 8]
	add	w2, w1, 1
	str	w2, [x19, 2620]
	ubfiz	w2, w0, 10, 16
	str	w2, [x19, 3732]
	strh	w0, [x20]
	strh	w0, [x21, 2]
	str	w1, [x21, 4]
.L1289:
	ldrh	w1, [x19, 114]
	ldr	x0, [x19, 3464]
	bl	js_hash
	str	w0, [x21, 12]
	mov	w3, 1
	add	x0, x19, 3728
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x20, 2]
	ldr	w1, [x19, 3728]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x20, 2]
	cmn	w1, #1
	bne	.L1290
	cmp	w0, 1
	bne	.L1291
	adrp	x0, .LC1
	mov	w2, 1138
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1291:
	ldrh	w0, [x20, 2]
	cmp	w0, 1
	bne	.L1292
	ldrh	w0, [x19, 108]
	sub	w0, w0, #1
	strh	w0, [x20, 2]
.L1292:
	add	w22, w22, 1
	and	w22, w22, 65535
	cmp	w22, 3
	bls	.L1288
	add	x23, x23, :lo12:.LANCHOR0
	adrp	x0, .LC110
	mov	w2, w22
	add	x0, x0, :lo12:.LC110
	ldr	w1, [x23, 3732]
	bl	sftl_printk
.L1294:
	b	.L1294
.L1290:
	cmp	w0, 1
	beq	.L1288
	cmp	w1, 256
	beq	.L1288
	mov	w0, 65535
	cmp	w24, w0
	beq	.L1295
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
.L1295:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
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
	bne	.L1308
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlSysFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1308:
	mov	w0, 0
	ret
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	sftl_discard
	.type	sftl_discard, %function
sftl_discard:
	stp	x29, x30, [sp, -64]!
	add	w3, w0, w1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w20, w1
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	ldr	w2, [x1, 144]
	cmp	w3, w2
	bhi	.L1319
	cmp	w20, 31
	bhi	.L1313
.L1327:
	mov	w0, 0
.L1311:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1313:
	ldrh	w1, [x1, 62]
	udiv	w21, w0, w1
	msub	w0, w1, w21, w0
	ands	w0, w0, 65535
	beq	.L1314
	sub	w1, w1, w0
	add	w21, w21, 1
	cmp	w1, w20
	csel	w1, w1, w20, ls
	sub	w20, w20, w1, uxth
.L1314:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	str	w0, [x29, 60]
.L1315:
	ldrh	w0, [x19, 62]
	cmp	w20, w0
	bcs	.L1317
	ldr	w0, [x19, 3964]
	cmp	w0, 32
	bls	.L1327
	str	wzr, [x19, 3964]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1327
.L1317:
	mov	w2, 0
	add	x1, x29, 56
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 56]
	cmn	w0, #1
	beq	.L1316
	ldr	w0, [x19, 3964]
	mov	w2, 1
	add	x1, x29, 60
	add	w0, w0, 1
	str	w0, [x19, 3964]
	ldr	w0, [x19, 2592]
	add	w0, w0, 1
	str	w0, [x19, 2592]
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 56]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1316:
	ldrh	w0, [x19, 62]
	add	w21, w21, 1
	sub	w20, w20, w0
	b	.L1315
.L1319:
	mov	w0, -1
	b	.L1311
	.size	sftl_discard, .-sftl_discard
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC101
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x1, 576
	add	x0, x0, :lo12:.LC101
	str	x23, [sp, 48]
	bl	sftl_printk
	ldr	x0, [x19, 3568]
	mov	w21, 0
	ldrh	w2, [x19, 46]
	mov	w1, 0
	lsl	w2, w2, 1
	bl	ftl_memset
.L1329:
	ldr	w0, [x19, 2584]
	cmp	w21, w0
	bcc	.L1331
	adrp	x22, .LC111
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC111
	mov	w19, 0
	mov	w23, 65535
.L1332:
	ldrh	w0, [x20, 44]
	cmp	w0, w19
	bhi	.L1337
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 80
	ret
.L1331:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1330
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x19, 3568]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L1330:
	add	w21, w21, 1
	b	.L1329
.L1337:
	ldr	x0, [x20, 2376]
	ubfiz	x21, x19, 1, 16
	ldrh	w2, [x0, x21]
	ldr	x0, [x20, 3568]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L1334
	cmp	w2, w23
	beq	.L1334
	mov	x0, x22
	mov	w1, w19
	bl	sftl_printk
	ldrh	w0, [x20, 2424]
	cmp	w0, w19
	beq	.L1334
	ldrh	w0, [x20, 2520]
	cmp	w0, w19
	beq	.L1334
	ldrh	w0, [x20, 2472]
	cmp	w0, w19
	beq	.L1334
	ldr	x0, [x20, 2376]
	ldrh	w1, [x0, x21]
	cbnz	w1, .L1336
	ldr	x1, [x20, 3568]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
.L1334:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1332
.L1336:
	ldr	x1, [x20, 3568]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
	mov	w0, w19
	bl	update_vpc_list
	b	.L1334
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	str	x21, [sp, 32]
	mov	x21, x0
	ldrh	w20, [x0]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 44]
	cmp	w0, w20
	bcs	.L1343
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2760
	add	x1, x1, 600
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1343:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L1344
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x20, 1, 16
	ldr	x1, [x1, 2376]
	ldrh	w0, [x1, x0]
	cbz	w0, .L1345
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L1344:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x21, 8]
	mov	w1, 65535
	ldrh	w0, [x2, 3714]
	cmp	w0, w1
	beq	.L1346
	cmp	w20, w0
	bne	.L1347
	ldr	x2, [x2, 2376]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1348
.L1347:
	bl	update_vpc_list
.L1348:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19, 3714]
.L1346:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	ldr	x21, [sp, 32]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1345:
	mov	w0, w20
	bl	INSERT_FREE_LIST
	b	.L1344
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
.L1354:
	cmp	x20, x24
	beq	.L1361
	sub	x23, x20, #4
	b	.L1362
.L1357:
	ldr	w0, [x20]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19]
	cmp	w1, w0, uxth
	bne	.L1355
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
.L1355:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1356
	mov	x0, x19
	bl	allocate_new_data_superblock
.L1356:
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
.L1362:
	ldr	w0, [x20, -4]
	cmn	w0, #1
	beq	.L1357
	ldrb	w1, [x19, 6]
	ldrh	w0, [x21, 36]
	cmp	w1, w0
	bcc	.L1358
	adrp	x0, .LC1
	mov	w2, 960
	add	x1, x25, 632
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1358:
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
	beq	.L1359
	ldr	x2, [x21, 2376]
	ubfiz	x0, x1, 1, 16
	ldrh	w0, [x2, x0]
	cbnz	w0, .L1360
	adrp	x0, .LC112
	mov	w2, 0
	add	x0, x0, :lo12:.LC112
	bl	sftl_printk
.L1360:
	mov	w0, w23
	bl	decrement_vpc_count
.L1359:
	add	x20, x20, 32
	b	.L1354
.L1361:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x22, 36]
	cmp	w1, w0
	bcc	.L1353
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 975
	add	x1, x1, 632
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1353:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlProgPages, .-FtlProgPages
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
	bne	.L1369
.L1379:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 2520
	ldrh	w3, [x0, 2520]
	str	wzr, [x0, 3720]
	cmp	w3, w2
	bne	.L1392
.L1395:
	mov	w0, 0
.L1368:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1369:
	cbz	w0, .L1372
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w4, [x0, 4]
	cmp	w4, w3
	beq	.L1373
.L1374:
	mov	w1, 2
.L1372:
	add	x21, x19, :lo12:.LANCHOR0
	add	x0, x21, 2520
	bl	FtlGcScanTempBlk
	str	w0, [x29, 92]
	cmn	w0, #1
	beq	.L1375
	ldr	x1, [x21, 2384]
	ubfiz	x20, x20, 1, 16
	ldrh	w0, [x1, x20]
	cmp	w0, 4
	bls	.L1376
	sub	w0, w0, #5
	strh	w0, [x1, x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L1376:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0, 3720]
	cbnz	w1, .L1377
	ldr	w1, [x0, 2816]
	add	w1, w1, 1
	str	w1, [x0, 2816]
	ldr	w0, [x29, 92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1377:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19, 3720]
.L1388:
	mov	w0, 1
	b	.L1368
.L1373:
	strh	wzr, [x0, 4]
	ldrh	w0, [x2, 2416]
	cmp	w0, 17
	bhi	.L1374
	b	.L1372
.L1375:
	adrp	x0, .LANCHOR2+4
	ldrh	w1, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1388
	b	.L1379
.L1392:
	ldrh	w2, [x0, 3304]
	ldrb	w1, [x1, 7]
	ldrh	w0, [x0, 106]
	mul	w0, w1, w0
	cmp	w2, w0
	beq	.L1380
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 162
	add	x1, x1, 648
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1380:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR1
	add	x22, x22, :lo12:.LANCHOR1
	adrp	x25, .LC1
	add	x22, x22, 648
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
.L1381:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21, 3304]
	cmp	w0, w20
	bhi	.L1385
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x21, 2520]
	ldr	x2, [x21, 2376]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1386
	bl	INSERT_DATA_LIST
.L1387:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, -1
	strh	wzr, [x19, 3304]
	strh	w20, [x19, 2520]
	strh	wzr, [x19, 3292]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w20, [x19, 2672]
	ldrh	w0, [x19, 2652]
	ldrh	w1, [x19, 2416]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	ble	.L1395
	mov	w0, 20
	strh	w0, [x19, 3232]
	b	.L1395
.L1385:
	mov	w24, 12
	ldr	x26, [x21, 3312]
	ldr	w0, [x21, 2584]
	umull	x24, w20, w24
	add	x23, x26, x24
	ldr	w1, [x23, 8]
	cmp	w1, w0
	bcc	.L1382
	mov	w2, 168
	mov	x1, x22
	mov	x0, x25
	bl	sftl_printk
.L1382:
	ldr	w0, [x23, 8]
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x26, x24]
	ldr	w1, [x29, 92]
	cmp	w0, w1
	bne	.L1383
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	mov	w21, w0
	ldr	w0, [x23, 8]
	mov	w2, 1
	add	x1, x23, 4
	bl	log2phys
	mov	w0, w21
.L1394:
	bl	decrement_vpc_count
.L1384:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1381
.L1383:
	ldr	w0, [x23, 4]
	cmp	w1, w0
	beq	.L1384
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2520]
	b	.L1394
.L1386:
	bl	INSERT_FREE_LIST
	b	.L1387
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
	bcc	.L1396
	add	x0, x19, 3648
	bl	FtlMapBlkWriteDump_data
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19, 3720]
.L1396:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 2424
	add	x19, x19, 2472
	mov	x0, x20
	str	wzr, [x19, 1292]
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
	ldr	x0, [x19, 3448]
	bl	FlashProgPages
	mov	w10, 0
.L1402:
	ldr	w1, [x19, 3248]
	cmp	w10, w1
	bcc	.L1405
	ldr	x0, [x19, 3448]
	bl	FtlGcBufFree
	str	wzr, [x19, 3248]
	ldrh	w0, [x19, 2524]
	cbnz	w0, .L1406
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	b	.L1408
.L1405:
	ldr	x0, [x19, 3448]
	ubfiz	x1, x10, 5, 16
	add	x3, x0, x1
	ldr	w0, [x0, x1]
	cmn	w0, #1
	bne	.L1403
	ldrh	w3, [x19, 2520]
	ldr	x2, [x19, 2376]
	strh	wzr, [x2, x3, lsl 1]
	strh	w0, [x19, 2520]
	ldr	w0, [x19, 2816]
	add	w0, w0, 1
	str	w0, [x19, 2816]
	ldr	x0, [x19, 3448]
	add	x0, x0, x1
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L1408:
	mov	w0, 1
.L1401:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1403:
	ldr	x0, [x3, 16]
	ldr	w1, [x3, 4]
	ldp	w2, w0, [x0, 8]
	bl	FtlGcUpdatePage
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L1402
.L1406:
	mov	w0, 0
	b	.L1401
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
	beq	.L1410
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1411
.L1410:
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
.L1411:
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
	ldr	w2, [x1, 3424]
	cbnz	w2, .L1486
	ldrh	w2, [x1, 2400]
	cmp	w2, 47
	bls	.L1486
	mov	w23, w0
	adrp	x0, .LANCHOR2+4
	ldrh	w2, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1415
.L1418:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 3322]
	cmp	w1, w2
	bne	.L1416
.L1417:
	add	x20, x19, :lo12:.LANCHOR0
	bl	FtlReadRefresh
	mov	w21, 65535
	ldr	w0, [x20, 3236]
	ldrh	w1, [x20, 2672]
	add	w0, w0, 1
	add	w0, w0, w23, lsl 7
	str	w0, [x20, 3236]
	cmp	w1, w21
	bne	.L1419
	ldrh	w21, [x20, 2520]
	cmp	w21, w1
	bne	.L1489
	ldrh	w22, [x20, 3320]
	cmp	w22, w21
	bne	.L1419
	ldrh	w1, [x20, 2416]
	mov	w2, 1024
	cmp	w1, 24
	mov	w1, 5120
	csel	w1, w1, w2, cc
	cmp	w0, w1
	bls	.L1492
	strh	wzr, [x20, 3968]
	str	wzr, [x20, 3236]
	bl	GetSwlReplaceBlock
	and	w21, w0, 65535
	cmp	w21, w22
	bne	.L1493
	ldrh	w1, [x20, 2416]
	ldrh	w0, [x20, 3234]
	cmp	w1, w0
	bcs	.L1422
	mov	w0, 64
	bl	List_get_gc_head_node
	and	w0, w0, 65535
	cmp	w0, w21
	beq	.L1424
	ldr	x1, [x20, 2376]
	ubfiz	x0, x0, 1, 16
	ldrh	w0, [x1, x0]
	cmp	w0, 7
	bhi	.L1425
	mov	w0, 0
	bl	List_get_gc_head_node
	and	w22, w0, 65535
	mov	w0, 128
	strh	w0, [x20, 3234]
	cmp	w22, w21
	bne	.L1421
.L1424:
	bl	FtlGcReFreshBadBlk
.L1419:
	cmp	w23, 0
	mov	w0, 65535
	add	x1, x19, :lo12:.LANCHOR0
	ccmp	w21, w0, 0, eq
	bne	.L1427
	ldrh	w0, [x1, 2416]
	cmp	w0, 24
	bhi	.L1494
	cmp	w0, 16
	ldrh	w20, [x1, 106]
	bls	.L1429
	lsr	w20, w20, 5
.L1428:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1, 3232]
	cmp	w2, w0
	bcs	.L1432
	ldrh	w0, [x1, 2520]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1433
	ldrh	w2, [x1, 3320]
	cmp	w2, w0
	bne	.L1433
	ldrh	w0, [x1, 3968]
	cbnz	w0, .L1434
	ldr	w2, [x1, 2584]
	ldr	w3, [x1, 2580]
	add	w2, w2, w2, lsl 1
	cmp	w3, w2, lsr 2
	bcs	.L1435
.L1434:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2, 2652]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2, 3232]
.L1436:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19, 3244]
.L1413:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1415:
	ldrh	w1, [x1, 2520]
	cmp	w1, w0
	beq	.L1418
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	cbz	w0, .L1418
	mov	w0, 1
	b	.L1413
.L1416:
	ldrh	w3, [x0, 3320]
	cmp	w3, w2
	bne	.L1417
	strh	w1, [x0, 3320]
	mov	w1, -1
	strh	w1, [x0, 3322]
	b	.L1417
.L1425:
	mov	w0, 64
.L1521:
	strh	w0, [x20, 3234]
	b	.L1424
.L1422:
	mov	w0, 80
	b	.L1521
.L1493:
	mov	w22, w21
.L1421:
	add	x0, x19, :lo12:.LANCHOR0
	ubfiz	x1, x22, 1, 32
	mov	w21, w22
	ldr	x2, [x0, 2376]
	ldr	x3, [x0, 2384]
	ldrh	w5, [x0, 3232]
	ldrh	w4, [x3, x1]
	ldrh	w3, [x2, x1]
	mov	w1, w22
	ldrh	w2, [x0, 2416]
	adrp	x0, .LC113
	add	x0, x0, :lo12:.LC113
	bl	sftl_printk
	b	.L1424
.L1489:
	mov	w21, w1
	b	.L1419
.L1492:
	mov	w21, w22
	b	.L1419
.L1429:
	cmp	w0, 12
	bls	.L1430
	lsr	w20, w20, 4
	b	.L1428
.L1430:
	mov	w1, w20
	cmp	w0, 9
	lsr	w20, w20, 2
	csel	w20, w20, w1, cs
	b	.L1428
.L1494:
	mov	w20, 1
	b	.L1428
.L1435:
	mov	w2, 18
	strh	w2, [x1, 3232]
	b	.L1436
.L1433:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2652]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 3232]
.L1432:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 3324]
	cbz	w0, .L1495
	add	w20, w20, 32
	and	w20, w20, 65535
.L1495:
	mov	w21, 65535
.L1438:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 2672]
	cmp	w1, w2
	bne	.L1447
	cmp	w21, w1
	beq	.L1448
	strh	w21, [x0, 2672]
.L1449:
	add	x5, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldrh	w0, [x5, 2672]
	strb	wzr, [x5, 2680]
	cmp	w0, w1
	beq	.L1447
	bl	IsBlkInGcList
	cbz	w0, .L1452
	mov	w0, -1
	strh	w0, [x5, 2672]
.L1452:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	add	x24, x22, 2672
	ldrh	w1, [x22, 2672]
	cmp	w1, w0
	beq	.L1447
	mov	x0, x24
	bl	make_superblock
	ldrh	w1, [x22, 2672]
	ldr	x0, [x22, 2376]
	strh	wzr, [x22, 2674]
	strb	wzr, [x22, 2678]
	strh	wzr, [x22, 3970]
	ldrh	w0, [x0, x1, lsl 1]
	strh	w0, [x22, 3972]
.L1447:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w2, [x0, 2424]
	cmp	w2, w1
	beq	.L1453
	ldrh	w0, [x0, 2472]
	cmp	w0, w1
	bne	.L1454
.L1453:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 2672]
.L1454:
	adrp	x25, .LANCHOR1
	add	x25, x25, :lo12:.LANCHOR1
	add	x25, x25, 672
.L1483:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w24, [x22, 2672]
	cmp	w24, w0
	bne	.L1455
	mov	w26, 2
	str	wzr, [x22, 3244]
.L1456:
	ldrh	w5, [x22, 3240]
	mov	w0, w5
	bl	List_get_gc_head_node
	and	w6, w0, 65535
	strh	w6, [x22, 2672]
	cmp	w6, w24
	bne	.L1457
	strh	wzr, [x22, 3240]
	mov	w0, 8
	b	.L1413
.L1427:
	ldrh	w2, [x1, 2520]
	cmp	w2, w0
	bne	.L1498
	ldrh	w0, [x1, 3320]
	cmp	w0, w2
	bne	.L1498
	cmp	w21, w0
	bne	.L1498
	ldrh	w0, [x1, 2672]
	cmp	w0, w21
	beq	.L1439
.L1498:
	mov	w20, 1
	b	.L1438
.L1439:
	ldrh	w2, [x1, 2416]
	ldrh	w0, [x1, 3232]
	str	wzr, [x1, 3244]
	cmp	w2, w0
	bls	.L1440
	ldrh	w0, [x1, 3968]
	cbnz	w0, .L1441
	ldr	w0, [x1, 2584]
	ldr	w2, [x1, 2580]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bcs	.L1442
.L1441:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2652]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
.L1522:
	strh	w0, [x1, 3232]
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x1, 2376]
	ldrh	w0, [x2, x0]
	cmp	w0, 4
	bls	.L1440
	ldrh	w0, [x1, 3968]
	b	.L1413
.L1442:
	mov	w0, 18
	b	.L1522
.L1440:
	add	x20, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20, 3968]
	cbnz	w0, .L1498
	ldrh	w5, [x20, 2652]
	add	w0, w5, w5, lsl 1
	asr	w0, w0, 2
	strh	w0, [x20, 3232]
	mov	w0, 0
	bl	List_get_gc_head_node
	ldr	x1, [x20, 2376]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x20, 36]
	ldrh	w1, [x1, x0]
	ldrh	w0, [x20, 108]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L1445
	ldrh	w0, [x20, 2416]
	sub	w5, w5, #1
	cmp	w0, w5
	blt	.L1445
	bl	FtlReadRefresh
	ldrh	w0, [x20, 3968]
	b	.L1413
.L1445:
	cbnz	w1, .L1498
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x19, 2416]
	add	w0, w0, 1
	b	.L1413
.L1448:
	ldrh	w1, [x0, 3320]
	cmp	w1, w21
	beq	.L1449
	ldr	x2, [x0, 2376]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2, x1]
	cbnz	w1, .L1450
	mov	w1, -1
	strh	w1, [x0, 3320]
.L1450:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3320]
	strh	w1, [x0, 2672]
	mov	w1, -1
	strh	w1, [x0, 3320]
	b	.L1449
.L1457:
	mov	w0, w6
	bl	IsBlkInGcList
	add	w5, w5, 1
	cbz	w0, .L1458
	strh	w5, [x22, 3240]
	b	.L1456
.L1458:
	ldrh	w4, [x22, 36]
	ubfiz	x1, x6, 1, 16
	ldrh	w0, [x22, 106]
	and	w5, w5, 65535
	ldr	x2, [x22, 2376]
	strh	w5, [x22, 3240]
	mul	w0, w0, w4
	ldrh	w3, [x2, x1]
	sdiv	w4, w0, w26
	cmp	w3, w4
	bgt	.L1460
	cmp	w5, 48
	bls	.L1461
	cmp	w3, 8
	bls	.L1461
	ldrh	w3, [x22, 3292]
	cmp	w3, 35
	bhi	.L1461
.L1460:
	strh	wzr, [x22, 3240]
.L1461:
	ldrh	w1, [x2, x1]
	cmp	w0, w1
	bgt	.L1462
	cmp	w21, w24
	bne	.L1462
	ldrh	w0, [x22, 3240]
	cmp	w0, 3
	bhi	.L1462
	mov	w0, -1
	strh	wzr, [x22, 3240]
	strh	w0, [x22, 2672]
.L1523:
	ldrh	w0, [x22, 3968]
	b	.L1413
.L1462:
	cbnz	w1, .L1463
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x22, 3240]
	add	w0, w0, 1
	strh	w0, [x22, 3240]
	b	.L1456
.L1463:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0, 2680]
	ldrh	w0, [x0, 2424]
	cmp	w0, w6
	bne	.L1464
	adrp	x0, .LC1
	mov	w2, 700
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1464:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w0, [x0, 2472]
	cmp	w1, w0
	bne	.L1465
	adrp	x0, .LC1
	mov	w2, 701
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1465:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w0, [x0, 2520]
	cmp	w1, w0
	bne	.L1466
	adrp	x0, .LC1
	mov	w2, 702
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1466:
	add	x22, x19, :lo12:.LANCHOR0
	add	x24, x22, 2672
	mov	x0, x24
	bl	make_superblock
	ldrh	w1, [x22, 2672]
	ldr	x0, [x22, 2376]
	strh	wzr, [x22, 3970]
	ldrh	w0, [x0, x1, lsl 1]
	strh	w0, [x22, 3972]
	strh	wzr, [x22, 2674]
	strb	wzr, [x22, 2678]
.L1455:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 1
	str	w0, [x1, 3424]
	ldrh	w26, [x1, 106]
	cbz	w23, .L1467
	ldrh	w0, [x1, 36]
	ldrh	w2, [x1, 2672]
	ldr	x1, [x1, 2376]
	mul	w0, w0, w26
	ldrh	w1, [x1, x2, lsl 1]
	sub	w0, w0, w1
	mov	w1, 4
	sdiv	w0, w0, w1
	add	w20, w20, w0
	and	w20, w20, 65535
.L1467:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2674]
	add	w1, w0, w20
	cmp	w1, w26
	ble	.L1468
	sub	w20, w26, w0
	and	w20, w20, 65535
.L1468:
	mov	w27, 0
.L1469:
	cmp	w20, w27, uxth
	bls	.L1477
	add	x0, x19, :lo12:.LANCHOR0
	mov	w24, 0
	add	x3, x0, 2688
	mov	w1, 0
	mov	w6, 65535
	ldrh	w4, [x0, 2674]
	ldrh	w7, [x0, 36]
	add	w4, w4, w27
	b	.L1478
.L1471:
	ldrh	w2, [x3]
	cmp	w2, w6
	beq	.L1470
	ldr	x5, [x0, 3280]
	ubfiz	x8, x24, 5, 16
	add	w24, w24, 1
	orr	w2, w4, w2, lsl 10
	add	x5, x5, x8
	and	w24, w24, 65535
	str	w2, [x5, 4]
.L1470:
	add	w1, w1, 1
	add	x3, x3, 2
	and	w1, w1, 65535
.L1478:
	cmp	w1, w7
	bne	.L1471
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, w24
	mov	x28, 0
	ldrb	w2, [x22, 2680]
	ldr	x0, [x22, 3280]
	bl	FlashReadPages
	ubfiz	x0, x24, 5, 16
	str	x0, [x29, 104]
.L1472:
	ldr	x0, [x29, 104]
	cmp	x0, x28
	bne	.L1476
	add	w27, w27, 1
	b	.L1469
.L1476:
	ldr	x0, [x22, 3280]
	add	x1, x0, x28
	ldr	w0, [x0, x28]
	cmn	w0, #1
	beq	.L1473
	ldr	x24, [x1, 16]
	mov	w0, 61589
	ldrh	w1, [x24]
	cmp	w1, w0
	bne	.L1473
	ldr	w4, [x24, 8]
	cmn	w4, #1
	bne	.L1474
	str	w4, [x29, 96]
	mov	w2, 736
	mov	x1, x25
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	ldr	w4, [x29, 96]
.L1474:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w4
	bl	log2phys
	ldr	x0, [x22, 3280]
	ldr	w1, [x29, 124]
	add	x0, x0, x28
	ldr	w2, [x0, 4]
	cmp	w2, w1
	bne	.L1473
	ldrh	w1, [x22, 3970]
	ldr	x2, [x22, 3448]
	add	w1, w1, 1
	strh	w1, [x22, 3970]
	ldr	w1, [x22, 3248]
	ldr	w0, [x0, 24]
	add	x1, x2, x1, lsl 5
	str	x1, [x29, 96]
	str	w0, [x1, 24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29, 96]
	str	w0, [x1, 4]
	ldr	w0, [x22, 3248]
	ldr	x1, [x22, 3448]
	add	x0, x1, x0, lsl 5
	ldr	x1, [x22, 3280]
	add	x1, x1, x28
	ldr	x2, [x1, 8]
	str	x2, [x0, 8]
	ldr	x1, [x1, 16]
	str	x1, [x0, 16]
	ldr	w0, [x29, 124]
	mov	w1, 1
	str	w0, [x24, 12]
	ldrh	w0, [x22, 2520]
	strh	w0, [x24, 2]
	ldr	w0, [x22, 2624]
	str	w0, [x24, 4]
	ldr	w0, [x22, 3248]
	add	w0, w0, 1
	str	w0, [x22, 3248]
	ldr	x0, [x22, 3280]
	add	x0, x0, x28
	bl	FtlGcBufAlloc
	ldrb	w1, [x22, 2527]
	ldr	w0, [x22, 3248]
	cmp	w1, w0
	beq	.L1475
	ldrh	w0, [x22, 2524]
	cbnz	w0, .L1473
.L1475:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1473
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 3968]
	str	wzr, [x19, 3424]
	b	.L1413
.L1473:
	add	x28, x28, 32
	b	.L1472
.L1477:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 2674]
	add	w20, w20, w0
	and	w20, w20, 65535
	strh	w20, [x22, 2674]
	cmp	w26, w20
	bhi	.L1479
	ldr	w0, [x22, 3248]
	cbz	w0, .L1480
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1480
	str	wzr, [x22, 3424]
	b	.L1523
.L1480:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3970]
	cbnz	w1, .L1481
	ldrh	w1, [x0, 2672]
	ldr	x2, [x0, 2376]
	lsl	x1, x1, 1
	ldrh	w3, [x2, x1]
	cbz	w3, .L1481
	strh	wzr, [x2, x1]
	ldrh	w0, [x0, 2672]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1481:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 2672]
.L1479:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2416]
	str	wzr, [x0, 3424]
	cmp	w1, 2
	bhi	.L1482
	ldrh	w20, [x0, 106]
	b	.L1483
.L1482:
	ldrh	w0, [x0, 3968]
	cmp	w0, 0
	csinc	w0, w0, w1, ne
	b	.L1413
.L1486:
	mov	w0, 0
	b	.L1413
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
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
	stp	x25, x26, [sp, 64]
	mov	w25, w2
	stp	x21, x22, [sp, 32]
	stp	x27, x28, [sp, 80]
	bne	.L1525
	mov	x2, x3
	mov	w1, w25
	add	w0, w19, 256
	bl	FtlVendorPartRead
	mov	w21, w0
.L1524:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1525:
	add	w0, w1, w2
	str	w0, [x29, 128]
	adrp	x0, .LANCHOR0
	add	x3, x0, :lo12:.LANCHOR0
	add	w2, w1, w2
	str	x0, [x29, 96]
	ldr	w1, [x3, 144]
	cmp	w2, w1
	bhi	.L1546
	adrp	x0, .LANCHOR2
	ldr	w21, [x0, #:lo12:.LANCHOR2]
	cmn	w21, #1
	beq	.L1524
	ldrh	w0, [x3, 62]
	sub	w24, w2, #1
	mov	x26, x3
	mov	w28, 0
	mov	w21, 0
	stp	wzr, wzr, [x29, 132]
	str	wzr, [x29, 140]
	udiv	w27, w19, w0
	udiv	w24, w24, w0
	ldr	w0, [x3, 2616]
	add	w0, w0, w25
	str	w0, [x3, 2616]
	ldr	w0, [x3, 2588]
	mov	w20, w27
	sub	w22, w24, w27
	add	w22, w22, 1
	add	w0, w0, w22
	str	w0, [x3, 2588]
.L1527:
	cbnz	w22, .L1544
	ldr	x0, [x29, 96]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3324]
	cbnz	w1, .L1545
	ldrh	w0, [x0, 2416]
	cmp	w0, 31
	bhi	.L1524
.L1545:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	b	.L1524
.L1544:
	add	x1, x29, 156
	mov	w2, 0
	mov	w0, w20
	bl	log2phys
	ldr	w1, [x29, 156]
	cmn	w1, #1
	bne	.L1528
	mov	w5, 0
.L1529:
	ldrh	w0, [x26, 62]
	cmp	w5, w0
	bcc	.L1531
.L1532:
	add	w20, w20, 1
	subs	w22, w22, #1
	beq	.L1536
	ldrh	w0, [x26, 36]
	cmp	w28, w0, lsl 2
	bne	.L1527
.L1536:
	cbz	w28, .L1527
	ldr	x0, [x26, 3440]
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
.L1543:
	ldr	x0, [x26, 3440]
	add	x0, x0, x28
	ldr	w1, [x0, 24]
	cmp	w27, w1
	bne	.L1538
	ldr	x1, [x0, 8]
	ldr	x0, [x26, 3488]
	cmp	x1, x0
	bne	.L1539
	ldr	x0, [x29, 120]
	ldr	w2, [x29, 116]
	add	x1, x1, x0
	mov	x0, x23
.L1556:
	bl	ftl_memcpy
.L1539:
	ldr	x0, [x26, 3440]
	add	x0, x0, x28
	ldr	x1, [x0, 16]
	ldr	w2, [x0, 24]
	ldr	w1, [x1, 8]
	cmp	w2, w1
	beq	.L1540
	ldr	w1, [x26, 2792]
	add	w1, w1, 1
	str	w1, [x26, 2792]
.L1540:
	ldr	w2, [x0]
	cmn	w2, #1
	bne	.L1541
	ldr	w1, [x26, 2792]
	mov	w21, w2
	add	w1, w1, 1
	str	w1, [x26, 2792]
.L1541:
	ldr	w1, [x0]
	cmp	w1, 256
	bne	.L1542
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1542:
	ldr	x0, [x29, 104]
	add	x28, x28, 32
	cmp	x0, x28
	bne	.L1543
	mov	w28, 0
	b	.L1527
.L1531:
	madd	w0, w20, w0, w5
	cmp	w19, w0
	bhi	.L1530
	ldr	w1, [x29, 128]
	cmp	w1, w0
	bls	.L1530
	sub	w0, w0, w19
	str	w5, [x29, 120]
	lsl	w0, w0, 9
	mov	w2, 512
	mov	w1, 0
	add	x0, x23, x0
	bl	ftl_memset
	ldr	w5, [x29, 120]
.L1530:
	add	w5, w5, 1
	b	.L1529
.L1528:
	ldr	x0, [x26, 3440]
	ubfiz	x2, x28, 5, 32
	cmp	w20, w27
	add	x0, x0, x2
	str	w1, [x0, 4]
	ldrh	w0, [x26, 62]
	bne	.L1533
	ldr	x1, [x26, 3440]
	ldr	x5, [x26, 3488]
	add	x1, x1, x2
	str	x5, [x1, 8]
	udiv	w5, w19, w0
	msub	w3, w5, w0, w19
	str	w3, [x29, 140]
	sub	w5, w0, w3
	cmp	w25, w5
	csel	w3, w25, w5, ls
	str	w3, [x29, 136]
	cmp	w3, w0
	bne	.L1534
	str	x23, [x1, 8]
.L1534:
	ldr	x0, [x26, 3440]
	ldr	x1, [x26, 3536]
	add	x2, x0, x2
	ldrh	w0, [x26, 116]
	str	w20, [x2, 24]
	mul	w0, w0, w28
	add	w28, w28, 1
	and	x0, x0, 4294967292
	add	x0, x1, x0
	str	x0, [x2, 16]
	b	.L1532
.L1533:
	cmp	w20, w24
	bne	.L1535
	ldr	x5, [x26, 3440]
	ldr	x1, [x26, 3496]
	add	x5, x5, x2
	ldr	w3, [x29, 128]
	str	x1, [x5, 8]
	mul	w1, w20, w0
	sub	w3, w3, w1
	str	w3, [x29, 132]
	cmp	w0, w3
	bne	.L1534
	sub	w1, w1, w19
	lsl	w1, w1, 9
	add	x1, x23, x1
	str	x1, [x5, 8]
	b	.L1534
.L1535:
	ldr	x1, [x26, 3440]
	mul	w0, w0, w20
	add	x1, x1, x2
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	str	x0, [x1, 8]
	b	.L1534
.L1538:
	cmp	w24, w1
	bne	.L1539
	ldr	x1, [x0, 8]
	ldr	x0, [x26, 3496]
	cmp	x1, x0
	bne	.L1539
	ldrh	w0, [x26, 62]
	ldr	w2, [x29, 112]
	mul	w0, w0, w24
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	b	.L1556
.L1546:
	mov	w21, -1
	b	.L1524
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
	mov	x26, x3
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x27, x28, [sp, 80]
	str	w2, [x29, 172]
	bne	.L1560
	add	w0, w1, 256
	ldr	w1, [x29, 172]
	mov	x2, x3
	bl	FtlVendorPartWrite
.L1559:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	ret
.L1560:
	ldr	w0, [x29, 172]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	add	w0, w23, w0
	str	w0, [x29, 148]
	ldr	w0, [x1, 144]
	ldr	w2, [x29, 148]
	cmp	w2, w0
	bhi	.L1596
	adrp	x2, .LANCHOR2
	str	x2, [x29, 136]
	add	x3, x2, :lo12:.LANCHOR2
	ldr	w0, [x2, #:lo12:.LANCHOR2]
	cmn	w0, #1
	beq	.L1559
	mov	w0, 2048
	ldr	w2, [x29, 148]
	str	w0, [x1, 3976]
	add	x20, x1, 2424
	ldrh	w0, [x1, 62]
	sub	w2, w2, #1
	udiv	w25, w23, w0
	udiv	w0, w2, w0
	ldr	w2, [x29, 172]
	str	w0, [x29, 144]
	sub	w0, w0, w25
	add	w24, w0, 1
	str	w0, [x29, 132]
	ldr	w0, [x1, 2612]
	add	w0, w0, w2
	str	w0, [x1, 2612]
	ldr	w0, [x1, 2596]
	add	w0, w0, w24
	str	w0, [x1, 2596]
	ldr	w0, [x3, 8]
	cbz	w0, .L1562
	ldrh	w0, [x20, 4]
	add	x1, x1, 2472
	cmp	w0, 0
	csel	x20, x1, x20, eq
.L1562:
	adrp	x0, .LANCHOR1
	mov	w21, w25
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 696
	str	x0, [x29, 160]
.L1563:
	cbnz	w24, .L1591
	ldr	w1, [x29, 132]
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x19, 2416]
	cmp	w0, 5
	bls	.L1592
.L1594:
	mov	w0, 0
	b	.L1559
.L1591:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1564
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1016
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1564:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1565
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 2424
	cmp	x20, x1
	bne	.L1566
	add	x0, x0, 2472
	ldrh	w1, [x0, 4]
	cbnz	w1, .L1567
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
.L1567:
	add	x20, x19, :lo12:.LANCHOR0
	add	x22, x20, 2424
	add	x20, x20, 2472
	mov	x0, x22
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	cmp	w0, 0
	csel	x20, x20, x22, ne
.L1568:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1565
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1565:
	ldrh	w0, [x20, 4]
	ldrb	w1, [x20, 7]
	cmp	w0, w24
	csel	w0, w0, w24, ls
	lsl	w1, w1, 2
	cmp	w0, w1
	csel	w0, w0, w1, ls
	str	w0, [x29, 128]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1569
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1049
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1569:
	mov	x28, 0
.L1570:
	ldr	w0, [x29, 128]
	mov	w22, w28
	cmp	w28, w0
	bcc	.L1589
	mov	x22, x0
.L1571:
	add	x0, x19, :lo12:.LANCHOR0
	mov	x3, x20
	mov	w2, 0
	mov	w1, w22
	ldr	x0, [x0, 3456]
	bl	FtlProgPages
	cmp	w24, w22
	bcs	.L1590
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1126
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1590:
	sub	w24, w24, w22
	b	.L1563
.L1566:
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1601
	mov	x0, x20
	bl	allocate_new_data_superblock
	b	.L1568
.L1601:
	mov	x20, x1
	b	.L1565
.L1589:
	ldrh	w0, [x20, 4]
	cbz	w0, .L1571
	add	x1, x29, 188
	mov	w2, 0
	mov	w0, w21
	bl	log2phys
	mov	x0, x20
	lsl	x27, x28, 5
	bl	get_new_active_ppa
	add	x4, x19, :lo12:.LANCHOR0
	str	x4, [x29, 152]
	ldr	x1, [x4, 3456]
	ldrh	w2, [x4, 116]
	add	x1, x1, x27
	str	w0, [x1, 4]
	mul	w22, w22, w2
	ldr	x0, [x4, 3456]
	and	x1, x22, 4294967292
	str	x1, [x29, 120]
	add	x0, x0, x27
	ldr	x1, [x4, 3536]
	ldr	x3, [x29, 120]
	str	x1, [x29, 112]
	add	x22, x1, x3
	str	w21, [x0, 24]
	str	x22, [x0, 16]
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	ldr	w0, [x29, 144]
	cmp	w21, w25
	ldr	x4, [x29, 152]
	ccmp	w21, w0, 4, ne
	bne	.L1572
	cmp	w21, w25
	bne	.L1573
	ldrh	w2, [x4, 62]
	udiv	w0, w23, w2
	msub	w0, w0, w2, w23
	str	w0, [x29, 168]
	sub	w2, w2, w0
	ldr	w0, [x29, 172]
	cmp	w2, w0
	csel	w0, w2, w0, ls
	str	w0, [x29, 152]
.L1574:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29, 152]
	ldrh	w1, [x0, 62]
	cmp	w2, w1
	bne	.L1575
	ldr	x0, [x0, 3456]
	cmp	w21, w25
	add	x3, x0, x27
	bne	.L1576
	str	x26, [x3, 8]
.L1577:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1586
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1117
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1586:
	ldp	x1, x2, [x29, 112]
	mov	w0, -3947
	add	x28, x28, 1
	strh	w0, [x1, x2]
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w0, [x1, 2624]
	stp	w0, w21, [x22, 4]
	add	w21, w21, 1
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1, 2624]
	ldr	w0, [x29, 188]
	str	w0, [x22, 12]
	ldrh	w0, [x20]
	strh	w0, [x22, 2]
	b	.L1570
.L1573:
	ldr	w0, [x29, 148]
	ldrh	w2, [x4, 62]
	str	wzr, [x29, 168]
	msub	w2, w21, w2, w0
	and	w0, w2, 65535
	str	w0, [x29, 152]
	b	.L1574
.L1576:
	ldr	w0, [x29, 152]
.L1608:
	mul	w0, w0, w21
	sub	w0, w0, w23
	lsl	w0, w0, 9
	add	x0, x26, x0
	str	x0, [x3, 8]
	b	.L1577
.L1575:
	ldr	x1, [x0, 3456]
	cmp	w21, w25
	add	x1, x1, x27
	bne	.L1578
	ldr	x0, [x0, 3488]
.L1607:
	str	x0, [x1, 8]
	ldr	w0, [x29, 188]
	cmn	w0, #1
	beq	.L1580
	add	x4, x19, :lo12:.LANCHOR0
	str	w0, [x29, 196]
	str	w21, [x29, 216]
	mov	w2, 0
	str	x4, [x29, 104]
	ldr	x0, [x4, 3456]
	add	x0, x0, x27
	ldp	x1, x0, [x0, 8]
	stp	x1, x0, [x29, 200]
	mov	w1, 1
	add	x0, x29, 192
	bl	FlashReadPages
	ldr	w0, [x29, 192]
	ldr	x4, [x29, 104]
	cmn	w0, #1
	bne	.L1581
	ldr	w0, [x4, 2792]
	add	w0, w0, 1
	str	w0, [x4, 2792]
.L1584:
	ldr	w0, [x29, 152]
	cmp	w21, w25
	lsl	w2, w0, 9
	add	x0, x19, :lo12:.LANCHOR0
	bne	.L1585
	ldr	x0, [x0, 3456]
	mov	x1, x26
	add	x3, x0, x27
	ldr	w0, [x29, 168]
	ldr	x3, [x3, 8]
	lsl	w0, w0, 9
	add	x0, x3, x0
.L1609:
	bl	ftl_memcpy
	b	.L1577
.L1578:
	ldr	x0, [x0, 3496]
	b	.L1607
.L1581:
	ldr	w0, [x22, 8]
	cmp	w21, w0
	beq	.L1583
	ldr	w0, [x4, 2792]
	mov	w2, w21
	add	w0, w0, 1
	str	w0, [x4, 2792]
	adrp	x0, .LC114
	add	x0, x0, :lo12:.LC114
	ldr	w1, [x22, 8]
	bl	sftl_printk
.L1583:
	ldr	w0, [x22, 8]
	cmp	w21, w0
	beq	.L1584
	ldr	x1, [x29, 160]
	mov	w2, 1102
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L1584
.L1580:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x0, [x1, 3456]
	ldrh	w2, [x1, 114]
	mov	w1, 0
	add	x0, x0, x27
	ldr	x0, [x0, 8]
	bl	ftl_memset
	b	.L1584
.L1585:
	ldrh	w1, [x0, 62]
	ldr	x0, [x0, 3456]
	add	x3, x0, x27
	mul	w1, w1, w21
	sub	w1, w1, w23
	ldr	x0, [x3, 8]
	lsl	w1, w1, 9
	add	x1, x26, x1
	b	.L1609
.L1572:
	ldr	x0, [x4, 3456]
	add	x3, x0, x27
	ldrh	w0, [x4, 62]
	b	.L1608
.L1592:
	mov	w20, 256
	mov	w21, 65535
	mov	w22, 128
.L1595:
	ldrh	w0, [x19, 2672]
	cmp	w0, w21
	bne	.L1593
	ldrh	w0, [x19, 3320]
	cmp	w0, w21
	bne	.L1593
	mov	w0, 0
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L1593:
	mov	w1, 1
	strh	w22, [x19, 3234]
	mov	w0, w1
	strh	w22, [x19, 3232]
	bl	rk_ftl_garbage_collect
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x19, 2416]
	cmp	w0, 2
	bhi	.L1594
	subs	w20, w20, #1
	bne	.L1595
	b	.L1594
.L1596:
	mov	w0, -1
	b	.L1559
	.size	FtlWrite, .-FtlWrite
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
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	stp	x29, x30, [sp, -80]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	add	x21, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	add	x20, x21, 3728
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldr	x0, [x21, 3464]
	str	x0, [x20, 8]
	ldr	x0, [x21, 3528]
	str	x0, [x20, 16]
	ldr	x0, [x21, 2376]
	ldrh	w2, [x21, 44]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x21, 2656]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1613
.L1624:
	mov	w0, -1
.L1612:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1613:
	add	x23, x21, 2656
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w19, w0
	adrp	x1, .LANCHOR1
	add	w0, w0, 1
	add	x1, x1, :lo12:.LANCHOR1
	strh	w0, [x23, 2]
	ldrsh	w23, [x21, 2656]
	add	x24, x1, 712
.L1615:
	tbz	w19, #31, .L1621
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1465
	add	x1, x1, 712
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L1620
.L1621:
	orr	w0, w19, w23, lsl 10
	str	w0, [x20, 4]
	ldr	x0, [x21, 3464]
	mov	w2, 1
	str	x0, [x20, 8]
	mov	w1, w2
	mov	x0, x20
	bl	FlashReadPages
	ldr	x0, [x20, 16]
	ldr	w7, [x0, 12]
	cbz	w7, .L1616
	ldrh	w1, [x21, 114]
	ldr	x0, [x20, 8]
	bl	js_hash
	cmp	w7, w0
	beq	.L1616
	ldrh	w4, [x21, 2660]
	mov	w6, w0
	mov	w5, w7
	adrp	x0, .LC115
	mov	w3, w19
	mov	w2, w23
	mov	x1, x24
	add	x0, x0, :lo12:.LC115
	bl	sftl_printk
	cbnz	w19, .L1617
	ldrh	w0, [x21, 2660]
	cmp	w23, w0
	beq	.L1617
	ldrh	w19, [x21, 108]
	sxth	w23, w0
.L1619:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1615
.L1617:
	mov	w0, -1
	str	w0, [x20]
.L1616:
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L1619
	ldr	x0, [x21, 3464]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1619
	ldr	x0, [x21, 3528]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L1619
.L1620:
	add	x1, x22, :lo12:.LANCHOR0
	ldrh	w0, [x1, 44]
	ldrh	w1, [x1, 114]
	add	x0, x0, 24
	cmp	x1, x0, lsl 1
	bcs	.L1623
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1467
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1623:
	add	x19, x22, :lo12:.LANCHOR0
	mov	w2, 48
	add	x20, x19, 2312
	mov	x0, x20
	ldr	x1, [x19, 3736]
	bl	ftl_memcpy
	ldr	x0, [x19, 2376]
	ldrh	w2, [x19, 44]
	ldr	x1, [x19, 3736]
	lsl	w2, w2, 1
	add	x1, x1, 48
	bl	ftl_memcpy
	ldrh	w1, [x19, 44]
	ldr	x0, [x19, 3736]
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
	bne	.L1624
	ldrh	w1, [x19, 2320]
	ldrb	w2, [x19, 2322]
	ldrh	w0, [x19, 58]
	strh	w1, [x19, 2662]
	cmp	w2, w0
	bne	.L1624
	ldrh	w0, [x19, 106]
	ldrh	w2, [x19, 62]
	ldrh	w3, [x19, 36]
	str	w1, [x19, 3980]
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
	bls	.L1625
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1489
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1625:
	add	x0, x22, :lo12:.LANCHOR0
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
	bls	.L1626
	str	w1, [x0, 2620]
.L1626:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x0, 2348]
	ldr	w2, [x0, 2624]
	cmp	w1, w2
	bls	.L1627
	str	w1, [x0, 2624]
.L1627:
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1628
	add	x0, x22, :lo12:.LANCHOR0
	add	x0, x0, 2424
	bl	make_superblock
.L1628:
	add	x1, x22, :lo12:.LANCHOR0
	add	x0, x1, 2472
	ldrh	w2, [x1, 2472]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1629
	bl	make_superblock
.L1629:
	add	x1, x22, :lo12:.LANCHOR0
	add	x0, x1, 2520
	ldrh	w2, [x1, 2520]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1630
	bl	make_superblock
.L1630:
	add	x22, x22, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x0, x22, 2672
	ldrh	w2, [x22, 2672]
	cmp	w2, w1
	beq	.L1631
	bl	make_superblock
.L1631:
	mov	w0, 0
	b	.L1612
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	stp	x29, x30, [sp, -144]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	w20, 0
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w25, [x0, 6]
	ldr	x23, [x0, 40]
	ldr	x24, [x0, 16]
	ldr	x0, [x0, 24]
	lsl	w2, w25, 2
	str	x0, [x29, 128]
	ldrh	w0, [x19, 8]
	str	w0, [x29, 136]
	mov	x0, x23
	bl	ftl_memset
	stp	wzr, wzr, [x19, 48]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x2, [x0, 3464]
	ldr	x22, [x0, 3528]
	str	x2, [x0, 3736]
	str	x22, [x0, 3744]
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x19, 2]
	mov	w0, 1
	str	w0, [x19, 56]
	ldr	w0, [x29, 136]
	sub	w0, w0, #1
	str	w0, [x29, 116]
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 728
	str	x0, [x29, 104]
.L1641:
	ldr	w0, [x29, 136]
	cmp	w20, w0
	bge	.L1660
	ldr	w0, [x29, 116]
	sxtw	x28, w20
	cmp	w20, w0
	bne	.L1642
	lsl	x0, x28, 1
	mov	w26, 0
	add	x1, x24, x0
	str	x1, [x29, 136]
	mov	w1, 1
	ldrh	w0, [x24, x0]
	adrp	x24, .LANCHOR1
	add	x24, x24, :lo12:.LANCHOR1
	add	x24, x24, 728
	bl	FtlGetLastWrittenPage
	sxth	w1, w0
	add	w0, w0, 1
	strh	w0, [x19, 2]
	ldr	x0, [x29, 128]
	strh	w20, [x19]
	str	w1, [x29, 120]
	ldr	w0, [x0, x28, lsl 2]
	add	x28, x21, :lo12:.LANCHOR0
	add	x27, x28, 3728
	str	w0, [x19, 48]
.L1643:
	ldr	w0, [x29, 120]
	add	w0, w0, 1
	cmp	w26, w0
	blt	.L1646
.L1660:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x0, x19
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 2]
	ldrh	w0, [x21, 108]
	cmp	w1, w0
	bne	.L1648
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1648:
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
	ldp	x29, x30, [sp], 144
	ret
.L1646:
	ldr	x0, [x29, 136]
	mov	w2, 1
	mov	w1, w2
	ldrh	w0, [x0]
	orr	w0, w26, w0, lsl 10
	str	w0, [x27, 4]
	mov	x0, x27
	bl	FlashReadPages
	ldr	x0, [x27, 16]
	ldr	w8, [x0, 12]
	cbz	w8, .L1644
	ldrh	w1, [x28, 114]
	ldr	x0, [x27, 8]
	bl	js_hash
	cmp	w8, w0
	beq	.L1644
	mov	w5, w0
	mov	w4, w8
	mov	w3, w26
	mov	w2, w20
	mov	x1, x24
	adrp	x0, .LC116
	add	x0, x0, :lo12:.LC116
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x27]
.L1644:
	ldr	w0, [x27]
	cmn	w0, #1
	beq	.L1645
	ldrh	w0, [x22, 8]
	cmp	w25, w0
	bls	.L1645
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L1645
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x27, 4]
	str	w1, [x23, x0]
.L1645:
	add	w6, w26, 1
	sxth	w26, w6
	b	.L1643
.L1642:
	add	x26, x21, :lo12:.LANCHOR0
	lsl	x2, x28, 1
	add	x0, x26, 3728
	ldr	x1, [x26, 3464]
	str	x1, [x0, 8]
	add	x1, x24, x2
	str	x1, [x29, 120]
	ldrh	w1, [x24, x2]
	ldrh	w2, [x26, 108]
	sub	w2, w2, #1
	orr	w1, w2, w1, lsl 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x26, 3728]
	cmn	w0, #1
	beq	.L1662
	ldrh	w1, [x22]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L1662
	ldrh	w1, [x22, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L1650
.L1662:
	add	x26, x21, :lo12:.LANCHOR0
	mov	w27, 0
	add	x28, x26, 3728
.L1651:
	ldrh	w0, [x26, 108]
	cmp	w27, w0
	bge	.L1658
	ldr	x0, [x29, 120]
	mov	w2, 1
	mov	w1, w2
	ldrh	w0, [x0]
	orr	w0, w27, w0, lsl 10
	str	w0, [x28, 4]
	mov	x0, x28
	bl	FlashReadPages
	ldr	x0, [x28, 16]
	ldr	w7, [x0, 12]
	cbz	w7, .L1655
	ldrh	w1, [x26, 114]
	ldr	x0, [x28, 8]
	bl	js_hash
	cmp	w7, w0
	beq	.L1655
	ldr	x1, [x29, 104]
	mov	w5, w0
	mov	w4, w7
	mov	w3, w27
	mov	w2, w20
	adrp	x0, .LC117
	add	x0, x0, :lo12:.LC117
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x28]
.L1655:
	ldr	w0, [x28]
	cmn	w0, #1
	beq	.L1656
	ldrh	w0, [x22, 8]
	cmp	w25, w0
	bls	.L1656
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L1656
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x28, 4]
	str	w1, [x23, x0]
.L1656:
	add	w6, w27, 1
	sxth	w27, w6
	b	.L1651
.L1650:
	mov	w0, 0
.L1652:
	ldrh	w1, [x26, 108]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1654
.L1658:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L1641
.L1654:
	ldr	x2, [x26, 3464]
	sbfiz	x3, x0, 3, 32
	ldrh	w1, [x2, x3]
	cmp	w25, w1
	bls	.L1653
	add	x2, x2, x3
	ubfiz	x1, x1, 2, 16
	ldr	w2, [x2, 4]
	str	w2, [x23, x1]
.L1653:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1652
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x0, x1, 3896
	add	x29, sp, 0
	ldrh	w2, [x1, 124]
	strh	w2, [x0, 10]
	mov	w2, -3962
	strh	w2, [x0, 4]
	ldrh	w2, [x1, 148]
	strh	w2, [x0, 8]
	ldrh	w2, [x1, 126]
	strh	w2, [x0, 6]
	ldr	x2, [x1, 152]
	str	x2, [x0, 16]
	ldr	x2, [x1, 3600]
	str	x2, [x0, 24]
	ldr	x2, [x1, 3592]
	ldr	x1, [x1, 3608]
	stp	x2, x1, [x0, 32]
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
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 3648
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x21, x20, :lo12:.LANCHOR0
	str	x23, [sp, 48]
	ldrh	w0, [x21, 40]
	strh	wzr, [x21, 3760]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x21, 2656]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1681
.L1683:
	mov	w23, -1
.L1680:
	mov	w0, w23
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1681:
	bl	FtlLoadSysInfo
	mov	w23, w0
	cbnz	w0, .L1683
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldr	x1, [x21, 2568]
	mov	w0, 0
	ldrh	w3, [x21, 142]
	add	x1, x1, 4
.L1684:
	cmp	w0, w3
	bge	.L1689
	ldr	w2, [x1], 16
	tbz	w2, #31, .L1685
.L1689:
	add	x2, x20, :lo12:.LANCHOR0
	cmp	w0, w3
	ldrh	w1, [x2, 2340]
	add	w1, w1, 1
	strh	w1, [x2, 2340]
	bge	.L1694
.L1686:
	add	x19, x20, :lo12:.LANCHOR0
	add	x22, x19, 2424
	add	x21, x19, 2472
	mov	x0, x22
	bl	FtlSuperblockPowerLostFix
	mov	x0, x21
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
	ldrh	w0, [x19, 2342]
	strh	wzr, [x19, 2476]
	add	w0, w0, 1
	strh	w0, [x19, 2342]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1690
.L1685:
	add	w0, w0, 1
	b	.L1684
.L1694:
	ldrh	w0, [x2, 3760]
	cbnz	w0, .L1686
.L1690:
	add	x19, x20, :lo12:.LANCHOR0
	bl	FtlVpcCheckAndModify
	mov	w1, 65535
	add	x21, x19, 2424
	ldrh	w0, [x19, 2424]
	cmp	w0, w1
	beq	.L1680
	ldrh	w1, [x19, 2428]
	cbnz	w1, .L1680
	ldrh	w1, [x19, 2476]
	add	x20, x19, 2472
	cbnz	w1, .L1680
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 2472]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	mov	x0, x21
	bl	allocate_new_data_superblock
	mov	x0, x20
	bl	allocate_new_data_superblock
	b	.L1680
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	ftl_low_format
	.type	ftl_low_format, %function
ftl_low_format:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	str	wzr, [x0, 2620]
	str	wzr, [x0, 2624]
	ldrh	w0, [x0, 40]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L1697
	bl	FtlMakeBbt
.L1697:
	mov	w4, 23752
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 0
	movk	w4, 0xa0f, lsl 16
.L1698:
	ldrh	w2, [x1, 62]
	cmp	w0, w2, lsl 7
	blt	.L1699
	ldrh	w21, [x1, 44]
	add	x22, x20, :lo12:.LANCHOR0
	mov	w19, 0
.L1700:
	ldrh	w0, [x22, 46]
	cmp	w0, w21
	bhi	.L1701
	ldrh	w0, [x22, 36]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L1702
.L1706:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w21, 0
	mov	w22, 0
.L1703:
	ldrh	w0, [x19, 44]
	cmp	w0, w22
	bhi	.L1707
	ldrh	w2, [x19, 36]
	ldr	w1, [x19, 48]
	ldrh	w0, [x19, 46]
	str	w0, [x19, 3436]
	udiv	w4, w1, w2
	ubfx	x3, x4, 5, 16
	str	w4, [x19, 2584]
	add	w0, w3, 36
	strh	w0, [x19, 2652]
	mov	w0, 24
	mul	w0, w2, w0
	cmp	w21, w0
	ble	.L1708
	sub	w1, w1, w21
	udiv	w1, w1, w2
	str	w1, [x19, 2584]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x19, 2652]
.L1708:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 98]
	cbz	w1, .L1710
	ldrh	w5, [x0, 2652]
	add	w5, w5, w1, lsr 1
	strh	w5, [x0, 2652]
	mul	w5, w1, w2
	cmp	w21, w5
	bge	.L1710
	add	w1, w1, 32
	str	w4, [x0, 2584]
	add	w1, w3, w1
	strh	w1, [x0, 2652]
.L1710:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w22, -1
	add	x21, x19, 2424
	ldrh	w1, [x19, 2652]
	ldr	w0, [x19, 2584]
	sub	w0, w0, w1
	ldrh	w1, [x19, 106]
	mul	w0, w0, w2
	str	w0, [x19, 3980]
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
	strh	w22, [x19, 2672]
	strh	wzr, [x19, 2674]
	strb	wzr, [x19, 2678]
	lsr	w2, w2, 3
	strb	wzr, [x19, 2680]
	strh	wzr, [x21, 2]
	strb	wzr, [x21, 6]
	strh	wzr, [x19, 2424]
	str	wzr, [x19, 2580]
	bl	ftl_memset
.L1712:
	mov	x0, x21
	bl	make_superblock
	ldrb	w1, [x21, 7]
	ldrh	w0, [x21]
	cbnz	w1, .L1713
	ldr	x1, [x19, 2376]
	ubfiz	x0, x0, 1, 16
	strh	w22, [x1, x0]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L1712
.L1699:
	ldr	x5, [x1, 3488]
	ubfiz	x3, x0, 2, 16
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w2, [x5, x3]
	ldr	x2, [x1, 3496]
	str	w4, [x2, x3]
	b	.L1698
.L1701:
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w19, w19, w0
	and	w21, w21, 65535
	and	w19, w19, 65535
	b	.L1700
.L1702:
	udiv	w0, w19, w0
	ldr	w19, [x22, 136]
	add	x21, x20, :lo12:.LANCHOR0
	add	w0, w0, w19
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22, 40]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x22, 44]
.L1704:
	ldrh	w0, [x21, 46]
	cmp	w0, w19
	bls	.L1706
	mov	w0, w19
	add	w19, w19, 1
	mov	w1, 1
	and	w19, w19, 65535
	bl	FtlLowFormatEraseBlock
	b	.L1704
.L1707:
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w21, w21, w0
	and	w22, w22, 65535
	and	w21, w21, 65535
	b	.L1703
.L1713:
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
.L1714:
	mov	x0, x20
	bl	make_superblock
	ldrb	w1, [x20, 7]
	ldrh	w0, [x20]
	cbnz	w1, .L1715
	ldr	x1, [x19, 2376]
	ubfiz	x0, x0, 1, 16
	strh	w21, [x1, x0]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L1714
.L1715:
	ldr	w1, [x19, 2620]
	ubfiz	x0, x0, 1, 16
	str	w1, [x20, 12]
	add	w1, w1, 1
	str	w1, [x19, 2620]
	ldr	x1, [x19, 2376]
	ldrh	w2, [x20, 4]
	mov	w20, -1
	strh	w2, [x1, x0]
	strh	w20, [x19, 2520]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19, 2656]
	ldr	w0, [x19, 3980]
	strh	w0, [x19, 2662]
	ldr	w0, [x19, 2620]
	str	w0, [x19, 2664]
	add	w0, w0, 1
	strh	wzr, [x19, 2658]
	strh	w20, [x19, 2660]
	str	w0, [x19, 2620]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L1716
	adrp	x0, .LANCHOR2
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR2]
.L1716:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
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
	cbnz	w0, .L1725
	bl	FtlSysBlkInit
	cbnz	w0, .L1725
	mov	w0, 1
	str	w0, [x20, #:lo12:.LANCHOR2]
	ldrh	w0, [x19, 2416]
	cmp	w0, 15
	bhi	.L1725
	mov	w19, 8129
.L1724:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L1724
.L1725:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	sftl_init, .-sftl_init
	.align	2
	.global	FtlWriteToIDB
	.type	FtlWriteToIDB, %function
FtlWriteToIDB:
	stp	x29, x30, [sp, -208]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	w24, w1, w0
	stp	x25, x26, [sp, 64]
	sub	w25, w24, #1
	stp	x19, x20, [sp, 16]
	cmp	w25, 63
	stp	x27, x28, [sp, 80]
	bls	.L1729
	mov	w19, w0
	cmp	w0, 575
	bls	.L1730
.L1729:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x0, 3984]
	cbnz	w1, .L1731
.L1797:
	mov	w20, 0
	b	.L1728
.L1731:
	ldr	x26, [x0, 3992]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	ldr	w2, [x26]
	cmp	w2, w1
	bne	.L1733
	ldrh	w1, [x0, 10]
	mov	x0, 262140
	mov	w20, 65535
	add	x2, x26, x0
	mov	w3, 4097
	mov	w0, 0
.L1737:
	ldr	w4, [x2]
	cbnz	w4, .L1734
	ldr	w4, [x26, w0, uxtw 2]
	add	w0, w0, 1
	str	w4, [x2], -4
	cmp	w0, w3
	sub	w20, w20, #1
	csel	w0, w0, wzr, cc
	cmp	w20, 4096
	bne	.L1737
	mov	w20, 512
	b	.L1736
.L1734:
	add	w20, w20, 127
	lsr	w20, w20, 7
.L1736:
	ubfiz	w0, w1, 2, 14
	add	w22, w20, 4
	add	x19, x21, :lo12:.LANCHOR0
	mov	w23, 0
	str	wzr, [x29, 140]
	udiv	w22, w22, w0
	lsl	w0, w20, 7
	str	w0, [x29, 128]
	add	w22, w22, 1
.L1759:
	ldr	x0, [x19, 4000]
	mov	w1, 0
	mov	x2, 512
	bl	memset
	ldrh	w28, [x19, 10]
	ldr	x1, [x19, 3400]
	mul	w24, w28, w23
	cbz	x1, .L1767
	ldr	x0, [x19, 3408]
	mov	w25, 6
	cmp	x0, 0
	mov	w0, 9
	csel	w25, w25, w0, eq
.L1738:
	ldr	x3, [x19, 3376]
	mov	w1, w24
	mov	w0, 0
	blr	x3
	cmp	w22, 1
	beq	.L1739
	ldr	x2, [x19, 3376]
	add	w1, w28, w24
	mov	w0, 0
	blr	x2
.L1739:
	cmp	w25, 9
	bne	.L1769
	ldr	x27, [x19, 4000]
	mov	w2, 1024
	mov	w1, 0
	mov	x0, x27
	bl	ftl_memset
	mov	w0, 18766
	mov	w1, 12
	movk	w0, 0x464e, lsl 16
	stp	w0, w1, [x27]
	mov	w0, 4
	strb	w0, [x27, 17]
	ldrh	w0, [x19, 10]
	strh	w0, [x27, 18]
	strb	wzr, [x27, 16]
	mov	w0, 16
	strb	wzr, [x27, 20]
	strb	w0, [x27, 21]
	add	x0, x27, 12
	strh	wzr, [x27, 22]
	str	wzr, [x27, 12]
	bl	js_hash
	str	w0, [x27, 8]
.L1740:
	mul	w0, w22, w28
	mov	x6, x26
	str	w0, [x29, 136]
	mov	x28, 0
	add	w0, w24, 1
	str	w0, [x29, 132]
.L1741:
	ldr	w0, [x29, 136]
	mov	w1, w28
	cmp	w28, w0
	bcs	.L1748
	cmp	w25, 9
	bne	.L1742
	ldr	w0, [x29, 132]
	add	w0, w0, w1
.L1795:
	str	w0, [x29, 144]
	mov	w0, 61424
	str	w0, [x29, 148]
	cmp	w1, 0
	ccmp	w25, 9, 0, eq
	bne	.L1744
	ldr	x1, [x19, 3400]
	add	x7, x19, 3368
	str	x6, [x29, 104]
	mov	w0, 70
	str	x7, [x29, 120]
	blr	x1
	ldr	x7, [x29, 120]
	add	x3, x29, 144
	mov	x2, x27
	mov	w1, w24
	str	x7, [x29, 112]
	mov	w0, 0
	ldr	x8, [x7, 16]
	blr	x8
	mov	w1, w0
	ldr	x7, [x29, 112]
	ldrb	w0, [x19, 22]
	str	w1, [x29, 120]
	ldr	x2, [x7, 32]
	blr	x2
	ldr	w1, [x29, 120]
	ldr	x6, [x29, 104]
	cmn	w1, #1
	bne	.L1745
.L1748:
	ldrb	w0, [x19, 14]
	str	w0, [x29, 120]
	ldrh	w0, [x19, 10]
	ldr	x25, [x19, 4000]
	mul	w1, w0, w23
	str	w1, [x29, 136]
	add	x1, x19, 3368
	ldr	x2, [x1, 32]
	cbnz	x2, .L1746
	mov	w1, 6
.L1796:
	mul	w0, w22, w0
	mov	x28, 0
	mov	w27, 61424
	str	w0, [x29, 112]
	str	w1, [x29, 132]
.L1750:
	ldr	w0, [x29, 112]
	mov	w1, w28
	cmp	w28, w0
	bcs	.L1754
	ldr	w0, [x29, 132]
	cmp	w28, 0
	ccmp	w0, 9, 0, eq
	bne	.L1751
	ldr	x1, [x19, 3400]
	mov	w0, 70
	blr	x1
	ldr	x1, [x19, 3408]
	mov	w0, 2
	blr	x1
	ldr	x7, [x19, 3392]
	add	x3, x29, 144
	ldr	w1, [x29, 136]
	mov	x2, x25
	mov	w0, 0
	blr	x7
	ldrb	w0, [x29, 120]
	ldr	x1, [x19, 3408]
	blr	x1
	ldrb	w0, [x19, 22]
	ldr	x1, [x19, 3400]
	blr	x1
	ldr	w0, [x25]
	mov	w1, 18766
	movk	w1, 0x464e, lsl 16
	cmp	w0, w1
	beq	.L1752
.L1754:
	ldr	x0, [x19, 4000]
	mov	x1, x26
	mov	w24, 0
	mov	x2, x0
.L1753:
	mov	x25, x2
	mov	x28, x1
	add	x2, x2, 4
	add	x1, x1, 4
	ldr	w4, [x25]
	ldr	w3, [x28]
	cmp	w4, w3
	beq	.L1756
	mov	x2, 512
	mov	w1, 0
	bl	memset
	ldr	w2, [x25]
	mov	w1, w23
	ldr	w3, [x28]
	mov	w4, w24
	adrp	x0, .LC118
	add	x0, x0, :lo12:.LC118
	bl	sftl_printk
	ldrh	w1, [x19, 10]
	mov	w0, 0
	ldr	x2, [x19, 3376]
	mul	w1, w1, w23
	blr	x2
.L1757:
	add	w23, w23, w22
	cmp	w23, 15
	bls	.L1759
	ldr	w0, [x29, 140]
	cbnz	w0, .L1760
.L1733:
	mov	w20, -1
.L1760:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21, 3992]
	str	wzr, [x21, 3984]
	bl	kfree
	ldr	x0, [x21, 4000]
	bl	kfree
.L1728:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1767:
	mov	w25, 6
	b	.L1738
.L1769:
	mov	x27, 0
	b	.L1740
.L1742:
	add	w0, w1, w24
	lsl	w0, w0, 2
	b	.L1795
.L1744:
	ldr	x7, [x19, 3384]
	add	x3, x29, 144
	mov	x2, x6
	add	w1, w1, w24
	mov	w0, 0
	str	x6, [x29, 120]
	blr	x7
	cmn	w0, #1
	beq	.L1748
	ldr	x6, [x29, 120]
	add	x6, x6, 2048
.L1745:
	add	x28, x28, 1
	b	.L1741
.L1746:
	ldr	x1, [x1, 40]
	mov	w24, 6
	cmp	x1, 0
	mov	w1, 9
	csel	w1, w24, w1, eq
	b	.L1796
.L1751:
	ldr	x6, [x19, 3392]
	add	x3, x29, 144
	ldr	w0, [x29, 136]
	mov	x2, x25
	add	w1, w1, w0
	mov	w0, 0
	blr	x6
	cmn	w0, #1
	beq	.L1754
	ldr	w0, [x29, 148]
	cmp	w0, w27
	bne	.L1754
	add	x25, x25, 2048
.L1752:
	add	x28, x28, 1
	b	.L1750
.L1756:
	ldr	w3, [x29, 128]
	add	w24, w24, 1
	cmp	w24, w3
	bne	.L1753
	ldr	w0, [x29, 140]
	add	w0, w0, 1
	str	w0, [x29, 140]
	cmp	w0, 5
	bls	.L1757
	b	.L1760
.L1730:
	mov	w22, w1
	mov	x23, x2
	cmp	w0, 64
	bne	.L1761
	add	x20, x21, :lo12:.LANCHOR0
	mov	w0, 262144
	bl	ftl_malloc
	str	x0, [x20, 3992]
	mov	w0, 262144
	bl	ftl_malloc
	mov	x1, x0
	ldr	x0, [x20, 3992]
	str	x1, [x20, 4000]
	cbz	x0, .L1762
	cbz	x1, .L1762
	mov	w1, 1
	mov	w2, 262144
	str	w1, [x20, 3984]
	mov	w1, 0
	bl	ftl_memset
.L1761:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	w0, [x21, 3984]
	cbz	w0, .L1797
	cmp	w19, 63
	ldr	x1, [x21, 3992]
	bhi	.L1764
	mov	w0, 64
	sub	w19, w0, w19
	sub	w22, w22, w19
	ubfiz	x19, x19, 9, 25
	add	x23, x23, x19
	mov	x20, x1
.L1765:
	cmp	w25, 575
	bls	.L1766
	sub	w22, w22, w24
	sub	w22, w22, #446
.L1766:
	mov	w4, w22
	mov	x3, x20
	mov	w2, 262144
	adrp	x0, .LC120
	add	x0, x0, :lo12:.LC120
	bl	sftl_printk
	lsl	w2, w22, 9
	mov	x1, x23
	mov	x0, x20
	bl	ftl_memcpy
	b	.L1797
.L1762:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 752
	adrp	x0, .LC119
	add	x0, x0, :lo12:.LC119
	bl	sftl_printk
	b	.L1761
.L1764:
	lsl	w20, w19, 7
	sub	w20, w20, #8192
	add	x20, x1, x20, lsl 2
	b	.L1765
	.size	FtlWriteToIDB, .-FtlWriteToIDB
	.align	2
	.global	sftl_write
	.type	sftl_write, %function
sftl_write:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w20, w1
	mov	w19, w0
	str	x21, [sp, 32]
	mov	x21, x2
	bl	FtlWriteToIDB
	mov	x3, x21
	mov	w2, w20
	mov	w1, w19
	mov	w0, 0
	bl	FtlWrite
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
	.size	sftl_write, .-sftl_write
	.align	2
	.global	rk_sftl_vendor_dev_ops_register
	.type	rk_sftl_vendor_dev_ops_register, %function
rk_sftl_vendor_dev_ops_register:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	ldr	x3, [x2, 4008]
	cbnz	x3, .L1802
	str	x0, [x2, 4008]
	mov	w0, 0
	str	x1, [x2, 4016]
	ret
.L1802:
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
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x22, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	bl	ftl_malloc
	str	x0, [x22, 4024]
	cbz	x0, .L1809
	mov	w26, 22084
	mov	w25, 0
	mov	w24, 0
	mov	w23, 0
	movk	w26, 0x524b, lsl 16
.L1807:
	ldr	x3, [x22, 4008]
	mov	w1, 128
	ldr	x2, [x22, 4024]
	lsl	w0, w23, 7
	blr	x3
	mov	w21, w0
	cbnz	w0, .L1805
	ldr	x19, [x22, 4024]
	ldr	w0, [x19]
	cmp	w0, w26
	bne	.L1806
	add	x0, x19, 61440
	ldr	w1, [x19, 4]
	ldr	w0, [x0, 4092]
	cmp	w0, w1
	bne	.L1806
	cmp	w0, w24
	bls	.L1806
	mov	w25, w23
	mov	w24, w0
.L1806:
	add	w23, w23, 1
	cmp	w23, 2
	bne	.L1807
	cbz	w24, .L1808
	add	x0, x20, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w1, 128
	ldr	x3, [x0, 4008]
	lsl	w0, w25, 7
	blr	x3
	mov	w21, w0
	cbz	w0, .L1803
.L1805:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w21, -1
	ldr	x0, [x20, 4024]
	bl	kfree
	str	xzr, [x20, 4024]
	b	.L1803
.L1808:
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
.L1803:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1809:
	mov	w21, -12
	b	.L1803
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	adrp	x3, .LANCHOR0+4024
	ldr	x4, [x3, #:lo12:.LANCHOR0+4024]
	cbz	x4, .L1817
	ldrh	w6, [x4, 10]
	add	x5, x4, 16
	mov	w3, 0
.L1814:
	cmp	w3, w6
	bcc	.L1816
.L1817:
	mov	w0, -1
	ret
.L1816:
	ldrh	w7, [x5], 8
	cmp	w7, w0
	bne	.L1815
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
.L1815:
	add	w3, w3, 1
	b	.L1814
	.size	rk_sftl_vendor_read, .-rk_sftl_vendor_read
	.align	2
	.global	rk_sftl_vendor_write
	.type	rk_sftl_vendor_write, %function
rk_sftl_vendor_write:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR0
	add	x3, x23, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x19, [x3, 4024]
	cbz	x19, .L1837
	add	w21, w2, 63
	ldrh	w3, [x19, 10]
	ldrh	w24, [x19, 8]
	mov	x4, x1
	mov	w28, w2
	and	w21, w21, -64
	add	x1, x19, 16
	mov	w20, 0
.L1824:
	cmp	w20, w3
	bcc	.L1832
	ldrh	w1, [x19, 14]
	cmp	w21, w1
	bhi	.L1837
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
	b	.L1839
.L1832:
	ldrh	w6, [x1], 8
	cmp	w6, w0
	bne	.L1825
	uxtw	x27, w20
	add	x5, x19, 1024
	add	x22, x19, x27, lsl 3
	ldrh	w25, [x22, 20]
	add	w25, w25, 63
	and	w25, w25, -64
	cmp	w28, w25
	bls	.L1826
	ldrh	w0, [x19, 14]
	cmp	w21, w0
	bhi	.L1837
	add	x27, x27, 2
	ldrh	w22, [x22, 18]
	add	x27, x19, x27, lsl 3
	sub	w26, w3, #1
.L1827:
	cmp	w20, w26
	bcc	.L1828
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
.L1839:
	ldr	w0, [x19, 4]
	add	x1, x19, 61440
	add	x23, x23, :lo12:.LANCHOR0
	mov	x2, x19
	add	w0, w0, 1
	str	w0, [x19, 4]
	str	w0, [x1, 4092]
	mov	w1, 128
	ldr	x3, [x23, 4016]
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	and	w0, w0, 65535
	cmp	w0, 1
	csel	w0, w0, wzr, ls
	strh	w0, [x19, 8]
	lsl	w0, w24, 7
	blr	x3
	mov	w0, 0
.L1822:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1828:
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
	b	.L1827
.L1826:
	ldrh	w0, [x22, 18]
	uxtw	x2, w28
	mov	x1, x4
	add	x0, x5, x0
	bl	memcpy
	strh	w28, [x22, 20]
	b	.L1839
.L1825:
	add	w20, w20, 1
	b	.L1824
.L1837:
	mov	w0, -1
	b	.L1822
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
	cbz	x0, .L1854
	mov	w1, 30209
	mov	x19, x0
	movk	w1, 0x4004, lsl 16
	cmp	w21, w1
	beq	.L1843
	add	w1, w1, 1
	cmp	w21, w1
	beq	.L1844
.L1864:
	mov	x20, -14
	b	.L1842
.L1843:
#APP
// 21 "./arch/arm64/include/asm/current.h" 1
	mrs x21, sp_el0
// 0 "" 2
#NO_APP
	mov	x1, x20
	ldr	x2, [x21, 8]
#APP
// 413 "./arch/arm64/include/asm/uaccess.h" 1
	adds x1, x1, 8; ccmp x1, x2, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1850
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbnz	x0, .L1864
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L1848
.L1849:
	mov	x20, -1
.L1842:
	mov	x0, x19
	bl	kfree
.L1840:
	mov	x0, x20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1848:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_read
	cmn	w0, #1
	beq	.L1849
	strh	w0, [x19, 6]
	and	x2, x0, 65535
	add	x2, x2, 8
	mov	x0, x20
	ldr	x1, [x21, 8]
#APP
// 423 "./arch/arm64/include/asm/uaccess.h" 1
	adds x0, x0, x2; ccmp x0, x1, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1864
	mov	x0, x20
	mov	x1, x19
	mov	x20, 0
	bl	__arch_copy_to_user
	cbz	x0, .L1842
	b	.L1864
.L1844:
#APP
// 21 "./arch/arm64/include/asm/current.h" 1
	mrs x21, sp_el0
// 0 "" 2
#NO_APP
	mov	x1, x20
	ldr	x2, [x21, 8]
#APP
// 413 "./arch/arm64/include/asm/uaccess.h" 1
	adds x1, x1, 8; ccmp x1, x2, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1850
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbnz	x0, .L1864
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L1849
	ldrh	w2, [x19, 6]
	cmp	w2, 4087
	bhi	.L1849
	add	w2, w2, 8
	mov	x0, x20
	sxtw	x2, w2
	ldr	x1, [x21, 8]
#APP
// 413 "./arch/arm64/include/asm/uaccess.h" 1
	adds x0, x0, x2; ccmp x0, x1, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1852
	mov	x1, x20
	mov	x0, x19
	bl	__arch_copy_from_user
	cbnz	x0, .L1864
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_write
	sxtw	x20, w0
	b	.L1842
.L1850:
	str	xzr, [x19]
	b	.L1864
.L1852:
	mov	w1, 0
	mov	x0, x19
	bl	memset
	b	.L1864
.L1854:
	mov	x20, -1
	b	.L1840
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
	.global	sftl_nand_check_spare_buf
	.global	sftl_temp_buf
	.global	sftl_nand_check_buf
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
	.type	__func__.3799, %object
	.size	__func__.3799, 17
__func__.3799:
	.string	"INSERT_DATA_LIST"
	.zero	7
	.type	__func__.3794, %object
	.size	__func__.3794, 17
__func__.3794:
	.string	"INSERT_FREE_LIST"
	.zero	7
	.type	__func__.3830, %object
	.size	__func__.3830, 17
__func__.3830:
	.string	"List_remove_node"
	.zero	7
	.type	__func__.3862, %object
	.size	__func__.3862, 22
__func__.3862:
	.string	"List_update_data_list"
	.zero	2
	.type	__func__.3969, %object
	.size	__func__.3969, 22
__func__.3969:
	.string	"select_l2p_ram_region"
	.zero	2
	.type	__func__.4296, %object
	.size	__func__.4296, 16
__func__.4296:
	.string	"make_superblock"
	.type	__func__.4471, %object
	.size	__func__.4471, 19
__func__.4471:
	.string	"get_new_active_ppa"
	.zero	5
	.type	__func__.16134, %object
	.size	__func__.16134, 17
__func__.16134:
	.string	"FlashEraseBlocks"
	.zero	7
	.type	__func__.3904, %object
	.size	__func__.3904, 26
__func__.3904:
	.string	"ftl_map_blk_alloc_new_blk"
	.zero	6
	.type	__func__.3409, %object
	.size	__func__.3409, 11
__func__.3409:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.3636, %object
	.size	__func__.3636, 14
__func__.3636:
	.string	"FtlBbt2Bitmap"
	.zero	2
	.type	__func__.4317, %object
	.size	__func__.4317, 18
__func__.4317:
	.string	"SupperBlkListInit"
	.zero	6
	.type	__func__.16091, %object
	.size	__func__.16091, 15
__func__.16091:
	.string	"FlashReadPages"
	.zero	1
	.type	__func__.4081, %object
	.size	__func__.4081, 14
__func__.4081:
	.string	"FtlScanSysBlk"
	.zero	2
	.type	__func__.3679, %object
	.size	__func__.3679, 11
__func__.3679:
	.string	"FtlLoadBbt"
	.zero	5
	.type	__func__.16110, %object
	.size	__func__.16110, 15
__func__.16110:
	.string	"FlashProgPages"
	.zero	1
	.type	__func__.4443, %object
	.size	__func__.4443, 25
__func__.4443:
	.string	"allocate_data_superblock"
	.zero	7
	.type	__func__.4484, %object
	.size	__func__.4484, 16
__func__.4484:
	.string	"update_vpc_list"
	.type	__func__.4491, %object
	.size	__func__.4491, 20
__func__.4491:
	.string	"decrement_vpc_count"
	.zero	4
	.type	__func__.3930, %object
	.size	__func__.3930, 31
__func__.3930:
	.string	"Ftl_write_map_blk_to_last_page"
	.zero	1
	.type	__func__.3944, %object
	.size	__func__.3944, 16
__func__.3944:
	.string	"FtlMapWritePage"
	.type	__func__.3871, %object
	.size	__func__.3871, 16
__func__.3871:
	.string	"load_l2p_region"
	.type	__func__.3915, %object
	.size	__func__.3915, 15
__func__.3915:
	.string	"ftl_map_blk_gc"
	.zero	1
	.type	__func__.3986, %object
	.size	__func__.3986, 9
__func__.3986:
	.string	"log2phys"
	.zero	7
	.type	__func__.4205, %object
	.size	__func__.4205, 16
__func__.4205:
	.string	"FtlReUsePrevPpa"
	.type	__func__.4239, %object
	.size	__func__.4239, 22
__func__.4239:
	.string	"FtlRecoverySuperblock"
	.zero	2
	.type	__func__.4358, %object
	.size	__func__.4358, 14
__func__.4358:
	.string	"ftl_check_vpc"
	.zero	2
	.type	__func__.4059, %object
	.size	__func__.4059, 15
__func__.4059:
	.string	"FtlVpcTblFlush"
	.zero	1
	.type	__func__.4342, %object
	.size	__func__.4342, 21
__func__.4342:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.4464, %object
	.size	__func__.4464, 29
__func__.4464:
	.string	"allocate_new_data_superblock"
	.zero	3
	.type	__func__.3533, %object
	.size	__func__.3533, 13
__func__.3533:
	.string	"FtlProgPages"
	.zero	3
	.type	__func__.4561, %object
	.size	__func__.4561, 19
__func__.4561:
	.string	"FtlGcFreeTempBlock"
	.zero	5
	.type	__func__.4672, %object
	.size	__func__.4672, 23
__func__.4672:
	.string	"rk_ftl_garbage_collect"
	.zero	1
	.type	__func__.3561, %object
	.size	__func__.3561, 9
__func__.3561:
	.string	"FtlWrite"
	.zero	7
	.type	__func__.4137, %object
	.size	__func__.4137, 15
__func__.4137:
	.string	"FtlLoadSysInfo"
	.zero	1
	.type	__func__.4159, %object
	.size	__func__.4159, 18
__func__.4159:
	.string	"FtlMapTblRecovery"
	.zero	6
	.type	__func__.16249, %object
	.size	__func__.16249, 14
__func__.16249:
	.string	"FtlWriteToIDB"
	.zero	2
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
	.xword	.LC121
	.xword	rk_sftl_vendor_storage_fops
	.zero	56
	.bss
	.align	3
	.set	.LANCHOR0,. + 0
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
	.size	gSysInfo, 16
gSysInfo:
	.zero	16
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
	.size	g_nand_ops, 48
g_nand_ops:
	.zero	48
	.type	req_erase, %object
	.size	req_erase, 8
req_erase:
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
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
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
	.type	p_io_data_buf_0, %object
	.size	p_io_data_buf_0, 8
p_io_data_buf_0:
	.zero	8
	.type	p_io_data_buf_1, %object
	.size	p_io_data_buf_1, 8
p_io_data_buf_1:
	.zero	8
	.type	sftl_nand_check_buf, %object
	.size	sftl_nand_check_buf, 8
sftl_nand_check_buf:
	.zero	8
	.type	sftl_temp_buf, %object
	.size	sftl_temp_buf, 8
sftl_temp_buf:
	.zero	8
	.type	sftl_nand_check_spare_buf, %object
	.size	sftl_nand_check_spare_buf, 8
sftl_nand_check_spare_buf:
	.zero	8
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 8
p_sys_spare_buf:
	.zero	8
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 8
p_io_spare_buf:
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
	.type	g_tmp_data_superblock_id, %object
	.size	g_tmp_data_superblock_id, 2
g_tmp_data_superblock_id:
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
	.type	req_sys, %object
	.size	req_sys, 32
req_sys:
	.zero	32
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
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 64
gVendorBlkInfo:
	.zero	64
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.zero	2
	.zero	2
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
	.type	idb_need_write_back, %object
	.size	idb_need_write_back, 4
idb_need_write_back:
	.zero	4
	.zero	4
	.type	idb_buf, %object
	.size	idb_buf, 8
idb_buf:
	.zero	8
	.type	gp_flash_check_buf, %object
	.size	gp_flash_check_buf, 8
gp_flash_check_buf:
	.zero	8
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
	.type	check_vpc_table, %object
	.size	check_vpc_table, 16384
check_vpc_table:
	.zero	16384
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 8
gp_last_act_superblock:
	.zero	8
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
	.string	"SFTL version: 5.0.50 20181227"
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
	.string	"%s: addr: %x is in id block!!!!!!!!!!\n"
.LC81:
	.string	"not free: w: d:"
.LC82:
	.string	"not free: w: s:"
.LC83:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
.LC84:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
.LC85:
	.string	"%s error allocating memory. return -1\n"
.LC86:
	.string	"prog read error: = %x\n"
.LC87:
	.string	"prog read s error: = %x %x %x\n"
.LC88:
	.string	"prog read d error: = %x %x %x\n"
.LC89:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC90:
	.string	"FtlBbmTblFlush error:%x\n"
.LC91:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC92:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC93:
	.string	"decrement_vpc_count %x = %d\n"
.LC94:
	.string	"FtlMapWritePage error = %x \n"
.LC95:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC96:
	.string	"region_id = %x phyAddr = %x\n"
.LC97:
	.string	"spare:"
.LC98:
	.string	"map_ppn:"
.LC99:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC100:
	.string	"spuer block %x vpn is 0\n "
.LC101:
	.string	"...%s enter...\n"
.LC102:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC103:
	.string	"free blk vpc error %x = %x  %x\n"
.LC104:
	.string	"ftl_scan_all_data = %x\n"
.LC105:
	.string	"scan lpa = %x ppa= %x\n"
.LC106:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC107:
	.string	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
.LC108:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC109:
	.string	"no ect"
.LC110:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC111:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC112:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC113:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC114:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC115:
	.string	"%s hash error this.id =%x page =%x pre_id =%x hash =%x hash_r =%x\n"
.LC116:
	.string	"%s last blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
.LC117:
	.string	"%s scan blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
.LC118:
	.string	"write_idblock fail! %x %x %x %x\n"
.LC119:
	.string	"%s idb buffer alloc fail\n"
.LC120:
	.string	"%p %x %p %x\n"
.LC121:
	.string	"vendor_storage"
