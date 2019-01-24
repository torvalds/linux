/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2019-02-20
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
	strh	w3, [x1, 264]
	strh	w6, [x1, 266]
	strh	w0, [x1, 268]
	strh	w2, [x1, 254]
	strh	w5, [x1, 270]
	bne	.L12
	lsr	w2, w2, 1
	strh	w2, [x1, 254]
	mov	w2, 8
	strh	w2, [x1, 270]
.L12:
	add	x2, x4, :lo12:.LANCHOR0
	mov	x1, 0
	add	x5, x2, 272
.L13:
	strb	w1, [x1, x5]
	add	x1, x1, 1
	cmp	x1, 32
	bne	.L13
	mov	w1, 5
	strh	wzr, [x2, 306]
	strh	w1, [x2, 304]
	cmp	w3, 1
	bne	.L14
	strh	w3, [x2, 304]
.L14:
	add	x3, x4, :lo12:.LANCHOR0
	mul	w6, w6, w0
	mov	w1, 640
	and	w6, w6, 65535
	ldrh	w5, [x3, 254]
	strh	w1, [x3, 308]
	strh	w6, [x3, 244]
	mul	w0, w0, w5
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
	bls	.L15
.L21:
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
	beq	.L17
.L22:
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
.L15:
	mov	w0, 4
	b	.L21
.L17:
	add	w5, w5, 4
	b	.L22
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w0, w0, 65535
	ldrh	w2, [x1, 356]
	cbz	w2, .L27
	ldrh	w2, [x1, 332]
	ldr	x3, [x1, 360]
	mov	x1, 0
.L25:
	cmp	w2, w1, uxth
	bhi	.L26
.L27:
	mov	w0, 0
	ret
.L26:
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L25
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
	blt	.L45
	mov	w0, w5
	ret
.L45:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
.L38:
	mov	w0, w4
	bl	FtlBbmIsBadBlock
	cbz	w0, .L37
	add	w5, w5, 1
	and	w5, w5, 65535
.L37:
	add	w4, w4, 1
	and	w4, w4, 65535
	cmp	w4, w6
	blt	.L38
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
	bls	.L50
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	ret
.L50:
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
	cbz	w3, .L54
	ldrh	w5, [x2, 2548]
	mov	w6, 0
	ldrh	w3, [x1, 2]
	mov	w4, 0
	ldrh	w2, [x1, 4]
	and	w5, w5, 31
.L56:
	cmp	w5, w4
	bgt	.L57
	cbz	w6, .L54
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0, 466]
	strh	w2, [x0, 468]
.L54:
	ret
.L57:
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
	b	.L56
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
	beq	.L67
	ldrh	w5, [x1, 2]
	mov	w3, 0
.L65:
	cmp	w3, w4
	bcc	.L66
.L67:
	mov	w0, 0
	ret
.L66:
	add	w2, w3, w5
	ubfiz	x2, x2, 1, 10
	add	x2, x1, x2
	ldrh	w2, [x2, 8]
	cmp	w2, w0
	beq	.L68
	add	w3, w3, 1
	b	.L65
.L68:
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
	bls	.L71
	mov	w6, 6
	ldr	x8, [x3, 2568]
	mov	w1, -1
	umull	x7, w0, w6
	add	x13, x8, x7
	strh	w1, [x13, 2]
	strh	w1, [x8, x7]
	ldr	x1, [x3, 2576]
	cbnz	x1, .L72
	str	x13, [x3, 2576]
.L71:
	mov	w0, 0
	ret
.L72:
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
.L77:
	add	w3, w3, 1
	and	w3, w3, 65535
	cmp	w3, w14
	bhi	.L71
	cmp	w0, w2
	beq	.L71
	ldrh	w12, [x1, 4]
	cbz	w12, .L75
	ubfiz	x9, x2, 1, 16
	ldrh	w9, [x11, x9]
	mul	w9, w9, w12
	cmp	w9, w5
	bcs	.L75
	ldrh	w9, [x1]
	cmp	w9, w15
	bne	.L76
	strh	w2, [x13, 2]
	add	x2, x4, :lo12:.LANCHOR0
	strh	w0, [x1]
	str	x13, [x2, 2584]
	b	.L71
.L76:
	umaddl	x1, w9, w6, x10
	mov	w2, w9
	b	.L77
.L75:
	strh	w2, [x8, x7]
	ldrh	w2, [x1, 2]
	strh	w2, [x13, 2]
	add	x2, x4, :lo12:.LANCHOR0
	ldr	x3, [x2, 2576]
	cmp	x1, x3
	bne	.L78
	strh	w0, [x1, 2]
	str	x13, [x2, 2576]
	b	.L71
.L78:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2568]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L71
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
	bcs	.L83
	adrp	x1, .LANCHOR1
	adrp	x0, .LC8
	mov	w2, 214
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L83:
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
	beq	.L87
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
	cbnz	x1, .L88
	str	x4, [x5, 2600]
.L87:
	mov	w0, 0
	ret
.L88:
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
.L91:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11, x5]
	cmp	w5, w12
	bcs	.L89
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L90
	strh	w2, [x4, 2]
	strh	w0, [x1]
	b	.L87
.L90:
	umaddl	x1, w5, w6, x10
	mov	w2, w5
	b	.L91
.L89:
	ldrh	w5, [x1, 2]
	strh	w5, [x4, 2]
	strh	w2, [x9, x8]
	add	x2, x3, :lo12:.LANCHOR0
	ldr	x3, [x2, 2600]
	cmp	x1, x3
	bne	.L92
	strh	w0, [x1, 2]
	str	x4, [x2, 2600]
	b	.L87
.L92:
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2568]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	strh	w0, [x1, 2]
	b	.L87
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
	bcs	.L93
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 207
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L93:
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
	bne	.L97
	ldr	x0, [x23]
	cmp	x20, x0
	beq	.L97
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 372
	add	x1, x1, 48
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L97:
	ldr	x0, [x23]
	mov	w1, 65535
	cmp	x20, x0
	ldrh	w0, [x22, x21]
	bne	.L98
	cmp	w0, w1
	bne	.L99
	str	xzr, [x23]
.L100:
	mov	w0, -1
	strh	w0, [x22, x21]
	strh	w0, [x20, 2]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L99:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 6
	ldr	x1, [x19, 2568]
	umaddl	x0, w0, w2, x1
	mov	w1, -1
	str	x0, [x23]
	strh	w1, [x0, 2]
	b	.L100
.L98:
	cmp	w0, w1
	ldrh	w1, [x20, 2]
	bne	.L101
	cmp	w1, w0
	beq	.L100
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 6
	mov	w2, -1
	umull	x1, w1, w0
	ldr	x0, [x19, 2568]
	strh	w2, [x0, x1]
	b	.L100
.L101:
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
	b	.L100
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
	ldr	x2, [x0]
	cbz	x2, .L109
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LANCHOR0+2568
	and	w1, w1, 65535
	mov	w4, 65535
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	w5, 6
	ldr	x19, [x3, #:lo12:.LANCHOR0+2568]
.L105:
	cbnz	w1, .L106
.L108:
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
.L106:
	ldrh	w3, [x2]
	cmp	w3, w4
	beq	.L108
	sub	w1, w1, #1
	umaddl	x2, w3, w5, x19
	and	w1, w1, 65535
	b	.L105
.L109:
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
	cbz	x1, .L121
	ldr	x3, [x0, 2568]
	mov	w4, 6
	mov	w0, 65535
.L118:
	cbz	w2, .L119
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L120
	ret
.L120:
	sub	w2, w2, #1
	umaddl	x1, w1, w4, x3
	and	w2, w2, 65535
	b	.L118
.L121:
	mov	w0, 65535
	ret
.L119:
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
	beq	.L124
	ldrh	w1, [x0, 88]
	cmp	w1, w20
	beq	.L124
	ldrh	w1, [x0, 136]
	cmp	w1, w20
	beq	.L124
	mov	w22, 6
	ldr	x24, [x0, 2568]
	ldr	x1, [x0, 2576]
	umull	x22, w20, w22
	add	x23, x24, x22
	cmp	x23, x1
	beq	.L124
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
	bne	.L127
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	bne	.L127
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 463
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L127:
	ldrh	w0, [x23, 2]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L128
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	beq	.L124
.L128:
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
	bcs	.L124
	mov	w1, w20
	add	x0, x22, 2576
	bl	List_remove_node
	ldrh	w0, [x22, 2592]
	cbnz	w0, .L130
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 474
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L130:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2592]
	sub	w0, w0, #1
	strh	w0, [x19, 2592]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L124:
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
.L135:
	and	w19, w3, 65535
	cmp	w19, w2
	bcc	.L137
	add	x4, x0, 4
	mov	w19, w2
	mov	w6, -2147483648
	mov	w3, 0
.L138:
	cmp	w3, w2
	bne	.L140
	cmp	w19, w2
	bcc	.L136
	add	x1, x1, :lo12:.LANCHOR0
	mov	w19, w2
	mov	w3, -1
	ldrh	w4, [x1, 2624]
	mov	w1, 0
.L141:
	cmp	w1, w2
	bne	.L143
	cmp	w19, w1
	bcc	.L136
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 789
	add	x1, x1, 96
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L136
.L137:
	add	x3, x3, 1
	add	x5, x0, x3, lsl 4
	ldrh	w5, [x5, -16]
	cmp	w5, w4
	bne	.L135
.L136:
	mov	w0, w19
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L140:
	ldr	w5, [x4]
	tbnz	w5, #31, .L139
	cmp	w6, w5
	bls	.L139
	mov	w6, w5
	mov	w19, w3
.L139:
	add	w3, w3, 1
	add	x4, x4, 16
	and	w3, w3, 65535
	b	.L138
.L143:
	ldr	w6, [x0, 4]
	cmp	w3, w6
	bls	.L142
	ldrh	w5, [x0]
	cmp	w5, w4
	csel	w3, w3, w6, eq
	csel	w19, w19, w1, eq
.L142:
	add	w1, w1, 1
	add	x0, x0, 16
	and	w1, w1, 65535
	b	.L141
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR0
	add	x3, x1, :lo12:.LANCHOR0
	ldrh	w2, [x3, 2626]
	cmp	w2, 4
	bhi	.L146
	cbnz	w0, .L146
	add	w2, w2, 1
	strh	w2, [x3, 2626]
	ret
.L146:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w6, 65535
	mov	x1, 0
	ldrh	w4, [x0, 252]
	ldr	x5, [x0, 80]
	strh	wzr, [x0, 2626]
	str	wzr, [x0, 2628]
.L147:
	cmp	w4, w1, uxth
	bhi	.L149
	ret
.L149:
	ldrh	w3, [x5, x1, lsl 1]
	cmp	w3, w6
	beq	.L148
	ldr	w2, [x0, 2628]
	add	w2, w2, w3
	str	w2, [x0, 2628]
.L148:
	add	x1, x1, 1
	b	.L147
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	and	w0, w0, 65535
	cbz	w1, .L152
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_set_blk_mode.part.7
	ldp	x29, x30, [sp], 16
	ret
.L152:
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
.L160:
	cmp	w3, w2, uxth
	bhi	.L162
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
.L163:
	cmp	x2, x3
	bne	.L165
	ret
.L162:
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5, 16]
	cmp	w5, w1
	beq	.L161
	ldrh	w5, [x0, 4]
	add	w5, w5, 1
	strh	w5, [x0, 4]
.L161:
	add	w2, w2, 1
	b	.L160
.L165:
	ldrh	w4, [x2]
	cmp	w4, w5
	beq	.L164
	ldrh	w4, [x0, 4]
	add	w4, w1, w4
	strh	w4, [x0, 4]
.L164:
	add	x2, x2, 2
	b	.L163
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	ldrh	w1, [x0, 4]
	cbz	w1, .L166
	ldrh	w2, [x0]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L166
	ldrb	w2, [x0, 6]
	add	x2, x2, 8
	ldrh	w3, [x0, x2, lsl 1]
	adrp	x2, .LANCHOR0+244
	ldrh	w4, [x2, #:lo12:.LANCHOR0+244]
	mov	w2, w1
.L169:
	cmp	w3, w2
	beq	.L171
.L166:
	ret
.L171:
	ldrb	w1, [x0, 6]
	add	w1, w1, 1
	and	w1, w1, 255
	strb	w1, [x0, 6]
	cmp	w1, w4
	bne	.L170
	ldrh	w1, [x0, 2]
	strb	wzr, [x0, 6]
	add	w1, w1, 1
	strh	w1, [x0, 2]
.L170:
	ldrb	w1, [x0, 6]
	add	x1, x1, 8
	ldrh	w3, [x0, x1, lsl 1]
	b	.L169
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
	bcc	.L173
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2148
	add	x1, x1, 120
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L173:
	add	x0, x20, :lo12:.LANCHOR0
	add	x6, x19, 16
	add	x7, x0, 272
	strh	wzr, [x19, 4]
	strb	wzr, [x19, 7]
	mov	x5, 0
	ldrh	w8, [x0, 244]
	mov	w9, -1
.L174:
	cmp	w8, w5, uxth
	bhi	.L176
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
	bls	.L177
	mov	w0, 1
	strb	w0, [x19, 9]
.L177:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L176:
	ldrh	w1, [x19]
	ldrb	w0, [x7, x5]
	bl	V2P_block
	mov	w4, w0
	strh	w9, [x6]
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L175
	strh	w4, [x6]
	ldrb	w0, [x19, 7]
	add	w0, w0, 1
	strb	w0, [x19, 7]
.L175:
	add	x5, x5, 1
	add	x6, x6, 2
	b	.L174
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
	bhi	.L191
	cbz	w6, .L189
	mov	w0, 32768
	sdiv	w6, w0, w6
.L190:
	add	x0, x5, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x0, [x0, 2568]
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
	cbz	x0, .L194
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
	ldr	x1, [x4, 2600]
	cbz	x1, .L202
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
	ldr	x0, [x2, 2608]
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
	ldrh	w0, [x21, 184]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L206
	ldr	x1, [x21, 80]
	ubfiz	x0, x0, 1, 16
	adrp	x2, .LC74
	add	x2, x2, :lo12:.LC74
	ldrh	w3, [x1, x0]
	mov	x0, x19
	mov	x1, 64
	bl	snprintf
	add	x19, x19, x0, sxtw
.L206:
	mov	w0, 0
	adrp	x24, .LC75
	bl	List_get_gc_head_node
	add	x21, x23, :lo12:.LANCHOR0
	and	w4, w0, 65535
	add	x24, x24, :lo12:.LC75
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
.L208:
	cmp	w4, w27
	beq	.L207
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
	bne	.L208
.L207:
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
.L210:
	cmp	w4, w25
	beq	.L209
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
	beq	.L209
	ldr	x0, [x23, 2568]
	ldrh	w4, [x0, x24]
	b	.L210
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	cmp	w0, 2047
	ble	.L218
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
.L218:
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
	bcs	.L224
	ldrh	w2, [x0, 252]
	mov	x1, 0
	ldr	x4, [x0, 2608]
	str	wzr, [x0, 2676]
.L225:
	cmp	w2, w1
	bhi	.L226
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w0, [x1, 2676]
	udiv	w2, w0, w2
	str	w2, [x1, 2684]
	ldr	w2, [x1, 2680]
	sub	w0, w0, w2
	ldrh	w2, [x1, 304]
	udiv	w0, w0, w2
	str	w0, [x1, 2676]
.L227:
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w9, [x1, 2696]
	ldr	w11, [x1, 2684]
	add	w0, w9, 256
	cmp	w0, w11
	bls	.L232
	ldr	w1, [x1, 2692]
	add	w0, w9, 768
	cmp	w0, w1
	bls	.L232
.L234:
	mov	w20, 65535
.L233:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L226:
	ldrh	w3, [x4, x1, lsl 1]
	add	x1, x1, 1
	ldr	w5, [x0, 2676]
	add	w3, w3, w5
	str	w3, [x0, 2676]
	b	.L225
.L224:
	ldr	w1, [x0, 2692]
	cmp	w2, w1
	bls	.L227
	add	w1, w1, 1
	str	w1, [x0, 2692]
	mov	w1, 0
.L229:
	ldrh	w2, [x0, 252]
	cmp	w1, w2
	bcs	.L227
	ldr	x4, [x0, 2608]
	ubfiz	x3, x1, 1, 32
	add	w1, w1, 1
	ldrh	w2, [x4, x3]
	add	w2, w2, 1
	strh	w2, [x4, x3]
	b	.L229
.L232:
	add	x10, x8, :lo12:.LANCHOR0
	ldrh	w0, [x10, 236]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	and	w6, w0, 65535
	add	w0, w9, 64
	cmp	w6, w0
	bcc	.L234
	ldr	x0, [x10, 2576]
	cbz	x0, .L234
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
.L235:
	ldrh	w3, [x0]
	cmp	w3, w10
	bne	.L238
	mov	w20, w2
.L237:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L234
	ubfiz	x7, x20, 1, 32
	ldrh	w3, [x5, x7]
	cmp	w9, w3
	bcs	.L239
	bl	GetFreeBlockMinEraseCount
	cmp	w9, w0, uxth
	bcs	.L239
	add	x0, x8, :lo12:.LANCHOR0
	str	w4, [x0, 2696]
.L239:
	cmp	w11, w3
	bls	.L234
	add	w0, w3, 128
	cmp	w6, w0
	ble	.L234
	add	w0, w3, 256
	cmp	w11, w0
	bhi	.L240
	add	x0, x8, :lo12:.LANCHOR0
	add	w3, w3, 768
	ldr	w0, [x0, 2692]
	cmp	w3, w0
	bcs	.L234
.L240:
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
	b	.L233
.L238:
	add	w1, w1, 1
	and	w1, w1, 65535
	cmp	w1, w12
	bhi	.L234
	ldrh	w15, [x0, 4]
	cbz	w15, .L236
	sub	x0, x0, x7
	asr	x0, x0, 1
	mul	x0, x0, x13
	and	w20, w0, 65535
	and	x0, x0, 65535
	ldrh	w0, [x5, x0, lsl 1]
	cmp	w9, w0
	bcs	.L237
	cmp	w4, w0
	bls	.L236
	mov	w4, w0
	mov	w2, w20
.L236:
	umaddl	x0, w3, w14, x7
	b	.L235
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
	and	w0, w0, 65535
	mov	w1, 65535
	cmp	w0, w1
	beq	.L251
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
.L251:
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
	bne	.L255
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2781
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L255:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x0, 314]
	cmp	w1, w0
	bne	.L256
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2782
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L256:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L257
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2783
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L257:
	ldrb	w0, [x19, 6]
	add	x1, x20, :lo12:.LANCHOR0
	strb	wzr, [x19, 10]
	add	x0, x0, 8
	ldrh	w3, [x1, 244]
	mov	w1, 65535
	ldrh	w0, [x19, x0, lsl 1]
