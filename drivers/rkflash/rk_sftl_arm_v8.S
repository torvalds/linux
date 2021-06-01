/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018-2021 Rockchip Electronics Co. Ltd.
 * date: 2021-06-01
 */
	.arch armv8-a
	.file	"rk_sftl.c"
	.text
	.align	2
	.type	uaccess_disable_not_uao, %function
uaccess_disable_not_uao:
	hint	25 // paciasp
#APP
// 271 "./arch/arm64/include/asm/uaccess.h" 1
	.if 1 == 1
661:
	nop
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 10
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
	.inst (0xd500401f | ((0) << 16 | (4) << 5) | ((!!1) << 8))
	
664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	hint	29 // autiasp
	ret
	.size	uaccess_disable_not_uao, .-uaccess_disable_not_uao
	.align	2
	.type	uaccess_enable_not_uao, %function
uaccess_enable_not_uao:
	hint	25 // paciasp
#APP
// 276 "./arch/arm64/include/asm/uaccess.h" 1
	.if 1 == 1
661:
	nop
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 10
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
	.inst (0xd500401f | ((0) << 16 | (4) << 5) | ((!!0) << 8))
	
664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	hint	29 // autiasp
	ret
	.size	uaccess_enable_not_uao, .-uaccess_enable_not_uao
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"Buffer overflow detected (%d < %lu)!\n"
	.text
	.align	2
	.type	copy_overflow, %function
copy_overflow:
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	x2, x1
	mov	x29, sp
	mov	w1, w0
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	__warn_printk
#APP
// 147 "./include/linux/thread_info.h" 1
	.pushsection __bug_table,"aw"; .align 2; 14470: .long 14471f - 14470b; .pushsection .rodata.str,"aMS",@progbits,1; 14472: .string "include/linux/thread_info.h"; .popsection; .long 14472b - 14470b; .short 147; .short (1 << 0)|((1 << 3) | ((9) << 8)); .popsection; 14471: brk 0x800
// 0 "" 2
#NO_APP
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	copy_overflow, .-copy_overflow
	.align	2
	.type	__range_ok, %function
__range_ok:
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x3, sp_el0
// 0 "" 2
#NO_APP
	ldr	w4, [x3, 44]
	hint	25 // paciasp
	ldr	x2, [x3, 8]
	tbnz	x4, 21, .L8
	ldr	x3, [x3]
	tst	w3, 67108864
	beq	.L9
.L8:
	sbfx	x3, x0, 0, 56
	and	x0, x0, x3
.L9:
#APP
// 79 "./arch/arm64/include/asm/uaccess.h" 1
		adds	x0, x0, x1
	csel	x2, xzr, x2, hi
	csinv	x0, x0, xzr, cc
	sbcs	xzr, x0, x2
	cset	x0, ls

// 0 "" 2
#NO_APP
	hint	29 // autiasp
	ret
	.size	__range_ok, .-__range_ok
	.align	2
	.type	l2p_addr_tran.isra.0, %function
l2p_addr_tran.isra.0:
	.section	__patchable_function_entries,"aw",@progbits
	.align	3
	.8byte	.LPFE1
	.text
.LPFE1:
	nop
	nop
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldrh	w3, [x4, 8]
	ldrh	w5, [x4, 10]
	ldrh	w4, [x4, 14]
	cmp	w4, 4
	bne	.L15
	lsr	w3, w3, 1
	ubfiz	w5, w5, 1, 15
.L15:
	ubfx	x4, x0, 10, 16
	ubfx	x6, x0, 10, 16
	and	w0, w0, 1023
	hint	29 // autiasp
	udiv	w4, w4, w3
	msub	w3, w3, w4, w6
	madd	w3, w3, w5, w0
	str	w3, [x1]
	str	w4, [x2]
	ret
	.size	l2p_addr_tran.isra.0, .-l2p_addr_tran.isra.0
	.align	2
	.type	_copy_from_user, %function
_copy_from_user:
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x20, x1
	mov	x19, x2
	mov	x1, x2
	str	x21, [sp, 32]
	mov	x21, x0
	mov	x0, x20
	bl	__range_ok
	cbz	x0, .L20
	bl	uaccess_enable_not_uao
	sbfx	x0, x20, 0, 56
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x1, sp_el0
// 0 "" 2
#NO_APP
	and	x0, x20, x0
	ldr	x2, [x1, 8]
#APP
// 289 "./arch/arm64/include/asm/uaccess.h" 1
		bics	xzr, x0, x2
	csel	x1, x20, xzr, eq

// 0 "" 2
// 297 "./arch/arm64/include/asm/uaccess.h" 1
	hint #20
// 0 "" 2
#NO_APP
	mov	x2, x19
	mov	x0, x21
	bl	__arch_copy_from_user
	mov	x20, x0
	bl	uaccess_disable_not_uao
	cbz	x20, .L17
.L18:
	sub	x19, x19, x20
	mov	x2, x20
	add	x0, x21, x19
	mov	w1, 0
	bl	memset
.L17:
	mov	x0, x20
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L20:
	mov	x20, x19
	b	.L18
	.size	_copy_from_user, .-_copy_from_user
	.section	.rodata.str1.1
.LC1:
	.string	"SFTL version: 5.0.56 20210329"
.LC2:
	.string	"\n%s\n"
.LC3:
	.string	"act blk: %x %x %x %x %x %x\n"
.LC4:
	.string	"buf blk: %x %x %x %x %x %x\n"
.LC5:
	.string	"tmp blk: %x %x %x %x %x %x\n"
.LC6:
	.string	"gc blk: %x %x %x %x %x %x\n"
.LC7:
	.string	"free blk: %x %x %x\n"
	.text
	.align	2
	.global	ftl_print_sblk_info
	.type	ftl_print_sblk_info, %function
ftl_print_sblk_info:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE2
	.text
.LPFE2:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LC1
	mov	x29, sp
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x1, x1, :lo12:.LC1
	adrp	x0, .LC2
	add	x0, x0, :lo12:.LC2
	bl	sftl_printk
	ldrh	w1, [x19, 24]
	ldr	x0, [x19, 72]
	ldrh	w5, [x19, 28]
	ldrb	w4, [x19, 32]
	ldrh	w6, [x0, x1, lsl 1]
	adrp	x0, .LC3
	ldrb	w3, [x19, 30]
	add	x0, x0, :lo12:.LC3
	ldrh	w2, [x19, 26]
	ldrh	w1, [x19, 24]
	bl	sftl_printk
	ldrh	w1, [x19, 80]
	ldr	x0, [x19, 72]
	ldrh	w5, [x19, 84]
	ldrb	w4, [x19, 88]
	ldrh	w6, [x0, x1, lsl 1]
	adrp	x0, .LC4
	ldrb	w3, [x19, 86]
	add	x0, x0, :lo12:.LC4
	ldrh	w2, [x19, 82]
	ldrh	w1, [x19, 80]
	bl	sftl_printk
	ldrh	w1, [x19, 128]
	ldr	x0, [x19, 72]
	ldrh	w5, [x19, 132]
	ldrb	w4, [x19, 136]
	ldrh	w6, [x0, x1, lsl 1]
	adrp	x0, .LC5
	ldrb	w3, [x19, 134]
	add	x0, x0, :lo12:.LC5
	ldrh	w2, [x19, 130]
	ldrh	w1, [x19, 128]
	bl	sftl_printk
	ldrh	w1, [x19, 176]
	ldr	x0, [x19, 72]
	ldrh	w5, [x19, 180]
	ldrb	w4, [x19, 184]
	ldrh	w6, [x0, x1, lsl 1]
	adrp	x0, .LC6
	ldrb	w3, [x19, 182]
	add	x0, x0, :lo12:.LC6
	ldrh	w2, [x19, 178]
	ldrh	w1, [x19, 176]
	bl	sftl_printk
	ldrh	w3, [x19, 224]
	adrp	x0, .LC7
	ldrh	w2, [x19, 226]
	add	x0, x0, :lo12:.LC7
	ldrh	w1, [x19, 228]
	bl	sftl_printk
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	ftl_print_sblk_info, .-ftl_print_sblk_info
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE3
	.text
.LPFE3:
	nop
	nop
	mov	w2, 1
	mov	w1, 0
	hint	25 // paciasp
.L28:
	cmp	w2, w0
	bls	.L29
	sub	w0, w1, #1
	hint	29 // autiasp
	ret
.L29:
	add	w1, w1, 1
	lsl	w2, w2, 1
	and	w1, w1, 65535
	b	.L28
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
	.type	FtlPrintInfo, %function
FtlPrintInfo:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE4
	.text
.LPFE4:
	nop
	nop
	hint	25 // paciasp
	hint	29 // autiasp
	ret
	.size	FtlPrintInfo, .-FtlPrintInfo
	.align	2
	.global	FtlSysBlkNumInit
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE5
	.text
.LPFE5:
	nop
	nop
	and	w0, w0, 65535
	mov	w1, 24
	cmp	w0, 24
	hint	25 // paciasp
	csel	w0, w0, w1, cs
	hint	29 // autiasp
	and	w1, w0, 65535
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w2, [x0, 236]
	ldrh	w3, [x0, 246]
	str	w1, [x0, 232]
	mul	w2, w2, w1
	sub	w1, w3, w1
	strh	w1, [x0, 244]
	ldr	w1, [x0, 252]
	str	w2, [x0, 240]
	sub	w2, w1, w2
	str	w2, [x0, 248]
	mov	w0, 0
	ret
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE6
	.text
.LPFE6:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	stp	x23, x24, [sp, 48]
	ldrh	w2, [x0, 14]
	str	x25, [sp, 64]
	ldrh	w1, [x0]
	ldrh	w20, [x0, 2]
	ldrh	w3, [x0, 4]
	ldrh	w0, [x0, 6]
	strh	w0, [x19, 246]
	strh	w1, [x19, 256]
	strh	w20, [x19, 258]
	strh	w3, [x19, 260]
	strh	w2, [x19, 262]
	cmp	w2, 4
	bne	.L36
	lsr	w0, w0, 1
	strh	w0, [x19, 246]
	mov	w0, 8
	strh	w0, [x19, 262]
.L37:
	add	x0, x19, 264
	mov	x24, 0
.L38:
	strb	w24, [x24, x0]
	add	x24, x24, 1
	cmp	x24, 32
	bne	.L38
	cmp	w1, 1
	mov	w0, 5
	csel	w0, w0, w1, ne
	strh	w0, [x19, 296]
	mov	w0, 640
	ldrh	w21, [x19, 246]
	strh	w0, [x19, 300]
	ldrh	w0, [x19, 260]
	strh	wzr, [x19, 298]
	mul	w20, w20, w0
	mul	w0, w0, w21
	and	w20, w20, 65535
	strh	w20, [x19, 236]
	and	w0, w0, 65535
	strh	w0, [x19, 302]
	bl	Ftl_log2
	ldrh	w25, [x22, 12]
	ldrh	w23, [x19, 262]
	strh	w0, [x19, 304]
	strh	w25, [x19, 306]
	mul	w0, w20, w25
	strh	w25, [x19, 308]
	strh	w0, [x19, 310]
	mov	w0, w23
	bl	Ftl_log2
	and	w3, w0, 65535
	ubfiz	w1, w23, 9, 7
	strh	w1, [x19, 314]
	and	w1, w23, 127
	mul	w2, w20, w21
	mul	w23, w23, w25
	str	w2, [x19, 252]
	ubfiz	w1, w1, 1, 15
	strh	w1, [x19, 316]
	ldrh	w1, [x22, 20]
	lsl	w21, w21, 6
	strh	w1, [x19, 318]
	mov	w1, 5120
	mul	w2, w2, w23
	strh	w0, [x19, 312]
	sdiv	w1, w1, w23
	asr	w2, w2, 11
	str	w2, [x19, 320]
	mov	w2, 4
	cmp	w1, 4
	csel	w1, w1, w2, gt
	mov	w2, 640
	asr	w2, w2, w0
	add	w0, w3, 9
	cmp	w20, 1
	add	w2, w2, 2
	asr	w0, w21, w0
	and	w21, w0, 65535
	strh	w0, [x19, 328]
	strh	w1, [x19, 324]
	mul	w0, w20, w21
	strh	w2, [x19, 326]
	str	w0, [x19, 332]
	and	w0, w1, 65535
	udiv	w0, w0, w20
	add	w0, w0, w21
	add	w1, w0, 12
	add	w0, w0, 8
	csel	w0, w0, w1, ne
	str	w0, [x19, 232]
	bl	FtlSysBlkNumInit
	strh	w24, [x19, 342]
	ldr	w0, [x19, 232]
	ldr	w1, [x19, 248]
	str	w0, [x19, 336]
	ldrh	w0, [x19, 306]
	str	wzr, [x19, 344]
	ldp	x21, x22, [sp, 32]
	mul	w0, w0, w1
	ldrh	w1, [x19, 312]
	ldp	x23, x24, [sp, 48]
	add	w1, w1, 9
	lsl	w0, w0, 2
	ldr	x25, [sp, 64]
	lsr	w0, w0, w1
	add	w0, w0, 2
	strh	w0, [x19, 340]
	ldrh	w0, [x19, 324]
	add	w0, w0, 3
	strh	w0, [x19, 324]
	ldr	w0, [x19, 332]
	add	w0, w0, 3
	str	w0, [x19, 332]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
.L36:
	cmp	w2, 8
	bne	.L37
	cmp	w3, 1
	bne	.L37
	lsr	w0, w0, 1
	strh	w0, [x19, 246]
	mov	w0, 2
	strh	w0, [x19, 260]
	b	.L37
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE7
	.text
.LPFE7:
	nop
	nop
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldrh	w2, [x1, 348]
	cbz	w2, .L50
	ldr	x2, [x1, 352]
	and	w0, w0, 65535
	ldrh	w3, [x1, 324]
	mov	x1, 0
	sub	x2, x2, #2
.L48:
	cmp	w3, w1, uxth
	bhi	.L49
.L50:
	mov	w0, 0
.L46:
	hint	29 // autiasp
	ret
.L49:
	add	x1, x1, 1
	ldrh	w4, [x2, x1, lsl 1]
	cmp	w4, w0
	bne	.L48
	mov	w0, 1
	b	.L46
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE8
	.text
.LPFE8:
	nop
	nop
	mov	w0, 0
	hint	25 // paciasp
	hint	29 // autiasp
	ret
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	sftl_get_density
	.type	sftl_get_density, %function
sftl_get_density:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE9
	.text
.LPFE9:
	nop
	nop
	adrp	x0, .LANCHOR0+344
	hint	25 // paciasp
	hint	29 // autiasp
	ldr	w0, [x0, #:lo12:.LANCHOR0+344]
	ret
	.size	sftl_get_density, .-sftl_get_density
	.section	.rodata.str1.1
.LC8:
	.string	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\n"
	.text
	.align	2
	.global	FtlBbmMapBadBlock
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE10
	.text
.LPFE10:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	and	w1, w0, 65535
	mov	x29, sp
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w4, 1
	add	x19, x19, 360
	ldrh	w3, [x19, -58]
	udiv	w2, w1, w3
	msub	w3, w2, w3, w1
	add	x0, x19, x2, uxth 3
	ubfx	x5, x3, 5, 11
	lsl	w4, w4, w3
	ldr	x0, [x0, 32]
	lsl	x5, x5, 2
	and	w3, w3, 65535
	ldr	w6, [x0, x5]
	orr	w4, w4, w6
	str	w4, [x0, x5]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	sftl_printk
	ldrh	w0, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 6]
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE11
	.text
.LPFE11:
	nop
	nop
	adrp	x2, .LANCHOR0
	add	x3, x2, :lo12:.LANCHOR0
	and	w0, w0, 65535
	hint	25 // paciasp
	hint	29 // autiasp
	ldrh	w1, [x3, 302]
	udiv	w2, w0, w1
	msub	w1, w2, w1, w0
	add	x2, x3, x2, uxth 3
	ldr	x0, [x2, 392]
	ubfx	x3, x1, 5, 11
	ldr	w0, [x0, x3, lsl 2]
	lsr	w0, w0, w1
	and	w0, w0, 1
	ret
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
	.type	FtlBbtInfoPrint, %function
FtlBbtInfoPrint:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE12
	.text
.LPFE12:
	nop
	nop
	hint	25 // paciasp
	hint	29 // autiasp
	ret
	.size	FtlBbtInfoPrint, .-FtlBbtInfoPrint
	.align	2
	.global	FtlBbtCalcTotleCnt
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE13
	.text
.LPFE13:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR0
	mov	x29, sp
	add	x0, x0, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	w20, 0
	mov	w19, 0
	str	x21, [sp, 32]
	ldrh	w21, [x0, 302]
	ldrh	w0, [x0, 258]
	mul	w21, w21, w0
.L64:
	cmp	w19, w21
	blt	.L66
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L66:
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cbz	w0, .L65
	add	w20, w20, 1
	and	w20, w20, 65535
.L65:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L64
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.type	V2P_block, %function
V2P_block:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE14
	.text
.LPFE14:
	nop
	nop
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	and	w0, w0, 65535
	and	w1, w1, 65535
	hint	25 // paciasp
	hint	29 // autiasp
	ldrh	w2, [x4, 260]
	ldrh	w4, [x4, 302]
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
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE15
	.text
.LPFE15:
	nop
	nop
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	and	w0, w0, 65535
	hint	25 // paciasp
	hint	29 // autiasp
	ldrh	w1, [x2, 260]
	ldrh	w3, [x2, 302]
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
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE16
	.text
.LPFE16:
	nop
	nop
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	and	w3, w0, 65535
	hint	25 // paciasp
	hint	29 // autiasp
	ldrh	w0, [x2, 302]
	udiv	w1, w3, w0
	msub	w0, w1, w0, w3
	ldrh	w1, [x2, 260]
	and	w0, w0, 65535
	udiv	w0, w0, w1
	ret
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE17
	.text
.LPFE17:
	nop
	nop
	cmp	w0, w1
	mov	w2, -2147483648
	hint	25 // paciasp
	bls	.L78
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
.L77:
	hint	29 // autiasp
	ret
.L78:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
	b	.L77
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE18
	.text
.LPFE18:
	nop
	nop
	adrp	x0, .LANCHOR0+462
	hint	25 // paciasp
	hint	29 // autiasp
	ldrh	w0, [x0, #:lo12:.LANCHOR0+462]
	cmp	w0, 0
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE19
	.text
.LPFE19:
	nop
	nop
	adrp	x0, .LANCHOR0+462
	hint	25 // paciasp
	hint	29 // autiasp
	ldrh	w0, [x0, #:lo12:.LANCHOR0+462]
	cmp	w0, 1024
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE20
	.text
.LPFE20:
	nop
	nop
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldrh	w1, [x0, 462]
	cbz	w1, .L85
	ldrh	w5, [x0, 2540]
	add	x4, x0, 456
	ldrh	w2, [x4, 2]
	mov	w6, 0
	ldrh	w1, [x4, 4]
	and	w5, w5, 31
	mov	w3, 0
.L88:
	cmp	w3, w5
	bne	.L89
	cbz	w6, .L85
	strh	w2, [x0, 458]
	strh	w1, [x0, 460]
.L85:
	hint	29 // autiasp
	ret
.L89:
	add	x6, x4, x2, sxtw 1
	add	w3, w3, 1
	add	w2, w2, 1
	and	w3, w3, 65535
	and	w2, w2, 1023
	ldrh	w7, [x6, 8]
	add	x6, x4, x1, sxtw 1
	add	w1, w1, 1
	and	w1, w1, 1023
	strh	w7, [x6, 8]
	mov	w6, 1
	b	.L88
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE21
	.text
.LPFE21:
	nop
	nop
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldrh	w3, [x1, 462]
	cmp	w3, 1024
	beq	.L99
	ldrh	w5, [x1, 458]
	add	x4, x1, 456
	and	w0, w0, 65535
	mov	w2, 0
.L97:
	cmp	w3, w2
	bhi	.L98
.L99:
	mov	w0, 0
.L95:
	hint	29 // autiasp
	ret
.L98:
	add	w1, w5, w2
	ubfiz	x1, x1, 1, 10
	add	x1, x4, x1
	ldrh	w1, [x1, 8]
	cmp	w1, w0
	beq	.L100
	add	w2, w2, 1
	b	.L97
.L100:
	mov	w0, 1
	b	.L95
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE22
	.text
.LPFE22:
	nop
	nop
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldrh	w2, [x1, 244]
	cmp	w2, w0, uxth
	bls	.L104
	and	w3, w0, 65535
	mov	w10, 6
	ldr	x7, [x1, 2560]
	mov	w0, -1
	umull	x6, w3, w10
	add	x4, x7, x6
	str	w0, [x7, x6]
	ldr	x2, [x1, 2568]
	cbnz	x2, .L105
.L117:
	str	x4, [x1, 2568]
	b	.L104
.L105:
	ldr	x11, [x1, 72]
	ubfiz	x5, x3, 1, 16
	ldrh	w8, [x4, 4]
	mov	w12, 0
	ldr	x9, [x1, 2560]
	cmp	w8, 0
	ldrh	w5, [x11, x5]
	mov	w15, 65535
	ldrh	w14, [x1, 244]
	mul	w5, w5, w8
	mov	x8, -6148914691236517206
	movk	x8, 0xaaab, lsl 0
	csel	w5, w5, w0, ne
	sub	x0, x2, x9
	asr	x0, x0, 1
	mul	x0, x0, x8
	and	w0, w0, 65535
.L110:
	add	w8, w12, 1
	and	w12, w8, 65535
	cmp	w14, w8, uxth
	bcc	.L104
	cmp	w3, w0
	beq	.L104
	ldrh	w13, [x2, 4]
	cbz	w13, .L108
	ubfiz	x8, x0, 1, 16
	ldrh	w8, [x11, x8]
	mul	w8, w8, w13
	cmp	w8, w5
	bcs	.L108
	ldrh	w8, [x2]
	cmp	w8, w15
	bne	.L109
	strh	w0, [x4, 2]
	strh	w3, [x2]
	str	x4, [x1, 2576]
.L104:
	mov	w0, 0
	hint	29 // autiasp
	ret
.L109:
	umaddl	x2, w8, w10, x9
	mov	w0, w8
	b	.L110
.L108:
	strh	w0, [x7, x6]
	ldrh	w0, [x2, 2]
	strh	w0, [x4, 2]
	ldr	x5, [x1, 2568]
	cmp	x2, x5
	bne	.L111
	strh	w3, [x2, 2]
	b	.L117
.L111:
	mov	w4, 6
	ldr	x1, [x1, 2560]
	umull	x0, w0, w4
	strh	w3, [x1, x0]
	strh	w3, [x2, 2]
	b	.L104
	.size	insert_data_list, .-insert_data_list
	.section	.rodata.str1.1
.LC9:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
	.text
	.align	2
	.global	INSERT_DATA_LIST
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE23
	.text
.LPFE23:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	bl	insert_data_list
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2584]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 2584]
	ldrh	w1, [x1, 244]
	cmp	w1, w0
	bcs	.L118
	adrp	x1, .LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC9
	mov	w2, 214
	bl	sftl_printk
.L118:
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.type	insert_free_list, %function
insert_free_list:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE24
	.text
.LPFE24:
	nop
	nop
	mov	w11, 65535
	hint	25 // paciasp
	cmp	w11, w0, uxth
	beq	.L122
	and	w3, w0, 65535
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w9, 6
	mov	w0, -1
	umull	x5, w3, w9
	ldr	x6, [x1, 2560]
	add	x4, x6, x5
	str	w0, [x6, x5]
	ldr	x0, [x1, 2592]
	cbnz	x0, .L123
.L129:
	str	x4, [x1, 2592]
	b	.L122
.L123:
	ldr	x10, [x1, 2600]
	ubfiz	x2, x3, 1, 16
	ldr	x8, [x1, 2560]
	mov	x7, -6148914691236517206
	ldrh	w12, [x10, x2]
	movk	x7, 0xaaab, lsl 0
	sub	x2, x0, x8
	asr	x2, x2, 1
	mul	x2, x2, x7
	and	w2, w2, 65535
.L126:
	ubfiz	x7, x2, 1, 16
	ldrh	w7, [x10, x7]
	cmp	w7, w12
	bcs	.L124
	ldrh	w7, [x0]
	cmp	w7, w11
	bne	.L125
	strh	w2, [x4, 2]
	strh	w3, [x0]
.L122:
	mov	w0, 0
	hint	29 // autiasp
	ret
.L125:
	umaddl	x0, w7, w9, x8
	mov	w2, w7
	b	.L126
.L124:
	ldrh	w7, [x0, 2]
	strh	w7, [x4, 2]
	strh	w2, [x6, x5]
	ldr	x2, [x1, 2592]
	cmp	x2, x0
	bne	.L127
	strh	w3, [x0, 2]
	b	.L129
.L127:
	ldrh	w2, [x0, 2]
	mov	w4, 6
	ldr	x1, [x1, 2560]
	umull	x2, w2, w4
	strh	w3, [x1, x2]
	strh	w3, [x0, 2]
	b	.L122
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE25
	.text
.LPFE25:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	bl	insert_free_list
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1, 228]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 228]
	ldrh	w1, [x1, 244]
	cmp	w1, w0
	bcs	.L130
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 17
	add	x0, x0, :lo12:.LC9
	mov	w2, 207
	bl	sftl_printk
.L130:
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE26
	.text
.LPFE26:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -64]!
	and	w1, w1, 65535
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	x21, x0
	mov	w0, 6
	ldr	x22, [x20, 2560]
	umull	x19, w1, w0
	str	x23, [sp, 48]
	mov	w0, 65535
	add	x23, x22, x19
	ldrh	w1, [x23, 2]
	cmp	w1, w0
	bne	.L134
	ldr	x0, [x21]
	cmp	x23, x0
	bne	.L135
.L138:
	ldrh	w0, [x22, x19]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L142
	str	xzr, [x21]
.L140:
	mov	w0, -1
	str	w0, [x22, x19]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	hint	29 // autiasp
	ret
.L135:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 34
	add	x0, x0, :lo12:.LC9
	mov	w2, 372
	bl	sftl_printk
.L134:
	ldr	x0, [x21]
	ldrh	w1, [x22, x19]
	cmp	x0, x23
	beq	.L138
	ldrh	w0, [x23, 2]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L141
	cmp	w0, w1
	beq	.L140
	mov	w1, 6
	mov	w2, -1
	umull	x0, w0, w1
	ldr	x1, [x20, 2560]
	strh	w2, [x1, x0]
	b	.L140
.L142:
	ldr	x1, [x20, 2560]
	mov	w2, 6
	umaddl	x0, w0, w2, x1
	mov	w1, -1
	str	x0, [x21]
	strh	w1, [x0, 2]
	b	.L140
.L141:
	ldr	x3, [x20, 2560]
	mov	w2, 6
	umaddl	x1, w1, w2, x3
	strh	w0, [x1, 2]
	umull	x0, w0, w2
	ldr	x1, [x20, 2560]
	ldrh	w3, [x22, x19]
	strh	w3, [x1, x0]
	b	.L140
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE27
	.text
.LPFE27:
	nop
	nop
	ldr	x2, [x0]
	cbz	x2, .L150
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LANCHOR0+2560
	mov	x29, sp
	str	x19, [sp, 16]
	and	w1, w1, 65535
	ldr	x19, [x3, #:lo12:.LANCHOR0+2560]
	mov	w4, 65535
	mov	w5, 6
.L147:
	cbz	w1, .L148
	ldrh	w3, [x2]
	cmp	w3, w4
	bne	.L149
.L148:
	sub	x19, x2, x19
	mov	x2, -6148914691236517206
	movk	x2, 0xaaab, lsl 0
	asr	x19, x19, 1
	mul	x19, x19, x2
	and	w19, w19, 65535
	mov	w1, w19
	bl	List_remove_node
	mov	w0, w19
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
.L149:
	sub	w1, w1, #1
	umaddl	x2, w3, w5, x19
	and	w1, w1, 65535
	b	.L147
.L150:
	mov	w0, 65535
	ret
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_pop_head_node
	.type	List_pop_head_node, %function
List_pop_head_node:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE28
	.text
.LPFE28:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	w1, 0
	mov	x29, sp
	bl	List_pop_index_node
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	List_pop_head_node, .-List_pop_head_node
	.align	2
	.global	List_get_gc_head_node
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE29
	.text
.LPFE29:
	nop
	nop
	and	w2, w0, 65535
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldr	x1, [x0, 2568]
	cbz	x1, .L165
	mov	w4, 6
	ldr	x3, [x0, 2560]
	mov	w0, 65535
.L162:
	cbz	w2, .L163
	ldrh	w1, [x1]
	cmp	w1, w0
	bne	.L164
.L160:
	hint	29 // autiasp
	ret
.L164:
	sub	w2, w2, #1
	umaddl	x1, w1, w4, x3
	and	w2, w2, 65535
	b	.L162
.L165:
	mov	w0, 65535
	b	.L160
.L163:
	sub	x0, x1, x3
	mov	x1, -6148914691236517206
	movk	x1, 0xaaab, lsl 0
	asr	x0, x0, 1
	mul	x0, x0, x1
	and	w0, w0, 65535
	b	.L160
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.type	List_update_data_list, %function
List_update_data_list:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE30
	.text
.LPFE30:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -64]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ldrh	w1, [x19, 24]
	stp	x23, x24, [sp, 48]
	cmp	w1, w0, uxth
	beq	.L169
	and	w20, w0, 65535
	ldrh	w0, [x19, 80]
	cmp	w0, w20
	beq	.L169
	ldrh	w0, [x19, 128]
	cmp	w0, w20
	beq	.L169
	mov	w22, 6
	ldr	x24, [x19, 2560]
	umull	x22, w20, w22
	ldr	x0, [x19, 2568]
	add	x23, x24, x22
	cmp	x23, x0
	beq	.L169
	ldr	x1, [x19, 72]
	ubfiz	x0, x20, 1, 16
	ldrh	w21, [x1, x0]
	mov	w1, 65535
	ldrh	w0, [x23, 4]
	cmp	w0, 0
	mul	w21, w21, w0
	ldrh	w0, [x23, 2]
	csinv	w21, w21, wzr, ne
	cmp	w0, w1
	bne	.L172
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	bne	.L172
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 51
	add	x0, x0, :lo12:.LC9
	mov	w2, 463
	bl	sftl_printk
.L172:
	ldrh	w0, [x23, 2]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L173
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	beq	.L169
.L173:
	mov	w1, 6
	mov	x2, -6148914691236517206
	movk	x2, 0xaaab, lsl 0
	umull	x0, w0, w1
	asr	x1, x0, 1
	mul	x1, x1, x2
	ldr	x2, [x19, 72]
	ldrh	w1, [x2, x1, lsl 1]
	ldr	x2, [x19, 2560]
	add	x0, x2, x0
	ldrh	w2, [x0, 4]
	cmp	w2, 0
	mul	w0, w1, w2
	csinv	w0, w0, wzr, ne
	cmp	w21, w0
	bcs	.L169
	mov	w1, w20
	add	x0, x19, 2568
	bl	List_remove_node
	ldrh	w0, [x19, 2584]
	cbnz	w0, .L175
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 51
	add	x0, x0, :lo12:.LC9
	mov	w2, 474
	bl	sftl_printk
.L175:
	ldrh	w0, [x19, 2584]
	sub	w0, w0, #1
	strh	w0, [x19, 2584]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L169:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	hint	29 // autiasp
	ret
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE31
	.text
.LPFE31:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR0
	mov	x29, sp
	add	x2, x2, :lo12:.LANCHOR0
	mov	x3, 0
	str	x19, [sp, 16]
	mov	w5, 65535
	ldr	x0, [x2, 2608]
	ldrh	w1, [x2, 342]
	sub	x4, x0, #16
.L180:
	and	w19, w3, 65535
	cmp	w1, w3, uxth
	bhi	.L182
	mov	w19, w1
	add	x7, x0, 4
	mov	x3, 0
	mov	w5, -2147483648
.L183:
	and	w6, w3, 65535
	cmp	w1, w3, uxth
	bhi	.L185
	cmp	w1, w19
	bhi	.L181
	mov	w19, w1
	ldrh	w5, [x2, 2616]
	mov	w3, -1
	mov	w2, 0
.L186:
	cmp	w1, w2
	bne	.L188
	cmp	w1, w19
	bhi	.L181
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 73
	mov	w2, 796
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	bl	sftl_printk
	b	.L181
.L182:
	add	x3, x3, 1
	lsl	x6, x3, 4
	ldrh	w6, [x4, x6]
	cmp	w6, w5
	bne	.L180
.L181:
	mov	w0, w19
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
.L185:
	lsl	x4, x3, 4
	ldr	w4, [x7, x4]
	tbnz	w4, #31, .L184
	cmp	w4, w5
	bcs	.L184
	mov	w5, w4
	mov	w19, w6
.L184:
	add	x3, x3, 1
	b	.L183
.L188:
	ldr	w4, [x0, 4]
	cmp	w4, w3
	bcs	.L187
	ldrh	w6, [x0]
	cmp	w6, w5
	csel	w3, w3, w4, eq
	csel	w19, w19, w2, eq
.L187:
	add	w2, w2, 1
	add	x0, x0, 16
	and	w2, w2, 65535
	b	.L186
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE32
	.text
.LPFE32:
	nop
	nop
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldrh	w2, [x1, 2618]
	cmp	w2, 4
	bhi	.L191
	cbnz	w0, .L191
	add	w2, w2, 1
	strh	w2, [x1, 2618]
.L190:
	hint	29 // autiasp
	ret
.L191:
	ldrh	w4, [x1, 244]
	mov	x0, 0
	ldr	x5, [x1, 72]
	mov	w6, 65535
	strh	wzr, [x1, 2618]
	str	wzr, [x1, 2620]
.L192:
	cmp	w4, w0, uxth
	bls	.L190
	ldrh	w3, [x5, x0, lsl 1]
	cmp	w3, w6
	beq	.L193
	ldr	w2, [x1, 2620]
	add	w2, w2, w3
	str	w2, [x1, 2620]
.L193:
	add	x0, x0, 1
	b	.L192
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_sb_update_avl_pages
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE33
	.text
.LPFE33:
	nop
	nop
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	and	w1, w1, 65535
	and	w2, w2, 65535
	mov	w6, 65535
	strh	wzr, [x0, 4]
	ldrh	w5, [x3, 236]
	hint	25 // paciasp
.L198:
	cmp	w5, w2, uxth
	bhi	.L200
	ldrh	w2, [x3, 306]
	add	x4, x0, 16
	mov	w6, 65535
	sub	w2, w2, w1
	mov	x1, 0
	sub	w2, w2, #1
	sxth	w2, w2
.L201:
	cmp	w5, w1, uxth
	bhi	.L203
	hint	29 // autiasp
	ret
.L200:
	add	x4, x0, x2, sxtw 1
	ldrh	w4, [x4, 16]
	cmp	w4, w6
	beq	.L199
	ldrh	w4, [x0, 4]
	add	w4, w4, 1
	strh	w4, [x0, 4]
.L199:
	add	w2, w2, 1
	b	.L198
.L203:
	ldrh	w3, [x4, x1, lsl 1]
	cmp	w3, w6
	beq	.L202
	ldrh	w3, [x0, 4]
	add	w3, w2, w3
	strh	w3, [x0, 4]
.L202:
	add	x1, x1, 1
	b	.L201
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE34
	.text
.LPFE34:
	nop
	nop
	ldrh	w1, [x0, 4]
	hint	25 // paciasp
	cbz	w1, .L205
	ldrh	w1, [x0]
	mov	w2, 65535
	cmp	w1, w2
	beq	.L205
	ldrb	w1, [x0, 6]
	adrp	x3, .LANCHOR0+236
	add	x1, x1, 8
	ldrh	w3, [x3, #:lo12:.LANCHOR0+236]
	ldrh	w1, [x0, x1, lsl 1]
.L208:
	cmp	w1, w2
	beq	.L210
.L205:
	hint	29 // autiasp
	ret
.L210:
	ldrb	w1, [x0, 6]
	add	w1, w1, 1
	and	w1, w1, 255
	strb	w1, [x0, 6]
	cmp	w1, w3
	bne	.L209
	ldrh	w1, [x0, 2]
	strb	wzr, [x0, 6]
	add	w1, w1, 1
	strh	w1, [x0, 2]
.L209:
	ldrb	w1, [x0, 6]
	add	x1, x1, 8
	ldrh	w1, [x0, x1, lsl 1]
	b	.L208
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE35
	.text
.LPFE35:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	x19, x0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	ldrh	w1, [x0]
	ldrh	w0, [x20, 244]
	cmp	w1, w0
	bcc	.L213
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 95
	add	x0, x0, :lo12:.LC9
	mov	w2, 2157
	bl	sftl_printk
.L213:
	ldrh	w26, [x20, 236]
	add	x23, x19, 16
	add	x27, x20, 264
	mov	x22, 0
	mov	w25, -1
	strh	wzr, [x19, 4]
	strb	wzr, [x19, 7]
.L214:
	ldrh	w1, [x19]
	ldrb	w21, [x19, 7]
	cmp	w26, w22, uxth
	bhi	.L216
	ldrh	w0, [x20, 306]
	ubfiz	x1, x1, 1, 16
	strb	wzr, [x19, 9]
	mul	w21, w21, w0
	strh	w21, [x19, 4]
	ldr	x0, [x20, 2600]
	ldp	x21, x22, [sp, 32]
	ldrh	w1, [x0, x1]
	mov	w0, 10000
	ldp	x23, x24, [sp, 48]
	cmp	w1, w0
	cset	w0, hi
	ldp	x25, x26, [sp, 64]
	strb	w0, [x19, 9]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
.L216:
	ldrb	w0, [x27, x22]
	bl	V2P_block
	strh	w25, [x23]
	mov	w24, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L215
	strh	w24, [x23]
	add	w21, w21, 1
	strb	w21, [x19, 7]
.L215:
	add	x22, x22, 1
	add	x23, x23, 2
	b	.L214
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE36
	.text
.LPFE36:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	w19, 0
	ldrh	w23, [x20, 236]
	ldrh	w24, [x20, 306]
	stp	x21, x22, [sp, 32]
	and	w21, w0, 65535
	mov	x22, 0
	str	x25, [sp, 64]
	add	x25, x20, 264
.L220:
	cmp	w23, w22, uxth
	bhi	.L222
	cbz	w19, .L223
	mov	w0, 32768
	udiv	w19, w0, w19
.L223:
	ldr	x0, [x20, 2560]
	mov	w1, 6
	umaddl	x21, w21, w1, x0
	mov	w0, 0
	strh	w19, [x21, 4]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
.L222:
	ldrb	w0, [x25, x22]
	mov	w1, w21
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L221
	add	w19, w24, w19
	and	w19, w19, 65535
.L221:
	add	x22, x22, 1
	b	.L220
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE37
	.text
.LPFE37:
	nop
	nop
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldr	x0, [x1, 2592]
	cbz	x0, .L230
	ldr	x2, [x1, 2560]
	ldr	x1, [x1, 2600]
	sub	x0, x0, x2
	mov	x2, -6148914691236517206
	asr	x0, x0, 1
	movk	x2, 0xaaab, lsl 0
	mul	x0, x0, x2
	and	x0, x0, 65535
	ldrh	w0, [x1, x0, lsl 1]
.L229:
	hint	29 // autiasp
	ret
.L230:
	mov	w0, 0
	b	.L229
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE38
	.text
.LPFE38:
	nop
	nop
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldr	x1, [x2, 2592]
	cbz	x1, .L238
	ldrh	w3, [x2, 228]
	mov	w4, 7
	and	w0, w0, 65535
	mul	w3, w3, w4
	asr	w3, w3, 3
	cmp	w0, w3
	ble	.L234
	and	w0, w3, 65535
.L234:
	ldr	x4, [x2, 2560]
	mov	x3, -6148914691236517206
	movk	x3, 0xaaab, lsl 0
	mov	w6, 6
	sub	x1, x1, x4
	mov	w7, 65535
	asr	x1, x1, 1
	mul	x1, x1, x3
	mov	w3, 0
	and	w1, w1, 65535
.L235:
	cmp	w0, w3
	bne	.L237
.L236:
	ldr	x0, [x2, 2600]
	ubfiz	x1, x1, 1, 16
	ldrh	w0, [x0, x1]
.L233:
	hint	29 // autiasp
	ret
.L237:
	umull	x5, w1, w6
	ldrh	w5, [x4, x5]
	cmp	w5, w7
	beq	.L236
	add	w3, w3, 1
	mov	w1, w5
	and	w3, w3, 65535
	b	.L235
.L238:
	mov	w0, 0
	b	.L233
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.section	.rodata.str1.1
.LC10:
	.string	"FLASH INFO:\n"
.LC11:
	.string	"Device Capacity: %d MB\n"
.LC12:
	.string	"FTL INFO:\n"
.LC13:
	.string	"g_MaxLpn = 0x%x\n"
.LC14:
	.string	"g_VaildLpn = 0x%x\n"
.LC15:
	.string	"read_page_count = 0x%x\n"
.LC16:
	.string	"discard_page_count = 0x%x\n"
.LC17:
	.string	"write_page_count = 0x%x\n"
.LC18:
	.string	"cache_write_count = 0x%x\n"
.LC19:
	.string	"l2p_write_count = 0x%x\n"
.LC20:
	.string	"gc_page_count = 0x%x\n"
.LC21:
	.string	"totle_write = %d MB\n"
.LC22:
	.string	"totle_read = %d MB\n"
.LC23:
	.string	"GSV = 0x%x\n"
.LC24:
	.string	"GDV = 0x%x\n"
.LC25:
	.string	"bad blk num = %d\n"
.LC26:
	.string	"free_superblocks = 0x%x\n"
.LC27:
	.string	"mlc_EC = 0x%x\n"
.LC28:
	.string	"slc_EC = 0x%x\n"
.LC29:
	.string	"avg_EC = 0x%x\n"
.LC30:
	.string	"sys_EC = 0x%x\n"
.LC31:
	.string	"max_EC = 0x%x\n"
.LC32:
	.string	"min_EC = 0x%x\n"
.LC33:
	.string	"PLT = 0x%x\n"
.LC34:
	.string	"POT = 0x%x\n"
.LC35:
	.string	"MaxSector = 0x%x\n"
.LC36:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC37:
	.string	"sys_blks_pp = 0x%x\n"
.LC38:
	.string	"free sysblock = 0x%x\n"
.LC39:
	.string	"data_blks_pp = 0x%x\n"
.LC40:
	.string	"data_op_blks_pp = 0x%x\n"
.LC41:
	.string	"max_data_blks = 0x%x\n"
.LC42:
	.string	"Sys.id = 0x%x\n"
.LC43:
	.string	"Bbt.id = 0x%x\n"
.LC44:
	.string	"ACT.page = 0x%x\n"
.LC45:
	.string	"ACT.plane = 0x%x\n"
.LC46:
	.string	"ACT.id = 0x%x\n"
.LC47:
	.string	"ACT.mode = 0x%x\n"
.LC48:
	.string	"ACT.a_pages = 0x%x\n"
.LC49:
	.string	"ACT VPC = 0x%x\n"
.LC50:
	.string	"BUF.page = 0x%x\n"
.LC51:
	.string	"BUF.plane = 0x%x\n"
.LC52:
	.string	"BUF.id = 0x%x\n"
.LC53:
	.string	"BUF.mode = 0x%x\n"
.LC54:
	.string	"BUF.a_pages = 0x%x\n"
.LC55:
	.string	"BUF VPC = 0x%x\n"
.LC56:
	.string	"TMP.page = 0x%x\n"
.LC57:
	.string	"TMP.plane = 0x%x\n"
.LC58:
	.string	"TMP.id = 0x%x\n"
.LC59:
	.string	"TMP.mode = 0x%x\n"
.LC60:
	.string	"TMP.a_pages = 0x%x\n"
.LC61:
	.string	"GC.page = 0x%x\n"
.LC62:
	.string	"GC.plane = 0x%x\n"
.LC63:
	.string	"GC.id = 0x%x\n"
.LC64:
	.string	"GC.mode = 0x%x\n"
.LC65:
	.string	"GC.a_pages = 0x%x\n"
.LC66:
	.string	"WR_CHK = %x %x %x\n"
.LC67:
	.string	"Read Err Cnt = 0x%x\n"
.LC68:
	.string	"Prog Err Cnt = 0x%x\n"
.LC69:
	.string	"gc_free_blk_th= 0x%x\n"
.LC70:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC71:
	.string	"gc_skip_write_count= 0x%x\n"
.LC72:
	.string	"gc_blk_index= 0x%x\n"
.LC73:
	.string	"free min EC= 0x%x\n"
.LC74:
	.string	"free max EC= 0x%x\n"
.LC75:
	.string	"GC__SB VPC = 0x%x\n"
.LC76:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC77:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
	.text
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE39
	.text
.LPFE39:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LC10
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x1, x1, :lo12:.LC10
	stp	x21, x22, [sp, 32]
	mov	x21, x0
	stp	x23, x24, [sp, 48]
	add	x20, x21, 12
	stp	x25, x26, [sp, 64]
	bl	strcpy
	ldr	w3, [x19, 320]
	adrp	x2, .LC11
	add	x2, x2, :lo12:.LC11
	mov	x0, x20
	mov	x1, 64
	bl	snprintf
	add	x20, x20, x0, sxtw
	mov	x0, x20
	adrp	x1, .LC12
	add	x1, x1, :lo12:.LC12
	bl	strcpy
	ldr	w3, [x19, 2624]
	add	x20, x20, 10
	mov	x0, x20
	mov	x1, 64
	adrp	x2, .LC13
	add	x2, x2, :lo12:.LC13
	bl	snprintf
	add	x22, x20, x0, sxtw
	ldr	w3, [x19, 2620]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC14
	add	x2, x2, :lo12:.LC14
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2628]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC15
	add	x2, x2, :lo12:.LC15
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2632]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC16
	add	x2, x2, :lo12:.LC16
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2636]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC17
	add	x2, x2, :lo12:.LC17
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2640]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC18
	add	x2, x2, :lo12:.LC18
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2644]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC19
	add	x2, x2, :lo12:.LC19
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2648]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC20
	add	x2, x2, :lo12:.LC20
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2652]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC21
	add	x2, x2, :lo12:.LC21
	lsr	w3, w3, 11
	bl	snprintf
	ldr	w3, [x19, 2656]
	add	x22, x22, x0, sxtw
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC22
	add	x2, x2, :lo12:.LC22
	lsr	w3, w3, 11
	bl	snprintf
	ldr	w3, [x19, 2660]
	add	x22, x22, x0, sxtw
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC23
	add	x2, x2, :lo12:.LC23
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2664]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC24
	add	x2, x2, :lo12:.LC24
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 366]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC25
	add	x2, x2, :lo12:.LC25
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 228]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC26
	add	x2, x2, :lo12:.LC26
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2668]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC27
	add	x2, x2, :lo12:.LC27
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2672]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC28
	add	x2, x2, :lo12:.LC28
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2676]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC29
	add	x2, x2, :lo12:.LC29
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2680]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC30
	add	x2, x2, :lo12:.LC30
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2684]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC31
	add	x2, x2, :lo12:.LC31
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2688]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC32
	add	x2, x2, :lo12:.LC32
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 2542]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC33
	add	x2, x2, :lo12:.LC33
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 2540]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC34
	add	x2, x2, :lo12:.LC34
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 344]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC35
	add	x2, x2, :lo12:.LC35
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 336]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC36
	add	x2, x2, :lo12:.LC36
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 232]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC37
	add	x2, x2, :lo12:.LC37
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 462]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC38
	add	x2, x2, :lo12:.LC38
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 244]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC39
	add	x2, x2, :lo12:.LC39
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 2692]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC40
	add	x2, x2, :lo12:.LC40
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 248]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC41
	add	x2, x2, :lo12:.LC41
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 2696]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC42
	add	x2, x2, :lo12:.LC42
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 360]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC43
	add	x2, x2, :lo12:.LC43
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 26]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC44
	add	x2, x2, :lo12:.LC44
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrb	w3, [x19, 30]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC45
	add	x2, x2, :lo12:.LC45
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 24]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC46
	add	x2, x2, :lo12:.LC46
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrb	w3, [x19, 32]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC47
	add	x2, x2, :lo12:.LC47
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 28]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC48
	add	x2, x2, :lo12:.LC48
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w1, [x19, 24]
	adrp	x2, .LC49
	ldr	x0, [x19, 72]
	add	x2, x2, :lo12:.LC49
	ldrh	w3, [x0, x1, lsl 1]
	mov	x0, x22
	mov	x1, 64
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 82]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC50
	add	x2, x2, :lo12:.LC50
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrb	w3, [x19, 86]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC51
	add	x2, x2, :lo12:.LC51
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 80]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC52
	add	x2, x2, :lo12:.LC52
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrb	w3, [x19, 88]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC53
	add	x2, x2, :lo12:.LC53
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 84]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC54
	add	x2, x2, :lo12:.LC54
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w1, [x19, 80]
	adrp	x2, .LC55
	ldr	x0, [x19, 72]
	add	x2, x2, :lo12:.LC55
	ldrh	w3, [x0, x1, lsl 1]
	mov	x0, x22
	mov	x1, 64
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 130]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC56
	add	x2, x2, :lo12:.LC56
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrb	w3, [x19, 134]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC57
	add	x2, x2, :lo12:.LC57
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 128]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC58
	add	x2, x2, :lo12:.LC58
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrb	w3, [x19, 136]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC59
	add	x2, x2, :lo12:.LC59
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 132]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC60
	add	x2, x2, :lo12:.LC60
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 178]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC61
	add	x2, x2, :lo12:.LC61
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrb	w3, [x19, 182]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC62
	add	x2, x2, :lo12:.LC62
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 176]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC63
	add	x2, x2, :lo12:.LC63
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrb	w3, [x19, 184]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC64
	add	x2, x2, :lo12:.LC64
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldrh	w3, [x19, 180]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC65
	add	x2, x2, :lo12:.LC65
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w5, [x19, 2788]
	mov	x0, x22
	ldr	w4, [x19, 2796]
	mov	x1, 64
	ldr	w3, [x19, 2792]
	adrp	x2, .LC66
	add	x2, x2, :lo12:.LC66
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2784]
	mov	x0, x22
	mov	x1, 64
	adrp	x2, .LC67
	add	x2, x2, :lo12:.LC67
	bl	snprintf
	add	x22, x22, x0, sxtw
	ldr	w3, [x19, 2808]
	mov	x1, 64
	mov	x0, x22
	adrp	x2, .LC68
	add	x2, x2, :lo12:.LC68
	bl	snprintf
	add	x20, x22, x0, sxtw
	ldrh	w3, [x19, 3224]
	mov	x0, x20
	mov	x1, 64
	adrp	x2, .LC69
	add	x2, x2, :lo12:.LC69
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x19, 3226]
	mov	x0, x20
	mov	x1, 64
	adrp	x2, .LC70
	add	x2, x2, :lo12:.LC70
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	w3, [x19, 3228]
	mov	x0, x20
	mov	x1, 64
	adrp	x2, .LC71
	add	x2, x2, :lo12:.LC71
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w3, [x19, 3232]
	adrp	x2, .LC72
	add	x2, x2, :lo12:.LC72
	mov	x1, 64
	mov	x0, x20
	bl	snprintf
	add	x20, x20, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	and	w3, w0, 65535
	mov	x1, 64
	mov	x0, x20
	adrp	x2, .LC73
	add	x2, x2, :lo12:.LC73
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldrh	w0, [x19, 228]
	bl	GetFreeBlockMaxEraseCount
	and	w3, w0, 65535
	adrp	x2, .LC74
	mov	x0, x20
	add	x2, x2, :lo12:.LC74
	mov	x1, 64
	bl	snprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	cmp	w0, 1
	beq	.L241
.L246:
	sub	w0, w20, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
.L241:
	ldrh	w0, [x19, 176]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L243
	ldr	x1, [x19, 72]
	ubfiz	x0, x0, 1, 16
	adrp	x2, .LC75
	add	x2, x2, :lo12:.LC75
	ldrh	w3, [x1, x0]
	mov	x0, x20
	mov	x1, 64
	bl	snprintf
	add	x20, x20, x0, sxtw
.L243:
	mov	w0, 0
	adrp	x23, .LC76
	bl	List_get_gc_head_node
	add	x23, x23, :lo12:.LC76
	and	w4, w0, 65535
	mov	w22, 0
	mov	w26, 65535
	mov	w25, 6
.L245:
	ldr	x0, [x19, 2560]
	cmp	w4, w26
	beq	.L244
	umull	x24, w4, w25
	ubfiz	x1, x4, 1, 16
	ldr	x2, [x19, 72]
	add	x0, x0, x24
	ldr	x3, [x19, 2600]
	ldrh	w5, [x2, x1]
	mov	x2, x23
	ldrh	w6, [x0, 4]
	mov	x0, x20
	ldrh	w7, [x3, x1]
	mov	w3, w22
	mov	x1, 64
	add	w22, w22, 1
	bl	snprintf
	add	x20, x20, x0, sxtw
	ldr	x0, [x19, 2560]
	ldrh	w4, [x0, x24]
	cmp	w22, 16
	bne	.L245
.L244:
	ldr	x0, [x19, 2560]
	adrp	x23, .LC77
	ldr	x4, [x19, 2592]
	add	x23, x23, :lo12:.LC77
	mov	w22, 0
	mov	w25, 65535
	sub	x4, x4, x0
	mov	x0, -6148914691236517206
	movk	x0, 0xaaab, lsl 0
	mov	w26, 6
	asr	x4, x4, 1
	mul	x4, x4, x0
	and	w4, w4, 65535
.L247:
	cmp	w4, w25
	beq	.L246
	ldr	x0, [x19, 2560]
	umull	x24, w4, w26
	ldr	x2, [x19, 2600]
	ubfiz	x1, x4, 1, 16
	add	x0, x0, x24
	mov	w3, w22
	add	w22, w22, 1
	ldrh	w6, [x2, x1]
	mov	x2, x23
	ldrh	w5, [x0, 4]
	mov	x1, 64
	mov	x0, x20
	bl	snprintf
	add	x20, x20, x0, sxtw
	cmp	w22, 4
	beq	.L246
	ldr	x0, [x19, 2560]
	ldrh	w4, [x0, x24]
	b	.L247
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.section	.rodata.str1.1
.LC78:
	.string	"%s\n"
	.text
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE40
	.text
.LPFE40:
	nop
	nop
	cmp	w0, 2047
	ble	.L255
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LC1
	mov	x29, sp
	add	x3, x3, :lo12:.LC1
	adrp	x2, .LC78
	add	x2, x2, :lo12:.LC78
	stp	x19, x20, [sp, 16]
	mov	x19, x1
	mov	x1, 64
	add	x20, x19, 30
	mov	x0, x19
	bl	snprintf
	mov	x0, x20
	bl	FtlPrintInfo2buf
	add	x0, x20, x0, sxtw
	sub	w0, w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
.L255:
	mov	w0, 0
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.section	.rodata.str1.1
.LC79:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
	.text
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE41
	.text
.LPFE41:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ldr	w1, [x19, 2676]
	ldr	w0, [x19, 2688]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	cmp	w1, w0
	bcs	.L261
	ldrh	w1, [x19, 244]
	mov	x2, 0
	ldr	x4, [x19, 2600]
	str	wzr, [x19, 2668]
.L262:
	ldr	w0, [x19, 2668]
	cmp	w1, w2
	bhi	.L263
	udiv	w1, w0, w1
	str	w1, [x19, 2676]
	ldr	w1, [x19, 2672]
	sub	w0, w0, w1
	ldrh	w1, [x19, 296]
	udiv	w0, w0, w1
	str	w0, [x19, 2668]
.L264:
	ldr	w22, [x19, 2688]
	ldr	w24, [x19, 2676]
	add	w0, w22, 256
	cmp	w0, w24
	bls	.L269
	ldr	w1, [x19, 2684]
	add	w0, w22, 768
	cmp	w0, w1
	bls	.L269
.L271:
	mov	w20, 65535
.L270:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
.L263:
	ldrh	w3, [x4, x2, lsl 1]
	add	x2, x2, 1
	add	w0, w3, w0
	str	w0, [x19, 2668]
	b	.L262
.L261:
	ldr	w0, [x19, 2684]
	cmp	w1, w0
	bls	.L264
	add	w0, w0, 1
	str	w0, [x19, 2684]
	mov	x0, 0
.L266:
	ldrh	w1, [x19, 244]
	cmp	w1, w0
	bls	.L264
	ldr	x3, [x19, 2600]
	lsl	x2, x0, 1
	add	x0, x0, 1
	ldrh	w1, [x3, x2]
	add	w1, w1, 1
	strh	w1, [x3, x2]
	b	.L266
.L269:
	ldrh	w0, [x19, 228]
	add	w0, w0, w0, lsl 1
	ubfx	x0, x0, 2, 16
	bl	GetFreeBlockMaxEraseCount
	add	w1, w22, 64
	and	w27, w0, 65535
	cmp	w1, w0, uxth
	bhi	.L271
	ldr	x0, [x19, 2568]
	cbz	x0, .L271
	ldrh	w7, [x19, 244]
	mov	w23, 65535
	ldr	x3, [x19, 2560]
	mov	x8, -6148914691236517206
	ldr	x25, [x19, 2600]
	mov	w1, w23
	mov	w6, w23
	mov	w4, 0
	movk	x8, 0xaaab, lsl 0
	mov	w9, 6
.L272:
	ldrh	w2, [x0]
	cmp	w2, w6
	bne	.L275
	mov	w20, w1
.L274:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L271
	ldrh	w21, [x25, w20, uxtw 1]
	ubfiz	x26, x20, 1, 32
	cmp	w22, w21
	bcs	.L276
	bl	GetFreeBlockMinEraseCount
	cmp	w22, w0, uxth
	bcs	.L276
	str	w23, [x19, 2688]
.L276:
	cmp	w24, w21
	bls	.L271
	add	w0, w21, 128
	cmp	w27, w0
	ble	.L271
	add	w0, w21, 256
	ldr	w3, [x19, 2684]
	cmp	w24, w0
	bhi	.L277
	add	w21, w21, 768
	cmp	w21, w3
	bcs	.L271
.L277:
	ldr	x0, [x19, 72]
	mov	w6, w27
	ldrh	w5, [x25, x26]
	mov	w2, w24
	mov	w1, w20
	ldrh	w4, [x0, x26]
	adrp	x0, .LC79
	add	x0, x0, :lo12:.LC79
	bl	sftl_printk
	mov	w0, 1
	str	w0, [x19, 3236]
	b	.L270
.L275:
	add	w5, w4, 1
	and	w4, w5, 65535
	cmp	w7, w5, uxth
	bcc	.L271
	ldrh	w5, [x0, 4]
	cbz	w5, .L273
	sub	x0, x0, x3
	asr	x0, x0, 1
	mul	x0, x0, x8
	and	w20, w0, 65535
	and	x0, x0, 65535
	ldrh	w0, [x25, x0, lsl 1]
	cmp	w22, w0
	bcs	.L274
	cmp	w0, w23
	bcs	.L273
	mov	w23, w0
	mov	w1, w20
.L273:
	umaddl	x0, w2, w9, x3
	b	.L272
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE42
	.text
.LPFE42:
	nop
	nop
	mov	w2, 65535
	cmp	w2, w0, uxth
	beq	.L288
	hint	25 // paciasp
	adrp	x2, .LANCHOR0+72
	stp	x29, x30, [sp, -16]!
	and	w1, w0, 65535
	mov	x29, sp
	ldr	x2, [x2, #:lo12:.LANCHOR0+72]
	ubfiz	x0, x1, 1, 16
	strh	wzr, [x2, x0]
	mov	w0, w1
	bl	INSERT_FREE_LIST
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
.L288:
	mov	w0, 0
	ret
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	get_new_active_ppa
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE43
	.text
.LPFE43:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	ldrh	w1, [x0]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L292
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 111
	add	x0, x0, :lo12:.LC9
	mov	w2, 2792
	bl	sftl_printk
.L292:
	adrp	x21, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x21, 306]
	cmp	w1, w0
	bne	.L293
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 111
	add	x0, x0, :lo12:.LC9
	mov	w2, 2793
	bl	sftl_printk
.L293:
	ldrh	w0, [x19, 4]
	cbnz	w0, .L294
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 111
	add	x0, x0, :lo12:.LC9
	mov	w2, 2794
	bl	sftl_printk
.L294:
	ldrb	w0, [x19, 6]
	mov	w2, 65535
	strb	wzr, [x19, 10]
	add	x0, x0, 8
	ldrh	w3, [x21, 236]
	ldrh	w0, [x19, x0, lsl 1]
.L295:
	ldrh	w20, [x19, 2]
	cmp	w0, w2
	beq	.L297
	ldrh	w2, [x21, 306]
	cmp	w2, w20
	bls	.L301
	orr	w20, w20, w0, lsl 10
	ldrh	w0, [x19, 4]
	ldrb	w1, [x19, 6]
	mov	w5, 65535
	sub	w0, w0, #1
	and	w0, w0, 65535
	strh	w0, [x19, 4]
.L300:
	add	w4, w1, 1
	and	w1, w4, 255
	cmp	w3, w4, uxtb
	bne	.L299
	ldrh	w1, [x19, 2]
	add	w1, w1, 1
	strh	w1, [x19, 2]
	mov	w1, 0
.L299:
	add	x4, x19, x1, sxtw 1
	ldrh	w4, [x4, 16]
	cmp	w4, w5
	beq	.L300
	strb	w1, [x19, 6]
	ldrh	w1, [x19, 2]
	cmp	w1, w2
	bne	.L291
	cbz	w0, .L291
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 111
	add	x0, x0, :lo12:.LC9
	mov	w2, 2817
	bl	sftl_printk
.L291:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L297:
	ldrb	w1, [x19, 6]
	add	w1, w1, 1
	and	w1, w1, 255
	strb	w1, [x19, 6]
	cmp	w1, w3
	bne	.L296
	add	w20, w20, 1
	strh	w20, [x19, 2]
	strb	wzr, [x19, 6]
.L296:
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
	b	.L295
.L301:
	mov	w20, 65535
	b	.L291
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE44
	.text
.LPFE44:
	nop
	nop
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w7, 24
	mov	w8, 1
	mov	w4, 4
	str	wzr, [x0, 3240]
	hint	25 // paciasp
.L308:
	ldrh	w1, [x0, 236]
	cmp	w1, w2
	bhi	.L309
	mov	w6, 24
	mov	w5, 4
.L310:
	ldr	w2, [x0, 3280]
	cmp	w1, w2
	bcc	.L311
	hint	29 // autiasp
	ret
.L309:
	ldr	x5, [x0, 3248]
	umull	x3, w2, w7
	ldr	x6, [x0, 3256]
	add	x1, x5, x3
	str	w8, [x1, 16]
	ldrh	w1, [x0, 314]
	mul	w1, w1, w2
	sdiv	w1, w1, w4
	add	x1, x6, x1, sxtw 2
	str	x1, [x5, x3]
	ldrh	w1, [x0, 316]
	ldr	x6, [x0, 3248]
	ldr	x9, [x0, 3264]
	mul	w1, w1, w2
	add	x5, x6, x3
	sdiv	w1, w1, w4
	add	x1, x9, x1, sxtw 2
	str	x1, [x5, 8]
	ldr	x1, [x0, 3272]
	ubfiz	x9, x2, 5, 16
	ldr	x3, [x6, x3]
	add	x1, x1, x9
	add	w2, w2, 1
	and	w2, w2, 65535
	str	x3, [x1, 8]
	ldr	x3, [x5, 8]
	str	x3, [x1, 16]
	b	.L308
.L311:
	ldr	x3, [x0, 3248]
	umull	x4, w1, w6
	ldr	x7, [x0, 3256]
	add	x2, x3, x4
	str	wzr, [x2, 16]
	ldrh	w2, [x0, 314]
	mul	w2, w2, w1
	sdiv	w2, w2, w5
	add	x2, x7, x2, sxtw 2
	str	x2, [x3, x4]
	ldrh	w2, [x0, 316]
	ldr	x3, [x0, 3248]
	mul	w2, w2, w1
	add	x3, x3, x4
	ldr	x4, [x0, 3264]
	add	w1, w1, 1
	and	w1, w1, 65535
	sdiv	w2, w2, w5
	add	x2, x4, x2, sxtw 2
	str	x2, [x3, 8]
	b	.L310
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE45
	.text
.LPFE45:
	nop
	nop
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	mov	w3, 0
	mov	w8, 24
	hint	25 // paciasp
	ldr	w7, [x2, 3280]
	ldr	x5, [x2, 3248]
.L314:
	cmp	w3, w1
	bcs	.L313
	ubfiz	x4, x3, 5, 16
	mov	w2, 0
	add	x4, x0, x4
	b	.L319
.L315:
	add	w2, w2, 1
	and	w2, w2, 65535
.L319:
	cmp	w2, w7
	bcs	.L316
	umull	x6, w2, w8
	add	x9, x5, x6
	ldr	x10, [x5, x6]
	ldr	x6, [x4, 8]
	cmp	x10, x6
	bne	.L315
	str	wzr, [x9, 16]
.L316:
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L314
.L313:
	hint	29 // autiasp
	ret
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE46
	.text
.LPFE46:
	nop
	nop
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	mov	w7, 24
	mov	w8, 1
	hint	25 // paciasp
	ldr	w6, [x2, 3280]
	ldr	x5, [x2, 3248]
	mov	w2, 0
.L322:
	cmp	w2, w1
	bcc	.L327
	hint	29 // autiasp
	ret
.L323:
	add	w3, w3, 1
	and	w3, w3, 65535
.L326:
	cmp	w3, w6
	bcs	.L324
	umaddl	x4, w3, w7, x5
	ldr	w9, [x4, 16]
	cbnz	w9, .L323
	ubfiz	x3, x2, 5, 16
	str	w8, [x4, 16]
	add	x3, x0, x3
	ldr	x9, [x4]
	str	x9, [x3, 8]
	ldr	x4, [x4, 8]
	str	x4, [x3, 16]
.L324:
	add	w2, w2, 1
	and	w2, w2, 65535
	b	.L322
.L327:
	mov	w3, 0
	b	.L326
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE47
	.text
.LPFE47:
	nop
	nop
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w0, w0, 65535
	hint	25 // paciasp
	ldr	x2, [x1, 3288]
	ldrh	w3, [x1, 3284]
	mov	x1, 0
	sub	x2, x2, #2
.L330:
	cmp	w3, w1, uxth
	bhi	.L332
	mov	w0, 0
.L329:
	hint	29 // autiasp
	ret
.L332:
	add	x1, x1, 1
	ldrh	w4, [x2, x1, lsl 1]
	cmp	w4, w0
	bne	.L330
	mov	w0, 1
	b	.L329
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE48
	.text
.LPFE48:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	mov	w20, w2
	ubfx	x0, x19, 10, 16
	str	x21, [sp, 32]
	mov	w21, w1
	bl	P2V_block_in_plane
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	and	w6, w0, 65535
	mov	x4, 0
	ldr	x1, [x3, 3288]
	ldrh	w5, [x3, 3284]
	sub	x7, x1, #2
.L336:
	and	w2, w4, 65535
	cmp	w5, w2
	bhi	.L338
	bne	.L337
	strh	w0, [x1, x4, lsl 1]
	ldrh	w0, [x3, 3284]
	add	w0, w0, 1
	strh	w0, [x3, 3284]
	b	.L337
.L338:
	add	x4, x4, 1
	ldrh	w2, [x7, x4, lsl 1]
	cmp	w2, w6
	bne	.L336
.L337:
	ldrh	w4, [x3, 3296]
	mov	w0, 12
	umull	x4, w4, w0
	ldr	x0, [x3, 3304]
	add	x0, x0, x4
	str	w21, [x0, 4]
	ldr	x0, [x3, 3304]
	add	x1, x0, x4
	str	w20, [x1, 8]
	str	w19, [x0, x4]
	ldrh	w0, [x3, 3296]
	add	w0, w0, 1
	strh	w0, [x3, 3296]
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.section	.rodata.str1.1
.LC80:
	.string	"FtlGcRefreshBlock  0x%x\n"
	.text
	.align	2
	.global	FtlGcRefreshBlock
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE49
	.text
.LPFE49:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	str	x19, [sp, 16]
	and	w19, w0, 65535
	mov	w1, w19
	adrp	x0, .LC80
	add	x0, x0, :lo12:.LC80
	bl	sftl_printk
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w3, [x0, 226]
	cmp	w3, w19
	beq	.L341
	ldrh	w2, [x0, 224]
	cmp	w2, w19
	beq	.L341
	mov	w1, 65535
	cmp	w3, w1
	bne	.L342
	strh	w19, [x0, 226]
.L341:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
.L342:
	cmp	w2, w1
	bne	.L341
	strh	w19, [x0, 224]
	b	.L341
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.section	.rodata.str1.1
.LC81:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
	.text
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE50
	.text
.LPFE50:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	and	w20, w0, 65535
	str	x21, [sp, 32]
	mov	w0, w20
	bl	P2V_block_in_plane
	ldrh	w1, [x19, 3312]
	mov	w2, w20
	mov	w21, w0
	adrp	x0, .LC81
	add	x0, x0, :lo12:.LC81
	bl	sftl_printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 3312]
	add	x2, x19, 3314
	mov	x1, 0
.L345:
	cmp	w0, w1, uxth
	bhi	.L347
	cmp	w0, 15
	bhi	.L346
	add	w1, w0, 1
	add	x19, x19, 3314
	strh	w1, [x19, -2]
	strh	w20, [x19, w0, sxtw 1]
	b	.L346
.L347:
	add	x1, x1, 1
	add	x3, x2, x1, lsl 1
	ldrh	w3, [x3, -2]
	cmp	w3, w20
	bne	.L345
.L346:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE51
	.text
.LPFE51:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 3312]
	cbz	w0, .L350
	ldrh	w2, [x19, 226]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L350
	ldrh	w1, [x19, 3348]
	cmp	w1, w0
	bcc	.L351
	strh	wzr, [x19, 3348]
.L351:
	ldrh	w1, [x19, 3348]
	add	x0, x19, 3314
	ldrh	w0, [x0, x1, lsl 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 3348]
	add	w0, w0, 1
	strh	w0, [x19, 3348]
.L350:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	ftl_malloc
	.type	ftl_malloc, %function
ftl_malloc:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE52
	.text
.LPFE52:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	w1, 3265
	mov	x29, sp
	sxtw	x0, w0
	bl	__kmalloc
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	ftl_malloc, .-ftl_malloc
	.align	2
	.global	ftl_free
	.type	ftl_free, %function
ftl_free:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE53
	.text
.LPFE53:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	bl	kfree
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	ftl_free, .-ftl_free
	.section	.rodata.str1.1
.LC82:
	.string	"%s %p + 0x%x:"
.LC83:
	.string	"0x%08x,"
.LC84:
	.string	"0x%04x,"
.LC85:
	.string	"0x%02x,"
.LC86:
	.string	"\n"
	.text
	.align	2
	.global	rknand_print_hex
	.type	rknand_print_hex, %function
rknand_print_hex:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE54
	.text
.LPFE54:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x20, x1
	mov	x19, 0
	stp	x21, x22, [sp, 32]
	mov	w22, w2
	mov	w21, 0
	stp	x23, x24, [sp, 48]
	mov	w23, w3
	mov	w24, 0
	stp	x25, x26, [sp, 64]
	adrp	x26, .LC82
	mov	x25, x0
	add	x26, x26, :lo12:.LC82
	stp	x27, x28, [sp, 80]
	adrp	x27, .LC85
	add	x27, x27, :lo12:.LC85
	adrp	x28, .LC84
.L361:
	cmp	w23, w19
	bhi	.L367
	adrp	x0, .LC86
	add	x0, x0, :lo12:.LC86
	bl	sftl_printk
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
.L367:
	cbnz	w21, .L362
	mov	w3, w24
	mov	x2, x20
	mov	x1, x25
	mov	x0, x26
	bl	sftl_printk
.L362:
	cmp	w22, 4
	bne	.L363
	ldr	w1, [x20, x19, lsl 2]
	adrp	x0, .LC83
	add	x0, x0, :lo12:.LC83
.L369:
	add	w21, w21, 1
	bl	sftl_printk
	cmp	w21, 15
	bls	.L366
	mov	w21, 0
	adrp	x0, .LC86
	add	x0, x0, :lo12:.LC86
	bl	sftl_printk
.L366:
	add	x19, x19, 1
	add	w24, w24, w22
	b	.L361
.L363:
	cmp	w22, 2
	bne	.L365
	ldrh	w1, [x20, x19, lsl 1]
	add	x0, x28, :lo12:.LC84
	b	.L369
.L365:
	ldrb	w1, [x20, x19]
	mov	x0, x27
	b	.L369
	.size	rknand_print_hex, .-rknand_print_hex
	.section	.rodata.str1.1
.LC87:
	.string	"%s: addr: %x is in id block!!!!!!!!!!\n"
.LC88:
	.string	"not free: w: d:"
.LC89:
	.string	"not free: w: s:"
	.text
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE55
	.text
.LPFE55:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mrs	x1, sp_el0
	mov	x29, sp
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w0, 32
	stp	x23, x24, [sp, 48]
	mov	x20, x19
	umaddl	x22, w2, w0, x19
	stp	x25, x26, [sp, 64]
	mov	w24, -1
	ldrh	w23, [x21, 12]
	ldr	x3, [x1, 1376]
	str	x3, [sp, 88]
	mov	x3, 0
	lsl	w26, w23, 3
.L371:
	cmp	x20, x22
	beq	.L375
	ldr	w0, [x20, 4]
	add	x2, sp, 80
	add	x1, sp, 84
	bl	l2p_addr_tran.isra.0
	ldr	w0, [sp, 80]
	cbnz	w0, .L372
	ldr	w1, [sp, 84]
	cmp	w26, w1
	bls	.L372
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x21, .LC87
	add	x20, x20, 130
	add	x21, x21, :lo12:.LC87
.L373:
	cmp	x19, x22
	bne	.L374
	bl	dump_stack
.L375:
	mrs	x0, sp_el0
	ldr	x1, [sp, 88]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L382
	bl	__stack_chk_fail
.L374:
	ldr	w2, [sp, 84]
	mov	w0, -1
	str	w0, [x19]
	mov	x1, x20
	mov	x0, x21
	add	x19, x19, 32
	bl	sftl_printk
	ldr	x1, [x19, -24]
	mov	w3, 16
	mov	w2, 4
	adrp	x0, .LC88
	add	x0, x0, :lo12:.LC88
	bl	rknand_print_hex
	ldr	x1, [x19, -16]
	mov	w3, 4
	adrp	x0, .LC89
	mov	w2, w3
	add	x0, x0, :lo12:.LC89
	bl	rknand_print_hex
	b	.L373
.L372:
	ldr	w1, [sp, 84]
	ldr	x2, [x21, 3360]
	blr	x2
	cmp	w0, 0
	csel	w0, wzr, w24, eq
	str	w0, [x20]
	ldrh	w0, [x21, 14]
	cmp	w0, 4
	bne	.L379
	ldrb	w0, [sp, 80]
	ldr	x2, [x21, 3360]
	ldr	w1, [sp, 84]
	add	w1, w23, w1
	blr	x2
	cbz	w0, .L379
	str	w24, [x20]
.L379:
	add	x20, x20, 32
	b	.L371
.L382:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE56
	.text
.LPFE56:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	str	x21, [sp, 32]
	and	w21, w0, 65535
	sub	w2, w21, #1
	mov	w0, 65533
	stp	x19, x20, [sp, 16]
	cmp	w0, w2, uxth
	bcc	.L388
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 462]
	cmp	w0, 1024
	beq	.L388
	and	w1, w1, 65535
	cbz	w1, .L390
	mov	w0, w21
	bl	P2V_block_in_plane
	and	w20, w0, 65535
	lsl	w1, w21, 10
	ldr	x0, [x19, 3400]
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 3400]
	bl	FlashEraseBlocks
	ldr	x2, [x19, 2600]
	ubfiz	x0, x20, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	ldr	w0, [x19, 2680]
	add	w0, w0, 1
	str	w0, [x19, 2680]
.L390:
	ldrh	w0, [x19, 462]
	add	x19, x19, 456
	ldrh	w1, [x19, 4]
	add	w0, w0, 1
	strh	w0, [x19, 6]
	ldrh	w0, [x19, 4]
	add	x1, x19, x1, lsl 1
	add	w0, w0, 1
	and	w0, w0, 1023
	strh	w0, [x19, 4]
	strh	w21, [x1, 8]
.L388:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.section	.rodata.str1.1
.LC90:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
	.text
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE57
	.text
.LPFE57:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w2, [x19, 462]
	cbz	w2, .L402
	ldrh	w3, [x19, 458]
	add	x0, x19, 456
	ldrh	w1, [x19, 458]
	sub	w2, w2, #1
	strh	w2, [x19, 462]
	mov	w2, 1
	add	x3, x0, x3, lsl 1
	add	w1, w1, 1
	ldr	x0, [x19, 3400]
	and	w1, w1, 1023
	ldrh	w20, [x3, 8]
	strh	w1, [x19, 458]
	lsl	w1, w20, 10
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 3400]
	bl	FlashEraseBlocks
	ldr	w0, [x19, 2680]
	sub	w1, w20, #1
	add	w0, w0, 1
	str	w0, [x19, 2680]
	mov	w0, 65533
	cmp	w0, w1, uxth
	bcs	.L400
.L399:
	ldrh	w2, [x19, 462]
	adrp	x0, .LC90
	mov	w1, w20
	add	x0, x0, :lo12:.LC90
	bl	sftl_printk
.L401:
	b	.L401
.L402:
	mov	w20, 65535
	b	.L399
.L400:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.section	.rodata.str1.1
.LC91:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
	.text
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE58
	.text
.LPFE58:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w20, 0
	str	x21, [sp, 32]
	ldrh	w1, [x0, 10]
	ldr	x0, [x0, 16]
.L405:
	cmp	w1, w20
	beq	.L411
	mov	x21, x0
	ldrh	w2, [x0], 2
	cbnz	w2, .L406
	bl	FtlFreeSysBlkQueueOut
	and	w1, w0, 65535
	sub	w2, w1, #1
	strh	w0, [x21]
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L407
	adrp	x0, .LANCHOR0+462
	ldrh	w2, [x0, #:lo12:.LANCHOR0+462]
	adrp	x0, .LC91
	add	x0, x0, :lo12:.LC91
	bl	sftl_printk
.L408:
	b	.L408
.L407:
	ldr	w0, [x19, 48]
	strh	w20, [x19]
	add	w0, w0, 1
	str	w0, [x19, 48]
	ldrh	w0, [x19, 8]
	strh	wzr, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 8]
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L409
.L411:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 147
	add	x0, x0, :lo12:.LC9
	mov	w2, 578
	bl	sftl_printk
.L409:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L406:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L405
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	ftl_memset
	.type	ftl_memset, %function
ftl_memset:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE59
	.text
.LPFE59:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	uxtw	x2, w2
	mov	x29, sp
	bl	memset
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	ftl_memset, .-ftl_memset
	.section	.rodata.str1.1
.LC92:
	.string	"%s error allocating memory. return -1\n"
	.text
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE60
	.text
.LPFE60:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	w0, 65535
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x4, x19, 3416
	add	x3, x19, 2676
	add	x2, x19, 2656
	add	x1, x19, 2636
	str	wzr, [x19, 2688]
	stp	wzr, w0, [x4, -8]
	mov	w0, -1
	strh	w0, [x19, 224]
	strh	w0, [x19, 226]
	mov	w0, 32
	stp	wzr, wzr, [x2, -12]
	stp	wzr, wzr, [x3, -8]
	stp	wzr, wzr, [x1, -8]
	stp	wzr, wzr, [x4]
	stp	wzr, wzr, [x1]
	stp	wzr, wzr, [x3, 4]
	stp	wzr, wzr, [x2, 4]
	strh	w0, [x19, 3224]
	mov	w0, 128
	str	x21, [sp, 32]
	strh	w0, [x19, 3226]
	str	wzr, [x19, 3228]
	strh	wzr, [x19, 3232]
	str	wzr, [x19, 3236]
	strh	wzr, [x19, 3312]
	strh	wzr, [x19, 3348]
	ldrh	w0, [x19, 310]
	lsl	w0, w0, 1
	bl	ftl_malloc
	ldrh	w1, [x19, 310]
	str	x0, [x19, 3288]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w20, [x19, 236]
	str	x0, [x19, 3304]
	lsl	w21, w20, 7
	lsl	w20, w20, 5
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3424]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3432]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19, 3440]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3400]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3272]
	ldrh	w0, [x19, 236]
	ldrh	w20, [x19, 314]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19, 3280]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3448]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3456]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3464]
	ldr	w0, [x19, 3280]
	mul	w0, w20, w0
	bl	ftl_malloc
	str	x0, [x19, 3256]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3472]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3480]
	ldr	w1, [x19, 3280]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3248]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3488]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3496]
	ldrh	w0, [x19, 262]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w20, [x19, 316]
	str	x0, [x19, 3504]
	ldrh	w0, [x19, 236]
	mul	w20, w20, w0
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3512]
	lsl	w0, w20, 2
	bl	ftl_malloc
	str	x0, [x19, 3520]
	ldrh	w1, [x19, 316]
	ldr	w0, [x19, 3280]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3264]
	ldrh	w0, [x19, 246]
	ubfiz	w1, w0, 1, 15
	ubfiz	w0, w0, 1, 15
	strh	w1, [x19, 3528]
	bl	ftl_malloc
	str	x0, [x19, 3536]
	ldrh	w0, [x19, 3528]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19, 3528]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w20, [x19, 246]
	str	x0, [x19, 3544]
	add	x0, x0, 32
	str	x0, [x19, 2600]
	lsl	w20, w20, 1
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3552]
	mov	w0, w20
	bl	ftl_malloc
	ldr	w20, [x19, 332]
	str	x0, [x19, 72]
	lsl	w20, w20, 1
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3560]
	mov	w0, w20
	bl	ftl_malloc
	str	x0, [x19, 3568]
	ldrh	w0, [x19, 324]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19, 352]
	ldrh	w0, [x19, 324]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19, 3576]
	ldrh	w0, [x19, 324]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3584]
	ldrh	w0, [x19, 326]
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w2, [x19, 326]
	mov	w1, 0
	str	x0, [x19, 3592]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x19, 340]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3600]
	ldr	w0, [x19, 332]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19, 3608]
	ldrh	w0, [x19, 342]
	lsl	w0, w0, 4
	bl	ftl_malloc
	ldrh	w1, [x19, 342]
	str	x0, [x19, 2608]
	ldrh	w0, [x19, 314]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 3616]
	ldrh	w1, [x19, 246]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19, 2560]
	ldrh	w0, [x19, 302]
	ldrh	w1, [x19, 258]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19, 3624]
	mul	w0, w1, w0
	lsl	w0, w0, 2
	bl	ftl_malloc
	ldrh	w3, [x19, 3624]
	add	x4, x19, 360
	ldrh	w1, [x19, 258]
	mov	x20, x0
	mov	w2, w3
	str	x0, [x4, 32]
	mov	x0, 0
.L416:
	add	x0, x0, 1
	cmp	w1, w0
	bhi	.L417
	cmp	w1, 0
	mov	w2, 8
	csinc	w1, w1, wzr, ne
	add	x0, x19, 360
	sub	w2, w2, w1
	add	w1, w1, 4
	ubfiz	x3, x1, 3, 17
	lsl	x2, x2, 3
	add	x0, x0, x3
	mov	w1, 0
	bl	memset
	ldr	x0, [x19, 3560]
	cbnz	x0, .L418
.L420:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 173
	adrp	x0, .LC92
	add	x0, x0, :lo12:.LC92
	bl	sftl_printk
	mov	w0, -1
.L415:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L417:
	add	x5, x4, x0, lsl 3
	add	x6, x20, x2, uxtw 2
	add	w2, w2, w3
	str	x6, [x5, 32]
	b	.L416
.L418:
	ldr	x0, [x19, 3568]
	cbz	x0, .L420
	ldr	x0, [x19, 3600]
	cbz	x0, .L420
	ldr	x0, [x19, 3608]
	cbz	x0, .L420
	ldr	x0, [x19, 2608]
	cbz	x0, .L420
	ldr	x0, [x19, 3616]
	cbz	x0, .L420
	ldr	x0, [x19, 2560]
	cbz	x0, .L420
	cbz	x20, .L420
	ldr	x0, [x19, 72]
	cbz	x0, .L420
	ldr	x0, [x19, 3288]
	cbz	x0, .L420
	ldr	x0, [x19, 3304]
	cbz	x0, .L420
	ldr	x0, [x19, 3424]
	cbz	x0, .L420
	ldr	x0, [x19, 3440]
	cbz	x0, .L420
	ldr	x0, [x19, 3400]
	cbz	x0, .L420
	ldr	x0, [x19, 3272]
	cbz	x0, .L420
	ldr	x0, [x19, 3432]
	cbz	x0, .L420
	ldr	x0, [x19, 3448]
	cbz	x0, .L420
	ldr	x0, [x19, 3456]
	cbz	x0, .L420
	ldr	x0, [x19, 3464]
	cbz	x0, .L420
	ldr	x0, [x19, 3256]
	cbz	x0, .L420
	ldr	x0, [x19, 3472]
	cbz	x0, .L420
	ldr	x0, [x19, 3480]
	cbz	x0, .L420
	ldr	x0, [x19, 3248]
	cbz	x0, .L420
	ldr	x0, [x19, 3512]
	cbz	x0, .L420
	ldr	x0, [x19, 3520]
	cbz	x0, .L420
	ldr	x0, [x19, 3264]
	cbz	x0, .L420
	ldr	x0, [x19, 2600]
	cbz	x0, .L420
	ldr	x0, [x19, 3536]
	cbz	x0, .L420
	ldr	x0, [x19, 352]
	cbz	x0, .L420
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3576]
	cbz	x1, .L420
	ldr	x1, [x0, 3584]
	cbz	x1, .L420
	ldr	x0, [x0, 3592]
	cbz	x0, .L420
	mov	w0, 0
	b	.L415
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE61
	.text
.LPFE61:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -64]!
	mov	x29, sp
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR0
	add	x23, x23, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR1
	add	x21, x21, :lo12:.LANCHOR1
	ldrh	w2, [x23, 3624]
	adrp	x24, .LC9
	add	x21, x21, 184
	add	x24, x24, :lo12:.LC9
	stp	x19, x20, [sp, 16]
	mov	x19, 0
	mov	x20, x1
	mov	x22, x0
	lsl	w2, w2, 2
	mov	x0, x20
	mov	w1, 0
	bl	ftl_memset
.L518:
	ldrh	w0, [x22, x19, lsl 1]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L515
	ldrh	w1, [x23, 302]
	cmp	w1, w0
	bhi	.L517
	mov	x1, x21
	mov	x0, x24
	mov	w2, 74
	bl	sftl_printk
.L517:
	ldrh	w3, [x22, x19, lsl 1]
	mov	w2, 1
	add	x19, x19, 1
	ubfx	x1, x3, 5, 11
	lsl	w3, w2, w3
	lsl	x1, x1, 2
	ldr	w2, [x20, x1]
	orr	w2, w2, w3
	str	w2, [x20, x1]
	cmp	x19, 512
	bne	.L518
.L515:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	hint	29 // autiasp
	ret
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE62
	.text
.LPFE62:
	nop
	nop
	hint	25 // paciasp
	adrp	x0, .LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	mov	x29, sp
	mov	w2, 16
	add	x0, x0, 372
	strh	w1, [x0, -12]
	mov	w1, 255
	strh	wzr, [x0, -6]
	bl	ftl_memset
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE63
	.text
.LPFE63:
	nop
	nop
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	hint	25 // paciasp
	add	x1, x3, 456
	stp	x29, x30, [sp, -16]!
	mov	w2, 2048
	mov	x29, sp
	str	wzr, [x1, 2]
	mov	w1, 0
	strh	w0, [x3, 456]
	add	x0, x3, 464
	strh	wzr, [x3, 462]
	bl	ftl_memset
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	ftl_free_no_use_map_blk
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE64
	.text
.LPFE64:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mov	w1, 0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldp	x21, x20, [x0, 32]
	ldrh	w2, [x0, 10]
	ldr	x22, [x0, 16]
	lsl	w2, w2, 1
	mov	x0, x21
	bl	ftl_memset
	mov	w0, 0
.L526:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L530
	adrp	x0, .LANCHOR0+308
	mov	w24, 0
	mov	w20, 0
	ldrh	w1, [x0, #:lo12:.LANCHOR0+308]
	ldrh	w0, [x19]
	strh	w1, [x21, x0, lsl 1]
	ldrh	w25, [x21]
.L531:
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L534
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
.L530:
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20, x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L527:
	ldrh	w3, [x19, 10]
	cmp	w3, w1
	bhi	.L529
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L526
.L529:
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22, x3]
	cmp	w4, w2
	bne	.L528
	cbz	w2, .L528
	ldrh	w4, [x21, x3]
	add	w4, w4, 1
	strh	w4, [x21, x3]
.L528:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L527
.L534:
	ubfiz	x23, x20, 1, 16
	ldrh	w0, [x21, x23]
	cmp	w0, w25
	bcs	.L532
	ldrh	w1, [x22, x23]
	cmp	w1, 0
	csel	w24, w24, w20, eq
	csel	w25, w25, w0, eq
.L532:
	cbnz	w0, .L533
	ldrh	w0, [x22, x23]
	cbz	w0, .L533
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x22, x23]
	ldrh	w0, [x19, 8]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
.L533:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L531
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE65
	.text
.LPFE65:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	w1, 0
	mov	x29, sp
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19, 3568]
	ldr	w2, [x19, 332]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x19, 342]
	mov	w1, 255
	ldrh	w2, [x19, 314]
	mul	w2, w2, w0
	ldr	x0, [x19, 3616]
	bl	ftl_memset
	mov	w0, 0
	mov	w4, -1
.L545:
	ldrh	w1, [x19, 342]
	cmp	w1, w0
	bhi	.L546
	ldr	w1, [x19, 332]
	strh	w1, [x19, 3642]
	mov	w1, -1
	str	w1, [x19, 3632]
	mov	w1, -3902
	strh	w1, [x19, 3636]
	ldrh	w1, [x19, 3696]
	strh	w1, [x19, 3640]
	ldrh	w1, [x19, 340]
	strh	w1, [x19, 3638]
	ldr	x1, [x19, 3560]
	str	x1, [x19, 3648]
	ldr	x1, [x19, 3608]
	str	x1, [x19, 3656]
	ldr	x1, [x19, 3568]
	str	x1, [x19, 3664]
	ldr	x1, [x19, 3600]
	str	x1, [x19, 3672]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
.L546:
	ldr	x2, [x19, 2608]
	ubfiz	x1, x0, 4, 16
	add	x2, x2, x1
	str	wzr, [x2, 4]
	ldr	x2, [x19, 2608]
	strh	w4, [x2, x1]
	ldr	x2, [x19, 2608]
	ldr	x3, [x19, 3616]
	add	x2, x2, x1
	ldrh	w1, [x19, 314]
	mul	w1, w1, w0
	add	w0, w0, 1
	and	w0, w0, 65535
	sxtw	x1, w1
	and	x1, x1, -4
	add	x1, x3, x1
	str	x1, [x2, 8]
	b	.L545
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE66
	.text
.LPFE66:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	w0, -1
	mov	x29, sp
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	strh	w0, [x19, 3698]
	mov	w0, -1
	str	w0, [x19, 3708]
	ldr	x0, [x19, 352]
	strh	wzr, [x19, 348]
	ldrh	w2, [x19, 324]
	str	wzr, [x19, 3700]
	str	wzr, [x19, 3704]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 2600]
	mov	w1, 0
	ldrh	w2, [x19, 246]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 3536]
	mov	w1, 0
	ldrh	w2, [x19, 246]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x19, 2512
	mov	w2, 48
	mov	w1, 0
	bl	ftl_memset
	mov	w2, 512
	mov	w1, 0
	add	x0, x19, 2712
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE67
	.text
.LPFE67:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -112]!
	mov	w0, 6
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	add	x25, x19, 264
	mov	w1, 0
	ldrh	w2, [x19, 246]
	mov	w20, 0
	mov	w26, -1
	stp	x21, x22, [sp, 32]
	mov	w21, 0
	stp	x23, x24, [sp, 48]
	mov	w23, 0
	mul	w2, w2, w0
	ldr	x0, [x19, 2560]
	stp	x27, x28, [sp, 80]
	mov	w27, 32768
	bl	ftl_memset
	strh	wzr, [x19, 228]
	str	xzr, [x19, 2568]
	str	xzr, [x19, 2576]
	strh	wzr, [x19, 2584]
	str	xzr, [x19, 2592]
.L551:
	ldrh	w0, [x19, 244]
	cmp	w0, w20
	bls	.L558
	ldrh	w2, [x19, 236]
	mov	x24, 0
	ldrh	w28, [x19, 306]
	mov	w22, 0
	b	.L559
.L553:
	ldrb	w0, [x25, x24]
	mov	w1, w20
	str	w2, [sp, 108]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	w2, [sp, 108]
	cbnz	w0, .L552
	add	w22, w28, w22
	and	w22, w22, 65535
.L552:
	add	x24, x24, 1
.L559:
	cmp	w2, w24, uxth
	bhi	.L553
	cbz	w22, .L554
	udiv	w22, w27, w22
.L555:
	ldr	x1, [x19, 2560]
	mov	w0, 6
	umaddl	x0, w20, w0, x1
	strh	w22, [x0, 4]
	ldrh	w0, [x19, 24]
	cmp	w0, w20
	beq	.L556
	ldrh	w0, [x19, 80]
	cmp	w0, w20
	beq	.L556
	ldrh	w0, [x19, 128]
	cmp	w0, w20
	beq	.L556
	ldr	x1, [x19, 72]
	ubfiz	x0, x20, 1, 16
	ldrh	w0, [x1, x0]
	cbnz	w0, .L557
	add	w23, w23, 1
	mov	w0, w20
	and	w23, w23, 65535
	bl	INSERT_FREE_LIST
.L556:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L551
.L554:
	ldr	x1, [x19, 72]
	ubfiz	x0, x20, 1, 16
	strh	w26, [x1, x0]
	b	.L555
.L557:
	add	w21, w21, 1
	mov	w0, w20
	and	w21, w21, 65535
	bl	INSERT_DATA_LIST
	b	.L556
.L558:
	strh	w23, [x19, 228]
	strh	w21, [x19, 2584]
	add	w21, w21, w23
	cmp	w21, w0
	ble	.L560
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 198
	add	x0, x0, :lo12:.LC9
	mov	w2, 2219
	bl	sftl_printk
.L560:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	hint	29 // autiasp
	ret
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE68
	.text
.LPFE68:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	w1, 255
	mov	x29, sp
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19, 3288]
	strh	wzr, [x19, 3284]
	ldrh	w2, [x19, 310]
	strh	wzr, [x19, 3296]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x19, 310]
	mov	w0, 12
	mov	w1, 255
	mul	w2, w2, w0
	ldr	x0, [x19, 3304]
	bl	ftl_memset
	bl	FtlGcBufInit
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE69
	.text
.LPFE69:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	w2, 256
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	mov	x19, x0
	str	x21, [sp, 32]
	mov	w21, w1
	mov	w1, 255
	bl	ftl_memset
	ldr	x2, [x20, 3352]
	mov	w1, w21
	mov	x0, x19
	blr	x2
	and	w0, w0, 65535
	cmp	w0, 50
	bls	.L565
	mov	x0, x19
	mov	w2, 256
	mov	w1, 255
	bl	ftl_memset
	mov	w0, 0
.L565:
	ldrh	w1, [x20, 14]
	cmp	w1, 4
	bne	.L570
	mov	x1, 0
	b	.L566
.L567:
	ldrh	w2, [x19, x1, lsl 1]
	lsr	w2, w2, 1
	strh	w2, [x19, x1, lsl 1]
	add	x1, x1, 1
.L566:
	cmp	w0, w1, uxth
	bhi	.L567
.L570:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	ftl_memcpy
	.type	ftl_memcpy, %function
ftl_memcpy:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE70
	.text
.LPFE70:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	uxtw	x2, w2
	mov	x29, sp
	bl	memcpy
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	ftl_memcpy, .-ftl_memcpy
	.section	.rodata.str1.1
.LC93:
	.string	"FlashReadPages %x %x error_ecc_bits %d\n"
.LC94:
	.string	"data:"
.LC95:
	.string	"spare:"
	.text
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE71
	.text
.LPFE71:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mrs	x2, sp_el0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, 32
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR1
	add	x22, x22, :lo12:.LANCHOR1
	stp	x23, x24, [sp, 48]
	umaddl	x23, w1, w0, x19
	adrp	x24, .LC9
	stp	x25, x26, [sp, 64]
	add	x22, x22, 216
	ldrh	w25, [x20, 12]
	ldr	x3, [x2, 1376]
	str	x3, [sp, 88]
	mov	x3, 0
	add	x24, x24, :lo12:.LC9
.L575:
	cmp	x19, x23
	bne	.L588
	mrs	x0, sp_el0
	ldr	x1, [sp, 88]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L589
	bl	__stack_chk_fail
.L588:
	ldr	x0, [x19, 8]
	cbz	x0, .L576
	ldr	x0, [x19, 16]
	cbnz	x0, .L577
.L576:
	mov	x1, x22
	mov	x0, x24
	mov	w2, 96
	bl	sftl_printk
.L577:
	ldr	w0, [x19, 4]
	add	x2, sp, 80
	add	x1, sp, 84
	bl	l2p_addr_tran.isra.0
	ldr	w0, [sp, 80]
	cmp	w0, 3
	bls	.L578
	mov	w0, -1
	str	w0, [x19]
.L579:
	add	x19, x19, 32
	b	.L575
.L578:
	ldr	x21, [x19, 8]
	tst	x21, 63
	beq	.L580
	ldr	x21, [x20, 3488]
.L580:
	mov	x2, x21
	ldr	w1, [sp, 84]
	ldr	x3, [x19, 16]
	ldr	x4, [x20, 3376]
	blr	x4
	str	w0, [x19]
	ldrh	w0, [x20, 14]
	cmp	w0, 4
	bne	.L582
	ldrb	w0, [sp, 80]
	add	x2, x21, 2048
	ldr	x4, [x20, 3376]
	ldr	x3, [x19, 16]
	ldr	w1, [sp, 84]
	add	x3, x3, 8
	add	w1, w25, w1
	blr	x4
	cmn	w0, #1
	beq	.L583
	ldr	x1, [x19, 16]
	ldr	w2, [x1, 12]
	cmn	w2, #1
	bne	.L584
	ldr	w2, [x1, 8]
	cmn	w2, #1
	bne	.L584
	ldr	w1, [x1]
	cmn	w1, #1
	beq	.L584
.L583:
	mov	w0, -1
.L615:
	str	w0, [x19]
	b	.L585
.L584:
	ldr	w1, [x19]
	cmn	w1, #1
	beq	.L585
	cmp	w0, 256
	beq	.L615
.L585:
	ldr	w3, [x19]
	cmp	w3, 256
	ccmn	w3, #1, 4, ne
	bne	.L582
	ldr	w1, [x19, 4]
	adrp	x0, .LC93
	ldr	w2, [sp, 84]
	add	x0, x0, :lo12:.LC93
	bl	sftl_printk
	ldr	x1, [x19, 8]
	cbz	x1, .L587
	mov	w3, 4
	adrp	x0, .LC94
	mov	w2, w3
	add	x0, x0, :lo12:.LC94
	bl	rknand_print_hex
.L587:
	ldr	x1, [x19, 16]
	cbz	x1, .L582
	mov	w3, 4
	adrp	x0, .LC95
	mov	w2, w3
	add	x0, x0, :lo12:.LC95
	bl	rknand_print_hex
.L582:
	ldr	x0, [x20, 3488]
	cmp	x0, x21
	bne	.L579
	ldr	x0, [x19, 8]
	cmp	x0, x21
	beq	.L579
	ldrh	w2, [x20, 262]
	mov	x1, x21
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L579
.L589:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE72
	.text
.LPFE72:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x21, x20, 3712
	mov	w22, 0
	stp	x23, x24, [sp, 48]
	add	x23, x20, 360
	stp	x25, x26, [sp, 64]
	mov	w25, -1
	mov	w26, 61664
	ldr	x0, [x20, 3448]
	ldr	x24, [x20, 3512]
	stp	x0, x24, [x21, 8]
.L617:
	ldrh	w0, [x20, 258]
	cmp	w0, w22
	bhi	.L622
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
.L622:
	ldrh	w19, [x20, 302]
	strh	w25, [x23, 12]
.L619:
	ldrh	w0, [x20, 302]
	sub	w19, w19, #1
	and	w19, w19, 65535
	sub	w1, w0, #16
	cmp	w19, w1
	ble	.L620
	madd	w0, w0, w22, w19
	mov	w2, 1
	mov	w1, w2
	lsl	w0, w0, 10
	str	w0, [x21, 4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L619
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L619
	strh	w19, [x23, 12]
.L620:
	add	w22, w22, 1
	add	x23, x23, 2
	b	.L617
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE73
	.text
.LPFE73:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -176]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	mov	w22, w1
	mrs	x1, sp_el0
	stp	x23, x24, [sp, 48]
	cmp	w22, 1
	ldr	x2, [x1, 1376]
	str	x2, [sp, 168]
	mov	x2, 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	bne	.L628
	ldrh	w19, [x1, 308]
.L629:
	sub	w19, w19, #1
	lsl	w21, w0, 10
	ldr	x1, [x1, 3496]
	sxth	w19, w19
	orr	w0, w19, w0, lsl 10
	str	x1, [sp, 80]
	add	x1, sp, 104
	mov	w2, w22
	str	w0, [sp, 76]
	add	x0, sp, 72
	str	x1, [sp, 88]
	mov	w1, 1
	bl	FlashReadPages
	ldr	w0, [sp, 104]
	cmn	w0, #1
	bne	.L631
	mov	w23, 0
	mov	w24, 2
	b	.L630
.L628:
	ldrh	w19, [x1, 306]
	b	.L629
.L634:
	add	w0, w23, w19
	mov	w1, 1
	mov	w2, w22
	sdiv	w0, w0, w24
	sxth	w20, w0
	orr	w0, w0, w21
	str	w0, [sp, 76]
	add	x0, sp, 72
	bl	FlashReadPages
	ldp	w0, w1, [sp, 104]
	and	w0, w0, w1
	cmn	w0, #1
	bne	.L632
	ldr	w0, [sp, 72]
	cmn	w0, #1
	beq	.L632
	sub	w19, w20, #1
	sxth	w19, w19
.L630:
	cmp	w23, w19
	ble	.L634
.L631:
	mrs	x0, sp_el0
	ldr	x1, [sp, 168]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L635
	bl	__stack_chk_fail
.L632:
	add	w20, w20, 1
	sxth	w23, w20
	b	.L630
.L635:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 176
	hint	29 // autiasp
	ret
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE74
	.text
.LPFE74:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mov	w1, 0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ldr	x0, [x19, 3608]
	stp	x23, x24, [sp, 48]
	ldr	w2, [x19, 332]
	stp	x25, x26, [sp, 64]
	adrp	x24, .LANCHOR1
	add	x24, x24, :lo12:.LANCHOR1
	lsl	w2, w2, 2
	stp	x27, x28, [sp, 80]
	add	x24, x24, 231
	strh	wzr, [x19, 348]
	strh	wzr, [x19, 3696]
	bl	ftl_memset
	ldr	x0, [x19, 3560]
	mov	w1, 0
	ldr	w2, [x19, 332]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19, 3584]
	mov	w1, 0
	ldrh	w2, [x19, 324]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x19, 352]
	mov	w1, 0
	ldrh	w2, [x19, 324]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x19, 2696
	mov	w2, 16
	mov	w1, 255
	bl	ftl_memset
	ldrh	w23, [x19, 244]
.L642:
	ldrh	w0, [x19, 246]
	cmp	w0, w23
	bls	.L681
	ldrh	w28, [x19, 236]
	add	x26, x19, 264
	ldrh	w27, [x19, 316]
	mov	x21, 0
	mov	w22, 0
	mov	w25, 4
	b	.L682
.L644:
	ldrb	w0, [x26, x21]
	mov	w1, w23
	bl	V2P_block
	and	w20, w0, 65535
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L643
	ldr	x0, [x19, 3424]
	ubfiz	x2, x22, 5, 16
	lsl	w20, w20, 10
	add	x0, x0, x2
	str	w20, [x0, 4]
	ldr	x1, [x19, 3424]
	ldr	x0, [x19, 3256]
	add	x1, x1, x2
	ldr	x2, [x19, 3264]
	str	x0, [x1, 8]
	mul	w0, w22, w27
	add	w22, w22, 1
	and	w22, w22, 65535
	sdiv	w0, w0, w25
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L643:
	add	x21, x21, 1
.L682:
	cmp	w28, w21, uxth
	bhi	.L644
	cbnz	w22, .L645
.L680:
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L642
.L645:
	ldr	x0, [x19, 3424]
	adrp	x26, .LC9
	mov	w1, w22
	add	x26, x26, :lo12:.LC9
	mov	w2, 1
	mov	x25, 0
	bl	FlashReadPages
.L679:
	ldr	x0, [x19, 3424]
	lsl	x27, x25, 5
	add	x1, x0, x25, lsl 5
	ldr	w0, [x0, x27]
	ldr	w20, [x1, 4]
	ldr	x21, [x1, 16]
	ubfx	x20, x20, 10, 16
	cmn	w0, #1
	bne	.L648
	mov	w28, 16
.L650:
	ldr	x0, [x19, 3424]
	mov	w2, 1
	add	x0, x0, x27
	ldr	w1, [x0, 4]
	add	w1, w1, 1
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 3424]
	add	x0, x0, x27
	bl	FlashReadPages
	ldrh	w0, [x21]
	mov	w3, 65535
	cmp	w0, w3
	ldr	x0, [x19, 3424]
	bne	.L647
	mov	w1, -1
	str	w1, [x0, x27]
	ldr	x0, [x19, 3424]
	ldr	w0, [x0, x27]
	cmp	w0, w1
	beq	.L649
.L648:
	ldr	w1, [x19, 2660]
	ldr	w0, [x21, 4]
	cmn	w1, #1
	beq	.L697
	cmp	w1, w0
	bhi	.L651
.L697:
	cmn	w0, #1
	beq	.L651
	add	w1, w0, 1
	str	w1, [x19, 2660]
.L651:
	ldrh	w1, [x21]
	mov	w2, 61634
	cmp	w1, w2
	beq	.L652
	bhi	.L653
	mov	w2, 61574
	cmp	w1, w2
	beq	.L654
	mov	w2, 61604
	cmp	w1, w2
	beq	.L655
.L656:
	add	x25, x25, 1
	cmp	w22, w25, uxth
	bhi	.L679
	b	.L680
.L647:
	ldr	w0, [x0, x27]
	cmn	w0, #1
	bne	.L648
	sub	w28, w28, #1
	ands	w28, w28, 65535
	bne	.L650
.L649:
	mov	w1, 1
	b	.L721
.L653:
	mov	w0, 65535
	cmp	w1, w0
	bne	.L656
	mov	w1, 0
.L721:
	mov	w0, w20
	bl	FtlFreeSysBlkQueueIn
	b	.L656
.L652:
	ldrh	w1, [x19, 3696]
	ldr	w0, [x19, 332]
	cmp	w1, w0
	bls	.L658
	mov	x1, x24
	mov	x0, x26
	mov	w2, 1232
	bl	sftl_printk
.L658:
	ldr	w4, [x19, 332]
	ldrh	w3, [x19, 3696]
	and	w0, w4, 65535
	sub	w1, w0, #1
	sub	w0, w0, w3
	ldr	x5, [x19, 3608]
	sub	w0, w0, #1
	sxth	w2, w1
	sxth	x1, w1
	sxth	w0, w0
	add	x7, x5, 4
.L659:
	cmp	w0, w2
	bge	.L664
	sub	x6, x1, #1
	ldr	w10, [x21, 4]
	lsl	x8, x1, 2
	ldr	w9, [x7, x6, lsl 2]
	cmp	w10, w9
	bls	.L660
	ldr	w0, [x5]
	cbnz	w0, .L661
	cmp	w4, w3
	beq	.L661
	add	w3, w3, 1
	strh	w3, [x19, 3696]
.L661:
	mov	w0, 0
.L662:
	ldr	x3, [x19, 3608]
	cmp	w0, w2
	bne	.L663
	ldr	w0, [x21, 4]
	str	w0, [x3, x8]
	ldr	x0, [x19, 3560]
	strh	w20, [x0, x1, lsl 1]
.L664:
	tbnz	w2, #31, .L656
	ldrh	w1, [x19, 3696]
	ldr	w0, [x19, 332]
	sub	w0, w0, w1
	sub	w0, w0, #1
	cmp	w2, w0, sxth
	bgt	.L656
	ldr	x0, [x19, 3608]
	add	w1, w1, 1
	strh	w1, [x19, 3696]
	ldr	w1, [x21, 4]
	str	w1, [x0, w2, sxtw 2]
	ldr	x0, [x19, 3560]
.L720:
	strh	w20, [x0, w2, sxtw 1]
	b	.L656
.L663:
	add	w4, w0, 1
	ldr	w5, [x3, w4, sxtw 2]
	str	w5, [x3, w0, sxtw 2]
	ldr	x3, [x19, 3560]
	ldrh	w5, [x3, w4, sxtw 1]
	strh	w5, [x3, w0, sxtw 1]
	sxth	w0, w4
	b	.L662
.L660:
	sub	w2, w2, #1
	mov	x1, x6
	sxth	w2, w2
	b	.L659
.L654:
	ldrh	w1, [x19, 348]
	ldrh	w0, [x19, 324]
	cmp	w1, w0
	bls	.L668
	mov	x1, x24
	mov	x0, x26
	mov	w2, 1273
	bl	sftl_printk
.L668:
	ldrh	w3, [x19, 324]
	ldrh	w0, [x19, 348]
	sub	w1, w3, #1
	ldr	x4, [x19, 3584]
	sxth	w2, w1
	sub	w1, w1, w0
.L669:
	cmp	w2, w1
	ble	.L674
	sbfiz	x5, x2, 2, 32
	ldr	w8, [x21, 4]
	sxtw	x6, w2
	ldr	w7, [x4, x5]
	cmp	w8, w7
	bls	.L670
	ldr	w1, [x4]
	cbnz	w1, .L671
	cmp	w3, w0
	beq	.L671
	add	w0, w0, 1
	strh	w0, [x19, 348]
.L671:
	mov	w0, 0
.L672:
	ldr	x1, [x19, 3584]
	cmp	w0, w2
	bne	.L673
	ldr	w0, [x21, 4]
	str	w0, [x1, x5]
	ldr	x0, [x19, 352]
	strh	w20, [x0, x6, lsl 1]
.L674:
	tbnz	w2, #31, .L656
	ldrh	w0, [x19, 324]
	ldrh	w1, [x19, 348]
	sub	w0, w0, #1
	sub	w0, w0, w1
	cmp	w2, w0, sxth
	bgt	.L656
	ldr	x0, [x19, 3584]
	add	w1, w1, 1
	strh	w1, [x19, 348]
	ldr	w1, [x21, 4]
	str	w1, [x0, w2, sxtw 2]
	ldr	x0, [x19, 352]
	b	.L720
.L673:
	add	w3, w0, 1
	ldr	w4, [x1, w3, sxtw 2]
	str	w4, [x1, w0, sxtw 2]
	ldr	x1, [x19, 352]
	ldrh	w4, [x1, w3, sxtw 1]
	strh	w4, [x1, w0, sxtw 1]
	sxth	w0, w3
	b	.L672
.L670:
	sub	w2, w2, #1
	sxth	w2, w2
	b	.L669
.L655:
	ldrh	w3, [x19, 2696]
	add	x1, x19, 2560
	mov	w2, 65535
	cmp	w3, w2
	bne	.L676
	strh	w20, [x1, 136]
	str	w0, [x1, 144]
	b	.L656
.L676:
	ldrh	w0, [x1, 140]
	cmp	w0, w2
	beq	.L677
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L677:
	ldr	w1, [x21, 4]
	add	x0, x19, 2560
	ldr	w2, [x0, 144]
	cmp	w2, w1
	bcs	.L678
	ldrh	w1, [x0, 136]
	strh	w1, [x0, 140]
	strh	w20, [x0, 136]
	ldr	w1, [x21, 4]
	str	w1, [x0, 144]
	b	.L656
.L678:
	strh	w20, [x0, 140]
	b	.L656
.L681:
	ldr	x2, [x19, 3560]
	ldrh	w0, [x2]
	cbz	w0, .L683
.L686:
	ldr	x1, [x19, 352]
	ldrh	w0, [x1]
	cbz	w0, .L684
.L685:
	ldrh	w1, [x19, 3696]
	ldr	w0, [x19, 332]
	cmp	w1, w0
	bls	.L716
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 231
	add	x0, x0, :lo12:.LC9
	mov	w2, 1398
	bl	sftl_printk
.L716:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
.L683:
	ldrh	w0, [x19, 3696]
	cbz	w0, .L686
	ldr	w3, [x19, 332]
	mov	w0, 0
.L687:
	cmp	w0, w3
	bcs	.L686
	ldrh	w4, [x2, w0, sxtw 1]
	add	w1, w0, 1
	sxth	w1, w1
	cbz	w4, .L699
	mov	w1, w0
.L688:
	ldr	x2, [x19, 3560]
	sub	w3, w1, w0
	ldrh	w4, [x2, w1, sxtw 1]
	strh	w4, [x2, w3, sxtw 1]
	ldr	x2, [x19, 3608]
	ldr	w4, [x2, w1, sxtw 2]
	str	w4, [x2, w3, sxtw 2]
	ldr	x2, [x19, 3560]
	strh	wzr, [x2, w1, sxtw 1]
	add	w2, w1, 1
	ldr	w3, [x19, 332]
	sxth	w1, w2
	cmp	w3, w2, sxth
	bhi	.L688
	b	.L686
.L699:
	mov	w0, w1
	b	.L687
.L684:
	ldrh	w0, [x19, 348]
	cbz	w0, .L685
	ldrh	w2, [x19, 324]
	mov	w0, 0
.L692:
	mov	w3, w0
	cmp	w0, w2
	bge	.L685
	ldrh	w4, [x1, w0, sxtw 1]
	cbz	w4, .L717
.L693:
	ldrh	w1, [x19, 324]
	cmp	w0, w1
	bge	.L685
	ldr	x1, [x19, 352]
	sub	w2, w0, w3
	ldrh	w4, [x1, w0, sxtw 1]
	strh	w4, [x1, w2, sxtw 1]
	ldr	x1, [x19, 3584]
	ldr	w4, [x1, w0, sxtw 2]
	str	w4, [x1, w2, sxtw 2]
	ldr	x1, [x19, 352]
	strh	wzr, [x1, w0, sxtw 1]
	add	w0, w0, 1
	sxth	w0, w0
	b	.L693
.L717:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L692
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE75
	.text
.LPFE75:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x22, x19, 3712
	ldr	x0, [x19, 3448]
	stp	x23, x24, [sp, 48]
	mov	w23, 61649
	str	x25, [sp, 64]
	ldr	x21, [x19, 3512]
	stp	x0, x21, [x22, 8]
	bl	FtlBbtMemInit
	ldrh	w20, [x19, 302]
	sub	w20, w20, #1
	and	w20, w20, 65535
.L723:
	ldrh	w0, [x19, 302]
	sub	w0, w0, #16
	cmp	w20, w0
	ble	.L728
	lsl	w0, w20, 10
	mov	w2, 1
	mov	w1, w2
	str	w0, [x22, 4]
	mov	x0, x22
	bl	FlashReadPages
	ldr	w0, [x22]
	cmn	w0, #1
	beq	.L724
.L727:
	ldrh	w0, [x21]
	cmp	w0, w23
	bne	.L726
	ldr	w1, [x21, 4]
	str	w1, [x19, 368]
	strh	w20, [x19, 360]
	ldrh	w1, [x21, 8]
	strh	w1, [x19, 364]
.L728:
	ldrh	w1, [x19, 360]
	mov	w0, 65535
	cmp	w1, w0
	beq	.L742
	ldrh	w1, [x19, 364]
	cmp	w1, w0
	beq	.L732
	add	x0, x19, 3712
	lsl	w1, w1, 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x19, 3712]
	cmn	w0, #1
	beq	.L732
	ldrh	w1, [x21]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L732
	ldr	w1, [x19, 368]
	ldr	w0, [x21, 4]
	cmp	w0, w1
	bls	.L732
	str	w0, [x19, 368]
	ldrh	w1, [x19, 364]
	ldrh	w0, [x21, 8]
	strh	w1, [x19, 360]
	strh	w0, [x19, 364]
.L732:
	ldrh	w0, [x19, 360]
	mov	w1, 1
	add	x23, x19, 3712
	mov	w20, 0
	mov	w25, 61649
	bl	FtlGetLastWrittenPage
	sxth	w24, w0
	mov	w22, w0
	add	w0, w24, 1
	strh	w0, [x19, 362]
.L734:
	sub	w0, w22, w20
	tbz	x0, 15, .L737
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 245
	add	x0, x0, :lo12:.LC9
	mov	w2, 251
	bl	sftl_printk
.L736:
	ldrh	w0, [x21, 10]
	mov	w1, 65535
	strh	w0, [x19, 366]
	ldrh	w0, [x21, 12]
	cmp	w0, w1
	beq	.L739
	ldr	w2, [x19, 232]
	cmp	w0, w2
	beq	.L739
	ldrh	w1, [x19, 246]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L739
	cmp	w0, w1
	bcs	.L739
	bl	FtlSysBlkNumInit
.L739:
	add	x21, x19, 360
	mov	x20, 0
.L740:
	ldrh	w0, [x19, 258]
	cmp	w0, w20
	bhi	.L741
	mov	w0, 0
.L722:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
.L724:
	ldr	w0, [x22, 4]
	mov	w2, 1
	mov	w1, w2
	add	w0, w0, 1
	str	w0, [x22, 4]
	mov	x0, x22
	bl	FlashReadPages
	ldr	w0, [x22]
	cmn	w0, #1
	bne	.L727
.L726:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L723
.L737:
	ldrh	w0, [x19, 360]
	sub	w1, w24, w20
	mov	w2, 1
	orr	w0, w1, w0, lsl 10
	str	w0, [x23, 4]
	ldr	x0, [x19, 3448]
	mov	w1, w2
	str	x0, [x23, 8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L735
	ldrh	w0, [x21]
	cmp	w0, w25
	beq	.L736
.L735:
	add	w20, w20, 1
	b	.L734
.L741:
	ldrh	w2, [x19, 3624]
	add	x0, x21, x20, lsl 3
	ldr	x1, [x19, 3720]
	ldr	x0, [x0, 32]
	mul	w3, w2, w20
	lsl	w2, w2, 2
	add	x20, x20, 1
	add	x1, x1, x3, lsl 2
	bl	ftl_memcpy
	b	.L740
.L742:
	mov	w0, -1
	b	.L722
	.size	FtlLoadBbt, .-FtlLoadBbt
	.section	.rodata.str1.1
.LC96:
	.string	"prog read error: = %x\n"
.LC97:
	.string	"prog read REFRESH: = %x\n"
.LC98:
	.string	"prog read s error: = %x %x %x\n"
.LC99:
	.string	"prog read d error: = %x %x %x\n"
	.text
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE76
	.text
.LPFE76:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -160]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	x19, x0
	mov	w0, 32
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR1
	umaddl	x22, w1, w0, x19
	stp	x25, x26, [sp, 64]
	add	x23, x23, :lo12:.LANCHOR1
	mov	w25, w2
	stp	x27, x28, [sp, 80]
	mrs	x2, sp_el0
	adrp	x26, .LC9
	ldrh	w27, [x20, 12]
	ldr	x3, [x2, 1376]
	str	x3, [sp, 152]
	mov	x3, 0
	mov	x21, x19
	add	x23, x23, 256
	add	x26, x26, :lo12:.LC9
	lsl	w28, w27, 3
.L755:
	cmp	x21, x22
	bne	.L769
	adrp	x21, .LC96
	adrp	x23, .LC97
	add	x21, x21, :lo12:.LC96
	add	x23, x23, :lo12:.LC97
.L770:
	cmp	x19, x22
	beq	.L763
	ldr	x0, [x20, 3488]
	mov	w2, w25
	mov	w1, 1
	str	wzr, [x0]
	ldr	x0, [x20, 3504]
	str	wzr, [x0]
	ldr	w0, [x19, 4]
	str	w0, [sp, 124]
	ldr	x0, [x20, 3488]
	str	x0, [sp, 128]
	ldr	x0, [x20, 3504]
	str	x0, [sp, 136]
	add	x0, sp, 120
	bl	FlashReadPages
	ldr	w24, [sp, 120]
	cmn	w24, #1
	bne	.L771
	ldr	w1, [x19, 4]
	mov	x0, x21
	bl	sftl_printk
	str	w24, [x19]
.L771:
	ldr	w24, [sp, 120]
	cmp	w24, 256
	bne	.L772
	ldr	w1, [x19, 4]
	mov	x0, x23
	bl	sftl_printk
	str	w24, [x19]
.L772:
	ldr	x0, [x19, 16]
	cbz	x0, .L773
	ldr	w2, [x0]
	ldr	x0, [x20, 3504]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L773
	ldr	w1, [x19, 4]
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x19]
.L773:
	ldr	x0, [x19, 8]
	cbz	x0, .L774
	ldr	w2, [x0]
	ldr	x0, [x20, 3488]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L774
	ldr	w1, [x19, 4]
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x19]
.L774:
	add	x19, x19, 32
	b	.L770
.L769:
	ldr	x0, [x21, 8]
	cbz	x0, .L756
	ldr	x0, [x21, 16]
	cbnz	x0, .L757
.L756:
	mov	x1, x23
	mov	x0, x26
	mov	w2, 148
	bl	sftl_printk
.L757:
	ldr	w0, [x21, 4]
	add	x2, sp, 112
	add	x1, sp, 116
	bl	l2p_addr_tran.isra.0
	ldr	w0, [sp, 112]
	cmp	w0, 3
	bls	.L758
	mov	w0, -1
	str	w0, [x21]
.L759:
	add	x21, x21, 32
	b	.L755
.L758:
	cbnz	w0, .L760
	ldr	w0, [sp, 116]
	cmp	w28, w0
	bls	.L760
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x21, .LC87
	add	x20, x20, 256
	add	x21, x21, :lo12:.LC87
.L761:
	cmp	x19, x22
	bne	.L762
	bl	dump_stack
.L763:
	mrs	x0, sp_el0
	ldr	x1, [sp, 152]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L776
	bl	__stack_chk_fail
.L762:
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
	adrp	x0, .LC88
	add	x0, x0, :lo12:.LC88
	bl	rknand_print_hex
	ldr	x1, [x19, -16]
	mov	w3, 4
	adrp	x0, .LC89
	mov	w2, w3
	add	x0, x0, :lo12:.LC89
	bl	rknand_print_hex
	b	.L761
.L760:
	ldr	x1, [x21, 8]
	tst	x1, 63
	beq	.L777
	ldr	x24, [x20, 3488]
	cmp	x24, x1
	beq	.L764
	ldrh	w2, [x20, 262]
	mov	x0, x24
	lsl	w2, w2, 9
	bl	ftl_memcpy
.L764:
	ldrb	w0, [sp, 112]
	add	x5, x20, 3352
	ldr	w1, [sp, 116]
	mov	x2, x24
	ldr	x3, [x21, 16]
	str	x5, [sp, 104]
	ldr	x4, [x20, 3368]
	blr	x4
	cmp	w0, 0
	mov	w4, -1
	csel	w0, wzr, w4, eq
	str	w0, [x21]
	ldr	x5, [sp, 104]
	ldrh	w0, [x20, 14]
	cmp	w0, 4
	bne	.L759
	ldrb	w0, [sp, 112]
	add	x2, x24, 2048
	ldr	x5, [x5, 16]
	ldr	x3, [x21, 16]
	ldr	w1, [sp, 116]
	add	x3, x3, 8
	add	w1, w27, w1
	blr	x5
	cbz	w0, .L759
	mov	w4, -1
	str	w4, [x21]
	b	.L759
.L777:
	mov	x24, x1
	b	.L764
.L776:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	hint	29 // autiasp
	ret
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE77
	.text
.LPFE77:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -112]!
	adrp	x4, .LANCHOR0
	mov	x29, sp
	stp	x27, x28, [sp, 80]
	add	x28, x4, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	and	w21, w1, 255
	and	w22, w0, 65535
	stp	x23, x24, [sp, 48]
	add	x24, x28, 264
	mov	w23, 0
	stp	x19, x20, [sp, 16]
	mov	w20, 0
	mov	w19, 0
	stp	x25, x26, [sp, 64]
	mov	w25, 4
	str	w22, [x28, 3420]
.L793:
	ldrh	w0, [x28, 236]
	cmp	w0, w23
	bhi	.L797
	cbz	w20, .L792
	ldr	x0, [x28, 3400]
	mov	w2, w20
	mov	w1, 0
	mov	x23, 0
	bl	FlashEraseBlocks
.L801:
	ldr	x1, [x28, 3400]
	lsl	x0, x23, 5
	ldr	w0, [x1, x0]
	add	x2, x1, x23, lsl 5
	cmn	w0, #1
	bne	.L800
	ldr	w0, [x2, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	ubfx	x0, x0, 10, 16
	bl	FtlBbmMapBadBlock
.L800:
	add	x23, x23, 1
	cmp	w20, w23, uxth
	bhi	.L801
	cbz	w21, .L814
	ldrh	w26, [x28, 308]
	mov	w24, 1
.L802:
	add	x0, x28, 264
	mov	w23, 0
	str	x0, [sp, 96]
.L810:
	mov	w27, 0
	mov	w20, 0
.L803:
	ldrh	w0, [x28, 236]
	cmp	w0, w27
	bhi	.L806
	cbz	w20, .L792
	ldr	x0, [x28, 3400]
	mov	w2, w24
	mov	w1, w20
	mov	w3, 1
	mov	x25, 0
	bl	FlashProgPages
.L809:
	ldr	x1, [x28, 3400]
	lsl	x0, x25, 5
	ldr	w0, [x1, x0]
	add	x2, x1, x25, lsl 5
	cbz	w0, .L808
	ldr	w0, [x2, 4]
	add	w19, w19, 1
	and	w19, w19, 65535
	ubfx	x0, x0, 10, 16
	bl	FtlBbmMapBadBlock
.L808:
	add	x25, x25, 1
	cmp	w20, w25, uxth
	bhi	.L809
	add	w23, w23, 1
	cmp	w26, w23, uxth
	bhi	.L810
	mov	x23, 0
.L812:
	cbz	w21, .L811
	ldr	x1, [x28, 3400]
	lsl	x0, x23, 5
	ldr	w0, [x1, x0]
	add	x2, x1, x23, lsl 5
	cbnz	w0, .L811
	ldr	w0, [x2, 4]
	mov	w1, 1
	ubfx	x0, x0, 10, 16
	bl	FtlFreeSysBlkQueueIn
.L811:
	add	x23, x23, 1
	cmp	w20, w23, uxth
	bhi	.L812
	cmp	w22, 63
	ccmp	w21, 0, 0, hi
	beq	.L792
	ldr	x0, [x28, 3400]
	mov	w2, w20
	mov	w1, w24
	bl	FlashEraseBlocks
.L792:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	hint	29 // autiasp
	ret
.L797:
	ldr	x1, [x28, 3400]
	ubfiz	x0, x23, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w22
	ldrb	w0, [x24, w23, sxtw]
	bl	V2P_block
	and	w27, w0, 65535
	mov	w26, w27
	cbz	w21, .L794
	bl	IsBlkInVendorPart
	cbnz	w0, .L795
.L794:
	mov	w0, w26
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L796
	ldr	x0, [x28, 3400]
	ubfiz	x2, x20, 5, 16
	lsl	w27, w27, 10
	add	x0, x0, x2
	str	w27, [x0, 4]
	ldr	x1, [x28, 3400]
	ldr	x0, [x28, 3480]
	add	x1, x1, x2
	ldr	x2, [x28, 3520]
	str	x0, [x1, 8]
	ldrh	w0, [x28, 316]
	mul	w0, w0, w20
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w0, w0, w25
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L795:
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L793
.L796:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L795
.L814:
	mov	w24, 0
	mov	w26, 2
	b	.L802
.L806:
	ldr	x1, [x28, 3400]
	ubfiz	x0, x27, 5, 16
	str	wzr, [x1, x0]
	mov	w1, w22
	ldr	x0, [sp, 96]
	ldrb	w0, [x0, w27, sxtw]
	bl	V2P_block
	and	w25, w0, 65535
	mov	w1, w25
	cbz	w21, .L804
	str	w25, [sp, 108]
	bl	IsBlkInVendorPart
	ldr	w1, [sp, 108]
	cbnz	w0, .L805
.L804:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L805
	ldr	x0, [x28, 3400]
	ubfiz	x3, x20, 5, 16
	add	w25, w23, w25, lsl 10
	mov	w2, 4
	add	x0, x0, x3
	str	w25, [x0, 4]
	ldr	x1, [x28, 3400]
	ldr	x0, [x28, 3472]
	add	x1, x1, x3
	ldr	x3, [x28, 3480]
	str	x0, [x1, 8]
	ldrh	w0, [x28, 316]
	mul	w0, w0, w20
	add	w20, w20, 1
	and	w20, w20, 65535
	sdiv	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	str	x0, [x1, 16]
.L805:
	add	w2, w27, 1
	and	w27, w2, 65535
	b	.L803
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE78
	.text
.LPFE78:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -144]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	and	w19, w0, 65535
	mrs	x0, sp_el0
	ldr	x1, [x0, 1376]
	str	x1, [sp, 136]
	mov	x1, 0
	cmp	w19, 11
	bls	.L839
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, 32
	mov	w1, 165
	lsl	w19, w19, 10
	ldr	x0, [x20, 3496]
	str	x0, [sp, 48]
	add	x0, sp, 72
	str	x0, [sp, 56]
	bl	ftl_memset
	ldr	x0, [x20, 3496]
	mov	w2, 8
	mov	w1, 90
	bl	ftl_memset
	add	x0, sp, 40
	mov	w2, 1
	mov	w1, w2
	str	w19, [sp, 44]
	bl	FlashEraseBlocks
	mov	w3, 1
	add	x0, sp, 40
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldr	w0, [sp, 40]
	cbnz	w0, .L840
	add	w0, w19, 1
	mov	w3, 1
	mov	w2, w3
	mov	w1, w3
	str	w0, [sp, 44]
	add	x0, sp, 40
	bl	FlashProgPages
	ldr	w0, [sp, 40]
	cmp	w0, 0
	csetm	w20, ne
.L837:
	add	x0, sp, 40
	mov	w2, 1
	mov	w1, 0
	str	w19, [sp, 44]
	bl	FlashEraseBlocks
.L835:
	mrs	x0, sp_el0
	ldr	x1, [sp, 136]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L838
	bl	__stack_chk_fail
.L840:
	mov	w20, -1
	b	.L837
.L839:
	mov	w20, 0
	b	.L835
.L838:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 144
	hint	29 // autiasp
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.section	.rodata.str1.1
.LC100:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC101:
	.string	"FtlBbmTblFlush error:%x\n"
.LC102:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
	.text
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE79
	.text
.LPFE79:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mov	w1, 0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	mov	x20, 0
	stp	x21, x22, [sp, 32]
	add	x22, x19, 360
	ldr	x2, [x19, 3512]
	str	x2, [x19, 3728]
	ldrh	w2, [x19, 314]
	ldr	x0, [x19, 3448]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x0, [x19, 3720]
	bl	ftl_memset
.L843:
	ldrh	w0, [x19, 258]
	add	x1, x19, 3712
	cmp	w0, w20
	bgt	.L844
	ldr	x21, [x1, 16]
	mov	w2, 16
	mov	w1, 255
	adrp	x25, .LC100
	mov	x0, x21
	bl	ftl_memset
	mov	w0, -3887
	strh	w0, [x21]
	adrp	x26, .LC101
	add	x20, x19, 360
	ldr	w0, [x22, 8]
	add	x25, x25, :lo12:.LC100
	str	w0, [x21, 4]
	add	x26, x26, :lo12:.LC101
	ldrh	w0, [x19, 360]
	mov	w24, 0
	strh	w0, [x21, 2]
	ldr	w0, [x22, 4]
	mov	w22, 0
	str	w0, [x21, 8]
	ldr	w0, [x19, 232]
	strh	w0, [x21, 12]
	ldr	w0, [x19, 2676]
	strh	w0, [x21, 14]
.L845:
	ldr	x0, [x19, 3448]
	str	x0, [x19, 3720]
	ldr	x0, [x19, 3512]
	str	x0, [x19, 3728]
	ldrh	w1, [x20]
	ldrh	w2, [x20, 2]
	ldrh	w3, [x20, 4]
	ldrh	w4, [x21, 10]
	orr	w0, w2, w1, lsl 10
	str	wzr, [x19, 3712]
	str	w0, [x19, 3716]
	mov	x0, x25
	bl	sftl_printk
	ldrh	w0, [x19, 308]
	ldrh	w1, [x20, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L846
	ldr	w0, [x20, 8]
	mov	w2, 1
	strh	wzr, [x20, 2]
	add	w0, w0, 1
	str	w0, [x20, 8]
	str	w0, [x21, 4]
	ldrh	w0, [x20]
	ldrh	w1, [x20, 4]
	strh	w0, [x20, 4]
	strh	w0, [x21, 8]
	ldr	x0, [x19, 3400]
	strh	w1, [x20]
	lsl	w1, w1, 10
	str	w1, [x19, 3716]
	str	w1, [x0, 4]
	mov	w1, w2
	ldr	x0, [x19, 3400]
	bl	FlashEraseBlocks
.L846:
	add	x23, x19, 3712
	mov	w3, 1
	mov	x0, x23
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
	ldrh	w0, [x20, 2]
	add	w0, w0, 1
	strh	w0, [x20, 2]
	ldr	w0, [x19, 3712]
	cmn	w0, #1
	bne	.L847
	ldr	w1, [x19, 3716]
	add	w22, w22, 1
	and	w22, w22, 65535
	mov	x0, x26
	bl	sftl_printk
	cmp	w22, 3
	bls	.L845
	ldr	w1, [x19, 3716]
	adrp	x0, .LC102
	mov	w2, w22
	add	x0, x0, :lo12:.LC102
	bl	sftl_printk
.L849:
	b	.L849
.L844:
	ldrh	w2, [x19, 3624]
	add	x4, x22, x20, lsl 3
	ldr	x0, [x1, 8]
	ldr	x1, [x4, 32]
	mul	w3, w2, w20
	lsl	w2, w2, 2
	add	x20, x20, 1
	add	x0, x0, x3, sxtw 2
	bl	ftl_memcpy
	b	.L843
.L847:
	add	w24, w24, 1
	cmp	w24, 1
	ble	.L845
	cmp	w0, 256
	beq	.L845
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE80
	.text
.LPFE80:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -112]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	mov	x20, x0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR1
	stp	x25, x26, [sp, 64]
	add	x25, x19, 128
	stp	x21, x22, [sp, 32]
	stp	x27, x28, [sp, 80]
.L855:
	ldrh	w1, [x19, 2584]
	ldrh	w0, [x19, 228]
	add	w0, w0, w1
	ldrh	w1, [x19, 244]
	cmp	w0, w1
	ble	.L856
	adrp	x0, .LC9
	add	x1, x23, 271
	add	x0, x0, :lo12:.LC9
	mov	w2, 2667
	bl	sftl_printk
.L856:
	cmp	x20, x25
	bne	.L881
	ldrh	w1, [x19, 228]
	mov	w2, 65535
	ldr	w0, [x19, 3236]
	mul	w0, w1, w0
	lsr	w1, w1, 1
	add	w0, w1, w0, lsr 2
	and	w1, w0, 65535
	cmp	w2, w0, uxth
	csel	w1, w1, wzr, ne
.L857:
	add	x0, x19, 2592
	bl	List_pop_index_node
	and	w22, w0, 65535
	ldrh	w0, [x19, 228]
	cbnz	w0, .L858
	adrp	x0, .LC9
	add	x1, x23, 271
	add	x0, x0, :lo12:.LC9
	mov	w2, 2676
	bl	sftl_printk
.L858:
	ldrh	w0, [x19, 228]
	sub	w0, w0, #1
	strh	w0, [x19, 228]
	ldrh	w0, [x19, 244]
	cmp	w0, w22
	bls	.L855
	ldr	x0, [x19, 72]
	ubfiz	x24, x22, 1, 16
	ldrh	w21, [x0, x24]
	cbnz	w21, .L855
	strh	w22, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20, 7]
	cbnz	w0, .L860
	ldr	x0, [x19, 72]
	mov	w1, -1
	strh	w1, [x0, x24]
	mov	w0, w22
	bl	INSERT_DATA_LIST
	ldrh	w1, [x19, 2584]
	ldrh	w0, [x19, 228]
	add	w0, w0, w1
	ldrh	w1, [x19, 244]
	cmp	w0, w1
	ble	.L855
	add	x1, x23, 271
	mov	w2, 2690
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	bl	sftl_printk
	b	.L855
.L881:
	mov	w1, 0
	b	.L857
.L860:
	ldrh	w1, [x19, 2584]
	ldrh	w0, [x19, 228]
	add	w0, w0, w1
	ldrh	w1, [x19, 244]
	cmp	w0, w1
	ble	.L862
	adrp	x0, .LC9
	add	x1, x23, 271
	add	x0, x0, :lo12:.LC9
	mov	w2, 2693
	bl	sftl_printk
.L862:
	ldrh	w3, [x19, 236]
	add	x4, x20, 16
	mov	x0, 0
	mov	w5, 65535
.L863:
	cmp	w3, w0, uxth
	bhi	.L865
	cbnz	w21, .L866
	adrp	x0, .LC9
	add	x1, x23, 271
	add	x0, x0, :lo12:.LC9
	mov	w2, 2704
	bl	sftl_printk
.L866:
	ldrh	w0, [x19, 176]
	cmp	w0, w22
	bne	.L867
	adrp	x0, .LC9
	add	x1, x23, 271
	add	x0, x0, :lo12:.LC9
	mov	w2, 2706
	bl	sftl_printk
.L867:
	ldrb	w2, [x20, 8]
	ldr	x0, [x19, 2600]
	ldrh	w1, [x0, x24]
	cbnz	w2, .L868
	cbz	w1, .L882
	ldrh	w2, [x19, 296]
	add	w1, w1, w2
	and	w1, w1, 65535
.L869:
	strh	w1, [x0, x24]
	ldr	w0, [x19, 2668]
	add	w0, w0, 1
	str	w0, [x19, 2668]
.L870:
	ldr	x0, [x19, 2600]
	ldr	w1, [x19, 2684]
	ldrh	w0, [x0, x24]
	cmp	w0, w1
	bls	.L871
	str	w0, [x19, 2684]
.L871:
	ldr	w1, [x19, 2672]
	ldrh	w0, [x19, 296]
	ldr	w2, [x19, 2668]
	madd	w0, w0, w2, w1
	ldrh	w1, [x19, 244]
	udiv	w0, w0, w1
	ldr	x1, [x19, 3544]
	str	w0, [x19, 2676]
	ldr	w0, [x1, 16]
	add	w0, w0, 1
	str	w0, [x1, 16]
	mov	x1, 0
.L872:
	ldr	x0, [x19, 3400]
	cmp	w21, w1, uxth
	bhi	.L873
	ldrb	w1, [x20, 8]
	mov	w2, w21
	mov	x26, 0
	mov	w27, 0
	bl	FlashEraseBlocks
	add	x2, x20, 16
.L874:
	cmp	w21, w26, uxth
	bhi	.L876
	cmp	w27, 0
	ble	.L877
	mov	w0, w22
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L877:
	ldrb	w0, [x20, 7]
	cbnz	w0, .L878
	ldr	x0, [x19, 72]
	mov	w1, -1
	strh	w1, [x0, x24]
	b	.L855
.L865:
	ldr	x1, [x19, 3400]
	add	x2, x1, x0, lsl 5
	stp	xzr, xzr, [x2, 8]
	ldrh	w2, [x4, x0, lsl 1]
	cmp	w2, w5
	beq	.L864
	ubfiz	x6, x21, 5, 16
	add	w21, w21, 1
	add	x1, x1, x6
	and	w21, w21, 65535
	lsl	w2, w2, 10
	str	w2, [x1, 4]
.L864:
	add	x0, x0, 1
	b	.L863
.L882:
	mov	w1, 2
	b	.L869
.L868:
	add	w1, w1, 1
	strh	w1, [x0, x24]
	ldr	w0, [x19, 2672]
	add	w0, w0, 1
	str	w0, [x19, 2672]
	b	.L870
.L873:
	add	x0, x0, x1, lsl 5
	add	x1, x1, 1
	ldr	w2, [x0, 4]
	and	w2, w2, -1024
	str	w2, [x0, 4]
	b	.L872
.L876:
	ldr	x1, [x19, 3400]
	lsl	x0, x26, 5
	ldr	w28, [x1, x0]
	add	x3, x1, x26, lsl 5
	cmn	w28, #1
	bne	.L875
	ldr	w0, [x3, 4]
	add	w27, w27, 1
	str	x2, [sp, 104]
	ubfx	x0, x0, 10, 16
	bl	FtlBbmMapBadBlock
	ldr	x2, [sp, 104]
	strh	w28, [x2, x26, lsl 1]
	ldrb	w0, [x20, 7]
	sub	w0, w0, #1
	strb	w0, [x20, 7]
.L875:
	add	x26, x26, 1
	b	.L874
.L878:
	ldrh	w1, [x19, 306]
	strh	w22, [x20]
	strh	wzr, [x20, 2]
	strb	wzr, [x20, 6]
	mul	w0, w0, w1
	ldr	w1, [x19, 2660]
	str	w1, [x20, 12]
	and	w0, w0, 65535
	add	w1, w1, 1
	strh	w0, [x20, 4]
	str	w1, [x19, 2660]
	ldr	x1, [x19, 72]
	ldrh	w2, [x20]
	strh	w0, [x1, x2, lsl 1]
	ldrh	w0, [x20, 4]
	cbz	w0, .L879
	ldrb	w0, [x20, 7]
	cbnz	w0, .L880
.L879:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 271
	add	x0, x0, :lo12:.LC9
	mov	w2, 2759
	bl	sftl_printk
.L880:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	hint	29 // autiasp
	ret
	.size	allocate_data_superblock, .-allocate_data_superblock
	.section	.rodata.str1.1
.LC103:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
	.text
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE81
	.text
.LPFE81:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	and	w25, w0, 65535
	ldrh	w0, [x19, 3312]
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x27, [sp, 80]
	cbnz	w0, .L894
.L889:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
.L893:
	add	x0, x19, 264
	mov	w1, w25
	mov	w20, 0
	ldrb	w0, [x0, w21, sxtw]
	bl	V2P_block
	and	w22, w0, 65535
.L890:
	ldrh	w0, [x19, 3312]
	cmp	w0, w20
	bhi	.L892
	add	w21, w21, 1
	and	w21, w21, 65535
.L888:
	ldrh	w0, [x19, 236]
	cmp	w0, w21
	bhi	.L893
	bl	FtlGcReFreshBadBlk
	b	.L889
.L892:
	ldrh	w0, [x24, w20, sxtw 1]
	add	w27, w20, 1
	cmp	w0, w22
	bne	.L891
	mov	w1, w22
	mov	x0, x26
	bl	sftl_printk
	mov	w0, w22
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w23, [x19, 3312]
	add	x1, x24, x27, sxtw 1
	add	x0, x24, x20, uxth 1
	cmp	w20, w23
	sub	w2, w23, w20
	sub	w23, w23, #1
	ubfiz	x2, x2, 1, 16
	csel	x2, x2, xzr, ls
	bl	memmove
	strh	w23, [x19, 3312]
.L891:
	and	w20, w27, 65535
	b	.L890
.L894:
	adrp	x26, .LC103
	add	x24, x19, 3314
	add	x26, x26, :lo12:.LC103
	mov	w21, 0
	b	.L888
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE82
	.text
.LPFE82:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	and	w20, w0, 65535
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x19, 72]
	ldrh	w0, [x1, x0]
	cbnz	w0, .L897
	ldrh	w0, [x19, 226]
	cmp	w0, w20
	bne	.L898
	mov	w0, -1
	strh	w0, [x19, 226]
.L898:
	ldrh	w0, [x19, 224]
	cmp	w0, w20
	bne	.L899
	mov	w0, -1
	strh	w0, [x19, 224]
.L899:
	ldrh	w0, [x19, 176]
	cmp	w0, w20
	bne	.L900
	mov	w0, -1
	strh	w0, [x19, 176]
.L901:
	mov	w1, w20
	add	x0, x19, 2568
	bl	List_remove_node
	ldrh	w0, [x19, 2584]
	cbnz	w0, .L903
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 296
	add	x0, x0, :lo12:.LC9
	mov	w2, 2835
	bl	sftl_printk
.L903:
	ldrh	w0, [x19, 2584]
	sub	w0, w0, #1
	strh	w0, [x19, 2584]
	mov	w0, w20
	bl	free_data_superblock
	mov	w0, w20
	bl	FtlGcFreeBadSuperBlk
	ldrh	w1, [x19, 2584]
	ldrh	w0, [x19, 228]
	add	w0, w0, w1
	ldrh	w1, [x19, 244]
	cmp	w0, w1
	ble	.L907
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 296
	add	x0, x0, :lo12:.LC9
	mov	w2, 2838
	bl	sftl_printk
.L907:
	mov	w0, 1
	b	.L896
.L900:
	ldrh	w0, [x19, 24]
	cmp	w0, w20
	beq	.L906
	ldrh	w0, [x19, 80]
	cmp	w0, w20
	beq	.L906
	ldrh	w0, [x19, 128]
	cmp	w0, w20
	bne	.L901
.L906:
	mov	w0, 0
.L896:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
.L897:
	mov	w0, w20
	bl	List_update_data_list
	b	.L906
	.size	update_vpc_list, .-update_vpc_list
	.section	.rodata.str1.1
.LC104:
	.string	"decrement_vpc_count %x = %d\n"
	.text
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE83
	.text
.LPFE83:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	w1, 65535
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	cmp	w1, w0, uxth
	adrp	x19, .LANCHOR0
	str	x21, [sp, 32]
	and	w20, w0, 65535
	add	x19, x19, :lo12:.LANCHOR0
	beq	.L910
	ldr	x1, [x19, 72]
	ubfiz	x21, x20, 1, 16
	ldrh	w0, [x1, x21]
	cbnz	w0, .L911
	mov	w1, w20
	mov	w2, 0
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	bl	sftl_printk
	ldr	x0, [x19, 72]
	ldrh	w0, [x0, x21]
	cbz	w0, .L912
.L917:
	mov	w21, 0
.L909:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L912:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 312
	mov	w2, 2853
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	bl	sftl_printk
	b	.L917
.L911:
	sub	w0, w0, #1
	strh	w0, [x1, x21]
.L910:
	ldrh	w0, [x19, 3698]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L914
	strh	w20, [x19, 3698]
	b	.L917
.L914:
	cmp	w0, w20
	beq	.L917
	bl	update_vpc_list
	cmp	w0, 0
	ldr	x1, [x19, 2560]
	strh	w20, [x19, 3698]
	ldr	x0, [x19, 2568]
	cset	w21, ne
	sub	x0, x0, x1
	mov	x1, -6148914691236517206
	movk	x1, 0xaaab, lsl 0
	asr	x0, x0, 1
	mul	x0, x0, x1
	ldr	x1, [x19, 72]
	and	x2, x0, 65535
	ldrh	w1, [x1, x2, lsl 1]
	cbnz	w1, .L909
	cmp	w20, w0, uxth
	beq	.L909
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 312
	mov	w2, 2869
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	bl	sftl_printk
	b	.L909
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE84
	.text
.LPFE84:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	x19, x0
	mrs	x0, sp_el0
	ldr	x1, [x0, 1376]
	str	x1, [sp, 88]
	mov	x1, 0
	mov	w0, -1
	ldr	x22, [x20, 3512]
	str	w0, [sp, 80]
	ldr	x0, [x20, 3448]
	str	x0, [sp, 64]
	mov	x0, -3
	str	x22, [sp, 72]
	movk	x0, 0xfffe, lsl 32
	str	x0, [x22, 8]
	mov	w1, 61589
	ldrh	w0, [x19]
	strh	wzr, [x22]
	strh	w0, [x22, 2]
	ldr	x0, [x20, 3448]
	str	w1, [x0]
	mov	w1, 22136
	movk	w1, 0x1234, lsl 16
	ldr	x0, [x20, 3448]
	str	w1, [x0, 4]
	ldrh	w21, [x19, 4]
	and	w21, w21, 1
	add	w21, w21, 6
.L919:
	ldrh	w0, [x19, 4]
	cbz	w21, .L921
	cbnz	w0, .L920
.L921:
	ldrh	w0, [x19]
	ldr	x2, [x20, 72]
	lsl	x0, x0, 1
	ldrh	w3, [x19, 4]
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strh	wzr, [x19, 4]
	ldrh	w0, [x20, 306]
	strh	w0, [x19, 2]
	mrs	x0, sp_el0
	strb	wzr, [x19, 6]
	ldr	x1, [sp, 88]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L925
	bl	__stack_chk_fail
.L920:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [sp, 60]
	cmn	w0, #1
	beq	.L921
	ldr	w0, [x20, 2664]
	mov	w3, 0
	str	w0, [x22, 4]
	mov	w2, 0
	cmn	w0, #2
	mov	w1, 1
	csinc	w0, wzr, w0, eq
	str	w0, [x20, 2664]
	add	x0, sp, 56
	sub	w21, w21, #1
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L919
.L925:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE85
	.text
.LPFE85:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x24, x19, 360
	add	x20, x19, 3712
	stp	x25, x26, [sp, 64]
	mov	x25, x24
	mov	w23, 0
	stp	x27, x28, [sp, 80]
	mov	w27, 65535
	mov	w28, -3872
	stp	x21, x22, [sp, 32]
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L932:
	ldrh	w0, [x19, 258]
	cmp	w0, w23
	bhi	.L938
	mov	w20, 0
.L939:
	ldrh	w0, [x19, 318]
	cmp	w0, w20
	bhi	.L940
	ldrh	w20, [x19, 372]
	add	x21, x19, 360
	mov	w22, 65535
	sub	w20, w20, #1
	and	w20, w20, 65535
.L941:
	ldrh	w0, [x21, 12]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L945
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L942
	mov	w0, w20
	bl	FlashTestBlk
	cbz	w0, .L943
	mov	w0, w20
	bl	FtlBbmMapBadBlock
.L942:
	sub	w20, w20, #1
	and	w20, w20, 65535
	b	.L941
.L938:
	ldrh	w1, [x25, 12]
	ldr	x0, [x19, 3448]
	ldr	x26, [x19, 3512]
	stp	x0, x26, [x20, 8]
	cmp	w1, w27
	beq	.L933
	ldrh	w22, [x19, 302]
	mov	w2, 1
	madd	w22, w22, w23, w1
	mov	w1, w2
	lsl	w0, w22, 10
	str	w0, [x20, 4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x19, 302]
	ldr	x1, [x20, 8]
	add	w2, w2, 7
	ldr	x0, [x24, 32]
	lsr	w2, w2, 3
	bl	ftl_memcpy
.L934:
	mov	w0, w22
	add	w23, w23, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L932
.L933:
	mov	w1, w23
	bl	FlashGetBadBlockList
	ldr	x0, [x20, 8]
	ldr	x1, [x24, 32]
	bl	FtlBbt2Bitmap
	ldrh	w21, [x19, 302]
.L936:
	sub	w21, w21, #1
	and	w21, w21, 65535
.L935:
	ldrh	w0, [x19, 302]
	madd	w0, w23, w0, w21
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L936
	ldr	x0, [x19, 3512]
	strh	w21, [x25, 12]
	mov	w2, 16
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x19, 3448]
	mov	w2, 4096
	mov	w1, 0
	bl	ftl_memset
	strh	w28, [x26]
	str	wzr, [x26, 4]
	ldrh	w22, [x19, 302]
	ldrh	w0, [x25, 12]
	strh	w0, [x26, 2]
	ldrh	w2, [x19, 3624]
	ldr	x1, [x24, 32]
	madd	w22, w22, w23, w0
	lsl	w2, w2, 2
	lsl	w0, w22, 10
	str	w0, [x20, 4]
	ldr	x0, [x20, 8]
	bl	ftl_memcpy
	mov	w2, 1
	mov	x0, x20
	mov	w1, w2
	bl	FlashEraseBlocks
	mov	w3, 1
	mov	x0, x20
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldr	w1, [x20]
	and	w0, w22, 65535
	cmn	w1, #1
	bne	.L934
	bl	FtlBbmMapBadBlock
	b	.L935
.L940:
	mov	w0, w20
	add	w20, w20, 1
	and	w20, w20, 65535
	bl	FtlBbmMapBadBlock
	b	.L939
.L943:
	ldrh	w0, [x21]
	cmp	w0, w22
	bne	.L944
	strh	w20, [x21]
	b	.L942
.L944:
	strh	w20, [x21, 4]
.L945:
	ldr	x1, [x19, 3400]
	str	wzr, [x19, 368]
	ldrh	w0, [x19, 360]
	mov	w2, 2
	strh	wzr, [x19, 362]
	lsl	w0, w0, 10
	str	w0, [x1, 4]
	ldr	x0, [x19, 3400]
	ldrh	w1, [x19, 364]
	lsl	w1, w1, 10
	str	w1, [x0, 36]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x19, 360]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19, 364]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19, 362]
	ldr	w0, [x19, 368]
	ldrh	w1, [x19, 364]
	add	w0, w0, 1
	str	w0, [x19, 368]
	ldrh	w0, [x19, 360]
	strh	w1, [x19, 360]
	strh	w0, [x19, 364]
	bl	FtlBbmTblFlush
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	ftl_memcmp
	.type	ftl_memcmp, %function
ftl_memcmp:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE86
	.text
.LPFE86:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	uxtw	x2, w2
	mov	x29, sp
	bl	memcmp
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	ftl_memcmp, .-ftl_memcmp
	.align	2
	.global	js_hash
	.type	js_hash, %function
js_hash:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE87
	.text
.LPFE87:
	nop
	nop
	mov	x4, x0
	mov	w0, 42982
	mov	x3, 0
	movk	w0, 0x47c6, lsl 16
	hint	25 // paciasp
.L954:
	cmp	w1, w3
	bhi	.L955
	hint	29 // autiasp
	ret
.L955:
	ldrb	w5, [x4, x3]
	lsr	w2, w0, 2
	add	w2, w2, w0, lsl 5
	add	x3, x3, 1
	add	w2, w2, w5
	eor	w0, w0, w2
	b	.L954
	.size	js_hash, .-js_hash
	.section	.rodata.str1.1
.LC105:
	.string	"FtlMapWritePage error = %x \n"
.LC106:
	.string	"FtlMapWritePage error = %x error count = %d\n"
	.text
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE88
	.text
.LPFE88:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mov	x29, sp
	stp	x23, x24, [sp, 48]
	mov	w24, w1
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	add	x21, x20, 3712
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR1
	mov	x26, x2
	add	x25, x25, :lo12:.LANCHOR1
	stp	x27, x28, [sp, 80]
	ubfiz	x27, x24, 2, 32
	mov	w22, 0
.L958:
	ldr	w0, [x20, 2644]
	add	w0, w0, 1
	str	w0, [x20, 2644]
	ldrh	w0, [x20, 308]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L959
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L960
.L959:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L960:
	ldrh	w1, [x19]
	ldr	x0, [x19, 16]
	ldrh	w0, [x0, x1, lsl 1]
	cbnz	w0, .L961
	adrp	x0, .LC9
	add	x1, x25, 332
	add	x0, x0, :lo12:.LC9
	mov	w2, 700
	bl	sftl_printk
.L961:
	ldrh	w1, [x19]
	ldrh	w0, [x19, 10]
	cmp	w1, w0
	bcc	.L962
	adrp	x0, .LC9
	add	x1, x25, 332
	add	x0, x0, :lo12:.LC9
	mov	w2, 701
	bl	sftl_printk
.L962:
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19, 16]
	ldrh	w28, [x0, x1, lsl 1]
	mov	w1, 0
	ldrh	w0, [x19, 2]
	str	x26, [x21, 8]
	orr	w0, w0, w28, lsl 10
	str	w0, [x21, 4]
	ldr	x0, [x20, 3512]
	str	x0, [x21, 16]
	bl	ftl_memset
	ldr	x23, [x21, 16]
	ldr	w0, [x19, 48]
	str	w0, [x23, 4]
	strh	w24, [x23, 8]
	ldrh	w0, [x19, 4]
	strh	w0, [x23]
	strh	w28, [x23, 2]
	ldrh	w1, [x20, 314]
	ldr	x0, [x21, 8]
	bl	js_hash
	str	w0, [x23, 12]
	mov	w3, 1
	mov	x0, x21
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	ldp	w0, w1, [x21]
	cmn	w0, #1
	bne	.L963
	adrp	x0, .LC105
	add	x0, x0, :lo12:.LC105
	bl	sftl_printk
	add	w22, w22, 1
	ldrh	w0, [x19, 2]
	and	w22, w22, 65535
	cmp	w0, 2
	bhi	.L964
	ldrh	w0, [x20, 308]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L964:
	cmp	w22, 3
	bls	.L965
	ldr	w1, [x20, 3716]
	adrp	x0, .LC106
	mov	w2, w22
	add	x0, x0, :lo12:.LC106
	bl	sftl_printk
.L966:
	b	.L966
.L965:
	ldr	w0, [x19, 52]
	cbz	w0, .L958
.L968:
	b	.L968
.L963:
	ldr	x0, [x19, 40]
	str	w1, [x0, x27]
	ldrh	w0, [x19, 2]
	cmp	w0, 1
	beq	.L969
	ldr	w0, [x21]
	cmp	w0, 256
	beq	.L969
	ldr	w1, [x19, 56]
	cbz	w1, .L970
.L969:
	str	wzr, [x19, 56]
	b	.L958
.L970:
	cmn	w0, #1
	bne	.L971
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 332
	add	x0, x0, :lo12:.LC9
	mov	w2, 745
	bl	sftl_printk
.L971:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
	.size	FtlMapWritePage, .-FtlMapWritePage
	.section	.rodata.str1.1
.LC107:
	.string	"region_id = %x phyAddr = %x\n"
.LC108:
	.string	"map_ppn:"
.LC109:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
	.text
	.align	2
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE89
	.text
.LPFE89:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -64]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	and	w20, w1, 65535
	stp	x21, x22, [sp, 32]
	and	w21, w0, 65535
	ldrh	w1, [x19, 340]
	stp	x23, x24, [sp, 48]
	cmp	w1, w0, uxth
	bcs	.L983
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 348
	add	x0, x0, :lo12:.LC9
	mov	w2, 485
	bl	sftl_printk
.L983:
	ldr	x1, [x19, 3600]
	ubfiz	x0, x21, 2, 16
	ubfiz	x20, x20, 4, 16
	ldr	w23, [x1, x0]
	ldr	x0, [x19, 2608]
	add	x0, x0, x20
	ldr	x0, [x0, 8]
	cbnz	w23, .L984
	ldrh	w2, [x19, 314]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x19, 2608]
	strh	w21, [x0, x20]
	ldr	x0, [x19, 2608]
	add	x20, x0, x20
	str	wzr, [x20, 4]
.L985:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	hint	29 // autiasp
	ret
.L984:
	add	x22, x19, 3712
	str	x0, [x19, 3720]
	ldr	x0, [x19, 3512]
	mov	w2, 1
	mov	w1, w2
	str	w23, [x19, 3716]
	str	x0, [x19, 3728]
	mov	x0, x22
	bl	FlashReadPages
	ldr	x24, [x19, 3728]
	ldrh	w0, [x24, 8]
	cmp	w0, w21
	beq	.L986
	mov	w2, w23
	mov	w1, w21
	adrp	x0, .LC107
	add	x0, x0, :lo12:.LC107
	bl	sftl_printk
	ldr	x1, [x19, 3728]
	mov	w3, 4
	adrp	x0, .LC95
	mov	w2, w3
	add	x0, x0, :lo12:.LC95
	bl	rknand_print_hex
	ldrh	w3, [x19, 340]
	adrp	x0, .LC108
	ldr	x1, [x19, 3600]
	add	x0, x0, :lo12:.LC108
	mov	w2, 4
	bl	rknand_print_hex
.L987:
	ldrh	w0, [x24, 8]
	cmp	w0, w21
	beq	.L989
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 348
	add	x0, x0, :lo12:.LC9
	mov	w2, 508
	bl	sftl_printk
.L989:
	ldr	x0, [x19, 2608]
	add	x0, x0, x20
	str	wzr, [x0, 4]
	ldr	x0, [x19, 2608]
	strh	w21, [x0, x20]
	b	.L985
.L986:
	ldr	w0, [x19, 3712]
	cmp	w0, 256
	bne	.L989
	mov	w2, w23
	mov	w1, w21
	adrp	x0, .LC109
	add	x0, x0, :lo12:.LC109
	bl	sftl_printk
	ldr	x0, [x19, 2608]
	mov	w1, w21
	add	x0, x0, x20
	ldr	x2, [x0, 8]
	add	x0, x19, 3632
	bl	FtlMapWritePage
	b	.L987
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE90
	.text
.LPFE90:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x20, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	ldr	x21, [x0, 16]
	ldr	x24, [x0, 40]
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 10]
	ldrh	w2, [x19, 8]
	sub	w1, w1, #5
	cmp	w2, w1
	blt	.L993
	ubfiz	x0, x0, 1, 16
	ldrh	w25, [x21, x0]
	cbz	w25, .L993
	ldr	w1, [x19, 52]
	cbnz	w1, .L993
	mov	w1, 1
	str	w1, [x19, 52]
	strh	wzr, [x21, x0]
	ldrh	w0, [x19, 8]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	ldrh	w0, [x20, 308]
	cmp	w1, w0
	bcc	.L994
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L994:
	adrp	x22, .LANCHOR1
	add	x22, x22, :lo12:.LANCHOR1
	adrp	x26, .LC9
	add	x22, x22, 364
	add	x26, x26, :lo12:.LC9
	mov	w21, 0
.L995:
	ldrh	w0, [x19, 6]
	cmp	w0, w21
	bhi	.L1002
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19, 52]
.L993:
	ldrh	w1, [x19, 2]
	ldrh	w0, [x20, 308]
	cmp	w1, w0
	bcc	.L1003
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1003:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
.L1002:
	ubfiz	x1, x21, 2, 16
	add	x27, x24, x1
	ldr	w0, [x24, x1]
	cmp	w25, w0, lsr 10
	bne	.L996
	add	x0, x20, 3712
	ldr	x2, [x20, 3456]
	ldr	x23, [x20, 3512]
	stp	x2, x23, [x0, 8]
	mov	w2, 1
	ldr	w1, [x24, x1]
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldrh	w0, [x23, 8]
	cmp	w0, w21
	beq	.L997
	mov	x1, x22
	mov	x0, x26
	mov	w2, 611
	bl	sftl_printk
.L997:
	ldr	w1, [x20, 3712]
	add	x0, x20, 3712
	cmn	w1, #1
	bne	.L998
.L1000:
	str	wzr, [x27]
.L999:
	b	.L999
.L998:
	ldrh	w1, [x23, 8]
	cmp	w1, w21
	bne	.L1000
	ldrh	w2, [x23]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L1000
	ldr	x2, [x0, 8]
	mov	w1, w21
	mov	x0, x19
	bl	FtlMapWritePage
.L996:
	add	w21, w21, 1
	and	w21, w21, 65535
	b	.L995
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE91
	.text
.LPFE91:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -64]!
	mov	w1, 65535
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	str	x23, [sp, 48]
	ldr	x20, [x0, 16]
	ldrh	w0, [x0]
	cmp	w0, w1
	bne	.L1010
	ldrh	w0, [x19, 8]
	cbz	w0, .L1011
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 379
	add	x0, x0, :lo12:.LC9
	mov	w2, 641
	bl	sftl_printk
.L1011:
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	strh	w0, [x19, 8]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x20]
	str	wzr, [x19]
	ldr	w0, [x19, 48]
	add	w0, w0, 1
	str	w0, [x19, 48]
.L1012:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	hint	29 // autiasp
	ret
.L1010:
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x19, 2]
	ldr	x23, [x19, 40]
	ldrh	w22, [x20, x0]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	ldr	w0, [x19, 48]
	orr	w1, w1, w22, lsl 10
	ldr	x21, [x20, 3512]
	str	w1, [x20, 3716]
	ldr	x1, [x20, 3448]
	str	x1, [x20, 3720]
	str	x21, [x20, 3728]
	mov	w1, 255
	str	w0, [x21, 4]
	mov	w0, -1291
	strh	w0, [x21, 8]
	ldrh	w0, [x19, 4]
	strh	w0, [x21]
	strh	w22, [x21, 2]
	ldrh	w2, [x20, 308]
	ldr	x0, [x20, 3448]
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w2, 0
	mov	w0, 0
.L1013:
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L1015
	ldrh	w1, [x20, 314]
	add	x22, x20, 3712
	ldr	x0, [x20, 3720]
	bl	js_hash
	str	w0, [x21, 12]
	mov	w2, 1
	mov	x0, x22
	mov	w1, w2
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	mov	x0, x19
	bl	ftl_map_blk_gc
	b	.L1012
.L1015:
	ubfiz	x3, x0, 2, 16
	ldr	w1, [x23, x3]
	cmp	w22, w1, lsr 10
	bne	.L1014
	add	w1, w2, 1
	ldr	x5, [x20, 3448]
	ubfiz	x4, x1, 3, 16
	and	w2, w1, 65535
	ubfiz	x1, x1, 3, 16
	add	x1, x1, 4
	str	w0, [x5, x4]
	ldr	w4, [x23, x3]
	ldr	x3, [x20, 3448]
	str	w4, [x3, x1]
.L1014:
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1013
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	flush_l2p_region
	.type	flush_l2p_region, %function
flush_l2p_region:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE92
	.text
.LPFE92:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x20, x0, 4, 16
	ldr	x0, [x19, 2608]
	add	x1, x0, x20
	ldr	x2, [x1, 8]
	ldrh	w1, [x0, x20]
	add	x0, x19, 3632
	bl	FtlMapWritePage
	ldr	x0, [x19, 2608]
	add	x0, x0, x20
	ldr	w1, [x0, 4]
	and	w1, w1, 2147483647
	str	w1, [x0, 4]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE93
	.text
.LPFE93:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	w21, w0
	mov	x22, x1
	ldrh	w19, [x20, 312]
	stp	x23, x24, [sp, 48]
	mov	w23, w2
	add	w0, w19, 7
	mov	x19, 1
	str	x25, [sp, 64]
	lsr	w24, w21, w0
	and	w24, w24, 65535
	lsl	x19, x19, x0
	ldr	w0, [x20, 2624]
	sub	w19, w19, #1
	and	w19, w19, w21
	and	w19, w19, 65535
	cmp	w0, w21
	bhi	.L1023
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 410
	mov	w2, 820
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	bl	sftl_printk
	ldr	w0, [x20, 2624]
	cmp	w0, w21
	bhi	.L1023
	mov	w0, -1
	cbnz	w23, .L1022
	str	w0, [x22]
.L1022:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
.L1029:
	add	x0, x0, 1
	lsl	x3, x0, 4
	ldrh	w3, [x1, x3]
	cmp	w3, w24
	bne	.L1025
.L1026:
	ldr	x0, [x20, 2608]
	ubfiz	x2, x25, 4, 16
	ubfiz	x19, x19, 2, 16
	add	x0, x0, x2
	ldr	x0, [x0, 8]
	cbnz	w23, .L1027
	ldr	w0, [x0, x19]
	str	w0, [x22]
.L1028:
	ldr	x0, [x20, 2608]
	add	x0, x0, x2
	ldr	w1, [x0, 4]
	cmn	w1, #1
	beq	.L1032
	add	w1, w1, 1
	str	w1, [x0, 4]
.L1032:
	mov	w0, 0
	b	.L1022
.L1027:
	ldr	w1, [x22]
	str	w1, [x0, x19]
	strh	w24, [x20, 2616]
	ldr	x1, [x20, 2608]
	add	x1, x1, x2
	ldr	w0, [x1, 4]
	orr	w0, w0, -2147483648
	str	w0, [x1, 4]
	b	.L1028
.L1023:
	ldr	x1, [x20, 2608]
	mov	x0, 0
	ldrh	w2, [x20, 342]
	sub	x1, x1, #16
.L1025:
	and	w25, w0, 65535
	cmp	w2, w0, uxth
	bhi	.L1029
	bl	select_l2p_ram_region
	and	w25, w0, 65535
	mov	w21, w25
	ldr	x2, [x20, 2608]
	ubfiz	x1, x21, 4, 16
	add	x3, x2, x1
	ldrh	w2, [x2, x1]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1030
	ldr	w1, [x3, 4]
	tbz	w1, #31, .L1030
	bl	flush_l2p_region
.L1030:
	mov	w1, w21
	mov	w0, w24
	bl	load_l2p_region
	b	.L1026
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlWriteDump_data
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE94
	.text
.LPFE94:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -112]!
	mrs	x0, sp_el0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	str	x23, [sp, 48]
	ldrh	w2, [x19, 28]
	ldr	x1, [x0, 1376]
	str	x1, [sp, 104]
	mov	x1, 0
	cbz	w2, .L1036
	ldrb	w1, [x19, 32]
	cbnz	w1, .L1036
	ldrb	w1, [x19, 31]
	ldrh	w3, [x19, 306]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L1036
	ldrb	w0, [x19, 34]
	cbnz	w0, .L1035
	ldr	w22, [x19, 2624]
	add	x1, sp, 68
	ldrh	w21, [x19, 236]
	mov	w2, 0
	sub	w22, w22, #1
	mov	w0, w22
	bl	log2phys
	ldr	x20, [x19, 3512]
	str	w22, [sp, 96]
	ldr	w0, [sp, 68]
	ldr	x1, [x19, 3448]
	str	w0, [sp, 76]
	stp	x1, x20, [sp, 80]
	str	wzr, [x20, 4]
	cmn	w0, #1
	beq	.L1038
	add	x0, sp, 72
	mov	w2, 0
	mov	w1, 1
	bl	FlashReadPages
.L1039:
	lsl	w21, w21, 2
	add	x23, x19, 24
	mov	w0, -3947
	strh	w0, [x20]
.L1040:
	cbnz	w21, .L1044
.L1041:
	mov	w0, 1
	strb	w0, [x19, 34]
.L1035:
	mrs	x0, sp_el0
	ldr	x1, [sp, 104]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1046
	bl	__stack_chk_fail
.L1038:
	ldrh	w2, [x19, 314]
	mov	w1, 255
	ldr	x0, [x19, 3448]
	bl	ftl_memset
	b	.L1039
.L1044:
	ldrh	w0, [x23, 4]
	cbz	w0, .L1041
	ldr	w0, [sp, 76]
	sub	w21, w21, #1
	stp	w22, w0, [x20, 8]
	ldrh	w0, [x23]
	strh	w0, [x20, 2]
	mov	x0, x23
	bl	get_new_active_ppa
	str	w0, [sp, 76]
	ldr	w0, [x19, 2664]
	mov	w3, 0
	str	w0, [x20, 4]
	mov	w2, 0
	cmn	w0, #2
	mov	w1, 1
	csinc	w0, wzr, w0, eq
	str	w0, [x19, 2664]
	add	x0, sp, 72
	bl	FlashProgPages
	ldrh	w0, [x23]
	bl	decrement_vpc_count
	b	.L1040
.L1036:
	strb	wzr, [x19, 34]
	b	.L1035
.L1046:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 112
	hint	29 // autiasp
	ret
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	FtlReUsePrevPpa
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE95
	.text
.LPFE95:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -64]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	w21, w0
	ubfx	x0, x1, 10, 16
	str	w1, [sp, 60]
	bl	P2V_block_in_plane
	and	w4, w0, 65535
	mov	w22, w4
	ldr	x0, [x19, 72]
	ubfiz	x20, x22, 1, 16
	ldrh	w3, [x0, x20]
	cbnz	w3, .L1055
	ldr	x2, [x19, 2592]
	cbz	x2, .L1056
	ldr	x0, [x19, 2560]
	mov	x5, -6148914691236517206
	movk	x5, 0xaaab, lsl 0
	ldrh	w1, [x19, 228]
	sub	x2, x2, x0
	mov	w6, 65535
	asr	x2, x2, 1
	mul	x2, x2, x5
	mov	w5, 6
	and	w2, w2, 65535
.L1057:
	cmp	w3, w1
	beq	.L1056
	cmp	w2, w4
	bne	.L1058
	mov	w1, w22
	add	x0, x19, 2592
	bl	List_remove_node
	ldrh	w0, [x19, 228]
	cbnz	w0, .L1059
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 419
	add	x0, x0, :lo12:.LC9
	mov	w2, 1742
	bl	sftl_printk
.L1059:
	ldrh	w0, [x19, 228]
	sub	w0, w0, #1
	strh	w0, [x19, 228]
	mov	w0, w22
	bl	INSERT_DATA_LIST
	ldr	x1, [x19, 72]
	ldrh	w0, [x1, x20]
	add	w0, w0, 1
	strh	w0, [x1, x20]
.L1056:
	add	x1, sp, 60
	mov	w0, w21
	mov	w2, 1
	bl	log2phys
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	hint	29 // autiasp
	ret
.L1058:
	umull	x2, w2, w5
	ldrh	w2, [x0, x2]
	cmp	w2, w6
	beq	.L1056
	add	w3, w3, 1
	and	w3, w3, 65535
	b	.L1057
.L1055:
	add	w3, w3, 1
	strh	w3, [x0, x20]
	b	.L1056
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.section	.rodata.str1.1
.LC110:
	.string	"data prev_ppa = %x error...................\n"
.LC111:
	.string	"spuer block %x vpn is 0\n "
	.text
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE96
	.text
.LPFE96:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -192]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	mrs	x0, sp_el0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x1, [x0, 1376]
	str	x1, [sp, 184]
	mov	x1, 0
	ldrh	w0, [x20]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1067
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x20, 6]
	ldrh	w24, [x20, 2]
	str	w0, [sp, 152]
	ldrh	w0, [x19, 306]
	cmp	w0, w24
	bne	.L1068
	strh	w0, [x20, 2]
	strh	wzr, [x20, 4]
.L1189:
	strb	wzr, [x20, 6]
.L1067:
	mrs	x0, sp_el0
	ldr	x1, [sp, 184]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1133
	bl	__stack_chk_fail
.L1068:
	ldrh	w0, [x20, 16]
	mov	w21, 0
.L1069:
	cmp	w0, w1
	beq	.L1070
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	mov	w26, w0
	cmn	w0, #1
	beq	.L1071
	ldrh	w6, [x19, 236]
	add	x9, x20, 16
	ldrh	w7, [x19, 314]
	mov	x1, 0
	ldrh	w8, [x19, 316]
	mov	w22, 0
	mov	w10, 65535
	mov	w4, 4
.L1072:
	ldr	x0, [x19, 3424]
	cmp	w6, w1, uxth
	bhi	.L1076
	mov	w2, 0
	mov	w1, w22
	bl	FlashReadPages
	ldr	w21, [x19, 2664]
	ldr	x28, [x19, 3424]
	and	w4, w26, 65535
	sub	w21, w21, #1
	mov	w25, 65535
	mov	x2, x28
	mov	w27, 0
.L1077:
	cmp	w22, w27
	bne	.L1082
	add	w23, w26, 1
	ldr	w0, [x28, 4]
	and	w23, w23, 65535
.L1184:
	ubfx	x0, x0, 10, 16
	bl	P2V_plane
	and	w22, w0, 65535
	ldrh	w0, [x19, 306]
	cmp	w0, w23
	bne	.L1084
	strh	w23, [x20, 2]
	strh	wzr, [x20, 4]
	strb	wzr, [x20, 6]
.L1084:
	ldr	w0, [sp, 152]
	cmp	w23, w24
	ccmp	w22, w0, 0, eq
	bne	.L1085
.L1190:
	mov	w2, w22
	mov	w1, w23
	mov	x0, x20
	bl	ftl_sb_update_avl_pages
	b	.L1067
.L1070:
	add	w0, w21, 1
	and	w21, w0, 65535
	add	x0, x20, x0, uxth 1
	ldrh	w0, [x0, 16]
	b	.L1069
.L1071:
	cbz	w24, .L1073
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 435
	add	x0, x0, :lo12:.LC9
	mov	w2, 1809
	bl	sftl_printk
.L1073:
	ldr	w0, [sp, 152]
	cmp	w0, 0
	ccmp	w21, w0, 4, ne
	beq	.L1074
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 435
	add	x0, x0, :lo12:.LC9
	mov	w2, 1810
	bl	sftl_printk
.L1074:
	strh	wzr, [x20, 2]
	b	.L1189
.L1076:
	ldrh	w3, [x9, x1, lsl 1]
	cmp	w3, w10
	beq	.L1075
	ubfiz	x5, x22, 5, 16
	orr	w3, w26, w3, lsl 10
	add	x0, x0, x5
	str	w3, [x0, 4]
	mul	w0, w22, w7
	ldr	x2, [x19, 3424]
	sdiv	w0, w0, w4
	ldr	x3, [x19, 3256]
	add	x2, x2, x5
	add	x0, x3, x0, sxtw 2
	str	x0, [x2, 8]
	mul	w0, w22, w8
	add	w22, w22, 1
	ldr	x3, [x19, 3264]
	and	w22, w22, 65535
	sdiv	w0, w0, w4
	add	x0, x3, x0, sxtw 2
	str	x0, [x2, 16]
.L1075:
	add	x1, x1, 1
	b	.L1072
.L1082:
	ldr	w0, [x2]
	cbnz	w0, .L1078
	ldr	x3, [x2, 16]
	ldr	w23, [x3, 4]
	cmn	w23, #1
	beq	.L1079
	ldr	w1, [x19, 2664]
	mov	w0, w23
	stp	x3, x2, [sp, 96]
	str	w4, [sp, 112]
	bl	ftl_cmp_data_ver
	ldr	w4, [sp, 112]
	ldp	x3, x2, [sp, 96]
	cbz	w0, .L1079
	add	w23, w23, 1
	str	w23, [x19, 2664]
.L1079:
	ldr	w0, [x3]
	cmn	w0, #1
	bne	.L1081
	ubfiz	x27, x27, 5, 16
	and	w23, w26, 65535
	add	x27, x28, x27
	ldr	w0, [x27, 4]
	b	.L1184
.L1078:
	mov	w25, w4
.L1081:
	add	w27, w27, 1
	add	x2, x2, 32
	and	w27, w27, 65535
	b	.L1077
.L1085:
	mov	w0, 65535
	cmp	w25, w0
	bne	.L1086
	ldrb	w0, [x20, 8]
	cbnz	w0, .L1087
.L1086:
	ldr	w0, [x19, 3708]
	and	w27, w26, 65535
	cmn	w0, #1
	bne	.L1088
	str	w21, [x19, 3708]
.L1088:
	add	w0, w24, 7
	sub	w25, w26, #7
	and	w25, w25, 65535
	cmp	w0, w26, uxth
	csel	w25, w25, w24, lt
	add	x4, x20, 16
	mov	w28, 65535
.L1090:
	ldrh	w3, [x19, 236]
	cmp	w25, w27
	bls	.L1135
	mov	w0, -1
	str	w0, [x19, 3708]
.L1087:
	mov	w0, 1
	mov	w27, w24
	strh	w0, [x19, 3744]
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 435
	str	x0, [sp, 120]
.L1098:
	ldrh	w3, [x19, 236]
	add	x4, x20, 16
	mov	x1, 0
	mov	w28, 0
	mov	w5, 65535
.L1099:
	ldr	x0, [x19, 3424]
	cmp	w3, w1, uxth
	bhi	.L1101
	mov	w1, w28
	mov	w2, 0
	bl	FlashReadPages
	str	xzr, [sp, 96]
	adrp	x0, .LC111
	add	x0, x0, :lo12:.LC111
	str	x0, [sp, 128]
.L1102:
	ldrh	w0, [sp, 96]
	cmp	w28, w0, uxth
	bhi	.L1129
	ldrh	w1, [x19, 306]
	add	w0, w27, 1
	and	w27, w0, 65535
	cmp	w1, w0, uxth
	bne	.L1098
	ldrh	w2, [x19, 236]
	add	x3, x20, 14
	mov	x0, 0
	mov	w4, 65535
	strh	w27, [x20, 2]
	strh	wzr, [x20, 4]
.L1130:
	cmp	w2, w0, uxth
	bls	.L1067
	add	x1, x0, 1
	ldrh	w5, [x3, x1, lsl 1]
	cmp	w5, w4
	beq	.L1136
	strb	w0, [x20, 6]
	b	.L1067
.L1092:
	ldrh	w2, [x4, x1, lsl 1]
	cmp	w2, w28
	beq	.L1091
	ubfiz	x5, x26, 5, 16
	add	w26, w26, 1
	add	x0, x0, x5
	and	w26, w26, 65535
	orr	w2, w25, w2, lsl 10
	str	w2, [x0, 4]
.L1091:
	add	x1, x1, 1
.L1097:
	ldr	x0, [x19, 3424]
	cmp	w3, w1, uxth
	bhi	.L1092
	mov	w1, w26
	mov	w2, 0
	str	x4, [sp, 96]
	bl	FlashReadPages
	ldr	x0, [x19, 3424]
	mov	w1, 32
	ldr	x4, [sp, 96]
	umaddl	x1, w1, w26, x0
.L1093:
	cmp	x1, x0
	bne	.L1096
	add	w25, w25, 1
	and	w25, w25, 65535
	b	.L1090
.L1135:
	mov	x1, 0
	mov	w26, 0
	b	.L1097
.L1096:
	ldr	w2, [x0]
	cbnz	w2, .L1087
	ldr	x2, [x0, 16]
	ldrh	w3, [x2]
	cmp	w3, w28
	beq	.L1095
	ldr	w2, [x2, 4]
	cmn	w2, #1
	beq	.L1095
	str	w2, [x19, 3708]
.L1095:
	add	x0, x0, 32
	b	.L1093
.L1101:
	ldrh	w2, [x4, x1, lsl 1]
	cmp	w2, w5
	beq	.L1100
	ubfiz	x6, x28, 5, 16
	add	w28, w28, 1
	add	x0, x0, x6
	and	w28, w28, 65535
	orr	w2, w27, w2, lsl 10
	str	w2, [x0, 4]
.L1100:
	add	x1, x1, 1
	b	.L1099
.L1129:
	ldr	x0, [sp, 96]
	ldr	x2, [x19, 3424]
	lsl	x26, x0, 5
	add	x1, x2, x0, lsl 5
	stp	x1, x2, [sp, 104]
	ldr	w25, [x1, 4]
	str	w25, [sp, 180]
	cmp	w27, w24
	bcc	.L1103
	ubfx	x0, x25, 10, 16
	bl	P2V_plane
	ldr	w1, [sp, 152]
	and	w0, w0, 65535
	cmp	w27, w24
	ccmp	w1, w0, 0, eq
	bhi	.L1103
	cmp	w27, w23
	ccmp	w22, w0, 0, eq
	ldp	x1, x2, [sp, 104]
	bne	.L1104
	strh	w23, [x20, 2]
	strb	w22, [x20, 6]
	b	.L1190
.L1104:
	ldr	w0, [x2, x26]
	cmn	w0, #1
	beq	.L1105
	ldr	x3, [x1, 16]
	mov	w0, 61589
	ldrh	w25, [x3]
	cmp	w25, w0
	beq	.L1106
	ldrh	w0, [x20]
.L1186:
	bl	decrement_vpc_count
	b	.L1103
.L1106:
	ldr	w21, [x3, 4]
	cmn	w21, #1
	beq	.L1108
	ldr	w1, [x19, 2664]
	mov	w0, w21
	str	x3, [sp, 104]
	bl	ftl_cmp_data_ver
	ldr	x3, [sp, 104]
	cbz	w0, .L1108
	add	w0, w21, 1
	str	w0, [x19, 2664]
	ldrh	w0, [x3]
	cmp	w0, w25
	beq	.L1108
	ldr	x1, [sp, 120]
	mov	w2, 1961
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	bl	sftl_printk
	ldr	x3, [sp, 104]
.L1108:
	add	x1, sp, 176
	ldp	w25, w0, [x3, 8]
	mov	w2, 0
	str	w0, [sp, 172]
	mov	w0, w25
	bl	log2phys
	ldr	w1, [x19, 3708]
	ldr	w2, [sp, 172]
	cmn	w1, #1
	beq	.L1110
	mov	w0, w21
	str	w2, [sp, 104]
	bl	ftl_cmp_data_ver
	ldr	w2, [sp, 104]
	cbz	w0, .L1110
	cmn	w2, #1
	beq	.L1111
	ldr	x0, [x19, 3424]
	add	x0, x0, x26
	str	w2, [x0, 4]
	mov	w2, 0
	ldr	x1, [x0, 16]
	str	x1, [sp, 104]
	ldr	x0, [x19, 3424]
	mov	w1, 1
	add	x0, x0, x26
	bl	FlashReadPages
	ldr	x2, [x19, 3424]
	add	x3, x2, x26
	ldr	w0, [x2, x26]
	cmn	w0, #1
	bne	.L1112
.L1113:
	mov	w0, -1
	str	w0, [sp, 172]
.L1120:
	ldr	w0, [sp, 172]
	cmn	w0, #1
	beq	.L1103
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	and	w26, w0, 65535
	ldrh	w0, [x19, 244]
	mov	w25, w26
	cmp	w0, w26, uxth
	bhi	.L1125
	ldr	x1, [sp, 120]
	adrp	x0, .LC9
	mov	w2, 2066
	add	x0, x0, :lo12:.LC9
	bl	sftl_printk
.L1125:
	ldr	x1, [x19, 72]
	ubfiz	x0, x26, 1, 16
	ldrh	w0, [x1, x0]
	cbz	w0, .L1126
	mov	w0, w25
	b	.L1186
.L1111:
	ldp	w1, w0, [sp, 176]
	cmp	w1, w0
	bne	.L1103
	add	x1, sp, 172
	mov	w0, w25
	mov	w2, 1
	bl	log2phys
.L1103:
	ldr	x0, [sp, 96]
	add	x0, x0, 1
	str	x0, [sp, 96]
	b	.L1102
.L1112:
	ldr	x0, [sp, 104]
	ldr	w4, [x0, 8]
	cmp	w4, w25
	bne	.L1113
	ldr	w0, [x0, 4]
	str	w0, [sp, 112]
	uxtw	x1, w0
	ldr	w0, [x19, 3708]
	stp	x3, x2, [sp, 136]
	str	w4, [sp, 156]
	bl	ftl_cmp_data_ver
	cbz	w0, .L1113
	ldp	w1, w0, [sp, 172]
	ldr	w5, [sp, 180]
	ldr	w4, [sp, 156]
	cmp	w0, w5
	ldp	x3, x2, [sp, 136]
	bne	.L1115
	mov	w0, w4
.L1185:
	bl	FtlReUsePrevPpa
	b	.L1113
.L1115:
	cmp	w0, w1
	beq	.L1113
	cmn	w0, #1
	beq	.L1116
	str	w0, [x3, 4]
	mov	w2, 0
	ldr	x1, [x3, 16]
	str	x1, [sp, 104]
	ldr	x0, [x19, 3424]
	mov	w1, 1
	add	x0, x0, x26
	bl	FlashReadPages
.L1117:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 3424]
	ldr	w1, [x1, x26]
	cmn	w1, #1
	beq	.L1118
	ldr	x1, [sp, 104]
	ldr	w0, [x0, 3708]
	ldr	w26, [x1, 4]
	mov	w1, w26
	bl	ftl_cmp_data_ver
	cbz	w0, .L1118
	ldr	w0, [sp, 112]
	mov	w1, w26
	bl	ftl_cmp_data_ver
	cbz	w0, .L1113
.L1118:
	ldr	w1, [sp, 172]
	mov	w0, w25
	b	.L1185
.L1116:
	str	w0, [x2, x26]
	b	.L1117
.L1110:
	ldp	w1, w0, [sp, 176]
	cmp	w1, w0
	beq	.L1120
	cmn	w2, #1
	beq	.L1122
	ldr	w0, [x19, 252]
	cmp	w0, w2, lsr 10
	bhi	.L1122
	adrp	x0, .LC110
	mov	w1, w2
	add	x0, x0, :lo12:.LC110
.L1188:
	bl	sftl_printk
	b	.L1103
.L1122:
	add	x1, sp, 180
	mov	w0, w25
	mov	w2, 1
	bl	log2phys
	ldr	w26, [sp, 176]
	cmn	w26, #1
	beq	.L1120
	ldr	w0, [sp, 172]
	cmp	w26, w0
	beq	.L1120
	ubfx	x0, x26, 10, 16
	bl	P2V_block_in_plane
	ldrh	w2, [x19, 24]
	and	w1, w0, 65535
	cmp	w2, w0, uxth
	beq	.L1124
	ldrh	w0, [x19, 80]
	cmp	w0, w1
	beq	.L1124
	ldrh	w0, [x19, 128]
	cmp	w0, w1
	bne	.L1120
.L1124:
	ldr	x0, [x19, 3424]
	mov	w2, 0
	mov	w1, 1
	str	w26, [x0, 4]
	ldr	x3, [x0, 16]
	str	x3, [sp, 104]
	ldr	x0, [x19, 3424]
	bl	FlashReadPages
	ldr	x0, [x19, 3424]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1120
	ldr	x3, [sp, 104]
	mov	w0, w21
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
	cbnz	w0, .L1120
	add	x1, sp, 176
	mov	w0, w25
	mov	w2, 1
	bl	log2phys
	b	.L1120
.L1126:
	mov	w1, w26
	ldr	x0, [sp, 128]
	b	.L1188
.L1105:
	ldr	w0, [x19, 3748]
	cmp	w0, 31
	bhi	.L1127
	add	x1, x19, 3752
	str	w25, [x1, w0, uxtw 2]
	add	w0, w0, 1
	str	w0, [x19, 3748]
.L1127:
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	ldr	w0, [x19, 3708]
	cmn	w0, #1
	bne	.L1128
.L1187:
	str	w21, [x19, 3708]
	b	.L1103
.L1128:
	cmp	w0, w21
	bls	.L1103
	b	.L1187
.L1136:
	mov	x0, x1
	b	.L1130
.L1133:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 192
	hint	29 // autiasp
	ret
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.section	.rodata.str1.1
.LC112:
	.string	"...%s enter...\n"
.LC113:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC114:
	.string	"free blk vpc error %x = %x  %x\n"
	.text
	.align	2
	.global	ftl_check_vpc
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE97
	.text
.LPFE97:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -112]!
	mrs	x0, sp_el0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	w19, 0
	adrp	x22, .LANCHOR1
	stp	x23, x24, [sp, 48]
	add	x22, x22, :lo12:.LANCHOR1
	adrp	x21, check_vpc_table
	stp	x25, x26, [sp, 64]
	add	x21, x21, :lo12:check_vpc_table
	stp	x27, x28, [sp, 80]
	ldr	x1, [x0, 1376]
	str	x1, [sp, 104]
	mov	x1, 0
	adrp	x0, .LC112
	add	x1, x22, 457
	add	x0, x0, :lo12:.LC112
	bl	sftl_printk
	mov	x0, x21
	mov	w2, 8192
	mov	w1, 0
	bl	ftl_memset
.L1192:
	ldr	w0, [x20, 2624]
	cmp	w0, w19
	bhi	.L1194
	adrp	x23, .LC113
	add	x23, x23, :lo12:.LC113
	mov	w24, 0
	mov	w19, 0
	mov	w26, 65535
	mov	w27, 1
.L1195:
	ldrh	w0, [x20, 244]
	cmp	w0, w19
	bhi	.L1197
	ldr	x19, [x20, 2592]
	cbz	x19, .L1198
	ldr	x1, [x20, 2560]
	adrp	x25, .LC114
	ldrh	w26, [x20, 228]
	add	x25, x25, :lo12:.LC114
	sub	x19, x19, x1
	mov	x1, -6148914691236517206
	movk	x1, 0xaaab, lsl 0
	mov	w23, 0
	asr	x19, x19, 1
	mov	w27, 6
	mov	w28, 65535
	mul	x19, x19, x1
	and	w19, w19, 65535
.L1199:
	cmp	w23, w26
	bne	.L1201
.L1198:
	cbz	w24, .L1191
	adrp	x0, .LC9
	add	x1, x22, 457
	add	x0, x0, :lo12:.LC9
	mov	w2, 2394
	bl	sftl_printk
.L1191:
	mrs	x0, sp_el0
	ldr	x1, [sp, 104]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1203
	bl	__stack_chk_fail
.L1194:
	add	x1, sp, 100
	mov	w0, w19
	mov	w2, 0
	bl	log2phys
	ldr	w0, [sp, 100]
	cmn	w0, #1
	beq	.L1193
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	and	x0, x0, 65535
	ldrh	w1, [x21, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x21, x0, lsl 1]
.L1193:
	add	w19, w19, 1
	b	.L1192
.L1197:
	ldr	x0, [x20, 72]
	ubfiz	x25, x19, 1, 16
	ldrh	w3, [x21, w19, sxtw 1]
	sxtw	x28, w19
	ldrh	w2, [x0, x25]
	cmp	w2, w3
	beq	.L1196
	mov	x0, x23
	mov	w1, w19
	bl	sftl_printk
	ldr	x0, [x20, 72]
	ldrh	w0, [x0, x25]
	cmp	w0, w26
	beq	.L1196
	ldrh	w1, [x21, x28, lsl 1]
	cmp	w1, w0
	csel	w24, w24, w27, ls
.L1196:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1195
.L1201:
	ldr	x1, [x20, 72]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L1200
	ldrh	w3, [x21, w19, sxtw 1]
	mov	w24, 1
	mov	w1, w19
	mov	x0, x25
	bl	sftl_printk
.L1200:
	umull	x19, w19, w27
	ldr	x0, [x20, 2560]
	ldrh	w19, [x0, x19]
	cmp	w19, w28
	beq	.L1198
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L1199
.L1203:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	hint	29 // autiasp
	ret
	.size	ftl_check_vpc, .-ftl_check_vpc
	.section	.rodata.str1.1
.LC115:
	.string	"ftl_scan_all_data = %x\n"
.LC116:
	.string	"scan lpa = %x ppa= %x\n"
.LC117:
	.string	"lpa = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
	.text
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE98
	.text
.LPFE98:
	nop
	nop
	hint	25 // paciasp
	sub	sp, sp, #96
	mrs	x0, sp_el0
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	mov	w20, 0
	stp	x21, x22, [sp, 48]
	adrp	x21, .LANCHOR0
	add	x21, x21, :lo12:.LANCHOR0
	str	x23, [sp, 64]
	adrp	x22, .LC116
	add	x19, x21, 3712
	add	x22, x22, :lo12:.LC116
	ldr	x1, [x0, 1376]
	str	x1, [sp, 88]
	mov	x1, 0
	adrp	x0, .LC115
	add	x0, x0, :lo12:.LC115
	bl	sftl_printk
.L1218:
	ldr	w0, [x21, 2624]
	cmp	w0, w20
	bhi	.L1224
	mrs	x0, sp_el0
	ldr	x1, [sp, 88]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1225
	bl	__stack_chk_fail
.L1224:
	add	x1, sp, 84
	mov	w0, w20
	mov	w2, 0
	bl	log2phys
	tst	x20, 2047
	bne	.L1219
	ldr	w2, [sp, 84]
	mov	w1, w20
	mov	x0, x22
	bl	sftl_printk
.L1219:
	ldr	w0, [sp, 84]
	cmn	w0, #1
	beq	.L1221
	stp	wzr, w0, [x19]
	mov	w2, 0
	ldr	x0, [x21, 3448]
	mov	w1, 1
	ldr	x23, [x21, 3512]
	str	x0, [x19, 8]
	mov	x0, x19
	str	x23, [x19, 16]
	str	w20, [x19, 24]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmp	w0, 256
	ccmn	w0, #1, 4, ne
	beq	.L1222
	ldr	w0, [x23, 8]
	cmp	w0, w20
	beq	.L1221
.L1222:
	ldp	x0, x1, [x19, 8]
	ldr	w2, [x0, 4]
	str	w2, [sp]
	ldr	w7, [x0]
	adrp	x0, .LC117
	ldp	w3, w4, [x1]
	add	x0, x0, :lo12:.LC117
	ldp	w5, w6, [x1, 8]
	mov	w1, w20
	ldr	w2, [x19, 4]
	bl	sftl_printk
.L1221:
	add	w20, w20, 1
	b	.L1218
.L1225:
	ldp	x29, x30, [sp, 16]
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	hint	29 // autiasp
	ret
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.section	.rodata.str1.1
.LC118:
	.string	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
	.text
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE99
	.text
.LPFE99:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -160]!
	mov	x29, sp
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	add	x22, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	mrs	x0, sp_el0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w28, [x22, 4]
	str	w1, [sp, 104]
	ldr	x1, [x0, 1376]
	str	x1, [sp, 152]
	mov	x1, 0
	mov	w0, 65535
	cmp	w28, w0
	beq	.L1251
	cbz	w28, .L1229
.L1228:
	adrp	x0, .LANCHOR0+306
	ldr	w1, [sp, 104]
	ldrh	w0, [x0, #:lo12:.LANCHOR0+306]
	cmp	w0, w1
	bne	.L1230
.L1229:
	bl	FtlGcPageVarInit
.L1230:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w24, 0
	mov	w26, 65535
.L1246:
	ldrh	w0, [x20]
	strb	wzr, [x20, 8]
	cmp	w0, w26
	beq	.L1231
	add	x0, x20, 16
	str	x0, [sp, 96]
.L1232:
	ldrh	w7, [x19, 236]
	mov	x1, 0
	ldrh	w8, [x19, 314]
	mov	w21, 0
	ldrh	w9, [x19, 316]
	mov	w5, 4
.L1233:
	ldr	x0, [x19, 3424]
	cmp	w7, w1, uxth
	bhi	.L1235
	mov	x23, 0
	mov	w1, w21
	mov	w2, 0
	bl	FlashReadPages
.L1236:
	cmp	w21, w23, uxth
	bhi	.L1247
	ldr	w0, [sp, 104]
	add	w3, w28, 1
	add	w24, w24, 1
	and	w28, w3, 65535
	cmp	w0, w24
	bhi	.L1248
	ldrh	w0, [x22, 4]
	cmp	w0, w26
	beq	.L1248
	add	w0, w0, w24
	strh	w0, [x22, 4]
	ldrh	w0, [x19, 306]
	cmp	w0, w28
	bls	.L1231
.L1249:
	mrs	x0, sp_el0
	ldr	x1, [sp, 152]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1250
	bl	__stack_chk_fail
.L1251:
	mov	w28, 0
	b	.L1228
.L1235:
	ldr	x2, [sp, 96]
	ldrh	w4, [x2, x1, lsl 1]
	cmp	w4, w26
	beq	.L1234
	ubfiz	x6, x21, 5, 16
	orr	w4, w28, w4, lsl 10
	add	x0, x0, x6
	str	w4, [x0, 4]
	mul	w0, w21, w8
	ldr	x2, [x19, 3424]
	sdiv	w0, w0, w5
	ldr	x4, [x19, 3256]
	add	x2, x2, x6
	add	x0, x4, x0, sxtw 2
	str	x0, [x2, 8]
	mul	w0, w21, w9
	add	w21, w21, 1
	ldr	x4, [x19, 3264]
	and	w21, w21, 65535
	sdiv	w0, w0, w5
	add	x0, x4, x0, sxtw 2
	str	x0, [x2, 16]
.L1234:
	add	x1, x1, 1
	b	.L1233
.L1247:
	ldr	x0, [x19, 3424]
	lsl	x27, x23, 5
	add	x1, x0, x23, lsl 5
	ldr	w0, [x0, x27]
	ldr	w5, [x1, 4]
	ldr	x25, [x1, 16]
	cbnz	w0, .L1237
	ldrh	w0, [x25]
	cmp	w0, w26
	beq	.L1261
	ldr	w1, [x19, 2624]
	ldr	w0, [x25, 8]
	cmp	w0, w1
	bls	.L1239
.L1261:
	ldrh	w1, [x20]
	mov	w28, 0
	ldr	x0, [x19, 72]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x20]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x20]
	strh	w0, [x19, 176]
	bl	FtlGcPageVarInit
	b	.L1246
.L1239:
	add	x1, sp, 116
	mov	w2, 0
	str	w5, [sp, 108]
	bl	log2phys
	ldr	w0, [x25, 12]
	ldr	w1, [sp, 116]
	ldr	w5, [sp, 108]
	cmp	w0, w1
	beq	.L1241
.L1243:
	ldr	w2, [x25, 8]
.L1262:
	ldr	w0, [x25, 12]
	mov	w1, w5
	add	x23, x23, 1
	bl	FtlGcUpdatePage
	b	.L1236
.L1241:
	cmn	w0, #1
	beq	.L1243
	str	w0, [sp, 124]
	mov	w2, 0
	ldr	x0, [x19, 3480]
	str	x0, [sp, 128]
	ldr	x0, [x19, 3520]
	mov	w1, 1
	str	w5, [sp, 108]
	str	x0, [sp, 136]
	add	x0, sp, 120
	bl	FlashReadPages
	ldr	w5, [sp, 108]
	ldrh	w1, [x19, 262]
	ldr	x2, [sp, 128]
	ldr	x0, [x19, 3424]
	lsl	w1, w1, 7
	sub	x2, x2, #4
	add	x27, x0, x27
	mov	x0, 0
.L1244:
	cmp	w1, w0
	bls	.L1243
	ldr	x6, [x27, 8]
	ldr	w7, [x6, x0, lsl 2]
	add	x0, x0, 1
	ldr	w6, [x2, x0, lsl 2]
	cmp	w7, w6
	beq	.L1244
	ldrh	w1, [x20]
	adrp	x0, .LC118
	ldr	w2, [sp, 124]
	add	x0, x0, :lo12:.LC118
	bl	sftl_printk
	b	.L1261
.L1237:
	mov	w2, -1
	b	.L1262
.L1248:
	ldrh	w0, [x19, 306]
	cmp	w0, w28
	bhi	.L1232
.L1231:
	strh	w28, [x20, 2]
	mov	w0, -1
	strb	wzr, [x20, 6]
	mov	w1, w28
	mov	w2, 0
	strh	w0, [x22, 4]
	mov	x0, x20
	bl	ftl_sb_update_avl_pages
	b	.L1249
.L1250:
	mov	w0, -1
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	hint	29 // autiasp
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE100
	.text
.LPFE100:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -160]!
	mrs	x0, sp_el0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	ldr	x1, [x0, 1376]
	str	x1, [sp, 152]
	mov	x1, 0
	ldr	w0, [x19, 2624]
	add	x1, x19, 2712
	ldr	w2, [x1, 80]
	cbz	w2, .L1264
	ldr	w2, [x1, 84]
	cmp	w2, w0
	bcs	.L1265
	mov	w20, 2048
.L1270:
	ldr	w1, [x19, 2624]
	add	x21, x19, 2712
	ldr	w0, [x21, 84]
	cmp	w0, w1
	bcc	.L1266
.L1269:
	mov	w0, -1
.L1263:
	mrs	x1, sp_el0
	ldr	x2, [sp, 152]
	ldr	x3, [x1, 1376]
	subs	x2, x2, x3
	mov	x3, 0
	beq	.L1274
	bl	__stack_chk_fail
.L1266:
	add	x1, sp, 52
	mov	w2, 0
	bl	log2phys
	ldr	w1, [sp, 52]
	ldr	w0, [x21, 84]
	add	w0, w0, 1
	str	w0, [x21, 84]
	cmn	w1, #1
	beq	.L1268
	str	w0, [sp, 80]
	mov	w2, 0
	ldr	x0, [x19, 3496]
	str	x0, [sp, 64]
	add	x0, sp, 88
	stp	wzr, w1, [sp, 56]
	mov	w1, 1
	str	x0, [sp, 72]
	add	x0, sp, 56
	bl	FlashReadPages
	ldr	w0, [sp, 56]
	cmp	w0, 256
	bne	.L1269
	ldr	w0, [sp, 52]
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1269
.L1268:
	subs	w20, w20, #1
	bne	.L1270
	b	.L1269
.L1265:
	ldr	w0, [x19, 2628]
	str	w0, [x1, 76]
	str	xzr, [x1, 80]
.L1277:
	mov	w0, 0
	b	.L1263
.L1264:
	ldr	w2, [x19, 2684]
	mov	w3, 10000
	mov	w5, 31
	ldr	w6, [x1, 76]
	cmp	w2, w3
	mov	w3, 63
	csel	w5, w5, w3, hi
	ldr	w3, [x19, 2628]
	ldrh	w4, [x19, 2540]
	add	w7, w3, 1048576
	cmp	w6, w7
	bhi	.L1273
	mov	w7, 1000
	lsr	w2, w2, 10
	add	w2, w2, 1
	mul	w0, w0, w7
	udiv	w0, w0, w2
	add	w0, w0, w6
	cmp	w3, w0
	bhi	.L1273
	tst	w5, w4
	bne	.L1277
	ldr	w0, [x1, 100]
	cmp	w0, w4
	beq	.L1277
.L1273:
	mov	x0, 1
	str	w3, [x19, 2788]
	str	x0, [x19, 2792]
	str	w4, [x19, 2812]
	b	.L1277
.L1274:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 160
	hint	29 // autiasp
	ret
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	l2p_flush
	.type	l2p_flush, %function
l2p_flush:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE101
	.text
.LPFE101:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w19, 0
	bl	FtlWriteDump_data
.L1281:
	ldrh	w0, [x20, 342]
	cmp	w0, w19
	bhi	.L1283
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
.L1283:
	ldr	x1, [x20, 2608]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L1282
	mov	w0, w19
	bl	flush_l2p_region
.L1282:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L1281
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE102
	.text
.LPFE102:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -224]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	w21, w0
	mrs	x0, sp_el0
	stp	x23, x24, [sp, 48]
	mov	w20, w1
	ldrh	w22, [x19, 312]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	lsr	w22, w21, w22
	ldr	x1, [x0, 1376]
	str	x1, [sp, 216]
	mov	x1, 0
	ldrh	w0, [x19, 300]
	add	w1, w21, w20
	cmp	w1, w0
	bhi	.L1294
	mov	x24, x2
	add	x27, x19, 3880
	mov	w25, 0
	mov	w28, -1
.L1287:
	cbnz	w20, .L1292
.L1285:
	mrs	x0, sp_el0
	ldr	x1, [sp, 216]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1293
	bl	__stack_chk_fail
.L1292:
	ldrh	w1, [x19, 262]
	ldr	x0, [x19, 3592]
	udiv	w23, w21, w1
	ldr	w2, [x0, w22, uxtw 2]
	msub	w23, w23, w1, w21
	sub	w0, w1, w23
	and	w26, w0, 65535
	cmp	w20, w0, uxth
	bcs	.L1288
	and	w26, w20, 65535
.L1288:
	ldr	x0, [x19, 3464]
	cbz	w2, .L1289
	cmp	w1, w26
	beq	.L1289
	str	x0, [sp, 128]
	add	x0, sp, 152
	str	w2, [sp, 124]
	mov	w2, 1
	mov	w1, w2
	str	x0, [sp, 136]
	add	x0, sp, 120
	bl	FlashReadPages
.L1290:
	lsl	w4, w26, 9
	sbfiz	x23, x23, 9, 23
	ldr	x0, [x19, 3464]
	mov	w2, w4
	mov	x1, x24
	str	w4, [sp, 108]
	add	x0, x0, x23
	bl	ftl_memcpy
	ldr	x2, [x19, 3464]
	mov	w1, w22
	mov	x0, x27
	sub	w20, w20, w26
	add	w21, w21, w26
	add	w22, w22, 1
	bl	FtlMapWritePage
	ldr	w4, [sp, 108]
	cmn	w0, #1
	csel	w25, w25, w28, ne
	add	x24, x24, x4, sxtw
	b	.L1287
.L1289:
	ldrh	w2, [x19, 314]
	mov	w1, 0
	bl	ftl_memset
	b	.L1290
.L1294:
	mov	w25, -1
	b	.L1285
.L1293:
	mov	w0, w25
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	hint	29 // autiasp
	ret
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE103
	.text
.LPFE103:
	nop
	nop
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w3, [x0, 2712]
	cmp	w3, w1
	bne	.L1302
	add	x2, x0, 2712
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	w1, 86
	movk	w1, 0x5000, lsl 16
	mov	x29, sp
	str	w1, [x2, 4]
	ldr	w1, [x0, 2652]
	str	w1, [x2, 88]
	ldr	w1, [x0, 2656]
	str	w1, [x2, 92]
	ldr	w1, [x0, 2648]
	str	w1, [x2, 8]
	ldr	w1, [x0, 2636]
	str	w1, [x2, 12]
	ldr	w1, [x0, 2628]
	str	w1, [x2, 16]
	ldr	w1, [x0, 2644]
	str	w1, [x2, 20]
	ldr	w1, [x0, 2672]
	str	w1, [x2, 28]
	ldr	w1, [x0, 2680]
	str	w1, [x2, 32]
	ldr	w1, [x0, 2632]
	str	w1, [x2, 36]
	ldr	w1, [x0, 2640]
	str	w1, [x2, 40]
	ldr	w1, [x0, 2684]
	ldr	w0, [x0, 2688]
	stp	w1, w0, [x2, 44]
	mov	w1, 1
	mov	w0, 0
	bl	FtlVendorPartWrite
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
.L1302:
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE104
	.text
.LPFE104:
	nop
	nop
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	ldrh	w1, [x2, 3944]
	cmp	w1, 31
	bhi	.L1309
	add	w1, w1, 1
	mov	w3, 1
	strh	w1, [x2, 3944]
.L1306:
	ldr	x1, [x2, 3544]
	ldr	w4, [x1, 16]
	cbnz	w0, .L1307
	ldr	w0, [x1, 20]
	add	w3, w3, w0
	cmp	w3, w4
	bhi	.L1311
.L1307:
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	w0, 17221
	mov	x29, sp
	movk	w0, 0x4254, lsl 16
	str	w0, [x1]
	str	w4, [x1, 20]
	ldr	x0, [x2, 3544]
	ldrh	w1, [x2, 3528]
	str	wzr, [x0, 4]
	lsl	w3, w1, 9
	str	w3, [x0, 12]
	ldr	w3, [x0, 8]
	ldr	x2, [x2, 3544]
	add	w3, w3, 1
	str	w3, [x0, 8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
.L1309:
	mov	w3, 32
	b	.L1306
.L1311:
	mov	w0, 0
	ret
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	sftl_vendor_write
	.type	sftl_vendor_write, %function
sftl_vendor_write:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE105
	.text
.LPFE105:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	add	w0, w0, 256
	mov	x29, sp
	bl	FtlVendorPartWrite
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	sftl_vendor_write, .-sftl_vendor_write
	.section	.rodata.str1.1
.LC119:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
	.text
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE106
	.text
.LPFE106:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -224]!
	adrp	x3, .LANCHOR0
	mov	x29, sp
	stp	x27, x28, [sp, 80]
	add	x28, x3, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	w20, w0
	mrs	x0, sp_el0
	stp	x21, x22, [sp, 32]
	mov	w19, w1
	ldrh	w21, [x28, 312]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	lsr	w21, w20, w21
	ldr	x1, [x0, 1376]
	str	x1, [sp, 216]
	mov	x1, 0
	ldrh	w0, [x28, 300]
	add	w1, w20, w19
	cmp	w1, w0
	bhi	.L1325
	adrp	x26, .LC119
	mov	x22, x2
	add	x26, x26, :lo12:.LC119
	mov	w24, 0
.L1317:
	cbnz	w19, .L1323
.L1315:
	mrs	x0, sp_el0
	ldr	x1, [sp, 216]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1324
	bl	__stack_chk_fail
.L1323:
	ldr	x0, [x28, 3592]
	ldr	w4, [x0, w21, uxtw 2]
	ldrh	w0, [x28, 262]
	udiv	w23, w20, w0
	msub	w23, w23, w0, w20
	sub	w0, w0, w23
	and	w25, w0, 65535
	cmp	w19, w0, uxth
	bcs	.L1318
	and	w25, w19, 65535
.L1318:
	lsl	w27, w25, 9
	cbz	w4, .L1319
	ldr	x0, [x28, 3464]
	str	x0, [sp, 128]
	add	x0, sp, 152
	mov	w2, 1
	mov	w1, w2
	str	w4, [sp, 108]
	str	w4, [sp, 124]
	str	x0, [sp, 136]
	add	x0, sp, 120
	bl	FlashReadPages
	ldr	w0, [sp, 120]
	ldr	w4, [sp, 108]
	cmn	w0, #1
	ldr	w0, [x28, 3712]
	csinv	w24, w24, wzr, ne
	cmp	w0, 256
	bne	.L1321
	mov	w2, w4
	mov	w1, w21
	mov	x0, x26
	bl	sftl_printk
	ldr	x2, [x28, 3464]
	mov	w1, w21
	add	x0, x28, 3880
	bl	FtlMapWritePage
.L1321:
	ldr	x1, [x28, 3464]
	sbfiz	x23, x23, 9, 23
	mov	w2, w27
	mov	x0, x22
	add	x1, x1, x23
	bl	ftl_memcpy
.L1322:
	add	w21, w21, 1
	sub	w19, w19, w25
	add	w20, w20, w25
	add	x22, x22, x27, sxtw
	b	.L1317
.L1319:
	mov	w2, w27
	mov	x0, x22
	mov	w1, 0
	bl	ftl_memset
	b	.L1322
.L1325:
	mov	w24, -1
	b	.L1315
.L1324:
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	hint	29 // autiasp
	ret
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.section	.rodata.str1.1
.LC120:
	.string	"no ect"
	.text
	.align	2
	.global	FtlLoadEctTbl
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE107
	.text
.LPFE107:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	w0, 64
	mov	x29, sp
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19, 3528]
	ldr	x2, [x19, 3544]
	bl	FtlVendorPartRead
	ldr	x0, [x19, 3544]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1328
	adrp	x1, .LC120
	adrp	x0, .LC78
	add	x1, x1, :lo12:.LC120
	add	x0, x0, :lo12:.LC78
	bl	sftl_printk
	ldr	x0, [x19, 3544]
	mov	w1, 0
	ldrh	w2, [x19, 3528]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1328:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE108
	.text
.LPFE108:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	w1, 1
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	add	x21, x19, 2712
	mov	x2, x21
	mov	w0, 0
	bl	FtlVendorPartRead
	mov	w20, 19539
	ldr	w0, [x19, 2712]
	movk	w20, 0x4654, lsl 16
	cmp	w0, w20
	beq	.L1331
	mov	x0, x21
	mov	w2, 512
	mov	w1, 0
	bl	ftl_memset
	str	w20, [x19, 2712]
.L1331:
	ldr	w1, [x19, 2800]
	str	w1, [x19, 2652]
	ldr	w1, [x19, 2804]
	ldr	w2, [x19, 2744]
	str	w1, [x19, 2656]
	ldr	w1, [x19, 2720]
	str	w1, [x19, 2648]
	ldr	w1, [x19, 2724]
	str	w2, [x19, 2680]
	ldr	w2, [x19, 2748]
	str	w2, [x19, 2632]
	ldr	w2, [x19, 2752]
	str	w1, [x19, 2636]
	ldr	w1, [x19, 2728]
	ldr	w0, [x19, 2760]
	str	w1, [x19, 2628]
	ldr	w1, [x19, 2732]
	str	w2, [x19, 2640]
	ldr	w2, [x19, 2756]
	str	w1, [x19, 2644]
	ldr	w1, [x19, 2740]
	str	w2, [x19, 2684]
	ldr	w2, [x19, 2668]
	str	w0, [x19, 2688]
	ldrh	w0, [x19, 296]
	str	w1, [x19, 2672]
	ldr	x21, [sp, 32]
	madd	w0, w0, w2, w1
	ldrh	w1, [x19, 244]
	udiv	w0, w0, w1
	str	w0, [x19, 2676]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	sftl_vendor_read
	.type	sftl_vendor_read, %function
sftl_vendor_read:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE109
	.text
.LPFE109:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	add	w0, w0, 256
	mov	x29, sp
	bl	FtlVendorPartRead
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	sftl_vendor_read, .-sftl_vendor_read
	.align	2
	.global	FtlMapBlkWriteDump_data
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE110
	.text
.LPFE110:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	ldr	w0, [x0, 56]
	cbz	w0, .L1335
	ldrh	w1, [x19, 6]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x19, 56]
	sub	w1, w1, #1
	add	x0, x20, 3712
	ldr	x2, [x19, 40]
	and	w21, w1, 65535
	ubfiz	x1, x1, 2, 16
	ldr	x3, [x20, 3456]
	ldr	x4, [x20, 3512]
	stp	x3, x4, [x0, 8]
	ldr	w1, [x2, x1]
	str	w1, [x0, 4]
	cbz	w1, .L1337
	mov	w2, 1
	mov	w1, w2
	bl	FlashReadPages
.L1338:
	ldr	x2, [x20, 3720]
	mov	w1, w21
	mov	x0, x19
	bl	FtlMapWritePage
.L1335:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L1337:
	ldrh	w2, [x20, 314]
	mov	x0, x3
	mov	w1, 255
	bl	ftl_memset
	b	.L1338
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.section	.rodata.str1.1
.LC121:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
	.text
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE111
	.text
.LPFE111:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x21, x19, 2560
	add	x20, x19, 2512
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR1
	str	x25, [sp, 64]
	add	x23, x23, 471
	ldr	x22, [x19, 3512]
	str	x22, [x19, 3728]
	ldr	x0, [x19, 3448]
	str	x0, [x19, 3720]
	ldrh	w0, [x21, 136]
	mov	w24, 65535
	strh	w0, [x22, 2]
	mov	w0, -3932
	strh	w0, [x22]
	ldr	w0, [x21, 144]
	str	w0, [x22, 4]
	mov	x0, 19539
	str	xzr, [x22, 8]
	movk	x0, 0x4654, lsl 16
	ldrb	w1, [x19, 30]
	movk	x0, 0x56, lsl 32
	movk	x0, 0x5000, lsl 48
	str	x0, [x19, 2512]
	ldrh	w0, [x21, 142]
	strh	w0, [x19, 2520]
	ldrh	w0, [x19, 258]
	strb	w0, [x19, 2522]
	ldrh	w0, [x19, 24]
	strh	w0, [x19, 2526]
	ldrh	w0, [x19, 26]
	orr	w0, w1, w0, lsl 6
	strh	w0, [x19, 2528]
	ldrb	w0, [x19, 32]
	strb	w0, [x19, 2523]
	ldrh	w0, [x19, 80]
	ldrb	w1, [x19, 86]
	strh	w0, [x19, 2530]
	ldrh	w0, [x19, 82]
	ldrh	w2, [x19, 314]
	orr	w0, w1, w0, lsl 6
	strh	w0, [x19, 2532]
	ldrb	w0, [x19, 88]
	strb	w0, [x19, 2524]
	ldrh	w0, [x19, 128]
	ldrb	w1, [x19, 134]
	strh	w0, [x19, 2534]
	ldrh	w0, [x19, 130]
	orr	w0, w1, w0, lsl 6
	strh	w0, [x19, 2536]
	ldrb	w0, [x19, 136]
	mov	w1, 255
	strb	w0, [x19, 2525]
	ldr	w0, [x19, 2668]
	str	w0, [x19, 2544]
	ldr	w0, [x19, 2660]
	str	w0, [x19, 2552]
	ldr	w0, [x19, 2664]
	str	w0, [x19, 2548]
	ldr	x0, [x19, 3720]
	bl	ftl_memset
	ldr	x0, [x19, 3720]
	mov	x1, x20
	mov	w2, 48
	mov	w20, 0
	bl	ftl_memcpy
	ldrh	w2, [x19, 244]
	ldr	x1, [x19, 72]
	ldr	x0, [x19, 3720]
	lsl	w2, w2, 1
	add	x0, x0, 48
	bl	ftl_memcpy
	mov	w0, 0
	bl	FtlUpdateVaildLpn
.L1344:
	ldrh	w1, [x21, 136]
	ldrh	w2, [x21, 138]
	ldr	x0, [x19, 3448]
	str	x0, [x19, 3720]
	ldr	x0, [x19, 3512]
	str	x0, [x19, 3728]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19, 3716]
	ldrh	w0, [x19, 308]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L1345
	ldrh	w24, [x21, 140]
	strh	wzr, [x21, 138]
	strh	w1, [x21, 140]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19, 2660]
	str	w1, [x21, 144]
	add	w2, w1, 1
	str	w2, [x19, 2660]
	ubfiz	w2, w0, 10, 16
	str	w2, [x19, 3716]
	strh	w0, [x22, 2]
	str	w1, [x22, 4]
	strh	w0, [x21, 136]
.L1345:
	ldrh	w1, [x19, 314]
	ldr	x0, [x19, 3448]
	bl	js_hash
	str	w0, [x22, 12]
	mov	w3, 1
	add	x0, x19, 3712
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
	ldrh	w0, [x21, 138]
	ldr	w1, [x19, 3712]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x21, 138]
	cmn	w1, #1
	bne	.L1346
	cmp	w0, 1
	bne	.L1348
	mov	x1, x23
	mov	w2, 1145
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	bl	sftl_printk
	ldrh	w0, [x21, 138]
	cmp	w0, 1
	bne	.L1348
	ldrh	w0, [x19, 308]
	sub	w0, w0, #1
	strh	w0, [x21, 138]
.L1348:
	add	w20, w20, 1
	and	w20, w20, 65535
	cmp	w20, 3
	bls	.L1344
	ldr	w1, [x19, 3716]
	adrp	x0, .LC121
	mov	w2, w20
	add	x0, x0, :lo12:.LC121
	bl	sftl_printk
.L1351:
	b	.L1351
.L1346:
	cmp	w0, 1
	beq	.L1344
	cmp	w1, 256
	beq	.L1344
	mov	w0, 65535
	cmp	w24, w0
	beq	.L1352
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1352:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE112
	.text
.LPFE112:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	sftl_deinit
	.type	sftl_deinit, %function
sftl_deinit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE113
	.text
.LPFE113:
	nop
	nop
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	cmp	w0, 1
	bne	.L1365
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	bl	FtlSysFlush
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
.L1365:
	mov	w0, 0
	ret
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	sftl_discard
	.type	sftl_discard, %function
sftl_discard:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE114
	.text
.LPFE114:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -64]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	mov	w20, w1
	mrs	x1, sp_el0
	ldr	x2, [x1, 1376]
	str	x2, [sp, 56]
	mov	x2, 0
	ldr	w1, [x19, 344]
	cmp	w1, w0
	bls	.L1379
	cmp	w1, w20
	bcc	.L1379
	add	w2, w0, w20
	cmp	w1, w2
	bcc	.L1379
	cmp	w20, 31
	bhi	.L1370
.L1387:
	mov	w0, 0
.L1368:
	mrs	x1, sp_el0
	ldr	x2, [sp, 56]
	ldr	x3, [x1, 1376]
	subs	x2, x2, x3
	mov	x3, 0
	beq	.L1376
	bl	__stack_chk_fail
.L1370:
	ldrh	w1, [x19, 262]
	udiv	w21, w0, w1
	msub	w0, w1, w21, w0
	ands	w0, w0, 65535
	beq	.L1371
	sub	w0, w1, w0
	add	w21, w21, 1
	cmp	w0, w20
	csel	w0, w0, w20, ls
	sub	w20, w20, w0, uxth
.L1371:
	mov	w0, -1
	str	w0, [sp, 52]
.L1372:
	ldrh	w0, [x19, 262]
	cmp	w0, w20
	bls	.L1374
	ldr	w0, [x19, 3948]
	cmp	w0, 32
	bls	.L1387
	str	wzr, [x19, 3948]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1387
.L1374:
	add	x1, sp, 48
	mov	w0, w21
	mov	w2, 0
	bl	log2phys
	ldr	w0, [sp, 48]
	cmn	w0, #1
	beq	.L1373
	ldr	w0, [x19, 3948]
	add	x1, sp, 52
	mov	w2, 1
	add	w0, w0, 1
	str	w0, [x19, 3948]
	ldr	w0, [x19, 2632]
	add	w0, w0, 1
	str	w0, [x19, 2632]
	mov	w0, w21
	bl	log2phys
	ldr	w0, [sp, 48]
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1373:
	ldrh	w0, [x19, 262]
	add	w21, w21, 1
	sub	w20, w20, w0
	b	.L1372
.L1379:
	mov	w0, -1
	b	.L1368
.L1376:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 64
	hint	29 // autiasp
	ret
	.size	sftl_discard, .-sftl_discard
	.section	.rodata.str1.1
.LC122:
	.string	"FtlCheckVpc %x = %x  %x\n"
	.text
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE115
	.text
.LPFE115:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -96]!
	mrs	x0, sp_el0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR1
	add	x22, x22, :lo12:.LANCHOR1
	stp	x23, x24, [sp, 48]
	mov	w20, 0
	str	x25, [sp, 64]
	ldr	x1, [x0, 1376]
	str	x1, [sp, 88]
	mov	x1, 0
	adrp	x0, .LC112
	add	x1, x22, 486
	add	x0, x0, :lo12:.LC112
	bl	sftl_printk
	ldr	x0, [x19, 3552]
	mov	w1, 0
	ldrh	w2, [x19, 246]
	lsl	w2, w2, 1
	bl	ftl_memset
.L1389:
	ldr	w0, [x19, 2624]
	cmp	w0, w20
	bhi	.L1391
	adrp	x23, .LC122
	adrp	x24, .LC9
	add	x23, x23, :lo12:.LC122
	add	x24, x24, :lo12:.LC9
	mov	w20, 0
	mov	w25, 65535
.L1392:
	ldrh	w0, [x19, 244]
	cmp	w0, w20
	bhi	.L1398
	bl	l2p_flush
	bl	FtlVpcTblFlush
	mrs	x0, sp_el0
	ldr	x1, [sp, 88]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1399
	bl	__stack_chk_fail
.L1391:
	add	x1, sp, 84
	mov	w0, w20
	mov	w2, 0
	bl	log2phys
	ldr	w0, [sp, 84]
	cmn	w0, #1
	beq	.L1390
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	ldr	x2, [x19, 3552]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L1390:
	add	w20, w20, 1
	b	.L1389
.L1398:
	ldr	x0, [x19, 72]
	ubfiz	x21, x20, 1, 16
	ldrh	w2, [x0, x21]
	ldr	x0, [x19, 3552]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L1394
	cmp	w2, w25
	beq	.L1394
	mov	x0, x23
	mov	w1, w20
	bl	sftl_printk
	ldrh	w0, [x19, 24]
	cmp	w0, w20
	beq	.L1394
	ldrh	w0, [x19, 128]
	cmp	w0, w20
	beq	.L1394
	ldrh	w0, [x19, 80]
	cmp	w0, w20
	beq	.L1394
	ldr	x0, [x19, 72]
	ldr	x1, [x19, 3552]
	ldrh	w2, [x0, x21]
	ldrh	w1, [x1, x21]
	strh	w1, [x0, x21]
	cbnz	w2, .L1396
	mov	w1, w20
	add	x0, x19, 2592
	bl	List_remove_node
	ldrh	w0, [x19, 228]
	cbnz	w0, .L1397
	add	x1, x22, 486
	mov	x0, x24
	mov	w2, 2330
	bl	sftl_printk
.L1397:
	ldrh	w0, [x19, 228]
	sub	w0, w0, #1
	strh	w0, [x19, 228]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L1394:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1392
.L1396:
	mov	w0, w20
	bl	update_vpc_list
	b	.L1394
.L1399:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 96
	hint	29 // autiasp
	ret
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE116
	.text
.LPFE116:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	mov	x21, x0
	ldrh	w19, [x0]
	ldrh	w0, [x20, 244]
	cmp	w0, w19
	bcs	.L1405
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 507
	add	x0, x0, :lo12:.LC9
	mov	w2, 2766
	bl	sftl_printk
.L1405:
	mov	w0, 65535
	cmp	w19, w0
	beq	.L1406
	ldr	x1, [x20, 72]
	ubfiz	x0, x19, 1, 16
	ldrh	w0, [x1, x0]
	cbz	w0, .L1407
	mov	w0, w19
	bl	INSERT_DATA_LIST
.L1406:
	mov	w0, 1
	strb	w0, [x21, 8]
	ldrh	w0, [x20, 3698]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1408
	cmp	w0, w19
	bne	.L1409
	ldr	x1, [x20, 72]
	ubfiz	x19, x19, 1, 16
	ldrh	w1, [x1, x19]
	cbz	w1, .L1410
.L1409:
	bl	update_vpc_list
.L1410:
	mov	w0, -1
	strh	w0, [x20, 3698]
.L1408:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L1407:
	mov	w0, w19
	bl	INSERT_FREE_LIST
	b	.L1406
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.section	.rodata.str1.1
.LC123:
	.string	"FtlProgPages error %x = %d\n"
.LC124:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
	.text
	.align	2
	.global	FtlProgPages
	.type	FtlProgPages, %function
FtlProgPages:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE117
	.text
.LPFE117:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mrs	x2, sp_el0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x20, x3
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	mov	w21, w1
	stp	x23, x24, [sp, 48]
	adrp	x24, .LC123
	adrp	x23, .LANCHOR0
	ldr	x3, [x2, 1376]
	str	x3, [sp, 72]
	mov	x3, 0
	mov	w2, 0
	ldrb	w3, [x20, 9]
	add	x24, x24, :lo12:.LC123
	add	x23, x23, :lo12:.LANCHOR0
	bl	FlashProgPages
	mov	w1, 32
	umaddl	x21, w21, w1, x19
.L1416:
	cmp	x21, x19
	bne	.L1423
	adrp	x0, .LANCHOR0+236
	ldrb	w1, [x20, 6]
	ldrh	w0, [x0, #:lo12:.LANCHOR0+236]
	cmp	w1, w0
	bcc	.L1415
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 536
	add	x0, x0, :lo12:.LC9
	mov	w2, 1000
	bl	sftl_printk
.L1415:
	mrs	x0, sp_el0
	ldr	x1, [sp, 72]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1425
	bl	__stack_chk_fail
.L1418:
	ldr	w1, [x19, 4]
	mov	x0, x24
	bl	sftl_printk
	ldr	w0, [x19, 4]
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1417
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1417:
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x19, 4]
	mov	w2, 0
	mov	w1, 1
	str	w0, [sp, 68]
	ldrb	w3, [x20, 9]
	mov	x0, x19
	bl	FlashProgPages
.L1423:
	ldr	w2, [x19]
	cmp	w2, 256
	ccmn	w2, #1, 4, ne
	beq	.L1418
	ldrb	w1, [x20, 6]
	ldrh	w0, [x23, 236]
	cmp	w1, w0
	bcc	.L1419
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 536
	add	x0, x0, :lo12:.LC9
	mov	w2, 985
	bl	sftl_printk
.L1419:
	ldr	w0, [x19, 4]
	add	x1, sp, 68
	str	w0, [sp, 68]
	mov	w2, 1
	ldr	w0, [x19, 24]
	bl	log2phys
	ldr	x0, [x19, 16]
	ldr	w0, [x0, 12]
	cmn	w0, #1
	beq	.L1420
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	and	w1, w0, 65535
	mov	w22, w1
	ldr	x2, [x23, 72]
	ubfiz	x0, x22, 1, 16
	ldrh	w0, [x2, x0]
	cbnz	w0, .L1421
	adrp	x0, .LC124
	mov	w2, 0
	add	x0, x0, :lo12:.LC124
	bl	sftl_printk
.L1421:
	mov	w0, w22
	bl	decrement_vpc_count
.L1420:
	add	x19, x19, 32
	b	.L1416
.L1425:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
	.size	FtlProgPages, .-FtlProgPages
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE118
	.text
.LPFE118:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mrs	x1, sp_el0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	ldrh	w3, [x19, 128]
	ldr	x2, [x1, 1376]
	str	x2, [sp, 72]
	mov	x2, 0
	ldrh	w1, [x19, 306]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L1431
.L1438:
	ldrh	w2, [x19, 128]
	add	x0, x19, 128
	str	wzr, [x19, 3704]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L1452
.L1456:
	mov	w0, 0
.L1430:
	mrs	x1, sp_el0
	ldr	x2, [sp, 72]
	ldr	x3, [x1, 1376]
	subs	x2, x2, x3
	mov	x3, 0
	beq	.L1447
	bl	__stack_chk_fail
.L1431:
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	cbz	w0, .L1434
	ldrh	w0, [x20, 4]
	cmp	w0, w2
	beq	.L1435
.L1436:
	mov	w1, 2
.L1434:
	add	x0, x19, 128
	bl	FtlGcScanTempBlk
	ldrh	w1, [x20, 4]
	str	w0, [sp, 68]
	mov	w0, 65535
	cmp	w1, w0
	beq	.L1438
	mov	w0, 1
	b	.L1430
.L1435:
	ldrh	w0, [x19, 228]
	strh	wzr, [x20, 4]
	cmp	w0, 17
	bhi	.L1436
	b	.L1434
.L1452:
	ldrb	w0, [x0, 7]
	ldrh	w2, [x19, 306]
	ldrh	w1, [x19, 3296]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L1439
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 549
	add	x0, x0, :lo12:.LC9
	mov	w2, 164
	bl	sftl_printk
.L1439:
	ldrb	w0, [x19, 135]
	mov	w20, 0
	ldrh	w3, [x19, 306]
	mov	w24, 12
	ldrh	w2, [x19, 128]
	ldr	x1, [x19, 72]
	mul	w0, w0, w3
	strh	w0, [x1, x2, lsl 1]
	ldr	w1, [x19, 2648]
	ldrh	w0, [x19, 3296]
	add	w0, w0, w1
	str	w0, [x19, 2648]
.L1440:
	ldrh	w0, [x19, 3296]
	cmp	w0, w20
	bhi	.L1444
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w2, [x19, 128]
	ldr	x1, [x19, 72]
	ldrh	w0, [x19, 128]
	ldrh	w1, [x1, x2, lsl 1]
	cbz	w1, .L1445
	bl	INSERT_DATA_LIST
.L1446:
	mov	w20, -1
	strh	w20, [x19, 128]
	strh	wzr, [x19, 3284]
	strh	wzr, [x19, 3296]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w20, [x19, 176]
	ldrh	w0, [x19, 2692]
	ldrh	w1, [x19, 228]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	ble	.L1456
	mov	w0, 20
	strh	w0, [x19, 3224]
	b	.L1456
.L1444:
	ldr	x23, [x19, 3304]
	umull	x22, w20, w24
	ldr	w1, [x19, 2624]
	add	x21, x23, x22
	ldr	w0, [x21, 8]
	cmp	w0, w1
	bcc	.L1441
.L1454:
	ldrh	w0, [x19, 128]
	b	.L1455
.L1441:
	add	x1, sp, 68
	mov	w2, 0
	bl	log2phys
	ldr	w1, [sp, 68]
	ldr	w0, [x23, x22]
	cmp	w0, w1
	bne	.L1443
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	mov	w22, w0
	ldr	w0, [x21, 8]
	add	x1, x21, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w22
.L1455:
	bl	decrement_vpc_count
	b	.L1442
.L1443:
	ldr	w0, [x21, 4]
	cmp	w1, w0
	bne	.L1454
.L1442:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1440
.L1445:
	bl	INSERT_FREE_LIST
	b	.L1446
.L1447:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE119
	.text
.LPFE119:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 128
	mov	x0, x20
	ldrh	w1, [x19, 306]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19, 130]
	ldrh	w0, [x19, 306]
	cmp	w1, w0
	bcc	.L1457
	add	x0, x19, 3632
	bl	FtlMapBlkWriteDump_data
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	str	wzr, [x19, 3704]
.L1457:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE120
	.text
.LPFE120:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 24
	add	x19, x19, 80
	mov	x0, x20
	str	wzr, [x19, 3668]
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
	hint	29 // autiasp
	ret
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE121
	.text
.LPFE121:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	w3, 0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w20, 0
	ldr	w1, [x19, 3240]
	ldr	x0, [x19, 3432]
	bl	FlashProgPages
.L1463:
	ldr	w1, [x19, 3240]
	ldr	x2, [x19, 3432]
	cmp	w20, w1
	bcc	.L1466
	mov	x0, x2
	bl	FtlGcBufFree
	ldrh	w0, [x19, 132]
	str	wzr, [x19, 3240]
	cbnz	w0, .L1468
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w0, 1
.L1462:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
.L1466:
	ubfiz	x4, x20, 5, 16
	add	x1, x2, x4
	ldr	w2, [x2, x4]
	ldr	x3, [x1, 16]
	ldr	w1, [x1, 4]
	ldr	w0, [x3, 12]
	cmn	w2, #1
	bne	.L1464
.L1470:
	add	w20, w20, 1
	bl	FtlGcUpdatePage
	and	w20, w20, 65535
	b	.L1463
.L1464:
	ldr	w2, [x3, 8]
	b	.L1470
.L1468:
	mov	w0, 0
	b	.L1462
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE122
	.text
.LPFE122:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	mov	w1, 65535
	mov	x29, sp
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w2, [x19, 128]
	cmp	w2, w1
	beq	.L1472
	ldrh	w0, [x19, 132]
	cbnz	w0, .L1473
.L1472:
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x19, 128
	strb	wzr, [x0, 8]
	bl	allocate_data_superblock
	strh	wzr, [x19, 3284]
	strh	wzr, [x19, 3296]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1473:
	add	x0, x19, 128
	bl	get_new_active_ppa
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.section	.rodata.str1.1
.LC125:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
	.text
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE123
	.text
.LPFE123:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -144]!
	adrp	x6, .LANCHOR0
	mov	x29, sp
	stp	x23, x24, [sp, 48]
	add	x23, x6, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	str	w0, [sp, 108]
	mrs	x0, sp_el0
	ldr	x1, [x0, 1376]
	str	x1, [sp, 136]
	mov	x1, 0
	ldr	w0, [x23, 3408]
	cbnz	w0, .L1552
	ldrh	w0, [x23, 2584]
	cmp	w0, 47
	bls	.L1552
	adrp	x0, .LANCHOR2+4
	ldrh	w1, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1477
.L1480:
	ldrh	w0, [x23, 224]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1478
.L1479:
	bl	FtlReadRefresh
	ldr	w1, [sp, 108]
	ldr	w0, [x23, 3228]
	mov	w19, 65535
	ldrh	w2, [x23, 128]
	add	w0, w0, 1
	add	w0, w0, w1, lsl 7
	ldrh	w1, [x23, 176]
	str	w0, [x23, 3228]
	and	w1, w1, w2
	ldrh	w2, [x23, 226]
	and	w1, w1, w2
	and	w20, w1, 65535
	cmp	w19, w1, uxth
	bne	.L1554
	ldrh	w1, [x23, 228]
	mov	w2, 1024
	cmp	w1, 24
	mov	w1, 5120
	csel	w1, w1, w2, cc
	cmp	w0, w1
	bls	.L1481
	str	wzr, [x23, 3228]
	strh	wzr, [x23, 3952]
	bl	GetSwlReplaceBlock
	and	w20, w0, 65535
	cmp	w19, w0, uxth
	bne	.L1483
	ldrh	w1, [x23, 228]
	ldrh	w0, [x23, 3226]
	cmp	w1, w0
	bcs	.L1484
	mov	w0, 64
	bl	List_get_gc_head_node
	and	w1, w0, 65535
	cmp	w20, w0, uxth
	beq	.L1486
	ldr	x0, [x23, 72]
	ubfiz	x1, x1, 1, 16
	ldrh	w0, [x0, x1]
	cmp	w0, 7
	bhi	.L1487
	mov	w0, 0
	bl	List_get_gc_head_node
	mov	w2, 128
	strh	w2, [x23, 3226]
	and	w1, w0, 65535
	cmp	w20, w0, uxth
	beq	.L1486
	mov	w20, w1
.L1483:
	ldr	x1, [x23, 72]
	ubfiz	x0, x20, 1, 16
	ldr	x2, [x23, 2600]
	ldrh	w3, [x1, x0]
	mov	w1, w20
	ldrh	w5, [x23, 3224]
	ldrh	w4, [x2, x0]
	adrp	x0, .LC125
	ldrh	w2, [x23, 228]
	add	x0, x0, :lo12:.LC125
	bl	sftl_printk
	b	.L1486
.L1477:
	ldrh	w1, [x23, 128]
	cmp	w1, w0
	beq	.L1480
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	cbz	w0, .L1480
	mov	w0, 1
	b	.L1475
.L1478:
	ldrh	w2, [x23, 226]
	cmp	w2, w1
	bne	.L1479
	strh	w0, [x23, 226]
	mov	w0, -1
	strh	w0, [x23, 224]
	b	.L1479
.L1487:
	mov	w0, 64
.L1582:
	strh	w0, [x23, 3226]
.L1486:
	bl	FtlGcReFreshBadBlk
.L1481:
	ldr	w1, [sp, 108]
	mov	w0, 65535
	cmp	w1, 0
	ccmp	w20, w0, 0, eq
	bne	.L1489
	ldrh	w0, [x23, 228]
	cmp	w0, 24
	bhi	.L1557
	ldrh	w19, [x23, 306]
	cmp	w0, 16
	bls	.L1491
	lsr	w19, w19, 5
.L1490:
	ldrh	w1, [x23, 3224]
	cmp	w1, w0
	bcs	.L1493
	ldrh	w1, [x23, 226]
	ldrh	w0, [x23, 128]
	and	w0, w0, w1
	mov	w1, 65535
	cmp	w1, w0, uxth
	bne	.L1494
	ldrh	w0, [x23, 3952]
	cbnz	w0, .L1495
	ldr	w1, [x23, 2624]
	ldr	w2, [x23, 2620]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L1558
.L1495:
	ldrh	w1, [x23, 2692]
	add	w1, w1, w1, lsl 1
	lsr	w1, w1, 2
.L1496:
	strh	w1, [x23, 3224]
	str	wzr, [x23, 3236]
.L1475:
	mrs	x1, sp_el0
	ldr	x2, [sp, 136]
	ldr	x3, [x1, 1376]
	subs	x2, x2, x3
	mov	x3, 0
	beq	.L1550
	bl	__stack_chk_fail
.L1484:
	mov	w0, 80
	b	.L1582
.L1554:
	mov	w20, w19
	b	.L1481
.L1491:
	cmp	w0, 12
	bls	.L1492
	lsr	w19, w19, 4
	b	.L1490
.L1492:
	cmp	w0, 8
	bls	.L1490
	lsr	w19, w19, 2
	b	.L1490
.L1557:
	mov	w19, 1
	b	.L1490
.L1558:
	mov	w1, 18
	b	.L1496
.L1494:
	ldrh	w0, [x23, 2692]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x23, 3224]
.L1493:
	ldrh	w0, [x23, 3312]
	cbz	w0, .L1559
	add	w19, w19, 32
	and	w19, w19, 65535
.L1559:
	mov	w20, 65535
.L1498:
	ldrh	w0, [x23, 176]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1506
	cmp	w20, w0
	beq	.L1507
	strh	w20, [x23, 176]
.L1508:
	ldrh	w0, [x23, 176]
	add	x21, x23, 176
	strb	wzr, [x21, 8]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1506
	bl	IsBlkInGcList
	cbz	w0, .L1511
	mov	w0, -1
	strh	w0, [x23, 176]
.L1506:
	ldrh	w0, [x23, 176]
	ldrh	w1, [x23, 24]
	cmp	w1, w0
	beq	.L1512
	ldrh	w1, [x23, 80]
	cmp	w1, w0
	beq	.L1512
	ldrh	w1, [x23, 128]
	cmp	w1, w0
	bne	.L1513
.L1512:
	mov	w0, -1
	strh	w0, [x23, 176]
.L1513:
	adrp	x21, .LANCHOR1
	add	x21, x21, :lo12:.LANCHOR1
	add	x21, x21, 568
.L1548:
	ldrh	w24, [x23, 176]
	mov	w0, 65535
	cmp	w24, w0
	bne	.L1514
	mov	w25, 2
	str	wzr, [x23, 3236]
.L1515:
	ldrh	w22, [x23, 3232]
	mov	w0, w22
	bl	List_get_gc_head_node
	and	w26, w0, 65535
	strh	w26, [x23, 176]
	cmp	w26, w24
	bne	.L1516
	mov	w0, 8
	strh	wzr, [x23, 3232]
	b	.L1475
.L1489:
	ldrh	w19, [x23, 128]
	ldrh	w1, [x23, 226]
	and	w19, w19, w1
	and	w19, w20, w19
	cmp	w19, w0
	bne	.L1560
	ldrh	w0, [x23, 176]
	cmp	w0, w19
	beq	.L1499
.L1503:
	mov	w20, w19
.L1560:
	mov	w19, 1
	b	.L1498
.L1499:
	ldrh	w1, [x23, 228]
	ldrh	w0, [x23, 3224]
	str	wzr, [x23, 3236]
	cmp	w1, w0
	bls	.L1500
	ldrh	w0, [x23, 3952]
	cbnz	w0, .L1501
	ldr	w0, [x23, 2624]
	ldr	w1, [x23, 2620]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bcs	.L1561
.L1501:
	ldrh	w0, [x23, 2692]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
.L1502:
	strh	w0, [x23, 3224]
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	ldr	x1, [x23, 72]
	ubfiz	x0, x0, 1, 16
	ldrh	w0, [x1, x0]
	cmp	w0, 4
	bls	.L1500
.L1583:
	ldrh	w0, [x23, 3952]
	b	.L1475
.L1561:
	mov	w0, 18
	b	.L1502
.L1500:
	ldrh	w0, [x23, 3952]
	cbnz	w0, .L1503
	ldrh	w20, [x23, 2692]
	add	w0, w20, w20, lsl 1
	asr	w0, w0, 2
	strh	w0, [x23, 3224]
	mov	w0, 0
	bl	List_get_gc_head_node
	ldr	x1, [x23, 72]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x23, 236]
	ldrh	w1, [x1, x0]
	ldrh	w0, [x23, 308]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L1504
	ldrh	w0, [x23, 228]
	sub	w20, w20, #1
	cmp	w0, w20
	blt	.L1504
	bl	FtlReadRefresh
	b	.L1583
.L1504:
	cbnz	w1, .L1503
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x23, 228]
	add	w0, w0, 1
	b	.L1475
.L1507:
	ldrh	w0, [x23, 226]
	cmp	w0, w20
	beq	.L1508
	ldr	x1, [x23, 72]
	ubfiz	x0, x0, 1, 16
	ldrh	w0, [x1, x0]
	cbnz	w0, .L1509
	mov	w0, -1
	strh	w0, [x23, 226]
.L1509:
	ldrh	w0, [x23, 226]
	strh	w0, [x23, 176]
	mov	w0, -1
	strh	w0, [x23, 226]
	b	.L1508
.L1516:
	mov	w0, w26
	bl	IsBlkInGcList
	add	w1, w22, 1
	and	w1, w1, 65535
	strh	w1, [x23, 3232]
	cbnz	w0, .L1515
	ldrh	w5, [x23, 236]
	ubfiz	x2, x26, 1, 16
	ldrh	w0, [x23, 306]
	ldr	x3, [x23, 72]
	mul	w0, w0, w5
	ldrh	w4, [x3, x2]
	sdiv	w5, w0, w25
	cmp	w4, w5
	bgt	.L1519
	cmp	w1, 48
	bls	.L1520
	cmp	w4, 8
	bls	.L1520
	ldrh	w1, [x23, 3284]
	cmp	w1, 35
	bhi	.L1520
.L1519:
	strh	wzr, [x23, 3232]
.L1520:
	ldrh	w1, [x3, x2]
	cmp	w0, w1
	bgt	.L1521
	cmp	w20, w24
	bne	.L1521
	ldrh	w0, [x23, 3232]
	cmp	w0, 3
	bhi	.L1521
	mov	w0, -1
	strh	w0, [x23, 176]
	strh	wzr, [x23, 3232]
	b	.L1583
.L1521:
	cbnz	w1, .L1522
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x23, 3232]
	add	w0, w0, 1
	strh	w0, [x23, 3232]
	b	.L1515
.L1522:
	ldrh	w0, [x23, 24]
	strb	wzr, [x23, 184]
	cmp	w0, w26
	bne	.L1523
	adrp	x0, .LC9
	mov	x1, x21
	add	x0, x0, :lo12:.LC9
	mov	w2, 717
	bl	sftl_printk
.L1523:
	ldrh	w1, [x23, 176]
	ldrh	w0, [x23, 80]
	cmp	w1, w0
	bne	.L1524
	adrp	x0, .LC9
	mov	x1, x21
	add	x0, x0, :lo12:.LC9
	mov	w2, 718
	bl	sftl_printk
.L1524:
	ldrh	w1, [x23, 176]
	ldrh	w0, [x23, 128]
	cmp	w1, w0
	bne	.L1525
	adrp	x0, .LC9
	mov	x1, x21
	add	x0, x0, :lo12:.LC9
	mov	w2, 719
	bl	sftl_printk
.L1525:
	add	x22, x23, 176
	mov	x0, x22
	bl	make_superblock
	ldrh	w1, [x23, 176]
	ldr	x0, [x23, 72]
	strh	wzr, [x23, 3954]
	ldrh	w0, [x0, x1, lsl 1]
	strh	wzr, [x23, 178]
	strb	wzr, [x23, 182]
	strh	w0, [x23, 3956]
.L1514:
	mov	w0, 1
	str	w0, [x23, 3408]
	ldrh	w0, [x23, 306]
	str	w0, [sp, 104]
	ldr	w0, [sp, 108]
	cbz	w0, .L1526
	ldr	w1, [sp, 104]
	ldrh	w0, [x23, 236]
	ldrh	w2, [x23, 176]
	mul	w0, w0, w1
	ldr	x1, [x23, 72]
	ldrh	w1, [x1, x2, lsl 1]
	sub	w0, w0, w1
	mov	w1, 4
	sdiv	w0, w0, w1
	add	w19, w0, w19
	and	w19, w19, 65535
.L1526:
	ldrh	w0, [x23, 178]
	ldr	w2, [sp, 104]
	add	w1, w0, w19
	cmp	w1, w2
	ble	.L1527
	sub	w0, w2, w0
	and	w19, w0, 65535
.L1527:
	add	x26, x23, 176
	mov	w27, 0
.L1528:
	ldrh	w0, [x26, 2]
	cmp	w19, w27, uxth
	bls	.L1536
	ldrh	w7, [x23, 236]
	add	w1, w0, w27
	mov	x4, x26
	mov	w22, 0
	mov	w2, 0
	mov	w5, 65535
	b	.L1537
.L1530:
	ldrh	w3, [x4, 16]
	cmp	w3, w5
	beq	.L1529
	ubfiz	x8, x22, 5, 16
	add	w22, w22, 1
	add	x0, x0, x8
	and	w22, w22, 65535
	orr	w3, w1, w3, lsl 10
	str	w3, [x0, 4]
.L1529:
	add	w2, w2, 1
	add	x4, x4, 2
	and	w2, w2, 65535
.L1537:
	ldr	x0, [x23, 3272]
	cmp	w2, w7
	bne	.L1530
	ldrb	w2, [x26, 8]
	mov	w1, w22
	mov	x24, 0
	bl	FlashReadPages
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	str	x0, [sp, 112]
.L1531:
	cmp	w22, w24, uxth
	bhi	.L1535
	add	w27, w27, 1
	b	.L1528
.L1535:
	ldr	x0, [x23, 3272]
	lsl	x28, x24, 5
	add	x1, x0, x24, lsl 5
	ldr	w0, [x0, x28]
	cmn	w0, #1
	beq	.L1532
	ldr	x25, [x1, 16]
	mov	w0, 61589
	ldrh	w1, [x25]
	cmp	w1, w0
	bne	.L1532
	ldr	w4, [x25, 8]
	cmn	w4, #1
	bne	.L1533
	ldr	x0, [sp, 112]
	mov	x1, x21
	mov	w2, 753
	str	w4, [sp, 120]
	bl	sftl_printk
	ldr	w4, [sp, 120]
.L1533:
	add	x1, sp, 128
	mov	w2, 0
	mov	w0, w4
	bl	log2phys
	ldr	x0, [x23, 3272]
	ldr	w1, [sp, 128]
	add	x0, x0, x28
	ldr	w2, [x0, 4]
	cmp	w2, w1
	bne	.L1532
	ldrh	w1, [x23, 3954]
	ldr	x2, [x23, 3432]
	add	w1, w1, 1
	strh	w1, [x23, 3954]
	ldr	w1, [x23, 3240]
	ldr	w0, [x0, 24]
	add	x1, x2, x1, lsl 5
	str	x1, [sp, 120]
	str	w0, [x1, 24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [sp, 120]
	str	w0, [x1, 4]
	ldr	w0, [x23, 3240]
	ldr	x1, [x23, 3272]
	ldr	x2, [x23, 3432]
	add	x1, x1, x28
	add	x0, x2, x0, lsl 5
	ldr	x2, [x1, 8]
	str	x2, [x0, 8]
	add	x2, x23, 128
	str	x2, [sp, 120]
	ldr	x1, [x1, 16]
	str	x1, [x0, 16]
	ldr	w0, [sp, 128]
	mov	w1, 1
	str	w0, [x25, 12]
	ldrh	w0, [x23, 128]
	strh	w0, [x25, 2]
	ldr	w0, [x23, 2664]
	str	w0, [x25, 4]
	ldr	w0, [x23, 3240]
	add	w0, w0, 1
	str	w0, [x23, 3240]
	ldr	x0, [x23, 3272]
	add	x0, x0, x28
	bl	FtlGcBufAlloc
	ldr	x2, [sp, 120]
	ldr	w0, [x23, 3240]
	ldrb	w1, [x2, 7]
	cmp	w1, w0
	beq	.L1534
	ldrh	w0, [x2, 4]
	cbnz	w0, .L1532
.L1534:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1532
	mov	w0, 65535
	str	w0, [x23, 176]
	str	wzr, [x23, 3408]
	b	.L1583
.L1532:
	add	x24, x24, 1
	b	.L1531
.L1536:
	add	w19, w19, w0
	ldr	w0, [sp, 104]
	and	w19, w19, 65535
	strh	w19, [x26, 2]
	cmp	w0, w19
	bhi	.L1538
	ldr	w0, [x23, 3240]
	cbz	w0, .L1539
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L1539
	str	wzr, [x23, 3408]
	b	.L1583
.L1539:
	ldrh	w0, [x23, 3954]
	cbnz	w0, .L1540
	ldrh	w1, [x23, 176]
	ldr	x0, [x23, 72]
	ldrh	w0, [x0, x1, lsl 1]
	cbz	w0, .L1540
	mov	w19, 0
	b	.L1541
.L1544:
	add	x1, sp, 132
	mov	w0, w19
	mov	w2, 0
	bl	log2phys
	ldr	w0, [sp, 132]
	cmn	w0, #1
	beq	.L1542
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	ldrh	w1, [x23, 176]
	cmp	w1, w0, uxth
	beq	.L1543
.L1542:
	add	w19, w19, 1
.L1541:
	ldr	w0, [x23, 2624]
	cmp	w19, w0
	bcc	.L1544
.L1546:
	ldrh	w1, [x23, 176]
	ldr	x0, [x23, 72]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x23, 176]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1540:
	mov	w0, -1
	strh	w0, [x23, 176]
.L1538:
	ldrh	w0, [x23, 228]
	str	wzr, [x23, 3408]
	cmp	w0, 2
	bhi	.L1547
	ldrh	w19, [x23, 306]
	b	.L1548
.L1543:
	ldr	w0, [x23, 2624]
	cmp	w19, w0
	bcc	.L1540
	b	.L1546
.L1547:
	ldrh	w1, [x23, 3952]
	cmp	w1, 0
	csinc	w0, w1, w0, ne
	b	.L1475
.L1552:
	mov	w0, 0
	b	.L1475
.L1511:
	mov	x0, x21
	bl	make_superblock
	ldrh	w1, [x23, 176]
	ldr	x0, [x23, 72]
	strh	wzr, [x21, 2]
	strb	wzr, [x21, 6]
	strh	wzr, [x23, 3954]
	ldrh	w0, [x0, x1, lsl 1]
	strh	w0, [x23, 3956]
	b	.L1506
.L1550:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	hint	29 // autiasp
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE124
	.text
.LPFE124:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -160]!
	and	w0, w0, 255
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	mov	x21, x3
	stp	x23, x24, [sp, 48]
	mov	w23, w2
	stp	x25, x26, [sp, 64]
	mov	w26, w1
	mrs	x1, sp_el0
	stp	x27, x28, [sp, 80]
	ldr	x2, [x1, 1376]
	str	x2, [sp, 152]
	mov	x2, 0
	cmp	w0, 16
	bne	.L1585
	mov	x2, x3
	mov	w1, w23
	add	w0, w26, 256
	bl	FtlVendorPartRead
	mov	w19, w0
.L1584:
	mrs	x0, sp_el0
	ldr	x1, [sp, 152]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1607
	bl	__stack_chk_fail
.L1585:
	adrp	x3, .LANCHOR0
	add	x28, x3, :lo12:.LANCHOR0
	ldr	w0, [x28, 344]
	cmp	w0, w26
	bls	.L1610
	cmp	w0, w23
	bcc	.L1610
	add	w1, w26, w23
	str	w1, [sp, 116]
	cmp	w0, w1
	bcc	.L1610
	adrp	x0, .LANCHOR2
	ldr	w19, [x0, #:lo12:.LANCHOR2]
	cmn	w19, #1
	beq	.L1584
	ldrh	w0, [x28, 262]
	mov	w20, 0
	mov	w19, 0
	stp	wzr, wzr, [sp, 108]
	udiv	w1, w26, w0
	stp	w1, wzr, [sp, 100]
	add	w1, w26, w23
	sub	w25, w1, #1
	ldr	w22, [sp, 100]
	udiv	w25, w25, w0
	ldr	w0, [sp, 100]
	sub	w24, w25, w0
	ldr	w0, [x28, 2656]
	add	w24, w24, 1
	add	w0, w0, w23
	str	w0, [x28, 2656]
	ldr	w0, [x28, 2628]
	add	w0, w0, w24
	str	w0, [x28, 2628]
.L1587:
	cbnz	w24, .L1605
	ldrh	w0, [x28, 3312]
	cbnz	w0, .L1606
	ldrh	w0, [x28, 228]
	cmp	w0, 31
	bhi	.L1584
.L1606:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	b	.L1584
.L1605:
	add	x1, sp, 148
	mov	w0, w22
	mov	w2, 0
	bl	log2phys
	ldr	w1, [sp, 148]
	cmn	w1, #1
	bne	.L1620
	mov	w27, 0
	b	.L1588
.L1591:
	madd	w0, w0, w22, w27
	cmp	w26, w0
	bhi	.L1590
	ldr	w1, [sp, 116]
	cmp	w1, w0
	bls	.L1590
	sub	w0, w0, w26
	mov	w2, 512
	mov	w1, 0
	lsl	w0, w0, 9
	add	x0, x21, x0
	bl	ftl_memset
.L1590:
	add	w27, w27, 1
.L1588:
	ldrh	w0, [x28, 262]
	cmp	w0, w27
	bhi	.L1591
.L1592:
	add	w22, w22, 1
	subs	w24, w24, #1
	beq	.L1597
	ldrh	w0, [x28, 236]
	cmp	w20, w0, lsl 2
	bne	.L1587
.L1597:
	cbz	w20, .L1587
	ldr	x0, [x28, 3424]
	mov	w1, w20
	mov	w2, 0
	mov	x27, 0
	bl	FlashReadPages
	ldr	w0, [sp, 112]
	lsl	w0, w0, 9
	str	w0, [sp, 132]
	ldr	w0, [sp, 104]
	lsl	w0, w0, 9
	str	x0, [sp, 120]
	ldr	w0, [sp, 108]
	lsl	w0, w0, 9
	str	w0, [sp, 128]
.L1604:
	ldr	x0, [x28, 3424]
	lsl	x6, x27, 5
	ldr	w2, [sp, 100]
	add	x0, x0, x27, lsl 5
	ldr	w1, [x0, 24]
	cmp	w1, w2
	bne	.L1599
	ldr	x1, [x0, 8]
	ldr	x0, [x28, 3472]
	cmp	x1, x0
	bne	.L1600
	ldr	x0, [sp, 120]
	str	x6, [sp, 136]
	ldr	w2, [sp, 128]
	add	x1, x1, x0
	mov	x0, x21
.L1623:
	bl	ftl_memcpy
	ldr	x6, [sp, 136]
.L1600:
	ldr	x1, [x28, 3424]
	add	x0, x1, x6
	ldr	x2, [x0, 16]
	ldr	w7, [x0, 24]
	ldr	w2, [x2, 8]
	cmp	w7, w2
	beq	.L1601
	ldr	w2, [x28, 2784]
	add	w2, w2, 1
	str	w2, [x28, 2784]
.L1601:
	ldr	w7, [x1, x6]
	cmn	w7, #1
	bne	.L1602
	ldr	w2, [x28, 2784]
	mov	w19, w7
	add	w2, w2, 1
	str	w2, [x28, 2784]
.L1602:
	ldr	w1, [x1, x6]
	cmp	w1, 256
	bne	.L1603
	ldr	w0, [x0, 4]
	ubfx	x0, x0, 10, 16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1603:
	add	x27, x27, 1
	cmp	w20, w27
	bhi	.L1604
	mov	w20, 0
	b	.L1587
.L1620:
	ldr	x0, [x28, 3424]
	ubfiz	x5, x20, 5, 32
	add	x0, x0, x5
	str	w1, [x0, 4]
	ldr	w0, [sp, 100]
	ldrh	w1, [x28, 262]
	cmp	w22, w0
	bne	.L1593
	udiv	w6, w26, w1
	ldr	x0, [x28, 3424]
	ldr	x2, [x28, 3472]
	msub	w3, w6, w1, w26
	add	x0, x0, x5
	str	w3, [sp, 104]
	sub	w6, w1, w3
	cmp	w23, w6
	csel	w3, w23, w6, ls
	str	w3, [sp, 108]
	cmp	w1, w3
	csel	x1, x2, x21, ne
	str	x1, [x0, 8]
.L1594:
	ldrh	w0, [x28, 316]
	ldr	x1, [x28, 3424]
	ldr	x2, [x28, 3520]
	mul	w0, w0, w20
	add	x1, x1, x5
	add	w20, w20, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1, 16]
	str	w22, [x1, 24]
	b	.L1592
.L1593:
	ldr	x2, [x28, 3424]
	mul	w0, w22, w1
	add	x2, x2, x5
	cmp	w22, w25
	bne	.L1595
	ldr	w3, [sp, 116]
	sub	w3, w3, w0
	str	w3, [sp, 112]
	cmp	w3, w1
	beq	.L1595
	ldr	x0, [x28, 3480]
.L1622:
	str	x0, [x2, 8]
	b	.L1594
.L1595:
	sub	w0, w0, w26
	lsl	w0, w0, 9
	add	x0, x21, x0
	b	.L1622
.L1599:
	cmp	w1, w25
	bne	.L1600
	ldr	x1, [x0, 8]
	ldr	x0, [x28, 3480]
	cmp	x1, x0
	bne	.L1600
	ldrh	w0, [x28, 262]
	ldr	w2, [sp, 132]
	str	x6, [sp, 136]
	mul	w0, w0, w25
	sub	w0, w0, w26
	lsl	w0, w0, 9
	add	x0, x21, x0
	b	.L1623
.L1610:
	mov	w19, -1
	b	.L1584
.L1607:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	hint	29 // autiasp
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	sftl_read
	.type	sftl_read, %function
sftl_read:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE125
	.text
.LPFE125:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	x3, x2
	mov	x29, sp
	mov	w2, w1
	mov	w1, w0
	mov	w0, 0
	bl	FtlRead
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	sftl_read, .-sftl_read
	.section	.rodata.str1.1
.LC126:
	.string	"FtlWrite: ecc error:%x %x %x\n"
.LC127:
	.string	"FtlWrite: lpa error:%x %x\n"
	.text
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE126
	.text
.LPFE126:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -224]!
	and	w0, w0, 255
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	mov	w21, w1
	mrs	x1, sp_el0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	mov	x25, x3
	stp	x27, x28, [sp, 80]
	str	w2, [sp, 120]
	ldr	x2, [x1, 1376]
	str	x2, [sp, 216]
	mov	x2, 0
	cmp	w0, 16
	bne	.L1627
	ldr	w1, [sp, 120]
	mov	x2, x3
	add	w0, w21, 256
	bl	FtlVendorPartWrite
.L1626:
	mrs	x1, sp_el0
	ldr	x2, [sp, 216]
	ldr	x3, [x1, 1376]
	subs	x2, x2, x3
	mov	x3, 0
	beq	.L1664
	bl	__stack_chk_fail
.L1627:
	adrp	x4, .LANCHOR0
	add	x24, x4, :lo12:.LANCHOR0
	ldr	w0, [x24, 344]
	cmp	w0, w21
	bls	.L1667
	ldr	w1, [sp, 120]
	cmp	w0, w1
	bcc	.L1667
	add	w1, w21, w1
	str	w1, [sp, 124]
	cmp	w0, w1
	bcc	.L1667
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	cmn	w0, #1
	beq	.L1626
	mov	w0, 2048
	str	w0, [x24, 3960]
	ldrh	w0, [x24, 262]
	sub	w1, w1, #1
	add	x20, x24, 24
	udiv	w27, w21, w0
	udiv	w0, w1, w0
	ldr	w1, [sp, 120]
	str	w0, [sp, 160]
	sub	w0, w0, w27
	add	w26, w0, 1
	str	w0, [sp, 136]
	ldr	w0, [x24, 2652]
	add	w0, w0, w1
	str	w0, [x24, 2652]
	ldr	w0, [x24, 2636]
	add	w0, w0, w26
	str	w0, [x24, 2636]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	cbz	w0, .L1629
	ldrh	w1, [x20, 4]
	add	x0, x24, 80
	cmp	w1, 0
	csel	x20, x0, x20, eq
.L1629:
	adrp	x23, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR1
	adrp	x28, .LC9
	mov	w19, w27
	add	x23, x23, 591
	add	x28, x28, :lo12:.LC9
.L1630:
	cbnz	w26, .L1660
	ldr	w1, [sp, 136]
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x24, 228]
	cmp	w0, 5
	bls	.L1672
.L1663:
	mov	w0, 0
	b	.L1626
.L1660:
	ldrb	w1, [x20, 6]
	ldrh	w0, [x24, 236]
	cmp	w1, w0
	bcc	.L1631
	mov	x1, x23
	mov	x0, x28
	mov	w2, 1041
	bl	sftl_printk
.L1631:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1632
	add	x0, x24, 24
	cmp	x20, x0
	bne	.L1633
	ldrh	w1, [x24, 84]
	add	x0, x24, 80
	cbnz	w1, .L1634
	bl	allocate_new_data_superblock
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
.L1634:
	add	x0, x24, 24
	bl	allocate_new_data_superblock
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	cbnz	w0, .L1671
.L1636:
	add	x20, x24, 24
.L1635:
	ldrh	w0, [x20, 4]
	cbnz	w0, .L1632
	mov	x0, x20
	bl	allocate_new_data_superblock
.L1632:
	ldrh	w0, [x20, 4]
	ldrb	w1, [x20, 7]
	cmp	w0, w26
	csel	w0, w0, w26, ls
	lsl	w1, w1, 2
	cmp	w1, w0
	csel	w0, w1, w0, ls
	ldrb	w1, [x20, 6]
	str	w0, [sp, 140]
	ldrh	w0, [x24, 236]
	cmp	w1, w0
	bcc	.L1637
	mov	x1, x23
	mov	x0, x28
	mov	w2, 1074
	bl	sftl_printk
.L1637:
	ldr	w0, [sp, 140]
	str	xzr, [sp, 104]
	add	w0, w0, w19
	str	w0, [sp, 164]
	adrp	x0, .LC127
	add	x0, x0, :lo12:.LC127
	str	x0, [sp, 168]
.L1638:
	ldr	w0, [sp, 164]
	ldr	w22, [sp, 104]
	cmp	w19, w0
	bne	.L1658
	ldr	w22, [sp, 140]
.L1639:
	ldr	x0, [x24, 3440]
	mov	x3, x20
	mov	w1, w22
	mov	w2, 0
	bl	FtlProgPages
	cmp	w26, w22
	bcs	.L1659
	mov	x1, x23
	mov	x0, x28
	mov	w2, 1152
	bl	sftl_printk
.L1659:
	sub	w26, w26, w22
	b	.L1630
.L1633:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w0, [x0, 4]
	str	wzr, [x1, 8]
	cbnz	w0, .L1636
	mov	x0, x20
	bl	allocate_new_data_superblock
	b	.L1635
.L1671:
	add	x20, x24, 80
	b	.L1635
.L1658:
	ldrh	w0, [x20, 4]
	cbz	w0, .L1639
	add	x1, sp, 180
	mov	w2, 0
	mov	w0, w19
	bl	log2phys
	mov	x0, x20
	bl	get_new_active_ppa
	ldr	x1, [sp, 104]
	ldr	x2, [sp, 104]
	lsl	x1, x1, 5
	str	x1, [sp, 112]
	ldr	x1, [x24, 3440]
	add	x1, x1, x2, lsl 5
	ldrh	w2, [x24, 316]
	str	w0, [x1, 4]
	ldr	x1, [sp, 104]
	mul	w22, w2, w22
	ldr	x0, [x24, 3440]
	add	x0, x0, x1, lsl 5
	and	x1, x22, 4294967292
	str	x1, [sp, 144]
	ldr	x1, [x24, 3520]
	str	w19, [x0, 24]
	ldr	x3, [sp, 144]
	str	x1, [sp, 152]
	add	x22, x1, x3
	str	x22, [x0, 16]
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	ldr	w0, [sp, 160]
	cmp	w19, w27
	ldrh	w1, [x24, 262]
	ccmp	w19, w0, 4, ne
	bne	.L1640
	cmp	w19, w27
	bne	.L1641
	udiv	w0, w21, w1
	msub	w0, w0, w1, w21
	str	w0, [sp, 128]
	sub	w2, w1, w0
	ldr	w0, [sp, 120]
	cmp	w2, w0
	csel	w0, w2, w0, ls
	str	w0, [sp, 132]
.L1642:
	ldr	x0, [sp, 112]
	ldr	x3, [x24, 3440]
	add	x3, x3, x0
	ldr	w0, [sp, 132]
	cmp	w1, w0
	bne	.L1643
	mul	w0, w1, w19
	cmp	w19, w27
	sub	w0, w0, w21
	lsl	w0, w0, 9
	add	x0, x25, x0
	csel	x0, x25, x0, eq
	str	x0, [x3, 8]
.L1645:
	ldrb	w1, [x20, 6]
	ldrh	w0, [x24, 236]
	cmp	w1, w0
	bcc	.L1655
	mov	x1, x23
	mov	x0, x28
	mov	w2, 1143
	bl	sftl_printk
.L1655:
	ldp	x2, x1, [sp, 144]
	mov	w0, -3947
	strh	w0, [x1, x2]
	ldr	w0, [x24, 2664]
	stp	w0, w19, [x22, 4]
	add	w19, w19, 1
	cmn	w0, #2
	csinc	w0, wzr, w0, eq
	str	w0, [x24, 2664]
	ldr	w0, [sp, 180]
	str	w0, [x22, 12]
	ldrh	w0, [x20]
	strh	w0, [x22, 2]
	ldr	x0, [sp, 104]
	add	x0, x0, 1
	str	x0, [sp, 104]
	b	.L1638
.L1641:
	ldr	w0, [sp, 124]
	msub	w2, w1, w19, w0
	and	w0, w2, 65535
	stp	wzr, w0, [sp, 128]
	b	.L1642
.L1643:
	cmp	w19, w27
	bne	.L1646
	ldr	x0, [x24, 3472]
.L1678:
	str	x0, [x3, 8]
	ldr	w0, [sp, 180]
	cmn	w0, #1
	beq	.L1648
	str	w19, [sp, 208]
	mov	w2, 0
	str	w0, [sp, 188]
	mov	w1, 1
	ldr	x0, [x3, 8]
	str	x0, [sp, 192]
	ldr	x0, [x3, 16]
	str	x0, [sp, 200]
	add	x0, sp, 184
	bl	FlashReadPages
	ldr	w3, [sp, 184]
	cmn	w3, #1
	bne	.L1649
	ldr	w0, [x24, 2784]
	mov	w2, w19
	add	w0, w0, 1
	str	w0, [x24, 2784]
	adrp	x0, .LC126
	add	x0, x0, :lo12:.LC126
	ldr	w1, [x22, 8]
	bl	sftl_printk
.L1653:
	ldr	w0, [sp, 132]
	cmp	w19, w27
	lsl	w2, w0, 9
	bne	.L1654
	ldr	x0, [sp, 112]
	ldr	x1, [x24, 3440]
	add	x1, x1, x0
	ldr	w0, [sp, 128]
	ldr	x3, [x1, 8]
	lsl	w0, w0, 9
	mov	x1, x25
	add	x0, x3, x0
.L1679:
	bl	ftl_memcpy
	b	.L1645
.L1646:
	ldr	x0, [x24, 3480]
	b	.L1678
.L1649:
	ldr	w0, [x22, 8]
	cmp	w0, w19
	beq	.L1653
	ldr	w0, [x24, 2784]
	mov	w2, w19
	add	w0, w0, 1
	str	w0, [x24, 2784]
	ldr	x0, [sp, 168]
	ldr	w1, [x22, 8]
	bl	sftl_printk
	ldr	w0, [x22, 8]
	cmp	w0, w19
	beq	.L1653
	mov	x1, x23
	mov	x0, x28
	mov	w2, 1128
	bl	sftl_printk
	b	.L1653
.L1648:
	ldrh	w2, [x24, 314]
	mov	w1, 0
	ldr	x0, [x3, 8]
	bl	ftl_memset
	b	.L1653
.L1654:
	ldrh	w1, [x24, 262]
	ldr	x3, [sp, 112]
	ldr	x0, [x24, 3440]
	mul	w1, w1, w19
	add	x0, x0, x3
	sub	w1, w1, w21
	lsl	w1, w1, 9
	add	x1, x25, x1
	ldr	x0, [x0, 8]
	b	.L1679
.L1640:
	ldr	x2, [sp, 112]
	mul	w1, w1, w19
	ldr	x0, [x24, 3440]
	sub	w1, w1, w21
	add	x0, x0, x2
	lsl	w1, w1, 9
	add	x1, x25, x1
	str	x1, [x0, 8]
	b	.L1645
.L1672:
	mov	w19, 256
	mov	w21, 65535
	mov	w20, 128
.L1661:
	ldrh	w0, [x24, 176]
	ldrh	w1, [x24, 226]
	and	w0, w0, w1
	cmp	w21, w0, uxth
	bne	.L1662
	mov	w0, 0
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L1662:
	mov	w1, 1
	mov	w0, w1
	strh	w20, [x24, 3224]
	strh	w20, [x24, 3226]
	bl	rk_ftl_garbage_collect
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	ldrh	w0, [x24, 228]
	cmp	w0, 2
	bhi	.L1663
	subs	w19, w19, #1
	bne	.L1661
	b	.L1663
.L1667:
	mov	w0, -1
	b	.L1626
.L1664:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 224
	hint	29 // autiasp
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_gc
	.type	sftl_gc, %function
sftl_gc:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE127
	.text
.LPFE127:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	w1, 1
	mov	x29, sp
	mov	w0, w1
	bl	rk_ftl_garbage_collect
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	sftl_gc, .-sftl_gc
	.section	.rodata.str1.1
.LC128:
	.string	"%s hash error this.id =%x page =%x pre_id =%x hash =%x hash_r =%x\n"
	.text
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE128
	.text
.LPFE128:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -80]!
	mov	w1, 0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	add	x25, x19, 3712
	stp	x21, x22, [sp, 32]
	ldrh	w2, [x19, 244]
	stp	x23, x24, [sp, 48]
	ldr	x0, [x19, 3448]
	str	x0, [x25, 8]
	ldr	x0, [x19, 3512]
	str	x0, [x25, 16]
	ldr	x0, [x19, 72]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x19, 2696]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1683
.L1695:
	mov	w0, -1
.L1682:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	hint	29 // autiasp
	ret
.L1683:
	add	x21, x19, 2560
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w20, w0
	add	w0, w20, 1
	mov	w24, 19539
	ldrsh	w22, [x21, 136]
	movk	w24, 0x4654, lsl 16
	strh	w0, [x21, 138]
	adrp	x21, .LANCHOR1
	add	x21, x21, :lo12:.LANCHOR1
	add	x21, x21, 600
.L1685:
	tbz	w20, #31, .L1692
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 600
	mov	w2, 1474
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	bl	sftl_printk
	b	.L1691
.L1692:
	orr	w0, w20, w22, lsl 10
	str	w0, [x25, 4]
	ldr	x0, [x19, 3448]
	mov	w2, 1
	mov	w1, w2
	str	x0, [x25, 8]
	mov	x0, x25
	bl	FlashReadPages
	ldr	x0, [x25, 16]
	ldr	w23, [x0, 12]
	ldr	w0, [x25]
	cmn	w0, #1
	cbz	w23, .L1686
	beq	.L1687
	ldrh	w1, [x19, 314]
	ldr	x0, [x25, 8]
	bl	js_hash
	mov	w6, w0
	cmp	w23, w0
	beq	.L1688
	ldrh	w4, [x19, 2700]
	adrp	x0, .LC128
	mov	w5, w23
	mov	w3, w20
	mov	w2, w22
	mov	x1, x21
	add	x0, x0, :lo12:.LC128
	bl	sftl_printk
	cbnz	w20, .L1689
	ldrh	w0, [x19, 2700]
	cmp	w22, w0
	beq	.L1689
	ldrh	w20, [x19, 308]
	sxth	w22, w0
.L1687:
	sub	w20, w20, #1
	sxth	w20, w20
	b	.L1685
.L1689:
	mov	w0, -1
	str	w0, [x25]
	b	.L1687
.L1686:
	beq	.L1687
.L1688:
	ldr	x0, [x19, 3448]
	ldr	w0, [x0]
	cmp	w0, w24
	bne	.L1687
	ldr	x0, [x19, 3512]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L1687
.L1691:
	ldrh	w0, [x19, 244]
	ldrh	w1, [x19, 314]
	add	x0, x0, 24
	cmp	x1, x0, lsl 1
	bcs	.L1694
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 600
	add	x0, x0, :lo12:.LC9
	mov	w2, 1476
	bl	sftl_printk
.L1694:
	ldr	x1, [x19, 3720]
	add	x21, x19, 2512
	mov	w2, 48
	mov	x0, x21
	bl	ftl_memcpy
	ldr	x0, [x19, 72]
	ldrh	w2, [x19, 244]
	ldr	x1, [x19, 3720]
	lsl	w2, w2, 1
	add	x1, x1, 48
	bl	ftl_memcpy
	ldr	w1, [x19, 2512]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1695
	ldrh	w1, [x19, 2520]
	ldrb	w2, [x19, 2522]
	ldrh	w0, [x19, 258]
	strh	w1, [x19, 2702]
	cmp	w2, w0
	bne	.L1695
	ldrh	w0, [x19, 306]
	ldrh	w2, [x19, 262]
	ldrh	w3, [x19, 236]
	str	w1, [x19, 3964]
	mul	w0, w0, w1
	str	w0, [x19, 2624]
	mul	w0, w2, w0
	ldr	w2, [x19, 248]
	str	w0, [x19, 344]
	ldrh	w0, [x19, 366]
	add	w0, w0, w1
	sub	w0, w2, w0
	udiv	w0, w0, w3
	strh	w0, [x19, 2692]
	cmp	w1, w2
	bls	.L1696
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC9
	add	x1, x1, 600
	add	x0, x0, :lo12:.LC9
	mov	w2, 1498
	bl	sftl_printk
.L1696:
	ldrh	w2, [x19, 2528]
	add	x0, x19, 2660
	ldr	w1, [x19, 2544]
	ldrh	w3, [x19, 2526]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strb	w2, [x19, 30]
	ldrb	w2, [x19, 2523]
	strb	w2, [x19, 32]
	mov	w2, 65535
	str	w2, [x19, 176]
	ldrh	w2, [x19, 2530]
	strh	w2, [x19, 80]
	ldrh	w2, [x19, 2532]
	strh	w4, [x19, 26]
	stp	wzr, wzr, [x0, -16]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strb	w2, [x19, 86]
	ldrb	w2, [x19, 2524]
	strb	w2, [x19, 88]
	ldrh	w2, [x19, 2534]
	strh	w2, [x19, 128]
	ldrh	w2, [x19, 2536]
	strh	w4, [x19, 82]
	stp	w1, wzr, [x0, 8]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strb	w2, [x19, 134]
	ldrb	w2, [x19, 2525]
	strh	w3, [x19, 24]
	strh	w4, [x19, 130]
	strb	w2, [x19, 136]
	strb	wzr, [x19, 182]
	strb	wzr, [x19, 184]
	str	wzr, [x19, 2628]
	str	wzr, [x19, 2636]
	str	wzr, [x19, 2684]
	str	wzr, [x19, 2640]
	ldr	w0, [x19, 2552]
	ldr	w1, [x19, 2660]
	cmp	w0, w1
	bls	.L1697
	str	w0, [x19, 2660]
.L1697:
	ldr	w0, [x19, 2548]
	ldr	w1, [x19, 2664]
	cmp	w0, w1
	bls	.L1698
	str	w0, [x19, 2664]
.L1698:
	mov	w0, 65535
	cmp	w3, w0
	beq	.L1699
	add	x0, x19, 24
	bl	make_superblock
.L1699:
	ldrh	w2, [x19, 80]
	add	x0, x19, 80
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1700
	bl	make_superblock
.L1700:
	ldrh	w2, [x19, 128]
	add	x0, x19, 128
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1701
	bl	make_superblock
.L1701:
	ldrh	w2, [x19, 176]
	add	x0, x19, 176
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1702
	bl	make_superblock
.L1702:
	mov	w0, 0
	b	.L1682
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.section	.rodata.str1.1
.LC129:
	.string	"%s last blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
.LC130:
	.string	"%s scan blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
	.text
	.align	2
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE129
	.text
.LPFE129:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -144]!
	mov	w1, 0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w20, 0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w23, [x0, 6]
	ldr	x22, [x0, 40]
	ldp	x27, x28, [x0, 16]
	lsl	w2, w23, 2
	ldrh	w0, [x0, 8]
	str	w0, [sp, 112]
	mov	x0, x22
	bl	ftl_memset
	str	xzr, [x19, 48]
	adrp	x6, .LANCHOR0
	add	x24, x6, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x24, 3448]
	str	x1, [x24, 3720]
	ldr	x21, [x24, 3512]
	str	x21, [x24, 3728]
	str	w0, [x19]
	mov	w0, 1
	str	w0, [x19, 56]
	ldr	w0, [sp, 112]
	sub	w0, w0, #1
	str	w0, [sp, 124]
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 615
	str	x0, [sp, 128]
.L1712:
	ldr	w0, [sp, 112]
	cmp	w20, w0
	bge	.L1732
	add	x0, x27, x20, sxtw 1
	str	x0, [sp, 104]
	ldr	w0, [sp, 124]
	sxtw	x26, w20
	sbfiz	x1, x20, 1, 32
	cmp	w0, w20
	bne	.L1713
	ldr	x0, [sp, 104]
	mov	w1, 1
	adrp	x27, .LC129
	add	x27, x27, :lo12:.LC129
	ldrh	w0, [x0]
	bl	FtlGetLastWrittenPage
	sxth	w25, w0
	add	w25, w25, 1
	strh	w20, [x19]
	strh	w25, [x19, 2]
	ldr	w0, [x28, x26, lsl 2]
	adrp	x26, .LANCHOR1
	add	x26, x26, :lo12:.LANCHOR1
	mov	w28, 0
	add	x26, x26, 615
	str	w0, [x19, 48]
.L1714:
	cmp	w28, w25
	blt	.L1718
.L1732:
	mov	x0, x19
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19, 2]
	ldrh	w0, [x24, 308]
	cmp	w1, w0
	bne	.L1720
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1720:
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
	hint	29 // autiasp
	ret
.L1718:
	ldr	x0, [sp, 104]
	add	x3, x24, 3712
	mov	w2, 1
	mov	w1, w2
	str	x3, [sp, 112]
	ldrh	w0, [x0]
	orr	w0, w28, w0, lsl 10
	str	w0, [x24, 3716]
	mov	x0, x3
	bl	FlashReadPages
	ldr	x3, [sp, 112]
	ldr	x0, [x3, 16]
	ldr	w4, [x0, 12]
	ldr	w0, [x24, 3712]
	str	w4, [sp, 112]
	cmn	w0, #1
	cbz	w4, .L1715
	beq	.L1733
	ldrh	w1, [x24, 314]
	ldr	x0, [x3, 8]
	bl	js_hash
	ldr	w4, [sp, 112]
	mov	w5, w0
	cmp	w4, w0
	beq	.L1717
	mov	x0, x27
	mov	w3, w28
	mov	w2, w20
	mov	x1, x26
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x24, 3712]
.L1733:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L1714
.L1715:
	beq	.L1733
.L1717:
	ldrh	w0, [x21, 8]
	cmp	w0, w23
	bcs	.L1733
	ldrh	w2, [x21]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L1733
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x24, 3716]
	str	w1, [x22, x0]
	b	.L1733
.L1713:
	add	x0, x24, 3712
	ldr	x2, [x24, 3448]
	str	x2, [x0, 8]
	ldrh	w2, [x24, 308]
	ldrh	w1, [x27, x1]
	sub	w2, w2, #1
	orr	w1, w2, w1, lsl 10
	mov	w2, 1
	str	w1, [x0, 4]
	mov	w1, w2
	bl	FlashReadPages
	ldr	w0, [x24, 3712]
	cmn	w0, #1
	beq	.L1736
	ldrh	w1, [x21]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L1736
	ldrh	w1, [x21, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L1737
.L1736:
	adrp	x25, .LC130
	mov	w26, 0
	add	x25, x25, :lo12:.LC130
	b	.L1728
.L1724:
	ldr	x3, [x24, 3448]
	lsl	w1, w0, 1
	ldr	w4, [x3, w1, sxtw 2]
	and	w2, w4, 65535
	cmp	w23, w4, uxth
	bls	.L1723
	add	w1, w1, 1
	ldr	w3, [x3, w1, sxtw 2]
	ubfiz	x1, x2, 2, 16
	str	w3, [x22, x1]
.L1723:
	add	w0, w0, 1
	sxth	w0, w0
.L1722:
	ldrh	w1, [x24, 308]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1724
.L1730:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L1712
.L1737:
	mov	w0, 0
	b	.L1722
.L1729:
	ldr	x0, [sp, 104]
	add	x5, x24, 3712
	mov	w2, 1
	mov	w1, w2
	str	x5, [sp, 136]
	ldrh	w0, [x0]
	orr	w0, w26, w0, lsl 10
	str	w0, [x24, 3716]
	mov	x0, x5
	bl	FlashReadPages
	ldr	x5, [sp, 136]
	ldr	x0, [x5, 16]
	ldr	w4, [x0, 12]
	ldr	w0, [x24, 3712]
	str	w4, [sp, 136]
	cmn	w0, #1
	cbz	w4, .L1725
	beq	.L1734
	ldrh	w1, [x24, 314]
	ldr	x0, [x5, 8]
	bl	js_hash
	ldr	w4, [sp, 136]
	mov	w5, w0
	cmp	w4, w0
	beq	.L1727
	ldr	x1, [sp, 128]
	mov	x0, x25
	mov	w3, w26
	mov	w2, w20
	bl	sftl_printk
	mov	w0, -1
	str	w0, [x24, 3712]
.L1734:
	add	w3, w26, 1
	sxth	w26, w3
.L1728:
	ldrh	w0, [x24, 308]
	cmp	w26, w0
	blt	.L1729
	b	.L1730
.L1725:
	beq	.L1734
.L1727:
	ldrh	w0, [x21, 8]
	cmp	w0, w23
	bcs	.L1734
	ldrh	w2, [x21]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L1734
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x24, 3716]
	str	w1, [x22, x0]
	b	.L1734
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE130
	.text
.LPFE130:
	nop
	nop
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	add	x0, x1, 3880
	mov	x29, sp
	ldrh	w2, [x1, 324]
	strh	w2, [x0, 10]
	mov	w2, -3962
	strh	w2, [x0, 4]
	ldrh	w2, [x1, 348]
	strh	w2, [x0, 8]
	ldrh	w2, [x1, 326]
	strh	w2, [x0, 6]
	ldr	x2, [x1, 352]
	str	x2, [x0, 16]
	ldr	x2, [x1, 3584]
	str	x2, [x0, 24]
	ldr	x2, [x1, 3576]
	ldr	x1, [x1, 3592]
	stp	x2, x1, [x0, 32]
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlLoadMapInfo
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE131
	.text
.LPFE131:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	bl	FtlL2PDataInit
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 3632
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE132
	.text
.LPFE132:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ldrh	w0, [x19, 240]
	strh	wzr, [x19, 3744]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x19, 2696]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1756
.L1758:
	mov	w20, -1
.L1755:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L1756:
	bl	FtlLoadSysInfo
	mov	w20, w0
	cbnz	w0, .L1758
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldr	x1, [x19, 2608]
	mov	x0, 0
	ldrh	w2, [x19, 342]
	add	x1, x1, 4
.L1759:
	mov	w3, w0
	cmp	w2, w0
	ble	.L1763
	lsl	x4, x0, 4
	add	x0, x0, 1
	ldr	w4, [x1, x4]
	tbz	w4, #31, .L1759
.L1763:
	ldrh	w0, [x19, 2540]
	add	w0, w0, 1
	strh	w0, [x19, 2540]
	cmp	w3, w2
	bge	.L1770
.L1760:
	add	x22, x19, 24
	add	x21, x19, 80
	mov	x0, x22
	bl	FtlSuperblockPowerLostFix
	mov	x0, x21
	bl	FtlSuperblockPowerLostFix
	ldrh	w0, [x19, 24]
	ldr	x2, [x19, 72]
	lsl	x0, x0, 1
	ldrh	w3, [x19, 28]
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strh	wzr, [x19, 28]
	ldrh	w0, [x19, 306]
	strh	w0, [x19, 26]
	ldrh	w0, [x19, 80]
	ldr	x2, [x19, 72]
	strb	wzr, [x19, 30]
	lsl	x0, x0, 1
	ldrh	w3, [x19, 84]
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	strh	wzr, [x19, 84]
	ldrh	w0, [x19, 306]
	strh	w0, [x19, 82]
	ldrh	w0, [x19, 2542]
	strb	wzr, [x19, 86]
	add	w0, w0, 1
	strh	w0, [x19, 2542]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1764
.L1770:
	ldrh	w0, [x19, 3744]
	cbnz	w0, .L1760
.L1764:
	ldrh	w0, [x19, 24]
	add	x22, x19, 24
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1765
	ldrh	w1, [x19, 28]
	add	x21, x19, 80
	ldrh	w2, [x19, 84]
	orr	w1, w1, w2
	tst	w1, 65535
	bne	.L1765
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19, 80]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	mov	x0, x22
	bl	allocate_new_data_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
.L1765:
	bl	FtlVpcCheckAndModify
	b	.L1755
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	ftl_low_format
	.type	ftl_low_format, %function
ftl_low_format:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE133
	.text
.LPFE133:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	ldrh	w0, [x19, 240]
	str	wzr, [x19, 2660]
	str	wzr, [x19, 2664]
	str	wzr, [x19, 2676]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L1773
	bl	FtlMakeBbt
.L1773:
	mov	w3, 23752
	mov	w0, 0
	movk	w3, 0xa0f, lsl 16
.L1774:
	ldrh	w1, [x19, 262]
	cmp	w0, w1, lsl 7
	blt	.L1775
	ldrh	w21, [x19, 244]
	mov	w20, 0
.L1776:
	ldrh	w0, [x19, 246]
	cmp	w0, w21
	bhi	.L1777
	ldrh	w0, [x19, 236]
	sub	w1, w20, #3
	cmp	w1, w0, lsl 1
	bge	.L1778
.L1782:
	mov	w20, 0
	mov	w21, 0
.L1779:
	ldrh	w0, [x19, 244]
	cmp	w0, w21
	bhi	.L1783
	ldrh	w0, [x19, 246]
	ldrh	w2, [x19, 236]
	str	w0, [x19, 3420]
	ldr	w0, [x19, 248]
	udiv	w4, w0, w2
	ubfx	x3, x4, 5, 16
	str	w4, [x19, 2624]
	add	w1, w3, 36
	strh	w1, [x19, 2692]
	mov	w1, 24
	mul	w1, w2, w1
	cmp	w20, w1
	ble	.L1784
	sub	w0, w0, w20
	udiv	w0, w0, w2
	str	w0, [x19, 2624]
	lsr	w0, w0, 5
	add	w0, w0, 24
	strh	w0, [x19, 2692]
.L1784:
	ldrh	w1, [x19, 298]
	cbz	w1, .L1786
	ldrh	w0, [x19, 2692]
	add	w0, w0, w1, lsr 1
	strh	w0, [x19, 2692]
	mul	w0, w1, w2
	cmp	w20, w0
	bge	.L1786
	add	w1, w1, 32
	str	w4, [x19, 2624]
	add	w1, w3, w1
	strh	w1, [x19, 2692]
.L1786:
	ldrh	w1, [x19, 2692]
	add	x21, x19, 24
	ldr	w0, [x19, 2624]
	mov	w20, -1
	sub	w0, w0, w1
	ldrh	w1, [x19, 306]
	mul	w0, w0, w2
	str	w0, [x19, 3964]
	mul	w0, w1, w0
	ldrh	w1, [x19, 262]
	str	w0, [x19, 2624]
	mul	w0, w1, w0
	str	w0, [x19, 344]
	bl	FtlBbmTblFlush
	ldr	x0, [x19, 72]
	mov	w1, 0
	ldrh	w2, [x19, 246]
	lsl	w2, w2, 1
	bl	ftl_memset
	mov	w1, 65535
	mov	w0, 1
	strb	wzr, [x21, 6]
	strb	w0, [x21, 8]
	str	wzr, [x19, 24]
	str	w1, [x19, 176]
	strb	wzr, [x19, 182]
	strb	wzr, [x19, 184]
	str	wzr, [x19, 2620]
.L1788:
	mov	x0, x21
	bl	make_superblock
	ldrb	w1, [x21, 7]
	ldrh	w0, [x21]
	cbnz	w1, .L1789
	ldr	x1, [x19, 72]
	ubfiz	x0, x0, 1, 16
	strh	w20, [x1, x0]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L1788
.L1775:
	ldr	x4, [x19, 3472]
	ubfiz	x2, x0, 2, 16
	mvn	w1, w0
	orr	w1, w0, w1, lsl 16
	str	w1, [x4, x2]
	add	w0, w0, 1
	ldr	x1, [x19, 3480]
	and	w0, w0, 65535
	str	w3, [x1, x2]
	b	.L1774
.L1777:
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w20, w0, w20
	and	w21, w21, 65535
	and	w20, w20, 65535
	b	.L1776
.L1778:
	udiv	w20, w20, w0
	ldr	w0, [x19, 336]
	add	w0, w20, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x19, 240]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w20, [x19, 244]
.L1780:
	ldrh	w0, [x19, 246]
	cmp	w0, w20
	bls	.L1782
	mov	w0, w20
	add	w20, w20, 1
	mov	w1, 1
	and	w20, w20, 65535
	bl	FtlLowFormatEraseBlock
	b	.L1780
.L1783:
	mov	w0, w21
	mov	w1, 0
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w20, w0, w20
	and	w21, w21, 65535
	and	w20, w20, 65535
	b	.L1779
.L1789:
	ldr	w1, [x19, 2660]
	ubfiz	x0, x0, 1, 16
	str	w1, [x21, 12]
	add	x20, x19, 80
	add	w1, w1, 1
	str	w1, [x19, 2660]
	ldr	x1, [x19, 72]
	ldrh	w2, [x21, 4]
	strh	w2, [x1, x0]
	strh	wzr, [x20, 2]
	ldrh	w0, [x21]
	mov	w21, -1
	strb	wzr, [x20, 6]
	add	w0, w0, 1
	strh	w0, [x19, 80]
	mov	w0, 1
	strb	w0, [x20, 8]
.L1790:
	mov	x0, x20
	bl	make_superblock
	ldrb	w1, [x20, 7]
	ldrh	w0, [x20]
	cbnz	w1, .L1791
	ldr	x1, [x19, 72]
	ubfiz	x0, x0, 1, 16
	strh	w21, [x1, x0]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L1790
.L1791:
	ldr	w1, [x19, 2660]
	ubfiz	x0, x0, 1, 16
	str	w1, [x20, 12]
	add	w1, w1, 1
	str	w1, [x19, 2660]
	ldr	x1, [x19, 72]
	ldrh	w2, [x20, 4]
	strh	w2, [x1, x0]
	mov	w0, -1
	strh	w0, [x19, 128]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19, 2696]
	add	x1, x19, 2560
	mov	w0, -65536
	str	w0, [x1, 138]
	ldr	w0, [x19, 3964]
	strh	w0, [x19, 2702]
	ldr	w0, [x19, 2660]
	str	w0, [x19, 2704]
	add	w0, w0, 1
	str	w0, [x19, 2660]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L1792
	adrp	x0, .LANCHOR2
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR2]
.L1792:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	sftl_init
	.type	sftl_init, %function
sftl_init:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE134
	.text
.LPFE134:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LC1
	mov	x29, sp
	add	x1, x1, :lo12:.LC1
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	mov	w0, -1
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	w0, [x20, #:lo12:.LANCHOR2]
	adrp	x0, .LC78
	add	x0, x0, :lo12:.LC78
	bl	sftl_printk
	mov	x0, x19
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x19, 240]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	w0, .L1798
	bl	FtlSysBlkInit
	cbnz	w0, .L1798
	mov	w0, 1
	str	w0, [x20, #:lo12:.LANCHOR2]
	ldrh	w0, [x19, 228]
	cmp	w0, 15
	bhi	.L1798
	mov	w19, 8129
.L1799:
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1
	bne	.L1799
.L1798:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
	.size	sftl_init, .-sftl_init
	.section	.rodata.str1.1
.LC131:
	.string	"write_idblock %x %x\n"
.LC132:
	.string	"write_idblock fail! %x %x %x %x\n"
.LC133:
	.string	"%s idb buffer alloc fail\n"
	.text
	.align	2
	.global	FtlWriteToIDB
	.type	FtlWriteToIDB, %function
FtlWriteToIDB:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE135
	.text
.LPFE135:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -208]!
	adrp	x5, .LANCHOR0
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	w20, w0
	mrs	x0, sp_el0
	stp	x21, x22, [sp, 32]
	mov	w19, w1
	add	w22, w19, w20
	stp	x23, x24, [sp, 48]
	sub	w23, w22, #1
	cmp	w23, 63
	stp	x25, x26, [sp, 64]
	add	x25, x5, :lo12:.LANCHOR0
	stp	x27, x28, [sp, 80]
	ldr	x1, [x0, 1376]
	str	x1, [sp, 200]
	mov	x1, 0
	bls	.L1803
	cmp	w20, 575
	bls	.L1804
.L1803:
	ldr	w0, [x25, 3968]
	cbnz	w0, .L1805
.L1867:
	mov	w19, 0
	b	.L1802
.L1805:
	ldr	x22, [x25, 3976]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	ldr	w1, [x22]
	cmp	w1, w0
	bne	.L1807
	add	x1, x22, 258048
	ldrh	w2, [x25, 10]
	add	x1, x1, 2044
	mov	w0, 0
	mov	w19, 65023
.L1811:
	ldr	w3, [x1]
	cbnz	w3, .L1808
	ldr	w3, [x22, w0, uxtw 2]
	add	w0, w0, 1
	str	w3, [x1, 2048]
	cmp	w0, 4096
	bls	.L1809
	mov	w0, 0
.L1809:
	sub	w19, w19, #1
	sub	x1, x1, #4
	cmp	w19, 4096
	bne	.L1811
	mov	w19, 512
	b	.L1810
.L1808:
	add	w19, w19, 127
	lsr	w19, w19, 7
.L1810:
	ubfiz	w0, w2, 2, 14
	add	w20, w19, 4
	mov	w1, w19
	mov	w23, 0
	mov	w21, 0
	udiv	w20, w20, w0
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	add	w20, w20, 1
	mov	w2, w20
	bl	sftl_printk
	lsl	w0, w19, 7
	str	w0, [sp, 100]
.L1812:
	add	w0, w20, w21
	str	w0, [sp, 104]
	cmp	w0, 8
	bls	.L1834
	cbnz	w23, .L1833
.L1807:
	mov	w19, -1
	b	.L1833
.L1834:
	ldr	x0, [x25, 3984]
	mov	w1, 0
	mov	x2, 512
	bl	memset
	ldrh	w27, [x25, 10]
	ldr	x1, [x25, 3384]
	mul	w0, w21, w27
	str	w0, [sp, 96]
	cbz	x1, .L1843
	ldr	x0, [x25, 3392]
	mov	w24, 6
	cmp	x0, 0
	mov	w0, 9
	csel	w24, w24, w0, eq
.L1813:
	mul	w28, w21, w27
	mov	w26, 0
.L1814:
	ldr	x3, [x25, 3360]
	mov	w1, w28
	add	w26, w26, 1
	add	w28, w28, w27
	mov	w0, 0
	blr	x3
	cmp	w20, w26
	bhi	.L1814
	cmp	w24, 9
	bne	.L1845
	ldr	x28, [x25, 3984]
	mov	w2, 1024
	mov	w1, 0
	mov	x0, x28
	bl	ftl_memset
	mov	x0, 18766
	str	wzr, [x28, 12]
	movk	x0, 0x464e, lsl 16
	mov	w1, 12
	movk	x0, 0xc, lsl 32
	str	x0, [x28]
	mov	w0, 1024
	strh	w0, [x28, 16]
	ldrh	w0, [x25, 10]
	strh	w0, [x28, 18]
	mov	w0, 4096
	str	w0, [x28, 20]
	add	x0, x28, 12
	bl	js_hash
	str	w0, [x28, 8]
.L1815:
	mul	w0, w20, w27
	mov	x27, x22
	mov	w26, 0
	str	w0, [sp, 108]
.L1816:
	ldr	w0, [sp, 108]
	cmp	w26, w0
	beq	.L1823
	cmp	w24, 9
	lsl	w0, w26, 2
	csinc	w0, w0, w26, ne
	str	w0, [sp, 136]
	mov	w0, 61424
	str	w0, [sp, 140]
	cmp	w26, 0
	ccmp	w24, 9, 0, eq
	bne	.L1819
	ldr	x1, [x25, 3384]
	add	x8, x25, 3352
	mov	w0, 70
	str	x8, [sp, 112]
	blr	x1
	ldr	w1, [sp, 96]
	ldr	x8, [sp, 112]
	add	x3, sp, 136
	mov	x2, x28
	mov	w0, 0
	str	x8, [sp, 120]
	ldr	x9, [x8, 16]
	blr	x9
	mov	w1, w0
	ldr	x8, [sp, 120]
	str	w1, [sp, 112]
	ldrb	w0, [x25, 22]
	ldr	x2, [x8, 32]
	blr	x2
	ldr	w1, [sp, 112]
	cmn	w1, #1
	bne	.L1820
.L1823:
	ldrb	w0, [x25, 14]
	add	x1, x25, 3352
	str	w0, [sp, 108]
	ldrh	w0, [x25, 10]
	ldr	x3, [x1, 32]
	ldr	x26, [x25, 3984]
	mul	w27, w21, w0
	cbnz	x3, .L1821
	mov	w1, 6
.L1866:
	mul	w0, w20, w0
	mov	w28, 0
	str	w1, [sp, 96]
	str	w0, [sp, 112]
.L1825:
	ldr	w0, [sp, 112]
	cmp	w28, w0
	beq	.L1829
	ldr	w0, [sp, 96]
	cmp	w28, 0
	ccmp	w0, 9, 0, eq
	bne	.L1826
	ldr	x1, [x25, 3384]
	mov	w0, 70
	blr	x1
	ldr	x1, [x25, 3392]
	mov	w0, 2
	blr	x1
	add	x3, sp, 136
	ldr	x7, [x25, 3376]
	mov	x2, x26
	mov	w1, w27
	mov	w0, 0
	blr	x7
	ldrb	w0, [sp, 108]
	ldr	x1, [x25, 3392]
	blr	x1
	ldrb	w0, [x25, 22]
	ldr	x1, [x25, 3384]
	blr	x1
	ldr	w0, [x26]
	mov	w1, 18766
	movk	w1, 0x464e, lsl 16
	cmp	w0, w1
	beq	.L1827
.L1829:
	ldr	x0, [x25, 3984]
	mov	x24, 0
.L1828:
	ldr	w2, [x0, x24, lsl 2]
	add	x27, x0, x24, lsl 2
	ldr	w1, [x22, x24, lsl 2]
	add	x28, x22, x24, lsl 2
	cmp	w2, w1
	beq	.L1831
	mov	x2, 512
	mov	w1, 0
	bl	memset
	ldr	w2, [x27]
	mov	w1, w21
	ldr	w3, [x28]
	mov	w4, w24
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	bl	sftl_printk
	ldrh	w1, [x25, 10]
	mov	w0, 0
	ldr	x2, [x25, 3360]
	mul	w1, w1, w21
	blr	x2
.L1832:
	ldr	w21, [sp, 104]
	b	.L1812
.L1843:
	mov	w24, 6
	b	.L1813
.L1845:
	mov	x28, 0
	b	.L1815
.L1819:
	ldr	x8, [x25, 3368]
	add	x3, sp, 136
	ldr	w0, [sp, 96]
	mov	x2, x27
	add	w1, w0, w26
	mov	w0, 0
	blr	x8
	cmn	w0, #1
	beq	.L1823
	add	x27, x27, 2048
.L1820:
	add	w26, w26, 1
	b	.L1816
.L1821:
	ldr	x1, [x1, 40]
	mov	w24, 6
	cmp	x1, 0
	mov	w1, 9
	csel	w1, w24, w1, eq
	b	.L1866
.L1826:
	ldr	x6, [x25, 3376]
	add	x3, sp, 136
	mov	x2, x26
	add	w1, w27, w28
	mov	w0, 0
	blr	x6
	cmn	w0, #1
	beq	.L1829
	ldr	w0, [sp, 140]
	mov	w1, 61424
	cmp	w0, w1
	bne	.L1829
	add	x26, x26, 2048
.L1827:
	add	w28, w28, 1
	b	.L1825
.L1831:
	ldr	w1, [sp, 100]
	add	x24, x24, 1
	cmp	w1, w24
	bhi	.L1828
	add	w23, w23, 1
	cmp	w23, 5
	bls	.L1832
.L1833:
	ldr	x0, [x25, 3976]
	str	wzr, [x25, 3968]
	bl	kfree
	ldr	x0, [x25, 3984]
	bl	kfree
.L1802:
	mrs	x0, sp_el0
	ldr	x1, [sp, 200]
	ldr	x2, [x0, 1376]
	subs	x1, x1, x2
	mov	x2, 0
	beq	.L1842
	bl	__stack_chk_fail
.L1804:
	mov	x21, x2
	cmp	w20, 64
	bne	.L1835
	mov	w0, 262144
	bl	ftl_malloc
	str	x0, [x25, 3976]
	mov	w0, 262144
	bl	ftl_malloc
	mov	x1, x0
	ldr	x0, [x25, 3976]
	str	x1, [x25, 3984]
	cbz	x0, .L1836
	cbz	x1, .L1836
	mov	w1, 1
	mov	w2, 262144
	str	w1, [x25, 3968]
	mov	w1, 0
	bl	ftl_memset
.L1837:
	sub	w1, w20, #64
	ldr	x0, [x25, 3976]
	cmp	w20, 63
	bhi	.L1839
	add	w19, w19, w1
	mov	w1, 64
	sub	w20, w1, w20
	ubfiz	x20, x20, 9, 25
	add	x21, x21, x20
.L1840:
	cmp	w23, 575
	bls	.L1841
	sub	w19, w19, w22
	add	w19, w19, 576
.L1841:
	lsl	w2, w19, 9
	mov	x1, x21
	bl	ftl_memcpy
	b	.L1867
.L1836:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC133
	add	x1, x1, 633
	add	x0, x0, :lo12:.LC133
	bl	sftl_printk
.L1835:
	ldr	w0, [x25, 3968]
	cbnz	w0, .L1837
	b	.L1867
.L1839:
	ubfiz	x1, x1, 9, 25
	add	x0, x0, x1
	b	.L1840
.L1842:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 208
	hint	29 // autiasp
	ret
	.size	FtlWriteToIDB, .-FtlWriteToIDB
	.align	2
	.global	sftl_write
	.type	sftl_write, %function
sftl_write:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE136
	.text
.LPFE136:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	x29, sp
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
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
	.size	sftl_write, .-sftl_write
	.align	2
	.global	rk_sftl_vendor_dev_ops_register
	.type	rk_sftl_vendor_dev_ops_register, %function
rk_sftl_vendor_dev_ops_register:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE137
	.text
.LPFE137:
	nop
	nop
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	hint	25 // paciasp
	ldr	x3, [x2, 3992]
	cbnz	x3, .L1872
	str	x0, [x2, 3992]
	mov	w0, 0
	str	x1, [x2, 4000]
.L1870:
	hint	29 // autiasp
	ret
.L1872:
	mov	w0, -1
	b	.L1870
	.size	rk_sftl_vendor_dev_ops_register, .-rk_sftl_vendor_dev_ops_register
	.align	2
	.global	rk_sftl_vendor_storage_init
	.type	rk_sftl_vendor_storage_init, %function
rk_sftl_vendor_storage_init:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE138
	.text
.LPFE138:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -64]!
	mov	w0, 65536
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	bl	ftl_malloc
	str	x0, [x20, 4008]
	cbz	x0, .L1880
	mov	w24, 22084
	mov	w23, 0
	mov	w22, 0
	mov	w21, 0
	movk	w24, 0x524b, lsl 16
.L1878:
	ldr	x3, [x20, 3992]
	lsl	w0, w21, 7
	ldr	x2, [x20, 4008]
	mov	w1, 128
	blr	x3
	mov	w19, w0
	cbnz	w0, .L1876
	ldr	x3, [x20, 4008]
	ldr	w0, [x3]
	cmp	w0, w24
	bne	.L1877
	add	x0, x3, 49152
	ldr	w1, [x3, 4]
	ldr	w0, [x0, 16380]
	cmp	w0, w1
	bne	.L1877
	cmp	w0, w22
	bls	.L1877
	mov	w23, w21
	mov	w22, w0
.L1877:
	cbz	w21, .L1881
	cbz	w22, .L1879
	ldr	x4, [x20, 3992]
	mov	x2, x3
	lsl	w0, w23, 7
	mov	w1, 128
	blr	x4
	mov	w19, w0
	cbz	w0, .L1874
.L1876:
	ldr	x0, [x20, 4008]
	mov	w19, -1
	bl	kfree
	str	xzr, [x20, 4008]
	b	.L1874
.L1881:
	mov	w21, 1
	b	.L1878
.L1879:
	mov	x0, x3
	mov	x2, 65536
	mov	w1, 0
	bl	memset
	mov	x3, x0
	mov	x0, 22084
	movk	x0, 0x524b, lsl 16
	movk	x0, 0x1, lsl 32
	str	x0, [x3]
	add	x0, x3, 49152
	str	w21, [x0, 16380]
	mov	w0, -1032
	strh	w0, [x3, 14]
.L1874:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	hint	29 // autiasp
	ret
.L1880:
	mov	w19, -12
	b	.L1874
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE139
	.text
.LPFE139:
	nop
	nop
	mov	w5, w0
	mov	x0, x1
	adrp	x1, .LANCHOR0+4008
	ldr	x4, [x1, #:lo12:.LANCHOR0+4008]
	cbz	x4, .L1887
	ldrh	w1, [x4, 10]
	add	x6, x4, 16
	mov	x3, 0
.L1885:
	cmp	w1, w3
	bhi	.L1886
.L1887:
	mov	w0, -1
	ret
.L1886:
	add	x7, x3, 1
	lsl	x3, x3, 3
	ldrh	w8, [x6, x3]
	cmp	w8, w5
	bne	.L1888
	hint	25 // paciasp
	stp	x29, x30, [sp, -32]!
	add	x3, x4, x3
	mov	x29, sp
	str	x19, [sp, 16]
	ldrh	w5, [x3, 20]
	ldrh	w1, [x3, 18]
	cmp	w5, w2
	add	x1, x1, 1024
	csel	w19, w5, w2, ls
	add	x1, x4, x1
	csel	w2, w5, w2, ls
	bl	memcpy
	mov	w0, w19
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	hint	29 // autiasp
	ret
.L1888:
	mov	x3, x7
	b	.L1885
	.size	rk_sftl_vendor_read, .-rk_sftl_vendor_read
	.align	2
	.global	rk_sftl_vendor_write
	.type	rk_sftl_vendor_write, %function
rk_sftl_vendor_write:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE140
	.text
.LPFE140:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -128]!
	mov	x29, sp
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	add	x22, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	ldr	x19, [x22, 4008]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	cbz	x19, .L1908
	add	w21, w2, 63
	ldrh	w3, [x19, 10]
	ldrh	w23, [x19, 8]
	mov	w7, w0
	mov	x4, x1
	mov	w27, w2
	and	w21, w21, -64
	add	x0, x19, 16
	mov	x20, 0
.L1895:
	mov	w25, w20
	cmp	w3, w20
	bhi	.L1902
	ldrh	w1, [x19, 14]
	cmp	w1, w21
	bcc	.L1908
	add	x3, x19, x3, uxth 3
	and	w21, w21, 65535
	strh	w7, [x3, 16]
	ldrh	w0, [x19, 12]
	strh	w0, [x3, 18]
	strh	w27, [x3, 20]
	add	w2, w0, w21
	sub	w21, w1, w21
	and	x0, x0, 65535
	strh	w2, [x19, 12]
	add	x0, x0, 1024
	strh	w21, [x19, 14]
	uxtw	x2, w27
	mov	x1, x4
	add	x0, x19, x0
	bl	memcpy
	ldrh	w0, [x19, 10]
	add	w0, w0, 1
	strh	w0, [x19, 10]
	b	.L1910
.L1902:
	add	x1, x20, 1
	lsl	x20, x20, 3
	ldrh	w2, [x0, x20]
	cmp	w2, w7
	bne	.L1906
	add	x20, x19, x20
	add	x5, x19, 1024
	ldrh	w26, [x20, 20]
	add	w26, w26, 63
	and	w26, w26, -64
	cmp	w27, w26
	bls	.L1896
	ldrh	w0, [x19, 14]
	cmp	w0, w21
	bcc	.L1908
	ldrh	w6, [x20, 18]
	sub	w24, w3, #1
.L1897:
	and	w28, w6, 65535
	add	x20, x20, 8
	cmp	w24, w25
	bhi	.L1898
	add	x25, x19, x25, uxtw 3
	add	x0, x5, x28, uxth
	uxtw	x2, w27
	mov	x1, x4
	and	w21, w21, 65535
	strh	w7, [x25, 16]
	strh	w28, [x25, 18]
	add	w28, w21, w28
	strh	w27, [x25, 20]
	bl	memcpy
	strh	w28, [x19, 12]
	ldrh	w0, [x19, 14]
	sub	w21, w0, w21
	add	w26, w21, w26
	strh	w26, [x19, 14]
.L1910:
	ldr	w0, [x19, 4]
	add	x1, x19, 49152
	ldr	x3, [x22, 4000]
	add	w0, w0, 1
	str	w0, [x19, 4]
	mov	x2, x19
	str	w0, [x1, 16380]
	mov	w1, 128
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	and	w0, w0, 65535
	cmp	w0, 1
	csel	w0, w0, wzr, ls
	strh	w0, [x19, 8]
	lsl	w0, w23, 7
	blr	x3
	mov	w0, 0
.L1893:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	hint	29 // autiasp
	ret
.L1898:
	add	w25, w25, 1
	str	x5, [sp, 104]
	stp	w6, w7, [sp, 112]
	add	x1, x19, x25, uxtw 3
	str	x4, [sp, 120]
	ldrh	w0, [x1, 16]
	strh	w0, [x20, 8]
	ldrh	w0, [x1, 20]
	strh	w28, [x20, 10]
	strh	w0, [x20, 12]
	add	x0, x5, x6, uxtw
	ldrh	w2, [x1, 20]
	ldrh	w1, [x1, 18]
	add	w2, w2, 63
	and	w28, w2, -64
	add	x1, x5, x1
	and	x2, x2, 131008
	bl	memcpy
	ldp	w6, w7, [sp, 112]
	ldr	x5, [sp, 104]
	add	w6, w6, w28
	ldr	x4, [sp, 120]
	b	.L1897
.L1896:
	ldrh	w0, [x20, 18]
	uxtw	x2, w27
	mov	x1, x4
	add	x0, x5, x0
	bl	memcpy
	strh	w27, [x20, 20]
	b	.L1910
.L1906:
	mov	x20, x1
	b	.L1895
.L1908:
	mov	w0, -1
	b	.L1893
	.size	rk_sftl_vendor_write, .-rk_sftl_vendor_write
	.section	.rodata.str1.1
.LC134:
	.string	"copy_from_user error %d %p %p\n"
	.text
	.align	2
	.global	rk_sftl_vendor_storage_ioctl
	.type	rk_sftl_vendor_storage_ioctl, %function
rk_sftl_vendor_storage_ioctl:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE141
	.text
.LPFE141:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	mov	w0, 4096
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x20, x2
	str	x21, [sp, 32]
	mov	w21, w1
	bl	ftl_malloc
	cbz	x0, .L1922
	mov	w1, 30209
	mov	x19, x0
	movk	w1, 0x4004, lsl 16
	cmp	w21, w1
	beq	.L1913
	add	w1, w1, 1
	cmp	w21, w1
	beq	.L1914
.L1932:
	mov	x20, -14
.L1915:
	mov	x0, x19
	bl	kfree
.L1911:
	mov	x0, x20
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
.L1913:
	mov	x1, x20
	mov	x2, 8
	bl	_copy_from_user
	cbz	x0, .L1916
	mov	x2, x20
	mov	w1, 256
.L1933:
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
	bl	sftl_printk
	b	.L1932
.L1916:
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L1917
.L1918:
	mov	x20, -1
	b	.L1915
.L1917:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_read
	cmn	w0, #1
	beq	.L1918
	strh	w0, [x19, 6]
	and	x0, x0, 65535
	add	x21, x0, 8
	mov	x0, x20
	mov	x1, x21
	bl	__range_ok
	cbz	x0, .L1932
	bl	uaccess_enable_not_uao
	sbfx	x1, x20, 0, 56
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x0, sp_el0
// 0 "" 2
#NO_APP
	and	x1, x1, x20
	ldr	x2, [x0, 8]
#APP
// 289 "./arch/arm64/include/asm/uaccess.h" 1
		bics	xzr, x1, x2
	csel	x0, x20, xzr, eq

// 0 "" 2
// 297 "./arch/arm64/include/asm/uaccess.h" 1
	hint #20
// 0 "" 2
#NO_APP
	mov	x2, x21
	mov	x1, x19
	mov	x20, 0
	bl	__arch_copy_to_user
	mov	x21, x0
	bl	uaccess_disable_not_uao
	cbz	x21, .L1915
	b	.L1932
.L1914:
	mov	x1, x20
	mov	x2, 8
	bl	_copy_from_user
	cbz	x0, .L1920
	mov	x2, x20
	mov	w1, 276
	b	.L1933
.L1920:
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L1918
	ldrh	w2, [x19, 6]
	cmp	w2, 4087
	bhi	.L1918
	add	w2, w2, 8
	mov	x1, x20
	and	x2, x2, 65535
	mov	x0, x19
	bl	_copy_from_user
	cbz	x0, .L1921
	mov	x2, x20
	mov	w1, 283
	b	.L1933
.L1921:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_sftl_vendor_write
	sxtw	x20, w0
	b	.L1915
.L1922:
	mov	x20, -1
	b	.L1911
	.size	rk_sftl_vendor_storage_ioctl, .-rk_sftl_vendor_storage_ioctl
	.align	2
	.global	rk_sftl_vendor_register
	.type	rk_sftl_vendor_register, %function
rk_sftl_vendor_register:
	hint	34 // bti c
	.section	__patchable_function_entries
	.align	3
	.8byte	.LPFE142
	.text
.LPFE142:
	nop
	nop
	hint	25 // paciasp
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR2
	mov	x29, sp
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 16
	bl	misc_register
	ldp	x29, x30, [sp], 16
	hint	29 // autiasp
	ret
	.size	rk_sftl_vendor_register, .-rk_sftl_vendor_register
	.section	.rodata.str1.1
.LC135:
	.string	"vendor_storage"
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
	.type	__func__.30, %object
	.size	__func__.30, 17
__func__.30:
	.string	"INSERT_DATA_LIST"
	.type	__func__.31, %object
	.size	__func__.31, 17
__func__.31:
	.string	"INSERT_FREE_LIST"
	.type	__func__.29, %object
	.size	__func__.29, 17
__func__.29:
	.string	"List_remove_node"
	.type	__func__.28, %object
	.size	__func__.28, 22
__func__.28:
	.string	"List_update_data_list"
	.type	__func__.22, %object
	.size	__func__.22, 22
__func__.22:
	.string	"select_l2p_ram_region"
	.type	__func__.14, %object
	.size	__func__.14, 16
__func__.14:
	.string	"make_superblock"
	.type	__func__.8, %object
	.size	__func__.8, 19
__func__.8:
	.string	"get_new_active_ppa"
	.type	__func__.1, %object
	.size	__func__.1, 17
__func__.1:
	.string	"FlashEraseBlocks"
	.type	__func__.26, %object
	.size	__func__.26, 26
__func__.26:
	.string	"ftl_map_blk_alloc_new_blk"
	.type	__func__.37, %object
	.size	__func__.37, 11
__func__.37:
	.string	"FtlMemInit"
	.type	__func__.34, %object
	.size	__func__.34, 14
__func__.34:
	.string	"FtlBbt2Bitmap"
	.type	__func__.13, %object
	.size	__func__.13, 18
__func__.13:
	.string	"SupperBlkListInit"
	.type	__func__.3, %object
	.size	__func__.3, 15
__func__.3:
	.string	"FlashReadPages"
	.type	__func__.19, %object
	.size	__func__.19, 14
__func__.19:
	.string	"FtlScanSysBlk"
	.type	__func__.32, %object
	.size	__func__.32, 11
__func__.32:
	.string	"FtlLoadBbt"
	.type	__func__.2, %object
	.size	__func__.2, 15
__func__.2:
	.string	"FlashProgPages"
	.type	__func__.10, %object
	.size	__func__.10, 25
__func__.10:
	.string	"allocate_data_superblock"
	.type	__func__.7, %object
	.size	__func__.7, 16
__func__.7:
	.string	"update_vpc_list"
	.type	__func__.6, %object
	.size	__func__.6, 20
__func__.6:
	.string	"decrement_vpc_count"
	.type	__func__.23, %object
	.size	__func__.23, 16
__func__.23:
	.string	"FtlMapWritePage"
	.type	__func__.27, %object
	.size	__func__.27, 16
__func__.27:
	.string	"load_l2p_region"
	.type	__func__.25, %object
	.size	__func__.25, 15
__func__.25:
	.string	"ftl_map_blk_gc"
	.type	__func__.24, %object
	.size	__func__.24, 31
__func__.24:
	.string	"Ftl_write_map_blk_to_last_page"
	.type	__func__.21, %object
	.size	__func__.21, 9
__func__.21:
	.string	"log2phys"
	.type	__func__.16, %object
	.size	__func__.16, 16
__func__.16:
	.string	"FtlReUsePrevPpa"
	.type	__func__.15, %object
	.size	__func__.15, 22
__func__.15:
	.string	"FtlRecoverySuperblock"
	.type	__func__.11, %object
	.size	__func__.11, 14
__func__.11:
	.string	"ftl_check_vpc"
	.type	__func__.20, %object
	.size	__func__.20, 15
__func__.20:
	.string	"FtlVpcTblFlush"
	.type	__func__.12, %object
	.size	__func__.12, 21
__func__.12:
	.string	"FtlVpcCheckAndModify"
	.type	__func__.9, %object
	.size	__func__.9, 29
__func__.9:
	.string	"allocate_new_data_superblock"
	.type	__func__.36, %object
	.size	__func__.36, 13
__func__.36:
	.string	"FtlProgPages"
	.type	__func__.5, %object
	.size	__func__.5, 19
__func__.5:
	.string	"FtlGcFreeTempBlock"
	.type	__func__.4, %object
	.size	__func__.4, 23
__func__.4:
	.string	"rk_ftl_garbage_collect"
	.type	__func__.35, %object
	.size	__func__.35, 9
__func__.35:
	.string	"FtlWrite"
	.type	__func__.18, %object
	.size	__func__.18, 15
__func__.18:
	.string	"FtlLoadSysInfo"
	.type	__func__.17, %object
	.size	__func__.17, 18
__func__.17:
	.string	"FtlMapTblRecovery"
	.type	__func__.0, %object
	.size	__func__.0, 14
__func__.0:
	.string	"FtlWriteToIDB"
	.zero	1
	.type	rk_sftl_vendor_storage_fops, %object
	.size	rk_sftl_vendor_storage_fops, 256
rk_sftl_vendor_storage_fops:
	.zero	80
	.xword	rk_sftl_vendor_storage_ioctl
	.xword	rk_sftl_vendor_storage_ioctl
	.zero	160
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
	.xword	.LC135
	.xword	rk_sftl_vendor_storage_fops
	.zero	56
	.bss
	.align	3
	.set	.LANCHOR0,. + 0
	.type	g_nand_phy_info, %object
	.size	g_nand_phy_info, 24
g_nand_phy_info:
	.zero	24
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
	.type	g_gc_bad_block_temp_tbl, %object
	.size	g_gc_bad_block_temp_tbl, 34
g_gc_bad_block_temp_tbl:
	.zero	34
	.type	g_gc_bad_block_gc_index, %object
	.size	g_gc_bad_block_gc_index, 2
g_gc_bad_block_gc_index:
	.zero	2
	.zero	2
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
	.ident	"GCC: (GNU Toolchain for the A-profile Architecture 10.2-2020.11 (arm-10.16)) 10.2.1 20201103"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align	3
	.word	4
	.word	16
	.word	5
	.string	"GNU"
	.word	3221225472
	.word	4
	.word	3
	.align	3
