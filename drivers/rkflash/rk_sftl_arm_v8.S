/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-08-17
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
	.global	test_node_in_list
	.type	test_node_in_list, %function
test_node_in_list:
	ldr	x2, [x0]
	adrp	x0, .LANCHOR0+2360
	mov	x4, -6148914691236517206
	and	w1, w1, 65535
	ldr	x3, [x0, #:lo12:.LANCHOR0+2360]
	movk	x4, 0xaaab, lsl 0
	mov	w5, 65535
	sub	x0, x2, x3
	asr	x0, x0, 1
	mul	x0, x0, x4
	mov	w4, 6
	and	w0, w0, 65535
.L69:
	cmp	w0, w1
	beq	.L70
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L71
	umaddl	x2, w0, w4, x3
	b	.L69
.L70:
	mov	w0, 1
	ret
.L71:
	mov	w0, 0
	ret
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x2, .LANCHOR0
	add	x3, x2, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w1, [x3, 44]
	cmp	w1, w0
	bls	.L88
	mov	w7, 6
	ldr	x12, [x3, 2360]
	mov	w1, -1
	umull	x11, w0, w7
	add	x5, x12, x11
	strh	w1, [x5, 2]
	strh	w1, [x12, x11]
	ldr	x1, [x3, 2368]
	cbnz	x1, .L75
	str	x5, [x3, 2368]
.L88:
	mov	w0, 0
	ret
.L75:
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
.L83:
	add	w3, w3, 1
	and	w3, w3, 65535
	cmp	w3, w18
	bhi	.L74
	cmp	w0, w2
	beq	.L74
	ubfiz	x16, x2, 1, 16
	ldrh	w17, [x1, 4]
	cmp	w17, 0
	ldrh	w7, [x14, x16]
	mul	w7, w7, w17
	csel	w7, w7, w10, ne
	cmp	w6, w7
	bne	.L79
	ldrh	w16, [x15, x16]
	ldrh	w7, [x9]
	cmp	w16, w7
	bcc	.L81
.L80:
	strh	w2, [x12, x11]
	ldrh	w2, [x1, 2]
	strh	w2, [x5, 2]
	add	x2, x4, :lo12:.LANCHOR0
	ldr	x3, [x2, 2368]
	cmp	x1, x3
	bne	.L84
	strh	w0, [x1, 2]
	str	x5, [x2, 2368]
	b	.L74
.L79:
	bcc	.L80
.L81:
	ldrh	w7, [x1]
	cmp	w7, w30
	bne	.L82
	strh	w2, [x5, 2]
	add	x2, x4, :lo12:.LANCHOR0
	strh	w0, [x1]
	str	x5, [x2, 2392]
.L74:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L82:
	umaddl	x1, w7, w8, x13
	mov	w2, w7
	b	.L83
.L84:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2360]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L74
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
	bcs	.L90
	adrp	x1, .LANCHOR1
	adrp	x0, .LC1
	mov	w2, 221
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L90:
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
	beq	.L94
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
	cbnz	x1, .L95
	str	x4, [x5, 2408]
.L94:
	mov	w0, 0
	ret
.L95:
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
.L98:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11, x5]
	cmp	w5, w12
	bcs	.L96
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L97
	strh	w2, [x4, 2]
	strh	w0, [x1]
	b	.L94
.L97:
	umaddl	x1, w5, w6, x10
	mov	w2, w5
	b	.L98
.L96:
	ldrh	w5, [x1, 2]
	strh	w5, [x4, 2]
	strh	w2, [x9, x8]
	add	x2, x3, :lo12:.LANCHOR0
	ldr	x3, [x2, 2408]
	cmp	x1, x3
	bne	.L99
	strh	w0, [x1, 2]
	str	x4, [x2, 2408]
	b	.L94
.L99:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2360]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L94
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
	bcs	.L100
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 214
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L100:
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
	bne	.L104
	ldr	x0, [x23]
	cmp	x20, x0
	beq	.L104
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 397
	add	x1, x1, 48
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L104:
	ldr	x0, [x23]
	mov	w1, 65535
	cmp	x20, x0
	ldrh	w0, [x22, x21]
	bne	.L105
	cmp	w0, w1
	bne	.L106
	str	xzr, [x23]
.L107:
	mov	w0, -1
	strh	w0, [x22, x21]
	strh	w0, [x20, 2]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L106:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 6
	ldr	x1, [x19, 2360]
	umaddl	x0, w0, w2, x1
	mov	w1, -1
	str	x0, [x23]
	strh	w1, [x0, 2]
	b	.L107
.L105:
	cmp	w0, w1
	ldrh	w1, [x20, 2]
	bne	.L108
	cmp	w1, w0
	beq	.L107
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 6
	mov	w2, -1
	umull	x1, w1, w0
	ldr	x0, [x19, 2360]
	strh	w2, [x0, x1]
	b	.L107
.L108:
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
	b	.L107
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
	ldr	x2, [x0]
	cbz	x2, .L116
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LANCHOR0+2360
	and	w1, w1, 65535
	mov	w4, 65535
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	w5, 6
	ldr	x19, [x3, #:lo12:.LANCHOR0+2360]
.L112:
	cbnz	w1, .L113
.L115:
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
.L113:
	ldrh	w3, [x2]
	cmp	w3, w4
	beq	.L115
	sub	w1, w1, #1
	umaddl	x2, w3, w5, x19
	and	w1, w1, 65535
	b	.L112
.L116:
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
	cbz	x1, .L128
	ldr	x3, [x0, 2360]
	mov	w4, 6
	mov	w0, 65535
.L125:
	cbz	w2, .L126
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L127
	ret
.L127:
	sub	w2, w2, #1
	umaddl	x1, w1, w4, x3
	and	w2, w2, 65535
	b	.L125
.L128:
	mov	w0, 65535
	ret
.L126:
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
	beq	.L131
	ldrh	w1, [x0, 2472]
	cmp	w1, w20
	beq	.L131
	ldrh	w1, [x0, 2520]
	cmp	w1, w20
	beq	.L131
	mov	w22, 6
	ldr	x24, [x0, 2360]
	ldr	x1, [x0, 2368]
	umull	x22, w20, w22
	add	x23, x24, x22
	cmp	x23, x1
	beq	.L131
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
	bne	.L134
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	bne	.L134
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 488
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L134:
	ldrh	w0, [x23, 2]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L135
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	beq	.L131
.L135:
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
	bcs	.L131
	mov	w1, w20
	add	x0, x22, 2368
	bl	List_remove_node
	ldrh	w0, [x22, 2400]
	cbnz	w0, .L137
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 499
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L137:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2400]
	sub	w0, w0, #1
	strh	w0, [x19, 2400]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L131:
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
.L142:
	and	w19, w3, 65535
	cmp	w19, w2
	bcc	.L144
	add	x4, x0, 4
	mov	w19, w2
	mov	w6, -2147483648
	mov	w3, 0
.L145:
	cmp	w3, w2
	bne	.L147
	cmp	w19, w2
	bcc	.L143
	add	x1, x1, :lo12:.LANCHOR0
	mov	w19, w2
	mov	w3, -1
	ldrh	w4, [x1, 2576]
	mov	w1, 0
.L148:
	cmp	w1, w2
	bne	.L150
	cmp	w19, w1
	bcc	.L143
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 823
	add	x1, x1, 96
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L143
.L144:
	add	x3, x3, 1
	add	x5, x0, x3, lsl 4
	ldrh	w5, [x5, -16]
	cmp	w5, w4
	bne	.L142
.L143:
	mov	w0, w19
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L147:
	ldr	w5, [x4]
	tbnz	w5, #31, .L146
	cmp	w6, w5
	bls	.L146
	mov	w6, w5
	mov	w19, w3
.L146:
	add	w3, w3, 1
	add	x4, x4, 16
	and	w3, w3, 65535
	b	.L145
.L150:
	ldr	w6, [x0, 4]
	cmp	w3, w6
	bls	.L149
	ldrh	w5, [x0]
	cmp	w5, w4
	csel	w3, w3, w6, eq
	csel	w19, w19, w1, eq
.L149:
	add	w1, w1, 1
	add	x0, x0, 16
	and	w1, w1, 65535
	b	.L148
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR0
	add	x3, x1, :lo12:.LANCHOR0
	ldrh	w2, [x3, 2578]
	cmp	w2, 4
	bhi	.L153
	cbnz	w0, .L153
	add	w2, w2, 1
	strh	w2, [x3, 2578]
	ret
.L153:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w6, 65535
	mov	x1, 0
	ldrh	w4, [x0, 44]
	ldr	x5, [x0, 2376]
	strh	wzr, [x0, 2578]
	str	wzr, [x0, 2580]
.L154:
	cmp	w4, w1, uxth
	bhi	.L156
	ret
.L156:
	ldrh	w3, [x5, x1, lsl 1]
	cmp	w3, w6
	beq	.L155
	ldr	w2, [x0, 2580]
	add	w2, w2, w3
	str	w2, [x0, 2580]
.L155:
	add	x1, x1, 1
	b	.L154
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	and	w0, w0, 65535
	cbz	w1, .L159
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_set_blk_mode.part.7
	ldp	x29, x30, [sp], 16
	ret
.L159:
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
.L167:
	cmp	w3, w2, uxth
	bhi	.L169
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
.L170:
	cmp	x2, x3
	bne	.L172
	ret
.L169:
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5, 16]
	cmp	w5, w1
	beq	.L168
	ldrh	w5, [x0, 4]
	add	w5, w5, 1
	strh	w5, [x0, 4]
.L168:
	add	w2, w2, 1
	b	.L167
.L172:
	ldrh	w4, [x2]
	cmp	w4, w5
	beq	.L171
	ldrh	w4, [x0, 4]
	add	w4, w1, w4
	strh	w4, [x0, 4]
.L171:
	add	x2, x2, 2
	b	.L170
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	ldrh	w1, [x0, 4]
	cbz	w1, .L173
	ldrh	w2, [x0]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L173
	ldrb	w2, [x0, 6]
	add	x2, x2, 8
	ldrh	w3, [x0, x2, lsl 1]
	adrp	x2, .LANCHOR0+36
	ldrh	w4, [x2, #:lo12:.LANCHOR0+36]
	mov	w2, w1
.L176:
	cmp	w3, w2
	beq	.L178
.L173:
	ret
.L178:
	ldrb	w1, [x0, 6]
	add	w1, w1, 1
	and	w1, w1, 255
	strb	w1, [x0, 6]
	cmp	w1, w4
	bne	.L177
	ldrh	w1, [x0, 2]
	strb	wzr, [x0, 6]
	add	w1, w1, 1
	strh	w1, [x0, 2]
.L177:
	ldrb	w1, [x0, 6]
	add	x1, x1, 8
	ldrh	w3, [x0, x1, lsl 1]
	b	.L176
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
	bcc	.L180
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2146
	add	x1, x1, 120
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L180:
	add	x0, x20, :lo12:.LANCHOR0
	add	x6, x19, 16
	add	x7, x0, 64
	strh	wzr, [x19, 4]
	strb	wzr, [x19, 7]
	mov	x5, 0
	ldrh	w8, [x0, 36]
	mov	w9, -1
.L181:
	cmp	w8, w5, uxth
	bhi	.L183
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
.L183:
	ldrh	w1, [x19]
	ldrb	w0, [x7, x5]
	bl	V2P_block
	mov	w4, w0
	strh	w9, [x6]
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L182
	strh	w4, [x6]
	ldrb	w0, [x19, 7]
	add	w0, w0, 1
	strb	w0, [x19, 7]
.L182:
	add	x5, x5, 1
	add	x6, x6, 2
	b	.L181
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
	bhi	.L197
	cbz	w6, .L195
	mov	w0, 32768
	sdiv	w6, w0, w6
.L196:
	add	x0, x5, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x0, [x0, 2360]
	umaddl	x8, w8, w1, x0
	mov	w0, 0
	strh	w6, [x8, 4]
	ret
.L190:
	mov	w6, 0
	b	.L189
.L195:
	mov	w6, 0
	b	.L196
.L197:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L188:
	ldrb	w0, [x7, x9]
	mov	w1, w8
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L187
	add	w6, w6, w11
	and	w6, w6, 65535
.L187:
	add	x9, x9, 1
	cmp	w10, w9, uxth
	bhi	.L188
	cbz	w6, .L190
	mov	w0, 32768
	sdiv	w6, w0, w6
.L189:
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
	cbz	x0, .L200
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
.L200:
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
	cbz	x1, .L208
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
.L204:
	cmp	w0, w3
	beq	.L207
	umull	x5, w1, w6
	ldrh	w5, [x4, x5]
	cmp	w5, w7
	bne	.L205
.L207:
	add	x2, x2, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2, 2384]
	ldrh	w0, [x0, x1]
	ret
.L205:
	add	w3, w3, 1
	mov	w1, w5
	and	w3, w3, 65535
	b	.L204
.L208:
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
	beq	.L210
.L215:
	sub	w0, w19, w22
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L210:
	ldrh	w0, [x21, 2672]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L212
	ldr	x1, [x21, 2376]
	ubfiz	x0, x0, 1, 16
	adrp	x2, .LC67
	add	x2, x2, :lo12:.LC67
	ldrh	w3, [x1, x0]
	mov	x0, x19
	mov	x1, 64
	bl	snprintf
	add	x19, x19, x0, sxtw
.L212:
	mov	w0, 0
	adrp	x24, .LC68
	bl	List_get_gc_head_node
	add	x21, x23, :lo12:.LANCHOR0
	and	w4, w0, 65535
	add	x24, x24, :lo12:.LC68
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
.L214:
	cmp	w4, w27
	beq	.L213
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
	bne	.L214
.L213:
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
.L216:
	cmp	w4, w25
	beq	.L215
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
	beq	.L215
	ldr	x0, [x23, 2360]
	ldrh	w4, [x0, x24]
	b	.L216
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	cmp	w0, 2047
	ble	.L224
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
.L224:
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
	bcs	.L230
	ldrh	w2, [x0, 44]
	mov	x1, 0
	ldr	x4, [x0, 2384]
	str	wzr, [x0, 2628]
.L231:
	cmp	w2, w1
	bhi	.L232
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w0, [x1, 2628]
	udiv	w2, w0, w2
	str	w2, [x1, 2636]
	ldr	w2, [x1, 2632]
	sub	w0, w0, w2
	ldrh	w2, [x1, 96]
	udiv	w0, w0, w2
	str	w0, [x1, 2628]
.L233:
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w9, [x1, 2648]
	ldr	w11, [x1, 2636]
	add	w0, w9, 256
	cmp	w0, w11
	bls	.L238
	ldr	w1, [x1, 2644]
	add	w0, w9, 768
	cmp	w0, w1
	bls	.L238
.L240:
	mov	w20, 65535
.L239:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L232:
	ldrh	w3, [x4, x1, lsl 1]
	add	x1, x1, 1
	ldr	w5, [x0, 2628]
	add	w3, w3, w5
	str	w3, [x0, 2628]
	b	.L231
.L230:
	ldr	w1, [x0, 2644]
	cmp	w2, w1
	bls	.L233
	add	w1, w1, 1
	str	w1, [x0, 2644]
	mov	w1, 0
.L235:
	ldrh	w2, [x0, 44]
	cmp	w1, w2
	bcs	.L233
	ldr	x4, [x0, 2384]
	ubfiz	x3, x1, 1, 32
	add	w1, w1, 1
	ldrh	w2, [x4, x3]
	add	w2, w2, 1
	strh	w2, [x4, x3]
	b	.L235
.L238:
	add	x10, x8, :lo12:.LANCHOR0
	ldrh	w0, [x10, 2416]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	and	w6, w0, 65535
	add	w0, w9, 64
	cmp	w6, w0
	bcc	.L240
	ldr	x0, [x10, 2368]
	cbz	x0, .L240
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
.L241:
	ldrh	w3, [x0]
	cmp	w3, w10
	bne	.L244
	mov	w20, w2
.L243:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L240
	ubfiz	x7, x20, 1, 32
	ldrh	w3, [x5, x7]
	cmp	w9, w3
	bcs	.L245
	bl	GetFreeBlockMinEraseCount
	cmp	w9, w0, uxth
	bcs	.L245
	add	x0, x8, :lo12:.LANCHOR0
	str	w4, [x0, 2648]
.L245:
	cmp	w11, w3
	bls	.L240
	add	w0, w3, 128
	cmp	w6, w0
	ble	.L240
	add	w0, w3, 256
	cmp	w11, w0
	bhi	.L246
	add	x0, x8, :lo12:.LANCHOR0
	add	w3, w3, 768
	ldr	w0, [x0, 2644]
	cmp	w3, w0
	bcs	.L240
.L246:
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
	b	.L239
.L244:
	add	w1, w1, 1
	and	w1, w1, 65535
	cmp	w1, w12
	bhi	.L240
	ldrh	w15, [x0, 4]
	cbz	w15, .L242
	sub	x0, x0, x7
	asr	x0, x0, 1
	mul	x0, x0, x13
	and	w20, w0, 65535
	and	x0, x0, 65535
	ldrh	w0, [x5, x0, lsl 1]
	cmp	w9, w0
	bcs	.L243
	cmp	w4, w0
	bls	.L242
	mov	w4, w0
	mov	w2, w20
.L242:
	umaddl	x0, w3, w14, x7
	b	.L241
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
	and	w0, w0, 65535
	mov	w1, 65535
	cmp	w0, w1
	beq	.L257
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
.L257:
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
	bne	.L261
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2776
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L261:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x0, 106]
	cmp	w1, w0
	bne	.L262
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2777
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L262:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L263
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2778
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L263:
	ldrb	w0, [x19, 6]
	add	x1, x20, :lo12:.LANCHOR0
	strb	wzr, [x19, 10]
	add	x0, x0, 8
	ldrh	w2, [x1, 36]
	mov	w1, 65535
	ldrh	w0, [x19, x0, lsl 1]
.L264:
	cmp	w0, w1
	beq	.L266
	ldrh	w21, [x19, 2]
	mov	w3, 65535
	ldrh	w1, [x19, 4]
	orr	w21, w21, w0, lsl 10
	ldrb	w0, [x19, 6]
	sub	w1, w1, #1
	and	w1, w1, 65535
	strh	w1, [x19, 4]
.L268:
	add	w0, w0, 1
	and	w0, w0, 255
	cmp	w2, w0
	bne	.L267
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	w0, 0
.L267:
	add	x4, x19, x0, sxtw 1
	ldrh	w4, [x4, 16]
	cmp	w4, w3
	beq	.L268
	add	x20, x20, :lo12:.LANCHOR0
	strb	w0, [x19, 6]
	ldrh	w2, [x19, 2]
	ldrh	w0, [x20, 106]
	cmp	w2, w0
	bne	.L260
	cbz	w1, .L260
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2799
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L260:
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L266:
	ldrb	w0, [x19, 6]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	cmp	w0, w2
	bne	.L265
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L265:
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
	b	.L264
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
.L276:
	ldrh	w2, [x1, 36]
	cmp	w3, w2
	bcc	.L277
	add	x0, x0, :lo12:.LANCHOR0
	mov	w6, 24
	mov	w5, 4
.L278:
	ldr	w1, [x0, 3288]
	cmp	w2, w1
	bcc	.L279
	ret
.L277:
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
	b	.L276
.L279:
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
	b	.L278
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
.L281:
	cmp	w3, w1
	bcs	.L280
	ubfiz	x4, x3, 5, 16
	mov	w2, 0
	add	x4, x0, x4
	b	.L286
.L282:
	add	w2, w2, 1
	and	w2, w2, 65535
.L286:
	cmp	w2, w7
	bcs	.L283
	umull	x6, w2, w8
	add	x9, x5, x6
	ldr	x10, [x5, x6]
	ldr	x6, [x4, 8]
	cmp	x10, x6
	bne	.L282
	str	wzr, [x9, 16]
.L283:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L281
.L280:
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
.L288:
	cmp	w3, w1
	bcs	.L287
	mov	w2, 0
	b	.L293
.L289:
	add	w2, w2, 1
	and	w2, w2, 65535
.L293:
	cmp	w2, w5
	bcs	.L290
	umaddl	x4, w2, w7, x6
	ldr	w9, [x4, 16]
	cbnz	w9, .L289
	ubfiz	x2, x3, 5, 16
	ldr	x9, [x4]
	add	x2, x0, x2
	str	w8, [x4, 16]
	str	x9, [x2, 8]
	ldr	x4, [x4, 8]
	str	x4, [x2, 16]
.L290:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L288
.L287:
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
.L295:
	cmp	w2, w1, uxth
	bhi	.L297
	mov	w0, 0
	ret
.L297:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L295
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
.L300:
	and	w2, w4, 65535
	cmp	w2, w1
	bcc	.L302
	bne	.L301
	and	x4, x4, 65535
	strh	w0, [x6, x4, lsl 1]
	add	x4, x3, :lo12:.LANCHOR0
	ldrh	w0, [x4, 3292]
	add	w0, w0, 1
	strh	w0, [x4, 3292]
	b	.L301
.L302:
	add	x4, x4, 1
	add	x2, x6, x4, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w9
	bne	.L300
.L301:
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
	ldrh	w5, [x0, 3320]
	cmp	w19, w5
	beq	.L305
	ldrh	w4, [x0, 3322]
	cmp	w19, w4
	beq	.L305
	ldrh	w3, [x0, 3324]
	cmp	w19, w3
	beq	.L305
	ldrh	w2, [x0, 3326]
	cmp	w19, w2
	beq	.L305
	mov	w1, 65535
	cmp	w5, w1
	bne	.L306
	strh	w19, [x0, 3320]
.L305:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L306:
	cmp	w4, w1
	bne	.L307
	strh	w19, [x0, 3322]
	b	.L305
.L307:
	cmp	w3, w1
	bne	.L308
	strh	w19, [x0, 3324]
	b	.L305
.L308:
	cmp	w2, w1
	bne	.L305
	strh	w19, [x0, 3326]
	b	.L305
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
	add	x20, x20, 3336
	mov	w0, w21
	bl	P2V_block_in_plane
	ldrh	w1, [x20, -8]
	mov	w2, w21
	mov	w22, w0
	adrp	x0, .LC74
	add	x0, x0, :lo12:.LC74
	bl	sftl_printk
	mov	w0, w22
	bl	FtlGcRefreshBlock
	ldrh	w0, [x20, -8]
	mov	x1, 0
.L311:
	cmp	w0, w1, uxth
	bhi	.L313
	cmp	w0, 15
	bhi	.L312
	add	x19, x19, :lo12:.LANCHOR0
	add	w1, w0, 1
	add	x19, x19, 3336
	strh	w1, [x19, -8]
	strh	w21, [x19, w0, sxtw 1]
	b	.L312
.L313:
	add	x1, x1, 1
	add	x2, x20, x1, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w21
	bne	.L311
.L312:
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
	ldrh	w1, [x0, 3328]
	cbz	w1, .L316
	ldrh	w3, [x0, 3320]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L316
	ldrh	w2, [x0, 3370]
	cmp	w2, w1
	bcc	.L317
	strh	wzr, [x0, 3370]
.L317:
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x19, 3336
	ldrh	w1, [x19, 3370]
	ldrh	w0, [x0, x1, lsl 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 3370]
	add	w0, w0, 1
	strh	w0, [x19, 3370]
.L316:
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
.L327:
	cmp	w26, w19
	bhi	.L333
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
.L333:
	cbnz	w20, .L328
	mov	w3, w22
	mov	x2, x21
	mov	x1, x27
	mov	x0, x24
	bl	sftl_printk
.L328:
	cmp	w23, 4
	bne	.L329
	ldr	w1, [x21, x19, lsl 2]
	adrp	x0, .LC76
	add	x0, x0, :lo12:.LC76
.L335:
	bl	sftl_printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L332
	mov	w20, 0
	adrp	x0, .LC79
	add	x0, x0, :lo12:.LC79
	bl	sftl_printk
.L332:
	add	x19, x19, 1
	add	w22, w22, w23
	b	.L327
.L329:
	cmp	w23, 2
	bne	.L331
	ldrh	w1, [x21, x19, lsl 1]
	add	x0, x28, :lo12:.LC77
	b	.L335
.L331:
	ldrb	w1, [x21, x19]
	mov	x0, x25
	b	.L335
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
.L337:
	cmp	x19, x23
	bne	.L344
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L344:
	ldr	x0, [x19, 8]
	cbz	x0, .L338
	ldr	x0, [x19, 16]
	cbnz	x0, .L339
.L338:
	mov	w2, 101
	mov	x1, x20
	mov	x0, x21
	bl	sftl_printk
.L339:
	ldr	w0, [x19, 4]
	add	x26, x22, :lo12:.LANCHOR0
	add	x2, x29, 88
	add	x1, x29, 92
	bl	l2p_addr_tran.isra.0
	ldrb	w0, [x29, 88]
	ldr	w1, [x29, 92]
	ldp	x2, x3, [x19, 8]
	ldr	x4, [x26, 3400]
	blr	x4
	str	w0, [x19]
	ldrh	w0, [x26, 14]
	cmp	w0, 4
	bne	.L341
	ldrb	w0, [x29, 88]
	ldr	x4, [x26, 3400]
	ldp	x2, x3, [x19, 8]
	ldr	w1, [x29, 92]
	add	x3, x3, 8
	add	x2, x2, 2048
	add	w1, w25, w1
	blr	x4
	cmn	w0, #1
	beq	.L342
	ldr	x0, [x19, 16]
	ldr	w1, [x0, 12]
	cmn	w1, #1
	bne	.L341
	ldr	w1, [x0, 8]
	cmn	w1, #1
	bne	.L341
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L341
.L342:
	mov	w0, -1
	str	w0, [x19]