.L258:
	cmp	w0, w1
	beq	.L260
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w2, [x20, 314]
	cmp	w1, w2
	bcs	.L264
	orr	w20, w1, w0, lsl 10
	ldrh	w1, [x19, 4]
	ldrb	w0, [x19, 6]
	mov	w4, 65535
	sub	w1, w1, #1
	and	w1, w1, 65535
	strh	w1, [x19, 4]
.L263:
	add	w0, w0, 1
	and	w0, w0, 255
	cmp	w3, w0
	bne	.L262
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	w0, 0
.L262:
	add	x5, x19, x0, sxtw 1
	ldrh	w5, [x5, 16]
	cmp	w5, w4
	beq	.L263
	strb	w0, [x19, 6]
	ldrh	w0, [x19, 2]
	cmp	w0, w2
	bne	.L254
	cbz	w1, .L254
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2806
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L254:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L260:
	ldrb	w0, [x19, 6]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	cmp	w0, w3
	bne	.L259
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L259:
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
	b	.L258
.L264:
	mov	w20, 65535
	b	.L254
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
	ldrh	w2, [x1, 244]
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
	b	.L271
.L274:
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
	adrp	x0, .LC79
	add	x0, x0, :lo12:.LC79
	bl	sftl_printk
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w3, [x0, 234]
	cmp	w19, w3
	beq	.L300
	ldrh	w2, [x0, 232]
	cmp	w19, w2
	beq	.L300
	mov	w1, 65535
	cmp	w3, w1
	bne	.L301
	strh	w19, [x0, 234]
.L300:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L301:
	cmp	w2, w1
	bne	.L300
	strh	w19, [x0, 232]
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
.L304:
	cmp	w0, w1, uxth
	bhi	.L306
	cmp	w0, 15
	bhi	.L305
	add	x19, x19, :lo12:.LANCHOR0
	add	w1, w0, 1
	add	x19, x19, 3328
	strh	w1, [x19, -8]
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
	ldrh	w1, [x0, 3320]
	cbz	w1, .L309
	ldrh	w3, [x0, 234]
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
.L320:
	cmp	w26, w19
	bhi	.L326
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
	adrp	x0, .LC82
	add	x0, x0, :lo12:.LC82
.L328:
	bl	sftl_printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L325
	mov	w20, 0
	adrp	x0, .LC85
	add	x0, x0, :lo12:.LC85
	bl	sftl_printk
.L325:
	add	x19, x19, 1
	add	w22, w22, w23
	b	.L320
.L322:
	cmp	w23, 2
	bne	.L324
	ldrh	w1, [x21, x19, lsl 1]
	add	x0, x28, :lo12:.LC83
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
	adrp	x21, .LC86
	add	x20, x20, 160
	add	x21, x21, :lo12:.LC86
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
	adrp	x0, .LC87
	add	x0, x0, :lo12:.LC87
	bl	rknand_print_hex
	ldr	x1, [x19, -16]
	mov	w3, 4
	adrp	x0, .LC88
	mov	w2, w3
	add	x0, x0, :lo12:.LC88
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
	ldrh	w2, [x20, 470]
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
	ldr	x2, [x20, 2608]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x20, 2688]
	add	w0, w0, 1
	str	w0, [x20, 2688]
.L347:
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
	ldrh	w2, [x21, 470]
	cbz	w2, .L359
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
.L356:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L357
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrh	w2, [x0, 470]
	adrp	x0, .LC89
	add	x0, x0, :lo12:.LC89
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
	adrp	x0, .LANCHOR0+470
	ldrh	w2, [x0, #:lo12:.LANCHOR0+470]
	adrp	x0, .LC90
	add	x0, x0, :lo12:.LC90
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
	adrp	x0, .LC8
	mov	w2, 578
	add	x1, x1, 184
	add	x0, x0, :lo12:.LC8
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
.L373:
	cmp	w1, w6
	bcc	.L374
	mov	w2, 8
	add	x4, x0, :lo12:.LANCHOR0
	sub	w2, w2, w1
	add	x4, x4, 368
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
	adrp	x0, .LC91
	add	x0, x0, :lo12:.LC91
	bl	sftl_printk
	mov	w0, -1
.L372:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L374:
	ldr	x3, [x19, 400]
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
	ldr	x2, [x1, 2616]
	cbz	x2, .L379
	ldr	x2, [x1, 3632]
	cbz	x2, .L379
	ldr	x2, [x1, 2568]
	cbz	x2, .L379
	ldr	x2, [x1, 400]
	cbz	x2, .L379
	ldr	x1, [x1, 80]
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
	ldr	x1, [x0, 2608]
	cbz	x1, .L379
	ldr	x1, [x0, 3552]
	cbz	x1, .L379
	ldr	x0, [x0, 360]
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
.L477:
	ldrh	w0, [x23, x19]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L474
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w1, [x1, 310]
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
.L486:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L490
	adrp	x0, .LANCHOR0+316
	mov	w23, 0
	mov	w20, 0
	ldrh	w1, [x0, #:lo12:.LANCHOR0+316]
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
.L507:
	ldrh	w2, [x0, 350]
	cmp	w2, w1
	bhi	.L508
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
.L508:
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
.L513:
	ldrh	w0, [x19, 252]
	cmp	w20, w0
	bcs	.L520
	ldrh	w8, [x19, 244]
	mov	x6, 0
	ldrh	w7, [x19, 314]
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
	ldr	x1, [x19, 2568]
	mov	w0, 6
	umaddl	x0, w20, w0, x1
	strh	w5, [x0, 4]
	ldrh	w0, [x19, 32]
	cmp	w0, w20
	beq	.L518
	ldrh	w0, [x19, 88]
	cmp	w0, w20
	beq	.L518
	ldrh	w0, [x19, 136]
	cmp	w0, w20
	beq	.L518
	ldr	x1, [x19, 80]
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
	ldr	x1, [x19, 80]
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
	strh	w21, [x19, 2592]
	add	w21, w21, w22
	strh	w22, [x19, 236]
	cmp	w21, w0
	ble	.L522
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2210
	add	x1, x1, 248
	add	x0, x0, :lo12:.LC8
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
	adrp	x24, .LC8
	mov	x19, x0
	add	x23, x0, x1
	ldrh	w25, [x2, 12]
	add	x22, x22, 272
	add	x24, x24, :lo12:.LC8
.L536:
	cmp	x23, x19
	bne	.L549
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L549:
	ldr	x0, [x19, 8]
	cbz	x0, .L537
	ldr	x0, [x19, 16]
	cbnz	x0, .L538
.L537:
	mov	w2, 96
	mov	x1, x22
	mov	x0, x24
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
	ldr	x1, [x19, 16]
	ldr	w2, [x1, 12]
	cmn	w2, #1
	bne	.L545
	ldr	w2, [x1, 8]
	cmn	w2, #1
	bne	.L545
	ldr	w1, [x1]
	cmn	w1, #1
	beq	.L545
.L544:
	mov	w1, -1
	str	w1, [x19]
.L545:
	ldr	w1, [x19]
	cmn	w1, #1
	beq	.L546
	cmp	w0, 256
	bne	.L546
	str	w0, [x19]
.L546:
	ldr	w3, [x19]
	cmp	w3, 256
	ccmn	w3, #1, 4, ne
	bne	.L543
	ldr	w1, [x19, 4]
	adrp	x0, .LC92
	ldr	w2, [x29, 108]
	add	x0, x0, :lo12:.LC92
	bl	sftl_printk
	ldr	x1, [x19, 8]
	cbz	x1, .L548
	mov	w3, 4
	adrp	x0, .LC93
	mov	w2, w3
	add	x0, x0, :lo12:.LC93
	bl	rknand_print_hex
.L548:
	ldr	x1, [x19, 16]
	cbz	x1, .L543
	mov	w3, 4
	adrp	x0, .LC94
	mov	w2, w3
	add	x0, x0, :lo12:.LC94
	bl	rknand_print_hex
.L543:
	add	x1, x21, :lo12:.LANCHOR0
	ldr	x0, [x1, 3504]
	cmp	x20, x0
	bne	.L540
	ldr	x0, [x19, 8]
	cmp	x20, x0
	beq	.L540
	ldrh	w2, [x1, 270]
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
.L576:
	ldrh	w0, [x19, 266]
	cmp	w21, w0
	bcc	.L581
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L581:
	ldrh	w20, [x19, 310]
	mov	w0, -1
	strh	w0, [x22]
.L578:
	ldrh	w0, [x19, 310]
	sub	w20, w20, #1
	and	w20, w20, 65535
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L579
	madd	w0, w0, w21, w20
	mov	w2, 1
	mov	w1, w2
	lsl	w0, w0, 10
	str	w0, [x25, 4]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L578
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L578
	strh	w20, [x22]
.L579:
	add	w21, w21, 1
	add	x22, x22, 2
	b	.L576
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
	bne	.L587
	ldrh	w19, [x2, 316]
.L588:
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
	bne	.L589
	mov	w22, 0
	mov	w24, 2
.L590:
	cmp	w22, w19
	ble	.L593
.L589:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 160
	ret
.L587:
	ldrh	w19, [x2, 314]
	b	.L588
.L593:
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
	bne	.L591
	ldr	w0, [x29, 100]
	cmn	w0, #1
	bne	.L591
	ldr	w0, [x29, 64]
	cmn	w0, #1
	beq	.L591
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L590
.L591:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L590
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
.L599:
	ldrh	w0, [x19, 254]
	cmp	w0, w24
	bls	.L639
	ldrh	w9, [x19, 244]
	mov	x5, 0
	ldrh	w8, [x19, 324]
	mov	w20, 0
	add	x7, x19, 272
	mov	w6, 4
	b	.L640
.L601:
	ldrb	w0, [x7, x5]
	mov	w1, w24
	bl	V2P_block
	and	w4, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L600
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
.L600:
	add	x5, x5, 1
.L640:
	cmp	w9, w5, uxth
	bhi	.L601
	cbnz	w20, .L602
.L638:
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L599
.L602:
	ldr	x0, [x19, 3440]
	mov	w1, w20
	mov	w2, 1
	ubfiz	x20, x20, 5, 16
	mov	x23, 0
	add	x27, x19, 2704
	bl	FlashReadPages
.L637:
	ldr	x0, [x19, 3440]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	ldr	w22, [x1, 4]
	cmn	w0, #1
	ldr	x26, [x1, 16]
	ubfx	x22, x22, 10, 16
	bne	.L605
	mov	w28, 16
	mov	w3, 65535
.L607:
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
	bne	.L604
	mov	w1, -1
	str	w1, [x0, x23]
	ldr	x0, [x19, 3440]
	ldr	w0, [x0, x23]
	cmp	w0, w1
	bne	.L605
.L606:
	mov	w1, 1
	b	.L677
.L604:
	ldr	w0, [x0, x23]
	cmn	w0, #1
	bne	.L605
	sub	w28, w28, #1
	ands	w28, w28, 65535
	bne	.L607
	b	.L606
.L605:
	ldr	w1, [x19, 2668]
	ldr	w0, [x26, 4]
	cmn	w1, #1
	beq	.L608
	cmp	w1, w0
	bhi	.L609
.L608:
	cmn	w0, #1
	beq	.L609
	add	w1, w0, 1
	str	w1, [x19, 2668]
.L609:
	ldrh	w1, [x26]
	mov	w2, 61604
	cmp	w1, w2
	beq	.L611
	bhi	.L612
	mov	w0, 61574
	cmp	w1, w0
	beq	.L613
.L610:
	add	x23, x23, 32
	cmp	x20, x23
	bne	.L637
	b	.L638
.L612:
	mov	w0, 61634
	cmp	w1, w0
	beq	.L614
	mov	w0, 65535
	cmp	w1, w0
	bne	.L610
	mov	w1, 0
.L677:
	mov	w0, w22
	bl	FtlFreeSysBlkQueueIn
	b	.L610
.L614:
	ldrh	w1, [x19, 3712]
	ldr	w0, [x19, 340]
	cmp	w1, w0
	bls	.L616
	adrp	x0, .LC8
	mov	w2, 1225
	mov	x1, x25
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L616:
	ldr	w3, [x19, 340]
	ldrh	w1, [x19, 3712]
	and	w2, w3, 65535
	ldr	x4, [x19, 3624]
	sub	w0, w2, #1
	sub	w2, w2, w1
	sub	w2, w2, #1
	sxth	x0, w0
	sxth	w2, w2
.L617:
	cmp	w0, w2
	bgt	.L623
	tbz	w0, #31, .L657
	b	.L610
.L623:
	sxtw	x5, w0
	ldr	w8, [x26, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L618
	ldr	w2, [x4]
	cbnz	w2, .L619
	cmp	w3, w1
	beq	.L619
	add	w1, w1, 1
	strh	w1, [x19, 3712]
.L619:
	mov	w1, 0
.L620:
	cmp	w1, w0
	bne	.L621
	ldr	x1, [x19, 3624]
	ldr	w2, [x26, 4]
	str	w2, [x1, x6]
	ldr	x1, [x19, 3576]
	strh	w22, [x1, x5, lsl 1]
	tbnz	w0, #31, .L610
	ldrh	w1, [x19, 3712]
	ldr	w2, [x19, 340]
	sub	w2, w2, w1
	sub	w2, w2, #1
	cmp	w0, w2, sxth
	bgt	.L610
.L657:
	add	w1, w1, 1
	strh	w1, [x19, 3712]
	ldr	x1, [x19, 3624]
	ldr	w2, [x26, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 3576]
.L675:
	strh	w22, [x1, x0, lsl 1]
	b	.L610
.L621:
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
	b	.L620
.L618:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L617
.L613:
	ldrh	w1, [x19, 356]
	ldrh	w0, [x19, 332]
	cmp	w1, w0
	bls	.L626
	adrp	x0, .LC8
	mov	w2, 1266
	mov	x1, x25
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L626:
	ldrh	w3, [x19, 332]
	ldrh	w1, [x19, 356]
	sub	w2, w3, #1
	ldr	x4, [x19, 3600]
	sxth	x0, w2
	sub	w2, w2, w1
.L627:
	cmp	w0, w2
	ble	.L632
	sxtw	x5, w0
	ldr	w8, [x26, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L628
	ldr	w2, [x4]
	cbnz	w2, .L629
	cmp	w3, w1
	beq	.L629
	add	w1, w1, 1
	strh	w1, [x19, 356]
.L629:
	mov	w1, 0
.L630:
	cmp	w1, w0
	bne	.L631
	ldr	x1, [x19, 3600]
	ldr	w2, [x26, 4]
	str	w2, [x1, x6]
	ldr	x1, [x19, 360]
	strh	w22, [x1, x5, lsl 1]
.L632:
	tbnz	w0, #31, .L610
	ldrh	w1, [x19, 332]
	ldrh	w2, [x19, 356]
	sub	w1, w1, #1
	sub	w1, w1, w2
	cmp	w0, w1, sxth
	bgt	.L610
	ldr	x1, [x19, 3600]
	add	w2, w2, 1
	strh	w2, [x19, 356]
	ldr	w2, [x26, 4]
	str	w2, [x1, x0, lsl 2]
	ldr	x1, [x19, 360]
	b	.L675
.L631:
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
	b	.L630
.L628:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L627
.L611:
	ldrh	w2, [x27]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L634
	strh	w22, [x27]
.L676:
	str	w0, [x27, 8]
	b	.L610
.L634:
	ldrh	w0, [x27, 4]
	cmp	w0, w1
	beq	.L635
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L635:
	ldr	w0, [x26, 4]
	ldr	w1, [x27, 8]
	cmp	w1, w0
	bcs	.L636
	ldrh	w0, [x27]
	strh	w0, [x27, 4]
	strh	w22, [x27]
	ldr	w0, [x26, 4]
	b	.L676
.L636:
	strh	w22, [x27, 4]
	b	.L610
.L639:
	ldr	x1, [x19, 3576]
	ldrh	w0, [x1]
	cbz	w0, .L641
.L644:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0, 360]
	ldrh	w2, [x1]
	cbz	w2, .L642
.L643:
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w1, [x21, 3712]
	ldr	w0, [x21, 340]
	cmp	w1, w0
	bls	.L673
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1391
	add	x1, x1, 288
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L673:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L641:
	ldrh	w0, [x19, 3712]
	cbz	w0, .L644
	ldr	w2, [x19, 340]
	mov	w0, 0
.L645:
	cmp	w0, w2
	bcs	.L644
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L646
	mov	w1, w0
	add	x2, x21, :lo12:.LANCHOR0
.L647:
	ldr	w3, [x2, 340]
	cmp	w1, w3
	bcs	.L644
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
	b	.L647
.L646:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L645
.L642:
	ldrh	w2, [x0, 356]
	cbz	w2, .L643
	ldrh	w2, [x0, 332]
	mov	w0, 0
.L652:
	mov	w6, w0
	cmp	w0, w2
	bge	.L643
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L653
	add	x1, x21, :lo12:.LANCHOR0
.L654:
	ldrh	w2, [x1, 332]
	cmp	w0, w2
	bge	.L643
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
	b	.L654
.L653:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L652
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
.L679:
	ldrh	w0, [x23, 310]
	sub	w0, w0, #16
	cmp	w20, w0
	ble	.L682
	lsl	w0, w20, 10
	mov	w2, 1
	str	w0, [x21, 4]
	mov	w1, w2
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L680
	ldr	w0, [x21, 4]
	mov	w2, 1
	mov	w1, w2
	add	w0, w0, 1
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
.L680:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L681
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L681
	ldr	w1, [x22, 4]
	str	w1, [x23, 376]
	strh	w20, [x23, 368]
	ldrh	w1, [x22, 8]
	strh	w1, [x23, 372]
.L682:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21, 368]
	cmp	w1, w0
	beq	.L696
	ldrh	w1, [x21, 372]
	cmp	w1, w0
	beq	.L686
	add	x0, x21, 3728
	lsl	w1, w1, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x21, 3728]
	cmn	w0, #1
	beq	.L686
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L686
	ldr	w1, [x21, 376]
	ldr	w0, [x22, 4]
	cmp	w0, w1
	bls	.L686
	str	w0, [x21, 376]
	ldrh	w1, [x21, 372]
	ldrh	w0, [x22, 8]
	strh	w1, [x21, 368]
	strh	w0, [x21, 372]
.L686:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x23, x20, 3728
	mov	w24, 61649
	ldrh	w0, [x20, 368]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20, 370]
.L688:
	tbz	w21, #31, .L691
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 251
	add	x1, x1, 304
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L690:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 10]
	mov	w2, 65535
	strh	w0, [x1, 374]
	ldrh	w0, [x22, 12]
	cmp	w0, w2
	beq	.L693
	ldr	w2, [x1, 240]
	cmp	w0, w2
	beq	.L693
	ldrh	w1, [x1, 254]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L693
	cmp	w0, w1
	bcs	.L693
	bl	FtlSysBlkNumInit
.L693:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 0
	add	x21, x19, 400
