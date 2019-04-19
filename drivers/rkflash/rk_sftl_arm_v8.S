/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2019-04-19
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
	.global	ftl_print_sblk_info
	.type	ftl_print_sblk_info, %function
ftl_print_sblk_info:
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LC0
	adrp	x0, .LC1
	add	x1, x1, :lo12:.LC0
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC1
	bl	sftl_printk
	adrp	x0, .LC2
	add	x0, x0, :lo12:.LC2
	ldrh	w1, [x19, 32]
	ldr	x3, [x19, 80]
	ldrh	w5, [x19, 36]
	ubfiz	x2, x1, 1, 16
	ldrb	w4, [x19, 40]
	ldrh	w6, [x3, x2]
	ldrb	w3, [x19, 38]
	ldrh	w2, [x19, 34]
	bl	sftl_printk
	ldrh	w1, [x19, 88]
	adrp	x0, .LC3
	ldr	x3, [x19, 80]
	add	x0, x0, :lo12:.LC3
	ldrh	w5, [x19, 92]
	ubfiz	x2, x1, 1, 16
	ldrb	w4, [x19, 96]
	ldrh	w6, [x3, x2]
	ldrb	w3, [x19, 94]
	ldrh	w2, [x19, 90]
	bl	sftl_printk
	ldrh	w1, [x19, 136]
	adrp	x0, .LC4
	ldr	x3, [x19, 80]
	add	x0, x0, :lo12:.LC4
	ldrh	w5, [x19, 140]
	ubfiz	x2, x1, 1, 16
	ldrb	w4, [x19, 144]
	ldrh	w6, [x3, x2]
	ldrb	w3, [x19, 142]
	ldrh	w2, [x19, 138]
	bl	sftl_printk
	ldrh	w1, [x19, 184]
	adrp	x0, .LC5
	ldr	x3, [x19, 80]
	add	x0, x0, :lo12:.LC5
	ldrh	w5, [x19, 188]
	ubfiz	x2, x1, 1, 16
	ldrb	w4, [x19, 192]
	ldrh	w6, [x3, x2]
	ldrb	w3, [x19, 190]
	ldrh	w2, [x19, 186]
	bl	sftl_printk
	ldrh	w3, [x19, 232]
	adrp	x0, .LC6
	ldrh	w2, [x19, 234]
	add	x0, x0, :lo12:.LC6
	ldrh	w1, [x19, 236]
	bl	sftl_printk
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_print_sblk_info, .-ftl_print_sblk_info
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	mov	w2, 1
	mov	w1, 0
.L7:
	cmp	w2, w0
	bls	.L8
	sub	w0, w1, #1
	ret
.L8:
	add	w1, w1, 1
	lsl	w2, w2, 1
	and	w1, w1, 65535
	b	.L7
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
	ldrh	w2, [x1, 244]
	ldrh	w3, [x1, 254]
	str	w0, [x1, 240]
	mul	w2, w2, w0
	sub	w0, w3, w0
	strh	w0, [x1, 252]
	ldr	w0, [x1, 260]
	str	w2, [x1, 248]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1, 256]
	ret
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	stp	x29, x30, [sp, -16]!
	mov	x7, x0
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	ldrh	w2, [x0]
	add	x0, x4, :lo12:.LANCHOR0
	ldrh	w3, [x7, 14]
	ldrh	w6, [x7, 2]
	ldrh	w5, [x7, 4]
	cmp	w3, 4
	ldrh	w1, [x7, 6]
	strh	w2, [x0, 264]
	strh	w6, [x0, 266]
	strh	w5, [x0, 268]
	strh	w1, [x0, 254]
	strh	w3, [x0, 270]
	bne	.L12
	lsr	w1, w1, 1
	strh	w1, [x0, 254]
	mov	w1, 8
	strh	w1, [x0, 270]
.L13:
	add	x1, x4, :lo12:.LANCHOR0
	mov	x0, 0
	add	x3, x1, 272
.L14:
	strb	w0, [x0, x3]
	add	x0, x0, 1
	cmp	x0, 32
	bne	.L14
	mov	w0, 5
	strh	wzr, [x1, 306]
	strh	w0, [x1, 304]
	cmp	w2, 1
	bne	.L15
	strh	w2, [x1, 304]
.L15:
	add	x3, x4, :lo12:.LANCHOR0
	mov	w0, 640
	strh	w0, [x3, 308]
	ldrh	w0, [x3, 268]
	ldrh	w5, [x3, 254]
	mul	w6, w6, w0
	mul	w0, w0, w5
	and	w6, w6, 65535
	strh	w6, [x3, 244]
	and	w0, w0, 65535
	strh	w0, [x3, 310]
	bl	Ftl_log2
	ldrh	w9, [x7, 12]
	ldrh	w8, [x3, 270]
	strh	w0, [x3, 312]
	strh	w9, [x3, 314]
	mul	w0, w6, w9
	strh	w9, [x3, 316]
	strh	w0, [x3, 318]
	mov	w0, w8
	bl	Ftl_log2
	and	w2, w0, 65535
	strh	w0, [x3, 320]
	ubfiz	w0, w8, 9, 7
	strh	w0, [x3, 322]
	ubfx	w0, w0, 8, 8
	strh	w0, [x3, 324]
	ldrh	w0, [x7, 20]
	strh	w0, [x3, 326]
	mul	w0, w6, w5
	str	w0, [x3, 260]
	mul	w0, w0, w8
	mul	w8, w8, w9
	mul	w0, w0, w9
	asr	w0, w0, 11
	str	w0, [x3, 328]
	mov	w0, 5120
	sdiv	w0, w0, w8
	and	w0, w0, 65535
	cmp	w0, 4
	bls	.L16
.L22:
	add	x1, x4, :lo12:.LANCHOR0
	strh	w0, [x3, 332]
	mov	w0, 640
	lsl	w5, w5, 6
	asr	w0, w0, w2
	add	w2, w2, 9
	add	w0, w0, 2
	strh	w0, [x1, 334]
	asr	w5, w5, w2
	strh	w5, [x1, 336]
	and	w5, w5, 65535
	cmp	w6, 1
	mul	w0, w5, w6
	add	w5, w5, 8
	str	w0, [x1, 340]
	ldrh	w0, [x1, 332]
	udiv	w0, w0, w6
	add	w5, w0, w5
	beq	.L18
.L23:
	add	x4, x4, :lo12:.LANCHOR0
	str	w5, [x1, 240]
	ldrh	w0, [x4, 240]
	bl	FtlSysBlkNumInit
	ldr	w0, [x4, 240]
	str	w0, [x4, 344]
	ldr	w0, [x4, 256]
	ldrh	w1, [x4, 314]
	str	wzr, [x4, 352]
	lsl	w0, w0, 2
	mul	w0, w0, w1
	ldrh	w1, [x4, 320]
	add	w1, w1, 9
	lsr	w0, w0, w1
	add	w0, w0, 2
	strh	w0, [x4, 348]
	mov	w0, 32
	strh	w0, [x4, 350]
	ldrh	w0, [x4, 332]
	add	w0, w0, 3
	strh	w0, [x4, 332]
	ldr	w0, [x4, 340]
	add	w0, w0, 3
	str	w0, [x4, 340]
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L12:
	cmp	w3, 8
	bne	.L13
	cmp	w5, 1
	bne	.L13
	lsr	w1, w1, 1
	strh	w1, [x0, 254]
	mov	w1, 2
	strh	w1, [x0, 268]
	b	.L13
.L16:
	mov	w0, 4
	b	.L22
.L18:
	add	w5, w5, 4
	b	.L23
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w2, [x1, 356]
	cbz	w2, .L28
	ldrh	w2, [x1, 332]
	ldr	x3, [x1, 360]
	mov	x1, 0
.L26:
	cmp	w2, w1, uxth
	bhi	.L27
.L28:
	mov	w0, 0
	ret
.L27:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L26
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
	adrp	x0, .LANCHOR0+352
	ldr	w0, [x0, #:lo12:.LANCHOR0+352]
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
	add	x19, x19, 368
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
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
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
	ldrh	w1, [x3, 310]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	and	w0, w0, 65535
	ldr	x1, [x2, 400]
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
	ldrh	w6, [x0, 310]
	ldrh	w0, [x0, 266]
	mul	w6, w6, w0
	cmp	w4, w6
	blt	.L46
	mov	w0, w5
	ret
.L46:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L39:
	mov	w0, w4
	bl	FtlBbmIsBadBlock
	cbz	w0, .L38
	add	w5, w5, 1
	and	w5, w5, 65535
.L38:
	add	w4, w4, 1
	and	w4, w4, 65535
	cmp	w4, w6
	blt	.L39
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
	ldrh	w2, [x4, 268]
	ldrh	w4, [x4, 310]
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
	ldrh	w1, [x2, 268]
	ldrh	w2, [x2, 310]
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
	ldrh	w2, [x1, 310]
	ldrh	w1, [x1, 268]
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
	bls	.L51
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	ret
.L51:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	adrp	x0, .LANCHOR0+470
	ldrh	w0, [x0, #:lo12:.LANCHOR0+470]
	cmp	w0, 0
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR0+470
	ldrh	w0, [x0, #:lo12:.LANCHOR0+470]
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
	add	x1, x2, 464
	ldrh	w3, [x1, 6]
	cbz	w3, .L55
	ldrh	w5, [x2, 2548]
	mov	w6, 0
	ldrh	w3, [x1, 2]
	mov	w4, 0
	ldrh	w2, [x1, 4]
	and	w5, w5, 31
.L57:
	cmp	w5, w4
	bgt	.L58
	cbz	w6, .L55
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0, 466]
	strh	w2, [x0, 468]
.L55:
	ret
.L58:
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
	b	.L57
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 464
	and	w0, w0, 65535
	ldrh	w4, [x1, 6]
	cmp	w4, 1024
	beq	.L68
	ldrh	w5, [x1, 2]
	mov	w3, 0
.L66:
	cmp	w3, w4
	bcc	.L67
.L68:
	mov	w0, 0
	ret
.L67:
	add	w2, w3, w5
	ubfiz	x2, x2, 1, 10
	add	x2, x1, x2
	ldrh	w2, [x2, 8]
	cmp	w2, w0
	beq	.L69
	add	w3, w3, 1
	b	.L66
.L69:
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
	mov	x4, x2
	ldrh	w1, [x3, 252]
	cmp	w1, w0
	bls	.L72
	mov	w6, 6
	ldr	x8, [x3, 2568]
	mov	w1, -1
	umull	x7, w0, w6
	add	x13, x8, x7
	strh	w1, [x13, 2]
	strh	w1, [x8, x7]
	ldr	x1, [x3, 2576]
	cbnz	x1, .L73
	str	x13, [x3, 2576]
.L72:
	mov	w0, 0
	ret
.L73:
	ldr	x11, [x3, 80]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x3, 2568]
	mov	x9, -6148914691236517206
	movk	x9, 0xaaab, lsl 0
	ldrh	w14, [x3, 252]
	mov	w15, 65535
	mov	w3, 0
	ldrh	w5, [x11, x2]
	ldrh	w2, [x13, 4]
	cmp	w2, 0
	mul	w5, w5, w2
	sub	x2, x1, x10
	asr	x2, x2, 1
	csinv	w5, w5, wzr, ne
	mul	x2, x2, x9
	and	w2, w2, 65535
.L78:
	add	w3, w3, 1
	and	w3, w3, 65535
	cmp	w3, w14
	bhi	.L72
	cmp	w0, w2
	beq	.L72
	ldrh	w12, [x1, 4]
	cbz	w12, .L76
	ubfiz	x9, x2, 1, 16
	ldrh	w9, [x11, x9]
	mul	w9, w9, w12
	cmp	w9, w5
	bcs	.L76
	ldrh	w9, [x1]
	cmp	w9, w15
	bne	.L77
	strh	w2, [x13, 2]
	add	x2, x4, :lo12:.LANCHOR0
	strh	w0, [x1]
	str	x13, [x2, 2584]
	b	.L72
.L77:
	umaddl	x1, w9, w6, x10
	mov	w2, w9
	b	.L78
.L76:
	strh	w2, [x8, x7]
	ldrh	w2, [x1, 2]
	strh	w2, [x13, 2]
	add	x2, x4, :lo12:.LANCHOR0
	ldr	x3, [x2, 2576]
	cmp	x1, x3
	bne	.L79
	strh	w0, [x1, 2]
	str	x13, [x2, 2576]
	b	.L72
.L79:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2568]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L72
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
	ldrh	w0, [x1, 2592]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 2592]
	ldrh	w1, [x1, 252]
	cmp	w1, w0
	bcs	.L84
	adrp	x1, .LANCHOR1
	adrp	x0, .LC8
	mov	w2, 214
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC8
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
	ldr	x9, [x5, 2568]
	umull	x8, w0, w6
	add	x4, x9, x8
	strh	w1, [x4, 2]
	strh	w1, [x9, x8]
	ldr	x1, [x5, 2600]
	cbnz	x1, .L89
	str	x4, [x5, 2600]
.L88:
	mov	w0, 0
	ret
.L89:
	ldr	x11, [x5, 2608]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5, 2568]
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
	ldr	x3, [x2, 2600]
	cmp	x1, x3
	bne	.L93
	strh	w0, [x1, 2]
	str	x4, [x2, 2600]
	b	.L88
.L93:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2568]
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
	ldrh	w0, [x1, 236]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 236]
	ldrh	w1, [x1, 252]
	cmp	w1, w0
	bcs	.L94
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 207
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC8
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
	ldr	x22, [x0, 2568]
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
	adrp	x0, .LC8
	mov	w2, 372
	add	x1, x1, 48
	add	x0, x0, :lo12:.LC8
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
	ldr	x1, [x19, 2568]
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
	ldr	x0, [x19, 2568]
	strh	w2, [x0, x1]
	b	.L101
.L102:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 6
	ldr	x3, [x19, 2568]
	umaddl	x0, w0, w2, x3
	strh	w1, [x0, 2]
	ldrh	w0, [x20, 2]
	ldr	x1, [x19, 2568]
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
	adrp	x3, .LANCHOR0+2568
	and	w1, w1, 65535
	mov	w4, 65535
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	w5, 6
	ldr	x19, [x3, #:lo12:.LANCHOR0+2568]
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
	ldr	x1, [x0, 2576]
	cbz	x1, .L122
	ldr	x3, [x0, 2568]
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
	ldrh	w1, [x0, 32]
	cmp	w1, w20
	beq	.L125
	ldrh	w1, [x0, 88]
	cmp	w1, w20
	beq	.L125
	ldrh	w1, [x0, 136]
	cmp	w1, w20
	beq	.L125
	mov	w22, 6
	ldr	x24, [x0, 2568]
	ldr	x1, [x0, 2576]
	umull	x22, w20, w22
	add	x23, x24, x22
	cmp	x23, x1
	beq	.L125
	ldr	x2, [x0, 80]
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
	adrp	x0, .LC8
	mov	w2, 463
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC8
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
	ldr	x2, [x22, 80]
	ldrh	w1, [x2, x1, lsl 1]
	ldr	x2, [x22, 2568]
	add	x0, x2, x0
	ldrh	w2, [x0, 4]
	cmp	w2, 0
	mul	w0, w1, w2
	csinv	w0, w0, wzr, ne
	cmp	w21, w0
	bcs	.L125
	mov	w1, w20
	add	x0, x22, 2576
	bl	List_remove_node
	ldrh	w0, [x22, 2592]
	cbnz	w0, .L131
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 474
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L131:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2592]
	sub	w0, w0, #1
	strh	w0, [x19, 2592]
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
	ldrh	w2, [x0, 350]
	ldr	x0, [x0, 2616]
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
	ldrh	w4, [x1, 2624]
	mov	w1, 0
.L142:
	cmp	w1, w2
	bne	.L144
	cmp	w19, w1
	bcc	.L137
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 789
	add	x1, x1, 96
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
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
	ldrh	w2, [x3, 2626]
	cmp	w2, 4
	bhi	.L147
	cbnz	w0, .L147
	add	w2, w2, 1
	strh	w2, [x3, 2626]
	ret
.L147:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w6, 65535
	mov	x1, 0
	ldrh	w4, [x0, 252]
	ldr	x5, [x0, 80]
	strh	wzr, [x0, 2626]
	str	wzr, [x0, 2628]
.L148:
	cmp	w4, w1, uxth
	bhi	.L150
	ret
.L150:
	ldrh	w3, [x5, x1, lsl 1]
	cmp	w3, w6
	beq	.L149
	ldr	w2, [x0, 2628]
	add	w2, w2, w3
	str	w2, [x0, 2628]
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
	ldrh	w3, [x1, 244]
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
	ldrh	w1, [x4, 314]
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
	adrp	x2, .LANCHOR0+244
	ldrh	w4, [x2, #:lo12:.LANCHOR0+244]
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
	ldrh	w0, [x0, 252]
	cmp	w1, w0
	bcc	.L174
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2148
	add	x1, x1, 120
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L174:
	add	x0, x20, :lo12:.LANCHOR0
	add	x6, x19, 16
	add	x7, x0, 272
	strh	wzr, [x19, 4]
	strb	wzr, [x19, 7]
	mov	x5, 0
	ldrh	w8, [x0, 244]
	mov	w9, -1
.L175:
	cmp	w8, w5, uxth
	bhi	.L177
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19, 7]
	strb	wzr, [x19, 9]
	ldrh	w1, [x20, 314]
	mul	w0, w0, w1
	ldrh	w1, [x19]
	strh	w0, [x19, 4]
	ldr	x0, [x20, 2608]
	ldrh	w1, [x0, x1, lsl 1]
	mov	w0, 10000
	cmp	w1, w0
	bls	.L178
	mov	w0, 1
	strb	w0, [x19, 9]
.L178:
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
	add	x7, x1, 272
	ldrh	w10, [x1, 244]
	ldrh	w11, [x1, 314]
	cmp	w10, w9, uxth
	bhi	.L192
	cbz	w6, .L190
	mov	w0, 32768
	sdiv	w6, w0, w6
.L191:
	add	x0, x5, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x0, [x0, 2568]
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
	ldr	x0, [x0, 2568]
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
	ldr	x0, [x1, 2600]
	cbz	x0, .L195
	ldr	x2, [x1, 2568]
	ldr	x1, [x1, 2608]
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
	ldr	x1, [x4, 2600]
	cbz	x1, .L203
	ldrh	w3, [x4, 236]
	mov	w5, 7
	ldr	x4, [x4, 2568]
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
	ldr	x0, [x2, 2608]
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
	adrp	x1, .LC9
	add	x1, x1, :lo12:.LC9
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
	ldr	w3, [x21, 328]
	adrp	x2, .LC10
	add	x2, x2, :lo12:.LC10
	mov	x1, 64
	mov	x0, x20
	bl	snprintf
	add	x20, x20, x0, sxtw
	mov	x0, x20
	adrp	x1, .LC11
	add	x1, x1, :lo12:.LC11
	bl	strcpy
	ldr	w3, [x21, 2632]
	add	x20, x20, 10
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC12
	add	x2, x2, :lo12:.LC12
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2628]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC13
	add	x2, x2, :lo12:.LC13
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2636]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC14
	add	x2, x2, :lo12:.LC14
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2640]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC15
	add	x2, x2, :lo12:.LC15
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2644]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC16
	add	x2, x2, :lo12:.LC16
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2648]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC17
	add	x2, x2, :lo12:.LC17
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2652]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC18
	add	x2, x2, :lo12:.LC18
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2656]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC19
	add	x2, x2, :lo12:.LC19
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2660]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC20
	add	x2, x2, :lo12:.LC20
	lsr	w3, w3, 11
	bl	snprintf
	ldr	w3, [x21, 2664]
	add	x20, x20, x0, sxtw
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC21
	add	x2, x2, :lo12:.LC21
	lsr	w3, w3, 11
	bl	snprintf
	ldr	w3, [x21, 2668]
	add	x20, x20, x0, sxtw
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC22
	add	x2, x2, :lo12:.LC22
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2672]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC23
	add	x2, x2, :lo12:.LC23
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 374]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC24
	add	x2, x2, :lo12:.LC24
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 236]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC25
	add	x2, x2, :lo12:.LC25
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2676]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC26
	add	x2, x2, :lo12:.LC26
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2680]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC27
	add	x2, x2, :lo12:.LC27
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2684]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC28
	add	x2, x2, :lo12:.LC28
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2688]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC29
	add	x2, x2, :lo12:.LC29
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2692]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC30
	add	x2, x2, :lo12:.LC30
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 2696]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC31
	add	x2, x2, :lo12:.LC31
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2550]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC32
	add	x2, x2, :lo12:.LC32
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2548]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC33
	add	x2, x2, :lo12:.LC33
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 352]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC34
	add	x2, x2, :lo12:.LC34
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 344]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC35
	add	x2, x2, :lo12:.LC35
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 240]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC36
	add	x2, x2, :lo12:.LC36
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 470]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC37
	add	x2, x2, :lo12:.LC37
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 252]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC38
	add	x2, x2, :lo12:.LC38
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2700]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC39
	add	x2, x2, :lo12:.LC39
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x21, 256]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC40
	add	x2, x2, :lo12:.LC40
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 2704]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC41
	add	x2, x2, :lo12:.LC41
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 368]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC42
	add	x2, x2, :lo12:.LC42
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 34]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC43
	add	x2, x2, :lo12:.LC43
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 38]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC44
	add	x2, x2, :lo12:.LC44
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 32]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC45
	add	x2, x2, :lo12:.LC45
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 40]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC46
	add	x2, x2, :lo12:.LC46
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 36]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC47
	add	x2, x2, :lo12:.LC47
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w1, [x21, 32]
	adrp	x2, .LC48
	ldr	x0, [x21, 80]
	add	x2, x2, :lo12:.LC48
	ldrh	w3, [x0, x1, lsl 1]
	mov	x1, 64
	mov	x0, x20
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 90]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC49
	add	x2, x2, :lo12:.LC49
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 94]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC50
	add	x2, x2, :lo12:.LC50
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 88]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC51
	add	x2, x2, :lo12:.LC51
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 96]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC52
	add	x2, x2, :lo12:.LC52
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 92]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC53
	add	x2, x2, :lo12:.LC53
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w1, [x21, 88]
	adrp	x2, .LC54
	ldr	x0, [x21, 80]
	add	x2, x2, :lo12:.LC54
	ldrh	w3, [x0, x1, lsl 1]
	mov	x1, 64
	mov	x0, x20
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 138]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC55
	add	x2, x2, :lo12:.LC55
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 142]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC56
	add	x2, x2, :lo12:.LC56
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 136]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC57
	add	x2, x2, :lo12:.LC57
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 144]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC58
	add	x2, x2, :lo12:.LC58
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 140]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC59
	add	x2, x2, :lo12:.LC59
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 186]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC60
	add	x2, x2, :lo12:.LC60
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 190]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC61
	add	x2, x2, :lo12:.LC61
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 184]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC62
	add	x2, x2, :lo12:.LC62
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrb	w3, [x21, 192]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC63
	add	x2, x2, :lo12:.LC63
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x21, 188]
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC64
	add	x2, x2, :lo12:.LC64
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w5, [x21, 2796]
	mov	x1, 64
	ldr	w4, [x21, 2804]
	adrp	x2, .LC65
	ldr	w3, [x21, 2800]
	add	x2, x2, :lo12:.LC65
	mov	x0, x20
	bl	snprintf
	add	x19, x20, x0, sxtw
	ldr	w3, [x21, 2792]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC66
	add	x2, x2, :lo12:.LC66
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldr	w3, [x21, 2816]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC67
	add	x2, x2, :lo12:.LC67
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldrh	w3, [x21, 3232]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC68
	add	x2, x2, :lo12:.LC68
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldrh	w3, [x21, 3234]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC69
	add	x2, x2, :lo12:.LC69
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldr	w3, [x21, 3236]
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC70
	add	x2, x2, :lo12:.LC70
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldrh	w3, [x21, 3240]
	adrp	x2, .LC71
	add	x2, x2, :lo12:.LC71
	mov	x1, 64
	mov	x0, x19
	bl	snprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	and	w3, w0, 65535
	mov	x1, 64
	mov	x0, x19
	adrp	x2, .LC72
	add	x2, x2, :lo12:.LC72
	bl	snprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x21, 236]
	bl	GetFreeBlockMaxEraseCount
	and	w3, w0, 65535
	adrp	x2, .LC73
	mov	x0, x19
	add	x2, x2, :lo12:.LC73
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
	ldrh	w0, [x21, 184]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L207
	ldr	x1, [x21, 80]
	ubfiz	x0, x0, 1, 16
	adrp	x2, .LC74
	add	x2, x2, :lo12:.LC74
	ldrh	w3, [x1, x0]
	mov	x0, x19
	mov	x1, 64
	bl	snprintf
	add	x19, x19, x0, sxtw
.L207:
	mov	w0, 0
	adrp	x24, .LC75
	bl	List_get_gc_head_node
	add	x21, x23, :lo12:.LANCHOR0
	and	w4, w0, 65535
	add	x24, x24, :lo12:.LC75
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
.L209:
	cmp	w4, w27
	beq	.L208
	umull	x25, w4, w26
	ldr	x0, [x21, 2568]
	ldr	x2, [x21, 80]
	ubfiz	x1, x4, 1, 16
	ldr	x3, [x21, 2608]
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
	ldr	x0, [x21, 2568]
	cmp	w20, 16
	ldrh	w4, [x0, x25]
	bne	.L209
.L208:
	add	x23, x23, :lo12:.LANCHOR0
	adrp	x21, .LC76
	add	x21, x21, :lo12:.LC76
	mov	w20, 0
	mov	w25, 65535
	mov	w26, 6
	ldr	x0, [x23, 2568]
	ldr	x4, [x23, 2600]
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
	ldr	x0, [x23, 2568]
	ldr	x2, [x23, 2608]
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
	ldr	x0, [x23, 2568]
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
	adrp	x3, .LC0
	adrp	x2, .LC77
	add	x3, x3, :lo12:.LC0
	add	x29, sp, 0
	add	x2, x2, :lo12:.LC77
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
	ldr	w2, [x0, 2684]
	ldr	w1, [x0, 2696]
	cmp	w2, w1
	bcs	.L225
	ldrh	w2, [x0, 252]
	mov	x1, 0
	ldr	x4, [x0, 2608]
	str	wzr, [x0, 2676]
.L226:
	cmp	w2, w1
	bhi	.L227
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w0, [x1, 2676]
	udiv	w2, w0, w2
	str	w2, [x1, 2684]
	ldr	w2, [x1, 2680]
	sub	w0, w0, w2
	ldrh	w2, [x1, 304]
	udiv	w0, w0, w2
	str	w0, [x1, 2676]
.L228:
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w9, [x1, 2696]
	ldr	w11, [x1, 2684]
	add	w0, w9, 256
	cmp	w0, w11
	bls	.L233
	ldr	w1, [x1, 2692]
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
	ldr	w5, [x0, 2676]
	add	w3, w3, w5
	str	w3, [x0, 2676]
	b	.L226
.L225:
	ldr	w1, [x0, 2692]
	cmp	w2, w1
	bls	.L228
	add	w1, w1, 1
	str	w1, [x0, 2692]
	mov	w1, 0
.L230:
	ldrh	w2, [x0, 252]
	cmp	w1, w2
	bcs	.L228
	ldr	x4, [x0, 2608]
	ubfiz	x3, x1, 1, 32
	add	w1, w1, 1
	ldrh	w2, [x4, x3]
	add	w2, w2, 1
	strh	w2, [x4, x3]
	b	.L230
.L233:
	add	x10, x8, :lo12:.LANCHOR0
	ldrh	w0, [x10, 236]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	and	w6, w0, 65535
	add	w0, w9, 64
	cmp	w6, w0
	bcc	.L235
	ldr	x0, [x10, 2576]
	cbz	x0, .L235
	mov	w4, 65535
	ldrh	w12, [x10, 252]
	mov	x13, -6148914691236517206
	ldr	x7, [x10, 2568]
	ldr	x5, [x10, 2608]
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
	str	w4, [x0, 2696]
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
	ldr	w0, [x0, 2692]
	cmp	w3, w0
	bcs	.L235
.L241:
	add	x19, x8, :lo12:.LANCHOR0
	ldrh	w5, [x5, x7]
	mov	w2, w11
	mov	w1, w20
	ldr	x0, [x19, 80]
	ldr	w3, [x19, 2692]
	ldrh	w4, [x0, x7]
	adrp	x0, .LC78
	add	x0, x0, :lo12:.LC78
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
	adrp	x2, .LANCHOR0+80
	ubfiz	x1, x0, 1, 16
	add	x29, sp, 0
	ldr	x2, [x2, #:lo12:.LANCHOR0+80]
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
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	ldrh	w1, [x0]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L256
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2781
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L256:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x0, 314]
	cmp	w1, w0
	bne	.L257
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2782
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L257:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L258
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2783
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L258:
	ldrb	w0, [x19, 6]
	add	x1, x20, :lo12:.LANCHOR0
	strb	wzr, [x19, 10]
	add	x0, x0, 8
	ldrh	w3, [x1, 244]
	mov	w1, 65535
	ldrh	w0, [x19, x0, lsl 1]