.L341:
	add	x19, x19, 32
	b	.L337
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
	add	x23, x0, 3408
	mov	x25, x23
	mov	w26, 61664
	ldr	x1, [x0, 3440]
	ldr	x24, [x0, 3448]
	stp	x1, x24, [x23, 8]
.L356:
	ldrh	w0, [x19, 58]
	cmp	w21, w0
	bcc	.L361
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L361:
	ldrh	w20, [x19, 102]
	mov	w0, -1
	strh	w0, [x22]
.L358:
	ldrh	w0, [x19, 102]
	sub	w20, w20, #1
	and	w20, w20, 65535
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L359
	madd	w0, w0, w21, w20
	mov	w2, 1
	mov	w1, w2
	lsl	w0, w0, 10
	str	w0, [x25, 4]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L358
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L358
	strh	w20, [x22]
.L359:
	add	w21, w21, 1
	add	x22, x22, 2
	b	.L356
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
	bne	.L367
	ldrh	w19, [x1, 108]
.L368:
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
	bne	.L369
	mov	w22, 0
	mov	w24, 2
.L370:
	cmp	w22, w19
	ble	.L373
.L369:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 160
	ret
.L367:
	ldrh	w19, [x1, 106]
	b	.L368
.L373:
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
	bne	.L371
	ldr	w0, [x29, 100]
	cmn	w0, #1
	bne	.L371
	ldr	w0, [x29, 64]
	cmn	w0, #1
	beq	.L371
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L370
.L371:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L370
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
.L379:
	cmp	x19, x24
	bne	.L387
	cbnz	w26, .L388
.L406:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L387:
	ldr	x0, [x19, 8]
	cbz	x0, .L380
	ldr	x0, [x19, 16]
	cbnz	x0, .L381
.L380:
	mov	w2, 131
	mov	x1, x22
	mov	x0, x23
	bl	sftl_printk
.L381:
	ldr	w0, [x19, 4]
	add	x2, x29, 104
	add	x1, x29, 108
	bl	l2p_addr_tran.isra.0
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29, 108]
	ldp	x2, x3, [x19, 8]
	ldr	x4, [x0, 3392]
	ldrb	w0, [x29, 104]
	blr	x4
	cbnz	w0, .L382
	str	wzr, [x19]
.L383:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0, 14]
	cmp	w1, 4
	bne	.L385
	ldr	x4, [x0, 3392]
	ldrb	w0, [x29, 104]
	ldp	x2, x3, [x19, 8]
	ldr	w1, [x29, 108]
	add	x3, x3, 8
	add	x2, x2, 2048
	add	w1, w27, w1
	blr	x4
	cbz	w0, .L385
	mov	w0, -1
	str	w0, [x19]
.L385:
	add	x19, x19, 32
	b	.L379
.L382:
	mov	w0, -1
	str	w0, [x19]
	b	.L383
.L392:
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
	bne	.L389
	ldr	w1, [x20, 4]
	adrp	x0, .LC80
	add	x0, x0, :lo12:.LC80
	bl	sftl_printk
	str	w24, [x20]
.L389:
	ldr	x0, [x20, 16]
	cbz	x0, .L390
	ldr	w2, [x0]
	ldr	w3, [x21]
	cmp	w2, w3
	beq	.L390
	ldr	w1, [x20, 4]
	adrp	x0, .LC81
	add	x0, x0, :lo12:.LC81
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x20]
.L390:
	ldr	x0, [x20, 8]
	cbz	x0, .L391
	ldr	w3, [x22, #:lo12:check_buf]
	ldr	w2, [x0]
	cmp	w2, w3
	beq	.L391
	ldr	w1, [x20, 4]
	adrp	x0, .LC82
	add	x0, x0, :lo12:.LC82
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x20]
.L391:
	add	x20, x20, 32
.L407:
	cmp	x20, x19
	bne	.L392
	b	.L406
.L388:
	add	x21, x21, :lo12:.LANCHOR0
	adrp	x22, check_buf
	add	x21, x21, 3456
	add	x23, x22, :lo12:check_buf
	b	.L407
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
.L410:
	cmp	x20, x22
	bne	.L416
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L416:
	ldr	w0, [x20]
	add	x2, x29, 72
	add	x1, x29, 76
	bl	l2p_addr_tran.isra.0
	ldrb	w0, [x29, 72]
	ldr	w1, [x29, 76]
	ldr	x2, [x19, 3384]
	blr	x2
	cmp	w0, 0
	csel	w0, w21, wzr, ne
	str	w0, [x20, -4]
	ldrh	w0, [x19, 14]
	cmp	w0, 4
	bne	.L414
	ldrb	w0, [x29, 72]
	ldr	x2, [x19, 3384]
	ldr	w1, [x29, 76]
	add	w1, w23, w1
	blr	x2
	cbz	w0, .L414
	str	w21, [x20, -4]
.L414:
	add	x20, x20, 32
	b	.L410
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
	bcc	.L421
	adrp	x0, .LANCHOR0
	add	x20, x0, :lo12:.LANCHOR0
	mov	x19, x0
	ldrh	w2, [x20, 262]
	cmp	w2, 1024
	beq	.L421
	and	w1, w1, 65535
	cbz	w1, .L423
	mov	w0, w21
	bl	P2V_block_in_plane
	and	w22, w0, 65535
	ldr	x0, [x20, 3968]
	lsl	w1, w21, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x20, 3968]
	bl	FlashEraseBlocks
	ldr	x2, [x20, 2384]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x20, 2640]
	add	w0, w0, 1
	str	w0, [x20, 2640]
.L423:
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
.L421:
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
	str	w24, [x5, 3976]
.L432:
	ldrh	w0, [x5, 36]
	cmp	w0, w10
	bhi	.L436
	cbz	w21, .L431
	ldr	x0, [x5, 3968]
	mov	w2, w21
	mov	w1, 0
	ubfiz	x21, x21, 5, 16
	mov	x22, 0
	add	x25, x20, :lo12:.LANCHOR0
	bl	FlashEraseBlocks
.L440:
	ldr	x0, [x25, 3968]
	add	x1, x0, x22
	ldr	w0, [x0, x22]
	cmn	w0, #1
	bne	.L439
	ldr	w0, [x1, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.L439:
	add	x22, x22, 32
	cmp	x22, x21
	bne	.L440
	cbz	w23, .L453
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 1
	ldrh	w26, [x0, 108]
	lsr	w0, w26, 2
	str	w0, [x29, 124]
.L441:
	add	x27, x20, :lo12:.LANCHOR0
	mov	w22, 0
	add	x0, x27, 64
	str	x0, [x29, 112]
.L449:
	mov	w5, 0
	mov	w21, 0
.L442:
	ldrh	w0, [x27, 36]
	cmp	w0, w5
	bhi	.L445
	cbz	w21, .L431
	ldr	x0, [x27, 3968]
	mov	w1, w21
	mov	w3, 1
	mov	w2, w25
	mov	x28, 0
	bl	FlashProgPages
	ubfiz	x1, x21, 5, 16
.L448:
	ldr	x0, [x27, 3968]
	add	x3, x0, x28
	ldr	w0, [x0, x28]
	cbz	w0, .L447
	ldr	w0, [x3, 4]
	add	w19, w19, 1
	str	x1, [x29, 104]
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29, 104]
.L447:
	add	x28, x28, 32
	cmp	x1, x28
	bne	.L448
	ldr	w0, [x29, 124]
	add	w22, w22, w0
	and	w22, w22, 65535
	cmp	w26, w22
	bhi	.L449
	add	x26, x20, :lo12:.LANCHOR0
	mov	x22, 0
.L451:
	cbz	w23, .L450
	ldr	x0, [x26, 3968]
	add	x1, x0, x22
	ldr	w0, [x0, x22]
	cbnz	w0, .L450
	ldr	w0, [x1, 4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L450:
	add	x22, x22, 32
	cmp	x22, x28
	bne	.L451
	cmp	w24, 63
	ccmp	w23, 0, 0, hi
	beq	.L431
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	mov	w1, w25
	ldr	x0, [x20, 3968]
	bl	FlashEraseBlocks
.L431:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L436:
	ldr	x1, [x5, 3968]
	ubfiz	x0, x10, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w24
	ldrb	w0, [x8, w10, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w23, .L433
	bl	IsBlkInVendorPart
	cbnz	w0, .L434
.L433:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L435
	ldr	x0, [x5, 3968]
	ubfiz	x2, x21, 5, 16
	lsl	w6, w6, 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	ldr	x1, [x5, 3968]
	ldr	x0, [x5, 3984]
	add	x1, x1, x2
	ldr	x2, [x5, 3992]
	str	x0, [x1, 8]
	ldrh	w0, [x5, 116]
	mul	w0, w0, w21
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w0, w0, w9
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L434:
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L432
.L435:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L434
.L453:
	mov	w0, 6
	mov	w25, 0
	mov	w26, 1
	str	w0, [x29, 124]
	b	.L441
.L445:
	ldr	x1, [x27, 3968]
	ubfiz	x0, x5, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w24
	ldr	x0, [x29, 112]
	ldrb	w0, [x0, w5, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w23, .L443
	bl	IsBlkInVendorPart
	cbnz	w0, .L444
.L443:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L444
	ldr	x0, [x27, 3968]
	ubfiz	x2, x21, 5, 16
	add	w6, w22, w6, lsl 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	ldr	x1, [x27, 3968]
	ldr	x0, [x27, 4000]
	add	x1, x1, x2
	mov	w2, 4
	str	x0, [x1, 8]
	ldrh	w0, [x27, 116]
	mul	w0, w0, w21
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w0, w0, w2
	ldr	x2, [x27, 3984]
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L444:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L442
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
.L475:
	ldrh	w1, [x20, 6]
	cbz	w1, .L476
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
	ldr	x0, [x19, 3968]
	lsl	w1, w21, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 3968]
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
	bcc	.L477
	mov	w0, w21
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L476:
	adrp	x0, .LC83
	mov	w1, 0
	add	x0, x0, :lo12:.LC83
	bl	sftl_printk
.L479:
	b	.L479
.L477:
	ldrh	w2, [x20, 6]
	mov	w1, w21
	mov	x0, x23
	bl	sftl_printk
	b	.L475
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
.L483:
	cmp	w20, w1
	beq	.L487
	mov	x21, x0
	ldrh	w2, [x0], 2
	cbnz	w2, .L484
	bl	FtlFreeSysBlkQueueOut
	and	w1, w0, 65535
	strh	w0, [x21]
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L485
	adrp	x0, .LANCHOR0+262
	ldrh	w2, [x0, #:lo12:.LANCHOR0+262]
	adrp	x0, .LC85
	add	x0, x0, :lo12:.LC85
	bl	sftl_printk
.L486:
	b	.L486
.L485:
	ldr	w0, [x19, 48]
	strh	wzr, [x19, 2]
	add	w0, w0, 1
	str	w0, [x19, 48]
	ldrh	w0, [x19, 8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19, 8]
.L487:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L489
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 603
	add	x1, x1, 192
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L489:
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L484:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L483
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
	stp	x29, x30, [sp, -64]!
	mov	w0, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x19, 2560
	add	x2, x19, 2560
	strh	wzr, [x19, 3240]
	stp	wzr, wzr, [x1, 28]
	stp	wzr, wzr, [x1, 36]
	str	wzr, [x19, 2604]
	str	wzr, [x19, 2608]
	stp	wzr, wzr, [x2, 60]
	stp	wzr, wzr, [x2, 68]
	str	w0, [x19, 4012]
	mov	w0, -1
	strh	w0, [x19, 3320]
	strh	w0, [x19, 3322]
	mov	w0, 32
	str	x23, [sp, 48]
	strh	w0, [x19, 3232]
	mov	w0, 128
	str	wzr, [x19, 2640]
	strh	w0, [x19, 3234]
	str	wzr, [x19, 2644]
	str	wzr, [x19, 2648]
	str	wzr, [x19, 3236]
	str	wzr, [x19, 3244]
	str	wzr, [x19, 3976]
	str	wzr, [x19, 4008]
	str	wzr, [x19, 4016]
	strh	wzr, [x19, 3328]
	ldrh	w0, [x19, 110]
	strh	wzr, [x19, 3370]
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
	str	x0, [x19, 4024]
	mov	w0, w22
	bl	ftl_malloc
	str	x0, [x19, 4032]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4040]
	mov	w0, w22
	bl	ftl_malloc
	str	x0, [x19, 3968]
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
	str	x0, [x19, 3440]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4048]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4056]
	ldr	w0, [x19, 3288]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19, 3264]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4000]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3984]
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
	str	x0, [x19, 3448]
	lsl	w0, w21, 2
	bl	ftl_malloc
	str	x0, [x19, 3992]
	ldrh	w1, [x19, 116]
	ldr	w0, [x19, 3288]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3272]
	ldrh	w0, [x19, 46]
	ubfiz	w0, w0, 1, 15
	strh	w0, [x19, 4064]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19, 4072]
	ldrh	w0, [x19, 4064]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19, 4064]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w21, [x19, 46]
	str	x0, [x19, 4080]
	add	x0, x0, 32
	str	x0, [x19, 2384]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 4088]
	mov	w0, w21
	bl	ftl_malloc
	ldr	w23, [x19, 132]
	adrp	x21, .LANCHOR3
	add	x22, x21, :lo12:.LANCHOR3
	str	x0, [x19, 2376]
	lsl	w23, w23, 1
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x22, -256]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x22, -248]
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
	str	x0, [x22, -240]
	ldrh	w0, [x19, 124]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x22, -232]
	ldrh	w0, [x19, 126]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w2, [x19, 126]
	mov	w1, 0
	str	x0, [x22, -224]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x19, 140]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x22, -216]
	ldr	w0, [x19, 132]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x22, -208]
	ldrh	w0, [x19, 142]
	lsl	w0, w0, 4
	bl	ftl_malloc
	ldrh	w1, [x19, 142]
	str	x0, [x19, 2568]
	ldrh	w0, [x19, 114]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x22, -200]
	ldrh	w1, [x19, 46]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 2360]
	ldrh	w0, [x19, 102]
	ldrh	w1, [x19, 58]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x22, -192]
	mul	w0, w1, w0
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w4, [x22, -192]
	add	x5, x19, 200
	ldrh	w6, [x19, 58]
	mov	x1, 1
	str	x0, [x19, 192]
	mov	w2, w4
	mov	x0, x20
.L494:
	cmp	w1, w6
	bcc	.L495
	mov	w2, 8
	add	x4, x0, :lo12:.LANCHOR0
	sub	w2, w2, w1
	add	x4, x4, 160
	add	x2, x2, 1
	mov	x3, 0
.L496:
	add	x3, x3, 1
	cmp	x3, x2
	bne	.L497
	add	x1, x21, :lo12:.LANCHOR3
	ldr	x2, [x1, -256]
	cbnz	x2, .L498
.L500:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 224
	adrp	x0, .LC86
	add	x0, x0, :lo12:.LC86
	bl	sftl_printk
	mov	w0, -1
.L493:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L495:
	ldr	x3, [x19, 192]
	add	w1, w1, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x5], 8
	b	.L494
.L497:
	add	x5, x1, x3
	add	x5, x4, x5, lsl 3
	str	xzr, [x5, 24]
	b	.L496
.L498:
	ldr	x2, [x1, -248]
	cbz	x2, .L500
	ldr	x2, [x1, -216]
	cbz	x2, .L500
	ldr	x2, [x1, -208]
	cbz	x2, .L500
	add	x2, x0, :lo12:.LANCHOR0
	ldr	x3, [x2, 2568]
	cbz	x3, .L500
	ldr	x1, [x1, -200]
	cbz	x1, .L500
	ldr	x1, [x2, 2360]
	cbz	x1, .L500
	ldr	x1, [x2, 192]
	cbz	x1, .L500
	ldr	x1, [x2, 2376]
	cbz	x1, .L500
	mov	x1, x2
	ldr	x2, [x2, 3296]
	cbz	x2, .L500
	ldr	x2, [x1, 3312]
	cbz	x2, .L500
	ldr	x2, [x1, 4024]
	cbz	x2, .L500
	ldr	x2, [x1, 4040]
	cbz	x2, .L500
	ldr	x2, [x1, 3968]
	cbz	x2, .L500
	ldr	x2, [x1, 3280]
	cbz	x2, .L500
	ldr	x2, [x1, 4032]
	cbz	x2, .L500
	ldr	x2, [x1, 3440]
	cbz	x2, .L500
	ldr	x2, [x1, 4048]
	cbz	x2, .L500
	ldr	x1, [x1, 4056]
	cbz	x1, .L500
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3264]
	cbz	x1, .L500
	ldr	x1, [x0, 4000]
	cbz	x1, .L500
	ldr	x1, [x0, 3984]
	cbz	x1, .L500
	ldr	x1, [x0, 3256]
	cbz	x1, .L500
	ldr	x1, [x0, 3448]
	cbz	x1, .L500
	ldr	x1, [x0, 3992]
	cbz	x1, .L500
	ldr	x1, [x0, 3272]
	cbz	x1, .L500
	ldr	x1, [x0, 2384]
	cbz	x1, .L500
	ldr	x1, [x0, 4072]
	cbz	x1, .L500
	ldr	x0, [x0, 152]
	cbz	x0, .L500
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x1, [x0, -240]
	cbz	x1, .L500
	ldr	x1, [x0, -232]
	cbz	x1, .L500
	ldr	x0, [x0, -224]
	cbz	x0, .L500
	mov	w0, 0
	b	.L493
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	adrp	x0, .LANCHOR3-192
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	ldrh	w2, [x0, #:lo12:.LANCHOR3-192]
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
.L598:
	ldrh	w0, [x22, x19]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L595
	ldrh	w1, [x23, 102]
	cmp	w1, w0
	bhi	.L597
	adrp	x0, .LC1
	mov	w2, 74
	mov	x1, x20
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L597:
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
	bne	.L598
.L595:
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
.L607:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L611
	ldrh	w26, [x21]
	adrp	x24, .LANCHOR0
	add	x24, x24, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w20, 0
.L612:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L616
	mov	w0, w25
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L611:
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20, x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L608:
	ldrh	w3, [x19, 10]
	cmp	w3, w1
	bhi	.L610
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L607
.L610:
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x23, x3]
	cmp	w4, w2
	bne	.L609
	ldrh	w4, [x21, x3]
	add	w4, w4, 1
	strh	w4, [x21, x3]
.L609:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L608
.L616:
	ldrh	w0, [x19]
	uxtw	x22, w20
	cmp	w0, w20
	bne	.L613
	ldrh	w0, [x24, 108]
	ldrh	w1, [x19, 2]
	cmp	w1, w0
	bcs	.L613
	strh	w0, [x21, x22, lsl 1]
.L613:
	lsl	x22, x22, 1
	ldrh	w0, [x21, x22]
	cmp	w26, w0
	bls	.L614
	mov	w25, w20
	mov	w26, w0
.L614:
	cbnz	w0, .L615
	ldrh	w0, [x23, x22]
	cbz	w0, .L615
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x23, x22]
	ldrh	w0, [x19, 8]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
.L615:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L612
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
	bne	.L622
	ldrh	w0, [x19, 8]
	cbz	w0, .L623
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 677
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L623:
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
.L624:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L622:
	ubfiz	x0, x0, 1, 16
	adrp	x21, .LANCHOR0
	ldr	w1, [x19, 48]
	ldr	x23, [x19, 40]
	ldrh	w22, [x20, x0]
	add	x20, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2]
	orr	w0, w0, w22, lsl 10
	str	w0, [x20, 3412]
	ldr	x0, [x20, 3440]
	str	x0, [x20, 3416]
	ldr	x0, [x20, 3448]
	str	x0, [x20, 3424]
	str	w1, [x0, 4]
	mov	w1, -1291
	strh	w1, [x0, 8]
	ldrh	w1, [x19, 4]
	strh	w1, [x0]
	strh	w22, [x0, 2]
	mov	w1, 255
	ldrh	w2, [x20, 108]
	ldr	x0, [x20, 3440]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, 0
	mov	w1, 0
.L625:
	ldrh	w2, [x19, 6]
	cmp	w2, w1
	bhi	.L627
	add	x0, x21, :lo12:.LANCHOR0
	mov	w2, 1
	mov	w3, 0
	mov	w1, w2
	add	x0, x0, 3408
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	x0, x19
	bl	ftl_map_blk_gc
	b	.L624
.L627:
	ubfiz	x2, x1, 2, 16
	ldr	w3, [x23, x2]
	cmp	w22, w3, lsr 10
	bne	.L626
	add	w0, w0, 1
	ldr	x4, [x20, 3440]
	and	w0, w0, 65535
	ubfiz	x3, x0, 3, 16
	str	w1, [x4, x3]
	ldr	w4, [x23, x2]
	ldr	x2, [x20, 3440]
	add	x2, x2, x3
	str	w4, [x2, 4]
.L626:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L625
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x24, .LANCHOR0
	stp	x25, x26, [sp, 64]
	add	x25, x24, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	adrp	x23, .LANCHOR1
	stp	x21, x22, [sp, 32]
	mov	x19, x0
	stp	x27, x28, [sp, 80]
	mov	w26, w1
	mov	x27, x2
	mov	x21, x24
	add	x23, x23, :lo12:.LANCHOR1
	add	x20, x25, 3408
	mov	w22, 0
.L633:
	add	x0, x24, :lo12:.LANCHOR0
	ldr	w1, [x0, 2604]
	add	w1, w1, 1
	str	w1, [x0, 2604]
	ldrh	w0, [x0, 108]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L634
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L635
.L634:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L635:
	ldrh	w1, [x19]
	ldr	x0, [x19, 16]
	ldrh	w0, [x0, x1, lsl 1]
	cbnz	w0, .L636
	adrp	x0, .LC1
	mov	w2, 735
	add	x1, x23, 288
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L636:
	ldrh	w1, [x19]
	ldrh	w0, [x19, 10]
	cmp	w1, w0
	bcc	.L637
	adrp	x0, .LC1
	mov	w2, 736
	add	x1, x23, 288
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L637:
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19, 16]
	ldrh	w28, [x0, x1, lsl 1]
	mov	w1, 0
	ldrh	w0, [x19, 2]
	str	x27, [x20, 8]
	orr	w0, w0, w28, lsl 10
	str	w0, [x20, 4]
	ldr	x0, [x25, 3448]
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
	bne	.L638
	ldr	w1, [x20, 4]
	adrp	x0, .LC87
	add	x0, x0, :lo12:.LC87
	add	w22, w22, 1
	and	w22, w22, 65535
	bl	sftl_printk
	ldrh	w0, [x19, 2]
	cmp	w0, 2
	bhi	.L639
	ldrh	w0, [x25, 108]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L639:
	cmp	w22, 3
	bls	.L640
	add	x21, x21, :lo12:.LANCHOR0
	adrp	x0, .LC88
	mov	w2, w22
	add	x0, x0, :lo12:.LC88
	ldr	w1, [x21, 3412]
	bl	sftl_printk
.L641:
	b	.L641
.L640:
	ldr	w0, [x19, 52]
	cbz	w0, .L633
.L658:
	b	.L658
.L638:
	cbz	w1, .L644
	strh	w28, [x19, 60]
	cmp	w0, 1
	beq	.L633
	cmp	w1, 256
.L661:
	beq	.L633
	ldr	x0, [x19, 40]
	add	x21, x21, :lo12:.LANCHOR0
	ldr	w1, [x21, 3412]
	str	w1, [x0, w26, uxtw 2]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L644:
	cmp	w0, 1
	b	.L661
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	and	w21, w0, 65535
	add	x0, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	and	x20, x1, 65535
	ldrh	w0, [x0, 140]
	cmp	w0, w21
	bcs	.L663
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 510
	add	x1, x1, 304
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L663:
	adrp	x22, .LANCHOR3
	add	x26, x22, :lo12:.LANCHOR3
	ubfiz	x0, x21, 2, 16
	ldr	x1, [x26, -216]
	ldr	w23, [x1, x0]
	cbnz	w23, .L664
	add	x19, x19, :lo12:.LANCHOR0
	lsl	x20, x20, 4
	mov	w1, 255
	ldr	x0, [x19, 2568]
	ldrh	w2, [x19, 114]
	add	x0, x0, x20
	ldr	x0, [x0, 8]
	bl	ftl_memset
	ldr	x0, [x19, 2568]
	strh	w21, [x0, x20]
	ldr	x0, [x19, 2568]
	add	x20, x0, x20
	str	wzr, [x20, 4]
.L665:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L664:
	add	x24, x19, :lo12:.LANCHOR0
	lsl	x20, x20, 4
	add	x25, x24, 3408
	mov	w2, 1
	mov	w1, w2
	ldr	x0, [x24, 2568]
	str	w23, [x24, 3412]
	add	x0, x0, x20
	ldr	x0, [x0, 8]
	str	x0, [x24, 3416]
	ldr	x0, [x24, 3448]
	str	x0, [x24, 3424]
	mov	x0, x25
	bl	FlashReadPages
	ldr	x25, [x24, 3424]
	ldr	w0, [x24, 3408]
	cmp	w0, 256
	bne	.L666
	mov	w2, w23
	mov	w1, w21
	adrp	x0, .LC89
	add	x0, x0, :lo12:.LC89
	bl	sftl_printk
	lsr	w0, w23, 10
	strh	w0, [x26, -124]
	ldr	x0, [x24, 2568]
	mov	w1, w21
	add	x0, x0, x20
	ldr	x2, [x0, 8]
	sub	x0, x26, #184
	bl	FtlMapWritePage