.L694:
	ldrh	w0, [x19, 266]
	cmp	w20, w0
	bcc	.L695
	mov	w0, 0
.L678:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L681:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L679
.L691:
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
	beq	.L689
	ldrh	w0, [x22]
	cmp	w0, w24
	beq	.L690
.L689:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L688
.L695:
	ldrh	w2, [x19, 3640]
	ldr	x0, [x19, 3736]
	mul	w1, w2, w20
	lsl	w2, w2, 2
	add	w20, w20, 1
	add	x1, x0, x1, lsl 2
	ldr	x0, [x21], 8
	bl	ftl_memcpy
	b	.L694
.L696:
	mov	w0, -1
	b	.L678
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
.L709:
	cmp	x20, x22
	bne	.L723
	cbz	w27, .L717
	adrp	x20, .LC95
	adrp	x23, .LC96
	add	x20, x20, :lo12:.LC95
	add	x23, x23, :lo12:.LC96
	b	.L745
.L723:
	ldr	x0, [x20, 8]
	cbz	x0, .L710
	ldr	x0, [x20, 16]
	cbnz	x0, .L711
.L710:
	mov	w2, 148
	mov	x1, x23
	mov	x0, x24
	bl	sftl_printk
.L711:
	ldr	w0, [x20, 4]
	add	x2, x29, 120
	add	x1, x29, 124
	bl	l2p_addr_tran.isra.0
	ldr	w0, [x29, 120]
	cmp	w0, 3
	bls	.L712
.L747:
	mov	w0, -1
	str	w0, [x20]
	b	.L713
.L712:
	cbnz	w0, .L714
	ldr	w0, [x29, 124]
	cmp	w28, w0
	bls	.L714
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x21, .LC86
	add	x20, x20, 320
	add	x21, x21, :lo12:.LC86
	b	.L744
.L716:
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
.L744:
	cmp	x19, x22
	bne	.L716
	bl	dump_stack
.L717:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L714:
	ldr	x1, [x20, 8]
	tst	x1, 63
	beq	.L731
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x26, [x0, 3504]
	cmp	x1, x26
	beq	.L718
	ldrh	w2, [x0, 270]
	mov	x0, x26
	lsl	w2, w2, 9
	bl	ftl_memcpy
.L718:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29, 124]
	ldr	x3, [x20, 16]
	mov	x2, x26
	ldr	x5, [x0, 3384]
	ldrb	w0, [x29, 120]
	blr	x5
	cbnz	w0, .L719
	str	wzr, [x20]
.L720:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0, 14]
	cmp	w1, 4
	bne	.L713
	ldr	x5, [x0, 3384]
	add	x2, x26, 2048
	ldrb	w0, [x29, 120]
	ldr	w1, [x29, 124]
	ldr	x3, [x20, 16]
	add	w1, w25, w1
	add	x3, x3, 8
	blr	x5
	cbnz	w0, .L747
.L713:
	add	x20, x20, 32
	b	.L709
.L731:
	mov	x26, x1
	b	.L718
.L719:
	mov	w0, -1
	str	w0, [x20]
	b	.L720
.L729:
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
	bne	.L725
	ldr	w1, [x19, 4]
	mov	x0, x20
	bl	sftl_printk
	str	w24, [x19]
.L725:
	ldr	w24, [x29, 128]
	cmp	w24, 256
	bne	.L726
	ldr	w1, [x19, 4]
	mov	x0, x23
	bl	sftl_printk
	str	w24, [x19]
.L726:
	ldr	x0, [x19, 16]
	cbz	x0, .L727
	ldr	w2, [x0]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0, 3520]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L727
	ldr	w1, [x19, 4]
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x19]
.L727:
	ldr	x0, [x19, 8]
	cbz	x0, .L728
	ldr	w2, [x0]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0, 3504]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L728
	ldr	w1, [x19, 4]
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x19]
.L728:
	add	x19, x19, 32
.L745:
	cmp	x19, x22
	bne	.L729
	b	.L717
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
.L749:
	ldrh	w0, [x5, 244]
	cmp	w0, w10
	bhi	.L753
	cbz	w20, .L748
	ldr	x0, [x5, 3416]
	mov	w2, w20
	mov	w1, 0
	ubfiz	x20, x20, 5, 16
	mov	x24, 0
	add	x25, x21, :lo12:.LANCHOR0
	bl	FlashEraseBlocks
.L757:
	ldr	x0, [x25, 3416]
	add	x1, x0, x24
	ldr	w0, [x0, x24]
	cmn	w0, #1
	bne	.L756
	ldr	w0, [x1, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.L756:
	add	x24, x24, 32
	cmp	x24, x20
	bne	.L757
	cbz	w22, .L770
	add	x0, x21, :lo12:.LANCHOR0
	mov	w25, 1
	ldrh	w26, [x0, 316]
.L758:
	add	x28, x21, :lo12:.LANCHOR0
	mov	w24, 0
	add	x0, x28, 272
	str	x0, [x29, 104]
.L766:
	mov	w5, 0
	mov	w20, 0
.L759:
	ldrh	w0, [x28, 244]
	cmp	w0, w5
	bhi	.L762
	cbz	w20, .L748
	ldr	x0, [x28, 3416]
	mov	w1, w20
	mov	w3, 1
	mov	w2, w25
	mov	x27, 0
	bl	FlashProgPages
	ubfiz	x1, x20, 5, 16
.L765:
	ldr	x0, [x28, 3416]
	add	x3, x0, x27
	ldr	w0, [x0, x27]
	cbz	w0, .L764
	ldr	w0, [x3, 4]
	add	w19, w19, 1
	str	x1, [x29, 96]
	and	w19, w19, 65535
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29, 96]
.L764:
	add	x27, x27, 32
	cmp	x1, x27
	bne	.L765
	add	w24, w24, 1
	cmp	w26, w24, uxth
	bhi	.L766
	add	x26, x21, :lo12:.LANCHOR0
	mov	x24, 0
.L768:
	cbz	w22, .L767
	ldr	x0, [x26, 3416]
	add	x1, x0, x24
	ldr	w0, [x0, x24]
	cbnz	w0, .L767
	ldr	w0, [x1, 4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L767:
	add	x24, x24, 32
	cmp	x24, x27
	bne	.L768
	cmp	w23, 63
	ccmp	w22, 0, 0, hi
	beq	.L748
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, w20
	mov	w1, w25
	ldr	x0, [x21, 3416]
	bl	FlashEraseBlocks
.L748:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L753:
	ldr	x1, [x5, 3416]
	ubfiz	x0, x10, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w23
	ldrb	w0, [x8, w10, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w22, .L750
	bl	IsBlkInVendorPart
	cbnz	w0, .L751
.L750:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L752
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
.L751:
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L749
.L752:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L751
.L770:
	mov	w25, 0
	mov	w26, 2
	b	.L758
.L762:
	ldr	x1, [x28, 3416]
	ubfiz	x0, x5, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w23
	ldr	x0, [x29, 104]
	ldrb	w0, [x0, w5, sxtw]
	bl	V2P_block
	and	w6, w0, 65535
	mov	w7, w6
	cbz	w22, .L760
	bl	IsBlkInVendorPart
	cbnz	w0, .L761
.L760:
	mov	w0, w7
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L761
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
.L761:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L759
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
	bls	.L794
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
	cbnz	w0, .L795
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
.L793:
	mov	w2, 1
	mov	w1, 0
	add	x0, x29, 32
	str	w19, [x29, 36]
	bl	FlashEraseBlocks
.L791:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 128
	ret
.L795:
	mov	w20, -1
	b	.L793
.L794:
	mov	w20, 0
	b	.L791
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
.L798:
	ldrh	w0, [x20, 266]
	add	x1, x20, 3728
	cmp	w19, w0
	blt	.L799
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
.L800:
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
	blt	.L801
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
.L801:
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
	bne	.L802
	ldr	w1, [x25, 3732]
	add	w21, w21, 1
	add	x0, x24, :lo12:.LC100
	and	w21, w21, 65535
	bl	sftl_printk
	cmp	w21, 3
	bls	.L800
	ldr	w1, [x25, 3732]
	adrp	x0, .LC101
	mov	w2, w21
	add	x0, x0, :lo12:.LC101
	bl	sftl_printk
.L804:
	b	.L804
.L799:
	ldrh	w2, [x20, 3640]
	ldr	x0, [x1, 8]
	ldr	x1, [x21], 8
	mul	w3, w19, w2
	lsl	w2, w2, 2
	add	w19, w19, 1
	add	x0, x0, x3, sxtw 2
	bl	ftl_memcpy
	b	.L798
.L805:
	mov	w22, 1
	b	.L800
.L802:
	add	w22, w22, 1
	cmp	w22, 1
	ble	.L805
	cmp	w0, 256
	beq	.L800
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
.L811:
	ldrh	w1, [x22, 2592]
	ldrh	w0, [x22, 236]
	add	w0, w0, w1
	ldrh	w1, [x22, 252]
	cmp	w0, w1
	ble	.L812
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2656
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L812:
	cmp	x20, x23
	bne	.L838
	ldrh	w0, [x19, 236]
	ldr	w1, [x19, 3244]
	mul	w1, w0, w1
	lsr	w0, w0, 1
	add	w0, w0, 1
	add	w1, w0, w1, lsr 2
	ands	w1, w1, 65535
	beq	.L813
	sub	w1, w1, #1
	and	w1, w1, 65535
.L813:
	add	x0, x19, 2600
	bl	List_pop_index_node
	and	w25, w0, 65535
	ldrh	w0, [x19, 236]
	cbnz	w0, .L814
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2665
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L814:
	ldrh	w0, [x19, 236]
	sub	w0, w0, #1
	strh	w0, [x19, 236]
	ldrh	w0, [x19, 252]
	cmp	w0, w25
	bls	.L811
	ldr	x0, [x19, 80]
	ubfiz	x26, x25, 1, 16
	ldrh	w24, [x0, x26]
	cbnz	w24, .L811
	strh	w25, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20, 7]
	cbnz	w0, .L816
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
	ble	.L811
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2679
	add	x1, x1, 336
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L811
.L838:
	mov	w1, 0
	b	.L813
.L816:
	ldrh	w1, [x19, 2592]
	ldrh	w0, [x19, 236]
	add	w0, w0, w1
	ldrh	w1, [x19, 252]
	cmp	w0, w1
	ble	.L818
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2682
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L818:
	add	x27, x20, 16
	ldrh	w4, [x19, 244]
	mov	x3, x27
	mov	x0, 0
	mov	w5, 65535
.L819:
	cmp	w4, w0, uxth
	bhi	.L821
	cbnz	w24, .L822
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2693
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L822:
	ldrh	w0, [x19, 184]
	cmp	w0, w25
	bne	.L823
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2695
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L823:
	ldrb	w0, [x20, 8]
	ldr	x1, [x19, 2608]
	cbnz	w0, .L824
	ldrh	w0, [x1, x26]
	cbz	w0, .L825
	ldrh	w2, [x19, 304]
	add	w0, w0, w2
.L846:
	strh	w0, [x1, x26]
	mov	w1, 0
	ldr	w0, [x19, 2676]
	add	w0, w0, 1
	str	w0, [x19, 2676]
	mov	w0, w25
	bl	ftl_set_blk_mode
.L827:
	ldr	x0, [x19, 2608]
	ldr	w1, [x19, 2692]
	ldrh	w0, [x0, x26]
	cmp	w0, w1
	bls	.L828
	str	w0, [x19, 2692]
.L828:
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
.L829:
	cmp	x3, x0
	bne	.L830
	ldrb	w1, [x20, 8]
	mov	w2, w24
	ldr	x0, [x19, 3416]
	mov	x28, 0
	bl	FlashEraseBlocks
	mov	w1, 0
.L831:
	cmp	w24, w28, uxth
	bhi	.L833
	cmp	w1, 0
	ble	.L834
	mov	w0, w25
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L834:
	ldrb	w0, [x20, 7]
	cbnz	w0, .L835
	ldr	x0, [x19, 80]
	mov	w1, -1
	strh	w1, [x0, x26]
	b	.L811
.L821:
	ldr	x1, [x19, 3416]
	add	x2, x1, x0, lsl 5
	stp	xzr, xzr, [x2, 8]
	ldrh	w2, [x3]
	cmp	w2, w5
	beq	.L820
	ubfiz	x6, x24, 5, 16
	add	w24, w24, 1
	add	x1, x1, x6
	and	w24, w24, 65535
	lsl	w2, w2, 10
	str	w2, [x1, 4]
.L820:
	add	x0, x0, 1
	add	x3, x3, 2
	b	.L819
.L825:
	mov	w0, 2
	b	.L846
.L824:
	ldrh	w0, [x1, x26]
	add	w0, w0, 1
	strh	w0, [x1, x26]
	ldr	w0, [x19, 2680]
	add	w0, w0, 1
	str	w0, [x19, 2680]
	mov	w0, w25
	bl	ftl_set_blk_mode.part.7
	b	.L827
.L830:
	ldr	x1, [x19, 3416]
	add	x1, x1, x0
	add	x0, x0, 32
	ldr	w2, [x1, 4]
	and	w2, w2, -1024
	str	w2, [x1, 4]
	b	.L829
.L833:
	ldr	x2, [x19, 3416]
	lsl	x0, x28, 5
	add	x3, x2, x0
	ldr	w2, [x2, x0]
	cmn	w2, #1
	bne	.L832
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
.L832:
	add	x28, x28, 1
	add	x27, x27, 2
	b	.L831
.L835:
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
	cbz	w0, .L836
	ldrb	w0, [x20, 7]
	cbnz	w0, .L837
.L836:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2748
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L837:
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
	cbz	w0, .L848
	add	x23, x19, 272
	add	x24, x19, 3328
	mov	w21, 0
.L849:
	ldrh	w0, [x19, 244]
	cmp	w0, w21
	bhi	.L855
	bl	FtlGcReFreshBadBlk
.L848:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L855:
	ldrb	w0, [x23, w21, sxtw]
	mov	w1, w22
	adrp	x26, .LC102
	add	x26, x26, :lo12:.LC102
	mov	w20, 0
	bl	V2P_block
	and	w25, w0, 65535
.L850:
	ldrh	w0, [x19, 3320]
	cmp	w0, w20
	bhi	.L854
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L849
.L854:
	ldrh	w0, [x24, w20, sxtw 1]
	add	w27, w20, 1
	cmp	w0, w25
	bne	.L851
	mov	w1, w25
	mov	x0, x26
	bl	sftl_printk
	mov	w0, w25
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w0, [x19, 3320]
	mov	w1, w27
.L852:
	cmp	w20, w0
	bcc	.L853
	sub	w0, w0, #1
	strh	w0, [x19, 3320]
.L851:
	and	w20, w27, 65535
	b	.L850
.L853:
	sub	w2, w1, #1
	ldrh	w3, [x24, w1, sxtw 1]
	add	w20, w20, 1
	add	w1, w1, 1
	and	w20, w20, 65535
	strh	w3, [x24, w2, sxtw 1]
	b	.L852
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
	cbnz	w1, .L861
	ldrh	w1, [x0, 234]
	cmp	w1, w20
	bne	.L862
	mov	w1, -1
	strh	w1, [x0, 234]
.L862:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 232]
	cmp	w1, w20
	bne	.L863
	mov	w1, -1
	strh	w1, [x0, 232]
.L863:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 184]
	cmp	w1, w20
	bne	.L864
	mov	w1, -1
	strh	w1, [x0, 184]
.L865:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w1, w20
	add	x0, x21, 2576
	bl	List_remove_node
	ldrh	w0, [x21, 2592]
	cbnz	w0, .L867
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2824
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L867:
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
	ble	.L871
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2827
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L871:
	mov	w0, 1
	b	.L860
.L864:
	ldrh	w1, [x0, 32]
	cmp	w1, w20
	beq	.L870
	ldrh	w1, [x0, 88]
	cmp	w1, w20
	beq	.L870
	ldrh	w0, [x0, 136]
	cmp	w0, w20
	bne	.L865
.L870:
	mov	w0, 0
.L860:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L861:
	mov	w0, w20
	bl	List_update_data_list
	b	.L870
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
	beq	.L874
	add	x22, x19, :lo12:.LANCHOR0
	ubfiz	x21, x20, 1, 16
	ldr	x1, [x22, 80]
	ldrh	w0, [x1, x21]
	cbnz	w0, .L875
	mov	w2, 0
	mov	w1, w20
	adrp	x0, .LC103
	add	x0, x0, :lo12:.LC103
	bl	sftl_printk
	ldr	x0, [x22, 80]
	ldrh	w0, [x0, x21]
	cbz	w0, .L876
.L881:
	mov	w21, 0
.L873:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L876:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2842
	add	x1, x1, 384
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L881
.L875:
	sub	w0, w0, #1
	strh	w0, [x1, x21]
.L874:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldrh	w0, [x19, 3714]
	cmp	w0, w1
	bne	.L878
	strh	w20, [x19, 3714]
	b	.L881
.L878:
	cmp	w20, w0
	beq	.L881
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
	cbnz	w1, .L873
	cmp	w20, w0, uxth
	beq	.L873
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2858
	add	x1, x1, 384
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L873
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
.L888:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L884
.L885:
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
.L884:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29, 68]
	cmn	w0, #1
	beq	.L885
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
	bne	.L888
	b	.L885
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
.L897:
	ldrh	w0, [x19, 266]
	cmp	w24, w0
	bcc	.L903
	add	x20, x21, :lo12:.LANCHOR0
	mov	w19, 0
.L904:
	ldrh	w0, [x20, 326]
	cmp	w0, w19
	bhi	.L905
	add	x20, x20, 368
	ldrh	w19, [x20, 12]
	mov	w22, 65535
	sub	w19, w19, #1
	and	w19, w19, 65535