.L259:
	cmp	w0, w1
	beq	.L261
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w2, [x20, 314]
	cmp	w1, w2
	bcs	.L265
	orr	w20, w1, w0, lsl 10
	ldrh	w1, [x19, 4]
	ldrb	w0, [x19, 6]
	mov	w4, 65535
	sub	w1, w1, #1
	and	w1, w1, 65535
	strh	w1, [x19, 4]
.L264:
	add	w0, w0, 1
	and	w0, w0, 255
	cmp	w3, w0
	bne	.L263
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	w0, 0
.L263:
	add	x5, x19, x0, sxtw 1
	ldrh	w5, [x5, 16]
	cmp	w5, w4
	beq	.L264
	strb	w0, [x19, 6]
	ldrh	w0, [x19, 2]
	cmp	w0, w2
	bne	.L255
	cbz	w1, .L255
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2806
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L255:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L261:
	ldrb	w0, [x19, 6]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	cmp	w0, w3
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
.L265:
	mov	w20, 65535
	b	.L255
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
.L272:
	ldrh	w2, [x1, 244]
	cmp	w3, w2
	bcc	.L273
	add	x0, x0, :lo12:.LANCHOR0
	mov	w6, 24
	mov	w5, 4
.L274:
	ldr	w1, [x0, 3288]
	cmp	w2, w1
	bcc	.L275
	ret
.L273:
	umull	x4, w3, w8
	ldr	x6, [x1, 3256]
	ldr	x7, [x1, 3264]
	add	x2, x6, x4
	str	w9, [x2, 16]
	ldrh	w2, [x1, 322]
	mul	w2, w2, w3
	sdiv	w2, w2, w5
	add	x2, x7, x2, sxtw 2
	str	x2, [x6, x4]
	ldrh	w2, [x1, 324]
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
	b	.L272
.L275:
	umull	x4, w2, w6
	ldr	x3, [x0, 3256]
	ldr	x7, [x0, 3264]
	add	x1, x3, x4
	str	wzr, [x1, 16]
	ldrh	w1, [x0, 322]
	mul	w1, w1, w2
	sdiv	w1, w1, w5
	add	x1, x7, x1, sxtw 2
	str	x1, [x3, x4]
	ldrh	w1, [x0, 324]
	ldr	x3, [x0, 3256]
	add	x3, x3, x4
	ldr	x4, [x0, 3272]
	mul	w1, w1, w2
	add	w2, w2, 1
	and	w2, w2, 65535
	sdiv	w1, w1, w5
	add	x1, x4, x1, sxtw 2
	str	x1, [x3, 8]
	b	.L274
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
.L277:
	cmp	w3, w1
	bcs	.L276
	ubfiz	x4, x3, 5, 16
	mov	w2, 0
	add	x4, x0, x4
	b	.L282
.L278:
	add	w2, w2, 1
	and	w2, w2, 65535
.L282:
	cmp	w2, w7
	bcs	.L279
	umull	x6, w2, w8
	add	x9, x5, x6
	ldr	x10, [x5, x6]
	ldr	x6, [x4, 8]
	cmp	x10, x6
	bne	.L278
	str	wzr, [x9, 16]
.L279:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L277
.L276:
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
.L284:
	cmp	w3, w1
	bcs	.L283
	mov	w2, 0
	b	.L289
.L285:
	add	w2, w2, 1
	and	w2, w2, 65535
.L289:
	cmp	w2, w5
	bcs	.L286
	umaddl	x4, w2, w7, x6
	ldr	w9, [x4, 16]
	cbnz	w9, .L285
	ubfiz	x2, x3, 5, 16
	ldr	x9, [x4]
	add	x2, x0, x2
	str	w8, [x4, 16]
	str	x9, [x2, 8]
	ldr	x4, [x4, 8]
	str	x4, [x2, 16]
.L286:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L284
.L283:
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
.L291:
	cmp	w2, w1, uxth
	bhi	.L293
	mov	w0, 0
	ret
.L293:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L291
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
.L296:
	and	w2, w4, 65535
	cmp	w2, w1
	bcc	.L298
	bne	.L297
	and	x4, x4, 65535
	strh	w0, [x6, x4, lsl 1]
	add	x4, x3, :lo12:.LANCHOR0
	ldrh	w0, [x4, 3292]
	add	w0, w0, 1
	strh	w0, [x4, 3292]
	b	.L297
.L298:
	add	x4, x4, 1
	add	x2, x6, x4, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w9
	bne	.L296
.L297:
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
	adrp	x0, .LC79
	add	x0, x0, :lo12:.LC79
	bl	sftl_printk
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w3, [x0, 234]
	cmp	w19, w3
	beq	.L301
	ldrh	w2, [x0, 232]
	cmp	w19, w2
	beq	.L301
	mov	w1, 65535
	cmp	w3, w1
	bne	.L302
	strh	w19, [x0, 234]
.L301:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L302:
	cmp	w2, w1
	bne	.L301
	strh	w19, [x0, 232]
	b	.L301
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
	ldrh	w1, [x20, -8]
	mov	w2, w21
	mov	w22, w0
	adrp	x0, .LC80
	add	x0, x0, :lo12:.LC80
	bl	sftl_printk
	mov	w0, w22
	bl	FtlGcRefreshBlock
	ldrh	w0, [x20, -8]
	mov	x1, 0
.L305:
	cmp	w0, w1, uxth
	bhi	.L307
	cmp	w0, 15
	bhi	.L306
	add	x19, x19, :lo12:.LANCHOR0
	add	w1, w0, 1
	add	x19, x19, 3328
	strh	w1, [x19, -8]
	strh	w21, [x19, w0, sxtw 1]
	b	.L306
.L307:
	add	x1, x1, 1
	add	x2, x20, x1, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w21
	bne	.L305
.L306:
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
	ldrh	w1, [x0, 3320]
	cbz	w1, .L310
	ldrh	w3, [x0, 234]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L310
	ldrh	w2, [x0, 3362]
	cmp	w2, w1
	bcc	.L311
	strh	wzr, [x0, 3362]
.L311:
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x19, 3328
	ldrh	w1, [x19, 3362]
	ldrh	w0, [x0, x1, lsl 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 3362]
	add	w0, w0, 1
	strh	w0, [x19, 3362]
.L310:
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
	adrp	x24, .LC81
	stp	x25, x26, [sp, 64]
	mov	w23, w2
	stp	x27, x28, [sp, 80]
	adrp	x25, .LC84
	mov	x27, x0
	mov	w26, w3
	add	x24, x24, :lo12:.LC81
	add	x25, x25, :lo12:.LC84
	stp	x19, x20, [sp, 16]
	mov	w22, 0
	mov	x19, 0
	mov	w20, 0
	adrp	x28, .LC83
.L321:
	cmp	w26, w19
	bhi	.L327
	adrp	x0, .LC85
	add	x0, x0, :lo12:.LC85
	bl	sftl_printk
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L327:
	cbnz	w20, .L322
	mov	w3, w22
	mov	x2, x21
	mov	x1, x27
	mov	x0, x24
	bl	sftl_printk
.L322:
	cmp	w23, 4
	bne	.L323
	ldr	w1, [x21, x19, lsl 2]
	adrp	x0, .LC82
	add	x0, x0, :lo12:.LC82
.L329:
	bl	sftl_printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L326
	mov	w20, 0
	adrp	x0, .LC85
	add	x0, x0, :lo12:.LC85
	bl	sftl_printk
.L326:
	add	x19, x19, 1
	add	w22, w22, w23
	b	.L321
.L323:
	cmp	w23, 2
	bne	.L325
	ldrh	w1, [x21, x19, lsl 1]
	add	x0, x28, :lo12:.LC83
	b	.L329
.L325:
	ldrb	w1, [x21, x19]
	mov	x0, x25
	b	.L329
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
.L331:
	cmp	x21, x23
	beq	.L335
	ldr	w0, [x21]
	add	x2, x29, 88
	add	x1, x29, 92
	bl	l2p_addr_tran.isra.0
	ldr	w0, [x29, 88]
	cbnz	w0, .L332
	ldr	w1, [x29, 92]
	cmp	w26, w1
	bls	.L332
	mov	x19, x20
	add	x22, x20, x22
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x21, .LC86
	add	x20, x20, 160
	add	x21, x21, :lo12:.LC86
.L333:
	cmp	x19, x22
	bne	.L334
	bl	dump_stack
.L335:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L334:
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
	adrp	x0, .LC87
	add	x0, x0, :lo12:.LC87
	bl	rknand_print_hex
	ldr	x1, [x19, -16]
	mov	w3, 4
	adrp	x0, .LC88
	mov	w2, w3
	add	x0, x0, :lo12:.LC88
	bl	rknand_print_hex
	b	.L333
.L332:
	ldr	w1, [x29, 92]
	ldr	x2, [x19, 3376]
	blr	x2
	cmp	w0, 0
	csel	w0, w25, wzr, ne
	str	w0, [x21, -4]
	ldrh	w0, [x19, 14]
	cmp	w0, 4
	bne	.L339
	ldrb	w0, [x29, 88]
	ldr	x2, [x19, 3376]
	ldr	w1, [x29, 92]
	add	w1, w24, w1
	blr	x2
	cbz	w0, .L339
	str	w25, [x21, -4]
.L339:
	add	x21, x21, 32
	b	.L331
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
	bcc	.L346
	adrp	x0, .LANCHOR0
	add	x20, x0, :lo12:.LANCHOR0
	mov	x19, x0
	ldrh	w2, [x20, 470]
	cmp	w2, 1024
	beq	.L346
	and	w1, w1, 65535
	cbz	w1, .L348
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
	ldr	x2, [x20, 2608]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x20, 2688]
	add	w0, w0, 1
	str	w0, [x20, 2688]
.L348:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 464
	ldrh	w1, [x0, 6]
	add	w1, w1, 1
	strh	w1, [x0, 6]
	ldrh	w1, [x0, 4]
	add	x2, x0, x1, sxtw 1
	add	w1, w1, 1
	and	w1, w1, 1023
	strh	w1, [x0, 4]
	strh	w21, [x2, 8]
.L346:
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
	ldrh	w2, [x21, 470]
	cbz	w2, .L360
	ldrh	w0, [x21, 466]
	add	x1, x21, 464
	sub	w2, w2, #1
	strh	w2, [x21, 470]
	mov	w2, 1
	add	x3, x1, x0, sxtw 1
	add	w0, w0, 1
	and	w0, w0, 1023
	strh	w0, [x21, 466]
	ldr	x0, [x21, 3416]
	ldrh	w20, [x3, 8]
	lsl	w1, w20, 10
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x21, 3416]
	bl	FlashEraseBlocks
	ldr	w0, [x21, 2688]
	add	w0, w0, 1
	str	w0, [x21, 2688]
.L357:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L358
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrh	w2, [x0, 470]
	adrp	x0, .LC89
	add	x0, x0, :lo12:.LC89
	bl	sftl_printk
.L359:
	b	.L359
.L360:
	mov	w20, 65535
	b	.L357
.L358:
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
.L363:
	cmp	w20, w1
	beq	.L367
	mov	x21, x0
	ldrh	w2, [x0], 2
	cbnz	w2, .L364
	bl	FtlFreeSysBlkQueueOut
	and	w1, w0, 65535
	strh	w0, [x21]
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L365
	adrp	x0, .LANCHOR0+470
	ldrh	w2, [x0, #:lo12:.LANCHOR0+470]
	adrp	x0, .LC90
	add	x0, x0, :lo12:.LC90
	bl	sftl_printk
.L366:
	b	.L366
.L365:
	ldr	w0, [x19, 48]
	strh	wzr, [x19, 2]
	add	w0, w0, 1
	str	w0, [x19, 48]
	ldrh	w0, [x19, 8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19, 8]
.L367:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L369
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 578
	add	x1, x1, 184
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L369:
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L364:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L363
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
	strh	wzr, [x19, 3320]
	stp	wzr, wzr, [x1, 76]
	stp	wzr, wzr, [x1, 84]
	str	wzr, [x19, 2652]
	str	wzr, [x19, 2656]
	stp	wzr, wzr, [x2, 108]
	stp	wzr, wzr, [x2, 116]
	str	wzr, [x19, 2688]
	str	wzr, [x19, 2692]
	str	wzr, [x19, 2696]
	str	wzr, [x19, 3236]
	str	wzr, [x19, 3244]
	stp	wzr, w0, [x3, 96]
	mov	w0, -1
	stp	wzr, wzr, [x3, 104]
	strh	w0, [x19, 234]
	strh	w0, [x19, 232]
	mov	w0, 32
	strh	w0, [x19, 3232]
	mov	w0, 128
	strh	w0, [x19, 3234]
	strh	wzr, [x19, 3362]
	ldrh	w0, [x19, 318]
	lsl	w0, w0, 1
	bl	ftl_malloc
	ldrh	w1, [x19, 318]
	str	x0, [x19, 3296]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19, 244]
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
	ldrh	w0, [x19, 244]
	ldrh	w21, [x19, 322]
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
	ldrh	w0, [x19, 270]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w21, [x19, 324]
	str	x0, [x19, 3520]
	ldrh	w0, [x19, 244]
	mul	w21, w21, w0
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3528]
	lsl	w0, w21, 2
	bl	ftl_malloc
	str	x0, [x19, 3536]
	ldrh	w1, [x19, 324]
	ldr	w0, [x19, 3288]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3272]
	ldrh	w0, [x19, 254]
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
	ldrh	w21, [x19, 254]
	str	x0, [x19, 3560]
	add	x0, x0, 32
	str	x0, [x19, 2608]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3568]
	mov	w0, w21
	bl	ftl_malloc
	ldr	w21, [x19, 340]
	str	x0, [x19, 80]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3576]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3584]
	ldrh	w0, [x19, 254]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19, 24]
	ldrh	w0, [x19, 332]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19, 360]
	ldrh	w0, [x19, 332]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19, 3592]
	ldrh	w0, [x19, 332]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3600]
	ldrh	w0, [x19, 334]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w2, [x19, 334]
	mov	w1, 0
	str	x0, [x19, 3608]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x19, 348]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3616]
	ldr	w0, [x19, 340]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3624]
	ldrh	w0, [x19, 350]
	lsl	w0, w0, 4
	bl	ftl_malloc
	ldrh	w1, [x19, 350]
	str	x0, [x19, 2616]
	ldrh	w0, [x19, 322]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3632]
	ldrh	w1, [x19, 254]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 2568]
	ldrh	w0, [x19, 310]
	ldrh	w1, [x19, 266]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19, 3640]
	mul	w0, w1, w0
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w4, [x19, 3640]
	add	x5, x19, 408
	ldrh	w6, [x19, 266]
	mov	x1, 1
	str	x0, [x19, 400]
	mov	w2, w4
	mov	x0, x20
.L374:
	cmp	w1, w6
	bcc	.L375
	mov	w2, 8
	add	x4, x0, :lo12:.LANCHOR0
	sub	w2, w2, w1
	add	x4, x4, 368
	add	x2, x2, 1
	mov	x3, 0
.L376:
	add	x3, x3, 1
	cmp	x3, x2
	bne	.L377
	add	x1, x0, :lo12:.LANCHOR0
	ldr	x2, [x1, 3576]
	cbnz	x2, .L378
.L380:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 216
	adrp	x0, .LC91
	add	x0, x0, :lo12:.LC91
	bl	sftl_printk
	mov	w0, -1
.L373:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L375:
	ldr	x3, [x19, 400]
	add	w1, w1, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x5], 8
	b	.L374
.L377:
	add	x5, x1, x3
	add	x5, x4, x5, lsl 3
	str	xzr, [x5, 24]
	b	.L376
.L378:
	ldr	x2, [x1, 3584]
	cbz	x2, .L380
	ldr	x2, [x1, 3616]
	cbz	x2, .L380
	ldr	x2, [x1, 3624]
	cbz	x2, .L380
	ldr	x2, [x1, 2616]
	cbz	x2, .L380
	ldr	x2, [x1, 3632]
	cbz	x2, .L380
	ldr	x2, [x1, 2568]
	cbz	x2, .L380
	ldr	x2, [x1, 400]
	cbz	x2, .L380
	ldr	x1, [x1, 80]
	cbz	x1, .L380
	add	x1, x0, :lo12:.LANCHOR0
	ldr	x2, [x1, 3296]
	cbz	x2, .L380
	ldr	x2, [x1, 3312]
	cbz	x2, .L380
	ldr	x2, [x1, 3440]
	cbz	x2, .L380
	ldr	x2, [x1, 3456]
	cbz	x2, .L380
	ldr	x2, [x1, 3416]
	cbz	x2, .L380
	ldr	x2, [x1, 3280]
	cbz	x2, .L380
	ldr	x2, [x1, 3448]
	cbz	x2, .L380
	ldr	x2, [x1, 3464]
	cbz	x2, .L380
	ldr	x2, [x1, 3472]
	cbz	x2, .L380
	ldr	x1, [x1, 3480]
	cbz	x1, .L380
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3264]
	cbz	x1, .L380
	ldr	x1, [x0, 3488]
	cbz	x1, .L380
	ldr	x1, [x0, 3496]
	cbz	x1, .L380
	ldr	x1, [x0, 3256]
	cbz	x1, .L380
	ldr	x1, [x0, 3528]
	cbz	x1, .L380
	ldr	x1, [x0, 3536]
	cbz	x1, .L380
	ldr	x1, [x0, 3272]
	cbz	x1, .L380
	ldr	x1, [x0, 2608]
	cbz	x1, .L380
	ldr	x1, [x0, 3552]
	cbz	x1, .L380
	ldr	x0, [x0, 360]
	cbz	x0, .L380
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3592]
	cbz	x1, .L380
	ldr	x1, [x0, 3600]
	cbz	x1, .L380
	ldr	x0, [x0, 3608]
	cbz	x0, .L380
	mov	w0, 0
	b	.L373
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
	adrp	x24, .LC8
	add	x20, x20, 232
	ldrh	w2, [x0, 3640]
	add	x24, x24, :lo12:.LC8
	mov	x19, 0
	mov	x22, x1
	mov	x0, x22
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
.L478:
	ldrh	w0, [x23, x19]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L475
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w1, [x1, 310]
	cmp	w1, w0
	bhi	.L477
	mov	w2, 74
	mov	x1, x20
	mov	x0, x24
	bl	sftl_printk
.L477:
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
	bne	.L478
.L475:
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
	add	x0, x0, 380
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
	strh	w0, [x3, 464]
	add	x0, x3, 472
	strh	wzr, [x3, 466]
	strh	wzr, [x3, 468]
	strh	wzr, [x3, 470]
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
.L487:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L491
	adrp	x0, .LANCHOR0+316
	mov	w23, 0
	mov	w20, 0
	ldrh	w1, [x0, #:lo12:.LANCHOR0+316]
	ldrh	w0, [x19]
	strh	w1, [x21, x0, lsl 1]
	ldrh	w24, [x21]
.L492:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L496
	mov	w0, w23
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L491:
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20, x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L488:
	ldrh	w3, [x19, 10]
	cmp	w3, w1
	bhi	.L490
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L487
.L490:
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22, x3]
	cmp	w4, w2
	bne	.L489
	cbz	w2, .L489
	ldrh	w4, [x21, x3]
	add	w4, w4, 1
	strh	w4, [x21, x3]
.L489:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L488
.L496:
	ubfiz	x0, x20, 1, 16
	ldrh	w1, [x21, x0]
	cmp	w24, w1
	bls	.L493
	add	x25, x22, x0
	ldrh	w0, [x22, x0]
	cbnz	w0, .L494
.L495:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L492
.L493:
	cbnz	w1, .L495
	add	x25, x22, x0
	ldrh	w0, [x22, x0]
	cbz	w0, .L495
.L497:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19, 8]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	b	.L495
.L498:
	mov	w24, 0
	b	.L497
.L494:
	mov	w23, w20
	cbz	w1, .L498
	mov	w24, w1
	b	.L495
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
	ldr	w2, [x19, 340]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x19, 350]
	mov	w1, 255
	ldrh	w2, [x19, 322]
	mul	w2, w2, w0
	ldr	x0, [x19, 3632]
	bl	ftl_memset
	mov	x0, x19
	mov	w1, 0
	mov	w5, -1
.L508:
	ldrh	w2, [x0, 350]
	cmp	w2, w1
	bhi	.L509
	mov	w2, -1
	strh	w2, [x0, 3650]
	strh	w2, [x0, 3648]
	add	x1, x0, 3648
	ldr	w2, [x0, 340]
	strh	w2, [x0, 3658]
	mov	w2, -3902
	strh	w2, [x0, 3652]
	ldrh	w2, [x0, 3712]
	strh	w2, [x0, 3656]
	ldrh	w2, [x0, 348]
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
.L509:
	ldr	x3, [x0, 2616]
	ubfiz	x2, x1, 4, 16
	add	x3, x3, x2
	str	wzr, [x3, 4]
	ldr	x3, [x0, 2616]
	strh	w5, [x3, x2]
	ldr	x3, [x0, 2616]
	ldr	x4, [x0, 3632]
	add	x3, x3, x2
	ldrh	w2, [x0, 322]
	mul	w2, w2, w1
	add	w1, w1, 1
	and	w1, w1, 65535
	sxtw	x2, w2
	and	x2, x2, -4
	add	x2, x4, x2
	str	x2, [x3, 8]
	b	.L508
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
	ldr	x0, [x19, 360]
	ldrh	w2, [x19, 332]
	strh	wzr, [x19, 356]
	str	wzr, [x19, 3716]
	lsl	w2, w2, 1
	str	wzr, [x19, 3720]
	bl	ftl_memset
	ldr	x0, [x19, 2608]
	mov	w1, 0
	ldrh	w2, [x19, 254]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 3552]
	mov	w1, 0
	ldrh	w2, [x19, 254]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w2, 48
	mov	w1, 0
	add	x0, x19, 2520
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
	add	x23, x19, 272
	stp	x21, x22, [sp, 32]
	mov	w20, 0
	mov	w22, 0
	ldrh	w2, [x19, 254]
	mov	w21, 0
	mul	w2, w2, w0
	ldr	x0, [x19, 2568]
	bl	ftl_memset
	strh	wzr, [x19, 2592]
	strh	wzr, [x19, 236]
	str	xzr, [x19, 2576]
	str	xzr, [x19, 2584]
	str	xzr, [x19, 2600]
.L514:
	ldrh	w0, [x19, 252]
	cmp	w20, w0
	bcs	.L521
	ldrh	w8, [x19, 244]
	mov	x6, 0
	ldrh	w7, [x19, 314]
	mov	w5, 0
	b	.L522
.L516:
	ldrb	w0, [x23, x6]
	mov	w1, w20
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L515
	add	w5, w5, w7
	and	w5, w5, 65535
.L515:
	add	x6, x6, 1
.L522:
	cmp	w8, w6, uxth
	bhi	.L516
	cbz	w5, .L517
	mov	w0, 32768
	sdiv	w5, w0, w5
.L518:
	ldr	x1, [x19, 2568]
	mov	w0, 6
	umaddl	x0, w20, w0, x1
	strh	w5, [x0, 4]
	ldrh	w0, [x19, 32]
	cmp	w0, w20
	beq	.L519
	ldrh	w0, [x19, 88]
	cmp	w0, w20
	beq	.L519
	ldrh	w0, [x19, 136]
	cmp	w0, w20
	beq	.L519
	ldr	x1, [x19, 80]
	ubfiz	x0, x20, 1, 16
	ldrh	w0, [x1, x0]
	cbnz	w0, .L520
	add	w22, w22, 1
	mov	w0, w20
	and	w22, w22, 65535
	bl	INSERT_FREE_LIST
.L519:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L514
.L517:
	ldr	x1, [x19, 80]
	ubfiz	x0, x20, 1, 16
	mov	w2, -1
	strh	w2, [x1, x0]
	b	.L518
.L520:
	add	w21, w21, 1
	mov	w0, w20
	and	w21, w21, 65535
	bl	INSERT_DATA_LIST
	b	.L519
.L521:
	strh	w21, [x19, 2592]
	add	w21, w21, w22
	strh	w22, [x19, 236]
	cmp	w21, w0
	ble	.L523
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2210
	add	x1, x1, 248
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L523:
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
	ldrh	w2, [x19, 318]
	strh	wzr, [x19, 3292]
	strh	wzr, [x19, 3304]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x19, 318]
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
	bls	.L528
	mov	w2, 256
	mov	w1, 255
	mov	x0, x20
	bl	ftl_memset
	mov	w0, 0
.L528:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19, 14]
	cmp	w1, 4
	bne	.L532
	mov	x1, 0
.L530:
	cmp	w0, w1, uxth
	bhi	.L531
.L532:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L531:
	ldrh	w2, [x20, x1, lsl 1]
	lsr	w2, w2, 1
	strh	w2, [x20, x1, lsl 1]
	add	x1, x1, 1
	b	.L530
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
	adrp	x24, .LC8
	mov	x19, x0
	add	x23, x0, x1
	ldrh	w25, [x2, 12]
	add	x22, x22, 272
	add	x24, x24, :lo12:.LC8
.L537:
	cmp	x23, x19
	bne	.L550
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L550:
	ldr	x0, [x19, 8]
	cbz	x0, .L538
	ldr	x0, [x19, 16]
	cbnz	x0, .L539
.L538:
	mov	w2, 96
	mov	x1, x22
	mov	x0, x24
	bl	sftl_printk
.L539:
	ldr	w0, [x19, 4]
	add	x2, x29, 104
	add	x1, x29, 108
	bl	l2p_addr_tran.isra.0
	ldr	w0, [x29, 104]
	cmp	w0, 3
	bls	.L540
	mov	w0, -1
	str	w0, [x19]
.L541:
	add	x19, x19, 32
	b	.L537
.L540:
	ldr	x20, [x19, 8]
	tst	x20, 63
	beq	.L542
	add	x1, x21, :lo12:.LANCHOR0
	ldr	x20, [x1, 3504]
.L542:
	add	x27, x21, :lo12:.LANCHOR0
	ldr	w1, [x29, 108]
	ldr	x3, [x19, 16]
	mov	x2, x20
	ldr	x4, [x27, 3392]
	blr	x4
	str	w0, [x19]
	ldrh	w0, [x27, 14]
	cmp	w0, 4
	bne	.L544
	ldrb	w0, [x29, 104]
	add	x2, x20, 2048
	ldr	x4, [x27, 3392]
	ldr	w1, [x29, 108]
	ldr	x3, [x19, 16]
	add	w1, w25, w1
	add	x3, x3, 8
	blr	x4
	cmn	w0, #1
	beq	.L545
	ldr	x1, [x19, 16]
	ldr	w2, [x1, 12]
	cmn	w2, #1
	bne	.L546
	ldr	w2, [x1, 8]
	cmn	w2, #1
	bne	.L546
	ldr	w1, [x1]
	cmn	w1, #1
	beq	.L546
.L545:
	mov	w1, -1
	str	w1, [x19]
.L546:
	ldr	w1, [x19]
	cmn	w1, #1
	beq	.L547
	cmp	w0, 256
	bne	.L547
	str	w0, [x19]
.L547:
	ldr	w3, [x19]
	cmp	w3, 256
	ccmn	w3, #1, 4, ne
	bne	.L544
	ldr	w1, [x19, 4]
	adrp	x0, .LC92
	ldr	w2, [x29, 108]
	add	x0, x0, :lo12:.LC92
	bl	sftl_printk
	ldr	x1, [x19, 8]
	cbz	x1, .L549
	mov	w3, 4
	adrp	x0, .LC93
	mov	w2, w3
	add	x0, x0, :lo12:.LC93
	bl	rknand_print_hex
.L549:
	ldr	x1, [x19, 16]
	cbz	x1, .L544
	mov	w3, 4
	adrp	x0, .LC94
	mov	w2, w3
	add	x0, x0, :lo12:.LC94
	bl	rknand_print_hex
.L544:
	add	x1, x21, :lo12:.LANCHOR0
	ldr	x0, [x1, 3504]
	cmp	x20, x0
	bne	.L541
	ldr	x0, [x19, 8]
	cmp	x20, x0
	beq	.L541
	ldrh	w2, [x1, 270]
	mov	x1, x20
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L541
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
	add	x22, x0, 380
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
.L577:
	ldrh	w0, [x19, 266]
	cmp	w21, w0
	bcc	.L582
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L582:
	ldrh	w20, [x19, 310]
	mov	w0, -1
	strh	w0, [x22]
.L579:
	ldrh	w0, [x19, 310]
	sub	w20, w20, #1
	and	w20, w20, 65535
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L580
	madd	w0, w0, w21, w20
	mov	w2, 1
	mov	w1, w2
	lsl	w0, w0, 10
	str	w0, [x25, 4]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L579
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L579
	strh	w20, [x22]