.L666:
	ldrh	w0, [x25, 8]
	cmp	w0, w21
	beq	.L667
	mov	w2, w23
	add	x23, x19, :lo12:.LANCHOR0
	mov	w1, w21
	adrp	x0, .LC90
	add	x0, x0, :lo12:.LC90
	bl	sftl_printk
	ldr	x1, [x23, 3424]
	add	x22, x22, :lo12:.LANCHOR3
	mov	w3, 4
	adrp	x0, .LC91
	mov	w2, w3
	add	x0, x0, :lo12:.LC91
	bl	rknand_print_hex
	ldrh	w3, [x23, 140]
	adrp	x0, .LC92
	ldr	x1, [x22, -216]
	mov	w2, 4
	add	x0, x0, :lo12:.LC92
	bl	rknand_print_hex
.L667:
	ldrh	w0, [x25, 8]
	cmp	w0, w21
	beq	.L668
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 535
	add	x1, x1, 304
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L668:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19, 2568]
	add	x0, x0, x20
	str	wzr, [x0, 4]
	ldr	x0, [x19, 2568]
	strh	w21, [x0, x20]
	b	.L665
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
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x20, [x0, 16]
	ldr	x22, [x0, 40]
	bl	ftl_free_no_use_map_blk
	ldrh	w4, [x19, 10]
	ldrh	w2, [x19, 8]
	sub	w1, w4, #4
	cmp	w2, w1
	ldrh	w1, [x19, 60]
	bge	.L671
	mov	w2, 65535
	cmp	w1, w2
	beq	.L673
	add	x2, x21, :lo12:.LANCHOR0
	ldrh	w3, [x19, 2]
	ldrh	w2, [x2, 108]
	cmp	w3, w2
	bcc	.L674
.L671:
	mov	w2, 65535
	and	w0, w0, 65535
	cmp	w1, w2
	beq	.L675
	add	x2, x21, :lo12:.LANCHOR0
	ldrh	w3, [x19, 2]
	ldrh	w2, [x2, 108]
	cmp	w2, w3
	bls	.L676
.L675:
	ubfiz	x0, x0, 1, 32
	ldrh	w26, [x20, x0]
	cbz	w26, .L673
	ldr	w1, [x19, 52]
	cbnz	w1, .L673
	mov	w1, 1
	str	w1, [x19, 52]
	strh	wzr, [x20, x0]
	ldrh	w0, [x19, 8]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0, 108]
	cmp	w1, w0
	bcc	.L681
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L681:
	add	x27, x21, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR1
	add	x24, x24, :lo12:.LANCHOR1
	add	x23, x27, 3408
	add	x24, x24, 320
	mov	w20, 0
.L682:
	ldrh	w0, [x19, 6]
	cmp	w0, w20
	bhi	.L689
	mov	w1, 1
	mov	w0, w26
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19, 52]
.L673:
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x21, 108]
	cmp	w1, w0
	bcc	.L674
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L674:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L679:
	ldrh	w6, [x20, x2, lsl 1]
	add	x5, x2, 1
	cmp	w6, w1
	beq	.L677
	mov	x2, x5
.L678:
	and	w23, w2, 65535
	cmp	w23, w4
	bcc	.L679
	mov	w23, w0
.L677:
	and	x2, x2, 65535
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	ldrh	w2, [x20, x2, lsl 1]
	bl	sftl_printk
	mov	w0, -1
	strh	w0, [x19, 60]
	mov	w0, w23
	b	.L675
.L676:
	mov	x2, 0
	b	.L678
.L689:
	ubfiz	x0, x20, 2, 16
	add	x28, x22, x0
	ldr	w1, [x22, x0]
	cmp	w26, w1, lsr 10
	bne	.L683
	ldr	x25, [x27, 3448]
	mov	w2, 1
	ldr	x1, [x27, 4048]
	stp	x1, x25, [x23, 8]
	mov	w1, w2
	ldr	w0, [x22, x0]
	str	w0, [x23, 4]
	mov	x0, x23
	bl	FlashReadPages
	ldrh	w0, [x25, 8]
	cmp	w0, w20
	beq	.L684
	adrp	x0, .LC1
	mov	w2, 647
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L684:
	ldr	w0, [x23]
	cmn	w0, #1
	bne	.L685
.L687:
	str	wzr, [x28]
.L686:
	b	.L686
.L685:
	ldrh	w0, [x25, 8]
	cmp	w0, w20
	bne	.L687
	ldrh	w1, [x25]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L687
	ldr	x2, [x23, 8]
	mov	w1, w20
	mov	x0, x19
	bl	FtlMapWritePage
.L683:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L682
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
	sub	x0, x0, #184
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
	bcc	.L698
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 847
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L698:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x0, 142]
	ldr	x2, [x0, 2568]
	mov	x0, 0
.L699:
	and	x20, x0, 65535
	cmp	w20, w1
	bcc	.L704
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
	beq	.L705
	ldr	w1, [x3, 4]
	tbz	w1, #31, .L705
	bl	flush_l2p_region
.L705:
	mov	w1, w25
	mov	w0, w21
	bl	load_l2p_region
	b	.L700
.L704:
	add	x0, x0, 1
	add	x3, x2, x0, lsl 4
	ldrh	w3, [x3, -16]
	cmp	w3, w21
	bne	.L699
.L700:
	cbnz	w23, .L701
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x0, [x0, 2568]
	add	x0, x0, x20, lsl 4
	ldr	x0, [x0, 8]
	ldr	w0, [x0, x19, lsl 2]
	str	w0, [x22]
.L702:
	add	x24, x24, :lo12:.LANCHOR0
	ldr	x0, [x24, 2568]
	add	x20, x0, x20, lsl 4
	ldr	w0, [x20, 4]
	cmn	w0, #1
	beq	.L707
	add	w0, w0, 1
	str	w0, [x20, 4]
.L707:
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L701:
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
	b	.L702
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
	cbnz	w1, .L713
	ldr	x19, [x2, 2408]
	cbz	x19, .L714
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
.L715:
	cmp	w1, w3
	beq	.L714
	cmp	w19, w0
	bne	.L716
	add	x23, x22, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x23, 2408
	bl	List_remove_node
	ldrh	w0, [x23, 2416]
	cbnz	w0, .L717
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1737
	add	x1, x1, 352
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L717:
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
.L714:
	add	x1, x29, 76
	mov	w2, 1
	mov	w0, w21
	bl	log2phys
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L716:
	umull	x19, w19, w4
	ldrh	w19, [x2, x19]
	cmp	w19, w5
	beq	.L714
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L715
.L713:
	add	w1, w1, 1
	strh	w1, [x3, x20]
	b	.L714
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	sub	sp, sp, #96
	adrp	x0, .LC94
	mov	w1, 0
	add	x0, x0, :lo12:.LC94
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x21, x22, [sp, 48]
	adrp	x22, .LANCHOR0
	add	x21, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 32]
	str	x23, [sp, 64]
	mov	w20, 0
	add	x19, x21, 3408
	bl	sftl_printk
.L724:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0, 2584]
	cmp	w20, w0
	bcc	.L730
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x29, x30, [sp, 16]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	ret
.L730:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w20
	bl	log2phys
	tst	x20, 2047
	bne	.L725
	ldr	w2, [x29, 76]
	adrp	x0, .LC95
	mov	w1, w20
	add	x0, x0, :lo12:.LC95
	bl	sftl_printk
.L725:
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L727
	ldr	x23, [x21, 3448]
	mov	w2, 0
	stp	wzr, w0, [x19]
	mov	w1, 1
	ldr	x0, [x21, 3440]
	str	x0, [x19, 8]
	mov	x0, x19
	str	x23, [x19, 16]
	str	w20, [x19, 24]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmp	w0, 256
	ccmn	w0, #1, 4, ne
	beq	.L728
	ldr	w0, [x23, 8]
	cmp	w20, w0
	beq	.L727
.L728:
	ldp	x1, x0, [x19, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC96
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC96
	ldr	w2, [x19, 4]
	mov	w1, w20
	bl	sftl_printk
.L727:
	add	w20, w20, 1
	b	.L724
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x2, x1, 2720
	ldr	w3, [x2, 80]
	cbz	w3, .L733
	ldr	w0, [x1, 2584]
	ldr	w3, [x2, 84]
	cmp	w3, w0
	bcs	.L734
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x1
	mov	w20, 2048
	str	x21, [sp, 32]
.L739:
	ldr	w1, [x19, 2584]
	ldr	w0, [x19, 2804]
	cmp	w0, w1
	bcs	.L736
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x19, 2804]
	ldr	w1, [x29, 60]
	add	w0, w0, 1
	str	w0, [x19, 2804]
	cmn	w1, #1
	beq	.L737
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
	bne	.L736
	ldr	w0, [x29, 60]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L736:
	ldp	x19, x20, [sp, 16]
	mov	w0, -1
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 160
	ret
.L737:
	subs	w20, w20, #1
	bne	.L739
	b	.L736
.L734:
	ldr	w0, [x1, 2588]
	stp	w0, wzr, [x2, 76]
	str	wzr, [x2, 84]
.L743:
	mov	w0, 0
	ret
.L733:
	ldr	w3, [x1, 2588]
	ldr	w5, [x2, 76]
	add	w2, w3, 1048576
	cmp	w5, w2
	bhi	.L742
	ldr	w2, [x1, 2644]
	lsr	w4, w2, 10
	mov	w2, 33554432
	asr	w2, w2, w4
	add	w2, w2, w5
	cmp	w3, w2
	bhi	.L742
	ldrb	w1, [x1, 2340]
	cbnz	w1, .L743
.L742:
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 1
	str	w3, [x0, 2796]
	str	w1, [x0, 2800]
	str	wzr, [x0, 2804]
	b	.L743
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlMapBlkWriteDump_data
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	ldr	w1, [x0, 56]
	cbz	w1, .L757
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x2, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	add	x0, x2, 3408
	mov	x21, x1
	ldrh	w20, [x19, 6]
	ldr	x4, [x2, 3448]
	sub	w20, w20, #1
	ldr	x3, [x2, 4048]
	and	w20, w20, 65535
	ldr	x5, [x19, 40]
	stp	x3, x4, [x0, 8]
	ubfiz	x4, x20, 2, 16
	str	wzr, [x19, 56]
	ldr	w4, [x5, x4]
	str	w4, [x0, 4]
	cbz	w4, .L751
	mov	w2, 1
	mov	w1, w2
	bl	FlashReadPages
.L752:
	add	x1, x21, :lo12:.LANCHOR0
	mov	x0, x19
	ldr	x2, [x1, 3416]
	mov	w1, w20
	bl	FtlMapWritePage
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L751:
	ldrh	w2, [x2, 114]
	mov	w1, 255
	mov	x0, x3
	bl	ftl_memset
	b	.L752
.L757:
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
	strh	wzr, [x21, -120]
	add	x25, x25, :lo12:.LANCHOR1
	ldr	x0, [x21, -208]
	add	x25, x25, 368
	ldr	w2, [x19, 132]
	strh	wzr, [x19, 148]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x21, -256]
	mov	w1, 0
	ldr	w2, [x19, 132]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x21, -232]
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
.L761:
	ldrh	w0, [x19, 46]
	cmp	w0, w24
	bls	.L801
	ldrh	w9, [x19, 36]
	mov	x5, 0
	ldrh	w8, [x19, 116]
	mov	w21, 0
	add	x7, x19, 64
	mov	w6, 4
	b	.L802
.L763:
	ldrb	w0, [x7, x5]
	mov	w1, w24
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L762
	ldr	x0, [x19, 4024]
	ubfiz	x2, x21, 5, 16
	lsl	w4, w4, 10
	add	x0, x0, x2
	str	w4, [x0, 4]
	ldr	x1, [x19, 4024]
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
.L762:
	add	x5, x5, 1
.L802:
	cmp	w9, w5, uxth
	bhi	.L763
	cbnz	w21, .L764
.L800:
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L761
.L764:
	ldr	x0, [x19, 4024]
	mov	w1, w21
	mov	w2, 1
	ubfiz	x21, x21, 5, 16
	mov	x23, 0
	add	x28, x19, 2656
	bl	FlashReadPages
.L799:
	ldr	x0, [x19, 4024]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	ldr	w26, [x1, 4]
	cmn	w0, #1
	ldr	x27, [x1, 16]
	ubfx	x26, x26, 10, 16
	bne	.L767
	mov	w3, 16
	mov	w4, 65535
.L769:
	ldr	x0, [x19, 4024]
	mov	w2, 1
	stp	w4, w3, [x29, 104]
	add	x0, x0, x23
	ldr	w1, [x0, 4]
	add	w1, w1, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 4024]
	add	x0, x0, x23
	bl	FlashReadPages
	ldp	w4, w3, [x29, 104]
	ldrh	w0, [x27]
	cmp	w0, w4
	ldr	x0, [x19, 4024]
	bne	.L766
	mov	w1, -1
	str	w1, [x0, x23]
	ldr	x0, [x19, 4024]
	ldr	w0, [x0, x23]
	cmp	w0, w1
	bne	.L767
.L768:
	mov	w1, 0
	mov	w0, w26
	bl	FtlFreeSysBlkQueueIn
	b	.L772
.L766:
	ldr	w0, [x0, x23]
	cmn	w0, #1
	bne	.L767
	sub	w3, w3, #1
	ands	w3, w3, 65535
	bne	.L769
	b	.L768
.L767:
	ldr	w1, [x19, 2620]
	ldr	w0, [x27, 4]
	cmn	w1, #1
	beq	.L770
	cmp	w1, w0
	bhi	.L771
.L770:
	cmn	w0, #1
	beq	.L771
	add	w1, w0, 1
	str	w1, [x19, 2620]
.L771:
	ldrh	w1, [x27]
	mov	w2, 61604
	cmp	w1, w2
	beq	.L773
	bhi	.L774
	mov	w0, 61574
	cmp	w1, w0
	beq	.L775
.L772:
	add	x23, x23, 32
	cmp	x21, x23
	bne	.L799
	b	.L800
.L774:
	mov	w0, 61634
	cmp	w1, w0
	beq	.L776
	mov	w0, 65535
	cmp	w1, w0
	beq	.L768
	b	.L772
.L776:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w1, [x0, -120]
	ldr	w0, [x19, 132]
	cmp	w1, w0
	bls	.L778
	adrp	x0, .LC1
	mov	w2, 1256
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L778:
	add	x4, x20, :lo12:.LANCHOR3
	ldr	w3, [x19, 132]
	and	w2, w3, 65535
	ldrh	w1, [x4, -120]
	sub	w0, w2, #1
	ldr	x4, [x4, -208]
	sxth	x0, w0
	sub	w2, w2, w1
	sub	w2, w2, #1
	sxth	w2, w2
.L779:
	cmp	w0, w2
	bgt	.L785
	tbz	w0, #31, .L819
	b	.L772
.L785:
	sxtw	x5, w0
	ldr	w8, [x27, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L780
	ldr	w2, [x4]
	cbnz	w2, .L781
	cmp	w3, w1
	beq	.L781
	add	x2, x20, :lo12:.LANCHOR3
	add	w1, w1, 1
	strh	w1, [x2, -120]
.L781:
	add	x3, x20, :lo12:.LANCHOR3
	mov	w1, 0
.L782:
	cmp	w1, w0
	bne	.L783
	add	x1, x20, :lo12:.LANCHOR3
	ldr	w3, [x27, 4]
	ldr	x2, [x1, -208]
	str	w3, [x2, x6]
	ldr	x2, [x1, -256]
	strh	w26, [x2, x5, lsl 1]
	tbnz	w0, #31, .L772
	ldrh	w1, [x1, -120]
	ldr	w2, [x19, 132]
	sub	w2, w2, w1
	sub	w2, w2, #1
	cmp	w0, w2, sxth
	bgt	.L772
.L819:
	add	x2, x20, :lo12:.LANCHOR3
	add	w1, w1, 1
	ldr	w3, [x27, 4]
	strh	w1, [x2, -120]
	ldr	x1, [x2, -208]
	str	w3, [x1, x0, lsl 2]
	ldr	x1, [x2, -256]
.L837:
	strh	w26, [x1, x0, lsl 1]
	b	.L772
.L783:
	ldr	x7, [x3, -208]
	sxtw	x2, w1
	lsl	x4, x2, 2
	lsl	x2, x2, 1
	add	x8, x7, x4
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w8, [x8, 4]
	str	w8, [x7, x4]
	ldr	x4, [x3, -256]
	add	x7, x4, x2
	ldrh	w7, [x7, 2]
	strh	w7, [x4, x2]
	b	.L782
.L780:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L779
.L775:
	ldrh	w1, [x19, 148]
	ldrh	w0, [x19, 124]
	cmp	w1, w0
	bls	.L788
	adrp	x0, .LC1
	mov	w2, 1297
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L788:
	add	x4, x20, :lo12:.LANCHOR3
	ldrh	w3, [x19, 124]
	ldrh	w1, [x19, 148]
	sub	w2, w3, #1
	ldr	x4, [x4, -232]
	sxth	x0, w2
	sub	w2, w2, w1
.L789:
	cmp	w0, w2
	ble	.L794
	sxtw	x5, w0
	ldr	w8, [x27, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L790
	ldr	w2, [x4]
	cbnz	w2, .L791
	cmp	w3, w1
	beq	.L791
	add	w1, w1, 1
	strh	w1, [x19, 148]
.L791:
	add	x7, x20, :lo12:.LANCHOR3
	mov	w1, 0
.L792:
	cmp	w1, w0
	bne	.L793
	add	x1, x20, :lo12:.LANCHOR3
	ldr	w2, [x27, 4]
	ldr	x1, [x1, -232]
	str	w2, [x1, x6]
	ldr	x1, [x19, 152]
	strh	w26, [x1, x5, lsl 1]
.L794:
	tbnz	w0, #31, .L772
	ldrh	w1, [x19, 124]
	ldrh	w2, [x19, 148]
	sub	w1, w1, #1
	sub	w1, w1, w2
	cmp	w0, w1, sxth
	bgt	.L772
	add	x1, x20, :lo12:.LANCHOR3
	add	w2, w2, 1
	strh	w2, [x19, 148]
	ldr	w2, [x27, 4]
	ldr	x1, [x1, -232]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 152]
	b	.L837
.L793:
	ldr	x4, [x7, -232]
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
	b	.L792
.L790:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L789
.L773:
	ldrh	w2, [x28]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L796
	strh	w26, [x28]
.L838:
	str	w0, [x28, 8]
	b	.L772
.L796:
	ldrh	w0, [x28, 4]
	cmp	w0, w1
	beq	.L797
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L797:
	ldr	w0, [x27, 4]
	ldr	w1, [x28, 8]
	cmp	w1, w0
	bcs	.L798
	ldrh	w0, [x28]
	strh	w0, [x28, 4]
	strh	w26, [x28]
	ldr	w0, [x27, 4]
	b	.L838
.L798:
	strh	w26, [x28, 4]
	b	.L772
.L801:
	add	x0, x20, :lo12:.LANCHOR3
	ldr	x1, [x0, -256]
	ldrh	w2, [x1]
	cbz	w2, .L803
.L806:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x1, [x0, 152]
	ldrh	w2, [x1]
	cbz	w2, .L804
.L805:
	add	x20, x20, :lo12:.LANCHOR3
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x20, -120]
	ldr	w0, [x22, 132]
	cmp	w1, w0
	bls	.L835
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1422
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L835:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L803:
	ldrh	w0, [x0, -120]
	cbz	w0, .L806
	ldr	w2, [x19, 132]
	mov	w0, 0
.L807:
	cmp	w0, w2
	bcs	.L806
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L808
	mov	w1, w0
	add	x7, x22, :lo12:.LANCHOR0
	add	x3, x20, :lo12:.LANCHOR3
.L809:
	ldr	w2, [x7, 132]
	cmp	w1, w2
	bcs	.L806
	ldr	x5, [x3, -256]
	sxtw	x6, w1
	lsl	x4, x6, 1
	sub	w2, w1, w0
	sxtw	x2, w2
	add	w1, w1, 1
	sxth	w1, w1
	ldrh	w8, [x5, x4]
	strh	w8, [x5, x2, lsl 1]
	ldr	x5, [x3, -208]
	ldr	w6, [x5, x6, lsl 2]
	str	w6, [x5, x2, lsl 2]
	ldr	x2, [x3, -256]
	strh	wzr, [x2, x4]
	b	.L809
.L808:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L807
.L804:
	ldrh	w2, [x0, 148]
	cbz	w2, .L805
	ldrh	w2, [x0, 124]
	mov	w0, 0
.L814:
	mov	w6, w0
	cmp	w0, w2
	bge	.L805
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L815
	add	x2, x22, :lo12:.LANCHOR0
	add	x7, x20, :lo12:.LANCHOR3
.L816:
	ldrh	w1, [x2, 124]
	cmp	w0, w1
	bge	.L805
	ldr	x4, [x2, 152]
	sxtw	x5, w0
	lsl	x3, x5, 1
	sub	w1, w0, w6
	sxtw	x1, w1
	add	w0, w0, 1
	sxth	w0, w0
	ldrh	w8, [x4, x3]
	strh	w8, [x4, x1, lsl 1]
	ldr	x4, [x7, -232]
	ldr	w5, [x4, x5, lsl 2]
	str	w5, [x4, x1, lsl 2]
	ldr	x1, [x2, 152]
	strh	wzr, [x1, x3]
	b	.L816
.L815:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L814
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
	add	x20, x3, 3408
	mov	x25, x3
	ldr	x0, [x3, 3440]
	ldr	x22, [x3, 3448]
	stp	x0, x22, [x20, 8]
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x19, 2]
	mov	w0, 1
	str	w0, [x19, 56]
	ldr	w0, [x29, 116]
	sub	w0, w0, #1
	str	w0, [x29, 112]
.L840:
	ldr	w0, [x29, 116]
	cmp	w27, w0
	bge	.L858
	ldr	w0, [x29, 112]
	sxtw	x28, w27
	cmp	w27, w0
	bne	.L841
	lsl	x0, x28, 1
	mov	w1, 1
	add	x20, x23, x0
	add	x25, x21, :lo12:.LANCHOR0
	add	x25, x25, 3408
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
.L842:
	cmp	w23, w26
	blt	.L845
.L858:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x0, x19
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 2]
	ldrh	w0, [x21, 108]
	cmp	w1, w0
	bne	.L847
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L847:
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
.L845:
	ldrh	w0, [x20]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w23, w0, lsl 10
	str	w0, [x25, 4]
	mov	x0, x25
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L843
	ldrh	w0, [x22, 8]
	cmp	w24, w0
	bls	.L844
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L844
	ldr	x2, [x29, 120]
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x25, 4]
	str	w1, [x2, x0]
.L844:
	add	w23, w23, 1
	sxth	w23, w23
	b	.L842
.L843:
	ldrh	w0, [x20]
	strh	w0, [x19, 60]
	b	.L844
.L841:
	ldr	x0, [x25, 3440]
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
	beq	.L860
	ldrh	w1, [x22]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L860
	ldrh	w1, [x22, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L849
.L860:
	mov	w5, 0
.L850:
	ldrh	w0, [x25, 108]
	cmp	w5, w0
	bge	.L856
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
	beq	.L854
	ldrh	w0, [x22, 8]
	cmp	w24, w0
	bls	.L854
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L854
	ldr	x2, [x29, 120]
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x20, 4]
	str	w1, [x2, x0]
.L854:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L850
.L849:
	mov	w0, 0
.L851:
	ldrh	w1, [x25, 108]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L853
.L856:
	add	w4, w27, 1
	sxth	w27, w4
	b	.L840
.L853:
	ldr	x2, [x25, 3440]
	sbfiz	x5, x0, 3, 32
	ldrh	w1, [x2, x5]
	cmp	w24, w1
	bls	.L852
	add	x2, x2, x5
	ldr	x3, [x29, 120]
	ubfiz	x1, x1, 2, 16
	ldr	w2, [x2, 4]
	str	w2, [x3, x1]
.L852:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L851
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
	sub	x0, x0, #112
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
	str	x1, [x0, 40]
	mov	w1, -1
	strh	w2, [x0, 6]
	strh	w1, [x0, 60]
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
	ldr	x0, [x21, -248]
	ldr	w2, [x20, 132]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x20, 142]
	mov	w1, 255
	ldrh	w2, [x20, 114]
	mul	w2, w2, w0
	ldr	x0, [x21, -200]
	bl	ftl_memset
	mov	x1, x20
	mov	w0, 0
	mov	w5, -1
.L868:
	ldrh	w2, [x1, 142]
	cmp	w2, w0
	bhi	.L869
	add	x0, x19, :lo12:.LANCHOR3
	ldr	w3, [x1, 132]
	ldrh	w1, [x1, 140]
	mov	w2, -1
	strh	w1, [x0, -178]
	ldr	x1, [x0, -256]
	str	x1, [x0, -168]
	ldr	x1, [x0, -208]
	strh	w3, [x0, -174]
	mov	w3, -3902
	str	x1, [x0, -160]
	ldr	x1, [x0, -248]
	strh	w3, [x0, -180]
	str	x1, [x0, -152]
	ldrh	w3, [x0, -120]
	ldr	x1, [x0, -216]
	strh	w2, [x0, -182]
	strh	w2, [x0, -184]
	strh	w2, [x0, -124]
	strh	w3, [x0, -176]
	str	x1, [x0, -144]
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L869:
	ldr	x3, [x1, 2568]
	ubfiz	x2, x0, 4, 16
	add	x3, x3, x2
	str	wzr, [x3, 4]
	ldr	x3, [x1, 2568]
	strh	w5, [x3, x2]
	ldr	x3, [x1, 2568]
	ldr	x4, [x21, -200]
	add	x3, x3, x2
	ldrh	w2, [x1, 114]
	mul	w2, w2, w0
	add	w0, w0, 1
	and	w0, w0, 65535
	sxtw	x2, w2
	and	x2, x2, -4
	add	x2, x4, x2
	str	x2, [x3, 8]
	b	.L868
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
	sub	x0, x0, #184
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
	stp	wzr, wzr, [x0, -44]
	mov	w1, -1
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
	ldr	x0, [x19, 4072]
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
.L876:
	ldrh	w0, [x19, 44]
	cmp	w20, w0
	bcs	.L883
	ldrh	w8, [x19, 36]
	mov	x6, 0
	ldrh	w7, [x19, 106]
	mov	w5, 0
	b	.L884