.L906:
	ldrh	w0, [x20, 12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L910
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L907
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L908
	mov	w0, w19
	bl	FtlBbmMapBadBlock
.L907:
	sub	w19, w19, #1
	and	w19, w19, 65535
	b	.L906
.L903:
	ldr	x0, [x19, 3464]
	mov	w2, 65535
	ldr	x27, [x19, 3528]
	ldrh	w1, [x25]
	stp	x0, x27, [x20, 8]
	cmp	w1, w2
	beq	.L898
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
.L899:
	mov	w0, w23
	add	w24, w24, 1
	bl	FtlBbmMapBadBlock
	add	x26, x26, 8
	add	x25, x25, 2
	b	.L897
.L898:
	mov	w1, w24
	bl	FlashGetBadBlockList
	ldr	x0, [x20, 8]
	ldr	x1, [x26]
	bl	FtlBbt2Bitmap
	ldrh	w22, [x19, 310]
.L901:
	sub	w22, w22, #1
	and	w22, w22, 65535
.L900:
	ldrh	w0, [x19, 310]
	madd	w0, w24, w0, w22
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L901
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
	bne	.L899
	mov	w0, w23
	bl	FtlBbmMapBadBlock
	b	.L900
.L905:
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	and	w19, w19, 65535
	b	.L904
.L908:
	ldrh	w0, [x20]
	cmp	w0, w22
	bne	.L909
	strh	w19, [x20]
	b	.L907
.L909:
	strh	w19, [x20, 4]
.L910:
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
.L919:
	cmp	w1, w3
	bhi	.L920
	ret
.L920:
	lsr	w2, w0, 2
	ldrb	w5, [x4, x3]
	add	w2, w2, w0, lsl 5
	add	x3, x3, 1
	add	w2, w2, w5
	eor	w0, w0, w2
	b	.L919
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
	bne	.L922
	ldrh	w0, [x19, 8]
	cbz	w0, .L923
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 641
	add	x1, x1, 408
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L923:
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
.L924:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L922:
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
.L925:
	ldrh	w2, [x19, 6]
	cmp	w2, w0
	bhi	.L927
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
	b	.L924
.L927:
	ubfiz	x2, x0, 2, 16
	ldr	w3, [x24, x2]
	cmp	w23, w3, lsr 10
	bne	.L926
	add	w1, w1, 1
	ldr	x4, [x20, 3464]
	and	w1, w1, 65535
	ubfiz	x3, x1, 3, 16
	str	w0, [x4, x3]
	ldr	w4, [x24, x2]
	ldr	x2, [x20, 3464]
	add	x2, x2, x3
	str	w4, [x2, 4]
.L926:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L925
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
.L933:
	add	x0, x24, :lo12:.LANCHOR0
	ldr	w1, [x0, 2652]
	add	w1, w1, 1
	str	w1, [x0, 2652]
	ldrh	w0, [x0, 316]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L934
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L935
.L934:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L935:
	ldrh	w1, [x19]
	ldr	x0, [x19, 16]
	ldrh	w0, [x0, x1, lsl 1]
	cbnz	w0, .L936
	adrp	x0, .LC8
	mov	w2, 700
	add	x1, x23, 440
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L936:
	ldrh	w1, [x19]
	ldrh	w0, [x19, 10]
	cmp	w1, w0
	bcc	.L937
	adrp	x0, .LC8
	mov	w2, 701
	add	x1, x23, 440
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L937:
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
	bne	.L938
	ldr	w1, [x20, 4]
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	add	w21, w21, 1
	and	w21, w21, 65535
	bl	sftl_printk
	ldrh	w0, [x19, 2]
	cmp	w0, 2
	bhi	.L939
	ldrh	w0, [x22, 316]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L939:
	cmp	w21, 3
	bls	.L940
	add	x25, x25, :lo12:.LANCHOR0
	adrp	x0, .LC105
	mov	w2, w21
	add	x0, x0, :lo12:.LC105
	ldr	w1, [x25, 3732]
	bl	sftl_printk
.L941:
	b	.L941
.L940:
	ldr	w0, [x19, 52]
	cbz	w0, .L933
.L955:
	b	.L955
.L938:
	cmp	w0, 1
	beq	.L944
	cmp	w1, 256
	beq	.L944
	ldr	w0, [x19, 56]
	cbz	w0, .L945
.L944:
	str	wzr, [x19, 56]
	b	.L933
.L945:
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
	bcs	.L958
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 485
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L958:
	add	x20, x21, :lo12:.LANCHOR0
	ubfiz	x0, x22, 2, 16
	ldr	x1, [x20, 3616]
	ldr	w24, [x1, x0]
	cbnz	w24, .L959
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
.L960:
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L959:
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
	beq	.L961
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
.L962:
	ldrh	w0, [x25, 8]
	cmp	w0, w22
	beq	.L963
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 508
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L963:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21, 2616]
	add	x0, x0, x19
	str	wzr, [x0, 4]
	ldr	x0, [x21, 2616]
	strh	w22, [x0, x19]
	b	.L960
.L961:
	ldr	w0, [x20, 3728]
	cmp	w0, 256
	bne	.L962
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
	b	.L962
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
	blt	.L966
	ubfiz	x0, x0, 1, 16
	ldrh	w26, [x20, x0]
	cbz	w26, .L966
	ldr	w1, [x19, 52]
	cbnz	w1, .L966
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
	bcc	.L967
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L967:
	add	x27, x22, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR1
	add	x21, x27, 3728
	add	x23, x23, 472
	mov	w20, 0
.L968:
	ldrh	w0, [x19, 6]
	cmp	w0, w20
	bhi	.L975
	mov	w1, 1
	mov	w0, w26
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19, 52]
.L966:
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x22, 316]
	cmp	w1, w0
	bcc	.L976
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L976:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L975:
	ubfiz	x0, x20, 2, 16
	add	x28, x25, x0
	ldr	w1, [x25, x0]
	cmp	w26, w1, lsr 10
	bne	.L969
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
	beq	.L970
	adrp	x0, .LC8
	mov	w2, 611
	mov	x1, x23
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L970:
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L971
.L973:
	str	wzr, [x28]
.L972:
	b	.L972
.L971:
	ldrh	w0, [x24, 8]
	cmp	w0, w20
	bne	.L973
	ldrh	w1, [x24]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L973
	ldr	x2, [x21, 8]
	mov	w1, w20
	mov	x0, x19
	bl	FtlMapWritePage
.L969:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L968
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
	bcc	.L985
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 813
	add	x1, x1, 488
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L985:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0, 2632]
	cmp	w25, w1
	bcs	.L986
	add	w24, w24, 7
	ldrh	w1, [x0, 350]
	ldr	x2, [x0, 2616]
	mov	x0, 0
	lsr	w21, w25, w24
	and	w21, w21, 65535
.L987:
	and	x19, x0, 65535
	cmp	w19, w1
	bcc	.L992
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
	beq	.L993
	ldr	w1, [x3, 4]
	tbz	w1, #31, .L993
	bl	flush_l2p_region
.L993:
	mov	w1, w26
	mov	w0, w21
	bl	load_l2p_region
	b	.L989
.L986:
	mov	w0, -1
	cbnz	w23, .L984
	str	w0, [x22]
.L984:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L992:
	add	x0, x0, 1
	add	x3, x2, x0, lsl 4
	ldrh	w3, [x3, -16]
	cmp	w3, w21
	bne	.L987
.L989:
	mov	x0, 1
	lsl	x0, x0, x24
	sub	w0, w0, #1
	and	w0, w0, w25
	and	x0, x0, 65535
	cbnz	w23, .L990
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x1, [x1, 2616]
	add	x1, x1, x19, lsl 4
	ldr	x1, [x1, 8]
	ldr	w0, [x1, x0, lsl 2]
	str	w0, [x22]
.L991:
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20, 2616]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19, 4]
	cmn	w0, #1
	beq	.L995
	add	w0, w0, 1
	str	w0, [x19, 4]
.L995:
	mov	w0, 0
	b	.L984
.L990:
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
	b	.L991
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
	cbz	w2, .L999
	ldrb	w1, [x20, 40]
	cbnz	w1, .L999
	ldrb	w1, [x20, 39]
	ldrh	w3, [x20, 314]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L999
	ldrb	w0, [x20, 42]
	cbnz	w0, .L998
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
	beq	.L1001
	mov	w2, 0
	mov	w1, 1
	add	x0, x29, 96
	bl	FlashReadPages
.L1002:
	add	x25, x19, :lo12:.LANCHOR0
	lsl	w24, w24, 2
	add	x20, x25, 32
	mov	w0, -3947
	mov	w23, 0
	strh	w0, [x21]
.L1003:
	cmp	w24, w23
	bne	.L1007
.L1004:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19, 42]
.L998:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 128
	ret
.L1001:
	ldrh	w2, [x20, 322]
	mov	w1, 255
	ldr	x0, [x20, 3464]
	bl	ftl_memset
	b	.L1002
.L1007:
	ldrh	w0, [x20, 4]
	cbz	w0, .L1004
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
	b	.L1003
.L999:
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x19, 42]
	b	.L998
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
	cbnz	w1, .L1017
	ldr	x19, [x2, 2600]
	cbz	x19, .L1018
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
.L1019:
	cmp	w1, w3
	beq	.L1018
	cmp	w19, w0
	bne	.L1020
	add	x23, x22, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x23, 2600
	bl	List_remove_node
	ldrh	w0, [x23, 236]
	cbnz	w0, .L1021
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1733
	add	x1, x1, 504
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1021:
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
.L1018:
	add	x1, x29, 76
	mov	w2, 1
	mov	w0, w21
	bl	log2phys
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L1020:
	umull	x19, w19, w4
	ldrh	w19, [x2, x19]
	cmp	w19, w5
	beq	.L1018
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L1019
.L1017:
	add	w1, w1, 1
	strh	w1, [x3, x20]
	b	.L1018
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
	beq	.L1148
	ldrb	w0, [x20, 6]
	adrp	x19, .LANCHOR0
	str	w0, [x29, 140]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w26, [x20, 2]
	ldrh	w0, [x0, 314]
	cmp	w0, w26
	bne	.L1030
	strh	wzr, [x20, 4]
.L1155:
	strb	wzr, [x20, 6]
.L1148:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1030:
	ldrh	w0, [x20, 16]
	mov	w21, 0
.L1031:
	cmp	w0, w1
	beq	.L1032
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	mov	w23, w0
	cmn	w0, #1
	beq	.L1033
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
.L1034:
	cmp	x1, x3
	bne	.L1038
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
.L1039:
	cmp	w22, w3
	bne	.L1044
	add	w22, w23, 1
	ldr	w0, [x4, 4]
	and	w22, w22, 65535
.L1150:
	lsr	w0, w0, 10
	bl	P2V_plane
	and	w27, w0, 65535
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 314]
	cmp	w0, w22
	bne	.L1046
	strh	w22, [x20, 2]
	strb	wzr, [x20, 6]
	strh	wzr, [x20, 4]
.L1046:
	ldr	w0, [x29, 140]
	cmp	w22, w26
	ccmp	w27, w0, 0, eq
	bne	.L1047
.L1156:
	mov	w2, w27
	mov	w1, w22
	mov	x0, x20
	bl	ftl_sb_update_avl_pages
	b	.L1148
.L1032:
	add	w21, w21, 1
	and	w21, w21, 65535
	add	x0, x20, x21, sxtw 1
	ldrh	w0, [x0, 16]
	b	.L1031
.L1033:
	cbz	w26, .L1035
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1800
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1035:
	ldr	w0, [x29, 140]
	cmp	w0, 0
	ccmp	w21, w0, 4, ne
	beq	.L1036
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1801
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1036:
	strh	wzr, [x20, 2]
	b	.L1155
.L1038:
	ldrh	w2, [x3]
	cmp	w2, w10
	beq	.L1037
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
.L1037:
	add	x3, x3, 2
	b	.L1034
.L1044:
	ldr	w0, [x6]
	cbnz	w0, .L1040
	ldr	x8, [x6, 16]
	ldr	w5, [x8, 4]
	cmn	w5, #1
	beq	.L1041
	ldr	w1, [x24, 2672]
	mov	w0, w5
	bl	ftl_cmp_data_ver
	cbz	w0, .L1041
	add	w5, w5, 1
	str	w5, [x24, 2672]
.L1041:
	ldr	w0, [x8]
	cmn	w0, #1
	bne	.L1043
	ubfiz	x3, x3, 5, 16
	and	w22, w23, 65535
	add	x3, x4, x3
	ldr	w0, [x3, 4]
	b	.L1150
.L1040:
	mov	w7, w9
.L1043:
	add	w3, w3, 1
	add	x6, x6, 32
	and	w3, w3, 65535
	b	.L1039
.L1047:
	mov	w0, 65535
	cmp	w7, w0
	bne	.L1048
	ldrb	w0, [x20, 8]
	cbnz	w0, .L1049
.L1048:
	add	x0, x19, :lo12:.LANCHOR0
	and	w3, w23, 65535
	ldr	w1, [x0, 3724]
	cmn	w1, #1
	bne	.L1050
	str	w21, [x0, 3724]
.L1050:
	add	w0, w26, 7
	cmp	w0, w23, uxth
	bge	.L1098
	sub	w24, w3, #7
	and	w24, w24, 65535
.L1051:
	add	x25, x19, :lo12:.LANCHOR0
	mov	w4, 65535
.L1052:
	cmp	w24, w3
	bhi	.L1059
	ldrh	w0, [x25, 244]
	mov	x1, x28
	mov	w23, 0
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
	b	.L1060
.L1098:
	mov	w24, w26
	b	.L1051
.L1054:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1053
	ldr	x5, [x25, 3440]
	ubfiz	x6, x23, 5, 16
	add	w23, w23, 1
	orr	w2, w24, w2, lsl 10
	add	x5, x5, x6
	and	w23, w23, 65535
	str	w2, [x5, 4]
.L1053:
	add	x1, x1, 2
.L1060:
	cmp	x0, x1
	bne	.L1054
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
.L1055:
	cmp	x23, x0
	bne	.L1058
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L1052
.L1058:
	ldr	w1, [x0, -16]
	cbnz	w1, .L1049
	ldr	x1, [x0]
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1057
	ldr	w1, [x1, 4]
	cmn	w1, #1
	beq	.L1057
	str	w1, [x25, 3724]
.L1057:
	add	x0, x0, 32
	b	.L1055
.L1059:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	str	w1, [x0, 3724]
.L1049:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	mov	w24, w26
	strh	w0, [x19, 3760]
	add	x0, x19, 3768
	str	x0, [x29, 104]
.L1061:
	ldrh	w0, [x19, 244]
	mov	x1, x28
	mov	w23, 0
	mov	w4, 65535
	add	x0, x0, 8
	add	x0, x20, x0, lsl 1
.L1062:
	cmp	x0, x1
	bne	.L1064
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
.L1065:
	ldr	x0, [x29, 112]
	cmp	x0, x23
	bne	.L1092
	add	w24, w24, 1
	ldrh	w0, [x19, 314]
	and	w24, w24, 65535
	cmp	w0, w24
	bne	.L1061
	ldrh	w1, [x19, 244]
	mov	w0, 0
	strh	w24, [x20, 2]
	mov	w2, 65535
	strh	wzr, [x20, 4]
.L1093:
	cmp	w0, w1
	beq	.L1148
	ldrh	w3, [x28], 2
	cmp	w3, w2
	beq	.L1094
	strb	w0, [x20, 6]
	b	.L1148
.L1064:
	ldrh	w2, [x1]
	cmp	w2, w4
	beq	.L1063
	ldr	x3, [x19, 3440]
	ubfiz	x5, x23, 5, 16
	add	w23, w23, 1
	orr	w2, w24, w2, lsl 10
	add	x3, x3, x5
	and	w23, w23, 65535
	str	w2, [x3, 4]
.L1063:
	add	x1, x1, 2
	b	.L1062
.L1092:
	ldr	x4, [x19, 3440]
	add	x4, x4, x23
	ldr	w5, [x4, 4]
	str	w5, [x29, 156]
	lsr	w0, w5, 10
	bl	P2V_plane
	and	w0, w0, 65535
	cmp	w24, w26
	bcc	.L1066
	ldr	w1, [x29, 140]
	ccmp	w1, w0, 0, eq
	bhi	.L1066
	cmp	w24, w22
	ccmp	w27, w0, 0, eq
	beq	.L1067
	ldr	w0, [x4]
	cmn	w0, #1
	beq	.L1068
	ldr	x3, [x4, 16]
	mov	w0, 61589
	ldrh	w1, [x3]
	cmp	w1, w0
	beq	.L1069
	ldrh	w0, [x20]
.L1152:
	bl	decrement_vpc_count
	b	.L1066
.L1069:
	ldr	w21, [x3, 4]
	cmn	w21, #1
	beq	.L1070
	ldr	w1, [x19, 2672]
	mov	w0, w21
	str	x3, [x29, 128]
	bl	ftl_cmp_data_ver
	ldr	x3, [x29, 128]
	cbz	w0, .L1070
	add	w0, w21, 1
	str	w0, [x19, 2672]
.L1070:
	ldrh	w1, [x3]
	mov	w0, 61589
	cmp	w1, w0
	beq	.L1071
	ldr	x1, [x29, 120]
	mov	w2, 1952
	str	x3, [x29, 128]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	ldr	x3, [x29, 128]
.L1071:
	ldp	w25, w0, [x3, 8]
	add	x1, x29, 152
	str	w0, [x29, 148]
	mov	w2, 0
	mov	w0, w25
	bl	log2phys
	ldr	w1, [x19, 3724]
	ldr	w3, [x29, 148]
	cmn	w1, #1
	beq	.L1072
	mov	w0, w21
	bl	ftl_cmp_data_ver
	cbz	w0, .L1072
	cmn	w3, #1
	beq	.L1073
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
	bne	.L1074
.L1075:
	mov	w0, -1
	str	w0, [x29, 148]
.L1082:
	ldr	w4, [x29, 148]
	cmn	w4, #1
	beq	.L1066
.L1097:
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	and	w25, w0, 65535
	ldrh	w0, [x19, 252]
	mov	w3, w25
	cmp	w0, w25
	bhi	.L1088
	ldr	x1, [x29, 120]
	mov	w2, 2057
	adrp	x0, .LC8
	str	w25, [x29, 128]
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	ldr	w3, [x29, 128]
.L1088:
	ldr	x1, [x19, 80]
	ubfiz	x0, x25, 1, 16
	ldrh	w0, [x1, x0]
	cbz	w0, .L1089
	mov	w0, w3
	b	.L1152
.L1073:
	ldp	w1, w0, [x29, 152]
	cmp	w1, w0
	bne	.L1066
	mov	w2, 1
	add	x1, x29, 148
	mov	w0, w25
	bl	log2phys
.L1066:
	add	x23, x23, 32
	b	.L1065
.L1074:
	ldr	w0, [x4, 8]
	cmp	w25, w0
	bne	.L1075
	ldr	w0, [x4, 4]
	str	w0, [x29, 128]
	str	x4, [x29, 96]
	uxtw	x1, w0
	ldr	w0, [x19, 3724]
	bl	ftl_cmp_data_ver
	cbz	w0, .L1075
	ldp	w0, w1, [x29, 152]
	ldr	x4, [x29, 96]
	cmp	w0, w1
	ldr	w1, [x29, 148]
	bne	.L1077
.L1151:
	mov	w0, w25
	bl	FtlReUsePrevPpa
	b	.L1075
.L1077:
	cmp	w0, w1
	beq	.L1075
	cmn	w0, #1
	beq	.L1078
	ldr	x4, [x3, 16]
	mov	w2, 0
	str	w0, [x3, 4]
	mov	w1, 1
	str	x4, [x29, 96]
	ldr	x0, [x19, 3440]
	add	x0, x0, x23
	bl	FlashReadPages
	ldr	x4, [x29, 96]
.L1079:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3440]
	ldr	w1, [x1, x23]
	cmn	w1, #1
	beq	.L1080
	ldr	w3, [x4, 4]
	ldr	w0, [x0, 3724]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1080
	ldr	w0, [x29, 128]
	mov	w1, w3
	bl	ftl_cmp_data_ver
	cbz	w0, .L1075