.L580:
	add	w21, w21, 1
	add	x22, x22, 2
	b	.L577
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
	bne	.L588
	ldrh	w19, [x2, 316]
.L589:
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
	bne	.L590
	mov	w22, 0
	mov	w24, 2
.L591:
	cmp	w22, w19
	ble	.L594
.L590:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 160
	ret
.L588:
	ldrh	w19, [x2, 314]
	b	.L589
.L594:
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
	bne	.L592
	ldr	w0, [x29, 100]
	cmn	w0, #1
	bne	.L592
	ldr	w0, [x29, 64]
	cmn	w0, #1
	beq	.L592
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L591
.L592:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L591
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
	ldr	w2, [x19, 340]
	strh	wzr, [x19, 3712]
	strh	wzr, [x19, 356]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x19, 3576]
	mov	w1, 0
	ldr	w2, [x19, 340]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 3600]
	mov	w1, 0
	ldrh	w2, [x19, 332]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x19, 360]
	mov	w1, 0
	ldrh	w2, [x19, 332]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w2, 16
	mov	w1, 255
	add	x0, x19, 2704
	bl	ftl_memset
	ldrh	w24, [x19, 252]
.L600:
	ldrh	w0, [x19, 254]
	cmp	w0, w24
	bls	.L640
	ldrh	w9, [x19, 244]
	mov	x5, 0
	ldrh	w8, [x19, 324]
	mov	w20, 0
	add	x7, x19, 272
	mov	w6, 4
	b	.L641
.L602:
	ldrb	w0, [x7, x5]
	mov	w1, w24
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L601
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
.L601:
	add	x5, x5, 1
.L641:
	cmp	w9, w5, uxth
	bhi	.L602
	cbnz	w20, .L603
.L639:
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L600
.L603:
	ldr	x0, [x19, 3440]
	mov	w1, w20
	mov	w2, 1
	ubfiz	x20, x20, 5, 16
	mov	x23, 0
	add	x27, x19, 2704
	bl	FlashReadPages
.L638:
	ldr	x0, [x19, 3440]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	ldr	w22, [x1, 4]
	cmn	w0, #1
	ldr	x26, [x1, 16]
	ubfx	x22, x22, 10, 16
	bne	.L606
	mov	w28, 16
	mov	w3, 65535
.L608:
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
	bne	.L605
	mov	w1, -1
	str	w1, [x0, x23]
	ldr	x0, [x19, 3440]
	ldr	w0, [x0, x23]
	cmp	w0, w1
	bne	.L606
.L607:
	mov	w1, 1
	b	.L678
.L605:
	ldr	w0, [x0, x23]
	cmn	w0, #1
	bne	.L606
	sub	w28, w28, #1
	ands	w28, w28, 65535
	bne	.L608
	b	.L607
.L606:
	ldr	w1, [x19, 2668]
	ldr	w0, [x26, 4]
	cmn	w1, #1
	beq	.L609
	cmp	w1, w0
	bhi	.L610
.L609:
	cmn	w0, #1
	beq	.L610
	add	w1, w0, 1
	str	w1, [x19, 2668]
.L610:
	ldrh	w1, [x26]
	mov	w2, 61604
	cmp	w1, w2
	beq	.L612
	bhi	.L613
	mov	w0, 61574
	cmp	w1, w0
	beq	.L614
.L611:
	add	x23, x23, 32
	cmp	x20, x23
	bne	.L638
	b	.L639
.L613:
	mov	w0, 61634
	cmp	w1, w0
	beq	.L615
	mov	w0, 65535
	cmp	w1, w0
	bne	.L611
	mov	w1, 0
.L678:
	mov	w0, w22
	bl	FtlFreeSysBlkQueueIn
	b	.L611
.L615:
	ldrh	w1, [x19, 3712]
	ldr	w0, [x19, 340]
	cmp	w1, w0
	bls	.L617
	adrp	x0, .LC8
	mov	w2, 1225
	mov	x1, x25
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L617:
	ldr	w3, [x19, 340]
	ldrh	w1, [x19, 3712]
	and	w2, w3, 65535
	ldr	x4, [x19, 3624]
	sub	w0, w2, #1
	sub	w2, w2, w1
	sub	w2, w2, #1
	sxth	x0, w0
	sxth	w2, w2
.L618:
	cmp	w0, w2
	bgt	.L624
	tbz	w0, #31, .L658
	b	.L611
.L624:
	sxtw	x5, w0
	ldr	w8, [x26, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L619
	ldr	w2, [x4]
	cbnz	w2, .L620
	cmp	w3, w1
	beq	.L620
	add	w1, w1, 1
	strh	w1, [x19, 3712]
.L620:
	mov	w1, 0
.L621:
	cmp	w1, w0
	bne	.L622
	ldr	x1, [x19, 3624]
	ldr	w2, [x26, 4]
	str	w2, [x1, x6]
	ldr	x1, [x19, 3576]
	strh	w22, [x1, x5, lsl 1]
	tbnz	w0, #31, .L611
	ldrh	w1, [x19, 3712]
	ldr	w2, [x19, 340]
	sub	w2, w2, w1
	sub	w2, w2, #1
	cmp	w0, w2, sxth
	bgt	.L611
.L658:
	add	w1, w1, 1
	strh	w1, [x19, 3712]
	ldr	x1, [x19, 3624]
	ldr	w2, [x26, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 3576]
.L676:
	strh	w22, [x1, x0, lsl 1]
	b	.L611
.L622:
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
	b	.L621
.L619:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L618
.L614:
	ldrh	w1, [x19, 356]
	ldrh	w0, [x19, 332]
	cmp	w1, w0
	bls	.L627
	adrp	x0, .LC8
	mov	w2, 1266
	mov	x1, x25
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L627:
	ldrh	w3, [x19, 332]
	ldrh	w1, [x19, 356]
	sub	w2, w3, #1
	ldr	x4, [x19, 3600]
	sxth	x0, w2
	sub	w2, w2, w1
.L628:
	cmp	w0, w2
	ble	.L633
	sxtw	x5, w0
	ldr	w8, [x26, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L629
	ldr	w2, [x4]
	cbnz	w2, .L630
	cmp	w3, w1
	beq	.L630
	add	w1, w1, 1
	strh	w1, [x19, 356]
.L630:
	mov	w1, 0
.L631:
	cmp	w1, w0
	bne	.L632
	ldr	x1, [x19, 3600]
	ldr	w2, [x26, 4]
	str	w2, [x1, x6]
	ldr	x1, [x19, 360]
	strh	w22, [x1, x5, lsl 1]
.L633:
	tbnz	w0, #31, .L611
	ldrh	w1, [x19, 332]
	ldrh	w2, [x19, 356]
	sub	w1, w1, #1
	sub	w1, w1, w2
	cmp	w0, w1, sxth
	bgt	.L611
	ldr	x1, [x19, 3600]
	add	w2, w2, 1
	strh	w2, [x19, 356]
	ldr	w2, [x26, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 360]
	b	.L676
.L632:
	ldr	x4, [x19, 3600]
	sxtw	x2, w1
	lsl	x3, x2, 2
	lsl	x2, x2, 1
	add	x7, x4, x3
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w7, [x7, 4]
	str	w7, [x4, x3]
	ldr	x3, [x19, 360]
	add	x4, x3, x2
	ldrh	w4, [x4, 2]
	strh	w4, [x3, x2]
	b	.L631
.L629:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L628
.L612:
	ldrh	w2, [x27]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L635
	strh	w22, [x27]
.L677:
	str	w0, [x27, 8]
	b	.L611
.L635:
	ldrh	w0, [x27, 4]
	cmp	w0, w1
	beq	.L636
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L636:
	ldr	w0, [x26, 4]
	ldr	w1, [x27, 8]
	cmp	w1, w0
	bcs	.L637
	ldrh	w0, [x27]
	strh	w0, [x27, 4]
	strh	w22, [x27]
	ldr	w0, [x26, 4]
	b	.L677
.L637:
	strh	w22, [x27, 4]
	b	.L611
.L640:
	ldr	x1, [x19, 3576]
	ldrh	w0, [x1]
	cbz	w0, .L642
.L645:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0, 360]
	ldrh	w2, [x1]
	cbz	w2, .L643
.L644:
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w1, [x21, 3712]
	ldr	w0, [x21, 340]
	cmp	w1, w0
	bls	.L674
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1391
	add	x1, x1, 288
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L674:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L642:
	ldrh	w0, [x19, 3712]
	cbz	w0, .L645
	ldr	w2, [x19, 340]
	mov	w0, 0
.L646:
	cmp	w0, w2
	bcs	.L645
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L647
	mov	w1, w0
	add	x2, x21, :lo12:.LANCHOR0
.L648:
	ldr	w3, [x2, 340]
	cmp	w1, w3
	bcs	.L645
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
	b	.L648
.L647:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L646
.L643:
	ldrh	w2, [x0, 356]
	cbz	w2, .L644
	ldrh	w2, [x0, 332]
	mov	w0, 0
.L653:
	mov	w6, w0
	cmp	w0, w2
	bge	.L644
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L654
	add	x1, x21, :lo12:.LANCHOR0
.L655:
	ldrh	w2, [x1, 332]
	cmp	w0, w2
	bge	.L644
	ldr	x4, [x1, 360]
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
	ldr	x2, [x1, 360]
	strh	wzr, [x2, x3]
	b	.L655
.L654:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L653
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
	ldrh	w20, [x23, 310]
	sub	w20, w20, #1
	and	w20, w20, 65535
.L680:
	ldrh	w0, [x23, 310]
	sub	w0, w0, #16
	cmp	w20, w0
	ble	.L683
	lsl	w0, w20, 10
	mov	w2, 1
	str	w0, [x21, 4]
	mov	w1, w2
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L681
	ldr	w0, [x21, 4]
	mov	w2, 1
	mov	w1, w2
	add	w0, w0, 1
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
.L681:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L682
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L682
	ldr	w1, [x22, 4]
	str	w1, [x23, 376]
	strh	w20, [x23, 368]
	ldrh	w1, [x22, 8]
	strh	w1, [x23, 372]
.L683:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21, 368]
	cmp	w1, w0
	beq	.L697
	ldrh	w1, [x21, 372]
	cmp	w1, w0
	beq	.L687
	add	x0, x21, 3728
	lsl	w1, w1, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x21, 3728]
	cmn	w0, #1
	beq	.L687
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L687
	ldr	w1, [x21, 376]
	ldr	w0, [x22, 4]
	cmp	w0, w1
	bls	.L687
	str	w0, [x21, 376]
	ldrh	w1, [x21, 372]
	ldrh	w0, [x22, 8]
	strh	w1, [x21, 368]
	strh	w0, [x21, 372]
.L687:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x23, x20, 3728
	mov	w24, 61649
	ldrh	w0, [x20, 368]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20, 370]
.L689:
	tbz	w21, #31, .L692
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 251
	add	x1, x1, 304
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L691:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 10]
	mov	w2, 65535
	strh	w0, [x1, 374]
	ldrh	w0, [x22, 12]
	cmp	w0, w2
	beq	.L694
	ldr	w2, [x1, 240]
	cmp	w0, w2
	beq	.L694
	ldrh	w1, [x1, 254]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L694
	cmp	w0, w1
	bcs	.L694
	bl	FtlSysBlkNumInit
.L694:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 0
	add	x21, x19, 400
.L695:
	ldrh	w0, [x19, 266]
	cmp	w20, w0
	bcc	.L696
	mov	w0, 0
.L679:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L682:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L680
.L692:
	ldrh	w0, [x20, 368]
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
	beq	.L690
	ldrh	w0, [x22]
	cmp	w0, w24
	beq	.L691
.L690:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L689
.L696:
	ldrh	w2, [x19, 3640]
	ldr	x0, [x19, 3736]
	mul	w1, w2, w20
	lsl	w2, w2, 2
	add	w20, w20, 1
	add	x1, x0, x1, lsl 2
	ldr	x0, [x21], 8
	bl	ftl_memcpy
	b	.L695
.L697:
	mov	w0, -1
	b	.L679
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
	adrp	x24, .LC8
	mov	w27, w3
	mov	x19, x0
	ldrh	w25, [x2, 12]
	mov	x20, x0
	add	x23, x23, 320
	add	x24, x24, :lo12:.LC8
	lsl	w28, w25, 3
.L710:
	cmp	x20, x22
	bne	.L724
	cbz	w27, .L718
	adrp	x20, .LC95
	adrp	x23, .LC96
	add	x20, x20, :lo12:.LC95
	add	x23, x23, :lo12:.LC96
	b	.L746
.L724:
	ldr	x0, [x20, 8]
	cbz	x0, .L711
	ldr	x0, [x20, 16]
	cbnz	x0, .L712
.L711:
	mov	w2, 148
	mov	x1, x23
	mov	x0, x24
	bl	sftl_printk
.L712:
	ldr	w0, [x20, 4]
	add	x2, x29, 120
	add	x1, x29, 124
	bl	l2p_addr_tran.isra.0
	ldr	w0, [x29, 120]
	cmp	w0, 3
	bls	.L713
.L748:
	mov	w0, -1
	str	w0, [x20]
	b	.L714
.L713:
	cbnz	w0, .L715
	ldr	w0, [x29, 124]
	cmp	w28, w0
	bls	.L715
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x21, .LC86
	add	x20, x20, 320
	add	x21, x21, :lo12:.LC86
	b	.L745
.L717:
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
	adrp	x0, .LC87
	add	x0, x0, :lo12:.LC87
	bl	rknand_print_hex
	ldr	x1, [x19, -16]
	mov	w3, 4
	adrp	x0, .LC88
	mov	w2, w3
	add	x0, x0, :lo12:.LC88
	bl	rknand_print_hex
.L745:
	cmp	x19, x22
	bne	.L717
	bl	dump_stack
.L718:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L715:
	ldr	x1, [x20, 8]
	tst	x1, 63
	beq	.L732
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x26, [x0, 3504]
	cmp	x1, x26
	beq	.L719
	ldrh	w2, [x0, 270]
	mov	x0, x26
	lsl	w2, w2, 9
	bl	ftl_memcpy
.L719:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29, 124]
	ldr	x3, [x20, 16]
	mov	x2, x26
	ldr	x5, [x0, 3384]
	ldrb	w0, [x29, 120]
	blr	x5
	cbnz	w0, .L720
	str	wzr, [x20]
.L721:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0, 14]
	cmp	w1, 4
	bne	.L714
	ldr	x5, [x0, 3384]
	add	x2, x26, 2048
	ldrb	w0, [x29, 120]
	ldr	w1, [x29, 124]
	ldr	x3, [x20, 16]
	add	w1, w25, w1
	add	x3, x3, 8
	blr	x5
	cbnz	w0, .L748
.L714:
	add	x20, x20, 32
	b	.L710
.L732:
	mov	x26, x1
	b	.L719
.L720:
	mov	w0, -1
	str	w0, [x20]
	b	.L721
.L730:
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
	bne	.L726
	ldr	w1, [x19, 4]
	mov	x0, x20
	bl	sftl_printk
	str	w24, [x19]
.L726:
	ldr	w24, [x29, 128]
	cmp	w24, 256
	bne	.L727
	ldr	w1, [x19, 4]
	mov	x0, x23
	bl	sftl_printk
	str	w24, [x19]
.L727:
	ldr	x0, [x19, 16]
	cbz	x0, .L728
	ldr	w2, [x0]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0, 3520]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L728
	ldr	w1, [x19, 4]
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x19]
.L728:
	ldr	x0, [x19, 8]
	cbz	x0, .L729
	ldr	w2, [x0]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0, 3504]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L729
	ldr	w1, [x19, 4]
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x19]
.L729:
	add	x19, x19, 32
.L746:
	cmp	x19, x22
	bne	.L730
	b	.L718
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
	add	x8, x5, 272
	stp	x27, x28, [sp, 80]
	and	w23, w0, 65535
	mov	w20, 0
	mov	w19, 0
	str	w23, [x5, 3436]
.L750:
	ldrh	w0, [x5, 244]
	cmp	w0, w10
	bhi	.L754
	cbz	w20, .L749
	ldr	x0, [x5, 3416]
	mov	w2, w20
	mov	w1, 0
	ubfiz	x20, x20, 5, 16
	mov	x24, 0
	add	x25, x21, :lo12:.LANCHOR0
	bl	FlashEraseBlocks
.L758:
	ldr	x0, [x25, 3416]
	add	x1, x0, x24
	ldr	w0, [x0, x24]
	cmn	w0, #1
	bne	.L757
	ldr	w0, [x1, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.L757:
	add	x24, x24, 32
	cmp	x24, x20
	bne	.L758
	cbz	w22, .L771
	add	x0, x21, :lo12:.LANCHOR0
	mov	w25, 1
	ldrh	w26, [x0, 316]
.L759:
	add	x28, x21, :lo12:.LANCHOR0
	mov	w24, 0
	add	x0, x28, 272
	str	x0, [x29, 104]
.L767:
	mov	w5, 0
	mov	w20, 0
.L760:
	ldrh	w0, [x28, 244]
	cmp	w0, w5
	bhi	.L763
	cbz	w20, .L749
	ldr	x0, [x28, 3416]
	mov	w1, w20
	mov	w3, 1
	mov	w2, w25
	mov	x27, 0
	bl	FlashProgPages
	ubfiz	x1, x20, 5, 16
.L766:
	ldr	x0, [x28, 3416]
	add	x3, x0, x27
	ldr	w0, [x0, x27]
	cbz	w0, .L765
	ldr	w0, [x3, 4]
	add	w19, w19, 1
	str	x1, [x29, 96]
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29, 96]
.L765:
	add	x27, x27, 32
	cmp	x1, x27
	bne	.L766
	add	w24, w24, 1
	cmp	w26, w24, uxth
	bhi	.L767
	add	x26, x21, :lo12:.LANCHOR0
	mov	x24, 0
.L769:
	cbz	w22, .L768
	ldr	x0, [x26, 3416]
	add	x1, x0, x24
	ldr	w0, [x0, x24]
	cbnz	w0, .L768
	ldr	w0, [x1, 4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L768:
	add	x24, x24, 32
	cmp	x24, x27
	bne	.L769
	cmp	w23, 63
	ccmp	w22, 0, 0, hi
	beq	.L749
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, w20
	mov	w1, w25
	ldr	x0, [x21, 3416]
	bl	FlashEraseBlocks
.L749:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L754:
	ldr	x1, [x5, 3416]
	ubfiz	x0, x10, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w23
	ldrb	w0, [x8, w10, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w22, .L751
	bl	IsBlkInVendorPart
	cbnz	w0, .L752
.L751:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L753
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
	ldrh	w0, [x5, 324]
	mul	w0, w0, w20
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w0, w0, w9
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L752:
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L750
.L753:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L752
.L771:
	mov	w25, 0
	mov	w26, 2
	b	.L759
.L763:
	ldr	x1, [x28, 3416]
	ubfiz	x0, x5, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w23
	ldr	x0, [x29, 104]
	ldrb	w0, [x0, w5, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w22, .L761
	bl	IsBlkInVendorPart
	cbnz	w0, .L762
.L761:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L762
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
	ldrh	w0, [x28, 324]
	mul	w0, w0, w20
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w0, w0, w2
	ldr	x2, [x28, 3496]
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L762:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L760
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
	bls	.L795
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
	cbnz	w0, .L796
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
.L794:
	mov	w2, 1
	mov	w1, 0
	add	x0, x29, 32
	str	w19, [x29, 36]
	bl	FlashEraseBlocks
.L792:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 128
	ret
.L796:
	mov	w20, -1
	b	.L794
.L795:
	mov	w20, 0
	b	.L792
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
	add	x21, x20, 400
	ldr	x2, [x20, 3528]
	ldr	x0, [x20, 3464]
	str	x2, [x20, 3744]
	ldrh	w2, [x20, 322]
	str	x0, [x20, 3736]
	bl	ftl_memset
.L799:
	ldrh	w0, [x20, 266]
	add	x1, x20, 3728
	cmp	w19, w0
	blt	.L800
	ldr	x26, [x1, 16]
	add	x19, x20, 368
	mov	w2, 16
	mov	w1, 255
	mov	x0, x26
	bl	ftl_memset
	mov	w0, -3887
	adrp	x23, .LC99
	strh	w0, [x26]
	add	x25, x25, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC99
	mov	w21, 0
	ldr	w0, [x19, 8]
	mov	w22, 0
	str	w0, [x26, 4]
	adrp	x24, .LC100
	ldrh	w0, [x20, 368]
	strh	w0, [x26, 2]
	ldrh	w0, [x19, 4]
	strh	w0, [x26, 8]
	ldrh	w0, [x19, 6]
	strh	w0, [x26, 10]
	ldr	w0, [x20, 240]
	strh	w0, [x26, 12]
	ldr	w0, [x20, 2684]
	strh	w0, [x26, 14]
.L801:
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
	mov	x0, x23
	bl	sftl_printk
	ldrh	w0, [x25, 316]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L802
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
.L802:
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
	bne	.L803
	ldr	w1, [x25, 3732]
	add	w21, w21, 1
	add	x0, x24, :lo12:.LC100
	and	w21, w21, 65535
	bl	sftl_printk
	cmp	w21, 3
	bls	.L801
	ldr	w1, [x25, 3732]
	adrp	x0, .LC101
	mov	w2, w21
	add	x0, x0, :lo12:.LC101
	bl	sftl_printk
.L805:
	b	.L805
.L800:
	ldrh	w2, [x20, 3640]
	ldr	x0, [x1, 8]
	ldr	x1, [x21], 8
	mul	w3, w19, w2
	lsl	w2, w2, 2
	add	w19, w19, 1
	add	x0, x0, x3, sxtw 2
	bl	ftl_memcpy
	b	.L799
.L806:
	mov	w22, 1
	b	.L801
.L803:
	add	w22, w22, 1
	cmp	w22, 1
	ble	.L806
	cmp	w0, 256
	beq	.L801
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
	add	x23, x22, 136
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
.L812:
	ldrh	w1, [x22, 2592]
	ldrh	w0, [x22, 236]
	add	w0, w0, w1
	ldrh	w1, [x22, 252]
	cmp	w0, w1
	ble	.L813
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2656
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L813:
	cmp	x20, x23
	bne	.L839
	ldrh	w0, [x19, 236]
	ldr	w1, [x19, 3244]
	mul	w1, w0, w1
	lsr	w0, w0, 1
	add	w0, w0, 1
	add	w1, w0, w1, lsr 2
	ands	w1, w1, 65535
	beq	.L814
	sub	w1, w1, #1
	and	w1, w1, 65535
.L814:
	add	x0, x19, 2600
	bl	List_pop_index_node
	and	w25, w0, 65535
	ldrh	w0, [x19, 236]
	cbnz	w0, .L815
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2665
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L815:
	ldrh	w0, [x19, 236]
	sub	w0, w0, #1
	strh	w0, [x19, 236]
	ldrh	w0, [x19, 252]
	cmp	w0, w25
	bls	.L812
	ldr	x0, [x19, 80]
	ubfiz	x26, x25, 1, 16
	ldrh	w24, [x0, x26]
	cbnz	w24, .L812
	strh	w25, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20, 7]
	cbnz	w0, .L817
	ldr	x0, [x19, 80]
	mov	w1, -1
	strh	w1, [x0, x26]
	mov	w0, w25
	bl	INSERT_DATA_LIST
	ldrh	w1, [x19, 2592]
	ldrh	w0, [x19, 236]
	add	w0, w0, w1
	ldrh	w1, [x19, 252]
	cmp	w0, w1
	ble	.L812
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2679
	add	x1, x1, 336
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L812
.L839:
	mov	w1, 0
	b	.L814
.L817:
	ldrh	w1, [x19, 2592]
	ldrh	w0, [x19, 236]
	add	w0, w0, w1
	ldrh	w1, [x19, 252]
	cmp	w0, w1
	ble	.L819
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2682
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L819:
	add	x27, x20, 16
	ldrh	w4, [x19, 244]
	mov	x3, x27
	mov	x0, 0
	mov	w5, 65535
.L820:
	cmp	w4, w0, uxth
	bhi	.L822
	cbnz	w24, .L823
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2693
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L823:
	ldrh	w0, [x19, 184]
	cmp	w0, w25
	bne	.L824
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2695
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L824:
	ldrb	w0, [x20, 8]
	ldr	x1, [x19, 2608]
	cbnz	w0, .L825
	ldrh	w0, [x1, x26]
	cbz	w0, .L826
	ldrh	w2, [x19, 304]
	add	w0, w0, w2
.L847:
	strh	w0, [x1, x26]
	mov	w1, 0
	ldr	w0, [x19, 2676]
	add	w0, w0, 1
	str	w0, [x19, 2676]
	mov	w0, w25
	bl	ftl_set_blk_mode
.L828:
	ldr	x0, [x19, 2608]
	ldr	w1, [x19, 2692]
	ldrh	w0, [x0, x26]
	cmp	w0, w1
	bls	.L829
	str	w0, [x19, 2692]
.L829:
	ldr	w1, [x19, 2680]
	ubfiz	x3, x24, 5, 16
	ldrh	w0, [x19, 304]
	ldr	w2, [x19, 2676]
	madd	w0, w0, w2, w1
	ldrh	w1, [x19, 252]
	udiv	w0, w0, w1
	ldr	x1, [x19, 3560]
	str	w0, [x19, 2684]
	ldr	w0, [x1, 16]
	add	w0, w0, 1
	str	w0, [x1, 16]
	mov	x0, 0
.L830:
	cmp	x3, x0
	bne	.L831
	ldrb	w1, [x20, 8]
	mov	w2, w24
	ldr	x0, [x19, 3416]
	mov	x28, 0
	bl	FlashEraseBlocks
	mov	w1, 0
.L832:
	cmp	w24, w28, uxth
	bhi	.L834
	cmp	w1, 0
	ble	.L835
	mov	w0, w25
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L835:
	ldrb	w0, [x20, 7]
	cbnz	w0, .L836
	ldr	x0, [x19, 80]
	mov	w1, -1
	strh	w1, [x0, x26]
	b	.L812
.L822:
	ldr	x1, [x19, 3416]
	add	x2, x1, x0, lsl 5
	stp	xzr, xzr, [x2, 8]
	ldrh	w2, [x3]
	cmp	w2, w5
	beq	.L821
	ubfiz	x6, x24, 5, 16
	add	w24, w24, 1
	add	x1, x1, x6
	and	w24, w24, 65535
	lsl	w2, w2, 10
	str	w2, [x1, 4]
.L821:
	add	x0, x0, 1
	add	x3, x3, 2
	b	.L820
.L826:
	mov	w0, 2
	b	.L847
.L825:
	ldrh	w0, [x1, x26]
	add	w0, w0, 1
	strh	w0, [x1, x26]
	ldr	w0, [x19, 2680]
	add	w0, w0, 1
	str	w0, [x19, 2680]
	mov	w0, w25
	bl	ftl_set_blk_mode.part.7
	b	.L828
.L831:
	ldr	x1, [x19, 3416]
	add	x1, x1, x0
	add	x0, x0, 32
	ldr	w2, [x1, 4]
	and	w2, w2, -1024
	str	w2, [x1, 4]
	b	.L830
.L834:
	ldr	x2, [x19, 3416]
	lsl	x0, x28, 5
	add	x3, x2, x0
	ldr	w2, [x2, x0]
	cmn	w2, #1
	bne	.L833
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
.L833:
	add	x28, x28, 1
	add	x27, x27, 2
	b	.L832
.L836:
	add	x21, x21, :lo12:.LANCHOR0
	strh	wzr, [x20, 2]
	strb	wzr, [x20, 6]
	ldrh	w1, [x21, 314]
	strh	w25, [x20]
	mul	w0, w0, w1
	ldr	w1, [x21, 2668]
	str	w1, [x20, 12]
	and	w0, w0, 65535
	add	w1, w1, 1
	strh	w0, [x20, 4]
	str	w1, [x21, 2668]
	ldr	x1, [x21, 80]
	ldrh	w2, [x20]
	strh	w0, [x1, x2, lsl 1]
	ldrh	w0, [x20, 4]
	cbz	w0, .L837
	ldrb	w0, [x20, 7]
	cbnz	w0, .L838
.L837:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2748
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L838:
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
	ldrh	w0, [x19, 3320]
	str	x27, [sp, 80]
	cbz	w0, .L849
	add	x23, x19, 272
	add	x24, x19, 3328
	mov	w21, 0
.L850:
	ldrh	w0, [x19, 244]
	cmp	w0, w21
	bhi	.L856
	bl	FtlGcReFreshBadBlk
.L849:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L856:
	ldrb	w0, [x23, w21, sxtw]
	mov	w1, w22
	adrp	x26, .LC102
	add	x26, x26, :lo12:.LC102
	mov	w20, 0
	bl	V2P_block
	and	w25, w0, 65535
.L851:
	ldrh	w0, [x19, 3320]
	cmp	w0, w20
	bhi	.L855
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L850
.L855:
	ldrh	w0, [x24, w20, sxtw 1]
	add	w27, w20, 1
	cmp	w0, w25
	bne	.L852
	mov	w1, w25
	mov	x0, x26
	bl	sftl_printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w0, [x19, 3320]
	mov	w1, w27
.L853:
	cmp	w20, w0
	bcc	.L854
	sub	w0, w0, #1
	strh	w0, [x19, 3320]
.L852:
	and	w20, w27, 65535
	b	.L851
.L854:
	sub	w2, w1, #1
	ldrh	w3, [x24, w1, sxtw 1]
	add	w20, w20, 1
	add	w1, w1, 1
	and	w20, w20, 65535
	strh	w3, [x24, w2, sxtw 1]
	b	.L853
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 65535
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	ubfiz	x1, x20, 1, 16
	ldr	x2, [x0, 80]
	ldrh	w1, [x2, x1]
	cbnz	w1, .L862
	ldrh	w1, [x0, 234]
	cmp	w1, w20
	bne	.L863
	mov	w1, -1
	strh	w1, [x0, 234]
.L863:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 232]
	cmp	w1, w20
	bne	.L864
	mov	w1, -1
	strh	w1, [x0, 232]
.L864:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 184]
	cmp	w1, w20
	bne	.L865
	mov	w1, -1
	strh	w1, [x0, 184]