.L878:
	ldrb	w0, [x23, x6]
	mov	w1, w20
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L877
	add	w5, w5, w7
	and	w5, w5, 65535
.L877:
	add	x6, x6, 1
.L884:
	cmp	w8, w6, uxth
	bhi	.L878
	cbz	w5, .L879
	mov	w0, 32768
	sdiv	w5, w0, w5
.L880:
	ldr	x1, [x19, 2360]
	mov	w0, 6
	umaddl	x0, w20, w0, x1
	strh	w5, [x0, 4]
	ldrh	w0, [x19, 2424]
	cmp	w0, w20
	beq	.L881
	ldrh	w0, [x19, 2472]
	cmp	w0, w20
	beq	.L881
	ldrh	w0, [x19, 2520]
	cmp	w0, w20
	beq	.L881
	ldr	x1, [x19, 2376]
	ubfiz	x0, x20, 1, 16
	ldrh	w0, [x1, x0]
	cbnz	w0, .L882
	add	w22, w22, 1
	mov	w0, w20
	and	w22, w22, 65535
	bl	INSERT_FREE_LIST
.L881:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L876
.L879:
	ldr	x1, [x19, 2376]
	ubfiz	x0, x20, 1, 16
	mov	w2, -1
	strh	w2, [x1, x0]
	b	.L880
.L882:
	add	w21, w21, 1
	mov	w0, w20
	and	w21, w21, 65535
	bl	INSERT_DATA_LIST
	b	.L881
.L883:
	strh	w21, [x19, 2400]
	add	w21, w21, w22
	strh	w22, [x19, 2416]
	cmp	w21, w0
	ble	.L885
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2206
	add	x1, x1, 384
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L885:
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
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
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
.L888:
	ldr	w0, [x24, 2584]
	cmp	w23, w0
	bcc	.L890
	adrp	x25, .LC98
	add	x23, x20, :lo12:.LANCHOR0
	add	x26, x22, :lo12:check_vpc_table
	add	x25, x25, :lo12:.LC98
	mov	w24, 0
	mov	w19, 0
	mov	w4, 65535
.L891:
	ldrh	w0, [x23, 44]
	cmp	w0, w19
	bhi	.L893
	ldr	x19, [x23, 2408]
	cbz	x19, .L894
	ldr	x0, [x23, 2360]
	adrp	x25, .LC99
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:check_vpc_table
	sub	x19, x19, x0
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	movk	x0, 0xaaab, lsl 0
	add	x25, x25, :lo12:.LC99
	ldrh	w26, [x23, 2416]
	mov	w27, 6
	mov	w23, 0
	mul	x19, x19, x0
	and	w19, w19, 65535
.L895:
	cmp	w23, w26
	bne	.L897
.L894:
	cbz	w24, .L887
	add	x1, x21, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2373
	add	x1, x1, 408
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L887:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L890:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w23
	bl	log2phys
	ldr	w0, [x29, 124]
	cmn	w0, #1
	beq	.L889
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	and	x0, x0, 65535
	ldrh	w1, [x19, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x19, x0, lsl 1]
.L889:
	add	w23, w23, 1
	b	.L888
.L893:
	ldr	x0, [x23, 2376]
	ubfiz	x28, x19, 1, 16
	sxtw	x27, w19
	ldrh	w2, [x0, x28]
	ldrh	w3, [x26, x27, lsl 1]
	cmp	w2, w3
	beq	.L892
	mov	x0, x25
	str	w4, [x29, 108]
	mov	w1, w19
	bl	sftl_printk
	ldr	x0, [x23, 2376]
	ldr	w4, [x29, 108]
	ldrh	w0, [x0, x28]
	cmp	w0, w4
	beq	.L892
	ldrh	w1, [x26, x27, lsl 1]
	cmp	w1, w0
	csinc	w24, w24, wzr, ls
.L892:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L891
.L897:
	ldr	x1, [x20, 2376]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L896
	ldrh	w3, [x22, w19, sxtw 1]
	mov	w24, 1
	mov	w1, w19
	mov	x0, x25
	bl	sftl_printk
.L896:
	ldr	x0, [x20, 2360]
	umull	x19, w19, w27
	ldrh	w19, [x0, x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L894
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L895
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
	beq	.L931
	cbnz	w19, .L915
.L916:
	bl	FtlGcPageVarInit
	b	.L917
.L931:
	mov	w19, 0
.L915:
	adrp	x0, .LANCHOR0+106
	ldrh	w0, [x0, #:lo12:.LANCHOR0+106]
	cmp	w0, w25
	beq	.L916
.L917:
	adrp	x26, .LANCHOR0
	add	x22, x26, :lo12:.LANCHOR0
	mov	w24, 0
.L925:
	ldrh	w1, [x21]
	mov	w0, 65535
	strb	wzr, [x21, 8]
	cmp	w1, w0
	beq	.L932
.L928:
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
.L919:
	cmp	x0, x2
	bne	.L921
	ldr	x0, [x22, 4024]
	mov	w1, w20
	mov	w2, 0
	ubfiz	x20, x20, 5, 16
	bl	FlashReadPages
	mov	x10, 0
	mov	w11, 65535
.L922:
	cmp	x10, x20
	bne	.L926
	add	w19, w19, 1
	add	w24, w24, 1
	and	w19, w19, 65535
	cmp	w25, w24
	bls	.L927
.L929:
	ldrh	w0, [x22, 106]
	cmp	w0, w19
	bhi	.L928
.L932:
	mov	w2, 0
	b	.L918
.L921:
	ldrh	w1, [x2]
	cmp	w1, w9
	beq	.L920
	ldr	x4, [x22, 4024]
	ubfiz	x6, x20, 5, 16
	orr	w1, w19, w1, lsl 10
	add	x4, x4, x6
	str	w1, [x4, 4]
	mul	w1, w20, w7
	ldr	x3, [x22, 4024]
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
.L920:
	add	x2, x2, 2
	b	.L919
.L926:
	ldr	x6, [x22, 4024]
	add	x5, x6, x10
	ldr	w4, [x5, 4]
	lsr	w0, w4, 10
	bl	P2V_plane
	and	w2, w0, 65535
	ldr	w0, [x6, x10]
	cbnz	w0, .L923
	ldr	x0, [x5, 16]
	add	x10, x10, 32
	ldrh	w1, [x0]
	cmp	w1, w11
	bne	.L924
	adrp	x0, .LANCHOR3-40
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR3-40]
.L918:
	add	x23, x23, :lo12:.LANCHOR2
	strh	w19, [x21, 2]
	strb	w2, [x21, 6]
	mov	w0, -1
	mov	w1, w19
	strh	w0, [x23, 4]
	mov	x0, x21
	bl	ftl_sb_update_avl_pages
	b	.L930
.L924:
	ldp	w2, w0, [x0, 8]
	mov	w1, w4
	bl	FtlGcUpdatePage
	b	.L922
.L923:
	ldr	x0, [x22, 2376]
	mov	w19, 0
	ldrh	w1, [x21]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x21]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x21]
	bl	FtlGcPageVarInit
	b	.L925
.L927:
	add	x1, x23, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x1, 4]
	cmp	w0, w2
	beq	.L929
	add	w0, w0, w24
	strh	w0, [x1, 4]
	ldrh	w0, [x22, 106]
	cmp	w0, w19
	bls	.L929
.L930:
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
	bls	.L940
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
.L938:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 128
	ret
.L940:
	mov	w19, 0
	b	.L938
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
	ldr	x2, [x0, 3376]
	mov	x0, x20
	blr	x2
	and	w0, w0, 65535
	cmp	w0, 50
	bls	.L943
	mov	w2, 256
	mov	w1, 255
	mov	x0, x20
	bl	ftl_memset
	mov	w0, 0
.L943:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19, 14]
	cmp	w1, 4
	bne	.L947
	mov	x1, 0
.L945:
	cmp	w0, w1, uxth
	bhi	.L946
.L947:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L946:
	ldrh	w2, [x20, x1, lsl 1]
	lsr	w2, w2, 1
	strh	w2, [x20, x1, lsl 1]
	add	x1, x1, 1
	b	.L945
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
	ldr	x2, [x19, 3448]
	add	x23, x19, 192
	ldr	x0, [x19, 3440]
	str	x2, [x19, 3424]
	ldrh	w2, [x19, 114]
	str	x0, [x19, 3416]
	bl	ftl_memset
.L952:
	ldrh	w0, [x19, 58]
	add	x20, x19, 3408
	cmp	w21, w0
	blt	.L953
	ldr	x24, [x20, 16]
	mov	w1, 255
	mov	w2, 16
	adrp	x22, .LC100
	mov	x0, x24
	bl	ftl_memset
	mov	w0, -3887
	add	x1, x19, 160
	strh	w0, [x24]
	add	x25, x25, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC100
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
.L954:
	ldr	x0, [x25, 3440]
	str	x0, [x20, 8]
	ldr	x0, [x25, 3448]
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
	blt	.L955
	ldr	w0, [x19, 8]
	mov	w2, 1
	ldrh	w1, [x19]
	add	w0, w0, 1
	str	w0, [x19, 8]
	str	w0, [x24, 4]
	strh	w1, [x24, 8]
	ldrh	w0, [x19, 4]
	strh	w1, [x19, 4]
	ldr	x1, [x25, 3968]
	strh	w0, [x19]
	lsl	w0, w0, 10
	str	w0, [x20, 4]
	strh	wzr, [x19, 2]
	str	w0, [x1, 4]
	mov	w1, w2
	ldr	x0, [x25, 3968]
	bl	FlashEraseBlocks
	mov	w3, 1
	mov	x0, x20
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
.L955:
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L956
	ldr	w1, [x20, 4]
	add	w21, w21, 1
	adrp	x0, .LC101
	and	w21, w21, 65535
	add	x0, x0, :lo12:.LC101
	bl	sftl_printk
	cmp	w21, 3
	bls	.L954
	ldr	w1, [x20, 4]
	adrp	x0, .LC102
	mov	w2, w21
	add	x0, x0, :lo12:.LC102
	bl	sftl_printk
.L958:
	b	.L958
.L953:
	ldrh	w2, [x22, -192]
	ldr	x1, [x23], 8
	ldr	x0, [x20, 8]
	mul	w3, w21, w2
	lsl	w2, w2, 2
	add	w21, w21, 1
	add	x0, x0, x3, sxtw 2
	bl	ftl_memcpy
	b	.L952
.L959:
	mov	w23, 1
	b	.L954
.L956:
	cbz	w23, .L959
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
.L962:
	ldrh	w1, [x22, 2400]
	ldrh	w0, [x22, 2416]
	add	w0, w0, w1
	ldrh	w1, [x22, 44]
	cmp	w0, w1
	ble	.L963
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2654
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L963:
	cmp	x20, x23
	bne	.L988
	ldrh	w0, [x19, 2416]
	ldr	w1, [x19, 3244]
	mul	w1, w0, w1
	lsr	w0, w0, 1
	add	w0, w0, 1
	add	w1, w0, w1, lsr 2
	ands	w1, w1, 65535
	beq	.L964
	sub	w1, w1, #1
	and	w1, w1, 65535
.L964:
	add	x0, x19, 2408
	bl	List_pop_index_node
	and	w24, w0, 65535
	ldrh	w0, [x19, 2416]
	cbnz	w0, .L965
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2663
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L965:
	ldrh	w0, [x19, 2416]
	sub	w0, w0, #1
	strh	w0, [x19, 2416]
	strh	w24, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20, 7]
	cbnz	w0, .L966
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
	ble	.L962
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2674
	add	x1, x1, 424
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L962
.L988:
	mov	w1, 0
	b	.L964
.L966:
	ldrh	w1, [x19, 2400]
	ldrh	w0, [x19, 2416]
	add	w0, w0, w1
	ldrh	w1, [x19, 44]
	cmp	w0, w1
	ble	.L968
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2677
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L968:
	add	x27, x20, 16
	ldrh	w4, [x19, 36]
	mov	x3, x27
	mov	x0, 0
	mov	w25, 0
	mov	w5, 65535
.L969:
	cmp	w4, w0, uxth
	bhi	.L971
	cbnz	w25, .L972
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2688
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L972:
	ldrh	w0, [x19, 2672]
	cmp	w0, w24
	bne	.L973
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2690
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L973:
	ldrb	w0, [x20, 8]
	uxtw	x26, w24
	cbnz	w0, .L974
	ldr	x2, [x19, 2384]
	lsl	x1, x26, 1
	ldrh	w0, [x2, x1]
	cbz	w0, .L975
	ldrh	w3, [x19, 96]
	add	w0, w0, w3
.L996:
	strh	w0, [x2, x1]
	mov	w1, 0
	ldr	w0, [x19, 2628]
	add	w0, w0, 1
	str	w0, [x19, 2628]
	mov	w0, w24
	bl	ftl_set_blk_mode
.L977:
	ldr	x0, [x19, 2384]
	lsl	x26, x26, 1
	ldr	w1, [x19, 2644]
	ldrh	w0, [x0, x26]
	cmp	w0, w1
	bls	.L978
	str	w0, [x19, 2644]
.L978:
	ldr	w1, [x19, 2632]
	ubfiz	x3, x25, 5, 16
	ldrh	w0, [x19, 96]
	ldr	w2, [x19, 2628]
	madd	w0, w0, w2, w1
	ldrh	w1, [x19, 44]
	udiv	w0, w0, w1
	ldr	x1, [x19, 4080]
	str	w0, [x19, 2636]
	ldr	w0, [x1, 16]
	add	w0, w0, 1
	str	w0, [x1, 16]
	mov	x0, 0
.L979:
	cmp	x3, x0
	bne	.L980
	ldrb	w1, [x20, 8]
	mov	w2, w25
	ldr	x0, [x19, 3968]
	mov	x28, 0
	bl	FlashEraseBlocks
	mov	w1, 0
.L981:
	cmp	w25, w28, uxth
	bhi	.L983
	cmp	w1, 0
	ble	.L984
	mov	w0, w24
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L984:
	ldrb	w0, [x20, 7]
	cbnz	w0, .L985
	ldr	x0, [x19, 2376]
	mov	w1, -1
	strh	w1, [x0, x26]
	b	.L962
.L971:
	ldr	x1, [x19, 3968]
	add	x2, x1, x0, lsl 5
	stp	xzr, xzr, [x2, 8]
	ldrh	w2, [x3]
	cmp	w2, w5
	beq	.L970
	ubfiz	x6, x25, 5, 16
	add	w25, w25, 1
	add	x1, x1, x6
	and	w25, w25, 65535
	lsl	w2, w2, 10
	str	w2, [x1, 4]
.L970:
	add	x0, x0, 1
	add	x3, x3, 2
	b	.L969
.L975:
	mov	w0, 2
	b	.L996
.L974:
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
	b	.L977
.L980:
	ldr	x1, [x19, 3968]
	add	x1, x1, x0
	add	x0, x0, 32
	ldr	w2, [x1, 4]
	and	w2, w2, -1024
	str	w2, [x1, 4]
	b	.L979
.L983:
	ldr	x2, [x19, 3968]
	lsl	x0, x28, 5
	add	x3, x2, x0
	ldr	w2, [x2, x0]
	cmn	w2, #1
	bne	.L982
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
.L982:
	add	x28, x28, 1
	add	x27, x27, 2
	b	.L981
.L985:
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
	cbz	w0, .L986
	ldrb	w0, [x20, 7]
	cbnz	w0, .L987
.L986:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2743
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L987:
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
	ldrh	w0, [x19, 3328]
	str	x27, [sp, 80]
	cbz	w0, .L998
	add	x23, x19, 64
	add	x24, x19, 3336
	mov	w21, 0
.L999:
	ldrh	w0, [x19, 36]
	cmp	w0, w21
	bhi	.L1005
	bl	FtlGcReFreshBadBlk
.L998:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1005:
	ldrb	w0, [x23, w21, sxtw]
	mov	w1, w22
	adrp	x26, .LC103
	add	x26, x26, :lo12:.LC103
	mov	w20, 0
	bl	V2P_block
	and	w25, w0, 65535
.L1000:
	ldrh	w0, [x19, 3328]
	cmp	w0, w20
	bhi	.L1004
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L999
.L1004:
	ldrh	w0, [x24, w20, sxtw 1]
	add	w27, w20, 1
	cmp	w0, w25
	bne	.L1001
	mov	w1, w25
	mov	x0, x26
	bl	sftl_printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w0, [x19, 3328]
	mov	w1, w27
.L1002:
	cmp	w20, w0
	bcc	.L1003
	sub	w0, w0, #1
	strh	w0, [x19, 3328]
.L1001:
	and	w20, w27, 65535
	b	.L1000
.L1003:
	sub	w2, w1, #1
	ldrh	w3, [x24, w1, sxtw 1]
	add	w20, w20, 1
	add	w1, w1, 1
	and	w20, w20, 65535
	strh	w3, [x24, w2, sxtw 1]
	b	.L1002
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
	cbnz	w1, .L1011
	ldrh	w1, [x0, 2672]
	cmp	w1, w19
	bne	.L1012
	mov	w1, -1
	strh	w1, [x0, 2672]
.L1013:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x21, 2368
	bl	List_remove_node
	ldrh	w0, [x21, 2400]
	cbnz	w0, .L1015
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2813
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1015:
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
	ble	.L1019
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2816
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1019:
	mov	w0, 1
	b	.L1010
.L1012:
	ldrh	w1, [x0, 2424]
	cmp	w1, w19
	beq	.L1018
	ldrh	w1, [x0, 2472]
	cmp	w1, w19
	beq	.L1018
	ldrh	w0, [x0, 2520]
	cmp	w0, w19
	bne	.L1013
.L1018:
	mov	w0, 0
.L1010:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L1011:
	mov	w0, w19
	bl	List_update_data_list
	b	.L1018
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 65535
	stp	x21, x22, [sp, 32]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L1022
	adrp	x20, .LANCHOR0
	add	x22, x20, :lo12:.LANCHOR0
	ubfiz	x21, x19, 1, 16
	ldr	x1, [x22, 2376]
	ldrh	w0, [x1, x21]
	cbnz	w0, .L1023
	mov	w2, 0
	mov	w1, w19
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	bl	sftl_printk
	ldr	x0, [x22, 2376]
	ldrh	w0, [x0, x21]
	cbnz	w0, .L1024
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2831
	add	x1, x1, 472
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1024:
	add	x22, x20, :lo12:.LANCHOR0
	mov	w1, 16
	add	x6, x22, 2408
	ldr	x0, [x22, 2376]
	strh	w1, [x0, x21]
	mov	w1, w19
	mov	x0, x6
	bl	test_node_in_list
	cbz	w0, .L1025
	mov	w1, w19
	mov	x0, x6
	bl	List_remove_node
	ldrh	w0, [x22, 2416]
	cbnz	w0, .L1026
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2835
	add	x1, x1, 472
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1026:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20, 2416]
	sub	w0, w0, #1
	strh	w0, [x20, 2416]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x0, [x20, 2376]
	mov	w1, w19
	ldrh	w2, [x0, x21]
	adrp	x0, .LC105
	add	x0, x0, :lo12:.LC105
	bl	sftl_printk
.L1025:
	mov	w0, w19
	bl	FtlGcRefreshBlock
.L1029:
	mov	w21, 0
	b	.L1021
.L1023:
	sub	w0, w0, #1
	strh	w0, [x1, x21]
.L1022:
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	mov	w1, 65535
	ldrh	w0, [x20, -48]
	cmp	w0, w1
	bne	.L1028
	strh	w19, [x20, -48]
	b	.L1029
.L1028:
	cmp	w19, w0
	beq	.L1029
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
	cbnz	w1, .L1021
	cmp	w19, w0, uxth
	beq	.L1021
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2856
	add	x1, x1, 472
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1021:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
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
	cbz	w2, .L1035
	ldrb	w1, [x20, 2432]
	cbnz	w1, .L1035
	ldrb	w1, [x20, 2431]
	ldrh	w3, [x20, 106]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L1035
	ldrb	w0, [x20, 2434]
	cbnz	w0, .L1034
	ldr	w22, [x20, 2584]
	add	x1, x29, 92
	ldrh	w24, [x20, 36]
	mov	w2, 0
	sub	w22, w22, #1
	mov	w0, w22
	bl	log2phys
	ldr	x21, [x20, 3448]
	ldr	w0, [x29, 92]
	ldr	x1, [x20, 3440]
	str	w0, [x29, 100]
	cmn	w0, #1
	stp	x1, x21, [x29, 104]
	str	w22, [x29, 120]
	str	wzr, [x21, 4]
	beq	.L1037
	mov	w2, 0
	mov	w1, 1
	add	x0, x29, 96
	bl	FlashReadPages
.L1038:
	add	x25, x19, :lo12:.LANCHOR0
	lsl	w24, w24, 2
	add	x20, x25, 2424
	mov	w0, -3947
	mov	w23, 0
	strh	w0, [x21]
.L1039:
	cmp	w24, w23
	bne	.L1043
.L1040:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19, 2434]
.L1034:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 128
	ret
.L1037:
	ldrh	w2, [x20, 114]
	mov	w1, 255
	ldr	x0, [x20, 3440]
	bl	ftl_memset
	b	.L1038
.L1043:
	ldrh	w0, [x20, 4]
	cbz	w0, .L1040
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
	b	.L1039
.L1035:
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x19, 2434]
	b	.L1034
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
.L1053:
	ldrh	w0, [x20, 142]
	cmp	w0, w19
	bhi	.L1055
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1055:
	ldr	x1, [x20, 2568]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L1054
	mov	w0, w19
	bl	flush_l2p_region
.L1054:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1053
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
	beq	.L1175
	ldrb	w0, [x19, 6]
	adrp	x20, .LANCHOR0
	str	w0, [x29, 156]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w27, [x19, 2]
	ldrh	w0, [x0, 106]
	cmp	w0, w27
	bne	.L1060
	strh	wzr, [x19, 4]
.L1181:
	strb	wzr, [x19, 6]
.L1175:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1060:
	ldrh	w0, [x19, 16]
	mov	w21, 0
.L1061:
	cmp	w0, w1
	beq	.L1062
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	mov	w24, w0
	cmn	w0, #1
	beq	.L1063
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
.L1064:
	cmp	x5, x1
	bne	.L1068
	add	x23, x20, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w1, w21
	ldr	x0, [x23, 4024]
	bl	FlashReadPages
	ldr	w22, [x23, 2624]
	ldr	x4, [x23, 4024]
	and	w9, w24, 65535
	sub	w22, w22, #1
	mov	w7, 65535
	mov	x6, x4
	mov	w3, 0
.L1069:
	cmp	w21, w3
	bne	.L1074
	add	w23, w24, 1
	ldr	w0, [x4, 4]
	and	w23, w23, 65535
.L1177:
	lsr	w0, w0, 10
	bl	P2V_plane
	and	w28, w0, 65535
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0, 106]
	cmp	w0, w23
	bne	.L1076
	strh	w23, [x19, 2]
	strb	wzr, [x19, 6]
	strh	wzr, [x19, 4]
.L1076:
	ldr	w0, [x29, 156]
	cmp	w23, w27
	ccmp	w28, w0, 0, eq
	bne	.L1077
.L1182:
	mov	w2, w28
	mov	w1, w23
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
	b	.L1175
.L1062:
	add	w21, w21, 1
	and	w21, w21, 65535
	add	x0, x19, x21, sxtw 1
	ldrh	w0, [x0, 16]
	b	.L1061
.L1063:
	cbz	w27, .L1065
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1804
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1065:
	ldr	w0, [x29, 156]
	cmp	w0, 0
	ccmp	w21, w0, 4, ne
	beq	.L1066
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1805
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1066:
	strh	wzr, [x19, 2]
	b	.L1181
.L1068:
	ldrh	w2, [x5]
	cmp	w2, w10
	beq	.L1067
	ldr	x4, [x0, 4024]
	ubfiz	x7, x21, 5, 16
	orr	w2, w24, w2, lsl 10
	add	x4, x4, x7
	str	w2, [x4, 4]
	mul	w2, w21, w8
	ldr	x3, [x0, 4024]
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
.L1067:
	add	x5, x5, 2
	b	.L1064
.L1074:
	ldr	w0, [x6]
	cbnz	w0, .L1070
	ldr	x8, [x6, 16]
	ldr	w5, [x8, 4]
	cmn	w5, #1
	beq	.L1071
	ldr	w1, [x23, 2624]
	mov	w0, w5
	bl	ftl_cmp_data_ver
	cbz	w0, .L1071
	add	w5, w5, 1
	str	w5, [x23, 2624]
.L1071:
	ldr	w0, [x8]
	cmn	w0, #1
	bne	.L1073
	ubfiz	x3, x3, 5, 16
	and	w23, w24, 65535
	add	x3, x4, x3
	ldr	w0, [x3, 4]
	b	.L1177