.L1080:
	ldr	w1, [x29, 148]
	b	.L1151
.L1078:
	str	w0, [x3]
	b	.L1079
.L1072:
	ldp	w1, w0, [x29, 152]
	cmp	w1, w0
	beq	.L1082
	cmn	w3, #1
	beq	.L1084
	ldr	w0, [x19, 260]
	cmp	w0, w3, lsr 10
	bhi	.L1084
	adrp	x0, .LC109
	mov	w1, w3
	add	x0, x0, :lo12:.LC109
.L1154:
	bl	sftl_printk
	b	.L1066
.L1084:
	mov	w2, 1
	add	x1, x29, 156
	mov	w0, w25
	bl	log2phys
	ldr	w4, [x29, 152]
	cmn	w4, #1
	beq	.L1082
	ldr	w0, [x29, 148]
	cmp	w4, w0
	beq	.L1097
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x19, 32]
	and	w0, w0, 65535
	cmp	w1, w0
	beq	.L1087
	ldrh	w1, [x19, 88]
	cmp	w1, w0
	beq	.L1087
	ldrh	w1, [x19, 136]
	cmp	w1, w0
	bne	.L1082
.L1087:
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
	beq	.L1082
	ldr	x3, [x29, 128]
	mov	w0, w21
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
	cbnz	w0, .L1082
	mov	w2, 1
	add	x1, x29, 152
	mov	w0, w25
	bl	log2phys
	b	.L1082
.L1089:
	adrp	x0, .LC110
	mov	w1, w25
	add	x0, x0, :lo12:.LC110
	b	.L1154
.L1068:
	ldr	w0, [x19, 3764]
	cmp	w0, 31
	bhi	.L1090
	ldr	x1, [x29, 104]
	str	w5, [x1, w0, uxtw 2]
	add	w0, w0, 1
	str	w0, [x19, 3764]
.L1090:
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	ldr	w0, [x19, 3724]
	cmn	w0, #1
	bne	.L1091
.L1153:
	str	w21, [x19, 3724]
	b	.L1066
.L1091:
	cmp	w21, w0
	bcs	.L1066
	b	.L1153
.L1094:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1093
.L1067:
	strb	w27, [x20, 6]
	strh	w22, [x20, 2]
	b	.L1156
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
.L1158:
	ldr	w0, [x24, 2632]
	cmp	w23, w0
	bcc	.L1160
	adrp	x25, .LC112
	add	x23, x20, :lo12:.LANCHOR0
	add	x26, x22, :lo12:check_vpc_table
	add	x25, x25, :lo12:.LC112
	mov	w24, 0
	mov	w19, 0
	mov	w4, 65535
.L1161:
	ldrh	w0, [x23, 252]
	cmp	w0, w19
	bhi	.L1163
	ldr	x19, [x23, 2600]
	cbz	x19, .L1164
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
.L1165:
	cmp	w23, w26
	bne	.L1167
.L1164:
	cbz	w24, .L1157
	add	x1, x21, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2383
	add	x1, x1, 544
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1157:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1160:
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w23
	bl	log2phys
	ldr	w0, [x29, 124]
	cmn	w0, #1
	beq	.L1159
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	and	x0, x0, 65535
	ldrh	w1, [x19, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x19, x0, lsl 1]
.L1159:
	add	w23, w23, 1
	b	.L1158
.L1163:
	ldr	x0, [x23, 80]
	ubfiz	x28, x19, 1, 16
	sxtw	x27, w19
	ldrh	w2, [x0, x28]
	ldrh	w3, [x26, x27, lsl 1]
	cmp	w2, w3
	beq	.L1162
	mov	x0, x25
	str	w4, [x29, 108]
	mov	w1, w19
	bl	sftl_printk
	ldr	x0, [x23, 80]
	ldr	w4, [x29, 108]
	ldrh	w0, [x0, x28]
	cmp	w0, w4
	beq	.L1162
	ldrh	w1, [x26, x27, lsl 1]
	cmp	w1, w0
	csinc	w24, w24, wzr, ls
.L1162:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1161
.L1167:
	ldr	x1, [x20, 80]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L1166
	ldrh	w3, [x22, w19, sxtw 1]
	mov	w24, 1
	mov	w1, w19
	mov	x0, x25
	bl	sftl_printk
.L1166:
	ldr	x0, [x20, 2568]
	umull	x19, w19, w27
	ldrh	w19, [x0, x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L1164
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L1165
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
.L1183:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0, 2632]
	cmp	w20, w0
	bcc	.L1189
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x29, x30, [sp, 16]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	ret
.L1189:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w20
	bl	log2phys
	tst	x20, 2047
	bne	.L1184
	ldr	w2, [x29, 76]
	adrp	x0, .LC115
	mov	w1, w20
	add	x0, x0, :lo12:.LC115
	bl	sftl_printk
.L1184:
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1186
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
	beq	.L1187
	ldr	w0, [x23, 8]
	cmp	w20, w0
	beq	.L1186
.L1187:
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
.L1186:
	add	w20, w20, 1
	b	.L1183
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
	beq	.L1215
	cbnz	w28, .L1192
.L1193:
	bl	FtlGcPageVarInit
	b	.L1194
.L1215:
	mov	w28, 0
.L1192:
	adrp	x0, .LANCHOR0+314
	ldrh	w0, [x0, #:lo12:.LANCHOR0+314]
	cmp	w0, w25
	beq	.L1193
.L1194:
	adrp	x26, .LANCHOR0
	add	x19, x26, :lo12:.LANCHOR0
	mov	w24, 0
.L1209:
	ldrh	w1, [x20]
	mov	w0, 65535
	strb	wzr, [x20, 8]
	cmp	w1, w0
	beq	.L1195
.L1212:
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
.L1196:
	cmp	x0, x2
	bne	.L1198
	ldr	x0, [x19, 3440]
	mov	w2, 0
	mov	w1, w21
	mov	x23, 0
	bl	FlashReadPages
	ubfiz	x0, x21, 5, 16
	str	x0, [x29, 104]
.L1199:
	ldr	x0, [x29, 104]
	cmp	x0, x23
	bne	.L1210
	add	w3, w28, 1
	add	w24, w24, 1
	and	w28, w3, 65535
	cmp	w25, w24
	bls	.L1211
.L1213:
	ldrh	w0, [x19, 314]
	cmp	w0, w28
	bhi	.L1212
.L1195:
	add	x22, x22, :lo12:.LANCHOR2
	strh	w28, [x20, 2]
	strb	wzr, [x20, 6]
	mov	w0, -1
	mov	w2, 0
	mov	w1, w28
	strh	w0, [x22, 4]
	mov	x0, x20
	bl	ftl_sb_update_avl_pages
	b	.L1214
.L1198:
	ldrh	w1, [x2]
	cmp	w1, w10
	beq	.L1197
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
.L1197:
	add	x2, x2, 2
	b	.L1196
.L1210:
	ldr	x0, [x19, 3440]
	add	x1, x0, x23
	ldr	w0, [x0, x23]
	ldr	w27, [x1, 4]
	ldr	x21, [x1, 16]
	cbnz	w0, .L1200
	ldrh	w0, [x21]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1201
.L1225:
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
	b	.L1209
.L1201:
	ldr	w1, [x19, 2632]
	ldr	w0, [x21, 8]
	cmp	w0, w1
	bhi	.L1225
	add	x1, x29, 124
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x21, 12]
	ldr	w1, [x29, 124]
	cmp	w0, w1
	beq	.L1204
.L1206:
	ldr	w2, [x21, 8]
.L1226:
	ldr	w0, [x21, 12]
	mov	w1, w27
	add	x23, x23, 32
	bl	FtlGcUpdatePage
	b	.L1199
.L1204:
	cmn	w0, #1
	beq	.L1206
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
.L1207:
	cmp	x0, x1
	beq	.L1206
	ldr	x6, [x2, 8]
	ldr	w7, [x6, x0]
	add	x0, x0, 4
	add	x6, x5, x0
	ldr	w6, [x6, -4]
	cmp	w7, w6
	beq	.L1207
	ldrh	w1, [x20]
	adrp	x0, .LC117
	ldr	w2, [x29, 132]
	add	x0, x0, :lo12:.LC117
	bl	sftl_printk
	b	.L1225
.L1200:
	mov	w2, -1
	b	.L1226
.L1211:
	add	x1, x22, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x1, 4]
	cmp	w0, w2
	beq	.L1213
	add	w0, w0, w24
	strh	w0, [x1, 4]
	ldrh	w0, [x19, 314]
	cmp	w0, w28
	bls	.L1213
.L1214:
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
	cbz	w3, .L1228
	ldr	w1, [x0, 2632]
	ldr	w3, [x2, 84]
	cmp	w3, w1
	bcs	.L1229
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w20, 2048
	str	x21, [sp, 32]
.L1234:
	add	x21, x19, 2720
	ldr	w1, [x19, 2632]
	ldr	w0, [x21, 84]
	cmp	w0, w1
	bcc	.L1230
.L1233:
	ldp	x19, x20, [sp, 16]
	mov	w0, -1
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 160
	ret
.L1230:
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x21, 84]
	ldr	w1, [x29, 60]
	add	w0, w0, 1
	str	w0, [x21, 84]
	cmn	w1, #1
	beq	.L1232
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
	bne	.L1233
	ldr	w0, [x29, 60]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1233
.L1232:
	subs	w20, w20, #1
	bne	.L1234
	b	.L1233
.L1229:
	ldr	w0, [x0, 2636]
	stp	w0, wzr, [x2, 76]
	str	wzr, [x2, 84]
.L1241:
	mov	w0, 0
	ret
.L1228:
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
	bhi	.L1238
	ldr	w3, [x0, 2632]
	mov	w8, 1000
	lsr	w4, w4, 10
	add	w4, w4, 1
	mul	w3, w3, w8
	udiv	w3, w3, w4
	add	w3, w3, w7
	cmp	w5, w3
	bhi	.L1238
	ldrh	w0, [x0, 2548]
	tst	w6, w0
	bne	.L1241
	ldr	w2, [x2, 100]
	cmp	w0, w2
	beq	.L1241
.L1238:
	add	x0, x1, :lo12:.LANCHOR0
	add	x1, x0, 2720
	ldrh	w0, [x0, 2548]
	str	w0, [x1, 100]
	str	w5, [x1, 76]
	mov	w0, 1
	str	wzr, [x1, 84]
	str	w0, [x1, 80]
	b	.L1241
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
.L1248:
	ldrh	w0, [x20, 350]
	cmp	w0, w19
	bhi	.L1250
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1250:
	ldr	x1, [x20, 2616]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L1249
	mov	w0, w19
	bl	flush_l2p_region
.L1249:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1248
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
	bhi	.L1260
	ldrh	w23, [x19, 320]
	mov	x25, x2
	add	x26, x29, 144
	add	x27, x19, 3896
	mov	w24, 0
	lsr	w23, w28, w23
.L1254:
	cbnz	w22, .L1259
.L1252:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1259:
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
	cbz	w2, .L1256
	cmp	w20, w1
	beq	.L1256
	ldr	x0, [x19, 3480]
	str	w2, [x29, 116]
	mov	w2, 1
	stp	x0, x26, [x29, 120]
	mov	w1, w2
	add	x0, x29, 112
	bl	FlashReadPages
.L1257:
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
	b	.L1254
.L1256:
	ldrh	w2, [x19, 322]
	mov	w1, 0
	ldr	x0, [x19, 3480]
	bl	ftl_memset
	b	.L1257
.L1260:
	mov	w24, -1
	b	.L1252
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
	bne	.L1268
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
.L1268:
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
	bhi	.L1275
	add	w2, w2, 1
	strh	w2, [x3, 3960]
	mov	w2, 1
.L1272:
	cbnz	w0, .L1273
	add	x0, x1, :lo12:.LANCHOR0
	ldr	x0, [x0, 3560]
	ldr	w3, [x0, 20]
	ldr	w0, [x0, 16]
	add	w2, w2, w3
	cmp	w0, w2
	bcc	.L1277
.L1273:
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
.L1275:
	mov	w2, 32
	b	.L1272
.L1277:
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
	bhi	.L1290
	ldrh	w21, [x3, 320]
	adrp	x26, .LC118
	mov	x25, x2
	mov	x28, x3
	add	x26, x26, :lo12:.LC118
	mov	w24, 0
	lsr	w21, w23, w21
.L1283:
	cbnz	w22, .L1289
.L1281:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1289:
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
	cbz	w4, .L1285
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
	bne	.L1287
	mov	w2, w4
	mov	w1, w21
	mov	x0, x26
	bl	sftl_printk
	ldr	x2, [x28, 3480]
	mov	w1, w21
	add	x0, x28, 3896
	bl	FtlMapWritePage
.L1287:
	ldr	x1, [x28, 3480]
	lsl	w20, w20, 9
	asr	w20, w20, 2
	mov	w2, w27
	mov	x0, x25
	add	x1, x1, x20, sxtw 2
	bl	ftl_memcpy
.L1288:
	add	w21, w21, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x25, x25, x27, sxtw
	b	.L1283
.L1285:
	mov	w2, w27
	mov	w1, 0
	mov	x0, x25
	bl	ftl_memset
	b	.L1288
.L1290:
	mov	w24, -1
	b	.L1281
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
	beq	.L1293
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
.L1293:
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
	beq	.L1296
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	str	w21, [x20, 2720]
.L1296:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0, 2720]
	cmp	w3, w2
	bne	.L1297
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
.L1297:
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
	cbz	w1, .L1309
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
	cbz	w4, .L1303
	mov	w2, 1
	mov	w1, w2
	bl	FlashReadPages
.L1304:
	add	x1, x21, :lo12:.LANCHOR0
	mov	x0, x19
	ldr	x2, [x1, 3736]
	mov	w1, w20
	bl	FtlMapWritePage
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1303:
	ldrh	w2, [x2, 322]
	mov	w1, 255
	mov	x0, x3
	bl	ftl_memset
	b	.L1304
.L1309:
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
.L1313:
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
	blt	.L1314
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
.L1314:
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
	bne	.L1315
	cmp	w0, 1
	bne	.L1316
	adrp	x0, .LC8
	mov	w2, 1138
	mov	x1, x25
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1316:
	ldrh	w0, [x20, 2]
	cmp	w0, 1
	bne	.L1317
	ldrh	w0, [x19, 316]
	sub	w0, w0, #1
	strh	w0, [x20, 2]
.L1317:
	add	w22, w22, 1
	and	w22, w22, 65535
	cmp	w22, 3
	bls	.L1313
	add	x23, x23, :lo12:.LANCHOR0
	adrp	x0, .LC120
	mov	w2, w22
	add	x0, x0, :lo12:.LC120
	ldr	w1, [x23, 3732]
	bl	sftl_printk
.L1319:
	b	.L1319
.L1315:
	cmp	w0, 1
	beq	.L1313
	cmp	w1, 256
	beq	.L1313
	mov	w0, 65535
	cmp	w24, w0
	beq	.L1320
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
.L1320:
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
	bne	.L1333
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlSysFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
.L1333:
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
	bhi	.L1344
	cmp	w20, 31
	bhi	.L1338
.L1352:
	mov	w0, 0
.L1336:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1338:
	ldrh	w1, [x1, 270]
	udiv	w21, w0, w1
	msub	w0, w1, w21, w0
	ands	w0, w0, 65535
	beq	.L1339
	sub	w1, w1, w0
	add	w21, w21, 1
	cmp	w1, w20
	csel	w1, w1, w20, ls
	sub	w20, w20, w1, uxth
.L1339:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	str	w0, [x29, 60]
.L1340:
	ldrh	w0, [x19, 270]
	cmp	w20, w0
	bcs	.L1342
	ldr	w0, [x19, 3964]
	cmp	w0, 32
	bls	.L1352
	str	wzr, [x19, 3964]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1352
.L1342:
	mov	w2, 0
	add	x1, x29, 56
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 56]
	cmn	w0, #1
	beq	.L1341
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
.L1341:
	ldrh	w0, [x19, 270]
	add	w21, w21, 1
	sub	w20, w20, w0
	b	.L1340
.L1344:
	mov	w0, -1
	b	.L1336
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
.L1354:
	ldr	w0, [x19, 2632]
	cmp	w21, w0
	bcc	.L1356
	adrp	x22, .LC121
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC121
	mov	w19, 0
	mov	w23, 65535
.L1357:
	ldrh	w0, [x20, 252]
	cmp	w0, w19
	bhi	.L1362
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 80
	ret
.L1356:
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1355
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldr	x2, [x19, 3568]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L1355:
	add	w21, w21, 1
	b	.L1354
.L1362:
	ldr	x0, [x20, 80]
	ubfiz	x21, x19, 1, 16
	ldrh	w2, [x0, x21]
	ldr	x0, [x20, 3568]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L1359
	cmp	w2, w23
	beq	.L1359
	mov	x0, x22
	mov	w1, w19
	bl	sftl_printk
	ldrh	w0, [x20, 32]
	cmp	w0, w19
	beq	.L1359
	ldrh	w0, [x20, 136]
	cmp	w0, w19
	beq	.L1359
	ldrh	w0, [x20, 88]
	cmp	w0, w19
	beq	.L1359
	ldr	x0, [x20, 80]
	ldrh	w1, [x0, x21]
	cbnz	w1, .L1361
	ldr	x1, [x20, 3568]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
.L1359:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1357
.L1361:
	ldr	x1, [x20, 3568]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
	mov	w0, w19
	bl	update_vpc_list
	b	.L1359
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
	bcs	.L1368
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 2755
	add	x1, x1, 600
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1368:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L1369
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x20, 1, 16
	ldr	x1, [x1, 80]
	ldrh	w0, [x1, x0]
	cbz	w0, .L1370
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L1369:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x21, 8]
	mov	w1, 65535
	ldrh	w0, [x2, 3714]
	cmp	w0, w1
	beq	.L1371
	cmp	w20, w0
	bne	.L1372
	ldr	x2, [x2, 80]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1373
.L1372:
	bl	update_vpc_list
.L1373:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19, 3714]
.L1371:
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
.L1370:
	mov	w0, w20
	bl	INSERT_FREE_LIST
	b	.L1369
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
.L1379:
	cmp	x21, x19
	beq	.L1385
	sub	x22, x19, #4
	b	.L1386
.L1381:
	ldr	w1, [x19]
	mov	x0, x23
	bl	sftl_printk
	ldr	w0, [x19]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1380
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1380:
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x19]
	mov	w2, 0
	str	w0, [x29, 92]
	mov	w1, 1
	ldrb	w3, [x20, 9]
	mov	x0, x22
	bl	FlashProgPages