.L866:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w1, w20
	add	x0, x21, 2576
	bl	List_remove_node
	ldrh	w0, [x21, 2592]
	cbnz	w0, .L868
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2824
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L868:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2592]
	sub	w0, w0, #1
	strh	w0, [x19, 2592]
	mov	w0, w20
	bl	free_data_superblock
	mov	w0, w20
	bl	FtlGcFreeBadSuperBlk
	ldrh	w1, [x19, 2592]
	ldrh	w0, [x19, 236]
	add	w0, w0, w1
	ldrh	w1, [x19, 252]
	cmp	w0, w1
	ble	.L872
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2827
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L872:
	mov	w0, 1
	b	.L861
.L865:
	ldrh	w1, [x0, 32]
	cmp	w1, w20
	beq	.L871
	ldrh	w1, [x0, 88]
	cmp	w1, w20
	beq	.L871
	ldrh	w0, [x0, 136]
	cmp	w0, w20
	bne	.L866
.L871:
	mov	w0, 0
.L861:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L862:
	mov	w0, w20
	bl	List_update_data_list
	b	.L871
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
	beq	.L875
	add	x22, x19, :lo12:.LANCHOR0
	ubfiz	x21, x20, 1, 16
	ldr	x1, [x22, 80]
	ldrh	w0, [x1, x21]
	cbnz	w0, .L876
	mov	w2, 0
	mov	w1, w20
	adrp	x0, .LC103
	add	x0, x0, :lo12:.LC103
	bl	sftl_printk
	ldr	x0, [x22, 80]
	ldrh	w0, [x0, x21]
	cbz	w0, .L877
.L882:
	mov	w21, 0
.L874:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L877:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2842
	add	x1, x1, 384
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L882
.L876:
	sub	w0, w0, #1
	strh	w0, [x1, x21]
.L875:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldrh	w0, [x19, 3714]
	cmp	w0, w1
	bne	.L879
	strh	w20, [x19, 3714]
	b	.L882
.L879:
	cmp	w20, w0
	beq	.L882
	bl	update_vpc_list
	cmp	w0, 0
	ldr	x1, [x19, 2568]
	cset	w21, ne
	ldr	x0, [x19, 2576]
	strh	w20, [x19, 3714]
	sub	x0, x0, x1
	mov	x1, -6148914691236517206
	asr	x0, x0, 1
	movk	x1, 0xaaab, lsl 0
	mul	x0, x0, x1
	ldr	x1, [x19, 80]
	and	x2, x0, 65535
	ldrh	w1, [x1, x2, lsl 1]
	cbnz	w1, .L874
	cmp	w20, w0, uxth
	beq	.L874
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2858
	add	x1, x1, 384
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L874
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
.L889:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L885
.L886:
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19]
	ldrh	w3, [x19, 4]
	ldr	x2, [x21, 80]
	lsl	x0, x0, 1
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strb	wzr, [x19, 6]
	ldrh	w0, [x21, 314]
	strh	w0, [x19, 2]
	strh	wzr, [x19, 4]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 96
	ret
.L885:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29, 68]
	cmn	w0, #1
	beq	.L886
	ldr	w0, [x20, 2672]
	mov	w3, 0
	str	w0, [x23, 4]
	mov	w2, 0
	add	w0, w0, 1
	mov	w1, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x20, 2672]
	add	x0, x29, 64
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	subs	w22, w22, #1
	bne	.L889
	b	.L886
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
	add	x26, x0, 400
	add	x25, x0, 380
	mov	x19, x0
	add	x20, x0, 3728
.L898:
	ldrh	w0, [x19, 266]
	cmp	w24, w0
	bcc	.L904
	add	x20, x21, :lo12:.LANCHOR0
	mov	w19, 0
.L905:
	ldrh	w0, [x20, 326]
	cmp	w0, w19
	bhi	.L906
	add	x20, x20, 368
	ldrh	w19, [x20, 12]
	mov	w22, 65535
	sub	w19, w19, #1
	and	w19, w19, 65535
.L907:
	ldrh	w0, [x20, 12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L911
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L908
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L909
	mov	w0, w19
	bl	FtlBbmMapBadBlock
.L908:
	sub	w19, w19, #1
	and	w19, w19, 65535
	b	.L907
.L904:
	ldr	x0, [x19, 3464]
	mov	w2, 65535
	ldr	x27, [x19, 3528]
	ldrh	w1, [x25]
	stp	x0, x27, [x20, 8]
	cmp	w1, w2
	beq	.L899
	ldrh	w23, [x19, 310]
	mov	w2, 1
	madd	w23, w23, w24, w1
	mov	w1, w2
	lsl	w0, w23, 10
	str	w0, [x20, 4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	x1, [x20, 8]
	ldr	x0, [x26]
	ldrh	w2, [x19, 310]
	add	w2, w2, 7
	lsr	w2, w2, 3
	bl	ftl_memcpy
.L900:
	mov	w0, w23
	add	w24, w24, 1
	bl	FtlBbmMapBadBlock
	add	x26, x26, 8
	add	x25, x25, 2
	b	.L898
.L899:
	mov	w1, w24
	bl	FlashGetBadBlockList
	ldr	x0, [x20, 8]
	ldr	x1, [x26]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x19, 310]
.L902:
	sub	w22, w22, #1
	and	w22, w22, 65535
.L901:
	ldrh	w0, [x19, 310]
	madd	w0, w24, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L902
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
	ldrh	w23, [x19, 310]
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
	bne	.L900
	mov	w0, w23
	bl	FtlBbmMapBadBlock
	b	.L901
.L906:
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	and	w19, w19, 65535
	b	.L905
.L909:
	ldrh	w0, [x20]
	cmp	w0, w22
	bne	.L910
	strh	w19, [x20]
	b	.L908
.L910:
	strh	w19, [x20, 4]
.L911:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, 2
	ldr	x1, [x21, 3416]
	ldrh	w0, [x21, 368]
	str	wzr, [x21, 376]
	strh	wzr, [x21, 370]
	lsl	w0, w0, 10
	str	w0, [x1, 4]
	ldr	x0, [x21, 3416]
	ldrh	w1, [x21, 372]
	lsl	w1, w1, 10
	str	w1, [x0, 36]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x21, 368]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x21, 372]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x21, 370]
	ldr	w0, [x21, 376]
	ldrh	w1, [x21, 372]
	add	w0, w0, 1
	str	w0, [x21, 376]
	ldrh	w0, [x21, 368]
	strh	w0, [x21, 372]
	strh	w1, [x21, 368]
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
.L920:
	cmp	w1, w3
	bhi	.L921
	ret
.L921:
	lsr	w2, w0, 2
	ldrb	w5, [x4, x3]
	add	w2, w2, w0, lsl 5
	add	x3, x3, 1
	add	w2, w2, w5
	eor	w0, w0, w2
	b	.L920
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
	bne	.L923
	ldrh	w0, [x19, 8]
	cbz	w0, .L924
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 641
	add	x1, x1, 408
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L924:
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
.L925:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L923:
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
	ldrh	w2, [x20, 316]
	ldr	x0, [x20, 3464]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w1, 0
	mov	w0, 0
.L926:
	ldrh	w2, [x19, 6]
	cmp	w2, w0
	bhi	.L928
	add	x21, x21, :lo12:.LANCHOR0
	add	x6, x21, 3728
	ldrh	w1, [x21, 322]
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
	b	.L925
.L928:
	ubfiz	x2, x0, 2, 16
	ldr	w3, [x24, x2]
	cmp	w23, w3, lsr 10
	bne	.L927
	add	w1, w1, 1
	ldr	x4, [x20, 3464]
	and	w1, w1, 65535
	ubfiz	x3, x1, 3, 16
	str	w0, [x4, x3]
	ldr	w4, [x24, x2]
	ldr	x2, [x20, 3464]
	add	x2, x2, x3
	str	w4, [x2, 4]
.L927:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L926
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
.L934:
	add	x0, x24, :lo12:.LANCHOR0
	ldr	w1, [x0, 2652]
	add	w1, w1, 1
	str	w1, [x0, 2652]
	ldrh	w0, [x0, 316]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L935
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L936
.L935:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L936:
	ldrh	w1, [x19]
	ldr	x0, [x19, 16]
	ldrh	w0, [x0, x1, lsl 1]
	cbnz	w0, .L937
	adrp	x0, .LC8
	mov	w2, 700
	add	x1, x23, 440
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L937:
	ldrh	w1, [x19]
	ldrh	w0, [x19, 10]
	cmp	w1, w0
	bcc	.L938
	adrp	x0, .LC8
	mov	w2, 701
	add	x1, x23, 440
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L938:
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
	ldrh	w1, [x22, 322]
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
	bne	.L939
	ldr	w1, [x20, 4]
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	add	w21, w21, 1
	and	w21, w21, 65535
	bl	sftl_printk
	ldrh	w0, [x19, 2]
	cmp	w0, 2
	bhi	.L940
	ldrh	w0, [x22, 316]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L940:
	cmp	w21, 3
	bls	.L941
	add	x25, x25, :lo12:.LANCHOR0
	adrp	x0, .LC105
	mov	w2, w21
	add	x0, x0, :lo12:.LC105
	ldr	w1, [x25, 3732]
	bl	sftl_printk
.L942:
	b	.L942
.L941:
	ldr	w0, [x19, 52]
	cbz	w0, .L934
.L956:
	b	.L956
.L939:
	cmp	w0, 1
	beq	.L945
	cmp	w1, 256
	beq	.L945
	ldr	w0, [x19, 56]
	cbz	w0, .L946
.L945:
	str	wzr, [x19, 56]
	b	.L934
.L946:
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
	ldrh	w0, [x0, 348]
	cmp	w0, w22
	bcs	.L959
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 485
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L959:
	add	x20, x21, :lo12:.LANCHOR0
	ubfiz	x0, x22, 2, 16
	ldr	x1, [x20, 3616]
	ldr	w24, [x1, x0]
	cbnz	w24, .L960
	ldr	x0, [x20, 2616]
	lsl	x19, x19, 4
	ldrh	w2, [x20, 322]
	mov	w1, 255
	add	x0, x0, x19
	ldr	x0, [x0, 8]
	bl	ftl_memset
	ldr	x0, [x20, 2616]
	strh	w22, [x0, x19]
	ldr	x0, [x20, 2616]
	add	x19, x0, x19
	str	wzr, [x19, 4]
.L961:
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L960:
	ldr	x0, [x20, 2616]
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
	beq	.L962
	mov	w2, w24
	mov	w1, w22
	adrp	x0, .LC106
	add	x0, x0, :lo12:.LC106
	bl	sftl_printk
	ldr	x1, [x20, 3744]
	mov	w3, 4
	adrp	x0, .LC94
	mov	w2, w3
	add	x0, x0, :lo12:.LC94
	bl	rknand_print_hex
	ldrh	w3, [x20, 348]
	adrp	x0, .LC107
	ldr	x1, [x20, 3616]
	mov	w2, 4
	add	x0, x0, :lo12:.LC107
	bl	rknand_print_hex
.L963:
	ldrh	w0, [x25, 8]
	cmp	w0, w22
	beq	.L964
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 508
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L964:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21, 2616]
	add	x0, x0, x19
	str	wzr, [x0, 4]
	ldr	x0, [x21, 2616]
	strh	w22, [x0, x19]
	b	.L961
.L962:
	ldr	w0, [x20, 3728]
	cmp	w0, 256
	bne	.L963
	mov	w2, w24
	mov	w1, w22
	adrp	x0, .LC108
	add	x0, x0, :lo12:.LC108
	bl	sftl_printk
	ldr	x0, [x20, 2616]
	mov	w1, w22
	add	x0, x0, x19
	ldr	x2, [x0, 8]
	add	x0, x20, 3648
	bl	FtlMapWritePage
	b	.L963
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
	blt	.L967
	ubfiz	x0, x0, 1, 16
	ldrh	w26, [x20, x0]
	cbz	w26, .L967
	ldr	w1, [x19, 52]
	cbnz	w1, .L967
	mov	w1, 1
	str	w1, [x19, 52]
	strh	wzr, [x20, x0]
	ldrh	w0, [x19, 8]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0, 316]
	cmp	w1, w0
	bcc	.L968
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L968:
	add	x27, x22, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR1
	add	x21, x27, 3728
	add	x23, x23, 472
	mov	w20, 0
.L969:
	ldrh	w0, [x19, 6]
	cmp	w0, w20
	bhi	.L976
	mov	w1, 1
	mov	w0, w26
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19, 52]
.L967:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x22, 316]
	cmp	w1, w0
	bcc	.L977
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L977:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L976:
	ubfiz	x0, x20, 2, 16
	add	x28, x25, x0
	ldr	w1, [x25, x0]
	cmp	w26, w1, lsr 10
	bne	.L970
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
	beq	.L971
	adrp	x0, .LC8
	mov	w2, 611
	mov	x1, x23
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L971:
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L972
.L974:
	str	wzr, [x28]
.L973:
	b	.L973
.L972:
	ldrh	w0, [x24, 8]
	cmp	w0, w20
	bne	.L974
	ldrh	w1, [x24]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L974
	ldr	x2, [x21, 8]
	mov	w1, w20
	mov	x0, x19
	bl	FtlMapWritePage
.L970:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L969
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
	ldr	x0, [x19, 2616]
	add	x1, x0, x20
	ldr	x2, [x1, 8]
	ldrh	w1, [x0, x20]
	add	x0, x19, 3648
	bl	FtlMapWritePage
	ldr	x0, [x19, 2616]
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
	ldrh	w24, [x0, 320]
	ldr	w0, [x0, 2632]
	cmp	w25, w0
	bcc	.L986
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 813
	add	x1, x1, 488
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L986:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0, 2632]
	cmp	w25, w1
	bcs	.L987
	add	w24, w24, 7
	ldrh	w1, [x0, 350]
	ldr	x2, [x0, 2616]
	mov	x0, 0
	lsr	w21, w25, w24
	and	w21, w21, 65535
.L988:
	and	x19, x0, 65535
	cmp	w19, w1
	bcc	.L993
	bl	select_l2p_ram_region
	and	x19, x0, 65535
	add	x2, x20, :lo12:.LANCHOR0
	ubfiz	x1, x19, 4, 16
	mov	w26, w0
	ldr	x2, [x2, 2616]
	add	x3, x2, x1
	ldrh	w2, [x2, x1]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L994
	ldr	w1, [x3, 4]
	tbz	w1, #31, .L994
	bl	flush_l2p_region
.L994:
	mov	w1, w26
	mov	w0, w21
	bl	load_l2p_region
	b	.L990
.L987:
	mov	w0, -1
	cbnz	w23, .L985
	str	w0, [x22]
.L985:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L993:
	add	x0, x0, 1
	add	x3, x2, x0, lsl 4
	ldrh	w3, [x3, -16]
	cmp	w3, w21
	bne	.L988
.L990:
	mov	x0, 1
	lsl	x0, x0, x24
	sub	w0, w0, #1
	and	w0, w0, w25
	and	x0, x0, 65535
	cbnz	w23, .L991
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x1, [x1, 2616]
	add	x1, x1, x19, lsl 4
	ldr	x1, [x1, 8]
	ldr	w0, [x1, x0, lsl 2]
	str	w0, [x22]
.L992:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20, 2616]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19, 4]
	cmn	w0, #1
	beq	.L996
	add	w0, w0, 1
	str	w0, [x19, 4]
.L996:
	mov	w0, 0
	b	.L985
.L991:
	add	x2, x20, :lo12:.LANCHOR0
	lsl	x1, x19, 4
	ldr	w4, [x22]
	ldr	x3, [x2, 2616]
	add	x3, x3, x1
	ldr	x3, [x3, 8]
	str	w4, [x3, x0, lsl 2]
	strh	w21, [x2, 2624]
	ldr	x0, [x2, 2616]
	add	x0, x0, x1
	ldr	w1, [x0, 4]
	orr	w1, w1, -2147483648
	str	w1, [x0, 4]
	b	.L992
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
	ldrh	w2, [x20, 36]
	cbz	w2, .L1000
	ldrb	w1, [x20, 40]
	cbnz	w1, .L1000
	ldrb	w1, [x20, 39]
	ldrh	w3, [x20, 314]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L1000
	ldrb	w0, [x20, 42]
	cbnz	w0, .L999
	ldr	w22, [x20, 2632]
	add	x1, x29, 92
	ldrh	w24, [x20, 244]
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
	beq	.L1002
	mov	w2, 0
	mov	w1, 1
	add	x0, x29, 96
	bl	FlashReadPages
.L1003:
	add	x25, x19, :lo12:.LANCHOR0
	lsl	w24, w24, 2
	add	x20, x25, 32
	mov	w0, -3947
	mov	w23, 0
	strh	w0, [x21]
.L1004:
	cmp	w24, w23
	bne	.L1008
.L1005:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19, 42]
.L999:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 128
	ret
.L1002:
	ldrh	w2, [x20, 322]
	mov	w1, 255
	ldr	x0, [x20, 3464]
	bl	ftl_memset
	b	.L1003
.L1008:
	ldrh	w0, [x20, 4]
	cbz	w0, .L1005
	ldr	w0, [x29, 100]
	add	w23, w23, 1
	stp	w22, w0, [x21, 8]
	ldrh	w0, [x20]
	strh	w0, [x21, 2]
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29, 100]
	ldr	w0, [x25, 2672]
	mov	w3, 0
	str	w0, [x21, 4]
	mov	w2, 0
	add	w0, w0, 1
	mov	w1, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x25, 2672]
	add	x0, x29, 96
	bl	FlashProgPages
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	b	.L1004
.L1000:
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x19, 42]
	b	.L999
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
	ldr	x3, [x2, 80]
	ldrh	w1, [x3, x20]
	cbnz	w1, .L1018
	ldr	x19, [x2, 2600]
	cbz	x19, .L1019
	ldrh	w3, [x2, 236]
	mov	x4, -6148914691236517206
	ldr	x2, [x2, 2568]
	movk	x4, 0xaaab, lsl 0
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	mul	x19, x19, x4
	mov	w4, 6
	and	w19, w19, 65535
.L1020:
	cmp	w1, w3
	beq	.L1019
	cmp	w19, w0
	bne	.L1021
	add	x23, x22, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x23, 2600
	bl	List_remove_node
	ldrh	w0, [x23, 236]
	cbnz	w0, .L1022
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1733
	add	x1, x1, 504
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1022:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w0, [x22, 236]
	sub	w0, w0, #1
	strh	w0, [x22, 236]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x1, [x22, 80]
	ldrh	w0, [x1, x20]
	add	w0, w0, 1
	strh	w0, [x1, x20]
.L1019:
	add	x1, x29, 76
	mov	w2, 1
	mov	w0, w21
	bl	log2phys
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L1021:
	umull	x19, w19, w4
	ldrh	w19, [x2, x19]
	cmp	w19, w5
	beq	.L1019
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L1020
.L1018:
	add	w1, w1, 1
	strh	w1, [x3, x20]
	b	.L1019
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
	beq	.L1149
	ldrb	w0, [x20, 6]
	adrp	x19, .LANCHOR0
	str	w0, [x29, 140]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w26, [x20, 2]
	ldrh	w0, [x0, 314]
	cmp	w0, w26
	bne	.L1031
	strh	wzr, [x20, 4]
.L1156:
	strb	wzr, [x20, 6]
.L1149:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1031:
	ldrh	w0, [x20, 16]
	mov	w21, 0
.L1032:
	cmp	w0, w1
	beq	.L1033
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	mov	w23, w0
	cmn	w0, #1
	beq	.L1034
	add	x0, x19, :lo12:.LANCHOR0
	add	x28, x20, 16
	mov	x3, x28
	mov	w22, 0
	mov	w10, 65535
	mov	w6, 4
	ldrh	w1, [x0, 244]
	ldrh	w8, [x0, 322]
	add	x1, x1, 8
	ldrh	w9, [x0, 324]
	add	x1, x20, x1, lsl 1
.L1035:
	cmp	x1, x3
	bne	.L1039
	add	x24, x19, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w1, w22
	ldr	x0, [x24, 3440]
	bl	FlashReadPages
	ldr	w21, [x24, 2672]
	ldr	x4, [x24, 3440]
	and	w9, w23, 65535
	sub	w21, w21, #1
	mov	w7, 65535
	mov	x6, x4
	mov	w3, 0
.L1040:
	cmp	w22, w3
	bne	.L1045
	add	w22, w23, 1
	ldr	w0, [x4, 4]
	and	w22, w22, 65535
.L1151:
	lsr	w0, w0, 10
	bl	P2V_plane
	and	w27, w0, 65535
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 314]
	cmp	w0, w22
	bne	.L1047
	strh	w22, [x20, 2]
	strb	wzr, [x20, 6]
	strh	wzr, [x20, 4]
.L1047:
	ldr	w0, [x29, 140]
	cmp	w22, w26
	ccmp	w27, w0, 0, eq
	bne	.L1048
.L1157:
	mov	w2, w27
	mov	w1, w22
	mov	x0, x20
	bl	ftl_sb_update_avl_pages
	b	.L1149
.L1033:
	add	w21, w21, 1
	and	w21, w21, 65535
	add	x0, x20, x21, sxtw 1
	ldrh	w0, [x0, 16]
	b	.L1032
.L1034:
	cbz	w26, .L1036
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1800
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1036:
	ldr	w0, [x29, 140]
	cmp	w0, 0
	ccmp	w21, w0, 4, ne
	beq	.L1037
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1801
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1037:
	strh	wzr, [x20, 2]
	b	.L1156
.L1039:
	ldrh	w2, [x3]
	cmp	w2, w10
	beq	.L1038
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
.L1038:
	add	x3, x3, 2
	b	.L1035
.L1045:
	ldr	w0, [x6]
	cbnz	w0, .L1041
	ldr	x8, [x6, 16]
	ldr	w5, [x8, 4]
	cmn	w5, #1
	beq	.L1042
	ldr	w1, [x24, 2672]
	mov	w0, w5
	bl	ftl_cmp_data_ver
	cbz	w0, .L1042
	add	w5, w5, 1
	str	w5, [x24, 2672]
.L1042:
	ldr	w0, [x8]
	cmn	w0, #1
	bne	.L1044
	ubfiz	x3, x3, 5, 16
	and	w22, w23, 65535
	add	x3, x4, x3
	ldr	w0, [x3, 4]
	b	.L1151
.L1041:
	mov	w7, w9
.L1044:
	add	w3, w3, 1
	add	x6, x6, 32
	and	w3, w3, 65535
	b	.L1040
.L1048:
	mov	w0, 65535
	cmp	w7, w0
	bne	.L1049
	ldrb	w0, [x20, 8]
	cbnz	w0, .L1050
.L1049:
	add	x0, x19, :lo12:.LANCHOR0
	and	w3, w23, 65535
	ldr	w1, [x0, 3724]
	cmn	w1, #1
	bne	.L1051
	str	w21, [x0, 3724]
.L1051:
	add	w0, w26, 7
	cmp	w0, w23, uxth
	bge	.L1099
	sub	w24, w3, #7
	and	w24, w24, 65535
.L1052:
	add	x25, x19, :lo12:.LANCHOR0
	mov	w4, 65535
.L1053:
	cmp	w24, w3
	bhi	.L1060
	ldrh	w0, [x25, 244]
	mov	x1, x28
	mov	w23, 0
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
	b	.L1061
.L1099:
	mov	w24, w26
	b	.L1052
.L1055:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1054
	ldr	x5, [x25, 3440]
	ubfiz	x6, x23, 5, 16
	add	w23, w23, 1
	orr	w2, w24, w2, lsl 10
	add	x5, x5, x6
	and	w23, w23, 65535
	str	w2, [x5, 4]
.L1054:
	add	x1, x1, 2
.L1061:
	cmp	x0, x1
	bne	.L1055
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
.L1056:
	cmp	x23, x0
	bne	.L1059
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L1053
.L1059:
	ldr	w1, [x0, -16]
	cbnz	w1, .L1050
	ldr	x1, [x0]
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1058
	ldr	w1, [x1, 4]
	cmn	w1, #1
	beq	.L1058
	str	w1, [x25, 3724]
.L1058:
	add	x0, x0, 32
	b	.L1056
.L1060:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	str	w1, [x0, 3724]
.L1050:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	mov	w24, w26
	strh	w0, [x19, 3760]
	add	x0, x19, 3768
	str	x0, [x29, 104]
.L1062:
	ldrh	w0, [x19, 244]
	mov	x1, x28
	mov	w23, 0
	mov	w4, 65535
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
.L1063:
	cmp	x0, x1
	bne	.L1065
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
.L1066:
	ldr	x0, [x29, 112]
	cmp	x0, x23
	bne	.L1093
	add	w24, w24, 1
	ldrh	w0, [x19, 314]
	and	w24, w24, 65535
	cmp	w0, w24
	bne	.L1062
	ldrh	w1, [x19, 244]
	mov	w0, 0
	strh	w24, [x20, 2]
	mov	w2, 65535
	strh	wzr, [x20, 4]
.L1094:
	cmp	w0, w1
	beq	.L1149
	ldrh	w3, [x28], 2
	cmp	w3, w2
	beq	.L1095
	strb	w0, [x20, 6]
	b	.L1149
.L1065:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1064
	ldr	x3, [x19, 3440]
	ubfiz	x5, x23, 5, 16
	add	w23, w23, 1
	orr	w2, w24, w2, lsl 10
	add	x3, x3, x5
	and	w23, w23, 65535
	str	w2, [x3, 4]
.L1064:
	add	x1, x1, 2
	b	.L1063
.L1093:
	ldr	x4, [x19, 3440]
	add	x4, x4, x23
	ldr	w5, [x4, 4]
	str	w5, [x29, 156]
	lsr	w0, w5, 10
	bl	P2V_plane
	and	w0, w0, 65535
	cmp	w24, w26
	bcc	.L1067
	ldr	w1, [x29, 140]
	ccmp	w1, w0, 0, eq
	bhi	.L1067
	cmp	w24, w22
	ccmp	w27, w0, 0, eq
	beq	.L1068
	ldr	w0, [x4]
	cmn	w0, #1
	beq	.L1069
	ldr	x3, [x4, 16]
	mov	w0, 61589
	ldrh	w1, [x3]
	cmp	w1, w0
	beq	.L1070
	ldrh	w0, [x20]
.L1153:
	bl	decrement_vpc_count
	b	.L1067
.L1070:
	ldr	w21, [x3, 4]
	cmn	w21, #1
	beq	.L1071
	ldr	w1, [x19, 2672]
	mov	w0, w21
	str	x3, [x29, 128]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29, 128]
	cbz	w0, .L1071
	add	w0, w21, 1
	str	w0, [x19, 2672]
.L1071:
	ldrh	w1, [x3]
	mov	w0, 61589
	cmp	w1, w0
	beq	.L1072
	ldr	x1, [x29, 120]
	mov	w2, 1952
	str	x3, [x29, 128]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	ldr	x3, [x29, 128]
.L1072:
	ldp	w25, w0, [x3, 8]
	add	x1, x29, 152
	str	w0, [x29, 148]
	mov	w2, 0
	mov	w0, w25
	bl	log2phys
	ldr	w1, [x19, 3724]
	ldr	w3, [x29, 148]
	cmn	w1, #1
	beq	.L1073
	mov	w0, w21
	bl	ftl_cmp_data_ver
	cbz	w0, .L1073
	cmn	w3, #1
	beq	.L1074
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
	bne	.L1075
.L1076:
	mov	w0, -1
	str	w0, [x29, 148]
.L1083:
	ldr	w4, [x29, 148]
	cmn	w4, #1
	beq	.L1067