.L1070:
	mov	w7, w9
.L1073:
	add	w3, w3, 1
	add	x6, x6, 32
	and	w3, w3, 65535
	b	.L1069
.L1077:
	mov	w0, 65535
	adrp	x21, .LANCHOR3
	cmp	w7, w0
	bne	.L1078
	ldrb	w0, [x19, 8]
	cbnz	w0, .L1079
.L1078:
	add	x0, x21, :lo12:.LANCHOR3
	and	w3, w24, 65535
	ldr	w1, [x0, -36]
	cmn	w1, #1
	bne	.L1080
	str	w22, [x0, -36]
.L1080:
	add	w0, w27, 7
	cmp	w0, w24, uxth
	bge	.L1128
	sub	w25, w3, #7
	and	w25, w25, 65535
.L1081:
	add	x26, x20, :lo12:.LANCHOR0
	mov	w4, 65535
.L1082:
	cmp	w25, w3
	bhi	.L1089
	ldrh	w0, [x26, 36]
	mov	w24, 0
	ldr	x1, [x29, 144]
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
	b	.L1090
.L1128:
	mov	w25, w27
	b	.L1081
.L1084:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1083
	ldr	x5, [x26, 4024]
	ubfiz	x6, x24, 5, 16
	add	w24, w24, 1
	orr	w2, w25, w2, lsl 10
	add	x5, x5, x6
	and	w24, w24, 65535
	str	w2, [x5, 4]
.L1083:
	add	x1, x1, 2
.L1090:
	cmp	x0, x1
	bne	.L1084
	ldr	x0, [x26, 4024]
	mov	w1, w24
	mov	w2, 0
	str	w4, [x29, 128]
	str	w3, [x29, 136]
	ubfiz	x24, x24, 5, 16
	add	x24, x24, 16
	bl	FlashReadPages
	ldr	x1, [x26, 4024]
	add	x2, x21, :lo12:.LANCHOR3
	ldr	w4, [x29, 128]
	ldr	w3, [x29, 136]
	add	x0, x1, 16
	add	x24, x1, x24
.L1085:
	cmp	x24, x0
	bne	.L1088
	add	w25, w25, 1
	and	w25, w25, 65535
	b	.L1082
.L1088:
	ldr	w1, [x0, -16]
	cbnz	w1, .L1079
	ldr	x1, [x0]
	ldrh	w5, [x1]
	cmp	w5, w4
	beq	.L1087
	ldr	w1, [x1, 4]
	cmn	w1, #1
	beq	.L1087
	str	w1, [x2, -36]
.L1087:
	add	x0, x0, 32
	b	.L1085
.L1089:
	add	x0, x21, :lo12:.LANCHOR3
	mov	w1, -1
	str	w1, [x0, -36]
.L1079:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 1
	mov	w25, w27
	add	x20, x20, :lo12:.LANCHOR0
	strh	w0, [x21, -32]
	sub	x0, x21, #184
	bl	FtlMapBlkWriteDump_data
	sub	x0, x21, #24
	str	x0, [x29, 112]
.L1091:
	ldrh	w0, [x20, 36]
	mov	w24, 0
	ldr	x1, [x29, 144]
	mov	w4, 65535
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
.L1092:
	cmp	x0, x1
	bne	.L1094
	ldr	x0, [x20, 4024]
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
.L1095:
	ldr	x0, [x29, 120]
	cmp	x0, x24
	bne	.L1122
	add	w25, w25, 1
	ldrh	w0, [x20, 106]
	and	w25, w25, 65535
	cmp	w0, w25
	bne	.L1091
	ldrh	w1, [x20, 36]
	mov	w0, 0
	strh	w25, [x19, 2]
	mov	w2, 65535
	strh	wzr, [x19, 4]
.L1123:
	cmp	w0, w1
	beq	.L1175
	ldr	x4, [x29, 144]
	ldrh	w3, [x4], 2
	str	x4, [x29, 144]
	cmp	w3, w2
	beq	.L1124
	strb	w0, [x19, 6]
	b	.L1175
.L1094:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1093
	ldr	x3, [x20, 4024]
	ubfiz	x5, x24, 5, 16
	add	w24, w24, 1
	orr	w2, w25, w2, lsl 10
	add	x3, x3, x5
	and	w24, w24, 65535
	str	w2, [x3, 4]
.L1093:
	add	x1, x1, 2
	b	.L1092
.L1122:
	ldr	x4, [x20, 4024]
	add	x4, x4, x24
	ldr	w5, [x4, 4]
	str	w5, [x29, 172]
	lsr	w0, w5, 10
	bl	P2V_plane
	and	w0, w0, 65535
	cmp	w25, w27
	bcc	.L1096
	ldr	w1, [x29, 156]
	ccmp	w1, w0, 0, eq
	bhi	.L1096
	cmp	w25, w23
	ccmp	w28, w0, 0, eq
	beq	.L1097
	ldr	w0, [x4]
	cmn	w0, #1
	beq	.L1098
	ldr	x3, [x4, 16]
	mov	w0, 61589
	ldrh	w1, [x3]
	cmp	w1, w0
	beq	.L1099
.L1106:
	ldrh	w0, [x19]
.L1180:
	bl	decrement_vpc_count
.L1096:
	add	x24, x24, 32
	b	.L1095
.L1099:
	ldr	w22, [x3, 4]
	cmn	w22, #1
	beq	.L1100
	ldr	w1, [x20, 2624]
	mov	w0, w22
	str	x3, [x29, 136]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29, 136]
	cbz	w0, .L1100
	add	w0, w22, 1
	str	w0, [x20, 2624]
.L1100:
	ldrh	w1, [x3]
	mov	w0, 61589
	cmp	w1, w0
	beq	.L1101
	ldr	x1, [x29, 128]
	mov	w2, 1954
	str	x3, [x29, 136]
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	ldr	x3, [x29, 136]
.L1101:
	ldp	w26, w0, [x3, 8]
	add	x1, x29, 168
	str	w0, [x29, 164]
	mov	w2, 0
	mov	w0, w26
	bl	log2phys
	ldr	w1, [x21, -36]
	cmn	w1, #1
	beq	.L1102
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L1102
	ldr	w1, [x29, 164]
	cmn	w1, #1
	beq	.L1103
	ldr	x0, [x20, 4024]
	mov	w2, 0
	add	x0, x0, x24
	ldr	x4, [x0, 16]
	str	w1, [x0, 4]
	mov	w1, 1
	str	x4, [x29, 136]
	ldr	x0, [x20, 4024]
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x0, [x20, 4024]
	ldr	x4, [x29, 136]
	add	x3, x0, x24
	ldr	w0, [x0, x24]
	cmn	w0, #1
	bne	.L1104
.L1105:
	mov	w0, -1
	str	w0, [x29, 164]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
.L1113:
	ldr	w4, [x29, 164]
	cmn	w4, #1
	beq	.L1096
.L1127:
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	and	w26, w0, 65535
	ldrh	w0, [x20, 44]
	mov	w3, w26
	cmp	w0, w26
	bhi	.L1118
	ldr	x1, [x29, 128]
	mov	w2, 2055
	adrp	x0, .LC1
	str	w26, [x29, 136]
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	ldr	w3, [x29, 136]
.L1118:
	ldr	x1, [x20, 2376]
	ubfiz	x0, x26, 1, 16
	ldrh	w0, [x1, x0]
	cbz	w0, .L1119
	mov	w0, w3
	b	.L1180
.L1103:
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	bne	.L1106
	mov	w2, 1
	add	x1, x29, 164
	mov	w0, w26
	bl	log2phys
	b	.L1106
.L1104:
	ldr	w0, [x4, 8]
	cmp	w26, w0
	bne	.L1105
	ldr	w0, [x4, 4]
	str	w0, [x29, 136]
	str	x4, [x29, 104]
	uxtw	x1, w0
	ldr	w0, [x21, -36]
	bl	ftl_cmp_data_ver
	cbz	w0, .L1105
	ldp	w0, w1, [x29, 168]
	ldr	x4, [x29, 104]
	cmp	w0, w1
	ldr	w1, [x29, 164]
	bne	.L1108
.L1178:
	mov	w0, w26
	bl	FtlReUsePrevPpa
	b	.L1105
.L1108:
	cmp	w0, w1
	beq	.L1105
	cmn	w0, #1
	beq	.L1109
	ldr	x4, [x3, 16]
	mov	w2, 0
	str	w0, [x3, 4]
	mov	w1, 1
	str	x4, [x29, 104]
	ldr	x0, [x20, 4024]
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x4, [x29, 104]
.L1110:
	adrp	x0, .LANCHOR0+4024
	ldr	x0, [x0, #:lo12:.LANCHOR0+4024]
	ldr	w0, [x0, x24]
	cmn	w0, #1
	beq	.L1111
	ldr	w3, [x4, 4]
	ldr	w0, [x21, -36]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1111
	ldr	w0, [x29, 136]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1105
.L1111:
	ldr	w1, [x29, 164]
	b	.L1178
.L1109:
	str	w0, [x3]
	b	.L1110
.L1102:
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	beq	.L1113
	mov	w2, 1
	add	x1, x29, 172
	mov	w0, w26
	bl	log2phys
	ldr	w4, [x29, 168]
	cmn	w4, #1
	beq	.L1113
	ldr	w0, [x29, 164]
	cmp	w4, w0
	beq	.L1127
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20, 2424]
	and	w0, w0, 65535
	cmp	w1, w0
	beq	.L1117
	ldrh	w1, [x20, 2472]
	cmp	w1, w0
	beq	.L1117
	ldrh	w1, [x20, 2520]
	cmp	w1, w0
	bne	.L1113
.L1117:
	ldr	x0, [x20, 4024]
	mov	w2, 0
	mov	w1, 1
	str	w4, [x0, 4]
	ldr	x3, [x0, 16]
	ldr	x0, [x20, 4024]
	str	x3, [x29, 136]
	bl	FlashReadPages
	ldr	x0, [x20, 4024]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1113
	ldr	x3, [x29, 136]
	mov	w0, w22
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
	cbnz	w0, .L1113
	mov	w2, 1
	add	x1, x29, 168
	mov	w0, w26
	bl	log2phys
	b	.L1113
.L1119:
	mov	w1, w26
	adrp	x0, .LC106
	add	x0, x0, :lo12:.LC106
	bl	sftl_printk
	b	.L1096
.L1098:
	ldr	w0, [x21, -28]
	cmp	w0, 31
	bhi	.L1120
	ldr	x1, [x29, 112]
	str	w5, [x1, w0, uxtw 2]
	add	w0, w0, 1
	str	w0, [x21, -28]
.L1120:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldr	w0, [x21, -36]
	cmn	w0, #1
	bne	.L1121
.L1179:
	str	w22, [x21, -36]
	b	.L1096
.L1121:
	cmp	w22, w0
	bcs	.L1096
	b	.L1179
.L1124:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1123
.L1097:
	strb	w28, [x19, 6]
	strh	w23, [x19, 2]
	b	.L1182
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
	ldr	x21, [x0, 3448]
	ldr	x1, [x0, 3440]
	stp	x1, x21, [x29, 72]
	mov	w1, -3
	str	w1, [x21, 8]
	mov	w1, -2
	str	w1, [x21, 12]
	ldrh	w1, [x19]
	strh	w1, [x21, 2]
	strh	wzr, [x21]
	ldr	x1, [x0, 3440]
	str	w3, [x1]
	mov	w3, 22136
	movk	w3, 0x1234, lsl 16
	ldr	x1, [x0, 3440]
	str	w3, [x1, 4]
.L1184:
	subs	w22, w22, #1
	beq	.L1186
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1185
.L1186:
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
.L1185:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29, 68]
	cmn	w0, #1
	beq	.L1186
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
	b	.L1184
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
	add	x21, x23, 3408
	mov	w24, 61649
	ldr	x0, [x23, 3440]
	ldr	x22, [x23, 3448]
	stp	x0, x22, [x21, 8]
	bl	FtlBbtMemInit
	ldrh	w20, [x23, 102]
	sub	w20, w20, #1
	and	w20, w20, 65535
.L1196:
	ldrh	w0, [x23, 102]
	sub	w0, w0, #16
	cmp	w20, w0
	ble	.L1199
	lsl	w0, w20, 10
	mov	w2, 1
	str	w0, [x21, 4]
	mov	w1, w2
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1197
	ldr	w0, [x21, 4]
	mov	w2, 1
	mov	w1, w2
	add	w0, w0, 1
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
.L1197:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1198
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1198
	ldr	w1, [x22, 4]
	str	w1, [x23, 168]
	strh	w20, [x23, 160]
	ldrh	w1, [x22, 8]
	strh	w1, [x23, 164]
.L1199:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21, 160]
	cmp	w1, w0
	beq	.L1213
	ldrh	w1, [x21, 164]
	cmp	w1, w0
	beq	.L1203
	add	x0, x21, 3408
	lsl	w1, w1, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x21, 3408]
	cmn	w0, #1
	beq	.L1203
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1203
	ldr	w1, [x21, 168]
	ldr	w0, [x22, 4]
	cmp	w0, w1
	bls	.L1203
	str	w0, [x21, 168]
	ldrh	w1, [x21, 164]
	ldrh	w0, [x22, 8]
	strh	w1, [x21, 160]
	strh	w0, [x21, 164]
.L1203:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x23, x20, 3408
	mov	w24, 61649
	ldrh	w0, [x20, 160]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20, 162]
.L1205:
	tbz	w21, #31, .L1208
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 254
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1207:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 10]
	mov	w2, 65535
	strh	w0, [x1, 166]
	ldrh	w0, [x22, 12]
	cmp	w0, w2
	beq	.L1210
	ldr	w2, [x1, 32]
	cmp	w0, w2
	beq	.L1210
	ldrh	w1, [x1, 46]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L1210
	cmp	w0, w1
	bcs	.L1210
	bl	FtlSysBlkNumInit
.L1210:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	add	x22, x19, 192
	add	x21, x21, :lo12:.LANCHOR3
	mov	w20, 0
.L1211:
	ldrh	w0, [x19, 58]
	cmp	w20, w0
	bcc	.L1212
	mov	w0, 0
.L1195:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1198:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L1196
.L1208:
	ldrh	w0, [x20, 160]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w21, w0, lsl 10
	str	w0, [x23, 4]
	ldr	x0, [x20, 3440]
	str	x0, [x23, 8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1206
	ldrh	w0, [x22]
	cmp	w0, w24
	beq	.L1207
.L1206:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L1205
.L1212:
	ldrh	w2, [x21, -192]
	ldr	x0, [x19, 3416]
	mul	w1, w2, w20
	lsl	w2, w2, 2
	add	w20, w20, 1
	add	x1, x0, x1, lsl 2
	ldr	x0, [x22], 8
	bl	ftl_memcpy
	b	.L1211
.L1213:
	mov	w0, -1
	b	.L1195
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
	add	x20, x0, 3408
.L1226:
	ldrh	w0, [x19, 58]
	cmp	w24, w0
	bcc	.L1232
	add	x20, x21, :lo12:.LANCHOR0
	mov	w19, 0
.L1233:
	ldrh	w0, [x20, 118]
	cmp	w0, w19
	bhi	.L1234
	add	x20, x20, 160
	ldrh	w19, [x20, 12]
	mov	w22, 65535
	sub	w19, w19, #1
	and	w19, w19, 65535
.L1235:
	ldrh	w0, [x20, 12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1239
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1236
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L1237
	mov	w0, w19
	bl	FtlBbmMapBadBlock
.L1236:
	sub	w19, w19, #1
	and	w19, w19, 65535
	b	.L1235
.L1232:
	ldr	x0, [x19, 3440]
	mov	w2, 65535
	ldr	x27, [x19, 3448]
	ldrh	w1, [x25]
	stp	x0, x27, [x20, 8]
	cmp	w1, w2
	beq	.L1227
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
.L1228:
	mov	w0, w23
	add	w24, w24, 1
	bl	FtlBbmMapBadBlock
	add	x26, x26, 8
	add	x25, x25, 2
	b	.L1226
.L1227:
	mov	w1, w24
	bl	FlashGetBadBlockList
	ldr	x0, [x20, 8]
	ldr	x1, [x26]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x19, 102]
.L1230:
	sub	w22, w22, #1
	and	w22, w22, 65535
.L1229:
	ldrh	w0, [x19, 102]
	madd	w0, w24, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1230
	ldr	x0, [x19, 3448]
	mov	w2, 16
	strh	w22, [x25]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x19, 3440]
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
	ldrh	w2, [x28, -192]
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
	bne	.L1228
	mov	w0, w23
	bl	FtlBbmMapBadBlock
	b	.L1229
.L1234:
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	and	w19, w19, 65535
	b	.L1233
.L1237:
	ldrh	w0, [x20]
	cmp	w0, w22
	bne	.L1238
	strh	w19, [x20]
	b	.L1236
.L1238:
	strh	w19, [x20, 4]
.L1239:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, 2
	ldr	x1, [x21, 3968]
	ldrh	w0, [x21, 160]
	str	wzr, [x21, 168]
	strh	wzr, [x21, 162]
	lsl	w0, w0, 10
	str	w0, [x1, 4]
	ldr	x0, [x21, 3968]
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
	bhi	.L1253
	ldrh	w21, [x0, 112]
	adrp	x26, .LANCHOR3
	add	x25, x26, :lo12:.LANCHOR3
	mov	w23, 0
	sub	x25, x25, #112
	lsr	w21, w27, w21
.L1247:
	cbnz	w28, .L1252
.L1245:
	mov	w0, w23
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1252:
	add	x2, x22, :lo12:.LANCHOR0
	add	x0, x26, :lo12:.LANCHOR3
	ldrh	w1, [x2, 62]
	ldr	x0, [x0, -224]
	udiv	w20, w27, w1
	ldr	w4, [x0, w21, uxtw 2]
	and	w0, w28, 65535
	msub	w20, w20, w1, w27
	sub	w19, w1, w20
	and	w19, w19, 65535
	cmp	w28, w19
	csel	w19, w0, w19, cc
	cbz	w4, .L1249
	cmp	w19, w1
	beq	.L1249
	ldr	x0, [x2, 4056]
	mov	w2, 1
	str	x0, [x29, 120]
	add	x0, x29, 144
	mov	w1, w2
	str	x0, [x29, 128]
	str	w4, [x29, 116]
	add	x0, x29, 112
	bl	FlashReadPages
.L1250:
	add	x5, x22, :lo12:.LANCHOR0
	lsl	w4, w19, 9
	lsl	w20, w20, 9
	mov	w2, w4
	asr	w20, w20, 2
	mov	x1, x24
	ldr	x0, [x5, 4056]
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
	ldr	x2, [x5, 4056]
	bl	FtlMapWritePage
	cmn	w0, #1
	ldr	w4, [x29, 108]
	csinv	w23, w23, wzr, ne
	add	x24, x24, x4, sxtw
	b	.L1247
.L1249:
	add	x0, x22, :lo12:.LANCHOR0
	mov	w1, 0
	ldrh	w2, [x0, 114]
	ldr	x0, [x0, 4056]
	bl	ftl_memset
	b	.L1250
.L1253:
	mov	w23, -1
	b	.L1245
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
	bne	.L1261
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 2720
	mov	w1, 69
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
.L1261:
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
	bhi	.L1268
	add	w2, w2, 1
	strh	w2, [x1, 104]
	mov	w2, 1
.L1265:
	adrp	x1, .LANCHOR0
	cbnz	w0, .L1266
	add	x0, x1, :lo12:.LANCHOR0
	ldr	x0, [x0, 4080]
	ldr	w3, [x0, 20]
	ldr	w0, [x0, 16]
	add	w2, w2, w3
	cmp	w0, w2
	bcc	.L1270
.L1266:
	add	x0, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x0, 4080]
	ldr	w2, [x1, 16]
	str	w2, [x1, 20]
	mov	w2, 17221
	movk	w2, 0x4254, lsl 16
	str	w2, [x1]
	ldr	x2, [x0, 4080]
	ldrh	w1, [x0, 4064]
	lsl	w3, w1, 9
	str	wzr, [x2, 4]
	str	w3, [x2, 12]
	ldr	w3, [x2, 8]
	add	w3, w3, 1
	str	w3, [x2, 8]
	ldr	x2, [x0, 4080]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1268:
	mov	w2, 32
	b	.L1265
.L1270:
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
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, w0
	stp	x25, x26, [sp, 64]
	adrp	x24, .LANCHOR0
	mov	x26, x2
	add	w2, w0, w1
	add	x0, x24, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x19, x20, [sp, 16]
	mov	w22, w1
	stp	x27, x28, [sp, 80]
	ldrh	w1, [x0, 100]
	cmp	w2, w1
	bhi	.L1283
	ldrh	w21, [x0, 112]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	mov	w25, 0
	sub	x0, x0, #112
	str	x0, [x29, 104]
	lsr	w21, w23, w21
.L1276:
	cbnz	w22, .L1282
.L1274:
	mov	w0, w25
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1282:
	add	x28, x24, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrh	w19, [x28, 62]
	ldr	x0, [x0, -224]
	udiv	w20, w23, w19
	ldr	w4, [x0, w21, uxtw 2]
	and	w0, w22, 65535
	msub	w20, w20, w19, w23
	sub	w19, w19, w20
	and	w19, w19, 65535
	cmp	w22, w19
	csel	w19, w0, w19, cc
	lsl	w27, w19, 9
	cbz	w4, .L1278
	ldr	x0, [x28, 4056]
	mov	w2, 1
	str	x0, [x29, 120]
	add	x0, x29, 144
	str	w4, [x29, 100]
	mov	w1, w2
	str	w4, [x29, 116]
	str	x0, [x29, 128]
	add	x0, x29, 112
	bl	FlashReadPages
	ldr	w0, [x29, 112]
	ldr	w4, [x29, 100]
	cmn	w0, #1
	ldr	w0, [x28, 3408]
	csinv	w25, w25, wzr, ne
	cmp	w0, 256
	bne	.L1280
	mov	w2, w4
	mov	w1, w21
	adrp	x0, .LC107
	add	x0, x0, :lo12:.LC107
	bl	sftl_printk
	ldr	x2, [x28, 4056]
	mov	w1, w21
	ldr	x0, [x29, 104]
	bl	FtlMapWritePage
.L1280:
	add	x0, x24, :lo12:.LANCHOR0
	lsl	w20, w20, 9
	asr	w20, w20, 2
	mov	w2, w27
	ldr	x1, [x0, 4056]
	mov	x0, x26
	add	x1, x1, x20, sxtw 2
	bl	ftl_memcpy
.L1281:
	add	w21, w21, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x26, x26, x27, sxtw
	b	.L1276
.L1278:
	mov	w2, w27
	mov	w1, 0
	mov	x0, x26
	bl	ftl_memset
	b	.L1281
.L1283:
	mov	w25, -1
	b	.L1274
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
	ldrh	w1, [x19, 4064]
	ldr	x2, [x19, 4080]
	bl	FtlVendorPartRead
	ldr	x0, [x19, 4080]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1286
	adrp	x1, .LC108
	adrp	x0, .LC71
	add	x1, x1, :lo12:.LC108
	add	x0, x0, :lo12:.LC71
	bl	sftl_printk
	ldr	x0, [x19, 4080]
	mov	w1, 0
	ldrh	w2, [x19, 4064]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1286:
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
	beq	.L1289
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	str	w21, [x20, 2720]
.L1289:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0, 2720]
	cmp	w3, w2
	bne	.L1290
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
.L1290:
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
	add	x25, x19, 3408
	mov	w22, 0
	ldr	x21, [x19, 3448]
	mov	w24, 65535
	ldr	x0, [x19, 3440]
	ldrh	w1, [x19, 2656]
	str	x0, [x19, 3416]
	str	x21, [x19, 3424]
	strh	w1, [x21, 2]
	mov	w1, -3932
	strh	w1, [x21]
	str	wzr, [x21, 12]
	ldr	w1, [x19, 2664]
	stp	w1, wzr, [x21, 4]
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	str	w1, [x19, 2312]
	mov	w1, 69
	ldrb	w2, [x19, 2430]
	movk	w1, 0x5000, lsl 16
	str	w1, [x20, 4]
	ldrh	w1, [x19, 2424]
	strh	w1, [x20, 14]
	ldrh	w1, [x19, 2426]
	ldrh	w0, [x19, 2662]
	strh	w0, [x20, 8]
	ldrh	w0, [x19, 58]
	orr	w1, w2, w1, lsl 6
	strb	w0, [x20, 10]
	strh	w1, [x20, 16]
	ldrh	w1, [x19, 2472]
	ldrb	w0, [x19, 2432]
	strh	w1, [x20, 18]
	ldrb	w2, [x19, 2478]
	ldrh	w1, [x19, 2474]
	strb	w0, [x20, 11]
	ldrb	w0, [x19, 2480]
	strb	w0, [x20, 12]
	orr	w1, w2, w1, lsl 6
	ldrb	w0, [x19, 2528]
	strb	w0, [x20, 13]
	ldr	w0, [x19, 2628]
	strh	w1, [x20, 20]
	str	w0, [x20, 32]
	ldrh	w1, [x19, 2520]
	ldr	w0, [x19, 2620]
	strh	w1, [x20, 22]
	ldrb	w2, [x19, 2526]
	str	w0, [x20, 40]
	ldrh	w1, [x19, 2522]
	ldr	w0, [x19, 2624]
	str	w0, [x20, 36]
	ldrh	w0, [x19, 3320]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x20, 44]
	ldrh	w0, [x19, 3322]
	strh	w0, [x20, 46]
	ldrh	w2, [x19, 114]
	ldr	x0, [x19, 3416]
	strh	w1, [x20, 24]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x19, 3416]
	mov	x1, x20
	mov	w2, 48
	mov	x20, x25
	adrp	x25, .LANCHOR1
	add	x25, x25, :lo12:.LANCHOR1
	add	x25, x25, 536
	bl	ftl_memcpy
	ldr	x1, [x19, 2376]
	ldrh	w2, [x19, 44]
	ldr	x0, [x19, 3416]
	lsl	w2, w2, 1
	add	x0, x0, 48
	bl	ftl_memcpy
	ldrh	w0, [x19, 44]
	ldr	x3, [x19, 3416]
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
.L1295:
	ldr	x0, [x19, 3440]
	mov	w3, 1
	str	x0, [x20, 8]
	mov	w2, w3
	ldr	x0, [x19, 3448]
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
	blt	.L1296
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
.L1296:
	ldrh	w0, [x19, 2658]
	ldr	w1, [x20]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2658]
	cmn	w1, #1
	bne	.L1297
	cmp	w0, 1
	bne	.L1298
	adrp	x0, .LC1
	mov	w2, 1169
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1298:
	ldrh	w0, [x19, 2658]
	cmp	w0, 1
	bne	.L1299
	ldrh	w0, [x19, 108]
	sub	w0, w0, #1
	strh	w0, [x19, 2658]