.L1386:
	ldr	w2, [x19, -4]
	cmp	w2, 256
	ccmn	w2, #1, 4, ne
	beq	.L1381
	ldrb	w1, [x20, 6]
	ldrh	w0, [x25, 244]
	cmp	w1, w0
	bcc	.L1382
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 982
	add	x1, x1, 632
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1382:
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
	beq	.L1383
	add	x2, x24, :lo12:.LANCHOR0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x2, 80]
	ldrh	w0, [x2, x0]
	cbnz	w0, .L1384
	adrp	x0, .LC123
	mov	w2, 0
	add	x0, x0, :lo12:.LC123
	bl	sftl_printk
.L1384:
	mov	w0, w22
	bl	decrement_vpc_count
.L1383:
	add	x19, x19, 32
	b	.L1379
.L1385:
	adrp	x0, .LANCHOR0+244
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, #:lo12:.LANCHOR0+244]
	cmp	w1, w0
	bcc	.L1378
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 997
	add	x1, x1, 632
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1378:
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
	bne	.L1393
.L1400:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 136
	ldrh	w3, [x0, 136]
	str	wzr, [x0, 3720]
	cmp	w3, w2
	bne	.L1413
.L1417:
	mov	w0, 0
.L1392:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1393:
	adrp	x20, .LANCHOR2
	cbz	w0, .L1396
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w4, [x0, 4]
	cmp	w4, w3
	beq	.L1397
.L1398:
	mov	w1, 2
.L1396:
	add	x20, x20, :lo12:.LANCHOR2
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 136
	bl	FtlGcScanTempBlk
	str	w0, [x29, 92]
	mov	w0, 65535
	ldrh	w1, [x20, 4]
	cmp	w1, w0
	beq	.L1400
	mov	w0, 1
	b	.L1392
.L1397:
	strh	wzr, [x0, 4]
	ldrh	w0, [x2, 236]
	cmp	w0, 17
	bhi	.L1398
	b	.L1396
.L1413:
	ldrh	w2, [x0, 3304]
	ldrb	w1, [x1, 7]
	ldrh	w0, [x0, 314]
	mul	w0, w1, w0
	cmp	w2, w0
	beq	.L1401
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 164
	add	x1, x1, 648
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1401:
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
.L1402:
	ldrh	w0, [x20, 3304]
	cmp	w0, w21
	bhi	.L1406
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x20, 136]
	ldr	x2, [x20, 80]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1407
	bl	INSERT_DATA_LIST
.L1408:
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
	ble	.L1417
	mov	w0, 20
	strh	w0, [x19, 3232]
	b	.L1417
.L1406:
	umull	x23, w21, w25
	ldr	x24, [x20, 3312]
	ldr	w1, [x20, 2632]
	add	x22, x24, x23
	ldr	w0, [x22, 8]
	cmp	w0, w1
	bcc	.L1403
.L1415:
	ldrh	w0, [x20, 136]
	b	.L1416
.L1403:
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x24, x23]
	ldr	w1, [x29, 92]
	cmp	w0, w1
	bne	.L1405
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	mov	w23, w0
	ldr	w0, [x22, 8]
	mov	w2, 1
	add	x1, x22, 4
	bl	log2phys
	mov	w0, w23
.L1416:
	bl	decrement_vpc_count
	b	.L1404
.L1405:
	ldr	w0, [x22, 4]
	cmp	w1, w0
	bne	.L1415
.L1404:
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L1402
.L1407:
	bl	INSERT_FREE_LIST
	b	.L1408
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
	bcc	.L1418
	add	x0, x19, 3648
	bl	FtlMapBlkWriteDump_data
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19, 3720]
.L1418:
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
.L1424:
	ldr	w1, [x19, 3248]
	cmp	w10, w1
	bcc	.L1427
	ldr	x0, [x19, 3448]
	bl	FtlGcBufFree
	str	wzr, [x19, 3248]
	ldrh	w0, [x19, 140]
	cbnz	w0, .L1429
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w0, 1
.L1423:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1427:
	ldr	x2, [x19, 3448]
	ubfiz	x0, x10, 5, 16
	add	x1, x2, x0
	ldr	w2, [x2, x0]
	ldr	x3, [x1, 16]
	cmn	w2, #1
	bne	.L1425
.L1431:
	ldr	w1, [x1, 4]
	ldr	w0, [x3, 12]
	bl	FtlGcUpdatePage
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L1424
.L1425:
	ldr	w2, [x3, 8]
	b	.L1431
.L1429:
	mov	w0, 0
	b	.L1423
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
	beq	.L1433
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1434
.L1433:
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
.L1434:
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
	cbnz	w2, .L1514
	ldrh	w2, [x1, 2592]
	cmp	w2, 47
	bls	.L1514
	mov	w23, w0
	adrp	x0, .LANCHOR2+4
	ldrh	w2, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1438
.L1441:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 232]
	cmp	w1, w2
	bne	.L1439
.L1440:
	add	x20, x19, :lo12:.LANCHOR0
	bl	FtlReadRefresh
	mov	w21, 65535
	ldr	w0, [x20, 3236]
	ldrh	w1, [x20, 184]
	add	w0, w0, 1
	add	w0, w0, w23, lsl 7
	str	w0, [x20, 3236]
	cmp	w1, w21
	bne	.L1442
	ldrh	w21, [x20, 136]
	cmp	w21, w1
	bne	.L1517
	ldrh	w22, [x20, 234]
	cmp	w22, w21
	bne	.L1442
	ldrh	w1, [x20, 236]
	mov	w2, 1024
	cmp	w1, 24
	mov	w1, 5120
	csel	w1, w1, w2, cc
	cmp	w0, w1
	bls	.L1520
	strh	wzr, [x20, 3968]
	str	wzr, [x20, 3236]
	bl	GetSwlReplaceBlock
	and	w21, w0, 65535
	cmp	w21, w22
	bne	.L1521
	ldrh	w1, [x20, 236]
	ldrh	w0, [x20, 3234]
	cmp	w1, w0
	bcs	.L1445
	mov	w0, 64
	bl	List_get_gc_head_node
	and	w0, w0, 65535
	cmp	w0, w21
	beq	.L1447
	ldr	x1, [x20, 80]
	ubfiz	x0, x0, 1, 16
	ldrh	w0, [x1, x0]
	cmp	w0, 7
	bhi	.L1448
	mov	w0, 0
	bl	List_get_gc_head_node
	and	w22, w0, 65535
	mov	w0, 128
	strh	w0, [x20, 3234]
	cmp	w22, w21
	bne	.L1444
.L1447:
	bl	FtlGcReFreshBadBlk
.L1442:
	cmp	w23, 0
	mov	w0, 65535
	add	x1, x19, :lo12:.LANCHOR0
	ccmp	w21, w0, 0, eq
	bne	.L1450
	ldrh	w0, [x1, 236]
	cmp	w0, 24
	bhi	.L1522
	cmp	w0, 16
	ldrh	w20, [x1, 314]
	bls	.L1452
	lsr	w20, w20, 5
.L1451:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1, 3232]
	cmp	w2, w0
	bcs	.L1455
	ldrh	w0, [x1, 136]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1456
	ldrh	w2, [x1, 234]
	cmp	w2, w0
	bne	.L1456
	ldrh	w0, [x1, 3968]
	cbnz	w0, .L1457
	ldr	w2, [x1, 2632]
	ldr	w3, [x1, 2628]
	add	w2, w2, w2, lsl 1
	cmp	w3, w2, lsr 2
	bcs	.L1458
.L1457:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2, 2700]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2, 3232]
.L1459:
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19, 3244]
.L1436:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1438:
	ldrh	w1, [x1, 136]
	cmp	w1, w0
	beq	.L1441
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	cbz	w0, .L1441
	mov	w0, 1
	b	.L1436
.L1439:
	ldrh	w3, [x0, 234]
	cmp	w3, w2
	bne	.L1440
	strh	w1, [x0, 234]
	mov	w1, -1
	strh	w1, [x0, 232]
	b	.L1440
.L1448:
	mov	w0, 64
.L1552:
	strh	w0, [x20, 3234]
	b	.L1447
.L1445:
	mov	w0, 80
	b	.L1552
.L1521:
	mov	w22, w21
.L1444:
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
	b	.L1447
.L1517:
	mov	w21, w1
	b	.L1442
.L1520:
	mov	w21, w22
	b	.L1442
.L1452:
	cmp	w0, 12
	bls	.L1453
	lsr	w20, w20, 4
	b	.L1451
.L1453:
	mov	w1, w20
	cmp	w0, 9
	lsr	w20, w20, 2
	csel	w20, w20, w1, cs
	b	.L1451
.L1522:
	mov	w20, 1
	b	.L1451
.L1458:
	mov	w2, 18
	strh	w2, [x1, 3232]
	b	.L1459
.L1456:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2700]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 3232]
.L1455:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 3320]
	cbz	w0, .L1523
	add	w20, w20, 32
	and	w20, w20, 65535
.L1523:
	mov	w21, 65535
.L1461:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 184]
	cmp	w1, w2
	bne	.L1470
	cmp	w21, w1
	beq	.L1471
	strh	w21, [x0, 184]
.L1472:
	add	x5, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldrh	w0, [x5, 184]
	strb	wzr, [x5, 192]
	cmp	w0, w1
	beq	.L1470
	bl	IsBlkInGcList
	cbz	w0, .L1475
	mov	w0, -1
	strh	w0, [x5, 184]
.L1475:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	add	x24, x22, 184
	ldrh	w1, [x22, 184]
	cmp	w1, w0
	beq	.L1470
	mov	x0, x24
	bl	make_superblock
	ldrh	w1, [x22, 184]
	ldr	x0, [x22, 80]
	strh	wzr, [x22, 186]
	strb	wzr, [x22, 190]
	strh	wzr, [x22, 3970]
	ldrh	w0, [x0, x1, lsl 1]
	strh	w0, [x22, 3972]
.L1470:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 184]
	ldrh	w2, [x0, 32]
	cmp	w2, w1
	beq	.L1476
	ldrh	w2, [x0, 88]
	cmp	w2, w1
	beq	.L1476
	ldrh	w0, [x0, 136]
	cmp	w0, w1
	bne	.L1477
.L1476:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 184]
.L1477:
	adrp	x24, .LANCHOR1
	add	x24, x24, :lo12:.LANCHOR1
	add	x24, x24, 672
.L1511:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w25, [x22, 184]
	cmp	w25, w0
	bne	.L1478
	mov	w26, 2
	str	wzr, [x22, 3244]
.L1479:
	ldrh	w5, [x22, 3240]
	mov	w0, w5
	bl	List_get_gc_head_node
	and	w6, w0, 65535
	strh	w6, [x22, 184]
	cmp	w6, w25
	bne	.L1480
	strh	wzr, [x22, 3240]
	mov	w0, 8
	b	.L1436
.L1450:
	ldrh	w2, [x1, 136]
	cmp	w2, w0
	bne	.L1526
	ldrh	w0, [x1, 234]
	cmp	w0, w2
	bne	.L1526
	cmp	w21, w0
	bne	.L1526
	ldrh	w0, [x1, 184]
	cmp	w0, w21
	beq	.L1462
.L1526:
	mov	w20, 1
	b	.L1461
.L1462:
	ldrh	w2, [x1, 236]
	ldrh	w0, [x1, 3232]
	str	wzr, [x1, 3244]
	cmp	w2, w0
	bls	.L1463
	ldrh	w0, [x1, 3968]
	cbnz	w0, .L1464
	ldr	w0, [x1, 2632]
	ldr	w2, [x1, 2628]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bcs	.L1465
.L1464:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2700]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
.L1553:
	strh	w0, [x1, 3232]
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x1, 80]
	ldrh	w0, [x2, x0]
	cmp	w0, 4
	bls	.L1463
	ldrh	w0, [x1, 3968]
	b	.L1436
.L1465:
	mov	w0, 18
	b	.L1553
.L1463:
	add	x20, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20, 3968]
	cbnz	w0, .L1526
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
	ble	.L1468
	ldrh	w0, [x20, 236]
	sub	w5, w5, #1
	cmp	w0, w5
	blt	.L1468
	bl	FtlReadRefresh
	ldrh	w0, [x20, 3968]
	b	.L1436
.L1468:
	cbnz	w1, .L1526
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x19, 236]
	add	w0, w0, 1
	b	.L1436
.L1471:
	ldrh	w1, [x0, 234]
	cmp	w1, w21
	beq	.L1472
	ldr	x2, [x0, 80]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2, x1]
	cbnz	w1, .L1473
	mov	w1, -1
	strh	w1, [x0, 234]
.L1473:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 234]
	strh	w1, [x0, 184]
	mov	w1, -1
	strh	w1, [x0, 234]
	b	.L1472
.L1480:
	mov	w0, w6
	bl	IsBlkInGcList
	add	w5, w5, 1
	cbz	w0, .L1481
	strh	w5, [x22, 3240]
	b	.L1479
.L1481:
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
	bgt	.L1483
	cmp	w5, 48
	bls	.L1484
	cmp	w3, 8
	bls	.L1484
	ldrh	w3, [x22, 3292]
	cmp	w3, 35
	bhi	.L1484
.L1483:
	strh	wzr, [x22, 3240]
.L1484:
	ldrh	w1, [x2, x1]
	cmp	w0, w1
	bgt	.L1485
	cmp	w21, w25
	bne	.L1485
	ldrh	w0, [x22, 3240]
	cmp	w0, 3
	bhi	.L1485
	mov	w0, -1
	strh	wzr, [x22, 3240]
	strh	w0, [x22, 184]
.L1554:
	ldrh	w0, [x22, 3968]
	b	.L1436
.L1485:
	cbnz	w1, .L1486
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x22, 3240]
	add	w0, w0, 1
	strh	w0, [x22, 3240]
	b	.L1479
.L1486:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0, 192]
	ldrh	w0, [x0, 32]
	cmp	w0, w6
	bne	.L1487
	adrp	x0, .LC8
	mov	w2, 717
	mov	x1, x24
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1487:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 184]
	ldrh	w0, [x0, 88]
	cmp	w1, w0
	bne	.L1488
	adrp	x0, .LC8
	mov	w2, 718
	mov	x1, x24
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1488:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 184]
	ldrh	w0, [x0, 136]
	cmp	w1, w0
	bne	.L1489
	adrp	x0, .LC8
	mov	w2, 719
	mov	x1, x24
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1489:
	add	x22, x19, :lo12:.LANCHOR0
	add	x25, x22, 184
	mov	x0, x25
	bl	make_superblock
	ldrh	w1, [x22, 184]
	ldr	x0, [x22, 80]
	strh	wzr, [x22, 3970]
	ldrh	w0, [x0, x1, lsl 1]
	strh	w0, [x22, 3972]
	strh	wzr, [x22, 186]
	strb	wzr, [x22, 190]
.L1478:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 1
	str	w0, [x1, 3424]
	ldrh	w26, [x1, 314]
	cbz	w23, .L1490
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
.L1490:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 186]
	add	w1, w0, w20
	cmp	w1, w26
	ble	.L1491
	sub	w20, w26, w0
	and	w20, w20, 65535
.L1491:
	mov	w27, 0
.L1492:
	cmp	w20, w27, uxth
	bls	.L1500
	add	x0, x19, :lo12:.LANCHOR0
	mov	w25, 0
	add	x3, x0, 200
	mov	w1, 0
	mov	w6, 65535
	ldrh	w4, [x0, 186]
	ldrh	w7, [x0, 244]
	add	w4, w4, w27
	b	.L1501
.L1494:
	ldrh	w2, [x3]
	cmp	w2, w6
	beq	.L1493
	ldr	x5, [x0, 3280]
	ubfiz	x8, x25, 5, 16
	add	w25, w25, 1
	orr	w2, w4, w2, lsl 10
	add	x5, x5, x8
	and	w25, w25, 65535
	str	w2, [x5, 4]
.L1493:
	add	w1, w1, 1
	add	x3, x3, 2
	and	w1, w1, 65535
.L1501:
	cmp	w1, w7
	bne	.L1494
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, w25
	mov	x28, 0
	ldrb	w2, [x22, 192]
	ldr	x0, [x22, 3280]
	bl	FlashReadPages
	ubfiz	x0, x25, 5, 16
	str	x0, [x29, 104]
.L1495:
	ldr	x0, [x29, 104]
	cmp	x0, x28
	bne	.L1499
	add	w27, w27, 1
	b	.L1492
.L1499:
	ldr	x0, [x22, 3280]
	add	x1, x0, x28
	ldr	w0, [x0, x28]
	cmn	w0, #1
	beq	.L1496
	ldr	x25, [x1, 16]
	mov	w0, 61589
	ldrh	w1, [x25]
	cmp	w1, w0
	bne	.L1496
	ldr	w4, [x25, 8]
	cmn	w4, #1
	bne	.L1497
	str	w4, [x29, 96]
	mov	w2, 753
	mov	x1, x24
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	ldr	w4, [x29, 96]
.L1497:
	mov	w2, 0
	add	x1, x29, 120
	mov	w0, w4
	bl	log2phys
	ldr	x0, [x22, 3280]
	ldr	w1, [x29, 120]
	add	x0, x0, x28
	ldr	w2, [x0, 4]
	cmp	w2, w1
	bne	.L1496
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
	str	w0, [x25, 12]
	ldrh	w0, [x22, 136]
	strh	w0, [x25, 2]
	ldr	w0, [x22, 2672]
	str	w0, [x25, 4]
	ldr	w0, [x22, 3248]
	add	w0, w0, 1
	str	w0, [x22, 3248]
	ldr	x0, [x22, 3280]
	add	x0, x0, x28
	bl	FtlGcBufAlloc
	ldrb	w1, [x22, 143]
	ldr	w0, [x22, 3248]
	cmp	w1, w0
	beq	.L1498
	ldrh	w0, [x22, 140]
	cbnz	w0, .L1496
.L1498:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1496
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19, 186]
	strh	w0, [x19, 184]
	str	wzr, [x19, 3424]
	ldrh	w0, [x19, 3968]
	b	.L1436
.L1496:
	add	x28, x28, 32
	b	.L1495
.L1500:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 186]
	add	w20, w20, w0
	and	w20, w20, 65535
	strh	w20, [x22, 186]
	cmp	w26, w20
	bhi	.L1502
	ldr	w0, [x22, 3248]
	cbz	w0, .L1503
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1503
	str	wzr, [x22, 3424]
	b	.L1554
.L1503:
	add	x20, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20, 3970]
	cbnz	w0, .L1504
	ldrh	w1, [x20, 184]
	ldr	x3, [x20, 80]
	ubfiz	x2, x1, 1, 16
	ldrh	w4, [x3, x2]
	cbz	w4, .L1504
	ldrh	w5, [x20, 3972]
	mov	w22, 0
	ldrh	w2, [x20, 186]
	mov	w3, 0
	adrp	x0, .LC125
	add	x0, x0, :lo12:.LC125
	bl	sftl_printk