.L1098:
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	and	w25, w0, 65535
	ldrh	w0, [x19, 252]
	mov	w3, w25
	cmp	w0, w25
	bhi	.L1089
	ldr	x1, [x29, 120]
	mov	w2, 2057
	adrp	x0, .LC8
	str	w25, [x29, 128]
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	ldr	w3, [x29, 128]
.L1089:
	ldr	x1, [x19, 80]
	ubfiz	x0, x25, 1, 16
	ldrh	w0, [x1, x0]
	cbz	w0, .L1090
	mov	w0, w3
	b	.L1153
.L1074:
	ldp	w1, w0, [x29, 152]
	cmp	w1, w0
	bne	.L1067
	mov	w2, 1
	add	x1, x29, 148
	mov	w0, w25
	bl	log2phys
.L1067:
	add	x23, x23, 32
	b	.L1066
.L1075:
	ldr	w0, [x4, 8]
	cmp	w25, w0
	bne	.L1076
	ldr	w0, [x4, 4]
	str	w0, [x29, 128]
	str	x4, [x29, 96]
	uxtw	x1, w0
	ldr	w0, [x19, 3724]
	bl	ftl_cmp_data_ver
	cbz	w0, .L1076
	ldp	w0, w1, [x29, 152]
	ldr	x4, [x29, 96]
	cmp	w0, w1
	ldr	w1, [x29, 148]
	bne	.L1078
.L1152:
	mov	w0, w25
	bl	FtlReUsePrevPpa
	b	.L1076
.L1078:
	cmp	w0, w1
	beq	.L1076
	cmn	w0, #1
	beq	.L1079
	ldr	x4, [x3, 16]
	mov	w2, 0
	str	w0, [x3, 4]
	mov	w1, 1
	str	x4, [x29, 96]
	ldr	x0, [x19, 3440]
	add	x0, x0, x23
	bl	FlashReadPages
	ldr	x4, [x29, 96]
.L1080:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3440]
	ldr	w1, [x1, x23]
	cmn	w1, #1
	beq	.L1081
	ldr	w3, [x4, 4]
	ldr	w0, [x0, 3724]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1081
	ldr	w0, [x29, 128]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1076
.L1081:
	ldr	w1, [x29, 148]
	b	.L1152
.L1079:
	str	w0, [x3]
	b	.L1080
.L1073:
	ldp	w1, w0, [x29, 152]
	cmp	w1, w0
	beq	.L1083
	cmn	w3, #1
	beq	.L1085
	ldr	w0, [x19, 260]
	cmp	w0, w3, lsr 10
	bhi	.L1085
	adrp	x0, .LC109
	mov	w1, w3
	add	x0, x0, :lo12:.LC109
.L1155:
	bl	sftl_printk
	b	.L1067
.L1085:
	mov	w2, 1
	add	x1, x29, 156
	mov	w0, w25
	bl	log2phys
	ldr	w4, [x29, 152]
	cmn	w4, #1
	beq	.L1083
	ldr	w0, [x29, 148]
	cmp	w4, w0
	beq	.L1098
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19, 32]
	and	w0, w0, 65535
	cmp	w1, w0
	beq	.L1088
	ldrh	w1, [x19, 88]
	cmp	w1, w0
	beq	.L1088
	ldrh	w1, [x19, 136]
	cmp	w1, w0
	bne	.L1083
.L1088:
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
	beq	.L1083
	ldr	x3, [x29, 128]
	mov	w0, w21
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
	cbnz	w0, .L1083
	mov	w2, 1
	add	x1, x29, 152
	mov	w0, w25
	bl	log2phys
	b	.L1083
.L1090:
	adrp	x0, .LC110
	mov	w1, w25
	add	x0, x0, :lo12:.LC110
	b	.L1155
.L1069:
	ldr	w0, [x19, 3764]
	cmp	w0, 31
	bhi	.L1091
	ldr	x1, [x29, 104]
	str	w5, [x1, w0, uxtw 2]
	add	w0, w0, 1
	str	w0, [x19, 3764]
.L1091:
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	ldr	w0, [x19, 3724]
	cmn	w0, #1
	bne	.L1092
.L1154:
	str	w21, [x19, 3724]
	b	.L1067
.L1092:
	cmp	w21, w0
	bcs	.L1067
	b	.L1154
.L1095:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1094
.L1068:
	strb	w27, [x20, 6]
	strh	w22, [x20, 2]
	b	.L1157
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	ftl_check_vpc
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	stp	x29, x30, [sp, -128]!
	adrp	x0, .LC111
	add	x0, x0, :lo12:.LC111
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
.L1159:
	ldr	w0, [x24, 2632]
	cmp	w23, w0
	bcc	.L1161
	adrp	x25, .LC112
	add	x23, x20, :lo12:.LANCHOR0
	add	x26, x22, :lo12:check_vpc_table
	add	x25, x25, :lo12:.LC112
	mov	w24, 0
	mov	w19, 0
	mov	w4, 65535
.L1162:
	ldrh	w0, [x23, 252]
	cmp	w0, w19
	bhi	.L1164
	ldr	x19, [x23, 2600]
	cbz	x19, .L1165
	ldr	x0, [x23, 2568]
	adrp	x25, .LC113
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:check_vpc_table
	sub	x19, x19, x0
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	movk	x0, 0xaaab, lsl 0
	add	x25, x25, :lo12:.LC113
	ldrh	w26, [x23, 236]
	mov	w27, 6
	mov	w23, 0
	mul	x19, x19, x0
	and	w19, w19, 65535
.L1166:
	cmp	w23, w26
	bne	.L1168
.L1165:
	cbz	w24, .L1158
	add	x1, x21, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2383
	add	x1, x1, 544
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1158:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1161:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w23
	bl	log2phys
	ldr	w0, [x29, 124]
	cmn	w0, #1
	beq	.L1160
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	and	x0, x0, 65535
	ldrh	w1, [x19, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x19, x0, lsl 1]
.L1160:
	add	w23, w23, 1
	b	.L1159
.L1164:
	ldr	x0, [x23, 80]
	ubfiz	x28, x19, 1, 16
	sxtw	x27, w19
	ldrh	w2, [x0, x28]
	ldrh	w3, [x26, x27, lsl 1]
	cmp	w2, w3
	beq	.L1163
	mov	x0, x25
	str	w4, [x29, 108]
	mov	w1, w19
	bl	sftl_printk
	ldr	x0, [x23, 80]
	ldr	w4, [x29, 108]
	ldrh	w0, [x0, x28]
	cmp	w0, w4
	beq	.L1163
	ldrh	w1, [x26, x27, lsl 1]
	cmp	w1, w0
	csinc	w24, w24, wzr, ls
.L1163:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1162
.L1168:
	ldr	x1, [x20, 80]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L1167
	ldrh	w3, [x22, w19, sxtw 1]
	mov	w24, 1
	mov	w1, w19
	mov	x0, x25
	bl	sftl_printk
.L1167:
	ldr	x0, [x20, 2568]
	umull	x19, w19, w27
	ldrh	w19, [x0, x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L1165
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L1166
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	sub	sp, sp, #96
	adrp	x0, .LC114
	mov	w1, 0
	add	x0, x0, :lo12:.LC114
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
.L1184:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0, 2632]
	cmp	w20, w0
	bcc	.L1190
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x29, x30, [sp, 16]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	ret
.L1190:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w20
	bl	log2phys
	tst	x20, 2047
	bne	.L1185
	ldr	w2, [x29, 76]
	adrp	x0, .LC115
	mov	w1, w20
	add	x0, x0, :lo12:.LC115
	bl	sftl_printk
.L1185:
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1187
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
	beq	.L1188
	ldr	w0, [x23, 8]
	cmp	w20, w0
	beq	.L1187
.L1188:
	ldp	x1, x0, [x19, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC116
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC116
	ldr	w2, [x19, 4]
	mov	w1, w20
	bl	sftl_printk
.L1187:
	add	w20, w20, 1
	b	.L1184
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x0, x22, :lo12:.LANCHOR2
	stp	x25, x26, [sp, 64]
	stp	x23, x24, [sp, 48]
	mov	w25, w1
	stp	x27, x28, [sp, 80]
	ldrh	w28, [x0, 4]
	mov	w0, 65535
	cmp	w28, w0
	beq	.L1216
	cbnz	w28, .L1193
.L1194:
	bl	FtlGcPageVarInit
	b	.L1195
.L1216:
	mov	w28, 0
.L1193:
	adrp	x0, .LANCHOR0+314
	ldrh	w0, [x0, #:lo12:.LANCHOR0+314]
	cmp	w0, w25
	beq	.L1194
.L1195:
	adrp	x26, .LANCHOR0
	add	x19, x26, :lo12:.LANCHOR0
	mov	w24, 0
.L1210:
	ldrh	w1, [x20]
	mov	w0, 65535
	strb	wzr, [x20, 8]
	cmp	w1, w0
	beq	.L1196
.L1213:
	add	x0, x26, :lo12:.LANCHOR0
	add	x2, x20, 16
	mov	w21, 0
	mov	w10, 65535
	mov	w6, 4
	ldrh	w8, [x0, 322]
	ldrh	w9, [x0, 324]
	ldrh	w0, [x0, 244]
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
.L1197:
	cmp	x0, x2
	bne	.L1199
	ldr	x0, [x19, 3440]
	mov	w2, 0
	mov	w1, w21
	mov	x23, 0
	bl	FlashReadPages
	ubfiz	x0, x21, 5, 16
	str	x0, [x29, 104]
.L1200:
	ldr	x0, [x29, 104]
	cmp	x0, x23
	bne	.L1211
	add	w3, w28, 1
	add	w24, w24, 1
	and	w28, w3, 65535
	cmp	w25, w24
	bls	.L1212
.L1214:
	ldrh	w0, [x19, 314]
	cmp	w0, w28
	bhi	.L1213
.L1196:
	add	x22, x22, :lo12:.LANCHOR2
	strh	w28, [x20, 2]
	strb	wzr, [x20, 6]
	mov	w0, -1
	mov	w2, 0
	mov	w1, w28
	strh	w0, [x22, 4]
	mov	x0, x20
	bl	ftl_sb_update_avl_pages
	b	.L1215
.L1199:
	ldrh	w1, [x2]
	cmp	w1, w10
	beq	.L1198
	ldr	x5, [x19, 3440]
	ubfiz	x7, x21, 5, 16
	orr	w1, w28, w1, lsl 10
	add	x5, x5, x7
	str	w1, [x5, 4]
	mul	w1, w21, w8
	ldr	x4, [x19, 3440]
	ldr	x5, [x19, 3264]
	sdiv	w1, w1, w6
	add	x4, x4, x7
	add	x1, x5, x1, sxtw 2
	str	x1, [x4, 8]
	mul	w1, w21, w9
	ldr	x5, [x19, 3272]
	add	w21, w21, 1
	and	w21, w21, 65535
	sdiv	w1, w1, w6
	add	x1, x5, x1, sxtw 2
	str	x1, [x4, 16]
.L1198:
	add	x2, x2, 2
	b	.L1197
.L1211:
	ldr	x0, [x19, 3440]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	ldr	w27, [x1, 4]
	ldr	x21, [x1, 16]
	cbnz	w0, .L1201
	ldrh	w0, [x21]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1202
.L1226:
	ldrh	w1, [x20]
	mov	w28, 0
	ldr	x0, [x19, 80]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x20]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x20]
	strh	w0, [x19, 184]
	bl	FtlGcPageVarInit
	b	.L1210
.L1202:
	ldr	w1, [x19, 2632]
	ldr	w0, [x21, 8]
	cmp	w0, w1
	bhi	.L1226
	add	x1, x29, 124
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x21, 12]
	ldr	w1, [x29, 124]
	cmp	w0, w1
	beq	.L1205
.L1207:
	ldr	w2, [x21, 8]
.L1227:
	ldr	w0, [x21, 12]
	mov	w1, w27
	add	x23, x23, 32
	bl	FtlGcUpdatePage
	b	.L1200
.L1205:
	cmn	w0, #1
	beq	.L1207
	str	w0, [x29, 132]
	mov	w2, 0
	ldr	x0, [x19, 3496]
	mov	w1, 1
	str	x0, [x29, 136]
	ldr	x0, [x19, 3536]
	str	x0, [x29, 144]
	add	x0, x29, 128
	bl	FlashReadPages
	ldrh	w1, [x19, 270]
	mov	x0, 0
	ldr	x2, [x19, 3440]
	ldr	x5, [x29, 136]
	ubfiz	x1, x1, 9, 16
	add	x2, x2, x23
.L1208:
	cmp	x0, x1
	beq	.L1207
	ldr	x6, [x2, 8]
	ldr	w7, [x6, x0]
	add	x0, x0, 4
	add	x6, x5, x0
	ldr	w6, [x6, -4]
	cmp	w7, w6
	beq	.L1208
	ldrh	w1, [x20]
	adrp	x0, .LC117
	ldr	w2, [x29, 132]
	add	x0, x0, :lo12:.LC117
	bl	sftl_printk
	b	.L1226
.L1201:
	mov	w2, -1
	b	.L1227
.L1212:
	add	x1, x22, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x1, 4]
	cmp	w0, w2
	beq	.L1214
	add	w0, w0, w24
	strh	w0, [x1, 4]
	ldrh	w0, [x19, 314]
	cmp	w0, w28
	bls	.L1214
.L1215:
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
	cbz	w3, .L1229
	ldr	w1, [x0, 2632]
	ldr	w3, [x2, 84]
	cmp	w3, w1
	bcs	.L1230
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w20, 2048
	str	x21, [sp, 32]
.L1235:
	add	x21, x19, 2720
	ldr	w1, [x19, 2632]
	ldr	w0, [x21, 84]
	cmp	w0, w1
	bcc	.L1231
.L1234:
	ldp	x19, x20, [sp, 16]
	mov	w0, -1
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 160
	ret
.L1231:
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x21, 84]
	ldr	w1, [x29, 60]
	add	w0, w0, 1
	str	w0, [x21, 84]
	cmn	w1, #1
	beq	.L1233
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
	bne	.L1234
	ldr	w0, [x29, 60]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1234
.L1233:
	subs	w20, w20, #1
	bne	.L1235
	b	.L1234
.L1230:
	ldr	w0, [x0, 2636]
	stp	w0, wzr, [x2, 76]
	str	wzr, [x2, 84]
.L1242:
	mov	w0, 0
	ret
.L1229:
	ldr	w4, [x0, 2692]
	mov	w3, 10000
	ldr	w5, [x0, 2636]
	mov	w6, 31
	cmp	w4, w3
	ldr	w7, [x2, 76]
	mov	w3, 63
	csel	w6, w6, w3, hi
	add	w3, w5, 1048576
	cmp	w7, w3
	bhi	.L1239
	ldr	w3, [x0, 2632]
	mov	w8, 1000
	lsr	w4, w4, 10
	add	w4, w4, 1
	mul	w3, w3, w8
	udiv	w3, w3, w4
	add	w3, w3, w7
	cmp	w5, w3
	bhi	.L1239
	ldrh	w0, [x0, 2548]
	tst	w6, w0
	bne	.L1242
	ldr	w2, [x2, 100]
	cmp	w0, w2
	beq	.L1242
.L1239:
	add	x0, x1, :lo12:.LANCHOR0
	add	x1, x0, 2720
	ldrh	w0, [x0, 2548]
	str	w0, [x1, 100]
	str	w5, [x1, 76]
	mov	w0, 1
	str	wzr, [x1, 84]
	str	w0, [x1, 80]
	b	.L1242
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
.L1249:
	ldrh	w0, [x20, 350]
	cmp	w0, w19
	bhi	.L1251
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1251:
	ldr	x1, [x20, 2616]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L1250
	mov	w0, w19
	bl	flush_l2p_region
.L1250:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1249
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
	ldrh	w0, [x19, 308]
	stp	x25, x26, [sp, 64]
	cmp	w1, w0
	bhi	.L1261
	ldrh	w23, [x19, 320]
	mov	x25, x2
	add	x26, x29, 144
	add	x27, x19, 3896
	mov	w24, 0
	lsr	w23, w28, w23
.L1255:
	cbnz	w22, .L1260
.L1253:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1260:
	ldrh	w1, [x19, 270]
	ldr	x0, [x19, 3608]
	udiv	w21, w28, w1
	ldr	w2, [x0, w23, uxtw 2]
	and	w0, w22, 65535
	msub	w21, w21, w1, w28
	sub	w20, w1, w21
	and	w20, w20, 65535
	cmp	w22, w20
	csel	w20, w0, w20, cc
	cbz	w2, .L1257
	cmp	w20, w1
	beq	.L1257
	ldr	x0, [x19, 3480]
	str	w2, [x29, 116]
	mov	w2, 1
	stp	x0, x26, [x29, 120]
	mov	w1, w2
	add	x0, x29, 112
	bl	FlashReadPages
.L1258:
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
	b	.L1255
.L1257:
	ldrh	w2, [x19, 322]
	mov	w1, 0
	ldr	x0, [x19, 3480]
	bl	ftl_memset
	b	.L1258
.L1261:
	mov	w24, -1
	b	.L1253
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
	bne	.L1269
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 2720
	mov	w1, 72
	add	x29, sp, 0
	movk	w1, 0x5000, lsl 16
	str	w1, [x2, 4]
	ldr	w1, [x0, 2660]
	str	w1, [x2, 88]
	ldr	w1, [x0, 2664]
	str	w1, [x2, 92]
	ldr	w1, [x0, 2656]
	str	w1, [x2, 8]
	ldr	w1, [x0, 2644]
	str	w1, [x2, 12]
	ldr	w1, [x0, 2636]
	str	w1, [x2, 16]
	ldr	w1, [x0, 2652]
	str	w1, [x2, 20]
	ldr	w1, [x0, 2680]
	str	w1, [x2, 28]
	ldr	w1, [x0, 2688]
	str	w1, [x2, 32]
	ldr	w1, [x0, 2640]
	str	w1, [x2, 36]
	ldr	w1, [x0, 2648]
	str	w1, [x2, 40]
	ldr	w1, [x0, 2692]
	ldr	w0, [x0, 2696]
	stp	w1, w0, [x2, 44]
	mov	w1, 1
	mov	w0, 0
	bl	FtlVendorPartWrite
	ldp	x29, x30, [sp], 16
	ret
.L1269:
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
	bhi	.L1276
	add	w2, w2, 1
	strh	w2, [x3, 3960]
	mov	w2, 1
.L1273:
	cbnz	w0, .L1274
	add	x0, x1, :lo12:.LANCHOR0
	ldr	x0, [x0, 3560]
	ldr	w3, [x0, 20]
	ldr	w0, [x0, 16]
	add	w2, w2, w3
	cmp	w0, w2
	bcc	.L1278
.L1274:
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
.L1276:
	mov	w2, 32
	b	.L1273
.L1278:
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
	ldrh	w0, [x3, 308]
	cmp	w1, w0
	bhi	.L1291
	ldrh	w21, [x3, 320]
	adrp	x26, .LC118
	mov	x25, x2
	mov	x28, x3
	add	x26, x26, :lo12:.LC118
	mov	w24, 0
	lsr	w21, w23, w21
.L1284:
	cbnz	w22, .L1290
.L1282:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1290:
	ldrh	w19, [x28, 270]
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
	cbz	w4, .L1286
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
	bne	.L1288
	mov	w2, w4
	mov	w1, w21
	mov	x0, x26
	bl	sftl_printk
	ldr	x2, [x28, 3480]
	mov	w1, w21
	add	x0, x28, 3896
	bl	FtlMapWritePage
.L1288:
	ldr	x1, [x28, 3480]
	lsl	w20, w20, 9
	asr	w20, w20, 2
	mov	w2, w27
	mov	x0, x25
	add	x1, x1, x20, sxtw 2
	bl	ftl_memcpy
.L1289:
	add	w21, w21, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x25, x25, x27, sxtw
	b	.L1284
.L1286:
	mov	w2, w27
	mov	w1, 0
	mov	x0, x25
	bl	ftl_memset
	b	.L1289
.L1291:
	mov	w24, -1
	b	.L1282
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
	beq	.L1294
	adrp	x1, .LC119
	adrp	x0, .LC77
	add	x1, x1, :lo12:.LC119
	add	x0, x0, :lo12:.LC77
	bl	sftl_printk
	ldr	x0, [x19, 3560]
	mov	w1, 0
	ldrh	w2, [x19, 3544]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1294:
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
	beq	.L1297
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	str	w21, [x20, 2720]
.L1297:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0, 2720]
	cmp	w3, w2
	bne	.L1298
	ldr	w2, [x0, 2808]
	str	w2, [x0, 2660]
	ldr	w2, [x0, 2812]
	str	w2, [x0, 2664]
	ldr	w2, [x0, 2728]
	str	w2, [x0, 2656]
	ldr	w2, [x0, 2732]
	str	w2, [x0, 2644]
	ldr	w2, [x0, 2736]
	str	w2, [x0, 2636]
	ldr	w2, [x0, 2740]
	str	w2, [x0, 2652]
	ldr	w2, [x0, 2748]
	str	w2, [x0, 2680]
	ldr	w2, [x0, 2752]
	str	w2, [x0, 2688]
	ldr	w2, [x0, 2756]
	str	w2, [x0, 2640]
	ldr	w2, [x0, 2760]
	ldr	w1, [x0, 2768]
	str	w2, [x0, 2648]
	ldr	w2, [x0, 2764]
	str	w1, [x0, 2696]
	str	w2, [x0, 2692]
.L1298:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x0, 2680]
	ldrh	w1, [x0, 304]
	ldr	w3, [x0, 2676]
	madd	w1, w1, w3, w2
	ldrh	w2, [x0, 252]
	udiv	w1, w1, w2
	str	w1, [x0, 2684]
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
	cbz	w1, .L1310
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
	cbz	w4, .L1304
	mov	w2, 1
	mov	w1, w2
	bl	FlashReadPages
.L1305:
	add	x1, x21, :lo12:.LANCHOR0
	mov	x0, x19
	ldr	x2, [x1, 3736]
	mov	w1, w20
	bl	FtlMapWritePage
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1304:
	ldrh	w2, [x2, 322]
	mov	w1, 255
	mov	x0, x3
	bl	ftl_memset
	b	.L1305
.L1310:
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
	add	x20, x19, 2520
	add	x25, x19, 2704
	mov	w22, 0
	ldr	x21, [x19, 3528]
	mov	w24, 65535
	ldr	x0, [x19, 3464]
	str	x0, [x19, 3736]
	ldrh	w0, [x19, 2704]
	str	x21, [x19, 3744]
	strh	w0, [x21, 2]
	mov	w0, -3932
	strh	w0, [x21]
	str	wzr, [x21, 12]
	ldr	w0, [x19, 2712]
	stp	w0, wzr, [x21, 4]
	mov	w0, 19539
	ldrh	w1, [x19, 32]
	movk	w0, 0x4654, lsl 16
	str	w0, [x19, 2520]
	mov	w0, 72
	ldrb	w2, [x19, 38]
	strh	w1, [x19, 2534]
	movk	w0, 0x5000, lsl 16
	ldrh	w1, [x19, 34]
	str	w0, [x19, 2524]
	ldrh	w0, [x19, 2710]
	strh	w0, [x19, 2528]
	orr	w1, w2, w1, lsl 6
	ldrh	w0, [x19, 266]
	strb	w0, [x19, 2530]
	strh	w1, [x19, 2536]
	ldrh	w1, [x19, 88]
	ldrb	w2, [x19, 94]
	strh	w1, [x19, 2538]
	ldrh	w1, [x19, 90]
	ldrb	w0, [x19, 40]
	strb	w0, [x19, 2531]
	orr	w1, w2, w1, lsl 6
	ldrb	w0, [x19, 96]
	strh	w1, [x19, 2540]
	strb	w0, [x19, 2532]
	ldrh	w1, [x19, 136]
	ldrb	w0, [x19, 144]
	ldrb	w2, [x19, 142]
	strh	w1, [x19, 2542]
	strb	w0, [x19, 2533]
	ldrh	w1, [x19, 138]
	ldr	w0, [x19, 2676]
	str	w0, [x19, 2552]
	ldr	w0, [x19, 2668]
	orr	w1, w2, w1, lsl 6
	str	w0, [x19, 2560]
	ldr	w0, [x19, 2672]
	ldrh	w2, [x19, 322]
	str	w0, [x19, 2556]
	ldr	x0, [x19, 3736]
	strh	w1, [x19, 2544]
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
	ldr	x1, [x19, 80]
	ldrh	w2, [x19, 252]
	ldr	x0, [x19, 3736]
	lsl	w2, w2, 1
	add	x0, x0, 48
	bl	ftl_memcpy
	ldrh	w0, [x19, 252]
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
.L1314:
	ldrh	w2, [x20, 2]
	ldrh	w1, [x20]
	ldr	x0, [x19, 3464]
	str	x0, [x19, 3736]
	ldr	x0, [x19, 3528]
	str	x0, [x19, 3744]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19, 3732]
	ldrh	w0, [x19, 316]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L1315
	ldrh	w24, [x20, 4]
	strh	wzr, [x20, 2]
	strh	w1, [x20, 4]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19, 2668]
	str	w1, [x20, 8]
	add	w2, w1, 1
	str	w2, [x19, 2668]
	ubfiz	w2, w0, 10, 16
	str	w2, [x19, 3732]
	strh	w0, [x20]
	strh	w0, [x21, 2]
	str	w1, [x21, 4]
.L1315:
	ldrh	w1, [x19, 322]
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
	bne	.L1316
	cmp	w0, 1
	bne	.L1317
	adrp	x0, .LC8
	mov	w2, 1138
	mov	x1, x25
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1317:
	ldrh	w0, [x20, 2]
	cmp	w0, 1
	bne	.L1318
	ldrh	w0, [x19, 316]
	sub	w0, w0, #1
	strh	w0, [x20, 2]
.L1318:
	add	w22, w22, 1
	and	w22, w22, 65535
	cmp	w22, 3
	bls	.L1314
	add	x23, x23, :lo12:.LANCHOR0
	adrp	x0, .LC120
	mov	w2, w22
	add	x0, x0, :lo12:.LC120
	ldr	w1, [x23, 3732]
	bl	sftl_printk
.L1320:
	b	.L1320
.L1316:
	cmp	w0, 1
	beq	.L1314
	cmp	w1, 256
	beq	.L1314
	mov	w0, 65535
	cmp	w24, w0
	beq	.L1321
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
.L1321:
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
	bne	.L1334
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlSysFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1334:
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
	ldr	w2, [x1, 352]
	cmp	w3, w2
	bhi	.L1345
	cmp	w20, 31
	bhi	.L1339
.L1353:
	mov	w0, 0
.L1337:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1339:
	ldrh	w1, [x1, 270]
	udiv	w21, w0, w1
	msub	w0, w1, w21, w0
	ands	w0, w0, 65535
	beq	.L1340
	sub	w1, w1, w0
	add	w21, w21, 1
	cmp	w1, w20
	csel	w1, w1, w20, ls
	sub	w20, w20, w1, uxth
.L1340:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	str	w0, [x29, 60]
.L1341:
	ldrh	w0, [x19, 270]
	cmp	w20, w0
	bcs	.L1343
	ldr	w0, [x19, 3964]
	cmp	w0, 32
	bls	.L1353
	str	wzr, [x19, 3964]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1353
.L1343:
	mov	w2, 0
	add	x1, x29, 56
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 56]
	cmn	w0, #1
	beq	.L1342
	ldr	w0, [x19, 3964]
	mov	w2, 1
	add	x1, x29, 60
	add	w0, w0, 1
	str	w0, [x19, 3964]
	ldr	w0, [x19, 2640]
	add	w0, w0, 1
	str	w0, [x19, 2640]
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 56]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1342:
	ldrh	w0, [x19, 270]
	add	w21, w21, 1
	sub	w20, w20, w0
	b	.L1341
.L1345:
	mov	w0, -1
	b	.L1337
	.size	sftl_discard, .-sftl_discard
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC111
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x1, 576
	add	x0, x0, :lo12:.LC111
	str	x23, [sp, 48]
	bl	sftl_printk
	ldr	x0, [x19, 3568]
	mov	w21, 0
	ldrh	w2, [x19, 254]
	mov	w1, 0
	lsl	w2, w2, 1
	bl	ftl_memset
.L1355:
	ldr	w0, [x19, 2632]
	cmp	w21, w0
	bcc	.L1357
	adrp	x22, .LC121
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC121
	mov	w19, 0
	mov	w23, 65535
.L1358:
	ldrh	w0, [x20, 252]
	cmp	w0, w19
	bhi	.L1363
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 80
	ret
.L1357:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1356
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x19, 3568]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L1356:
	add	w21, w21, 1
	b	.L1355