.L1299:
	add	w22, w22, 1
	and	w22, w22, 65535
	cmp	w22, 3
	bls	.L1295
	add	x23, x23, :lo12:.LANCHOR0
	adrp	x0, .LC109
	mov	w2, w22
	add	x0, x0, :lo12:.LC109
	ldr	w1, [x23, 3412]
	bl	sftl_printk
.L1301:
	b	.L1301
.L1297:
	cmp	w0, 1
	beq	.L1295
	cmp	w1, 256
	beq	.L1295
	mov	w0, 65535
	cmp	w24, w0
	beq	.L1302
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
.L1302:
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
	add	w3, w0, w1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w1
	adrp	x20, .LANCHOR0
	add	x1, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ldr	w2, [x1, 144]
	cmp	w3, w2
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
	ldrh	w1, [x1, 62]
	udiv	w21, w0, w1
	msub	w0, w1, w21, w0
	ands	w0, w0, 65535
	beq	.L1321
	sub	w1, w1, w0
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L1321:
	adrp	x22, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR3
	mov	w0, -1
	str	w0, [x29, 60]
.L1322:
	ldrh	w0, [x20, 62]
	cmp	w19, w0
	bcs	.L1324
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	w1, [x0, 108]
	cmp	w1, 32
	bls	.L1334
	str	wzr, [x0, 108]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1334
.L1324:
	mov	w2, 0
	add	x1, x29, 56
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 56]
	cmn	w0, #1
	beq	.L1323
	ldr	w0, [x22, 108]
	mov	w2, 1
	add	x1, x29, 60
	add	w0, w0, 1
	str	w0, [x22, 108]
	ldr	w0, [x20, 2592]
	add	w0, w0, 1
	str	w0, [x20, 2592]
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 56]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1323:
	ldrh	w0, [x20, 62]
	add	w21, w21, 1
	sub	w19, w19, w0
	b	.L1322
.L1326:
	mov	w0, -1
	b	.L1318
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC97
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x1, 552
	add	x0, x0, :lo12:.LC97
	str	x23, [sp, 48]
	bl	sftl_printk
	ldr	x0, [x19, 4088]
	mov	w21, 0
	ldrh	w2, [x19, 46]
	mov	w1, 0
	lsl	w2, w2, 1
	bl	ftl_memset
.L1336:
	ldr	w0, [x19, 2584]
	cmp	w21, w0
	bcc	.L1338
	adrp	x22, .LC110
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC110
	mov	w19, 0
	mov	w23, 65535
.L1339:
	ldrh	w0, [x20, 44]
	cmp	w0, w19
	bhi	.L1343
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L1338:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1337
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x19, 4088]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L1337:
	add	w21, w21, 1
	b	.L1336
.L1343:
	ldr	x0, [x20, 2376]
	ubfiz	x21, x19, 1, 16
	ldrh	w2, [x0, x21]
	ldr	x0, [x20, 4088]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L1341
	cmp	w2, w23
	beq	.L1341
	mov	x0, x22
	mov	w1, w19
	bl	sftl_printk
	ldrh	w0, [x20, 2424]
	cmp	w0, w19
	beq	.L1341
	ldrh	w0, [x20, 2520]
	cmp	w0, w19
	beq	.L1341
	ldrh	w0, [x20, 2472]
	cmp	w0, w19
	beq	.L1341
	ldr	x0, [x20, 4088]
	ldrh	w1, [x0, x21]
	ldr	x0, [x20, 2376]
	strh	w1, [x0, x21]
	mov	w0, w19
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1341:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1339
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
	bcs	.L1349
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2750
	add	x1, x1, 576
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1349:
	mov	w0, 65535
	cmp	w21, w0
	beq	.L1350
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x21, 1, 16
	ldr	x1, [x1, 2376]
	ldrh	w0, [x1, x0]
	cbz	w0, .L1351
	mov	w0, w21
	bl	INSERT_DATA_LIST
.L1350:
	mov	w0, 1
	adrp	x1, .LANCHOR3
	strb	w0, [x22, 8]
	add	x0, x1, :lo12:.LANCHOR3
	mov	w2, 65535
	mov	x20, x1
	ldrh	w0, [x0, -48]
	cmp	w0, w2
	beq	.L1352
	cmp	w21, w0
	bne	.L1353
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x19, 2376]
	ldrh	w1, [x2, x1]
	cbz	w1, .L1354
.L1353:
	bl	update_vpc_list
.L1354:
	add	x1, x20, :lo12:.LANCHOR3
	mov	w0, -1
	strh	w0, [x1, -48]
.L1352:
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
.L1351:
	mov	w0, w21
	bl	INSERT_FREE_LIST
	b	.L1350
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
.L1360:
	cmp	x20, x24
	beq	.L1367
	sub	x23, x20, #4
	b	.L1368
.L1363:
	ldr	w0, [x20]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19]
	cmp	w1, w0, uxth
	bne	.L1361
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
.L1361:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1362
	mov	x0, x19
	bl	allocate_new_data_superblock
.L1362:
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
.L1368:
	ldr	w0, [x20, -4]
	cmn	w0, #1
	beq	.L1363
	ldrb	w1, [x19, 6]
	ldrh	w0, [x21, 36]
	cmp	w1, w0
	bcc	.L1364
	adrp	x0, .LC1
	mov	w2, 941
	add	x1, x25, 608
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1364:
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
	beq	.L1365
	ldr	x2, [x21, 2376]
	ubfiz	x0, x1, 1, 16
	ldrh	w0, [x2, x0]
	cbnz	w0, .L1366
	adrp	x0, .LC111
	mov	w2, 0
	add	x0, x0, :lo12:.LC111
	bl	sftl_printk
.L1366:
	mov	w0, w23
	bl	decrement_vpc_count
.L1365:
	add	x20, x20, 32
	b	.L1360
.L1367:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x22, 36]
	cmp	w1, w0
	bcc	.L1359
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 956
	add	x1, x1, 608
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1359:
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
	bne	.L1375
.L1385:
	adrp	x0, .LANCHOR3-40
	mov	w2, 65535
	str	wzr, [x0, #:lo12:.LANCHOR3-40]
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 2520
	ldrh	w3, [x0, 2520]
	cmp	w3, w2
	bne	.L1398
.L1401:
	mov	w0, 0
.L1374:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1375:
	cbz	w0, .L1378
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w4, [x0, 4]
	cmp	w4, w3
	beq	.L1379
.L1380:
	mov	w1, 2
.L1378:
	add	x21, x19, :lo12:.LANCHOR0
	add	x0, x21, 2520
	bl	FtlGcScanTempBlk
	str	w0, [x29, 92]
	cmn	w0, #1
	beq	.L1381
	ldr	x1, [x21, 2384]
	ubfiz	x20, x20, 1, 16
	ldrh	w0, [x1, x20]
	cmp	w0, 4
	bls	.L1382
	sub	w0, w0, #5
	strh	w0, [x1, x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L1382:
	adrp	x0, .LANCHOR3
	add	x1, x0, :lo12:.LANCHOR3
	mov	x20, x0
	ldr	w1, [x1, -40]
	cbnz	w1, .L1383
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19, 2816]
	add	w0, w0, 1
	str	w0, [x19, 2816]
	ldr	w0, [x29, 92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1383:
	add	x0, x20, :lo12:.LANCHOR3
	str	wzr, [x0, -40]
.L1394:
	mov	w0, 1
	b	.L1374
.L1379:
	strh	wzr, [x0, 4]
	ldrh	w0, [x2, 2416]
	cmp	w0, 17
	bhi	.L1380
	b	.L1378
.L1381:
	adrp	x0, .LANCHOR2+4
	ldrh	w1, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1394
	b	.L1385
.L1398:
	ldrh	w2, [x0, 3304]
	ldrb	w1, [x1, 7]
	ldrh	w0, [x0, 106]
	mul	w0, w1, w0
	cmp	w2, w0
	beq	.L1386
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 162
	add	x1, x1, 624
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1386:
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
.L1387:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21, 3304]
	cmp	w0, w20
	bhi	.L1391
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x21, 2520]
	ldr	x2, [x21, 2376]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1392
	bl	INSERT_DATA_LIST
.L1393:
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
	ble	.L1401
	mov	w0, 20
	strh	w20, [x19, 2672]
	strh	w0, [x19, 3232]
	b	.L1401
.L1391:
	mov	w24, 12
	ldr	x26, [x21, 3312]
	ldr	w0, [x21, 2584]
	umull	x24, w20, w24
	add	x23, x26, x24
	ldr	w1, [x23, 8]
	cmp	w1, w0
	bcc	.L1388
	mov	w2, 168
	mov	x1, x22
	mov	x0, x25
	bl	sftl_printk
.L1388:
	ldr	w0, [x23, 8]
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x26, x24]
	ldr	w1, [x29, 92]
	cmp	w0, w1
	bne	.L1389
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	mov	w21, w0
	ldr	w0, [x23, 8]
	mov	w2, 1
	add	x1, x23, 4
	bl	log2phys
	mov	w0, w21
.L1400:
	bl	decrement_vpc_count
.L1390:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1387
.L1389:
	ldr	w0, [x23, 4]
	cmp	w1, w0
	beq	.L1390
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2520]
	b	.L1400
.L1392:
	bl	INSERT_FREE_LIST
	b	.L1393
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
	bcc	.L1402
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	sub	x0, x19, #184
	bl	FtlMapBlkWriteDump_data
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19, -40]
.L1402:
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
	ldr	x0, [x19, 4032]
	bl	FlashProgPages
	mov	w10, 0
.L1408:
	ldr	w1, [x19, 3248]
	cmp	w10, w1
	bcc	.L1411
	ldr	x0, [x19, 4032]
	bl	FtlGcBufFree
	str	wzr, [x19, 3248]
	ldrh	w0, [x19, 2524]
	cbnz	w0, .L1412
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	b	.L1414
.L1411:
	ldr	x0, [x19, 4032]
	ubfiz	x1, x10, 5, 16
	add	x3, x0, x1
	ldr	w0, [x0, x1]
	cmn	w0, #1
	bne	.L1409
	ldrh	w3, [x19, 2520]
	ldr	x2, [x19, 2376]
	strh	wzr, [x2, x3, lsl 1]
	strh	w0, [x19, 2520]
	ldr	w0, [x19, 2816]
	add	w0, w0, 1
	str	w0, [x19, 2816]
	ldr	x0, [x19, 4032]
	add	x0, x0, x1
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L1414:
	mov	w0, 1
.L1407:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1409:
	ldr	x0, [x3, 16]
	ldr	w1, [x3, 4]
	ldp	w2, w0, [x0, 8]
	bl	FtlGcUpdatePage
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L1408
.L1412:
	mov	w0, 0
	b	.L1407
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
	beq	.L1416
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1417
.L1416:
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
.L1417:
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
	ldr	w2, [x1, 4008]
	cbnz	w2, .L1490
	ldrh	w2, [x1, 2400]
	cmp	w2, 47
	bls	.L1490
	mov	w23, w0
	adrp	x0, .LANCHOR2+4
	ldrh	w2, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1421
.L1424:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w4, [x1, 3322]
	cmp	w4, w2
	bne	.L1422
.L1423:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w20, 65535
	ldr	w1, [x21, 3236]
	ldrh	w0, [x21, 2672]
	add	w1, w1, 1
	add	w1, w1, w23, lsl 7
	str	w1, [x21, 3236]
	cmp	w0, w20
	bne	.L1425
	ldrh	w20, [x21, 2520]
	cmp	w20, w0
	bne	.L1493
	ldrh	w22, [x21, 3320]
	cmp	w22, w20
	bne	.L1425
	ldrh	w0, [x21, 2416]
	mov	w2, 1024
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L1496
	adrp	x0, .LANCHOR3+112
	str	wzr, [x21, 3236]
	strh	wzr, [x0, #:lo12:.LANCHOR3+112]
	bl	GetSwlReplaceBlock
	and	w20, w0, 65535
	cmp	w20, w22
	bne	.L1497
	ldrh	w1, [x21, 2416]
	ldrh	w0, [x21, 3234]
	cmp	w1, w0
	bcs	.L1428
	mov	w0, 64
	bl	List_get_gc_head_node
	and	w0, w0, 65535
	cmp	w0, w20
	beq	.L1430
	ldr	x1, [x21, 2376]
	ubfiz	x0, x0, 1, 16
	ldrh	w0, [x1, x0]
	cmp	w0, 7
	bhi	.L1431
	mov	w0, 0
	bl	List_get_gc_head_node
	and	w22, w0, 65535
	mov	w0, 128
	strh	w0, [x21, 3234]
	cmp	w22, w20
	bne	.L1427
.L1430:
	bl	FtlGcReFreshBadBlk
.L1425:
	cmp	w23, 0
	mov	w0, 65535
	ccmp	w20, w0, 0, eq
	bne	.L1433
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2416]
	cmp	w0, 24
	bhi	.L1498
	cmp	w0, 16
	ldrh	w21, [x1, 106]
	bls	.L1435
	lsr	w21, w21, 5
.L1434:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1, 3232]
	cmp	w2, w0
	bcs	.L1438
	ldrh	w0, [x1, 2520]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1439
	ldrh	w2, [x1, 3320]
	cmp	w2, w0
	bne	.L1439
	adrp	x0, .LANCHOR3+112
	ldrh	w0, [x0, #:lo12:.LANCHOR3+112]
	cbnz	w0, .L1440
	ldr	w2, [x1, 2584]
	ldr	w3, [x1, 2580]
	add	w2, w2, w2, lsl 1
	cmp	w3, w2, lsr 2
	bcs	.L1441
.L1440:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2, 2652]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2, 3232]
.L1442:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19, 3244]
.L1419:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1421:
	ldrh	w1, [x1, 2520]
	cmp	w1, w0
	beq	.L1424
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	cbz	w0, .L1424
	mov	w0, 1
	b	.L1419
.L1422:
	ldrh	w0, [x1, 3320]
	cmp	w0, w2
	bne	.L1423
	ldrh	w3, [x1, 3324]
	cmp	w3, w0
	beq	.L1423
	ldrh	w2, [x1, 3326]
	cmp	w2, w0
	beq	.L1423
	mov	w0, -1
	strh	w4, [x1, 3320]
	strh	w3, [x1, 3322]
	strh	w2, [x1, 3324]
	strh	w0, [x1, 3326]
	b	.L1423
.L1431:
	mov	w0, 64
.L1522:
	strh	w0, [x21, 3234]
	b	.L1430
.L1428:
	mov	w0, 80
	b	.L1522
.L1497:
	mov	w22, w20
.L1427:
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
	adrp	x0, .LC112
	add	x0, x0, :lo12:.LC112
	bl	sftl_printk
	b	.L1430
.L1493:
	mov	w20, w0
	b	.L1425
.L1496:
	mov	w20, w22
	b	.L1425
.L1435:
	cmp	w0, 12
	bls	.L1436
	lsr	w21, w21, 4
	b	.L1434
.L1436:
	mov	w1, w21
	cmp	w0, 9
	lsr	w21, w21, 2
	csel	w21, w21, w1, cs
	b	.L1434
.L1498:
	mov	w21, 1
	b	.L1434
.L1441:
	mov	w2, 18
	strh	w2, [x1, 3232]
	b	.L1442
.L1439:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2652]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 3232]
.L1438:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 3328]
	cbz	w0, .L1499
	add	w21, w21, 32
	and	w21, w21, 65535
.L1499:
	mov	w20, 65535
.L1444:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 2672]
	cmp	w1, w2
	bne	.L1453
	cmp	w20, w1
	beq	.L1454
	strh	w20, [x0, 2672]
.L1455:
	add	x5, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldrh	w0, [x5, 2672]
	strb	wzr, [x5, 2680]
	cmp	w0, w1
	beq	.L1453
	bl	IsBlkInGcList
	cbz	w0, .L1458
	mov	w0, -1
	strh	w0, [x5, 2672]
.L1458:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	add	x23, x22, 2672
	ldrh	w1, [x22, 2672]
	cmp	w1, w0
	beq	.L1453
	mov	x0, x23
	bl	make_superblock
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrh	w2, [x22, 2672]
	ldr	x1, [x22, 2376]
	strh	wzr, [x22, 2674]
	strb	wzr, [x22, 2678]
	strh	wzr, [x0, 114]
	ldrh	w1, [x1, x2, lsl 1]
	strh	w1, [x0, 116]
.L1453:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w2, [x0, 2424]
	cmp	w2, w1
	beq	.L1459
	ldrh	w0, [x0, 2472]
	cmp	w0, w1
	beq	.L1459
	adrp	x24, .LANCHOR1
	add	x24, x24, :lo12:.LANCHOR1
	add	x24, x24, 648
.L1460:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w23, [x22, 2672]
	cmp	w23, w0
	bne	.L1461
	mov	w25, 2
	str	wzr, [x22, 3244]
.L1462:
	ldrh	w5, [x22, 3240]
	mov	w0, w5
	bl	List_get_gc_head_node
	and	w6, w0, 65535
	strh	w6, [x22, 2672]
	cmp	w6, w23
	bne	.L1463
	strh	wzr, [x22, 3240]
	mov	w0, 8
	b	.L1419
.L1433:
	add	x5, x19, :lo12:.LANCHOR0
	ldrh	w1, [x5, 2520]
	cmp	w1, w0
	bne	.L1502
	ldrh	w0, [x5, 3320]
	cmp	w0, w1
	bne	.L1502
	cmp	w20, w0
	bne	.L1502
	ldrh	w0, [x5, 2672]
	cmp	w0, w20
	beq	.L1445
.L1502:
	mov	w21, 1
	b	.L1444
.L1445:
	ldrh	w7, [x5, 2416]
	adrp	x21, .LANCHOR3
	ldrh	w0, [x5, 3232]
	str	wzr, [x5, 3244]
	cmp	w0, w7
	bcs	.L1446
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, 112]
	cbnz	w0, .L1447
	ldr	w0, [x5, 2584]
	ldr	w1, [x5, 2580]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bcs	.L1448
.L1447:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2652]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x19, 3232]
.L1449:
	bl	FtlReadRefresh
.L1523:
	add	x21, x21, :lo12:.LANCHOR3
	b	.L1524
.L1448:
	mov	w0, 18
	strh	w0, [x5, 3232]
	b	.L1449
.L1446:
	add	x21, x21, :lo12:.LANCHOR3
	ldrh	w0, [x21, 112]
	cbnz	w0, .L1502
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
	ble	.L1451
	sub	w6, w6, #1
	cmp	w7, w6
	blt	.L1451
	bl	FtlReadRefresh
.L1524:
	ldrh	w0, [x21, 112]
	b	.L1419
.L1451:
	cbnz	w1, .L1502
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x19, 2416]
	add	w0, w0, 1
	b	.L1419
.L1454:
	ldrh	w1, [x0, 3320]
	cmp	w1, w20
	beq	.L1455
	ldr	x2, [x0, 2376]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2, x1]
	cbnz	w1, .L1456
	mov	w1, -1
	strh	w1, [x0, 3320]
.L1456:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3320]
	strh	w1, [x0, 2672]
	mov	w1, -1
	strh	w1, [x0, 3320]
	b	.L1455
.L1459:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19, 2672]
.L1525:
	adrp	x0, .LANCHOR3+112
	ldrh	w0, [x0, #:lo12:.LANCHOR3+112]
	b	.L1419
.L1463:
	mov	w0, w6
	bl	IsBlkInGcList
	add	w5, w5, 1
	cbz	w0, .L1464
	strh	w5, [x22, 3240]
	b	.L1462
.L1464:
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
	bgt	.L1466
	cmp	w5, 48
	bls	.L1467
	cmp	w3, 8
	bls	.L1467
	ldrh	w3, [x22, 3292]
	cmp	w3, 35
	bhi	.L1467
.L1466:
	strh	wzr, [x22, 3240]
.L1467:
	ldrh	w1, [x2, x1]
	cmp	w0, w1
	bgt	.L1468
	cmp	w20, w23
	bne	.L1468
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19, 3240]
	strh	w0, [x19, 2672]
	b	.L1525
.L1468:
	cbnz	w1, .L1469
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x22, 3240]
	add	w0, w0, 1
	strh	w0, [x22, 3240]
	b	.L1462
.L1469:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0, 2680]
	ldrh	w0, [x0, 2424]
	cmp	w0, w6
	bne	.L1470
	adrp	x0, .LC1
	mov	w2, 665
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1470:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w0, [x0, 2472]
	cmp	w1, w0
	bne	.L1471
	adrp	x0, .LC1
	mov	w2, 666
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1471:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2672]
	ldrh	w0, [x0, 2520]
	cmp	w1, w0
	bne	.L1472
	adrp	x0, .LC1
	mov	w2, 667
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1472:
	add	x23, x19, :lo12:.LANCHOR0
	add	x22, x23, 2672
	mov	x0, x22
	bl	make_superblock
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrh	w2, [x23, 2672]
	ldr	x1, [x23, 2376]
	strh	wzr, [x0, 114]
	ldrh	w1, [x1, x2, lsl 1]
	strh	w1, [x0, 116]
	strh	wzr, [x23, 2674]
	strb	wzr, [x23, 2678]
.L1461:
	bl	FtlReadRefresh
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	ldrh	w26, [x0, 106]
	str	w1, [x0, 4008]
	ldrh	w0, [x0, 2674]
	add	w1, w0, w21
	cmp	w1, w26
	ble	.L1473
	sub	w21, w26, w0
	and	w21, w21, 65535
.L1473:
	mov	w25, 0
.L1474:
	cmp	w21, w25, uxth
	bls	.L1482
	add	x0, x19, :lo12:.LANCHOR0
	mov	w23, 0
	add	x3, x0, 2688
	mov	w1, 0
	mov	w6, 65535
	ldrh	w4, [x0, 2674]
	ldrh	w7, [x0, 36]
	add	w4, w4, w25
	b	.L1483
.L1476:
	ldrh	w2, [x3]
	cmp	w2, w6
	beq	.L1475
	ldr	x5, [x0, 3280]
	ubfiz	x8, x23, 5, 16
	add	w23, w23, 1
	orr	w2, w4, w2, lsl 10
	add	x5, x5, x8
	and	w23, w23, 65535
	str	w2, [x5, 4]
.L1475:
	add	w1, w1, 1
	add	x3, x3, 2
	and	w1, w1, 65535
.L1483:
	cmp	w1, w7
	bne	.L1476
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, w23
	mov	x27, 0
	ldrb	w2, [x22, 2680]
	ldr	x0, [x22, 3280]
	bl	FlashReadPages
	ubfiz	x0, x23, 5, 16
	str	x0, [x29, 104]
.L1477:
	ldr	x0, [x29, 104]
	cmp	x27, x0
	bne	.L1481
	add	w25, w25, 1
	b	.L1474
.L1481:
	ldr	x0, [x22, 3280]
	add	x1, x0, x27
	ldr	w0, [x0, x27]
	cmn	w0, #1
	beq	.L1478
	ldr	x23, [x1, 16]
	mov	w0, 61589
	ldrh	w1, [x23]
	cmp	w1, w0
	bne	.L1478
	ldr	w28, [x23, 8]
	cmn	w28, #1
	bne	.L1479
	adrp	x0, .LC1
	mov	w2, 703
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1479:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w28
	bl	log2phys
	ldr	x0, [x22, 3280]
	ldr	w1, [x29, 124]
	add	x0, x0, x27
	ldr	w2, [x0, 4]
	cmp	w2, w1
	bne	.L1478
	adrp	x28, .LANCHOR3
	add	x2, x28, :lo12:.LANCHOR3
	ldr	w0, [x0, 24]
	ldrh	w1, [x2, 114]
	add	w1, w1, 1
	strh	w1, [x2, 114]
	ldr	x2, [x22, 4032]
	ldr	w1, [x22, 3248]
	add	x1, x2, x1, lsl 5
	str	x1, [x29, 96]
	str	w0, [x1, 24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29, 96]
	str	w0, [x1, 4]
	ldr	w0, [x22, 3248]
	ldr	x1, [x22, 4032]
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
	beq	.L1480
	ldrh	w0, [x22, 2524]
	cbnz	w0, .L1478
.L1480:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1478
	add	x19, x19, :lo12:.LANCHOR0
	add	x28, x28, :lo12:.LANCHOR3
	str	wzr, [x19, 4008]
	ldrh	w0, [x28, 112]
	b	.L1419