.L1505:
	ldr	w0, [x20, 2632]
	cmp	w22, w0
	bcs	.L1507
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w22
	bl	log2phys
	ldr	w4, [x29, 124]
	cmn	w4, #1
	beq	.L1506
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20, 184]
	cmp	w1, w0, uxth
	bne	.L1506
	adrp	x0, .LC126
	mov	w2, w4
	mov	w1, w22
	add	x0, x0, :lo12:.LC126
	bl	sftl_printk
.L1507:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0, 2632]
	cmp	w22, w1
	bcc	.L1504
	ldrh	w2, [x0, 184]
	ldr	x1, [x0, 80]
	strh	wzr, [x1, x2, lsl 1]
	ldrh	w0, [x0, 184]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1504:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 184]
.L1502:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 236]
	str	wzr, [x0, 3424]
	cmp	w1, 2
	bhi	.L1510
	ldrh	w20, [x0, 314]
	b	.L1511
.L1506:
	add	w22, w22, 1
	b	.L1505
.L1510:
	ldrh	w0, [x0, 3968]
	cmp	w0, 0
	csinc	w0, w0, w1, ne
	b	.L1436
.L1514:
	mov	w0, 0
	b	.L1436
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
	bne	.L1556
	mov	x2, x3
	mov	w1, w25
	add	w0, w19, 256
	bl	FtlVendorPartRead
	mov	w21, w0
.L1555:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L1556:
	add	w0, w1, w2
	str	w0, [x29, 128]
	adrp	x0, .LANCHOR0
	add	x3, x0, :lo12:.LANCHOR0
	add	w2, w1, w2
	str	x0, [x29, 96]
	ldr	w1, [x3, 352]
	cmp	w2, w1
	bhi	.L1577
	adrp	x0, .LANCHOR2
	ldr	w21, [x0, #:lo12:.LANCHOR2]
	cmn	w21, #1
	beq	.L1555
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
.L1558:
	cbnz	w22, .L1575
	ldr	x0, [x29, 96]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3320]
	cbnz	w1, .L1576
	ldrh	w0, [x0, 236]
	cmp	w0, 31
	bhi	.L1555
.L1576:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	b	.L1555
.L1575:
	add	x1, x29, 156
	mov	w2, 0
	mov	w0, w20
	bl	log2phys
	ldr	w1, [x29, 156]
	cmn	w1, #1
	bne	.L1559
	mov	w5, 0
.L1560:
	ldrh	w0, [x26, 270]
	cmp	w5, w0
	bcc	.L1562
.L1563:
	add	w20, w20, 1
	subs	w22, w22, #1
	beq	.L1567
	ldrh	w0, [x26, 244]
	cmp	w28, w0, lsl 2
	bne	.L1558
.L1567:
	cbz	w28, .L1558
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
.L1574:
	ldr	x0, [x26, 3440]
	add	x0, x0, x28
	ldr	w1, [x0, 24]
	cmp	w27, w1
	bne	.L1569
	ldr	x1, [x0, 8]
	ldr	x0, [x26, 3488]
	cmp	x1, x0
	bne	.L1570
	ldr	x0, [x29, 120]
	ldr	w2, [x29, 116]
	add	x1, x1, x0
	mov	x0, x23
.L1587:
	bl	ftl_memcpy
.L1570:
	ldr	x0, [x26, 3440]
	add	x0, x0, x28
	ldr	x1, [x0, 16]
	ldr	w2, [x0, 24]
	ldr	w1, [x1, 8]
	cmp	w2, w1
	beq	.L1571
	ldr	w1, [x26, 2792]
	add	w1, w1, 1
	str	w1, [x26, 2792]
.L1571:
	ldr	w2, [x0]
	cmn	w2, #1
	bne	.L1572
	ldr	w1, [x26, 2792]
	mov	w21, w2
	add	w1, w1, 1
	str	w1, [x26, 2792]
.L1572:
	ldr	w1, [x0]
	cmp	w1, 256
	bne	.L1573
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1573:
	ldr	x0, [x29, 104]
	add	x28, x28, 32
	cmp	x0, x28
	bne	.L1574
	mov	w28, 0
	b	.L1558
.L1562:
	madd	w0, w20, w0, w5
	cmp	w19, w0
	bhi	.L1561
	ldr	w1, [x29, 128]
	cmp	w1, w0
	bls	.L1561
	sub	w0, w0, w19
	str	w5, [x29, 120]
	lsl	w0, w0, 9
	mov	w2, 512
	mov	w1, 0
	add	x0, x23, x0
	bl	ftl_memset
	ldr	w5, [x29, 120]
.L1561:
	add	w5, w5, 1
	b	.L1560
.L1559:
	ldr	x0, [x26, 3440]
	ubfiz	x2, x28, 5, 32
	cmp	w20, w27
	add	x0, x0, x2
	str	w1, [x0, 4]
	ldrh	w0, [x26, 270]
	bne	.L1564
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
	bne	.L1565
	str	x23, [x1, 8]
.L1565:
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
	b	.L1563
.L1564:
	cmp	w20, w24
	bne	.L1566
	ldr	x5, [x26, 3440]
	ldr	x1, [x26, 3496]
	add	x5, x5, x2
	ldr	w3, [x29, 128]
	str	x1, [x5, 8]
	mul	w1, w20, w0
	sub	w3, w3, w1
	str	w3, [x29, 132]
	cmp	w0, w3
	bne	.L1565
	sub	w1, w1, w19
	lsl	w1, w1, 9
	add	x1, x23, x1
	str	x1, [x5, 8]
	b	.L1565
.L1566:
	ldr	x1, [x26, 3440]
	mul	w0, w0, w20
	add	x1, x1, x2
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	str	x0, [x1, 8]
	b	.L1565
.L1569:
	cmp	w24, w1
	bne	.L1570
	ldr	x1, [x0, 8]
	ldr	x0, [x26, 3496]
	cmp	x1, x0
	bne	.L1570
	ldrh	w0, [x26, 270]
	ldr	w2, [x29, 112]
	mul	w0, w0, w24
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	b	.L1587
.L1577:
	mov	w21, -1
	b	.L1555
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
	bne	.L1591
	add	w0, w1, 256
	ldr	w1, [x29, 172]
	mov	x2, x3
	bl	FtlVendorPartWrite
.L1590:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	ret
.L1591:
	ldr	w0, [x29, 172]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	add	w0, w23, w0
	str	w0, [x29, 148]
	ldr	w0, [x1, 352]
	ldr	w2, [x29, 148]
	cmp	w2, w0
	bhi	.L1627
	adrp	x2, .LANCHOR2
	str	x2, [x29, 136]
	add	x3, x2, :lo12:.LANCHOR2
	ldr	w0, [x2, #:lo12:.LANCHOR2]
	cmn	w0, #1
	beq	.L1590
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
	cbz	w0, .L1593
	ldrh	w0, [x20, 4]
	add	x1, x1, 88
	cmp	w0, 0
	csel	x20, x1, x20, eq
.L1593:
	adrp	x0, .LANCHOR1
	mov	w21, w25
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 696
	str	x0, [x29, 160]
.L1594:
	cbnz	w24, .L1622
	ldr	w1, [x29, 132]
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x19, 236]
	cmp	w0, 5
	bls	.L1623
.L1625:
	mov	w0, 0
	b	.L1590
.L1622:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 244]
	cmp	w1, w0
	bcc	.L1595
	ldr	x1, [x29, 160]
	adrp	x0, .LC8
	mov	w2, 1038
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1595:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1596
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 32
	cmp	x20, x1
	bne	.L1597
	add	x0, x0, 88
	ldrh	w1, [x0, 4]
	cbnz	w1, .L1598
	bl	allocate_new_data_superblock
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
.L1598:
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
.L1599:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1596
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1596:
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
	bcc	.L1600
	ldr	x1, [x29, 160]
	adrp	x0, .LC8
	mov	w2, 1071
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1600:
	mov	x28, 0
.L1601:
	ldr	w0, [x29, 128]
	mov	w22, w28
	cmp	w28, w0
	bcc	.L1620
	mov	x22, x0
.L1602:
	add	x0, x19, :lo12:.LANCHOR0
	mov	x3, x20
	mov	w2, 0
	mov	w1, w22
	ldr	x0, [x0, 3456]
	bl	FtlProgPages
	cmp	w24, w22
	bcs	.L1621
	ldr	x1, [x29, 160]
	adrp	x0, .LC8
	mov	w2, 1149
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1621:
	sub	w24, w24, w22
	b	.L1594
.L1597:
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1632
	mov	x0, x20
	bl	allocate_new_data_superblock
	b	.L1599
.L1632:
	mov	x20, x1
	b	.L1596
.L1620:
	ldrh	w0, [x20, 4]
	cbz	w0, .L1602
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
	bne	.L1603
	cmp	w21, w25
	bne	.L1604
	ldrh	w2, [x3, 270]
	udiv	w0, w23, w2
	msub	w0, w0, w2, w23
	str	w0, [x29, 168]
	sub	w2, w2, w0
	ldr	w0, [x29, 172]
	cmp	w2, w0
	csel	w0, w2, w0, ls
	str	w0, [x29, 152]
.L1605:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29, 152]
	ldrh	w1, [x0, 270]
	cmp	w2, w1
	bne	.L1606
	ldr	x0, [x0, 3456]
	cmp	w21, w25
	add	x4, x0, x27
	bne	.L1607
	str	x26, [x4, 8]
.L1608:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, 244]
	cmp	w1, w0
	bcc	.L1617
	ldr	x1, [x29, 160]
	adrp	x0, .LC8
	mov	w2, 1140
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1617:
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
	b	.L1601
.L1604:
	ldr	w0, [x29, 148]
	ldrh	w2, [x3, 270]
	str	wzr, [x29, 168]
	msub	w2, w21, w2, w0
	and	w0, w2, 65535
	str	w0, [x29, 152]
	b	.L1605
.L1607:
	ldr	w0, [x29, 152]
.L1639:
	mul	w0, w0, w21
	sub	w0, w0, w23
	lsl	w0, w0, 9
	add	x0, x26, x0
	str	x0, [x4, 8]
	b	.L1608
.L1606:
	ldr	x1, [x0, 3456]
	cmp	w21, w25
	add	x1, x1, x27
	bne	.L1609
	ldr	x0, [x0, 3488]
.L1638:
	str	x0, [x1, 8]
	ldr	w0, [x29, 188]
	cmn	w0, #1
	beq	.L1611
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
	bne	.L1612
	ldr	w0, [x5, 2792]
	mov	w2, w21
	add	w0, w0, 1
	str	w0, [x5, 2792]
	adrp	x0, .LC127
	add	x0, x0, :lo12:.LC127
	ldr	w1, [x22, 8]
	bl	sftl_printk
.L1615:
	ldr	w0, [x29, 152]
	cmp	w21, w25
	lsl	w2, w0, 9
	add	x0, x19, :lo12:.LANCHOR0
	bne	.L1616
	ldr	x0, [x0, 3456]
	mov	x1, x26
	add	x4, x0, x27
	ldr	w0, [x29, 168]
	ldr	x3, [x4, 8]
	lsl	w0, w0, 9
	add	x0, x3, x0
.L1640:
	bl	ftl_memcpy
	b	.L1608
.L1609:
	ldr	x0, [x0, 3496]
	b	.L1638
.L1612:
	ldr	w0, [x22, 8]
	cmp	w21, w0
	beq	.L1614
	ldr	w0, [x5, 2792]
	mov	w2, w21
	add	w0, w0, 1
	str	w0, [x5, 2792]
	adrp	x0, .LC128
	add	x0, x0, :lo12:.LC128
	ldr	w1, [x22, 8]
	bl	sftl_printk
.L1614:
	ldr	w0, [x22, 8]
	cmp	w21, w0
	beq	.L1615
	ldr	x1, [x29, 160]
	mov	w2, 1125
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L1615
.L1611:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x0, [x1, 3456]
	ldrh	w2, [x1, 322]
	mov	w1, 0
	add	x0, x0, x27
	ldr	x0, [x0, 8]
	bl	ftl_memset
	b	.L1615
.L1616:
	ldrh	w1, [x0, 270]
	ldr	x0, [x0, 3456]
	add	x4, x0, x27
	mul	w1, w1, w21
	sub	w1, w1, w23
	ldr	x0, [x4, 8]
	lsl	w1, w1, 9
	add	x1, x26, x1
	b	.L1640
.L1603:
	ldr	x0, [x3, 3456]
	add	x4, x0, x27
	ldrh	w0, [x3, 270]
	b	.L1639
.L1623:
	mov	w20, 256
	mov	w21, 65535
	mov	w22, 128
.L1626:
	ldrh	w0, [x19, 184]
	cmp	w0, w21
	bne	.L1624
	ldrh	w0, [x19, 234]
	cmp	w0, w21
	bne	.L1624
	mov	w0, 0
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L1624:
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
	bhi	.L1625
	subs	w20, w20, #1
	bne	.L1626
	b	.L1625
.L1627:
	mov	w0, -1
	b	.L1590
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
	bne	.L1644
.L1655:
	mov	w0, -1
.L1643:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1644:
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
.L1646:
	tbz	w19, #31, .L1652
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1465
	add	x1, x1, 712
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	b	.L1651
.L1652:
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
	cbz	w7, .L1647
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L1647
	ldrh	w1, [x21, 322]
	ldr	x0, [x20, 8]
	bl	js_hash
	cmp	w7, w0
	beq	.L1647
	ldrh	w4, [x21, 2708]
	mov	w6, w0
	mov	w5, w7
	adrp	x0, .LC129
	mov	w3, w19
	mov	w2, w23
	mov	x1, x24
	add	x0, x0, :lo12:.LC129
	bl	sftl_printk
	cbnz	w19, .L1648
	ldrh	w0, [x21, 2708]
	cmp	w23, w0
	beq	.L1648
	ldrh	w19, [x21, 316]
	sxth	w23, w0
.L1650:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1646
.L1648:
	mov	w0, -1
	str	w0, [x20]
.L1647:
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L1650
	ldr	x0, [x21, 3464]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1650
	ldr	x0, [x21, 3528]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L1650
.L1651:
	add	x1, x22, :lo12:.LANCHOR0
	ldrh	w0, [x1, 252]
	ldrh	w1, [x1, 322]
	add	x0, x0, 24
	cmp	x1, x0, lsl 1
	bcs	.L1654
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1467
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1654:
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
	bne	.L1655
	ldrh	w1, [x19, 2528]
	ldrb	w2, [x19, 2530]
	ldrh	w0, [x19, 266]
	strh	w1, [x19, 2710]
	cmp	w2, w0
	bne	.L1655
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
	bls	.L1656
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC8
	mov	w2, 1489
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
.L1656:
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
	bls	.L1657
	str	w1, [x0, 2668]
.L1657:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x0, 2556]
	ldr	w2, [x0, 2672]
	cmp	w1, w2
	bls	.L1658
	str	w1, [x0, 2672]
.L1658:
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1659
	add	x0, x22, :lo12:.LANCHOR0
	add	x0, x0, 32
	bl	make_superblock
.L1659:
	add	x1, x22, :lo12:.LANCHOR0
	add	x0, x1, 88
	ldrh	w2, [x1, 88]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1660
	bl	make_superblock
.L1660:
	add	x1, x22, :lo12:.LANCHOR0
	add	x0, x1, 136
	ldrh	w2, [x1, 136]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1661
	bl	make_superblock
.L1661:
	add	x22, x22, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x0, x22, 184
	ldrh	w2, [x22, 184]
	cmp	w2, w1
	beq	.L1662
	bl	make_superblock
.L1662:
	mov	w0, 0
	b	.L1643
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
.L1675:
	ldr	w0, [x29, 136]
	cmp	w20, w0
	bge	.L1694
	ldr	w0, [x29, 116]
	sxtw	x28, w20
	cmp	w20, w0
	bne	.L1676
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
.L1677:
	ldr	w0, [x29, 120]
	add	w0, w0, 1
	cmp	w26, w0
	blt	.L1680
.L1694:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x0, x19
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 2]
	ldrh	w0, [x21, 316]
	cmp	w1, w0
	bne	.L1682
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1682:
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
.L1680:
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
	cbz	w8, .L1678
	ldr	w0, [x27]
	cmn	w0, #1
	beq	.L1678
	ldrh	w1, [x28, 322]
	ldr	x0, [x27, 8]
	bl	js_hash
	cmp	w8, w0
	beq	.L1678
	mov	w5, w0
	mov	w4, w8
	mov	w3, w26
	mov	w2, w20
	mov	x1, x24
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x27]
.L1678:
	ldr	w0, [x27]
	cmn	w0, #1
	beq	.L1679
	ldrh	w0, [x22, 8]
	cmp	w25, w0
	bls	.L1679
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L1679
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x27, 4]
	str	w1, [x23, x0]
.L1679:
	add	w7, w26, 1
	sxth	w26, w7
	b	.L1677
.L1676:
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
	beq	.L1696
	ldrh	w1, [x22]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L1696
	ldrh	w1, [x22, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L1684
.L1696:
	add	x26, x21, :lo12:.LANCHOR0
	mov	w27, 0
	add	x28, x26, 3728
.L1685:
	ldrh	w0, [x26, 316]
	cmp	w27, w0
	bge	.L1692
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
	cbz	w7, .L1689
	ldr	w0, [x28]
	cmn	w0, #1
	beq	.L1689
	ldrh	w1, [x26, 322]
	ldr	x0, [x28, 8]
	bl	js_hash
	cmp	w7, w0
	beq	.L1689
	ldr	x1, [x29, 104]
	mov	w5, w0
	mov	w4, w7
	mov	w3, w27
	mov	w2, w20
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x28]
.L1689:
	ldr	w0, [x28]
	cmn	w0, #1
	beq	.L1690
	ldrh	w0, [x22, 8]
	cmp	w25, w0
	bls	.L1690
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L1690
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x28, 4]
	str	w1, [x23, x0]
.L1690:
	add	w6, w27, 1
	sxth	w27, w6
	b	.L1685
.L1684:
	mov	w0, 0
.L1686:
	ldrh	w1, [x26, 316]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1688
.L1692:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L1675
.L1688:
	ldr	x2, [x26, 3464]
	sbfiz	x3, x0, 3, 32
	ldrh	w1, [x2, x3]
	cmp	w25, w1
	bls	.L1687
	add	x2, x2, x3
	ubfiz	x1, x1, 2, 16
	ldr	w2, [x2, 4]
	str	w2, [x23, x1]
.L1687:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1686
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
	bne	.L1721
.L1723:
	mov	w23, -1
.L1720:
	mov	w0, w23
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1721:
	bl	FtlLoadSysInfo
	mov	w23, w0
	cbnz	w0, .L1723
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
.L1724:
	cmp	w0, w3
	bge	.L1729
	ldr	w2, [x1], 16
	tbz	w2, #31, .L1725
.L1729:
	add	x2, x20, :lo12:.LANCHOR0
	cmp	w0, w3
	ldrh	w1, [x2, 2548]
	add	w1, w1, 1
	strh	w1, [x2, 2548]
	bge	.L1735