.L1363:
	ldr	x0, [x20, 80]
	ubfiz	x21, x19, 1, 16
	ldrh	w2, [x0, x21]
	ldr	x0, [x20, 3568]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L1360
	cmp	w2, w23
	beq	.L1360
	mov	x0, x22
	mov	w1, w19
	bl	sftl_printk
	ldrh	w0, [x20, 32]
	cmp	w0, w19
	beq	.L1360
	ldrh	w0, [x20, 136]
	cmp	w0, w19
	beq	.L1360
	ldrh	w0, [x20, 88]
	cmp	w0, w19
	beq	.L1360
	ldr	x0, [x20, 80]
	ldrh	w1, [x0, x21]
	cbnz	w1, .L1362
	ldr	x1, [x20, 3568]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
.L1360:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1358
.L1362:
	ldr	x1, [x20, 3568]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
	mov	w0, w19
	bl	update_vpc_list
	b	.L1360
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
	ldrh	w0, [x0, 252]
	cmp	w0, w20
	bcs	.L1369
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2755
	add	x1, x1, 600
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1369:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L1370
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x20, 1, 16
	ldr	x1, [x1, 80]
	ldrh	w0, [x1, x0]
	cbz	w0, .L1371
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L1370:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x21, 8]
	mov	w1, 65535
	ldrh	w0, [x2, 3714]
	cmp	w0, w1
	beq	.L1372
	cmp	w20, w0
	bne	.L1373
	ldr	x2, [x2, 80]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1374
.L1373:
	bl	update_vpc_list
.L1374:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19, 3714]
.L1372:
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
.L1371:
	mov	w0, w20
	bl	INSERT_FREE_LIST
	b	.L1370
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.type	FtlProgPages, %function
FtlProgPages:
	stp	x29, x30, [sp, -96]!
	mov	w2, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x3
	stp	x23, x24, [sp, 48]
	adrp	x23, .LC122
	stp	x21, x22, [sp, 32]
	adrp	x24, .LANCHOR0
	str	x25, [sp, 64]
	mov	w21, w1
	add	x23, x23, :lo12:.LC122
	add	x25, x24, :lo12:.LANCHOR0
	ldrb	w3, [x3, 9]
	ubfiz	x21, x21, 5, 32
	add	x21, x21, 4
	add	x19, x0, 4
	add	x21, x0, x21
	bl	FlashProgPages
.L1380:
	cmp	x21, x19
	beq	.L1386
	sub	x22, x19, #4
	b	.L1387
.L1382:
	ldr	w1, [x19]
	mov	x0, x23
	bl	sftl_printk
	ldr	w0, [x19]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1381
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1381:
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x19]
	mov	w2, 0
	str	w0, [x29, 92]
	mov	w1, 1
	ldrb	w3, [x20, 9]
	mov	x0, x22
	bl	FlashProgPages
.L1387:
	ldr	w2, [x19, -4]
	cmp	w2, 256
	ccmn	w2, #1, 4, ne
	beq	.L1382
	ldrb	w1, [x20, 6]
	ldrh	w0, [x25, 244]
	cmp	w1, w0
	bcc	.L1383
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 985
	add	x1, x1, 632
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1383:
	add	x1, x29, 96
	ldr	w0, [x22, 4]
	mov	w2, 1
	str	w0, [x1, -4]!
	ldr	w0, [x22, 24]
	bl	log2phys
	ldr	x0, [x22, 16]
	ldr	w4, [x0, 12]
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	and	w1, w0, 65535
	mov	w22, w1
	cmn	w4, #1
	beq	.L1384
	add	x2, x24, :lo12:.LANCHOR0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x2, 80]
	ldrh	w0, [x2, x0]
	cbnz	w0, .L1385
	adrp	x0, .LC123
	mov	w2, 0
	add	x0, x0, :lo12:.LC123
	bl	sftl_printk
.L1385:
	mov	w0, w22
	bl	decrement_vpc_count
.L1384:
	add	x19, x19, 32
	b	.L1380
.L1386:
	adrp	x0, .LANCHOR0+244
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, #:lo12:.LANCHOR0+244]
	cmp	w1, w0
	bcc	.L1379
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1000
	add	x1, x1, 632
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1379:
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
	str	x25, [sp, 64]
	ldrh	w4, [x2, 136]
	ldrh	w1, [x2, 314]
	cmp	w4, w3
	bne	.L1394
.L1401:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 136
	ldrh	w3, [x0, 136]
	str	wzr, [x0, 3720]
	cmp	w3, w2
	bne	.L1414
.L1418:
	mov	w0, 0
.L1393:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1394:
	adrp	x20, .LANCHOR2
	cbz	w0, .L1397
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w4, [x0, 4]
	cmp	w4, w3
	beq	.L1398
.L1399:
	mov	w1, 2
.L1397:
	add	x20, x20, :lo12:.LANCHOR2
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 136
	bl	FtlGcScanTempBlk
	str	w0, [x29, 92]
	mov	w0, 65535
	ldrh	w1, [x20, 4]
	cmp	w1, w0
	beq	.L1401
	mov	w0, 1
	b	.L1393
.L1398:
	strh	wzr, [x0, 4]
	ldrh	w0, [x2, 236]
	cmp	w0, 17
	bhi	.L1399
	b	.L1397
.L1414:
	ldrh	w2, [x0, 3304]
	ldrb	w1, [x1, 7]
	ldrh	w0, [x0, 314]
	mul	w0, w1, w0
	cmp	w2, w0
	beq	.L1402
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 164
	add	x1, x1, 648
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1402:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w21, 0
	mov	x20, x0
	mov	w25, 12
	ldrb	w1, [x0, 143]
	ldrh	w4, [x0, 314]
	ldr	x2, [x0, 80]
	ldrh	w3, [x0, 136]
	mul	w1, w1, w4
	strh	w1, [x2, x3, lsl 1]
	ldr	w2, [x0, 2656]
	ldrh	w1, [x0, 3304]
	add	w1, w1, w2
	str	w1, [x0, 2656]
.L1403:
	ldrh	w0, [x20, 3304]
	cmp	w0, w21
	bhi	.L1407
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x20, 136]
	ldr	x2, [x20, 80]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1408
	bl	INSERT_DATA_LIST
.L1409:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, -1
	strh	wzr, [x19, 3304]
	strh	w20, [x19, 136]
	strh	wzr, [x19, 3292]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w20, [x19, 184]
	ldrh	w0, [x19, 2700]
	ldrh	w1, [x19, 236]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	ble	.L1418
	mov	w0, 20
	strh	w0, [x19, 3232]
	b	.L1418
.L1407:
	umull	x23, w21, w25
	ldr	x24, [x20, 3312]
	ldr	w1, [x20, 2632]
	add	x22, x24, x23
	ldr	w0, [x22, 8]
	cmp	w0, w1
	bcc	.L1404
.L1416:
	ldrh	w0, [x20, 136]
	b	.L1417
.L1404:
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x24, x23]
	ldr	w1, [x29, 92]
	cmp	w0, w1
	bne	.L1406
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	mov	w23, w0
	ldr	w0, [x22, 8]
	mov	w2, 1
	add	x1, x22, 4
	bl	log2phys
	mov	w0, w23
.L1417:
	bl	decrement_vpc_count
	b	.L1405
.L1406:
	ldr	w0, [x22, 4]
	cmp	w1, w0
	bne	.L1416
.L1405:
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L1403
.L1408:
	bl	INSERT_FREE_LIST
	b	.L1409
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
	add	x20, x19, 136
	mov	x0, x20
	ldrh	w1, [x19, 314]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19, 138]
	ldrh	w0, [x19, 314]
	cmp	w1, w0
	bcc	.L1419
	add	x0, x19, 3648
	bl	FtlMapBlkWriteDump_data
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19, 3720]
.L1419:
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
	add	x20, x19, 32
	add	x19, x19, 88
	mov	x0, x20
	str	wzr, [x19, 3676]
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
.L1425:
	ldr	w1, [x19, 3248]
	cmp	w10, w1
	bcc	.L1428
	ldr	x0, [x19, 3448]
	bl	FtlGcBufFree
	str	wzr, [x19, 3248]
	ldrh	w0, [x19, 140]
	cbnz	w0, .L1430
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w0, 1
.L1424:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1428:
	ldr	x2, [x19, 3448]
	ubfiz	x0, x10, 5, 16
	add	x1, x2, x0
	ldr	w2, [x2, x0]
	ldr	x3, [x1, 16]
	cmn	w2, #1
	bne	.L1426
.L1432:
	ldr	w1, [x1, 4]
	ldr	w0, [x3, 12]
	bl	FtlGcUpdatePage
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L1425
.L1426:
	ldr	w2, [x3, 8]
	b	.L1432
.L1430:
	mov	w0, 0
	b	.L1424
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
	add	x1, x0, 136
	ldrh	w2, [x0, 136]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L1434
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1435
.L1434:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x20, 136
	strb	wzr, [x0, 8]
	bl	allocate_data_superblock
	strh	wzr, [x20, 3292]
	strh	wzr, [x20, 3304]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1435:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 136
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
	cbnz	w2, .L1516
	ldrh	w2, [x1, 2592]
	cmp	w2, 47
	bls	.L1516
	mov	w23, w0
	adrp	x0, .LANCHOR2+4
	ldrh	w2, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1439
.L1442:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 232]
	cmp	w1, w2
	bne	.L1440
.L1441:
	add	x20, x19, :lo12:.LANCHOR0
	bl	FtlReadRefresh
	mov	w21, 65535
	ldr	w0, [x20, 3236]
	ldrh	w1, [x20, 184]
	add	w0, w0, 1
	add	w0, w0, w23, lsl 7
	str	w0, [x20, 3236]
	cmp	w1, w21
	bne	.L1443
	ldrh	w21, [x20, 136]
	cmp	w21, w1
	bne	.L1519
	ldrh	w22, [x20, 234]
	cmp	w22, w21
	bne	.L1443
	ldrh	w1, [x20, 236]
	mov	w2, 1024
	cmp	w1, 24
	mov	w1, 5120
	csel	w1, w1, w2, cc
	cmp	w0, w1
	bls	.L1522
	strh	wzr, [x20, 3968]
	str	wzr, [x20, 3236]
	bl	GetSwlReplaceBlock
	and	w21, w0, 65535
	cmp	w21, w22
	bne	.L1523
	ldrh	w1, [x20, 236]
	ldrh	w0, [x20, 3234]
	cmp	w1, w0
	bcs	.L1446
	mov	w0, 64
	bl	List_get_gc_head_node
	and	w0, w0, 65535
	cmp	w0, w21
	beq	.L1448
	ldr	x1, [x20, 80]
	ubfiz	x0, x0, 1, 16
	ldrh	w0, [x1, x0]
	cmp	w0, 7
	bhi	.L1449
	mov	w0, 0
	bl	List_get_gc_head_node
	and	w22, w0, 65535
	mov	w0, 128
	strh	w0, [x20, 3234]
	cmp	w22, w21
	bne	.L1445
.L1448:
	bl	FtlGcReFreshBadBlk
.L1443:
	cmp	w23, 0
	mov	w0, 65535
	add	x1, x19, :lo12:.LANCHOR0
	ccmp	w21, w0, 0, eq
	bne	.L1451
	ldrh	w0, [x1, 236]
	cmp	w0, 24
	bhi	.L1524
	cmp	w0, 16
	ldrh	w20, [x1, 314]
	bls	.L1453
	lsr	w20, w20, 5
.L1452:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1, 3232]
	cmp	w2, w0
	bcs	.L1456
	ldrh	w0, [x1, 136]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1457
	ldrh	w2, [x1, 234]
	cmp	w2, w0
	bne	.L1457
	ldrh	w0, [x1, 3968]
	cbnz	w0, .L1458
	ldr	w2, [x1, 2632]
	ldr	w3, [x1, 2628]
	add	w2, w2, w2, lsl 1
	cmp	w3, w2, lsr 2
	bcs	.L1459
.L1458:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2, 2700]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2, 3232]
.L1460:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19, 3244]
.L1437:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1439:
	ldrh	w1, [x1, 136]
	cmp	w1, w0
	beq	.L1442
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	cbz	w0, .L1442
	mov	w0, 1
	b	.L1437
.L1440:
	ldrh	w3, [x0, 234]
	cmp	w3, w2
	bne	.L1441
	strh	w1, [x0, 234]
	mov	w1, -1
	strh	w1, [x0, 232]
	b	.L1441
.L1449:
	mov	w0, 64
.L1554:
	strh	w0, [x20, 3234]
	b	.L1448
.L1446:
	mov	w0, 80
	b	.L1554
.L1523:
	mov	w22, w21
.L1445:
	add	x0, x19, :lo12:.LANCHOR0
	ubfiz	x1, x22, 1, 32
	mov	w21, w22
	ldr	x2, [x0, 80]
	ldr	x3, [x0, 2608]
	ldrh	w5, [x0, 3232]
	ldrh	w4, [x3, x1]
	ldrh	w3, [x2, x1]
	mov	w1, w22
	ldrh	w2, [x0, 236]
	adrp	x0, .LC124
	add	x0, x0, :lo12:.LC124
	bl	sftl_printk
	b	.L1448
.L1519:
	mov	w21, w1
	b	.L1443
.L1522:
	mov	w21, w22
	b	.L1443
.L1453:
	cmp	w0, 12
	bls	.L1454
	lsr	w20, w20, 4
	b	.L1452
.L1454:
	mov	w1, w20
	cmp	w0, 9
	lsr	w20, w20, 2
	csel	w20, w20, w1, cs
	b	.L1452
.L1524:
	mov	w20, 1
	b	.L1452
.L1459:
	mov	w2, 18
	strh	w2, [x1, 3232]
	b	.L1460
.L1457:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2700]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 3232]
.L1456:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 3320]
	cbz	w0, .L1525
	add	w20, w20, 32
	and	w20, w20, 65535
.L1525:
	mov	w21, 65535
.L1462:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 184]
	cmp	w1, w2
	bne	.L1471
	cmp	w21, w1
	beq	.L1472
	strh	w21, [x0, 184]
.L1473:
	add	x5, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldrh	w0, [x5, 184]
	strb	wzr, [x5, 192]
	cmp	w0, w1
	beq	.L1471
	bl	IsBlkInGcList
	cbz	w0, .L1476
	mov	w0, -1
	strh	w0, [x5, 184]
.L1476:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	add	x24, x22, 184
	ldrh	w1, [x22, 184]
	cmp	w1, w0
	beq	.L1471
	mov	x0, x24
	bl	make_superblock
	ldrh	w1, [x22, 184]
	ldr	x0, [x22, 80]
	strh	wzr, [x22, 186]
	strb	wzr, [x22, 190]
	strh	wzr, [x22, 3970]
	ldrh	w0, [x0, x1, lsl 1]
	strh	w0, [x22, 3972]
.L1471:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 184]
	ldrh	w2, [x0, 32]
	cmp	w2, w1
	beq	.L1477
	ldrh	w2, [x0, 88]
	cmp	w2, w1
	beq	.L1477
	ldrh	w0, [x0, 136]
	cmp	w0, w1
	bne	.L1478
.L1477:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 184]
.L1478:
	adrp	x25, .LANCHOR1
	add	x25, x25, :lo12:.LANCHOR1
	add	x25, x25, 672
.L1513:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w24, [x22, 184]
	cmp	w24, w0
	bne	.L1479
	mov	w26, 2
	str	wzr, [x22, 3244]
.L1480:
	ldrh	w5, [x22, 3240]
	mov	w0, w5
	bl	List_get_gc_head_node
	and	w6, w0, 65535
	strh	w6, [x22, 184]
	cmp	w6, w24
	bne	.L1481
	strh	wzr, [x22, 3240]
	mov	w0, 8
	b	.L1437
.L1451:
	ldrh	w2, [x1, 136]
	cmp	w2, w0
	bne	.L1528
	ldrh	w0, [x1, 234]
	cmp	w0, w2
	bne	.L1528
	cmp	w21, w0
	bne	.L1528
	ldrh	w0, [x1, 184]
	cmp	w0, w21
	beq	.L1463
.L1528:
	mov	w20, 1
	b	.L1462
.L1463:
	ldrh	w2, [x1, 236]
	ldrh	w0, [x1, 3232]
	str	wzr, [x1, 3244]
	cmp	w2, w0
	bls	.L1464
	ldrh	w0, [x1, 3968]
	cbnz	w0, .L1465
	ldr	w0, [x1, 2632]
	ldr	w2, [x1, 2628]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bcs	.L1466
.L1465:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2700]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
.L1555:
	strh	w0, [x1, 3232]
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x1, 80]
	ldrh	w0, [x2, x0]
	cmp	w0, 4
	bls	.L1464
	ldrh	w0, [x1, 3968]
	b	.L1437
.L1466:
	mov	w0, 18
	b	.L1555
.L1464:
	add	x20, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20, 3968]
	cbnz	w0, .L1528
	ldrh	w5, [x20, 2700]
	add	w0, w5, w5, lsl 1
	asr	w0, w0, 2
	strh	w0, [x20, 3232]
	mov	w0, 0
	bl	List_get_gc_head_node
	ldr	x1, [x20, 80]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x20, 244]
	ldrh	w1, [x1, x0]
	ldrh	w0, [x20, 316]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L1469
	ldrh	w0, [x20, 236]
	sub	w5, w5, #1
	cmp	w0, w5
	blt	.L1469
	bl	FtlReadRefresh
	ldrh	w0, [x20, 3968]
	b	.L1437
.L1469:
	cbnz	w1, .L1528
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x19, 236]
	add	w0, w0, 1
	b	.L1437
.L1472:
	ldrh	w1, [x0, 234]
	cmp	w1, w21
	beq	.L1473
	ldr	x2, [x0, 80]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2, x1]
	cbnz	w1, .L1474
	mov	w1, -1
	strh	w1, [x0, 234]
.L1474:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 234]
	strh	w1, [x0, 184]
	mov	w1, -1
	strh	w1, [x0, 234]
	b	.L1473
.L1481:
	mov	w0, w6
	bl	IsBlkInGcList
	add	w5, w5, 1
	cbz	w0, .L1482
	strh	w5, [x22, 3240]
	b	.L1480
.L1482:
	ldrh	w4, [x22, 244]
	ubfiz	x1, x6, 1, 16
	ldrh	w0, [x22, 314]
	and	w5, w5, 65535
	ldr	x2, [x22, 80]
	strh	w5, [x22, 3240]
	mul	w0, w0, w4
	ldrh	w3, [x2, x1]
	sdiv	w4, w0, w26
	cmp	w3, w4
	bgt	.L1484
	cmp	w5, 48
	bls	.L1485
	cmp	w3, 8
	bls	.L1485
	ldrh	w3, [x22, 3292]
	cmp	w3, 35
	bhi	.L1485
.L1484:
	strh	wzr, [x22, 3240]
.L1485:
	ldrh	w1, [x2, x1]
	cmp	w0, w1
	bgt	.L1486
	cmp	w21, w24
	bne	.L1486
	ldrh	w0, [x22, 3240]
	cmp	w0, 3
	bhi	.L1486
	mov	w0, -1
	strh	wzr, [x22, 3240]
	strh	w0, [x22, 184]
.L1556:
	ldrh	w0, [x22, 3968]
	b	.L1437
.L1486:
	cbnz	w1, .L1487
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x22, 3240]
	add	w0, w0, 1
	strh	w0, [x22, 3240]
	b	.L1480
.L1487:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0, 192]
	ldrh	w0, [x0, 32]
	cmp	w0, w6
	bne	.L1488
	adrp	x0, .LC8
	mov	w2, 717
	mov	x1, x25
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1488:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 184]
	ldrh	w0, [x0, 88]
	cmp	w1, w0
	bne	.L1489
	adrp	x0, .LC8
	mov	w2, 718
	mov	x1, x25
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1489:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 184]
	ldrh	w0, [x0, 136]
	cmp	w1, w0
	bne	.L1490
	adrp	x0, .LC8
	mov	w2, 719
	mov	x1, x25
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1490:
	add	x22, x19, :lo12:.LANCHOR0
	add	x24, x22, 184
	mov	x0, x24
	bl	make_superblock
	ldrh	w1, [x22, 184]
	ldr	x0, [x22, 80]
	strh	wzr, [x22, 3970]
	ldrh	w0, [x0, x1, lsl 1]
	strh	w0, [x22, 3972]
	strh	wzr, [x22, 186]
	strb	wzr, [x22, 190]
.L1479:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 1
	str	w0, [x1, 3424]
	ldrh	w26, [x1, 314]
	cbz	w23, .L1491
	ldrh	w0, [x1, 244]
	ldrh	w2, [x1, 184]
	ldr	x1, [x1, 80]
	mul	w0, w0, w26
	ldrh	w1, [x1, x2, lsl 1]
	sub	w0, w0, w1
	mov	w1, 4
	sdiv	w0, w0, w1
	add	w20, w20, w0
	and	w20, w20, 65535
.L1491:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 186]
	add	w1, w0, w20
	cmp	w1, w26
	ble	.L1492
	sub	w20, w26, w0
	and	w20, w20, 65535
.L1492:
	mov	w27, 0
.L1493:
	cmp	w20, w27, uxth
	bls	.L1501
	add	x0, x19, :lo12:.LANCHOR0
	mov	w24, 0
	add	x3, x0, 200
	mov	w1, 0
	mov	w6, 65535
	ldrh	w4, [x0, 186]
	ldrh	w7, [x0, 244]
	add	w4, w4, w27
	b	.L1502
.L1495:
	ldrh	w2, [x3]
	cmp	w2, w6
	beq	.L1494
	ldr	x5, [x0, 3280]
	ubfiz	x8, x24, 5, 16
	add	w24, w24, 1
	orr	w2, w4, w2, lsl 10
	add	x5, x5, x8
	and	w24, w24, 65535
	str	w2, [x5, 4]
.L1494:
	add	w1, w1, 1
	add	x3, x3, 2
	and	w1, w1, 65535
.L1502:
	cmp	w1, w7
	bne	.L1495
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, w24
	mov	x28, 0
	ldrb	w2, [x22, 192]
	ldr	x0, [x22, 3280]
	bl	FlashReadPages
	ubfiz	x0, x24, 5, 16
	str	x0, [x29, 104]
.L1496:
	ldr	x0, [x29, 104]
	cmp	x0, x28
	bne	.L1500
	add	w27, w27, 1
	b	.L1493
.L1500:
	ldr	x0, [x22, 3280]
	add	x1, x0, x28
	ldr	w0, [x0, x28]
	cmn	w0, #1
	beq	.L1497
	ldr	x24, [x1, 16]
	mov	w0, 61589
	ldrh	w1, [x24]
	cmp	w1, w0
	bne	.L1497
	ldr	w4, [x24, 8]
	cmn	w4, #1
	bne	.L1498
	str	w4, [x29, 96]
	mov	w2, 753
	mov	x1, x25
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	ldr	w4, [x29, 96]
.L1498:
	mov	w2, 0
	add	x1, x29, 120
	mov	w0, w4
	bl	log2phys
	ldr	x0, [x22, 3280]
	ldr	w1, [x29, 120]
	add	x0, x0, x28
	ldr	w2, [x0, 4]
	cmp	w2, w1
	bne	.L1497
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
	ldr	w0, [x29, 120]
	mov	w1, 1
	str	w0, [x24, 12]
	ldrh	w0, [x22, 136]
	strh	w0, [x24, 2]
	ldr	w0, [x22, 2672]
	str	w0, [x24, 4]
	ldr	w0, [x22, 3248]
	add	w0, w0, 1
	str	w0, [x22, 3248]
	ldr	x0, [x22, 3280]
	add	x0, x0, x28
	bl	FtlGcBufAlloc
	ldrb	w1, [x22, 143]
	ldr	w0, [x22, 3248]
	cmp	w1, w0
	beq	.L1499
	ldrh	w0, [x22, 140]
	cbnz	w0, .L1497
.L1499:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1497
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19, 186]
	strh	w0, [x19, 184]
	str	wzr, [x19, 3424]
	ldrh	w0, [x19, 3968]
	b	.L1437
.L1497:
	add	x28, x28, 32
	b	.L1496
.L1501:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 186]
	add	w20, w20, w0
	and	w20, w20, 65535
	strh	w20, [x22, 186]
	cmp	w26, w20
	bhi	.L1503
	ldr	w0, [x22, 3248]
	cbz	w0, .L1504
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1504
	str	wzr, [x22, 3424]
	b	.L1556
.L1504:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3970]
	cbnz	w1, .L1505
	ldrh	w2, [x0, 184]
	ldr	x1, [x0, 80]
	ldrh	w1, [x1, x2, lsl 1]
	cbz	w1, .L1505
	mov	x20, x0
	mov	w22, 0
.L1506:
	ldr	w0, [x20, 2632]
	cmp	w22, w0
	bcs	.L1511
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w22
	bl	log2phys
	ldr	w0, [x29, 124]
	cmn	w0, #1
	beq	.L1507
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20, 184]
	cmp	w1, w0, uxth
	bne	.L1507
.L1511:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0, 2632]
	cmp	w22, w1
	bcc	.L1505
	ldrh	w2, [x0, 184]
	ldr	x1, [x0, 80]
	strh	wzr, [x1, x2, lsl 1]
	ldrh	w0, [x0, 184]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1505:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 184]
.L1503:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 236]
	str	wzr, [x0, 3424]
	cmp	w1, 2
	bhi	.L1512
	ldrh	w20, [x0, 314]
	b	.L1513
.L1507:
	add	w22, w22, 1
	b	.L1506
.L1512:
	ldrh	w0, [x0, 3968]
	cmp	w0, 0
	csinc	w0, w0, w1, ne
	b	.L1437
.L1516:
	mov	w0, 0
	b	.L1437
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
	bne	.L1558
	mov	x2, x3
	mov	w1, w25
	add	w0, w19, 256
	bl	FtlVendorPartRead
	mov	w21, w0
.L1557:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1558:
	add	w0, w1, w2
	str	w0, [x29, 128]
	adrp	x0, .LANCHOR0
	add	x3, x0, :lo12:.LANCHOR0
	add	w2, w1, w2
	str	x0, [x29, 96]
	ldr	w1, [x3, 352]
	cmp	w2, w1
	bhi	.L1579
	adrp	x0, .LANCHOR2
	ldr	w21, [x0, #:lo12:.LANCHOR2]
	cmn	w21, #1
	beq	.L1557
	ldrh	w0, [x3, 270]
	sub	w24, w2, #1
	mov	x26, x3
	mov	w28, 0
	mov	w21, 0
	stp	wzr, wzr, [x29, 132]
	str	wzr, [x29, 140]
	udiv	w27, w19, w0
	udiv	w24, w24, w0
	ldr	w0, [x3, 2664]
	add	w0, w0, w25
	str	w0, [x3, 2664]
	ldr	w0, [x3, 2636]
	mov	w20, w27
	sub	w22, w24, w27
	add	w22, w22, 1
	add	w0, w0, w22
	str	w0, [x3, 2636]
.L1560:
	cbnz	w22, .L1577
	ldr	x0, [x29, 96]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3320]
	cbnz	w1, .L1578
	ldrh	w0, [x0, 236]
	cmp	w0, 31
	bhi	.L1557
.L1578:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	b	.L1557
.L1577:
	add	x1, x29, 156
	mov	w2, 0
	mov	w0, w20
	bl	log2phys
	ldr	w1, [x29, 156]
	cmn	w1, #1
	bne	.L1561
	mov	w5, 0
.L1562:
	ldrh	w0, [x26, 270]
	cmp	w5, w0
	bcc	.L1564
.L1565:
	add	w20, w20, 1
	subs	w22, w22, #1
	beq	.L1569
	ldrh	w0, [x26, 244]
	cmp	w28, w0, lsl 2
	bne	.L1560
.L1569:
	cbz	w28, .L1560
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
.L1576:
	ldr	x0, [x26, 3440]
	add	x0, x0, x28
	ldr	w1, [x0, 24]
	cmp	w27, w1
	bne	.L1571
	ldr	x1, [x0, 8]
	ldr	x0, [x26, 3488]
	cmp	x1, x0
	bne	.L1572
	ldr	x0, [x29, 120]
	ldr	w2, [x29, 116]
	add	x1, x1, x0
	mov	x0, x23
.L1589:
	bl	ftl_memcpy
.L1572:
	ldr	x0, [x26, 3440]
	add	x0, x0, x28
	ldr	x1, [x0, 16]
	ldr	w2, [x0, 24]
	ldr	w1, [x1, 8]
	cmp	w2, w1
	beq	.L1573
	ldr	w1, [x26, 2792]
	add	w1, w1, 1
	str	w1, [x26, 2792]
.L1573:
	ldr	w2, [x0]
	cmn	w2, #1
	bne	.L1574
	ldr	w1, [x26, 2792]
	mov	w21, w2
	add	w1, w1, 1
	str	w1, [x26, 2792]
.L1574:
	ldr	w1, [x0]
	cmp	w1, 256
	bne	.L1575
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1575:
	ldr	x0, [x29, 104]
	add	x28, x28, 32
	cmp	x0, x28
	bne	.L1576
	mov	w28, 0
	b	.L1560