.L1478:
	add	x27, x27, 32
	b	.L1477
.L1482:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 2674]
	add	w21, w21, w0
	and	w21, w21, 65535
	strh	w21, [x22, 2674]
	cmp	w26, w21
	bhi	.L1484
	ldr	w0, [x22, 3248]
	adrp	x21, .LANCHOR3
	cbz	w0, .L1485
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1485
	str	wzr, [x22, 4008]
	b	.L1523
.L1485:
	add	x21, x21, :lo12:.LANCHOR3
	ldrh	w0, [x21, 114]
	cbnz	w0, .L1486
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w1, [x22, 2672]
	ldr	x3, [x22, 2376]
	ubfiz	x2, x1, 1, 16
	ldrh	w4, [x3, x2]
	cbz	w4, .L1486
	ldrh	w5, [x21, 116]
	mov	w3, 0
	ldrh	w2, [x22, 2674]
	adrp	x0, .LC113
	add	x0, x0, :lo12:.LC113
	bl	sftl_printk
	ldrh	w1, [x22, 2672]
	ldr	x0, [x22, 2376]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x22, 2672]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1486:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 2672]
.L1484:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2416]
	cmp	w0, 2
	bhi	.L1487
	ldrh	w21, [x1, 106]
	b	.L1460
.L1487:
	str	wzr, [x1, 4008]
	adrp	x1, .LANCHOR3+112
	ldrh	w1, [x1, #:lo12:.LANCHOR3+112]
	cmp	w1, 0
	csinc	w0, w1, w0, ne
	b	.L1419
.L1490:
	mov	w0, 0
	b	.L1419
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
	sub	sp, sp, #192
	and	w0, w0, 255
	cmp	w0, 16
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	mov	w19, w1
	stp	x23, x24, [sp, 64]
	mov	x23, x3
	stp	x27, x28, [sp, 96]
	mov	w27, w2
	stp	x21, x22, [sp, 48]
	stp	x25, x26, [sp, 80]
	bne	.L1529
	mov	x2, x3
	mov	w1, w27
	add	w0, w19, 256
	bl	FtlVendorPartRead
	mov	w21, w0
.L1528:
	mov	w0, w21
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 192
	ret
.L1529:
	add	w0, w1, w2
	str	w0, [x29, 144]
	adrp	x0, .LANCHOR0
	add	x8, x0, :lo12:.LANCHOR0
	add	w2, w1, w2
	str	x0, [x29, 112]
	ldr	w1, [x8, 144]
	cmp	w2, w1
	bhi	.L1550
	adrp	x0, .LANCHOR2
	ldr	w21, [x0, #:lo12:.LANCHOR2]
	cmn	w21, #1
	beq	.L1528
	ldrh	w0, [x8, 62]
	sub	w24, w2, #1
	mov	x25, x8
	mov	w28, 0
	mov	w21, 0
	stp	wzr, wzr, [x29, 148]
	str	wzr, [x29, 156]
	udiv	w26, w19, w0
	udiv	w24, w24, w0
	ldr	w0, [x8, 2588]
	mov	w20, w26
	sub	w22, w24, w26
	add	w22, w22, 1
	add	w0, w0, w22
	str	w0, [x8, 2588]
	adrp	x0, .LC96
	add	x0, x0, :lo12:.LC96
	str	x0, [x29, 104]
.L1531:
	cbnz	w22, .L1548
	ldr	x0, [x29, 112]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3328]
	cbnz	w1, .L1549
	ldrh	w0, [x0, 2416]
	cmp	w0, 31
	bhi	.L1528
.L1549:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	b	.L1528
.L1548:
	add	x1, x29, 172
	mov	w2, 0
	mov	w0, w20
	bl	log2phys
	ldr	w1, [x29, 172]
	cmn	w1, #1
	bne	.L1532
	mov	w4, 0
.L1533:
	ldrh	w0, [x25, 62]
	cmp	w4, w0
	bcc	.L1535
.L1536:
	add	w20, w20, 1
	subs	w22, w22, #1
	beq	.L1540
	ldrh	w0, [x25, 36]
	cmp	w28, w0, lsl 2
	bne	.L1531
.L1540:
	cbz	w28, .L1531
	ldr	x0, [x25, 4024]
	mov	w1, w28
	mov	w2, 0
	bl	FlashReadPages
	ldr	w0, [x29, 148]
	lsl	w0, w0, 9
	str	w0, [x29, 128]
	ldr	w0, [x29, 156]
	lsl	w0, w0, 9
	str	x0, [x29, 136]
	ldr	w0, [x29, 152]
	lsl	w0, w0, 9
	str	w0, [x29, 132]
	ubfiz	x0, x28, 5, 32
	mov	x28, 0
	str	x0, [x29, 120]
.L1547:
	ldr	x0, [x25, 4024]
	add	x0, x0, x28
	ldr	w1, [x0, 24]
	cmp	w26, w1
	bne	.L1542
	ldr	x1, [x0, 8]
	ldr	x0, [x25, 4000]
	cmp	x1, x0
	bne	.L1543
	ldr	x0, [x29, 136]
	ldr	w2, [x29, 132]
	add	x1, x1, x0
	mov	x0, x23
.L1560:
	bl	ftl_memcpy
.L1543:
	ldr	x0, [x25, 4024]
	add	x0, x0, x28
	ldr	x1, [x0, 16]
	ldr	w2, [x0, 24]
	ldr	w1, [x1, 8]
	cmp	w2, w1
	beq	.L1544
	ldr	w1, [x25, 2792]
	add	w1, w1, 1
	str	w1, [x25, 2792]
	ldp	x2, x1, [x0, 8]
	ldr	w3, [x2, 4]
	str	w3, [sp]
	ldp	w3, w4, [x1]
	ldp	w5, w6, [x1, 8]
	ldr	w7, [x2]
	ldr	w1, [x0, 24]
	ldr	w2, [x0, 4]
	ldr	x0, [x29, 104]
	bl	sftl_printk
.L1544:
	ldr	x0, [x25, 4024]
	add	x1, x0, x28
	ldr	w2, [x0, x28]
	cmn	w2, #1
	bne	.L1545
	ldr	w0, [x25, 2792]
	mov	w21, w2
	add	w0, w0, 1
	str	w0, [x25, 2792]
.L1545:
	ldr	w0, [x1]
	cmp	w0, 256
	bne	.L1546
	ldr	w0, [x1, 4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1546:
	ldr	x0, [x29, 120]
	add	x28, x28, 32
	cmp	x0, x28
	bne	.L1547
	mov	w28, 0
	b	.L1531
.L1535:
	madd	w0, w20, w0, w4
	cmp	w19, w0
	bhi	.L1534
	ldr	w1, [x29, 144]
	cmp	w1, w0
	bls	.L1534
	sub	w0, w0, w19
	str	w4, [x29, 136]
	lsl	w0, w0, 9
	mov	w2, 512
	mov	w1, 0
	add	x0, x23, x0
	bl	ftl_memset
	ldr	w4, [x29, 136]
.L1534:
	add	w4, w4, 1
	b	.L1533
.L1532:
	ldr	x0, [x25, 4024]
	ubfiz	x2, x28, 5, 32
	cmp	w20, w26
	add	x0, x0, x2
	str	w1, [x0, 4]
	ldrh	w0, [x25, 62]
	bne	.L1537
	ldr	x1, [x25, 4024]
	ldr	x4, [x25, 4000]
	add	x1, x1, x2
	str	x4, [x1, 8]
	udiv	w4, w19, w0
	msub	w3, w4, w0, w19
	str	w3, [x29, 156]
	sub	w4, w0, w3
	cmp	w27, w4
	csel	w3, w27, w4, ls
	str	w3, [x29, 152]
	cmp	w3, w0
	bne	.L1538
	str	x23, [x1, 8]
.L1538:
	ldr	x0, [x25, 4024]
	ldr	x1, [x25, 3992]
	add	x2, x0, x2
	ldrh	w0, [x25, 116]
	str	w20, [x2, 24]
	mul	w0, w0, w28
	add	w28, w28, 1
	and	x0, x0, 4294967292
	add	x0, x1, x0
	str	x0, [x2, 16]
	b	.L1536
.L1537:
	cmp	w20, w24
	bne	.L1539
	ldr	x4, [x25, 4024]
	ldr	x1, [x25, 3984]
	add	x4, x4, x2
	ldr	w3, [x29, 144]
	str	x1, [x4, 8]
	mul	w1, w20, w0
	sub	w3, w3, w1
	str	w3, [x29, 148]
	cmp	w0, w3
	bne	.L1538
	sub	w1, w1, w19
	lsl	w1, w1, 9
	add	x1, x23, x1
	str	x1, [x4, 8]
	b	.L1538
.L1539:
	ldr	x1, [x25, 4024]
	mul	w0, w0, w20
	add	x1, x1, x2
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	str	x0, [x1, 8]
	b	.L1538
.L1542:
	cmp	w24, w1
	bne	.L1543
	ldr	x1, [x0, 8]
	ldr	x0, [x25, 3984]
	cmp	x1, x0
	bne	.L1543
	ldrh	w0, [x25, 62]
	ldr	w2, [x29, 128]
	mul	w0, w0, w24
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	b	.L1560
.L1550:
	mov	w21, -1
	b	.L1528
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
	mov	w24, w1
	stp	x27, x28, [sp, 80]
	mov	x27, x3
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	str	w2, [x29, 156]
	bne	.L1564
	add	w0, w1, 256
	ldr	w1, [x29, 156]
	mov	x2, x3
	bl	FtlVendorPartWrite
.L1563:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	ret
.L1564:
	ldr	w0, [x29, 156]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	add	w0, w24, w0
	str	w0, [x29, 148]
	ldr	w0, [x1, 144]
	ldr	w2, [x29, 148]
	cmp	w2, w0
	bhi	.L1599
	adrp	x2, .LANCHOR2
	str	x2, [x29, 136]
	add	x3, x2, :lo12:.LANCHOR2
	ldr	w0, [x2, #:lo12:.LANCHOR2]
	cmn	w0, #1
	beq	.L1563
	adrp	x0, .LANCHOR3+120
	mov	w2, 2048
	add	x20, x1, 2424
	str	w2, [x0, #:lo12:.LANCHOR3+120]
	ldr	w2, [x29, 148]
	ldrh	w0, [x1, 62]
	sub	w2, w2, #1
	udiv	w26, w24, w0
	udiv	w0, w2, w0
	str	w0, [x29, 144]
	sub	w0, w0, w26
	add	w25, w0, 1
	str	w0, [x29, 132]
	ldr	w0, [x1, 2596]
	add	w0, w0, w25
	str	w0, [x1, 2596]
	ldr	w0, [x3, 8]
	cbz	w0, .L1566
	ldrh	w0, [x20, 4]
	add	x1, x1, 2472
	cmp	w0, 0
	csel	x20, x1, x20, eq
.L1566:
	adrp	x22, .LANCHOR1
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, w26
	add	x22, x22, 672
.L1567:
	cbnz	w25, .L1595
	ldr	w1, [x29, 132]
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x19, 2416]
	cmp	w0, 15
	bls	.L1596
.L1598:
	mov	w0, 0
	b	.L1563
.L1595:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1568
	adrp	x0, .LC1
	mov	w2, 996
	mov	x1, x22
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1568:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1569
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 2424
	cmp	x20, x1
	bne	.L1570
	add	x0, x0, 2472
	ldrh	w1, [x0, 4]
	cbnz	w1, .L1571
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
.L1571:
	add	x20, x19, :lo12:.LANCHOR0
	add	x23, x20, 2424
	add	x20, x20, 2472
	mov	x0, x23
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	cmp	w0, 0
	csel	x20, x20, x23, ne
.L1572:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1569
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1569:
	ldrh	w0, [x20, 4]
	ldrb	w1, [x20, 7]
	cmp	w0, w25
	csel	w0, w0, w25, ls
	lsl	w1, w1, 2
	cmp	w1, w0
	csel	w0, w1, w0, ls
	str	w0, [x29, 128]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1573
	adrp	x0, .LC1
	mov	w2, 1029
	mov	x1, x22
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1573:
	str	xzr, [x29, 168]
.L1574:
	ldr	w1, [x29, 128]
	ldr	w23, [x29, 168]
	cmp	w23, w1
	bcc	.L1593
	mov	x23, x1
.L1575:
	add	x0, x19, :lo12:.LANCHOR0
	mov	x3, x20
	mov	w2, 0
	mov	w1, w23
	ldr	x0, [x0, 4040]
	bl	FtlProgPages
	cmp	w25, w23
	bcs	.L1594
	adrp	x0, .LC1
	mov	w2, 1106
	mov	x1, x22
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1594:
	sub	w25, w25, w23
	b	.L1567
.L1570:
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1604
	mov	x0, x20
	bl	allocate_new_data_superblock
	b	.L1572
.L1604:
	mov	x20, x1
	b	.L1569
.L1593:
	ldrh	w0, [x20, 4]
	cbz	w0, .L1575
	add	x1, x29, 188
	mov	w2, 0
	mov	w0, w21
	bl	log2phys
	mov	x0, x20
	bl	get_new_active_ppa
	add	x4, x19, :lo12:.LANCHOR0
	ldr	x1, [x29, 168]
	str	x4, [x29, 160]
	lsl	x28, x1, 5
	ldr	x1, [x4, 4040]
	ldrh	w2, [x4, 116]
	add	x1, x1, x28
	str	w0, [x1, 4]
	mul	w23, w23, w2
	ldr	x0, [x4, 4040]
	and	x1, x23, 4294967292
	str	x1, [x29, 120]
	add	x0, x0, x28
	ldr	x1, [x4, 3992]
	ldr	x3, [x29, 120]
	str	x1, [x29, 112]
	add	x23, x1, x3
	str	w21, [x0, 24]
	str	x23, [x0, 16]
	mov	w1, 0
	mov	x0, x23
	bl	ftl_memset
	ldr	w0, [x29, 144]
	cmp	w21, w26
	ldr	x4, [x29, 160]
	ccmp	w21, w0, 4, ne
	bne	.L1576
	cmp	w21, w26
	bne	.L1577
	ldrh	w2, [x4, 62]
	udiv	w0, w24, w2
	msub	w0, w0, w2, w24
	str	w0, [x29, 152]
	sub	w2, w2, w0
	ldr	w0, [x29, 156]
	cmp	w2, w0
	csel	w0, w2, w0, ls
	str	w0, [x29, 160]
.L1578:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29, 160]
	ldrh	w1, [x0, 62]
	cmp	w2, w1
	bne	.L1579
	ldr	x0, [x0, 4040]
	cmp	w21, w26
	add	x3, x0, x28
	bne	.L1580
	str	x27, [x3, 8]
.L1581:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 36]
	cmp	w1, w0
	bcc	.L1590
	adrp	x0, .LC1
	mov	w2, 1097
	mov	x1, x22
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1590:
	ldp	x1, x2, [x29, 112]
	mov	w0, -3947
	strh	w0, [x1, x2]
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w0, [x1, 2624]
	stp	w0, w21, [x23, 4]
	add	w21, w21, 1
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1, 2624]
	ldr	w0, [x29, 188]
	str	w0, [x23, 12]
	ldrh	w0, [x20]
	strh	w0, [x23, 2]
	ldr	x0, [x29, 168]
	add	x0, x0, 1
	str	x0, [x29, 168]
	b	.L1574
.L1577:
	ldr	w0, [x29, 148]
	ldrh	w2, [x4, 62]
	str	wzr, [x29, 152]
	msub	w2, w21, w2, w0
	and	w0, w2, 65535
	str	w0, [x29, 160]
	b	.L1578
.L1580:
	ldr	w0, [x29, 160]
.L1611:
	mul	w0, w0, w21
	sub	w0, w0, w24
	lsl	w0, w0, 9
	add	x0, x27, x0
	str	x0, [x3, 8]
	b	.L1581
.L1579:
	ldr	x1, [x0, 4040]
	cmp	w21, w26
	add	x1, x1, x28
	bne	.L1582
	ldr	x0, [x0, 4000]
.L1610:
	str	x0, [x1, 8]
	ldr	w0, [x29, 188]
	cmn	w0, #1
	beq	.L1584
	add	x4, x19, :lo12:.LANCHOR0
	str	w0, [x29, 196]
	str	w21, [x29, 216]
	mov	w2, 0
	str	x4, [x29, 104]
	ldr	x0, [x4, 4040]
	add	x0, x0, x28
	ldp	x1, x0, [x0, 8]
	stp	x1, x0, [x29, 200]
	mov	w1, 1
	add	x0, x29, 192
	bl	FlashReadPages
	ldr	w0, [x29, 192]
	ldr	x4, [x29, 104]
	cmn	w0, #1
	bne	.L1585
	ldr	w0, [x4, 2792]
	add	w0, w0, 1
	str	w0, [x4, 2792]
.L1588:
	ldr	w0, [x29, 160]
	cmp	w21, w26
	lsl	w2, w0, 9
	add	x0, x19, :lo12:.LANCHOR0
	bne	.L1589
	ldr	x0, [x0, 4040]
	mov	x1, x27
	add	x3, x0, x28
	ldr	w0, [x29, 152]
	ldr	x3, [x3, 8]
	lsl	w0, w0, 9
	add	x0, x3, x0
.L1612:
	bl	ftl_memcpy
	b	.L1581
.L1582:
	ldr	x0, [x0, 3984]
	b	.L1610
.L1585:
	ldr	w0, [x23, 8]
	cmp	w21, w0
	beq	.L1587
	ldr	w0, [x4, 2792]
	mov	w2, w21
	add	w0, w0, 1
	str	w0, [x4, 2792]
	adrp	x0, .LC114
	add	x0, x0, :lo12:.LC114
	ldr	w1, [x23, 8]
	bl	sftl_printk
.L1587:
	ldr	w0, [x23, 8]
	cmp	w21, w0
	beq	.L1588
	mov	w2, 1082
	mov	x1, x22
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	b	.L1588
.L1584:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x0, [x1, 4040]
	ldrh	w2, [x1, 114]
	mov	w1, 0
	add	x0, x0, x28
	ldr	x0, [x0, 8]
	bl	ftl_memset
	b	.L1588
.L1589:
	ldrh	w1, [x0, 62]
	ldr	x0, [x0, 4040]
	add	x3, x0, x28
	mul	w1, w1, w21
	sub	w1, w1, w24
	ldr	x0, [x3, 8]
	lsl	w1, w1, 9
	add	x1, x27, x1
	b	.L1612
.L1576:
	ldr	x0, [x4, 4040]
	add	x3, x0, x28
	ldrh	w0, [x4, 62]
	b	.L1611
.L1596:
	mov	w21, 65535
	mov	w20, 128
.L1608:
	ldrh	w0, [x19, 2672]
	cmp	w0, w21
	bne	.L1597
	ldrh	w0, [x19, 3320]
	cmp	w0, w21
	bne	.L1597
	mov	w0, 0
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L1597:
	mov	w1, 1
	strh	w20, [x19, 3234]
	mov	w0, w1
	strh	w20, [x19, 3232]
	bl	rk_ftl_garbage_collect
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x19, 2416]
	cmp	w0, 8
	bls	.L1608
	b	.L1598
.L1599:
	mov	w0, -1
	b	.L1563
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_write
	.type	sftl_write, %function
sftl_write:
	stp	x29, x30, [sp, -224]!
	add	w4, w0, w1
	sub	w5, w4, #1
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	cmp	w5, 63
	stp	x23, x24, [sp, 48]
	mov	w22, w0
	stp	x25, x26, [sp, 64]
	mov	w24, w1
	stp	x19, x20, [sp, 16]
	mov	x25, x2
	stp	x27, x28, [sp, 80]
	bls	.L1614
	cmp	w0, 576
	bls	.L1615
.L1614:
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	w0, [x0, 124]
	cbz	w0, .L1616
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	mov	w23, 512
	adrp	x1, idb_buf
	mov	x5, -49148
	add	x3, x1, :lo12:idb_buf
	ldrh	w26, [x0, 14]
	mov	x2, 0
	ldrh	w0, [x0, 10]
	mov	x8, 262140
	and	w21, w26, 255
	mov	w6, 4097
	movk	x5, 0xfffc, lsl 16
	mul	w21, w21, w0
	mov	w0, 0
	and	w21, w21, 65535
	udiv	w23, w23, w21
.L1620:
	add	x4, x2, x8
	ldr	w7, [x4, x3]
	cbz	w7, .L1617
.L1621:
	mul	w0, w21, w23
	add	x20, x20, :lo12:.LANCHOR0
	stp	w0, wzr, [x29, 152]
	mov	w19, 0
	add	x0, x1, :lo12:idb_buf
	mov	w27, 0
	str	x0, [x29, 136]
.L1618:
	adrp	x5, gp_flash_check_buf
	mov	x2, 512
	add	x0, x5, :lo12:gp_flash_check_buf
	str	x5, [x29, 104]
	mov	w1, 0
	bl	memset
	ldrh	w3, [x20, 14]
	add	x4, x20, 3376
	ldrh	w2, [x20, 10]
	mov	w0, 0
	and	w28, w3, 255
	ldr	x6, [x20, 3384]
	str	x4, [x29, 112]
	udiv	w1, w19, w3
	stp	w3, w28, [x29, 124]
	mul	w2, w28, w2
	and	w2, w2, 65535
	str	w2, [x29, 144]
	blr	x6
	ldp	x5, x4, [x29, 104]
	ldr	w2, [x29, 144]
	str	x5, [x29, 144]
	ldr	w3, [x29, 124]
	cmp	w2, 511
	bhi	.L1622
	add	w1, w2, w19
	ldr	x4, [x4, 8]
	str	w2, [x29, 124]
	mov	w0, 0
	udiv	w1, w1, w3
	blr	x4
	ldr	w2, [x29, 124]
.L1622:
	udiv	w5, w19, w2
	mov	w4, 0
	mov	w7, 61424
	msub	w5, w5, w2, w19
	sub	w6, w19, w5
.L1623:
	cmp	w4, 511
	bls	.L1624
	ldrh	w4, [x20, 14]
	mov	w5, 0
	ldrh	w0, [x20, 10]
	and	w28, w4, 255
	mov	w4, w28
	mul	w0, w28, w0
	and	w0, w0, 65535
	udiv	w6, w19, w0
	msub	w6, w6, w0, w19
	and	w1, w6, 65535
	sub	w7, w19, w6
	sdiv	w0, w1, w28
	msub	w0, w0, w28, w1
	ldr	x1, [x29, 144]
	and	w0, w0, 65535
	add	x9, x1, :lo12:gp_flash_check_buf
.L1625:
	cmp	w5, 511
	bhi	.L1629
	add	w1, w6, w5
	sub	w8, w4, w0
	add	w0, w0, w7
	ldr	x10, [x20, 3400]
	and	w8, w8, 65535
	ubfiz	x2, x5, 9, 16
	udiv	w1, w1, w28
	add	x2, x9, x2
	stp	w4, w5, [x29, 100]
	add	x3, x29, 160
	str	w6, [x29, 112]
	stp	w8, w7, [x29, 120]
	and	w1, w1, 65535
	str	x9, [x29, 128]
	madd	w1, w1, w28, w0
	mov	w0, 0
	udiv	w1, w1, w28
	blr	x10
	cmn	w0, #1
	ldp	w4, w5, [x29, 100]
	ldr	w6, [x29, 112]
	ldp	w8, w7, [x29, 120]
	ldr	x9, [x29, 128]
	bne	.L1626
.L1629:
	ldr	x0, [x29, 144]
	mov	x28, 0
	add	x5, x0, :lo12:gp_flash_check_buf
.L1627:
	ldr	x1, [x29, 136]
	lsl	x0, x28, 2
	ldr	w3, [x0, x1]
	ldr	w0, [x0, x5]
	cmp	w0, w3
	beq	.L1630
	mov	x2, 512
	mov	w1, 0
	mov	x0, x5
	stp	w28, w3, [x29, 124]
	str	x5, [x29, 144]
	bl	memset
	ldp	w6, w3, [x29, 124]
	mov	w4, w28
	ldr	x5, [x29, 144]
	mov	w1, w19
	adrp	x0, .LC115
	add	x0, x0, :lo12:.LC115
	ldr	w2, [x5, w6, uxtw 2]
	bl	sftl_printk
	udiv	w1, w19, w26
	ldr	x2, [x20, 3384]
	mov	w0, 0
	blr	x2
	cmp	w23, 1
	bls	.L1631
	add	w1, w21, w19
	ldr	x2, [x20, 3384]
	mov	w0, 0
	udiv	w1, w1, w26
	blr	x2
.L1631:
	ldr	w0, [x29, 152]
	add	w27, w27, w23
	cmp	w27, 15
	add	w19, w19, w0
	bls	.L1618
.L1636:
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	str	wzr, [x0, 124]
.L1616:
	mov	x3, x25
	mov	w2, w24
	mov	w1, w22
	mov	w0, 0
	bl	FtlWrite
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	ret
.L1617:
	ldr	w7, [x3, w0, uxtw 2]
	add	w0, w0, 1
	str	w7, [x4, x3]
	cmp	w0, w6
	sub	x2, x2, #4
	csel	w0, w0, wzr, cc
	cmp	x2, x5
	bne	.L1620
	b	.L1621