.L1726:
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
	b	.L1730
.L1725:
	add	w0, w0, 1
	b	.L1724
.L1735:
	ldrh	w0, [x2, 3760]
	cbnz	w0, .L1726
.L1730:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x21, x19, 32
	ldrh	w0, [x19, 32]
	cmp	w0, w1
	beq	.L1731
	ldrh	w1, [x19, 36]
	cbnz	w1, .L1731
	ldrh	w1, [x19, 92]
	add	x20, x19, 88
	cbnz	w1, .L1731
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 88]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	mov	x0, x21
	bl	allocate_new_data_superblock
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1731:
	bl	FtlVpcCheckAndModify
	b	.L1720
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
	cbz	w0, .L1738
	bl	FtlMakeBbt
.L1738:
	mov	w4, 23752
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 0
	movk	w4, 0xa0f, lsl 16
.L1739:
	ldrh	w2, [x1, 270]
	cmp	w0, w2, lsl 7
	blt	.L1740
	ldrh	w21, [x1, 252]
	add	x22, x20, :lo12:.LANCHOR0
	mov	w19, 0
.L1741:
	ldrh	w0, [x22, 254]
	cmp	w0, w21
	bhi	.L1742
	ldrh	w0, [x22, 244]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L1743
.L1747:
	add	x19, x20, :lo12:.LANCHOR0
	mov	w21, 0
	mov	w22, 0
.L1744:
	ldrh	w0, [x19, 252]
	cmp	w0, w22
	bhi	.L1748
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
	ble	.L1749
	sub	w1, w1, w21
	udiv	w1, w1, w2
	str	w1, [x19, 2632]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x19, 2700]
.L1749:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 306]
	cbz	w1, .L1751
	ldrh	w5, [x0, 2700]
	add	w5, w5, w1, lsr 1
	strh	w5, [x0, 2700]
	mul	w5, w1, w2
	cmp	w21, w5
	bge	.L1751
	add	w1, w1, 32
	str	w4, [x0, 2632]
	add	w1, w3, w1
	strh	w1, [x0, 2700]
.L1751:
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
.L1753:
	mov	x0, x21
	bl	make_superblock
	ldrb	w1, [x21, 7]
	ldrh	w0, [x21]
	cbnz	w1, .L1754
	ldr	x1, [x19, 80]
	ubfiz	x0, x0, 1, 16
	strh	w22, [x1, x0]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L1753
.L1740:
	ldr	x5, [x1, 3488]
	ubfiz	x3, x0, 2, 16
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w2, [x5, x3]
	ldr	x2, [x1, 3496]
	str	w4, [x2, x3]
	b	.L1739
.L1742:
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w19, w19, w0
	and	w21, w21, 65535
	and	w19, w19, 65535
	b	.L1741
.L1743:
	udiv	w0, w19, w0
	ldr	w19, [x22, 344]
	add	x21, x20, :lo12:.LANCHOR0
	add	w0, w0, w19
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22, 248]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w19, [x22, 252]
.L1745:
	ldrh	w0, [x21, 254]
	cmp	w0, w19
	bls	.L1747
	mov	w0, w19
	add	w19, w19, 1
	mov	w1, 1
	and	w19, w19, 65535
	bl	FtlLowFormatEraseBlock
	b	.L1745
.L1748:
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w21, w21, w0
	and	w22, w22, 65535
	and	w21, w21, 65535
	b	.L1744
.L1754:
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
.L1755:
	mov	x0, x20
	bl	make_superblock
	ldrb	w1, [x20, 7]
	ldrh	w0, [x20]
	cbnz	w1, .L1756
	ldr	x1, [x19, 80]
	ubfiz	x0, x0, 1, 16
	strh	w21, [x1, x0]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L1755
.L1756:
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
	cbnz	w0, .L1757
	adrp	x0, .LANCHOR2
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR2]
.L1757:
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
	cbnz	w0, .L1766
	bl	FtlSysBlkInit
	cbnz	w0, .L1766
	mov	w0, 1
	str	w0, [x20, #:lo12:.LANCHOR2]
	ldrh	w0, [x19, 236]
	cmp	w0, 15
	bhi	.L1766
	mov	w19, 8129
.L1765:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L1765
.L1766:
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
	bls	.L1770
	mov	w19, w0
	cmp	w0, 575
	bls	.L1771
.L1770:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x0, 3984]
	cbnz	w1, .L1772
.L1838:
	mov	w20, 0
	b	.L1769
.L1772:
	ldr	x26, [x0, 3992]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	ldr	w2, [x26]
	cmp	w2, w1
	bne	.L1774
	ldrh	w1, [x0, 10]
	mov	x0, 262140
	mov	w20, 65535
	add	x2, x26, x0
	mov	w3, 4097
	mov	w0, 0
.L1778:
	ldr	w4, [x2]
	cbnz	w4, .L1775
	ldr	w4, [x26, w0, uxtw 2]
	add	w0, w0, 1
	str	w4, [x2], -4
	cmp	w0, w3
	sub	w20, w20, #1
	csel	w0, w0, wzr, cc
	cmp	w20, 4096
	bne	.L1778
	mov	w20, 512
	b	.L1777
.L1775:
	add	w20, w20, 127
	lsr	w20, w20, 7
.L1777:
	ubfiz	w0, w1, 2, 14
	add	w22, w20, 4
	add	x19, x21, :lo12:.LANCHOR0
	mov	w23, 0
	str	wzr, [x29, 140]
	udiv	w22, w22, w0
	lsl	w0, w20, 7
	str	w0, [x29, 128]
	add	w22, w22, 1
.L1800:
	ldr	x0, [x19, 4000]
	mov	w1, 0
	mov	x2, 512
	bl	memset
	ldrh	w28, [x19, 10]
	ldr	x1, [x19, 3400]
	mul	w24, w28, w23
	cbz	x1, .L1808
	ldr	x0, [x19, 3408]
	mov	w25, 6
	cmp	x0, 0
	mov	w0, 9
	csel	w25, w25, w0, eq
.L1779:
	ldr	x3, [x19, 3376]
	mov	w1, w24
	mov	w0, 0
	blr	x3
	cmp	w22, 1
	beq	.L1780
	ldr	x2, [x19, 3376]
	add	w1, w28, w24
	mov	w0, 0
	blr	x2
.L1780:
	cmp	w25, 9
	bne	.L1810
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
.L1781:
	mul	w0, w22, w28
	mov	x6, x26
	str	w0, [x29, 136]
	mov	x28, 0
	add	w0, w24, 1
	str	w0, [x29, 132]
.L1782:
	ldr	w0, [x29, 136]
	mov	w1, w28
	cmp	w28, w0
	bcs	.L1789
	cmp	w25, 9
	bne	.L1783
	ldr	w0, [x29, 132]
	add	w0, w0, w1
.L1836:
	str	w0, [x29, 144]
	mov	w0, 61424
	str	w0, [x29, 148]
	cmp	w1, 0
	ccmp	w25, 9, 0, eq
	bne	.L1785
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
	bne	.L1786
.L1789:
	ldrb	w0, [x19, 14]
	str	w0, [x29, 120]
	ldrh	w0, [x19, 10]
	ldr	x25, [x19, 4000]
	mul	w1, w0, w23
	str	w1, [x29, 136]
	add	x1, x19, 3368
	ldr	x2, [x1, 32]
	cbnz	x2, .L1787
	mov	w1, 6
.L1837:
	mul	w0, w22, w0
	mov	x28, 0
	mov	w27, 61424
	str	w0, [x29, 112]
	str	w1, [x29, 132]
.L1791:
	ldr	w0, [x29, 112]
	mov	w1, w28
	cmp	w28, w0
	bcs	.L1795
	ldr	w0, [x29, 132]
	cmp	w28, 0
	ccmp	w0, 9, 0, eq
	bne	.L1792
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
	beq	.L1793
.L1795:
	ldr	x0, [x19, 4000]
	mov	x1, x26
	mov	w24, 0
	mov	x2, x0
.L1794:
	mov	x25, x2
	mov	x28, x1
	add	x2, x2, 4
	add	x1, x1, 4
	ldr	w4, [x25]
	ldr	w3, [x28]
	cmp	w4, w3
	beq	.L1797
	mov	x2, 512
	mov	w1, 0
	bl	memset
	ldr	w2, [x25]
	mov	w1, w23
	ldr	w3, [x28]
	mov	w4, w24
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	bl	sftl_printk
	ldrh	w1, [x19, 10]
	mov	w0, 0
	ldr	x2, [x19, 3376]
	mul	w1, w1, w23
	blr	x2
.L1798:
	add	w23, w23, w22
	cmp	w23, 15
	bls	.L1800
	ldr	w0, [x29, 140]
	cbnz	w0, .L1801
.L1774:
	mov	w20, -1
.L1801:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21, 3992]
	str	wzr, [x21, 3984]
	bl	kfree
	ldr	x0, [x21, 4000]
	bl	kfree
.L1769:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	ret
.L1808:
	mov	w25, 6
	b	.L1779
.L1810:
	mov	x27, 0
	b	.L1781
.L1783:
	add	w0, w1, w24
	lsl	w0, w0, 2
	b	.L1836
.L1785:
	ldr	x7, [x19, 3384]
	add	x3, x29, 144
	mov	x2, x6
	add	w1, w1, w24
	mov	w0, 0
	str	x6, [x29, 120]
	blr	x7
	cmn	w0, #1
	beq	.L1789
	ldr	x6, [x29, 120]
	add	x6, x6, 2048
.L1786:
	add	x28, x28, 1
	b	.L1782
.L1787:
	ldr	x1, [x1, 40]
	mov	w24, 6
	cmp	x1, 0
	mov	w1, 9
	csel	w1, w24, w1, eq
	b	.L1837
.L1792:
	ldr	x6, [x19, 3392]
	add	x3, x29, 144
	ldr	w0, [x29, 136]
	mov	x2, x25
	add	w1, w1, w0
	mov	w0, 0
	blr	x6
	cmn	w0, #1
	beq	.L1795
	ldr	w0, [x29, 148]
	cmp	w0, w27
	bne	.L1795
	add	x25, x25, 2048
.L1793:
	add	x28, x28, 1
	b	.L1791
.L1797:
	ldr	w3, [x29, 128]
	add	w24, w24, 1
	cmp	w24, w3
	bne	.L1794
	ldr	w0, [x29, 140]
	add	w0, w0, 1
	str	w0, [x29, 140]
	cmp	w0, 5
	bls	.L1798
	b	.L1801
.L1771:
	mov	w22, w1
	mov	x23, x2
	cmp	w0, 64
	bne	.L1802
	add	x20, x21, :lo12:.LANCHOR0
	mov	w0, 262144
	bl	ftl_malloc
	str	x0, [x20, 3992]
	mov	w0, 262144
	bl	ftl_malloc
	mov	x1, x0
	ldr	x0, [x20, 3992]
	str	x1, [x20, 4000]
	cbz	x0, .L1803
	cbz	x1, .L1803
	mov	w1, 1
	mov	w2, 262144
	str	w1, [x20, 3984]
	mov	w1, 0
	bl	ftl_memset
.L1802:
	add	x21, x21, :lo12:.LANCHOR0
	ldr	w0, [x21, 3984]
	cbz	w0, .L1838
	cmp	w19, 63
	ldr	x1, [x21, 3992]
	bhi	.L1805
	mov	w0, 64
	sub	w19, w0, w19
	sub	w22, w22, w19
	ubfiz	x19, x19, 9, 25
	add	x23, x23, x19
	mov	x20, x1
.L1806:
	cmp	w25, 575
	bls	.L1807
	sub	w22, w22, w24
	sub	w22, w22, #446
.L1807:
	mov	w4, w22
	mov	x3, x20
	mov	w2, 262144
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
	bl	sftl_printk
	lsl	w2, w22, 9
	mov	x1, x23
	mov	x0, x20
	bl	ftl_memcpy
	b	.L1838
.L1803:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 752
	adrp	x0, .LC133
	add	x0, x0, :lo12:.LC133
	bl	sftl_printk
	b	.L1802
.L1805:
	lsl	w20, w19, 7
	sub	w20, w20, #8192
	add	x20, x1, x20, lsl 2
	b	.L1806
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
	cbnz	x3, .L1843
	str	x0, [x2, 4008]
	mov	w0, 0
	str	x1, [x2, 4016]
	ret
.L1843:
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
	cbz	x0, .L1850
	mov	w26, 22084
	mov	w25, 0
	mov	w24, 0
	mov	w23, 0
	movk	w26, 0x524b, lsl 16
.L1848:
	ldr	x3, [x22, 4008]
	mov	w1, 128
	ldr	x2, [x22, 4024]
	lsl	w0, w23, 7
	blr	x3
	mov	w21, w0
	cbnz	w0, .L1846
	ldr	x19, [x22, 4024]
	ldr	w0, [x19]
	cmp	w0, w26
	bne	.L1847
	add	x0, x19, 61440
	ldr	w1, [x19, 4]
	ldr	w0, [x0, 4092]
	cmp	w0, w1
	bne	.L1847
	cmp	w0, w24
	bls	.L1847
	mov	w25, w23
	mov	w24, w0
.L1847:
	add	w23, w23, 1
	cmp	w23, 2
	bne	.L1848
	cbz	w24, .L1849
	add	x0, x20, :lo12:.LANCHOR0
	mov	x2, x19
	mov	w1, 128
	ldr	x3, [x0, 4008]
	lsl	w0, w25, 7
	blr	x3
	mov	w21, w0
	cbz	w0, .L1844
.L1846:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w21, -1
	ldr	x0, [x20, 4024]
	bl	kfree
	str	xzr, [x20, 4024]
	b	.L1844
.L1849:
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
.L1844:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1850:
	mov	w21, -12
	b	.L1844
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	adrp	x3, .LANCHOR0+4024
	ldr	x4, [x3, #:lo12:.LANCHOR0+4024]
	cbz	x4, .L1858
	ldrh	w6, [x4, 10]
	add	x5, x4, 16
	mov	w3, 0
.L1855:
	cmp	w3, w6
	bcc	.L1857
.L1858:
	mov	w0, -1
	ret
.L1857:
	ldrh	w7, [x5], 8
	cmp	w7, w0
	bne	.L1856
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
.L1856:
	add	w3, w3, 1
	b	.L1855
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
	cbz	x19, .L1878
	add	w21, w2, 63
	ldrh	w3, [x19, 10]
	ldrh	w24, [x19, 8]
	mov	x4, x1
	mov	w28, w2
	and	w21, w21, -64
	add	x1, x19, 16
	mov	w20, 0
.L1865:
	cmp	w20, w3
	bcc	.L1873
	ldrh	w1, [x19, 14]
	cmp	w21, w1
	bhi	.L1878
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
	b	.L1880
.L1873:
	ldrh	w6, [x1], 8
	cmp	w6, w0
	bne	.L1866
	uxtw	x27, w20
	add	x5, x19, 1024
	add	x22, x19, x27, lsl 3
	ldrh	w25, [x22, 20]
	add	w25, w25, 63
	and	w25, w25, -64
	cmp	w28, w25
	bls	.L1867
	ldrh	w0, [x19, 14]
	cmp	w21, w0
	bhi	.L1878
	add	x27, x27, 2
	ldrh	w22, [x22, 18]
	add	x27, x19, x27, lsl 3
	sub	w26, w3, #1
.L1868:
	cmp	w20, w26
	bcc	.L1869
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
.L1880:
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
.L1863:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1869:
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
	b	.L1868
.L1867:
	ldrh	w0, [x22, 18]
	uxtw	x2, w28
	mov	x1, x4
	add	x0, x5, x0
	bl	memcpy
	strh	w28, [x22, 20]
	b	.L1880
.L1866:
	add	w20, w20, 1
	b	.L1865
.L1878:
	mov	w0, -1
	b	.L1863
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
	cbz	x0, .L1898
	mov	w1, 30209
	mov	x19, x0
	movk	w1, 0x4004, lsl 16
	cmp	w21, w1
	beq	.L1884
	add	w1, w1, 1
	cmp	w21, w1
	beq	.L1885
.L1908:
	mov	x20, -14
	b	.L1883
.L1884:
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
	cbz	x3, .L1886
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbz	x0, .L1909
.L1887:
	mov	x2, x20
	mov	w1, 256
.L1913:
	adrp	x0, .LC135
	add	x0, x0, :lo12:.LC135
	bl	sftl_printk
	b	.L1908
.L1886:
	str	xzr, [x0]
	b	.L1887
.L1909:
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L1889
.L1890:
	mov	x20, -1
.L1883:
	mov	x0, x19
	bl	kfree
.L1881:
	mov	x0, x20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1889:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_read
	cmn	w0, #1
	beq	.L1890
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
	cbz	x3, .L1908
	mov	x0, x20
	mov	x1, x19
	mov	x20, 0
	bl	__arch_copy_to_user
	cbz	x0, .L1883
	b	.L1908
.L1885:
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
	cbz	x3, .L1892
	mov	x2, 8
	mov	x1, x20
	bl	__arch_copy_from_user
	cbz	x0, .L1910
.L1893:
	mov	x2, x20
	mov	w1, 276
	b	.L1913
.L1892:
	str	xzr, [x0]
	b	.L1893
.L1910:
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L1890
	ldrh	w2, [x19, 6]
	cmp	w2, 4087
	bhi	.L1890
	add	w2, w2, 8
	mov	x0, x20
	sxtw	x2, w2
	ldr	x1, [x21, 8]
#APP
// 413 "./arch/arm64/include/asm/uaccess.h" 1
	adds x0, x0, x2; ccmp x0, x1, #2, cc; cset x3, ls
// 0 "" 2
#NO_APP
	cbz	x3, .L1895
	mov	x1, x20
	mov	x0, x19
	bl	__arch_copy_from_user
	cbz	x0, .L1911
.L1896:
	mov	x2, x20
	mov	w1, 283
	b	.L1913
.L1895:
	mov	w1, 0
	mov	x0, x19
	bl	memset
	b	.L1896
.L1911:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_write
	sxtw	x20, w0
	b	.L1883
.L1898:
	mov	x20, -1
	b	.L1881
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
	.xword	.LC136
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
	.string	"g_gc_superblock_free %x %x %x %x %x\n"
.LC126:
	.string	"lpa=%x, ppa=%x\n"
.LC127:
	.string	"FtlWrite: ecc error:%x %x %x\n"
.LC128:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC129:
	.string	"%s hash error this.id =%x page =%x pre_id =%x hash =%x hash_r =%x\n"
.LC130:
	.string	"%s last blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
.LC131:
	.string	"%s scan blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
.LC132:
	.string	"write_idblock fail! %x %x %x %x\n"
.LC133:
	.string	"%s idb buffer alloc fail\n"
.LC134:
	.string	"%p %x %p %x\n"
.LC135:
	.string	"copy_from_user error %d %p %p\n"
.LC136:
	.string	"vendor_storage"