.L1564:
	madd	w0, w20, w0, w5
	cmp	w19, w0
	bhi	.L1563
	ldr	w1, [x29, 128]
	cmp	w1, w0
	bls	.L1563
	sub	w0, w0, w19
	str	w5, [x29, 120]
	lsl	w0, w0, 9
	mov	w2, 512
	mov	w1, 0
	add	x0, x23, x0
	bl	ftl_memset
	ldr	w5, [x29, 120]
.L1563:
	add	w5, w5, 1
	b	.L1562
.L1561:
	ldr	x0, [x26, 3440]
	ubfiz	x2, x28, 5, 32
	cmp	w20, w27
	add	x0, x0, x2
	str	w1, [x0, 4]
	ldrh	w0, [x26, 270]
	bne	.L1566
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
	bne	.L1567
	str	x23, [x1, 8]
.L1567:
	ldr	x0, [x26, 3440]
	ldr	x1, [x26, 3536]
	add	x2, x0, x2
	ldrh	w0, [x26, 324]
	str	w20, [x2, 24]
	mul	w0, w0, w28
	add	w28, w28, 1
	and	x0, x0, 4294967292
	add	x0, x1, x0
	str	x0, [x2, 16]
	b	.L1565
.L1566:
	cmp	w20, w24
	bne	.L1568
	ldr	x5, [x26, 3440]
	ldr	x1, [x26, 3496]
	add	x5, x5, x2
	ldr	w3, [x29, 128]
	str	x1, [x5, 8]
	mul	w1, w20, w0
	sub	w3, w3, w1
	str	w3, [x29, 132]
	cmp	w0, w3
	bne	.L1567
	sub	w1, w1, w19
	lsl	w1, w1, 9
	add	x1, x23, x1
	str	x1, [x5, 8]
	b	.L1567
.L1568:
	ldr	x1, [x26, 3440]
	mul	w0, w0, w20
	add	x1, x1, x2
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	str	x0, [x1, 8]
	b	.L1567
.L1571:
	cmp	w24, w1
	bne	.L1572
	ldr	x1, [x0, 8]
	ldr	x0, [x26, 3496]
	cmp	x1, x0
	bne	.L1572
	ldrh	w0, [x26, 270]
	ldr	w2, [x29, 112]
	mul	w0, w0, w24
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	b	.L1589
.L1579:
	mov	w21, -1
	b	.L1557
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
	bne	.L1593
	add	w0, w1, 256
	ldr	w1, [x29, 172]
	mov	x2, x3
	bl	FtlVendorPartWrite
.L1592:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	ret
.L1593:
	ldr	w0, [x29, 172]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	add	w0, w23, w0
	str	w0, [x29, 148]
	ldr	w0, [x1, 352]
	ldr	w2, [x29, 148]
	cmp	w2, w0
	bhi	.L1629
	adrp	x2, .LANCHOR2
	str	x2, [x29, 136]
	add	x3, x2, :lo12:.LANCHOR2
	ldr	w0, [x2, #:lo12:.LANCHOR2]
	cmn	w0, #1
	beq	.L1592
	mov	w0, 2048
	ldr	w2, [x29, 148]
	str	w0, [x1, 3976]
	add	x20, x1, 32
	ldrh	w0, [x1, 270]
	sub	w2, w2, #1
	udiv	w25, w23, w0
	udiv	w0, w2, w0
	ldr	w2, [x29, 172]
	str	w0, [x29, 144]
	sub	w0, w0, w25
	add	w24, w0, 1
	str	w0, [x29, 132]
	ldr	w0, [x1, 2660]
	add	w0, w0, w2
	str	w0, [x1, 2660]
	ldr	w0, [x1, 2644]
	add	w0, w0, w24
	str	w0, [x1, 2644]
	ldr	w0, [x3, 8]
	cbz	w0, .L1595
	ldrh	w0, [x20, 4]
	add	x1, x1, 88
	cmp	w0, 0
	csel	x20, x1, x20, eq
.L1595:
	adrp	x0, .LANCHOR1
	mov	w21, w25
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 696
	str	x0, [x29, 160]
.L1596:
	cbnz	w24, .L1624
	ldr	w1, [x29, 132]
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x19, 236]
	cmp	w0, 5
	bls	.L1625
.L1627:
	mov	w0, 0
	b	.L1592
.L1624:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 244]
	cmp	w1, w0
	bcc	.L1597
	ldr	x1, [x29, 160]
	adrp	x0, .LC8
	mov	w2, 1041
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1597:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1598
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 32
	cmp	x20, x1
	bne	.L1599
	add	x0, x0, 88
	ldrh	w1, [x0, 4]
	cbnz	w1, .L1600
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
.L1600:
	add	x20, x19, :lo12:.LANCHOR0
	add	x22, x20, 32
	add	x20, x20, 88
	mov	x0, x22
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	cmp	w0, 0
	csel	x20, x20, x22, ne
.L1601:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1598
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1598:
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
	ldrh	w0, [x0, 244]
	cmp	w1, w0
	bcc	.L1602
	ldr	x1, [x29, 160]
	adrp	x0, .LC8
	mov	w2, 1074
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1602:
	mov	x28, 0
.L1603:
	ldr	w0, [x29, 128]
	mov	w22, w28
	cmp	w28, w0
	bcc	.L1622
	mov	x22, x0
.L1604:
	add	x0, x19, :lo12:.LANCHOR0
	mov	x3, x20
	mov	w2, 0
	mov	w1, w22
	ldr	x0, [x0, 3456]
	bl	FtlProgPages
	cmp	w24, w22
	bcs	.L1623
	ldr	x1, [x29, 160]
	adrp	x0, .LC8
	mov	w2, 1152
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1623:
	sub	w24, w24, w22
	b	.L1596
.L1599:
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1634
	mov	x0, x20
	bl	allocate_new_data_superblock
	b	.L1601
.L1634:
	mov	x20, x1
	b	.L1598
.L1622:
	ldrh	w0, [x20, 4]
	cbz	w0, .L1604
	add	x1, x29, 188
	mov	w2, 0
	mov	w0, w21
	bl	log2phys
	mov	x0, x20
	lsl	x27, x28, 5
	bl	get_new_active_ppa
	add	x3, x19, :lo12:.LANCHOR0
	str	x3, [x29, 152]
	ldr	x1, [x3, 3456]
	ldrh	w2, [x3, 324]
	add	x1, x1, x27
	str	w0, [x1, 4]
	mul	w22, w22, w2
	ldr	x0, [x3, 3456]
	and	x1, x22, 4294967292
	str	x1, [x29, 120]
	add	x0, x0, x27
	ldr	x1, [x3, 3536]
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
	ldr	x3, [x29, 152]
	ccmp	w21, w0, 4, ne
	bne	.L1605
	cmp	w21, w25
	bne	.L1606
	ldrh	w2, [x3, 270]
	udiv	w0, w23, w2
	msub	w0, w0, w2, w23
	str	w0, [x29, 168]
	sub	w2, w2, w0
	ldr	w0, [x29, 172]
	cmp	w2, w0
	csel	w0, w2, w0, ls
	str	w0, [x29, 152]
.L1607:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29, 152]
	ldrh	w1, [x0, 270]
	cmp	w2, w1
	bne	.L1608
	ldr	x0, [x0, 3456]
	cmp	w21, w25
	add	x4, x0, x27
	bne	.L1609
	str	x26, [x4, 8]
.L1610:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 244]
	cmp	w1, w0
	bcc	.L1619
	ldr	x1, [x29, 160]
	adrp	x0, .LC8
	mov	w2, 1143
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1619:
	ldp	x1, x2, [x29, 112]
	mov	w0, -3947
	add	x28, x28, 1
	strh	w0, [x1, x2]
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w0, [x1, 2672]
	stp	w0, w21, [x22, 4]
	add	w21, w21, 1
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1, 2672]
	ldr	w0, [x29, 188]
	str	w0, [x22, 12]
	ldrh	w0, [x20]
	strh	w0, [x22, 2]
	b	.L1603
.L1606:
	ldr	w0, [x29, 148]
	ldrh	w2, [x3, 270]
	str	wzr, [x29, 168]
	msub	w2, w21, w2, w0
	and	w0, w2, 65535
	str	w0, [x29, 152]
	b	.L1607
.L1609:
	ldr	w0, [x29, 152]
.L1641:
	mul	w0, w0, w21
	sub	w0, w0, w23
	lsl	w0, w0, 9
	add	x0, x26, x0
	str	x0, [x4, 8]
	b	.L1610
.L1608:
	ldr	x1, [x0, 3456]
	cmp	w21, w25
	add	x1, x1, x27
	bne	.L1611
	ldr	x0, [x0, 3488]
.L1640:
	str	x0, [x1, 8]
	ldr	w0, [x29, 188]
	cmn	w0, #1
	beq	.L1613
	add	x5, x19, :lo12:.LANCHOR0
	str	w0, [x29, 196]
	str	w21, [x29, 216]
	mov	w2, 0
	str	x5, [x29, 104]
	ldr	x0, [x5, 3456]
	add	x0, x0, x27
	ldp	x1, x0, [x0, 8]
	stp	x1, x0, [x29, 200]
	mov	w1, 1
	add	x0, x29, 192
	bl	FlashReadPages
	ldr	w3, [x29, 192]
	ldr	x5, [x29, 104]
	cmn	w3, #1
	bne	.L1614
	ldr	w0, [x5, 2792]
	mov	w2, w21
	add	w0, w0, 1
	str	w0, [x5, 2792]
	adrp	x0, .LC125
	add	x0, x0, :lo12:.LC125
	ldr	w1, [x22, 8]
	bl	sftl_printk
.L1617:
	ldr	w0, [x29, 152]
	cmp	w21, w25
	lsl	w2, w0, 9
	add	x0, x19, :lo12:.LANCHOR0
	bne	.L1618
	ldr	x0, [x0, 3456]
	mov	x1, x26
	add	x4, x0, x27
	ldr	w0, [x29, 168]
	ldr	x3, [x4, 8]
	lsl	w0, w0, 9
	add	x0, x3, x0
.L1642:
	bl	ftl_memcpy
	b	.L1610
.L1611:
	ldr	x0, [x0, 3496]
	b	.L1640
.L1614:
	ldr	w0, [x22, 8]
	cmp	w21, w0
	beq	.L1616
	ldr	w0, [x5, 2792]
	mov	w2, w21
	add	w0, w0, 1
	str	w0, [x5, 2792]
	adrp	x0, .LC126
	add	x0, x0, :lo12:.LC126
	ldr	w1, [x22, 8]
	bl	sftl_printk
.L1616:
	ldr	w0, [x22, 8]
	cmp	w21, w0
	beq	.L1617
	ldr	x1, [x29, 160]
	mov	w2, 1128
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L1617
.L1613:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x0, [x1, 3456]
	ldrh	w2, [x1, 322]
	mov	w1, 0
	add	x0, x0, x27
	ldr	x0, [x0, 8]
	bl	ftl_memset
	b	.L1617
.L1618:
	ldrh	w1, [x0, 270]
	ldr	x0, [x0, 3456]
	add	x4, x0, x27
	mul	w1, w1, w21
	sub	w1, w1, w23
	ldr	x0, [x4, 8]
	lsl	w1, w1, 9
	add	x1, x26, x1
	b	.L1642
.L1605:
	ldr	x0, [x3, 3456]
	add	x4, x0, x27
	ldrh	w0, [x3, 270]
	b	.L1641
.L1625:
	mov	w20, 256
	mov	w21, 65535
	mov	w22, 128
.L1628:
	ldrh	w0, [x19, 184]
	cmp	w0, w21
	bne	.L1626
	ldrh	w0, [x19, 234]
	cmp	w0, w21
	bne	.L1626
	mov	w0, 0
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L1626:
	mov	w1, 1
	strh	w22, [x19, 3234]
	mov	w0, w1
	strh	w22, [x19, 3232]
	bl	rk_ftl_garbage_collect
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x19, 236]
	cmp	w0, 2
	bhi	.L1627
	subs	w20, w20, #1
	bne	.L1628
	b	.L1627
.L1629:
	mov	w0, -1
	b	.L1592
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
	ldr	x0, [x21, 80]
	ldrh	w2, [x21, 252]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x21, 2704]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1646
.L1657:
	mov	w0, -1
.L1645:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1646:
	add	x23, x21, 2704
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w19, w0
	adrp	x1, .LANCHOR1
	add	w0, w0, 1
	add	x1, x1, :lo12:.LANCHOR1
	strh	w0, [x23, 2]
	ldrsh	w23, [x21, 2704]
	add	x24, x1, 712
.L1648:
	tbz	w19, #31, .L1654
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1465
	add	x1, x1, 712
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L1653
.L1654:
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
	cbz	w7, .L1649
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L1649
	ldrh	w1, [x21, 322]
	ldr	x0, [x20, 8]
	bl	js_hash
	cmp	w7, w0
	beq	.L1649
	ldrh	w4, [x21, 2708]
	mov	w6, w0
	mov	w5, w7
	adrp	x0, .LC127
	mov	w3, w19
	mov	w2, w23
	mov	x1, x24
	add	x0, x0, :lo12:.LC127
	bl	sftl_printk
	cbnz	w19, .L1650
	ldrh	w0, [x21, 2708]
	cmp	w23, w0
	beq	.L1650
	ldrh	w19, [x21, 316]
	sxth	w23, w0
.L1652:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1648
.L1650:
	mov	w0, -1
	str	w0, [x20]
.L1649:
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L1652
	ldr	x0, [x21, 3464]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1652
	ldr	x0, [x21, 3528]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L1652
.L1653:
	add	x1, x22, :lo12:.LANCHOR0
	ldrh	w0, [x1, 252]
	ldrh	w1, [x1, 322]
	add	x0, x0, 24
	cmp	x1, x0, lsl 1
	bcs	.L1656
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1467
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1656:
	add	x19, x22, :lo12:.LANCHOR0
	mov	w2, 48
	add	x20, x19, 2520
	mov	x0, x20
	ldr	x1, [x19, 3736]
	bl	ftl_memcpy
	ldr	x0, [x19, 80]
	ldrh	w2, [x19, 252]
	ldr	x1, [x19, 3736]
	lsl	w2, w2, 1
	add	x1, x1, 48
	bl	ftl_memcpy
	ldrh	w1, [x19, 252]
	ldr	x0, [x19, 3736]
	lsr	w2, w1, 3
	ubfiz	x1, x1, 1, 16
	add	x1, x1, 48
	add	w2, w2, 4
	and	x1, x1, -4
	add	x1, x0, x1
	ldr	x0, [x19, 24]
	bl	ftl_memcpy
	ldr	w1, [x19, 2520]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1657
	ldrh	w1, [x19, 2528]
	ldrb	w2, [x19, 2530]
	ldrh	w0, [x19, 266]
	strh	w1, [x19, 2710]
	cmp	w2, w0
	bne	.L1657
	ldrh	w0, [x19, 314]
	ldrh	w2, [x19, 270]
	ldrh	w3, [x19, 244]
	str	w1, [x19, 3980]
	mul	w0, w0, w1
	str	w0, [x19, 2632]
	mul	w0, w2, w0
	ldr	w2, [x19, 256]
	str	w0, [x19, 352]
	ldrh	w0, [x19, 374]
	cmp	w1, w2
	sub	w0, w2, w0
	sub	w0, w0, w1
	udiv	w0, w0, w3
	strh	w0, [x19, 2700]
	bls	.L1658
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1489
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1658:
	add	x0, x22, :lo12:.LANCHOR0
	mov	w3, -1
	ldrh	w2, [x0, 2536]
	ldrh	w4, [x0, 2534]
	strh	w4, [x0, 32]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0, 38]
	ldrb	w2, [x0, 2531]
	strb	w2, [x0, 40]
	ldrh	w2, [x0, 2538]
	strh	w2, [x0, 88]
	ldrh	w2, [x0, 2540]
	strh	w5, [x0, 34]
	strh	w3, [x0, 184]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0, 94]
	ldrb	w2, [x0, 2532]
	strb	w2, [x0, 96]
	ldrh	w2, [x0, 2542]
	strh	w2, [x0, 136]
	ldrh	w2, [x0, 2544]
	strh	w5, [x0, 90]
	strh	wzr, [x0, 186]
	lsr	w5, w2, 6
	and	w2, w2, 63
	strb	w2, [x0, 142]
	ldrb	w2, [x0, 2533]
	strb	w2, [x0, 144]
	ldr	w2, [x0, 2552]
	str	w2, [x0, 2676]
	strb	wzr, [x0, 190]
	strb	wzr, [x0, 192]
	strh	w5, [x0, 138]
	str	wzr, [x0, 2636]
	str	wzr, [x0, 2644]
	str	wzr, [x0, 2652]
	str	wzr, [x0, 2656]
	str	wzr, [x0, 2680]
	str	wzr, [x0, 2692]
	ldr	w1, [x0, 2560]
	ldr	w2, [x0, 2668]
	str	wzr, [x0, 2648]
	cmp	w1, w2
	bls	.L1659
	str	w1, [x0, 2668]
.L1659:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x0, 2556]
	ldr	w2, [x0, 2672]
	cmp	w1, w2
	bls	.L1660
	str	w1, [x0, 2672]
.L1660:
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1661
	add	x0, x22, :lo12:.LANCHOR0
	add	x0, x0, 32
	bl	make_superblock
.L1661:
	add	x1, x22, :lo12:.LANCHOR0
	add	x0, x1, 88
	ldrh	w2, [x1, 88]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1662
	bl	make_superblock
.L1662:
	add	x1, x22, :lo12:.LANCHOR0
	add	x0, x1, 136
	ldrh	w2, [x1, 136]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1663
	bl	make_superblock
.L1663:
	add	x22, x22, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x0, x22, 184
	ldrh	w2, [x22, 184]
	cmp	w2, w1
	beq	.L1664
	bl	make_superblock
.L1664:
	mov	w0, 0
	b	.L1645
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
.L1677:
	ldr	w0, [x29, 136]
	cmp	w20, w0
	bge	.L1696
	ldr	w0, [x29, 116]
	sxtw	x28, w20
	cmp	w20, w0
	bne	.L1678
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
.L1679:
	ldr	w0, [x29, 120]
	add	w0, w0, 1
	cmp	w26, w0
	blt	.L1682
.L1696:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x0, x19
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 2]
	ldrh	w0, [x21, 316]
	cmp	w1, w0
	bne	.L1684
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1684:
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
.L1682:
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
	cbz	w8, .L1680
	ldr	w0, [x27]
	cmn	w0, #1
	beq	.L1680
	ldrh	w1, [x28, 322]
	ldr	x0, [x27, 8]
	bl	js_hash
	cmp	w8, w0
	beq	.L1680
	mov	w5, w0
	mov	w4, w8
	mov	w3, w26
	mov	w2, w20
	mov	x1, x24
	adrp	x0, .LC128
	add	x0, x0, :lo12:.LC128
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x27]
.L1680:
	ldr	w0, [x27]
	cmn	w0, #1
	beq	.L1681
	ldrh	w0, [x22, 8]
	cmp	w25, w0
	bls	.L1681
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L1681
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x27, 4]
	str	w1, [x23, x0]
.L1681:
	add	w7, w26, 1
	sxth	w26, w7
	b	.L1679
.L1678:
	add	x26, x21, :lo12:.LANCHOR0
	lsl	x2, x28, 1
	add	x0, x26, 3728
	ldr	x1, [x26, 3464]
	str	x1, [x0, 8]
	add	x1, x24, x2
	str	x1, [x29, 120]
	ldrh	w1, [x24, x2]
	ldrh	w2, [x26, 316]
	sub	w2, w2, #1
	orr	w1, w2, w1, lsl 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x26, 3728]
	cmn	w0, #1
	beq	.L1698
	ldrh	w1, [x22]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L1698
	ldrh	w1, [x22, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L1686
.L1698:
	add	x26, x21, :lo12:.LANCHOR0
	mov	w27, 0
	add	x28, x26, 3728
.L1687:
	ldrh	w0, [x26, 316]
	cmp	w27, w0
	bge	.L1694
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
	cbz	w7, .L1691
	ldr	w0, [x28]
	cmn	w0, #1
	beq	.L1691
	ldrh	w1, [x26, 322]
	ldr	x0, [x28, 8]
	bl	js_hash
	cmp	w7, w0
	beq	.L1691
	ldr	x1, [x29, 104]
	mov	w5, w0
	mov	w4, w7
	mov	w3, w27
	mov	w2, w20
	adrp	x0, .LC129
	add	x0, x0, :lo12:.LC129
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x28]
.L1691:
	ldr	w0, [x28]
	cmn	w0, #1
	beq	.L1692
	ldrh	w0, [x22, 8]
	cmp	w25, w0
	bls	.L1692
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L1692
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x28, 4]
	str	w1, [x23, x0]
.L1692:
	add	w6, w27, 1
	sxth	w27, w6
	b	.L1687
.L1686:
	mov	w0, 0
.L1688:
	ldrh	w1, [x26, 316]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1690
.L1694:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L1677
.L1690:
	ldr	x2, [x26, 3464]
	sbfiz	x3, x0, 3, 32
	ldrh	w1, [x2, x3]
	cmp	w25, w1
	bls	.L1689
	add	x2, x2, x3
	ubfiz	x1, x1, 2, 16
	ldr	w2, [x2, 4]
	str	w2, [x23, x1]
.L1689:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1688
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
	ldrh	w2, [x1, 332]
	strh	w2, [x0, 10]
	mov	w2, -3962
	strh	w2, [x0, 4]
	ldrh	w2, [x1, 356]
	strh	w2, [x0, 8]
	ldrh	w2, [x1, 334]
	strh	w2, [x0, 6]
	ldr	x2, [x1, 360]
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
	ldrh	w0, [x21, 248]
	strh	wzr, [x21, 3760]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x21, 2704]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1723
.L1725:
	mov	w23, -1
.L1722:
	mov	w0, w23
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1723:
	bl	FtlLoadSysInfo
	mov	w23, w0
	cbnz	w0, .L1725
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldr	x1, [x21, 2616]
	mov	w0, 0
	ldrh	w3, [x21, 350]
	add	x1, x1, 4
.L1726:
	cmp	w0, w3
	bge	.L1731
	ldr	w2, [x1], 16
	tbz	w2, #31, .L1727
.L1731:
	add	x2, x20, :lo12:.LANCHOR0
	cmp	w0, w3
	ldrh	w1, [x2, 2548]
	add	w1, w1, 1
	strh	w1, [x2, 2548]
	bge	.L1737
.L1728:
	add	x19, x20, :lo12:.LANCHOR0
	add	x22, x19, 32
	add	x21, x19, 88
	mov	x0, x22
	bl	FtlSuperblockPowerLostFix
	mov	x0, x21
	bl	FtlSuperblockPowerLostFix
	ldrh	w0, [x19, 32]
	ldr	x2, [x19, 80]
	ldrh	w3, [x19, 36]
	lsl	x0, x0, 1
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strb	wzr, [x19, 38]
	ldrh	w0, [x19, 314]
	strh	w0, [x19, 34]
	ldrh	w0, [x19, 88]
	ldr	x2, [x19, 80]
	strh	wzr, [x19, 36]
	lsl	x0, x0, 1
	ldrh	w3, [x19, 92]
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strb	wzr, [x19, 94]
	ldrh	w0, [x19, 314]
	strh	w0, [x19, 90]
	ldrh	w0, [x19, 2550]
	strh	wzr, [x19, 92]
	add	w0, w0, 1
	strh	w0, [x19, 2550]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1732
.L1727:
	add	w0, w0, 1
	b	.L1726
.L1737:
	ldrh	w0, [x2, 3760]
	cbnz	w0, .L1728
.L1732:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x21, x19, 32
	ldrh	w0, [x19, 32]
	cmp	w0, w1
	beq	.L1733
	ldrh	w1, [x19, 36]
	cbnz	w1, .L1733
	ldrh	w1, [x19, 92]
	add	x20, x19, 88
	cbnz	w1, .L1733
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 88]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	mov	x0, x21
	bl	allocate_new_data_superblock
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1733:
	bl	FtlVpcCheckAndModify
	b	.L1722
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
	str	wzr, [x0, 2668]
	str	wzr, [x0, 2672]
	str	wzr, [x0, 2684]
	ldrh	w0, [x0, 248]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L1740
	bl	FtlMakeBbt
.L1740:
	mov	w4, 23752
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 0
	movk	w4, 0xa0f, lsl 16
.L1741:
	ldrh	w2, [x1, 270]
	cmp	w0, w2, lsl 7
	blt	.L1742
	ldrh	w21, [x1, 252]
	add	x22, x20, :lo12:.LANCHOR0
	mov	w19, 0
.L1743:
	ldrh	w0, [x22, 254]
	cmp	w0, w21
	bhi	.L1744
	ldrh	w0, [x22, 244]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L1745
.L1749:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w21, 0
	mov	w22, 0
.L1746:
	ldrh	w0, [x19, 252]
	cmp	w0, w22
	bhi	.L1750
	ldrh	w2, [x19, 244]
	ldr	w1, [x19, 256]
	ldrh	w0, [x19, 254]
	str	w0, [x19, 3436]
	udiv	w4, w1, w2
	ubfx	x3, x4, 5, 16
	str	w4, [x19, 2632]
	add	w0, w3, 36
	strh	w0, [x19, 2700]
	mov	w0, 24
	mul	w0, w2, w0
	cmp	w21, w0
	ble	.L1751
	sub	w1, w1, w21
	udiv	w1, w1, w2
	str	w1, [x19, 2632]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x19, 2700]
.L1751:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 306]
	cbz	w1, .L1753
	ldrh	w5, [x0, 2700]
	add	w5, w5, w1, lsr 1
	strh	w5, [x0, 2700]
	mul	w5, w1, w2
	cmp	w21, w5
	bge	.L1753
	add	w1, w1, 32
	str	w4, [x0, 2632]
	add	w1, w3, w1
	strh	w1, [x0, 2700]
.L1753:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w22, -1
	add	x21, x19, 32
	ldrh	w1, [x19, 2700]
	ldr	w0, [x19, 2632]
	sub	w0, w0, w1
	ldrh	w1, [x19, 314]
	mul	w0, w0, w2
	str	w0, [x19, 3980]
	mul	w0, w1, w0
	ldrh	w1, [x19, 270]
	str	w0, [x19, 2632]
	mul	w0, w1, w0
	str	w0, [x19, 352]
	bl	FtlBbmTblFlush
	ldr	x0, [x19, 80]
	mov	w1, 0
	ldrh	w2, [x19, 254]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w0, 1
	strb	w0, [x21, 8]
	ldr	x0, [x19, 24]
	mov	w1, 255
	ldrh	w2, [x19, 252]
	strh	w22, [x19, 184]
	strh	wzr, [x19, 186]
	strb	wzr, [x19, 190]
	lsr	w2, w2, 3
	strb	wzr, [x19, 192]
	strh	wzr, [x21, 2]
	strb	wzr, [x21, 6]
	strh	wzr, [x19, 32]
	str	wzr, [x19, 2628]
	bl	ftl_memset
.L1755:
	mov	x0, x21
	bl	make_superblock
	ldrb	w1, [x21, 7]
	ldrh	w0, [x21]
	cbnz	w1, .L1756
	ldr	x1, [x19, 80]
	ubfiz	x0, x0, 1, 16
	strh	w22, [x1, x0]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L1755
.L1742:
	ldr	x5, [x1, 3488]
	ubfiz	x3, x0, 2, 16
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w2, [x5, x3]
	ldr	x2, [x1, 3496]
	str	w4, [x2, x3]
	b	.L1741
.L1744:
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w19, w19, w0
	and	w21, w21, 65535
	and	w19, w19, 65535
	b	.L1743
.L1745:
	udiv	w0, w19, w0
	ldr	w19, [x22, 344]
	add	x21, x20, :lo12:.LANCHOR0
	add	w0, w0, w19
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22, 248]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x22, 252]
.L1747:
	ldrh	w0, [x21, 254]
	cmp	w0, w19
	bls	.L1749
	mov	w0, w19
	add	w19, w19, 1
	mov	w1, 1
	and	w19, w19, 65535
	bl	FtlLowFormatEraseBlock
	b	.L1747
.L1750:
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w21, w21, w0
	and	w22, w22, 65535
	and	w21, w21, 65535
	b	.L1746
.L1756:
	ldr	w1, [x19, 2668]
	ubfiz	x0, x0, 1, 16
	str	w1, [x21, 12]
	add	w1, w1, 1
	str	w1, [x19, 2668]
	ldr	x1, [x19, 80]
	ldrh	w2, [x21, 4]
	strh	w2, [x1, x0]
	add	x0, x19, 88
	strh	wzr, [x19, 90]
	ldrh	w1, [x21]
	mov	w21, -1
	strb	wzr, [x19, 94]
	add	w1, w1, 1
	strh	w1, [x19, 88]
	mov	w1, 1
	strb	w1, [x19, 96]
	add	x19, x20, :lo12:.LANCHOR0
	mov	x20, x0