.L1624:
	add	w0, w5, w4
	ubfiz	x2, x4, 9, 16
	str	w4, [x29, 112]
	add	x3, x29, 160
	ldr	x4, [x29, 136]
	udiv	w0, w0, w28
	ldr	x8, [x20, 3392]
	str	w7, [x29, 104]
	add	x2, x4, x2
	stp	w5, w6, [x29, 120]
	and	w0, w0, 65535
	mul	w1, w0, w28
	stp	w1, w7, [x29, 160]
	udiv	w1, w6, w28
	add	w1, w1, w0
	mov	w0, 0
	blr	x8
	ldr	w4, [x29, 112]
	ldr	w0, [x29, 128]
	ldr	w7, [x29, 104]
	add	w4, w0, w4
	ldp	w5, w6, [x29, 120]
	and	w4, w4, 65535
	b	.L1623
.L1626:
	add	w5, w8, w5
	mov	w0, 0
	and	w5, w5, 65535
	b	.L1625
.L1630:
	add	x28, x28, 1
	cmp	x28, 65536
	bne	.L1627
	ldr	w0, [x29, 156]
	add	w0, w0, 1
	str	w0, [x29, 156]
	cmp	w0, 5
	bls	.L1631
	b	.L1636
.L1615:
	cmp	w0, 63
	adrp	x3, idb_buf
	bhi	.L1633
	mov	w1, 64
	sub	w1, w1, w0
	sub	w2, w24, w1
	ubfiz	x1, x1, 9, 25
	add	x1, x25, x1
	add	x0, x3, :lo12:idb_buf
.L1634:
	adrp	x3, .LANCHOR3
	add	x3, x3, :lo12:.LANCHOR3
	mov	w6, 1
	cmp	w5, 576
	str	w6, [x3, 124]
	bls	.L1635
	sub	w2, w2, w4
	sub	w2, w2, #447
.L1635:
	lsl	w2, w2, 9
	bl	memcpy
	b	.L1616
.L1633:
	lsl	w0, w0, 7
	add	x3, x3, :lo12:idb_buf
	sub	w0, w0, #8192
	mov	x1, x2
	add	x0, x3, x0, lsl 2
	mov	w2, w24
	b	.L1634
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
	add	x21, x19, 3408
	stp	x23, x24, [sp, 48]
	ldr	x0, [x19, 3440]
	str	x0, [x21, 8]
	ldr	x0, [x19, 3448]
	str	x0, [x21, 16]
	ldr	x0, [x19, 2376]
	ldrh	w2, [x19, 44]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x19, 2656]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1647
.L1655:
	mov	w0, -1
.L1646:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1647:
	add	x23, x19, 2656
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w22, w0
	add	w0, w0, 1
	mov	w24, 61604
	strh	w0, [x23, 2]
	mov	w23, 19539
	movk	w23, 0x4654, lsl 16
.L1649:
	tbz	w22, #31, .L1652
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1482
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1651:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x1, 44]
	ldrh	w1, [x1, 114]
	add	x0, x0, 24
	cmp	x1, x0, lsl 1
	bcs	.L1654
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1484
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1654:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w2, 48
	add	x21, x19, 2312
	mov	x0, x21
	ldr	x1, [x19, 3416]
	bl	ftl_memcpy
	ldr	x0, [x19, 2376]
	ldrh	w2, [x19, 44]
	ldr	x1, [x19, 3416]
	lsl	w2, w2, 1
	add	x1, x1, 48
	bl	ftl_memcpy
	ldrh	w1, [x19, 44]
	ldr	x0, [x19, 3416]
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
	bne	.L1655
	ldrh	w1, [x19, 2320]
	ldrb	w2, [x19, 2322]
	ldrh	w0, [x19, 58]
	strh	w1, [x19, 2662]
	cmp	w2, w0
	bne	.L1655
	adrp	x0, .LANCHOR3+128
	ldrh	w2, [x19, 62]
	ldrh	w3, [x19, 36]
	str	w1, [x0, #:lo12:.LANCHOR3+128]
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
	bls	.L1656
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1506
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
.L1656:
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
	bls	.L1657
	str	w1, [x0, 2620]
.L1657:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0, 2348]
	ldr	w2, [x0, 2624]
	cmp	w1, w2
	bls	.L1658
	str	w1, [x0, 2624]
.L1658:
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1659
	add	x0, x20, :lo12:.LANCHOR0
	add	x0, x0, 2424
	bl	make_superblock
.L1659:
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2472
	ldrh	w2, [x1, 2472]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1660
	bl	make_superblock
.L1660:
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2520
	ldrh	w2, [x1, 2520]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1661
	bl	make_superblock
.L1661:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x0, x20, 2672
	ldrh	w2, [x20, 2672]
	cmp	w2, w1
	beq	.L1662
	bl	make_superblock
.L1662:
	mov	w0, 0
	b	.L1646
.L1652:
	ldrh	w0, [x19, 2656]
	mov	w2, 1
	mov	w1, w2
	orr	w0, w22, w0, lsl 10
	str	w0, [x21, 4]
	ldr	x0, [x19, 3440]
	str	x0, [x21, 8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1650
	ldr	x0, [x19, 3440]
	ldr	w0, [x0]
	cmp	w0, w23
	bne	.L1650
	ldr	x0, [x19, 3448]
	ldrh	w0, [x0]
	cmp	w0, w24
	beq	.L1651
.L1650:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1649
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	add	x0, x21, :lo12:.LANCHOR3
	str	x23, [sp, 48]
	strh	wzr, [x0, -32]
	ldrh	w0, [x20, 40]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x20, 2656]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1669
.L1671:
	mov	w22, -1
.L1668:
	mov	w0, w22
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1669:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L1671
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldr	x1, [x20, 2568]
	mov	w0, 0
	ldrh	w3, [x20, 142]
	add	x1, x1, 4
.L1672:
	cmp	w0, w3
	bge	.L1677
	ldr	w2, [x1], 16
	tbz	w2, #31, .L1673
.L1677:
	add	x1, x19, :lo12:.LANCHOR0
	cmp	w0, w3
	ldrh	w2, [x1, 2340]
	add	w2, w2, 1
	strh	w2, [x1, 2340]
	bge	.L1691
.L1674:
	add	x20, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	sub	x0, x21, #184
	ldrh	w1, [x20, 2424]
	ldr	x3, [x20, 2376]
	ldrh	w4, [x20, 2428]
	lsl	x1, x1, 1
	ldrh	w2, [x3, x1]
	sub	w2, w2, w4
	strh	w2, [x3, x1]
	strb	wzr, [x20, 2430]
	ldrh	w1, [x20, 106]
	strh	w1, [x20, 2426]
	ldrh	w1, [x20, 2472]
	ldr	x3, [x20, 2376]
	strh	wzr, [x20, 2428]
	lsl	x1, x1, 1
	ldrh	w4, [x20, 2476]
	ldrh	w2, [x3, x1]
	sub	w2, w2, w4
	strh	w2, [x3, x1]
	strb	wzr, [x20, 2478]
	ldrh	w1, [x20, 106]
	strh	w1, [x20, 2474]
	strh	wzr, [x20, 2476]
	bl	FtlMapBlkWriteDump_data
	sub	x0, x21, #112
	bl	FtlMapBlkWriteDump_data
	ldrh	w0, [x20, 2342]
	add	w0, w0, 1
	strh	w0, [x20, 2342]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1678
.L1673:
	add	w0, w0, 1
	b	.L1672
.L1691:
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, -32]
	cbnz	w0, .L1674
.L1678:
	add	x7, x19, :lo12:.LANCHOR0
	mov	w0, 65533
	ldrh	w6, [x7, 2356]
	sub	w1, w6, #1
	cmp	w0, w1, uxth
	bcc	.L1680
	strh	w6, [x7, 3320]
	mov	w1, w6
	add	x0, x7, 2408
	bl	test_node_in_list
	cbnz	w0, .L1681
	ldrh	w0, [x7, 44]
	cmp	w0, w6
	bls	.L1681
	ldr	x0, [x7, 2376]
	ubfiz	x6, x6, 1, 16
	ldrh	w0, [x0, x6]
	cbnz	w0, .L1680
.L1681:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 3320]
.L1680:
	add	x7, x19, :lo12:.LANCHOR0
	mov	w0, 65533
	ldrh	w6, [x7, 2358]
	sub	w1, w6, #1
	cmp	w0, w1, uxth
	bcc	.L1684
	strh	w6, [x7, 3322]
	mov	w1, w6
	add	x0, x7, 2408
	bl	test_node_in_list
	cbnz	w0, .L1685
	ldrh	w0, [x7, 44]
	cmp	w0, w6
	bls	.L1685
	ldr	x0, [x7, 2376]
	ubfiz	x6, x6, 1, 16
	ldrh	w0, [x0, x6]
	cbnz	w0, .L1684
.L1685:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 3322]
.L1684:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x23, x20, 2424
	ldrh	w0, [x20, 2424]
	cmp	w0, w1
	beq	.L1687
	ldrh	w1, [x20, 2428]
	cbnz	w1, .L1687
	ldrh	w1, [x20, 2476]
	add	x21, x20, 2472
	cbnz	w1, .L1687
	bl	FtlGcRefreshBlock
	ldrh	w0, [x20, 2472]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	mov	x0, x23
	bl	allocate_new_data_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
.L1687:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2340]
	tst	x0, 31
	bne	.L1668
	bl	FtlVpcCheckAndModify
	b	.L1668
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
	cbz	w0, .L1694
	bl	FtlMakeBbt
.L1694:
	mov	w4, 23752
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 0
	movk	w4, 0xa0f, lsl 16
.L1695:
	ldrh	w2, [x1, 62]
	cmp	w0, w2, lsl 7
	blt	.L1696
	ldrh	w21, [x1, 44]
	add	x22, x20, :lo12:.LANCHOR0
	mov	w19, 0
.L1697:
	ldrh	w0, [x22, 46]
	cmp	w0, w21
	bhi	.L1698
	ldrh	w0, [x22, 36]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L1699
.L1703:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w21, 0
	mov	w22, 0
.L1700:
	ldrh	w0, [x19, 44]
	cmp	w0, w22
	bhi	.L1704
	ldrh	w2, [x19, 36]
	ldr	w1, [x19, 48]
	ldrh	w0, [x19, 46]
	str	w0, [x19, 3976]
	udiv	w4, w1, w2
	ubfx	x3, x4, 5, 16
	str	w4, [x19, 2584]
	add	w0, w3, 36
	strh	w0, [x19, 2652]
	mov	w0, 24
	mul	w0, w2, w0
	cmp	w21, w0
	ble	.L1705
	sub	w1, w1, w21
	udiv	w1, w1, w2
	str	w1, [x19, 2584]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x19, 2652]
.L1705:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 98]
	cbz	w1, .L1707
	ldrh	w5, [x0, 2652]
	add	w5, w5, w1, lsr 1
	strh	w5, [x0, 2652]
	mul	w5, w1, w2
	cmp	w21, w5
	bge	.L1707
	add	w1, w1, 32
	str	w4, [x0, 2584]
	add	w1, w3, w1
	strh	w1, [x0, 2652]
.L1707:
	add	x19, x20, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR3
	add	x21, x19, 2424
	mov	w23, -1
	ldrh	w1, [x19, 2652]
	ldr	w0, [x19, 2584]
	sub	w0, w0, w1
	add	x1, x22, :lo12:.LANCHOR3
	mul	w0, w0, w2
	str	w0, [x1, 128]
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
.L1709:
	mov	x0, x21
	bl	make_superblock
	ldrb	w1, [x21, 7]
	ldrh	w0, [x21]
	cbnz	w1, .L1710
	ldr	x1, [x19, 2376]
	ubfiz	x0, x0, 1, 16
	strh	w23, [x1, x0]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L1709
.L1696:
	ldr	x5, [x1, 4000]
	ubfiz	x3, x0, 2, 16
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w2, [x5, x3]
	ldr	x2, [x1, 3984]
	str	w4, [x2, x3]
	b	.L1695
.L1698:
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w19, w19, w0
	and	w21, w21, 65535
	and	w19, w19, 65535
	b	.L1697
.L1699:
	udiv	w0, w19, w0
	ldr	w19, [x22, 136]
	add	x21, x20, :lo12:.LANCHOR0
	add	w0, w0, w19
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22, 40]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x22, 44]
.L1701:
	ldrh	w0, [x21, 46]
	cmp	w0, w19
	bls	.L1703
	mov	w0, w19
	add	w19, w19, 1
	mov	w1, 1
	and	w19, w19, 65535
	bl	FtlLowFormatEraseBlock
	b	.L1701
.L1704:
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w21, w21, w0
	and	w22, w22, 65535
	and	w21, w21, 65535
	b	.L1700
.L1710:
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
.L1711:
	mov	x0, x20
	bl	make_superblock
	ldrb	w1, [x20, 7]
	ldrh	w0, [x20]
	cbnz	w1, .L1712
	ldr	x1, [x19, 2376]
	ubfiz	x0, x0, 1, 16
	strh	w21, [x1, x0]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L1711
.L1712:
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
	ldr	w0, [x22, 128]
	strh	w0, [x19, 2662]
	ldr	w0, [x19, 2620]
	str	w0, [x19, 2664]
	add	w0, w0, 1
	strh	wzr, [x19, 2658]
	strh	w20, [x19, 2660]
	str	w0, [x19, 2620]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L1713
	adrp	x0, .LANCHOR2
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR2]
.L1713:
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
	cbnz	w0, .L1722
	bl	FtlSysBlkInit
	cbnz	w0, .L1722
	mov	w0, 1
	str	w0, [x20, #:lo12:.LANCHOR2]
	ldrh	w0, [x19, 2416]
	cmp	w0, 15
	bhi	.L1722
	mov	w19, 8129
.L1721:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L1721
.L1722:
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
	ldr	x3, [x2, 136]
	cbnz	x3, .L1729
	stp	x0, x1, [x2, 136]
	mov	w0, 0
	ret
.L1729:
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
	str	x0, [x22, 152]
	cbz	x0, .L1736
	mov	w26, 22084
	mov	w25, 0
	mov	w24, 0
	mov	w23, 0
	movk	w26, 0x524b, lsl 16
.L1734:
	ldr	x3, [x22, 136]
	mov	w1, 128
	ldr	x2, [x22, 152]
	lsl	w0, w23, 7
	blr	x3
	mov	w21, w0
	cbnz	w0, .L1732
	ldr	x19, [x22, 152]
	ldr	w0, [x19]
	cmp	w0, w26
	bne	.L1733
	add	x0, x19, 61440
	ldr	w1, [x19, 4]
	ldr	w0, [x0, 4092]
	cmp	w0, w1
	bne	.L1733
	cmp	w0, w24
	bls	.L1733
	mov	w25, w23
	mov	w24, w0
.L1733:
	add	w23, w23, 1
	cmp	w23, 2
	bne	.L1734
	cbz	w24, .L1735
	add	x0, x20, :lo12:.LANCHOR3
	mov	x2, x19
	mov	w1, 128
	ldr	x3, [x0, 136]
	lsl	w0, w25, 7
	blr	x3
	mov	w21, w0
	cbz	w0, .L1730
.L1732:
	add	x20, x20, :lo12:.LANCHOR3
	mov	w21, -1
	ldr	x0, [x20, 152]
	bl	kfree
	str	xzr, [x20, 152]
	b	.L1730
.L1735:
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
.L1730:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1736:
	mov	w21, -12
	b	.L1730
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	adrp	x3, .LANCHOR3+152
	ldr	x4, [x3, #:lo12:.LANCHOR3+152]
	cbz	x4, .L1744
	ldrh	w6, [x4, 10]
	add	x5, x4, 16
	mov	w3, 0
.L1741:
	cmp	w3, w6
	bcc	.L1743
.L1744:
	mov	w0, -1
	ret
.L1743:
	ldrh	w7, [x5], 8
	cmp	w7, w0
	bne	.L1742
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
.L1742:
	add	w3, w3, 1
	b	.L1741
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
	ldr	x19, [x3, 152]
	cbz	x19, .L1764
	add	w21, w2, 63
	ldrh	w3, [x19, 10]
	ldrh	w24, [x19, 8]
	mov	x4, x1
	mov	w28, w2
	and	w21, w21, -64
	add	x1, x19, 16
	mov	w20, 0
.L1751:
	cmp	w20, w3
	bcc	.L1759
	ldrh	w1, [x19, 14]
	cmp	w21, w1
	bhi	.L1764
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
	b	.L1766
.L1759:
	ldrh	w6, [x1], 8
	cmp	w6, w0
	bne	.L1752
	uxtw	x27, w20
	add	x5, x19, 1024
	add	x22, x19, x27, lsl 3
	ldrh	w25, [x22, 20]
	add	w25, w25, 63
	and	w25, w25, -64
	cmp	w28, w25
	bls	.L1753
	ldrh	w0, [x19, 14]
	cmp	w21, w0
	bhi	.L1764
	add	x27, x27, 2
	ldrh	w22, [x22, 18]
	add	x27, x19, x27, lsl 3
	sub	w26, w3, #1
.L1754:
	cmp	w20, w26
	bcc	.L1755
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
.L1766:
	ldr	w0, [x19, 4]
	add	x1, x19, 61440
	add	x23, x23, :lo12:.LANCHOR3
	mov	x2, x19
	add	w0, w0, 1
	str	w0, [x19, 4]
	str	w0, [x1, 4092]
	mov	w1, 128
	ldr	x3, [x23, 144]
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	and	w0, w0, 65535
	cmp	w0, 1
	csel	w0, w0, wzr, ls
	strh	w0, [x19, 8]
	lsl	w0, w24, 7
	blr	x3
	mov	w0, 0
.L1749:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1755:
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
	b	.L1754
.L1753:
	ldrh	w0, [x22, 18]
	uxtw	x2, w28
	mov	x1, x4
	add	x0, x5, x0
	bl	memcpy
	strh	w28, [x22, 20]
	b	.L1766
.L1752:
	add	w20, w20, 1
	b	.L1751
.L1764:
	mov	w0, -1
	b	.L1749
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
	cbz	x0, .L1781
	mov	w1, 30209
	mov	x19, x0
	movk	w1, 0x4004, lsl 16
	cmp	w21, w1
	beq	.L1770
	add	w1, w1, 1
	cmp	w21, w1
	beq	.L1771
.L1791:
	mov	x20, -14
	b	.L1769
.L1770:
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
	cbz	x3, .L1777
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbnz	x0, .L1791
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L1775
.L1776:
	mov	x20, -1
.L1769:
	mov	x0, x19
	bl	kfree
.L1767:
	mov	x0, x20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1775:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_read
	cmn	w0, #1
	beq	.L1776
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
	cbz	x3, .L1791
	mov	x0, x20
	mov	x1, x19
	mov	x20, 0
	bl	__arch_copy_to_user
	cbz	x0, .L1769
	b	.L1791
.L1771:
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
	cbz	x3, .L1777
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbnz	x0, .L1791
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L1776
	ldrh	w2, [x19, 6]
	cmp	w2, 4087
	bhi	.L1776
	add	w2, w2, 8
	mov	x0, x20
	sxtw	x2, w2
	ldr	x1, [x21, 8]
#APP
// 410 "./arch/arm64/include/asm/uaccess.h" 1
	adds x0, x0, x2; ccmp x0, x1, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1779
	mov	x1, x20
	mov	x0, x19
	bl	__arch_copy_from_user
	cbnz	x0, .L1791
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_write
	sxtw	x20, w0
	b	.L1769
.L1777:
	str	xzr, [x19]
	b	.L1791
.L1779:
	mov	w1, 0
	mov	x0, x19
	bl	memset
	b	.L1791
.L1781:
	mov	x20, -1
	b	.L1767
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
	.type	__func__.3844, %object
	.size	__func__.3844, 17
__func__.3844:
	.string	"INSERT_DATA_LIST"
	.zero	7
	.type	__func__.3839, %object
	.size	__func__.3839, 17
__func__.3839:
	.string	"INSERT_FREE_LIST"
	.zero	7
	.type	__func__.3882, %object
	.size	__func__.3882, 17
__func__.3882:
	.string	"List_remove_node"
	.zero	7
	.type	__func__.3914, %object
	.size	__func__.3914, 22
__func__.3914:
	.string	"List_update_data_list"
	.zero	2
	.type	__func__.4025, %object
	.size	__func__.4025, 22
__func__.4025:
	.string	"select_l2p_ram_region"
	.zero	2
	.type	__func__.4348, %object
	.size	__func__.4348, 16
__func__.4348:
	.string	"make_superblock"
	.type	__func__.4523, %object
	.size	__func__.4523, 19
__func__.4523:
	.string	"get_new_active_ppa"
	.zero	5
	.type	__func__.16106, %object
	.size	__func__.16106, 15
__func__.16106:
	.string	"FlashReadPages"
	.zero	1
	.type	__func__.16123, %object
	.size	__func__.16123, 15
__func__.16123:
	.string	"FlashProgPages"
	.zero	1
	.type	__func__.3956, %object
	.size	__func__.3956, 26
__func__.3956:
	.string	"ftl_map_blk_alloc_new_blk"
	.zero	6
	.type	__func__.3381, %object
	.size	__func__.3381, 11
__func__.3381:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.3679, %object
	.size	__func__.3679, 14
__func__.3679:
	.string	"FtlBbt2Bitmap"
	.zero	2
	.type	__func__.3986, %object
	.size	__func__.3986, 31
__func__.3986:
	.string	"Ftl_write_map_blk_to_last_page"
	.zero	1
	.type	__func__.4000, %object
	.size	__func__.4000, 16
__func__.4000:
	.string	"FtlMapWritePage"
	.type	__func__.3923, %object
	.size	__func__.3923, 16
__func__.3923:
	.string	"load_l2p_region"
	.type	__func__.3971, %object
	.size	__func__.3971, 15
__func__.3971:
	.string	"ftl_map_blk_gc"
	.zero	1
	.type	__func__.4042, %object
	.size	__func__.4042, 9
__func__.4042:
	.string	"log2phys"
	.zero	7
	.type	__func__.4257, %object
	.size	__func__.4257, 16
__func__.4257:
	.string	"FtlReUsePrevPpa"
	.type	__func__.4137, %object
	.size	__func__.4137, 14
__func__.4137:
	.string	"FtlScanSysBlk"
	.zero	2
	.type	__func__.4369, %object
	.size	__func__.4369, 18
__func__.4369:
	.string	"SupperBlkListInit"
	.zero	6
	.type	__func__.4410, %object
	.size	__func__.4410, 14
__func__.4410:
	.string	"ftl_check_vpc"
	.zero	2
	.type	__func__.4495, %object
	.size	__func__.4495, 25
__func__.4495:
	.string	"allocate_data_superblock"
	.zero	7
	.type	__func__.4536, %object
	.size	__func__.4536, 16
__func__.4536:
	.string	"update_vpc_list"
	.type	__func__.4543, %object
	.size	__func__.4543, 20
__func__.4543:
	.string	"decrement_vpc_count"
	.zero	4
	.type	__func__.4291, %object
	.size	__func__.4291, 22
__func__.4291:
	.string	"FtlRecoverySuperblock"
	.zero	2
	.type	__func__.3722, %object
	.size	__func__.3722, 11
__func__.3722:
	.string	"FtlLoadBbt"
	.zero	5
	.type	__func__.4115, %object
	.size	__func__.4115, 15
__func__.4115:
	.string	"FtlVpcTblFlush"
	.zero	1
	.type	__func__.4394, %object
	.size	__func__.4394, 21
__func__.4394:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.4516, %object
	.size	__func__.4516, 29
__func__.4516:
	.string	"allocate_new_data_superblock"
	.zero	3
	.type	__func__.3505, %object
	.size	__func__.3505, 13
__func__.3505:
	.string	"FtlProgPages"
	.zero	3
	.type	__func__.4614, %object
	.size	__func__.4614, 19
__func__.4614:
	.string	"FtlGcFreeTempBlock"
	.zero	5
	.type	__func__.4720, %object
	.size	__func__.4720, 23
__func__.4720:
	.string	"rk_ftl_garbage_collect"
	.zero	1
	.type	__func__.3533, %object
	.size	__func__.3533, 9
__func__.3533:
	.string	"FtlWrite"
	.zero	7
	.type	__func__.4194, %object
	.size	__func__.4194, 15
__func__.4194:
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
	.xword	.LC116
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
	.type	idb_need_write_back, %object
	.size	idb_need_write_back, 4
idb_need_write_back:
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
	.type	gp_flash_check_buf, %object
	.size	gp_flash_check_buf, 262144
gp_flash_check_buf:
	.zero	262144
	.type	idb_buf, %object
	.size	idb_buf, 262144
idb_buf:
	.zero	262144
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
	.string	"SFTL version: 5.0.45 20180810"
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
	.string	"FtlMapWritePage error = %x \n"
.LC88:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC89:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC90:
	.string	"region_id = %x phyAddr = %x\n"
.LC91:
	.string	"spare:"
.LC92:
	.string	"map_ppn:"
.LC93:
	.string	"ftl_map_blk_gc blk info: %x %x %x\n"
.LC94:
	.string	"ftl_scan_all_data = %x\n"
.LC95:
	.string	"scan lpa = %x ppa= %x\n"
.LC96:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC97:
	.string	"...%s enter...\n"
.LC98:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC99:
	.string	"free blk vpc error %x = %x  %x\n"
.LC100:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC101:
	.string	"FtlBbmTblFlush error:%x\n"
.LC102:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC103:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC104:
	.string	"decrement_vpc_count %x = %d\n"
.LC105:
	.string	"decrement_vpc_count %x = %d in free list\n"
.LC106:
	.string	"spuer block %x vpn is 0\n "
.LC107:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC108:
	.string	"no ect"
.LC109:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC110:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC111:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC112:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC113:
	.string	"g_gc_superblock_free %x %x %x %x %x\n"
.LC114:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC115:
	.string	"write_idblock fail! %x %x %x %x\n"
.LC116:
	.string	"vendor_storage"