.L1757:
	mov	x0, x20
	bl	make_superblock
	ldrb	w1, [x20, 7]
	ldrh	w0, [x20]
	cbnz	w1, .L1758
	ldr	x1, [x19, 80]
	ubfiz	x0, x0, 1, 16
	strh	w21, [x1, x0]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L1757
.L1758:
	ldr	w1, [x19, 2668]
	ubfiz	x0, x0, 1, 16
	str	w1, [x20, 12]
	add	w1, w1, 1
	str	w1, [x19, 2668]
	ldr	x1, [x19, 80]
	ldrh	w2, [x20, 4]
	mov	w20, -1
	strh	w2, [x1, x0]
	strh	w20, [x19, 136]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19, 2704]
	ldr	w0, [x19, 3980]
	strh	w0, [x19, 2710]
	ldr	w0, [x19, 2668]
	str	w0, [x19, 2712]
	add	w0, w0, 1
	strh	wzr, [x19, 2706]
	strh	w20, [x19, 2708]
	str	w0, [x19, 2668]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L1759
	adrp	x0, .LANCHOR2
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR2]
.L1759:
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
	adrp	x1, .LC0
	add	x1, x1, :lo12:.LC0
	mov	w0, -1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	w0, [x20, #:lo12:.LANCHOR2]
	adrp	x0, .LC77
	add	x0, x0, :lo12:.LC77
	bl	sftl_printk
	mov	x0, x19
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x19, 248]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	w0, .L1768
	bl	FtlSysBlkInit
	cbnz	w0, .L1768
	mov	w0, 1
	str	w0, [x20, #:lo12:.LANCHOR2]
	ldrh	w0, [x19, 236]
	cmp	w0, 15
	bhi	.L1768
	mov	w19, 8129
.L1767:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L1767
.L1768:
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
	bls	.L1772
	mov	w19, w0
	cmp	w0, 575
	bls	.L1773
.L1772:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x0, 3984]
	cbnz	w1, .L1774
.L1840:
	mov	w20, 0
	b	.L1771
.L1774:
	ldr	x26, [x0, 3992]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	ldr	w2, [x26]
	cmp	w2, w1
	bne	.L1776
	ldrh	w1, [x0, 10]
	mov	x0, 262140
	mov	w20, 65535
	add	x2, x26, x0
	mov	w3, 4097
	mov	w0, 0
.L1780:
	ldr	w4, [x2]
	cbnz	w4, .L1777
	ldr	w4, [x26, w0, uxtw 2]
	add	w0, w0, 1
	str	w4, [x2], -4
	cmp	w0, w3
	sub	w20, w20, #1
	csel	w0, w0, wzr, cc
	cmp	w20, 4096
	bne	.L1780
	mov	w20, 512
	b	.L1779
.L1777:
	add	w20, w20, 127
	lsr	w20, w20, 7
.L1779:
	ubfiz	w0, w1, 2, 14
	add	w22, w20, 4
	add	x19, x21, :lo12:.LANCHOR0
	mov	w23, 0
	str	wzr, [x29, 140]
	udiv	w22, w22, w0
	lsl	w0, w20, 7
	str	w0, [x29, 128]
	add	w22, w22, 1
.L1802:
	ldr	x0, [x19, 4000]
	mov	w1, 0
	mov	x2, 512
	bl	memset
	ldrh	w28, [x19, 10]
	ldr	x1, [x19, 3400]
	mul	w24, w28, w23
	cbz	x1, .L1810
	ldr	x0, [x19, 3408]
	mov	w25, 6
	cmp	x0, 0
	mov	w0, 9
	csel	w25, w25, w0, eq
.L1781:
	ldr	x3, [x19, 3376]
	mov	w1, w24
	mov	w0, 0
	blr	x3
	cmp	w22, 1
	beq	.L1782
	ldr	x2, [x19, 3376]
	add	w1, w28, w24
	mov	w0, 0
	blr	x2
.L1782:
	cmp	w25, 9
	bne	.L1812
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
.L1783:
	mul	w0, w22, w28
	mov	x6, x26
	str	w0, [x29, 136]
	mov	x28, 0
	add	w0, w24, 1
	str	w0, [x29, 132]
.L1784:
	ldr	w0, [x29, 136]
	mov	w1, w28
	cmp	w28, w0
	bcs	.L1791
	cmp	w25, 9
	bne	.L1785
	ldr	w0, [x29, 132]
	add	w0, w0, w1
.L1838:
	str	w0, [x29, 144]
	mov	w0, 61424
	str	w0, [x29, 148]
	cmp	w1, 0
	ccmp	w25, 9, 0, eq
	bne	.L1787
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
	bne	.L1788
.L1791:
	ldrb	w0, [x19, 14]
	str	w0, [x29, 120]
	ldrh	w0, [x19, 10]
	ldr	x25, [x19, 4000]
	mul	w1, w0, w23
	str	w1, [x29, 136]
	add	x1, x19, 3368
	ldr	x2, [x1, 32]
	cbnz	x2, .L1789
	mov	w1, 6
.L1839:
	mul	w0, w22, w0
	mov	x28, 0
	mov	w27, 61424
	str	w0, [x29, 112]
	str	w1, [x29, 132]
.L1793:
	ldr	w0, [x29, 112]
	mov	w1, w28
	cmp	w28, w0
	bcs	.L1797
	ldr	w0, [x29, 132]
	cmp	w28, 0
	ccmp	w0, 9, 0, eq
	bne	.L1794
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
	beq	.L1795
.L1797:
	ldr	x0, [x19, 4000]
	mov	x1, x26
	mov	w24, 0
	mov	x2, x0
.L1796:
	mov	x25, x2
	mov	x28, x1
	add	x2, x2, 4
	add	x1, x1, 4
	ldr	w4, [x25]
	ldr	w3, [x28]
	cmp	w4, w3
	beq	.L1799
	mov	x2, 512
	mov	w1, 0
	bl	memset
	ldr	w2, [x25]
	mov	w1, w23
	ldr	w3, [x28]
	mov	w4, w24
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
	bl	sftl_printk
	ldrh	w1, [x19, 10]
	mov	w0, 0
	ldr	x2, [x19, 3376]
	mul	w1, w1, w23
	blr	x2
.L1800:
	add	w23, w23, w22
	cmp	w23, 15
	bls	.L1802
	ldr	w0, [x29, 140]
	cbnz	w0, .L1803
.L1776:
	mov	w20, -1
.L1803:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21, 3992]
	str	wzr, [x21, 3984]
	bl	kfree
	ldr	x0, [x21, 4000]
	bl	kfree
.L1771:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1810:
	mov	w25, 6
	b	.L1781
.L1812:
	mov	x27, 0
	b	.L1783
.L1785:
	add	w0, w1, w24
	lsl	w0, w0, 2
	b	.L1838
.L1787:
	ldr	x7, [x19, 3384]
	add	x3, x29, 144
	mov	x2, x6
	add	w1, w1, w24
	mov	w0, 0
	str	x6, [x29, 120]
	blr	x7
	cmn	w0, #1
	beq	.L1791
	ldr	x6, [x29, 120]
	add	x6, x6, 2048
.L1788:
	add	x28, x28, 1
	b	.L1784
.L1789:
	ldr	x1, [x1, 40]
	mov	w24, 6
	cmp	x1, 0
	mov	w1, 9
	csel	w1, w24, w1, eq
	b	.L1839
.L1794:
	ldr	x6, [x19, 3392]
	add	x3, x29, 144
	ldr	w0, [x29, 136]
	mov	x2, x25
	add	w1, w1, w0
	mov	w0, 0
	blr	x6
	cmn	w0, #1
	beq	.L1797
	ldr	w0, [x29, 148]
	cmp	w0, w27
	bne	.L1797
	add	x25, x25, 2048
.L1795:
	add	x28, x28, 1
	b	.L1793
.L1799:
	ldr	w3, [x29, 128]
	add	w24, w24, 1
	cmp	w24, w3
	bne	.L1796
	ldr	w0, [x29, 140]
	add	w0, w0, 1
	str	w0, [x29, 140]
	cmp	w0, 5
	bls	.L1800
	b	.L1803
.L1773:
	mov	w22, w1
	mov	x23, x2
	cmp	w0, 64
	bne	.L1804
	add	x20, x21, :lo12:.LANCHOR0
	mov	w0, 262144
	bl	ftl_malloc
	str	x0, [x20, 3992]
	mov	w0, 262144
	bl	ftl_malloc
	mov	x1, x0
	ldr	x0, [x20, 3992]
	str	x1, [x20, 4000]
	cbz	x0, .L1805
	cbz	x1, .L1805
	mov	w1, 1
	mov	w2, 262144
	str	w1, [x20, 3984]
	mov	w1, 0
	bl	ftl_memset
.L1804:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	w0, [x21, 3984]
	cbz	w0, .L1840
	cmp	w19, 63
	ldr	x1, [x21, 3992]
	bhi	.L1807
	mov	w0, 64
	sub	w19, w0, w19
	sub	w22, w22, w19
	ubfiz	x19, x19, 9, 25
	add	x23, x23, x19
	mov	x20, x1
.L1808:
	cmp	w25, 575
	bls	.L1809
	sub	w22, w22, w24
	sub	w22, w22, #446
.L1809:
	mov	w4, w22
	mov	x3, x20
	mov	w2, 262144
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	bl	sftl_printk
	lsl	w2, w22, 9
	mov	x1, x23
	mov	x0, x20
	bl	ftl_memcpy
	b	.L1840
.L1805:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 752
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	bl	sftl_printk
	b	.L1804
.L1807:
	lsl	w20, w19, 7
	sub	w20, w20, #8192
	add	x20, x1, x20, lsl 2
	b	.L1808
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
	cbnz	x3, .L1845
	str	x0, [x2, 4008]
	mov	w0, 0
	str	x1, [x2, 4016]
	ret
.L1845:
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
	cbz	x0, .L1852
	mov	w26, 22084
	mov	w25, 0
	mov	w24, 0
	mov	w23, 0
	movk	w26, 0x524b, lsl 16
.L1850:
	ldr	x3, [x22, 4008]
	mov	w1, 128
	ldr	x2, [x22, 4024]
	lsl	w0, w23, 7
	blr	x3
	mov	w21, w0
	cbnz	w0, .L1848
	ldr	x19, [x22, 4024]
	ldr	w0, [x19]
	cmp	w0, w26
	bne	.L1849
	add	x0, x19, 61440
	ldr	w1, [x19, 4]
	ldr	w0, [x0, 4092]
	cmp	w0, w1
	bne	.L1849
	cmp	w0, w24
	bls	.L1849
	mov	w25, w23
	mov	w24, w0
.L1849:
	add	w23, w23, 1
	cmp	w23, 2
	bne	.L1850
	cbz	w24, .L1851
	add	x0, x20, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w1, 128
	ldr	x3, [x0, 4008]
	lsl	w0, w25, 7
	blr	x3
	mov	w21, w0
	cbz	w0, .L1846
.L1848:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w21, -1
	ldr	x0, [x20, 4024]
	bl	kfree
	str	xzr, [x20, 4024]
	b	.L1846
.L1851:
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
.L1846:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1852:
	mov	w21, -12
	b	.L1846
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	adrp	x3, .LANCHOR0+4024
	ldr	x4, [x3, #:lo12:.LANCHOR0+4024]
	cbz	x4, .L1860
	ldrh	w6, [x4, 10]
	add	x5, x4, 16
	mov	w3, 0
.L1857:
	cmp	w3, w6
	bcc	.L1859
.L1860:
	mov	w0, -1
	ret
.L1859:
	ldrh	w7, [x5], 8
	cmp	w7, w0
	bne	.L1858
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
.L1858:
	add	w3, w3, 1
	b	.L1857
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
	cbz	x19, .L1880
	add	w21, w2, 63
	ldrh	w3, [x19, 10]
	ldrh	w24, [x19, 8]
	mov	x4, x1
	mov	w28, w2
	and	w21, w21, -64
	add	x1, x19, 16
	mov	w20, 0
.L1867:
	cmp	w20, w3
	bcc	.L1875
	ldrh	w1, [x19, 14]
	cmp	w21, w1
	bhi	.L1880
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
	b	.L1882
.L1875:
	ldrh	w6, [x1], 8
	cmp	w6, w0
	bne	.L1868
	uxtw	x27, w20
	add	x5, x19, 1024
	add	x22, x19, x27, lsl 3
	ldrh	w25, [x22, 20]
	add	w25, w25, 63
	and	w25, w25, -64
	cmp	w28, w25
	bls	.L1869
	ldrh	w0, [x19, 14]
	cmp	w21, w0
	bhi	.L1880
	add	x27, x27, 2
	ldrh	w22, [x22, 18]
	add	x27, x19, x27, lsl 3
	sub	w26, w3, #1
.L1870:
	cmp	w20, w26
	bcc	.L1871
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
.L1882:
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
.L1865:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1871:
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
	b	.L1870
.L1869:
	ldrh	w0, [x22, 18]
	uxtw	x2, w28
	mov	x1, x4
	add	x0, x5, x0
	bl	memcpy
	strh	w28, [x22, 20]
	b	.L1882
.L1868:
	add	w20, w20, 1
	b	.L1867
.L1880:
	mov	w0, -1
	b	.L1865
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
	cbz	x0, .L1900
	mov	w1, 30209
	mov	x19, x0
	movk	w1, 0x4004, lsl 16
	cmp	w21, w1
	beq	.L1886
	add	w1, w1, 1
	cmp	w21, w1
	beq	.L1887
.L1910:
	mov	x20, -14
	b	.L1885
.L1886:
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
	cbz	x3, .L1888
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbz	x0, .L1911
.L1889:
	mov	x2, x20
	mov	w1, 256
.L1915:
	adrp	x0, .LC133
	add	x0, x0, :lo12:.LC133
	bl	sftl_printk
	b	.L1910
.L1888:
	str	xzr, [x0]
	b	.L1889
.L1911:
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L1891
.L1892:
	mov	x20, -1
.L1885:
	mov	x0, x19
	bl	kfree
.L1883:
	mov	x0, x20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1891:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_read
	cmn	w0, #1
	beq	.L1892
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
	cbz	x3, .L1910
	mov	x0, x20
	mov	x1, x19
	mov	x20, 0
	bl	__arch_copy_to_user
	cbz	x0, .L1885
	b	.L1910
.L1887:
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
	cbz	x3, .L1894
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbz	x0, .L1912
.L1895:
	mov	x2, x20
	mov	w1, 276
	b	.L1915
.L1894:
	str	xzr, [x0]
	b	.L1895
.L1912:
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L1892
	ldrh	w2, [x19, 6]
	cmp	w2, 4087
	bhi	.L1892
	add	w2, w2, 8
	mov	x0, x20
	sxtw	x2, w2
	ldr	x1, [x21, 8]
#APP
// 413 "./arch/arm64/include/asm/uaccess.h" 1
	adds x0, x0, x2; ccmp x0, x1, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1897
	mov	x1, x20
	mov	x0, x19
	bl	__arch_copy_from_user
	cbz	x0, .L1913
.L1898:
	mov	x2, x20
	mov	w1, 283
	b	.L1915
.L1897:
	mov	w1, 0
	mov	x0, x19
	bl	memset
	b	.L1898
.L1913:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_write
	sxtw	x20, w0
	b	.L1885
.L1900:
	mov	x20, -1
	b	.L1883
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
	.type	__func__.3803, %object
	.size	__func__.3803, 17
__func__.3803:
	.string	"INSERT_DATA_LIST"
	.zero	7
	.type	__func__.3798, %object
	.size	__func__.3798, 17
__func__.3798:
	.string	"INSERT_FREE_LIST"
	.zero	7
	.type	__func__.3834, %object
	.size	__func__.3834, 17
__func__.3834:
	.string	"List_remove_node"
	.zero	7
	.type	__func__.3866, %object
	.size	__func__.3866, 22
__func__.3866:
	.string	"List_update_data_list"
	.zero	2
	.type	__func__.3973, %object
	.size	__func__.3973, 22
__func__.3973:
	.string	"select_l2p_ram_region"
	.zero	2
	.type	__func__.4300, %object
	.size	__func__.4300, 16
__func__.4300:
	.string	"make_superblock"
	.type	__func__.4475, %object
	.size	__func__.4475, 19
__func__.4475:
	.string	"get_new_active_ppa"
	.zero	5
	.type	__func__.16146, %object
	.size	__func__.16146, 17
__func__.16146:
	.string	"FlashEraseBlocks"
	.zero	7
	.type	__func__.3908, %object
	.size	__func__.3908, 26
__func__.3908:
	.string	"ftl_map_blk_alloc_new_blk"
	.zero	6
	.type	__func__.3413, %object
	.size	__func__.3413, 11
__func__.3413:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.3640, %object
	.size	__func__.3640, 14
__func__.3640:
	.string	"FtlBbt2Bitmap"
	.zero	2
	.type	__func__.4321, %object
	.size	__func__.4321, 18
__func__.4321:
	.string	"SupperBlkListInit"
	.zero	6
	.type	__func__.16103, %object
	.size	__func__.16103, 15
__func__.16103:
	.string	"FlashReadPages"
	.zero	1
	.type	__func__.4085, %object
	.size	__func__.4085, 14
__func__.4085:
	.string	"FtlScanSysBlk"
	.zero	2
	.type	__func__.3683, %object
	.size	__func__.3683, 11
__func__.3683:
	.string	"FtlLoadBbt"
	.zero	5
	.type	__func__.16122, %object
	.size	__func__.16122, 15
__func__.16122:
	.string	"FlashProgPages"
	.zero	1
	.type	__func__.4447, %object
	.size	__func__.4447, 25
__func__.4447:
	.string	"allocate_data_superblock"
	.zero	7
	.type	__func__.4488, %object
	.size	__func__.4488, 16
__func__.4488:
	.string	"update_vpc_list"
	.type	__func__.4495, %object
	.size	__func__.4495, 20
__func__.4495:
	.string	"decrement_vpc_count"
	.zero	4
	.type	__func__.3934, %object
	.size	__func__.3934, 31
__func__.3934:
	.string	"Ftl_write_map_blk_to_last_page"
	.zero	1
	.type	__func__.3948, %object
	.size	__func__.3948, 16
__func__.3948:
	.string	"FtlMapWritePage"
	.type	__func__.3875, %object
	.size	__func__.3875, 16
__func__.3875:
	.string	"load_l2p_region"
	.type	__func__.3919, %object
	.size	__func__.3919, 15
__func__.3919:
	.string	"ftl_map_blk_gc"
	.zero	1
	.type	__func__.3990, %object
	.size	__func__.3990, 9
__func__.3990:
	.string	"log2phys"
	.zero	7
	.type	__func__.4209, %object
	.size	__func__.4209, 16
__func__.4209:
	.string	"FtlReUsePrevPpa"
	.type	__func__.4243, %object
	.size	__func__.4243, 22
__func__.4243:
	.string	"FtlRecoverySuperblock"
	.zero	2
	.type	__func__.4362, %object
	.size	__func__.4362, 14
__func__.4362:
	.string	"ftl_check_vpc"
	.zero	2
	.type	__func__.4063, %object
	.size	__func__.4063, 15
__func__.4063:
	.string	"FtlVpcTblFlush"
	.zero	1
	.type	__func__.4346, %object
	.size	__func__.4346, 21
__func__.4346:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.4468, %object
	.size	__func__.4468, 29
__func__.4468:
	.string	"allocate_new_data_superblock"
	.zero	3
	.type	__func__.3537, %object
	.size	__func__.3537, 13
__func__.3537:
	.string	"FtlProgPages"
	.zero	3
	.type	__func__.4565, %object
	.size	__func__.4565, 19
__func__.4565:
	.string	"FtlGcFreeTempBlock"
	.zero	5
	.type	__func__.4678, %object
	.size	__func__.4678, 23
__func__.4678:
	.string	"rk_ftl_garbage_collect"
	.zero	1
	.type	__func__.3565, %object
	.size	__func__.3565, 9
__func__.3565:
	.string	"FtlWrite"
	.zero	7
	.type	__func__.4141, %object
	.size	__func__.4141, 15
__func__.4141:
	.string	"FtlLoadSysInfo"
	.zero	1
	.type	__func__.4163, %object
	.size	__func__.4163, 18
__func__.4163:
	.string	"FtlMapTblRecovery"
	.zero	6
	.type	__func__.16261, %object
	.size	__func__.16261, 14
__func__.16261:
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
	.xword	.LC134
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
	.type	g_active_superblock, %object
	.size	g_active_superblock, 48
g_active_superblock:
	.zero	48
	.type	p_valid_page_count_table, %object
	.size	p_valid_page_count_table, 8
p_valid_page_count_table:
	.zero	8
	.type	g_buffer_superblock, %object
	.size	g_buffer_superblock, 48
g_buffer_superblock:
	.zero	48
	.type	g_gc_temp_superblock, %object
	.size	g_gc_temp_superblock, 48
g_gc_temp_superblock:
	.zero	48
	.type	g_gc_superblock, %object
	.size	g_gc_superblock, 48
g_gc_superblock:
	.zero	48
	.type	g_gc_next_blk_1, %object
	.size	g_gc_next_blk_1, 2
g_gc_next_blk_1:
	.zero	2
	.type	g_gc_next_blk, %object
	.size	g_gc_next_blk, 2
g_gc_next_blk:
	.zero	2
	.type	g_num_free_superblocks, %object
	.size	g_num_free_superblocks, 2
g_num_free_superblocks:
	.zero	2
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
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 8
p_erase_count_table:
	.zero	8
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
	.string	"SFTL version: 5.0.50 20190215"
.LC1:
	.string	"\n%s\n"
.LC2:
	.string	"act blk: %x %x %x %x %x %x\n"
.LC3:
	.string	"buf blk: %x %x %x %x %x %x\n"
.LC4:
	.string	"tmp blk: %x %x %x %x %x %x\n"
.LC5:
	.string	"gc blk: %x %x %x %x %x %x\n"
.LC6:
	.string	"free blk: %x %x %x\n"
.LC7:
	.string	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\n"
.LC8:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC9:
	.string	"FLASH INFO:\n"
.LC10:
	.string	"Device Capacity: %d MB\n"
.LC11:
	.string	"FTL INFO:\n"
.LC12:
	.string	"g_MaxLpn = 0x%x\n"
.LC13:
	.string	"g_VaildLpn = 0x%x\n"
.LC14:
	.string	"read_page_count = 0x%x\n"
.LC15:
	.string	"discard_page_count = 0x%x\n"
.LC16:
	.string	"write_page_count = 0x%x\n"
.LC17:
	.string	"cache_write_count = 0x%x\n"
.LC18:
	.string	"l2p_write_count = 0x%x\n"
.LC19:
	.string	"gc_page_count = 0x%x\n"
.LC20:
	.string	"totle_write = %d MB\n"
.LC21:
	.string	"totle_read = %d MB\n"
.LC22:
	.string	"GSV = 0x%x\n"
.LC23:
	.string	"GDV = 0x%x\n"
.LC24:
	.string	"bad blk num = %d\n"
.LC25:
	.string	"free_superblocks = 0x%x\n"
.LC26:
	.string	"mlc_EC = 0x%x\n"
.LC27:
	.string	"slc_EC = 0x%x\n"
.LC28:
	.string	"avg_EC = 0x%x\n"
.LC29:
	.string	"sys_EC = 0x%x\n"
.LC30:
	.string	"max_EC = 0x%x\n"
.LC31:
	.string	"min_EC = 0x%x\n"
.LC32:
	.string	"PLT = 0x%x\n"
.LC33:
	.string	"POT = 0x%x\n"
.LC34:
	.string	"MaxSector = 0x%x\n"
.LC35:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC36:
	.string	"sys_blks_pp = 0x%x\n"
.LC37:
	.string	"free sysblock = 0x%x\n"
.LC38:
	.string	"data_blks_pp = 0x%x\n"
.LC39:
	.string	"data_op_blks_pp = 0x%x\n"
.LC40:
	.string	"max_data_blks = 0x%x\n"
.LC41:
	.string	"Sys.id = 0x%x\n"
.LC42:
	.string	"Bbt.id = 0x%x\n"
.LC43:
	.string	"ACT.page = 0x%x\n"
.LC44:
	.string	"ACT.plane = 0x%x\n"
.LC45:
	.string	"ACT.id = 0x%x\n"
.LC46:
	.string	"ACT.mode = 0x%x\n"
.LC47:
	.string	"ACT.a_pages = 0x%x\n"
.LC48:
	.string	"ACT VPC = 0x%x\n"
.LC49:
	.string	"BUF.page = 0x%x\n"
.LC50:
	.string	"BUF.plane = 0x%x\n"
.LC51:
	.string	"BUF.id = 0x%x\n"
.LC52:
	.string	"BUF.mode = 0x%x\n"
.LC53:
	.string	"BUF.a_pages = 0x%x\n"
.LC54:
	.string	"BUF VPC = 0x%x\n"
.LC55:
	.string	"TMP.page = 0x%x\n"
.LC56:
	.string	"TMP.plane = 0x%x\n"
.LC57:
	.string	"TMP.id = 0x%x\n"
.LC58:
	.string	"TMP.mode = 0x%x\n"
.LC59:
	.string	"TMP.a_pages = 0x%x\n"
.LC60:
	.string	"GC.page = 0x%x\n"
.LC61:
	.string	"GC.plane = 0x%x\n"
.LC62:
	.string	"GC.id = 0x%x\n"
.LC63:
	.string	"GC.mode = 0x%x\n"
.LC64:
	.string	"GC.a_pages = 0x%x\n"
.LC65:
	.string	"WR_CHK = %x %x %x\n"
.LC66:
	.string	"Read Err Cnt = 0x%x\n"
.LC67:
	.string	"Prog Err Cnt = 0x%x\n"
.LC68:
	.string	"gc_free_blk_th= 0x%x\n"
.LC69:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC70:
	.string	"gc_skip_write_count= 0x%x\n"
.LC71:
	.string	"gc_blk_index= 0x%x\n"
.LC72:
	.string	"free min EC= 0x%x\n"
.LC73:
	.string	"free max EC= 0x%x\n"
.LC74:
	.string	"GC__SB VPC = 0x%x\n"
.LC75:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC76:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC77:
	.string	"%s\n"
.LC78:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC79:
	.string	"FtlGcRefreshBlock  0x%x\n"
.LC80:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
.LC81:
	.string	"%s %p + 0x%x:"
.LC82:
	.string	"0x%08x,"
.LC83:
	.string	"0x%04x,"
.LC84:
	.string	"0x%02x,"
.LC85:
	.string	"\n"
.LC86:
	.string	"%s: addr: %x is in id block!!!!!!!!!!\n"
.LC87:
	.string	"not free: w: d:"
.LC88:
	.string	"not free: w: s:"
.LC89:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
.LC90:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
.LC91:
	.string	"%s error allocating memory. return -1\n"
.LC92:
	.string	"FlashReadPages %x %x error_ecc_bits %d\n"
.LC93:
	.string	"data:"
.LC94:
	.string	"spare:"
.LC95:
	.string	"prog read error: = %x\n"
.LC96:
	.string	"prog read REFRESH: = %x\n"
.LC97:
	.string	"prog read s error: = %x %x %x\n"
.LC98:
	.string	"prog read d error: = %x %x %x\n"
.LC99:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC100:
	.string	"FtlBbmTblFlush error:%x\n"
.LC101:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC102:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC103:
	.string	"decrement_vpc_count %x = %d\n"
.LC104:
	.string	"FtlMapWritePage error = %x \n"
.LC105:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC106:
	.string	"region_id = %x phyAddr = %x\n"
.LC107:
	.string	"map_ppn:"
.LC108:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC109:
	.string	"data prev_ppa = %x error...................\n"
.LC110:
	.string	"spuer block %x vpn is 0\n "
.LC111:
	.string	"...%s enter...\n"
.LC112:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC113:
	.string	"free blk vpc error %x = %x  %x\n"
.LC114:
	.string	"ftl_scan_all_data = %x\n"
.LC115:
	.string	"scan lpa = %x ppa= %x\n"
.LC116:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC117:
	.string	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
.LC118:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC119:
	.string	"no ect"
.LC120:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC121:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC122:
	.string	"FtlProgPages error %x = %d\n"
.LC123:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC124:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC125:
	.string	"FtlWrite: ecc error:%x %x %x\n"
.LC126:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC127:
	.string	"%s hash error this.id =%x page =%x pre_id =%x hash =%x hash_r =%x\n"
.LC128:
	.string	"%s last blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
.LC129:
	.string	"%s scan blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
.LC130:
	.string	"write_idblock fail! %x %x %x %x\n"
.LC131:
	.string	"%s idb buffer alloc fail\n"
.LC132:
	.string	"%p %x %p %x\n"
.LC133:
	.string	"copy_from_user error %d %p %p\n"
.LC134:
	.string	"vendor_storage"
