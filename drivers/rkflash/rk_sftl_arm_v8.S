/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-07-13
 */
	.arch armv8-a
	.file	"rk_sftl.c"
	.text
.Ltext0:
	.cfi_sections	.debug_frame
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
	.align	2
	.type	l2p_addr_tran.isra.0, %function
l2p_addr_tran.isra.0:
.LFB269:
	.file 1 "drivers/rkflash/rksftl/sftl_flash.c"
	.loc 1 17 0
	.cfi_startproc
.LVL0:
	.loc 1 25 0
	adrp	x4, g_nand_phy_info
	add	x4, x4, :lo12:g_nand_phy_info
	ldrh	w3, [x4, 8]
.LVL1:
	.loc 1 26 0
	ldrh	w6, [x4, 10]
.LVL2:
	.loc 1 27 0
	ldrh	w4, [x4, 14]
	cmp	w4, 4
	bne	.L2
	.loc 1 28 0
	lsr	w3, w3, 1
	.loc 1 29 0
	ubfiz	w6, w6, 1, 15
.L2:
	.loc 1 33 0
	lsr	w5, w0, 10
	.loc 1 37 0
	and	w0, w0, 1023
	.loc 1 34 0
	and	w4, w5, 65535
	.loc 1 35 0
	and	w5, w5, 65535
	.loc 1 34 0
	udiv	w4, w4, w3
.LVL3:
	.loc 1 35 0
	msub	w3, w3, w4, w5
.LVL4:
	.loc 1 37 0
	madd	w3, w3, w6, w0
.LVL5:
	.loc 1 41 0
	mov	w0, 0
	.loc 1 37 0
	str	w3, [x1]
	.loc 1 38 0
	str	w4, [x2]
	.loc 1 41 0
	ret
	.cfi_endproc
.LFE269:
	.size	l2p_addr_tran.isra.0, .-l2p_addr_tran.isra.0
	.align	2
	.type	ftl_set_blk_mode.part.7, %function
ftl_set_blk_mode.part.7:
.LFB276:
	.file 2 "drivers/rkflash/rksftl/sftl_sys.c"
	.loc 2 1395 0
	.cfi_startproc
.LVL6:
	.loc 2 1395 0
	and	w0, w0, 65535
	.loc 2 1398 0
	adrp	x2, .LANCHOR0
	ubfx	x1, x0, 5, 11
	ldr	x3, [x2, #:lo12:.LANCHOR0]
	lsl	x1, x1, 2
	mov	w2, 1
	lsl	w2, w2, w0
	ldr	w0, [x3, x1]
.LVL7:
	orr	w0, w0, w2
	str	w0, [x3, x1]
	ret
	.cfi_endproc
.LFE276:
	.size	ftl_set_blk_mode.part.7, .-ftl_set_blk_mode.part.7
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
.LFB141:
	.file 3 "drivers/rkflash/rksftl/sftl_plat.c"
	.loc 3 127 0
	.cfi_startproc
.LVL8:
	.loc 3 129 0
	mov	w2, 1
.LVL9:
	.loc 3 131 0
	mov	w1, 0
.L5:
	.loc 3 131 0 is_stmt 0 discriminator 1
	cmp	w2, w0
	bls	.L6
	.loc 3 133 0 is_stmt 1
	sub	w0, w1, #1
.LVL10:
	.loc 3 135 0
	ret
.LVL11:
.L6:
	.loc 3 131 0 discriminator 3
	add	w1, w1, 1
	.loc 3 132 0 discriminator 3
	lsl	w2, w2, 1
	.loc 3 131 0 discriminator 3
	and	w1, w1, 65535
	b	.L5
	.cfi_endproc
.LFE141:
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
	.type	FtlPrintInfo, %function
FtlPrintInfo:
.LFB142:
	.loc 3 159 0
	.cfi_startproc
.LVL12:
	ret
	.cfi_endproc
.LFE142:
	.size	FtlPrintInfo, .-FtlPrintInfo
	.align	2
	.global	FtlSysBlkNumInit
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
.LFB143:
	.loc 3 165 0
	.cfi_startproc
.LVL13:
	.loc 3 165 0
	and	w0, w0, 65535
	mov	w1, 24
	cmp	w0, 24
	csel	w0, w0, w1, cs
.LVL14:
	.loc 3 168 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	and	w0, w0, 65535
.LVL15:
	.loc 3 169 0
	ldrh	w2, [x1, 12]
	.loc 3 170 0
	ldrh	w3, [x1, 22]
	.loc 3 168 0
	str	w0, [x1, 8]
	.loc 3 169 0
	mul	w2, w2, w0
	.loc 3 170 0
	sub	w0, w3, w0
.LVL16:
	strh	w0, [x1, 20]
	.loc 3 171 0
	ldr	w0, [x1, 28]
	.loc 3 169 0
	str	w2, [x1, 16]
	.loc 3 171 0
	sub	w2, w0, w2
	.loc 3 174 0
	mov	w0, 0
	.loc 3 171 0
	str	w2, [x1, 24]
	.loc 3 174 0
	ret
	.cfi_endproc
.LFE143:
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
.LFB144:
	.loc 3 183 0
	.cfi_startproc
.LVL17:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	mov	x7, x0
	.loc 3 188 0
	adrp	x4, .LANCHOR0
	add	x1, x4, :lo12:.LANCHOR0
	.loc 3 183 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 3 188 0
	ldrh	w3, [x0]
	.loc 3 189 0
	ldrh	w6, [x0, 2]
	.loc 3 190 0
	ldrh	w0, [x0, 4]
.LVL18:
	.loc 3 191 0
	ldrh	w2, [x7, 14]
	.loc 3 188 0
	strh	w3, [x1, 32]
	.loc 3 189 0
	strh	w6, [x1, 34]
	.loc 3 191 0
	cmp	w2, 4
	.loc 3 190 0
	strh	w0, [x1, 36]
	.loc 3 191 0
	bne	.L10
	.loc 3 192 0
	ldrh	w2, [x7, 6]
	lsr	w2, w2, 1
	strh	w2, [x1, 22]
	.loc 3 193 0
	mov	w2, 8
	strh	w2, [x1, 38]
.L10:
	.loc 3 197 0 discriminator 3
	add	x2, x4, :lo12:.LANCHOR0
	.loc 3 183 0 discriminator 3
	mov	x1, 0
	.loc 3 197 0 discriminator 3
	add	x5, x2, 40
.L11:
	strb	w1, [x1, x5]
	add	x1, x1, 1
	.loc 3 196 0 discriminator 3
	cmp	x1, 32
	bne	.L11
	.loc 3 198 0
	mov	w1, 5
	.loc 3 199 0
	strh	wzr, [x2, 74]
	.loc 3 198 0
	strh	w1, [x2, 72]
	.loc 3 200 0
	cmp	w3, 1
	bne	.L12
	.loc 3 201 0
	strh	w3, [x2, 72]
.L12:
	.loc 3 202 0
	add	x3, x4, :lo12:.LANCHOR0
	.loc 3 204 0
	mul	w6, w6, w0
	.loc 3 202 0
	mov	w1, 640
	.loc 3 204 0
	and	w6, w6, 65535
	.loc 3 205 0
	ldrh	w5, [x3, 22]
	.loc 3 202 0
	strh	w1, [x3, 76]
	.loc 3 204 0
	strh	w6, [x3, 12]
	.loc 3 205 0
	mul	w0, w0, w5
	and	w0, w0, 65535
	strh	w0, [x3, 78]
	.loc 3 206 0
	bl	Ftl_log2
.LVL19:
	.loc 3 207 0
	ldrh	w9, [x7, 12]
	.loc 3 210 0
	ldrh	w8, [x3, 38]
	.loc 3 206 0
	strh	w0, [x3, 80]
	.loc 3 207 0
	strh	w9, [x3, 82]
	.loc 3 209 0
	mul	w0, w6, w9
	.loc 3 208 0
	strh	w9, [x3, 84]
	.loc 3 209 0
	strh	w0, [x3, 86]
	.loc 3 210 0
	mov	w0, w8
	bl	Ftl_log2
.LVL20:
	and	w2, w0, 65535
	strh	w0, [x3, 88]
	.loc 3 212 0
	ubfiz	w0, w8, 9, 7
	strh	w0, [x3, 90]
	.loc 3 213 0
	ubfx	w0, w0, 8, 8
	strh	w0, [x3, 92]
	.loc 3 214 0
	ldrh	w0, [x7, 20]
	strh	w0, [x3, 94]
	.loc 3 216 0
	mul	w0, w6, w5
	str	w0, [x3, 28]
	.loc 3 217 0
	mul	w0, w0, w8
	.loc 3 218 0
	mul	w8, w8, w9
	.loc 3 217 0
	mul	w0, w0, w9
	asr	w0, w0, 11
	str	w0, [x3, 96]
	.loc 3 218 0
	mov	w0, 5120
	sdiv	w0, w0, w8
	and	w0, w0, 65535
	.loc 3 219 0
	cmp	w0, 4
	bls	.L13
.L19:
	.loc 3 221 0
	add	x1, x4, :lo12:.LANCHOR0
	.loc 3 220 0
	strh	w0, [x3, 100]
	.loc 3 221 0
	mov	w0, 640
	.loc 3 223 0
	lsl	w5, w5, 6
	.loc 3 221 0
	asr	w0, w0, w2
	.loc 3 223 0
	add	w2, w2, 9
	.loc 3 221 0
	add	w0, w0, 2
	strh	w0, [x1, 102]
	.loc 3 223 0
	asr	w5, w5, w2
	strh	w5, [x1, 104]
	and	w5, w5, 65535
	.loc 3 227 0
	cmp	w6, 1
	.loc 3 224 0
	mul	w0, w5, w6
	.loc 3 226 0
	add	w5, w5, 8
	.loc 3 224 0
	str	w0, [x1, 108]
	.loc 3 226 0
	ldrh	w0, [x1, 100]
	udiv	w0, w0, w6
	add	w5, w0, w5
	.loc 3 227 0
	beq	.L15
.L20:
	.loc 3 231 0
	add	x4, x4, :lo12:.LANCHOR0
	.loc 3 228 0
	str	w5, [x1, 8]
	.loc 3 231 0
	ldrh	w0, [x4, 8]
	bl	FtlSysBlkNumInit
.LVL21:
	.loc 3 232 0
	ldr	w0, [x4, 8]
	str	w0, [x4, 112]
	.loc 3 234 0
	ldr	w0, [x4, 24]
	ldrh	w1, [x4, 82]
	.loc 3 236 0
	str	wzr, [x4, 120]
	.loc 3 234 0
	lsl	w0, w0, 2
	mul	w0, w0, w1
	ldrh	w1, [x4, 88]
	add	w1, w1, 9
	lsr	w0, w0, w1
	add	w0, w0, 2
	strh	w0, [x4, 116]
	.loc 3 235 0
	mov	w0, 32
	strh	w0, [x4, 118]
	.loc 3 237 0
	ldrh	w0, [x4, 100]
	add	w0, w0, 3
	strh	w0, [x4, 100]
	.loc 3 238 0
	ldr	w0, [x4, 108]
	add	w0, w0, 3
	str	w0, [x4, 108]
	.loc 3 263 0
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
.L13:
	.cfi_restore_state
	.loc 3 220 0
	mov	w0, 4
	b	.L19
.L15:
	.loc 3 228 0
	add	w5, w5, 4
	b	.L20
	.cfi_endproc
.LFE144:
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
.LFB147:
	.loc 3 514 0
	.cfi_startproc
.LVL22:
	.loc 3 517 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	.loc 3 514 0
	and	w0, w0, 65535
	.loc 3 517 0
	ldrh	w2, [x1, 124]
	cbz	w2, .L25
.LBB168:
.LBB169:
	.loc 3 518 0
	ldrh	w2, [x1, 100]
	.loc 3 519 0
	ldr	x3, [x1, 128]
	mov	x1, 0
.L23:
	.loc 3 518 0
	cmp	w2, w1, uxth
	bhi	.L24
.L25:
.LBE169:
.LBE168:
	.loc 3 524 0
	mov	w0, 0
.LVL23:
	ret
.LVL24:
.L24:
	add	x1, x1, 1
.LBB171:
.LBB170:
	.loc 3 519 0
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L23
	.loc 3 520 0
	mov	w0, 1
.LVL25:
.LBE170:
.LBE171:
	.loc 3 525 0
	ret
	.cfi_endproc
.LFE147:
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	sftl_get_density
	.type	sftl_get_density, %function
sftl_get_density:
.LFB160:
	.loc 3 1214 0
	.cfi_startproc
	.loc 3 1216 0
	adrp	x0, .LANCHOR0+120
	ldr	w0, [x0, #:lo12:.LANCHOR0+120]
	ret
	.cfi_endproc
.LFE160:
	.size	sftl_get_density, .-sftl_get_density
	.align	2
	.global	FtlBbmMapBadBlock
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
.LFB161:
	.file 4 "drivers/rkflash/rksftl/sftl_bbm.c"
	.loc 4 5 0
	.cfi_startproc
.LVL26:
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	and	w1, w0, 65535
.LVL27:
	.loc 4 15 0
	mov	w4, 1
	.loc 4 5 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 4 10 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	.loc 4 13 0
	add	x19, x19, 136
	.loc 4 10 0
	ldrh	w0, [x19, -58]
	udiv	w3, w1, w0
	and	w2, w3, 65535
.LVL28:
	msub	w3, w3, w0, w1
	.loc 4 13 0
	add	x0, x19, x2, uxth 3
	and	w3, w3, 65535
	.loc 4 15 0
	ldr	x0, [x0, 32]
	ubfx	x5, x3, 5, 11
	lsl	x5, x5, 2
	lsl	w4, w4, w3
	ldr	w6, [x0, x5]
	orr	w4, w4, w6
	str	w4, [x0, x5]
	.loc 4 16 0
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
.LVL29:
	.loc 4 17 0
	ldrh	w0, [x19, 6]
	add	w0, w0, 1
	strh	w0, [x19, 6]
	.loc 4 19 0
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE161:
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
.LFB162:
	.loc 4 24 0
	.cfi_startproc
.LVL30:
	.loc 4 30 0
	adrp	x2, .LANCHOR0
	add	x3, x2, :lo12:.LANCHOR0
	.loc 4 24 0
	and	w0, w0, 65535
	.loc 4 30 0
	ldrh	w1, [x3, 78]
	.loc 4 31 0
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
.LVL31:
	.loc 4 33 0
	add	x2, x3, x2, uxth 3
	.loc 4 31 0
	and	w0, w0, 65535
.LVL32:
	.loc 4 34 0
	ldr	x1, [x2, 168]
	ubfx	x3, x0, 5, 11
	ldr	w1, [x1, x3, lsl 2]
	lsr	w0, w1, w0
.LVL33:
	.loc 4 36 0
	and	w0, w0, 1
	ret
	.cfi_endproc
.LFE162:
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
	.type	FtlBbtInfoPrint, %function
FtlBbtInfoPrint:
.LFB163:
	.loc 4 39 0
	.cfi_startproc
	ret
	.cfi_endproc
.LFE163:
	.size	FtlBbtInfoPrint, .-FtlBbtInfoPrint
	.align	2
	.global	FtlBbtCalcTotleCnt
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
.LFB167:
	.loc 4 157 0
	.cfi_startproc
	.loc 4 160 0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w4, 0
.LVL34:
	.loc 4 159 0
	mov	w5, 0
.LVL35:
	.loc 4 160 0
	ldrh	w6, [x0, 78]
	ldrh	w0, [x0, 34]
	mul	w6, w6, w0
	cmp	w4, w6
	blt	.L42
	.loc 4 169 0
	mov	w0, w5
	ret
.L42:
	.loc 4 157 0
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x29, sp, 0
	.cfi_def_cfa_register 29
.L35:
	.loc 4 162 0
	mov	w0, w4
	bl	FtlBbmIsBadBlock
.LVL36:
	cbz	w0, .L34
	.loc 4 164 0
	add	w5, w5, 1
.LVL37:
	and	w5, w5, 65535
.LVL38:
.L34:
	.loc 4 160 0 discriminator 2
	add	w4, w4, 1
.LVL39:
	and	w4, w4, 65535
.LVL40:
	cmp	w4, w6
	blt	.L35
	.loc 4 169 0
	mov	w0, w5
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE167:
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.type	V2P_block, %function
V2P_block:
.LFB171:
	.loc 2 5 0
	.cfi_startproc
.LVL41:
	.loc 2 8 0
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	.loc 2 5 0
	and	w0, w0, 65535
	and	w1, w1, 65535
	.loc 2 8 0
	ldrh	w2, [x4, 36]
	ldrh	w4, [x4, 78]
	udiv	w3, w0, w2
	msub	w0, w3, w2, w0
.LVL42:
	madd	w2, w2, w1, w0
	madd	w0, w3, w4, w2
	.loc 2 10 0
	ret
	.cfi_endproc
.LFE171:
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.type	P2V_plane, %function
P2V_plane:
.LFB172:
	.loc 2 13 0
	.cfi_startproc
.LVL43:
	.loc 2 16 0
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	.loc 2 13 0
	and	w3, w0, 65535
.LVL44:
	.loc 2 16 0
	ldrh	w1, [x2, 36]
	ldrh	w2, [x2, 78]
	udiv	w0, w3, w1
	udiv	w2, w3, w2
	msub	w0, w0, w1, w3
	madd	w0, w1, w2, w0
	.loc 2 18 0
	ret
	.cfi_endproc
.LFE172:
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
.LFB173:
	.loc 2 21 0
	.cfi_startproc
.LVL45:
	.loc 2 24 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	.loc 2 21 0
	and	w3, w0, 65535
.LVL46:
	.loc 2 24 0
	ldrh	w2, [x1, 78]
	.loc 2 25 0
	ldrh	w1, [x1, 36]
	.loc 2 24 0
	udiv	w0, w3, w2
	msub	w0, w0, w2, w3
	and	w0, w0, 65535
	.loc 2 27 0
	udiv	w0, w0, w1
	ret
	.cfi_endproc
.LFE173:
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
.LFB174:
	.loc 2 31 0
	.cfi_startproc
.LVL47:
	.loc 2 34 0
	cmp	w0, w1
	mov	w2, -2147483648
	bls	.L47
	.loc 2 36 0
	sub	w1, w0, w1
.LVL48:
	cmp	w1, w2
	cset	w0, ls
.LVL49:
	ret
.LVL50:
.L47:
	.loc 2 40 0
	sub	w1, w1, w0
.LVL51:
	cmp	w1, w2
	cset	w0, hi
.LVL52:
	.loc 2 44 0
	ret
	.cfi_endproc
.LFE174:
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
.LFB177:
	.loc 2 88 0
	.cfi_startproc
	.loc 2 89 0
	adrp	x0, .LANCHOR0+238
	ldrh	w0, [x0, #:lo12:.LANCHOR0+238]
	cmp	w0, 0
	.loc 2 90 0
	cset	w0, eq
	ret
	.cfi_endproc
.LFE177:
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
.LFB178:
	.loc 2 93 0
	.cfi_startproc
	.loc 2 94 0
	adrp	x0, .LANCHOR0+238
	ldrh	w0, [x0, #:lo12:.LANCHOR0+238]
	cmp	w0, 1024
	.loc 2 95 0
	cset	w0, eq
	ret
	.cfi_endproc
.LFE178:
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
.LFB180:
	.loc 2 118 0
	.cfi_startproc
	.loc 2 123 0
	adrp	x0, .LANCHOR0
	add	x2, x0, :lo12:.LANCHOR0
	add	x1, x2, 232
	ldrh	w3, [x1, 6]
	cbz	w3, .L51
	.loc 2 119 0
	ldrh	w5, [x2, 2316]
	mov	w6, 0
	ldrh	w3, [x1, 2]
	mov	w4, 0
.LVL53:
	ldrh	w2, [x1, 4]
	and	w5, w5, 31
.LVL54:
.L53:
	.loc 2 124 0 discriminator 1
	cmp	w5, w4
	bgt	.L54
	cbz	w6, .L51
	add	x0, x0, :lo12:.LANCHOR0
	strh	w3, [x0, 234]
	strh	w2, [x0, 236]
.L51:
	ret
.L54:
	.loc 2 125 0 discriminator 3
	add	x6, x1, x3, sxtw 1
	.loc 2 124 0 discriminator 3
	add	w4, w4, 1
.LVL55:
	.loc 2 126 0 discriminator 3
	add	w3, w3, 1
	.loc 2 124 0 discriminator 3
	and	w4, w4, 65535
.LVL56:
	.loc 2 126 0 discriminator 3
	and	w3, w3, 1023
	.loc 2 125 0 discriminator 3
	ldrh	w7, [x6, 8]
	.loc 2 127 0 discriminator 3
	add	x6, x1, x2, sxtw 1
	strh	w7, [x6, 8]
	mov	w6, 1
	.loc 2 128 0 discriminator 3
	add	w2, w2, w6
	and	w2, w2, 1023
	b	.L53
	.cfi_endproc
.LFE180:
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.type	IsInFreeQueue, %function
IsInFreeQueue:
.LFB181:
	.loc 2 163 0
	.cfi_startproc
.LVL57:
.LBB172:
.LBB173:
	.loc 2 94 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 232
.LBE173:
.LBE172:
	.loc 2 163 0
	and	w0, w0, 65535
.LBB175:
.LBB174:
	.loc 2 94 0
	ldrh	w4, [x1, 6]
.LBE174:
.LBE175:
	.loc 2 168 0
	cmp	w4, 1024
	beq	.L64
	.loc 2 171 0
	ldrh	w5, [x1, 2]
	mov	w3, 0
.LVL58:
.L62:
	.loc 2 170 0 discriminator 1
	cmp	w3, w4
	bcc	.L63
.L64:
	.loc 2 164 0
	mov	w0, 0
.LVL59:
	ret
.LVL60:
.L63:
	.loc 2 171 0
	add	w2, w3, w5
	ubfiz	x2, x2, 1, 10
	add	x2, x1, x2
	ldrh	w2, [x2, 8]
	cmp	w2, w0
	beq	.L65
	.loc 2 170 0 discriminator 2
	add	w3, w3, 1
	b	.L62
.L65:
	.loc 2 172 0
	mov	w0, 1
.LVL61:
	.loc 2 178 0
	ret
	.cfi_endproc
.LFE181:
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
.LFB185:
	.loc 2 249 0
	.cfi_startproc
.LVL62:
	.loc 2 260 0
	adrp	x2, .LANCHOR0
	add	x3, x2, :lo12:.LANCHOR0
	.loc 2 249 0
	and	w0, w0, 65535
	.loc 2 260 0
	ldrh	w1, [x3, 20]
	cmp	w1, w0
	bls	.L82
.LBB178:
.LBB179:
	.loc 2 265 0
	mov	w7, 6
	ldr	x12, [x3, 2336]
	.loc 2 266 0
	mov	w1, -1
	.loc 2 265 0
	umull	x11, w0, w7
	add	x5, x12, x11
.LVL63:
	.loc 2 266 0
	strh	w1, [x5, 2]
	strh	w1, [x12, x11]
	.loc 2 268 0
	ldr	x1, [x3, 2344]
.LVL64:
	cbnz	x1, .L69
	.loc 2 269 0
	str	x5, [x3, 2344]
.L82:
.LBE179:
.LBE178:
	.loc 2 323 0
	mov	w0, 0
.LVL65:
	ret
.LVL66:
.L69:
	.loc 2 249 0
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
.LBB183:
.LBB180:
	.loc 2 273 0
	ubfiz	x8, x0, 1, 16
	mov	x4, x2
	.loc 2 279 0
	mov	x9, -6148914691236517206
.LBE180:
.LBE183:
	.loc 2 249 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
.LBB184:
.LBB181:
	.loc 2 273 0
	ldr	x14, [x3, 2352]
	.loc 2 279 0
	movk	x9, 0xaaab, lsl 0
	.loc 2 274 0
	ldrh	w2, [x5, 4]
	mov	w10, -1
	.loc 2 279 0
	ldr	x13, [x3, 2336]
	.loc 2 301 0
	mov	w30, 65535
	.loc 2 274 0
	cmp	w2, 0
	.loc 2 294 0
	ldr	x15, [x3, 2360]
	.loc 2 273 0
	ldrh	w6, [x14, x8]
.LVL67:
	.loc 2 282 0
	ldrh	w18, [x3, 20]
	.loc 2 256 0
	mov	w3, 0
.LVL68:
	.loc 2 274 0
	mul	w6, w6, w2
.LVL69:
	.loc 2 279 0
	sub	x2, x1, x13
	asr	x2, x2, 1
	.loc 2 274 0
	csel	w6, w6, w10, ne
.LVL70:
	.loc 2 279 0
	mul	x2, x2, x9
	.loc 2 294 0
	add	x9, x15, x8
	.loc 2 309 0
	mov	w8, w7
	.loc 2 279 0
	and	w2, w2, 65535
.L77:
	.loc 2 281 0
	add	w3, w3, 1
.LVL71:
	and	w3, w3, 65535
.LVL72:
	.loc 2 282 0
	cmp	w3, w18
	bhi	.L68
	.loc 2 285 0
	cmp	w0, w2
	beq	.L68
	.loc 2 288 0
	ubfiz	x16, x2, 1, 16
	.loc 2 289 0
	ldrh	w17, [x1, 4]
	cmp	w17, 0
	.loc 2 288 0
	ldrh	w7, [x14, x16]
.LVL73:
	.loc 2 289 0
	mul	w7, w7, w17
.LVL74:
	csel	w7, w7, w10, ne
.LVL75:
	.loc 2 293 0
	cmp	w6, w7
	bne	.L73
	.loc 2 294 0
	ldrh	w16, [x15, x16]
	ldrh	w7, [x9]
.LVL76:
	cmp	w16, w7
	bcc	.L75
.LVL77:
.L74:
	.loc 2 312 0
	strh	w2, [x12, x11]
	.loc 2 313 0
	ldrh	w2, [x1, 2]
	strh	w2, [x5, 2]
	.loc 2 315 0
	add	x2, x4, :lo12:.LANCHOR0
	ldr	x3, [x2, 2344]
.LVL78:
	cmp	x1, x3
	bne	.L78
	.loc 2 316 0
	strh	w0, [x1, 2]
	.loc 2 317 0
	str	x5, [x2, 2344]
	b	.L68
.LVL79:
.L73:
	.loc 2 298 0
	bcc	.L74
.L75:
	.loc 2 301 0
	ldrh	w7, [x1]
.LVL80:
	cmp	w7, w30
	bne	.L76
	.loc 2 302 0
	strh	w2, [x5, 2]
	.loc 2 304 0
	add	x2, x4, :lo12:.LANCHOR0
	.loc 2 303 0
	strh	w0, [x1]
	.loc 2 304 0
	str	x5, [x2, 2368]
.LVL81:
.L68:
.LBE181:
.LBE184:
	.loc 2 323 0
	mov	w0, 0
.LVL82:
	ldp	x29, x30, [sp], 16
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
.LVL83:
.L76:
	.cfi_restore_state
.LBB185:
.LBB182:
	.loc 2 309 0
	umaddl	x1, w7, w8, x13
	mov	w2, w7
	b	.L77
.LVL84:
.L78:
	.loc 2 319 0
	ldrh	w3, [x1, 2]
	mov	w4, 6
	ldr	x2, [x2, 2336]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	.loc 2 320 0
	strh	w0, [x1, 2]
	b	.L68
.LBE182:
.LBE185:
	.cfi_endproc
.LFE185:
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
.LFB184:
	.loc 2 218 0
	.cfi_startproc
.LVL85:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 2 219 0
	bl	insert_data_list
.LVL86:
	.loc 2 220 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2376]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 2376]
	.loc 2 221 0
	ldrh	w1, [x1, 20]
	cmp	w1, w0
	bcs	.L84
.LBB188:
.LBB189:
	adrp	x1, .LANCHOR1
	adrp	x0, .LC1
	mov	w2, 221
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL87:
.L84:
.LBE189:
.LBE188:
	.loc 2 222 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE184:
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.type	insert_free_list, %function
insert_free_list:
.LFB186:
	.loc 2 326 0
	.cfi_startproc
.LVL88:
	.loc 2 326 0
	and	w0, w0, 65535
	.loc 2 332 0
	mov	w7, 65535
	cmp	w0, w7
	beq	.L88
	.loc 2 335 0
	adrp	x2, .LANCHOR0
	add	x5, x2, :lo12:.LANCHOR0
	mov	w6, 6
	.loc 2 336 0
	mov	w1, -1
	mov	x3, x2
	.loc 2 335 0
	ldr	x9, [x5, 2336]
	umull	x8, w0, w6
	add	x4, x9, x8
.LVL89:
	.loc 2 336 0
	strh	w1, [x4, 2]
	strh	w1, [x9, x8]
	.loc 2 338 0
	ldr	x1, [x5, 2384]
.LVL90:
	cbnz	x1, .L89
	.loc 2 339 0
	str	x4, [x5, 2384]
.LVL91:
.L88:
	.loc 2 372 0
	mov	w0, 0
.LVL92:
	ret
.LVL93:
.L89:
	.loc 2 343 0
	ldr	x11, [x5, 2360]
	ubfiz	x2, x0, 1, 16
	.loc 2 345 0
	ldr	x10, [x5, 2336]
	mov	x5, -6148914691236517206
	movk	x5, 0xaaab, lsl 0
	.loc 2 343 0
	ldrh	w12, [x11, x2]
.LVL94:
	.loc 2 345 0
	sub	x2, x1, x10
	asr	x2, x2, 1
	mul	x2, x2, x5
	and	w2, w2, 65535
.L92:
	.loc 2 348 0
	ubfiz	x5, x2, 1, 16
	.loc 2 349 0
	ldrh	w5, [x11, x5]
	cmp	w5, w12
	bcs	.L90
	.loc 2 352 0
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L91
	.loc 2 353 0
	strh	w2, [x4, 2]
	.loc 2 354 0
	strh	w0, [x1]
	.loc 2 355 0
	b	.L88
.L91:
	.loc 2 358 0
	umaddl	x1, w5, w6, x10
	.loc 2 348 0
	mov	w2, w5
	b	.L92
.L90:
	.loc 2 361 0
	ldrh	w5, [x1, 2]
	strh	w5, [x4, 2]
	.loc 2 362 0
	strh	w2, [x9, x8]
	.loc 2 364 0
	add	x2, x3, :lo12:.LANCHOR0
	ldr	x3, [x2, 2384]
	cmp	x1, x3
	bne	.L93
	.loc 2 365 0
	strh	w0, [x1, 2]
	.loc 2 366 0
	str	x4, [x2, 2384]
	b	.L88
.L93:
	.loc 2 368 0
	ldrh	w3, [x1, 2]
	mov	w4, 6
.LVL95:
	ldr	x2, [x2, 2336]
	umull	x3, w3, w4
	strh	w0, [x2, x3]
	.loc 2 369 0
	strh	w0, [x1, 2]
	b	.L88
	.cfi_endproc
.LFE186:
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
.LFB183:
	.loc 2 211 0
	.cfi_startproc
.LVL96:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 2 212 0
	bl	insert_free_list
.LVL97:
	.loc 2 213 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2392]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 2392]
	.loc 2 214 0
	ldrh	w1, [x1, 20]
	cmp	w1, w0
	bcs	.L94
.LBB192:
.LBB193:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 214
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL98:
.L94:
.LBE193:
.LBE192:
	.loc 2 215 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE183:
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
.LFB187:
	.loc 2 375 0
	.cfi_startproc
.LVL99:
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	and	w1, w1, 65535
	.loc 2 378 0
	mov	w2, 6
	.loc 2 375 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	.loc 2 378 0
	adrp	x19, .LANCHOR0
	.loc 2 375 0
	str	x23, [sp, 48]
	.cfi_offset 23, -16
	mov	x23, x0
	.loc 2 378 0
	add	x0, x19, :lo12:.LANCHOR0
.LVL100:
	.loc 2 375 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.loc 2 378 0
	umull	x21, w1, w2
	ldr	x22, [x0, 2336]
	.loc 2 379 0
	mov	w0, 65535
	.loc 2 378 0
	add	x20, x22, x21
.LVL101:
	.loc 2 379 0
	ldrh	w1, [x20, 2]
.LVL102:
	cmp	w1, w0
	bne	.L98
	.loc 2 379 0 is_stmt 0 discriminator 1
	ldr	x0, [x23]
	cmp	x20, x0
	beq	.L98
	.loc 2 379 0 discriminator 2
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 379
	add	x1, x1, 48
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL103:
.L98:
	.loc 2 381 0 is_stmt 1
	ldr	x0, [x23]
	mov	w1, 65535
	cmp	x20, x0
	ldrh	w0, [x22, x21]
	bne	.L99
	.loc 2 382 0
	cmp	w0, w1
	bne	.L100
	.loc 2 383 0
	str	xzr, [x23]
.L101:
	.loc 2 399 0
	mov	w0, -1
	strh	w0, [x22, x21]
	strh	w0, [x20, 2]
	.loc 2 401 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
.LVL104:
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
.LVL105:
	ldp	x29, x30, [sp], 64
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL106:
.L100:
	.cfi_restore_state
	.loc 2 385 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 6
	ldr	x1, [x19, 2336]
	umaddl	x0, w0, w2, x1
	.loc 2 386 0
	mov	w1, -1
	.loc 2 385 0
	str	x0, [x23]
	.loc 2 386 0
	strh	w1, [x0, 2]
	b	.L101
.L99:
	.loc 2 388 0
	cmp	w0, w1
	ldrh	w1, [x20, 2]
	bne	.L102
	.loc 2 389 0
	cmp	w1, w0
	beq	.L101
	.loc 2 390 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 6
	.loc 2 391 0
	mov	w2, -1
	.loc 2 390 0
	umull	x1, w1, w0
	.loc 2 391 0
	ldr	x0, [x19, 2336]
	strh	w2, [x0, x1]
	b	.L101
.L102:
	.loc 2 394 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 6
	.loc 2 395 0
	ldr	x3, [x19, 2336]
	umaddl	x0, w0, w2, x3
	strh	w1, [x0, 2]
	.loc 2 396 0
	ldrh	w0, [x20, 2]
	.loc 2 397 0
	ldr	x1, [x19, 2336]
	ldrh	w3, [x22, x21]
	.loc 2 396 0
	umull	x0, w0, w2
	.loc 2 397 0
	strh	w3, [x1, x0]
	b	.L101
	.cfi_endproc
.LFE187:
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
.LFB188:
	.loc 2 404 0
	.cfi_startproc
.LVL107:
	.loc 2 408 0
	ldr	x2, [x0]
.LVL108:
	cbz	x2, .L110
	.loc 2 404 0
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 2 413 0
	adrp	x3, .LANCHOR0+2336
	and	w1, w1, 65535
	.loc 2 412 0
	mov	w4, 65535
	.loc 2 404 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 2 413 0
	mov	w5, 6
	ldr	x19, [x3, #:lo12:.LANCHOR0+2336]
.L106:
	.loc 2 412 0
	cbnz	w1, .L107
.L109:
	.loc 2 416 0
	sub	x19, x2, x19
	mov	x2, -6148914691236517206
.LVL109:
	asr	x19, x19, 1
	movk	x2, 0xaaab, lsl 0
	mul	x19, x19, x2
	and	w19, w19, 65535
.LVL110:
	.loc 2 417 0
	mov	w1, w19
.LVL111:
	bl	List_remove_node
.LVL112:
	.loc 2 419 0
	mov	w0, w19
	.loc 2 420 0
	ldr	x19, [sp, 16]
.LVL113:
	ldp	x29, x30, [sp], 32
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
.LVL114:
.L107:
	.cfi_restore_state
	.loc 2 412 0 discriminator 1
	ldrh	w3, [x2]
	cmp	w3, w4
	beq	.L109
	.loc 2 414 0
	sub	w1, w1, #1
.LVL115:
	.loc 2 413 0
	umaddl	x2, w3, w5, x19
	.loc 2 414 0
	and	w1, w1, 65535
.LVL116:
	b	.L106
.L110:
	.cfi_def_cfa 31, 0
	.cfi_restore 19
	.cfi_restore 29
	.cfi_restore 30
	.loc 2 409 0
	mov	w0, 65535
.LVL117:
	ret
	.cfi_endproc
.LFE188:
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_pop_head_node
	.type	List_pop_head_node, %function
List_pop_head_node:
.LFB189:
	.loc 2 423 0
	.cfi_startproc
.LVL118:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 2 424 0
	mov	w1, 0
	.loc 2 423 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 2 424 0
	bl	List_pop_index_node
.LVL119:
	.loc 2 425 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE189:
	.size	List_pop_head_node, .-List_pop_head_node
	.align	2
	.global	List_get_gc_head_node
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
.LFB190:
	.loc 2 428 0
	.cfi_startproc
.LVL120:
	.loc 2 428 0
	and	w2, w0, 65535
.LVL121:
	.loc 2 432 0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 2344]
.LVL122:
	cbz	x1, .L122
	.loc 2 437 0
	ldr	x3, [x0, 2336]
	mov	w4, 6
	.loc 2 436 0
	mov	w0, 65535
.L119:
	cbz	w2, .L120
	.loc 2 436 0 is_stmt 0 discriminator 1
	ldrh	w1, [x1]
.LVL123:
	cmp	w1, w0
	bne	.L121
	ret
.L121:
	.loc 2 438 0 is_stmt 1
	sub	w2, w2, #1
.LVL124:
	.loc 2 437 0
	umaddl	x1, w1, w4, x3
.LVL125:
	.loc 2 438 0
	and	w2, w2, 65535
.LVL126:
	b	.L119
.L122:
	.loc 2 433 0
	mov	w0, 65535
	ret
.L120:
	.loc 2 442 0
	sub	x0, x1, x3
	mov	x1, -6148914691236517206
.LVL127:
	asr	x0, x0, 1
	movk	x1, 0xaaab, lsl 0
	mul	x0, x0, x1
	.loc 2 444 0
	and	w0, w0, 65535
	.loc 2 445 0
	ret
	.cfi_endproc
.LFE190:
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.type	List_update_data_list, %function
List_update_data_list:
.LFB191:
	.loc 2 448 0
	.cfi_startproc
.LVL128:
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	and	w20, w0, 65535
.LVL129:
	.loc 2 457 0
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	.loc 2 448 0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.cfi_offset 23, -16
	.cfi_offset 24, -8
	.loc 2 457 0
	ldrh	w1, [x0, 2400]
	cmp	w1, w20
	beq	.L125
	.loc 2 457 0 is_stmt 0 discriminator 1
	ldrh	w1, [x0, 2448]
	cmp	w1, w20
	beq	.L125
	.loc 2 457 0 discriminator 2
	ldrh	w1, [x0, 2496]
	cmp	w1, w20
	beq	.L125
.LBB196:
.LBB197:
	.loc 2 460 0 is_stmt 1
	mov	w22, 6
	ldr	x24, [x0, 2336]
	.loc 2 462 0
	ldr	x1, [x0, 2344]
	.loc 2 460 0
	umull	x22, w20, w22
	add	x23, x24, x22
.LVL130:
	.loc 2 462 0
	cmp	x23, x1
	beq	.L125
	.loc 2 465 0
	ldr	x2, [x0, 2352]
	ubfiz	x1, x20, 1, 16
	.loc 2 466 0
	ldrh	w0, [x23, 4]
	cmp	w0, 0
	.loc 2 465 0
	ldrh	w21, [x2, x1]
.LVL131:
	.loc 2 470 0
	mov	w1, 65535
	.loc 2 466 0
	mul	w21, w21, w0
.LVL132:
	.loc 2 470 0
	ldrh	w0, [x23, 2]
	.loc 2 466 0
	csinv	w21, w21, wzr, ne
.LVL133:
	.loc 2 470 0
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
.LVL134:
.L128:
	.loc 2 471 0
	ldrh	w0, [x23, 2]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L129
	ldrh	w1, [x24, x22]
	cmp	w1, w0
	beq	.L125
.L129:
	.loc 2 473 0
	mov	w1, 6
	.loc 2 475 0
	add	x22, x19, :lo12:.LANCHOR0
	.loc 2 474 0
	mov	x2, -6148914691236517206
	.loc 2 473 0
	umull	x0, w0, w1
	.loc 2 474 0
	movk	x2, 0xaaab, lsl 0
	asr	x1, x0, 1
	mul	x1, x1, x2
	.loc 2 475 0
	ldr	x2, [x22, 2352]
	ldrh	w1, [x2, x1, lsl 1]
.LVL135:
	.loc 2 476 0
	ldr	x2, [x22, 2336]
	add	x0, x2, x0
	ldrh	w2, [x0, 4]
	cmp	w2, 0
	mul	w0, w1, w2
	csinv	w0, w0, wzr, ne
.LVL136:
	.loc 2 480 0
	cmp	w21, w0
	bcs	.L125
	.loc 2 481 0
	mov	w1, w20
.LVL137:
	add	x0, x22, 2344
.LVL138:
	bl	List_remove_node
.LVL139:
	ldrh	w0, [x22, 2376]
	cbnz	w0, .L131
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 481
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL140:
.L131:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2376]
	sub	w0, w0, #1
	strh	w0, [x19, 2376]
	.loc 2 482 0
	mov	w0, w20
	bl	INSERT_DATA_LIST
.LVL141:
.L125:
.LBE197:
.LBE196:
	.loc 2 485 0
	mov	w0, 0
.LVL142:
	ldp	x19, x20, [sp, 16]
.LVL143:
	ldp	x21, x22, [sp, 32]
.LVL144:
	ldp	x23, x24, [sp, 48]
.LVL145:
	ldp	x29, x30, [sp], 64
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE191:
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
.LFB199:
	.loc 2 749 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 2 754 0
	adrp	x1, .LANCHOR0
	add	x0, x1, :lo12:.LANCHOR0
	mov	x3, 0
	.loc 2 749 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 2 755 0
	mov	w4, 65535
	.loc 2 754 0
	ldrh	w2, [x0, 118]
	.loc 2 755 0
	ldr	x0, [x0, 2544]
.LVL146:
.L136:
	and	w19, w3, 65535
	.loc 2 754 0 discriminator 1
	cmp	w19, w2
	bcc	.L138
	add	x4, x0, 4
	.loc 2 754 0 is_stmt 0
	mov	w19, w2
	mov	w6, -2147483648
	mov	w3, 0
.L139:
	.loc 2 764 0 is_stmt 1 discriminator 1
	cmp	w3, w2
	bne	.L141
	.loc 2 773 0
	cmp	w19, w2
	bcc	.L137
	.loc 2 779 0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w19, w2
	mov	w3, -1
	ldrh	w4, [x1, 2552]
	mov	w1, 0
.L142:
	.loc 2 778 0 discriminator 1
	cmp	w1, w2
	bne	.L144
	.loc 2 784 0
	cmp	w19, w1
	bcc	.L137
	.loc 2 784 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 784
	add	x1, x1, 96
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL147:
	b	.L137
.L138:
	add	x3, x3, 1
	.loc 2 755 0 is_stmt 1
	add	x5, x0, x3, lsl 4
	ldrh	w5, [x5, -16]
	cmp	w5, w4
	bne	.L136
.L137:
	.loc 2 786 0
	mov	w0, w19
	ldr	x19, [sp, 16]
.LVL148:
	ldp	x29, x30, [sp], 32
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
.LVL149:
.L141:
	.cfi_restore_state
	.loc 2 765 0
	ldr	w5, [x4]
	tbnz	w5, #31, .L140
	.loc 2 766 0
	cmp	w6, w5
	bls	.L140
	mov	w6, w5
	mov	w19, w3
.L140:
	.loc 2 764 0 discriminator 2
	add	w3, w3, 1
	add	x4, x4, 16
	and	w3, w3, 65535
	b	.L139
.L144:
	.loc 2 779 0
	ldr	w6, [x0, 4]
	cmp	w3, w6
	bls	.L143
	.loc 2 779 0 is_stmt 0 discriminator 1
	ldrh	w5, [x0]
	cmp	w5, w4
	csel	w3, w3, w6, eq
	csel	w19, w19, w1, eq
.L143:
	.loc 2 778 0 is_stmt 1 discriminator 2
	add	w1, w1, 1
	add	x0, x0, 16
	and	w1, w1, 65535
	b	.L142
	.cfi_endproc
.LFE199:
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
.LFB205:
	.loc 2 931 0
	.cfi_startproc
.LVL150:
	.loc 2 934 0
	adrp	x1, .LANCHOR0
	add	x3, x1, :lo12:.LANCHOR0
	ldrh	w2, [x3, 2554]
	cmp	w2, 4
	bhi	.L147
	.loc 2 934 0 is_stmt 0 discriminator 1
	cbnz	w0, .L147
	.loc 2 934 0
	add	w2, w2, 1
	strh	w2, [x3, 2554]
	.loc 2 943 0 is_stmt 1
	ret
.L147:
	.loc 2 935 0
	add	x0, x1, :lo12:.LANCHOR0
.LVL151:
.LBB200:
.LBB201:
	.loc 2 939 0
	mov	w6, 65535
	mov	x1, 0
	.loc 2 937 0
	ldrh	w4, [x0, 20]
	.loc 2 939 0
	ldr	x5, [x0, 2352]
.LBE201:
.LBE200:
	.loc 2 935 0
	strh	wzr, [x0, 2554]
	.loc 2 936 0
	str	wzr, [x0, 2556]
.L148:
.LBB203:
.LBB202:
	.loc 2 937 0
	cmp	w4, w1, uxth
	bhi	.L150
	ret
.L150:
	.loc 2 939 0
	ldrh	w3, [x5, x1, lsl 1]
	cmp	w3, w6
	beq	.L149
	.loc 2 940 0
	ldr	w2, [x0, 2556]
	add	w2, w2, w3
	str	w2, [x0, 2556]
.L149:
	add	x1, x1, 1
	b	.L148
.LBE202:
.LBE203:
	.cfi_endproc
.LFE205:
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
.LFB211:
	.loc 2 1396 0
	.cfi_startproc
.LVL152:
	and	w0, w0, 65535
	.loc 2 1397 0
	cbz	w1, .L153
	.loc 2 1396 0
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	bl	ftl_set_blk_mode.part.7
.LVL153:
	.loc 2 1401 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
.LVL154:
.L153:
	.loc 2 1400 0
	adrp	x1, .LANCHOR0
.LVL155:
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1, #:lo12:.LANCHOR0]
	mov	w1, 1
	lsl	w0, w1, w0
.LVL156:
	ldr	w1, [x3, x2]
	bic	w0, w1, w0
	str	w0, [x3, x2]
	.loc 2 1401 0
	ret
	.cfi_endproc
.LFE211:
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
.LFB212:
	.loc 2 1404 0
	.cfi_startproc
.LVL157:
	.loc 2 1404 0
	and	w1, w0, 65535
.LVL158:
	.loc 2 1405 0
	adrp	x0, .LANCHOR0
	ldr	x0, [x0, #:lo12:.LANCHOR0]
	ubfx	x2, x1, 5, 11
	ldr	w0, [x0, x2, lsl 2]
	lsr	w0, w0, w1
	.loc 2 1406 0
	and	w0, w0, 1
	ret
	.cfi_endproc
.LFE212:
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
.LFB218:
	.loc 2 1657 0
	.cfi_startproc
.LVL159:
	.loc 2 1657 0
	and	w6, w1, 65535
.LVL160:
	.loc 2 1660 0
	adrp	x4, .LANCHOR0
	add	x1, x4, :lo12:.LANCHOR0
	and	w2, w2, 65535
.LVL161:
	.loc 2 1659 0
	strh	wzr, [x0, 4]
	.loc 2 1660 0
	ldrh	w3, [x1, 12]
	.loc 2 1662 0
	mov	w1, 65535
.L161:
	.loc 2 1660 0 discriminator 1
	cmp	w3, w2, uxth
	bhi	.L163
	.loc 2 1671 0
	add	x4, x4, :lo12:.LANCHOR0
	ubfiz	x3, x3, 1, 16
	add	x3, x3, 16
	add	x2, x0, 16
	add	x3, x0, x3
	.loc 2 1669 0
	mov	w5, 65535
	ldrh	w1, [x4, 82]
	sub	w1, w1, #1
	and	w1, w1, 65535
	.loc 2 1671 0
	sub	w1, w1, w6
.L164:
	.loc 2 1667 0 discriminator 1
	cmp	x2, x3
	bne	.L166
	.loc 2 1674 0
	ret
.L163:
	.loc 2 1662 0
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5, 16]
	cmp	w5, w1
	beq	.L162
	.loc 2 1664 0
	ldrh	w5, [x0, 4]
	add	w5, w5, 1
	strh	w5, [x0, 4]
.L162:
	add	w2, w2, 1
	b	.L161
.L166:
	.loc 2 1669 0
	ldrh	w4, [x2]
	cmp	w4, w5
	beq	.L165
	.loc 2 1671 0
	ldrh	w4, [x0, 4]
	add	w4, w1, w4
	strh	w4, [x0, 4]
.L165:
	add	x2, x2, 2
	b	.L164
	.cfi_endproc
.LFE218:
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
.LFB221:
	.loc 2 2062 0
	.cfi_startproc
.LVL162:
	.loc 2 2065 0
	ldrh	w1, [x0, 4]
	cbz	w1, .L167
	.loc 2 2067 0
	ldrh	w2, [x0]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L167
.LBB206:
.LBB207:
	.loc 2 2069 0
	ldrb	w2, [x0, 6]
	add	x2, x2, 8
	ldrh	w3, [x0, x2, lsl 1]
.LVL163:
	.loc 2 2072 0
	adrp	x2, .LANCHOR0+12
	ldrh	w4, [x2, #:lo12:.LANCHOR0+12]
	.loc 2 2070 0
	mov	w2, w1
.L170:
	cmp	w3, w2
	beq	.L172
.L167:
	ret
.L172:
	.loc 2 2071 0
	ldrb	w1, [x0, 6]
	add	w1, w1, 1
	and	w1, w1, 255
	strb	w1, [x0, 6]
	.loc 2 2072 0
	cmp	w1, w4
	bne	.L171
	.loc 2 2073 0
	ldrh	w1, [x0, 2]
	.loc 2 2074 0
	strb	wzr, [x0, 6]
	.loc 2 2073 0
	add	w1, w1, 1
	strh	w1, [x0, 2]
.L171:
	.loc 2 2076 0
	ldrb	w1, [x0, 6]
	add	x1, x1, 8
	ldrh	w3, [x0, x1, lsl 1]
	b	.L170
.LBE207:
.LBE206:
	.cfi_endproc
.LFE221:
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
.LFB223:
	.loc 2 2094 0
	.cfi_startproc
.LVL164:
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 2 2094 0
	mov	x19, x0
	.loc 2 2097 0
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
.LVL165:
	ldrh	w1, [x19]
	ldrh	w0, [x0, 20]
	cmp	w1, w0
	bcc	.L174
	.loc 2 2097 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2097
	add	x1, x1, 120
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL166:
.L174:
	.loc 2 2100 0 is_stmt 1
	add	x0, x20, :lo12:.LANCHOR0
	add	x6, x19, 16
	.loc 2 2101 0
	add	x7, x0, 40
	.loc 2 2098 0
	strh	wzr, [x19, 4]
	.loc 2 2099 0
	strb	wzr, [x19, 7]
	.loc 2 2100 0
	mov	x5, 0
	ldrh	w8, [x0, 12]
	.loc 2 2102 0
	mov	w9, -1
.L175:
	.loc 2 2100 0 discriminator 1
	cmp	w8, w5, uxth
	bhi	.L177
	.loc 2 2110 0
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19, 7]
	ldrh	w1, [x20, 82]
	mul	w0, w0, w1
	strh	w0, [x19, 4]
	.loc 2 2111 0
	mov	w0, 1
	strb	w0, [x19, 9]
	.loc 2 2113 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
.LVL167:
	ldp	x29, x30, [sp], 32
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL168:
.L177:
	.cfi_restore_state
	.loc 2 2101 0
	ldrh	w1, [x19]
	ldrb	w0, [x7, x5]
	bl	V2P_block
.LVL169:
	mov	w4, w0
	.loc 2 2102 0
	strh	w9, [x6]
	.loc 2 2103 0
	bl	FtlBbmIsBadBlock
.LVL170:
	cbnz	w0, .L176
	.loc 2 2104 0
	strh	w4, [x6]
	.loc 2 2105 0
	ldrb	w0, [x19, 7]
	add	w0, w0, 1
	strb	w0, [x19, 7]
.L176:
	add	x5, x5, 1
	add	x6, x6, 2
	b	.L175
	.cfi_endproc
.LFE223:
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
.LFB232:
	.loc 2 2451 0
	.cfi_startproc
.LVL171:
	and	w8, w0, 65535
.LVL172:
	.loc 2 2455 0
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	mov	x9, 0
	.loc 2 2453 0
	mov	w6, 0
	mov	x5, x0
	.loc 2 2456 0
	add	x7, x1, 40
	.loc 2 2455 0
	ldrh	w10, [x1, 12]
	.loc 2 2458 0
	ldrh	w11, [x1, 82]
	.loc 2 2455 0
	cmp	w10, w9, uxth
	bhi	.L191
	.loc 2 2460 0
	cbz	w6, .L189
	.loc 2 2461 0
	mov	w0, 32768
	sdiv	w6, w0, w6
.L190:
	.loc 2 2462 0
	add	x0, x5, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x0, [x0, 2336]
	umaddl	x8, w8, w1, x0
.LVL173:
	.loc 2 2464 0
	mov	w0, 0
	.loc 2 2462 0
	strh	w6, [x8, 4]
	.loc 2 2464 0
	ret
.LVL174:
.L184:
	.cfi_def_cfa 29, 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	mov	w6, 0
	b	.L183
.L189:
	.cfi_def_cfa 31, 0
	.cfi_restore 29
	.cfi_restore 30
	mov	w6, 0
	b	.L190
.L191:
	.loc 2 2451 0
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x29, sp, 0
	.cfi_def_cfa_register 29
.L182:
	.loc 2 2456 0
	ldrb	w0, [x7, x9]
	mov	w1, w8
	bl	V2P_block
.LVL175:
	.loc 2 2457 0
	bl	FtlBbmIsBadBlock
.LVL176:
	cbnz	w0, .L181
	.loc 2 2458 0
	add	w6, w6, w11
	and	w6, w6, 65535
.L181:
	add	x9, x9, 1
	.loc 2 2455 0 discriminator 2
	cmp	w10, w9, uxth
	bhi	.L182
	.loc 2 2460 0
	cbz	w6, .L184
	.loc 2 2461 0
	mov	w0, 32768
	sdiv	w6, w0, w6
.L183:
	.loc 2 2462 0
	add	x0, x5, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x0, [x0, 2336]
	umaddl	x8, w8, w1, x0
.LVL177:
	.loc 2 2464 0
	mov	w0, 0
	.loc 2 2462 0
	strh	w6, [x8, 4]
	.loc 2 2464 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE232:
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
.LFB233:
	.loc 2 2467 0
	.cfi_startproc
	.loc 2 2470 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1, 2384]
	cbz	x0, .L194
	.loc 2 2471 0
	ldr	x2, [x1, 2336]
	ldr	x1, [x1, 2360]
	sub	x0, x0, x2
	mov	x2, -6148914691236517206
	asr	x0, x0, 1
	movk	x2, 0xaaab, lsl 0
	mul	x0, x0, x2
	and	x0, x0, 65535
	ldrh	w0, [x1, x0, lsl 1]
.LVL178:
.LVL179:
	ret
.L194:
	.loc 2 2468 0
	mov	w0, 0
.LVL180:
.LVL181:
	.loc 2 2473 0
	ret
	.cfi_endproc
.LFE233:
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
.LFB234:
	.loc 2 2476 0
	.cfi_startproc
.LVL182:
	.loc 2 2481 0
	adrp	x2, .LANCHOR0
	add	x4, x2, :lo12:.LANCHOR0
	.loc 2 2476 0
	and	w0, w0, 65535
	.loc 2 2481 0
	ldr	x1, [x4, 2384]
	cbz	x1, .L202
	.loc 2 2482 0
	ldrh	w3, [x4, 2392]
	mov	w5, 7
	.loc 2 2484 0
	ldr	x4, [x4, 2336]
	.loc 2 2486 0
	mov	w6, 6
	mov	w7, 65535
	.loc 2 2484 0
	sub	x1, x1, x4
	.loc 2 2482 0
	mul	w3, w3, w5
	.loc 2 2484 0
	asr	x1, x1, 1
	.loc 2 2482 0
	asr	w3, w3, 3
	.loc 2 2483 0
	cmp	w0, w3
	csel	w0, w3, w0, gt
	.loc 2 2484 0
	mov	x3, -6148914691236517206
	movk	x3, 0xaaab, lsl 0
	mul	x1, x1, x3
	.loc 2 2485 0
	mov	w3, 0
.LVL183:
	.loc 2 2484 0
	and	w1, w1, 65535
.L198:
	.loc 2 2485 0 discriminator 1
	cmp	w0, w3
	beq	.L201
	.loc 2 2486 0
	umull	x5, w1, w6
	ldrh	w5, [x4, x5]
	cmp	w5, w7
	bne	.L199
.L201:
	.loc 2 2490 0
	add	x2, x2, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2, 2360]
.LVL184:
	ldrh	w0, [x0, x1]
.LVL185:
.LVL186:
	ret
.LVL187:
.L199:
	.loc 2 2485 0 discriminator 2
	add	w3, w3, 1
.LVL188:
	mov	w1, w5
	and	w3, w3, 65535
.LVL189:
	b	.L198
.LVL190:
.L202:
	.loc 2 2477 0
	mov	w0, 0
.LVL191:
.LVL192:
	.loc 2 2493 0
	ret
	.cfi_endproc
.LFE234:
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
.LFB139:
	.loc 3 11 0
	.cfi_startproc
.LVL193:
	stp	x29, x30, [sp, -96]!
	.cfi_def_cfa_offset 96
	.cfi_offset 29, -96
	.cfi_offset 30, -88
	.loc 3 15 0
	adrp	x1, .LC2
	add	x1, x1, :lo12:.LC2
	.loc 3 11 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -64
	.cfi_offset 22, -56
	mov	x22, x0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -48
	.cfi_offset 24, -40
	.loc 3 16 0
	adrp	x23, .LANCHOR0
	add	x21, x23, :lo12:.LANCHOR0
	.loc 3 11 0
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	.cfi_offset 19, -80
	.cfi_offset 20, -72
	.cfi_offset 25, -32
	.cfi_offset 26, -24
	.loc 3 15 0
	add	x20, x0, 12
	.loc 3 11 0
	str	x27, [sp, 80]
	.cfi_offset 27, -16
	.loc 3 15 0
	bl	strcpy
.LVL194:
	.loc 3 16 0
	ldr	w2, [x21, 96]
	mov	x0, x20
	adrp	x1, .LC3
	add	x1, x1, :lo12:.LC3
	bl	sprintf
.LVL195:
	add	x20, x20, x0, sxtw
	.loc 3 17 0
	mov	x0, x20
	adrp	x1, .LC4
	add	x1, x1, :lo12:.LC4
	bl	strcpy
.LVL196:
	.loc 3 18 0
	ldr	w2, [x21, 2560]
	.loc 3 17 0
	add	x20, x20, 10
	.loc 3 18 0
	mov	x0, x20
	adrp	x1, .LC5
	add	x1, x1, :lo12:.LC5
	bl	sprintf
.LVL197:
	add	x20, x20, x0, sxtw
	.loc 3 19 0
	ldr	w2, [x21, 2556]
	mov	x0, x20
	adrp	x1, .LC6
	add	x1, x1, :lo12:.LC6
	bl	sprintf
.LVL198:
	add	x20, x20, x0, sxtw
	.loc 3 20 0
	ldr	w2, [x21, 2564]
	mov	x0, x20
	adrp	x1, .LC7
	add	x1, x1, :lo12:.LC7
	bl	sprintf
.LVL199:
	add	x20, x20, x0, sxtw
	.loc 3 21 0
	ldr	w2, [x21, 2568]
	mov	x0, x20
	adrp	x1, .LC8
	add	x1, x1, :lo12:.LC8
	bl	sprintf
.LVL200:
	add	x20, x20, x0, sxtw
	.loc 3 22 0
	ldr	w2, [x21, 2572]
	mov	x0, x20
	adrp	x1, .LC9
	add	x1, x1, :lo12:.LC9
	bl	sprintf
.LVL201:
	add	x20, x20, x0, sxtw
	.loc 3 23 0
	ldr	w2, [x21, 2576]
	mov	x0, x20
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	bl	sprintf
.LVL202:
	add	x20, x20, x0, sxtw
	.loc 3 24 0
	ldr	w2, [x21, 2580]
	mov	x0, x20
	adrp	x1, .LC11
	add	x1, x1, :lo12:.LC11
	bl	sprintf
.LVL203:
	add	x20, x20, x0, sxtw
	.loc 3 25 0
	ldr	w2, [x21, 2584]
	mov	x0, x20
	adrp	x1, .LC12
	add	x1, x1, :lo12:.LC12
	bl	sprintf
.LVL204:
	add	x20, x20, x0, sxtw
	.loc 3 26 0
	ldr	w2, [x21, 2588]
	mov	x0, x20
	adrp	x1, .LC13
	add	x1, x1, :lo12:.LC13
	lsr	w2, w2, 11
	bl	sprintf
.LVL205:
	.loc 3 27 0
	ldr	w2, [x21, 2592]
	.loc 3 26 0
	add	x20, x20, x0, sxtw
	.loc 3 27 0
	mov	x0, x20
	adrp	x1, .LC14
	add	x1, x1, :lo12:.LC14
	lsr	w2, w2, 11
	bl	sprintf
.LVL206:
	.loc 3 28 0
	ldr	w2, [x21, 2596]
	.loc 3 27 0
	add	x20, x20, x0, sxtw
	.loc 3 28 0
	mov	x0, x20
	adrp	x1, .LC15
	add	x1, x1, :lo12:.LC15
	bl	sprintf
.LVL207:
	add	x20, x20, x0, sxtw
	.loc 3 29 0
	ldr	w2, [x21, 2600]
	mov	x0, x20
	adrp	x1, .LC16
	add	x1, x1, :lo12:.LC16
	bl	sprintf
.LVL208:
	add	x20, x20, x0, sxtw
	.loc 3 30 0
	ldrh	w2, [x21, 142]
	mov	x0, x20
	adrp	x1, .LC17
	add	x1, x1, :lo12:.LC17
	bl	sprintf
.LVL209:
	add	x20, x20, x0, sxtw
	.loc 3 31 0
	ldrh	w2, [x21, 2392]
	mov	x0, x20
	adrp	x1, .LC18
	add	x1, x1, :lo12:.LC18
	bl	sprintf
.LVL210:
	add	x20, x20, x0, sxtw
	.loc 3 32 0
	ldr	w2, [x21, 2604]
	mov	x0, x20
	adrp	x1, .LC19
	add	x1, x1, :lo12:.LC19
	bl	sprintf
.LVL211:
	add	x20, x20, x0, sxtw
	.loc 3 33 0
	ldr	w2, [x21, 2608]
	mov	x0, x20
	adrp	x1, .LC20
	add	x1, x1, :lo12:.LC20
	bl	sprintf
.LVL212:
	add	x20, x20, x0, sxtw
	.loc 3 34 0
	ldr	w2, [x21, 2612]
	mov	x0, x20
	adrp	x1, .LC21
	add	x1, x1, :lo12:.LC21
	bl	sprintf
.LVL213:
	add	x20, x20, x0, sxtw
	.loc 3 35 0
	ldr	w2, [x21, 2616]
	mov	x0, x20
	adrp	x1, .LC22
	add	x1, x1, :lo12:.LC22
	bl	sprintf
.LVL214:
	add	x20, x20, x0, sxtw
	.loc 3 36 0
	ldr	w2, [x21, 2620]
	mov	x0, x20
	adrp	x1, .LC23
	add	x1, x1, :lo12:.LC23
	bl	sprintf
.LVL215:
	add	x20, x20, x0, sxtw
	.loc 3 37 0
	ldr	w2, [x21, 2624]
	mov	x0, x20
	adrp	x1, .LC24
	add	x1, x1, :lo12:.LC24
	bl	sprintf
.LVL216:
	add	x20, x20, x0, sxtw
	.loc 3 38 0
	ldrh	w2, [x21, 2318]
	mov	x0, x20
	adrp	x1, .LC25
	add	x1, x1, :lo12:.LC25
	bl	sprintf
.LVL217:
	add	x20, x20, x0, sxtw
	.loc 3 39 0
	ldrh	w2, [x21, 2316]
	mov	x0, x20
	adrp	x1, .LC26
	add	x1, x1, :lo12:.LC26
	bl	sprintf
.LVL218:
	add	x20, x20, x0, sxtw
	.loc 3 40 0
	ldr	w2, [x21, 120]
	mov	x0, x20
	adrp	x1, .LC27
	add	x1, x1, :lo12:.LC27
	bl	sprintf
.LVL219:
	add	x20, x20, x0, sxtw
	.loc 3 41 0
	ldr	w2, [x21, 112]
	mov	x0, x20
	adrp	x1, .LC28
	add	x1, x1, :lo12:.LC28
	bl	sprintf
.LVL220:
	add	x20, x20, x0, sxtw
	.loc 3 42 0
	ldr	w2, [x21, 8]
	mov	x0, x20
	adrp	x1, .LC29
	add	x1, x1, :lo12:.LC29
	bl	sprintf
.LVL221:
	add	x20, x20, x0, sxtw
	.loc 3 43 0
	ldrh	w2, [x21, 238]
	mov	x0, x20
	adrp	x1, .LC30
	add	x1, x1, :lo12:.LC30
	bl	sprintf
.LVL222:
	add	x20, x20, x0, sxtw
	.loc 3 44 0
	ldrh	w2, [x21, 20]
	mov	x0, x20
	adrp	x1, .LC31
	add	x1, x1, :lo12:.LC31
	bl	sprintf
.LVL223:
	add	x20, x20, x0, sxtw
	.loc 3 45 0
	ldrh	w2, [x21, 2628]
	mov	x0, x20
	adrp	x1, .LC32
	add	x1, x1, :lo12:.LC32
	bl	sprintf
.LVL224:
	add	x20, x20, x0, sxtw
	.loc 3 46 0
	ldr	w2, [x21, 24]
	mov	x0, x20
	adrp	x1, .LC33
	add	x1, x1, :lo12:.LC33
	bl	sprintf
.LVL225:
	add	x20, x20, x0, sxtw
	.loc 3 47 0
	ldrh	w2, [x21, 2632]
	mov	x0, x20
	adrp	x1, .LC34
	add	x1, x1, :lo12:.LC34
	bl	sprintf
.LVL226:
	add	x20, x20, x0, sxtw
	.loc 3 48 0
	ldrh	w2, [x21, 136]
	mov	x0, x20
	adrp	x1, .LC35
	add	x1, x1, :lo12:.LC35
	bl	sprintf
.LVL227:
	add	x20, x20, x0, sxtw
	.loc 3 49 0
	ldrh	w2, [x21, 2402]
	mov	x0, x20
	adrp	x1, .LC36
	add	x1, x1, :lo12:.LC36
	bl	sprintf
.LVL228:
	add	x20, x20, x0, sxtw
	.loc 3 50 0
	ldrb	w2, [x21, 2406]
	mov	x0, x20
	adrp	x1, .LC37
	add	x1, x1, :lo12:.LC37
	bl	sprintf
.LVL229:
	add	x20, x20, x0, sxtw
	.loc 3 51 0
	ldrh	w2, [x21, 2400]
	mov	x0, x20
	adrp	x1, .LC38
	add	x1, x1, :lo12:.LC38
	bl	sprintf
.LVL230:
	add	x20, x20, x0, sxtw
	.loc 3 52 0
	ldrb	w2, [x21, 2408]
	mov	x0, x20
	adrp	x1, .LC39
	add	x1, x1, :lo12:.LC39
	bl	sprintf
.LVL231:
	add	x20, x20, x0, sxtw
	.loc 3 53 0
	ldrh	w2, [x21, 2404]
	mov	x0, x20
	adrp	x1, .LC40
	add	x1, x1, :lo12:.LC40
	bl	sprintf
.LVL232:
	add	x20, x20, x0, sxtw
	.loc 3 54 0
	ldrh	w1, [x21, 2400]
	ldr	x0, [x21, 2352]
	ldrh	w2, [x0, x1, lsl 1]
	mov	x0, x20
	adrp	x1, .LC41
	add	x1, x1, :lo12:.LC41
	bl	sprintf
.LVL233:
	add	x20, x20, x0, sxtw
	.loc 3 55 0
	ldrh	w2, [x21, 2450]
	mov	x0, x20
	adrp	x1, .LC42
	add	x1, x1, :lo12:.LC42
	bl	sprintf
.LVL234:
	add	x20, x20, x0, sxtw
	.loc 3 56 0
	ldrb	w2, [x21, 2454]
	mov	x0, x20
	adrp	x1, .LC43
	add	x1, x1, :lo12:.LC43
	bl	sprintf
.LVL235:
	add	x20, x20, x0, sxtw
	.loc 3 57 0
	ldrh	w2, [x21, 2448]
	mov	x0, x20
	adrp	x1, .LC44
	add	x1, x1, :lo12:.LC44
	bl	sprintf
.LVL236:
	add	x20, x20, x0, sxtw
	.loc 3 58 0
	ldrb	w2, [x21, 2456]
	mov	x0, x20
	adrp	x1, .LC45
	add	x1, x1, :lo12:.LC45
	bl	sprintf
.LVL237:
	add	x20, x20, x0, sxtw
	.loc 3 59 0
	ldrh	w2, [x21, 2452]
	mov	x0, x20
	adrp	x1, .LC46
	add	x1, x1, :lo12:.LC46
	bl	sprintf
.LVL238:
	add	x20, x20, x0, sxtw
	.loc 3 60 0
	ldrh	w1, [x21, 2448]
	ldr	x0, [x21, 2352]
	ldrh	w2, [x0, x1, lsl 1]
	mov	x0, x20
	adrp	x1, .LC47
	add	x1, x1, :lo12:.LC47
	bl	sprintf
.LVL239:
	add	x20, x20, x0, sxtw
	.loc 3 61 0
	ldrh	w2, [x21, 2498]
	mov	x0, x20
	adrp	x1, .LC48
	add	x1, x1, :lo12:.LC48
	bl	sprintf
.LVL240:
	add	x20, x20, x0, sxtw
	.loc 3 62 0
	ldrb	w2, [x21, 2502]
	mov	x0, x20
	adrp	x1, .LC49
	add	x1, x1, :lo12:.LC49
	bl	sprintf
.LVL241:
	add	x20, x20, x0, sxtw
	.loc 3 63 0
	ldrh	w2, [x21, 2496]
	mov	x0, x20
	adrp	x1, .LC50
	add	x1, x1, :lo12:.LC50
	bl	sprintf
.LVL242:
	add	x20, x20, x0, sxtw
	.loc 3 64 0
	ldrb	w2, [x21, 2504]
	mov	x0, x20
	adrp	x1, .LC51
	add	x1, x1, :lo12:.LC51
	bl	sprintf
.LVL243:
	add	x20, x20, x0, sxtw
	.loc 3 65 0
	ldrh	w2, [x21, 2500]
	mov	x0, x20
	adrp	x1, .LC52
	add	x1, x1, :lo12:.LC52
	bl	sprintf
.LVL244:
	add	x20, x20, x0, sxtw
	.loc 3 66 0
	ldrh	w2, [x21, 2650]
	mov	x0, x20
	adrp	x1, .LC53
	add	x1, x1, :lo12:.LC53
	bl	sprintf
.LVL245:
	add	x20, x20, x0, sxtw
	.loc 3 67 0
	ldrb	w2, [x21, 2654]
	mov	x0, x20
	adrp	x1, .LC54
	add	x1, x1, :lo12:.LC54
	bl	sprintf
.LVL246:
	add	x20, x20, x0, sxtw
	.loc 3 68 0
	ldrh	w2, [x21, 2648]
	mov	x0, x20
	adrp	x1, .LC55
	add	x1, x1, :lo12:.LC55
	bl	sprintf
.LVL247:
	add	x20, x20, x0, sxtw
	.loc 3 69 0
	ldrb	w2, [x21, 2656]
	mov	x0, x20
	adrp	x1, .LC56
	add	x1, x1, :lo12:.LC56
	bl	sprintf
.LVL248:
	add	x20, x20, x0, sxtw
	.loc 3 70 0
	ldrh	w2, [x21, 2652]
	mov	x0, x20
	adrp	x1, .LC57
	add	x1, x1, :lo12:.LC57
	bl	sprintf
.LVL249:
	add	x20, x20, x0, sxtw
	.loc 3 71 0
	ldr	w4, [x21, 2772]
	adrp	x1, .LC58
	ldr	w3, [x21, 2780]
	add	x1, x1, :lo12:.LC58
	ldr	w2, [x21, 2776]
	mov	x0, x20
	bl	sprintf
.LVL250:
	add	x19, x20, x0, sxtw
	.loc 3 72 0
	ldr	w2, [x21, 2768]
	mov	x0, x19
	adrp	x1, .LC59
	add	x1, x1, :lo12:.LC59
	bl	sprintf
.LVL251:
	add	x19, x19, x0, sxtw
	.loc 3 73 0
	ldr	w2, [x21, 2792]
	mov	x0, x19
	adrp	x1, .LC60
	add	x1, x1, :lo12:.LC60
	bl	sprintf
.LVL252:
	add	x19, x19, x0, sxtw
	.loc 3 74 0
	ldrh	w2, [x21, 3208]
	mov	x0, x19
	adrp	x1, .LC61
	add	x1, x1, :lo12:.LC61
	bl	sprintf
.LVL253:
	add	x19, x19, x0, sxtw
	.loc 3 75 0
	ldrh	w2, [x21, 3210]
	mov	x0, x19
	adrp	x1, .LC62
	add	x1, x1, :lo12:.LC62
	bl	sprintf
.LVL254:
	add	x19, x19, x0, sxtw
	.loc 3 76 0
	ldr	w2, [x21, 3212]
	mov	x0, x19
	adrp	x1, .LC63
	add	x1, x1, :lo12:.LC63
	bl	sprintf
.LVL255:
	add	x19, x19, x0, sxtw
	.loc 3 77 0
	ldrh	w2, [x21, 3216]
	adrp	x1, .LC64
	add	x1, x1, :lo12:.LC64
	mov	x0, x19
	bl	sprintf
.LVL256:
	add	x19, x19, x0, sxtw
	.loc 3 78 0
	bl	GetFreeBlockMinEraseCount
.LVL257:
	and	w2, w0, 65535
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
.LVL258:
	add	x19, x19, x0, sxtw
	.loc 3 79 0
	ldrh	w0, [x21, 2392]
	bl	GetFreeBlockMaxEraseCount
.LVL259:
	and	w2, w0, 65535
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	bl	sprintf
.LVL260:
	add	x19, x19, x0, sxtw
	.loc 3 80 0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	cmp	w0, 1
	beq	.L204
.LVL261:
.L209:
	.loc 3 103 0
	sub	w0, w19, w22
	.loc 3 104 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL262:
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL263:
.L204:
	.cfi_restore_state
	.loc 3 83 0
	ldrh	w0, [x21, 2648]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L206
	.loc 3 84 0
	ldr	x1, [x21, 2352]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x1, x0]
	mov	x0, x19
	adrp	x1, .LC67
	add	x1, x1, :lo12:.LC67
	bl	sprintf
.LVL264:
	add	x19, x19, x0, sxtw
.L206:
.LBB208:
	.loc 3 86 0
	mov	w0, 0
	.loc 3 90 0
	adrp	x24, .LC68
	.loc 3 86 0
	bl	List_get_gc_head_node
.LVL265:
	.loc 3 90 0
	add	x21, x23, :lo12:.LANCHOR0
	.loc 3 86 0
	and	w3, w0, 65535
.LVL266:
	.loc 3 90 0
	add	x24, x24, :lo12:.LC68
	.loc 3 87 0
	mov	w20, 0
	.loc 3 88 0
	mov	w27, 65535
	.loc 3 90 0
	mov	w26, 6
.L208:
	.loc 3 88 0
	cmp	w3, w27
	beq	.L207
	.loc 3 90 0 discriminator 2
	umull	x25, w3, w26
	ldr	x0, [x21, 2336]
	ldr	x2, [x21, 2352]
	ubfiz	x1, x3, 1, 16
	ldr	x4, [x21, 2360]
	add	x0, x0, x25
	ldrh	w5, [x0, 4]
	mov	x0, x19
	ldrh	w6, [x4, x1]
	ldrh	w4, [x2, x1]
	mov	w2, w20
	mov	x1, x24
	.loc 3 87 0 discriminator 2
	add	w20, w20, 1
	.loc 3 90 0 discriminator 2
	bl	sprintf
.LVL267:
	add	x19, x19, x0, sxtw
	.loc 3 91 0 discriminator 2
	ldr	x0, [x21, 2336]
	.loc 3 87 0 discriminator 2
	cmp	w20, 16
	.loc 3 91 0 discriminator 2
	ldrh	w3, [x0, x25]
.LVL268:
	.loc 3 87 0 discriminator 2
	bne	.L208
.L207:
.LBE208:
.LBB209:
	.loc 3 95 0
	add	x23, x23, :lo12:.LANCHOR0
	.loc 3 99 0
	adrp	x21, .LC69
	add	x21, x21, :lo12:.LC69
	.loc 3 96 0
	mov	w20, 0
	.loc 3 97 0
	mov	w25, 65535
	.loc 3 99 0
	mov	w26, 6
	.loc 3 95 0
	ldr	x0, [x23, 2336]
	ldr	x3, [x23, 2384]
.LVL269:
	sub	x3, x3, x0
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	movk	x0, 0xaaab, lsl 0
	mul	x3, x3, x0
	and	w3, w3, 65535
.LVL270:
.L210:
	.loc 3 97 0
	cmp	w3, w25
	beq	.L209
	.loc 3 99 0 discriminator 2
	umull	x24, w3, w26
	ldr	x0, [x23, 2336]
	ldr	x2, [x23, 2360]
	ubfiz	x1, x3, 1, 16
	add	x0, x0, x24
	ldrh	w5, [x2, x1]
	mov	w2, w20
	ldrh	w4, [x0, 4]
	mov	x1, x21
	mov	x0, x19
	.loc 3 96 0 discriminator 2
	add	w20, w20, 1
	.loc 3 99 0 discriminator 2
	bl	sprintf
.LVL271:
	add	x19, x19, x0, sxtw
	.loc 3 96 0 discriminator 2
	cmp	w20, 4
	beq	.L209
	.loc 3 100 0
	ldr	x0, [x23, 2336]
	ldrh	w3, [x0, x24]
.LVL272:
	b	.L210
.LBE209:
	.cfi_endproc
.LFE139:
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
.LFB140:
	.loc 3 113 0
	.cfi_startproc
.LVL273:
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 3 119 0
	adrp	x2, .LC70
	adrp	x1, .LC71
	add	x2, x2, :lo12:.LC70
	.loc 3 113 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 3 119 0
	add	x1, x1, :lo12:.LC71
	.loc 3 113 0
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 3 113 0
	mov	x20, x0
	.loc 3 119 0
	bl	sprintf
.LVL274:
	add	x19, x20, x0, sxtw
	.loc 3 121 0
	mov	x0, x19
	bl	FtlPrintInfo2buf
.LVL275:
	add	x0, x19, x0, sxtw
	.loc 3 123 0
	sub	w0, w0, w20
	ldp	x19, x20, [sp, 16]
.LVL276:
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE140:
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
.LFB235:
	.loc 2 2497 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 2 2507 0
	adrp	x8, .LANCHOR0
	add	x0, x8, :lo12:.LANCHOR0
	.loc 2 2497 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 2 2507 0
	ldr	w2, [x0, 2612]
	ldr	w1, [x0, 2624]
	cmp	w2, w1
	bcs	.L219
	.loc 2 2510 0
	ldrh	w2, [x0, 20]
	mov	x1, 0
	.loc 2 2511 0
	ldr	x4, [x0, 2360]
	.loc 2 2509 0
	str	wzr, [x0, 2604]
.L220:
	.loc 2 2510 0 discriminator 1
	cmp	w2, w1
	bhi	.L221
	.loc 2 2512 0
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w0, [x1, 2604]
	udiv	w2, w0, w2
	str	w2, [x1, 2612]
	.loc 2 2513 0
	ldr	w2, [x1, 2608]
	sub	w0, w0, w2
	ldrh	w2, [x1, 72]
	udiv	w0, w0, w2
	str	w0, [x1, 2604]
.L222:
	.loc 2 2519 0
	add	x1, x8, :lo12:.LANCHOR0
	ldr	w9, [x1, 2624]
	ldr	w11, [x1, 2612]
	add	w0, w9, 256
	cmp	w0, w11
	bls	.L227
	.loc 2 2519 0 is_stmt 0 discriminator 1
	ldr	w1, [x1, 2620]
	add	w0, w9, 768
	cmp	w0, w1
	bls	.L227
.LVL277:
.L229:
	.loc 2 2520 0 is_stmt 1
	mov	w20, 65535
.L228:
	.loc 2 2566 0
	mov	w0, w20
.LVL278:
	ldp	x19, x20, [sp, 16]
.LVL279:
	ldp	x29, x30, [sp], 32
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL280:
.L221:
	.cfi_restore_state
	.loc 2 2511 0 discriminator 3
	ldrh	w3, [x4, x1, lsl 1]
	add	x1, x1, 1
	ldr	w5, [x0, 2604]
	add	w3, w3, w5
	str	w3, [x0, 2604]
	b	.L220
.L219:
	.loc 2 2514 0
	ldr	w1, [x0, 2620]
	cmp	w2, w1
	bls	.L222
	.loc 2 2515 0
	add	w1, w1, 1
	str	w1, [x0, 2620]
	.loc 2 2516 0
	mov	w1, 0
.L224:
	.loc 2 2516 0 is_stmt 0 discriminator 1
	ldrh	w2, [x0, 20]
	cmp	w1, w2
	bcs	.L222
	.loc 2 2517 0 is_stmt 1 discriminator 3
	ldr	x4, [x0, 2360]
	ubfiz	x3, x1, 1, 32
	.loc 2 2516 0 discriminator 3
	add	w1, w1, 1
	.loc 2 2517 0 discriminator 3
	ldrh	w2, [x4, x3]
	add	w2, w2, 1
	strh	w2, [x4, x3]
	b	.L224
.L227:
	.loc 2 2522 0
	add	x10, x8, :lo12:.LANCHOR0
	ldrh	w0, [x10, 2392]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
.LVL281:
	and	w6, w0, 65535
.LVL282:
	.loc 2 2523 0
	add	w0, w9, 64
	cmp	w6, w0
	bcc	.L229
	.loc 2 2526 0
	ldr	x0, [x10, 2344]
.LVL283:
	cbz	x0, .L229
	.loc 2 2534 0
	mov	w4, 65535
.LVL284:
	.loc 2 2530 0
	ldrh	w12, [x10, 20]
	.loc 2 2533 0
	mov	x13, -6148914691236517206
	ldr	x7, [x10, 2336]
	.loc 2 2534 0
	ldr	x5, [x10, 2360]
	mov	w2, w4
	.loc 2 2528 0
	mov	w10, w4
	.loc 2 2534 0
	mov	w1, 0
.LVL285:
	.loc 2 2533 0
	movk	x13, 0xaaab, lsl 0
	.loc 2 2543 0
	mov	w14, 6
.LVL286:
.L230:
	.loc 2 2528 0
	ldrh	w3, [x0]
	cmp	w3, w10
	bne	.L233
	mov	w20, w2
.L232:
	.loc 2 2546 0
	mov	w0, 65535
.LVL287:
	cmp	w20, w0
	beq	.L229
	.loc 2 2548 0
	ubfiz	x7, x20, 1, 32
	ldrh	w3, [x5, x7]
.LVL288:
	.loc 2 2549 0
	cmp	w9, w3
	bcs	.L234
	.loc 2 2551 0
	bl	GetFreeBlockMinEraseCount
.LVL289:
	cmp	w9, w0, uxth
	bcs	.L234
	.loc 2 2552 0
	add	x0, x8, :lo12:.LANCHOR0
	str	w4, [x0, 2624]
.LVL290:
.L234:
	.loc 2 2554 0
	cmp	w11, w3
	bls	.L229
	.loc 2 2554 0 is_stmt 0 discriminator 1
	add	w0, w3, 128
	cmp	w6, w0
	ble	.L229
	.loc 2 2556 0 is_stmt 1
	add	w0, w3, 256
	cmp	w11, w0
	bhi	.L235
	.loc 2 2556 0 is_stmt 0 discriminator 1
	add	x0, x8, :lo12:.LANCHOR0
	add	w3, w3, 768
.LVL291:
	ldr	w0, [x0, 2620]
	cmp	w3, w0
	bcs	.L229
.LVL292:
.L235:
	.loc 2 2558 0 is_stmt 1
	add	x19, x8, :lo12:.LANCHOR0
	ldrh	w5, [x5, x7]
	mov	w2, w11
	mov	w1, w20
.LVL293:
	ldr	x0, [x19, 2352]
	ldr	w3, [x19, 2620]
.LVL294:
	ldrh	w4, [x0, x7]
.LVL295:
	adrp	x0, .LC72
	add	x0, x0, :lo12:.LC72
	bl	printk
.LVL296:
	.loc 2 2559 0
	mov	w0, 1
	str	w0, [x19, 3220]
	.loc 2 2560 0
	b	.L228
.LVL297:
.L233:
	.loc 2 2529 0
	add	w1, w1, 1
.LVL298:
	and	w1, w1, 65535
.LVL299:
	.loc 2 2530 0
	cmp	w1, w12
	bhi	.L229
	.loc 2 2532 0
	ldrh	w15, [x0, 4]
	cbz	w15, .L231
	.loc 2 2533 0
	sub	x0, x0, x7
.LVL300:
	asr	x0, x0, 1
	mul	x0, x0, x13
	and	w20, w0, 65535
	.loc 2 2534 0
	and	x0, x0, 65535
	ldrh	w0, [x5, x0, lsl 1]
	cmp	w9, w0
	bcs	.L232
	.loc 2 2538 0
	cmp	w4, w0
	bls	.L231
	mov	w4, w0
	.loc 2 2533 0
	mov	w2, w20
.LVL301:
.L231:
	.loc 2 2543 0
	umaddl	x0, w3, w14, x7
	b	.L230
	.cfi_endproc
.LFE235:
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
.LFB236:
	.loc 2 2570 0
	.cfi_startproc
.LVL302:
	and	w0, w0, 65535
	.loc 2 2571 0
	mov	w1, 65535
	cmp	w0, w1
	beq	.L246
	.loc 2 2570 0
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
.LBB212:
.LBB213:
	.loc 2 2573 0
	adrp	x2, .LANCHOR0+2352
	ubfiz	x1, x0, 1, 16
.LBE213:
.LBE212:
	.loc 2 2570 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
.LBB215:
.LBB214:
	.loc 2 2573 0
	ldr	x2, [x2, #:lo12:.LANCHOR0+2352]
	strh	wzr, [x2, x1]
	.loc 2 2574 0
	bl	INSERT_FREE_LIST
.LVL303:
.LBE214:
.LBE215:
	.loc 2 2576 0
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
.LVL304:
.L246:
	mov	w0, 0
.LVL305:
	ret
	.cfi_endproc
.LFE236:
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	get_new_active_ppa
	.type	get_new_active_ppa, %function
get_new_active_ppa:
.LFB239:
	.loc 2 2703 0
	.cfi_startproc
.LVL306:
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	mov	x19, x0
	str	x21, [sp, 32]
	.cfi_offset 21, -16
	.loc 2 2707 0
	ldrh	w1, [x0]
	mov	w0, 65535
.LVL307:
	cmp	w1, w0
	bne	.L250
	.loc 2 2707 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2707
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL308:
.L250:
	.loc 2 2708 0 is_stmt 1
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x0, 82]
	cmp	w1, w0
	bne	.L251
	.loc 2 2708 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2708
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL309:
.L251:
	.loc 2 2709 0 is_stmt 1
	ldrh	w0, [x19, 4]
	cbnz	w0, .L252
	.loc 2 2709 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2709
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL310:
.L252:
	.loc 2 2711 0 is_stmt 1
	ldrb	w0, [x19, 6]
	.loc 2 2714 0
	add	x1, x20, :lo12:.LANCHOR0
	.loc 2 2710 0
	strb	wzr, [x19, 10]
	.loc 2 2711 0
	add	x0, x0, 8
	.loc 2 2714 0
	ldrh	w2, [x1, 12]
	.loc 2 2712 0
	mov	w1, 65535
	.loc 2 2711 0
	ldrh	w0, [x19, x0, lsl 1]
.LVL311:
.L253:
	.loc 2 2712 0
	cmp	w0, w1
	beq	.L255
	.loc 2 2720 0
	ldrh	w21, [x19, 2]
	.loc 2 2728 0
	mov	w3, 65535
	.loc 2 2721 0
	ldrh	w1, [x19, 4]
	.loc 2 2720 0
	orr	w21, w21, w0, lsl 10
	ldrb	w0, [x19, 6]
.LVL312:
	.loc 2 2721 0
	sub	w1, w1, #1
	and	w1, w1, 65535
	strh	w1, [x19, 4]
.L257:
	.loc 2 2723 0
	add	w0, w0, 1
	and	w0, w0, 255
	.loc 2 2724 0
	cmp	w2, w0
	bne	.L256
	.loc 2 2725 0
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	.loc 2 2726 0
	mov	w0, 0
.L256:
	.loc 2 2728 0
	add	x4, x19, x0, sxtw 1
	ldrh	w4, [x4, 16]
	cmp	w4, w3
	beq	.L257
	.loc 2 2730 0
	add	x20, x20, :lo12:.LANCHOR0
	strb	w0, [x19, 6]
	ldrh	w2, [x19, 2]
	ldrh	w0, [x20, 82]
	cmp	w2, w0
	bne	.L249
	.loc 2 2730 0 is_stmt 0 discriminator 1
	cbz	w1, .L249
	.loc 2 2730 0 discriminator 2
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2730
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL313:
.L249:
	.loc 2 2732 0 is_stmt 1
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
.LVL314:
	ldp	x29, x30, [sp], 48
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL315:
.L255:
	.cfi_restore_state
	.loc 2 2713 0
	ldrb	w0, [x19, 6]
.LVL316:
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 6]
	.loc 2 2714 0
	cmp	w0, w2
	bne	.L254
	.loc 2 2715 0
	ldrh	w0, [x19, 2]
	.loc 2 2716 0
	strb	wzr, [x19, 6]
	.loc 2 2715 0
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L254:
	.loc 2 2718 0
	ldrb	w0, [x19, 6]
	add	x0, x0, 8
	ldrh	w0, [x19, x0, lsl 1]
.LVL317:
	b	.L253
	.cfi_endproc
.LFE239:
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
.LFB242:
	.file 5 "drivers/rkflash/rksftl/sftl_gc.c"
	.loc 5 22 0
	.cfi_startproc
	.loc 5 25 0
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	.loc 5 26 0
	mov	w3, 0
	.loc 5 27 0
	mov	w8, 24
	mov	w9, 1
	.loc 5 28 0
	mov	w5, 4
	.loc 5 25 0
	str	wzr, [x1, 3224]
.L265:
	.loc 5 26 0 discriminator 1
	ldrh	w2, [x1, 12]
	cmp	w3, w2
	bcc	.L266
	.loc 5 34 0 discriminator 1
	add	x0, x0, :lo12:.LANCHOR0
	.loc 5 35 0 discriminator 1
	mov	w6, 24
	.loc 5 36 0 discriminator 1
	mov	w5, 4
.L267:
	.loc 5 34 0 discriminator 1
	ldr	w1, [x0, 3264]
	cmp	w2, w1
	bcc	.L268
	.loc 5 39 0
	ret
.L266:
	.loc 5 27 0 discriminator 3
	umull	x4, w3, w8
	ldr	x6, [x1, 3232]
	.loc 5 28 0 discriminator 3
	ldr	x7, [x1, 3240]
	.loc 5 27 0 discriminator 3
	add	x2, x6, x4
	str	w9, [x2, 16]
	.loc 5 28 0 discriminator 3
	ldrh	w2, [x1, 90]
	mul	w2, w2, w3
	sdiv	w2, w2, w5
	add	x2, x7, x2, sxtw 2
	str	x2, [x6, x4]
	.loc 5 29 0 discriminator 3
	ldrh	w2, [x1, 92]
	ldr	x7, [x1, 3232]
	ldr	x10, [x1, 3248]
	add	x6, x7, x4
	mul	w2, w2, w3
	sdiv	w2, w2, w5
	add	x2, x10, x2, sxtw 2
	str	x2, [x6, 8]
	.loc 5 30 0 discriminator 3
	ldr	x2, [x1, 3256]
	ubfiz	x10, x3, 5, 16
	ldr	x4, [x7, x4]
	.loc 5 26 0 discriminator 3
	add	w3, w3, 1
	.loc 5 30 0 discriminator 3
	add	x2, x2, x10
	.loc 5 26 0 discriminator 3
	and	w3, w3, 65535
	.loc 5 30 0 discriminator 3
	str	x4, [x2, 8]
	.loc 5 31 0 discriminator 3
	ldr	x4, [x6, 8]
	str	x4, [x2, 16]
	b	.L265
.L268:
	.loc 5 35 0 discriminator 3
	umull	x4, w2, w6
	ldr	x3, [x0, 3232]
	.loc 5 36 0 discriminator 3
	ldr	x7, [x0, 3240]
	.loc 5 35 0 discriminator 3
	add	x1, x3, x4
	str	wzr, [x1, 16]
	.loc 5 36 0 discriminator 3
	ldrh	w1, [x0, 90]
	mul	w1, w1, w2
	sdiv	w1, w1, w5
	add	x1, x7, x1, sxtw 2
	str	x1, [x3, x4]
	.loc 5 37 0 discriminator 3
	ldrh	w1, [x0, 92]
	ldr	x3, [x0, 3232]
	add	x3, x3, x4
	ldr	x4, [x0, 3248]
	mul	w1, w1, w2
	.loc 5 34 0 discriminator 3
	add	w2, w2, 1
	and	w2, w2, 65535
	.loc 5 37 0 discriminator 3
	sdiv	w1, w1, w5
	add	x1, x4, x1, sxtw 2
	str	x1, [x3, 8]
	b	.L267
	.cfi_endproc
.LFE242:
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
.LFB243:
	.loc 5 42 0
	.cfi_startproc
.LVL318:
	.loc 5 47 0
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	.loc 5 46 0
	mov	w3, 0
.LVL319:
	.loc 5 48 0
	mov	w8, 24
	.loc 5 47 0
	ldr	w7, [x2, 3264]
	.loc 5 48 0
	ldr	x5, [x2, 3232]
.LVL320:
.L270:
	.loc 5 46 0 discriminator 1
	cmp	w3, w1
	bcs	.L269
	.loc 5 48 0
	ubfiz	x4, x3, 5, 16
	mov	w2, 0
	add	x4, x0, x4
	b	.L275
.L271:
	.loc 5 47 0 discriminator 2
	add	w2, w2, 1
.LVL321:
	and	w2, w2, 65535
.LVL322:
.L275:
	.loc 5 47 0 is_stmt 0 discriminator 1
	cmp	w2, w7
	bcs	.L272
	.loc 5 48 0 is_stmt 1
	umull	x6, w2, w8
	add	x9, x5, x6
	ldr	x10, [x5, x6]
	ldr	x6, [x4, 8]
	cmp	x10, x6
	bne	.L271
	.loc 5 49 0
	str	wzr, [x9, 16]
.L272:
	.loc 5 46 0 discriminator 2
	add	w3, w3, 1
.LVL323:
	and	w3, w3, 65535
.LVL324:
	b	.L270
.L269:
	ret
	.cfi_endproc
.LFE243:
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
.LFB244:
	.loc 5 57 0
	.cfi_startproc
.LVL325:
	.loc 5 62 0
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	.loc 5 61 0
	mov	w3, 0
.LVL326:
	.loc 5 63 0
	mov	w7, 24
	.loc 5 64 0
	mov	w8, 1
	.loc 5 62 0
	ldr	w5, [x2, 3264]
	.loc 5 63 0
	ldr	x6, [x2, 3232]
.LVL327:
.L277:
	.loc 5 61 0 discriminator 1
	cmp	w3, w1
	bcs	.L276
	mov	w2, 0
	b	.L282
.L278:
	.loc 5 62 0 discriminator 2
	add	w2, w2, 1
.LVL328:
	and	w2, w2, 65535
.LVL329:
.L282:
	.loc 5 62 0 is_stmt 0 discriminator 1
	cmp	w2, w5
	bcs	.L279
	.loc 5 63 0 is_stmt 1
	umaddl	x4, w2, w7, x6
	ldr	w9, [x4, 16]
	cbnz	w9, .L278
	.loc 5 65 0
	ubfiz	x2, x3, 5, 16
.LVL330:
	ldr	x9, [x4]
	add	x2, x0, x2
	.loc 5 64 0
	str	w8, [x4, 16]
	.loc 5 65 0
	str	x9, [x2, 8]
	.loc 5 66 0
	ldr	x4, [x4, 8]
	str	x4, [x2, 16]
.LVL331:
.L279:
	.loc 5 61 0 discriminator 2
	add	w3, w3, 1
.LVL332:
	and	w3, w3, 65535
.LVL333:
	b	.L277
.L276:
	ret
	.cfi_endproc
.LFE244:
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
.LFB245:
	.loc 5 74 0
	.cfi_startproc
.LVL334:
	.loc 5 77 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	.loc 5 74 0
	and	w0, w0, 65535
	.loc 5 77 0
	ldrh	w2, [x1, 3268]
	.loc 5 78 0
	ldr	x3, [x1, 3272]
	.loc 5 77 0
	mov	x1, 0
.L284:
	.loc 5 77 0 is_stmt 0 discriminator 1
	cmp	w2, w1, uxth
	bhi	.L286
	.loc 5 81 0 is_stmt 1
	mov	w0, 0
.LVL335:
	ret
.LVL336:
.L286:
	add	x1, x1, 1
	.loc 5 78 0
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4, -2]
	cmp	w4, w0
	bne	.L284
	.loc 5 79 0
	mov	w0, 1
.LVL337:
	.loc 5 82 0
	ret
	.cfi_endproc
.LFE245:
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
.LFB246:
	.loc 5 85 0
	.cfi_startproc
.LVL338:
	mov	w5, w0
	mov	w8, w1
	mov	w7, w2
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 5 88 0
	lsr	w0, w0, 10
.LVL339:
	.loc 5 85 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 5 88 0
	bl	P2V_block_in_plane
.LVL340:
	and	w9, w0, 65535
.LVL341:
	.loc 5 91 0
	adrp	x3, .LANCHOR0
	add	x4, x3, :lo12:.LANCHOR0
	ldrh	w1, [x4, 3268]
	.loc 5 92 0
	ldr	x6, [x4, 3272]
	.loc 5 91 0
	mov	x4, 0
.L289:
	and	w2, w4, 65535
.LVL342:
	.loc 5 91 0 is_stmt 0 discriminator 1
	cmp	w2, w1
	bcc	.L291
	.loc 5 96 0 is_stmt 1
	bne	.L290
	.loc 5 97 0
	and	x4, x4, 65535
	strh	w0, [x6, x4, lsl 1]
	.loc 5 98 0
	add	x4, x3, :lo12:.LANCHOR0
	ldrh	w0, [x4, 3268]
	add	w0, w0, 1
	strh	w0, [x4, 3268]
	b	.L290
.L291:
	add	x4, x4, 1
	.loc 5 92 0
	add	x2, x6, x4, lsl 1
.LVL343:
	ldrh	w2, [x2, -2]
	cmp	w2, w9
	bne	.L289
.LVL344:
.L290:
	.loc 5 101 0
	add	x0, x3, :lo12:.LANCHOR0
	mov	w1, 12
	ldrh	w3, [x0, 3280]
	ldr	x4, [x0, 3288]
	umull	x3, w3, w1
	add	x4, x4, x3
	str	w8, [x4, 4]
	.loc 5 102 0
	ldr	x1, [x0, 3288]
	add	x4, x1, x3
	str	w7, [x4, 8]
	.loc 5 103 0
	str	w5, [x1, x3]
	.loc 5 107 0
	ldrh	w1, [x0, 3280]
	add	w1, w1, 1
	strh	w1, [x0, 3280]
	.loc 5 108 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE246:
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshBlock
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
.LFB253:
	.loc 5 345 0
	.cfi_startproc
.LVL345:
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 5 345 0
	and	w19, w0, 65535
.LVL346:
	.loc 5 347 0
	mov	w1, w19
	adrp	x0, .LC73
	add	x0, x0, :lo12:.LC73
	bl	printk
.LVL347:
	.loc 5 349 0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w3, [x0, 3296]
	cmp	w19, w3
	beq	.L294
	.loc 5 349 0 is_stmt 0 discriminator 1
	ldrh	w2, [x0, 3298]
	cmp	w19, w2
	beq	.L294
	.loc 5 351 0 is_stmt 1
	mov	w1, 65535
	cmp	w3, w1
	bne	.L295
	.loc 5 352 0
	strh	w19, [x0, 3296]
.L294:
	.loc 5 356 0
	mov	w0, 0
	ldr	x19, [sp, 16]
.LVL348:
	ldp	x29, x30, [sp], 32
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
.LVL349:
.L295:
	.cfi_restore_state
	.loc 5 353 0
	cmp	w2, w1
	bne	.L294
	.loc 5 354 0
	strh	w19, [x0, 3298]
	b	.L294
	.cfi_endproc
.LFE253:
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
.LFB254:
	.loc 5 359 0
	.cfi_startproc
.LVL350:
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	.loc 5 364 0
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	.loc 5 359 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -16
	.cfi_offset 22, -8
	.loc 5 359 0
	and	w21, w0, 65535
.LVL351:
	.loc 5 369 0
	add	x20, x20, 3304
	.loc 5 361 0
	mov	w0, w21
	bl	P2V_block_in_plane
.LVL352:
	.loc 5 364 0
	ldrh	w1, [x20, -4]
	mov	w2, w21
	.loc 5 361 0
	mov	w22, w0
	.loc 5 364 0
	adrp	x0, .LC74
	add	x0, x0, :lo12:.LC74
	bl	printk
.LVL353:
	.loc 5 366 0
	mov	w0, w22
	bl	FtlGcRefreshBlock
.LVL354:
	.loc 5 368 0
	ldrh	w0, [x20, -4]
	mov	x1, 0
.L298:
	.loc 5 368 0 is_stmt 0 discriminator 1
	cmp	w0, w1, uxth
	bhi	.L300
	.loc 5 372 0 is_stmt 1
	cmp	w0, 15
	bhi	.L299
	.loc 5 373 0
	add	x19, x19, :lo12:.LANCHOR0
	add	w1, w0, 1
	add	x19, x19, 3304
	strh	w1, [x19, -4]
	strh	w21, [x19, w0, sxtw 1]
	b	.L299
.L300:
	add	x1, x1, 1
	.loc 5 369 0
	add	x2, x20, x1, lsl 1
	ldrh	w2, [x2, -2]
	cmp	w2, w21
	bne	.L298
.L299:
	.loc 5 375 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL355:
	ldp	x29, x30, [sp], 48
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE254:
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
.LFB255:
	.loc 5 379 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 5 380 0
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3300]
	cbz	w1, .L303
	.loc 5 380 0 is_stmt 0 discriminator 1
	ldrh	w3, [x0, 3296]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L303
	.loc 5 381 0 is_stmt 1
	ldrh	w2, [x0, 3338]
	cmp	w2, w1
	bcc	.L304
	.loc 5 382 0
	strh	wzr, [x0, 3338]
.L304:
.LBB218:
.LBB219:
	.loc 5 383 0
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x19, 3304
	ldrh	w1, [x19, 3338]
	ldrh	w0, [x0, x1, lsl 1]
	bl	P2V_block_in_plane
.LVL356:
	bl	FtlGcRefreshBlock
.LVL357:
	.loc 5 384 0
	ldrh	w0, [x19, 3338]
	add	w0, w0, 1
	strh	w0, [x19, 3338]
.L303:
.LBE219:
.LBE218:
	.loc 5 387 0
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE255:
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
.LFB261:
	.loc 1 44 0
	.cfi_startproc
.LVL358:
	stp	x29, x30, [sp, -96]!
	.cfi_def_cfa_offset 96
	.cfi_offset 29, -96
	.cfi_offset 30, -88
	ubfiz	x1, x1, 5, 32
.LVL359:
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -48
	.cfi_offset 24, -40
	.loc 1 46 0
	adrp	x23, g_nand_phy_info
	add	x2, x23, :lo12:g_nand_phy_info
.LVL360:
	.loc 1 44 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	.cfi_offset 19, -80
	.cfi_offset 20, -72
	.cfi_offset 21, -64
	.cfi_offset 22, -56
	.loc 1 51 0
	adrp	x20, .LANCHOR1
	.loc 1 44 0
	str	x25, [sp, 64]
	.cfi_offset 25, -32
	.loc 1 51 0
	add	x20, x20, :lo12:.LANCHOR1
	adrp	x22, .LC1
	mov	x19, x0
	.loc 1 46 0
	ldrh	w25, [x2, 12]
.LVL361:
	add	x24, x0, x1
.LVL362:
	.loc 1 51 0
	add	x20, x20, 160
	add	x22, x22, :lo12:.LC1
.LVL363:
.L310:
	.loc 1 50 0 discriminator 1
	cmp	x19, x24
	bne	.L317
	.loc 1 71 0
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
.LVL364:
	ldp	x29, x30, [sp], 96
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL365:
.L317:
	.cfi_restore_state
	.loc 1 51 0
	ldr	x0, [x19, 8]
	cbz	x0, .L311
	.loc 1 51 0 is_stmt 0 discriminator 2
	ldr	x0, [x19, 16]
	cbnz	x0, .L312
.L311:
	.loc 1 51 0 discriminator 3
	mov	w2, 51
	mov	x1, x20
	mov	x0, x22
	bl	printk
.LVL366:
.L312:
	.loc 1 52 0 is_stmt 1
	ldr	w0, [x19, 4]
	.loc 1 53 0
	adrp	x21, g_nand_ops
	add	x21, x21, :lo12:g_nand_ops
	.loc 1 52 0
	add	x2, x29, 88
	add	x1, x29, 92
	bl	l2p_addr_tran.isra.0
.LVL367:
	.loc 1 53 0
	ldrb	w0, [x29, 88]
	ldr	w1, [x29, 92]
	ldp	x2, x3, [x19, 8]
	ldr	x4, [x21, 24]
	blr	x4
.LVL368:
	.loc 1 57 0
	str	w0, [x19]
	.loc 1 58 0
	add	x0, x23, :lo12:g_nand_phy_info
	ldrh	w0, [x0, 14]
	cmp	w0, 4
	bne	.L314
	.loc 1 59 0
	ldrb	w0, [x29, 88]
	ldr	x4, [x21, 24]
	ldp	x2, x3, [x19, 8]
	ldr	w1, [x29, 92]
	add	x3, x3, 8
	add	x2, x2, 2048
	add	w1, w25, w1
	blr	x4
.LVL369:
	.loc 1 63 0
	cmn	w0, #1
	beq	.L315
	.loc 1 64 0 discriminator 1
	ldr	x0, [x19, 16]
	.loc 1 63 0 discriminator 1
	ldr	w1, [x0, 12]
	cmn	w1, #1
	bne	.L314
	.loc 1 64 0
	ldr	w1, [x0, 8]
	cmn	w1, #1
	bne	.L314
	.loc 1 65 0
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L314
.L315:
	.loc 1 67 0
	mov	w0, -1
	str	w0, [x19]
.L314:
	add	x19, x19, 32
	b	.L310
	.cfi_endproc
.LFE261:
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
.LFB169:
	.loc 4 279 0
	.cfi_startproc
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	.loc 4 286 0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	.loc 4 279 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	add	x22, x0, 148
	stp	x25, x26, [sp, 64]
	.loc 4 289 0
	mov	w21, 0
.LVL370:
	.loc 4 279 0
	stp	x19, x20, [sp, 16]
	.cfi_offset 25, -16
	.cfi_offset 26, -8
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.loc 4 289 0
	mov	x19, x0
	.loc 4 279 0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	.loc 4 286 0
	add	x23, x0, 3344
	.loc 4 293 0
	mov	x25, x23
	.loc 4 301 0
	mov	w26, 61664
	.loc 4 286 0
	ldr	x1, [x0, 3376]
	.loc 4 287 0
	ldr	x24, [x0, 3384]
	stp	x1, x24, [x23, 8]
.LVL371:
.L329:
	.loc 4 289 0 discriminator 1
	ldrh	w0, [x19, 34]
	cmp	w21, w0
	bcc	.L334
	.loc 4 314 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
.LVL372:
	ldp	x21, x22, [sp, 32]
.LVL373:
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL374:
.L334:
	.cfi_restore_state
	.loc 4 291 0
	ldrh	w20, [x19, 78]
.LVL375:
	.loc 4 290 0
	mov	w0, -1
	strh	w0, [x22]
.LVL376:
.L331:
	.loc 4 291 0 discriminator 2
	ldrh	w0, [x19, 78]
	sub	w20, w20, #1
.LVL377:
	and	w20, w20, 65535
.LVL378:
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L332
	.loc 4 292 0
	madd	w0, w0, w21, w20
	.loc 4 294 0
	mov	w2, 1
	mov	w1, w2
	.loc 4 292 0
	lsl	w0, w0, 10
.LVL379:
	.loc 4 293 0
	str	w0, [x25, 4]
	.loc 4 294 0
	mov	x0, x23
.LVL380:
	bl	FlashReadPages
.LVL381:
	.loc 4 295 0
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L331
	.loc 4 301 0
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L331
	.loc 4 303 0
	strh	w20, [x22]
.L332:
	.loc 4 289 0 discriminator 2
	add	w21, w21, 1
	add	x22, x22, 2
	b	.L329
	.cfi_endproc
.LFE169:
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
.LFB175:
	.loc 2 47 0
	.cfi_startproc
.LVL382:
	stp	x29, x30, [sp, -160]!
	.cfi_def_cfa_offset 160
	.cfi_offset 29, -160
	.cfi_offset 30, -152
	.loc 2 51 0
	cmp	w1, 1
	.loc 2 47 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -112
	.cfi_offset 24, -104
	mov	w23, w1
	stp	x19, x20, [sp, 16]
	adrp	x1, .LANCHOR0
.LVL383:
	stp	x21, x22, [sp, 32]
	.cfi_offset 19, -144
	.cfi_offset 20, -136
	.cfi_offset 21, -128
	.cfi_offset 22, -120
	.loc 2 51 0
	add	x1, x1, :lo12:.LANCHOR0
	bne	.L340
	.loc 2 51 0 is_stmt 0 discriminator 1
	ldrh	w19, [x1, 84]
.L341:
	.loc 2 56 0 is_stmt 1 discriminator 4
	sub	w19, w19, #1
	.loc 2 53 0 discriminator 4
	adrp	x1, ftl_temp_buf
	.loc 2 56 0 discriminator 4
	sxth	w19, w19
.LVL384:
	.loc 2 53 0 discriminator 4
	add	x1, x1, :lo12:ftl_temp_buf
	.loc 2 57 0 discriminator 4
	lsl	w21, w0, 10
	.loc 2 53 0 discriminator 4
	str	x1, [x29, 72]
	.loc 2 57 0 discriminator 4
	orr	w0, w19, w21
.LVL385:
	.loc 2 54 0 discriminator 4
	add	x1, x29, 96
	.loc 2 57 0 discriminator 4
	str	w0, [x29, 68]
	.loc 2 58 0 discriminator 4
	mov	w2, w23
	.loc 2 54 0 discriminator 4
	str	x1, [x29, 80]
	.loc 2 58 0 discriminator 4
	add	x0, x29, 64
	mov	w1, 1
	bl	FlashReadPages
.LVL386:
	.loc 2 59 0 discriminator 4
	ldr	w0, [x29, 96]
	cmn	w0, #1
	bne	.L342
	mov	w22, 0
.LVL387:
	.loc 2 61 0
	mov	w24, 2
.L343:
	.loc 2 60 0
	cmp	w22, w19
	ble	.L346
.L342:
	.loc 2 73 0
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
.LVL388:
	ldp	x21, x22, [sp, 32]
.LVL389:
	ldp	x23, x24, [sp, 48]
.LVL390:
	ldp	x29, x30, [sp], 160
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL391:
.L340:
	.cfi_restore_state
	.loc 2 51 0 discriminator 2
	ldrh	w19, [x1, 82]
	b	.L341
.LVL392:
.L346:
	.loc 2 61 0
	add	w20, w22, w19
	.loc 2 63 0
	mov	w2, w23
	mov	w1, 1
	.loc 2 61 0
	sdiv	w20, w20, w24
	.loc 2 62 0
	sxth	w0, w20
	orr	w0, w0, w21
	str	w0, [x29, 68]
	.loc 2 63 0
	add	x0, x29, 64
	bl	FlashReadPages
.LVL393:
	.loc 2 65 0
	ldr	w0, [x29, 96]
	cmn	w0, #1
	bne	.L344
	.loc 2 65 0 is_stmt 0 discriminator 1
	ldr	w0, [x29, 100]
	cmn	w0, #1
	bne	.L344
	.loc 2 66 0 is_stmt 1
	sub	w19, w20, #1
.LVL394:
	sxth	w19, w19
.LVL395:
	b	.L343
.L344:
	.loc 2 68 0
	add	w20, w20, 1
	sxth	w22, w20
	b	.L343
	.cfi_endproc
.LFE175:
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
.LFB262:
	.loc 1 74 0
	.cfi_startproc
.LVL396:
	stp	x29, x30, [sp, -144]!
	.cfi_def_cfa_offset 144
	.cfi_offset 29, -144
	.cfi_offset 30, -136
	ubfiz	x1, x1, 5, 32
.LVL397:
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -96
	.cfi_offset 24, -88
	.loc 1 76 0
	adrp	x23, g_nand_phy_info
	.loc 1 74 0
	stp	x25, x26, [sp, 64]
	.cfi_offset 25, -80
	.cfi_offset 26, -72
	mov	w26, w2
	.loc 1 76 0
	add	x2, x23, :lo12:g_nand_phy_info
.LVL398:
	.loc 1 74 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	.cfi_offset 19, -128
	.cfi_offset 20, -120
	.cfi_offset 21, -112
	.cfi_offset 22, -104
	.loc 1 81 0
	adrp	x22, .LANCHOR1
	.loc 1 74 0
	stp	x27, x28, [sp, 80]
	.cfi_offset 27, -64
	.cfi_offset 28, -56
	.loc 1 81 0
	add	x22, x22, :lo12:.LANCHOR1
	adrp	x25, .LC1
	.loc 1 74 0
	mov	w27, w3
	.loc 1 76 0
	ldrh	w28, [x2, 12]
.LVL399:
	mov	x20, x0
	add	x24, x0, x1
.LVL400:
	.loc 1 80 0
	mov	x19, x0
	.loc 1 81 0
	add	x22, x22, 176
	add	x25, x25, :lo12:.LC1
.LVL401:
.L349:
	.loc 1 80 0 discriminator 1
	cmp	x19, x24
	bne	.L357
	.loc 1 101 0
	cbnz	w27, .L358
.L376:
	.loc 1 131 0
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
.LVL402:
	ldp	x27, x28, [sp, 80]
.LVL403:
	ldp	x29, x30, [sp], 144
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL404:
.L357:
	.cfi_restore_state
	.loc 1 81 0
	ldr	x0, [x19, 8]
	cbz	x0, .L350
	.loc 1 81 0 is_stmt 0 discriminator 2
	ldr	x0, [x19, 16]
	cbnz	x0, .L351
.L350:
	.loc 1 81 0 discriminator 3
	mov	w2, 81
	mov	x1, x22
	mov	x0, x25
	bl	printk
.LVL405:
.L351:
	.loc 1 82 0 is_stmt 1
	ldr	w0, [x19, 4]
	add	x2, x29, 104
	add	x1, x29, 108
	.loc 1 83 0
	adrp	x21, g_nand_ops
	.loc 1 82 0
	bl	l2p_addr_tran.isra.0
.LVL406:
	.loc 1 83 0
	add	x0, x21, :lo12:g_nand_ops
	ldr	w1, [x29, 108]
	ldp	x2, x3, [x19, 8]
	ldr	x4, [x0, 16]
	ldrb	w0, [x29, 104]
	blr	x4
.LVL407:
	.loc 1 88 0
	cbnz	w0, .L352
	.loc 1 87 0
	str	wzr, [x19]
.L353:
	.loc 1 91 0
	add	x0, x23, :lo12:g_nand_phy_info
	ldrh	w0, [x0, 14]
	cmp	w0, 4
	bne	.L355
	.loc 1 92 0
	add	x21, x21, :lo12:g_nand_ops
	ldrb	w0, [x29, 104]
	ldp	x2, x3, [x19, 8]
	ldr	x4, [x21, 16]
	ldr	w1, [x29, 108]
	add	x3, x3, 8
	add	x2, x2, 2048
	add	w1, w28, w1
	blr	x4
.LVL408:
	.loc 1 96 0
	cbz	w0, .L355
	.loc 1 97 0
	mov	w0, -1
	str	w0, [x19]
.L355:
	add	x19, x19, 32
	b	.L349
.L352:
	.loc 1 89 0
	mov	w0, -1
	str	w0, [x19]
	b	.L353
.L362:
.LBB220:
	.loc 1 107 0
	str	wzr, [x21]
	.loc 1 112 0
	mov	w2, w26
	.loc 1 106 0
	str	wzr, [x23]
	.loc 1 112 0
	mov	w1, 1
	.loc 1 110 0
	stp	x23, x21, [x29, 120]
	.loc 1 108 0
	ldr	w0, [x20, 4]
	str	w0, [x29, 116]
	.loc 1 112 0
	add	x0, x29, 112
	bl	FlashReadPages
.LVL409:
	.loc 1 113 0
	ldr	w25, [x29, 112]
	cmn	w25, #1
	bne	.L359
	.loc 1 114 0
	ldr	w1, [x20, 4]
	adrp	x0, .LC75
	add	x0, x0, :lo12:.LC75
	bl	printk
.LVL410:
	.loc 1 115 0
	str	w25, [x20]
.L359:
	.loc 1 117 0
	ldr	x0, [x20, 16]
	cbz	x0, .L360
	.loc 1 118 0
	ldr	w2, [x0]
	add	x0, x24, :lo12:.LANCHOR0
	ldr	w3, [x0, 3392]
	cmp	w2, w3
	beq	.L360
	.loc 1 119 0
	ldr	w1, [x20, 4]
	adrp	x0, .LC76
	add	x0, x0, :lo12:.LC76
	bl	printk
.LVL411:
	.loc 1 120 0
	mov	w0, -1
	str	w0, [x20]
.L360:
	.loc 1 123 0
	ldr	x0, [x20, 8]
	cbz	x0, .L361
	.loc 1 124 0
	ldr	w3, [x22, #:lo12:check_buf]
	ldr	w2, [x0]
	cmp	w2, w3
	beq	.L361
	.loc 1 125 0
	ldr	w1, [x20, 4]
	adrp	x0, .LC77
	add	x0, x0, :lo12:.LC77
	bl	printk
.LVL412:
	.loc 1 126 0
	mov	w0, -1
	str	w0, [x20]
.L361:
	add	x20, x20, 32
.L377:
.LBE220:
	.loc 1 103 0 discriminator 1
	cmp	x20, x19
	bne	.L362
	b	.L376
.L358:
.LBB221:
	.loc 1 107 0 discriminator 1
	adrp	x24, .LANCHOR0
	add	x21, x24, :lo12:.LANCHOR0
	.loc 1 106 0 discriminator 1
	adrp	x22, check_buf
	.loc 1 107 0 discriminator 1
	add	x21, x21, 3392
	.loc 1 106 0 discriminator 1
	add	x23, x22, :lo12:check_buf
	b	.L377
.LBE221:
	.cfi_endproc
.LFE262:
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
.LFB263:
	.loc 1 134 0
	.cfi_startproc
.LVL413:
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	.loc 1 137 0
	adrp	x1, g_nand_phy_info
.LVL414:
	add	x3, x1, :lo12:g_nand_phy_info
	ubfiz	x2, x2, 5, 32
.LVL415:
	.loc 1 134 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	add	x2, x2, 4
	stp	x21, x22, [sp, 32]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	add	x19, x0, 4
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	.loc 1 142 0
	adrp	x21, g_nand_ops
	add	x22, x0, x2
	mov	x20, x1
	.loc 1 137 0
	ldrh	w23, [x3, 12]
.LVL416:
	.loc 1 147 0
	add	x24, x21, :lo12:g_nand_ops
.LVL417:
.L380:
	.loc 1 140 0 discriminator 1
	cmp	x19, x22
	bne	.L386
	.loc 1 153 0
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
.LVL418:
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL419:
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL420:
.L386:
	.cfi_restore_state
	.loc 1 141 0
	ldr	w0, [x19]
.LVL421:
	add	x2, x29, 72
	add	x1, x29, 76
	bl	l2p_addr_tran.isra.0
.LVL422:
	.loc 1 142 0
	add	x0, x21, :lo12:g_nand_ops
	ldr	w1, [x29, 76]
	ldr	x2, [x0, 8]
	ldrb	w0, [x29, 72]
	blr	x2
.LVL423:
	.loc 1 144 0
	cbnz	w0, .L381
	.loc 1 143 0
	str	wzr, [x19, -4]
.L382:
	.loc 1 146 0
	add	x0, x20, :lo12:g_nand_phy_info
	ldrh	w0, [x0, 14]
	cmp	w0, 4
	bne	.L384
	.loc 1 147 0
	ldrb	w0, [x29, 72]
	ldr	x2, [x24, 8]
	ldr	w1, [x29, 76]
	add	w1, w23, w1
	blr	x2
.LVL424:
	.loc 1 148 0
	cbz	w0, .L384
	.loc 1 149 0
	mov	w0, -1
	str	w0, [x19, -4]
.L384:
	add	x19, x19, 32
	b	.L380
.L381:
	.loc 1 145 0
	mov	w0, -1
	str	w0, [x19, -4]
	b	.L382
	.cfi_endproc
.LFE263:
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
.LFB179:
	.loc 2 98 0
	.cfi_startproc
.LVL425:
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -16
	.cfi_offset 22, -8
	and	w21, w0, 65535
.LVL426:
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	.loc 2 99 0
	sub	w2, w21, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcc	.L391
.LBB230:
.LBB231:
	.loc 2 94 0
	adrp	x0, .LANCHOR0
	add	x20, x0, :lo12:.LANCHOR0
	mov	x19, x0
.LBE231:
.LBE230:
	.loc 2 101 0
	ldrh	w2, [x20, 238]
	cmp	w2, 1024
	beq	.L391
	and	w1, w1, 65535
.LBB232:
.LBB233:
	.loc 2 103 0
	cbz	w1, .L393
.LBB234:
	.loc 2 104 0
	mov	w0, w21
	bl	P2V_block_in_plane
.LVL427:
	and	w22, w0, 65535
.LVL428:
	.loc 2 105 0
	ldr	x0, [x20, 3904]
	lsl	w1, w21, 10
	.loc 2 106 0
	mov	w2, 1
	.loc 2 105 0
	str	w1, [x0, 4]
	.loc 2 106 0
	mov	w1, w2
	ldr	x0, [x20, 3904]
	bl	FlashEraseBlocks
.LVL429:
	.loc 2 107 0
	ldr	x2, [x20, 2360]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	.loc 2 108 0
	ldr	w0, [x20, 2616]
	add	w0, w0, 1
	str	w0, [x20, 2616]
.LVL430:
.L393:
.LBE234:
	.loc 2 110 0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 232
	ldrh	w1, [x0, 6]
.LVL431:
	add	w1, w1, 1
	strh	w1, [x0, 6]
	.loc 2 111 0
	ldrh	w1, [x0, 4]
	add	x2, x0, x1, sxtw 1
	.loc 2 112 0
	add	w1, w1, 1
	and	w1, w1, 1023
	strh	w1, [x0, 4]
	.loc 2 111 0
	strh	w21, [x2, 8]
.LVL432:
.L391:
.LBE233:
.LBE232:
	.loc 2 115 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL433:
	ldp	x29, x30, [sp], 48
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE179:
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
.LFB148:
	.loc 3 528 0
	.cfi_startproc
.LVL434:
	stp	x29, x30, [sp, -128]!
	.cfi_def_cfa_offset 128
	.cfi_offset 29, -128
	.cfi_offset 30, -120
	.loc 3 539 0
	mov	w10, 0
	.loc 3 551 0
	mov	w9, 4
	.loc 3 528 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -112
	.cfi_offset 20, -104
	.loc 3 538 0
	adrp	x20, .LANCHOR0
	add	x5, x20, :lo12:.LANCHOR0
	.loc 3 528 0
	stp	x23, x24, [sp, 48]
	stp	x21, x22, [sp, 32]
	.cfi_offset 23, -80
	.cfi_offset 24, -72
	.cfi_offset 21, -96
	.cfi_offset 22, -88
	and	w23, w1, 255
.LVL435:
	stp	x25, x26, [sp, 64]
	.loc 3 541 0
	add	x8, x5, 40
	.loc 3 528 0
	stp	x27, x28, [sp, 80]
	.cfi_offset 25, -64
	.cfi_offset 26, -56
	.cfi_offset 27, -48
	.cfi_offset 28, -40
	.loc 3 528 0
	and	w24, w0, 65535
.LVL436:
	.loc 3 536 0
	mov	w21, 0
	.loc 3 537 0
	mov	w19, 0
.LVL437:
	.loc 3 538 0
	str	w24, [x5, 3912]
.L402:
	.loc 3 539 0 discriminator 1
	ldrh	w0, [x5, 12]
	cmp	w0, w10
	bhi	.L406
	.loc 3 559 0
	cbz	w21, .L401
	.loc 3 562 0
	ldr	x0, [x5, 3904]
	mov	w2, w21
	mov	w1, 0
	ubfiz	x21, x21, 5, 16
	mov	x22, 0
	.loc 3 564 0
	add	x25, x20, :lo12:.LANCHOR0
	.loc 3 562 0
	bl	FlashEraseBlocks
.LVL438:
.L410:
	.loc 3 564 0
	ldr	x0, [x25, 3904]
	add	x1, x0, x22
	ldr	w0, [x0, x22]
	cmn	w0, #1
	bne	.L409
	.loc 3 566 0
	ldr	w0, [x1, 4]
	.loc 3 565 0
	add	w19, w19, 1
.LVL439:
	and	w19, w19, 65535
.LVL440:
	.loc 3 566 0
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.LVL441:
.L409:
	add	x22, x22, 32
	.loc 3 563 0 discriminator 2
	cmp	x22, x21
	bne	.L410
	.loc 3 571 0
	cbz	w23, .L423
	.loc 3 573 0
	add	x0, x20, :lo12:.LANCHOR0
	.loc 3 572 0
	mov	w25, 1
.LVL442:
	.loc 3 573 0
	ldrh	w26, [x0, 84]
.LVL443:
	.loc 3 574 0
	lsr	w0, w26, 2
.LVL444:
	str	w0, [x29, 124]
.L411:
	.loc 3 578 0
	add	x27, x20, :lo12:.LANCHOR0
	mov	w22, 0
.LVL445:
	.loc 3 580 0
	add	x0, x27, 40
.LVL446:
	str	x0, [x29, 112]
.LVL447:
.L419:
	.loc 3 578 0
	mov	w5, 0
	.loc 3 577 0
	mov	w21, 0
.L412:
	.loc 3 578 0 discriminator 1
	ldrh	w0, [x27, 12]
.LVL448:
	cmp	w0, w5
	bhi	.L415
	.loc 3 593 0
	cbz	w21, .L401
	.loc 3 596 0
	ldr	x0, [x27, 3904]
	mov	w1, w21
	mov	w3, 1
	mov	w2, w25
	mov	x28, 0
	bl	FlashProgPages
.LVL449:
	ubfiz	x1, x21, 5, 16
.L418:
	.loc 3 598 0
	ldr	x0, [x27, 3904]
	add	x3, x0, x28
	ldr	w0, [x0, x28]
	cbz	w0, .L417
	.loc 3 600 0
	ldr	w0, [x3, 4]
	.loc 3 599 0
	add	w19, w19, 1
.LVL450:
	str	x1, [x29, 104]
	and	w19, w19, 65535
.LVL451:
	.loc 3 600 0
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.LVL452:
	ldr	x1, [x29, 104]
.L417:
	add	x28, x28, 32
	.loc 3 597 0 discriminator 2
	cmp	x1, x28
	bne	.L418
	.loc 3 604 0
	ldr	w0, [x29, 124]
.LVL453:
	add	w22, w22, w0
.LVL454:
	and	w22, w22, 65535
.LVL455:
	.loc 3 605 0
	cmp	w26, w22
	bhi	.L419
	.loc 3 611 0
	add	x26, x20, :lo12:.LANCHOR0
.LVL456:
	mov	x22, 0
.LVL457:
.L421:
	cbz	w23, .L420
	.loc 3 611 0 is_stmt 0 discriminator 1
	ldr	x0, [x26, 3904]
.LVL458:
	add	x1, x0, x22
	ldr	w0, [x0, x22]
	cbnz	w0, .L420
	.loc 3 612 0 is_stmt 1
	ldr	w0, [x1, 4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.LVL459:
.L420:
	add	x22, x22, 32
	.loc 3 610 0 discriminator 2
	cmp	x22, x28
	bne	.L421
	.loc 3 615 0
	cmp	w24, 63
	ccmp	w23, 0, 0, hi
	beq	.L401
	.loc 3 616 0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	mov	w1, w25
	ldr	x0, [x20, 3904]
.LVL460:
	bl	FlashEraseBlocks
.LVL461:
.L401:
	.loc 3 619 0
	mov	w0, w19
.LVL462:
	ldp	x19, x20, [sp, 16]
.LVL463:
	ldp	x21, x22, [sp, 32]
.LVL464:
	ldp	x23, x24, [sp, 48]
.LVL465:
	ldp	x25, x26, [sp, 64]
.LVL466:
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL467:
.L406:
	.cfi_restore_state
	.loc 3 540 0
	ldr	x1, [x5, 3904]
	ubfiz	x0, x10, 5, 16
	str	wzr, [x1, x0]
	.loc 3 541 0
	mov	w1, w24
	ldrb	w0, [x8, w10, sxtw]
	bl	V2P_block
.LVL468:
	and	w6, w0, 65535
	mov	w7, w6
	.loc 3 543 0
	cbz	w23, .L403
	.loc 3 544 0
	bl	IsBlkInVendorPart
.LVL469:
	cbnz	w0, .L404
.L403:
	.loc 3 548 0
	mov	w0, w7
	bl	FtlBbmIsBadBlock
.LVL470:
	cbnz	w0, .L405
	.loc 3 549 0
	ldr	x0, [x5, 3904]
	ubfiz	x2, x21, 5, 16
	lsl	w6, w6, 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	.loc 3 550 0
	ldr	x1, [x5, 3904]
	ldr	x0, [x5, 3920]
	add	x1, x1, x2
	.loc 3 551 0
	ldr	x2, [x5, 3928]
	.loc 3 550 0
	str	x0, [x1, 8]
	.loc 3 551 0
	ldrh	w0, [x5, 92]
	mul	w0, w0, w21
	.loc 3 552 0
	add	w21, w21, 1
	and	w21, w21, 65535
	.loc 3 551 0
	sdiv	w0, w0, w9
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L404:
	.loc 3 539 0 discriminator 2
	add	w10, w10, 1
	and	w10, w10, 65535
	b	.L402
.L405:
	.loc 3 554 0
	add	w19, w19, 1
.LVL471:
	and	w19, w19, 65535
.LVL472:
	b	.L404
.L423:
	.loc 3 533 0
	mov	w0, 6
.LVL473:
	.loc 3 534 0
	mov	w25, 0
.LVL474:
	.loc 3 531 0
	mov	w26, 1
.LVL475:
	.loc 3 533 0
	str	w0, [x29, 124]
	b	.L411
.LVL476:
.L415:
	.loc 3 579 0
	ldr	x1, [x27, 3904]
	ubfiz	x0, x5, 5, 16
	str	wzr, [x1, x0]
	.loc 3 580 0
	mov	w1, w24
	ldr	x0, [x29, 112]
	ldrb	w0, [x0, w5, sxtw]
	bl	V2P_block
.LVL477:
	and	w6, w0, 65535
	mov	w7, w6
	.loc 3 581 0
	cbz	w23, .L413
	.loc 3 582 0
	bl	IsBlkInVendorPart
.LVL478:
	cbnz	w0, .L414
.L413:
	.loc 3 585 0
	mov	w0, w7
	bl	FtlBbmIsBadBlock
.LVL479:
	cbnz	w0, .L414
	.loc 3 586 0
	ldr	x0, [x27, 3904]
	ubfiz	x2, x21, 5, 16
	add	w6, w22, w6, lsl 10
	add	x0, x0, x2
	str	w6, [x0, 4]
	.loc 3 587 0
	ldr	x1, [x27, 3904]
	ldr	x0, [x27, 3936]
	add	x1, x1, x2
	.loc 3 588 0
	mov	w2, 4
	.loc 3 587 0
	str	x0, [x1, 8]
	.loc 3 588 0
	ldrh	w0, [x27, 92]
	mul	w0, w0, w21
	.loc 3 589 0
	add	w21, w21, 1
	and	w21, w21, 65535
	.loc 3 588 0
	sdiv	w0, w0, w2
	ldr	x2, [x27, 3920]
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L414:
	.loc 3 578 0 discriminator 2
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L412
	.cfi_endproc
.LFE148:
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
.LFB182:
	.loc 2 181 0
	.cfi_startproc
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
.LBB235:
.LBB236:
	.loc 2 89 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
.LBE236:
.LBE235:
	.loc 2 181 0
	str	x23, [sp, 48]
.LBB239:
.LBB237:
	.loc 2 89 0
	add	x20, x19, 232
	.cfi_offset 23, -16
.LBE237:
.LBE239:
	.loc 2 203 0
	adrp	x23, .LC79
	add	x23, x23, :lo12:.LC79
	.loc 2 181 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
.LVL480:
.L445:
.LBB240:
.LBB238:
	.loc 2 89 0
	ldrh	w1, [x20, 6]
.LBE238:
.LBE240:
	.loc 2 186 0
	cbz	w1, .L446
	.loc 2 187 0
	ldrh	w0, [x20, 2]
	.loc 2 188 0
	sub	w1, w1, #1
	strh	w1, [x20, 6]
	.loc 2 187 0
	add	x2, x20, x0, sxtw 1
	.loc 2 189 0
	add	w0, w0, 1
	and	w0, w0, 1023
	strh	w0, [x20, 2]
	.loc 2 187 0
	ldrh	w21, [x2, 8]
.LBB241:
	.loc 2 191 0
	mov	w0, w21
	bl	P2V_block_in_plane
.LVL481:
	and	w22, w0, 65535
	.loc 2 192 0
	ldr	x0, [x19, 3904]
	lsl	w1, w21, 10
	.loc 2 193 0
	mov	w2, 1
	.loc 2 192 0
	str	w1, [x0, 4]
	.loc 2 193 0
	mov	w1, w2
	ldr	x0, [x19, 3904]
	bl	FlashEraseBlocks
.LVL482:
	.loc 2 194 0
	ldr	x2, [x19, 2360]
	ubfiz	x0, x22, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.LBE241:
	.loc 2 202 0
	sub	w1, w21, #1
.LBB242:
	.loc 2 195 0
	ldr	w0, [x19, 2616]
	add	w0, w0, 1
	str	w0, [x19, 2616]
.LBE242:
	.loc 2 202 0
	mov	w0, 65533
	cmp	w0, w1, uxth
	bcs	.L450
	.loc 2 203 0
	ldrh	w2, [x20, 6]
	mov	w1, w21
	mov	x0, x23
	bl	printk
.LVL483:
	.loc 2 205 0
	b	.L445
.L446:
	.loc 2 199 0
	adrp	x0, .LC78
	mov	w1, 0
	add	x0, x0, :lo12:.LC78
	bl	printk
.LVL484:
.L449:
	b	.L449
.L450:
	.loc 2 208 0
	mov	w0, w21
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL485:
	ldp	x29, x30, [sp], 64
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE182:
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
.LFB194:
	.loc 2 562 0
	.cfi_startproc
.LVL486:
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	mov	x19, x0
	str	x21, [sp, 32]
	.cfi_offset 21, -16
	.loc 2 566 0
	mov	w20, 0
.LVL487:
	ldrh	w1, [x0, 10]
	ldr	x0, [x0, 16]
.LVL488:
.L453:
	.loc 2 566 0 is_stmt 0 discriminator 1
	cmp	w20, w1
	beq	.L457
	.loc 2 567 0 is_stmt 1
	mov	x21, x0
	ldrh	w2, [x0], 2
	cbnz	w2, .L454
	.loc 2 568 0
	bl	FtlFreeSysBlkQueueOut
.LVL489:
	and	w1, w0, 65535
	strh	w0, [x21]
	.loc 2 569 0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L455
	.loc 2 570 0
	adrp	x0, .LANCHOR0+238
	ldrh	w2, [x0, #:lo12:.LANCHOR0+238]
	adrp	x0, .LC80
	add	x0, x0, :lo12:.LC80
	bl	printk
.LVL490:
.L456:
	b	.L456
.L455:
	.loc 2 576 0
	ldr	w0, [x19, 48]
	.loc 2 574 0
	strh	wzr, [x19, 2]
	.loc 2 576 0
	add	w0, w0, 1
	str	w0, [x19, 48]
	.loc 2 577 0
	ldrh	w0, [x19, 8]
	.loc 2 575 0
	strh	w20, [x19]
	.loc 2 577 0
	add	w0, w0, 1
	strh	w0, [x19, 8]
.L457:
	.loc 2 581 0
	ldrh	w0, [x19, 10]
	cmp	w0, w20
	bhi	.L459
.LBB245:
.LBB246:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 581
	add	x1, x1, 192
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL491:
.L459:
.LBE246:
.LBE245:
	.loc 2 583 0
	mov	w0, 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
.LVL492:
	ldp	x29, x30, [sp], 48
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL493:
.L454:
	.cfi_restore_state
	.loc 2 566 0 discriminator 2
	add	w20, w20, 1
.LVL494:
	and	w20, w20, 65535
.LVL495:
	b	.L453
	.cfi_endproc
.LFE194:
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	ftl_memset
	.type	ftl_memset, %function
ftl_memset:
.LFB266:
	.loc 1 201 0
	.cfi_startproc
.LVL496:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 1 202 0
	uxtw	x2, w2
.LVL497:
	.loc 1 201 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 1 202 0
	bl	memset
.LVL498:
	.loc 1 203 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE266:
	.size	ftl_memset, .-ftl_memset
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
.LFB145:
	.loc 3 279 0
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	.loc 3 297 0
	mov	w0, 65535
	.loc 3 279 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	.loc 3 282 0
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	.loc 3 279 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -16
	.cfi_offset 22, -8
	.loc 3 287 0
	add	x1, x19, 2560
	.loc 3 291 0
	add	x2, x19, 2560
	.loc 3 305 0
	strh	wzr, [x19, 3216]
	.loc 3 306 0
	strh	wzr, [x19, 3300]
	.loc 3 287 0
	stp	wzr, wzr, [x1, 4]
	stp	wzr, wzr, [x1, 12]
	.loc 3 288 0
	str	wzr, [x19, 2580]
	.loc 3 284 0
	str	wzr, [x19, 2584]
	.loc 3 291 0
	stp	wzr, wzr, [x2, 36]
	stp	wzr, wzr, [x2, 44]
	.loc 3 297 0
	str	w0, [x19, 3948]
	.loc 3 301 0
	mov	w0, -1
	strh	w0, [x19, 3296]
	.loc 3 302 0
	strh	w0, [x19, 3298]
	.loc 3 303 0
	mov	w0, 32
	.loc 3 292 0
	str	wzr, [x19, 2616]
	.loc 3 303 0
	strh	w0, [x19, 3208]
	.loc 3 304 0
	mov	w0, 128
	.loc 3 293 0
	str	wzr, [x19, 2620]
	.loc 3 304 0
	strh	w0, [x19, 3210]
	.loc 3 294 0
	str	wzr, [x19, 2624]
	.loc 3 299 0
	str	wzr, [x19, 3212]
	.loc 3 296 0
	str	wzr, [x19, 3220]
	.loc 3 300 0
	str	wzr, [x19, 3912]
	.loc 3 295 0
	str	wzr, [x19, 3944]
	.loc 3 298 0
	str	wzr, [x19, 3952]
	.loc 3 307 0
	strh	wzr, [x19, 3338]
	.loc 3 310 0
	ldrh	w0, [x19, 86]
	lsl	w0, w0, 1
	bl	ftl_malloc
.LVL499:
	.loc 3 311 0
	ldrh	w1, [x19, 86]
	.loc 3 310 0
	str	x0, [x19, 3272]
	.loc 3 311 0
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
.LVL500:
	.loc 3 312 0
	ldrh	w21, [x19, 12]
	.loc 3 311 0
	str	x0, [x19, 3288]
	.loc 3 312 0
	lsl	w22, w21, 5
	.loc 3 313 0
	lsl	w21, w21, 7
	mov	w0, w21
	bl	ftl_malloc
.LVL501:
	str	x0, [x19, 3960]
	.loc 3 314 0
	mov	w0, w22
	bl	ftl_malloc
.LVL502:
	str	x0, [x19, 3968]
	.loc 3 315 0
	mov	w0, w21
	bl	ftl_malloc
.LVL503:
	str	x0, [x19, 3976]
	.loc 3 316 0
	mov	w0, w22
	bl	ftl_malloc
.LVL504:
	str	x0, [x19, 3904]
	.loc 3 317 0
	mov	w0, w22
	bl	ftl_malloc
.LVL505:
	str	x0, [x19, 3256]
	.loc 3 320 0
	ldrh	w0, [x19, 12]
	.loc 3 319 0
	ldrh	w21, [x19, 90]
	.loc 3 320 0
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19, 3264]
	.loc 3 321 0
	mov	w0, w21
	bl	ftl_malloc
.LVL506:
	str	x0, [x19, 3376]
	.loc 3 322 0
	mov	w0, w21
	bl	ftl_malloc
.LVL507:
	str	x0, [x19, 3984]
	.loc 3 323 0
	mov	w0, w21
	bl	ftl_malloc
.LVL508:
	str	x0, [x19, 3992]
	.loc 3 324 0
	ldr	w0, [x19, 3264]
	mul	w0, w21, w0
	bl	ftl_malloc
.LVL509:
	str	x0, [x19, 3240]
	.loc 3 325 0
	mov	w0, w21
	bl	ftl_malloc
.LVL510:
	str	x0, [x19, 3936]
	.loc 3 326 0
	mov	w0, w21
	bl	ftl_malloc
.LVL511:
	str	x0, [x19, 3920]
	.loc 3 327 0
	ldr	w1, [x19, 3264]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
.LVL512:
	.loc 3 329 0
	ldrh	w21, [x19, 92]
	.loc 3 327 0
	str	x0, [x19, 3232]
	.loc 3 329 0
	ldrh	w0, [x19, 12]
	mul	w21, w21, w0
	.loc 3 330 0
	mov	w0, w21
	bl	ftl_malloc
.LVL513:
	str	x0, [x19, 3384]
	.loc 3 331 0
	lsl	w0, w21, 2
	bl	ftl_malloc
.LVL514:
	str	x0, [x19, 3928]
	.loc 3 332 0
	ldrh	w1, [x19, 92]
	ldr	w0, [x19, 3264]
	mul	w0, w1, w0
	bl	ftl_malloc
.LVL515:
	str	x0, [x19, 3248]
	.loc 3 335 0
	ldrh	w0, [x19, 22]
	ubfiz	w0, w0, 1, 15
	strh	w0, [x19, 4000]
	.loc 3 336 0
	and	w0, w0, 65534
	bl	ftl_malloc
.LVL516:
	str	x0, [x19, 4008]
	.loc 3 337 0
	ldrh	w0, [x19, 4000]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19, 4000]
	.loc 3 338 0
	lsl	w0, w0, 9
	bl	ftl_malloc
.LVL517:
	.loc 3 345 0
	ldrh	w21, [x19, 22]
	.loc 3 338 0
	str	x0, [x19, 4016]
	.loc 3 339 0
	add	x0, x0, 32
	str	x0, [x19, 2360]
	.loc 3 345 0
	lsl	w21, w21, 1
	.loc 3 347 0
	mov	w0, w21
	bl	ftl_malloc
.LVL518:
	str	x0, [x19, 4024]
	.loc 3 349 0
	mov	w0, w21
	bl	ftl_malloc
.LVL519:
	.loc 3 350 0
	ldr	w21, [x19, 108]
	.loc 3 349 0
	str	x0, [x19, 2352]
	.loc 3 350 0
	lsl	w21, w21, 1
	.loc 3 351 0
	mov	w0, w21
	bl	ftl_malloc
.LVL520:
	str	x0, [x19, 4032]
	.loc 3 352 0
	mov	w0, w21
	bl	ftl_malloc
.LVL521:
	str	x0, [x19, 4040]
	.loc 3 353 0
	ldrh	w0, [x19, 22]
	.loc 3 370 0
	adrp	x21, .LANCHOR3
	add	x21, x21, :lo12:.LANCHOR3
	.loc 3 353 0
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
.LVL522:
	str	x0, [x20, #:lo12:.LANCHOR0]
	.loc 3 354 0
	ldrh	w0, [x19, 100]
	lsl	w0, w0, 1
	bl	ftl_malloc
.LVL523:
	str	x0, [x19, 128]
	.loc 3 355 0
	ldrh	w0, [x19, 100]
	lsl	w0, w0, 1
	bl	ftl_malloc
.LVL524:
	str	x0, [x19, 4048]
	.loc 3 356 0
	ldrh	w0, [x19, 100]
	lsl	w0, w0, 2
	bl	ftl_malloc
.LVL525:
	str	x0, [x19, 4056]
	.loc 3 357 0
	ldrh	w0, [x19, 102]
	lsl	w0, w0, 2
	bl	ftl_malloc
.LVL526:
	.loc 3 358 0
	ldrh	w2, [x19, 102]
	mov	w1, 0
	.loc 3 357 0
	str	x0, [x19, 4064]
	.loc 3 358 0
	lsl	w2, w2, 2
	bl	ftl_memset
.LVL527:
	.loc 3 360 0
	ldrh	w0, [x19, 116]
	.loc 3 361 0
	lsl	w0, w0, 2
	bl	ftl_malloc
.LVL528:
	str	x0, [x19, 4072]
	.loc 3 362 0
	ldr	w0, [x19, 108]
	.loc 3 363 0
	lsl	w0, w0, 2
	bl	ftl_malloc
.LVL529:
	str	x0, [x19, 4080]
	.loc 3 364 0
	ldrh	w0, [x19, 118]
	.loc 3 365 0
	lsl	w0, w0, 4
	bl	ftl_malloc
.LVL530:
	.loc 3 366 0
	ldrh	w1, [x19, 118]
	.loc 3 365 0
	str	x0, [x19, 2544]
	.loc 3 366 0
	ldrh	w0, [x19, 90]
	.loc 3 367 0
	mul	w0, w1, w0
	bl	ftl_malloc
.LVL531:
	str	x0, [x19, 4088]
	.loc 3 368 0
	ldrh	w1, [x19, 22]
	.loc 3 369 0
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
.LVL532:
	str	x0, [x19, 2336]
	.loc 3 370 0
	ldrh	w0, [x19, 78]
	.loc 3 371 0
	ldrh	w1, [x19, 34]
	.loc 3 370 0
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x21, -256]
	.loc 3 371 0
	mul	w0, w1, w0
	.loc 3 372 0
	lsl	w0, w0, 2
	bl	ftl_malloc
.LVL533:
	.loc 3 374 0
	ldrh	w4, [x21, -256]
	add	x5, x19, 176
	.loc 3 373 0
	ldrh	w6, [x19, 34]
	mov	x1, 1
	.loc 3 372 0
	str	x0, [x19, 168]
	.loc 3 373 0
	mov	w2, w4
	mov	x0, x20
.L464:
.LVL534:
	.loc 3 373 0 is_stmt 0 discriminator 1
	cmp	w1, w6
	bcc	.L465
	mov	w2, 8
	.loc 3 379 0 is_stmt 1
	add	x4, x0, :lo12:.LANCHOR0
	sub	w2, w2, w1
	add	x4, x4, 136
	add	x2, x2, 1
	.loc 3 373 0
	mov	x3, 0
.L466:
	add	x3, x3, 1
	.loc 3 377 0 discriminator 1
	cmp	x3, x2
	bne	.L467
	.loc 3 383 0
	add	x1, x0, :lo12:.LANCHOR0
.LVL535:
	ldr	x2, [x1, 4032]
	cbnz	x2, .L468
.L470:
	.loc 3 385 0
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 224
	adrp	x0, .LC81
	add	x0, x0, :lo12:.LC81
	bl	printk
.LVL536:
	.loc 3 386 0
	mov	w0, -1
.L463:
	.loc 3 484 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL537:
.L465:
	.cfi_restore_state
	.loc 3 374 0 discriminator 3
	ldr	x3, [x19, 168]
	.loc 3 373 0 discriminator 3
	add	w1, w1, 1
.LVL538:
	.loc 3 374 0 discriminator 3
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x5], 8
	b	.L464
.LVL539:
.L467:
	.loc 3 379 0 discriminator 2
	add	x5, x1, x3
	add	x5, x4, x5, lsl 3
	str	xzr, [x5, 24]
	b	.L466
.LVL540:
.L468:
	.loc 3 388 0
	ldr	x2, [x1, 4040]
	cbz	x2, .L470
	.loc 3 393 0
	ldr	x2, [x1, 4072]
	cbz	x2, .L470
	.loc 3 398 0
	ldr	x2, [x1, 4080]
	cbz	x2, .L470
	.loc 3 404 0
	ldr	x2, [x1, 2544]
	cbz	x2, .L470
	.loc 3 409 0
	ldr	x2, [x1, 4088]
	cbz	x2, .L470
	.loc 3 414 0
	ldr	x2, [x1, 2336]
	cbz	x2, .L470
	.loc 3 419 0
	ldr	x2, [x1, 168]
	cbz	x2, .L470
	.loc 3 425 0
	ldr	x1, [x1, 2352]
	cbz	x1, .L470
	.loc 3 430 0
	add	x1, x0, :lo12:.LANCHOR0
	ldr	x2, [x1, 3272]
	cbz	x2, .L470
	.loc 3 430 0 is_stmt 0 discriminator 1
	ldr	x2, [x1, 3288]
	cbz	x2, .L470
	.loc 3 435 0 is_stmt 1
	ldr	x2, [x1, 3960]
	cbz	x2, .L470
	.loc 3 435 0 is_stmt 0 discriminator 1
	ldr	x2, [x1, 3976]
	cbz	x2, .L470
	.loc 3 435 0 discriminator 2
	ldr	x2, [x1, 3904]
	cbz	x2, .L470
	.loc 3 435 0 discriminator 3
	ldr	x2, [x1, 3256]
	cbz	x2, .L470
	.loc 3 435 0 discriminator 4
	ldr	x2, [x1, 3968]
	cbz	x2, .L470
	.loc 3 440 0 is_stmt 1
	ldr	x2, [x1, 3376]
	cbz	x2, .L470
	.loc 3 440 0 is_stmt 0 discriminator 1
	ldr	x2, [x1, 3984]
	cbz	x2, .L470
	.loc 3 440 0 discriminator 2
	ldr	x1, [x1, 3992]
	cbz	x1, .L470
	.loc 3 441 0 is_stmt 1 discriminator 3
	add	x0, x0, :lo12:.LANCHOR0
	.loc 3 440 0 discriminator 3
	ldr	x1, [x0, 3240]
	cbz	x1, .L470
	.loc 3 441 0
	ldr	x1, [x0, 3936]
	cbz	x1, .L470
	.loc 3 441 0 is_stmt 0 discriminator 1
	ldr	x1, [x0, 3920]
	cbz	x1, .L470
	.loc 3 441 0 discriminator 2
	ldr	x1, [x0, 3232]
	cbz	x1, .L470
	.loc 3 446 0 is_stmt 1
	ldr	x1, [x0, 3384]
	cbz	x1, .L470
	.loc 3 446 0 is_stmt 0 discriminator 1
	ldr	x1, [x0, 3928]
	cbz	x1, .L470
	.loc 3 446 0 discriminator 2
	ldr	x1, [x0, 3248]
	cbz	x1, .L470
	.loc 3 452 0 is_stmt 1
	ldr	x1, [x0, 2360]
	cbz	x1, .L470
	.loc 3 452 0 is_stmt 0 discriminator 1
	ldr	x1, [x0, 4008]
	cbz	x1, .L470
	.loc 3 458 0 is_stmt 1
	ldr	x0, [x0, 128]
	cbz	x0, .L470
	.loc 3 464 0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0, 4048]
	cbz	x1, .L470
	.loc 3 470 0
	ldr	x1, [x0, 4056]
	cbz	x1, .L470
	.loc 3 476 0
	ldr	x0, [x0, 4064]
	cbz	x0, .L470
	.loc 3 483 0
	mov	w0, 0
	b	.L463
	.cfi_endproc
.LFE145:
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
.LFB164:
	.loc 4 66 0
	.cfi_startproc
.LVL541:
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	mov	x22, x0
	.loc 4 69 0
	adrp	x0, .LANCHOR3-256
.LVL542:
	.loc 4 66 0
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	.loc 4 74 0
	adrp	x20, .LANCHOR1
	add	x20, x20, :lo12:.LANCHOR1
	.loc 4 69 0
	ldrh	w2, [x0, #:lo12:.LANCHOR3-256]
	.loc 4 74 0
	add	x20, x20, 240
	.loc 4 66 0
	str	x23, [sp, 48]
	.cfi_offset 23, -16
	.loc 4 74 0
	adrp	x23, .LANCHOR0
	add	x23, x23, :lo12:.LANCHOR0
	.loc 4 69 0
	mov	x19, 0
	.loc 4 66 0
	mov	x21, x1
	.loc 4 69 0
	lsl	w2, w2, 2
	mov	w1, 0
.LVL543:
	mov	x0, x21
	bl	ftl_memset
.LVL544:
.L568:
	.loc 4 72 0
	ldrh	w0, [x22, x19]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L565
	.loc 4 74 0
	ldrh	w1, [x23, 78]
	cmp	w1, w0
	bhi	.L567
	.loc 4 74 0 is_stmt 0 discriminator 1
	adrp	x0, .LC1
	mov	w2, 74
	mov	x1, x20
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL545:
.L567:
	.loc 4 75 0 is_stmt 1 discriminator 2
	ldrh	w2, [x22, x19]
	mov	w1, 1
	add	x19, x19, 2
	.loc 4 70 0 discriminator 2
	cmp	x19, 1024
	.loc 4 75 0 discriminator 2
	ubfx	x0, x2, 5, 11
	lsl	w2, w1, w2
	lsl	x0, x0, 2
	ldr	w1, [x21, x0]
	orr	w1, w1, w2
	str	w1, [x21, x0]
	.loc 4 70 0 discriminator 2
	bne	.L568
.L565:
	.loc 4 77 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL546:
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE164:
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
.LFB166:
	.loc 4 149 0
	.cfi_startproc
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 4 150 0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	.loc 4 149 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 4 152 0
	mov	w2, 16
	add	x0, x0, 148
	.loc 4 150 0
	strh	w1, [x0, -12]
	.loc 4 152 0
	mov	w1, 255
	.loc 4 151 0
	strh	wzr, [x0, -6]
	.loc 4 152 0
	bl	ftl_memset
.LVL547:
	.loc 4 153 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE166:
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
.LFB176:
	.loc 2 76 0
	.cfi_startproc
.LVL548:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 2 79 0
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	.loc 2 83 0
	mov	w2, 2048
	.loc 2 76 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 2 83 0
	mov	w1, 0
	.loc 2 82 0
	strh	w0, [x3, 232]
	.loc 2 83 0
	add	x0, x3, 240
.LVL549:
	.loc 2 79 0
	strh	wzr, [x3, 234]
	.loc 2 80 0
	strh	wzr, [x3, 236]
	.loc 2 81 0
	strh	wzr, [x3, 238]
	.loc 2 83 0
	bl	ftl_memset
.LVL550:
	.loc 2 85 0
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE176:
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
.LFB192:
	.loc 2 488 0
	.cfi_startproc
.LVL551:
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	.loc 2 492 0
	adrp	x21, .LANCHOR0
	.loc 2 488 0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	and	w23, w0, 65535
.LVL552:
	.loc 2 492 0
	add	x0, x21, :lo12:.LANCHOR0
	.loc 2 488 0
	stp	x19, x20, [sp, 16]
	str	x25, [sp, 64]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.cfi_offset 25, -16
	.loc 2 488 0
	and	x19, x1, 65535
	.loc 2 492 0
	ldrh	w0, [x0, 116]
	cmp	w0, w23
	bcs	.L577
	.loc 2 492 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
.LVL553:
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 492
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL554:
.L577:
	.loc 2 493 0 is_stmt 1
	add	x20, x21, :lo12:.LANCHOR0
	ubfiz	x0, x23, 2, 16
	ldr	x1, [x20, 4072]
.LVL555:
	ldr	w24, [x1, x0]
.LVL556:
	.loc 2 495 0
	cbnz	w24, .L578
	.loc 2 496 0
	ldr	x0, [x20, 2544]
.LVL557:
	lsl	x19, x19, 4
.LVL558:
	ldrh	w2, [x20, 90]
	mov	w1, 255
	add	x0, x0, x19
	ldr	x0, [x0, 8]
	bl	ftl_memset
.LVL559:
	.loc 2 497 0
	ldr	x0, [x20, 2544]
	strh	w23, [x0, x19]
	.loc 2 498 0
	ldr	x0, [x20, 2544]
	add	x19, x0, x19
	str	wzr, [x19, 4]
.LVL560:
.L579:
	.loc 2 519 0
	mov	w0, 0
	ldr	x25, [sp, 64]
.LVL561:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL562:
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL563:
.L578:
	.cfi_restore_state
	.loc 2 502 0
	ldr	x0, [x20, 2544]
.LVL564:
	lsl	x19, x19, 4
.LVL565:
	.loc 2 501 0
	add	x22, x20, 3344
	.loc 2 504 0
	mov	w2, 1
	.loc 2 502 0
	add	x0, x0, x19
	.loc 2 504 0
	mov	w1, w2
	.loc 2 501 0
	str	w24, [x20, 3348]
	.loc 2 502 0
	ldr	x0, [x0, 8]
	str	x0, [x20, 3352]
	.loc 2 503 0
	ldr	x0, [x20, 3384]
	str	x0, [x20, 3360]
	.loc 2 504 0
	mov	x0, x22
	bl	FlashReadPages
.LVL566:
	.loc 2 505 0
	ldr	x25, [x20, 3360]
.LVL567:
	.loc 2 507 0
	ldrh	w0, [x25, 8]
	cmp	w0, w23
	beq	.L580
	.loc 2 508 0
	mov	w2, w24
	mov	w1, w23
	adrp	x0, .LC82
	add	x0, x0, :lo12:.LC82
	bl	printk
.LVL568:
	.loc 2 509 0
	ldr	x1, [x20, 3360]
	mov	w3, 4
	adrp	x0, .LC83
	mov	w2, w3
	add	x0, x0, :lo12:.LC83
	bl	rknand_print_hex
.LVL569:
	.loc 2 510 0
	ldrh	w3, [x20, 116]
	adrp	x0, .LC84
	ldr	x1, [x20, 4072]
	mov	w2, 4
	add	x0, x0, :lo12:.LC84
	bl	rknand_print_hex
.LVL570:
.L580:
	.loc 2 513 0
	ldrh	w0, [x25, 8]
	cmp	w0, w23
	beq	.L581
	.loc 2 513 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 513
	add	x1, x1, 256
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL571:
.L581:
	.loc 2 516 0 is_stmt 1
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21, 2544]
	add	x0, x0, x19
	str	wzr, [x0, 4]
	.loc 2 517 0
	ldr	x0, [x21, 2544]
	strh	w23, [x0, x19]
	.loc 2 518 0
	b	.L579
	.cfi_endproc
.LFE192:
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	ftl_free_no_use_map_blk
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
.LFB193:
	.loc 2 522 0
	.cfi_startproc
.LVL572:
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	.loc 2 531 0
	mov	w1, 0
	.loc 2 522 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	.cfi_offset 25, -16
	.cfi_offset 26, -8
	.loc 2 531 0
	ldrh	w2, [x0, 10]
	.loc 2 524 0
	ldp	x21, x20, [x0, 32]
.LVL573:
	.loc 2 523 0
	ldr	x23, [x0, 16]
.LVL574:
	.loc 2 531 0
	lsl	w2, w2, 1
	mov	x0, x21
.LVL575:
	bl	ftl_memset
.LVL576:
	.loc 2 532 0
	mov	w0, 0
.LVL577:
.L584:
	.loc 2 532 0 is_stmt 0 discriminator 1
	ldrh	w1, [x19, 6]
	cmp	w1, w0
	bhi	.L588
	.loc 2 540 0 is_stmt 1
	ldrh	w26, [x21]
.LVL578:
	.loc 2 544 0
	adrp	x24, .LANCHOR0
	add	x24, x24, :lo12:.LANCHOR0
	.loc 2 541 0
	mov	w25, 0
.LVL579:
	.loc 2 543 0
	mov	w20, 0
.LVL580:
.L589:
	.loc 2 543 0 is_stmt 0 discriminator 1
	ldrh	w0, [x19, 10]
.LVL581:
	cmp	w0, w20
	bhi	.L593
	.loc 2 559 0 is_stmt 1
	mov	w0, w25
	ldp	x19, x20, [sp, 16]
.LVL582:
	ldp	x21, x22, [sp, 32]
.LVL583:
	ldp	x23, x24, [sp, 48]
.LVL584:
	ldp	x25, x26, [sp, 64]
.LVL585:
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL586:
.L588:
	.cfi_restore_state
	.loc 2 533 0
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20, x1]
.LVL587:
	.loc 2 534 0
	mov	w1, 0
	.loc 2 533 0
	ubfx	x2, x2, 10, 16
.LVL588:
.L585:
	.loc 2 534 0 discriminator 1
	ldrh	w3, [x19, 10]
	cmp	w3, w1
	bhi	.L587
	.loc 2 532 0 discriminator 2
	add	w0, w0, 1
.LVL589:
	and	w0, w0, 65535
.LVL590:
	b	.L584
.L587:
	.loc 2 535 0
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x23, x3]
	cmp	w4, w2
	bne	.L586
	.loc 2 536 0
	ldrh	w4, [x21, x3]
	add	w4, w4, 1
	strh	w4, [x21, x3]
.L586:
	.loc 2 534 0 discriminator 2
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L585
.LVL591:
.L593:
	.loc 2 544 0
	ldrh	w0, [x19]
	uxtw	x22, w20
	cmp	w0, w20
	bne	.L590
	.loc 2 544 0 is_stmt 0 discriminator 1
	ldrh	w0, [x24, 84]
	ldrh	w1, [x19, 2]
	cmp	w1, w0
	bcs	.L590
	.loc 2 545 0 is_stmt 1
	strh	w0, [x21, x22, lsl 1]
.L590:
	.loc 2 546 0
	lsl	x22, x22, 1
	ldrh	w0, [x21, x22]
	cmp	w26, w0
	bls	.L591
	mov	w25, w20
	mov	w26, w0
.L591:
	.loc 2 550 0
	cbnz	w0, .L592
	.loc 2 550 0 is_stmt 0 discriminator 1
	ldrh	w0, [x23, x22]
	cbz	w0, .L592
	.loc 2 552 0 is_stmt 1
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.LVL592:
	.loc 2 553 0
	strh	wzr, [x23, x22]
	.loc 2 554 0
	ldrh	w0, [x19, 8]
	sub	w0, w0, #1
	strh	w0, [x19, 8]
.LVL593:
.L592:
	.loc 2 543 0 discriminator 2
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L589
	.cfi_endproc
.LFE193:
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
.LFB196:
	.loc 2 632 0
	.cfi_startproc
.LVL594:
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	.loc 2 640 0
	mov	w1, 65535
	.loc 2 632 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	str	x23, [sp, 48]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.cfi_offset 23, -16
	.loc 2 635 0
	ldr	x20, [x0, 16]
.LVL595:
	.loc 2 640 0
	ldrh	w0, [x0]
.LVL596:
	cmp	w0, w1
	bne	.L599
	.loc 2 641 0
	ldrh	w0, [x19, 8]
	cbz	w0, .L600
	.loc 2 641 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 641
	add	x1, x1, 272
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL597:
.L600:
	.loc 2 642 0 is_stmt 1
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	strh	w0, [x19, 8]
	.loc 2 643 0
	bl	FtlFreeSysBlkQueueOut
.LVL598:
	strh	w0, [x20]
	.loc 2 644 0
	strh	wzr, [x19, 2]
	.loc 2 646 0
	ldr	w0, [x19, 48]
	.loc 2 645 0
	strh	wzr, [x19]
	.loc 2 646 0
	add	w0, w0, 1
	str	w0, [x19, 48]
.LVL599:
.L601:
	.loc 2 677 0
	mov	w0, 0
	ldr	x23, [sp, 48]
.LVL600:
	ldp	x19, x20, [sp, 16]
.LVL601:
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL602:
.L599:
	.cfi_restore_state
	.loc 2 650 0
	ubfiz	x0, x0, 1, 16
	.loc 2 651 0
	adrp	x21, .LANCHOR0
	.loc 2 656 0
	ldr	w1, [x19, 48]
	.loc 2 636 0
	ldr	x23, [x19, 40]
.LVL603:
	.loc 2 650 0
	ldrh	w22, [x20, x0]
	.loc 2 651 0
	add	x20, x21, :lo12:.LANCHOR0
.LVL604:
	ldrh	w0, [x19, 2]
	orr	w0, w0, w22, lsl 10
	str	w0, [x20, 3348]
	.loc 2 652 0
	ldr	x0, [x20, 3376]
	str	x0, [x20, 3352]
	.loc 2 653 0
	ldr	x0, [x20, 3384]
	str	x0, [x20, 3360]
	.loc 2 656 0
	str	w1, [x0, 4]
	.loc 2 657 0
	mov	w1, -1291
	strh	w1, [x0, 8]
	.loc 2 658 0
	ldrh	w1, [x19, 4]
	strh	w1, [x0]
	.loc 2 659 0
	strh	w22, [x0, 2]
	.loc 2 661 0
	mov	w1, 255
	ldrh	w2, [x20, 84]
	ldr	x0, [x20, 3376]
	lsl	w2, w2, 3
	bl	ftl_memset
.LVL605:
	.loc 2 662 0
	mov	w0, 0
.LVL606:
	.loc 2 663 0
	mov	w1, 0
.LVL607:
.L602:
	.loc 2 663 0 is_stmt 0 discriminator 1
	ldrh	w2, [x19, 6]
	cmp	w2, w1
	bhi	.L604
	.loc 2 670 0 is_stmt 1
	add	x0, x21, :lo12:.LANCHOR0
.LVL608:
	mov	w2, 1
	mov	w3, 0
	mov	w1, w2
.LVL609:
	add	x0, x0, 3344
	bl	FlashProgPages
.LVL610:
	.loc 2 671 0
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	.loc 2 675 0
	mov	x0, x19
	bl	ftl_map_blk_gc
.LVL611:
	.loc 2 676 0
	b	.L601
.LVL612:
.L604:
	.loc 2 664 0
	ubfiz	x2, x1, 2, 16
	ldr	w3, [x23, x2]
	cmp	w22, w3, lsr 10
	bne	.L603
	.loc 2 665 0
	add	w0, w0, 1
.LVL613:
	.loc 2 666 0
	ldr	x4, [x20, 3376]
	.loc 2 665 0
	and	w0, w0, 65535
.LVL614:
	.loc 2 666 0
	ubfiz	x3, x0, 3, 16
	str	w1, [x4, x3]
	.loc 2 667 0
	ldr	w4, [x23, x2]
	ldr	x2, [x20, 3376]
	add	x2, x2, x3
	str	w4, [x2, 4]
.L603:
	.loc 2 663 0 discriminator 2
	add	w1, w1, 1
.LVL615:
	and	w1, w1, 65535
.LVL616:
	b	.L602
	.cfi_endproc
.LFE196:
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
.LFB197:
	.loc 2 680 0
	.cfi_startproc
.LVL617:
	stp	x29, x30, [sp, -96]!
	.cfi_def_cfa_offset 96
	.cfi_offset 29, -96
	.cfi_offset 30, -88
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -48
	.cfi_offset 24, -40
	.loc 2 686 0
	adrp	x23, .LANCHOR0
	.loc 2 703 0
	add	x24, x23, :lo12:.LANCHOR0
	.loc 2 680 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	.cfi_offset 19, -80
	.cfi_offset 20, -72
	.cfi_offset 21, -64
	.cfi_offset 22, -56
	mov	x19, x0
	stp	x25, x26, [sp, 64]
	.loc 2 699 0
	adrp	x22, .LANCHOR1
	.loc 2 680 0
	stp	x27, x28, [sp, 80]
	.cfi_offset 25, -32
	.cfi_offset 26, -24
	.cfi_offset 27, -16
	.cfi_offset 28, -8
	.loc 2 680 0
	mov	w26, w1
	mov	x27, x2
	mov	x25, x23
	.loc 2 699 0
	add	x22, x22, :lo12:.LANCHOR1
	.loc 2 703 0
	add	x20, x24, 3344
	.loc 2 683 0
	mov	w21, 0
.LVL618:
.L610:
	.loc 2 686 0
	add	x0, x23, :lo12:.LANCHOR0
	ldr	w1, [x0, 2580]
	add	w1, w1, 1
	str	w1, [x0, 2580]
	.loc 2 687 0
	ldrh	w0, [x0, 84]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L611
	.loc 2 687 0 is_stmt 0 discriminator 1
	ldrh	w1, [x19]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L612
.L611:
	.loc 2 688 0 is_stmt 1
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.LVL619:
.L612:
	.loc 2 699 0
	ldrh	w1, [x19]
	ldr	x0, [x19, 16]
	ldrh	w0, [x0, x1, lsl 1]
	cbnz	w0, .L613
	.loc 2 699 0 is_stmt 0 discriminator 1
	adrp	x0, .LC1
	mov	w2, 699
	add	x1, x22, 304
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL620:
.L613:
	.loc 2 700 0 is_stmt 1
	ldrh	w1, [x19]
	ldrh	w0, [x19, 10]
	cmp	w1, w0
	bcc	.L614
	.loc 2 700 0 is_stmt 0 discriminator 1
	adrp	x0, .LC1
	mov	w2, 700
	add	x1, x22, 304
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL621:
.L614:
	.loc 2 701 0 is_stmt 1
	ldrh	w1, [x19]
	.loc 2 707 0
	mov	w2, 16
	.loc 2 701 0
	ldr	x0, [x19, 16]
	ldrh	w28, [x0, x1, lsl 1]
	.loc 2 707 0
	mov	w1, 0
	.loc 2 703 0
	ldrh	w0, [x19, 2]
	.loc 2 704 0
	str	x27, [x20, 8]
	.loc 2 703 0
	orr	w0, w0, w28, lsl 10
	str	w0, [x20, 4]
	.loc 2 705 0
	ldr	x0, [x24, 3384]
	str	x0, [x20, 16]
	.loc 2 707 0
	bl	ftl_memset
.LVL622:
	.loc 2 708 0
	ldr	x0, [x20, 16]
.LVL623:
	.loc 2 713 0
	mov	w3, 1
	.loc 2 709 0
	ldr	w1, [x19, 48]
	.loc 2 713 0
	mov	w2, w3
	.loc 2 710 0
	strh	w26, [x0, 8]
	.loc 2 709 0
	str	w1, [x0, 4]
	.loc 2 711 0
	ldrh	w1, [x19, 4]
	strh	w1, [x0]
	.loc 2 713 0
	mov	w1, w3
	.loc 2 712 0
	strh	w28, [x0, 2]
	.loc 2 713 0
	mov	x0, x20
.LVL624:
	bl	FlashProgPages
.LVL625:
	.loc 2 714 0
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2]
	.loc 2 716 0
	ldr	w1, [x20]
	cmn	w1, #1
	bne	.L615
	.loc 2 717 0
	ldr	w1, [x20, 4]
	adrp	x0, .LC85
	add	x0, x0, :lo12:.LC85
	.loc 2 718 0
	add	w21, w21, 1
.LVL626:
	and	w21, w21, 65535
.LVL627:
	.loc 2 717 0
	bl	printk
.LVL628:
	.loc 2 719 0
	ldrh	w0, [x19, 2]
	cmp	w0, 2
	bhi	.L616
	.loc 2 720 0
	ldrh	w0, [x24, 84]
	sub	w0, w0, #1
	strh	w0, [x19, 2]
.L616:
	.loc 2 722 0
	cmp	w21, 3
	bls	.L617
	.loc 2 723 0
	add	x25, x25, :lo12:.LANCHOR0
	adrp	x0, .LC86
	mov	w2, w21
	add	x0, x0, :lo12:.LC86
	ldr	w1, [x25, 3348]
	bl	printk
.LVL629:
.L618:
	b	.L618
.L617:
	.loc 2 727 0
	ldr	w0, [x19, 52]
	cbz	w0, .L610
.L627:
	b	.L627
.L615:
	.loc 2 731 0
	cmp	w0, 1
	beq	.L610
	.loc 2 734 0
	ldr	x0, [x19, 40]
	ldr	w1, [x20, 4]
	str	w1, [x0, w26, uxtw 2]
	.loc 2 736 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
.LVL630:
	ldp	x21, x22, [sp, 32]
.LVL631:
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
.LVL632:
	ldp	x27, x28, [sp, 80]
.LVL633:
	ldp	x29, x30, [sp], 96
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE197:
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
.LFB195:
	.loc 2 586 0
	.cfi_startproc
.LVL634:
	stp	x29, x30, [sp, -96]!
	.cfi_def_cfa_offset 96
	.cfi_offset 29, -96
	.cfi_offset 30, -88
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -80
	.cfi_offset 20, -72
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -64
	.cfi_offset 22, -56
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	.cfi_offset 23, -48
	.cfi_offset 24, -40
	.cfi_offset 25, -32
	.cfi_offset 26, -24
	.cfi_offset 27, -16
	.cfi_offset 28, -8
	.loc 2 590 0
	ldr	x20, [x0, 16]
.LVL635:
	.loc 2 591 0
	ldr	x25, [x0, 40]
.LVL636:
	.loc 2 594 0
	bl	ftl_free_no_use_map_blk
.LVL637:
	.loc 2 596 0
	ldrh	w1, [x19, 10]
	ldrh	w2, [x19, 8]
	sub	w1, w1, #4
	cmp	w2, w1
	blt	.L630
	.loc 2 597 0
	ubfiz	x0, x0, 1, 16
	ldrh	w24, [x20, x0]
.LVL638:
	.loc 2 598 0
	cbz	w24, .L630
	.loc 2 598 0 is_stmt 0 discriminator 1
	ldr	w1, [x19, 52]
	cbnz	w1, .L630
	.loc 2 599 0 is_stmt 1
	mov	w1, 1
	str	w1, [x19, 52]
	.loc 2 600 0
	strh	wzr, [x20, x0]
	.loc 2 601 0
	ldrh	w0, [x19, 8]
	.loc 2 602 0
	ldrh	w1, [x19, 2]
	.loc 2 601 0
	sub	w0, w0, #1
	strh	w0, [x19, 8]
	.loc 2 602 0
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0, 84]
	cmp	w1, w0
	bcc	.L631
	.loc 2 603 0
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.LVL639:
.L631:
	.loc 2 607 0 discriminator 1
	add	x26, x22, :lo12:.LANCHOR0
	.loc 2 613 0 discriminator 1
	adrp	x23, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR1
	.loc 2 607 0 discriminator 1
	add	x21, x26, 3344
	.loc 2 613 0 discriminator 1
	add	x23, x23, 320
	.loc 2 586 0 discriminator 1
	mov	w20, 0
.LVL640:
.L632:
	.loc 2 605 0 discriminator 1
	ldrh	w0, [x19, 6]
	cmp	w0, w20
	bhi	.L636
	.loc 2 621 0
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
.LVL641:
	.loc 2 622 0
	str	wzr, [x19, 52]
.LVL642:
.L630:
	.loc 2 626 0
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x22, 84]
	cmp	w1, w0
	bcc	.L637
	.loc 2 627 0
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.LVL643:
.L637:
	.loc 2 629 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
.LVL644:
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL645:
	ldp	x25, x26, [sp, 64]
.LVL646:
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL647:
.L636:
	.cfi_restore_state
	.loc 2 606 0
	ubfiz	x27, x20, 2, 16
	ldr	w0, [x25, x27]
	cmp	w24, w0, lsr 10
	bne	.L633
	.loc 2 608 0
	ldr	x28, [x26, 3384]
	.loc 2 611 0
	mov	w2, 1
	.loc 2 607 0
	ldr	x0, [x26, 3984]
	.loc 2 611 0
	mov	w1, w2
	.loc 2 608 0
	stp	x0, x28, [x21, 8]
	.loc 2 609 0
	ldr	w0, [x25, x27]
	str	w0, [x21, 4]
	.loc 2 611 0
	mov	x0, x21
	bl	FlashReadPages
.LVL648:
	.loc 2 613 0
	ldrh	w0, [x28, 8]
	cmp	w0, w20
	beq	.L634
	.loc 2 613 0 is_stmt 0 discriminator 1
	adrp	x0, .LC1
	mov	w2, 613
	mov	x1, x23
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL649:
.L634:
	.loc 2 614 0 is_stmt 1
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L635
	.loc 2 615 0
	str	wzr, [x25, x27]
.L633:
	.loc 2 605 0 discriminator 2
	add	w20, w20, 1
.LVL650:
	and	w20, w20, 65535
.LVL651:
	b	.L632
.L635:
	.loc 2 617 0
	ldr	x2, [x21, 8]
	mov	w1, w20
	mov	x0, x19
	bl	FtlMapWritePage
.LVL652:
	b	.L633
	.cfi_endproc
.LFE195:
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	flush_l2p_region
	.type	flush_l2p_region, %function
flush_l2p_region:
.LFB198:
	.loc 2 739 0
	.cfi_startproc
.LVL653:
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 2 742 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x20, x0, 4, 16
	ldr	x0, [x19, 2544]
.LVL654:
	add	x1, x0, x20
	.loc 2 743 0
	ldr	x2, [x1, 8]
	ldrh	w1, [x0, x20]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	sub	x0, x0, #248
	bl	FtlMapWritePage
.LVL655:
	.loc 2 744 0
	ldr	x0, [x19, 2544]
	add	x0, x0, x20
	ldr	w1, [x0, 4]
	and	w1, w1, 2147483647
	str	w1, [x0, 4]
	.loc 2 746 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE198:
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
.LFB201:
	.loc 2 802 0
	.cfi_startproc
.LVL656:
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	mov	w23, w2
	.loc 2 805 0
	adrp	x24, .LANCHOR0
	add	x2, x24, :lo12:.LANCHOR0
.LVL657:
	.loc 2 802 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	mov	x22, x1
	stp	x19, x20, [sp, 16]
	str	x25, [sp, 64]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.cfi_offset 25, -16
	.loc 2 805 0
	ldrh	w19, [x2, 88]
	add	w1, w19, 7
.LVL658:
	.loc 2 806 0
	mov	x19, 1
	.loc 2 805 0
	lsr	w21, w0, w1
	and	w21, w21, 65535
.LVL659:
	.loc 2 806 0
	lsl	x19, x19, x1
	.loc 2 808 0
	ldr	w1, [x2, 2560]
	.loc 2 806 0
	sub	w19, w19, #1
	and	w19, w19, w0
	.loc 2 808 0
	cmp	w0, w1
	.loc 2 806 0
	and	x19, x19, 65535
	.loc 2 808 0
	bcc	.L645
	.loc 2 808 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
.LVL660:
	mov	w2, 808
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL661:
.L645:
	.loc 2 810 0 is_stmt 1
	add	x0, x24, :lo12:.LANCHOR0
.LVL662:
	ldrh	w1, [x0, 118]
	.loc 2 811 0
	ldr	x2, [x0, 2544]
	.loc 2 810 0
	mov	x0, 0
.LVL663:
.L646:
	and	x20, x0, 65535
.LVL664:
	.loc 2 810 0 is_stmt 0 discriminator 1
	cmp	w20, w1
	bcc	.L651
	.loc 2 825 0 is_stmt 1
	bl	select_l2p_ram_region
.LVL665:
	and	x20, x0, 65535
.LVL666:
	.loc 2 826 0
	add	x2, x24, :lo12:.LANCHOR0
.LVL667:
	ubfiz	x1, x20, 4, 16
	.loc 2 825 0
	mov	w25, w0
	.loc 2 826 0
	ldr	x2, [x2, 2544]
	add	x3, x2, x1
	ldrh	w2, [x2, x1]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L652
	.loc 2 826 0 is_stmt 0 discriminator 1
	ldr	w1, [x3, 4]
	tbz	w1, #31, .L652
	.loc 2 827 0 is_stmt 1
	bl	flush_l2p_region
.LVL668:
.L652:
	.loc 2 830 0
	mov	w1, w25
	mov	w0, w21
	bl	load_l2p_region
.LVL669:
	.loc 2 831 0
	b	.L647
.L651:
	add	x0, x0, 1
	.loc 2 811 0
	add	x3, x2, x0, lsl 4
	ldrh	w3, [x3, -16]
	cmp	w3, w21
	bne	.L646
.L647:
	.loc 2 813 0
	cbnz	w23, .L648
	.loc 2 814 0
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x0, [x0, 2544]
	add	x0, x0, x20, lsl 4
	ldr	x0, [x0, 8]
.LVL670:
	ldr	w0, [x0, x19, lsl 2]
	str	w0, [x22]
.L649:
	.loc 2 820 0
	add	x24, x24, :lo12:.LANCHOR0
	ldr	x0, [x24, 2544]
	add	x20, x0, x20, lsl 4
.LVL671:
	ldr	w0, [x20, 4]
	cmn	w0, #1
	beq	.L654
	.loc 2 821 0
	add	w0, w0, 1
	str	w0, [x20, 4]
.L654:
	.loc 2 832 0
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
.LVL672:
	ldp	x21, x22, [sp, 32]
.LVL673:
	ldp	x23, x24, [sp, 48]
.LVL674:
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL675:
.L648:
	.cfi_restore_state
	.loc 2 816 0
	add	x2, x24, :lo12:.LANCHOR0
	lsl	x0, x20, 4
	mov	x1, x0
	ldr	w4, [x22]
	ldr	x3, [x2, 2544]
	add	x0, x3, x0
	ldr	x0, [x0, 8]
.LVL676:
	str	w4, [x0, x19, lsl 2]
	.loc 2 818 0
	strh	w21, [x2, 2552]
	.loc 2 817 0
	ldr	x0, [x2, 2544]
	add	x0, x0, x1
	ldr	w1, [x0, 4]
	orr	w1, w1, -2147483648
	str	w1, [x0, 4]
	b	.L649
	.cfi_endproc
.LFE201:
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlReUsePrevPpa
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
.LFB219:
	.loc 2 1677 0
	.cfi_startproc
.LVL677:
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	mov	w21, w0
	stp	x19, x20, [sp, 16]
	.loc 2 1678 0
	lsr	w0, w1, 10
.LVL678:
	.loc 2 1677 0
	str	w1, [x29, 76]
	.loc 2 1680 0
	adrp	x22, .LANCHOR0
	.loc 2 1677 0
	str	x23, [sp, 48]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.cfi_offset 23, -32
	.loc 2 1678 0
	bl	P2V_block_in_plane
.LVL679:
	.loc 2 1680 0
	add	x2, x22, :lo12:.LANCHOR0
	.loc 2 1678 0
	and	w0, w0, 65535
.LVL680:
	.loc 2 1680 0
	ubfiz	x20, x0, 1, 16
	ldr	x3, [x2, 2352]
	ldrh	w1, [x3, x20]
.LVL681:
	cbnz	w1, .L660
	.loc 2 1681 0
	ldr	x19, [x2, 2384]
	cbz	x19, .L661
.LBB247:
	.loc 2 1684 0
	ldrh	w3, [x2, 2392]
.LVL682:
	.loc 2 1685 0
	mov	x4, -6148914691236517206
	ldr	x2, [x2, 2336]
	movk	x4, 0xaaab, lsl 0
	.loc 2 1693 0
	mov	w5, 65535
	.loc 2 1685 0
	sub	x19, x19, x2
	asr	x19, x19, 1
	mul	x19, x19, x4
	.loc 2 1693 0
	mov	w4, 6
	.loc 2 1685 0
	and	w19, w19, 65535
.LVL683:
.L662:
	.loc 2 1686 0 discriminator 1
	cmp	w1, w3
	beq	.L661
	.loc 2 1687 0
	cmp	w19, w0
	bne	.L663
	.loc 2 1688 0
	add	x23, x22, :lo12:.LANCHOR0
	mov	w1, w19
.LVL684:
	add	x0, x23, 2384
.LVL685:
	bl	List_remove_node
.LVL686:
	ldrh	w0, [x23, 2392]
	cbnz	w0, .L664
	.loc 2 1688 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1688
	add	x1, x1, 352
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL687:
.L664:
	.loc 2 1688 0 discriminator 3
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w0, [x22, 2392]
	sub	w0, w0, #1
	strh	w0, [x22, 2392]
	.loc 2 1689 0 is_stmt 1 discriminator 3
	mov	w0, w19
	bl	INSERT_DATA_LIST
.LVL688:
	.loc 2 1690 0 discriminator 3
	ldr	x1, [x22, 2352]
	ldrh	w0, [x1, x20]
	add	w0, w0, 1
	strh	w0, [x1, x20]
.LVL689:
.L661:
.LBE247:
	.loc 2 1701 0
	add	x1, x29, 76
.LVL690:
	mov	w2, 1
	mov	w0, w21
.LVL691:
	bl	log2phys
.LVL692:
	.loc 2 1702 0
	ldp	x19, x20, [sp, 16]
.LVL693:
	ldp	x21, x22, [sp, 32]
.LVL694:
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL695:
.L663:
	.cfi_restore_state
.LBB248:
	.loc 2 1693 0
	umull	x19, w19, w4
.LVL696:
	ldrh	w19, [x2, x19]
.LVL697:
	cmp	w19, w5
	beq	.L661
	.loc 2 1686 0 discriminator 2
	add	w1, w1, 1
.LVL698:
	and	w1, w1, 65535
.LVL699:
	b	.L662
.LVL700:
.L660:
.LBE248:
	.loc 2 1699 0
	add	w1, w1, 1
.LVL701:
	strh	w1, [x3, x20]
	b	.L661
	.cfi_endproc
.LFE219:
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
.LFB230:
	.loc 2 2325 0
	.cfi_startproc
	sub	sp, sp, #96
	.cfi_def_cfa_offset 96
	.loc 2 2330 0
	adrp	x0, .LC87
	mov	w1, 0
	add	x0, x0, :lo12:.LC87
	.loc 2 2325 0
	stp	x29, x30, [sp, 16]
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	add	x29, sp, 16
	.cfi_def_cfa 29, 80
	stp	x21, x22, [sp, 48]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	.loc 2 2331 0
	adrp	x22, .LANCHOR0
	.loc 2 2336 0
	add	x21, x22, :lo12:.LANCHOR0
	.loc 2 2325 0
	stp	x19, x20, [sp, 32]
	str	x23, [sp, 64]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.cfi_offset 23, -32
	.loc 2 2331 0
	mov	w20, 0
.LVL702:
	.loc 2 2336 0
	add	x19, x21, 3344
	.loc 2 2330 0
	bl	printk
.LVL703:
.L671:
	.loc 2 2331 0 discriminator 1
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w0, [x0, 2560]
	cmp	w20, w0
	bcc	.L677
	.loc 2 2353 0
	ldp	x19, x20, [sp, 32]
.LVL704:
	ldp	x21, x22, [sp, 48]
	ldp	x29, x30, [sp, 16]
	ldr	x23, [sp, 64]
	add	sp, sp, 96
	.cfi_remember_state
	.cfi_restore 29
	.cfi_restore 30
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL705:
.L677:
	.cfi_restore_state
	.loc 2 2332 0
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w20
	bl	log2phys
.LVL706:
	.loc 2 2333 0
	tst	x20, 2047
	bne	.L672
	.loc 2 2334 0
	ldr	w2, [x29, 76]
	adrp	x0, .LC88
	mov	w1, w20
	add	x0, x0, :lo12:.LC88
	bl	printk
.LVL707:
.L672:
	.loc 2 2335 0
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L674
	.loc 2 2339 0
	ldr	x23, [x21, 3384]
	.loc 2 2342 0
	mov	w2, 0
	.loc 2 2336 0
	stp	wzr, w0, [x19]
	.loc 2 2342 0
	mov	w1, 1
	.loc 2 2338 0
	ldr	x0, [x21, 3376]
	str	x0, [x19, 8]
	.loc 2 2342 0
	mov	x0, x19
	.loc 2 2339 0
	str	x23, [x19, 16]
	.loc 2 2337 0
	str	w20, [x19, 24]
	.loc 2 2342 0
	bl	FlashReadPages
.LVL708:
	.loc 2 2343 0
	ldr	w0, [x19]
	cmp	w0, 256
	ccmn	w0, #1, 4, ne
	beq	.L675
	.loc 2 2344 0
	ldr	w0, [x23, 8]
	cmp	w20, w0
	beq	.L674
.L675:
	.loc 2 2347 0
	ldp	x1, x0, [x19, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC89
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC89
	ldr	w2, [x19, 4]
	mov	w1, w20
	bl	printk
.LVL709:
.L674:
	.loc 2 2331 0 discriminator 2
	add	w20, w20, 1
	b	.L671
	.cfi_endproc
.LFE230:
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
.LFB257:
	.loc 5 419 0
	.cfi_startproc
	.loc 5 425 0
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x2, x1, 2696
	ldr	w3, [x2, 80]
	cbz	w3, .L680
	.loc 5 426 0
	ldr	w0, [x1, 2560]
	ldr	w3, [x2, 84]
	cmp	w3, w0
	bcs	.L681
	.loc 5 419 0
	stp	x29, x30, [sp, -160]!
	.cfi_def_cfa_offset 160
	.cfi_offset 29, -160
	.cfi_offset 30, -152
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -144
	.cfi_offset 20, -136
.LBB254:
.LBB255:
	.loc 5 428 0
	mov	x19, x1
.LBE255:
.LBE254:
	mov	w20, 2048
	.loc 5 419 0
	str	x21, [sp, 32]
	.cfi_offset 21, -128
.LVL710:
.L686:
.LBB260:
.LBB257:
	.loc 5 428 0
	ldr	w1, [x19, 2560]
	ldr	w0, [x19, 2780]
	cmp	w0, w1
	bcs	.L683
	.loc 5 430 0
	add	x1, x29, 60
	mov	w2, 0
	bl	log2phys
.LVL711:
	.loc 5 431 0
	ldr	w0, [x19, 2780]
	.loc 5 432 0
	ldr	w1, [x29, 60]
	.loc 5 431 0
	add	w0, w0, 1
	str	w0, [x19, 2780]
	.loc 5 432 0
	cmn	w1, #1
	beq	.L684
.LBB256:
	.loc 5 435 0
	str	w0, [x29, 88]
	.loc 5 436 0
	adrp	x0, ftl_temp_buf
	add	x0, x0, :lo12:ftl_temp_buf
	str	x0, [x29, 72]
	.loc 5 437 0
	add	x0, x29, 96
	str	x0, [x29, 80]
	.loc 5 438 0
	add	x0, x29, 160
	.loc 5 434 0
	str	w1, [x29, 68]
	.loc 5 439 0
	mov	w2, 0
	mov	w1, 1
	.loc 5 438 0
	str	wzr, [x0, -96]!
	.loc 5 439 0
	bl	FlashReadPages
.LVL712:
	.loc 5 440 0
	ldr	w0, [x29, 64]
	cmp	w0, 256
	bne	.L683
	.loc 5 442 0
	ldr	w0, [x29, 60]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
.LVL713:
	bl	FtlGcRefreshBlock
.LVL714:
.L683:
.LBE256:
.LBE257:
.LBE260:
	.loc 5 466 0
	ldp	x19, x20, [sp, 16]
.LBB261:
.LBB258:
	mov	w0, -1
.LBE258:
.LBE261:
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 160
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.L684:
	.cfi_restore_state
.LBB262:
.LBB259:
	.loc 5 427 0
	subs	w20, w20, #1
	bne	.L686
	b	.L683
.LVL715:
.L681:
	.cfi_def_cfa 31, 0
	.cfi_restore 19
	.cfi_restore 20
	.cfi_restore 21
	.cfi_restore 29
	.cfi_restore 30
.LBE259:
.LBE262:
	.loc 5 451 0
	ldr	w0, [x1, 2564]
	.loc 5 449 0
	stp	w0, wzr, [x2, 76]
	.loc 5 450 0
	str	wzr, [x2, 84]
.L690:
	.loc 5 420 0
	mov	w0, 0
	.loc 5 465 0
	ret
.L680:
.LBB263:
	.loc 5 456 0
	ldr	w3, [x1, 2564]
	ldr	w5, [x2, 76]
	add	w2, w3, 1048576
	cmp	w5, w2
	bhi	.L689
	.loc 5 454 0
	ldr	w2, [x1, 2620]
	lsr	w4, w2, 10
	mov	w2, 33554432
	asr	w2, w2, w4
.LVL716:
	.loc 5 457 0
	add	w2, w2, w5
.LVL717:
	cmp	w3, w2
	bhi	.L689
	.loc 5 458 0
	ldrb	w1, [x1, 2316]
	cbnz	w1, .L690
.L689:
	.loc 5 460 0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 1
	.loc 5 462 0
	str	w3, [x0, 2772]
	.loc 5 460 0
	str	w1, [x0, 2776]
	.loc 5 461 0
	str	wzr, [x0, 2780]
	b	.L690
.LBE263:
	.cfi_endproc
.LFE257:
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlMapBlkWriteDump_data
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
.LFB206:
	.loc 2 946 0
	.cfi_startproc
.LVL718:
	.loc 2 952 0
	ldr	w1, [x0, 56]
	cbz	w1, .L704
	.loc 2 946 0
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	.loc 2 956 0
	adrp	x1, .LANCHOR0
	add	x2, x1, :lo12:.LANCHOR0
	.loc 2 946 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	mov	x19, x0
	str	x21, [sp, 32]
	.cfi_offset 21, -16
	.loc 2 956 0
	add	x0, x2, 3344
.LVL719:
	mov	x21, x1
	.loc 2 948 0
	ldrh	w20, [x19, 6]
	.loc 2 957 0
	ldr	x4, [x2, 3384]
	.loc 2 948 0
	sub	w20, w20, #1
	.loc 2 956 0
	ldr	x3, [x2, 3984]
	.loc 2 948 0
	and	w20, w20, 65535
.LVL720:
	.loc 2 949 0
	ldr	x5, [x19, 40]
.LVL721:
	.loc 2 957 0
	stp	x3, x4, [x0, 8]
	.loc 2 979 0
	ubfiz	x4, x20, 2, 16
	.loc 2 954 0
	str	wzr, [x19, 56]
	.loc 2 979 0
	ldr	w4, [x5, x4]
.LVL722:
	.loc 2 980 0
	str	w4, [x0, 4]
	.loc 2 981 0
	cbz	w4, .L698
	.loc 2 983 0
	mov	w2, 1
	mov	w1, w2
	bl	FlashReadPages
.LVL723:
.L699:
	.loc 2 990 0
	add	x1, x21, :lo12:.LANCHOR0
	mov	x0, x19
	ldr	x2, [x1, 3352]
	mov	w1, w20
	bl	FtlMapWritePage
.LVL724:
	.loc 2 992 0
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
.LVL725:
	ldp	x29, x30, [sp], 48
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL726:
.L698:
	.cfi_restore_state
	.loc 2 987 0
	ldrh	w2, [x2, 90]
	mov	w1, 255
	mov	x0, x3
	bl	ftl_memset
.LVL727:
	b	.L699
.LVL728:
.L704:
	.cfi_def_cfa 31, 0
	.cfi_restore 19
	.cfi_restore 20
	.cfi_restore 21
	.cfi_restore 29
	.cfi_restore 30
	ret
	.cfi_endproc
.LFE206:
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
.LFB209:
	.loc 2 1144 0
	.cfi_startproc
	stp	x29, x30, [sp, -112]!
	.cfi_def_cfa_offset 112
	.cfi_offset 29, -112
	.cfi_offset 30, -104
	.loc 2 1152 0
	mov	w1, 0
	.loc 2 1144 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -96
	.cfi_offset 20, -88
	.loc 2 1150 0
	adrp	x20, .LANCHOR3
	.loc 2 1144 0
	stp	x21, x22, [sp, 32]
	.loc 2 1150 0
	add	x0, x20, :lo12:.LANCHOR3
	.cfi_offset 21, -80
	.cfi_offset 22, -72
	.loc 2 1151 0
	adrp	x21, .LANCHOR0
	add	x19, x21, :lo12:.LANCHOR0
	.loc 2 1144 0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	.cfi_offset 23, -64
	.cfi_offset 24, -56
	.cfi_offset 25, -48
	.cfi_offset 26, -40
	.loc 2 1211 0
	adrp	x25, .LANCHOR1
	.loc 2 1144 0
	stp	x27, x28, [sp, 80]
	.cfi_offset 27, -32
	.cfi_offset 28, -24
	.loc 2 1211 0
	add	x25, x25, :lo12:.LANCHOR1
	.loc 2 1150 0
	strh	wzr, [x0, -184]
	.loc 2 1211 0
	add	x25, x25, 368
	.loc 2 1152 0
	ldr	x0, [x19, 4080]
	ldr	w2, [x19, 108]
	.loc 2 1151 0
	strh	wzr, [x19, 124]
	.loc 2 1152 0
	lsl	w2, w2, 2
	bl	ftl_memset
.LVL729:
	.loc 2 1153 0
	ldr	x0, [x19, 4032]
	mov	w1, 0
	ldr	w2, [x19, 108]
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL730:
	.loc 2 1154 0
	ldr	x0, [x19, 4056]
	mov	w1, 0
	ldrh	w2, [x19, 100]
	lsl	w2, w2, 2
	bl	ftl_memset
.LVL731:
	.loc 2 1155 0
	ldr	x0, [x19, 128]
	mov	w1, 0
	ldrh	w2, [x19, 100]
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL732:
	.loc 2 1156 0
	mov	w2, 12
	mov	w1, 255
	add	x0, x19, 2632
	bl	ftl_memset
.LVL733:
	.loc 2 1158 0
	ldrh	w24, [x19, 20]
.LVL734:
.L708:
	.loc 2 1158 0 is_stmt 0 discriminator 1
	ldrh	w0, [x19, 22]
	cmp	w0, w24
	bls	.L748
	.loc 2 1162 0 is_stmt 1 discriminator 1
	ldrh	w9, [x19, 12]
	.loc 2 1169 0 discriminator 1
	mov	x5, 0
	ldrh	w8, [x19, 92]
	mov	w22, 0
	.loc 2 1164 0 discriminator 1
	add	x7, x19, 40
	.loc 2 1169 0 discriminator 1
	mov	w6, 4
	b	.L749
.L710:
	.loc 2 1164 0
	ldrb	w0, [x7, x5]
	mov	w1, w24
	bl	V2P_block
.LVL735:
	and	w4, w0, 65535
	.loc 2 1165 0
	bl	FtlBbmIsBadBlock
.LVL736:
	cbnz	w0, .L709
	.loc 2 1167 0
	ldr	x0, [x19, 3960]
	ubfiz	x2, x22, 5, 16
	lsl	w4, w4, 10
	add	x0, x0, x2
	str	w4, [x0, 4]
	.loc 2 1168 0
	ldr	x1, [x19, 3960]
	ldr	x0, [x19, 3240]
	add	x1, x1, x2
	.loc 2 1169 0
	ldr	x2, [x19, 3248]
	.loc 2 1168 0
	str	x0, [x1, 8]
	.loc 2 1169 0
	mul	w0, w22, w8
	.loc 2 1170 0
	add	w22, w22, 1
.LVL737:
	and	w22, w22, 65535
.LVL738:
	.loc 2 1169 0
	sdiv	w0, w0, w6
	add	x0, x2, x0, sxtw 2
	str	x0, [x1, 16]
.L709:
	add	x5, x5, 1
.L749:
	.loc 2 1162 0 discriminator 1
	cmp	w9, w5, uxth
	bhi	.L710
	.loc 2 1173 0
	cbnz	w22, .L711
.L747:
	.loc 2 1158 0
	add	w24, w24, 1
.LVL739:
	and	w24, w24, 65535
.LVL740:
	b	.L708
.L711:
	.loc 2 1176 0
	ldr	x0, [x19, 3960]
	mov	w1, w22
	mov	w2, 1
	ubfiz	x22, x22, 5, 16
.LVL741:
	mov	x23, 0
	.loc 2 1297 0
	add	x28, x19, 2632
	.loc 2 1176 0
	bl	FlashReadPages
.LVL742:
.L746:
	.loc 2 1178 0
	ldr	x0, [x19, 3960]
	add	x1, x0, x23
	.loc 2 1181 0
	ldr	w0, [x0, x23]
	.loc 2 1178 0
	ldr	w26, [x1, 4]
	.loc 2 1181 0
	cmn	w0, #1
	.loc 2 1179 0
	ldr	x27, [x1, 16]
	.loc 2 1178 0
	ubfx	x26, x26, 10, 16
	.loc 2 1181 0
	bne	.L714
	mov	w3, 16
	.loc 2 1185 0
	mov	w4, 65535
.L716:
	.loc 2 1183 0
	ldr	x0, [x19, 3960]
	.loc 2 1184 0
	mov	w2, 1
	stp	w4, w3, [x29, 104]
	.loc 2 1183 0
	add	x0, x0, x23
	ldr	w1, [x0, 4]
	add	w1, w1, 1
	str	w1, [x0, 4]
	.loc 2 1184 0
	mov	w1, w2
	ldr	x0, [x19, 3960]
	add	x0, x0, x23
	bl	FlashReadPages
.LVL743:
	.loc 2 1185 0
	ldp	w4, w3, [x29, 104]
	ldrh	w0, [x27]
	cmp	w0, w4
	.loc 2 1186 0
	ldr	x0, [x19, 3960]
	.loc 2 1185 0
	bne	.L713
	.loc 2 1186 0
	mov	w1, -1
	str	w1, [x0, x23]
	.loc 2 1194 0
	ldr	x0, [x19, 3960]
	ldr	w0, [x0, x23]
	cmp	w0, w1
	bne	.L714
.L715:
	.loc 2 1336 0
	mov	w1, 0
	mov	w0, w26
	bl	FtlFreeSysBlkQueueIn
.LVL744:
	b	.L719
.L713:
	.loc 2 1188 0
	ldr	w0, [x0, x23]
	cmn	w0, #1
	bne	.L714
	sub	w3, w3, #1
	.loc 2 1182 0 discriminator 2
	ands	w3, w3, 65535
	bne	.L716
	b	.L715
.L714:
	.loc 2 1196 0
	ldr	w1, [x19, 2596]
	ldr	w0, [x27, 4]
	cmn	w1, #1
	beq	.L717
	.loc 2 1196 0 is_stmt 0 discriminator 1
	cmp	w1, w0
	bhi	.L718
.L717:
	.loc 2 1198 0 is_stmt 1
	cmn	w0, #1
	beq	.L718
	.loc 2 1201 0
	add	w1, w0, 1
	str	w1, [x19, 2596]
.L718:
	.loc 2 1206 0
	ldrh	w1, [x27]
	mov	w2, 61604
	cmp	w1, w2
	beq	.L720
	bhi	.L721
	mov	w0, 61574
	cmp	w1, w0
	beq	.L722
.L719:
	add	x23, x23, 32
	.loc 2 1177 0 discriminator 2
	cmp	x22, x23
	bne	.L746
	b	.L747
.L721:
	.loc 2 1206 0
	mov	w0, 61634
	cmp	w1, w0
	beq	.L723
	mov	w0, 65535
	cmp	w1, w0
	beq	.L715
	b	.L719
.L723:
	.loc 2 1211 0
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w1, [x0, -184]
	ldr	w0, [x19, 108]
	cmp	w1, w0
	bls	.L725
	.loc 2 1211 0 is_stmt 0 discriminator 1
	adrp	x0, .LC1
	mov	w2, 1211
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL745:
.L725:
	.loc 2 1212 0 is_stmt 1
	add	x1, x20, :lo12:.LANCHOR3
	ldr	w3, [x19, 108]
	.loc 2 1214 0
	ldr	x4, [x19, 4080]
	.loc 2 1212 0
	and	w2, w3, 65535
	ldrh	w1, [x1, -184]
	sub	w0, w2, #1
	sxth	x0, w0
	sub	w2, w2, w1
	sub	w2, w2, #1
	sxth	w2, w2
.L726:
	.loc 2 1212 0 is_stmt 0 discriminator 1
	cmp	w0, w2
	bgt	.L732
	.loc 2 1235 0 is_stmt 1
	tbz	w0, #31, .L766
	b	.L719
.L732:
	.loc 2 1214 0
	sxtw	x5, w0
	ldr	w8, [x27, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L727
	.loc 2 1216 0
	ldr	w2, [x4]
	cbnz	w2, .L728
	.loc 2 1216 0 is_stmt 0 discriminator 1
	cmp	w3, w1
	beq	.L728
	.loc 2 1221 0 is_stmt 1
	add	x2, x20, :lo12:.LANCHOR3
	add	w1, w1, 1
	strh	w1, [x2, -184]
.L728:
	mov	w1, 0
.L729:
	.loc 2 1224 0 discriminator 1
	cmp	w1, w0
	bne	.L730
	.loc 2 1229 0
	ldr	x1, [x19, 4080]
	ldr	w2, [x27, 4]
	str	w2, [x1, x6]
	.loc 2 1230 0
	ldr	x1, [x19, 4032]
	strh	w26, [x1, x5, lsl 1]
	.loc 2 1235 0
	tbnz	w0, #31, .L719
	.loc 2 1242 0
	add	x1, x20, :lo12:.LANCHOR3
	ldr	w2, [x19, 108]
	ldrh	w1, [x1, -184]
	sub	w2, w2, w1
	sub	w2, w2, #1
	cmp	w0, w2, sxth
	bgt	.L719
.L766:
	.loc 2 1244 0
	add	x2, x20, :lo12:.LANCHOR3
	add	w1, w1, 1
	strh	w1, [x2, -184]
	.loc 2 1245 0
	ldr	x1, [x19, 4080]
	ldr	w2, [x27, 4]
	str	w2, [x1, x0, lsl 2]
	.loc 2 1246 0
	ldr	x1, [x19, 4032]
.L784:
	.loc 2 1290 0
	strh	w26, [x1, x0, lsl 1]
	b	.L719
.L730:
	.loc 2 1226 0 discriminator 3
	ldr	x4, [x19, 4080]
	sxtw	x2, w1
	lsl	x3, x2, 2
	.loc 2 1227 0 discriminator 3
	lsl	x2, x2, 1
	.loc 2 1226 0 discriminator 3
	add	x7, x4, x3
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w7, [x7, 4]
	str	w7, [x4, x3]
	.loc 2 1227 0 discriminator 3
	ldr	x3, [x19, 4032]
	add	x4, x3, x2
	ldrh	w4, [x4, 2]
	strh	w4, [x3, x2]
	b	.L729
.L727:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L726
.L722:
	.loc 2 1252 0
	ldrh	w1, [x19, 124]
	ldrh	w0, [x19, 100]
	cmp	w1, w0
	bls	.L735
	.loc 2 1252 0 is_stmt 0 discriminator 1
	adrp	x0, .LC1
	mov	w2, 1252
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL746:
.L735:
	.loc 2 1253 0 is_stmt 1
	ldrh	w3, [x19, 100]
	ldrh	w1, [x19, 124]
	sub	w2, w3, #1
	.loc 2 1255 0
	ldr	x4, [x19, 4056]
	.loc 2 1253 0
	sxth	x0, w2
	sub	w2, w2, w1
.L736:
	.loc 2 1253 0 is_stmt 0 discriminator 1
	cmp	w0, w2
	ble	.L741
	.loc 2 1255 0 is_stmt 1
	sxtw	x5, w0
	ldr	w8, [x27, 4]
	lsl	x6, x5, 2
	ldr	w7, [x4, x6]
	cmp	w8, w7
	bls	.L737
	.loc 2 1257 0
	ldr	w2, [x4]
	cbnz	w2, .L738
	.loc 2 1257 0 is_stmt 0 discriminator 1
	cmp	w3, w1
	beq	.L738
	.loc 2 1265 0 is_stmt 1
	add	w1, w1, 1
	strh	w1, [x19, 124]
.L738:
	mov	w1, 0
.L739:
	.loc 2 1267 0 discriminator 1
	cmp	w1, w0
	bne	.L740
	.loc 2 1272 0
	ldr	x1, [x19, 4056]
	ldr	w2, [x27, 4]
	str	w2, [x1, x6]
	.loc 2 1273 0
	ldr	x1, [x19, 128]
	strh	w26, [x1, x5, lsl 1]
.L741:
	.loc 2 1278 0
	tbnz	w0, #31, .L719
	.loc 2 1286 0
	ldrh	w1, [x19, 100]
	ldrh	w2, [x19, 124]
	sub	w1, w1, #1
	sub	w1, w1, w2
	cmp	w0, w1, sxth
	bgt	.L719
	.loc 2 1289 0
	ldr	x1, [x19, 4056]
	.loc 2 1288 0
	add	w2, w2, 1
	strh	w2, [x19, 124]
	.loc 2 1289 0
	ldr	w2, [x27, 4]
	str	w2, [x1, x0, lsl 2]
	.loc 2 1290 0
	ldr	x1, [x19, 128]
	b	.L784
.L740:
	.loc 2 1269 0 discriminator 3
	ldr	x4, [x19, 4056]
	sxtw	x2, w1
	lsl	x3, x2, 2
	.loc 2 1270 0 discriminator 3
	lsl	x2, x2, 1
	.loc 2 1269 0 discriminator 3
	add	x7, x4, x3
	add	w1, w1, 1
	sxth	w1, w1
	ldr	w7, [x7, 4]
	str	w7, [x4, x3]
	.loc 2 1270 0 discriminator 3
	ldr	x3, [x19, 128]
	add	x4, x3, x2
	ldrh	w4, [x4, 2]
	strh	w4, [x3, x2]
	b	.L739
.L737:
	sub	w0, w0, #1
	sxth	x0, w0
	b	.L736
.L720:
	.loc 2 1297 0
	ldrh	w2, [x28]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L743
	.loc 2 1299 0
	strh	w26, [x28]
.L785:
	.loc 2 1310 0
	str	w0, [x28, 8]
	b	.L719
.L743:
	.loc 2 1304 0
	ldrh	w0, [x28, 4]
	cmp	w0, w1
	beq	.L744
	.loc 2 1305 0
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.LVL747:
.L744:
	.loc 2 1306 0
	ldr	w0, [x27, 4]
	ldr	w1, [x28, 8]
	cmp	w1, w0
	bcs	.L745
	.loc 2 1308 0
	ldrh	w0, [x28]
	strh	w0, [x28, 4]
	.loc 2 1309 0
	strh	w26, [x28]
	.loc 2 1310 0
	ldr	w0, [x27, 4]
	b	.L785
.L745:
	.loc 2 1314 0
	strh	w26, [x28, 4]
	b	.L719
.LVL748:
.L748:
	.loc 2 1341 0
	ldr	x1, [x19, 4032]
	ldrh	w0, [x1]
	cbz	w0, .L750
.L753:
	.loc 2 1359 0
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0, 128]
	ldrh	w2, [x1]
	cbz	w2, .L751
.L752:
	.loc 2 1377 0
	add	x20, x20, :lo12:.LANCHOR3
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w1, [x20, -184]
	ldr	w0, [x21, 108]
	cmp	w1, w0
	bls	.L782
	.loc 2 1377 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1377
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL749:
.L782:
	.loc 2 1379 0 is_stmt 1
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
.LVL750:
	ldp	x23, x24, [sp, 48]
.LVL751:
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
.LVL752:
	ldp	x29, x30, [sp], 112
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL753:
.L750:
	.cfi_restore_state
	.loc 2 1341 0 discriminator 1
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0, -184]
	cbz	w0, .L753
	.loc 2 1343 0 discriminator 1
	ldr	w2, [x19, 108]
	mov	w0, 0
.L754:
	cmp	w0, w2
	bcs	.L753
	.loc 2 1345 0
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L755
	mov	w1, w0
	.loc 2 1347 0 discriminator 1
	add	x2, x21, :lo12:.LANCHOR0
.L756:
	ldr	w3, [x2, 108]
	cmp	w1, w3
	bcs	.L753
	.loc 2 1349 0 discriminator 3
	ldr	x5, [x2, 4032]
	sxtw	x6, w1
	lsl	x4, x6, 1
	sub	w3, w1, w0
	sxtw	x3, w3
	add	w1, w1, 1
	sxth	w1, w1
	ldrh	w7, [x5, x4]
	strh	w7, [x5, x3, lsl 1]
	.loc 2 1350 0 discriminator 3
	ldr	x5, [x2, 4080]
	ldr	w6, [x5, x6, lsl 2]
	str	w6, [x5, x3, lsl 2]
	.loc 2 1351 0 discriminator 3
	ldr	x3, [x2, 4032]
	strh	wzr, [x3, x4]
	b	.L756
.L755:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L754
.L751:
	.loc 2 1359 0 discriminator 1
	ldrh	w2, [x0, 124]
	cbz	w2, .L752
	.loc 2 1361 0 discriminator 1
	ldrh	w2, [x0, 100]
	mov	w0, 0
.L761:
	mov	w6, w0
	cmp	w0, w2
	bge	.L752
	.loc 2 1363 0
	ldrh	w3, [x1, w0, sxtw 1]
	cbz	w3, .L762
	.loc 2 1365 0 discriminator 1
	add	x1, x21, :lo12:.LANCHOR0
.L763:
	ldrh	w2, [x1, 100]
	cmp	w0, w2
	bge	.L752
	.loc 2 1367 0 discriminator 3
	ldr	x4, [x1, 128]
	sxtw	x5, w0
	lsl	x3, x5, 1
	sub	w2, w0, w6
	sxtw	x2, w2
	add	w0, w0, 1
	sxth	w0, w0
	ldrh	w7, [x4, x3]
	strh	w7, [x4, x2, lsl 1]
	.loc 2 1368 0 discriminator 3
	ldr	x4, [x1, 4056]
	ldr	w5, [x4, x5, lsl 2]
	str	w5, [x4, x2, lsl 2]
	.loc 2 1369 0 discriminator 3
	ldr	x2, [x1, 128]
	strh	wzr, [x2, x3]
	b	.L763
.L762:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L761
	.cfi_endproc
.LFE209:
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
.LFB214:
	.loc 2 1515 0
	.cfi_startproc
.LVL754:
	stp	x29, x30, [sp, -128]!
	.cfi_def_cfa_offset 128
	.cfi_offset 29, -128
	.cfi_offset 30, -120
	.loc 2 1526 0
	mov	w1, 0
	.loc 2 1515 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -112
	.cfi_offset 20, -104
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -96
	.cfi_offset 22, -88
	.loc 2 1527 0
	adrp	x21, .LANCHOR0
	.loc 2 1515 0
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	.cfi_offset 25, -64
	.cfi_offset 26, -56
	.cfi_offset 27, -48
	.cfi_offset 28, -40
	.loc 2 1535 0
	mov	w27, 0
.LVL755:
	.loc 2 1515 0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -80
	.cfi_offset 24, -72
	.loc 2 1522 0
	ldrh	w24, [x0, 6]
.LVL756:
	.loc 2 1519 0
	ldr	x0, [x0, 40]
.LVL757:
	str	x0, [x29, 120]
	.loc 2 1523 0
	ldrh	w0, [x19, 8]
.LVL758:
	.loc 2 1526 0
	lsl	w2, w24, 2
	.loc 2 1523 0
	str	w0, [x29, 116]
	.loc 2 1526 0
	ldr	x0, [x29, 120]
.LVL759:
	.loc 2 1521 0
	ldp	x23, x26, [x19, 16]
.LVL760:
	.loc 2 1526 0
	bl	ftl_memset
.LVL761:
	.loc 2 1532 0
	stp	wzr, wzr, [x19, 48]
	.loc 2 1527 0
	add	x3, x21, :lo12:.LANCHOR0
	add	x20, x3, 3344
	.loc 2 1559 0
	mov	x25, x3
	.loc 2 1527 0
	ldr	x0, [x3, 3376]
	.loc 2 1528 0
	ldr	x22, [x3, 3384]
	stp	x0, x22, [x20, 8]
	.loc 2 1530 0
	mov	w0, -1
	strh	w0, [x19]
	.loc 2 1531 0
	strh	w0, [x19, 2]
	.loc 2 1534 0
	mov	w0, 1
	str	w0, [x19, 56]
	.loc 2 1537 0
	ldr	w0, [x29, 116]
.LVL762:
	sub	w0, w0, #1
.LVL763:
	str	w0, [x29, 112]
.LVL764:
.L787:
	.loc 2 1535 0 discriminator 1
	ldr	w0, [x29, 116]
.LVL765:
	cmp	w27, w0
	bge	.L804
	.loc 2 1537 0
	ldr	w0, [x29, 112]
.LVL766:
	sxtw	x28, w27
.LVL767:
	cmp	w27, w0
	bne	.L788
	.loc 2 1539 0
	lsl	x0, x28, 1
	mov	w1, 1
	add	x20, x23, x0
	.loc 2 1545 0
	add	x25, x21, :lo12:.LANCHOR0
	add	x25, x25, 3344
	.loc 2 1539 0
	ldrh	w0, [x23, x0]
	.loc 2 1543 0
	mov	w23, 0
.LVL768:
	.loc 2 1539 0
	bl	FtlGetLastWrittenPage
.LVL769:
	.loc 2 1541 0
	strh	w27, [x19]
	.loc 2 1539 0
	sxth	w1, w0
.LVL770:
	.loc 2 1540 0
	add	w0, w0, 1
	strh	w0, [x19, 2]
	.loc 2 1542 0
	ldr	w0, [x26, x28, lsl 2]
	.loc 2 1543 0
	add	w26, w1, 1
.LVL771:
	.loc 2 1542 0
	str	w0, [x19, 48]
.LVL772:
.L789:
	.loc 2 1543 0 discriminator 1
	cmp	w23, w26
	blt	.L791
.LVL773:
.L804:
	.loc 2 1597 0
	add	x21, x21, :lo12:.LANCHOR0
	.loc 2 1596 0
	mov	x0, x19
.LVL774:
	bl	ftl_free_no_use_map_blk
.LVL775:
	.loc 2 1597 0
	ldrh	w1, [x19, 2]
	ldrh	w0, [x21, 84]
	cmp	w1, w0
	bne	.L793
	.loc 2 1599 0
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.LVL776:
.L793:
	.loc 2 1602 0
	mov	x0, x19
	bl	ftl_map_blk_gc
.LVL777:
	.loc 2 1604 0
	mov	x0, x19
	bl	ftl_map_blk_gc
.LVL778:
	.loc 2 1606 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
.LVL779:
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL780:
	ldp	x25, x26, [sp, 64]
.LVL781:
	ldp	x27, x28, [sp, 80]
.LVL782:
	ldp	x29, x30, [sp], 128
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL783:
.L791:
	.cfi_restore_state
	.loc 2 1545 0
	ldrh	w0, [x20]
	.loc 2 1546 0
	mov	w2, 1
.LVL784:
	mov	w1, w2
.LVL785:
	.loc 2 1545 0
	orr	w0, w23, w0, lsl 10
	str	w0, [x25, 4]
	.loc 2 1546 0
	mov	x0, x25
	bl	FlashReadPages
.LVL786:
	.loc 2 1549 0
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L790
	.loc 2 1551 0
	ldrh	w0, [x22, 8]
	cmp	w24, w0
	bls	.L790
	.loc 2 1551 0 is_stmt 0 discriminator 1
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L790
	.loc 2 1553 0 is_stmt 1
	ldr	x2, [x29, 120]
.LVL787:
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x25, 4]
	str	w1, [x2, x0]
.L790:
	add	w23, w23, 1
	sxth	w23, w23
	b	.L789
.LVL788:
.L788:
	.loc 2 1559 0
	ldr	x0, [x25, 3376]
	.loc 2 1561 0
	mov	w2, 1
	.loc 2 1559 0
	str	x0, [x20, 8]
	.loc 2 1560 0
	lsl	x0, x28, 1
	ldrh	w1, [x25, 84]
	add	x28, x23, x0
.LVL789:
	ldrh	w0, [x23, x0]
	sub	w1, w1, #1
	orr	w0, w1, w0, lsl 10
	.loc 2 1561 0
	mov	w1, w2
	.loc 2 1560 0
	str	w0, [x20, 4]
	.loc 2 1561 0
	mov	x0, x20
	bl	FlashReadPages
.LVL790:
	.loc 2 1564 0
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L806
	.loc 2 1564 0 is_stmt 0 discriminator 1
	ldrh	w1, [x22]
	ldrh	w0, [x19, 4]
	cmp	w1, w0
	bne	.L806
	.loc 2 1564 0 discriminator 2
	ldrh	w1, [x22, 8]
	mov	w0, 64245
	cmp	w1, w0
	beq	.L795
.L806:
	mov	w5, 0
.LVL791:
.L796:
	.loc 2 1579 0 is_stmt 1 discriminator 1
	ldrh	w0, [x25, 84]
	cmp	w5, w0
	bge	.L802
	.loc 2 1581 0
	ldrh	w0, [x28]
	.loc 2 1582 0
	mov	w2, 1
.LVL792:
	.loc 2 1581 0
	str	w5, [x29, 108]
	.loc 2 1582 0
	mov	w1, w2
	.loc 2 1581 0
	orr	w0, w5, w0, lsl 10
	str	w0, [x20, 4]
	.loc 2 1582 0
	mov	x0, x20
	bl	FlashReadPages
.LVL793:
	.loc 2 1585 0
	ldr	w0, [x20]
	ldr	w5, [x29, 108]
	cmn	w0, #1
	beq	.L800
	.loc 2 1587 0
	ldrh	w0, [x22, 8]
	cmp	w24, w0
	bls	.L800
	.loc 2 1587 0 is_stmt 0 discriminator 1
	ldrh	w2, [x22]
	ldrh	w1, [x19, 4]
	cmp	w2, w1
	bne	.L800
	.loc 2 1589 0 is_stmt 1
	ldr	x2, [x29, 120]
.LVL794:
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x20, 4]
	str	w1, [x2, x0]
.L800:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L796
.LVL795:
.L795:
	mov	w0, 0
.LVL796:
.L797:
	.loc 2 1567 0 discriminator 1
	ldrh	w1, [x25, 84]
.LVL797:
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L799
.L802:
	add	w4, w27, 1
	sxth	w27, w4
	b	.L787
.L799:
	.loc 2 1569 0
	ldr	x2, [x25, 3376]
	sbfiz	x5, x0, 3, 32
	ldrh	w1, [x2, x5]
.LVL798:
	.loc 2 1570 0
	cmp	w24, w1
	bls	.L798
	.loc 2 1572 0
	add	x2, x2, x5
	ldr	x3, [x29, 120]
	ubfiz	x1, x1, 2, 16
.LVL799:
	ldr	w2, [x2, 4]
	str	w2, [x3, x1]
.LVL800:
.L798:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L797
	.cfi_endproc
.LFE214:
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
.LFB215:
	.loc 2 1609 0
	.cfi_startproc
	.loc 2 1610 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	.loc 2 1609 0
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 2 1610 0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	.loc 2 1609 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 2 1610 0
	ldrh	w2, [x1, 100]
	strh	w2, [x0, -166]
	.loc 2 1611 0
	mov	w2, -3962
	strh	w2, [x0, -172]
	.loc 2 1612 0
	ldrh	w2, [x1, 124]
	strh	w2, [x0, -168]
	.loc 2 1613 0
	ldrh	w2, [x1, 102]
	strh	w2, [x0, -170]
	.loc 2 1614 0
	ldr	x2, [x1, 128]
	str	x2, [x0, -160]
	.loc 2 1615 0
	ldr	x2, [x1, 4056]
	str	x2, [x0, -152]
	.loc 2 1616 0
	ldr	x2, [x1, 4048]
	.loc 2 1617 0
	ldr	x1, [x1, 4064]
	stp	x2, x1, [x0, -144]
	.loc 2 1619 0
	sub	x0, x0, #176
	bl	FtlMapTblRecovery
.LVL801:
	.loc 2 1622 0
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE215:
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
.LFB216:
	.loc 2 1625 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 2 1627 0
	adrp	x0, .LANCHOR0
	mov	w1, 0
	.loc 2 1625 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 2 1627 0
	add	x19, x0, :lo12:.LANCHOR0
	ldr	x0, [x19, 4040]
	ldr	w2, [x19, 108]
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL802:
	.loc 2 1628 0
	ldrh	w0, [x19, 118]
	mov	w1, 255
	ldrh	w2, [x19, 90]
	mul	w2, w2, w0
	ldr	x0, [x19, 4088]
	bl	ftl_memset
.LVL803:
	.loc 2 1629 0
	mov	x0, x19
	mov	w1, 0
.LVL804:
	.loc 2 1632 0
	mov	w5, -1
.L817:
	.loc 2 1629 0 discriminator 1
	ldrh	w2, [x0, 118]
	cmp	w2, w1
	bhi	.L818
	.loc 2 1635 0
	adrp	x1, .LANCHOR3
.LVL805:
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, -1
	strh	w2, [x1, -246]
	.loc 2 1636 0
	strh	w2, [x1, -248]
	.loc 2 1637 0
	ldr	w2, [x0, 108]
	strh	w2, [x1, -238]
	.loc 2 1638 0
	mov	w2, -3902
	strh	w2, [x1, -244]
	.loc 2 1639 0
	ldrh	w2, [x1, -184]
	strh	w2, [x1, -240]
	.loc 2 1640 0
	ldrh	w2, [x0, 116]
	strh	w2, [x1, -242]
	.loc 2 1641 0
	ldr	x2, [x0, 4032]
	str	x2, [x1, -232]
	.loc 2 1642 0
	ldr	x2, [x0, 4080]
	str	x2, [x1, -224]
	.loc 2 1643 0
	ldr	x2, [x0, 4040]
	.loc 2 1644 0
	ldr	x0, [x0, 4072]
	stp	x2, x0, [x1, -216]
	.loc 2 1645 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
.LVL806:
.L818:
	.cfi_restore_state
	.loc 2 1631 0 discriminator 3
	ldr	x3, [x0, 2544]
	ubfiz	x2, x1, 4, 16
	add	x3, x3, x2
	str	wzr, [x3, 4]
	.loc 2 1632 0 discriminator 3
	ldr	x3, [x0, 2544]
	strh	w5, [x3, x2]
	.loc 2 1633 0 discriminator 3
	ldr	x3, [x0, 2544]
	ldr	x4, [x0, 4088]
	add	x3, x3, x2
	ldrh	w2, [x0, 90]
	mul	w2, w2, w1
	.loc 2 1629 0 discriminator 3
	add	w1, w1, 1
.LVL807:
	and	w1, w1, 65535
.LVL808:
	.loc 2 1633 0 discriminator 3
	sxtw	x2, w2
	and	x2, x2, -4
	add	x2, x4, x2
	str	x2, [x3, 8]
	b	.L817
	.cfi_endproc
.LFE216:
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlLoadMapInfo
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
.LFB217:
	.loc 2 1648 0
	.cfi_startproc
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 2 1649 0
	bl	FtlL2PDataInit
.LVL809:
	.loc 2 1651 0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	sub	x0, x0, #248
	bl	FtlMapTblRecovery
.LVL810:
	.loc 2 1654 0
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE217:
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
.LFB146:
	.loc 3 493 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 3 496 0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	.loc 3 497 0
	mov	w1, -1
	.loc 3 493 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 3 501 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	.loc 3 497 0
	strh	w1, [x0, -104]
	.loc 3 496 0
	str	xzr, [x0, -112]
	.loc 3 500 0
	mov	w1, -1
	.loc 3 499 0
	stp	wzr, wzr, [x0, -100]
	.loc 3 500 0
	str	w1, [x0, -92]
	.loc 3 502 0
	mov	w1, 0
	ldr	x0, [x19, 128]
	ldrh	w2, [x19, 100]
	.loc 3 501 0
	strh	wzr, [x19, 124]
	.loc 3 502 0
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL811:
	.loc 3 503 0
	ldr	x0, [x19, 2360]
	mov	w1, 0
	ldrh	w2, [x19, 22]
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL812:
	.loc 3 504 0
	ldr	x0, [x19, 4008]
	mov	w1, 0
	ldrh	w2, [x19, 22]
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL813:
	.loc 3 505 0
	mov	w2, 48
	mov	w1, 0
	add	x0, x19, 2288
	bl	ftl_memset
.LVL814:
	.loc 3 506 0
	mov	w2, 512
	mov	w1, 0
	add	x0, x19, 2696
	bl	ftl_memset
.LVL815:
	.loc 3 507 0
	bl	FtlGcBufInit
.LVL816:
	.loc 3 508 0
	bl	FtlL2PDataInit
.LVL817:
	.loc 3 510 0
	ldr	x19, [sp, 16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE146:
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
.LFB224:
	.loc 2 2116 0
	.cfi_startproc
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	.loc 2 2120 0
	mov	w0, 6
	mov	w1, 0
	.loc 2 2116 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	.loc 2 2120 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	.loc 2 2116 0
	str	x23, [sp, 48]
	.cfi_offset 23, -16
	.loc 2 2131 0
	add	x23, x19, 40
	.loc 2 2116 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.loc 2 2128 0
	mov	w20, 0
.LVL818:
	.loc 2 2126 0
	mov	w22, 0
.LVL819:
	.loc 2 2120 0
	ldrh	w2, [x19, 22]
	.loc 2 2126 0
	mov	w21, 0
.LVL820:
	.loc 2 2120 0
	mul	w2, w2, w0
	ldr	x0, [x19, 2336]
	bl	ftl_memset
.LVL821:
	.loc 2 2124 0
	strh	wzr, [x19, 2376]
	.loc 2 2125 0
	strh	wzr, [x19, 2392]
	.loc 2 2122 0
	str	xzr, [x19, 2344]
	.loc 2 2123 0
	str	xzr, [x19, 2368]
	.loc 2 2121 0
	str	xzr, [x19, 2384]
.L825:
	.loc 2 2128 0 discriminator 1
	ldrh	w0, [x19, 20]
	cmp	w20, w0
	bcs	.L831
	.loc 2 2130 0
	ldrh	w8, [x19, 12]
	.loc 2 2133 0
	mov	x6, 0
	ldrh	w7, [x19, 82]
	mov	w5, 0
	b	.L832
.L827:
	.loc 2 2131 0
	ldrb	w0, [x23, x6]
	mov	w1, w20
	bl	V2P_block
.LVL822:
	.loc 2 2132 0
	bl	FtlBbmIsBadBlock
.LVL823:
	cbnz	w0, .L826
	.loc 2 2133 0
	add	w5, w5, w7
	and	w5, w5, 65535
.L826:
	add	x6, x6, 1
.L832:
	.loc 2 2130 0 discriminator 1
	cmp	w8, w6, uxth
	bhi	.L827
	.loc 2 2135 0
	cbz	w5, .L834
	.loc 2 2136 0
	mov	w0, 32768
	sdiv	w5, w0, w5
.L828:
	.loc 2 2137 0
	ldr	x1, [x19, 2336]
	mov	w0, 6
	umaddl	x0, w20, w0, x1
	strh	w5, [x0, 4]
	.loc 2 2139 0
	ldrh	w0, [x19, 2400]
	cmp	w0, w20
	beq	.L829
	.loc 2 2139 0 is_stmt 0 discriminator 1
	ldrh	w0, [x19, 2448]
	cmp	w0, w20
	beq	.L829
	.loc 2 2140 0 is_stmt 1
	ldrh	w0, [x19, 2496]
	cmp	w0, w20
	beq	.L829
	.loc 2 2143 0
	ldr	x1, [x19, 2352]
	ubfiz	x0, x20, 1, 16
	ldrh	w0, [x1, x0]
	cbnz	w0, .L830
	.loc 2 2144 0
	add	w22, w22, 1
.LVL824:
	.loc 2 2145 0
	mov	w0, w20
	.loc 2 2144 0
	and	w22, w22, 65535
.LVL825:
	.loc 2 2145 0
	bl	INSERT_FREE_LIST
.LVL826:
.L829:
	.loc 2 2128 0 discriminator 2
	add	w20, w20, 1
.LVL827:
	and	w20, w20, 65535
.LVL828:
	b	.L825
.L834:
	mov	w5, 0
	b	.L828
.L830:
	.loc 2 2147 0
	add	w21, w21, 1
.LVL829:
	.loc 2 2148 0
	mov	w0, w20
	.loc 2 2147 0
	and	w21, w21, 65535
.LVL830:
	.loc 2 2148 0
	bl	INSERT_DATA_LIST
.LVL831:
	b	.L829
.L831:
	.loc 2 2152 0
	strh	w21, [x19, 2376]
	.loc 2 2154 0
	add	w21, w21, w22
.LVL832:
	.loc 2 2153 0
	strh	w22, [x19, 2392]
	.loc 2 2154 0
	cmp	w21, w0
	ble	.L833
	.loc 2 2154 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2154
	add	x1, x1, 384
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL833:
.L833:
	.loc 2 2156 0 is_stmt 1
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
.LVL834:
	ldp	x21, x22, [sp, 32]
.LVL835:
	ldp	x29, x30, [sp], 64
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE224:
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	ftl_check_vpc
	.type	ftl_check_vpc, %function
ftl_check_vpc:
.LFB229:
	.loc 2 2273 0
	.cfi_startproc
	stp	x29, x30, [sp, -128]!
	.cfi_def_cfa_offset 128
	.cfi_offset 29, -128
	.cfi_offset 30, -120
	.loc 2 2279 0
	adrp	x0, .LC90
	add	x0, x0, :lo12:.LC90
	.loc 2 2273 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -112
	.cfi_offset 20, -104
	.loc 2 2282 0
	adrp	x20, .LANCHOR0
	.loc 2 2273 0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -80
	.cfi_offset 24, -72
	.loc 2 2282 0
	add	x24, x20, :lo12:.LANCHOR0
	.loc 2 2273 0
	stp	x21, x22, [sp, 32]
	.loc 2 2282 0
	mov	w23, 0
.LVL836:
	.cfi_offset 21, -96
	.cfi_offset 22, -88
	.loc 2 2279 0
	adrp	x21, .LANCHOR1
	add	x1, x21, :lo12:.LANCHOR1
	add	x1, x1, 408
	.loc 2 2273 0
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	.cfi_offset 25, -64
	.cfi_offset 26, -56
	.cfi_offset 27, -48
	.cfi_offset 28, -40
	.loc 2 2281 0
	adrp	x22, check_vpc_table
	.loc 2 2279 0
	bl	printk
.LVL837:
	.loc 2 2281 0
	add	x19, x22, :lo12:check_vpc_table
	mov	w2, 8192
	mov	w1, 0
	mov	x0, x19
	bl	ftl_memset
.LVL838:
.L837:
	.loc 2 2282 0 discriminator 1
	ldr	w0, [x24, 2560]
	cmp	w23, w0
	bcc	.L839
	.loc 2 2297 0 discriminator 1
	adrp	x25, .LC91
	.loc 2 2293 0 discriminator 1
	add	x23, x20, :lo12:.LANCHOR0
.LVL839:
	.loc 2 2295 0 discriminator 1
	add	x26, x22, :lo12:check_vpc_table
	.loc 2 2297 0 discriminator 1
	add	x25, x25, :lo12:.LC91
	mov	w24, 0
.LVL840:
	mov	w19, 0
	.loc 2 2298 0 discriminator 1
	mov	w4, 65535
.L840:
	.loc 2 2293 0 discriminator 1
	ldrh	w0, [x23, 20]
	cmp	w0, w19
	bhi	.L842
	.loc 2 2305 0
	ldr	x19, [x23, 2384]
	cbz	x19, .L843
.LBB264:
	.loc 2 2308 0
	ldr	x0, [x23, 2336]
	.loc 2 2313 0
	adrp	x25, .LC92
	.loc 2 2311 0
	add	x20, x20, :lo12:.LANCHOR0
	.loc 2 2313 0
	add	x22, x22, :lo12:check_vpc_table
	.loc 2 2308 0
	sub	x19, x19, x0
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	movk	x0, 0xaaab, lsl 0
	.loc 2 2313 0
	add	x25, x25, :lo12:.LC92
	.loc 2 2307 0
	ldrh	w26, [x23, 2392]
.LVL841:
	.loc 2 2316 0
	mov	w27, 6
	.loc 2 2309 0
	mov	w23, 0
	.loc 2 2308 0
	mul	x19, x19, x0
	and	w19, w19, 65535
.L844:
	.loc 2 2309 0 discriminator 1
	cmp	w23, w26
	bne	.L846
.L843:
.LBE264:
	.loc 2 2321 0
	cbz	w24, .L836
	.loc 2 2321 0 is_stmt 0 discriminator 1
	add	x1, x21, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2321
	add	x1, x1, 408
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL842:
.L836:
	.loc 2 2322 0 is_stmt 1
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL843:
	ldp	x25, x26, [sp, 64]
.LVL844:
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL845:
.L839:
	.cfi_restore_state
	.loc 2 2284 0
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w23
	bl	log2phys
.LVL846:
	.loc 2 2285 0
	ldr	w0, [x29, 124]
	cmn	w0, #1
	beq	.L838
	.loc 2 2287 0
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
.LVL847:
	.loc 2 2288 0
	and	x0, x0, 65535
	ldrh	w1, [x19, x0, lsl 1]
	add	w1, w1, 1
	strh	w1, [x19, x0, lsl 1]
.L838:
	.loc 2 2282 0 discriminator 2
	add	w23, w23, 1
	b	.L837
.LVL848:
.L842:
	.loc 2 2295 0
	ldr	x0, [x23, 2352]
	ubfiz	x28, x19, 1, 16
	sxtw	x27, w19
	ldrh	w2, [x0, x28]
	ldrh	w3, [x26, x27, lsl 1]
	cmp	w2, w3
	beq	.L841
	str	w4, [x29, 108]
	.loc 2 2297 0
	mov	w1, w19
	mov	x0, x25
	bl	printk
.LVL849:
	.loc 2 2298 0
	ldr	x0, [x23, 2352]
	ldr	w4, [x29, 108]
	ldrh	w0, [x0, x28]
	cmp	w0, w4
	beq	.L841
	.loc 2 2298 0 is_stmt 0 discriminator 1
	ldrh	w1, [x26, x27, lsl 1]
	.loc 2 2300 0 is_stmt 1 discriminator 1
	cmp	w1, w0
	csinc	w24, w24, wzr, ls
.L841:
	.loc 2 2293 0 discriminator 2
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L840
.LVL850:
.L846:
.LBB265:
	.loc 2 2311 0
	ldr	x1, [x20, 2352]
	ubfiz	x0, x19, 1, 16
	ldrh	w2, [x1, x0]
	cbz	w2, .L845
	.loc 2 2313 0
	ldrh	w3, [x22, w19, sxtw 1]
	.loc 2 2314 0
	mov	w24, 1
	.loc 2 2313 0
	mov	w1, w19
	mov	x0, x25
	bl	printk
.LVL851:
.L845:
	.loc 2 2316 0
	ldr	x0, [x20, 2336]
	umull	x19, w19, w27
	ldrh	w19, [x0, x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L843
	.loc 2 2309 0 discriminator 2
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L844
.LBE265:
	.cfi_endproc
.LFE229:
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
.LFB248:
	.loc 5 202 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 5 205 0
	mov	w1, 255
	.loc 5 202 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 5 203 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	.loc 5 205 0
	ldr	x0, [x19, 3272]
	ldrh	w2, [x19, 86]
	.loc 5 203 0
	strh	wzr, [x19, 3268]
	.loc 5 204 0
	strh	wzr, [x19, 3280]
	.loc 5 205 0
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL852:
	.loc 5 206 0
	ldrh	w2, [x19, 86]
	mov	w0, 12
	mov	w1, 255
	mul	w2, w2, w0
	ldr	x0, [x19, 3288]
	bl	ftl_memset
.LVL853:
	.loc 5 207 0
	bl	FtlGcBufInit
.LVL854:
	.loc 5 208 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE248:
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
.LFB249:
	.loc 5 211 0
	.cfi_startproc
.LVL855:
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	mov	x21, x0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	.loc 5 220 0
	adrp	x23, .LANCHOR2
	add	x0, x23, :lo12:.LANCHOR2
.LVL856:
	.loc 5 211 0
	stp	x25, x26, [sp, 64]
	stp	x19, x20, [sp, 16]
	.cfi_offset 25, -16
	.cfi_offset 26, -8
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.loc 5 211 0
	mov	w25, w1
	.loc 5 220 0
	ldrh	w19, [x0, 4]
.LVL857:
	.loc 5 221 0
	mov	w0, 65535
	cmp	w19, w0
	beq	.L880
	.loc 5 223 0
	cbnz	w19, .L864
.L865:
	.loc 5 224 0
	bl	FtlGcPageVarInit
.LVL858:
	b	.L866
.LVL859:
.L880:
	.loc 5 222 0
	mov	w19, 0
.L864:
	.loc 5 223 0 discriminator 1
	adrp	x0, .LANCHOR0+82
	ldrh	w0, [x0, #:lo12:.LANCHOR0+82]
	cmp	w0, w25
	beq	.L865
.L866:
	.loc 5 235 0
	adrp	x26, .LANCHOR0
	.loc 5 234 0
	add	x22, x26, :lo12:.LANCHOR0
	.loc 5 222 0
	mov	w24, 0
.LVL860:
.L874:
	.loc 5 228 0
	ldrh	w1, [x21]
	mov	w0, 65535
	.loc 5 226 0
	strb	wzr, [x21, 8]
	.loc 5 228 0
	cmp	w1, w0
	beq	.L881
.L877:
	.loc 5 235 0
	add	x0, x26, :lo12:.LANCHOR0
	add	x2, x21, 16
	.loc 5 230 0
	mov	w20, 0
.LVL861:
	.loc 5 233 0
	mov	w9, 65535
	.loc 5 235 0
	mov	w5, 4
	ldrh	w7, [x0, 90]
	.loc 5 236 0
	ldrh	w8, [x0, 92]
	ldrh	w0, [x0, 12]
	add	x0, x0, 8
	add	x0, x21, x0, lsl 1
.LVL862:
.L868:
	.loc 5 231 0 discriminator 1
	cmp	x0, x2
	bne	.L870
	.loc 5 240 0
	ldr	x0, [x22, 3960]
	mov	w1, w20
.LVL863:
	mov	w2, 0
	ubfiz	x20, x20, 5, 16
.LVL864:
	bl	FlashReadPages
.LVL865:
	.loc 5 241 0
	mov	x10, 0
	.loc 5 248 0
	mov	w11, 65535
.L871:
	.loc 5 241 0 discriminator 1
	cmp	x10, x20
	bne	.L875
	.loc 5 264 0
	add	w19, w19, 1
.LVL866:
	.loc 5 266 0
	add	w24, w24, 1
	.loc 5 264 0
	and	w19, w19, 65535
.LVL867:
	.loc 5 268 0
	cmp	w25, w24
	bls	.L876
.L878:
	.loc 5 274 0
	ldrh	w0, [x22, 82]
	cmp	w0, w19
	bhi	.L877
.L881:
	.loc 5 227 0
	mov	w2, 0
.LVL868:
	b	.L867
.LVL869:
.L870:
	.loc 5 232 0
	ldrh	w1, [x2]
	.loc 5 233 0
	cmp	w1, w9
	beq	.L869
	.loc 5 234 0
	ldr	x4, [x22, 3960]
	ubfiz	x6, x20, 5, 16
	orr	w1, w19, w1, lsl 10
.LVL870:
	add	x4, x4, x6
	str	w1, [x4, 4]
	.loc 5 235 0
	mul	w1, w20, w7
	ldr	x3, [x22, 3960]
	ldr	x4, [x22, 3240]
	sdiv	w1, w1, w5
	add	x3, x3, x6
	add	x1, x4, x1, sxtw 2
	str	x1, [x3, 8]
	.loc 5 236 0
	mul	w1, w20, w8
	ldr	x4, [x22, 3248]
	.loc 5 237 0
	add	w20, w20, 1
.LVL871:
	and	w20, w20, 65535
.LVL872:
	.loc 5 236 0
	sdiv	w1, w1, w5
	add	x1, x4, x1, sxtw 2
	str	x1, [x3, 16]
.LVL873:
.L869:
	add	x2, x2, 2
	b	.L868
.LVL874:
.L875:
	.loc 5 242 0
	ldr	x6, [x22, 3960]
	add	x5, x6, x10
	ldr	w4, [x5, 4]
.LVL875:
	.loc 5 243 0
	lsr	w0, w4, 10
	bl	P2V_plane
.LVL876:
	and	w2, w0, 65535
.LVL877:
	.loc 5 245 0
	ldr	w0, [x6, x10]
	cbnz	w0, .L872
	.loc 5 247 0
	ldr	x0, [x5, 16]
.LVL878:
	add	x10, x10, 32
	.loc 5 248 0
	ldrh	w1, [x0]
	cmp	w1, w11
	bne	.L873
	.loc 5 249 0
	adrp	x0, .LANCHOR3-96
.LVL879:
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR3-96]
.L867:
	.loc 5 281 0
	add	x23, x23, :lo12:.LANCHOR2
	.loc 5 282 0
	strh	w19, [x21, 2]
	.loc 5 283 0
	strb	w2, [x21, 6]
	.loc 5 281 0
	mov	w0, -1
	.loc 5 284 0
	mov	w1, w19
	.loc 5 281 0
	strh	w0, [x23, 4]
	.loc 5 284 0
	mov	x0, x21
	bl	ftl_sb_update_avl_pages
.LVL880:
	.loc 5 285 0
	b	.L879
.LVL881:
.L873:
	.loc 5 252 0
	ldp	w2, w0, [x0, 8]
.LVL882:
	mov	w1, w4
	bl	FtlGcUpdatePage
.LVL883:
	b	.L871
.LVL884:
.L872:
	.loc 5 256 0
	ldr	x0, [x22, 2352]
	.loc 5 259 0
	mov	w19, 0
	.loc 5 256 0
	ldrh	w1, [x21]
	strh	wzr, [x0, x1, lsl 1]
	.loc 5 257 0
	ldrh	w0, [x21]
	bl	INSERT_FREE_LIST
.LVL885:
	.loc 5 258 0
	mov	w0, -1
	strh	w0, [x21]
	.loc 5 260 0
	bl	FtlGcPageVarInit
.LVL886:
	.loc 5 261 0
	b	.L874
.L876:
	.loc 5 268 0 discriminator 1
	add	x1, x23, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x1, 4]
	cmp	w0, w2
	beq	.L878
	.loc 5 269 0
	add	w0, w0, w24
	strh	w0, [x1, 4]
	.loc 5 270 0
	ldrh	w0, [x22, 82]
	cmp	w0, w19
	bls	.L878
.L879:
	.loc 5 286 0
	mov	w0, -1
	ldp	x19, x20, [sp, 16]
.LVL887:
	ldp	x21, x22, [sp, 32]
.LVL888:
	ldp	x23, x24, [sp, 48]
.LVL889:
	ldp	x25, x26, [sp, 64]
.LVL890:
	ldp	x29, x30, [sp], 80
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE249:
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
.LFB264:
	.loc 1 156 0
	.cfi_startproc
.LVL891:
	stp	x29, x30, [sp, -128]!
	.cfi_def_cfa_offset 128
	.cfi_offset 29, -128
	.cfi_offset 30, -120
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -112
	.cfi_offset 20, -104
	.loc 1 156 0
	and	w19, w0, 65535
.LVL892:
	.loc 1 161 0
	cmp	w19, 11
	bls	.L889
.LBB268:
.LBB269:
	.loc 1 166 0
	add	x0, x29, 64
	.loc 1 167 0
	mov	w2, 32
	mov	w1, 165
	.loc 1 165 0
	adrp	x20, ftl_temp_buf
	add	x20, x20, :lo12:ftl_temp_buf
	.loc 1 166 0
	stp	x20, x0, [x29, 40]
	.loc 1 167 0
	bl	ftl_memset
.LVL893:
	.loc 1 169 0
	lsl	w19, w19, 10
.LVL894:
	.loc 1 168 0
	mov	w2, 8
	mov	w1, 90
	mov	x0, x20
	bl	ftl_memset
.LVL895:
	.loc 1 170 0
	mov	w2, 1
	.loc 1 169 0
	str	w19, [x29, 36]
	.loc 1 170 0
	mov	w1, w2
	add	x0, x29, 32
	bl	FlashEraseBlocks
.LVL896:
	.loc 1 171 0
	mov	w3, 1
	add	x0, x29, 32
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
.LVL897:
	.loc 1 172 0
	ldr	w0, [x29, 32]
	.loc 1 178 0
	mov	w2, 1
	mov	w1, 0
	.loc 1 172 0
	cmp	w0, 0
	.loc 1 178 0
	add	x0, x29, 32
	.loc 1 172 0
	csetm	w19, ne
	.loc 1 178 0
	bl	FlashEraseBlocks
.LVL898:
.L887:
.LBE269:
.LBE268:
	.loc 1 180 0
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 128
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL899:
.L889:
	.cfi_restore_state
	.loc 1 162 0
	mov	w19, 0
.LVL900:
	b	.L887
	.cfi_endproc
.LFE264:
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
.LFB265:
	.loc 1 183 0
	.cfi_startproc
.LVL901:
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 1 186 0
	mov	w2, 256
	.loc 1 183 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 1 183 0
	mov	x19, x0
	mov	w20, w1
	.loc 1 186 0
	mov	w1, 255
.LVL902:
	bl	ftl_memset
.LVL903:
	.loc 1 187 0
	adrp	x0, g_nand_ops
	mov	w1, w20
	ldr	x2, [x0, #:lo12:g_nand_ops]
	mov	x0, x19
	blr	x2
.LVL904:
	and	w0, w0, 65535
.LVL905:
	.loc 1 188 0
	cmp	w0, 50
	bls	.L892
	.loc 1 189 0
	mov	w2, 256
	mov	w1, 255
	mov	x0, x19
.LVL906:
	bl	ftl_memset
.LVL907:
	.loc 1 190 0
	mov	w0, 0
.LVL908:
.L892:
	.loc 1 192 0
	adrp	x1, g_nand_phy_info+14
	ldrh	w1, [x1, #:lo12:g_nand_phy_info+14]
	cmp	w1, 4
	bne	.L896
	mov	x1, 0
.L894:
	.loc 1 193 0 discriminator 1
	cmp	w0, w1, uxth
	bhi	.L895
.L896:
	.loc 1 198 0
	ldp	x19, x20, [sp, 16]
.LVL909:
.LVL910:
	ldp	x29, x30, [sp], 32
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL911:
.L895:
	.cfi_restore_state
	.loc 1 194 0 discriminator 3
	ldrh	w2, [x19, x1, lsl 1]
	lsr	w2, w2, 1
	strh	w2, [x19, x1, lsl 1]
	add	x1, x1, 1
	b	.L894
	.cfi_endproc
.LFE265:
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	ftl_memcpy
	.type	ftl_memcpy, %function
ftl_memcpy:
.LFB267:
	.loc 1 206 0
	.cfi_startproc
.LVL912:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 1 207 0
	uxtw	x2, w2
.LVL913:
	.loc 1 206 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 1 207 0
	bl	memcpy
.LVL914:
	.loc 1 208 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE267:
	.size	ftl_memcpy, .-ftl_memcpy
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
.LFB165:
	.loc 4 80 0
	.cfi_startproc
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	.loc 4 89 0
	mov	w1, 0
	.loc 4 80 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	str	x25, [sp, 64]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.cfi_offset 25, -16
	.loc 4 87 0
	adrp	x25, .LANCHOR0
	add	x19, x25, :lo12:.LANCHOR0
	.loc 4 80 0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	.loc 4 92 0
	adrp	x22, .LANCHOR3
	.loc 4 91 0
	mov	w21, 0
.LVL915:
	.loc 4 92 0
	add	x22, x22, :lo12:.LANCHOR3
	.loc 4 88 0
	ldr	x2, [x19, 3384]
	add	x23, x19, 168
	.loc 4 87 0
	ldr	x0, [x19, 3376]
	.loc 4 88 0
	str	x2, [x19, 3360]
	.loc 4 89 0
	ldrh	w2, [x19, 90]
	.loc 4 87 0
	str	x0, [x19, 3352]
	.loc 4 89 0
	bl	ftl_memset
.LVL916:
.L901:
	.loc 4 91 0 discriminator 1
	ldrh	w0, [x19, 34]
	add	x20, x19, 3344
	cmp	w21, w0
	blt	.L902
	.loc 4 94 0
	ldr	x24, [x20, 16]
.LVL917:
	.loc 4 95 0
	mov	w1, 255
	mov	w2, 16
	.loc 4 108 0
	adrp	x22, .LC93
	.loc 4 95 0
	mov	x0, x24
	bl	ftl_memset
.LVL918:
	.loc 4 97 0
	mov	w0, -3887
	.loc 4 98 0
	add	x1, x19, 136
	.loc 4 97 0
	strh	w0, [x24]
	.loc 4 104 0
	add	x25, x25, :lo12:.LANCHOR0
	.loc 4 108 0
	add	x22, x22, :lo12:.LC93
	.loc 4 84 0
	mov	w21, 0
.LVL919:
	.loc 4 98 0
	ldr	w0, [x19, 144]
	.loc 4 82 0
	mov	w23, 0
.LVL920:
	.loc 4 98 0
	str	w0, [x24, 4]
	.loc 4 99 0
	ldrh	w0, [x19, 136]
	strh	w0, [x24, 2]
	.loc 4 100 0
	ldrh	w0, [x19, 140]
	strh	w0, [x24, 8]
	.loc 4 101 0
	ldrh	w0, [x19, 142]
	strh	w0, [x24, 10]
	.loc 4 102 0
	ldr	w0, [x19, 8]
	.loc 4 107 0
	mov	x19, x1
	.loc 4 102 0
	strh	w0, [x24, 12]
.L903:
	.loc 4 104 0
	ldr	x0, [x25, 3376]
	str	x0, [x20, 8]
	.loc 4 105 0
	ldr	x0, [x25, 3384]
	str	x0, [x20, 16]
	.loc 4 107 0
	ldrh	w1, [x19]
	ldrh	w2, [x19, 2]
	.loc 4 108 0
	ldrh	w3, [x19, 4]
	ldrh	w4, [x24, 10]
	.loc 4 107 0
	orr	w0, w2, w1, lsl 10
	.loc 4 106 0
	str	wzr, [x20]
	.loc 4 107 0
	str	w0, [x20, 4]
	.loc 4 108 0
	mov	x0, x22
	bl	printk
.LVL921:
	.loc 4 109 0
	mov	w3, 1
	mov	x0, x20
	mov	w1, w3
	mov	w2, w3
	bl	FlashProgPages
.LVL922:
	.loc 4 110 0
	ldrh	w0, [x25, 84]
	ldrh	w1, [x19, 2]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L904
	.loc 4 113 0
	ldr	w0, [x19, 8]
	.loc 4 122 0
	mov	w2, 1
	.loc 4 115 0
	ldrh	w1, [x19]
	.loc 4 113 0
	add	w0, w0, 1
	str	w0, [x19, 8]
	.loc 4 114 0
	str	w0, [x24, 4]
	.loc 4 115 0
	strh	w1, [x24, 8]
	.loc 4 116 0
	ldrh	w0, [x19, 4]
	.loc 4 118 0
	strh	w1, [x19, 4]
	.loc 4 121 0
	ldr	x1, [x25, 3904]
	.loc 4 116 0
	strh	w0, [x19]
	.loc 4 119 0
	lsl	w0, w0, 10
	str	w0, [x20, 4]
	.loc 4 117 0
	strh	wzr, [x19, 2]
	.loc 4 121 0
	str	w0, [x1, 4]
	.loc 4 122 0
	mov	w1, w2
	ldr	x0, [x25, 3904]
	bl	FlashEraseBlocks
.LVL923:
	.loc 4 123 0
	mov	w3, 1
	mov	x0, x20
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
.LVL924:
.L904:
	.loc 4 126 0
	ldrh	w0, [x19, 2]
	add	w0, w0, 1
	strh	w0, [x19, 2]
	.loc 4 127 0
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L905
	.loc 4 129 0
	ldr	w1, [x20, 4]
	.loc 4 130 0
	add	w21, w21, 1
.LVL925:
	.loc 4 129 0
	adrp	x0, .LC94
	.loc 4 130 0
	and	w21, w21, 65535
.LVL926:
	.loc 4 129 0
	add	x0, x0, :lo12:.LC94
	bl	printk
.LVL927:
	.loc 4 131 0
	cmp	w21, 3
	bls	.L903
	.loc 4 132 0
	ldr	w1, [x20, 4]
	adrp	x0, .LC95
	mov	w2, w21
	add	x0, x0, :lo12:.LC95
	bl	printk
.LVL928:
.L907:
	b	.L907
.LVL929:
.L902:
	.loc 4 92 0 discriminator 3
	ldrh	w2, [x22, -256]
	ldr	x1, [x23], 8
	ldr	x0, [x20, 8]
	mul	w3, w21, w2
	lsl	w2, w2, 2
	.loc 4 91 0 discriminator 3
	add	w21, w21, 1
	.loc 4 92 0 discriminator 3
	add	x0, x0, x3, sxtw 2
	bl	ftl_memcpy
.LVL930:
	b	.L901
.LVL931:
.L908:
	mov	w23, 1
	b	.L903
.L905:
	.loc 4 139 0
	cbz	w23, .L908
	.loc 4 146 0
	mov	w0, 0
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL932:
	ldp	x23, x24, [sp, 48]
.LVL933:
	ldp	x29, x30, [sp], 80
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE165:
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
.LFB237:
	.loc 2 2579 0
	.cfi_startproc
.LVL934:
	stp	x29, x30, [sp, -112]!
	.cfi_def_cfa_offset 112
	.cfi_offset 29, -112
	.cfi_offset 30, -104
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -80
	.cfi_offset 22, -72
	.loc 2 2585 0
	adrp	x21, .LANCHOR0
	add	x22, x21, :lo12:.LANCHOR0
	.loc 2 2579 0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	.cfi_offset 19, -96
	.cfi_offset 20, -88
	.cfi_offset 23, -64
	.cfi_offset 24, -56
	mov	x20, x0
	.loc 2 2587 0
	mov	x19, x22
	add	x23, x22, 2496
	.loc 2 2579 0
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	.cfi_offset 25, -48
	.cfi_offset 26, -40
	.cfi_offset 27, -32
	.cfi_offset 28, -24
.LVL935:
.L911:
	.loc 2 2585 0
	ldrh	w1, [x22, 2376]
	ldrh	w0, [x22, 2392]
	add	w0, w0, w1
	ldrh	w1, [x22, 20]
	cmp	w0, w1
	ble	.L912
	.loc 2 2585 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2585
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL936:
.L912:
	.loc 2 2587 0 is_stmt 1
	cmp	x20, x23
	bne	.L937
	.loc 2 2588 0
	ldrh	w0, [x19, 2392]
	ldr	w1, [x19, 3220]
	mul	w1, w0, w1
	lsr	w0, w0, 1
	add	w0, w0, 1
	add	w1, w0, w1, lsr 2
	.loc 2 2592 0
	ands	w1, w1, 65535
.LVL937:
	beq	.L913
	.loc 2 2593 0
	sub	w1, w1, #1
.LVL938:
	and	w1, w1, 65535
.LVL939:
.L913:
	.loc 2 2594 0
	add	x0, x19, 2384
	bl	List_pop_index_node
.LVL940:
	and	w24, w0, 65535
	ldrh	w0, [x19, 2392]
	cbnz	w0, .L914
	.loc 2 2594 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2594
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL941:
.L914:
	.loc 2 2594 0 discriminator 3
	ldrh	w0, [x19, 2392]
	sub	w0, w0, #1
	strh	w0, [x19, 2392]
	.loc 2 2597 0 is_stmt 1 discriminator 3
	strh	w24, [x20]
	.loc 2 2598 0 discriminator 3
	mov	x0, x20
	bl	make_superblock
.LVL942:
	.loc 2 2599 0 discriminator 3
	ldrb	w0, [x20, 7]
	cbnz	w0, .L915
	.loc 2 2603 0
	ldr	x1, [x19, 2352]
	ubfiz	x0, x24, 1, 16
	mov	w2, -1
	strh	w2, [x1, x0]
	.loc 2 2604 0
	mov	w0, w24
	bl	INSERT_DATA_LIST
.LVL943:
	.loc 2 2605 0
	ldrh	w1, [x19, 2376]
	ldrh	w0, [x19, 2392]
	add	w0, w0, w1
	ldrh	w1, [x19, 20]
	cmp	w0, w1
	ble	.L911
	.loc 2 2605 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2605
	add	x1, x1, 424
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL944:
	b	.L911
.L937:
	mov	w1, 0
.LVL945:
	b	.L913
.LVL946:
.L915:
	.loc 2 2608 0 is_stmt 1
	ldrh	w1, [x19, 2376]
	ldrh	w0, [x19, 2392]
	add	w0, w0, w1
	ldrh	w1, [x19, 20]
	cmp	w0, w1
	ble	.L917
	.loc 2 2608 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2608
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL947:
.L917:
	add	x27, x20, 16
	.loc 2 2611 0 is_stmt 1 discriminator 1
	ldrh	w4, [x19, 12]
	mov	x3, x27
	mov	x0, 0
	mov	w25, 0
	.loc 2 2614 0 discriminator 1
	mov	w5, 65535
.L918:
	.loc 2 2611 0 discriminator 1
	cmp	w4, w0, uxth
	bhi	.L920
	.loc 2 2619 0
	cbnz	w25, .L921
	.loc 2 2619 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2619
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL948:
.L921:
	.loc 2 2621 0 is_stmt 1
	ldrh	w0, [x19, 2648]
	cmp	w0, w24
	bne	.L922
	.loc 2 2621 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2621
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL949:
.L922:
	.loc 2 2622 0 is_stmt 1
	ldrb	w0, [x20, 8]
	uxtw	x26, w24
.LVL950:
	cbnz	w0, .L923
	.loc 2 2623 0
	ldr	x2, [x19, 2360]
	lsl	x1, x26, 1
	ldrh	w0, [x2, x1]
	cbz	w0, .L924
	.loc 2 2624 0
	ldrh	w3, [x19, 72]
	add	w0, w0, w3
.L945:
	.loc 2 2626 0
	strh	w0, [x2, x1]
	.loc 2 2628 0
	mov	w1, 0
	.loc 2 2627 0
	ldr	w0, [x19, 2604]
	add	w0, w0, 1
	str	w0, [x19, 2604]
.LVL951:
	.loc 2 2628 0
	mov	w0, w24
	bl	ftl_set_blk_mode
.LVL952:
.L926:
	.loc 2 2635 0
	ldr	x0, [x19, 2360]
.LVL953:
	lsl	x26, x26, 1
.LVL954:
	ldr	w1, [x19, 2620]
	ldrh	w0, [x0, x26]
	cmp	w0, w1
	bls	.L927
	.loc 2 2636 0
	str	w0, [x19, 2620]
.L927:
	.loc 2 2638 0
	ldr	w1, [x19, 2608]
	ubfiz	x3, x25, 5, 16
	ldrh	w0, [x19, 72]
	ldr	w2, [x19, 2604]
	madd	w0, w0, w2, w1
	ldrh	w1, [x19, 20]
	udiv	w0, w0, w1
	.loc 2 2639 0
	ldr	x1, [x19, 4016]
	.loc 2 2638 0
	str	w0, [x19, 2612]
	.loc 2 2639 0
	ldr	w0, [x1, 16]
	add	w0, w0, 1
	str	w0, [x1, 16]
	.loc 2 2641 0
	mov	x0, 0
.L928:
	.loc 2 2641 0 is_stmt 0 discriminator 1
	cmp	x3, x0
	bne	.L929
	.loc 2 2643 0 is_stmt 1
	ldrb	w1, [x20, 8]
	mov	w2, w25
	ldr	x0, [x19, 3904]
	.loc 2 2645 0
	mov	x28, 0
	.loc 2 2643 0
	bl	FlashEraseBlocks
.LVL955:
	.loc 2 2644 0
	mov	w1, 0
.LVL956:
.L930:
	.loc 2 2645 0 discriminator 1
	cmp	w25, w28, uxth
	bhi	.L932
	.loc 2 2656 0
	cmp	w1, 0
	ble	.L933
	.loc 2 2657 0
	mov	w0, w24
	bl	update_multiplier_value
.LVL957:
	.loc 2 2658 0
	bl	FtlBbmTblFlush
.LVL958:
.L933:
	.loc 2 2661 0
	ldrb	w0, [x20, 7]
	cbnz	w0, .L934
	.loc 2 2663 0
	ldr	x0, [x19, 2352]
	mov	w1, -1
.LVL959:
	strh	w1, [x0, x26]
	.loc 2 2664 0
	mov	w0, w24
	bl	INSERT_DATA_LIST
.LVL960:
	.loc 2 2665 0
	b	.L911
.L920:
	.loc 2 2612 0
	ldr	x1, [x19, 3904]
	add	x2, x1, x0, lsl 5
	.loc 2 2613 0
	stp	xzr, xzr, [x2, 8]
	.loc 2 2614 0
	ldrh	w2, [x3]
	cmp	w2, w5
	beq	.L919
	.loc 2 2615 0
	ubfiz	x6, x25, 5, 16
	.loc 2 2616 0
	add	w25, w25, 1
.LVL961:
	.loc 2 2615 0
	add	x1, x1, x6
	.loc 2 2616 0
	and	w25, w25, 65535
.LVL962:
	.loc 2 2615 0
	lsl	w2, w2, 10
	str	w2, [x1, 4]
.L919:
	add	x0, x0, 1
	add	x3, x3, 2
	b	.L918
.LVL963:
.L924:
	.loc 2 2626 0
	mov	w0, 2
	b	.L945
.L923:
	.loc 2 2630 0
	ldr	x2, [x19, 2360]
	lsl	x0, x26, 1
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
	.loc 2 2631 0
	ldr	w0, [x19, 2608]
	add	w0, w0, 1
	str	w0, [x19, 2608]
.LBB270:
.LBB271:
	mov	w0, w24
	bl	ftl_set_blk_mode.part.7
.LVL964:
	b	.L926
.LVL965:
.L929:
.LBE271:
.LBE270:
	.loc 2 2642 0 discriminator 3
	ldr	x1, [x19, 3904]
	add	x1, x1, x0
	add	x0, x0, 32
	ldr	w2, [x1, 4]
	and	w2, w2, -1024
	str	w2, [x1, 4]
	b	.L928
.LVL966:
.L932:
	.loc 2 2646 0
	ldr	x2, [x19, 3904]
	lsl	x0, x28, 5
	add	x3, x2, x0
	ldr	w2, [x2, x0]
	cmn	w2, #1
	bne	.L931
	.loc 2 2647 0
	add	w1, w1, 1
	.loc 2 2648 0
	ldr	w0, [x3, 4]
	.loc 2 2647 0
	stp	w2, w1, [x29, 104]
	.loc 2 2649 0
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.LVL967:
	.loc 2 2651 0
	ldp	w2, w1, [x29, 104]
	.loc 2 2650 0
	strh	w2, [x27]
	.loc 2 2651 0
	ldrb	w0, [x20, 7]
	sub	w0, w0, #1
	strb	w0, [x20, 7]
.LVL968:
.L931:
	add	x28, x28, 1
	add	x27, x27, 2
	b	.L930
.L934:
	.loc 2 2668 0
	add	x21, x21, :lo12:.LANCHOR0
	.loc 2 2670 0
	strh	wzr, [x20, 2]
	.loc 2 2671 0
	strb	wzr, [x20, 6]
	.loc 2 2668 0
	ldrh	w1, [x21, 82]
.LVL969:
	.loc 2 2669 0
	strh	w24, [x20]
	.loc 2 2668 0
	mul	w0, w0, w1
	.loc 2 2672 0
	ldr	w1, [x21, 2596]
	str	w1, [x20, 12]
	.loc 2 2668 0
	and	w0, w0, 65535
	.loc 2 2672 0
	add	w1, w1, 1
	.loc 2 2668 0
	strh	w0, [x20, 4]
	.loc 2 2672 0
	str	w1, [x21, 2596]
	.loc 2 2673 0
	ldr	x1, [x21, 2352]
	ldrh	w2, [x20]
	strh	w0, [x1, x2, lsl 1]
	.loc 2 2674 0
	ldrh	w0, [x20, 4]
	cbz	w0, .L935
	.loc 2 2674 0 is_stmt 0 discriminator 2
	ldrb	w0, [x20, 7]
	cbnz	w0, .L936
.L935:
	.loc 2 2674 0 discriminator 3
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2674
	add	x1, x1, 424
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL970:
.L936:
	.loc 2 2676 0 is_stmt 1
	ldp	x19, x20, [sp, 16]
.LVL971:
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL972:
	ldp	x25, x26, [sp, 64]
.LVL973:
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE237:
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
.LFB256:
	.loc 5 390 0
	.cfi_startproc
.LVL974:
	stp	x29, x30, [sp, -96]!
	.cfi_def_cfa_offset 96
	.cfi_offset 29, -96
	.cfi_offset 30, -88
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -80
	.cfi_offset 20, -72
	.loc 5 395 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	.loc 5 390 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -64
	.cfi_offset 22, -56
	and	w22, w0, 65535
.LVL975:
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	.loc 5 395 0
	ldrh	w0, [x19, 3300]
	.loc 5 390 0
	str	x27, [sp, 80]
	.cfi_offset 23, -48
	.cfi_offset 24, -40
	.cfi_offset 25, -32
	.cfi_offset 26, -24
	.cfi_offset 27, -16
	.loc 5 395 0
	cbz	w0, .L947
.LBB274:
.LBB275:
	.loc 5 397 0
	add	x23, x19, 40
	.loc 5 399 0
	add	x24, x19, 3304
.LBE275:
.LBE274:
	mov	w21, 0
.LVL976:
.L948:
.LBB278:
.LBB276:
	.loc 5 396 0
	ldrh	w0, [x19, 12]
	cmp	w0, w21
	bhi	.L954
	.loc 5 412 0
	bl	FtlGcReFreshBadBlk
.LVL977:
.L947:
.LBE276:
.LBE278:
	.loc 5 415 0
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
.LVL978:
	ldp	x21, x22, [sp, 32]
.LVL979:
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
.LVL980:
	ldp	x29, x30, [sp], 96
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL981:
.L954:
	.cfi_restore_state
.LBB279:
.LBB277:
	.loc 5 397 0
	ldrb	w0, [x23, w21, sxtw]
	mov	w1, w22
	.loc 5 401 0
	adrp	x26, .LC96
	add	x26, x26, :lo12:.LC96
	.loc 5 398 0
	mov	w20, 0
	.loc 5 397 0
	bl	V2P_block
.LVL982:
	and	w25, w0, 65535
.L949:
	.loc 5 398 0
	ldrh	w0, [x19, 3300]
	cmp	w0, w20
	bhi	.L953
	.loc 5 396 0
	add	w21, w21, 1
.LVL983:
	and	w21, w21, 65535
.LVL984:
	b	.L948
.L953:
	.loc 5 399 0
	ldrh	w0, [x24, w20, sxtw 1]
	add	w27, w20, 1
	cmp	w0, w25
	bne	.L950
	.loc 5 401 0
	mov	w1, w25
	mov	x0, x26
	bl	printk
.LVL985:
	.loc 5 403 0
	mov	w0, w25
	bl	FtlBbmMapBadBlock
.LVL986:
	.loc 5 404 0
	bl	FtlBbmTblFlush
.LVL987:
	.loc 5 405 0
	ldrh	w0, [x19, 3300]
	mov	w1, w27
.LVL988:
.L951:
	cmp	w20, w0
	bcc	.L952
	.loc 5 407 0
	sub	w0, w0, #1
	strh	w0, [x19, 3300]
.L950:
	.loc 5 398 0
	and	w20, w27, 65535
.LVL989:
	b	.L949
.LVL990:
.L952:
	.loc 5 406 0
	sub	w2, w1, #1
	ldrh	w3, [x24, w1, sxtw 1]
	.loc 5 405 0
	add	w20, w20, 1
.LVL991:
	add	w1, w1, 1
	and	w20, w20, 65535
.LVL992:
	.loc 5 406 0
	strh	w3, [x24, w2, sxtw 1]
	b	.L951
.LBE277:
.LBE279:
	.cfi_endproc
.LFE256:
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
.LFB240:
	.loc 2 2735 0
	.cfi_startproc
.LVL993:
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	and	w19, w0, 65535
.LVL994:
	.loc 2 2736 0
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	.loc 2 2735 0
	str	x21, [sp, 32]
	.cfi_offset 21, -16
	.loc 2 2736 0
	ubfiz	x1, x19, 1, 16
	ldr	x2, [x0, 2352]
	ldrh	w1, [x2, x1]
	cbnz	w1, .L960
	.loc 2 2737 0
	ldrh	w1, [x0, 2648]
	cmp	w1, w19
	bne	.L961
	.loc 2 2739 0
	mov	w1, -1
	strh	w1, [x0, 2648]
.L962:
.LBB282:
.LBB283:
	.loc 2 2744 0
	add	x21, x20, :lo12:.LANCHOR0
	mov	w1, w19
	add	x0, x21, 2344
	bl	List_remove_node
.LVL995:
	ldrh	w0, [x21, 2376]
	cbnz	w0, .L964
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2744
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL996:
.L964:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20, 2376]
	sub	w0, w0, #1
	strh	w0, [x20, 2376]
	.loc 2 2745 0
	mov	w0, w19
	bl	free_data_superblock
.LVL997:
	.loc 2 2746 0
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
.LVL998:
	.loc 2 2747 0
	ldrh	w1, [x20, 2376]
	mov	w19, 1
.LVL999:
	ldrh	w0, [x20, 2392]
	add	w0, w0, w1
	ldrh	w1, [x20, 20]
	cmp	w0, w1
	ble	.L959
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2747
	add	x1, x1, 456
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1000:
.L959:
.LBE283:
.LBE282:
	.loc 2 2753 0
	mov	w0, w19
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1001:
.L961:
	.cfi_restore_state
	.loc 2 2740 0
	ldrh	w1, [x0, 2400]
	cmp	w1, w19
	beq	.L967
	.loc 2 2740 0 is_stmt 0 discriminator 1
	ldrh	w1, [x0, 2448]
	cmp	w1, w19
	beq	.L967
	.loc 2 2740 0 discriminator 2
	ldrh	w0, [x0, 2496]
	cmp	w0, w19
	bne	.L962
.L967:
	.loc 2 2742 0 is_stmt 1
	mov	w19, 0
.LVL1002:
	b	.L959
.LVL1003:
.L960:
	.loc 2 2750 0
	mov	w0, w19
	bl	List_update_data_list
.LVL1004:
	b	.L967
	.cfi_endproc
.LFE240:
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
.LFB241:
	.loc 2 2756 0
	.cfi_startproc
.LVL1005:
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	and	w19, w0, 65535
.LVL1006:
	str	x21, [sp, 32]
	.cfi_offset 21, -16
	.loc 2 2759 0
	mov	w0, 65535
	cmp	w19, w0
	beq	.L971
	.loc 2 2760 0
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	ubfiz	x21, x19, 1, 16
	ldr	x1, [x20, 2352]
	ldrh	w0, [x1, x21]
	cbnz	w0, .L972
	.loc 2 2761 0
	mov	w2, 0
	mov	w1, w19
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
	bl	printk
.LVL1007:
	.loc 2 2762 0
	ldr	x0, [x20, 2352]
	ldrh	w0, [x0, x21]
	cbz	w0, .L973
.L978:
	.loc 2 2763 0 discriminator 1
	mov	w21, 0
.L970:
	.loc 2 2781 0
	mov	w0, w21
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
.LVL1008:
	ldp	x29, x30, [sp], 48
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1009:
.L973:
	.cfi_restore_state
	.loc 2 2762 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2762
	add	x1, x1, 472
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1010:
	b	.L978
.L972:
	.loc 2 2765 0
	sub	w0, w0, #1
	strh	w0, [x1, x21]
.L971:
	.loc 2 2769 0
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	mov	w1, 65535
	ldrh	w0, [x20, -104]
	cmp	w0, w1
	bne	.L975
	.loc 2 2770 0
	strh	w19, [x20, -104]
	b	.L978
.L975:
	.loc 2 2772 0
	cmp	w19, w0
	beq	.L978
	.loc 2 2775 0
	bl	update_vpc_list
.LVL1011:
	cmp	w0, 0
	.loc 2 2778 0
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	.loc 2 2777 0
	strh	w19, [x20, -104]
	.loc 2 2775 0
	cset	w21, ne
	.loc 2 2778 0
	ldr	x2, [x1, 2336]
	ldr	x0, [x1, 2344]
	ldr	x1, [x1, 2352]
	sub	x0, x0, x2
	mov	x2, -6148914691236517206
	asr	x0, x0, 1
	movk	x2, 0xaaab, lsl 0
	mul	x0, x0, x2
	and	x2, x0, 65535
	ldrh	w1, [x1, x2, lsl 1]
	cbnz	w1, .L970
	.loc 2 2778 0 is_stmt 0 discriminator 1
	cmp	w19, w0, uxth
	beq	.L970
	.loc 2 2778 0 discriminator 2
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2778
	add	x1, x1, 472
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1012:
	b	.L970
	.cfi_endproc
.LFE241:
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlWriteDump_data
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
.LFB207:
	.loc 2 996 0 is_stmt 1
	.cfi_startproc
	stp	x29, x30, [sp, -128]!
	.cfi_def_cfa_offset 128
	.cfi_offset 29, -128
	.cfi_offset 30, -120
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -112
	.cfi_offset 20, -104
	.loc 2 997 0
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	.loc 2 996 0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	.cfi_offset 21, -96
	.cfi_offset 22, -88
	.cfi_offset 23, -80
	.cfi_offset 24, -72
	.cfi_offset 25, -64
	.loc 2 997 0
	ldrh	w2, [x20, 2404]
	cbz	w2, .L980
	.loc 2 997 0 is_stmt 0 discriminator 1
	ldrb	w1, [x20, 2408]
	cbnz	w1, .L980
	.loc 2 998 0 is_stmt 1
	ldrb	w1, [x20, 2407]
	ldrh	w3, [x20, 82]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L980
.LBB287:
.LBB288:
	.loc 2 1005 0
	ldrb	w0, [x20, 2410]
	cbnz	w0, .L979
	.loc 2 1000 0
	ldr	w22, [x20, 2560]
	.loc 2 1007 0
	add	x1, x29, 92
	.loc 2 1002 0
	ldrh	w24, [x20, 12]
	.loc 2 1007 0
	mov	w2, 0
	.loc 2 1000 0
	sub	w22, w22, #1
.LVL1013:
	.loc 2 1007 0
	mov	w0, w22
	bl	log2phys
.LVL1014:
	.loc 2 1013 0
	ldr	x21, [x20, 3384]
.LVL1015:
	.loc 2 1010 0
	ldr	w0, [x29, 92]
	.loc 2 1012 0
	ldr	x1, [x20, 3376]
	.loc 2 1010 0
	str	w0, [x29, 100]
	.loc 2 1016 0
	cmn	w0, #1
	.loc 2 1013 0
	stp	x1, x21, [x29, 104]
	.loc 2 1011 0
	str	w22, [x29, 120]
	.loc 2 1015 0
	str	wzr, [x21, 4]
	.loc 2 1016 0
	beq	.L982
	.loc 2 1018 0
	mov	w2, 0
	mov	w1, 1
	add	x0, x29, 96
	bl	FlashReadPages
.LVL1016:
.L983:
	.loc 2 1027 0
	add	x25, x19, :lo12:.LANCHOR0
	.loc 2 1002 0
	lsl	w24, w24, 2
.LVL1017:
	.loc 2 1027 0
	add	x20, x25, 2400
	.loc 2 1024 0
	mov	w0, -3947
	mov	w23, 0
	strh	w0, [x21]
.L984:
	.loc 2 1025 0
	cmp	w24, w23
	bne	.L988
.L985:
	.loc 2 1041 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19, 2410]
.L979:
.LBE288:
.LBE287:
	.loc 2 1049 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL1018:
	ldp	x23, x24, [sp, 48]
.LVL1019:
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 128
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1020:
.L982:
	.cfi_restore_state
.LBB290:
.LBB289:
	.loc 2 1022 0
	ldrh	w2, [x20, 90]
	mov	w1, 255
	ldr	x0, [x20, 3376]
	bl	ftl_memset
.LVL1021:
	b	.L983
.LVL1022:
.L988:
	.loc 2 1027 0
	ldrh	w0, [x20, 4]
	cbz	w0, .L985
	.loc 2 1032 0
	ldr	w0, [x29, 100]
	add	w23, w23, 1
	stp	w22, w0, [x21, 8]
	.loc 2 1033 0
	ldrh	w0, [x20]
	strh	w0, [x21, 2]
	.loc 2 1035 0
	mov	x0, x20
	bl	get_new_active_ppa
.LVL1023:
	str	w0, [x29, 100]
	.loc 2 1036 0
	ldr	w0, [x25, 2600]
	.loc 2 1037 0
	mov	w3, 0
	.loc 2 1036 0
	str	w0, [x21, 4]
	.loc 2 1037 0
	mov	w2, 0
	.loc 2 1036 0
	add	w0, w0, 1
	.loc 2 1037 0
	mov	w1, 1
	.loc 2 1036 0
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x25, 2600]
	.loc 2 1037 0
	add	x0, x29, 96
	bl	FlashProgPages
.LVL1024:
	.loc 2 1039 0
	ldrh	w0, [x20]
	bl	decrement_vpc_count
.LVL1025:
	b	.L984
.LVL1026:
.L980:
.LBE289:
.LBE290:
	.loc 2 1047 0
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x19, 2410]
	.loc 2 1049 0
	b	.L979
	.cfi_endproc
.LFE207:
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	l2p_flush
	.type	l2p_flush, %function
l2p_flush:
.LFB200:
	.loc 2 789 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 2 795 0
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w19, 0
.LVL1027:
	.loc 2 793 0
	bl	FtlWriteDump_data
.LVL1028:
.L998:
	.loc 2 795 0 discriminator 1
	ldrh	w0, [x20, 118]
	cmp	w0, w19
	bhi	.L1000
	.loc 2 799 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
.LVL1029:
	ldp	x29, x30, [sp], 32
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1030:
.L1000:
	.cfi_restore_state
	.loc 2 796 0
	ldr	x1, [x20, 2544]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0, 4]
	tbz	w0, #31, .L999
	.loc 2 797 0
	mov	w0, w19
	bl	flush_l2p_region
.LVL1031:
.L999:
	.loc 2 795 0 discriminator 2
	add	w19, w19, 1
.LVL1032:
	and	w19, w19, 65535
.LVL1033:
	b	.L998
	.cfi_endproc
.LFE200:
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
.LFB220:
	.loc 2 1705 0
	.cfi_startproc
.LVL1034:
	stp	x29, x30, [sp, -176]!
	.cfi_def_cfa_offset 176
	.cfi_offset 29, -176
	.cfi_offset 30, -168
	.loc 2 1719 0
	mov	w1, 65535
	.loc 2 1705 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -160
	.cfi_offset 20, -152
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	.cfi_offset 21, -144
	.cfi_offset 22, -136
	.cfi_offset 23, -128
	.cfi_offset 24, -120
	.cfi_offset 25, -112
	.cfi_offset 26, -104
	.cfi_offset 27, -96
	.cfi_offset 28, -88
	.loc 2 1719 0
	ldrh	w0, [x0]
.LVL1035:
	cmp	w0, w1
	beq	.L1120
	.loc 2 1725 0
	ldrb	w0, [x19, 6]
.LVL1036:
	.loc 2 1730 0
	adrp	x20, .LANCHOR0
	.loc 2 1725 0
	str	w0, [x29, 156]
	.loc 2 1730 0
	add	x0, x20, :lo12:.LANCHOR0
.LVL1037:
	.loc 2 1724 0
	ldrh	w27, [x19, 2]
.LVL1038:
	.loc 2 1730 0
	ldrh	w0, [x0, 82]
	cmp	w0, w27
	bne	.L1005
	.loc 2 1731 0
	strh	wzr, [x19, 4]
.LVL1039:
.L1126:
	.loc 2 1758 0
	strb	wzr, [x19, 6]
.LVL1040:
.L1120:
	.loc 2 2059 0
	ldp	x19, x20, [sp, 16]
.LVL1041:
	mov	w0, 0
.LVL1042:
	ldp	x21, x22, [sp, 32]
.LVL1043:
	ldp	x23, x24, [sp, 48]
.LVL1044:
	ldp	x25, x26, [sp, 64]
.LVL1045:
	ldp	x27, x28, [sp, 80]
.LVL1046:
	ldp	x29, x30, [sp], 176
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1047:
.L1005:
	.cfi_restore_state
	.loc 2 1738 0
	ldrh	w0, [x19, 16]
	.loc 2 1737 0
	mov	w21, 0
.L1006:
	.loc 2 1739 0
	cmp	w0, w1
	beq	.L1007
	.loc 2 1746 0
	mov	w1, 1
	bl	FtlGetLastWrittenPage
.LVL1048:
	mov	w24, w0
.LVL1049:
	.loc 2 1754 0
	cmn	w0, #1
	beq	.L1008
	.loc 2 1770 0
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x19, 16
	str	x1, [x29, 144]
	.loc 2 1771 0
	add	x5, x19, 16
	mov	w21, 0
	.loc 2 1768 0
	mov	w10, 65535
	ldrh	w1, [x0, 12]
	.loc 2 1770 0
	mov	w6, 4
	ldrh	w8, [x0, 90]
	add	x1, x1, 8
	.loc 2 1771 0
	ldrh	w9, [x0, 92]
	add	x1, x19, x1, lsl 1
.L1009:
	.loc 2 1766 0 discriminator 1
	cmp	x5, x1
	bne	.L1013
	.loc 2 1776 0
	add	x23, x20, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w1, w21
	ldr	x0, [x23, 3960]
	bl	FlashReadPages
.LVL1050:
	.loc 2 1777 0
	ldr	w22, [x23, 2600]
	.loc 2 1779 0
	ldr	x4, [x23, 3960]
	.loc 2 1791 0
	and	w9, w24, 65535
	.loc 2 1777 0
	sub	w22, w22, #1
.LVL1051:
	.loc 2 1715 0
	mov	w7, 65535
	mov	x6, x4
	.loc 2 1778 0
	mov	w3, 0
.LVL1052:
.L1014:
	.loc 2 1778 0 is_stmt 0 discriminator 1
	cmp	w21, w3
	bne	.L1019
	.loc 2 1796 0 is_stmt 1
	add	w23, w24, 1
	.loc 2 1798 0
	ldr	w0, [x4, 4]
	.loc 2 1796 0
	and	w23, w23, 65535
.LVL1053:
.L1122:
	.loc 2 1802 0
	lsr	w0, w0, 10
	bl	P2V_plane
.LVL1054:
	and	w28, w0, 65535
.LVL1055:
	.loc 2 1807 0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0, 82]
	cmp	w0, w23
	bne	.L1021
	.loc 2 1808 0
	strh	w23, [x19, 2]
	.loc 2 1809 0
	strb	wzr, [x19, 6]
	.loc 2 1810 0
	strh	wzr, [x19, 4]
.L1021:
	.loc 2 1813 0
	ldr	w0, [x29, 156]
	cmp	w23, w27
.LVL1056:
	ccmp	w28, w0, 0, eq
	bne	.L1022
.LVL1057:
.L1127:
	.loc 2 2053 0
	mov	w2, w28
	mov	w1, w23
	mov	x0, x19
.LVL1058:
	bl	ftl_sb_update_avl_pages
.LVL1059:
	b	.L1120
.LVL1060:
.L1007:
	.loc 2 1740 0
	add	w21, w21, 1
	and	w21, w21, 65535
	.loc 2 1741 0
	add	x0, x19, x21, sxtw 1
	ldrh	w0, [x0, 16]
	b	.L1006
.LVL1061:
.L1008:
	.loc 2 1755 0
	cbz	w27, .L1010
	.loc 2 1755 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
.LVL1062:
	mov	w2, 1755
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1063:
.L1010:
	.loc 2 1756 0 is_stmt 1
	ldr	w0, [x29, 156]
.LVL1064:
	cmp	w0, 0
	ccmp	w21, w0, 4, ne
	beq	.L1011
	.loc 2 1756 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
.LVL1065:
	mov	w2, 1756
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1066:
.L1011:
	.loc 2 1757 0 is_stmt 1
	strh	wzr, [x19, 2]
	b	.L1126
.LVL1067:
.L1013:
	.loc 2 1767 0
	ldrh	w2, [x5]
	.loc 2 1768 0
	cmp	w2, w10
	beq	.L1012
	.loc 2 1769 0
	ldr	x4, [x0, 3960]
	ubfiz	x7, x21, 5, 16
	orr	w2, w24, w2, lsl 10
	add	x4, x4, x7
	str	w2, [x4, 4]
	.loc 2 1770 0
	mul	w2, w21, w8
	ldr	x3, [x0, 3960]
	ldr	x4, [x0, 3240]
	sdiv	w2, w2, w6
	add	x3, x3, x7
	add	x2, x4, x2, sxtw 2
	str	x2, [x3, 8]
	.loc 2 1771 0
	mul	w2, w21, w9
	ldr	x4, [x0, 3248]
	.loc 2 1772 0
	add	w21, w21, 1
	and	w21, w21, 65535
	.loc 2 1771 0
	sdiv	w2, w2, w6
	add	x2, x4, x2, sxtw 2
	str	x2, [x3, 16]
.L1012:
	add	x5, x5, 2
	b	.L1009
.LVL1068:
.L1019:
	.loc 2 1779 0
	ldr	w0, [x6]
	cbnz	w0, .L1015
	.loc 2 1780 0
	ldr	x8, [x6, 16]
	.loc 2 1781 0
	ldr	w5, [x8, 4]
	cmn	w5, #1
	beq	.L1016
	.loc 2 1782 0
	ldr	w1, [x23, 2600]
	mov	w0, w5
	bl	ftl_cmp_data_ver
.LVL1069:
	cbz	w0, .L1016
	.loc 2 1784 0
	add	w5, w5, 1
	str	w5, [x23, 2600]
.L1016:
	.loc 2 1787 0
	ldr	w0, [x8]
	cmn	w0, #1
	bne	.L1018
	.loc 2 1802 0
	ubfiz	x3, x3, 5, 16
.LVL1070:
	.loc 2 1801 0
	and	w23, w24, 65535
.LVL1071:
	.loc 2 1802 0
	add	x3, x4, x3
	ldr	w0, [x3, 4]
	b	.L1122
.LVL1072:
.L1015:
	.loc 2 1791 0
	mov	w7, w9
.L1018:
	.loc 2 1778 0 discriminator 2
	add	w3, w3, 1
.LVL1073:
	add	x6, x6, 32
	and	w3, w3, 65535
.LVL1074:
	b	.L1014
.LVL1075:
.L1022:
	.loc 2 1819 0
	mov	w0, 65535
.LVL1076:
	adrp	x21, .LANCHOR3
	cmp	w7, w0
	bne	.L1023
	.loc 2 1819 0 is_stmt 0 discriminator 1
	ldrb	w0, [x19, 8]
	cbnz	w0, .L1024
.L1023:
.LBB291:
	.loc 2 1824 0 is_stmt 1
	add	x0, x21, :lo12:.LANCHOR3
	.loc 2 1823 0
	and	w3, w24, 65535
	.loc 2 1824 0
	ldr	w1, [x0, -92]
	cmn	w1, #1
	bne	.L1025
	.loc 2 1825 0
	str	w22, [x0, -92]
.L1025:
	.loc 2 1827 0
	add	w0, w27, 7
	cmp	w0, w24, uxth
	bge	.L1073
	.loc 2 1828 0
	sub	w25, w3, #7
	and	w25, w25, 65535
.L1026:
	.loc 2 1831 0 discriminator 1
	add	x26, x20, :lo12:.LANCHOR0
	.loc 2 1833 0 discriminator 1
	mov	w4, 65535
.L1027:
	.loc 2 1829 0 discriminator 1
	cmp	w25, w3
	bhi	.L1034
	ldrh	w0, [x26, 12]
	.loc 2 1831 0
	mov	w24, 0
.LVL1077:
	ldr	x1, [x29, 144]
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
	b	.L1035
.LVL1078:
.L1073:
	mov	w25, w27
	b	.L1026
.LVL1079:
.L1029:
	.loc 2 1832 0
	ldrh	w2, [x1]
	.loc 2 1833 0
	cmp	w2, w4
	beq	.L1028
	.loc 2 1834 0
	ldr	x5, [x26, 3960]
	ubfiz	x6, x24, 5, 16
	.loc 2 1835 0
	add	w24, w24, 1
	.loc 2 1834 0
	orr	w2, w25, w2, lsl 10
	add	x5, x5, x6
	.loc 2 1835 0
	and	w24, w24, 65535
	.loc 2 1834 0
	str	w2, [x5, 4]
.L1028:
	add	x1, x1, 2
.L1035:
	.loc 2 1831 0 discriminator 1
	cmp	x0, x1
	bne	.L1029
	.loc 2 1838 0
	ldr	x0, [x26, 3960]
	mov	w1, w24
	mov	w2, 0
	str	w4, [x29, 128]
	str	w3, [x29, 136]
	ubfiz	x24, x24, 5, 16
	add	x24, x24, 16
	bl	FlashReadPages
.LVL1080:
	.loc 2 1840 0
	ldr	x1, [x26, 3960]
	.loc 2 1846 0
	add	x2, x21, :lo12:.LANCHOR3
	.loc 2 1839 0
	ldr	w4, [x29, 128]
	ldr	w3, [x29, 136]
	add	x0, x1, 16
	add	x24, x1, x24
.L1030:
	.loc 2 1839 0 is_stmt 0 discriminator 1
	cmp	x24, x0
	bne	.L1033
	.loc 2 1829 0 is_stmt 1
	add	w25, w25, 1
	and	w25, w25, 65535
	b	.L1027
.L1033:
	.loc 2 1840 0
	ldr	w1, [x0, -16]
	cbnz	w1, .L1024
	.loc 2 1841 0
	ldr	x1, [x0]
	.loc 2 1842 0
	ldrh	w5, [x1]
	cmp	w5, w4
	beq	.L1032
	.loc 2 1844 0
	ldr	w1, [x1, 4]
	cmn	w1, #1
	beq	.L1032
	.loc 2 1846 0
	str	w1, [x2, -92]
.L1032:
	add	x0, x0, 32
	b	.L1030
.LVL1081:
.L1034:
	.loc 2 1858 0
	add	x0, x21, :lo12:.LANCHOR3
	mov	w1, -1
	str	w1, [x0, -92]
.L1024:
.LBE291:
	.loc 2 1863 0
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 1
	.loc 2 1864 0
	mov	w25, w27
	.loc 2 1870 0
	add	x20, x20, :lo12:.LANCHOR0
	.loc 2 1863 0
	strh	w0, [x21, -88]
	.loc 2 1864 0
	sub	x0, x21, #248
	bl	FtlMapBlkWriteDump_data
.LVL1082:
	.loc 2 2019 0
	sub	x0, x21, #80
	str	x0, [x29, 112]
.LVL1083:
.L1036:
	ldrh	w0, [x20, 12]
	.loc 2 1869 0
	mov	w24, 0
.LVL1084:
	.loc 2 1870 0
	ldr	x1, [x29, 144]
.LVL1085:
	.loc 2 1872 0
	mov	w4, 65535
	add	x0, x0, 8
	add	x0, x19, x0, lsl 1
.L1037:
	.loc 2 1870 0 discriminator 1
	cmp	x0, x1
	bne	.L1039
	.loc 2 1877 0
	ldr	x0, [x20, 3960]
	mov	w1, w24
	mov	w2, 0
	bl	FlashReadPages
.LVL1086:
	ubfiz	x0, x24, 5, 16
	str	x0, [x29, 120]
	.loc 2 2006 0
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 496
	.loc 2 1878 0
	mov	x24, 0
	.loc 2 2006 0
	str	x0, [x29, 128]
.LVL1087:
.L1040:
	.loc 2 1878 0 discriminator 1
	ldr	x0, [x29, 120]
.LVL1088:
	cmp	x0, x24
	bne	.L1067
	.loc 2 2031 0
	add	w25, w25, 1
	.loc 2 2032 0
	ldrh	w0, [x20, 82]
	.loc 2 2031 0
	and	w25, w25, 65535
	.loc 2 2032 0
	cmp	w0, w25
	bne	.L1036
	.loc 2 2035 0
	ldrh	w1, [x20, 12]
.LVL1089:
	mov	w0, 0
	.loc 2 2033 0
	strh	w25, [x19, 2]
	.loc 2 2037 0
	mov	w2, 65535
	.loc 2 2034 0
	strh	wzr, [x19, 4]
.L1068:
	.loc 2 2035 0 discriminator 1
	cmp	w0, w1
	beq	.L1120
	.loc 2 2037 0
	ldr	x4, [x29, 144]
	ldrh	w3, [x4], 2
	str	x4, [x29, 144]
	cmp	w3, w2
	beq	.L1069
	.loc 2 2038 0
	strb	w0, [x19, 6]
	.loc 2 2039 0
	b	.L1120
.L1039:
	.loc 2 1871 0
	ldrh	w2, [x1]
	.loc 2 1872 0
	cmp	w2, w4
	beq	.L1038
	.loc 2 1873 0
	ldr	x3, [x20, 3960]
	ubfiz	x5, x24, 5, 16
	.loc 2 1874 0
	add	w24, w24, 1
	.loc 2 1873 0
	orr	w2, w25, w2, lsl 10
	add	x3, x3, x5
	.loc 2 1874 0
	and	w24, w24, 65535
	.loc 2 1873 0
	str	w2, [x3, 4]
.L1038:
	add	x1, x1, 2
	b	.L1037
.LVL1090:
.L1067:
	.loc 2 1879 0
	ldr	x4, [x20, 3960]
	add	x4, x4, x24
	ldr	w5, [x4, 4]
	str	w5, [x29, 172]
	.loc 2 1880 0
	lsr	w0, w5, 10
	bl	P2V_plane
.LVL1091:
	and	w0, w0, 65535
.LVL1092:
	.loc 2 1882 0
	cmp	w25, w27
	bcc	.L1041
	.loc 2 1882 0 is_stmt 0 discriminator 1
	ldr	w1, [x29, 156]
.LVL1093:
	ccmp	w1, w0, 0, eq
	bhi	.L1041
	.loc 2 1886 0 is_stmt 1
	cmp	w25, w23
	ccmp	w28, w0, 0, eq
	beq	.L1042
	.loc 2 1890 0
	ldr	w0, [x4]
.LVL1094:
	cmn	w0, #1
	beq	.L1043
	.loc 2 1891 0
	ldr	x3, [x4, 16]
	.loc 2 1892 0
	mov	w0, 61589
	ldrh	w1, [x3]
.LVL1095:
	cmp	w1, w0
	beq	.L1044
.L1051:
.LBB292:
	.loc 2 1925 0
	ldrh	w0, [x19]
.L1125:
.LBE292:
	.loc 2 2009 0
	bl	decrement_vpc_count
.LVL1096:
.L1041:
	add	x24, x24, 32
	b	.L1040
.LVL1097:
.L1044:
	.loc 2 1898 0
	ldr	w22, [x3, 4]
.LVL1098:
	.loc 2 1899 0
	cmn	w22, #1
	beq	.L1045
	.loc 2 1900 0 discriminator 1
	ldr	w1, [x20, 2600]
	mov	w0, w22
	str	x3, [x29, 136]
	bl	ftl_cmp_data_ver
.LVL1099:
	.loc 2 1899 0 discriminator 1
	ldr	x3, [x29, 136]
	cbz	w0, .L1045
	.loc 2 1902 0
	add	w0, w22, 1
	str	w0, [x20, 2600]
.L1045:
	.loc 2 1905 0
	ldrh	w1, [x3]
	mov	w0, 61589
	cmp	w1, w0
	beq	.L1046
	.loc 2 1905 0 is_stmt 0 discriminator 1
	ldr	x1, [x29, 128]
	mov	w2, 1905
	str	x3, [x29, 136]
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1100:
	ldr	x3, [x29, 136]
.L1046:
	.loc 2 1907 0 is_stmt 1
	ldp	w26, w0, [x3, 8]
.LVL1101:
	.loc 2 1908 0
	add	x1, x29, 168
	.loc 2 1907 0
	str	w0, [x29, 164]
	.loc 2 1908 0
	mov	w2, 0
.LVL1102:
	mov	w0, w26
	bl	log2phys
.LVL1103:
	.loc 2 1909 0
	ldr	w1, [x21, -92]
	cmn	w1, #1
	beq	.L1047
	.loc 2 1910 0 discriminator 1
	mov	w0, w22
	bl	ftl_cmp_data_ver
.LVL1104:
	.loc 2 1909 0 discriminator 1
	cbz	w0, .L1047
.LBB293:
	.loc 2 1914 0
	ldr	w1, [x29, 164]
	cmn	w1, #1
	beq	.L1048
	.loc 2 1915 0
	ldr	x0, [x20, 3960]
	.loc 2 1917 0
	mov	w2, 0
	.loc 2 1915 0
	add	x0, x0, x24
	ldr	x4, [x0, 16]
	.loc 2 1916 0
	str	w1, [x0, 4]
	.loc 2 1917 0
	mov	w1, 1
	.loc 2 1915 0
	str	x4, [x29, 136]
	.loc 2 1917 0
	ldr	x0, [x20, 3960]
	add	x0, x0, x24
	bl	FlashReadPages
.LVL1105:
	.loc 2 1930 0
	ldr	x0, [x20, 3960]
	ldr	x4, [x29, 136]
	add	x3, x0, x24
	ldr	w0, [x0, x24]
	cmn	w0, #1
	bne	.L1049
.LVL1106:
.L1050:
	.loc 2 1962 0
	mov	w0, -1
	str	w0, [x29, 164]
	.loc 2 1963 0
	ldrh	w0, [x19]
	bl	decrement_vpc_count
.LVL1107:
.L1058:
.LBE293:
	.loc 2 2003 0
	ldr	w4, [x29, 164]
	cmn	w4, #1
	beq	.L1041
.L1072:
	.loc 2 2005 0
	lsr	w0, w4, 10
.LVL1108:
	bl	P2V_block_in_plane
.LVL1109:
	and	w26, w0, 65535
.LVL1110:
	.loc 2 2006 0
	ldrh	w0, [x20, 20]
	.loc 2 2005 0
	mov	w3, w26
.LVL1111:
	.loc 2 2006 0
	cmp	w0, w26
	bhi	.L1063
	.loc 2 2006 0 is_stmt 0 discriminator 1
	ldr	x1, [x29, 128]
	mov	w2, 2006
	adrp	x0, .LC1
	str	w26, [x29, 136]
.LVL1112:
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1113:
	ldr	w3, [x29, 136]
.LVL1114:
.L1063:
	.loc 2 2007 0 is_stmt 1
	ldr	x1, [x20, 2352]
	ubfiz	x0, x26, 1, 16
	ldrh	w0, [x1, x0]
	cbz	w0, .L1064
	.loc 2 2009 0
	mov	w0, w3
	b	.L1125
.LVL1115:
.L1048:
.LBB294:
	.loc 2 1921 0
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	bne	.L1051
	.loc 2 1922 0
	mov	w2, 1
	add	x1, x29, 164
	mov	w0, w26
	bl	log2phys
.LVL1116:
	b	.L1051
.L1049:
	.loc 2 1930 0 discriminator 1
	ldr	w0, [x4, 8]
	cmp	w26, w0
	bne	.L1050
	.loc 2 1918 0 discriminator 2
	ldr	w0, [x4, 4]
.LVL1117:
	str	w0, [x29, 136]
	str	x4, [x29, 104]
	.loc 2 1931 0 discriminator 2
	uxtw	x1, w0
.LVL1118:
	ldr	w0, [x21, -92]
	bl	ftl_cmp_data_ver
.LVL1119:
	.loc 2 1930 0 discriminator 2
	cbz	w0, .L1050
	.loc 2 1932 0
	ldp	w0, w1, [x29, 168]
	ldr	x4, [x29, 104]
	cmp	w0, w1
	.loc 2 1934 0
	ldr	w1, [x29, 164]
	.loc 2 1932 0
	bne	.L1053
.LVL1120:
.L1123:
	.loc 2 1958 0
	mov	w0, w26
	bl	FtlReUsePrevPpa
.LVL1121:
	b	.L1050
.L1053:
	.loc 2 1935 0
	cmp	w0, w1
	beq	.L1050
	.loc 2 1936 0
	cmn	w0, #1
	beq	.L1054
	.loc 2 1937 0
	ldr	x4, [x3, 16]
	.loc 2 1939 0
	mov	w2, 0
	.loc 2 1938 0
	str	w0, [x3, 4]
	.loc 2 1939 0
	mov	w1, 1
	.loc 2 1937 0
	str	x4, [x29, 104]
	.loc 2 1939 0
	ldr	x0, [x20, 3960]
	add	x0, x0, x24
	bl	FlashReadPages
.LVL1122:
	ldr	x4, [x29, 104]
.L1055:
	.loc 2 1944 0
	adrp	x0, .LANCHOR0+3960
	ldr	x0, [x0, #:lo12:.LANCHOR0+3960]
	ldr	w0, [x0, x24]
	cmn	w0, #1
	beq	.L1056
	.loc 2 1945 0
	ldr	w3, [x4, 4]
	.loc 2 1946 0
	ldr	w0, [x21, -92]
.LVL1123:
	mov	w1, w3
	bl	ftl_cmp_data_ver
.LVL1124:
	cbz	w0, .L1056
	.loc 2 1947 0
	ldr	w0, [x29, 136]
	mov	w1, w3
	bl	ftl_cmp_data_ver
.LVL1125:
	cbz	w0, .L1050
.L1056:
	.loc 2 1958 0
	ldr	w1, [x29, 164]
	b	.L1123
.LVL1126:
.L1054:
	.loc 2 1941 0
	str	w0, [x3]
	b	.L1055
.LVL1127:
.L1047:
.LBE294:
	.loc 2 1964 0
	ldp	w1, w0, [x29, 168]
	cmp	w1, w0
	beq	.L1058
	.loc 2 1966 0
	mov	w2, 1
	add	x1, x29, 172
	mov	w0, w26
	bl	log2phys
.LVL1128:
	.loc 2 1967 0
	ldr	w4, [x29, 168]
	cmn	w4, #1
	beq	.L1058
	.loc 2 1967 0 is_stmt 0 discriminator 1
	ldr	w0, [x29, 164]
	cmp	w4, w0
	beq	.L1072
	.loc 2 1968 0 is_stmt 1
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
.LVL1129:
	.loc 2 1970 0
	ldrh	w1, [x20, 2400]
	.loc 2 1968 0
	and	w0, w0, 65535
.LVL1130:
	.loc 2 1970 0
	cmp	w1, w0
	beq	.L1062
	.loc 2 1970 0 is_stmt 0 discriminator 1
	ldrh	w1, [x20, 2448]
	cmp	w1, w0
	beq	.L1062
	.loc 2 1971 0 is_stmt 1
	ldrh	w1, [x20, 2496]
	cmp	w1, w0
	bne	.L1058
.L1062:
.LBB295:
	.loc 2 1976 0
	ldr	x0, [x20, 3960]
.LVL1131:
	.loc 2 1979 0
	mov	w2, 0
	mov	w1, 1
	.loc 2 1978 0
	str	w4, [x0, 4]
	.loc 2 1976 0
	ldr	x3, [x0, 16]
	.loc 2 1979 0
	ldr	x0, [x20, 3960]
	.loc 2 1976 0
	str	x3, [x29, 136]
	.loc 2 1979 0
	bl	FlashReadPages
.LVL1132:
	.loc 2 1982 0
	ldr	x0, [x20, 3960]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1058
	.loc 2 1983 0
	ldr	x3, [x29, 136]
	mov	w0, w22
	ldr	w1, [x3, 4]
	bl	ftl_cmp_data_ver
.LVL1133:
	cbnz	w0, .L1058
	.loc 2 1990 0
	mov	w2, 1
	add	x1, x29, 168
	mov	w0, w26
	bl	log2phys
.LVL1134:
	b	.L1058
.LVL1135:
.L1064:
.LBE295:
	.loc 2 2011 0
	mov	w1, w26
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	bl	printk
.LVL1136:
	b	.L1041
.LVL1137:
.L1043:
	.loc 2 2018 0
	ldr	w0, [x21, -84]
	cmp	w0, 31
	bhi	.L1065
	.loc 2 2019 0
	ldr	x1, [x29, 112]
.LVL1138:
	str	w5, [x1, w0, uxtw 2]
	.loc 2 2020 0
	add	w0, w0, 1
	str	w0, [x21, -84]
.LVL1139:
.L1065:
	.loc 2 2022 0
	ldrh	w0, [x19]
	bl	decrement_vpc_count
.LVL1140:
	.loc 2 2024 0
	ldr	w0, [x21, -92]
	cmn	w0, #1
	bne	.L1066
.L1124:
	.loc 2 2027 0
	str	w22, [x21, -92]
	b	.L1041
.L1066:
	.loc 2 2026 0
	cmp	w22, w0
	bcs	.L1041
	b	.L1124
.L1069:
	.loc 2 2035 0 discriminator 2
	add	w0, w0, 1
	and	w0, w0, 65535
	b	.L1068
.LVL1141:
.L1042:
	.loc 2 2051 0
	strb	w28, [x19, 6]
	.loc 2 2052 0
	strh	w23, [x19, 2]
	b	.L1127
	.cfi_endproc
.LFE220:
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
.LFB227:
	.loc 2 2205 0
	.cfi_startproc
.LVL1142:
	stp	x29, x30, [sp, -96]!
	.cfi_def_cfa_offset 96
	.cfi_offset 29, -96
	.cfi_offset 30, -88
	.loc 2 2212 0
	adrp	x2, .LANCHOR0
	.loc 2 2220 0
	mov	w3, 61589
	.loc 2 2205 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -80
	.cfi_offset 20, -72
	mov	x19, x0
	.loc 2 2211 0
	mov	w0, -1
.LVL1143:
	.loc 2 2205 0
	stp	x21, x22, [sp, 32]
	.loc 2 2211 0
	str	w0, [x29, 88]
	.loc 2 2212 0
	add	x0, x2, :lo12:.LANCHOR0
	.loc 2 2205 0
	str	x23, [sp, 48]
	.cfi_offset 21, -64
	.cfi_offset 22, -56
	.cfi_offset 23, -48
	.loc 2 2229 0
	mov	x20, x0
	mov	x23, x2
	.loc 2 2223 0
	mov	w22, 7
	.loc 2 2213 0
	ldr	x21, [x0, 3384]
	.loc 2 2212 0
	ldr	x1, [x0, 3376]
	.loc 2 2213 0
	stp	x1, x21, [x29, 72]
	.loc 2 2216 0
	mov	w1, -3
	str	w1, [x21, 8]
	.loc 2 2217 0
	mov	w1, -2
	str	w1, [x21, 12]
	.loc 2 2218 0
	ldrh	w1, [x19]
	strh	w1, [x21, 2]
	.loc 2 2219 0
	strh	wzr, [x21]
	.loc 2 2220 0
	ldr	x1, [x0, 3376]
	str	w3, [x1]
	.loc 2 2221 0
	mov	w3, 22136
	movk	w3, 0x1234, lsl 16
	ldr	x1, [x0, 3376]
	str	w3, [x1, 4]
.L1129:
	.loc 2 2223 0
	subs	w22, w22, #1
	beq	.L1131
	.loc 2 2224 0
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1130
.L1131:
	.loc 2 2234 0
	add	x2, x23, :lo12:.LANCHOR0
	ldrh	w0, [x19]
	ldrh	w4, [x19, 4]
	ldr	x3, [x2, 2352]
	lsl	x0, x0, 1
	ldrh	w1, [x3, x0]
	sub	w1, w1, w4
	strh	w1, [x3, x0]
	.loc 2 2236 0
	strb	wzr, [x19, 6]
	.loc 2 2235 0
	ldrh	w0, [x2, 82]
	strh	w0, [x19, 2]
	.loc 2 2237 0
	strh	wzr, [x19, 4]
	.loc 2 2238 0
	ldp	x19, x20, [sp, 16]
.LVL1144:
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 96
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1145:
.L1130:
	.cfi_restore_state
	.loc 2 2226 0
	mov	x0, x19
	bl	get_new_active_ppa
.LVL1146:
	str	w0, [x29, 68]
	.loc 2 2227 0
	cmn	w0, #1
	beq	.L1131
	.loc 2 2229 0
	ldr	w0, [x20, 2600]
	.loc 2 2230 0
	mov	w3, 0
	.loc 2 2229 0
	str	w0, [x21, 4]
	.loc 2 2230 0
	mov	w2, 0
	.loc 2 2229 0
	add	w0, w0, 1
	.loc 2 2230 0
	mov	w1, 1
	.loc 2 2229 0
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x20, 2600]
	.loc 2 2230 0
	add	x0, x29, 64
	bl	FlashProgPages
.LVL1147:
	.loc 2 2231 0
	ldrh	w0, [x19]
	bl	decrement_vpc_count
.LVL1148:
	b	.L1129
	.cfi_endproc
.LFE227:
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
.LFB168:
	.loc 4 173 0
	.cfi_startproc
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	.loc 4 180 0
	adrp	x19, .LANCHOR0
	.loc 4 173 0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -16
	.cfi_offset 24, -8
	.loc 4 180 0
	add	x23, x19, :lo12:.LANCHOR0
	.loc 4 173 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.loc 4 180 0
	add	x21, x23, 3344
	.loc 4 196 0
	mov	w24, 61649
	.loc 4 180 0
	ldr	x0, [x23, 3376]
	.loc 4 181 0
	ldr	x22, [x23, 3384]
	stp	x0, x22, [x21, 8]
	.loc 4 183 0
	bl	FtlBbtMemInit
.LVL1149:
	.loc 4 184 0
	ldrh	w20, [x23, 78]
	sub	w20, w20, #1
	and	w20, w20, 65535
.LVL1150:
.L1141:
	.loc 4 184 0 is_stmt 0 discriminator 1
	ldrh	w0, [x23, 78]
	sub	w0, w0, #16
	cmp	w20, w0
	ble	.L1144
	.loc 4 185 0 is_stmt 1
	lsl	w0, w20, 10
	.loc 4 186 0
	mov	w2, 1
	.loc 4 185 0
	str	w0, [x21, 4]
	.loc 4 186 0
	mov	w1, w2
	mov	x0, x21
	bl	FlashReadPages
.LVL1151:
	.loc 4 187 0
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1142
	.loc 4 188 0
	ldr	w0, [x21, 4]
	.loc 4 189 0
	mov	w2, 1
	mov	w1, w2
	.loc 4 188 0
	add	w0, w0, 1
	str	w0, [x21, 4]
	.loc 4 189 0
	mov	x0, x21
	bl	FlashReadPages
.LVL1152:
.L1142:
	.loc 4 191 0
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1143
	.loc 4 196 0
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1143
	.loc 4 199 0
	ldr	w1, [x22, 4]
	str	w1, [x23, 144]
	.loc 4 198 0
	strh	w20, [x23, 136]
	.loc 4 200 0
	ldrh	w1, [x22, 8]
	strh	w1, [x23, 140]
.L1144:
	.loc 4 216 0
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21, 136]
	cmp	w1, w0
	beq	.L1158
	.loc 4 222 0
	ldrh	w1, [x21, 140]
	cmp	w1, w0
	beq	.L1148
	.loc 4 224 0
	add	x0, x21, 3344
	lsl	w1, w1, 10
	.loc 4 225 0
	mov	w2, 1
	.loc 4 224 0
	str	w1, [x0, 4]
	.loc 4 225 0
	mov	w1, w2
	bl	FlashReadPages
.LVL1153:
	.loc 4 226 0
	ldr	w0, [x21, 3344]
	cmn	w0, #1
	beq	.L1148
	.loc 4 229 0
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1148
	.loc 4 229 0 is_stmt 0 discriminator 1
	ldr	w1, [x21, 144]
	ldr	w0, [x22, 4]
	cmp	w0, w1
	bls	.L1148
	.loc 4 232 0 is_stmt 1
	str	w0, [x21, 144]
	.loc 4 231 0
	ldrh	w1, [x21, 140]
	.loc 4 233 0
	ldrh	w0, [x22, 8]
	.loc 4 231 0
	strh	w1, [x21, 136]
	.loc 4 233 0
	strh	w0, [x21, 140]
.L1148:
	.loc 4 238 0
	add	x20, x19, :lo12:.LANCHOR0
.LVL1154:
	mov	w1, 1
	.loc 4 243 0
	add	x23, x20, 3344
	.loc 4 247 0
	mov	w24, 61649
	.loc 4 238 0
	ldrh	w0, [x20, 136]
	bl	FtlGetLastWrittenPage
.LVL1155:
	sxth	w21, w0
.LVL1156:
	.loc 4 239 0
	add	w0, w0, 1
	strh	w0, [x20, 138]
.L1150:
	.loc 4 241 0
	tbz	w21, #31, .L1153
	.loc 4 254 0
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 254
	add	x1, x1, 520
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1157:
.L1152:
	.loc 4 255 0
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x22, 10]
	.loc 4 257 0
	mov	w2, 65535
	.loc 4 255 0
	strh	w0, [x1, 142]
	.loc 4 257 0
	ldrh	w0, [x22, 12]
	cmp	w0, w2
	beq	.L1155
	.loc 4 259 0
	ldr	w2, [x1, 8]
	cmp	w0, w2
	beq	.L1155
	.loc 4 259 0 is_stmt 0 discriminator 1
	ldrh	w1, [x1, 22]
	lsr	w1, w1, 2
	cmp	w2, w1
	bcs	.L1155
	.loc 4 261 0 is_stmt 1
	cmp	w0, w1
	bcs	.L1155
	.loc 4 263 0
	bl	FtlSysBlkNumInit
.LVL1158:
.L1155:
	add	x19, x19, :lo12:.LANCHOR0
	.loc 4 269 0 discriminator 1
	adrp	x21, .LANCHOR3
.LVL1159:
	add	x22, x19, 168
	add	x21, x21, :lo12:.LANCHOR3
	.loc 4 173 0 discriminator 1
	mov	w20, 0
.LVL1160:
.L1156:
	.loc 4 268 0 discriminator 1
	ldrh	w0, [x19, 34]
	cmp	w20, w0
	bcc	.L1157
	.loc 4 275 0
	mov	w0, 0
.LVL1161:
.L1140:
	.loc 4 276 0
	ldp	x19, x20, [sp, 16]
.LVL1162:
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1163:
.L1143:
	.cfi_restore_state
	.loc 4 184 0 discriminator 2
	sub	w20, w20, #1
.LVL1164:
	and	w20, w20, 65535
.LVL1165:
	b	.L1141
.LVL1166:
.L1153:
	.loc 4 243 0
	ldrh	w0, [x20, 136]
	.loc 4 245 0
	mov	w2, 1
	mov	w1, w2
	.loc 4 243 0
	orr	w0, w21, w0, lsl 10
	str	w0, [x23, 4]
	.loc 4 244 0
	ldr	x0, [x20, 3376]
	str	x0, [x23, 8]
	.loc 4 245 0
	mov	x0, x23
	bl	FlashReadPages
.LVL1167:
	.loc 4 247 0
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1151
	.loc 4 247 0 is_stmt 0 discriminator 1
	ldrh	w0, [x22]
	cmp	w0, w24
	beq	.L1152
.L1151:
	sub	w21, w21, #1
.LVL1168:
	sxth	w21, w21
.LVL1169:
	b	.L1150
.LVL1170:
.L1157:
	.loc 4 269 0 is_stmt 1 discriminator 3
	ldrh	w2, [x21, -256]
	ldr	x0, [x19, 3352]
	mul	w1, w2, w20
	lsl	w2, w2, 2
	.loc 4 268 0 discriminator 3
	add	w20, w20, 1
	.loc 4 269 0 discriminator 3
	add	x1, x0, x1, lsl 2
	ldr	x0, [x22], 8
	bl	ftl_memcpy
.LVL1171:
	b	.L1156
.LVL1172:
.L1158:
	.loc 4 218 0
	mov	w0, -1
	b	.L1140
	.cfi_endproc
.LFE168:
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
.LFB170:
	.loc 4 317 0
	.cfi_startproc
	stp	x29, x30, [sp, -96]!
	.cfi_def_cfa_offset 96
	.cfi_offset 29, -96
	.cfi_offset 30, -88
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -80
	.cfi_offset 20, -72
	adrp	x19, .LANCHOR0
	stp	x25, x26, [sp, 64]
	stp	x21, x22, [sp, 32]
	.cfi_offset 25, -32
	.cfi_offset 26, -24
	.cfi_offset 21, -64
	.cfi_offset 22, -56
	mov	x21, x19
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -48
	.cfi_offset 24, -40
	.loc 4 327 0
	mov	w24, 0
.LVL1173:
	.loc 4 317 0
	stp	x27, x28, [sp, 80]
	.cfi_offset 27, -16
	.cfi_offset 28, -8
	.loc 4 325 0
	bl	FtlBbtMemInit
.LVL1174:
	.loc 4 326 0
	bl	FtlLoadFactoryBbt
.LVL1175:
	.loc 4 357 0
	adrp	x28, .LANCHOR3
	add	x0, x19, :lo12:.LANCHOR0
	add	x28, x28, :lo12:.LANCHOR3
	add	x26, x0, 168
	add	x25, x0, 148
	.loc 4 327 0
	mov	x19, x0
	.loc 4 328 0
	add	x20, x0, 3344
.LVL1176:
.L1171:
	.loc 4 327 0 discriminator 1
	ldrh	w0, [x19, 34]
	cmp	w24, w0
	bcc	.L1177
	.loc 4 381 0 discriminator 1
	add	x20, x21, :lo12:.LANCHOR0
	mov	w19, 0
.L1178:
	ldrh	w0, [x20, 94]
	cmp	w0, w19
	bhi	.L1179
	.loc 4 387 0
	add	x20, x20, 136
	ldrh	w19, [x20, 12]
	.loc 4 396 0
	mov	w22, 65535
	.loc 4 387 0
	sub	w19, w19, #1
	and	w19, w19, 65535
.L1180:
	.loc 4 387 0 is_stmt 0 discriminator 1
	ldrh	w0, [x20, 12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1184
	.loc 4 389 0 is_stmt 1
	mov	w0, w19
	bl	FtlBbmIsBadBlock
.LVL1177:
	cmp	w0, 1
	beq	.L1181
	.loc 4 391 0
	mov	w0, w19
	bl	FlashTestBlk
.LVL1178:
	cbz	w0, .L1182
	.loc 4 393 0
	mov	w0, w19
	bl	FtlBbmMapBadBlock
.LVL1179:
.L1181:
	.loc 4 387 0 discriminator 2
	sub	w19, w19, #1
	and	w19, w19, 65535
	b	.L1180
.L1177:
	.loc 4 328 0
	ldr	x0, [x19, 3376]
	.loc 4 331 0
	mov	w2, 65535
	.loc 4 329 0
	ldr	x27, [x19, 3384]
	.loc 4 331 0
	ldrh	w1, [x25]
	.loc 4 329 0
	stp	x0, x27, [x20, 8]
	.loc 4 331 0
	cmp	w1, w2
	beq	.L1172
	.loc 4 333 0
	ldrh	w23, [x19, 78]
.LVL1180:
	.loc 4 335 0
	mov	w2, 1
	.loc 4 333 0
	madd	w23, w23, w24, w1
.LVL1181:
	.loc 4 335 0
	mov	w1, w2
	.loc 4 334 0
	lsl	w0, w23, 10
	str	w0, [x20, 4]
	.loc 4 335 0
	mov	x0, x20
	bl	FlashReadPages
.LVL1182:
	.loc 4 336 0
	ldr	x1, [x20, 8]
	ldr	x0, [x26]
	ldrh	w2, [x19, 78]
	add	w2, w2, 7
	lsr	w2, w2, 3
	bl	ftl_memcpy
.LVL1183:
.L1173:
	.loc 4 376 0 discriminator 2
	mov	w0, w23
	.loc 4 327 0 discriminator 2
	add	w24, w24, 1
	.loc 4 376 0 discriminator 2
	bl	FtlBbmMapBadBlock
.LVL1184:
	add	x26, x26, 8
	add	x25, x25, 2
	b	.L1171
.L1172:
	.loc 4 340 0
	mov	w1, w24
	bl	FlashGetBadBlockList
.LVL1185:
	.loc 4 341 0
	ldr	x0, [x20, 8]
	ldr	x1, [x26]
	bl	FtlBbt2Bitmap
.LVL1186:
	.loc 4 343 0
	ldrh	w22, [x19, 78]
.L1175:
	.loc 4 347 0
	sub	w22, w22, #1
	and	w22, w22, 65535
.L1174:
	.loc 4 345 0
	ldrh	w0, [x19, 78]
	madd	w0, w24, w0, w22
	bl	FtlBbmIsBadBlock
.LVL1187:
	cmp	w0, 1
	beq	.L1175
	.loc 4 350 0
	ldr	x0, [x19, 3384]
	mov	w2, 16
	.loc 4 349 0
	strh	w22, [x25]
	.loc 4 350 0
	mov	w1, 0
	bl	ftl_memset
.LVL1188:
	.loc 4 351 0
	ldr	x0, [x19, 3376]
	mov	w2, 4096
	mov	w1, 0
	bl	ftl_memset
.LVL1189:
	.loc 4 353 0
	str	wzr, [x27, 4]
	.loc 4 352 0
	mov	w0, -3872
	strh	w0, [x27]
	.loc 4 354 0
	ldrh	w0, [x25]
	strh	w0, [x27, 2]
	.loc 4 355 0
	ldrh	w23, [x19, 78]
.LVL1190:
	ldrh	w0, [x25]
	.loc 4 357 0
	ldr	x1, [x26]
	ldrh	w2, [x28, -256]
	.loc 4 355 0
	madd	w23, w23, w24, w0
.LVL1191:
	.loc 4 357 0
	lsl	w2, w2, 2
	.loc 4 356 0
	lsl	w0, w23, 10
	str	w0, [x20, 4]
	.loc 4 357 0
	ldr	x0, [x20, 8]
	bl	ftl_memcpy
.LVL1192:
	.loc 4 358 0
	mov	w2, 1
	mov	x0, x20
	mov	w1, w2
	bl	FlashEraseBlocks
.LVL1193:
	.loc 4 359 0
	mov	w3, 1
	mov	x0, x20
	mov	w2, w3
	mov	w1, w3
	bl	FlashProgPages
.LVL1194:
	.loc 4 360 0
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L1173
	.loc 4 362 0
	mov	w0, w23
	bl	FtlBbmMapBadBlock
.LVL1195:
	.loc 4 363 0
	b	.L1174
.L1179:
	.loc 4 383 0 discriminator 3
	mov	w0, w19
	.loc 4 381 0 discriminator 3
	add	w19, w19, 1
	.loc 4 383 0 discriminator 3
	bl	FtlBbmMapBadBlock
.LVL1196:
	.loc 4 381 0 discriminator 3
	and	w19, w19, 65535
	b	.L1178
.L1182:
	.loc 4 396 0
	ldrh	w0, [x20]
	cmp	w0, w22
	bne	.L1183
	.loc 4 398 0
	strh	w19, [x20]
	b	.L1181
.L1183:
	.loc 4 402 0
	strh	w19, [x20, 4]
.L1184:
	.loc 4 411 0
	add	x21, x21, :lo12:.LANCHOR0
	.loc 4 416 0
	mov	w2, 2
	.loc 4 414 0
	ldr	x1, [x21, 3904]
	ldrh	w0, [x21, 136]
	.loc 4 413 0
	str	wzr, [x21, 144]
	.loc 4 411 0
	strh	wzr, [x21, 138]
	.loc 4 414 0
	lsl	w0, w0, 10
	str	w0, [x1, 4]
	.loc 4 415 0
	ldr	x0, [x21, 3904]
	ldrh	w1, [x21, 140]
	lsl	w1, w1, 10
	str	w1, [x0, 36]
	.loc 4 416 0
	mov	w1, 1
	bl	FlashEraseBlocks
.LVL1197:
	.loc 4 418 0
	ldrh	w0, [x21, 136]
	bl	FtlBbmMapBadBlock
.LVL1198:
	.loc 4 419 0
	ldrh	w0, [x21, 140]
	bl	FtlBbmMapBadBlock
.LVL1199:
	.loc 4 421 0
	bl	FtlBbmTblFlush
.LVL1200:
	.loc 4 426 0
	strh	wzr, [x21, 138]
	.loc 4 422 0
	ldr	w0, [x21, 144]
	.loc 4 425 0
	ldrh	w1, [x21, 140]
	.loc 4 422 0
	add	w0, w0, 1
	str	w0, [x21, 144]
	.loc 4 424 0
	ldrh	w0, [x21, 136]
	.loc 4 427 0
	strh	w0, [x21, 140]
	.loc 4 425 0
	strh	w1, [x21, 136]
	.loc 4 429 0
	bl	FtlBbmTblFlush
.LVL1201:
	.loc 4 432 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL1202:
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE170:
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
.LFB202:
	.loc 2 835 0
	.cfi_startproc
.LVL1203:
	stp	x29, x30, [sp, -208]!
	.cfi_def_cfa_offset 208
	.cfi_offset 29, -208
	.cfi_offset 30, -200
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -160
	.cfi_offset 24, -152
	.loc 2 843 0
	adrp	x23, .LANCHOR0
	.loc 2 835 0
	stp	x25, x26, [sp, 64]
	.cfi_offset 25, -144
	.cfi_offset 26, -136
	mov	x26, x2
	stp	x27, x28, [sp, 80]
	.loc 2 843 0
	add	w2, w0, w1
.LVL1204:
	.cfi_offset 27, -128
	.cfi_offset 28, -120
	.loc 2 835 0
	mov	w28, w0
	.loc 2 843 0
	add	x0, x23, :lo12:.LANCHOR0
.LVL1205:
	.loc 2 835 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -176
	.cfi_offset 22, -168
	mov	w21, w1
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -192
	.cfi_offset 20, -184
	.loc 2 843 0
	ldrh	w1, [x0, 76]
.LVL1206:
	cmp	w2, w1
	bhi	.L1198
	.loc 2 840 0
	ldrh	w22, [x0, 88]
	.loc 2 861 0
	adrp	x24, .LANCHOR3
	add	x24, x24, :lo12:.LANCHOR3
	.loc 2 855 0
	add	x27, x29, 144
	.loc 2 861 0
	sub	x24, x24, #176
	.loc 2 840 0
	mov	w25, 0
	lsr	w22, w28, w22
.LVL1207:
.L1192:
	.loc 2 846 0
	cbnz	w21, .L1197
.L1190:
	.loc 2 869 0
	mov	w0, w25
	ldp	x19, x20, [sp, 16]
.LVL1208:
	ldp	x21, x22, [sp, 32]
.LVL1209:
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
.LVL1210:
	ldp	x27, x28, [sp, 80]
.LVL1211:
	ldp	x29, x30, [sp], 208
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1212:
.L1197:
	.cfi_restore_state
	.loc 2 847 0
	add	x2, x23, :lo12:.LANCHOR0
	.loc 2 848 0
	ldrh	w1, [x2, 38]
	.loc 2 847 0
	ldr	x0, [x2, 4064]
	.loc 2 848 0
	udiv	w20, w28, w1
	.loc 2 847 0
	ldr	w3, [x0, w22, uxtw 2]
	.loc 2 851 0
	and	w0, w21, 65535
.LVL1213:
	.loc 2 848 0
	msub	w20, w20, w1, w28
	.loc 2 849 0
	sub	w19, w1, w20
.LVL1214:
	and	w19, w19, 65535
.LVL1215:
	.loc 2 851 0
	cmp	w21, w19
	csel	w19, w0, w19, cc
.LVL1216:
	.loc 2 852 0
	cbz	w3, .L1194
	.loc 2 852 0 is_stmt 0 discriminator 1
	cmp	w19, w1
	beq	.L1194
	.loc 2 854 0 is_stmt 1
	ldr	x0, [x2, 3992]
	.loc 2 856 0
	mov	w2, 1
	.loc 2 855 0
	stp	x0, x27, [x29, 120]
	.loc 2 856 0
	mov	w1, w2
	.loc 2 853 0
	str	w3, [x29, 116]
	.loc 2 856 0
	add	x0, x29, 112
	bl	FlashReadPages
.LVL1217:
.L1195:
	.loc 2 860 0
	add	x4, x23, :lo12:.LANCHOR0
	lsl	w3, w19, 9
	lsl	w20, w20, 9
	mov	w2, w3
	asr	w20, w20, 2
	mov	x1, x26
	ldr	x0, [x4, 3992]
	.loc 2 864 0
	sub	w21, w21, w19
	.loc 2 860 0
	str	w3, [x29, 108]
	.loc 2 865 0
	add	w28, w28, w19
	.loc 2 860 0
	str	x4, [x29, 96]
	add	x0, x0, x20, sxtw 2
	bl	ftl_memcpy
.LVL1218:
	.loc 2 861 0
	ldr	x4, [x29, 96]
	mov	w1, w22
	mov	x0, x24
	.loc 2 863 0
	add	w22, w22, 1
	.loc 2 861 0
	ldr	x2, [x4, 3992]
	bl	FtlMapWritePage
.LVL1219:
	.loc 2 862 0
	cmn	w0, #1
	.loc 2 866 0
	ldr	w3, [x29, 108]
	.loc 2 862 0
	csinv	w25, w25, wzr, ne
	.loc 2 866 0
	add	x26, x26, x3, sxtw
	b	.L1192
.LVL1220:
.L1194:
	.loc 2 858 0
	add	x0, x23, :lo12:.LANCHOR0
.LVL1221:
	mov	w1, 0
	ldrh	w2, [x0, 90]
	ldr	x0, [x0, 3992]
	bl	ftl_memset
.LVL1222:
	b	.L1195
.LVL1223:
.L1198:
	.loc 2 844 0
	mov	w25, -1
	b	.L1190
	.cfi_endproc
.LFE202:
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
.LFB225:
	.loc 2 2159 0
	.cfi_startproc
	.loc 2 2160 0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	ldr	w3, [x0, 2696]
	cmp	w3, w1
	bne	.L1206
	.loc 2 2159 0
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x2, x0, 2696
.LBB298:
.LBB299:
	.loc 2 2161 0
	mov	w1, 68
.LBE299:
.LBE298:
	.loc 2 2159 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
.LBB302:
.LBB300:
	.loc 2 2161 0
	movk	w1, 0x5000, lsl 16
.LBE300:
.LBE302:
	.loc 2 2159 0
.LBB303:
.LBB301:
	.loc 2 2161 0
	str	w1, [x2, 4]
	.loc 2 2162 0
	ldr	w1, [x0, 2588]
	str	w1, [x2, 88]
	.loc 2 2163 0
	ldr	w1, [x0, 2592]
	str	w1, [x2, 92]
	.loc 2 2164 0
	ldr	w1, [x0, 2584]
	str	w1, [x2, 8]
	.loc 2 2165 0
	ldr	w1, [x0, 2572]
	str	w1, [x2, 12]
	.loc 2 2166 0
	ldr	w1, [x0, 2564]
	str	w1, [x2, 16]
	.loc 2 2167 0
	ldr	w1, [x0, 2580]
	str	w1, [x2, 20]
	.loc 2 2168 0
	ldr	w1, [x0, 2608]
	str	w1, [x2, 28]
	.loc 2 2169 0
	ldr	w1, [x0, 2616]
	str	w1, [x2, 32]
	.loc 2 2170 0
	ldr	w1, [x0, 2568]
	str	w1, [x2, 36]
	.loc 2 2171 0
	ldr	w1, [x0, 2576]
	str	w1, [x2, 40]
	.loc 2 2172 0
	ldr	w1, [x0, 2620]
	.loc 2 2173 0
	ldr	w0, [x0, 2624]
	stp	w1, w0, [x2, 44]
	.loc 2 2174 0
	mov	w1, 1
	mov	w0, 0
	bl	FtlVendorPartWrite
.LVL1224:
.LBE301:
.LBE303:
	.loc 2 2176 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
.L1206:
	ret
	.cfi_endproc
.LFE225:
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
.LFB204:
	.loc 2 910 0
	.cfi_startproc
.LVL1225:
	.loc 2 913 0
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	ldrh	w2, [x1, 48]
	cmp	w2, 31
	bhi	.L1213
	.loc 2 914 0
	add	w2, w2, 1
	strh	w2, [x1, 48]
	.loc 2 915 0
	mov	w2, 1
.LVL1226:
.L1210:
	adrp	x1, .LANCHOR0
	.loc 2 917 0
	cbnz	w0, .L1211
	.loc 2 917 0 is_stmt 0 discriminator 1
	add	x0, x1, :lo12:.LANCHOR0
.LVL1227:
	ldr	x0, [x0, 4016]
	ldr	w3, [x0, 20]
	ldr	w0, [x0, 16]
	add	w2, w2, w3
.LVL1228:
	cmp	w0, w2
	bcc	.L1215
.LVL1229:
.L1211:
	.loc 2 919 0 is_stmt 1
	add	x0, x1, :lo12:.LANCHOR0
.LVL1230:
	.loc 2 910 0
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 2 919 0
	ldr	x1, [x0, 4016]
	ldr	w2, [x1, 16]
.LVL1231:
	str	w2, [x1, 20]
	.loc 2 920 0
	mov	w2, 17221
	movk	w2, 0x4254, lsl 16
	str	w2, [x1]
	.loc 2 921 0
	ldr	x2, [x0, 4016]
	ldrh	w1, [x0, 4000]
	lsl	w3, w1, 9
	.loc 2 923 0
	str	wzr, [x2, 4]
	.loc 2 921 0
	str	w3, [x2, 12]
	.loc 2 922 0
	ldr	w3, [x2, 8]
	add	w3, w3, 1
	str	w3, [x2, 8]
	.loc 2 924 0
	ldr	x2, [x0, 4016]
	mov	w0, 64
	bl	FtlVendorPartWrite
.LVL1232:
	.loc 2 925 0
	bl	Ftl_save_ext_data
.LVL1233:
	.loc 2 927 0
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
.LVL1234:
.L1213:
	.loc 2 911 0
	mov	w2, 32
.LVL1235:
	b	.L1210
.LVL1236:
.L1215:
	.loc 2 927 0
	mov	w0, 0
	ret
	.cfi_endproc
.LFE204:
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
.LFB203:
	.loc 2 872 0
	.cfi_startproc
.LVL1237:
	stp	x29, x30, [sp, -192]!
	.cfi_def_cfa_offset 192
	.cfi_offset 29, -192
	.cfi_offset 30, -184
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -176
	.cfi_offset 20, -168
	.loc 2 881 0
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	.loc 2 872 0
	stp	x27, x28, [sp, 80]
	stp	x21, x22, [sp, 32]
	.cfi_offset 27, -112
	.cfi_offset 28, -104
	.cfi_offset 21, -160
	.cfi_offset 22, -152
	mov	w28, w0
	mov	w27, w1
	stp	x23, x24, [sp, 48]
	.loc 2 881 0
	add	w1, w0, w1
.LVL1238:
	.loc 2 872 0
	stp	x25, x26, [sp, 64]
	.cfi_offset 23, -144
	.cfi_offset 24, -136
	.cfi_offset 25, -128
	.cfi_offset 26, -120
	.loc 2 881 0
	ldrh	w0, [x20, 76]
.LVL1239:
	cmp	w1, w0
	bhi	.L1225
	.loc 2 877 0
	ldrh	w3, [x20, 88]
	mov	x23, x2
	mov	w22, 0
	.loc 2 895 0
	mov	w25, -1
	.loc 2 877 0
	lsr	w24, w28, w3
.LVL1240:
.L1219:
	.loc 2 883 0
	cbnz	w27, .L1224
.L1217:
	.loc 2 906 0
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
.LVL1241:
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL1242:
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
.LVL1243:
	ldp	x29, x30, [sp], 192
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1244:
.L1224:
	.cfi_restore_state
	.loc 2 885 0
	ldrh	w19, [x20, 38]
.LVL1245:
	.loc 2 884 0
	ldr	x0, [x20, 4064]
	.loc 2 885 0
	udiv	w21, w28, w19
	.loc 2 884 0
	ldr	w1, [x0, w24, uxtw 2]
	.loc 2 888 0
	and	w0, w27, 65535
.LVL1246:
	.loc 2 885 0
	msub	w21, w21, w19, w28
	.loc 2 886 0
	sub	w19, w19, w21
	and	w19, w19, 65535
.LVL1247:
	.loc 2 888 0
	cmp	w27, w19
	csel	w19, w0, w19, cc
	lsl	w26, w19, 9
.LVL1248:
	.loc 2 889 0
	cbz	w1, .L1221
	.loc 2 891 0
	ldr	x0, [x20, 3992]
	.loc 2 893 0
	mov	w2, 1
	.loc 2 891 0
	str	x0, [x29, 104]
	.loc 2 892 0
	add	x0, x29, 128
	.loc 2 890 0
	str	w1, [x29, 100]
	.loc 2 893 0
	mov	w1, w2
.LVL1249:
	.loc 2 892 0
	str	x0, [x29, 112]
	.loc 2 893 0
	add	x0, x29, 96
	bl	FlashReadPages
.LVL1250:
	.loc 2 896 0
	lsl	w21, w21, 9
	.loc 2 894 0
	ldr	w0, [x29, 96]
	.loc 2 896 0
	asr	w21, w21, 2
	ldr	x1, [x20, 3992]
	mov	w2, w26
	.loc 2 895 0
	cmn	w0, #1
	.loc 2 896 0
	mov	x0, x23
	.loc 2 895 0
	csel	w22, w22, w25, ne
	.loc 2 896 0
	add	x1, x1, x21, sxtw 2
	bl	ftl_memcpy
.LVL1251:
.L1223:
	.loc 2 900 0
	add	w24, w24, 1
	.loc 2 901 0
	sub	w27, w27, w19
	.loc 2 902 0
	add	w28, w28, w19
	.loc 2 903 0
	add	x23, x23, x26, sxtw
	b	.L1219
.LVL1252:
.L1221:
	.loc 2 898 0
	mov	w2, w26
	mov	w1, 0
.LVL1253:
	mov	x0, x23
.LVL1254:
	bl	ftl_memset
.LVL1255:
	b	.L1223
.LVL1256:
.L1225:
	.loc 2 882 0
	mov	w22, -1
	b	.L1217
	.cfi_endproc
.LFE203:
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlLoadEctTbl
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
.LFB210:
	.loc 2 1383 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 2 1384 0
	mov	w0, 64
	.loc 2 1383 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 2 1384 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19, 4000]
	ldr	x2, [x19, 4016]
	bl	FtlVendorPartRead
.LVL1257:
	.loc 2 1385 0
	ldr	x0, [x19, 4016]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1228
.LBB306:
.LBB307:
	.loc 2 1387 0
	adrp	x1, .LC99
	adrp	x0, .LC71
	add	x1, x1, :lo12:.LC99
	add	x0, x0, :lo12:.LC71
	bl	printk
.LVL1258:
	.loc 2 1388 0
	ldr	x0, [x19, 4016]
	mov	w1, 0
	ldrh	w2, [x19, 4000]
	lsl	w2, w2, 9
	bl	ftl_memset
.LVL1259:
.L1228:
.LBE307:
.LBE306:
	.loc 2 1393 0
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE210:
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
.LFB226:
	.loc 2 2179 0
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	.loc 2 2180 0
	mov	w1, 1
	mov	w0, 0
	.loc 2 2179 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	.loc 2 2180 0
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	.loc 2 2179 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -16
	.cfi_offset 22, -8
	.loc 2 2180 0
	add	x22, x20, 2696
	.loc 2 2181 0
	mov	w21, 19539
	.loc 2 2180 0
	mov	x2, x22
	bl	FtlVendorPartRead
.LVL1260:
	.loc 2 2181 0
	ldr	w0, [x20, 2696]
	movk	w21, 0x4654, lsl 16
	cmp	w0, w21
	beq	.L1231
	.loc 2 2182 0
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
.LVL1261:
	.loc 2 2183 0
	str	w21, [x20, 2696]
.L1231:
	.loc 2 2186 0
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0, 2696]
	cmp	w3, w2
	bne	.L1232
	.loc 2 2187 0
	ldr	w2, [x0, 2784]
	str	w2, [x0, 2588]
	.loc 2 2188 0
	ldr	w2, [x0, 2788]
	str	w2, [x0, 2592]
	.loc 2 2189 0
	ldr	w2, [x0, 2704]
	str	w2, [x0, 2584]
	.loc 2 2190 0
	ldr	w2, [x0, 2708]
	str	w2, [x0, 2572]
	.loc 2 2191 0
	ldr	w2, [x0, 2712]
	str	w2, [x0, 2564]
	.loc 2 2192 0
	ldr	w2, [x0, 2716]
	str	w2, [x0, 2580]
	.loc 2 2193 0
	ldr	w2, [x0, 2724]
	str	w2, [x0, 2608]
	.loc 2 2194 0
	ldr	w2, [x0, 2728]
	str	w2, [x0, 2616]
	.loc 2 2195 0
	ldr	w2, [x0, 2732]
	str	w2, [x0, 2568]
	.loc 2 2196 0
	ldr	w2, [x0, 2736]
	.loc 2 2198 0
	ldr	w1, [x0, 2744]
	.loc 2 2196 0
	str	w2, [x0, 2576]
	.loc 2 2197 0
	ldr	w2, [x0, 2740]
	.loc 2 2198 0
	str	w1, [x0, 2624]
	.loc 2 2197 0
	str	w2, [x0, 2620]
.L1232:
	.loc 2 2201 0
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x0, 2608]
	ldrh	w1, [x0, 72]
	ldr	w3, [x0, 2604]
	madd	w1, w1, w3, w2
	ldrh	w2, [x0, 20]
	udiv	w1, w1, w2
	str	w1, [x0, 2612]
	.loc 2 2202 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE226:
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
.LFB208:
	.loc 2 1053 0
	.cfi_startproc
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	.loc 2 1059 0
	adrp	x22, .LANCHOR0
	add	x19, x22, :lo12:.LANCHOR0
	.loc 2 1053 0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	.cfi_offset 25, -16
	.cfi_offset 26, -8
	.loc 2 1068 0
	add	x20, x19, 2288
	.loc 2 1059 0
	add	x25, x19, 3344
	.loc 2 1056 0
	mov	w23, 0
.LVL1262:
	.loc 2 1060 0
	ldr	x21, [x19, 3384]
	.loc 2 1054 0
	mov	w24, 65535
.LVL1263:
	.loc 2 1059 0
	ldr	x0, [x19, 3376]
	.loc 2 1062 0
	ldrh	w1, [x19, 2632]
	.loc 2 1059 0
	str	x0, [x19, 3352]
	.loc 2 1060 0
	str	x21, [x19, 3360]
	.loc 2 1062 0
	strh	w1, [x21, 2]
	.loc 2 1063 0
	mov	w1, -3932
	strh	w1, [x21]
	.loc 2 1066 0
	str	wzr, [x21, 12]
	.loc 2 1064 0
	ldr	w1, [x19, 2640]
	.loc 2 1065 0
	stp	w1, wzr, [x21, 4]
	.loc 2 1068 0
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	str	w1, [x19, 2288]
	.loc 2 1069 0
	mov	w1, 68
	.loc 2 1074 0
	ldrb	w2, [x19, 2406]
	.loc 2 1069 0
	movk	w1, 0x5000, lsl 16
	str	w1, [x19, 2292]
	.loc 2 1073 0
	ldrh	w1, [x19, 2400]
	strh	w1, [x19, 2302]
	.loc 2 1074 0
	ldrh	w1, [x19, 2402]
	.loc 2 1070 0
	ldrh	w0, [x19, 2638]
	strh	w0, [x19, 2296]
	.loc 2 1071 0
	ldrh	w0, [x19, 34]
	.loc 2 1074 0
	orr	w1, w2, w1, lsl 6
	.loc 2 1071 0
	strb	w0, [x19, 2298]
	.loc 2 1074 0
	strh	w1, [x19, 2304]
	.loc 2 1077 0
	ldrh	w1, [x19, 2448]
	.loc 2 1078 0
	ldrb	w2, [x19, 2454]
	.loc 2 1077 0
	strh	w1, [x19, 2306]
	.loc 2 1078 0
	ldrh	w1, [x19, 2450]
	.loc 2 1075 0
	ldrb	w0, [x19, 2408]
	strb	w0, [x19, 2299]
	.loc 2 1078 0
	orr	w1, w2, w1, lsl 6
	.loc 2 1079 0
	ldrb	w0, [x19, 2456]
	.loc 2 1078 0
	strh	w1, [x19, 2308]
	.loc 2 1079 0
	strb	w0, [x19, 2300]
	.loc 2 1081 0
	ldrh	w1, [x19, 2496]
	.loc 2 1083 0
	ldrb	w0, [x19, 2504]
	.loc 2 1082 0
	ldrb	w2, [x19, 2502]
	.loc 2 1081 0
	strh	w1, [x19, 2310]
	.loc 2 1083 0
	strb	w0, [x19, 2301]
	.loc 2 1082 0
	ldrh	w1, [x19, 2498]
	.loc 2 1085 0
	ldr	w0, [x19, 2604]
	str	w0, [x19, 2320]
	.loc 2 1090 0
	ldr	w0, [x19, 2596]
	.loc 2 1082 0
	orr	w1, w2, w1, lsl 6
	.loc 2 1090 0
	str	w0, [x19, 2328]
	.loc 2 1091 0
	ldr	w0, [x19, 2600]
	.loc 2 1096 0
	ldrh	w2, [x19, 90]
	.loc 2 1091 0
	str	w0, [x19, 2324]
	.loc 2 1096 0
	ldr	x0, [x19, 3352]
	.loc 2 1082 0
	strh	w1, [x19, 2312]
	.loc 2 1096 0
	mov	w1, 255
	bl	ftl_memset
.LVL1264:
	.loc 2 1097 0
	ldr	x0, [x19, 3352]
	mov	x1, x20
	mov	w2, 48
	.loc 2 1102 0
	mov	x20, x25
	.loc 2 1124 0
	adrp	x25, .LANCHOR1
	add	x25, x25, :lo12:.LANCHOR1
	add	x25, x25, 536
	.loc 2 1097 0
	bl	ftl_memcpy
.LVL1265:
	.loc 2 1098 0
	ldr	x1, [x19, 2352]
	ldrh	w2, [x19, 20]
	ldr	x0, [x19, 3352]
	lsl	w2, w2, 1
	add	x0, x0, 48
	bl	ftl_memcpy
.LVL1266:
	.loc 2 1099 0
	ldrh	w0, [x19, 20]
	ldr	x3, [x19, 3352]
	ldr	x1, [x22, #:lo12:.LANCHOR0]
	lsr	w2, w0, 3
	ubfiz	x0, x0, 1, 16
	add	x0, x0, 48
	add	w2, w2, 4
	and	x0, x0, -4
	add	x0, x3, x0
	bl	ftl_memcpy
.LVL1267:
	.loc 2 1100 0
	mov	w0, 0
	bl	FtlUpdateVaildLpn
.LVL1268:
.L1235:
	.loc 2 1102 0
	ldr	x0, [x19, 3376]
	.loc 2 1105 0
	mov	w3, 1
	.loc 2 1102 0
	str	x0, [x20, 8]
	.loc 2 1105 0
	mov	w2, w3
	.loc 2 1103 0
	ldr	x0, [x19, 3384]
	.loc 2 1104 0
	ldrh	w1, [x19, 2634]
	.loc 2 1103 0
	str	x0, [x20, 16]
	.loc 2 1104 0
	ldrh	w0, [x19, 2632]
	orr	w0, w1, w0, lsl 10
	.loc 2 1105 0
	mov	w1, w3
	.loc 2 1104 0
	str	w0, [x20, 4]
	.loc 2 1105 0
	mov	x0, x20
	bl	FlashProgPages
.LVL1269:
	.loc 2 1106 0
	ldrh	w0, [x19, 84]
	ldrh	w1, [x19, 2634]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L1236
	.loc 2 1108 0
	ldrh	w0, [x19, 2632]
	.loc 2 1107 0
	ldrh	w24, [x19, 2636]
	.loc 2 1109 0
	strh	wzr, [x19, 2634]
	.loc 2 1108 0
	strh	w0, [x19, 2636]
	.loc 2 1110 0
	bl	FtlFreeSysBlkQueueOut
.LVL1270:
	strh	w0, [x19, 2632]
	.loc 2 1115 0
	ldr	w1, [x19, 2596]
	.loc 2 1119 0
	mov	w3, 1
	.loc 2 1115 0
	str	w1, [x19, 2640]
	add	w2, w1, 1
	str	w2, [x19, 2596]
	.loc 2 1116 0
	ubfiz	w2, w0, 10, 16
	str	w2, [x20, 4]
	.loc 2 1118 0
	strh	w0, [x21, 2]
	.loc 2 1119 0
	mov	w2, w3
	.loc 2 1117 0
	str	w1, [x21, 4]
	.loc 2 1119 0
	mov	x0, x20
	mov	w1, w3
	bl	FlashProgPages
.LVL1271:
.L1236:
	.loc 2 1121 0
	ldrh	w0, [x19, 2634]
	.loc 2 1122 0
	ldr	w1, [x20]
	.loc 2 1121 0
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x19, 2634]
	.loc 2 1122 0
	cmn	w1, #1
	bne	.L1237
	.loc 2 1124 0
	cmp	w0, 1
	bne	.L1238
	.loc 2 1124 0 is_stmt 0 discriminator 1
	adrp	x0, .LC1
	mov	w2, 1124
	mov	x1, x25
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1272:
.L1238:
	.loc 2 1125 0 is_stmt 1
	ldrh	w0, [x19, 2634]
	cmp	w0, 1
	bne	.L1239
	.loc 2 1126 0
	ldrh	w0, [x19, 84]
	sub	w0, w0, #1
	strh	w0, [x19, 2634]
.L1239:
	.loc 2 1127 0
	add	w23, w23, 1
.LVL1273:
	and	w23, w23, 65535
.LVL1274:
	.loc 2 1128 0
	cmp	w23, 3
	bls	.L1235
	.loc 2 1129 0
	add	x22, x22, :lo12:.LANCHOR0
	adrp	x0, .LC100
	mov	w2, w23
	add	x0, x0, :lo12:.LC100
	ldr	w1, [x22, 3348]
	bl	printk
.LVL1275:
.L1241:
	b	.L1241
.L1237:
	.loc 2 1135 0
	cmp	w0, 1
	beq	.L1235
	.loc 2 1138 0
	mov	w0, 65535
	cmp	w24, w0
	beq	.L1242
	.loc 2 1139 0
	mov	w1, 1
	mov	w0, w24
	bl	FtlFreeSysBlkQueueIn
.LVL1276:
.L1242:
	.loc 2 1141 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL1277:
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE208:
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
.LFB228:
	.loc 2 2242 0
	.cfi_startproc
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	.loc 2 2247 0
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC90
	.loc 2 2242 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.loc 2 2248 0
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	.loc 2 2242 0
	stp	x21, x22, [sp, 32]
	.loc 2 2247 0
	add	x1, x1, 552
	add	x0, x0, :lo12:.LC90
	.loc 2 2242 0
	str	x23, [sp, 48]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	.cfi_offset 23, -32
	.loc 2 2247 0
	bl	printk
.LVL1278:
	.loc 2 2248 0
	ldr	x0, [x19, 4024]
	.loc 2 2249 0
	mov	w21, 0
.LVL1279:
	.loc 2 2248 0
	ldrh	w2, [x19, 22]
	mov	w1, 0
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL1280:
.L1248:
	.loc 2 2249 0 discriminator 1
	ldr	w0, [x19, 2560]
	cmp	w21, w0
	bcc	.L1250
	.loc 2 2259 0 discriminator 1
	adrp	x22, .LC101
	.loc 2 2256 0 discriminator 1
	add	x20, x20, :lo12:.LANCHOR0
	.loc 2 2259 0 discriminator 1
	add	x22, x22, :lo12:.LC101
	mov	w19, 0
.LVL1281:
	.loc 2 2257 0 discriminator 1
	mov	w23, 65535
.L1251:
	.loc 2 2256 0 discriminator 1
	ldrh	w0, [x20, 20]
	cmp	w0, w19
	bhi	.L1255
	.loc 2 2268 0
	ldp	x19, x20, [sp, 16]
.LVL1282:
	ldp	x21, x22, [sp, 32]
.LVL1283:
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1284:
.L1250:
	.cfi_restore_state
	.loc 2 2250 0
	mov	w2, 0
	add	x1, x29, 76
	mov	w0, w21
	bl	log2phys
.LVL1285:
	.loc 2 2251 0
	ldr	w0, [x29, 76]
	cmn	w0, #1
	beq	.L1249
	.loc 2 2252 0
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
.LVL1286:
	.loc 2 2253 0
	ldr	x2, [x19, 4024]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x2, x0]
	add	w1, w1, 1
	strh	w1, [x2, x0]
.L1249:
	.loc 2 2249 0 discriminator 2
	add	w21, w21, 1
	b	.L1248
.LVL1287:
.L1255:
	.loc 2 2257 0
	ldr	x0, [x20, 2352]
	ubfiz	x21, x19, 1, 16
.LVL1288:
	ldrh	w2, [x0, x21]
	ldr	x0, [x20, 4024]
	ldrh	w3, [x0, x21]
	cmp	w2, w3
	beq	.L1253
	.loc 2 2257 0 is_stmt 0 discriminator 1
	cmp	w2, w23
	beq	.L1253
	.loc 2 2259 0 is_stmt 1
	mov	w1, w19
	mov	x0, x22
	bl	printk
.LVL1289:
	.loc 2 2260 0
	ldrh	w0, [x20, 2400]
	cmp	w0, w19
	beq	.L1253
	.loc 2 2260 0 is_stmt 0 discriminator 1
	ldrh	w0, [x20, 2496]
	cmp	w0, w19
	beq	.L1253
	.loc 2 2260 0 discriminator 2
	ldrh	w0, [x20, 2448]
	cmp	w0, w19
	beq	.L1253
	.loc 2 2261 0 is_stmt 1
	ldr	x0, [x20, 4024]
	ldrh	w1, [x0, x21]
	ldr	x0, [x20, 2352]
	strh	w1, [x0, x21]
	.loc 2 2262 0
	mov	w0, w19
	bl	update_vpc_list
.LVL1290:
	.loc 2 2263 0
	bl	l2p_flush
.LVL1291:
	.loc 2 2264 0
	bl	FtlVpcTblFlush
.LVL1292:
.L1253:
	.loc 2 2256 0 discriminator 2
	add	w19, w19, 1
.LVL1293:
	and	w19, w19, 65535
.LVL1294:
	b	.L1251
	.cfi_endproc
.LFE228:
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
.LFB238:
	.loc 2 2679 0
	.cfi_startproc
.LVL1295:
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	.loc 2 2681 0
	adrp	x19, .LANCHOR0
	.loc 2 2679 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -16
	.cfi_offset 22, -8
	.loc 2 2679 0
	mov	x22, x0
	.loc 2 2680 0
	ldrh	w21, [x0]
.LVL1296:
	.loc 2 2681 0
	add	x0, x19, :lo12:.LANCHOR0
.LVL1297:
	ldrh	w0, [x0, 20]
	cmp	w0, w21
	bcs	.L1261
	.loc 2 2681 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 2681
	add	x1, x1, 576
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1298:
.L1261:
	.loc 2 2683 0 is_stmt 1
	mov	w0, 65535
	cmp	w21, w0
	beq	.L1262
	.loc 2 2684 0
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x21, 1, 16
	ldr	x1, [x1, 2352]
	ldrh	w0, [x1, x0]
	cbz	w0, .L1263
	.loc 2 2685 0
	mov	w0, w21
	bl	INSERT_DATA_LIST
.LVL1299:
.L1262:
	.loc 2 2689 0
	mov	w0, 1
	.loc 2 2690 0
	adrp	x1, .LANCHOR3
	.loc 2 2689 0
	strb	w0, [x22, 8]
	.loc 2 2690 0
	add	x0, x1, :lo12:.LANCHOR3
	mov	w2, 65535
	mov	x20, x1
	ldrh	w0, [x0, -104]
	cmp	w0, w2
	beq	.L1264
	.loc 2 2691 0
	cmp	w21, w0
	bne	.L1265
	.loc 2 2691 0 is_stmt 0 discriminator 1
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x19, 2352]
	ldrh	w1, [x2, x1]
	cbz	w1, .L1266
.L1265:
	.loc 2 2692 0 is_stmt 1
	bl	update_vpc_list
.LVL1300:
.L1266:
	.loc 2 2693 0
	add	x1, x20, :lo12:.LANCHOR3
	mov	w0, -1
	strh	w0, [x1, -104]
.L1264:
	.loc 2 2695 0
	mov	x0, x22
	bl	allocate_data_superblock
.LVL1301:
	.loc 2 2696 0
	bl	l2p_flush
.LVL1302:
	.loc 2 2697 0
	mov	w0, 0
	bl	FtlEctTblFlush
.LVL1303:
	.loc 2 2698 0
	bl	FtlVpcTblFlush
.LVL1304:
	.loc 2 2700 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL1305:
	ldp	x29, x30, [sp], 48
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1306:
.L1263:
	.cfi_restore_state
	.loc 2 2687 0
	mov	w0, w21
	bl	INSERT_FREE_LIST
.LVL1307:
	b	.L1262
	.cfi_endproc
.LFE238:
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.type	FtlProgPages, %function
FtlProgPages:
.LFB154:
	.loc 3 904 0
	.cfi_startproc
.LVL1308:
	stp	x29, x30, [sp, -96]!
	.cfi_def_cfa_offset 96
	.cfi_offset 29, -96
	.cfi_offset 30, -88
	.loc 3 911 0
	mov	w2, 0
.LVL1309:
	.loc 3 904 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -80
	.cfi_offset 20, -72
	mov	x19, x3
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -64
	.cfi_offset 22, -56
	mov	w21, w1
	stp	x23, x24, [sp, 48]
	ubfiz	x21, x21, 5, 32
	str	x25, [sp, 64]
	.cfi_offset 23, -48
	.cfi_offset 24, -40
	.cfi_offset 25, -32
	add	x21, x21, 4
	add	x24, x0, x21
	adrp	x22, .LANCHOR0
	.loc 3 911 0
	ldrb	w3, [x3, 9]
.LVL1310:
	.loc 3 933 0
	adrp	x25, .LANCHOR1
	.loc 3 917 0
	add	x21, x22, :lo12:.LANCHOR0
	.loc 3 933 0
	add	x25, x25, :lo12:.LANCHOR1
	add	x20, x0, 4
	.loc 3 911 0
	bl	FlashProgPages
.LVL1311:
.L1272:
	.loc 3 913 0 discriminator 1
	cmp	x20, x24
	beq	.L1279
	sub	x23, x20, #4
	b	.L1280
.LVL1312:
.L1275:
	.loc 3 915 0
	ldr	w0, [x20]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
.LVL1313:
	.loc 3 916 0
	ldrh	w1, [x19]
	cmp	w1, w0, uxth
	bne	.L1273
	.loc 3 917 0
	ldr	x2, [x21, 2352]
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x19, 4]
	ldrh	w0, [x2, x1]
	sub	w0, w0, w3
	strh	w0, [x2, x1]
	.loc 3 919 0
	strb	wzr, [x19, 6]
	.loc 3 918 0
	ldrh	w0, [x21, 82]
	strh	w0, [x19, 2]
	.loc 3 920 0
	strh	wzr, [x19, 4]
.L1273:
	.loc 3 922 0
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1274
	.loc 3 923 0
	mov	x0, x19
	bl	allocate_new_data_superblock
.LVL1314:
.L1274:
	.loc 3 926 0
	ldr	w0, [x21, 2792]
	add	w0, w0, 1
	str	w0, [x21, 2792]
	.loc 3 927 0
	ldr	w0, [x20]
	lsr	w0, w0, 10
	bl	FtlGcMarkBadPhyBlk
.LVL1315:
	.loc 3 928 0
	mov	x0, x19
	bl	get_new_active_ppa
.LVL1316:
	.loc 3 929 0
	str	w0, [x20]
	.loc 3 930 0
	mov	w2, 0
	.loc 3 928 0
	str	w0, [x29, 92]
	.loc 3 930 0
	mov	w1, 1
	ldrb	w3, [x19, 9]
	mov	x0, x23
	bl	FlashProgPages
.LVL1317:
.L1280:
	.loc 3 914 0
	ldr	w0, [x20, -4]
	cmn	w0, #1
	beq	.L1275
	.loc 3 933 0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x21, 12]
	cmp	w1, w0
	bcc	.L1276
	.loc 3 933 0 is_stmt 0 discriminator 1
	adrp	x0, .LC1
	mov	w2, 933
	add	x1, x25, 608
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1318:
.L1276:
	.loc 3 934 0 is_stmt 1
	add	x1, x29, 96
	ldr	w0, [x23, 4]
	.loc 3 935 0
	mov	w2, 1
	.loc 3 934 0
	str	w0, [x1, -4]!
.LVL1319:
	.loc 3 935 0
	ldr	w0, [x23, 24]
	bl	log2phys
.LVL1320:
	.loc 3 937 0
	ldr	x0, [x23, 16]
	ldr	w4, [x0, 12]
.LVL1321:
	.loc 3 939 0
	lsr	w0, w4, 10
	bl	P2V_block_in_plane
.LVL1322:
	and	w1, w0, 65535
	mov	w23, w1
	.loc 3 940 0
	cmn	w4, #1
	beq	.L1277
	.loc 3 942 0
	ldr	x2, [x21, 2352]
	ubfiz	x0, x1, 1, 16
	ldrh	w0, [x2, x0]
	cbnz	w0, .L1278
	.loc 3 943 0
	adrp	x0, .LC102
	mov	w2, 0
	add	x0, x0, :lo12:.LC102
	bl	printk
.LVL1323:
.L1278:
	.loc 3 944 0
	mov	w0, w23
	bl	decrement_vpc_count
.LVL1324:
.L1277:
	add	x20, x20, 32
	b	.L1272
.L1279:
	.loc 3 948 0
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x22, 12]
	cmp	w1, w0
	bcc	.L1271
	.loc 3 948 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 948
	add	x1, x1, 608
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1325:
.L1271:
	.loc 3 949 0 is_stmt 1
	ldp	x19, x20, [sp, 16]
.LVL1326:
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 96
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE154:
	.size	FtlProgPages, .-FtlProgPages
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
.LFB155:
	.loc 3 952 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 3 953 0
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	ldr	x0, [x19, -112]
	cbz	x0, .L1287
	.loc 3 954 0
	ldr	x3, [x19, 56]
	mov	w2, 0
	mov	w1, 1
	bl	FtlProgPages
.LVL1327:
	.loc 3 955 0
	str	xzr, [x19, -112]
.L1287:
	.loc 3 958 0
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE155:
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
.LFB151:
	.loc 3 797 0
	.cfi_startproc
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 3 798 0
	bl	FtlCacheWriteBack
.LVL1328:
	.loc 3 799 0
	bl	l2p_flush
.LVL1329:
	.loc 3 800 0
	mov	w0, 1
	bl	FtlEctTblFlush
.LVL1330:
	.loc 3 801 0
	bl	FtlVpcTblFlush
.LVL1331:
	.loc 3 803 0
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE151:
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	sftl_deinit
	.type	sftl_deinit, %function
sftl_deinit:
.LFB152:
	.loc 3 806 0
	.cfi_startproc
	.loc 3 807 0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	cmp	w0, 1
	bne	.L1297
	.loc 3 806 0
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 3 808 0
	bl	FtlSysFlush
.LVL1332:
	.loc 3 810 0
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
.L1297:
	mov	w0, 0
	ret
	.cfi_endproc
.LFE152:
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
.LFB157:
	.loc 3 1157 0
	.cfi_startproc
.LVL1333:
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	.loc 3 1164 0
	adrp	x20, .LANCHOR0
	.loc 3 1157 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.loc 3 1164 0
	add	x22, x20, :lo12:.LANCHOR0
	.loc 3 1157 0
	mov	w21, w0
	mov	w19, w1
	.loc 3 1164 0
	add	w1, w0, w1
.LVL1334:
	ldr	w0, [x22, 120]
.LVL1335:
	cmp	w1, w0
	bhi	.L1308
	.loc 3 1167 0
	cmp	w19, 31
	bhi	.L1302
.LVL1336:
.L1316:
	.loc 3 1200 0
	mov	w0, 0
.L1300:
	.loc 3 1201 0
	ldp	x19, x20, [sp, 16]
.LVL1337:
	ldp	x21, x22, [sp, 32]
.LVL1338:
	ldp	x29, x30, [sp], 64
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1339:
.L1302:
	.cfi_restore_state
	.loc 3 1170 0
	bl	FtlCacheWriteBack
.LVL1340:
	.loc 3 1172 0
	ldrh	w1, [x22, 38]
	udiv	w22, w21, w1
.LVL1341:
	.loc 3 1173 0
	msub	w21, w1, w22, w21
.LVL1342:
	.loc 3 1174 0
	ands	w21, w21, 65535
.LVL1343:
	beq	.L1303
	.loc 3 1175 0
	sub	w1, w1, w21
	.loc 3 1176 0
	add	w22, w22, 1
	.loc 3 1175 0
	cmp	w1, w19
	csel	w1, w1, w19, ls
	.loc 3 1177 0
	sub	w19, w19, w1, uxth
.L1303:
	.loc 3 1184 0
	adrp	x21, .LANCHOR3
.LVL1344:
	.loc 3 1181 0
	add	x20, x20, :lo12:.LANCHOR0
	.loc 3 1184 0
	add	x21, x21, :lo12:.LANCHOR3
	.loc 3 1180 0
	mov	w0, -1
	str	w0, [x29, 60]
.LVL1345:
.L1304:
	.loc 3 1181 0
	ldrh	w0, [x20, 38]
	cmp	w19, w0
	bcs	.L1306
	.loc 3 1194 0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	w1, [x0, 64]
	cmp	w1, 32
	bls	.L1316
	.loc 3 1196 0
	str	wzr, [x0, 64]
	.loc 3 1197 0
	bl	l2p_flush
.LVL1346:
	.loc 3 1198 0
	bl	FtlVpcTblFlush
.LVL1347:
	b	.L1316
.L1306:
	.loc 3 1182 0
	mov	w2, 0
	add	x1, x29, 56
	mov	w0, w22
	bl	log2phys
.LVL1348:
	.loc 3 1183 0
	ldr	w0, [x29, 56]
	cmn	w0, #1
	beq	.L1305
	.loc 3 1184 0
	ldr	w0, [x21, 64]
	.loc 3 1186 0
	mov	w2, 1
	add	x1, x29, 60
	.loc 3 1184 0
	add	w0, w0, 1
	str	w0, [x21, 64]
	.loc 3 1185 0
	ldr	w0, [x20, 2568]
	add	w0, w0, 1
	str	w0, [x20, 2568]
	.loc 3 1186 0
	mov	w0, w22
	bl	log2phys
.LVL1349:
	.loc 3 1187 0
	ldr	w0, [x29, 56]
.LVL1350:
	lsr	w0, w0, 10
.LVL1351:
	bl	P2V_block_in_plane
.LVL1352:
	.loc 3 1188 0
	bl	decrement_vpc_count
.LVL1353:
.L1305:
	.loc 3 1191 0
	ldrh	w0, [x20, 38]
	.loc 3 1190 0
	add	w22, w22, 1
	.loc 3 1191 0
	sub	w19, w19, w0
	b	.L1304
.LVL1354:
.L1308:
	.loc 3 1165 0
	mov	w0, -1
	b	.L1300
	.cfi_endproc
.LFE157:
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
.LFB247:
	.loc 5 112 0
	.cfi_startproc
.LVL1355:
	stp	x29, x30, [sp, -96]!
	.cfi_def_cfa_offset 96
	.cfi_offset 29, -96
	.cfi_offset 30, -88
	.loc 5 119 0
	mov	w3, 65535
	.loc 5 112 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -80
	.cfi_offset 20, -72
	.loc 5 115 0
	adrp	x19, .LANCHOR0
	add	x2, x19, :lo12:.LANCHOR0
	.loc 5 112 0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	.cfi_offset 21, -64
	.cfi_offset 22, -56
	.cfi_offset 23, -48
	.cfi_offset 24, -40
	.cfi_offset 25, -32
	.cfi_offset 26, -24
	.loc 5 119 0
	ldrh	w20, [x2, 2496]
	.loc 5 115 0
	ldrh	w1, [x2, 82]
.LVL1356:
	.loc 5 119 0
	cmp	w20, w3
	bne	.L1318
.LVL1357:
.L1328:
	.loc 5 153 0
	add	x20, x19, :lo12:.LANCHOR0
	.loc 5 152 0
	adrp	x0, .LANCHOR3-96
.LVL1358:
	.loc 5 153 0
	add	x21, x20, 2496
	.loc 5 152 0
	str	wzr, [x0, #:lo12:.LANCHOR3-96]
	.loc 5 153 0
	mov	w0, 65535
	ldrh	w1, [x20, 2496]
.LVL1359:
	cmp	w1, w0
	bne	.L1341
.LVL1360:
.L1344:
	.loc 5 198 0
	mov	w0, 0
.L1317:
	.loc 5 199 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
.LVL1361:
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1362:
.L1318:
	.cfi_restore_state
	.loc 5 121 0
	cbz	w0, .L1321
	.loc 5 123 0
	adrp	x0, .LANCHOR2
.LVL1363:
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w4, [x0, 4]
	cmp	w4, w3
	beq	.L1322
.L1323:
	.loc 5 122 0
	mov	w1, 2
.LVL1364:
.L1321:
	.loc 5 131 0
	add	x21, x19, :lo12:.LANCHOR0
	add	x0, x21, 2496
.LVL1365:
	bl	FtlGcScanTempBlk
.LVL1366:
	str	w0, [x29, 92]
.LVL1367:
	.loc 5 132 0
	cmn	w0, #1
	beq	.L1324
	.loc 5 135 0
	ldr	x1, [x21, 2360]
	ubfiz	x20, x20, 1, 16
	ldrh	w0, [x1, x20]
	cmp	w0, 4
	bls	.L1325
	.loc 5 136 0
	sub	w0, w0, #5
	strh	w0, [x1, x20]
	.loc 5 137 0
	mov	w0, 1
	bl	FtlEctTblFlush
.LVL1368:
.L1325:
	.loc 5 139 0
	adrp	x0, .LANCHOR3
	add	x1, x0, :lo12:.LANCHOR3
	mov	x20, x0
	ldr	w1, [x1, -96]
	cbnz	w1, .L1326
	.loc 5 140 0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19, 2792]
	add	w0, w0, 1
	str	w0, [x19, 2792]
	.loc 5 141 0
	ldr	w0, [x29, 92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.LVL1369:
	.loc 5 142 0
	bl	FtlBbmTblFlush
.LVL1370:
.L1326:
	.loc 5 144 0
	add	x0, x20, :lo12:.LANCHOR3
	str	wzr, [x0, -96]
.L1337:
	.loc 5 149 0
	mov	w0, 1
	b	.L1317
.LVL1371:
.L1322:
	.loc 5 124 0
	strh	wzr, [x0, 4]
	.loc 5 125 0
	ldrh	w0, [x2, 2392]
	cmp	w0, 17
	bhi	.L1323
	b	.L1321
.LVL1372:
.L1324:
	.loc 5 148 0
	adrp	x0, .LANCHOR2+4
	ldrh	w1, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1337
	b	.L1328
.L1341:
	.loc 5 160 0
	bl	FtlCacheWriteBack
.LVL1373:
	.loc 5 162 0
	ldrb	w0, [x21, 7]
	ldrh	w2, [x20, 82]
	ldrh	w1, [x20, 3280]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L1329
	.loc 5 162 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 162
	add	x1, x1, 624
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1374:
.L1329:
	.loc 5 163 0 is_stmt 1
	add	x0, x19, :lo12:.LANCHOR0
	.loc 5 168 0
	adrp	x22, .LANCHOR1
	add	x22, x22, :lo12:.LANCHOR1
	adrp	x25, .LC1
	add	x22, x22, 624
	add	x25, x25, :lo12:.LC1
	.loc 5 163 0
	ldrb	w1, [x0, 2503]
	.loc 5 165 0
	mov	w20, 0
.LVL1375:
	.loc 5 163 0
	ldrh	w4, [x0, 82]
	ldr	x2, [x0, 2352]
	ldrh	w3, [x0, 2496]
	mul	w1, w1, w4
	strh	w1, [x2, x3, lsl 1]
	.loc 5 164 0
	ldr	w2, [x0, 2584]
	ldrh	w1, [x0, 3280]
	add	w1, w1, w2
	str	w1, [x0, 2584]
.LVL1376:
.L1330:
	.loc 5 165 0 discriminator 1
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21, 3280]
	cmp	w0, w20
	bhi	.L1334
	.loc 5 181 0
	mov	w0, -1
	bl	decrement_vpc_count
.LVL1377:
	.loc 5 182 0
	ldrh	w0, [x21, 2496]
	ldr	x2, [x21, 2352]
	ubfiz	x1, x0, 1, 16
	ldrh	w1, [x2, x1]
	cbz	w1, .L1335
	.loc 5 183 0
	bl	INSERT_DATA_LIST
.LVL1378:
.L1336:
	.loc 5 186 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, -1
.LVL1379:
	.loc 5 187 0
	strh	wzr, [x19, 3280]
	.loc 5 186 0
	strh	w20, [x19, 2496]
	.loc 5 188 0
	strh	wzr, [x19, 3268]
	.loc 5 189 0
	bl	l2p_flush
.LVL1380:
	.loc 5 190 0
	bl	FtlVpcTblFlush
.LVL1381:
	.loc 5 191 0
	ldrh	w0, [x19, 2628]
	ldrh	w1, [x19, 2392]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	ble	.L1344
	.loc 5 193 0
	mov	w0, 20
	.loc 5 192 0
	strh	w20, [x19, 2648]
	.loc 5 193 0
	strh	w0, [x19, 3208]
	b	.L1344
.LVL1382:
.L1334:
	.loc 5 166 0
	mov	w24, 12
	ldr	x26, [x21, 3288]
	.loc 5 168 0
	ldr	w0, [x21, 2560]
	.loc 5 166 0
	umull	x24, w20, w24
	add	x23, x26, x24
	.loc 5 168 0
	ldr	w1, [x23, 8]
	cmp	w1, w0
	bcc	.L1331
	.loc 5 168 0 is_stmt 0 discriminator 1
	mov	w2, 168
	mov	x1, x22
	mov	x0, x25
	bl	printk
.LVL1383:
.L1331:
	.loc 5 169 0 is_stmt 1
	ldr	w0, [x23, 8]
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
.LVL1384:
	.loc 5 170 0
	ldr	w0, [x26, x24]
	ldr	w1, [x29, 92]
	cmp	w0, w1
	bne	.L1332
	.loc 5 171 0
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
.LVL1385:
	mov	w21, w0
	.loc 5 172 0
	ldr	w0, [x23, 8]
	mov	w2, 1
	add	x1, x23, 4
	bl	log2phys
.LVL1386:
	.loc 5 173 0
	mov	w0, w21
.L1343:
	.loc 5 177 0
	bl	decrement_vpc_count
.LVL1387:
.L1333:
	.loc 5 165 0 discriminator 2
	add	w20, w20, 1
.LVL1388:
	and	w20, w20, 65535
.LVL1389:
	b	.L1330
.L1332:
	.loc 5 175 0
	ldr	w0, [x23, 4]
	cmp	w1, w0
	beq	.L1333
	.loc 5 177 0
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 2496]
	b	.L1343
.L1335:
	.loc 5 185 0
	bl	INSERT_FREE_LIST
.LVL1390:
	b	.L1336
	.cfi_endproc
.LFE247:
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
.LFB250:
	.loc 5 289 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 5 290 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 2496
	mov	x0, x20
	ldrh	w1, [x19, 82]
	bl	FtlGcScanTempBlk
.LVL1391:
	.loc 5 291 0
	ldrh	w1, [x19, 2498]
	ldrh	w0, [x19, 82]
	cmp	w1, w0
	bcc	.L1345
.LBB310:
.LBB311:
	.loc 5 293 0
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	sub	x0, x19, #248
	bl	FtlMapBlkWriteDump_data
.LVL1392:
	.loc 5 294 0
	mov	w0, 0
	bl	FtlGcFreeTempBlock
.LVL1393:
	.loc 5 295 0
	str	wzr, [x19, -96]
.L1345:
.LBE311:
.LBE310:
	.loc 5 297 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE250:
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
.LFB222:
	.loc 2 2081 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 2 2082 0
	adrp	x0, .LANCHOR3-84
	.loc 2 2081 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 2 2084 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	.loc 2 2082 0
	str	wzr, [x0, #:lo12:.LANCHOR3-84]
	.loc 2 2084 0
	add	x20, x19, 2400
	.loc 2 2086 0
	add	x19, x19, 2448
	.loc 2 2084 0
	mov	x0, x20
	bl	FtlRecoverySuperblock
.LVL1394:
	.loc 2 2085 0
	mov	x0, x20
	bl	FtlSlcSuperblockCheck
.LVL1395:
	.loc 2 2086 0
	mov	x0, x19
	bl	FtlRecoverySuperblock
.LVL1396:
	.loc 2 2087 0
	mov	x0, x19
	bl	FtlSlcSuperblockCheck
.LVL1397:
	.loc 2 2088 0
	bl	FtlGcPageRecovery
.LVL1398:
	.loc 2 2089 0
	mov	w0, -1
	bl	decrement_vpc_count
.LVL1399:
	.loc 2 2091 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE222:
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
.LFB252:
	.loc 5 316 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 5 320 0
	adrp	x11, .LANCHOR0
	mov	w3, 0
	mov	w2, 0
	.loc 5 316 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	str	x19, [sp, 16]
	.cfi_offset 19, -16
	.loc 5 320 0
	add	x19, x11, :lo12:.LANCHOR0
	ldr	w1, [x19, 3224]
	ldr	x0, [x19, 3968]
	bl	FlashProgPages
.LVL1400:
	.loc 5 321 0
	mov	w10, 0
.LVL1401:
.L1351:
	.loc 5 321 0 is_stmt 0 discriminator 1
	ldr	w1, [x19, 3224]
	cmp	w10, w1
	bcc	.L1354
	.loc 5 335 0 is_stmt 1
	ldr	x0, [x19, 3968]
	bl	FtlGcBufFree
.LVL1402:
	.loc 5 336 0
	str	wzr, [x19, 3224]
	.loc 5 337 0
	ldrh	w0, [x19, 2500]
	cbnz	w0, .L1355
	.loc 5 338 0
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.LVL1403:
	b	.L1357
.LVL1404:
.L1354:
	.loc 5 322 0
	ldr	x0, [x19, 3968]
	ubfiz	x1, x10, 5, 16
	add	x3, x0, x1
	ldr	w0, [x0, x1]
	cmn	w0, #1
	bne	.L1352
	.loc 5 323 0
	ldrh	w3, [x19, 2496]
	ldr	x2, [x19, 2352]
	strh	wzr, [x2, x3, lsl 1]
	.loc 5 324 0
	strh	w0, [x19, 2496]
	.loc 5 325 0
	ldr	w0, [x19, 2792]
	add	w0, w0, 1
	str	w0, [x19, 2792]
	.loc 5 326 0
	ldr	x0, [x19, 3968]
	add	x0, x0, x1
	ldr	w0, [x0, 4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
.LVL1405:
	.loc 5 327 0
	bl	FtlBbmTblFlush
.LVL1406:
	.loc 5 328 0
	bl	FtlGcPageVarInit
.LVL1407:
.L1357:
	.loc 5 339 0
	mov	w0, 1
.L1350:
	.loc 5 342 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_def_cfa 31, 0
	ret
.LVL1408:
.L1352:
	.cfi_restore_state
	.loc 5 331 0 discriminator 2
	ldr	x0, [x3, 16]
.LVL1409:
	.loc 5 332 0 discriminator 2
	ldr	w1, [x3, 4]
	ldp	w2, w0, [x0, 8]
.LVL1410:
	bl	FtlGcUpdatePage
.LVL1411:
	.loc 5 321 0 discriminator 2
	add	w10, w10, 1
.LVL1412:
	and	w10, w10, 65535
.LVL1413:
	b	.L1351
.LVL1414:
.L1355:
	.loc 5 341 0
	mov	w0, 0
	b	.L1350
	.cfi_endproc
.LFE252:
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
.LFB251:
	.loc 5 300 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 5 301 0
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 2496
	ldrh	w2, [x0, 2496]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L1359
	.loc 5 301 0 is_stmt 0 discriminator 1
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1360
.L1359:
	.loc 5 302 0 is_stmt 1
	bl	FtlCacheWriteBack
.LVL1415:
	.loc 5 304 0
	add	x20, x19, :lo12:.LANCHOR0
	.loc 5 303 0
	mov	w0, 0
	bl	FtlGcFreeTempBlock
.LVL1416:
	.loc 5 304 0
	add	x0, x20, 2496
	strb	wzr, [x0, 8]
	.loc 5 305 0
	bl	allocate_data_superblock
.LVL1417:
	.loc 5 306 0
	strh	wzr, [x20, 3268]
	.loc 5 307 0
	strh	wzr, [x20, 3280]
	.loc 5 308 0
	bl	l2p_flush
.LVL1418:
	.loc 5 309 0
	mov	w0, 0
	bl	FtlEctTblFlush
.LVL1419:
	.loc 5 310 0
	bl	FtlVpcTblFlush
.LVL1420:
.L1360:
	.loc 5 312 0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2496
	bl	get_new_active_ppa
.LVL1421:
	.loc 5 313 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE251:
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
.LFB258:
	.loc 5 470 0
	.cfi_startproc
.LVL1422:
	stp	x29, x30, [sp, -128]!
	.cfi_def_cfa_offset 128
	.cfi_offset 29, -128
	.cfi_offset 30, -120
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -112
	.cfi_offset 20, -104
	.loc 5 478 0
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
.LVL1423:
	.loc 5 470 0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	.cfi_offset 21, -96
	.cfi_offset 22, -88
	.cfi_offset 23, -80
	.cfi_offset 24, -72
	.cfi_offset 25, -64
	.cfi_offset 26, -56
	.cfi_offset 27, -48
	.cfi_offset 28, -40
	.loc 5 478 0
	ldr	w2, [x1, 3944]
	cbnz	w2, .L1434
	.loc 5 481 0
	ldrh	w2, [x1, 2376]
	cmp	w2, 47
	bls	.L1434
	mov	w23, w0
	.loc 5 485 0
	adrp	x0, .LANCHOR2+4
.LVL1424:
	ldrh	w2, [x0, #:lo12:.LANCHOR2+4]
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1364
.L1367:
.LBB315:
.LBB316:
	.loc 5 489 0
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 3298]
	cmp	w1, w2
	bne	.L1365
.L1366:
	.loc 5 496 0
	add	x21, x19, :lo12:.LANCHOR0
	.loc 5 497 0
	mov	w20, 65535
	.loc 5 496 0
	ldr	w1, [x21, 3212]
	.loc 5 497 0
	ldrh	w0, [x21, 2648]
	.loc 5 496 0
	add	w1, w1, 1
	add	w1, w1, w23, lsl 7
	str	w1, [x21, 3212]
	.loc 5 497 0
	cmp	w0, w20
	bne	.L1368
	ldrh	w20, [x21, 2496]
	cmp	w20, w0
	bne	.L1437
	ldrh	w22, [x21, 3296]
	cmp	w22, w20
	bne	.L1368
	.loc 5 498 0
	ldrh	w0, [x21, 2392]
	mov	w2, 1024
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L1440
.LBB317:
	.loc 5 502 0
	adrp	x0, .LANCHOR3+68
	.loc 5 501 0
	str	wzr, [x21, 3212]
	.loc 5 502 0
	strh	wzr, [x0, #:lo12:.LANCHOR3+68]
	.loc 5 503 0
	bl	GetSwlReplaceBlock
.LVL1425:
	and	w20, w0, 65535
	.loc 5 504 0
	cmp	w20, w22
	bne	.L1441
	.loc 5 505 0
	ldrh	w1, [x21, 2392]
	ldrh	w0, [x21, 3210]
	cmp	w1, w0
	bcs	.L1371
	.loc 5 506 0
	mov	w0, 64
	bl	List_get_gc_head_node
.LVL1426:
	and	w0, w0, 65535
.LVL1427:
	.loc 5 507 0
	cmp	w0, w20
	beq	.L1373
	.loc 5 508 0
	ldr	x1, [x21, 2352]
	ubfiz	x0, x0, 1, 16
.LVL1428:
	ldrh	w0, [x1, x0]
	cmp	w0, 7
	bhi	.L1374
	.loc 5 509 0
	mov	w0, 0
	bl	List_get_gc_head_node
.LVL1429:
	and	w22, w0, 65535
	.loc 5 510 0
	mov	w0, 128
	strh	w0, [x21, 3210]
	.loc 5 520 0
	cmp	w22, w20
	beq	.L1373
.L1370:
	.loc 5 521 0
	add	x0, x19, :lo12:.LANCHOR0
	ubfiz	x1, x22, 1, 32
	mov	w20, w22
	ldr	x2, [x0, 2352]
	ldr	x3, [x0, 2360]
	ldrh	w5, [x0, 3208]
	ldrh	w4, [x3, x1]
	ldrh	w3, [x2, x1]
	mov	w1, w22
	ldrh	w2, [x0, 2392]
	adrp	x0, .LC103
	add	x0, x0, :lo12:.LC103
	bl	printk
.LVL1430:
	b	.L1373
.L1364:
.LBE317:
.LBE316:
.LBE315:
	.loc 5 485 0 discriminator 1
	ldrh	w1, [x1, 2496]
	cmp	w1, w0
	beq	.L1367
	.loc 5 486 0
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.LVL1431:
	cbz	w0, .L1367
	.loc 5 487 0
	mov	w0, 1
	b	.L1362
.L1365:
.LBB322:
.LBB320:
	.loc 5 489 0
	ldrh	w3, [x0, 3296]
	cmp	w3, w2
	bne	.L1366
	.loc 5 490 0
	strh	w1, [x0, 3296]
	.loc 5 491 0
	mov	w1, -1
	strh	w1, [x0, 3298]
	b	.L1366
.L1374:
.LBB318:
	.loc 5 512 0
	mov	w0, 64
.L1466:
	.loc 5 516 0
	strh	w0, [x21, 3210]
.LVL1432:
.L1373:
	.loc 5 523 0
	bl	FtlGcReFreshBadBlk
.LVL1433:
.L1368:
.LBE318:
	.loc 5 527 0
	cmp	w23, 0
	mov	w0, 65535
	ccmp	w20, w0, 0, eq
	bne	.L1376
	.loc 5 528 0
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2392]
	cmp	w0, 24
	bhi	.L1442
	.loc 5 530 0
	cmp	w0, 16
	ldrh	w21, [x1, 82]
	bls	.L1378
	.loc 5 531 0
	lsr	w21, w21, 5
.LVL1434:
.L1377:
	.loc 5 539 0
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1, 3208]
	cmp	w2, w0
	bcs	.L1381
	.loc 5 540 0
	ldrh	w0, [x1, 2496]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1382
	ldrh	w2, [x1, 3296]
	cmp	w2, w0
	bne	.L1382
	.loc 5 542 0
	adrp	x0, .LANCHOR3+68
	ldrh	w0, [x0, #:lo12:.LANCHOR3+68]
	cbnz	w0, .L1383
	ldr	w2, [x1, 2560]
	ldr	w3, [x1, 2556]
	add	w2, w2, w2, lsl 1
	cmp	w3, w2, lsr 2
	bcs	.L1384
.L1383:
	.loc 5 543 0
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2, 2628]
	add	w1, w1, w1, lsl 1
	asr	w1, w1, 2
	strh	w1, [x2, 3208]
.L1385:
	.loc 5 546 0
	add	x19, x19, :lo12:.LANCHOR0
	str	wzr, [x19, 3220]
.LVL1435:
.L1362:
.LBE320:
.LBE322:
	.loc 5 754 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL1436:
	ldp	x23, x24, [sp, 48]
.LVL1437:
	ldp	x25, x26, [sp, 64]
.LVL1438:
	ldp	x27, x28, [sp, 80]
.LVL1439:
	ldp	x29, x30, [sp], 128
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1440:
.L1371:
	.cfi_restore_state
.LBB323:
.LBB321:
.LBB319:
	.loc 5 516 0
	mov	w0, 80
	b	.L1466
.L1441:
	.loc 5 504 0
	mov	w22, w20
	b	.L1370
.L1437:
.LBE319:
	.loc 5 473 0
	mov	w20, w0
	b	.L1368
.L1440:
	mov	w20, w22
	b	.L1368
.L1378:
	.loc 5 532 0
	cmp	w0, 12
	bls	.L1379
	.loc 5 533 0
	lsr	w21, w21, 4
.LVL1441:
	b	.L1377
.LVL1442:
.L1379:
	.loc 5 535 0
	mov	w1, w21
	cmp	w0, 9
	lsr	w21, w21, 2
	csel	w21, w21, w1, cs
.LVL1443:
	b	.L1377
.LVL1444:
.L1442:
	.loc 5 529 0
	mov	w21, 1
.LVL1445:
	b	.L1377
.L1384:
	.loc 5 545 0
	mov	w2, 18
	strh	w2, [x1, 3208]
	b	.L1385
.L1382:
	.loc 5 549 0
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2628]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1, 3208]
.L1381:
	.loc 5 551 0
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 3300]
	cbz	w0, .L1443
	.loc 5 552 0
	add	w21, w21, 32
.LVL1446:
	and	w21, w21, 65535
.LVL1447:
.L1443:
	.loc 5 551 0
	mov	w20, 65535
.L1387:
	.loc 5 595 0
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 2648]
	cmp	w1, w2
	bne	.L1396
	.loc 5 596 0
	cmp	w20, w1
	beq	.L1397
	.loc 5 597 0
	strh	w20, [x0, 2648]
.L1398:
	.loc 5 606 0
	add	x5, x19, :lo12:.LANCHOR0
	.loc 5 607 0
	mov	w1, 65535
	ldrh	w0, [x5, 2648]
	.loc 5 606 0
	strb	wzr, [x5, 2656]
	.loc 5 607 0
	cmp	w0, w1
	beq	.L1396
	.loc 5 608 0
	bl	IsBlkInGcList
.LVL1448:
	cbz	w0, .L1401
	.loc 5 609 0
	mov	w0, -1
	strh	w0, [x5, 2648]
.L1401:
	.loc 5 611 0
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	add	x23, x22, 2648
.LVL1449:
	ldrh	w1, [x22, 2648]
	cmp	w1, w0
	beq	.L1396
	.loc 5 612 0
	mov	x0, x23
	bl	make_superblock
.LVL1450:
	.loc 5 615 0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	.loc 5 616 0
	ldrh	w2, [x22, 2648]
	ldr	x1, [x22, 2352]
	.loc 5 613 0
	strh	wzr, [x22, 2650]
	.loc 5 614 0
	strb	wzr, [x22, 2654]
	.loc 5 615 0
	strh	wzr, [x0, 70]
	.loc 5 616 0
	ldrh	w1, [x1, x2, lsl 1]
	strh	w1, [x0, 72]
.LVL1451:
.L1396:
	.loc 5 621 0
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2648]
	ldrh	w2, [x0, 2400]
	cmp	w2, w1
	beq	.L1402
	ldrh	w0, [x0, 2448]
	cmp	w0, w1
	beq	.L1402
	.loc 5 660 0
	adrp	x24, .LANCHOR1
	add	x24, x24, :lo12:.LANCHOR1
	add	x24, x24, 648
.LVL1452:
.L1403:
	.loc 5 627 0
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w23, [x22, 2648]
.LVL1453:
	cmp	w23, w0
	bne	.L1404
	.loc 5 641 0
	mov	w25, 2
	.loc 5 628 0
	str	wzr, [x22, 3220]
.L1405:
	.loc 5 631 0
	ldrh	w5, [x22, 3216]
	mov	w0, w5
	bl	List_get_gc_head_node
.LVL1454:
	and	w6, w0, 65535
	strh	w6, [x22, 2648]
	.loc 5 632 0
	cmp	w6, w23
	bne	.L1406
	.loc 5 633 0
	strh	wzr, [x22, 3216]
	.loc 5 634 0
	mov	w0, 8
	b	.L1362
.LVL1455:
.L1376:
	.loc 5 557 0
	add	x5, x19, :lo12:.LANCHOR0
	ldrh	w1, [x5, 2496]
	cmp	w1, w0
	bne	.L1446
	ldrh	w0, [x5, 3296]
	cmp	w0, w1
	bne	.L1446
	cmp	w20, w0
	bne	.L1446
	ldrh	w0, [x5, 2648]
	cmp	w0, w20
	beq	.L1388
.L1446:
	.loc 5 556 0
	mov	w21, 1
.LVL1456:
	b	.L1387
.LVL1457:
.L1388:
	.loc 5 560 0
	ldrh	w7, [x5, 2392]
	adrp	x21, .LANCHOR3
	ldrh	w0, [x5, 3208]
	.loc 5 558 0
	str	wzr, [x5, 3220]
	.loc 5 560 0
	cmp	w0, w7
	bcs	.L1389
	.loc 5 561 0
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, 68]
	cbnz	w0, .L1390
	ldr	w0, [x5, 2560]
	ldr	w1, [x5, 2556]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bcs	.L1391
.L1390:
	.loc 5 562 0
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 2628]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x19, 3208]
.L1392:
	.loc 5 566 0
	bl	FtlReadRefresh
.LVL1458:
.L1467:
	.loc 5 730 0
	add	x21, x21, :lo12:.LANCHOR3
	b	.L1468
.LVL1459:
.L1391:
	.loc 5 564 0
	mov	w0, 18
	strh	w0, [x5, 3208]
	b	.L1392
.L1389:
	.loc 5 572 0
	add	x21, x21, :lo12:.LANCHOR3
	ldrh	w0, [x21, 68]
	cbnz	w0, .L1446
	.loc 5 573 0
	ldrh	w6, [x5, 2628]
	add	w0, w6, w6, lsl 1
	asr	w0, w0, 2
	strh	w0, [x5, 3208]
	.loc 5 574 0
	mov	w0, 0
	bl	List_get_gc_head_node
.LVL1460:
	.loc 5 575 0
	ldr	x1, [x5, 2352]
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x5, 12]
	ldrh	w1, [x1, x0]
	ldrh	w0, [x5, 84]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L1394
	.loc 5 576 0
	sub	w6, w6, #1
	.loc 5 575 0
	cmp	w7, w6
	blt	.L1394
	.loc 5 578 0
	bl	FtlReadRefresh
.LVL1461:
.L1468:
	.loc 5 730 0
	ldrh	w0, [x21, 68]
	b	.L1362
.LVL1462:
.L1394:
	.loc 5 584 0
	cbnz	w1, .L1446
	.loc 5 586 0
	add	x19, x19, :lo12:.LANCHOR0
	.loc 5 585 0
	mov	w0, -1
	bl	decrement_vpc_count
.LVL1463:
	.loc 5 586 0
	ldrh	w0, [x19, 2392]
	add	w0, w0, 1
	b	.L1362
.LVL1464:
.L1397:
	.loc 5 598 0
	ldrh	w1, [x0, 3296]
	cmp	w1, w20
	beq	.L1398
	.loc 5 600 0
	ldr	x2, [x0, 2352]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2, x1]
	cbnz	w1, .L1399
	.loc 5 601 0
	mov	w1, -1
	strh	w1, [x0, 3296]
.L1399:
	.loc 5 602 0
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3296]
	strh	w1, [x0, 2648]
	.loc 5 603 0
	mov	w1, -1
	strh	w1, [x0, 3296]
	b	.L1398
.L1402:
	.loc 5 622 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19, 2648]
.LVL1465:
.L1469:
	.loc 5 648 0
	adrp	x0, .LANCHOR3+68
	ldrh	w0, [x0, #:lo12:.LANCHOR3+68]
	b	.L1362
.LVL1466:
.L1406:
	.loc 5 636 0
	mov	w0, w6
	bl	IsBlkInGcList
.LVL1467:
	add	w5, w5, 1
	cbz	w0, .L1407
	.loc 5 637 0
	strh	w5, [x22, 3216]
	b	.L1405
.L1407:
	.loc 5 641 0
	ldrh	w4, [x22, 12]
	ubfiz	x1, x6, 1, 16
	ldrh	w0, [x22, 82]
	.loc 5 640 0
	and	w5, w5, 65535
	.loc 5 641 0
	ldr	x2, [x22, 2352]
	.loc 5 640 0
	strh	w5, [x22, 3216]
	.loc 5 641 0
	mul	w0, w0, w4
	ldrh	w3, [x2, x1]
	sdiv	w4, w0, w25
	cmp	w3, w4
	bgt	.L1409
	.loc 5 642 0
	cmp	w5, 48
	bls	.L1410
	cmp	w3, 8
	bls	.L1410
	ldrh	w3, [x22, 3268]
	cmp	w3, 35
	bhi	.L1410
.L1409:
	.loc 5 643 0
	strh	wzr, [x22, 3216]
.L1410:
	.loc 5 645 0
	ldrh	w1, [x2, x1]
	cmp	w0, w1
	bgt	.L1411
	cmp	w20, w23
	bne	.L1411
	.loc 5 646 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	.loc 5 647 0
	strh	wzr, [x19, 3216]
	.loc 5 646 0
	strh	w0, [x19, 2648]
	b	.L1469
.L1411:
	.loc 5 651 0
	cbnz	w1, .L1412
	.loc 5 652 0
	mov	w0, -1
	bl	decrement_vpc_count
.LVL1468:
	.loc 5 653 0
	ldrh	w0, [x22, 3216]
	add	w0, w0, 1
	strh	w0, [x22, 3216]
	b	.L1405
.L1412:
	.loc 5 656 0
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0, 2656]
	.loc 5 658 0
	ldrh	w0, [x0, 2400]
	cmp	w0, w6
	bne	.L1413
	adrp	x0, .LC1
	mov	w2, 658
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1469:
.L1413:
	.loc 5 659 0
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2648]
	ldrh	w0, [x0, 2448]
	cmp	w1, w0
	bne	.L1414
	adrp	x0, .LC1
	mov	w2, 659
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1470:
.L1414:
	.loc 5 660 0
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2648]
	ldrh	w0, [x0, 2496]
	cmp	w1, w0
	bne	.L1415
	adrp	x0, .LC1
	mov	w2, 660
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1471:
.L1415:
	.loc 5 661 0
	add	x23, x19, :lo12:.LANCHOR0
	add	x22, x23, 2648
	mov	x0, x22
	bl	make_superblock
.LVL1472:
	.loc 5 662 0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	.loc 5 663 0
	ldrh	w2, [x23, 2648]
	ldr	x1, [x23, 2352]
	.loc 5 662 0
	strh	wzr, [x0, 70]
	.loc 5 663 0
	ldrh	w1, [x1, x2, lsl 1]
	strh	w1, [x0, 72]
	.loc 5 664 0
	strh	wzr, [x23, 2650]
	.loc 5 665 0
	strb	wzr, [x23, 2654]
.L1404:
	.loc 5 671 0
	bl	FtlReadRefresh
.LVL1473:
	.loc 5 674 0
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	.loc 5 675 0
	ldrh	w26, [x0, 82]
	.loc 5 674 0
	str	w1, [x0, 3944]
	.loc 5 676 0
	ldrh	w0, [x0, 2650]
	add	w1, w0, w21
	cmp	w1, w26
	ble	.L1416
	.loc 5 677 0
	sub	w21, w26, w0
.LVL1474:
	and	w21, w21, 65535
.LVL1475:
.L1416:
	.loc 5 689 0
	mov	w25, 0
.LVL1476:
.L1417:
	.loc 5 679 0
	cmp	w21, w25, uxth
	bls	.L1425
	.loc 5 681 0
	add	x0, x19, :lo12:.LANCHOR0
	.loc 5 684 0
	mov	w23, 0
.LVL1477:
	add	x3, x0, 2664
	mov	w1, 0
.LVL1478:
	.loc 5 683 0
	mov	w6, 65535
	.loc 5 684 0
	ldrh	w4, [x0, 2650]
	.loc 5 681 0
	ldrh	w7, [x0, 12]
	.loc 5 684 0
	add	w4, w4, w25
	b	.L1426
.LVL1479:
.L1419:
	.loc 5 682 0
	ldrh	w2, [x3]
	.loc 5 683 0
	cmp	w2, w6
	beq	.L1418
	.loc 5 684 0
	ldr	x5, [x0, 3256]
	ubfiz	x8, x23, 5, 16
	.loc 5 685 0
	add	w23, w23, 1
.LVL1480:
	.loc 5 684 0
	orr	w2, w4, w2, lsl 10
.LVL1481:
	add	x5, x5, x8
	.loc 5 685 0
	and	w23, w23, 65535
.LVL1482:
	.loc 5 684 0
	str	w2, [x5, 4]
.LVL1483:
.L1418:
	.loc 5 681 0
	add	w1, w1, 1
.LVL1484:
	add	x3, x3, 2
	and	w1, w1, 65535
.LVL1485:
.L1426:
	cmp	w1, w7
	bne	.L1419
	.loc 5 689 0
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, w23
.LVL1486:
	mov	x27, 0
	ldrb	w2, [x22, 2656]
.LVL1487:
	ldr	x0, [x22, 3256]
	bl	FlashReadPages
.LVL1488:
	ubfiz	x0, x23, 5, 16
	str	x0, [x29, 104]
.LVL1489:
.L1420:
	.loc 5 691 0
	ldr	x0, [x29, 104]
	cmp	x27, x0
	bne	.L1424
	add	w25, w25, 1
	b	.L1417
.L1424:
	.loc 5 692 0
	ldr	x0, [x22, 3256]
	add	x1, x0, x27
	.loc 5 693 0
	ldr	w0, [x0, x27]
	cmn	w0, #1
	beq	.L1421
	.loc 5 692 0
	ldr	x23, [x1, 16]
.LVL1490:
	.loc 5 693 0
	mov	w0, 61589
	ldrh	w1, [x23]
	cmp	w1, w0
	bne	.L1421
	.loc 5 695 0
	ldr	w28, [x23, 8]
.LVL1491:
	.loc 5 696 0
	cmn	w28, #1
	bne	.L1422
	adrp	x0, .LC1
	mov	w2, 696
	mov	x1, x24
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1492:
.L1422:
	.loc 5 697 0
	mov	w2, 0
	add	x1, x29, 124
	mov	w0, w28
	bl	log2phys
.LVL1493:
	.loc 5 698 0
	ldr	x0, [x22, 3256]
	ldr	w1, [x29, 124]
	add	x0, x0, x27
	ldr	w2, [x0, 4]
.LVL1494:
	cmp	w2, w1
	bne	.L1421
	.loc 5 700 0
	adrp	x28, .LANCHOR3
.LVL1495:
	add	x2, x28, :lo12:.LANCHOR3
	.loc 5 701 0
	ldr	w0, [x0, 24]
	.loc 5 700 0
	ldrh	w1, [x2, 70]
	add	w1, w1, 1
	strh	w1, [x2, 70]
	.loc 5 701 0
	ldr	x2, [x22, 3968]
	ldr	w1, [x22, 3224]
	add	x1, x2, x1, lsl 5
	str	x1, [x29, 96]
	str	w0, [x1, 24]
	.loc 5 702 0
	bl	Ftl_get_new_temp_ppa
.LVL1496:
	ldr	x1, [x29, 96]
	str	w0, [x1, 4]
	.loc 5 703 0
	ldr	w0, [x22, 3224]
	ldr	x1, [x22, 3968]
	add	x0, x1, x0, lsl 5
	ldr	x1, [x22, 3256]
	add	x1, x1, x27
	ldr	x2, [x1, 8]
	str	x2, [x0, 8]
	.loc 5 704 0
	ldr	x1, [x1, 16]
	str	x1, [x0, 16]
	.loc 5 705 0
	ldr	w0, [x29, 124]
	.loc 5 712 0
	mov	w1, 1
.LVL1497:
	.loc 5 705 0
	str	w0, [x23, 12]
	.loc 5 706 0
	ldrh	w0, [x22, 2496]
.LVL1498:
	strh	w0, [x23, 2]
	.loc 5 709 0
	ldr	w0, [x22, 2600]
	str	w0, [x23, 4]
	.loc 5 711 0
	ldr	w0, [x22, 3224]
	add	w0, w0, 1
	str	w0, [x22, 3224]
	.loc 5 712 0
	ldr	x0, [x22, 3256]
	add	x0, x0, x27
	bl	FtlGcBufAlloc
.LVL1499:
	.loc 5 713 0
	ldrb	w1, [x22, 2503]
	ldr	w0, [x22, 3224]
	cmp	w1, w0
	beq	.L1423
	ldrh	w0, [x22, 2500]
	cbnz	w0, .L1421
.L1423:
	.loc 5 714 0
	bl	Ftl_gc_temp_data_write_back
.LVL1500:
	cbz	w0, .L1421
	.loc 5 715 0
	add	x19, x19, :lo12:.LANCHOR0
	.loc 5 716 0
	add	x28, x28, :lo12:.LANCHOR3
	.loc 5 715 0
	str	wzr, [x19, 3944]
	.loc 5 716 0
	ldrh	w0, [x28, 68]
	b	.L1362
.LVL1501:
.L1421:
	add	x27, x27, 32
	b	.L1420
.L1425:
	.loc 5 723 0
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2650]
	add	w21, w21, w0
.LVL1502:
	and	w21, w21, 65535
	strh	w21, [x1, 2650]
	.loc 5 725 0
	cmp	w26, w21
	adrp	x21, .LANCHOR3
	bls	.L1427
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w1, [x0, 70]
	ldrh	w0, [x0, 72]
	cmp	w1, w0
	bne	.L1428
.L1427:
	.loc 5 727 0
	add	x22, x19, :lo12:.LANCHOR0
	ldr	w0, [x22, 3224]
	cbz	w0, .L1429
	.loc 5 728 0
	bl	Ftl_gc_temp_data_write_back
.LVL1503:
	cbz	w0, .L1429
	.loc 5 729 0
	str	wzr, [x22, 3944]
	b	.L1467
.L1429:
	.loc 5 733 0
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, 70]
	cbnz	w0, .L1430
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2648]
	ldr	x2, [x1, 2352]
	lsl	x0, x0, 1
	ldrh	w3, [x2, x0]
	cbz	w3, .L1430
	.loc 5 735 0
	strh	wzr, [x2, x0]
	.loc 5 736 0
	ldrh	w0, [x1, 2648]
	bl	update_vpc_list
.LVL1504:
	.loc 5 737 0
	bl	FtlCacheWriteBack
.LVL1505:
	.loc 5 738 0
	bl	l2p_flush
.LVL1506:
	.loc 5 739 0
	bl	FtlVpcTblFlush
.LVL1507:
.L1430:
	.loc 5 741 0
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0, 2648]
.L1428:
	.loc 5 743 0
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 2392]
	cmp	w0, 2
	bhi	.L1431
	.loc 5 745 0
	ldrh	w21, [x1, 82]
.LVL1508:
	b	.L1403
.LVL1509:
.L1431:
	.loc 5 750 0
	add	x21, x21, :lo12:.LANCHOR3
	.loc 5 748 0
	str	wzr, [x1, 3944]
	.loc 5 750 0
	ldrh	w1, [x21, 68]
	.loc 5 751 0
	cmp	w1, 0
	csinc	w0, w1, w0, ne
	b	.L1362
.LVL1510:
.L1434:
.LBE321:
.LBE323:
	.loc 5 479 0
	mov	w0, 0
.LVL1511:
	b	.L1362
	.cfi_endproc
.LFE258:
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	sftl_gc
	.type	sftl_gc, %function
sftl_gc:
.LFB259:
	.loc 5 757 0
	.cfi_startproc
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 5 758 0
	mov	w1, 1
	mov	w0, w1
	.loc 5 757 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 5 758 0
	bl	rk_ftl_garbage_collect
.LVL1512:
	.loc 5 759 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE259:
	.size	sftl_gc, .-sftl_gc
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
.LFB153:
	.loc 3 814 0
	.cfi_startproc
.LVL1513:
	stp	x29, x30, [sp, -160]!
	.cfi_def_cfa_offset 160
	.cfi_offset 29, -160
	.cfi_offset 30, -152
	and	w0, w0, 255
	.loc 3 824 0
	cmp	w0, 16
	.loc 3 814 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -144
	.cfi_offset 20, -136
	mov	w19, w1
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -112
	.cfi_offset 24, -104
	mov	x23, x3
	stp	x27, x28, [sp, 80]
	.cfi_offset 27, -80
	.cfi_offset 28, -72
	mov	w27, w2
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	.cfi_offset 21, -128
	.cfi_offset 22, -120
	.cfi_offset 25, -96
	.cfi_offset 26, -88
	.loc 3 824 0
	bne	.L1473
	.loc 3 825 0
	mov	x2, x3
	mov	w1, w27
	add	w0, w19, 256
.LVL1514:
	bl	FtlVendorPartRead
.LVL1515:
	mov	w21, w0
.LVL1516:
.L1472:
	.loc 3 901 0
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
.LVL1517:
	ldp	x21, x22, [sp, 32]
.LVL1518:
	ldp	x23, x24, [sp, 48]
.LVL1519:
	ldp	x25, x26, [sp, 64]
.LVL1520:
	ldp	x27, x28, [sp, 80]
.LVL1521:
	ldp	x29, x30, [sp], 160
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1522:
.L1473:
	.cfi_restore_state
	.loc 3 830 0
	add	w0, w1, w2
.LVL1523:
	str	w0, [x29, 128]
	adrp	x0, .LANCHOR0
	add	x25, x0, :lo12:.LANCHOR0
	add	w2, w1, w2
	str	x0, [x29, 96]
	ldr	w1, [x25, 120]
	cmp	w2, w1
	bhi	.L1493
	.loc 3 830 0 is_stmt 0 discriminator 1
	adrp	x0, .LANCHOR2
	ldr	w21, [x0, #:lo12:.LANCHOR2]
	cmn	w21, #1
	beq	.L1472
	.loc 3 833 0 is_stmt 1
	bl	FtlCacheWriteBack
.LVL1524:
	.loc 3 840 0
	mov	w28, 0
.LVL1525:
	.loc 3 835 0
	ldrh	w0, [x25, 38]
	.loc 3 836 0
	add	w1, w19, w27
	sub	w24, w1, #1
	.loc 3 815 0
	mov	w21, 0
	.loc 3 828 0
	stp	wzr, wzr, [x29, 132]
	.loc 3 820 0
	str	wzr, [x29, 140]
	.loc 3 835 0
	udiv	w26, w19, w0
.LVL1526:
	.loc 3 836 0
	udiv	w24, w24, w0
.LVL1527:
	.loc 3 841 0
	ldr	w0, [x25, 2564]
	.loc 3 835 0
	mov	w20, w26
.LVL1528:
	.loc 3 837 0
	sub	w22, w24, w26
	add	w22, w22, 1
.LVL1529:
	.loc 3 841 0
	add	w0, w0, w22
	str	w0, [x25, 2564]
.LVL1530:
.L1475:
	.loc 3 842 0
	cbnz	w22, .L1491
	.loc 3 898 0
	ldr	x0, [x29, 96]
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3300]
	cbnz	w1, .L1492
	.loc 3 898 0 is_stmt 0 discriminator 1
	ldrh	w0, [x0, 2392]
	cmp	w0, 31
	bhi	.L1472
.L1492:
	.loc 3 899 0 is_stmt 1
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
.LVL1531:
	b	.L1472
.LVL1532:
.L1491:
	.loc 3 843 0
	add	x1, x29, 156
	mov	w2, 0
	mov	w0, w20
	bl	log2phys
.LVL1533:
	.loc 3 844 0
	ldr	w1, [x29, 156]
	cmn	w1, #1
	bne	.L1476
	mov	w5, 0
.LVL1534:
.L1477:
	.loc 3 845 0 discriminator 1
	ldrh	w0, [x25, 38]
.LVL1535:
	cmp	w5, w0
	bcc	.L1479
.LVL1536:
.L1480:
	.loc 3 872 0
	add	w20, w20, 1
	.loc 3 874 0
	subs	w22, w22, #1
	beq	.L1484
	.loc 3 874 0 is_stmt 0 discriminator 1
	ldrh	w0, [x25, 12]
	cmp	w28, w0, lsl 2
	bne	.L1475
.L1484:
	.loc 3 875 0 is_stmt 1
	cbz	w28, .L1475
	.loc 3 876 0
	ldr	x0, [x25, 3960]
	mov	w1, w28
	mov	w2, 0
	bl	FlashReadPages
.LVL1537:
	.loc 3 883 0
	ldr	w0, [x29, 132]
.LVL1538:
	lsl	w0, w0, 9
.LVL1539:
	str	w0, [x29, 112]
	.loc 3 880 0
	ldr	w0, [x29, 140]
.LVL1540:
	lsl	w0, w0, 9
.LVL1541:
	str	x0, [x29, 120]
	ldr	w0, [x29, 136]
	lsl	w0, w0, 9
	str	w0, [x29, 116]
	ubfiz	x0, x28, 5, 32
	mov	x28, 0
.LVL1542:
	str	x0, [x29, 104]
.L1490:
	.loc 3 878 0
	ldr	x0, [x25, 3960]
	add	x0, x0, x28
	ldr	w1, [x0, 24]
	cmp	w26, w1
	bne	.L1486
	.loc 3 879 0
	ldr	x1, [x0, 8]
	ldr	x0, [x25, 3936]
	cmp	x1, x0
	bne	.L1487
	.loc 3 880 0
	ldr	x0, [x29, 120]
	ldr	w2, [x29, 116]
	add	x1, x1, x0
	mov	x0, x23
.L1503:
	.loc 3 883 0
	bl	ftl_memcpy
.LVL1543:
.L1487:
	.loc 3 885 0
	ldr	x0, [x25, 3960]
	add	x1, x0, x28
	ldr	w2, [x0, x28]
	cmn	w2, #1
	bne	.L1488
	.loc 3 886 0
	ldr	w0, [x25, 2768]
	.loc 3 887 0
	mov	w21, w2
	.loc 3 886 0
	add	w0, w0, 1
	str	w0, [x25, 2768]
.L1488:
	.loc 3 889 0
	ldr	w0, [x1]
	cmp	w0, 256
	bne	.L1489
.LBB324:
	.loc 3 890 0
	ldr	w0, [x1, 4]
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
.LVL1544:
	.loc 3 891 0
	bl	FtlGcRefreshBlock
.LVL1545:
.L1489:
.LBE324:
	.loc 3 877 0 discriminator 2
	ldr	x0, [x29, 104]
	add	x28, x28, 32
	cmp	x0, x28
	bne	.L1490
	.loc 3 894 0
	mov	w28, 0
.LVL1546:
	b	.L1475
.LVL1547:
.L1479:
	.loc 3 846 0
	madd	w0, w20, w0, w5
.LVL1548:
	.loc 3 847 0
	cmp	w19, w0
	bhi	.L1478
	.loc 3 847 0 is_stmt 0 discriminator 1
	ldr	w1, [x29, 128]
	cmp	w1, w0
	bls	.L1478
	.loc 3 848 0 is_stmt 1
	sub	w0, w0, w19
.LVL1549:
	str	w5, [x29, 120]
	lsl	w0, w0, 9
	mov	w2, 512
	mov	w1, 0
	add	x0, x23, x0
	bl	ftl_memset
.LVL1550:
	ldr	w5, [x29, 120]
.LVL1551:
.L1478:
	.loc 3 845 0 discriminator 2
	add	w5, w5, 1
	b	.L1477
.LVL1552:
.L1476:
	.loc 3 851 0
	ldr	x0, [x25, 3960]
	ubfiz	x2, x28, 5, 32
	.loc 3 852 0
	cmp	w20, w26
	.loc 3 851 0
	add	x0, x0, x2
	str	w1, [x0, 4]
	ldrh	w0, [x25, 38]
	.loc 3 852 0
	bne	.L1481
	.loc 3 853 0
	ldr	x1, [x25, 3960]
	ldr	x5, [x25, 3936]
	add	x1, x1, x2
	str	x5, [x1, 8]
	.loc 3 854 0
	udiv	w5, w19, w0
	msub	w3, w5, w0, w19
.LVL1553:
	str	w3, [x29, 140]
	.loc 3 855 0
	sub	w5, w0, w3
	cmp	w27, w5
	csel	w3, w27, w5, ls
.LVL1554:
	str	w3, [x29, 136]
	.loc 3 857 0
	cmp	w3, w0
	bne	.L1482
	.loc 3 858 0
	str	x23, [x1, 8]
.LVL1555:
.L1482:
	.loc 3 867 0
	ldr	x0, [x25, 3960]
	ldr	x1, [x25, 3928]
	add	x2, x0, x2
	ldrh	w0, [x25, 92]
	.loc 3 868 0
	str	w20, [x2, 24]
	.loc 3 867 0
	mul	w0, w0, w28
	.loc 3 869 0
	add	w28, w28, 1
	.loc 3 867 0
	and	x0, x0, 4294967292
	add	x0, x1, x0
	str	x0, [x2, 16]
	b	.L1480
.LVL1556:
.L1481:
	.loc 3 859 0
	cmp	w20, w24
	bne	.L1483
	.loc 3 860 0
	ldr	x5, [x25, 3960]
	ldr	x1, [x25, 3920]
	add	x5, x5, x2
	.loc 3 861 0
	ldr	w3, [x29, 128]
	.loc 3 860 0
	str	x1, [x5, 8]
	.loc 3 861 0
	mul	w1, w20, w0
	sub	w3, w3, w1
.LVL1557:
	str	w3, [x29, 132]
	.loc 3 862 0
	cmp	w0, w3
	bne	.L1482
	.loc 3 863 0
	sub	w1, w1, w19
	lsl	w1, w1, 9
	add	x1, x23, x1
	str	x1, [x5, 8]
	b	.L1482
.LVL1558:
.L1483:
	.loc 3 865 0
	ldr	x1, [x25, 3960]
	mul	w0, w0, w20
	add	x1, x1, x2
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	str	x0, [x1, 8]
	b	.L1482
.LVL1559:
.L1486:
	.loc 3 881 0
	cmp	w24, w1
	bne	.L1487
	.loc 3 882 0
	ldr	x1, [x0, 8]
	ldr	x0, [x25, 3920]
	cmp	x1, x0
	bne	.L1487
	.loc 3 883 0
	ldrh	w0, [x25, 38]
	ldr	w2, [x29, 112]
	mul	w0, w0, w24
	sub	w0, w0, w19
	lsl	w0, w0, 9
	add	x0, x23, x0
	b	.L1503
.LVL1560:
.L1493:
	.loc 3 831 0
	mov	w21, -1
	b	.L1472
	.cfi_endproc
.LFE153:
	.size	FtlRead, .-FtlRead
	.align	2
	.global	sftl_read
	.type	sftl_read, %function
sftl_read:
.LFB158:
	.loc 3 1204 0
	.cfi_startproc
.LVL1561:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 3 1205 0
	mov	x3, x2
	mov	w2, w1
.LVL1562:
	mov	w1, w0
.LVL1563:
	.loc 3 1204 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 3 1205 0
	mov	w0, 0
.LVL1564:
	.loc 3 1204 0
	.loc 3 1205 0
	bl	FtlRead
.LVL1565:
	.loc 3 1206 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE158:
	.size	sftl_read, .-sftl_read
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
.LFB156:
	.loc 3 962 0
	.cfi_startproc
.LVL1566:
	stp	x29, x30, [sp, -224]!
	.cfi_def_cfa_offset 224
	.cfi_offset 29, -224
	.cfi_offset 30, -216
	and	w0, w0, 255
	.loc 3 970 0
	cmp	w0, 16
	.loc 3 962 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -176
	.cfi_offset 24, -168
	mov	w23, w1
	stp	x25, x26, [sp, 64]
	.cfi_offset 25, -160
	.cfi_offset 26, -152
	mov	w26, w2
	stp	x27, x28, [sp, 80]
	.cfi_offset 27, -144
	.cfi_offset 28, -136
	mov	x27, x3
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	.cfi_offset 19, -208
	.cfi_offset 20, -200
	.cfi_offset 21, -192
	.cfi_offset 22, -184
	.loc 3 970 0
	bne	.L1507
	.loc 3 971 0
	mov	x2, x3
	mov	w1, w26
	add	w0, w23, 256
.LVL1567:
	bl	FtlVendorPartWrite
.LVL1568:
.L1506:
	.loc 3 1153 0
	ldp	x19, x20, [sp, 16]
.LVL1569:
	ldp	x21, x22, [sp, 32]
.LVL1570:
	ldp	x23, x24, [sp, 48]
.LVL1571:
	ldp	x25, x26, [sp, 64]
.LVL1572:
	ldp	x27, x28, [sp, 80]
.LVL1573:
	ldp	x29, x30, [sp], 224
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 27
	.cfi_restore 28
	.cfi_restore 25
	.cfi_restore 26
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.LVL1574:
.L1507:
	.cfi_restore_state
	.loc 3 974 0
	add	w3, w1, w2
	adrp	x20, .LANCHOR0
	add	x2, x20, :lo12:.LANCHOR0
	ldr	w0, [x2, 120]
.LVL1575:
	cmp	w3, w0
	bhi	.L1549
	.loc 3 974 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR2
	str	x1, [x29, 136]
	ldr	w0, [x1, #:lo12:.LANCHOR2]
	cmn	w0, #1
	beq	.L1506
	.loc 3 978 0 is_stmt 1
	ldrh	w1, [x2, 38]
	.loc 3 977 0
	adrp	x4, .LANCHOR3
	.loc 3 979 0
	sub	w3, w3, #1
	.loc 3 977 0
	add	x5, x4, :lo12:.LANCHOR3
	mov	w0, 2048
	str	x4, [x29, 104]
	.loc 3 978 0
	udiv	w22, w23, w1
.LVL1576:
	.loc 3 977 0
	str	w0, [x5, 76]
	.loc 3 979 0
	udiv	w0, w3, w1
.LVL1577:
	.loc 3 983 0
	ldr	x3, [x5, -112]
	.loc 3 980 0
	sub	w24, w0, w22
	.loc 3 979 0
	str	w0, [x29, 148]
	.loc 3 982 0
	ldr	w0, [x2, 2572]
.LVL1578:
	.loc 3 980 0
	add	w25, w24, 1
.LVL1579:
	.loc 3 982 0
	add	w0, w0, w25
	str	w0, [x2, 2572]
	.loc 3 983 0
	cbz	x3, .L1509
	.loc 3 984 0
	ldr	w0, [x3, 24]
	cmp	w22, w0
	beq	.L1510
	.loc 3 985 0
	bl	FtlCacheWriteBack
.LVL1580:
.L1509:
	.loc 3 1008 0
	ldr	x0, [x29, 136]
	add	x19, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	cbz	w0, .L1551
	.loc 3 1008 0 is_stmt 0 discriminator 1
	ldrh	w1, [x19, 2404]
	add	x0, x19, 2400
	.loc 3 1009 0 is_stmt 1 discriminator 1
	add	x19, x19, 2448
	cmp	w1, 0
	csel	x19, x19, x0, eq
.LVL1581:
.L1512:
	.loc 3 1012 0
	adrp	x0, .LANCHOR1
	mov	w21, w22
.LVL1582:
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 672
	str	x0, [x29, 160]
.LVL1583:
.L1513:
	.loc 3 1011 0
	cbz	w25, .L1547
	.loc 3 1012 0
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x0, 12]
	cmp	w1, w0
	bcc	.L1514
	.loc 3 1012 0 is_stmt 0 discriminator 1
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1012
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1584:
.L1514:
	.loc 3 1013 0 is_stmt 1
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1515
	.loc 3 1014 0
	bl	FtlCacheWriteBack
.LVL1585:
	.loc 3 1016 0
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, 2400
	cmp	x19, x1
	bne	.L1516
	.loc 3 1017 0
	add	x0, x0, 2448
	ldrh	w1, [x0, 4]
	cbnz	w1, .L1517
	.loc 3 1018 0
	bl	allocate_new_data_superblock
.LVL1586:
	.loc 3 1019 0
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
.L1517:
	.loc 3 1021 0
	add	x19, x20, :lo12:.LANCHOR0
.LVL1587:
	add	x24, x19, 2400
	.loc 3 1023 0
	add	x19, x19, 2448
	.loc 3 1021 0
	mov	x0, x24
	bl	allocate_new_data_superblock
.LVL1588:
	.loc 3 1022 0
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0, 8]
	.loc 3 1023 0
	cmp	w0, 0
	csel	x19, x19, x24, ne
.LVL1589:
.L1518:
	.loc 3 1033 0
	ldrh	w0, [x19, 4]
	cbnz	w0, .L1515
	.loc 3 1034 0
	mov	x0, x19
	bl	allocate_new_data_superblock
.LVL1590:
.L1515:
	.loc 3 1043 0
	ldrh	w0, [x19, 4]
	.loc 3 1041 0
	ldrb	w1, [x19, 7]
	cmp	w0, w25
	csel	w0, w0, w25, ls
	lsl	w1, w1, 2
	cmp	w0, w1
	csel	w0, w0, w1, ls
	str	w0, [x29, 132]
	.loc 3 1045 0
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19, 6]
	ldrh	w0, [x0, 12]
	cmp	w1, w0
	bcc	.L1519
	.loc 3 1045 0 is_stmt 0 discriminator 1
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1045
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1591:
.L1519:
	.loc 3 1068 0 is_stmt 1 discriminator 1
	str	xzr, [x29, 168]
.L1520:
	.loc 3 1046 0 discriminator 1
	ldr	w1, [x29, 132]
	ldr	w28, [x29, 168]
.LVL1592:
	cmp	w28, w1
	bcc	.L1541
	mov	x28, x1
.L1521:
	.loc 3 1127 0
	ldr	x0, [x29, 104]
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x0, [x0, -112]
	cbz	x0, .L1542
	.loc 3 1129 0
	sub	w25, w25, #1
.LVL1593:
	.loc 3 1130 0
	subs	w28, w28, #1
	bne	.L1542
.L1547:
	.loc 3 1137 0
	ldr	w0, [x29, 148]
	.loc 3 1139 0
	add	x20, x20, :lo12:.LANCHOR0
.LVL1594:
	.loc 3 1137 0
	sub	w1, w0, w22
	mov	w0, 0
.LVL1595:
	bl	rk_ftl_garbage_collect
.LVL1596:
	.loc 3 1139 0
	ldrh	w0, [x20, 2392]
	cmp	w0, 15
	bhi	.L1544
	.loc 3 1141 0
	mov	w21, 65535
.LVL1597:
	.loc 3 1143 0
	mov	w19, 128
.LVL1598:
.L1567:
	.loc 3 1141 0
	ldrh	w0, [x20, 2648]
	cmp	w0, w21
	bne	.L1548
	.loc 3 1141 0 is_stmt 0 discriminator 1
	ldrh	w0, [x20, 3296]
	cmp	w0, w21
	bne	.L1548
	.loc 3 1142 0 is_stmt 1
	mov	w0, 0
	bl	List_get_gc_head_node
.LVL1599:
	bl	FtlGcRefreshBlock
.LVL1600:
.L1548:
	.loc 3 1145 0
	mov	w1, 1
	.loc 3 1143 0
	strh	w19, [x20, 3210]
	.loc 3 1145 0
	mov	w0, w1
	.loc 3 1144 0
	strh	w19, [x20, 3208]
	.loc 3 1145 0
	bl	rk_ftl_garbage_collect
.LVL1601:
	.loc 3 1146 0
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
.LVL1602:
	.loc 3 1147 0
	ldrh	w0, [x20, 2392]
	cmp	w0, 8
	bls	.L1567
	b	.L1544
.LVL1603:
.L1510:
	.loc 3 987 0
	ldr	w0, [x2, 2576]
	.loc 3 992 0
	ldr	x3, [x3, 8]
	.loc 3 987 0
	add	w0, w0, 1
	str	w0, [x2, 2576]
	.loc 3 988 0
	msub	w0, w22, w1, w23
	.loc 3 989 0
	sub	w1, w1, w0
	cmp	w26, w1
	.loc 3 992 0
	lsl	w0, w0, 9
	csel	w19, w26, w1, ls
	add	x0, x3, x0
	lsl	w21, w19, 9
	mov	x1, x27
	mov	w2, w21
	bl	ftl_memcpy
.LVL1604:
	.loc 3 994 0
	cbnz	w24, .L1511
.LVL1605:
.L1544:
	.loc 3 995 0
	mov	w0, 0
	b	.L1506
.LVL1606:
.L1511:
	.loc 3 996 0
	sub	w26, w26, w19
	.loc 3 997 0
	add	w23, w23, w19
	.loc 3 998 0
	add	x27, x27, x21
	.loc 3 999 0
	add	w22, w22, 1
	.loc 3 1000 0
	bl	FtlCacheWriteBack
.LVL1607:
	mov	w25, w24
	b	.L1509
.L1551:
	.loc 3 1006 0
	add	x19, x19, 2400
.LVL1608:
	b	.L1512
.LVL1609:
.L1516:
	.loc 3 1027 0
	ldr	x0, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR2
	str	wzr, [x0, 8]
	.loc 3 1028 0
	ldrh	w0, [x1, 4]
	cbnz	w0, .L1554
	.loc 3 1029 0
	mov	x0, x19
	bl	allocate_new_data_superblock
.LVL1610:
	b	.L1518
.L1554:
	mov	x19, x1
	b	.L1515
.LVL1611:
.L1541:
	.loc 3 1047 0
	ldrh	w0, [x19, 4]
	cbz	w0, .L1521
	.loc 3 1051 0
	add	x1, x29, 188
	mov	w2, 0
	mov	w0, w21
	bl	log2phys
.LVL1612:
	.loc 3 1052 0
	mov	x0, x19
	bl	get_new_active_ppa
.LVL1613:
	.loc 3 1054 0
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x1, [x29, 168]
	.loc 3 1055 0
	str	x3, [x29, 152]
	lsl	x24, x1, 5
	.loc 3 1054 0
	ldr	x1, [x3, 3976]
	.loc 3 1055 0
	ldrh	w2, [x3, 92]
	.loc 3 1054 0
	add	x1, x1, x24
	str	w0, [x1, 4]
.LVL1614:
	.loc 3 1055 0
	mul	w28, w28, w2
.LVL1615:
	ldr	x0, [x3, 3976]
	and	x1, x28, 4294967292
	str	x1, [x29, 120]
	add	x0, x0, x24
	ldr	x1, [x3, 3928]
	ldr	x3, [x29, 120]
	str	x1, [x29, 112]
	add	x28, x1, x3
	.loc 3 1056 0
	str	w21, [x0, 24]
	.loc 3 1055 0
	str	x28, [x0, 16]
	.loc 3 1059 0
	mov	w1, 0
	mov	x0, x28
	bl	ftl_memset
.LVL1616:
	.loc 3 1061 0
	ldr	w0, [x29, 148]
	cmp	w21, w22
	ldr	x3, [x29, 152]
.LVL1617:
	ccmp	w21, w0, 4, ne
	bne	.L1522
	.loc 3 1062 0
	cmp	w21, w22
	bne	.L1523
	.loc 3 1063 0
	ldrh	w2, [x3, 38]
	udiv	w0, w23, w2
.LVL1618:
	msub	w0, w0, w2, w23
	str	w0, [x29, 144]
	.loc 3 1064 0
	sub	w2, w2, w0
	cmp	w2, w26
	csel	w0, w2, w26, ls
	str	w0, [x29, 152]
.L1524:
	.loc 3 1072 0
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x29, 152]
	ldrh	w1, [x0, 38]
	cmp	w2, w1
	bne	.L1525
	.loc 3 1074 0
	ldr	x0, [x0, 3976]
	.loc 3 1073 0
	cmp	w21, w22
	.loc 3 1074 0
	add	x24, x0, x24
	.loc 3 1073 0
	bne	.L1526
	.loc 3 1074 0
	str	x27, [x24, 8]
.LVL1619:
.L1527:
	.loc 3 1119 0
	add	x0, x20, :lo12:.LANCHOR0
.LVL1620:
	ldrb	w1, [x19, 6]
	ldrh	w0, [x0, 12]
	cmp	w1, w0
	bcc	.L1538
	.loc 3 1119 0 is_stmt 0 discriminator 1
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1119
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1621:
.L1538:
	.loc 3 1120 0 is_stmt 1
	ldp	x1, x2, [x29, 112]
	mov	w0, -3947
	strh	w0, [x1, x2]
	.loc 3 1121 0
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w0, [x1, 2600]
	.loc 3 1122 0
	stp	w0, w21, [x28, 4]
	.loc 3 1125 0
	add	w21, w21, 1
	.loc 3 1121 0
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1, 2600]
	.loc 3 1123 0
	ldr	w0, [x29, 188]
.LVL1622:
	str	w0, [x28, 12]
	.loc 3 1124 0
	ldrh	w0, [x19]
.LVL1623:
	strh	w0, [x28, 2]
	ldr	x0, [x29, 168]
	add	x0, x0, 1
	str	x0, [x29, 168]
	b	.L1520
.LVL1624:
.L1523:
	.loc 3 1069 0
	ldrh	w0, [x3, 38]
.LVL1625:
	add	w2, w23, w26
	.loc 3 1068 0
	str	wzr, [x29, 144]
	.loc 3 1069 0
	msub	w2, w21, w0, w2
	and	w0, w2, 65535
	str	w0, [x29, 152]
	b	.L1524
.L1526:
	.loc 3 1076 0
	ldr	w0, [x29, 152]
.L1570:
	.loc 3 1117 0
	mul	w0, w0, w21
	sub	w0, w0, w23
	lsl	w0, w0, 9
	add	x0, x27, x0
	str	x0, [x24, 8]
	b	.L1527
.L1525:
	.loc 3 1080 0
	ldr	x1, [x0, 3976]
	.loc 3 1079 0
	cmp	w21, w22
	.loc 3 1080 0
	add	x1, x1, x24
	.loc 3 1079 0
	bne	.L1528
	.loc 3 1080 0
	ldr	x0, [x0, 3936]
.L1569:
	.loc 3 1082 0
	str	x0, [x1, 8]
	.loc 3 1084 0
	ldr	w0, [x29, 188]
	cmn	w0, #1
	beq	.L1530
.LBB325:
	.loc 3 1088 0
	add	x3, x20, :lo12:.LANCHOR0
	.loc 3 1086 0
	str	w0, [x29, 196]
	.loc 3 1087 0
	str	w21, [x29, 216]
	.loc 3 1090 0
	mov	w2, 0
	.loc 3 1088 0
	str	x3, [x29, 96]
	ldr	x0, [x3, 3976]
	add	x0, x0, x24
	.loc 3 1089 0
	ldp	x1, x0, [x0, 8]
	stp	x1, x0, [x29, 200]
	.loc 3 1090 0
	mov	w1, 1
	add	x0, x29, 192
	bl	FlashReadPages
.LVL1626:
	.loc 3 1091 0
	ldr	w0, [x29, 192]
	ldr	x3, [x29, 96]
	cmn	w0, #1
	bne	.L1531
	.loc 3 1092 0
	ldr	w0, [x3, 2768]
	add	w0, w0, 1
	str	w0, [x3, 2768]
.L1534:
	ldr	w0, [x29, 152]
.LBE325:
	.loc 3 1104 0
	cmp	w21, w22
	lsl	w2, w0, 9
	.loc 3 1105 0
	add	x0, x20, :lo12:.LANCHOR0
	.loc 3 1104 0
	bne	.L1535
	.loc 3 1105 0
	ldr	x1, [x0, 3976]
	ldr	w0, [x29, 144]
	add	x1, x1, x24
	lsl	w0, w0, 9
	ldr	x3, [x1, 8]
	mov	x1, x27
	add	x0, x3, x0
	bl	ftl_memcpy
.LVL1627:
	.loc 3 1109 0
	ldr	w0, [x29, 148]
.LVL1628:
	cmp	w21, w0
	bne	.L1527
.L1536:
	.loc 3 1110 0
	ldrh	w0, [x19, 4]
.LVL1629:
	cbz	w0, .L1527
	.loc 3 1111 0
	ldr	x0, [x29, 104]
	add	x1, x0, :lo12:.LANCHOR3
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0, 3976]
	.loc 3 1112 0
	str	x19, [x1, 56]
	.loc 3 1111 0
	add	x24, x0, x24
	str	x24, [x1, -112]
	b	.L1527
.L1528:
	.loc 3 1082 0
	ldr	x0, [x0, 3920]
	b	.L1569
.L1531:
.LBB326:
	.loc 3 1094 0
	ldr	w0, [x28, 8]
	cmp	w21, w0
	beq	.L1533
	.loc 3 1095 0
	ldr	w0, [x3, 2768]
	.loc 3 1096 0
	mov	w2, w21
	.loc 3 1095 0
	add	w0, w0, 1
	str	w0, [x3, 2768]
	.loc 3 1096 0
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	ldr	w1, [x28, 8]
	bl	printk
.LVL1630:
.L1533:
	.loc 3 1098 0
	ldr	w0, [x28, 8]
	cmp	w21, w0
	beq	.L1534
	.loc 3 1098 0 is_stmt 0 discriminator 1
	ldr	x1, [x29, 160]
	mov	w2, 1098
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1631:
	b	.L1534
.L1530:
.LBE326:
	.loc 3 1101 0 is_stmt 1
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1, 3976]
	ldrh	w2, [x1, 90]
	mov	w1, 0
	add	x0, x0, x24
	ldr	x0, [x0, 8]
	bl	ftl_memset
.LVL1632:
	b	.L1534
.L1535:
	.loc 3 1107 0
	ldrh	w1, [x0, 38]
	ldr	x0, [x0, 3976]
	add	x0, x0, x24
	mul	w1, w1, w21
	ldr	x0, [x0, 8]
	sub	w1, w1, w23
	lsl	w1, w1, 9
	add	x1, x27, x1
	bl	ftl_memcpy
.LVL1633:
	b	.L1536
.LVL1634:
.L1522:
	.loc 3 1117 0
	ldr	x0, [x3, 3976]
.LVL1635:
	add	x24, x0, x24
	ldrh	w0, [x3, 38]
	b	.L1570
.LVL1636:
.L1542:
	.loc 3 1133 0
	add	x0, x20, :lo12:.LANCHOR0
	mov	x3, x19
	mov	w2, 0
	mov	w1, w28
	ldr	x0, [x0, 3976]
	bl	FtlProgPages
.LVL1637:
	.loc 3 1134 0
	cmp	w25, w28
	bcs	.L1545
	.loc 3 1134 0 is_stmt 0 discriminator 1
	ldr	x1, [x29, 160]
	adrp	x0, .LC1
	mov	w2, 1134
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1638:
.L1545:
	.loc 3 1135 0 is_stmt 1
	sub	w25, w25, w28
	b	.L1513
.LVL1639:
.L1549:
	.loc 3 975 0
	mov	w0, -1
	b	.L1506
	.cfi_endproc
.LFE156:
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_write
	.type	sftl_write, %function
sftl_write:
.LFB159:
	.loc 3 1209 0
	.cfi_startproc
.LVL1640:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 3 1210 0
	mov	x3, x2
	mov	w2, w1
.LVL1641:
	mov	w1, w0
.LVL1642:
	.loc 3 1209 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 3 1210 0
	mov	w0, 0
.LVL1643:
	.loc 3 1209 0
	.loc 3 1210 0
	bl	FtlWrite
.LVL1644:
	.loc 3 1211 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE159:
	.size	sftl_write, .-sftl_write
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
.LFB213:
	.loc 2 1409 0
	.cfi_startproc
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	.loc 2 1417 0
	mov	w1, 0
	.loc 2 1409 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	.loc 2 1414 0
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	.loc 2 1409 0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.loc 2 1414 0
	add	x21, x19, 3344
	.loc 2 1409 0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -16
	.cfi_offset 24, -8
	.loc 2 1414 0
	ldr	x0, [x19, 3376]
	str	x0, [x21, 8]
	.loc 2 1415 0
	ldr	x0, [x19, 3384]
	str	x0, [x21, 16]
	.loc 2 1417 0
	ldr	x0, [x19, 2352]
	ldrh	w2, [x19, 20]
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL1645:
	.loc 2 1418 0
	ldrh	w0, [x19, 2632]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1574
.LVL1646:
.L1582:
	.loc 2 1419 0
	mov	w0, -1
.L1573:
	.loc 2 1512 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
.LVL1647:
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.L1574:
	.cfi_restore_state
	add	x23, x19, 2632
	.loc 2 1421 0
	mov	w1, 1
	bl	FtlGetLastWrittenPage
.LVL1648:
	sxth	w22, w0
.LVL1649:
	.loc 2 1422 0
	add	w0, w0, 1
	.loc 2 1431 0
	mov	w24, 61604
	.loc 2 1422 0
	strh	w0, [x23, 2]
	.loc 2 1430 0
	mov	w23, 19539
	movk	w23, 0x4654, lsl 16
.L1576:
	.loc 2 1424 0
	tbz	w22, #31, .L1579
	.loc 2 1437 0
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1437
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1650:
.L1578:
	.loc 2 1439 0
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x1, 20]
	ldrh	w1, [x1, 90]
	add	x0, x0, 24
	cmp	x1, x0, lsl 1
	bcs	.L1581
	.loc 2 1439 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1439
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1651:
.L1581:
	.loc 2 1441 0 is_stmt 1
	add	x19, x20, :lo12:.LANCHOR0
	mov	w2, 48
	add	x21, x19, 2288
	mov	x0, x21
	ldr	x1, [x19, 3352]
	bl	ftl_memcpy
.LVL1652:
	.loc 2 1442 0
	ldr	x0, [x19, 2352]
	ldrh	w2, [x19, 20]
	ldr	x1, [x19, 3352]
	lsl	w2, w2, 1
	add	x1, x1, 48
	bl	ftl_memcpy
.LVL1653:
	.loc 2 1443 0
	ldrh	w1, [x19, 20]
	ldr	x0, [x19, 3352]
	lsr	w2, w1, 3
	ubfiz	x1, x1, 1, 16
	add	x1, x1, 48
	add	w2, w2, 4
	and	x1, x1, -4
	add	x1, x0, x1
	ldr	x0, [x20, #:lo12:.LANCHOR0]
	bl	ftl_memcpy
.LVL1654:
	.loc 2 1445 0
	ldr	w1, [x19, 2288]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1582
	.loc 2 1448 0
	ldrh	w1, [x19, 2296]
	.loc 2 1449 0
	ldrb	w2, [x19, 2298]
	ldrh	w0, [x19, 34]
	.loc 2 1448 0
	strh	w1, [x19, 2638]
	.loc 2 1449 0
	cmp	w2, w0
	bne	.L1582
	.loc 2 1452 0
	adrp	x0, .LANCHOR3+80
	.loc 2 1454 0
	ldrh	w2, [x19, 38]
	.loc 2 1455 0
	ldrh	w3, [x19, 12]
	.loc 2 1452 0
	str	w1, [x0, #:lo12:.LANCHOR3+80]
	.loc 2 1453 0
	ldrh	w0, [x19, 82]
	mul	w0, w0, w1
	str	w0, [x19, 2560]
	.loc 2 1454 0
	mul	w0, w2, w0
	.loc 2 1455 0
	ldr	w2, [x19, 24]
	.loc 2 1454 0
	str	w0, [x19, 120]
	.loc 2 1455 0
	ldrh	w0, [x19, 142]
	.loc 2 1461 0
	cmp	w1, w2
	.loc 2 1455 0
	sub	w0, w2, w0
	sub	w0, w0, w1
	udiv	w0, w0, w3
	strh	w0, [x19, 2628]
	.loc 2 1461 0
	bls	.L1583
	.loc 2 1461 0 is_stmt 0 discriminator 1
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC1
	mov	w2, 1461
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC1
	bl	printk
.LVL1655:
.L1583:
	.loc 2 1463 0 is_stmt 1
	add	x0, x20, :lo12:.LANCHOR0
	.loc 2 1468 0
	mov	w3, -1
	.loc 2 1464 0
	ldrh	w2, [x0, 2304]
	.loc 2 1463 0
	ldrh	w4, [x0, 2302]
	strh	w4, [x0, 2400]
	.loc 2 1464 0
	lsr	w5, w2, 6
	.loc 2 1465 0
	and	w2, w2, 63
	strb	w2, [x0, 2406]
	.loc 2 1466 0
	ldrb	w2, [x0, 2299]
	strb	w2, [x0, 2408]
	.loc 2 1473 0
	ldrh	w2, [x0, 2306]
	strh	w2, [x0, 2448]
	.loc 2 1474 0
	ldrh	w2, [x0, 2308]
	.loc 2 1464 0
	strh	w5, [x0, 2402]
	.loc 2 1468 0
	strh	w3, [x0, 2648]
	.loc 2 1474 0
	lsr	w5, w2, 6
	.loc 2 1475 0
	and	w2, w2, 63
	strb	w2, [x0, 2454]
	.loc 2 1476 0
	ldrb	w2, [x0, 2300]
	strb	w2, [x0, 2456]
	.loc 2 1478 0
	ldrh	w2, [x0, 2310]
	strh	w2, [x0, 2496]
	.loc 2 1479 0
	ldrh	w2, [x0, 2312]
	.loc 2 1474 0
	strh	w5, [x0, 2450]
	.loc 2 1469 0
	strh	wzr, [x0, 2650]
	.loc 2 1479 0
	lsr	w5, w2, 6
	.loc 2 1480 0
	and	w2, w2, 63
	strb	w2, [x0, 2502]
	.loc 2 1481 0
	ldrb	w2, [x0, 2301]
	strb	w2, [x0, 2504]
	.loc 2 1488 0
	ldr	w2, [x0, 2320]
	str	w2, [x0, 2604]
	.loc 2 1470 0
	strb	wzr, [x0, 2654]
	.loc 2 1471 0
	strb	wzr, [x0, 2656]
	.loc 2 1479 0
	strh	w5, [x0, 2498]
	.loc 2 1485 0
	str	wzr, [x0, 2564]
	.loc 2 1484 0
	str	wzr, [x0, 2572]
	.loc 2 1486 0
	str	wzr, [x0, 2580]
	.loc 2 1483 0
	str	wzr, [x0, 2584]
	.loc 2 1489 0
	str	wzr, [x0, 2608]
	.loc 2 1490 0
	str	wzr, [x0, 2620]
	.loc 2 1493 0
	ldr	w1, [x0, 2328]
	ldr	w2, [x0, 2596]
	.loc 2 1491 0
	str	wzr, [x0, 2576]
	.loc 2 1493 0
	cmp	w1, w2
	bls	.L1584
	.loc 2 1494 0
	str	w1, [x0, 2596]
.L1584:
	.loc 2 1496 0
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w1, [x0, 2324]
	ldr	w2, [x0, 2600]
	cmp	w1, w2
	bls	.L1585
	.loc 2 1497 0
	str	w1, [x0, 2600]
.L1585:
	.loc 2 1499 0
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1586
	.loc 2 1500 0
	add	x0, x20, :lo12:.LANCHOR0
	add	x0, x0, 2400
	bl	make_superblock
.LVL1656:
.L1586:
	.loc 2 1502 0
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2448
	ldrh	w2, [x1, 2448]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1587
	.loc 2 1503 0
	bl	make_superblock
.LVL1657:
.L1587:
	.loc 2 1505 0
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x1, 2496
	ldrh	w2, [x1, 2496]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1588
	.loc 2 1506 0
	bl	make_superblock
.LVL1658:
.L1588:
	.loc 2 1508 0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x0, x20, 2648
	ldrh	w2, [x20, 2648]
	cmp	w2, w1
	beq	.L1589
	.loc 2 1509 0
	bl	make_superblock
.LVL1659:
.L1589:
	.loc 2 1511 0
	mov	w0, 0
	b	.L1573
.L1579:
	.loc 2 1425 0
	ldrh	w0, [x19, 2632]
	.loc 2 1427 0
	mov	w2, 1
	mov	w1, w2
	.loc 2 1425 0
	orr	w0, w22, w0, lsl 10
	str	w0, [x21, 4]
	.loc 2 1426 0
	ldr	x0, [x19, 3376]
	str	x0, [x21, 8]
	.loc 2 1427 0
	mov	x0, x21
	bl	FlashReadPages
.LVL1660:
	.loc 2 1430 0
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1577
	.loc 2 1431 0 discriminator 1
	ldr	x0, [x19, 3376]
	.loc 2 1430 0 discriminator 1
	ldr	w0, [x0]
	cmp	w0, w23
	bne	.L1577
	.loc 2 1432 0
	ldr	x0, [x19, 3384]
	ldrh	w0, [x0]
	.loc 2 1431 0
	cmp	w0, w24
	beq	.L1578
.L1577:
	sub	w22, w22, #1
.LVL1661:
	sxth	w22, w22
.LVL1662:
	b	.L1576
	.cfi_endproc
.LFE213:
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
.LFB231:
	.loc 2 2356 0
	.cfi_startproc
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	.loc 2 2362 0
	adrp	x20, .LANCHOR0
	.loc 2 2356 0
	stp	x21, x22, [sp, 32]
	.loc 2 2362 0
	add	x19, x20, :lo12:.LANCHOR0
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.loc 2 2360 0
	adrp	x21, .LANCHOR3
	add	x0, x21, :lo12:.LANCHOR3
	.loc 2 2356 0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -16
	.cfi_offset 24, -8
	.loc 2 2360 0
	strh	wzr, [x0, -88]
	.loc 2 2362 0
	ldrh	w0, [x19, 16]
	bl	FtlFreeSysBlkQueueInit
.LVL1663:
	.loc 2 2363 0
	bl	FtlScanSysBlk
.LVL1664:
	.loc 2 2364 0
	ldrh	w1, [x19, 2632]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1596
.L1598:
	.loc 2 2365 0
	mov	w24, -1
.L1595:
	.loc 2 2448 0
	mov	w0, w24
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
.L1596:
	.cfi_restore_state
	.loc 2 2367 0
	bl	FtlLoadSysInfo
.LVL1665:
	mov	w24, w0
	cbnz	w0, .L1598
	.loc 2 2370 0
	bl	FtlLoadMapInfo
.LVL1666:
	.loc 2 2371 0
	bl	FtlLoadVonderInfo
.LVL1667:
	.loc 2 2372 0
	bl	Ftl_load_ext_data
.LVL1668:
	.loc 2 2374 0
	bl	FtlLoadEctTbl
.LVL1669:
	.loc 2 2375 0
	bl	FtlFreeSysBLkSort
.LVL1670:
	.loc 2 2377 0
	bl	SupperBlkListInit
.LVL1671:
	.loc 2 2378 0
	bl	FtlPowerLostRecovery
.LVL1672:
	.loc 2 2380 0
	mov	w0, 1
	bl	FtlUpdateVaildLpn
.LVL1673:
	ldr	x1, [x19, 2544]
.LBB330:
.LBB331:
	.loc 2 2382 0
	mov	w0, 0
.LVL1674:
	ldrh	w3, [x19, 118]
	add	x1, x1, 4
.L1599:
	cmp	w0, w3
	bge	.L1604
	.loc 2 2383 0
	ldr	w2, [x1], 16
	tbz	w2, #31, .L1600
.L1604:
	.loc 2 2386 0
	add	x1, x20, :lo12:.LANCHOR0
	.loc 2 2388 0
	cmp	w0, w3
	.loc 2 2386 0
	ldrh	w2, [x1, 2316]
	add	w2, w2, 1
	strh	w2, [x1, 2316]
	.loc 2 2388 0
	bge	.L1610
.L1601:
	.loc 2 2389 0
	add	x19, x20, :lo12:.LANCHOR0
	.loc 2 2409 0
	add	x21, x21, :lo12:.LANCHOR3
	.loc 2 2389 0
	add	x23, x19, 2400
	.loc 2 2390 0
	add	x22, x19, 2448
	.loc 2 2389 0
	mov	x0, x23
.LVL1675:
	bl	FtlSuperblockPowerLostFix
.LVL1676:
	.loc 2 2390 0
	mov	x0, x22
	bl	FtlSuperblockPowerLostFix
.LVL1677:
.LBB332:
	.loc 2 2396 0
	ldrh	w0, [x19, 2400]
	ldr	x2, [x19, 2352]
	ldrh	w3, [x19, 2404]
	lsl	x0, x0, 1
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	.loc 2 2398 0
	strb	wzr, [x19, 2406]
	.loc 2 2397 0
	ldrh	w0, [x19, 82]
	strh	w0, [x19, 2402]
	.loc 2 2402 0
	ldrh	w0, [x19, 2448]
	ldr	x2, [x19, 2352]
	.loc 2 2399 0
	strh	wzr, [x19, 2404]
	.loc 2 2402 0
	lsl	x0, x0, 1
	ldrh	w3, [x19, 2452]
	ldrh	w1, [x2, x0]
	sub	w1, w1, w3
	strh	w1, [x2, x0]
	.loc 2 2404 0
	strb	wzr, [x19, 2454]
	.loc 2 2403 0
	ldrh	w0, [x19, 82]
	strh	w0, [x19, 2450]
.LBE332:
	.loc 2 2409 0
	sub	x0, x21, #248
.LBB333:
	.loc 2 2405 0
	strh	wzr, [x19, 2452]
.LBE333:
	.loc 2 2409 0
	bl	FtlMapBlkWriteDump_data
.LVL1678:
	.loc 2 2410 0
	sub	x0, x21, #176
	bl	FtlMapBlkWriteDump_data
.LVL1679:
	.loc 2 2422 0
	ldrh	w0, [x19, 2318]
	add	w0, w0, 1
	strh	w0, [x19, 2318]
	.loc 2 2423 0
	bl	l2p_flush
.LVL1680:
	.loc 2 2424 0
	bl	FtlVpcTblFlush
.LVL1681:
	.loc 2 2425 0
	bl	FtlVpcTblFlush
.LVL1682:
	b	.L1605
.LVL1683:
.L1600:
	.loc 2 2382 0
	add	w0, w0, 1
	b	.L1599
.L1610:
	.loc 2 2388 0
	add	x0, x21, :lo12:.LANCHOR3
.LVL1684:
	ldrh	w0, [x0, -88]
	cbnz	w0, .L1601
.L1605:
	.loc 2 2429 0
	add	x19, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x22, x19, 2400
	ldrh	w0, [x19, 2400]
	cmp	w0, w1
	beq	.L1606
	ldrh	w1, [x19, 2404]
	cbnz	w1, .L1606
	.loc 2 2430 0
	ldrh	w1, [x19, 2452]
	.loc 2 2431 0
	add	x21, x19, 2448
	.loc 2 2430 0
	cbnz	w1, .L1606
	.loc 2 2432 0
	bl	FtlGcRefreshBlock
.LVL1685:
	.loc 2 2433 0
	ldrh	w0, [x19, 2448]
	bl	FtlGcRefreshBlock
.LVL1686:
	.loc 2 2434 0
	mov	x0, x22
	bl	allocate_new_data_superblock
.LVL1687:
	.loc 2 2435 0
	mov	x0, x21
	bl	allocate_new_data_superblock
.LVL1688:
.L1606:
	.loc 2 2440 0
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20, 2316]
	tst	x0, 31
	bne	.L1595
	.loc 2 2441 0
	bl	FtlVpcCheckAndModify
.LVL1689:
	b	.L1595
.LBE331:
.LBE330:
	.cfi_endproc
.LFE231:
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	ftl_low_format
	.type	ftl_low_format, %function
ftl_low_format:
.LFB149:
	.loc 3 623 0
	.cfi_startproc
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	.loc 3 629 0
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	.loc 3 623 0
	stp	x21, x22, [sp, 32]
	str	x23, [sp, 48]
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.cfi_offset 23, -16
	.loc 3 629 0
	str	wzr, [x0, 2596]
	.loc 3 630 0
	str	wzr, [x0, 2600]
	.loc 3 631 0
	ldrh	w0, [x0, 16]
	bl	FtlFreeSysBlkQueueInit
.LVL1690:
	.loc 3 632 0
	bl	FtlLoadBbt
.LVL1691:
	cbz	w0, .L1613
	.loc 3 633 0
	bl	FtlMakeBbt
.LVL1692:
.L1613:
	.loc 3 637 0 discriminator 1
	mov	w4, 23752
	.loc 3 635 0 discriminator 1
	add	x1, x20, :lo12:.LANCHOR0
	.loc 3 623 0 discriminator 1
	mov	w0, 0
.LVL1693:
	.loc 3 637 0 discriminator 1
	movk	w4, 0xa0f, lsl 16
.L1614:
	.loc 3 635 0 discriminator 1
	ldrh	w2, [x1, 38]
	cmp	w0, w2, lsl 7
	blt	.L1615
	.loc 3 642 0
	ldrh	w21, [x1, 20]
	add	x22, x20, :lo12:.LANCHOR0
	.loc 3 641 0
	mov	w19, 0
.LVL1694:
.L1616:
	.loc 3 642 0 discriminator 1
	ldrh	w0, [x22, 22]
.LVL1695:
	cmp	w0, w21
	bhi	.L1617
	.loc 3 645 0
	ldrh	w0, [x22, 12]
	sub	w1, w19, #3
	cmp	w1, w0, lsl 1
	bge	.L1618
.L1622:
	.loc 3 656 0
	add	x19, x20, :lo12:.LANCHOR0
.LVL1696:
	.loc 3 641 0
	mov	w21, 0
.LVL1697:
	mov	w22, 0
.L1619:
	.loc 3 656 0 discriminator 1
	ldrh	w0, [x19, 20]
	cmp	w0, w22
	bhi	.L1623
	.loc 3 661 0
	ldrh	w2, [x19, 12]
	ldr	w1, [x19, 24]
	.loc 3 658 0
	ldrh	w0, [x19, 22]
	str	w0, [x19, 3912]
	.loc 3 661 0
	udiv	w4, w1, w2
	.loc 3 662 0
	ubfx	x3, x4, 5, 16
	.loc 3 661 0
	str	w4, [x19, 2560]
	.loc 3 662 0
	add	w0, w3, 36
	strh	w0, [x19, 2628]
	.loc 3 663 0
	mov	w0, 24
	mul	w0, w2, w0
	cmp	w21, w0
	ble	.L1624
	.loc 3 664 0
	sub	w1, w1, w21
	udiv	w1, w1, w2
	str	w1, [x19, 2560]
	.loc 3 665 0
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x19, 2628]
.L1624:
	.loc 3 668 0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 74]
	cbz	w1, .L1626
	.loc 3 669 0
	ldrh	w5, [x0, 2628]
	add	w5, w5, w1, lsr 1
	strh	w5, [x0, 2628]
	.loc 3 670 0
	mul	w5, w1, w2
	cmp	w21, w5
	bge	.L1626
	.loc 3 672 0
	add	w1, w1, 32
	.loc 3 671 0
	str	w4, [x0, 2560]
	.loc 3 672 0
	add	w1, w3, w1
	strh	w1, [x0, 2628]
.L1626:
	.loc 3 677 0
	add	x19, x20, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR3
	.loc 3 694 0
	add	x21, x19, 2400
.LVL1698:
	.loc 3 689 0
	mov	w23, -1
	.loc 3 677 0
	ldrh	w1, [x19, 2628]
	ldr	w0, [x19, 2560]
	sub	w0, w0, w1
	add	x1, x22, :lo12:.LANCHOR3
	mul	w0, w0, w2
	str	w0, [x1, 80]
	.loc 3 678 0
	ldrh	w1, [x19, 82]
	mul	w0, w1, w0
	.loc 3 679 0
	ldrh	w1, [x19, 38]
	.loc 3 678 0
	str	w0, [x19, 2560]
	.loc 3 679 0
	mul	w0, w1, w0
	str	w0, [x19, 120]
	.loc 3 684 0
	bl	FtlBbmTblFlush
.LVL1699:
	.loc 3 687 0
	ldr	x0, [x19, 2352]
	mov	w1, 0
	ldrh	w2, [x19, 22]
	lsl	w2, w2, 1
	bl	ftl_memset
.LVL1700:
	.loc 3 697 0
	mov	w0, 1
	strb	w0, [x21, 8]
	.loc 3 698 0
	ldr	x0, [x20, #:lo12:.LANCHOR0]
	mov	w1, 255
	ldrh	w2, [x19, 20]
	.loc 3 689 0
	strh	w23, [x19, 2648]
	.loc 3 690 0
	strh	wzr, [x19, 2650]
	.loc 3 691 0
	strb	wzr, [x19, 2654]
	.loc 3 698 0
	lsr	w2, w2, 3
	.loc 3 692 0
	strb	wzr, [x19, 2656]
	.loc 3 694 0
	strh	wzr, [x21, 2]
	.loc 3 695 0
	strb	wzr, [x21, 6]
	.loc 3 696 0
	strh	wzr, [x19, 2400]
	.loc 3 688 0
	str	wzr, [x19, 2556]
	.loc 3 698 0
	bl	ftl_memset
.LVL1701:
.L1628:
	.loc 3 701 0
	mov	x0, x21
	bl	make_superblock
.LVL1702:
	.loc 3 702 0
	ldrb	w1, [x21, 7]
	ldrh	w0, [x21]
	cbnz	w1, .L1629
	.loc 3 705 0
	ldr	x1, [x19, 2352]
	ubfiz	x0, x0, 1, 16
	strh	w23, [x1, x0]
	.loc 3 706 0
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	.loc 3 707 0
	b	.L1628
.LVL1703:
.L1615:
	.loc 3 636 0 discriminator 3
	ldr	x5, [x1, 3936]
	ubfiz	x3, x0, 2, 16
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	.loc 3 635 0 discriminator 3
	add	w0, w0, 1
.LVL1704:
	and	w0, w0, 65535
.LVL1705:
	.loc 3 636 0 discriminator 3
	str	w2, [x5, x3]
	.loc 3 637 0 discriminator 3
	ldr	x2, [x1, 3920]
	str	w4, [x2, x3]
	b	.L1614
.LVL1706:
.L1617:
	.loc 3 643 0 discriminator 3
	mov	w0, w21
	mov	w1, 1
	.loc 3 642 0 discriminator 3
	add	w21, w21, 1
	.loc 3 643 0 discriminator 3
	bl	FtlLowFormatEraseBlock
.LVL1707:
	add	w19, w19, w0
.LVL1708:
	.loc 3 642 0 discriminator 3
	and	w21, w21, 65535
	.loc 3 643 0 discriminator 3
	and	w19, w19, 65535
.LVL1709:
	b	.L1616
.L1618:
	.loc 3 647 0
	udiv	w0, w19, w0
	ldr	w19, [x22, 112]
.LVL1710:
	.loc 3 650 0
	add	x21, x20, :lo12:.LANCHOR0
	.loc 3 647 0
	add	w0, w0, w19
	bl	FtlSysBlkNumInit
.LVL1711:
	.loc 3 648 0
	ldrh	w0, [x22, 16]
	bl	FtlFreeSysBlkQueueInit
.LVL1712:
	.loc 3 650 0
	ldrh	w19, [x22, 20]
.L1620:
	.loc 3 650 0 is_stmt 0 discriminator 1
	ldrh	w0, [x21, 22]
	cmp	w0, w19
	bls	.L1622
	.loc 3 651 0 is_stmt 1 discriminator 3
	mov	w0, w19
	.loc 3 650 0 discriminator 3
	add	w19, w19, 1
	.loc 3 651 0 discriminator 3
	mov	w1, 1
	.loc 3 650 0 discriminator 3
	and	w19, w19, 65535
	.loc 3 651 0 discriminator 3
	bl	FtlLowFormatEraseBlock
.LVL1713:
	b	.L1620
.LVL1714:
.L1623:
	.loc 3 657 0 discriminator 3
	mov	w0, w22
	mov	w1, 0
	.loc 3 656 0 discriminator 3
	add	w22, w22, 1
	.loc 3 657 0 discriminator 3
	bl	FtlLowFormatEraseBlock
.LVL1715:
	add	w21, w21, w0
.LVL1716:
	.loc 3 656 0 discriminator 3
	and	w22, w22, 65535
	.loc 3 657 0 discriminator 3
	and	w21, w21, 65535
.LVL1717:
	b	.L1619
.LVL1718:
.L1629:
	.loc 3 709 0
	ldr	w1, [x19, 2596]
	.loc 3 710 0
	ubfiz	x0, x0, 1, 16
	.loc 3 709 0
	str	w1, [x21, 12]
	add	w1, w1, 1
	str	w1, [x19, 2596]
	.loc 3 710 0
	ldr	x1, [x19, 2352]
	ldrh	w2, [x21, 4]
	strh	w2, [x1, x0]
	.loc 3 712 0
	add	x0, x19, 2448
	strh	wzr, [x19, 2450]
	.loc 3 714 0
	ldrh	w1, [x21]
	.loc 3 721 0
	mov	w21, -1
	.loc 3 713 0
	strb	wzr, [x19, 2454]
	.loc 3 714 0
	add	w1, w1, 1
	strh	w1, [x19, 2448]
	.loc 3 715 0
	mov	w1, 1
	strb	w1, [x19, 2456]
	.loc 3 717 0
	add	x19, x20, :lo12:.LANCHOR0
	mov	x20, x0
.L1630:
	mov	x0, x20
	bl	make_superblock
.LVL1719:
	.loc 3 718 0
	ldrb	w1, [x20, 7]
	ldrh	w0, [x20]
	cbnz	w1, .L1631
	.loc 3 721 0
	ldr	x1, [x19, 2352]
	ubfiz	x0, x0, 1, 16
	strh	w21, [x1, x0]
	.loc 3 722 0
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	.loc 3 723 0
	b	.L1630
.L1631:
	.loc 3 725 0
	ldr	w1, [x19, 2596]
	.loc 3 726 0
	ubfiz	x0, x0, 1, 16
	.loc 3 725 0
	str	w1, [x20, 12]
	.loc 3 733 0
	add	x22, x22, :lo12:.LANCHOR3
	.loc 3 725 0
	add	w1, w1, 1
	str	w1, [x19, 2596]
	.loc 3 726 0
	ldr	x1, [x19, 2352]
	ldrh	w2, [x20, 4]
	.loc 3 727 0
	mov	w20, -1
	.loc 3 726 0
	strh	w2, [x1, x0]
	.loc 3 727 0
	strh	w20, [x19, 2496]
	.loc 3 730 0
	bl	FtlFreeSysBlkQueueOut
.LVL1720:
	strh	w0, [x19, 2632]
	.loc 3 733 0
	ldr	w0, [x22, 80]
	strh	w0, [x19, 2638]
	.loc 3 734 0
	ldr	w0, [x19, 2596]
	str	w0, [x19, 2640]
	add	w0, w0, 1
	.loc 3 731 0
	strh	wzr, [x19, 2634]
	.loc 3 732 0
	strh	w20, [x19, 2636]
	.loc 3 734 0
	str	w0, [x19, 2596]
	.loc 3 735 0
	bl	FtlVpcTblFlush
.LVL1721:
	.loc 3 736 0
	bl	FtlSysBlkInit
.LVL1722:
	cbnz	w0, .L1632
	.loc 3 737 0
	adrp	x0, .LANCHOR2
	mov	w1, 1
	str	w1, [x0, #:lo12:.LANCHOR2]
.L1632:
	.loc 3 739 0
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE149:
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	sftl_init
	.type	sftl_init, %function
sftl_init:
.LFB150:
	.loc 3 743 0
	.cfi_startproc
	stp	x29, x30, [sp, -32]!
	.cfi_def_cfa_offset 32
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	.loc 3 749 0
	adrp	x1, .LC70
	add	x1, x1, :lo12:.LC70
	.loc 3 746 0
	mov	w0, -1
	.loc 3 743 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -16
	.cfi_offset 20, -8
	.loc 3 746 0
	adrp	x20, .LANCHOR2
	.loc 3 753 0
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	.loc 3 746 0
	str	w0, [x20, #:lo12:.LANCHOR2]
	.loc 3 749 0
	adrp	x0, .LC71
	add	x0, x0, :lo12:.LC71
	bl	printk
.LVL1723:
	.loc 3 750 0
	adrp	x0, g_nand_phy_info
	add	x0, x0, :lo12:g_nand_phy_info
	bl	FtlConstantsInit
.LVL1724:
	.loc 3 751 0
	bl	FtlMemInit
.LVL1725:
	.loc 3 752 0
	bl	FtlVariablesInit
.LVL1726:
	.loc 3 753 0
	ldrh	w0, [x19, 16]
	bl	FtlFreeSysBlkQueueInit
.LVL1727:
	.loc 3 756 0
	bl	FtlLoadBbt
.LVL1728:
	cbnz	w0, .L1641
	.loc 3 767 0
	bl	FtlSysBlkInit
.LVL1729:
	cbnz	w0, .L1641
	.loc 3 779 0
	mov	w0, 1
	str	w0, [x20, #:lo12:.LANCHOR2]
	.loc 3 783 0
	ldrh	w0, [x19, 2392]
	cmp	w0, 15
	bhi	.L1641
	mov	w19, 8129
.L1640:
.LBB334:
	.loc 3 786 0 discriminator 3
	mov	w1, 1
	mov	w0, 0
	bl	rk_ftl_garbage_collect
.LVL1730:
	.loc 3 785 0 discriminator 3
	subs	w19, w19, #1
	bne	.L1640
.L1641:
.LBE334:
	.loc 3 794 0
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE150:
	.size	sftl_init, .-sftl_init
	.align	2
	.global	ftl_memcmp
	.type	ftl_memcmp, %function
ftl_memcmp:
.LFB268:
	.loc 1 211 0
	.cfi_startproc
.LVL1731:
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	.loc 1 212 0
	uxtw	x2, w2
.LVL1732:
	.loc 1 211 0
	add	x29, sp, 0
	.cfi_def_cfa_register 29
	.loc 1 212 0
	bl	memcmp
.LVL1733:
	.loc 1 213 0
	ldp	x29, x30, [sp], 16
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa 31, 0
	ret
	.cfi_endproc
.LFE268:
	.size	ftl_memcmp, .-ftl_memcmp
	.global	ftl_temp_buf
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
	.type	__func__.4676, %object
	.size	__func__.4676, 15
__func__.4676:
	.string	"FlashReadPages"
	.zero	1
	.type	__func__.4693, %object
	.size	__func__.4693, 15
__func__.4693:
	.string	"FlashProgPages"
	.zero	1
	.type	__func__.3874, %object
	.size	__func__.3874, 26
__func__.3874:
	.string	"ftl_map_blk_alloc_new_blk"
	.zero	6
	.type	__func__.3388, %object
	.size	__func__.3388, 11
__func__.3388:
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
	.type	__func__.3512, %object
	.size	__func__.3512, 13
__func__.3512:
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
	.type	__func__.3540, %object
	.size	__func__.3540, 9
__func__.3540:
	.string	"FtlWrite"
	.zero	7
	.type	__func__.4107, %object
	.size	__func__.4107, 15
__func__.4107:
	.string	"FtlLoadSysInfo"
	.data
	.align	2
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
	.bss
	.align	3
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR3,. + 4352
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
	.string	"prog read error: = %x\n"
.LC76:
	.string	"prog read s error: = %x %x %x\n"
.LC77:
	.string	"prog read d error: = %x %x %x\n"
.LC78:
	.string	"FtlFreeSysBlkQueueOut free count = %d\n"
.LC79:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
.LC80:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
.LC81:
	.string	"%s error allocating memory. return -1\n"
.LC82:
	.string	"region_id = %x phyAddr = %x\n"
.LC83:
	.string	"spare:"
.LC84:
	.string	"map_ppn:"
.LC85:
	.string	"FtlMapWritePage error = %x \n"
.LC86:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC87:
	.string	"ftl_scan_all_data = %x\n"
.LC88:
	.string	"scan lpa = %x ppa= %x\n"
.LC89:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC90:
	.string	"...%s enter...\n"
.LC91:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC92:
	.string	"free blk vpc error %x = %x  %x\n"
.LC93:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC94:
	.string	"FtlBbmTblFlush error:%x\n"
.LC95:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC96:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC97:
	.string	"decrement_vpc_count %x = %d\n"
.LC98:
	.string	"spuer block %x vpn is 0\n "
.LC99:
	.string	"no ect"
.LC100:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC101:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC102:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC103:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC104:
	.string	"FtlWrite: lpa error:%x %x\n"
	.text
.Letext0:
	.file 6 "include/asm-generic/int-ll64.h"
	.file 7 "include/linux/types.h"
	.file 8 "include/linux/init.h"
	.file 9 "./arch/arm64/include/asm/cachetype.h"
	.file 10 "include/linux/printk.h"
	.file 11 "include/linux/kernel.h"
	.file 12 "drivers/rkflash/rksftl/sftl_inc.h"
	.file 13 "drivers/rkflash/rksftl/flash_com.h"
	.file 14 "drivers/rkflash/rksftl/sftl_struct.h"
	.file 15 "drivers/rkflash/rksftl/sftl_global.h"
	.file 16 "./arch/arm64/include/asm/string.h"
	.file 17 "drivers/rkflash/rksftl/typedef.h"
	.file 18 "<built-in>"
	.section	.debug_info,"",@progbits
.Ldebug_info0:
	.4byte	0x75e0
	.2byte	0x4
	.4byte	.Ldebug_abbrev0
	.byte	0x8
	.uleb128 0x1
	.4byte	.LASF659
	.byte	0x1
	.4byte	.LASF660
	.4byte	.LASF661
	.8byte	.Ltext0
	.8byte	.Letext0-.Ltext0
	.4byte	.Ldebug_line0
	.uleb128 0x2
	.byte	0x8
	.uleb128 0x3
	.byte	0x4
	.byte	0x5
	.string	"int"
	.uleb128 0x4
	.byte	0x8
	.byte	0x7
	.4byte	.LASF0
	.uleb128 0x4
	.byte	0x8
	.byte	0x7
	.4byte	.LASF1
	.uleb128 0x4
	.byte	0x1
	.byte	0x8
	.4byte	.LASF2
	.uleb128 0x5
	.4byte	0x44
	.uleb128 0x4
	.byte	0x4
	.byte	0x7
	.4byte	.LASF3
	.uleb128 0x4
	.byte	0x1
	.byte	0x6
	.4byte	.LASF4
	.uleb128 0x4
	.byte	0x1
	.byte	0x8
	.4byte	.LASF5
	.uleb128 0x4
	.byte	0x2
	.byte	0x5
	.4byte	.LASF6
	.uleb128 0x4
	.byte	0x2
	.byte	0x7
	.4byte	.LASF7
	.uleb128 0x4
	.byte	0x8
	.byte	0x5
	.4byte	.LASF8
	.uleb128 0x4
	.byte	0x8
	.byte	0x7
	.4byte	.LASF9
	.uleb128 0x6
	.string	"u8"
	.byte	0x6
	.byte	0x10
	.4byte	0x5e
	.uleb128 0x6
	.string	"u16"
	.byte	0x6
	.byte	0x13
	.4byte	0x6c
	.uleb128 0x6
	.string	"s32"
	.byte	0x6
	.byte	0x15
	.4byte	0x2f
	.uleb128 0x6
	.string	"u32"
	.byte	0x6
	.byte	0x16
	.4byte	0x50
	.uleb128 0x4
	.byte	0x8
	.byte	0x5
	.4byte	.LASF10
	.uleb128 0x7
	.byte	0x8
	.4byte	0x44
	.uleb128 0x8
	.4byte	.LASF11
	.byte	0x7
	.byte	0x1d
	.4byte	0xc4
	.uleb128 0x4
	.byte	0x1
	.byte	0x2
	.4byte	.LASF12
	.uleb128 0x8
	.4byte	.LASF13
	.byte	0x8
	.byte	0x83
	.4byte	0xd6
	.uleb128 0x7
	.byte	0x8
	.4byte	0xdc
	.uleb128 0x9
	.4byte	0x2f
	.uleb128 0x7
	.byte	0x8
	.4byte	0xe7
	.uleb128 0xa
	.uleb128 0xb
	.4byte	0xcb
	.4byte	0xf3
	.uleb128 0xc
	.byte	0
	.uleb128 0xd
	.4byte	.LASF14
	.byte	0x8
	.byte	0x86
	.4byte	0xe8
	.uleb128 0xd
	.4byte	.LASF15
	.byte	0x8
	.byte	0x86
	.4byte	0xe8
	.uleb128 0xd
	.4byte	.LASF16
	.byte	0x8
	.byte	0x87
	.4byte	0xe8
	.uleb128 0xd
	.4byte	.LASF17
	.byte	0x8
	.byte	0x87
	.4byte	0xe8
	.uleb128 0xb
	.4byte	0x44
	.4byte	0x12a
	.uleb128 0xc
	.byte	0
	.uleb128 0xd
	.4byte	.LASF18
	.byte	0x8
	.byte	0x8e
	.4byte	0x11f
	.uleb128 0xd
	.4byte	.LASF19
	.byte	0x8
	.byte	0x8f
	.4byte	0xb3
	.uleb128 0xd
	.4byte	.LASF20
	.byte	0x8
	.byte	0x90
	.4byte	0x50
	.uleb128 0xd
	.4byte	.LASF21
	.byte	0x8
	.byte	0x9c
	.4byte	0xe1
	.uleb128 0xd
	.4byte	.LASF22
	.byte	0x8
	.byte	0x9e
	.4byte	0xb9
	.uleb128 0xd
	.4byte	.LASF23
	.byte	0x9
	.byte	0x28
	.4byte	0x36
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x177
	.uleb128 0xc
	.byte	0
	.uleb128 0x5
	.4byte	0x16c
	.uleb128 0xd
	.4byte	.LASF24
	.byte	0xa
	.byte	0xa
	.4byte	0x177
	.uleb128 0xd
	.4byte	.LASF25
	.byte	0xa
	.byte	0xb
	.4byte	0x177
	.uleb128 0xb
	.4byte	0x2f
	.4byte	0x19d
	.uleb128 0xc
	.byte	0
	.uleb128 0xd
	.4byte	.LASF26
	.byte	0xa
	.byte	0x2e
	.4byte	0x192
	.uleb128 0xd
	.4byte	.LASF27
	.byte	0xa
	.byte	0x9f
	.4byte	0x2f
	.uleb128 0xd
	.4byte	.LASF28
	.byte	0xa
	.byte	0xa0
	.4byte	0x2f
	.uleb128 0xd
	.4byte	.LASF29
	.byte	0xa
	.byte	0xa1
	.4byte	0x2f
	.uleb128 0xe
	.4byte	.LASF31
	.uleb128 0x5
	.4byte	0x1c9
	.uleb128 0xf
	.4byte	.LASF30
	.byte	0xa
	.2byte	0x1a4
	.4byte	0x1ce
	.uleb128 0xe
	.4byte	.LASF32
	.uleb128 0xf
	.4byte	.LASF33
	.byte	0xb
	.2byte	0x104
	.4byte	0x1df
	.uleb128 0x10
	.4byte	0xac
	.4byte	0x1ff
	.uleb128 0x11
	.4byte	0x2f
	.byte	0
	.uleb128 0xf
	.4byte	.LASF34
	.byte	0xb
	.2byte	0x105
	.4byte	0x20b
	.uleb128 0x7
	.byte	0x8
	.4byte	0x1f0
	.uleb128 0xf
	.4byte	.LASF35
	.byte	0xb
	.2byte	0x1bf
	.4byte	0x2f
	.uleb128 0xf
	.4byte	.LASF36
	.byte	0xb
	.2byte	0x1c0
	.4byte	0x2f
	.uleb128 0xf
	.4byte	.LASF37
	.byte	0xb
	.2byte	0x1c1
	.4byte	0x2f
	.uleb128 0xf
	.4byte	.LASF38
	.byte	0xb
	.2byte	0x1c2
	.4byte	0x2f
	.uleb128 0xf
	.4byte	.LASF39
	.byte	0xb
	.2byte	0x1c3
	.4byte	0x2f
	.uleb128 0xf
	.4byte	.LASF40
	.byte	0xb
	.2byte	0x1c4
	.4byte	0x2f
	.uleb128 0xf
	.4byte	.LASF41
	.byte	0xb
	.2byte	0x1c5
	.4byte	0x2f
	.uleb128 0xf
	.4byte	.LASF42
	.byte	0xb
	.2byte	0x1c7
	.4byte	0xb9
	.uleb128 0xf
	.4byte	.LASF43
	.byte	0xb
	.2byte	0x1da
	.4byte	0x2f
	.uleb128 0xf
	.4byte	.LASF44
	.byte	0xb
	.2byte	0x1dc
	.4byte	0xb9
	.uleb128 0x12
	.4byte	.LASF662
	.byte	0x4
	.4byte	0x50
	.byte	0xb
	.2byte	0x1df
	.4byte	0x2b9
	.uleb128 0x13
	.4byte	.LASF45
	.byte	0
	.uleb128 0x13
	.4byte	.LASF46
	.byte	0x1
	.uleb128 0x13
	.4byte	.LASF47
	.byte	0x2
	.uleb128 0x13
	.4byte	.LASF48
	.byte	0x3
	.uleb128 0x13
	.4byte	.LASF49
	.byte	0x4
	.byte	0
	.uleb128 0xf
	.4byte	.LASF50
	.byte	0xb
	.2byte	0x1e5
	.4byte	0x289
	.uleb128 0xf
	.4byte	.LASF51
	.byte	0xb
	.2byte	0x1f8
	.4byte	0x177
	.uleb128 0xf
	.4byte	.LASF52
	.byte	0xb
	.2byte	0x203
	.4byte	0x177
	.uleb128 0x8
	.4byte	.LASF53
	.byte	0xc
	.byte	0x9
	.4byte	0x5e
	.uleb128 0x8
	.4byte	.LASF54
	.byte	0xc
	.byte	0xa
	.4byte	0x6c
	.uleb128 0x8
	.4byte	.LASF55
	.byte	0xc
	.byte	0xb
	.4byte	0x50
	.uleb128 0x8
	.4byte	.LASF56
	.byte	0xc
	.byte	0xc
	.4byte	0x2f
	.uleb128 0x8
	.4byte	.LASF57
	.byte	0xc
	.byte	0xd
	.4byte	0x65
	.uleb128 0x14
	.4byte	.LASF62
	.byte	0x20
	.byte	0xd
	.byte	0x22
	.4byte	0x35d
	.uleb128 0x15
	.4byte	.LASF58
	.byte	0xd
	.byte	0x23
	.4byte	0xa1
	.byte	0
	.uleb128 0x15
	.4byte	.LASF59
	.byte	0xd
	.byte	0x24
	.4byte	0xa1
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF60
	.byte	0xd
	.byte	0x25
	.4byte	0x35d
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF61
	.byte	0xd
	.byte	0x26
	.4byte	0x35d
	.byte	0x10
	.uleb128 0x16
	.string	"lpa"
	.byte	0xd
	.byte	0x27
	.4byte	0xa1
	.byte	0x18
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0xa1
	.uleb128 0x14
	.4byte	.LASF63
	.byte	0x18
	.byte	0xd
	.byte	0x2a
	.4byte	0x400
	.uleb128 0x15
	.4byte	.LASF64
	.byte	0xd
	.byte	0x2b
	.4byte	0x8b
	.byte	0
	.uleb128 0x15
	.4byte	.LASF65
	.byte	0xd
	.byte	0x2c
	.4byte	0x8b
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF66
	.byte	0xd
	.byte	0x2d
	.4byte	0x8b
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF67
	.byte	0xd
	.byte	0x2e
	.4byte	0x8b
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF68
	.byte	0xd
	.byte	0x2f
	.4byte	0x8b
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF69
	.byte	0xd
	.byte	0x30
	.4byte	0x8b
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF70
	.byte	0xd
	.byte	0x31
	.4byte	0x8b
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF71
	.byte	0xd
	.byte	0x32
	.4byte	0x8b
	.byte	0xe
	.uleb128 0x15
	.4byte	.LASF72
	.byte	0xd
	.byte	0x33
	.4byte	0x8b
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF73
	.byte	0xd
	.byte	0x34
	.4byte	0x8b
	.byte	0x12
	.uleb128 0x15
	.4byte	.LASF74
	.byte	0xd
	.byte	0x35
	.4byte	0x8b
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF75
	.byte	0xd
	.byte	0x36
	.4byte	0x81
	.byte	0x16
	.byte	0
	.uleb128 0x5
	.4byte	0x363
	.uleb128 0x14
	.4byte	.LASF76
	.byte	0x20
	.byte	0xd
	.byte	0x39
	.4byte	0x442
	.uleb128 0x15
	.4byte	.LASF77
	.byte	0xd
	.byte	0x3a
	.4byte	0x45c
	.byte	0
	.uleb128 0x15
	.4byte	.LASF78
	.byte	0xd
	.byte	0x3b
	.4byte	0x476
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF79
	.byte	0xd
	.byte	0x3c
	.4byte	0x49a
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF80
	.byte	0xd
	.byte	0x3d
	.4byte	0x49a
	.byte	0x18
	.byte	0
	.uleb128 0x10
	.4byte	0x96
	.4byte	0x456
	.uleb128 0x11
	.4byte	0x456
	.uleb128 0x11
	.4byte	0xa1
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x8b
	.uleb128 0x7
	.byte	0x8
	.4byte	0x442
	.uleb128 0x10
	.4byte	0xa1
	.4byte	0x476
	.uleb128 0x11
	.4byte	0x81
	.uleb128 0x11
	.4byte	0xa1
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x462
	.uleb128 0x10
	.4byte	0xa1
	.4byte	0x49a
	.uleb128 0x11
	.4byte	0x81
	.uleb128 0x11
	.4byte	0xa1
	.uleb128 0x11
	.4byte	0x35d
	.uleb128 0x11
	.4byte	0x35d
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x47c
	.uleb128 0x14
	.4byte	.LASF81
	.byte	0x60
	.byte	0xe
	.byte	0x2b
	.4byte	0x500
	.uleb128 0x16
	.string	"id"
	.byte	0xe
	.byte	0x2d
	.4byte	0x2e8
	.byte	0
	.uleb128 0x15
	.4byte	.LASF82
	.byte	0xe
	.byte	0x2e
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF83
	.byte	0xe
	.byte	0x2f
	.4byte	0x2e8
	.byte	0x4
	.uleb128 0x16
	.string	"cnt"
	.byte	0xe
	.byte	0x30
	.4byte	0x2e8
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF84
	.byte	0xe
	.byte	0x31
	.4byte	0x2f3
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF85
	.byte	0xe
	.byte	0x32
	.4byte	0x500
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF86
	.byte	0xe
	.byte	0x33
	.4byte	0x510
	.byte	0x20
	.byte	0
	.uleb128 0xb
	.4byte	0x2e8
	.4byte	0x510
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x7
	.byte	0
	.uleb128 0xb
	.4byte	0x520
	.4byte	0x520
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x7
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x2f3
	.uleb128 0x14
	.4byte	.LASF87
	.byte	0xc
	.byte	0xe
	.byte	0x3f
	.4byte	0x56e
	.uleb128 0x16
	.string	"id"
	.byte	0xe
	.byte	0x41
	.4byte	0x2e8
	.byte	0
	.uleb128 0x15
	.4byte	.LASF82
	.byte	0xe
	.byte	0x42
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF83
	.byte	0xe
	.byte	0x43
	.4byte	0x2e8
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0xe
	.byte	0x44
	.4byte	0x2e8
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF84
	.byte	0xe
	.byte	0x45
	.4byte	0x2f3
	.byte	0x8
	.byte	0
	.uleb128 0x14
	.4byte	.LASF89
	.byte	0x40
	.byte	0xe
	.byte	0x48
	.4byte	0x617
	.uleb128 0x15
	.4byte	.LASF90
	.byte	0xe
	.byte	0x4a
	.4byte	0x2e8
	.byte	0
	.uleb128 0x15
	.4byte	.LASF82
	.byte	0xe
	.byte	0x4b
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF91
	.byte	0xe
	.byte	0x4d
	.4byte	0x2e8
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF92
	.byte	0xe
	.byte	0x4e
	.4byte	0x2e8
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF93
	.byte	0xe
	.byte	0x50
	.4byte	0x2e8
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF94
	.byte	0xe
	.byte	0x51
	.4byte	0x2e8
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF95
	.byte	0xe
	.byte	0x53
	.4byte	0x617
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF96
	.byte	0xe
	.byte	0x54
	.4byte	0x520
	.byte	0x18
	.uleb128 0x15
	.4byte	.LASF97
	.byte	0xe
	.byte	0x55
	.4byte	0x617
	.byte	0x20
	.uleb128 0x15
	.4byte	.LASF98
	.byte	0xe
	.byte	0x56
	.4byte	0x520
	.byte	0x28
	.uleb128 0x15
	.4byte	.LASF84
	.byte	0xe
	.byte	0x58
	.4byte	0x2f3
	.byte	0x30
	.uleb128 0x15
	.4byte	.LASF99
	.byte	0xe
	.byte	0x59
	.4byte	0x2f3
	.byte	0x34
	.uleb128 0x15
	.4byte	.LASF100
	.byte	0xe
	.byte	0x5a
	.4byte	0x2f3
	.byte	0x38
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x2e8
	.uleb128 0x14
	.4byte	.LASF101
	.byte	0x10
	.byte	0xe
	.byte	0x5d
	.4byte	0x65a
	.uleb128 0x15
	.4byte	.LASF102
	.byte	0xe
	.byte	0x5f
	.4byte	0x2e8
	.byte	0
	.uleb128 0x15
	.4byte	.LASF103
	.byte	0xe
	.byte	0x60
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x16
	.string	"hit"
	.byte	0xe
	.byte	0x61
	.4byte	0x2f3
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF104
	.byte	0xe
	.byte	0x62
	.4byte	0x520
	.byte	0x8
	.byte	0
	.uleb128 0x14
	.4byte	.LASF105
	.byte	0x24
	.byte	0xe
	.byte	0x65
	.4byte	0x6c7
	.uleb128 0x15
	.4byte	.LASF91
	.byte	0xe
	.byte	0x67
	.4byte	0x2f3
	.byte	0
	.uleb128 0x15
	.4byte	.LASF106
	.byte	0xe
	.byte	0x68
	.4byte	0x2f3
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF84
	.byte	0xe
	.byte	0x69
	.4byte	0x2f3
	.byte	0x8
	.uleb128 0x16
	.string	"len"
	.byte	0xe
	.byte	0x6a
	.4byte	0x2f3
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF107
	.byte	0xe
	.byte	0x6b
	.4byte	0x2f3
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF108
	.byte	0xe
	.byte	0x6c
	.4byte	0x2f3
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF109
	.byte	0xe
	.byte	0x6d
	.4byte	0x6c7
	.byte	0x18
	.uleb128 0x16
	.string	"tbl"
	.byte	0xe
	.byte	0x6e
	.4byte	0x6d7
	.byte	0x20
	.byte	0
	.uleb128 0xb
	.4byte	0x2f3
	.4byte	0x6d7
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x1
	.byte	0
	.uleb128 0xb
	.4byte	0x2e8
	.4byte	0x6e7
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x1
	.byte	0
	.uleb128 0x14
	.4byte	.LASF110
	.byte	0x10
	.byte	0xe
	.byte	0x72
	.4byte	0x747
	.uleb128 0x15
	.4byte	.LASF91
	.byte	0xe
	.byte	0x74
	.4byte	0x2e8
	.byte	0
	.uleb128 0x16
	.string	"id"
	.byte	0xe
	.byte	0x75
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF84
	.byte	0xe
	.byte	0x76
	.4byte	0x2f3
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF83
	.byte	0xe
	.byte	0x77
	.4byte	0x2e8
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF111
	.byte	0xe
	.byte	0x78
	.4byte	0x2e8
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF112
	.byte	0xe
	.byte	0x79
	.4byte	0x2e8
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF109
	.byte	0xe
	.byte	0x7a
	.4byte	0x747
	.byte	0xe
	.byte	0
	.uleb128 0xb
	.4byte	0x2dd
	.4byte	0x757
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x1
	.byte	0
	.uleb128 0x14
	.4byte	.LASF113
	.byte	0x10
	.byte	0xe
	.byte	0x7d
	.4byte	0x79f
	.uleb128 0x15
	.4byte	.LASF91
	.byte	0xe
	.byte	0x7f
	.4byte	0x2e8
	.byte	0
	.uleb128 0x16
	.string	"id"
	.byte	0xe
	.byte	0x80
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF84
	.byte	0xe
	.byte	0x81
	.4byte	0x2f3
	.byte	0x4
	.uleb128 0x16
	.string	"lpa"
	.byte	0xe
	.byte	0x82
	.4byte	0x2f3
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF114
	.byte	0xe
	.byte	0x83
	.4byte	0x2f3
	.byte	0xc
	.byte	0
	.uleb128 0x14
	.4byte	.LASF115
	.byte	0x10
	.byte	0xe
	.byte	0x8f
	.4byte	0x7e7
	.uleb128 0x15
	.4byte	.LASF91
	.byte	0xe
	.byte	0x91
	.4byte	0x2e8
	.byte	0
	.uleb128 0x16
	.string	"id"
	.byte	0xe
	.byte	0x92
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF84
	.byte	0xe
	.byte	0x93
	.4byte	0x2f3
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF116
	.byte	0xe
	.byte	0x94
	.4byte	0x2e8
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF109
	.byte	0xe
	.byte	0x95
	.4byte	0x7e7
	.byte	0xa
	.byte	0
	.uleb128 0xb
	.4byte	0x2dd
	.4byte	0x7f7
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x5
	.byte	0
	.uleb128 0x14
	.4byte	.LASF117
	.byte	0x10
	.byte	0xe
	.byte	0x98
	.4byte	0x83f
	.uleb128 0x15
	.4byte	.LASF91
	.byte	0xe
	.byte	0x9a
	.4byte	0x2e8
	.byte	0
	.uleb128 0x16
	.string	"id"
	.byte	0xe
	.byte	0x9b
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF84
	.byte	0xe
	.byte	0x9c
	.4byte	0x2f3
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF118
	.byte	0xe
	.byte	0x9d
	.4byte	0x2f3
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF119
	.byte	0xe
	.byte	0x9e
	.4byte	0x2f3
	.byte	0xc
	.byte	0
	.uleb128 0x14
	.4byte	.LASF120
	.byte	0x30
	.byte	0xe
	.byte	0xa1
	.4byte	0x93c
	.uleb128 0x16
	.string	"tag"
	.byte	0xe
	.byte	0xa3
	.4byte	0x2f3
	.byte	0
	.uleb128 0x16
	.string	"ver"
	.byte	0xe
	.byte	0xa4
	.4byte	0x2f3
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0xe
	.byte	0xa6
	.4byte	0x2e8
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF121
	.byte	0xe
	.byte	0xa7
	.4byte	0x2dd
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF122
	.byte	0xe
	.byte	0xa8
	.4byte	0x2dd
	.byte	0xb
	.uleb128 0x15
	.4byte	.LASF123
	.byte	0xe
	.byte	0xa9
	.4byte	0x2dd
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF124
	.byte	0xe
	.byte	0xaa
	.4byte	0x2dd
	.byte	0xd
	.uleb128 0x15
	.4byte	.LASF125
	.byte	0xe
	.byte	0xab
	.4byte	0x2e8
	.byte	0xe
	.uleb128 0x15
	.4byte	.LASF126
	.byte	0xe
	.byte	0xad
	.4byte	0x2e8
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF127
	.byte	0xe
	.byte	0xae
	.4byte	0x2e8
	.byte	0x12
	.uleb128 0x15
	.4byte	.LASF128
	.byte	0xe
	.byte	0xaf
	.4byte	0x2e8
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF129
	.byte	0xe
	.byte	0xb0
	.4byte	0x2e8
	.byte	0x16
	.uleb128 0x15
	.4byte	.LASF130
	.byte	0xe
	.byte	0xb2
	.4byte	0x2e8
	.byte	0x18
	.uleb128 0x15
	.4byte	.LASF131
	.byte	0xe
	.byte	0xb3
	.4byte	0x2e8
	.byte	0x1a
	.uleb128 0x15
	.4byte	.LASF132
	.byte	0xe
	.byte	0xb4
	.4byte	0x2e8
	.byte	0x1c
	.uleb128 0x15
	.4byte	.LASF133
	.byte	0xe
	.byte	0xb5
	.4byte	0x2e8
	.byte	0x1e
	.uleb128 0x15
	.4byte	.LASF134
	.byte	0xe
	.byte	0xb7
	.4byte	0x2f3
	.byte	0x20
	.uleb128 0x15
	.4byte	.LASF135
	.byte	0xe
	.byte	0xb8
	.4byte	0x2f3
	.byte	0x24
	.uleb128 0x15
	.4byte	.LASF136
	.byte	0xe
	.byte	0xb9
	.4byte	0x2f3
	.byte	0x28
	.uleb128 0x15
	.4byte	.LASF109
	.byte	0xe
	.byte	0xba
	.4byte	0x6d7
	.byte	0x2c
	.byte	0
	.uleb128 0x18
	.4byte	.LASF137
	.2byte	0x200
	.byte	0xe
	.byte	0xbe
	.4byte	0xa82
	.uleb128 0x16
	.string	"tag"
	.byte	0xe
	.byte	0xc0
	.4byte	0x2f3
	.byte	0
	.uleb128 0x16
	.string	"ver"
	.byte	0xe
	.byte	0xc1
	.4byte	0x2f3
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF138
	.byte	0xe
	.byte	0xc3
	.4byte	0x2f3
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF139
	.byte	0xe
	.byte	0xc4
	.4byte	0x2f3
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF140
	.byte	0xe
	.byte	0xc5
	.4byte	0x2f3
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF141
	.byte	0xe
	.byte	0xc6
	.4byte	0x2f3
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF134
	.byte	0xe
	.byte	0xc8
	.4byte	0x2f3
	.byte	0x18
	.uleb128 0x15
	.4byte	.LASF142
	.byte	0xe
	.byte	0xc9
	.4byte	0x2f3
	.byte	0x1c
	.uleb128 0x15
	.4byte	.LASF143
	.byte	0xe
	.byte	0xca
	.4byte	0x2f3
	.byte	0x20
	.uleb128 0x15
	.4byte	.LASF144
	.byte	0xe
	.byte	0xcb
	.4byte	0x2f3
	.byte	0x24
	.uleb128 0x15
	.4byte	.LASF131
	.byte	0xe
	.byte	0xcd
	.4byte	0x2f3
	.byte	0x28
	.uleb128 0x15
	.4byte	.LASF145
	.byte	0xe
	.byte	0xce
	.4byte	0x2f3
	.byte	0x2c
	.uleb128 0x15
	.4byte	.LASF146
	.byte	0xe
	.byte	0xcf
	.4byte	0x2f3
	.byte	0x30
	.uleb128 0x15
	.4byte	.LASF147
	.byte	0xe
	.byte	0xd0
	.4byte	0x2f3
	.byte	0x34
	.uleb128 0x15
	.4byte	.LASF148
	.byte	0xe
	.byte	0xd2
	.4byte	0x2f3
	.byte	0x38
	.uleb128 0x15
	.4byte	.LASF149
	.byte	0xe
	.byte	0xd3
	.4byte	0x2f3
	.byte	0x3c
	.uleb128 0x15
	.4byte	.LASF150
	.byte	0xe
	.byte	0xd4
	.4byte	0x2f3
	.byte	0x40
	.uleb128 0x15
	.4byte	.LASF151
	.byte	0xe
	.byte	0xd5
	.4byte	0x2f3
	.byte	0x44
	.uleb128 0x15
	.4byte	.LASF152
	.byte	0xe
	.byte	0xd7
	.4byte	0x2f3
	.byte	0x48
	.uleb128 0x15
	.4byte	.LASF153
	.byte	0xe
	.byte	0xd8
	.4byte	0x2f3
	.byte	0x4c
	.uleb128 0x15
	.4byte	.LASF154
	.byte	0xe
	.byte	0xd9
	.4byte	0x2f3
	.byte	0x50
	.uleb128 0x15
	.4byte	.LASF155
	.byte	0xe
	.byte	0xda
	.4byte	0x2f3
	.byte	0x54
	.uleb128 0x15
	.4byte	.LASF156
	.byte	0xe
	.byte	0xdc
	.4byte	0x2f3
	.byte	0x58
	.uleb128 0x15
	.4byte	.LASF157
	.byte	0xe
	.byte	0xdd
	.4byte	0x2f3
	.byte	0x5c
	.uleb128 0x15
	.4byte	.LASF158
	.byte	0xe
	.byte	0xde
	.4byte	0x2f3
	.byte	0x60
	.uleb128 0x15
	.4byte	.LASF109
	.byte	0xe
	.byte	0xe0
	.4byte	0xa82
	.byte	0x64
	.byte	0
	.uleb128 0xb
	.4byte	0x2f3
	.4byte	0xa92
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x66
	.byte	0
	.uleb128 0x14
	.4byte	.LASF159
	.byte	0x6
	.byte	0xe
	.byte	0xe4
	.4byte	0xac3
	.uleb128 0x15
	.4byte	.LASF160
	.byte	0xe
	.byte	0xe6
	.4byte	0x2e8
	.byte	0
	.uleb128 0x15
	.4byte	.LASF161
	.byte	0xe
	.byte	0xe7
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF162
	.byte	0xe
	.byte	0xe8
	.4byte	0x2e8
	.byte	0x4
	.byte	0
	.uleb128 0x18
	.4byte	.LASF163
	.2byte	0x808
	.byte	0xe
	.byte	0xeb
	.4byte	0xb0d
	.uleb128 0x16
	.string	"max"
	.byte	0xe
	.byte	0xed
	.4byte	0x2e8
	.byte	0
	.uleb128 0x15
	.4byte	.LASF164
	.byte	0xe
	.byte	0xee
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF165
	.byte	0xe
	.byte	0xef
	.4byte	0x2e8
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF111
	.byte	0xe
	.byte	0xf0
	.4byte	0x2e8
	.byte	0x6
	.uleb128 0x16
	.string	"arr"
	.byte	0xe
	.byte	0xf1
	.4byte	0xb0d
	.byte	0x8
	.byte	0
	.uleb128 0xb
	.4byte	0x2e8
	.4byte	0xb1e
	.uleb128 0x19
	.4byte	0x3d
	.2byte	0x3ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF166
	.byte	0x30
	.byte	0xe
	.byte	0xf5
	.4byte	0xbb0
	.uleb128 0x16
	.string	"id"
	.byte	0xe
	.byte	0xf7
	.4byte	0x2e8
	.byte	0
	.uleb128 0x15
	.4byte	.LASF167
	.byte	0xe
	.byte	0xf8
	.4byte	0x2e8
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF168
	.byte	0xe
	.byte	0xf9
	.4byte	0x2e8
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF169
	.byte	0xe
	.byte	0xfa
	.4byte	0x2dd
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF170
	.byte	0xe
	.byte	0xfb
	.4byte	0x2dd
	.byte	0x7
	.uleb128 0x15
	.4byte	.LASF171
	.byte	0xe
	.byte	0xfc
	.4byte	0x2dd
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF172
	.byte	0xe
	.byte	0xfd
	.4byte	0x2dd
	.byte	0x9
	.uleb128 0x15
	.4byte	.LASF173
	.byte	0xe
	.byte	0xfe
	.4byte	0x2dd
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF174
	.byte	0xe
	.byte	0xff
	.4byte	0x2dd
	.byte	0xb
	.uleb128 0x1a
	.4byte	.LASF84
	.byte	0xe
	.2byte	0x100
	.4byte	0x2f3
	.byte	0xc
	.uleb128 0x1a
	.4byte	.LASF175
	.byte	0xe
	.2byte	0x101
	.4byte	0xbb0
	.byte	0x10
	.byte	0
	.uleb128 0xb
	.4byte	0x2e8
	.4byte	0xbc0
	.uleb128 0x17
	.4byte	0x3d
	.byte	0xf
	.byte	0
	.uleb128 0x1b
	.4byte	.LASF176
	.byte	0xc
	.byte	0xe
	.2byte	0x105
	.4byte	0xbf5
	.uleb128 0x1a
	.4byte	.LASF177
	.byte	0xe
	.2byte	0x107
	.4byte	0x2f3
	.byte	0
	.uleb128 0x1a
	.4byte	.LASF178
	.byte	0xe
	.2byte	0x108
	.4byte	0x2f3
	.byte	0x4
	.uleb128 0x1c
	.string	"lpa"
	.byte	0xe
	.2byte	0x109
	.4byte	0x2f3
	.byte	0x8
	.byte	0
	.uleb128 0x1b
	.4byte	.LASF179
	.byte	0x18
	.byte	0xe
	.2byte	0x10c
	.4byte	0xc2a
	.uleb128 0x1a
	.4byte	.LASF60
	.byte	0xe
	.2byte	0x10e
	.4byte	0x520
	.byte	0
	.uleb128 0x1a
	.4byte	.LASF61
	.byte	0xe
	.2byte	0x10f
	.4byte	0x520
	.byte	0x8
	.uleb128 0x1a
	.4byte	.LASF180
	.byte	0xe
	.2byte	0x110
	.4byte	0x2f3
	.byte	0x10
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF181
	.byte	0xf
	.byte	0x12
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_type
	.uleb128 0x1d
	.4byte	.LASF182
	.byte	0xf
	.byte	0x13
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_die_num
	.uleb128 0x1d
	.4byte	.LASF183
	.byte	0xf
	.byte	0x14
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_planes_per_die
	.uleb128 0x1d
	.4byte	.LASF184
	.byte	0xf
	.byte	0x15
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_blks_per_die
	.uleb128 0x1d
	.4byte	.LASF185
	.byte	0xf
	.byte	0x16
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_blks_per_die_shift
	.uleb128 0x1d
	.4byte	.LASF186
	.byte	0xf
	.byte	0x17
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_planes_num
	.uleb128 0x1d
	.4byte	.LASF187
	.byte	0xf
	.byte	0x18
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_blk_pre_plane
	.uleb128 0x1d
	.4byte	.LASF188
	.byte	0xf
	.byte	0x19
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_ext_blk_pre_plane
	.uleb128 0x1d
	.4byte	.LASF189
	.byte	0xf
	.byte	0x1a
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_bbm_buf_size
	.uleb128 0x1d
	.4byte	.LASF190
	.byte	0xf
	.byte	0x1c
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_page_pre_blk
	.uleb128 0x1d
	.4byte	.LASF191
	.byte	0xf
	.byte	0x1d
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_page_pre_slc_blk
	.uleb128 0x1d
	.4byte	.LASF192
	.byte	0xf
	.byte	0x1e
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_page_pre_super_blk
	.uleb128 0x1d
	.4byte	.LASF193
	.byte	0xf
	.byte	0x1f
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_sec_pre_page
	.uleb128 0x1d
	.4byte	.LASF194
	.byte	0xf
	.byte	0x20
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_sec_pre_page_shift
	.uleb128 0x1d
	.4byte	.LASF195
	.byte	0xf
	.byte	0x24
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_byte_pre_page
	.uleb128 0x1d
	.4byte	.LASF196
	.byte	0xf
	.byte	0x25
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_byte_pre_oob
	.uleb128 0x1d
	.4byte	.LASF197
	.byte	0xf
	.byte	0x26
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_reserved_blks
	.uleb128 0x1d
	.4byte	.LASF198
	.byte	0xf
	.byte	0x27
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_totle_phy_blks
	.uleb128 0x1d
	.4byte	.LASF199
	.byte	0xf
	.byte	0x29
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_map_region_num
	.uleb128 0x1d
	.4byte	.LASF200
	.byte	0xf
	.byte	0x2a
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_l2pmap_ram_region_num
	.uleb128 0x1d
	.4byte	.LASF201
	.byte	0xf
	.byte	0x2c
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_vendor_region_num
	.uleb128 0x1d
	.4byte	.LASF202
	.byte	0xf
	.byte	0x2e
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_map_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF203
	.byte	0xf
	.byte	0x2f
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_max_map_blks
	.uleb128 0x1d
	.4byte	.LASF204
	.byte	0xf
	.byte	0x30
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_max_vendor_blks
	.uleb128 0x1d
	.4byte	.LASF205
	.byte	0xf
	.byte	0x31
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_vendor_part_size
	.uleb128 0x1d
	.4byte	.LASF206
	.byte	0xf
	.byte	0x32
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_sys_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF207
	.byte	0xf
	.byte	0x33
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_init_sys_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF208
	.byte	0xf
	.byte	0x34
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_max_sys_blks
	.uleb128 0x1d
	.4byte	.LASF209
	.byte	0xf
	.byte	0x35
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_data_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF210
	.byte	0xf
	.byte	0x36
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_data_op_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF211
	.byte	0xf
	.byte	0x37
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	c_ftl_nand_max_data_blks
	.uleb128 0x1d
	.4byte	.LASF212
	.byte	0xf
	.byte	0x38
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	ftl_gc_temp_power_lost_recovery_flag
	.uleb128 0x1d
	.4byte	.LASF213
	.byte	0xf
	.byte	0x3a
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_GlobalSysVersion
	.uleb128 0x1d
	.4byte	.LASF214
	.byte	0xf
	.byte	0x3b
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_GlobalDataVersion
	.uleb128 0x1d
	.4byte	.LASF215
	.byte	0xf
	.byte	0x3c
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_MaxLbaSector
	.uleb128 0x1d
	.4byte	.LASF216
	.byte	0xf
	.byte	0x3d
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_MaxLpn
	.uleb128 0x1d
	.4byte	.LASF217
	.byte	0xf
	.byte	0x3e
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_VaildLpn
	.uleb128 0x1d
	.4byte	.LASF218
	.byte	0xf
	.byte	0x3f
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_MaxLbn
	.uleb128 0x1d
	.4byte	.LASF219
	.byte	0xf
	.byte	0x41
	.4byte	0x4a0
	.uleb128 0x9
	.byte	0x3
	.8byte	gBbtInfo
	.uleb128 0x1d
	.4byte	.LASF220
	.byte	0xf
	.byte	0x42
	.4byte	0x526
	.uleb128 0x9
	.byte	0x3
	.8byte	gSysInfo
	.uleb128 0x1d
	.4byte	.LASF221
	.byte	0xf
	.byte	0x43
	.4byte	0xac3
	.uleb128 0x9
	.byte	0x3
	.8byte	gSysFreeQueue
	.uleb128 0x1d
	.4byte	.LASF222
	.byte	0xf
	.byte	0x44
	.4byte	0x56e
	.uleb128 0x9
	.byte	0x3
	.8byte	gL2pMapInfo
	.uleb128 0x1d
	.4byte	.LASF223
	.byte	0xf
	.byte	0x45
	.4byte	0x56e
	.uleb128 0x9
	.byte	0x3
	.8byte	gVendorBlkInfo
	.uleb128 0x1d
	.4byte	.LASF224
	.byte	0xf
	.byte	0x46
	.4byte	0x314
	.uleb128 0x9
	.byte	0x3
	.8byte	req_sys
	.uleb128 0x1d
	.4byte	.LASF225
	.byte	0xf
	.byte	0x47
	.4byte	0xfdb
	.uleb128 0x9
	.byte	0x3
	.8byte	req_read
	.uleb128 0x7
	.byte	0x8
	.4byte	0x314
	.uleb128 0x1d
	.4byte	.LASF226
	.byte	0xf
	.byte	0x48
	.4byte	0xfdb
	.uleb128 0x9
	.byte	0x3
	.8byte	req_prgm
	.uleb128 0x1d
	.4byte	.LASF227
	.byte	0xf
	.byte	0x49
	.4byte	0xfdb
	.uleb128 0x9
	.byte	0x3
	.8byte	req_erase
	.uleb128 0x1d
	.4byte	.LASF228
	.byte	0xf
	.byte	0x4a
	.4byte	0xfdb
	.uleb128 0x9
	.byte	0x3
	.8byte	req_gc
	.uleb128 0x1d
	.4byte	.LASF229
	.byte	0xf
	.byte	0x4b
	.4byte	0xfdb
	.uleb128 0x9
	.byte	0x3
	.8byte	req_gc_dst
	.uleb128 0x1d
	.4byte	.LASF230
	.byte	0xf
	.byte	0x4d
	.4byte	0xfdb
	.uleb128 0x9
	.byte	0x3
	.8byte	g_req_cache
	.uleb128 0xb
	.4byte	0x2dd
	.4byte	0x105a
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x1f
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF231
	.byte	0xf
	.byte	0x4f
	.4byte	0x104a
	.uleb128 0x9
	.byte	0x3
	.8byte	p_plane_order_table
	.uleb128 0x1d
	.4byte	.LASF232
	.byte	0xf
	.byte	0x51
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_sys_data_buf
	.uleb128 0x1d
	.4byte	.LASF233
	.byte	0xf
	.byte	0x52
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_sys_data_buf_1
	.uleb128 0x1d
	.4byte	.LASF234
	.byte	0xf
	.byte	0x53
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_vendor_data_buf
	.uleb128 0x1d
	.4byte	.LASF235
	.byte	0xf
	.byte	0x54
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_sys_spare_buf
	.uleb128 0x1d
	.4byte	.LASF236
	.byte	0xf
	.byte	0x55
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_io_data_buf_0
	.uleb128 0x1d
	.4byte	.LASF237
	.byte	0xf
	.byte	0x56
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_io_data_buf_1
	.uleb128 0x1d
	.4byte	.LASF238
	.byte	0xf
	.byte	0x57
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_io_spare_buf
	.uleb128 0x1d
	.4byte	.LASF239
	.byte	0xf
	.byte	0x58
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_gc_spare_buf
	.uleb128 0x1d
	.4byte	.LASF240
	.byte	0xf
	.byte	0x59
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_gc_data_buf
	.uleb128 0x1d
	.4byte	.LASF241
	.byte	0xf
	.byte	0x5a
	.4byte	0x1141
	.uleb128 0x9
	.byte	0x3
	.8byte	gp_gc_page_buf_info
	.uleb128 0x7
	.byte	0x8
	.4byte	0xbf5
	.uleb128 0x1d
	.4byte	.LASF242
	.byte	0xf
	.byte	0x5b
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	c_gc_page_buf_num
	.uleb128 0x1d
	.4byte	.LASF243
	.byte	0xf
	.byte	0x5c
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_num_req
	.uleb128 0x1d
	.4byte	.LASF244
	.byte	0xf
	.byte	0x5d
	.4byte	0x1186
	.uleb128 0x9
	.byte	0x3
	.8byte	gp_ect_tbl_info
	.uleb128 0x7
	.byte	0x8
	.4byte	0x65a
	.uleb128 0x1d
	.4byte	.LASF245
	.byte	0xf
	.byte	0x5e
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_ect_tbl_info_size
	.uleb128 0x1d
	.4byte	.LASF246
	.byte	0xf
	.byte	0x5f
	.4byte	0x617
	.uleb128 0x9
	.byte	0x3
	.8byte	p_erase_count_table
	.uleb128 0x1d
	.4byte	.LASF247
	.byte	0xf
	.byte	0x61
	.4byte	0x617
	.uleb128 0x9
	.byte	0x3
	.8byte	p_swl_mul_table
	.uleb128 0x1d
	.4byte	.LASF248
	.byte	0xf
	.byte	0x62
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_swl_count
	.uleb128 0x1d
	.4byte	.LASF249
	.byte	0xf
	.byte	0x63
	.4byte	0x617
	.uleb128 0x9
	.byte	0x3
	.8byte	p_valid_page_count_table
	.uleb128 0x1d
	.4byte	.LASF250
	.byte	0xf
	.byte	0x64
	.4byte	0x617
	.uleb128 0x9
	.byte	0x3
	.8byte	p_valid_page_count_check_table
	.uleb128 0x1d
	.4byte	.LASF251
	.byte	0xf
	.byte	0x65
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_blk_mode_table
	.uleb128 0x1d
	.4byte	.LASF252
	.byte	0xf
	.byte	0x67
	.4byte	0x617
	.uleb128 0x9
	.byte	0x3
	.8byte	p_map_block_table
	.uleb128 0x1d
	.4byte	.LASF253
	.byte	0xf
	.byte	0x68
	.4byte	0x617
	.uleb128 0x9
	.byte	0x3
	.8byte	p_map_block_valid_page_count
	.uleb128 0x1d
	.4byte	.LASF254
	.byte	0xf
	.byte	0x69
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_map_block_ver_table
	.uleb128 0x1d
	.4byte	.LASF255
	.byte	0xf
	.byte	0x6a
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_map_region_ppn_table
	.uleb128 0x1d
	.4byte	.LASF256
	.byte	0xf
	.byte	0x6b
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_map_block
	.uleb128 0x1d
	.4byte	.LASF257
	.byte	0xf
	.byte	0x6d
	.4byte	0x617
	.uleb128 0x9
	.byte	0x3
	.8byte	p_vendor_block_table
	.uleb128 0x1d
	.4byte	.LASF258
	.byte	0xf
	.byte	0x6e
	.4byte	0x617
	.uleb128 0x9
	.byte	0x3
	.8byte	p_vendor_block_valid_page_count
	.uleb128 0x1d
	.4byte	.LASF259
	.byte	0xf
	.byte	0x6f
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_vendor_block_ver_table
	.uleb128 0x1d
	.4byte	.LASF260
	.byte	0xf
	.byte	0x70
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_vendor_region_ppn_table
	.uleb128 0x1d
	.4byte	.LASF261
	.byte	0xf
	.byte	0x71
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_vendor_block
	.uleb128 0x1d
	.4byte	.LASF262
	.byte	0xf
	.byte	0x73
	.4byte	0x1306
	.uleb128 0x9
	.byte	0x3
	.8byte	p_l2p_ram_map
	.uleb128 0x7
	.byte	0x8
	.4byte	0x61d
	.uleb128 0x1d
	.4byte	.LASF263
	.byte	0xf
	.byte	0x74
	.4byte	0x520
	.uleb128 0x9
	.byte	0x3
	.8byte	p_l2p_map_buf
	.uleb128 0x1d
	.4byte	.LASF264
	.byte	0xf
	.byte	0x75
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_l2p_last_update_region_id
	.uleb128 0x1d
	.4byte	.LASF265
	.byte	0xf
	.byte	0x7c
	.4byte	0x134b
	.uleb128 0x9
	.byte	0x3
	.8byte	p_data_block_list_table
	.uleb128 0x7
	.byte	0x8
	.4byte	0xa92
	.uleb128 0x1d
	.4byte	.LASF266
	.byte	0xf
	.byte	0x7d
	.4byte	0x134b
	.uleb128 0x9
	.byte	0x3
	.8byte	p_free_data_block_list_head
	.uleb128 0x1d
	.4byte	.LASF267
	.byte	0xf
	.byte	0x7f
	.4byte	0x134b
	.uleb128 0x9
	.byte	0x3
	.8byte	p_data_block_list_head
	.uleb128 0x1d
	.4byte	.LASF268
	.byte	0xf
	.byte	0x80
	.4byte	0x134b
	.uleb128 0x9
	.byte	0x3
	.8byte	p_data_block_list_tail
	.uleb128 0x1d
	.4byte	.LASF269
	.byte	0xf
	.byte	0x81
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_num_free_superblocks
	.uleb128 0x1d
	.4byte	.LASF270
	.byte	0xf
	.byte	0x82
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_num_data_superblocks
	.uleb128 0x1d
	.4byte	.LASF271
	.byte	0xf
	.byte	0x83
	.4byte	0xb1e
	.uleb128 0x9
	.byte	0x3
	.8byte	g_active_superblock
	.uleb128 0x1d
	.4byte	.LASF272
	.byte	0xf
	.byte	0x84
	.4byte	0xb1e
	.uleb128 0x9
	.byte	0x3
	.8byte	g_buffer_superblock
	.uleb128 0x1d
	.4byte	.LASF273
	.byte	0xf
	.byte	0x85
	.4byte	0xb1e
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_temp_superblock
	.uleb128 0x1d
	.4byte	.LASF274
	.byte	0xf
	.byte	0x86
	.4byte	0xb1e
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_superblock
	.uleb128 0x1d
	.4byte	.LASF275
	.byte	0xf
	.byte	0x87
	.4byte	0x1423
	.uleb128 0x9
	.byte	0x3
	.8byte	gp_last_act_superblock
	.uleb128 0x7
	.byte	0x8
	.4byte	0xb1e
	.uleb128 0x1d
	.4byte	.LASF276
	.byte	0xf
	.byte	0x88
	.4byte	0x83f
	.uleb128 0x9
	.byte	0x3
	.8byte	g_sys_save_data
	.uleb128 0x1d
	.4byte	.LASF277
	.byte	0xf
	.byte	0x89
	.4byte	0x93c
	.uleb128 0x9
	.byte	0x3
	.8byte	g_sys_ext_data
	.uleb128 0x1d
	.4byte	.LASF278
	.byte	0xf
	.byte	0x8b
	.4byte	0x1468
	.uleb128 0x9
	.byte	0x3
	.8byte	p_gc_page_info
	.uleb128 0x7
	.byte	0x8
	.4byte	0xbc0
	.uleb128 0x1d
	.4byte	.LASF279
	.byte	0xf
	.byte	0x8c
	.4byte	0x617
	.uleb128 0x9
	.byte	0x3
	.8byte	p_gc_blk_tbl
	.uleb128 0x1d
	.4byte	.LASF280
	.byte	0xf
	.byte	0x8d
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_blk_num
	.uleb128 0x1d
	.4byte	.LASF281
	.byte	0xf
	.byte	0x8e
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_page_offset
	.uleb128 0x1d
	.4byte	.LASF282
	.byte	0xf
	.byte	0x8f
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_cur_blk_valid_pages
	.uleb128 0x1d
	.4byte	.LASF283
	.byte	0xf
	.byte	0x90
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_cur_blk_max_valid_pages
	.uleb128 0x1d
	.4byte	.LASF284
	.byte	0xf
	.byte	0x91
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_next_blk
	.uleb128 0x1d
	.4byte	.LASF285
	.byte	0xf
	.byte	0x92
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_next_blk_1
	.uleb128 0x1d
	.4byte	.LASF286
	.byte	0xf
	.byte	0x93
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_bad_block_temp_num
	.uleb128 0x1d
	.4byte	.LASF287
	.byte	0xf
	.byte	0x94
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_bad_block_gc_index
	.uleb128 0xb
	.4byte	0x2e8
	.4byte	0x153b
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x10
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF288
	.byte	0xf
	.byte	0x95
	.4byte	0x152b
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_bad_block_temp_tbl
	.uleb128 0x1d
	.4byte	.LASF289
	.byte	0xf
	.byte	0x97
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_free_blk_threshold
	.uleb128 0x1d
	.4byte	.LASF290
	.byte	0xf
	.byte	0x98
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_merge_free_blk_threshold
	.uleb128 0x1d
	.4byte	.LASF291
	.byte	0xf
	.byte	0x99
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_blk_index
	.uleb128 0x1d
	.4byte	.LASF292
	.byte	0xf
	.byte	0x9b
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_gc_page_count
	.uleb128 0x1d
	.4byte	.LASF293
	.byte	0xf
	.byte	0x9c
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_write_page_count
	.uleb128 0x1d
	.4byte	.LASF294
	.byte	0xf
	.byte	0x9d
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_write_sector
	.uleb128 0x1d
	.4byte	.LASF295
	.byte	0xf
	.byte	0x9e
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_read_sector
	.uleb128 0x1d
	.4byte	.LASF296
	.byte	0xf
	.byte	0xa0
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_discard_page_count
	.uleb128 0x1d
	.4byte	.LASF297
	.byte	0xf
	.byte	0xa1
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_read_page_count
	.uleb128 0x1d
	.4byte	.LASF298
	.byte	0xf
	.byte	0xa2
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_tmp_data_superblock_id
	.uleb128 0x1d
	.4byte	.LASF299
	.byte	0xf
	.byte	0xa3
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_cache_write_count
	.uleb128 0x1d
	.4byte	.LASF300
	.byte	0xf
	.byte	0xa4
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_l2p_write_count
	.uleb128 0x1d
	.4byte	.LASF301
	.byte	0xf
	.byte	0xa6
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_mlc_erase_count
	.uleb128 0x1d
	.4byte	.LASF302
	.byte	0xf
	.byte	0xa7
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_avg_erase_count
	.uleb128 0x1d
	.4byte	.LASF303
	.byte	0xf
	.byte	0xa8
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_min_erase_count
	.uleb128 0x1d
	.4byte	.LASF304
	.byte	0xf
	.byte	0xa9
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_slc_erase_count
	.uleb128 0x1d
	.4byte	.LASF305
	.byte	0xf
	.byte	0xaa
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_totle_sys_slc_erase_count
	.uleb128 0x1d
	.4byte	.LASF306
	.byte	0xf
	.byte	0xab
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_max_erase_count
	.uleb128 0x1d
	.4byte	.LASF307
	.byte	0xf
	.byte	0xac
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_in_gc_progress
	.uleb128 0x1d
	.4byte	.LASF308
	.byte	0xf
	.byte	0xad
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_in_swl_replace
	.uleb128 0x1d
	.4byte	.LASF309
	.byte	0xf
	.byte	0xae
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_ftl_nand_free_count
	.uleb128 0x1d
	.4byte	.LASF310
	.byte	0xf
	.byte	0xaf
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_head_data_block
	.uleb128 0x1d
	.4byte	.LASF311
	.byte	0xf
	.byte	0xb0
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_head_data_block_count
	.uleb128 0x1d
	.4byte	.LASF312
	.byte	0xf
	.byte	0xb1
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_gc_skip_write_count
	.uleb128 0x1d
	.4byte	.LASF313
	.byte	0xf
	.byte	0xb2
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_cur_erase_blk
	.uleb128 0x1d
	.4byte	.LASF314
	.byte	0xf
	.byte	0xb4
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_recovery_page_num
	.uleb128 0x1d
	.4byte	.LASF315
	.byte	0xf
	.byte	0xb5
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	g_recovery_page_min_ver
	.uleb128 0xb
	.4byte	0x2f3
	.4byte	0x1797
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x1f
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF316
	.byte	0xf
	.byte	0xb6
	.4byte	0x1787
	.uleb128 0x9
	.byte	0x3
	.8byte	g_recovery_ppa_tbl
	.uleb128 0x1d
	.4byte	.LASF317
	.byte	0xf
	.byte	0xb7
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	c_mlc_erase_count_value
	.uleb128 0x1d
	.4byte	.LASF318
	.byte	0xf
	.byte	0xb8
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_power_lost_recovery_flag
	.uleb128 0xd
	.4byte	.LASF319
	.byte	0xc
	.byte	0x14
	.4byte	0x363
	.uleb128 0xd
	.4byte	.LASF320
	.byte	0xc
	.byte	0x15
	.4byte	0x363
	.uleb128 0xd
	.4byte	.LASF321
	.byte	0xc
	.byte	0x16
	.4byte	0x405
	.uleb128 0xb
	.4byte	0xa1
	.4byte	0x1808
	.uleb128 0x19
	.4byte	0x3d
	.2byte	0x3ff
	.byte	0
	.uleb128 0xd
	.4byte	.LASF322
	.byte	0xc
	.byte	0x17
	.4byte	0x17f7
	.uleb128 0x1d
	.4byte	.LASF323
	.byte	0x3
	.byte	0x5
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	DeviceCapacity
	.uleb128 0x1d
	.4byte	.LASF324
	.byte	0x3
	.byte	0x6
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	gFtlInitStatus
	.uleb128 0xd
	.4byte	.LASF325
	.byte	0x3
	.byte	0x6c
	.4byte	0x2f3
	.uleb128 0xd
	.4byte	.LASF326
	.byte	0x3
	.byte	0x6d
	.4byte	0x2f3
	.uleb128 0xd
	.4byte	.LASF327
	.byte	0x3
	.byte	0x6e
	.4byte	0x2f3
	.uleb128 0xd
	.4byte	.LASF328
	.byte	0x3
	.byte	0x6f
	.4byte	0x2f3
	.uleb128 0x1e
	.4byte	.LASF329
	.byte	0x3
	.2byte	0x3c0
	.4byte	0x2f
	.uleb128 0x9
	.byte	0x3
	.8byte	power_up_flag
	.uleb128 0x1f
	.4byte	.LASF333
	.byte	0x3
	.2byte	0x483
	.4byte	0x2f3
	.uleb128 0x9
	.byte	0x3
	.8byte	gc_discard_updated
	.uleb128 0x1e
	.4byte	.LASF330
	.byte	0x2
	.2byte	0x38c
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	g_ect_tbl_power_up_flush
	.uleb128 0x1e
	.4byte	.LASF331
	.byte	0x2
	.2byte	0x3a1
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	FtlUpdateVaildLpnCount
	.uleb128 0xb
	.4byte	0x2e8
	.4byte	0x18d2
	.uleb128 0x19
	.4byte	0x3d
	.2byte	0x1fff
	.byte	0
	.uleb128 0x1e
	.4byte	.LASF332
	.byte	0x2
	.2byte	0x8df
	.4byte	0x18c1
	.uleb128 0x9
	.byte	0x3
	.8byte	check_vpc_table
	.uleb128 0x20
	.4byte	.LASF334
	.byte	0x5
	.byte	0x6d
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	ftl_gc_temp_block_bops_scan_page_addr
	.uleb128 0x1e
	.4byte	.LASF335
	.byte	0x5
	.2byte	0x1d4
	.4byte	0x2e8
	.uleb128 0x9
	.byte	0x3
	.8byte	gc_ink_free_return_value
	.uleb128 0x20
	.4byte	.LASF336
	.byte	0x1
	.byte	0xe
	.4byte	0x17f7
	.uleb128 0x9
	.byte	0x3
	.8byte	check_buf
	.uleb128 0x21
	.4byte	0x1808
	.byte	0x1
	.byte	0xf
	.uleb128 0x9
	.byte	0x3
	.8byte	ftl_temp_buf
	.uleb128 0xb
	.4byte	0xa1
	.4byte	0x1949
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x7f
	.byte	0
	.uleb128 0x20
	.4byte	.LASF337
	.byte	0x1
	.byte	0x10
	.4byte	0x1939
	.uleb128 0x9
	.byte	0x3
	.8byte	check_spare_buf
	.uleb128 0x22
	.4byte	.LASF340
	.byte	0x1
	.byte	0xd2
	.4byte	0x2f
	.8byte	.LFB268
	.8byte	.LFE268-.LFB268
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x19ba
	.uleb128 0x23
	.4byte	.LASF338
	.byte	0x1
	.byte	0xd2
	.4byte	0x2d
	.4byte	.LLST335
	.uleb128 0x23
	.4byte	.LASF339
	.byte	0x1
	.byte	0xd2
	.4byte	0x2d
	.4byte	.LLST336
	.uleb128 0x23
	.4byte	.LASF111
	.byte	0x1
	.byte	0xd2
	.4byte	0x50
	.4byte	.LLST337
	.uleb128 0x24
	.8byte	.LVL1733
	.4byte	0x7588
	.byte	0
	.uleb128 0x22
	.4byte	.LASF341
	.byte	0x1
	.byte	0xcd
	.4byte	0x2d
	.8byte	.LFB267
	.8byte	.LFE267-.LFB267
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1a16
	.uleb128 0x23
	.4byte	.LASF342
	.byte	0x1
	.byte	0xcd
	.4byte	0x2d
	.4byte	.LLST203
	.uleb128 0x23
	.4byte	.LASF343
	.byte	0x1
	.byte	0xcd
	.4byte	0x1a16
	.4byte	.LLST204
	.uleb128 0x23
	.4byte	.LASF344
	.byte	0x1
	.byte	0xcd
	.4byte	0x50
	.4byte	.LLST205
	.uleb128 0x24
	.8byte	.LVL914
	.4byte	0x7593
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x1a1c
	.uleb128 0x25
	.uleb128 0x22
	.4byte	.LASF345
	.byte	0x1
	.byte	0xc8
	.4byte	0x2d
	.8byte	.LFB266
	.8byte	.LFE266-.LFB266
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1a73
	.uleb128 0x26
	.string	"s"
	.byte	0x1
	.byte	0xc8
	.4byte	0x2d
	.4byte	.LLST117
	.uleb128 0x26
	.string	"c"
	.byte	0x1
	.byte	0xc8
	.4byte	0x2f
	.4byte	.LLST118
	.uleb128 0x26
	.string	"n"
	.byte	0x1
	.byte	0xc8
	.4byte	0x50
	.4byte	.LLST119
	.uleb128 0x24
	.8byte	.LVL498
	.4byte	0x759c
	.byte	0
	.uleb128 0x27
	.4byte	.LASF346
	.byte	0x1
	.byte	0xb6
	.4byte	0x96
	.8byte	.LFB265
	.8byte	.LFE265-.LFB265
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1ae5
	.uleb128 0x23
	.4byte	.LASF347
	.byte	0x1
	.byte	0xb6
	.4byte	0x456
	.4byte	.LLST200
	.uleb128 0x26
	.string	"die"
	.byte	0x1
	.byte	0xb6
	.4byte	0xa1
	.4byte	.LLST201
	.uleb128 0x28
	.4byte	.LASF111
	.byte	0x1
	.byte	0xb8
	.4byte	0x8b
	.4byte	.LLST202
	.uleb128 0x29
	.string	"i"
	.byte	0x1
	.byte	0xb8
	.4byte	0x8b
	.uleb128 0x24
	.8byte	.LVL903
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL907
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF367
	.byte	0x1
	.byte	0x9b
	.4byte	0x2fe
	.byte	0x1
	.4byte	0x1b29
	.uleb128 0x2b
	.4byte	.LASF360
	.byte	0x1
	.byte	0x9b
	.4byte	0x2e8
	.uleb128 0x29
	.string	"sts"
	.byte	0x1
	.byte	0x9d
	.4byte	0x2fe
	.uleb128 0x2c
	.4byte	.LASF348
	.byte	0x1
	.byte	0x9e
	.4byte	0x1b29
	.uleb128 0x29
	.string	"req"
	.byte	0x1
	.byte	0x9f
	.4byte	0x314
	.uleb128 0x2d
	.string	"out"
	.byte	0x1
	.byte	0xb1
	.byte	0
	.uleb128 0xb
	.4byte	0x2f3
	.4byte	0x1b39
	.uleb128 0x17
	.4byte	0x3d
	.byte	0xf
	.byte	0
	.uleb128 0x27
	.4byte	.LASF349
	.byte	0x1
	.byte	0x85
	.4byte	0x96
	.8byte	.LFB263
	.8byte	.LFE263-.LFB263
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1bdb
	.uleb128 0x23
	.4byte	.LASF350
	.byte	0x1
	.byte	0x85
	.4byte	0x2d
	.4byte	.LLST99
	.uleb128 0x23
	.4byte	.LASF351
	.byte	0x1
	.byte	0x85
	.4byte	0xa1
	.4byte	.LLST100
	.uleb128 0x23
	.4byte	.LASF352
	.byte	0x1
	.byte	0x85
	.4byte	0xa1
	.4byte	.LLST101
	.uleb128 0x29
	.string	"i"
	.byte	0x1
	.byte	0x87
	.4byte	0xa1
	.uleb128 0x29
	.string	"cs"
	.byte	0x1
	.byte	0x87
	.4byte	0xa1
	.uleb128 0x2c
	.4byte	.LASF58
	.byte	0x1
	.byte	0x87
	.4byte	0xa1
	.uleb128 0x20
	.4byte	.LASF353
	.byte	0x1
	.byte	0x87
	.4byte	0xa1
	.uleb128 0x2
	.byte	0x91
	.sleb128 44
	.uleb128 0x29
	.string	"req"
	.byte	0x1
	.byte	0x88
	.4byte	0xfdb
	.uleb128 0x28
	.4byte	.LASF354
	.byte	0x1
	.byte	0x89
	.4byte	0xa1
	.4byte	.LLST102
	.uleb128 0x24
	.8byte	.LVL422
	.4byte	0x673e
	.byte	0
	.uleb128 0x27
	.4byte	.LASF355
	.byte	0x1
	.byte	0x49
	.4byte	0x96
	.8byte	.LFB262
	.8byte	.LFE262-.LFB262
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1cf8
	.uleb128 0x23
	.4byte	.LASF350
	.byte	0x1
	.byte	0x49
	.4byte	0x2d
	.4byte	.LLST94
	.uleb128 0x23
	.4byte	.LASF352
	.byte	0x1
	.byte	0x49
	.4byte	0xa1
	.4byte	.LLST95
	.uleb128 0x23
	.4byte	.LASF351
	.byte	0x1
	.byte	0x49
	.4byte	0xa1
	.4byte	.LLST96
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x49
	.4byte	0xa1
	.4byte	.LLST97
	.uleb128 0x29
	.string	"i"
	.byte	0x1
	.byte	0x4b
	.4byte	0xa1
	.uleb128 0x29
	.string	"cs"
	.byte	0x1
	.byte	0x4b
	.4byte	0xa1
	.uleb128 0x2c
	.4byte	.LASF58
	.byte	0x1
	.byte	0x4b
	.4byte	0xa1
	.uleb128 0x20
	.4byte	.LASF353
	.byte	0x1
	.byte	0x4b
	.4byte	0xa1
	.uleb128 0x2
	.byte	0x91
	.sleb128 44
	.uleb128 0x28
	.4byte	.LASF354
	.byte	0x1
	.byte	0x4c
	.4byte	0xa1
	.4byte	.LLST98
	.uleb128 0x29
	.string	"req"
	.byte	0x1
	.byte	0x4d
	.4byte	0xfdb
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x1d08
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4693
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x110
	.4byte	0x1cdd
	.uleb128 0x20
	.4byte	.LASF358
	.byte	0x1
	.byte	0x68
	.4byte	0x314
	.uleb128 0x2
	.byte	0x91
	.sleb128 -32
	.uleb128 0x24
	.8byte	.LVL409
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL410
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL411
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL412
	.4byte	0x75a7
	.byte	0
	.uleb128 0x24
	.8byte	.LVL405
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL406
	.4byte	0x673e
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x1d08
	.uleb128 0x17
	.4byte	0x3d
	.byte	0xe
	.byte	0
	.uleb128 0x5
	.4byte	0x1cf8
	.uleb128 0x27
	.4byte	.LASF359
	.byte	0x1
	.byte	0x2b
	.4byte	0x96
	.8byte	.LFB261
	.8byte	.LFE261-.LFB261
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1dcf
	.uleb128 0x23
	.4byte	.LASF350
	.byte	0x1
	.byte	0x2b
	.4byte	0x2d
	.4byte	.LLST83
	.uleb128 0x23
	.4byte	.LASF352
	.byte	0x1
	.byte	0x2b
	.4byte	0xa1
	.4byte	.LLST84
	.uleb128 0x23
	.4byte	.LASF351
	.byte	0x1
	.byte	0x2b
	.4byte	0xa1
	.4byte	.LLST85
	.uleb128 0x29
	.string	"i"
	.byte	0x1
	.byte	0x2d
	.4byte	0xa1
	.uleb128 0x29
	.string	"cs"
	.byte	0x1
	.byte	0x2d
	.4byte	0xa1
	.uleb128 0x2c
	.4byte	.LASF58
	.byte	0x1
	.byte	0x2d
	.4byte	0xa1
	.uleb128 0x20
	.4byte	.LASF353
	.byte	0x1
	.byte	0x2d
	.4byte	0xa1
	.uleb128 0x2
	.byte	0x91
	.sleb128 60
	.uleb128 0x28
	.4byte	.LASF354
	.byte	0x1
	.byte	0x2e
	.4byte	0xa1
	.4byte	.LLST86
	.uleb128 0x29
	.string	"req"
	.byte	0x1
	.byte	0x2f
	.4byte	0xfdb
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x1d08
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4676
	.uleb128 0x24
	.8byte	.LVL366
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL367
	.4byte	0x673e
	.byte	0
	.uleb128 0x30
	.4byte	.LASF663
	.byte	0x1
	.byte	0x11
	.4byte	0xa1
	.byte	0x1
	.4byte	0x1e4e
	.uleb128 0x31
	.string	"req"
	.byte	0x1
	.byte	0x11
	.4byte	0xfdb
	.uleb128 0x2b
	.4byte	.LASF353
	.byte	0x1
	.byte	0x11
	.4byte	0x35d
	.uleb128 0x2b
	.4byte	.LASF361
	.byte	0x1
	.byte	0x11
	.4byte	0x35d
	.uleb128 0x2c
	.4byte	.LASF362
	.byte	0x1
	.byte	0x13
	.4byte	0x8b
	.uleb128 0x2c
	.4byte	.LASF363
	.byte	0x1
	.byte	0x13
	.4byte	0x8b
	.uleb128 0x2c
	.4byte	.LASF68
	.byte	0x1
	.byte	0x14
	.4byte	0x8b
	.uleb128 0x2c
	.4byte	.LASF69
	.byte	0x1
	.byte	0x14
	.4byte	0x8b
	.uleb128 0x2c
	.4byte	.LASF364
	.byte	0x1
	.byte	0x15
	.4byte	0xa1
	.uleb128 0x2c
	.4byte	.LASF59
	.byte	0x1
	.byte	0x16
	.4byte	0xa1
	.uleb128 0x2c
	.4byte	.LASF365
	.byte	0x1
	.byte	0x17
	.4byte	0xa1
	.byte	0
	.uleb128 0x32
	.4byte	.LASF366
	.byte	0x5
	.2byte	0x2f4
	.4byte	0xa1
	.8byte	.LFB259
	.8byte	.LFE259-.LFB259
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1e7e
	.uleb128 0x24
	.8byte	.LVL1512
	.4byte	0x1e7e
	.byte	0
	.uleb128 0x33
	.4byte	.LASF368
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x2f
	.byte	0x1
	.4byte	0x1f4b
	.uleb128 0x34
	.4byte	.LASF369
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x2f3
	.uleb128 0x34
	.4byte	.LASF370
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF371
	.byte	0x5
	.2byte	0x1d7
	.4byte	0x2e8
	.uleb128 0x36
	.string	"lpa"
	.byte	0x5
	.2byte	0x1d8
	.4byte	0x2f3
	.uleb128 0x36
	.string	"ppa"
	.byte	0x5
	.2byte	0x1d8
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF372
	.byte	0x5
	.2byte	0x1d9
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF82
	.byte	0x5
	.2byte	0x1da
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x5
	.2byte	0x1da
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF374
	.byte	0x5
	.2byte	0x1da
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF375
	.byte	0x5
	.2byte	0x1da
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF376
	.byte	0x5
	.2byte	0x1db
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF377
	.byte	0x5
	.2byte	0x1dc
	.4byte	0x1f4b
	.uleb128 0x37
	.4byte	.LASF378
	.byte	0x5
	.2byte	0x272
	.uleb128 0x37
	.4byte	.LASF379
	.byte	0x5
	.2byte	0x275
	.uleb128 0x38
	.4byte	.LASF357
	.4byte	0x1f61
	.4byte	.LASF368
	.uleb128 0x39
	.uleb128 0x35
	.4byte	.LASF380
	.byte	0x5
	.2byte	0x1f3
	.4byte	0x2e8
	.byte	0
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x757
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x1f61
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x16
	.byte	0
	.uleb128 0x5
	.4byte	0x1f51
	.uleb128 0x33
	.4byte	.LASF381
	.byte	0x5
	.2byte	0x1a2
	.4byte	0x2f
	.byte	0x1
	.4byte	0x1fc6
	.uleb128 0x36
	.string	"ret"
	.byte	0x5
	.2byte	0x1a4
	.4byte	0x2f
	.uleb128 0x36
	.string	"i"
	.byte	0x5
	.2byte	0x1a5
	.4byte	0x2f
	.uleb128 0x36
	.string	"ppa"
	.byte	0x5
	.2byte	0x1a6
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF348
	.byte	0x5
	.2byte	0x1a7
	.4byte	0x1b29
	.uleb128 0x3a
	.4byte	0x1fb7
	.uleb128 0x35
	.4byte	.LASF382
	.byte	0x5
	.2byte	0x1b1
	.4byte	0x314
	.byte	0
	.uleb128 0x39
	.uleb128 0x35
	.4byte	.LASF383
	.byte	0x5
	.2byte	0x1c6
	.4byte	0x2f3
	.byte	0
	.byte	0
	.uleb128 0x33
	.4byte	.LASF384
	.byte	0x5
	.2byte	0x185
	.4byte	0x2f
	.byte	0x1
	.4byte	0x2010
	.uleb128 0x34
	.4byte	.LASF385
	.byte	0x5
	.2byte	0x185
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x5
	.2byte	0x187
	.4byte	0x2e8
	.uleb128 0x36
	.string	"i"
	.byte	0x5
	.2byte	0x188
	.4byte	0x2e8
	.uleb128 0x36
	.string	"j"
	.byte	0x5
	.2byte	0x188
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF175
	.byte	0x5
	.2byte	0x189
	.4byte	0x2e8
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF494
	.byte	0x5
	.2byte	0x17a
	.4byte	0x2f
	.byte	0x1
	.uleb128 0x32
	.4byte	.LASF386
	.byte	0x5
	.2byte	0x166
	.4byte	0x2f
	.8byte	.LFB254
	.8byte	.LFE254-.LFB254
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x208d
	.uleb128 0x3c
	.4byte	.LASF175
	.byte	0x5
	.2byte	0x166
	.4byte	0x2e8
	.4byte	.LLST82
	.uleb128 0x36
	.string	"i"
	.byte	0x5
	.2byte	0x168
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF387
	.byte	0x5
	.2byte	0x169
	.4byte	0x2e8
	.uleb128 0x24
	.8byte	.LVL352
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL353
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL354
	.4byte	0x208d
	.byte	0
	.uleb128 0x32
	.4byte	.LASF388
	.byte	0x5
	.2byte	0x158
	.4byte	0x2f
	.8byte	.LFB253
	.8byte	.LFE253-.LFB253
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x20cd
	.uleb128 0x3c
	.4byte	.LASF389
	.byte	0x5
	.2byte	0x158
	.4byte	0x2e8
	.4byte	.LLST81
	.uleb128 0x24
	.8byte	.LVL347
	.4byte	0x75a7
	.byte	0
	.uleb128 0x32
	.4byte	.LASF390
	.byte	0x5
	.2byte	0x13b
	.4byte	0x2f3
	.8byte	.LFB252
	.8byte	.LFE252-.LFB252
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2169
	.uleb128 0x3d
	.string	"i"
	.byte	0x5
	.2byte	0x13d
	.4byte	0x2e8
	.4byte	.LLST286
	.uleb128 0x3e
	.4byte	.LASF377
	.byte	0x5
	.2byte	0x13e
	.4byte	0x1f4b
	.4byte	.LLST287
	.uleb128 0x24
	.8byte	.LVL1400
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL1402
	.4byte	0x25f1
	.uleb128 0x24
	.8byte	.LVL1403
	.4byte	0x2378
	.uleb128 0x24
	.8byte	.LVL1405
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL1406
	.4byte	0x510a
	.uleb128 0x24
	.8byte	.LVL1407
	.4byte	0x2333
	.uleb128 0x24
	.8byte	.LVL1411
	.4byte	0x24f1
	.byte	0
	.uleb128 0x32
	.4byte	.LASF391
	.byte	0x5
	.2byte	0x12b
	.4byte	0x2f3
	.8byte	.LFB251
	.8byte	.LFE251-.LFB251
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x21e7
	.uleb128 0x24
	.8byte	.LVL1415
	.4byte	0x57a6
	.uleb128 0x24
	.8byte	.LVL1416
	.4byte	0x2378
	.uleb128 0x24
	.8byte	.LVL1417
	.4byte	0x28b2
	.uleb128 0x24
	.8byte	.LVL1418
	.4byte	0x40c4
	.uleb128 0x24
	.8byte	.LVL1419
	.4byte	0x3d9f
	.uleb128 0x24
	.8byte	.LVL1420
	.4byte	0x3baa
	.uleb128 0x24
	.8byte	.LVL1421
	.4byte	0x2747
	.byte	0
	.uleb128 0x3f
	.4byte	.LASF450
	.byte	0x5
	.2byte	0x120
	.byte	0x1
	.uleb128 0x22
	.4byte	.LASF392
	.byte	0x5
	.byte	0xd2
	.4byte	0x2f3
	.8byte	.LFB249
	.8byte	.LFE249-.LFB249
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2333
	.uleb128 0x23
	.4byte	.LASF393
	.byte	0x5
	.byte	0xd2
	.4byte	0x1423
	.4byte	.LLST190
	.uleb128 0x23
	.4byte	.LASF394
	.byte	0x5
	.byte	0xd2
	.4byte	0x2f3
	.4byte	.LLST191
	.uleb128 0x28
	.4byte	.LASF375
	.byte	0x5
	.byte	0xd4
	.4byte	0x2e8
	.4byte	.LLST192
	.uleb128 0x28
	.4byte	.LASF352
	.byte	0x5
	.byte	0xd5
	.4byte	0x2e8
	.4byte	.LLST193
	.uleb128 0x29
	.string	"req"
	.byte	0x5
	.byte	0xd5
	.4byte	0x2e8
	.uleb128 0x2c
	.4byte	.LASF373
	.byte	0x5
	.byte	0xd6
	.4byte	0x2e8
	.uleb128 0x28
	.4byte	.LASF169
	.byte	0x5
	.byte	0xd6
	.4byte	0x2e8
	.4byte	.LLST194
	.uleb128 0x28
	.4byte	.LASF167
	.byte	0x5
	.byte	0xd6
	.4byte	0x2e8
	.4byte	.LLST195
	.uleb128 0x28
	.4byte	.LASF395
	.byte	0x5
	.byte	0xd7
	.4byte	0x2f3
	.4byte	.LLST196
	.uleb128 0x28
	.4byte	.LASF396
	.byte	0x5
	.byte	0xd8
	.4byte	0x2f3
	.4byte	.LLST197
	.uleb128 0x2c
	.4byte	.LASF397
	.byte	0x5
	.byte	0xd9
	.4byte	0x2f3
	.uleb128 0x28
	.4byte	.LASF377
	.byte	0x5
	.byte	0xda
	.4byte	0x1f4b
	.4byte	.LLST198
	.uleb128 0x40
	.4byte	.LASF398
	.byte	0x5
	.byte	0xe1
	.uleb128 0x40
	.4byte	.LASF399
	.byte	0x5
	.byte	0xe5
	.uleb128 0x41
	.4byte	.LASF468
	.byte	0x5
	.2byte	0x117
	.8byte	.L867
	.uleb128 0x24
	.8byte	.LVL858
	.4byte	0x2333
	.uleb128 0x24
	.8byte	.LVL865
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL876
	.4byte	0x4d18
	.uleb128 0x24
	.8byte	.LVL880
	.4byte	0x36b2
	.uleb128 0x24
	.8byte	.LVL883
	.4byte	0x24f1
	.uleb128 0x24
	.8byte	.LVL885
	.4byte	0x49c3
	.uleb128 0x24
	.8byte	.LVL886
	.4byte	0x2333
	.byte	0
	.uleb128 0x42
	.4byte	.LASF406
	.byte	0x5
	.byte	0xc9
	.8byte	.LFB248
	.8byte	.LFE248-.LFB248
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2378
	.uleb128 0x24
	.8byte	.LVL852
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL853
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL854
	.4byte	0x2645
	.byte	0
	.uleb128 0x22
	.4byte	.LASF400
	.byte	0x5
	.byte	0x6f
	.4byte	0x2f
	.8byte	.LFB247
	.8byte	.LFE247-.LFB247
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x24dc
	.uleb128 0x23
	.4byte	.LASF401
	.byte	0x5
	.byte	0x6f
	.4byte	0x2f
	.4byte	.LLST281
	.uleb128 0x43
	.string	"i"
	.byte	0x5
	.byte	0x71
	.4byte	0x2e8
	.4byte	.LLST282
	.uleb128 0x2c
	.4byte	.LASF402
	.byte	0x5
	.byte	0x71
	.4byte	0x2e8
	.uleb128 0x28
	.4byte	.LASF403
	.byte	0x5
	.byte	0x72
	.4byte	0x2f3
	.4byte	.LLST283
	.uleb128 0x28
	.4byte	.LASF404
	.byte	0x5
	.byte	0x73
	.4byte	0x2f3
	.4byte	.LLST284
	.uleb128 0x2c
	.4byte	.LASF393
	.byte	0x5
	.byte	0x74
	.4byte	0x1423
	.uleb128 0x28
	.4byte	.LASF405
	.byte	0x5
	.byte	0x75
	.4byte	0x1468
	.4byte	.LLST285
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x24ec
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4526
	.uleb128 0x24
	.8byte	.LVL1366
	.4byte	0x21f0
	.uleb128 0x24
	.8byte	.LVL1368
	.4byte	0x3d9f
	.uleb128 0x24
	.8byte	.LVL1369
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL1370
	.4byte	0x510a
	.uleb128 0x24
	.8byte	.LVL1373
	.4byte	0x57a6
	.uleb128 0x24
	.8byte	.LVL1374
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1377
	.4byte	0x266c
	.uleb128 0x24
	.8byte	.LVL1378
	.4byte	0x499e
	.uleb128 0x24
	.8byte	.LVL1380
	.4byte	0x40c4
	.uleb128 0x24
	.8byte	.LVL1381
	.4byte	0x3baa
	.uleb128 0x24
	.8byte	.LVL1383
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1384
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1385
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL1386
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1387
	.4byte	0x266c
	.uleb128 0x24
	.8byte	.LVL1390
	.4byte	0x49c3
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x24ec
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x12
	.byte	0
	.uleb128 0x5
	.4byte	0x24dc
	.uleb128 0x42
	.4byte	.LASF407
	.byte	0x5
	.byte	0x54
	.8byte	.LFB246
	.8byte	.LFE246-.LFB246
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2563
	.uleb128 0x23
	.4byte	.LASF408
	.byte	0x5
	.byte	0x54
	.4byte	0x2f3
	.4byte	.LLST77
	.uleb128 0x23
	.4byte	.LASF178
	.byte	0x5
	.byte	0x54
	.4byte	0x2f3
	.4byte	.LLST78
	.uleb128 0x26
	.string	"lpa"
	.byte	0x5
	.byte	0x54
	.4byte	0x2f3
	.4byte	.LLST79
	.uleb128 0x20
	.4byte	.LASF409
	.byte	0x5
	.byte	0x56
	.4byte	0x2e8
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x43
	.string	"i"
	.byte	0x5
	.byte	0x57
	.4byte	0x2e8
	.4byte	.LLST80
	.uleb128 0x24
	.8byte	.LVL340
	.4byte	0x4cdc
	.byte	0
	.uleb128 0x22
	.4byte	.LASF410
	.byte	0x5
	.byte	0x49
	.4byte	0x2f3
	.8byte	.LFB245
	.8byte	.LFE245-.LFB245
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x259d
	.uleb128 0x26
	.string	"blk"
	.byte	0x5
	.byte	0x49
	.4byte	0x2e8
	.4byte	.LLST76
	.uleb128 0x29
	.string	"i"
	.byte	0x5
	.byte	0x4b
	.4byte	0x2e8
	.byte	0
	.uleb128 0x42
	.4byte	.LASF411
	.byte	0x5
	.byte	0x38
	.8byte	.LFB244
	.8byte	.LFE244-.LFB244
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x25f1
	.uleb128 0x44
	.string	"req"
	.byte	0x5
	.byte	0x38
	.4byte	0xfdb
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x45
	.4byte	.LASF412
	.byte	0x5
	.byte	0x38
	.4byte	0x2f3
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x43
	.string	"i"
	.byte	0x5
	.byte	0x3a
	.4byte	0x2e8
	.4byte	.LLST74
	.uleb128 0x28
	.4byte	.LASF413
	.byte	0x5
	.byte	0x3b
	.4byte	0x2e8
	.4byte	.LLST75
	.byte	0
	.uleb128 0x42
	.4byte	.LASF414
	.byte	0x5
	.byte	0x29
	.8byte	.LFB243
	.8byte	.LFE243-.LFB243
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2645
	.uleb128 0x44
	.string	"req"
	.byte	0x5
	.byte	0x29
	.4byte	0xfdb
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x45
	.4byte	.LASF412
	.byte	0x5
	.byte	0x29
	.4byte	0x2f3
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x43
	.string	"i"
	.byte	0x5
	.byte	0x2b
	.4byte	0x2e8
	.4byte	.LLST72
	.uleb128 0x28
	.4byte	.LASF413
	.byte	0x5
	.byte	0x2c
	.4byte	0x2e8
	.4byte	.LLST73
	.byte	0
	.uleb128 0x42
	.4byte	.LASF415
	.byte	0x5
	.byte	0x15
	.8byte	.LFB242
	.8byte	.LFE242-.LFB242
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x266c
	.uleb128 0x29
	.string	"i"
	.byte	0x5
	.byte	0x17
	.4byte	0x2e8
	.byte	0
	.uleb128 0x32
	.4byte	.LASF416
	.byte	0x2
	.2byte	0xac3
	.4byte	0x2f
	.8byte	.LFB241
	.8byte	.LFE241-.LFB241
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x26f2
	.uleb128 0x3c
	.4byte	.LASF402
	.byte	0x2
	.2byte	0xac3
	.4byte	0x2e8
	.4byte	.LLST221
	.uleb128 0x36
	.string	"ret"
	.byte	0x2
	.2byte	0xac5
	.4byte	0x2f
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2702
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4456
	.uleb128 0x24
	.8byte	.LVL1007
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1010
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1011
	.4byte	0x2707
	.uleb128 0x24
	.8byte	.LVL1012
	.4byte	0x75a7
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x2702
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x13
	.byte	0
	.uleb128 0x5
	.4byte	0x26f2
	.uleb128 0x33
	.4byte	.LASF417
	.byte	0x2
	.2byte	0xaae
	.4byte	0x2f
	.byte	0x1
	.4byte	0x2732
	.uleb128 0x34
	.4byte	.LASF402
	.byte	0x2
	.2byte	0xaae
	.4byte	0x2e8
	.uleb128 0x38
	.4byte	.LASF357
	.4byte	0x2742
	.4byte	.LASF417
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x2742
	.uleb128 0x17
	.4byte	0x3d
	.byte	0xf
	.byte	0
	.uleb128 0x5
	.4byte	0x2732
	.uleb128 0x32
	.4byte	.LASF418
	.byte	0x2
	.2byte	0xa8e
	.4byte	0x2f3
	.8byte	.LFB239
	.8byte	.LFE239-.LFB239
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x27df
	.uleb128 0x3c
	.4byte	.LASF419
	.byte	0x2
	.2byte	0xa8e
	.4byte	0x1423
	.4byte	.LLST70
	.uleb128 0x3e
	.4byte	.LASF375
	.byte	0x2
	.2byte	0xa90
	.4byte	0x2e8
	.4byte	.LLST71
	.uleb128 0x1f
	.4byte	.LASF420
	.byte	0x2
	.2byte	0xa91
	.4byte	0x2f3
	.uleb128 0x1
	.byte	0x65
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x24ec
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4436
	.uleb128 0x24
	.8byte	.LVL308
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL309
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL310
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL313
	.4byte	0x75a7
	.byte	0
	.uleb128 0x32
	.4byte	.LASF421
	.byte	0x2
	.2byte	0xa76
	.4byte	0x2f
	.8byte	.LFB238
	.8byte	.LFE238-.LFB238
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x289d
	.uleb128 0x3c
	.4byte	.LASF419
	.byte	0x2
	.2byte	0xa76
	.4byte	0x1423
	.4byte	.LLST267
	.uleb128 0x3e
	.4byte	.LASF402
	.byte	0x2
	.2byte	0xa78
	.4byte	0x2e8
	.4byte	.LLST268
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x28ad
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4429
	.uleb128 0x24
	.8byte	.LVL1298
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1299
	.4byte	0x499e
	.uleb128 0x24
	.8byte	.LVL1300
	.4byte	0x2707
	.uleb128 0x24
	.8byte	.LVL1301
	.4byte	0x28b2
	.uleb128 0x24
	.8byte	.LVL1302
	.4byte	0x40c4
	.uleb128 0x24
	.8byte	.LVL1303
	.4byte	0x3d9f
	.uleb128 0x24
	.8byte	.LVL1304
	.4byte	0x3baa
	.uleb128 0x24
	.8byte	.LVL1307
	.4byte	0x49c3
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x28ad
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x1c
	.byte	0
	.uleb128 0x5
	.4byte	0x289d
	.uleb128 0x32
	.4byte	.LASF422
	.byte	0x2
	.2byte	0xa12
	.4byte	0x2f
	.8byte	.LFB237
	.8byte	.LFE237-.LFB237
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2a64
	.uleb128 0x46
	.string	"p"
	.byte	0x2
	.2byte	0xa12
	.4byte	0x1423
	.4byte	.LLST210
	.uleb128 0x3e
	.4byte	.LASF423
	.byte	0x2
	.2byte	0xa14
	.4byte	0x2e8
	.4byte	.LLST211
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x2
	.2byte	0xa15
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF352
	.byte	0x2
	.2byte	0xa15
	.4byte	0x2e8
	.4byte	.LLST212
	.uleb128 0x36
	.string	"n"
	.byte	0x2
	.2byte	0xa15
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF375
	.byte	0x2
	.2byte	0xa15
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF424
	.byte	0x2
	.2byte	0xa16
	.4byte	0x2f
	.4byte	.LLST213
	.uleb128 0x3e
	.4byte	.LASF425
	.byte	0x2
	.2byte	0xa17
	.4byte	0x2e8
	.4byte	.LLST214
	.uleb128 0x37
	.4byte	.LASF426
	.byte	0x2
	.2byte	0xa18
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2a74
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4408
	.uleb128 0x47
	.4byte	0x3a2f
	.8byte	.LBB270
	.8byte	.LBE270-.LBB270
	.byte	0x2
	.2byte	0xa48
	.4byte	0x2993
	.uleb128 0x48
	.4byte	0x3a48
	.uleb128 0x48
	.4byte	0x3a3c
	.uleb128 0x24
	.8byte	.LVL964
	.4byte	0x679c
	.byte	0
	.uleb128 0x24
	.8byte	.LVL936
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL940
	.4byte	0x47ac
	.uleb128 0x24
	.8byte	.LVL941
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL942
	.4byte	0x30ad
	.uleb128 0x24
	.8byte	.LVL943
	.4byte	0x499e
	.uleb128 0x24
	.8byte	.LVL944
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL947
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL948
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL949
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL952
	.4byte	0x3a2f
	.uleb128 0x24
	.8byte	.LVL955
	.4byte	0x1b39
	.uleb128 0x24
	.8byte	.LVL957
	.4byte	0x2be7
	.uleb128 0x24
	.8byte	.LVL958
	.4byte	0x510a
	.uleb128 0x24
	.8byte	.LVL960
	.4byte	0x499e
	.uleb128 0x24
	.8byte	.LVL967
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL970
	.4byte	0x75a7
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x2a74
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x18
	.byte	0
	.uleb128 0x5
	.4byte	0x2a64
	.uleb128 0x33
	.4byte	.LASF427
	.byte	0x2
	.2byte	0xa09
	.4byte	0x2f
	.byte	0x1
	.4byte	0x2a97
	.uleb128 0x34
	.4byte	.LASF402
	.byte	0x2
	.2byte	0xa09
	.4byte	0x2e8
	.byte	0
	.uleb128 0x32
	.4byte	.LASF428
	.byte	0x2
	.2byte	0x9c0
	.4byte	0x2e8
	.8byte	.LFB235
	.8byte	.LFE235-.LFB235
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2b57
	.uleb128 0x3e
	.4byte	.LASF429
	.byte	0x2
	.2byte	0x9c2
	.4byte	0x2e8
	.4byte	.LLST63
	.uleb128 0x35
	.4byte	.LASF430
	.byte	0x2
	.2byte	0x9c3
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF431
	.byte	0x2
	.2byte	0x9c4
	.4byte	0x2e8
	.4byte	.LLST64
	.uleb128 0x3e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x9c5
	.4byte	0x134b
	.4byte	.LLST65
	.uleb128 0x3e
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x9c6
	.4byte	0x2e8
	.4byte	.LLST66
	.uleb128 0x3e
	.4byte	.LASF434
	.byte	0x2
	.2byte	0x9c7
	.4byte	0x2e8
	.4byte	.LLST67
	.uleb128 0x36
	.string	"i"
	.byte	0x2
	.2byte	0x9c8
	.4byte	0x2f3
	.uleb128 0x3e
	.4byte	.LASF111
	.byte	0x2
	.2byte	0x9c9
	.4byte	0x2e8
	.4byte	.LLST68
	.uleb128 0x24
	.8byte	.LVL281
	.4byte	0x2b57
	.uleb128 0x24
	.8byte	.LVL289
	.4byte	0x2bb4
	.uleb128 0x24
	.8byte	.LVL296
	.4byte	0x75a7
	.byte	0
	.uleb128 0x32
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x9ab
	.4byte	0x2e8
	.8byte	.LFB234
	.8byte	.LFE234-.LFB234
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2bb4
	.uleb128 0x3c
	.4byte	.LASF90
	.byte	0x2
	.2byte	0x9ab
	.4byte	0x2e8
	.4byte	.LLST56
	.uleb128 0x3e
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x9ad
	.4byte	0x2e8
	.4byte	.LLST57
	.uleb128 0x35
	.4byte	.LASF389
	.byte	0x2
	.2byte	0x9ae
	.4byte	0x2e8
	.uleb128 0x3d
	.string	"i"
	.byte	0x2
	.2byte	0x9af
	.4byte	0x2e8
	.4byte	.LLST58
	.byte	0
	.uleb128 0x32
	.4byte	.LASF437
	.byte	0x2
	.2byte	0x9a2
	.4byte	0x2e8
	.8byte	.LFB233
	.8byte	.LFE233-.LFB233
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2be7
	.uleb128 0x3e
	.4byte	.LASF431
	.byte	0x2
	.2byte	0x9a4
	.4byte	0x2e8
	.4byte	.LLST55
	.byte	0
	.uleb128 0x32
	.4byte	.LASF438
	.byte	0x2
	.2byte	0x992
	.4byte	0x2f
	.8byte	.LFB232
	.8byte	.LFE232-.LFB232
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2c58
	.uleb128 0x3c
	.4byte	.LASF402
	.byte	0x2
	.2byte	0x992
	.4byte	0x2e8
	.4byte	.LLST54
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x2
	.2byte	0x994
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF375
	.byte	0x2
	.2byte	0x994
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF162
	.byte	0x2
	.2byte	0x995
	.4byte	0x2e8
	.uleb128 0x24
	.8byte	.LVL175
	.4byte	0x4d54
	.uleb128 0x24
	.8byte	.LVL176
	.4byte	0x5265
	.byte	0
	.uleb128 0x33
	.4byte	.LASF439
	.byte	0x2
	.2byte	0x933
	.4byte	0x2f
	.byte	0x1
	.4byte	0x2c82
	.uleb128 0x36
	.string	"i"
	.byte	0x2
	.2byte	0x935
	.4byte	0x2f
	.uleb128 0x39
	.uleb128 0x35
	.4byte	.LASF393
	.byte	0x2
	.2byte	0x95a
	.4byte	0x1423
	.byte	0
	.byte	0
	.uleb128 0x49
	.4byte	.LASF440
	.byte	0x2
	.2byte	0x914
	.8byte	.LFB230
	.8byte	.LFE230-.LFB230
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2d0b
	.uleb128 0x3d
	.string	"i"
	.byte	0x2
	.2byte	0x916
	.4byte	0x2f
	.4byte	.LLST164
	.uleb128 0x4a
	.string	"ppa"
	.byte	0x2
	.2byte	0x917
	.4byte	0x2f3
	.uleb128 0x2
	.byte	0x91
	.sleb128 44
	.uleb128 0x35
	.4byte	.LASF441
	.byte	0x2
	.2byte	0x918
	.4byte	0x1f4b
	.uleb128 0x24
	.8byte	.LVL703
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL706
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL707
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL708
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL709
	.4byte	0x75a7
	.byte	0
	.uleb128 0x49
	.4byte	.LASF442
	.byte	0x2
	.2byte	0x8e0
	.8byte	.LFB229
	.8byte	.LFE229-.LFB229
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2df8
	.uleb128 0x36
	.string	"i"
	.byte	0x2
	.2byte	0x8e2
	.4byte	0x2e8
	.uleb128 0x3d
	.string	"lpn"
	.byte	0x2
	.2byte	0x8e3
	.4byte	0x2f3
	.4byte	.LLST187
	.uleb128 0x1f
	.4byte	.LASF443
	.byte	0x2
	.2byte	0x8e4
	.4byte	0x2f3
	.uleb128 0x3
	.byte	0x91
	.sleb128 92
	.uleb128 0x36
	.string	"blk"
	.byte	0x2
	.2byte	0x8e5
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF444
	.byte	0x2
	.2byte	0x8e6
	.4byte	0x2e8
	.4byte	.LLST188
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2e08
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4323
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x230
	.4byte	0x2da9
	.uleb128 0x3e
	.4byte	.LASF90
	.byte	0x2
	.2byte	0x903
	.4byte	0x2e8
	.4byte	.LLST189
	.uleb128 0x24
	.8byte	.LVL851
	.4byte	0x75a7
	.byte	0
	.uleb128 0x24
	.8byte	.LVL837
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL838
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL842
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL846
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL847
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL849
	.4byte	0x75a7
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x2e08
	.uleb128 0x17
	.4byte	0x3d
	.byte	0xd
	.byte	0
	.uleb128 0x5
	.4byte	0x2df8
	.uleb128 0x49
	.4byte	.LASF445
	.byte	0x2
	.2byte	0x8c1
	.8byte	.LFB228
	.8byte	.LFE228-.LFB228
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2ee0
	.uleb128 0x3d
	.string	"i"
	.byte	0x2
	.2byte	0x8c3
	.4byte	0x2e8
	.4byte	.LLST265
	.uleb128 0x3d
	.string	"lpn"
	.byte	0x2
	.2byte	0x8c4
	.4byte	0x2f3
	.4byte	.LLST266
	.uleb128 0x1f
	.4byte	.LASF443
	.byte	0x2
	.2byte	0x8c5
	.4byte	0x2f3
	.uleb128 0x2
	.byte	0x91
	.sleb128 44
	.uleb128 0x36
	.string	"blk"
	.byte	0x2
	.2byte	0x8c6
	.4byte	0x2e8
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2ef0
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4307
	.uleb128 0x24
	.8byte	.LVL1278
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1280
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1285
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1286
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL1289
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1290
	.4byte	0x2707
	.uleb128 0x24
	.8byte	.LVL1291
	.4byte	0x40c4
	.uleb128 0x24
	.8byte	.LVL1292
	.4byte	0x3baa
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x2ef0
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x14
	.byte	0
	.uleb128 0x5
	.4byte	0x2ee0
	.uleb128 0x49
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x89c
	.8byte	.LFB227
	.8byte	.LFE227-.LFB227
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2f7e
	.uleb128 0x3c
	.4byte	.LASF393
	.byte	0x2
	.2byte	0x89c
	.4byte	0x1423
	.4byte	.LLST243
	.uleb128 0x1f
	.4byte	.LASF447
	.byte	0x2
	.2byte	0x89e
	.4byte	0x314
	.uleb128 0x2
	.byte	0x91
	.sleb128 -32
	.uleb128 0x35
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x89f
	.4byte	0x1f4b
	.uleb128 0x35
	.4byte	.LASF448
	.byte	0x2
	.2byte	0x8a0
	.4byte	0x2f
	.uleb128 0x35
	.4byte	.LASF171
	.byte	0x2
	.2byte	0x8a1
	.4byte	0x2f3
	.uleb128 0x24
	.8byte	.LVL1146
	.4byte	0x2747
	.uleb128 0x24
	.8byte	.LVL1147
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL1148
	.4byte	0x266c
	.byte	0
	.uleb128 0x49
	.4byte	.LASF449
	.byte	0x2
	.2byte	0x882
	.8byte	.LFB226
	.8byte	.LFE226-.LFB226
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2fb7
	.uleb128 0x24
	.8byte	.LVL1260
	.4byte	0x3dfc
	.uleb128 0x24
	.8byte	.LVL1261
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x3f
	.4byte	.LASF451
	.byte	0x2
	.2byte	0x86e
	.byte	0x1
	.uleb128 0x32
	.4byte	.LASF452
	.byte	0x2
	.2byte	0x843
	.4byte	0x2f
	.8byte	.LFB224
	.8byte	.LFE224-.LFB224
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3098
	.uleb128 0x3e
	.4byte	.LASF429
	.byte	0x2
	.2byte	0x845
	.4byte	0x2e8
	.4byte	.LLST184
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x2
	.2byte	0x845
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF375
	.byte	0x2
	.2byte	0x845
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF162
	.byte	0x2
	.2byte	0x845
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF453
	.byte	0x2
	.2byte	0x846
	.4byte	0x2e8
	.4byte	.LLST185
	.uleb128 0x3e
	.4byte	.LASF454
	.byte	0x2
	.2byte	0x846
	.4byte	0x2e8
	.4byte	.LLST186
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x30a8
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4282
	.uleb128 0x24
	.8byte	.LVL821
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL822
	.4byte	0x4d54
	.uleb128 0x24
	.8byte	.LVL823
	.4byte	0x5265
	.uleb128 0x24
	.8byte	.LVL826
	.4byte	0x49c3
	.uleb128 0x24
	.8byte	.LVL831
	.4byte	0x499e
	.uleb128 0x24
	.8byte	.LVL833
	.4byte	0x75a7
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x30a8
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x11
	.byte	0
	.uleb128 0x5
	.4byte	0x3098
	.uleb128 0x32
	.4byte	.LASF455
	.byte	0x2
	.2byte	0x82d
	.4byte	0x2f
	.8byte	.LFB223
	.8byte	.LFE223-.LFB223
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3130
	.uleb128 0x46
	.string	"p"
	.byte	0x2
	.2byte	0x82d
	.4byte	0x1423
	.4byte	.LLST53
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x2
	.2byte	0x82f
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF375
	.byte	0x2
	.2byte	0x82f
	.4byte	0x2e8
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2742
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4261
	.uleb128 0x24
	.8byte	.LVL166
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL169
	.4byte	0x4d54
	.uleb128 0x24
	.8byte	.LVL170
	.4byte	0x5265
	.byte	0
	.uleb128 0x32
	.4byte	.LASF456
	.byte	0x2
	.2byte	0x820
	.4byte	0x2f
	.8byte	.LFB222
	.8byte	.LFE222-.LFB222
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x31a1
	.uleb128 0x24
	.8byte	.LVL1394
	.4byte	0x31c7
	.uleb128 0x24
	.8byte	.LVL1395
	.4byte	0x31a1
	.uleb128 0x24
	.8byte	.LVL1396
	.4byte	0x31c7
	.uleb128 0x24
	.8byte	.LVL1397
	.4byte	0x31a1
	.uleb128 0x24
	.8byte	.LVL1398
	.4byte	0x21e7
	.uleb128 0x24
	.8byte	.LVL1399
	.4byte	0x266c
	.byte	0
	.uleb128 0x4b
	.4byte	.LASF492
	.byte	0x2
	.2byte	0x80d
	.byte	0x1
	.4byte	0x31c7
	.uleb128 0x34
	.4byte	.LASF419
	.byte	0x2
	.2byte	0x80d
	.4byte	0x1423
	.uleb128 0x35
	.4byte	.LASF375
	.byte	0x2
	.2byte	0x80f
	.4byte	0x2e8
	.byte	0
	.uleb128 0x32
	.4byte	.LASF457
	.byte	0x2
	.2byte	0x6a8
	.4byte	0x2f
	.8byte	.LFB220
	.8byte	.LFE220-.LFB220
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x35c2
	.uleb128 0x3c
	.4byte	.LASF393
	.byte	0x2
	.2byte	0x6a8
	.4byte	0x1423
	.4byte	.LLST226
	.uleb128 0x3e
	.4byte	.LASF458
	.byte	0x2
	.2byte	0x6aa
	.4byte	0x2e8
	.4byte	.LLST227
	.uleb128 0x3e
	.4byte	.LASF459
	.byte	0x2
	.2byte	0x6aa
	.4byte	0x2e8
	.4byte	.LLST228
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x2
	.2byte	0x6ab
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF169
	.byte	0x2
	.2byte	0x6ab
	.4byte	0x2e8
	.4byte	.LLST229
	.uleb128 0x35
	.4byte	.LASF167
	.byte	0x2
	.2byte	0x6ab
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF460
	.byte	0x2
	.2byte	0x6ac
	.4byte	0x2e8
	.4byte	.LLST230
	.uleb128 0x3e
	.4byte	.LASF461
	.byte	0x2
	.2byte	0x6ac
	.4byte	0x2e8
	.4byte	.LLST231
	.uleb128 0x35
	.4byte	.LASF375
	.byte	0x2
	.2byte	0x6ad
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF462
	.byte	0x2
	.2byte	0x6ae
	.4byte	0x2f
	.4byte	.LLST232
	.uleb128 0x35
	.4byte	.LASF463
	.byte	0x2
	.2byte	0x6ae
	.4byte	0x2f
	.uleb128 0x35
	.4byte	.LASF352
	.byte	0x2
	.2byte	0x6af
	.4byte	0x2e8
	.uleb128 0x3d
	.string	"n"
	.byte	0x2
	.2byte	0x6af
	.4byte	0x2e8
	.4byte	.LLST233
	.uleb128 0x36
	.string	"req"
	.byte	0x2
	.2byte	0x6af
	.4byte	0x2e8
	.uleb128 0x3d
	.string	"lpa"
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x2f3
	.4byte	.LLST234
	.uleb128 0x3e
	.4byte	.LASF443
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x2f3
	.4byte	.LLST235
	.uleb128 0x3e
	.4byte	.LASF403
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x2f3
	.4byte	.LLST236
	.uleb128 0x3e
	.4byte	.LASF395
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x2f3
	.4byte	.LLST237
	.uleb128 0x3e
	.4byte	.LASF409
	.byte	0x2
	.2byte	0x6b1
	.4byte	0x2e8
	.4byte	.LLST238
	.uleb128 0x3e
	.4byte	.LASF464
	.byte	0x2
	.2byte	0x6b1
	.4byte	0x2e8
	.4byte	.LLST239
	.uleb128 0x3e
	.4byte	.LASF465
	.byte	0x2
	.2byte	0x6b2
	.4byte	0x2f3
	.4byte	.LLST240
	.uleb128 0x35
	.4byte	.LASF466
	.byte	0x2
	.2byte	0x6b3
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x6b4
	.4byte	0x1f4b
	.uleb128 0x35
	.4byte	.LASF467
	.byte	0x2
	.2byte	0x6b5
	.4byte	0x2f3
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x35d2
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4204
	.uleb128 0x41
	.4byte	.LASF469
	.byte	0x2
	.2byte	0x744
	.8byte	.L1024
	.uleb128 0x41
	.4byte	.LASF399
	.byte	0x2
	.2byte	0x74c
	.8byte	.L1036
	.uleb128 0x41
	.4byte	.LASF470
	.byte	0x2
	.2byte	0x7ff
	.8byte	.L1042
	.uleb128 0x37
	.4byte	.LASF471
	.byte	0x2
	.2byte	0x809
	.uleb128 0x4c
	.8byte	.LBB291
	.8byte	.LBE291-.LBB291
	.4byte	0x33c9
	.uleb128 0x35
	.4byte	.LASF472
	.byte	0x2
	.2byte	0x71c
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF473
	.byte	0x2
	.2byte	0x71d
	.4byte	0x2f3
	.uleb128 0x24
	.8byte	.LVL1080
	.4byte	0x1d0d
	.byte	0
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x2d0
	.4byte	0x345b
	.uleb128 0x3e
	.4byte	.LASF474
	.byte	0x2
	.2byte	0x777
	.4byte	0x2f3
	.4byte	.LLST241
	.uleb128 0x3e
	.4byte	.LASF475
	.byte	0x2
	.2byte	0x778
	.4byte	0x2f3
	.4byte	.LLST242
	.uleb128 0x24
	.8byte	.LVL1105
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1107
	.4byte	0x266c
	.uleb128 0x24
	.8byte	.LVL1116
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1119
	.4byte	0x4c8f
	.uleb128 0x24
	.8byte	.LVL1121
	.4byte	0x35d7
	.uleb128 0x24
	.8byte	.LVL1122
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1124
	.4byte	0x4c8f
	.uleb128 0x24
	.8byte	.LVL1125
	.4byte	0x4c8f
	.byte	0
	.uleb128 0x4c
	.8byte	.LBB295
	.8byte	.LBE295-.LBB295
	.4byte	0x34b0
	.uleb128 0x35
	.4byte	.LASF476
	.byte	0x2
	.2byte	0x7b6
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF475
	.byte	0x2
	.2byte	0x7b7
	.4byte	0x2f3
	.uleb128 0x24
	.8byte	.LVL1132
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1133
	.4byte	0x4c8f
	.uleb128 0x24
	.8byte	.LVL1134
	.4byte	0x3fd5
	.byte	0
	.uleb128 0x24
	.8byte	.LVL1048
	.4byte	0x4be4
	.uleb128 0x24
	.8byte	.LVL1050
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1054
	.4byte	0x4d18
	.uleb128 0x24
	.8byte	.LVL1059
	.4byte	0x36b2
	.uleb128 0x24
	.8byte	.LVL1063
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1066
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1069
	.4byte	0x4c8f
	.uleb128 0x24
	.8byte	.LVL1082
	.4byte	0x3cf5
	.uleb128 0x24
	.8byte	.LVL1086
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1091
	.4byte	0x4d18
	.uleb128 0x24
	.8byte	.LVL1096
	.4byte	0x266c
	.uleb128 0x24
	.8byte	.LVL1099
	.4byte	0x4c8f
	.uleb128 0x24
	.8byte	.LVL1100
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1103
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1104
	.4byte	0x4c8f
	.uleb128 0x24
	.8byte	.LVL1109
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL1113
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1128
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1129
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL1136
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1140
	.4byte	0x266c
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x35d2
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x15
	.byte	0
	.uleb128 0x5
	.4byte	0x35c2
	.uleb128 0x49
	.4byte	.LASF477
	.byte	0x2
	.2byte	0x68c
	.8byte	.LFB219
	.8byte	.LFE219-.LFB219
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x36b2
	.uleb128 0x46
	.string	"lpa"
	.byte	0x2
	.2byte	0x68c
	.4byte	0x2f3
	.4byte	.LLST158
	.uleb128 0x3c
	.4byte	.LASF443
	.byte	0x2
	.2byte	0x68c
	.4byte	0x2f3
	.4byte	.LLST159
	.uleb128 0x3e
	.4byte	.LASF409
	.byte	0x2
	.2byte	0x68e
	.4byte	0x2e8
	.4byte	.LLST160
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2742
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4170
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1b0
	.4byte	0x3697
	.uleb128 0x3d
	.string	"i"
	.byte	0x2
	.2byte	0x692
	.4byte	0x2e8
	.4byte	.LLST161
	.uleb128 0x3e
	.4byte	.LASF389
	.byte	0x2
	.2byte	0x693
	.4byte	0x2e8
	.4byte	.LLST162
	.uleb128 0x3e
	.4byte	.LASF90
	.byte	0x2
	.2byte	0x694
	.4byte	0x2e8
	.4byte	.LLST163
	.uleb128 0x24
	.8byte	.LVL686
	.4byte	0x481c
	.uleb128 0x24
	.8byte	.LVL687
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL688
	.4byte	0x499e
	.byte	0
	.uleb128 0x24
	.8byte	.LVL679
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL692
	.4byte	0x3fd5
	.byte	0
	.uleb128 0x49
	.4byte	.LASF478
	.byte	0x2
	.2byte	0x678
	.8byte	.LFB218
	.8byte	.LFE218-.LFB218
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x370b
	.uleb128 0x4d
	.4byte	.LASF393
	.byte	0x2
	.2byte	0x678
	.4byte	0x1423
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x3c
	.4byte	.LASF167
	.byte	0x2
	.2byte	0x678
	.4byte	0x2e8
	.4byte	.LLST51
	.uleb128 0x3c
	.4byte	.LASF169
	.byte	0x2
	.2byte	0x678
	.4byte	0x2e8
	.4byte	.LLST52
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x2
	.2byte	0x67a
	.4byte	0x2e8
	.byte	0
	.uleb128 0x32
	.4byte	.LASF479
	.byte	0x2
	.2byte	0x66f
	.4byte	0x2f
	.8byte	.LFB217
	.8byte	.LFE217-.LFB217
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3748
	.uleb128 0x24
	.8byte	.LVL809
	.4byte	0x3748
	.uleb128 0x24
	.8byte	.LVL810
	.4byte	0x37c1
	.byte	0
	.uleb128 0x49
	.4byte	.LASF480
	.byte	0x2
	.2byte	0x658
	.8byte	.LFB216
	.8byte	.LFE216-.LFB216
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3791
	.uleb128 0x3e
	.4byte	.LASF116
	.byte	0x2
	.2byte	0x65a
	.4byte	0x2e8
	.4byte	.LLST183
	.uleb128 0x24
	.8byte	.LVL802
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL803
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x32
	.4byte	.LASF481
	.byte	0x2
	.2byte	0x648
	.4byte	0x2f
	.8byte	.LFB215
	.8byte	.LFE215-.LFB215
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x37c1
	.uleb128 0x24
	.8byte	.LVL801
	.4byte	0x37c1
	.byte	0
	.uleb128 0x32
	.4byte	.LASF482
	.byte	0x2
	.2byte	0x5ea
	.4byte	0x2f
	.8byte	.LFB214
	.8byte	.LFE214-.LFB214
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3901
	.uleb128 0x3c
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x5ea
	.4byte	0x3901
	.4byte	.LLST174
	.uleb128 0x3e
	.4byte	.LASF484
	.byte	0x2
	.2byte	0x5ec
	.4byte	0x309
	.4byte	.LLST175
	.uleb128 0x35
	.4byte	.LASF82
	.byte	0x2
	.2byte	0x5ec
	.4byte	0x309
	.uleb128 0x3e
	.4byte	.LASF485
	.byte	0x2
	.2byte	0x5ed
	.4byte	0x309
	.4byte	.LLST176
	.uleb128 0x3e
	.4byte	.LASF116
	.byte	0x2
	.2byte	0x5ee
	.4byte	0x2e8
	.4byte	.LLST177
	.uleb128 0x3e
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x5ef
	.4byte	0x520
	.4byte	.LLST178
	.uleb128 0x3e
	.4byte	.LASF95
	.byte	0x2
	.2byte	0x5f0
	.4byte	0x617
	.4byte	.LLST179
	.uleb128 0x3e
	.4byte	.LASF96
	.byte	0x2
	.2byte	0x5f1
	.4byte	0x520
	.4byte	.LLST180
	.uleb128 0x3e
	.4byte	.LASF92
	.byte	0x2
	.2byte	0x5f2
	.4byte	0x2e8
	.4byte	.LLST181
	.uleb128 0x3e
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x5f3
	.4byte	0x2e8
	.4byte	.LLST182
	.uleb128 0x35
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x5f4
	.4byte	0x3907
	.uleb128 0x24
	.8byte	.LVL761
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL769
	.4byte	0x4be4
	.uleb128 0x24
	.8byte	.LVL775
	.4byte	0x44fe
	.uleb128 0x24
	.8byte	.LVL776
	.4byte	0x44a6
	.uleb128 0x24
	.8byte	.LVL777
	.4byte	0x43ad
	.uleb128 0x24
	.8byte	.LVL778
	.4byte	0x43ad
	.uleb128 0x24
	.8byte	.LVL786
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL790
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL793
	.4byte	0x1d0d
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x56e
	.uleb128 0x7
	.byte	0x8
	.4byte	0x79f
	.uleb128 0x32
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x580
	.4byte	0x2f
	.8byte	.LFB213
	.8byte	.LFE213-.LFB213
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x39fc
	.uleb128 0x3e
	.4byte	.LASF485
	.byte	0x2
	.2byte	0x582
	.4byte	0x309
	.4byte	.LLST330
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x1d08
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4107
	.uleb128 0x24
	.8byte	.LVL1645
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1648
	.4byte	0x4be4
	.uleb128 0x24
	.8byte	.LVL1650
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1651
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1652
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1653
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1654
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1655
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1656
	.4byte	0x30ad
	.uleb128 0x24
	.8byte	.LVL1657
	.4byte	0x30ad
	.uleb128 0x24
	.8byte	.LVL1658
	.4byte	0x30ad
	.uleb128 0x24
	.8byte	.LVL1659
	.4byte	0x30ad
	.uleb128 0x24
	.8byte	.LVL1660
	.4byte	0x1d0d
	.byte	0
	.uleb128 0x32
	.4byte	.LASF490
	.byte	0x2
	.2byte	0x57b
	.4byte	0x2f
	.8byte	.LFB212
	.8byte	.LFE212-.LFB212
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3a2f
	.uleb128 0x3c
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x57b
	.4byte	0x2e8
	.4byte	.LLST50
	.byte	0
	.uleb128 0x4b
	.4byte	.LASF493
	.byte	0x2
	.2byte	0x573
	.byte	0x1
	.4byte	0x3a55
	.uleb128 0x34
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x573
	.4byte	0x2e8
	.uleb128 0x34
	.4byte	.LASF369
	.byte	0x2
	.2byte	0x573
	.4byte	0x2f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF495
	.byte	0x2
	.2byte	0x566
	.4byte	0x2f
	.byte	0x1
	.uleb128 0x32
	.4byte	.LASF496
	.byte	0x2
	.2byte	0x477
	.4byte	0x2f
	.8byte	.LFB209
	.8byte	.LFE209-.LFB209
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3baa
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x2
	.2byte	0x479
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF497
	.byte	0x2
	.2byte	0x479
	.4byte	0x2e8
	.4byte	.LLST171
	.uleb128 0x35
	.4byte	.LASF175
	.byte	0x2
	.2byte	0x47a
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF352
	.byte	0x2
	.2byte	0x47a
	.4byte	0x2e8
	.4byte	.LLST172
	.uleb128 0x36
	.string	"i"
	.byte	0x2
	.2byte	0x47b
	.4byte	0x309
	.uleb128 0x36
	.string	"j"
	.byte	0x2
	.2byte	0x47b
	.4byte	0x309
	.uleb128 0x3e
	.4byte	.LASF498
	.byte	0x2
	.2byte	0x47c
	.4byte	0x3907
	.4byte	.LLST173
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2e08
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4050
	.uleb128 0x24
	.8byte	.LVL729
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL730
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL731
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL732
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL733
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL735
	.4byte	0x4d54
	.uleb128 0x24
	.8byte	.LVL736
	.4byte	0x5265
	.uleb128 0x24
	.8byte	.LVL742
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL743
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL744
	.4byte	0x4b4a
	.uleb128 0x24
	.8byte	.LVL745
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL746
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL747
	.4byte	0x4b4a
	.uleb128 0x24
	.8byte	.LVL749
	.4byte	0x75a7
	.byte	0
	.uleb128 0x32
	.4byte	.LASF499
	.byte	0x2
	.2byte	0x41c
	.4byte	0x2f
	.8byte	.LFB208
	.8byte	.LFE208-.LFB208
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3ca3
	.uleb128 0x3e
	.4byte	.LASF83
	.byte	0x2
	.2byte	0x41e
	.4byte	0x2e8
	.4byte	.LLST263
	.uleb128 0x35
	.4byte	.LASF500
	.byte	0x2
	.2byte	0x41f
	.4byte	0x3ca3
	.uleb128 0x3e
	.4byte	.LASF501
	.byte	0x2
	.2byte	0x420
	.4byte	0x2e8
	.4byte	.LLST264
	.uleb128 0x37
	.4byte	.LASF502
	.byte	0x2
	.2byte	0x44d
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x1d08
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.4028
	.uleb128 0x24
	.8byte	.LVL1264
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1265
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1266
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1267
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1268
	.4byte	0x3d7b
	.uleb128 0x24
	.8byte	.LVL1269
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL1270
	.4byte	0x49e8
	.uleb128 0x24
	.8byte	.LVL1271
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL1272
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1275
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1276
	.4byte	0x4b4a
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x7f7
	.uleb128 0x4b
	.4byte	.LASF503
	.byte	0x2
	.2byte	0x3e3
	.byte	0x1
	.4byte	0x3cf5
	.uleb128 0x39
	.uleb128 0x36
	.string	"lpa"
	.byte	0x2
	.2byte	0x3e8
	.4byte	0x2f3
	.uleb128 0x36
	.string	"ppa"
	.byte	0x2
	.2byte	0x3e9
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF504
	.byte	0x2
	.2byte	0x3ea
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF447
	.byte	0x2
	.2byte	0x3eb
	.4byte	0x314
	.uleb128 0x35
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x3ec
	.4byte	0x1f4b
	.byte	0
	.byte	0
	.uleb128 0x49
	.4byte	.LASF505
	.byte	0x2
	.2byte	0x3b1
	.8byte	.LFB206
	.8byte	.LFE206-.LFB206
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3d7b
	.uleb128 0x3c
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x3b1
	.4byte	0x3901
	.4byte	.LLST167
	.uleb128 0x3d
	.string	"ppa"
	.byte	0x2
	.2byte	0x3b3
	.4byte	0x2f3
	.4byte	.LLST168
	.uleb128 0x3e
	.4byte	.LASF102
	.byte	0x2
	.2byte	0x3b4
	.4byte	0x2e8
	.4byte	.LLST169
	.uleb128 0x3e
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x3b5
	.4byte	0x520
	.4byte	.LLST170
	.uleb128 0x24
	.8byte	.LVL723
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL724
	.4byte	0x41c4
	.uleb128 0x24
	.8byte	.LVL727
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x4b
	.4byte	.LASF506
	.byte	0x2
	.2byte	0x3a2
	.byte	0x1
	.4byte	0x3d9f
	.uleb128 0x34
	.4byte	.LASF180
	.byte	0x2
	.2byte	0x3a2
	.4byte	0x2f3
	.uleb128 0x36
	.string	"i"
	.byte	0x2
	.2byte	0x3a4
	.4byte	0x2e8
	.byte	0
	.uleb128 0x32
	.4byte	.LASF507
	.byte	0x2
	.2byte	0x38d
	.4byte	0x2f
	.8byte	.LFB204
	.8byte	.LFE204-.LFB204
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3dfc
	.uleb128 0x3c
	.4byte	.LASF508
	.byte	0x2
	.2byte	0x38d
	.4byte	0x2f3
	.4byte	.LLST255
	.uleb128 0x3e
	.4byte	.LASF509
	.byte	0x2
	.2byte	0x38f
	.4byte	0x2f3
	.4byte	.LLST256
	.uleb128 0x24
	.8byte	.LVL1232
	.4byte	0x3ee5
	.uleb128 0x24
	.8byte	.LVL1233
	.4byte	0x2fb7
	.byte	0
	.uleb128 0x32
	.4byte	.LASF510
	.byte	0x2
	.2byte	0x367
	.4byte	0x2f3
	.8byte	.LFB203
	.8byte	.LFE203-.LFB203
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3edf
	.uleb128 0x3c
	.4byte	.LASF511
	.byte	0x2
	.2byte	0x367
	.4byte	0x2f3
	.4byte	.LLST257
	.uleb128 0x3c
	.4byte	.LASF512
	.byte	0x2
	.2byte	0x367
	.4byte	0x2f3
	.4byte	.LLST258
	.uleb128 0x3c
	.4byte	.LASF513
	.byte	0x2
	.2byte	0x367
	.4byte	0x3edf
	.4byte	.LLST259
	.uleb128 0x3d
	.string	"len"
	.byte	0x2
	.2byte	0x369
	.4byte	0x2e8
	.4byte	.LLST260
	.uleb128 0x35
	.4byte	.LASF514
	.byte	0x2
	.2byte	0x369
	.4byte	0x2e8
	.uleb128 0x3d
	.string	"ppn"
	.byte	0x2
	.2byte	0x36a
	.4byte	0x2f3
	.4byte	.LLST261
	.uleb128 0x1f
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x36b
	.4byte	0x2f3
	.uleb128 0x1
	.byte	0x66
	.uleb128 0x4a
	.string	"req"
	.byte	0x2
	.2byte	0x36c
	.4byte	0x314
	.uleb128 0x3
	.byte	0x91
	.sleb128 -96
	.uleb128 0x3e
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x36d
	.4byte	0x2f3
	.4byte	.LLST262
	.uleb128 0x1f
	.4byte	.LASF348
	.byte	0x2
	.2byte	0x36e
	.4byte	0x1b29
	.uleb128 0x2
	.byte	0x91
	.sleb128 -64
	.uleb128 0x24
	.8byte	.LVL1250
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1251
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1255
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x2dd
	.uleb128 0x32
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x342
	.4byte	0x2f3
	.8byte	.LFB202
	.8byte	.LFE202-.LFB202
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3fd5
	.uleb128 0x3c
	.4byte	.LASF511
	.byte	0x2
	.2byte	0x342
	.4byte	0x2f3
	.4byte	.LLST249
	.uleb128 0x3c
	.4byte	.LASF512
	.byte	0x2
	.2byte	0x342
	.4byte	0x2f3
	.4byte	.LLST250
	.uleb128 0x3c
	.4byte	.LASF513
	.byte	0x2
	.2byte	0x342
	.4byte	0x3edf
	.4byte	.LLST251
	.uleb128 0x3d
	.string	"len"
	.byte	0x2
	.2byte	0x344
	.4byte	0x2e8
	.4byte	.LLST252
	.uleb128 0x35
	.4byte	.LASF514
	.byte	0x2
	.2byte	0x344
	.4byte	0x2e8
	.uleb128 0x3d
	.string	"ppn"
	.byte	0x2
	.2byte	0x345
	.4byte	0x2f3
	.4byte	.LLST253
	.uleb128 0x1f
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x346
	.4byte	0x2f3
	.uleb128 0x1
	.byte	0x69
	.uleb128 0x4a
	.string	"req"
	.byte	0x2
	.2byte	0x347
	.4byte	0x314
	.uleb128 0x3
	.byte	0x91
	.sleb128 -96
	.uleb128 0x3e
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x348
	.4byte	0x2f3
	.4byte	.LLST254
	.uleb128 0x1f
	.4byte	.LASF348
	.byte	0x2
	.2byte	0x349
	.4byte	0x1b29
	.uleb128 0x2
	.byte	0x91
	.sleb128 -64
	.uleb128 0x24
	.8byte	.LVL1217
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1218
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1219
	.4byte	0x41c4
	.uleb128 0x24
	.8byte	.LVL1222
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x32
	.4byte	.LASF518
	.byte	0x2
	.2byte	0x321
	.4byte	0x2f
	.8byte	.LFB201
	.8byte	.LFE201-.LFB201
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x40af
	.uleb128 0x46
	.string	"lpn"
	.byte	0x2
	.2byte	0x321
	.4byte	0x2f3
	.4byte	.LLST152
	.uleb128 0x46
	.string	"ppn"
	.byte	0x2
	.2byte	0x321
	.4byte	0x520
	.4byte	.LLST153
	.uleb128 0x3c
	.4byte	.LASF519
	.byte	0x2
	.2byte	0x321
	.4byte	0x2f
	.4byte	.LLST154
	.uleb128 0x3e
	.4byte	.LASF520
	.byte	0x2
	.2byte	0x323
	.4byte	0x2e8
	.4byte	.LLST155
	.uleb128 0x3e
	.4byte	.LASF521
	.byte	0x2
	.2byte	0x323
	.4byte	0x2e8
	.4byte	.LLST156
	.uleb128 0x3e
	.4byte	.LASF522
	.byte	0x2
	.2byte	0x323
	.4byte	0x2e8
	.4byte	.LLST157
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x40bf
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3955
	.uleb128 0x41
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x32c
	.8byte	.L647
	.uleb128 0x24
	.8byte	.LVL661
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL665
	.4byte	0x410f
	.uleb128 0x24
	.8byte	.LVL668
	.4byte	0x4178
	.uleb128 0x24
	.8byte	.LVL669
	.4byte	0x45c7
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x40bf
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x8
	.byte	0
	.uleb128 0x5
	.4byte	0x40af
	.uleb128 0x32
	.4byte	.LASF524
	.byte	0x2
	.2byte	0x314
	.4byte	0x2f
	.8byte	.LFB200
	.8byte	.LFE200-.LFB200
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x410f
	.uleb128 0x3d
	.string	"i"
	.byte	0x2
	.2byte	0x316
	.4byte	0x2e8
	.4byte	.LLST225
	.uleb128 0x24
	.8byte	.LVL1028
	.4byte	0x3ca9
	.uleb128 0x24
	.8byte	.LVL1031
	.4byte	0x4178
	.byte	0
	.uleb128 0x32
	.4byte	.LASF525
	.byte	0x2
	.2byte	0x2ec
	.4byte	0x2e8
	.8byte	.LFB199
	.8byte	.LFE199-.LFB199
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4178
	.uleb128 0x36
	.string	"i"
	.byte	0x2
	.2byte	0x2ee
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF526
	.byte	0x2
	.2byte	0x2ef
	.4byte	0x2e8
	.4byte	.LLST46
	.uleb128 0x35
	.4byte	.LASF527
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x2f3
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x35d2
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3938
	.uleb128 0x24
	.8byte	.LVL147
	.4byte	0x75a7
	.byte	0
	.uleb128 0x32
	.4byte	.LASF528
	.byte	0x2
	.2byte	0x2e2
	.4byte	0x2f
	.8byte	.LFB198
	.8byte	.LFE198-.LFB198
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x41c4
	.uleb128 0x3c
	.4byte	.LASF520
	.byte	0x2
	.2byte	0x2e2
	.4byte	0x2e8
	.4byte	.LLST151
	.uleb128 0x35
	.4byte	.LASF102
	.byte	0x2
	.2byte	0x2e4
	.4byte	0x2f3
	.uleb128 0x24
	.8byte	.LVL655
	.4byte	0x41c4
	.byte	0
	.uleb128 0x32
	.4byte	.LASF529
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x2f
	.8byte	.LFB197
	.8byte	.LFE197-.LFB197
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x42b9
	.uleb128 0x3c
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x3901
	.4byte	.LLST141
	.uleb128 0x3c
	.4byte	.LASF102
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x2f3
	.4byte	.LLST142
	.uleb128 0x3c
	.4byte	.LASF513
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x520
	.4byte	.LLST143
	.uleb128 0x35
	.4byte	.LASF175
	.byte	0x2
	.2byte	0x2a9
	.4byte	0x2f3
	.uleb128 0x3e
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x2aa
	.4byte	0x3907
	.4byte	.LLST144
	.uleb128 0x3e
	.4byte	.LASF501
	.byte	0x2
	.2byte	0x2ab
	.4byte	0x2e8
	.4byte	.LLST145
	.uleb128 0x37
	.4byte	.LASF530
	.byte	0x2
	.2byte	0x2ad
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2742
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3913
	.uleb128 0x24
	.8byte	.LVL619
	.4byte	0x42b9
	.uleb128 0x24
	.8byte	.LVL620
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL621
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL622
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL625
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL628
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL629
	.4byte	0x75a7
	.byte	0
	.uleb128 0x32
	.4byte	.LASF531
	.byte	0x2
	.2byte	0x277
	.4byte	0x2f
	.8byte	.LFB196
	.8byte	.LFE196-.LFB196
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4398
	.uleb128 0x3c
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x277
	.4byte	0x3901
	.4byte	.LLST136
	.uleb128 0x35
	.4byte	.LASF175
	.byte	0x2
	.2byte	0x279
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF116
	.byte	0x2
	.2byte	0x279
	.4byte	0x2e8
	.4byte	.LLST137
	.uleb128 0x3e
	.4byte	.LASF532
	.byte	0x2
	.2byte	0x27a
	.4byte	0x2e8
	.4byte	.LLST138
	.uleb128 0x3e
	.4byte	.LASF533
	.byte	0x2
	.2byte	0x27b
	.4byte	0x617
	.4byte	.LLST139
	.uleb128 0x3e
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x27c
	.4byte	0x520
	.4byte	.LLST140
	.uleb128 0x35
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x27d
	.4byte	0x3907
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x43a8
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3899
	.uleb128 0x24
	.8byte	.LVL597
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL598
	.4byte	0x49e8
	.uleb128 0x24
	.8byte	.LVL605
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL610
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL611
	.4byte	0x43ad
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x43a8
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x1e
	.byte	0
	.uleb128 0x5
	.4byte	0x4398
	.uleb128 0x32
	.4byte	.LASF534
	.byte	0x2
	.2byte	0x249
	.4byte	0x2f
	.8byte	.LFB195
	.8byte	.LFE195-.LFB195
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x44a6
	.uleb128 0x3c
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x249
	.4byte	0x3901
	.4byte	.LLST146
	.uleb128 0x3e
	.4byte	.LASF535
	.byte	0x2
	.2byte	0x24b
	.4byte	0x2e8
	.4byte	.LLST147
	.uleb128 0x35
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x24c
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF116
	.byte	0x2
	.2byte	0x24d
	.4byte	0x2e8
	.4byte	.LLST148
	.uleb128 0x3e
	.4byte	.LASF533
	.byte	0x2
	.2byte	0x24e
	.4byte	0x617
	.4byte	.LLST149
	.uleb128 0x3e
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x24f
	.4byte	0x520
	.4byte	.LLST150
	.uleb128 0x35
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x250
	.4byte	0x3907
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x1d08
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3885
	.uleb128 0x24
	.8byte	.LVL637
	.4byte	0x44fe
	.uleb128 0x24
	.8byte	.LVL639
	.4byte	0x44a6
	.uleb128 0x24
	.8byte	.LVL641
	.4byte	0x4b4a
	.uleb128 0x24
	.8byte	.LVL643
	.4byte	0x44a6
	.uleb128 0x24
	.8byte	.LVL648
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL649
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL652
	.4byte	0x41c4
	.byte	0
	.uleb128 0x33
	.4byte	.LASF537
	.byte	0x2
	.2byte	0x231
	.4byte	0x2f
	.byte	0x1
	.4byte	0x44e9
	.uleb128 0x34
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x231
	.4byte	0x3901
	.uleb128 0x35
	.4byte	.LASF538
	.byte	0x2
	.2byte	0x233
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF533
	.byte	0x2
	.2byte	0x234
	.4byte	0x617
	.uleb128 0x38
	.4byte	.LASF357
	.4byte	0x44f9
	.4byte	.LASF537
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x44f9
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x19
	.byte	0
	.uleb128 0x5
	.4byte	0x44e9
	.uleb128 0x32
	.4byte	.LASF539
	.byte	0x2
	.2byte	0x209
	.4byte	0x2f
	.8byte	.LFB193
	.8byte	.LFE193-.LFB193
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x45c7
	.uleb128 0x3c
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x209
	.4byte	0x3901
	.4byte	.LLST128
	.uleb128 0x3e
	.4byte	.LASF533
	.byte	0x2
	.2byte	0x20b
	.4byte	0x617
	.4byte	.LLST129
	.uleb128 0x3e
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x20c
	.4byte	0x520
	.4byte	.LLST130
	.uleb128 0x3e
	.4byte	.LASF540
	.byte	0x2
	.2byte	0x20d
	.4byte	0x617
	.4byte	.LLST131
	.uleb128 0x3e
	.4byte	.LASF175
	.byte	0x2
	.2byte	0x20e
	.4byte	0x2e8
	.4byte	.LLST132
	.uleb128 0x3e
	.4byte	.LASF116
	.byte	0x2
	.2byte	0x20e
	.4byte	0x2e8
	.4byte	.LLST133
	.uleb128 0x35
	.4byte	.LASF538
	.byte	0x2
	.2byte	0x20f
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF541
	.byte	0x2
	.2byte	0x210
	.4byte	0x2e8
	.4byte	.LLST134
	.uleb128 0x3e
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x211
	.4byte	0x2e8
	.4byte	.LLST135
	.uleb128 0x24
	.8byte	.LVL576
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL592
	.4byte	0x4b4a
	.byte	0
	.uleb128 0x32
	.4byte	.LASF542
	.byte	0x2
	.2byte	0x1e7
	.4byte	0x2f
	.8byte	.LFB192
	.8byte	.LFE192-.LFB192
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4698
	.uleb128 0x3c
	.4byte	.LASF102
	.byte	0x2
	.2byte	0x1e7
	.4byte	0x2e8
	.4byte	.LLST124
	.uleb128 0x3c
	.4byte	.LASF520
	.byte	0x2
	.2byte	0x1e7
	.4byte	0x2e8
	.4byte	.LLST125
	.uleb128 0x3e
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x1e9
	.4byte	0x3907
	.4byte	.LLST126
	.uleb128 0x3e
	.4byte	.LASF543
	.byte	0x2
	.2byte	0x1ea
	.4byte	0x2f3
	.4byte	.LLST127
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2742
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3841
	.uleb128 0x24
	.8byte	.LVL554
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL559
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL566
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL568
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL569
	.4byte	0x75b2
	.uleb128 0x24
	.8byte	.LVL570
	.4byte	0x75b2
	.uleb128 0x24
	.8byte	.LVL571
	.4byte	0x75a7
	.byte	0
	.uleb128 0x33
	.4byte	.LASF544
	.byte	0x2
	.2byte	0x1bf
	.4byte	0x2f
	.byte	0x1
	.4byte	0x4717
	.uleb128 0x34
	.4byte	.LASF429
	.byte	0x2
	.2byte	0x1bf
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF545
	.byte	0x2
	.2byte	0x1c1
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF546
	.byte	0x2
	.2byte	0x1c2
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF547
	.byte	0x2
	.2byte	0x1c3
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF548
	.byte	0x2
	.2byte	0x1c5
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF549
	.byte	0x2
	.2byte	0x1c6
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x1c7
	.4byte	0x134b
	.uleb128 0x35
	.4byte	.LASF550
	.byte	0x2
	.2byte	0x1c7
	.4byte	0x134b
	.uleb128 0x38
	.4byte	.LASF357
	.4byte	0x35d2
	.4byte	.LASF544
	.byte	0
	.uleb128 0x32
	.4byte	.LASF551
	.byte	0x2
	.2byte	0x1ab
	.4byte	0x2f
	.8byte	.LFB190
	.8byte	.LFE190-.LFB190
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4766
	.uleb128 0x3c
	.4byte	.LASF90
	.byte	0x2
	.2byte	0x1ab
	.4byte	0x2e8
	.4byte	.LLST38
	.uleb128 0x35
	.4byte	.LASF429
	.byte	0x2
	.2byte	0x1ad
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x1ae
	.4byte	0x134b
	.4byte	.LLST39
	.byte	0
	.uleb128 0x32
	.4byte	.LASF552
	.byte	0x2
	.2byte	0x1a6
	.4byte	0x2f
	.8byte	.LFB189
	.8byte	.LFE189-.LFB189
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x47a6
	.uleb128 0x3c
	.4byte	.LASF553
	.byte	0x2
	.2byte	0x1a6
	.4byte	0x47a6
	.4byte	.LLST37
	.uleb128 0x24
	.8byte	.LVL119
	.4byte	0x47ac
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x134b
	.uleb128 0x32
	.4byte	.LASF554
	.byte	0x2
	.2byte	0x193
	.4byte	0x2f
	.8byte	.LFB188
	.8byte	.LFE188-.LFB188
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x481c
	.uleb128 0x3c
	.4byte	.LASF553
	.byte	0x2
	.2byte	0x193
	.4byte	0x47a6
	.4byte	.LLST33
	.uleb128 0x3c
	.4byte	.LASF90
	.byte	0x2
	.2byte	0x193
	.4byte	0x2e8
	.4byte	.LLST34
	.uleb128 0x3e
	.4byte	.LASF429
	.byte	0x2
	.2byte	0x195
	.4byte	0x2e8
	.4byte	.LLST35
	.uleb128 0x3e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x196
	.4byte	0x134b
	.4byte	.LLST36
	.uleb128 0x24
	.8byte	.LVL112
	.4byte	0x481c
	.byte	0
	.uleb128 0x32
	.4byte	.LASF555
	.byte	0x2
	.2byte	0x176
	.4byte	0x2f
	.8byte	.LFB187
	.8byte	.LFE187-.LFB187
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x489b
	.uleb128 0x3c
	.4byte	.LASF553
	.byte	0x2
	.2byte	0x176
	.4byte	0x47a6
	.4byte	.LLST30
	.uleb128 0x3c
	.4byte	.LASF429
	.byte	0x2
	.2byte	0x176
	.4byte	0x2e8
	.4byte	.LLST31
	.uleb128 0x3e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x178
	.4byte	0x134b
	.4byte	.LLST32
	.uleb128 0x35
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x178
	.4byte	0x134b
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x48ab
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3800
	.uleb128 0x24
	.8byte	.LVL103
	.4byte	0x75a7
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x48ab
	.uleb128 0x17
	.4byte	0x3d
	.byte	0x10
	.byte	0
	.uleb128 0x5
	.4byte	0x489b
	.uleb128 0x32
	.4byte	.LASF557
	.byte	0x2
	.2byte	0x145
	.4byte	0x2f
	.8byte	.LFB186
	.8byte	.LFE186-.LFB186
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4929
	.uleb128 0x3c
	.4byte	.LASF429
	.byte	0x2
	.2byte	0x145
	.4byte	0x2e8
	.4byte	.LLST26
	.uleb128 0x3e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x147
	.4byte	0x134b
	.4byte	.LLST27
	.uleb128 0x1f
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x147
	.4byte	0x134b
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x3e
	.4byte	.LASF558
	.byte	0x2
	.2byte	0x148
	.4byte	0x2e8
	.4byte	.LLST28
	.uleb128 0x35
	.4byte	.LASF559
	.byte	0x2
	.2byte	0x149
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF560
	.byte	0x2
	.2byte	0x14a
	.4byte	0x2e8
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF561
	.byte	0x2
	.byte	0xf8
	.4byte	0x2f
	.byte	0x1
	.4byte	0x499e
	.uleb128 0x2b
	.4byte	.LASF429
	.byte	0x2
	.byte	0xf8
	.4byte	0x2e8
	.uleb128 0x2c
	.4byte	.LASF432
	.byte	0x2
	.byte	0xfa
	.4byte	0x134b
	.uleb128 0x2c
	.4byte	.LASF556
	.byte	0x2
	.byte	0xfa
	.4byte	0x134b
	.uleb128 0x2c
	.4byte	.LASF562
	.byte	0x2
	.byte	0xfb
	.4byte	0x2e8
	.uleb128 0x2c
	.4byte	.LASF563
	.byte	0x2
	.byte	0xfc
	.4byte	0x2e8
	.uleb128 0x2c
	.4byte	.LASF547
	.byte	0x2
	.byte	0xfd
	.4byte	0x2f3
	.uleb128 0x2c
	.4byte	.LASF564
	.byte	0x2
	.byte	0xfe
	.4byte	0x2f3
	.uleb128 0x2c
	.4byte	.LASF560
	.byte	0x2
	.byte	0xff
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF111
	.byte	0x2
	.2byte	0x100
	.4byte	0x2e8
	.byte	0
	.uleb128 0x4e
	.4byte	.LASF565
	.byte	0x2
	.byte	0xd9
	.byte	0x1
	.4byte	0x49c3
	.uleb128 0x2b
	.4byte	.LASF429
	.byte	0x2
	.byte	0xd9
	.4byte	0x2e8
	.uleb128 0x38
	.4byte	.LASF357
	.4byte	0x48ab
	.4byte	.LASF565
	.byte	0
	.uleb128 0x4e
	.4byte	.LASF566
	.byte	0x2
	.byte	0xd2
	.byte	0x1
	.4byte	0x49e8
	.uleb128 0x2b
	.4byte	.LASF429
	.byte	0x2
	.byte	0xd2
	.4byte	0x2e8
	.uleb128 0x38
	.4byte	.LASF357
	.4byte	0x48ab
	.4byte	.LASF566
	.byte	0
	.uleb128 0x22
	.4byte	.LASF567
	.byte	0x2
	.byte	0xb4
	.4byte	0x2e8
	.8byte	.LFB182
	.8byte	.LFE182-.LFB182
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4a91
	.uleb128 0x43
	.string	"tmp"
	.byte	0x2
	.byte	0xb6
	.4byte	0x2e8
	.4byte	.LLST113
	.uleb128 0x29
	.string	"Q"
	.byte	0x2
	.byte	0xb7
	.4byte	0x4a91
	.uleb128 0x4f
	.4byte	.LASF568
	.byte	0x2
	.byte	0xb9
	.8byte	.L445
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x180
	.4byte	0x4a63
	.uleb128 0x28
	.4byte	.LASF569
	.byte	0x2
	.byte	0xbf
	.4byte	0x2e8
	.4byte	.LLST114
	.uleb128 0x24
	.8byte	.LVL481
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL482
	.4byte	0x1b39
	.byte	0
	.uleb128 0x50
	.4byte	0x4b91
	.8byte	.LBB235
	.4byte	.Ldebug_ranges0+0x140
	.byte	0x2
	.byte	0xba
	.uleb128 0x24
	.8byte	.LVL483
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL484
	.4byte	0x75a7
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0xac3
	.uleb128 0x22
	.4byte	.LASF570
	.byte	0x2
	.byte	0xa2
	.4byte	0x2f
	.8byte	.LFB181
	.8byte	.LFE181-.LFB181
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4afc
	.uleb128 0x26
	.string	"blk"
	.byte	0x2
	.byte	0xa2
	.4byte	0x2e8
	.4byte	.LLST19
	.uleb128 0x51
	.string	"ret"
	.byte	0x2
	.byte	0xa4
	.4byte	0x2f
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x51
	.string	"i"
	.byte	0x2
	.byte	0xa5
	.4byte	0x2f3
	.uleb128 0x1
	.byte	0x53
	.uleb128 0x29
	.string	"Q"
	.byte	0x2
	.byte	0xa6
	.4byte	0x4a91
	.uleb128 0x50
	.4byte	0x4b85
	.8byte	.LBB172
	.4byte	.Ldebug_ranges0+0x30
	.byte	0x2
	.byte	0xa8
	.byte	0
	.uleb128 0x42
	.4byte	.LASF571
	.byte	0x2
	.byte	0x75
	.8byte	.LFB180
	.8byte	.LFE180-.LFB180
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4b4a
	.uleb128 0x20
	.4byte	.LASF111
	.byte	0x2
	.byte	0x77
	.4byte	0x2fe
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x43
	.string	"i"
	.byte	0x2
	.byte	0x78
	.4byte	0x2e8
	.4byte	.LLST18
	.uleb128 0x51
	.string	"blk"
	.byte	0x2
	.byte	0x78
	.4byte	0x2e8
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x29
	.string	"Q"
	.byte	0x2
	.byte	0x79
	.4byte	0x4a91
	.byte	0
	.uleb128 0x4e
	.4byte	.LASF572
	.byte	0x2
	.byte	0x61
	.byte	0x1
	.4byte	0x4b85
	.uleb128 0x31
	.string	"blk"
	.byte	0x2
	.byte	0x61
	.4byte	0x2e8
	.uleb128 0x2b
	.4byte	.LASF573
	.byte	0x2
	.byte	0x61
	.4byte	0x2e8
	.uleb128 0x39
	.uleb128 0x29
	.string	"Q"
	.byte	0x2
	.byte	0x66
	.4byte	0x4a91
	.uleb128 0x39
	.uleb128 0x2c
	.4byte	.LASF569
	.byte	0x2
	.byte	0x68
	.4byte	0x2e8
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x52
	.4byte	.LASF574
	.byte	0x2
	.byte	0x5c
	.4byte	0x2e8
	.byte	0x1
	.uleb128 0x52
	.4byte	.LASF575
	.byte	0x2
	.byte	0x57
	.4byte	0x2e8
	.byte	0x1
	.uleb128 0x22
	.4byte	.LASF576
	.byte	0x2
	.byte	0x4b
	.4byte	0x2e8
	.8byte	.LFB176
	.8byte	.LFE176-.LFB176
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4be4
	.uleb128 0x26
	.string	"max"
	.byte	0x2
	.byte	0x4b
	.4byte	0x2e8
	.4byte	.LLST123
	.uleb128 0x29
	.string	"Q"
	.byte	0x2
	.byte	0x4d
	.4byte	0x4a91
	.uleb128 0x24
	.8byte	.LVL550
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x22
	.4byte	.LASF577
	.byte	0x2
	.byte	0x2e
	.4byte	0x2f
	.8byte	.LFB175
	.8byte	.LFE175-.LFB175
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4c8f
	.uleb128 0x23
	.4byte	.LASF175
	.byte	0x2
	.byte	0x2e
	.4byte	0x2f3
	.4byte	.LLST90
	.uleb128 0x23
	.4byte	.LASF351
	.byte	0x2
	.byte	0x2e
	.4byte	0x2f
	.4byte	.LLST91
	.uleb128 0x51
	.string	"req"
	.byte	0x2
	.byte	0x30
	.4byte	0x314
	.uleb128 0x3
	.byte	0x91
	.sleb128 -96
	.uleb128 0x43
	.string	"low"
	.byte	0x2
	.byte	0x31
	.4byte	0x309
	.4byte	.LLST92
	.uleb128 0x28
	.4byte	.LASF578
	.byte	0x2
	.byte	0x31
	.4byte	0x309
	.4byte	.LLST93
	.uleb128 0x29
	.string	"mid"
	.byte	0x2
	.byte	0x31
	.4byte	0x309
	.uleb128 0x20
	.4byte	.LASF579
	.byte	0x2
	.byte	0x32
	.4byte	0x1b29
	.uleb128 0x2
	.byte	0x91
	.sleb128 -64
	.uleb128 0x2c
	.4byte	.LASF580
	.byte	0x2
	.byte	0x33
	.4byte	0x2e8
	.uleb128 0x24
	.8byte	.LVL386
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL393
	.4byte	0x1d0d
	.byte	0
	.uleb128 0x22
	.4byte	.LASF581
	.byte	0x2
	.byte	0x1e
	.4byte	0x2f3
	.8byte	.LFB174
	.8byte	.LFE174-.LFB174
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4cdc
	.uleb128 0x23
	.4byte	.LASF582
	.byte	0x2
	.byte	0x1e
	.4byte	0x2f3
	.4byte	.LLST16
	.uleb128 0x23
	.4byte	.LASF583
	.byte	0x2
	.byte	0x1e
	.4byte	0x2f3
	.4byte	.LLST17
	.uleb128 0x51
	.string	"ret"
	.byte	0x2
	.byte	0x20
	.4byte	0x2f3
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x22
	.4byte	.LASF584
	.byte	0x2
	.byte	0x14
	.4byte	0x2e8
	.8byte	.LFB173
	.8byte	.LFE173-.LFB173
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4d18
	.uleb128 0x23
	.4byte	.LASF375
	.byte	0x2
	.byte	0x14
	.4byte	0x2e8
	.4byte	.LLST15
	.uleb128 0x2c
	.4byte	.LASF497
	.byte	0x2
	.byte	0x16
	.4byte	0x2e8
	.byte	0
	.uleb128 0x22
	.4byte	.LASF585
	.byte	0x2
	.byte	0xc
	.4byte	0x2e8
	.8byte	.LFB172
	.8byte	.LFE172-.LFB172
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4d54
	.uleb128 0x23
	.4byte	.LASF375
	.byte	0x2
	.byte	0xc
	.4byte	0x2e8
	.4byte	.LLST14
	.uleb128 0x2c
	.4byte	.LASF373
	.byte	0x2
	.byte	0xe
	.4byte	0x2e8
	.byte	0
	.uleb128 0x22
	.4byte	.LASF586
	.byte	0x2
	.byte	0x4
	.4byte	0x2e8
	.8byte	.LFB171
	.8byte	.LFE171-.LFB171
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4d9d
	.uleb128 0x23
	.4byte	.LASF373
	.byte	0x2
	.byte	0x4
	.4byte	0x2e8
	.4byte	.LLST13
	.uleb128 0x45
	.4byte	.LASF497
	.byte	0x2
	.byte	0x4
	.4byte	0x2e8
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x2c
	.4byte	.LASF389
	.byte	0x2
	.byte	0x6
	.4byte	0x2e8
	.byte	0
	.uleb128 0x32
	.4byte	.LASF587
	.byte	0x4
	.2byte	0x13c
	.4byte	0x2fe
	.8byte	.LFB170
	.8byte	.LFE170-.LFB170
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f2b
	.uleb128 0x3d
	.string	"die"
	.byte	0x4
	.2byte	0x13e
	.4byte	0x2f3
	.4byte	.LLST247
	.uleb128 0x36
	.string	"blk"
	.byte	0x4
	.2byte	0x13f
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF360
	.byte	0x4
	.2byte	0x140
	.4byte	0x2f3
	.4byte	.LLST248
	.uleb128 0x35
	.4byte	.LASF588
	.byte	0x4
	.2byte	0x142
	.4byte	0x4f2b
	.uleb128 0x37
	.4byte	.LASF589
	.byte	0x4
	.2byte	0x158
	.uleb128 0x24
	.8byte	.LVL1174
	.4byte	0x50df
	.uleb128 0x24
	.8byte	.LVL1175
	.4byte	0x4f31
	.uleb128 0x24
	.8byte	.LVL1177
	.4byte	0x5265
	.uleb128 0x24
	.8byte	.LVL1178
	.4byte	0x1ae5
	.uleb128 0x24
	.8byte	.LVL1179
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL1182
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1183
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1184
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL1185
	.4byte	0x1a73
	.uleb128 0x24
	.8byte	.LVL1186
	.4byte	0x51eb
	.uleb128 0x24
	.8byte	.LVL1187
	.4byte	0x5265
	.uleb128 0x24
	.8byte	.LVL1188
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1189
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1192
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1193
	.4byte	0x1b39
	.uleb128 0x24
	.8byte	.LVL1194
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL1195
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL1196
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL1197
	.4byte	0x1b39
	.uleb128 0x24
	.8byte	.LVL1198
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL1199
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL1200
	.4byte	0x510a
	.uleb128 0x24
	.8byte	.LVL1201
	.4byte	0x510a
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x6e7
	.uleb128 0x32
	.4byte	.LASF590
	.byte	0x4
	.2byte	0x116
	.4byte	0x2f
	.8byte	.LFB169
	.8byte	.LFE169-.LFB169
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f9d
	.uleb128 0x3d
	.string	"die"
	.byte	0x4
	.2byte	0x118
	.4byte	0x2f3
	.4byte	.LLST87
	.uleb128 0x3d
	.string	"blk"
	.byte	0x4
	.2byte	0x119
	.4byte	0x2e8
	.4byte	.LLST88
	.uleb128 0x3e
	.4byte	.LASF591
	.byte	0x4
	.2byte	0x11a
	.4byte	0x2f3
	.4byte	.LLST89
	.uleb128 0x35
	.4byte	.LASF588
	.byte	0x4
	.2byte	0x11b
	.4byte	0x4f2b
	.uleb128 0x24
	.8byte	.LVL381
	.4byte	0x1d0d
	.byte	0
	.uleb128 0x22
	.4byte	.LASF592
	.byte	0x4
	.byte	0xac
	.4byte	0x2f
	.8byte	.LFB168
	.8byte	.LFE168-.LFB168
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x507d
	.uleb128 0x43
	.string	"i"
	.byte	0x4
	.byte	0xae
	.4byte	0x2f3
	.4byte	.LLST244
	.uleb128 0x43
	.string	"blk"
	.byte	0x4
	.byte	0xaf
	.4byte	0x2e8
	.4byte	.LLST245
	.uleb128 0x28
	.4byte	.LASF485
	.byte	0x4
	.byte	0xb0
	.4byte	0x309
	.4byte	.LLST246
	.uleb128 0x2c
	.4byte	.LASF588
	.byte	0x4
	.byte	0xb1
	.4byte	0x4f2b
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x508d
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3647
	.uleb128 0x24
	.8byte	.LVL1149
	.4byte	0x50df
	.uleb128 0x24
	.8byte	.LVL1151
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1152
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1153
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1155
	.4byte	0x4be4
	.uleb128 0x24
	.8byte	.LVL1157
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1158
	.4byte	0x622e
	.uleb128 0x24
	.8byte	.LVL1167
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1171
	.4byte	0x19ba
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x508d
	.uleb128 0x17
	.4byte	0x3d
	.byte	0xa
	.byte	0
	.uleb128 0x5
	.4byte	0x507d
	.uleb128 0x22
	.4byte	.LASF593
	.byte	0x4
	.byte	0x9c
	.4byte	0x2e8
	.8byte	.LFB167
	.8byte	.LFE167-.LFB167
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x50df
	.uleb128 0x43
	.string	"blk"
	.byte	0x4
	.byte	0x9e
	.4byte	0x2e8
	.4byte	.LLST11
	.uleb128 0x28
	.4byte	.LASF594
	.byte	0x4
	.byte	0x9f
	.4byte	0x2e8
	.4byte	.LLST12
	.uleb128 0x24
	.8byte	.LVL36
	.4byte	0x5265
	.byte	0
	.uleb128 0x42
	.4byte	.LASF595
	.byte	0x4
	.byte	0x94
	.8byte	.LFB166
	.8byte	.LFE166-.LFB166
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x510a
	.uleb128 0x24
	.8byte	.LVL547
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x22
	.4byte	.LASF596
	.byte	0x4
	.byte	0x4f
	.4byte	0x2f
	.8byte	.LFB165
	.8byte	.LFE165-.LFB165
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x51eb
	.uleb128 0x43
	.string	"i"
	.byte	0x4
	.byte	0x51
	.4byte	0x2f
	.4byte	.LLST206
	.uleb128 0x28
	.4byte	.LASF597
	.byte	0x4
	.byte	0x52
	.4byte	0x2f
	.4byte	.LLST207
	.uleb128 0x28
	.4byte	.LASF588
	.byte	0x4
	.byte	0x53
	.4byte	0x4f2b
	.4byte	.LLST208
	.uleb128 0x28
	.4byte	.LASF501
	.byte	0x4
	.byte	0x54
	.4byte	0x2e8
	.4byte	.LLST209
	.uleb128 0x40
	.4byte	.LASF598
	.byte	0x4
	.byte	0x67
	.uleb128 0x53
	.4byte	.LASF357
	.4byte	0x1d08
	.uleb128 0x24
	.8byte	.LVL916
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL918
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL921
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL922
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL923
	.4byte	0x1b39
	.uleb128 0x24
	.8byte	.LVL924
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL927
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL928
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL930
	.4byte	0x19ba
	.byte	0
	.uleb128 0x42
	.4byte	.LASF599
	.byte	0x4
	.byte	0x41
	.8byte	.LFB164
	.8byte	.LFE164-.LFB164
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x525d
	.uleb128 0x23
	.4byte	.LASF533
	.byte	0x4
	.byte	0x41
	.4byte	0x617
	.4byte	.LLST121
	.uleb128 0x23
	.4byte	.LASF600
	.byte	0x4
	.byte	0x41
	.4byte	0x520
	.4byte	.LLST122
	.uleb128 0x29
	.string	"i"
	.byte	0x4
	.byte	0x43
	.4byte	0x2f3
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x2e08
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3604
	.uleb128 0x24
	.8byte	.LVL544
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL545
	.4byte	0x75a7
	.byte	0
	.uleb128 0x54
	.4byte	.LASF601
	.byte	0x4
	.byte	0x26
	.byte	0x1
	.uleb128 0x22
	.4byte	.LASF602
	.byte	0x4
	.byte	0x17
	.4byte	0x2f
	.8byte	.LFB162
	.8byte	.LFE162-.LFB162
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x52c4
	.uleb128 0x23
	.4byte	.LASF175
	.byte	0x4
	.byte	0x17
	.4byte	0x2e8
	.4byte	.LLST9
	.uleb128 0x29
	.string	"die"
	.byte	0x4
	.byte	0x19
	.4byte	0x2f3
	.uleb128 0x29
	.string	"i"
	.byte	0x4
	.byte	0x1a
	.4byte	0x2f3
	.uleb128 0x2c
	.4byte	.LASF600
	.byte	0x4
	.byte	0x1b
	.4byte	0x520
	.uleb128 0x28
	.4byte	.LASF365
	.byte	0x4
	.byte	0x1c
	.4byte	0x2e8
	.4byte	.LLST10
	.byte	0
	.uleb128 0x22
	.4byte	.LASF603
	.byte	0x4
	.byte	0x4
	.4byte	0x2f
	.8byte	.LFB161
	.8byte	.LFE161-.LFB161
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5327
	.uleb128 0x23
	.4byte	.LASF175
	.byte	0x4
	.byte	0x4
	.4byte	0x2e8
	.4byte	.LLST7
	.uleb128 0x43
	.string	"die"
	.byte	0x4
	.byte	0x6
	.4byte	0x2f3
	.4byte	.LLST8
	.uleb128 0x2c
	.4byte	.LASF600
	.byte	0x4
	.byte	0x7
	.4byte	0x520
	.uleb128 0x2c
	.4byte	.LASF365
	.byte	0x4
	.byte	0x8
	.4byte	0x2e8
	.uleb128 0x24
	.8byte	.LVL29
	.4byte	0x75a7
	.byte	0
	.uleb128 0x55
	.4byte	.LASF664
	.byte	0x3
	.2byte	0x4bd
	.4byte	0x2f3
	.8byte	.LFB160
	.8byte	.LFE160-.LFB160
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x32
	.4byte	.LASF604
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x2f
	.8byte	.LFB159
	.8byte	.LFE159-.LFB159
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x53a5
	.uleb128 0x3c
	.4byte	.LASF90
	.byte	0x3
	.2byte	0x4b8
	.4byte	0xa1
	.4byte	.LLST327
	.uleb128 0x3c
	.4byte	.LASF111
	.byte	0x3
	.2byte	0x4b8
	.4byte	0xa1
	.4byte	.LLST328
	.uleb128 0x46
	.string	"buf"
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x53a5
	.4byte	.LLST329
	.uleb128 0x24
	.8byte	.LVL1644
	.4byte	0x5501
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x81
	.uleb128 0x32
	.4byte	.LASF605
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x2f
	.8byte	.LFB158
	.8byte	.LFE158-.LFB158
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x540b
	.uleb128 0x3c
	.4byte	.LASF90
	.byte	0x3
	.2byte	0x4b3
	.4byte	0xa1
	.4byte	.LLST313
	.uleb128 0x3c
	.4byte	.LASF111
	.byte	0x3
	.2byte	0x4b3
	.4byte	0xa1
	.4byte	.LLST314
	.uleb128 0x46
	.string	"buf"
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x53a5
	.4byte	.LLST315
	.uleb128 0x24
	.8byte	.LVL1565
	.4byte	0x5947
	.byte	0
	.uleb128 0x32
	.4byte	.LASF606
	.byte	0x3
	.2byte	0x484
	.4byte	0x2f
	.8byte	.LFB157
	.8byte	.LFE157-.LFB157
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5501
	.uleb128 0x3c
	.4byte	.LASF511
	.byte	0x3
	.2byte	0x484
	.4byte	0x2f3
	.4byte	.LLST275
	.uleb128 0x3c
	.4byte	.LASF512
	.byte	0x3
	.2byte	0x484
	.4byte	0x2f3
	.4byte	.LLST276
	.uleb128 0x3e
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x486
	.4byte	0x2e8
	.4byte	.LLST277
	.uleb128 0x36
	.string	"tmp"
	.byte	0x3
	.2byte	0x487
	.4byte	0x2e8
	.uleb128 0x3d
	.string	"lpa"
	.byte	0x3
	.2byte	0x488
	.4byte	0x2f3
	.4byte	.LLST278
	.uleb128 0x3e
	.4byte	.LASF443
	.byte	0x3
	.2byte	0x489
	.4byte	0x2f3
	.4byte	.LLST279
	.uleb128 0x3e
	.4byte	.LASF420
	.byte	0x3
	.2byte	0x489
	.4byte	0x2f3
	.4byte	.LLST280
	.uleb128 0x35
	.4byte	.LASF409
	.byte	0x3
	.2byte	0x48a
	.4byte	0x2e8
	.uleb128 0x24
	.8byte	.LVL1340
	.4byte	0x57a6
	.uleb128 0x24
	.8byte	.LVL1346
	.4byte	0x40c4
	.uleb128 0x24
	.8byte	.LVL1347
	.4byte	0x3baa
	.uleb128 0x24
	.8byte	.LVL1348
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1349
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1352
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL1353
	.4byte	0x266c
	.byte	0
	.uleb128 0x32
	.4byte	.LASF607
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x2f
	.8byte	.LFB156
	.8byte	.LFE156-.LFB156
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x57a6
	.uleb128 0x46
	.string	"LUN"
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x2dd
	.4byte	.LLST316
	.uleb128 0x3c
	.4byte	.LASF511
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x2f3
	.4byte	.LLST317
	.uleb128 0x3c
	.4byte	.LASF512
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x2f3
	.4byte	.LLST318
	.uleb128 0x3c
	.4byte	.LASF513
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x3edf
	.4byte	.LLST319
	.uleb128 0x35
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x3c3
	.4byte	0x1f4b
	.uleb128 0x3d
	.string	"lpa"
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x2f3
	.4byte	.LLST320
	.uleb128 0x3e
	.4byte	.LASF608
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x2f3
	.4byte	.LLST321
	.uleb128 0x3e
	.4byte	.LASF609
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x2f3
	.4byte	.LLST322
	.uleb128 0x3e
	.4byte	.LASF443
	.byte	0x3
	.2byte	0x3c5
	.4byte	0x2f3
	.4byte	.LLST323
	.uleb128 0x36
	.string	"ppa"
	.byte	0x3
	.2byte	0x3c5
	.4byte	0x2f3
	.uleb128 0x3d
	.string	"i"
	.byte	0x3
	.2byte	0x3c6
	.4byte	0x2f3
	.4byte	.LLST324
	.uleb128 0x3e
	.4byte	.LASF610
	.byte	0x3
	.2byte	0x3c6
	.4byte	0x2f3
	.4byte	.LLST325
	.uleb128 0x35
	.4byte	.LASF374
	.byte	0x3
	.2byte	0x3c6
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF611
	.byte	0x3
	.2byte	0x3c7
	.4byte	0x2f3
	.uleb128 0x35
	.4byte	.LASF612
	.byte	0x3
	.2byte	0x3c7
	.4byte	0x2f3
	.uleb128 0x3e
	.4byte	.LASF419
	.byte	0x3
	.2byte	0x3c8
	.4byte	0x1423
	.4byte	.LLST326
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x40bf
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3540
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x390
	.4byte	0x5660
	.uleb128 0x1f
	.4byte	.LASF382
	.byte	0x3
	.2byte	0x43d
	.4byte	0x314
	.uleb128 0x2
	.byte	0x91
	.sleb128 -32
	.uleb128 0x24
	.8byte	.LVL1626
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1630
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1631
	.4byte	0x75a7
	.byte	0
	.uleb128 0x24
	.8byte	.LVL1568
	.4byte	0x3ee5
	.uleb128 0x24
	.8byte	.LVL1580
	.4byte	0x57a6
	.uleb128 0x24
	.8byte	.LVL1584
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1585
	.4byte	0x57a6
	.uleb128 0x24
	.8byte	.LVL1586
	.4byte	0x27df
	.uleb128 0x24
	.8byte	.LVL1588
	.4byte	0x27df
	.uleb128 0x24
	.8byte	.LVL1590
	.4byte	0x27df
	.uleb128 0x24
	.8byte	.LVL1591
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1596
	.4byte	0x1e7e
	.uleb128 0x24
	.8byte	.LVL1599
	.4byte	0x4717
	.uleb128 0x24
	.8byte	.LVL1600
	.4byte	0x208d
	.uleb128 0x24
	.8byte	.LVL1601
	.4byte	0x1e7e
	.uleb128 0x24
	.8byte	.LVL1602
	.4byte	0x1e7e
	.uleb128 0x24
	.8byte	.LVL1604
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1607
	.4byte	0x57a6
	.uleb128 0x24
	.8byte	.LVL1610
	.4byte	0x27df
	.uleb128 0x24
	.8byte	.LVL1612
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1613
	.4byte	0x2747
	.uleb128 0x24
	.8byte	.LVL1616
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1621
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1627
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1632
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1633
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1637
	.4byte	0x57d6
	.uleb128 0x24
	.8byte	.LVL1638
	.4byte	0x75a7
	.byte	0
	.uleb128 0x32
	.4byte	.LASF613
	.byte	0x3
	.2byte	0x3b7
	.4byte	0x2f
	.8byte	.LFB155
	.8byte	.LFE155-.LFB155
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x57d6
	.uleb128 0x24
	.8byte	.LVL1327
	.4byte	0x57d6
	.byte	0
	.uleb128 0x49
	.4byte	.LASF614
	.byte	0x3
	.2byte	0x387
	.8byte	.LFB154
	.8byte	.LFE154-.LFB154
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5932
	.uleb128 0x46
	.string	"req"
	.byte	0x3
	.2byte	0x387
	.4byte	0xfdb
	.4byte	.LLST269
	.uleb128 0x3c
	.4byte	.LASF374
	.byte	0x3
	.2byte	0x387
	.4byte	0x2f3
	.4byte	.LLST270
	.uleb128 0x3c
	.4byte	.LASF615
	.byte	0x3
	.2byte	0x387
	.4byte	0x2f3
	.4byte	.LLST271
	.uleb128 0x3c
	.4byte	.LASF419
	.byte	0x3
	.2byte	0x387
	.4byte	0x1423
	.4byte	.LLST272
	.uleb128 0x36
	.string	"i"
	.byte	0x3
	.2byte	0x389
	.4byte	0x2f3
	.uleb128 0x3d
	.string	"ppa"
	.byte	0x3
	.2byte	0x38a
	.4byte	0x2f3
	.4byte	.LLST273
	.uleb128 0x3e
	.4byte	.LASF443
	.byte	0x3
	.2byte	0x38a
	.4byte	0x2f3
	.4byte	.LLST274
	.uleb128 0x35
	.4byte	.LASF402
	.byte	0x3
	.2byte	0x38b
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x38c
	.4byte	0x1f4b
	.uleb128 0x35
	.4byte	.LASF467
	.byte	0x3
	.2byte	0x38d
	.4byte	0x2f3
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x5942
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3512
	.uleb128 0x24
	.8byte	.LVL1311
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL1313
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL1314
	.4byte	0x27df
	.uleb128 0x24
	.8byte	.LVL1315
	.4byte	0x201d
	.uleb128 0x24
	.8byte	.LVL1316
	.4byte	0x2747
	.uleb128 0x24
	.8byte	.LVL1317
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL1318
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1320
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1322
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL1323
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1324
	.4byte	0x266c
	.uleb128 0x24
	.8byte	.LVL1325
	.4byte	0x75a7
	.byte	0
	.uleb128 0xb
	.4byte	0x4b
	.4byte	0x5942
	.uleb128 0x17
	.4byte	0x3d
	.byte	0xc
	.byte	0
	.uleb128 0x5
	.4byte	0x5932
	.uleb128 0x32
	.4byte	.LASF616
	.byte	0x3
	.2byte	0x32d
	.4byte	0x2f
	.8byte	.LFB153
	.8byte	.LFE153-.LFB153
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5b05
	.uleb128 0x46
	.string	"LUN"
	.byte	0x3
	.2byte	0x32d
	.4byte	0x2dd
	.4byte	.LLST299
	.uleb128 0x3c
	.4byte	.LASF511
	.byte	0x3
	.2byte	0x32d
	.4byte	0x2f3
	.4byte	.LLST300
	.uleb128 0x3c
	.4byte	.LASF512
	.byte	0x3
	.2byte	0x32d
	.4byte	0x2f3
	.4byte	.LLST301
	.uleb128 0x3c
	.4byte	.LASF513
	.byte	0x3
	.2byte	0x32d
	.4byte	0x3edf
	.4byte	.LLST302
	.uleb128 0x4a
	.string	"ret"
	.byte	0x3
	.2byte	0x32f
	.4byte	0x2f
	.uleb128 0x1
	.byte	0x65
	.uleb128 0x3e
	.4byte	.LASF608
	.byte	0x3
	.2byte	0x330
	.4byte	0x2f3
	.4byte	.LLST303
	.uleb128 0x3e
	.4byte	.LASF609
	.byte	0x3
	.2byte	0x330
	.4byte	0x2f3
	.4byte	.LLST304
	.uleb128 0x3e
	.4byte	.LASF617
	.byte	0x3
	.2byte	0x330
	.4byte	0x2f3
	.4byte	.LLST305
	.uleb128 0x3d
	.string	"lpa"
	.byte	0x3
	.2byte	0x331
	.4byte	0x2f3
	.4byte	.LLST306
	.uleb128 0x3d
	.string	"ppa"
	.byte	0x3
	.2byte	0x331
	.4byte	0x2f3
	.4byte	.LLST307
	.uleb128 0x36
	.string	"n"
	.byte	0x3
	.2byte	0x333
	.4byte	0x2f3
	.uleb128 0x3e
	.4byte	.LASF374
	.byte	0x3
	.2byte	0x333
	.4byte	0x2f3
	.4byte	.LLST308
	.uleb128 0x3e
	.4byte	.LASF610
	.byte	0x3
	.2byte	0x333
	.4byte	0x2f3
	.4byte	.LLST309
	.uleb128 0x3e
	.4byte	.LASF618
	.byte	0x3
	.2byte	0x334
	.4byte	0x2f3
	.4byte	.LLST310
	.uleb128 0x35
	.4byte	.LASF619
	.byte	0x3
	.2byte	0x334
	.4byte	0x2f3
	.uleb128 0x3e
	.4byte	.LASF620
	.byte	0x3
	.2byte	0x335
	.4byte	0x2f3
	.4byte	.LLST311
	.uleb128 0x3e
	.4byte	.LASF621
	.byte	0x3
	.2byte	0x336
	.4byte	0x2f3
	.4byte	.LLST312
	.uleb128 0x4c
	.8byte	.LBB324
	.8byte	.LBE324-.LBB324
	.4byte	0x5aa9
	.uleb128 0x35
	.4byte	.LASF389
	.byte	0x3
	.2byte	0x37a
	.4byte	0x2e8
	.uleb128 0x24
	.8byte	.LVL1544
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL1545
	.4byte	0x208d
	.byte	0
	.uleb128 0x24
	.8byte	.LVL1515
	.4byte	0x3dfc
	.uleb128 0x24
	.8byte	.LVL1524
	.4byte	0x57a6
	.uleb128 0x24
	.8byte	.LVL1531
	.4byte	0x1e7e
	.uleb128 0x24
	.8byte	.LVL1533
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1537
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1543
	.4byte	0x19ba
	.uleb128 0x24
	.8byte	.LVL1550
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x32
	.4byte	.LASF622
	.byte	0x3
	.2byte	0x325
	.4byte	0x2f
	.8byte	.LFB152
	.8byte	.LFE152-.LFB152
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5b35
	.uleb128 0x24
	.8byte	.LVL1332
	.4byte	0x5b35
	.byte	0
	.uleb128 0x32
	.4byte	.LASF623
	.byte	0x3
	.2byte	0x31c
	.4byte	0x2f
	.8byte	.LFB151
	.8byte	.LFE151-.LFB151
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5b8c
	.uleb128 0x24
	.8byte	.LVL1328
	.4byte	0x57a6
	.uleb128 0x24
	.8byte	.LVL1329
	.4byte	0x40c4
	.uleb128 0x24
	.8byte	.LVL1330
	.4byte	0x3d9f
	.uleb128 0x24
	.8byte	.LVL1331
	.4byte	0x3baa
	.byte	0
	.uleb128 0x32
	.4byte	.LASF624
	.byte	0x3
	.2byte	0x2e6
	.4byte	0x2f
	.8byte	.LFB150
	.8byte	.LFE150-.LFB150
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c4b
	.uleb128 0x35
	.4byte	.LASF625
	.byte	0x3
	.2byte	0x2e8
	.4byte	0x5c4b
	.uleb128 0x37
	.4byte	.LASF626
	.byte	0x3
	.2byte	0x2f3
	.uleb128 0x4c
	.8byte	.LBB334
	.8byte	.LBE334-.LBB334
	.4byte	0x5bef
	.uleb128 0x36
	.string	"i"
	.byte	0x3
	.2byte	0x310
	.4byte	0x2f3
	.uleb128 0x24
	.8byte	.LVL1730
	.4byte	0x1e7e
	.byte	0
	.uleb128 0x24
	.8byte	.LVL1723
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1724
	.4byte	0x61cd
	.uleb128 0x24
	.8byte	.LVL1725
	.4byte	0x5fa9
	.uleb128 0x24
	.8byte	.LVL1726
	.4byte	0x5f2b
	.uleb128 0x24
	.8byte	.LVL1727
	.4byte	0x4b9d
	.uleb128 0x24
	.8byte	.LVL1728
	.4byte	0x4f9d
	.uleb128 0x24
	.8byte	.LVL1729
	.4byte	0x2c58
	.byte	0
	.uleb128 0x7
	.byte	0x8
	.4byte	0x400
	.uleb128 0x32
	.4byte	.LASF627
	.byte	0x3
	.2byte	0x26e
	.4byte	0x2f
	.8byte	.LFB149
	.8byte	.LFE149-.LFB149
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5d9e
	.uleb128 0x35
	.4byte	.LASF497
	.byte	0x3
	.2byte	0x270
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF628
	.byte	0x3
	.2byte	0x270
	.4byte	0x2e8
	.4byte	.LLST332
	.uleb128 0x3e
	.4byte	.LASF629
	.byte	0x3
	.2byte	0x271
	.4byte	0x2e8
	.4byte	.LLST333
	.uleb128 0x3d
	.string	"i"
	.byte	0x3
	.2byte	0x272
	.4byte	0x2e8
	.4byte	.LLST334
	.uleb128 0x41
	.4byte	.LASF630
	.byte	0x3
	.2byte	0x2bc
	.8byte	.L1628
	.uleb128 0x41
	.4byte	.LASF631
	.byte	0x3
	.2byte	0x2cc
	.8byte	.L1630
	.uleb128 0x24
	.8byte	.LVL1690
	.4byte	0x4b9d
	.uleb128 0x24
	.8byte	.LVL1691
	.4byte	0x4f9d
	.uleb128 0x24
	.8byte	.LVL1692
	.4byte	0x4d9d
	.uleb128 0x24
	.8byte	.LVL1699
	.4byte	0x510a
	.uleb128 0x24
	.8byte	.LVL1700
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1701
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1702
	.4byte	0x30ad
	.uleb128 0x24
	.8byte	.LVL1707
	.4byte	0x5d9e
	.uleb128 0x24
	.8byte	.LVL1711
	.4byte	0x622e
	.uleb128 0x24
	.8byte	.LVL1712
	.4byte	0x4b9d
	.uleb128 0x24
	.8byte	.LVL1713
	.4byte	0x5d9e
	.uleb128 0x24
	.8byte	.LVL1715
	.4byte	0x5d9e
	.uleb128 0x24
	.8byte	.LVL1719
	.4byte	0x30ad
	.uleb128 0x24
	.8byte	.LVL1720
	.4byte	0x49e8
	.uleb128 0x24
	.8byte	.LVL1721
	.4byte	0x3baa
	.uleb128 0x24
	.8byte	.LVL1722
	.4byte	0x2c58
	.byte	0
	.uleb128 0x32
	.4byte	.LASF632
	.byte	0x3
	.2byte	0x20f
	.4byte	0x2f
	.8byte	.LFB148
	.8byte	.LFE148-.LFB148
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f01
	.uleb128 0x3c
	.4byte	.LASF497
	.byte	0x3
	.2byte	0x20f
	.4byte	0x2e8
	.4byte	.LLST106
	.uleb128 0x3c
	.4byte	.LASF633
	.byte	0x3
	.2byte	0x20f
	.4byte	0x2dd
	.4byte	.LLST107
	.uleb128 0x3e
	.4byte	.LASF628
	.byte	0x3
	.2byte	0x211
	.4byte	0x2e8
	.4byte	.LLST108
	.uleb128 0x35
	.4byte	.LASF352
	.byte	0x3
	.2byte	0x211
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF373
	.byte	0x3
	.2byte	0x212
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF175
	.byte	0x3
	.2byte	0x212
	.4byte	0x2e8
	.uleb128 0x3e
	.4byte	.LASF634
	.byte	0x3
	.2byte	0x213
	.4byte	0x2e8
	.4byte	.LLST109
	.uleb128 0x3e
	.4byte	.LASF635
	.byte	0x3
	.2byte	0x214
	.4byte	0x2e8
	.4byte	.LLST110
	.uleb128 0x3e
	.4byte	.LASF636
	.byte	0x3
	.2byte	0x215
	.4byte	0x2e8
	.4byte	.LLST111
	.uleb128 0x3e
	.4byte	.LASF637
	.byte	0x3
	.2byte	0x216
	.4byte	0x2e8
	.4byte	.LLST112
	.uleb128 0x41
	.4byte	.LASF638
	.byte	0x3
	.2byte	0x240
	.8byte	.L411
	.uleb128 0x24
	.8byte	.LVL438
	.4byte	0x1b39
	.uleb128 0x24
	.8byte	.LVL441
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL449
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL452
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL459
	.4byte	0x4b4a
	.uleb128 0x24
	.8byte	.LVL461
	.4byte	0x1b39
	.uleb128 0x24
	.8byte	.LVL468
	.4byte	0x4d54
	.uleb128 0x24
	.8byte	.LVL469
	.4byte	0x5f01
	.uleb128 0x24
	.8byte	.LVL470
	.4byte	0x5265
	.uleb128 0x24
	.8byte	.LVL477
	.4byte	0x4d54
	.uleb128 0x24
	.8byte	.LVL478
	.4byte	0x5f01
	.uleb128 0x24
	.8byte	.LVL479
	.4byte	0x5265
	.byte	0
	.uleb128 0x33
	.4byte	.LASF639
	.byte	0x3
	.2byte	0x201
	.4byte	0x2f
	.byte	0x1
	.4byte	0x5f2b
	.uleb128 0x34
	.4byte	.LASF175
	.byte	0x3
	.2byte	0x201
	.4byte	0x2e8
	.uleb128 0x35
	.4byte	.LASF640
	.byte	0x3
	.2byte	0x203
	.4byte	0x2e8
	.byte	0
	.uleb128 0x32
	.4byte	.LASF641
	.byte	0x3
	.2byte	0x1ec
	.4byte	0x2f
	.8byte	.LFB146
	.8byte	.LFE146-.LFB146
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5fa9
	.uleb128 0x24
	.8byte	.LVL811
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL812
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL813
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL814
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL815
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL816
	.4byte	0x2645
	.uleb128 0x24
	.8byte	.LVL817
	.4byte	0x3748
	.byte	0
	.uleb128 0x32
	.4byte	.LASF642
	.byte	0x3
	.2byte	0x116
	.4byte	0x2f
	.8byte	.LFB145
	.8byte	.LFE145-.LFB145
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x61cd
	.uleb128 0x3d
	.string	"i"
	.byte	0x3
	.2byte	0x118
	.4byte	0x2f3
	.4byte	.LLST120
	.uleb128 0x35
	.4byte	.LASF643
	.byte	0x3
	.2byte	0x119
	.4byte	0x2f3
	.uleb128 0x2e
	.4byte	.LASF357
	.4byte	0x508d
	.uleb128 0x9
	.byte	0x3
	.8byte	__func__.3388
	.uleb128 0x24
	.8byte	.LVL499
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL500
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL501
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL502
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL503
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL504
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL505
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL506
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL507
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL508
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL509
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL510
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL511
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL512
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL513
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL514
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL515
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL516
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL517
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL518
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL519
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL520
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL521
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL522
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL523
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL524
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL525
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL526
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL527
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL528
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL529
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL530
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL531
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL532
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL533
	.4byte	0x75bd
	.uleb128 0x24
	.8byte	.LVL536
	.4byte	0x75a7
	.byte	0
	.uleb128 0x22
	.4byte	.LASF644
	.byte	0x3
	.byte	0xb6
	.4byte	0x2f
	.8byte	.LFB144
	.8byte	.LFE144-.LFB144
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x622e
	.uleb128 0x23
	.4byte	.LASF625
	.byte	0x3
	.byte	0xb6
	.4byte	0x5c4b
	.4byte	.LLST5
	.uleb128 0x29
	.string	"i"
	.byte	0x3
	.byte	0xb8
	.4byte	0x2f3
	.uleb128 0x24
	.8byte	.LVL19
	.4byte	0x6277
	.uleb128 0x24
	.8byte	.LVL20
	.4byte	0x6277
	.uleb128 0x24
	.8byte	.LVL21
	.4byte	0x622e
	.byte	0
	.uleb128 0x22
	.4byte	.LASF645
	.byte	0x3
	.byte	0xa4
	.4byte	0x2f
	.8byte	.LFB143
	.8byte	.LFE143-.LFB143
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x625f
	.uleb128 0x23
	.4byte	.LASF646
	.byte	0x3
	.byte	0xa4
	.4byte	0x2e8
	.4byte	.LLST4
	.byte	0
	.uleb128 0x4e
	.4byte	.LASF647
	.byte	0x3
	.byte	0x9e
	.byte	0x1
	.4byte	0x6277
	.uleb128 0x2b
	.4byte	.LASF648
	.byte	0x3
	.byte	0x9e
	.4byte	0x2f3
	.byte	0
	.uleb128 0x22
	.4byte	.LASF649
	.byte	0x3
	.byte	0x7e
	.4byte	0x2e8
	.8byte	.LFB141
	.8byte	.LFE141-.LFB141
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x62be
	.uleb128 0x26
	.string	"num"
	.byte	0x3
	.byte	0x7e
	.4byte	0x2f3
	.4byte	.LLST3
	.uleb128 0x29
	.string	"i"
	.byte	0x3
	.byte	0x80
	.4byte	0x2e8
	.uleb128 0x20
	.4byte	.LASF650
	.byte	0x3
	.byte	0x81
	.4byte	0x2f3
	.uleb128 0x1
	.byte	0x52
	.byte	0
	.uleb128 0x22
	.4byte	.LASF651
	.byte	0x3
	.byte	0x70
	.4byte	0x2f
	.8byte	.LFB140
	.8byte	.LFE140-.LFB140
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6314
	.uleb128 0x23
	.4byte	.LASF82
	.byte	0x3
	.byte	0x70
	.4byte	0xb3
	.4byte	.LLST62
	.uleb128 0x29
	.string	"buf"
	.byte	0x3
	.byte	0x72
	.4byte	0xb3
	.uleb128 0x24
	.8byte	.LVL274
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL275
	.4byte	0x6314
	.byte	0
	.uleb128 0x22
	.4byte	.LASF652
	.byte	0x3
	.byte	0xa
	.4byte	0x2f
	.8byte	.LFB139
	.8byte	.LFE139-.LFB139
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x673e
	.uleb128 0x23
	.4byte	.LASF82
	.byte	0x3
	.byte	0xa
	.4byte	0xb3
	.4byte	.LLST59
	.uleb128 0x29
	.string	"i"
	.byte	0x3
	.byte	0xc
	.4byte	0x2f
	.uleb128 0x29
	.string	"buf"
	.byte	0x3
	.byte	0xd
	.4byte	0xb3
	.uleb128 0x4c
	.8byte	.LBB208
	.8byte	.LBE208-.LBB208
	.4byte	0x6397
	.uleb128 0x28
	.4byte	.LASF389
	.byte	0x3
	.byte	0x56
	.4byte	0x2e8
	.4byte	.LLST60
	.uleb128 0x24
	.8byte	.LVL265
	.4byte	0x4717
	.uleb128 0x24
	.8byte	.LVL267
	.4byte	0x75c8
	.byte	0
	.uleb128 0x4c
	.8byte	.LBB209
	.8byte	.LBE209-.LBB209
	.4byte	0x63c9
	.uleb128 0x28
	.4byte	.LASF389
	.byte	0x3
	.byte	0x5f
	.4byte	0x2e8
	.4byte	.LLST61
	.uleb128 0x24
	.8byte	.LVL271
	.4byte	0x75c8
	.byte	0
	.uleb128 0x24
	.8byte	.LVL194
	.4byte	0x75d4
	.uleb128 0x24
	.8byte	.LVL195
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL196
	.4byte	0x75d4
	.uleb128 0x24
	.8byte	.LVL197
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL198
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL199
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL200
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL201
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL202
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL203
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL204
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL205
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL206
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL207
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL208
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL209
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL210
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL211
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL212
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL213
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL214
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL215
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL216
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL217
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL218
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL219
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL220
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL221
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL222
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL223
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL224
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL225
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL226
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL227
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL228
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL229
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL230
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL231
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL232
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL233
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL234
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL235
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL236
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL237
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL238
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL239
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL240
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL241
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL242
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL243
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL244
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL245
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL246
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL247
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL248
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL249
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL250
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL251
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL252
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL253
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL254
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL255
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL256
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL257
	.4byte	0x2bb4
	.uleb128 0x24
	.8byte	.LVL258
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL259
	.4byte	0x2b57
	.uleb128 0x24
	.8byte	.LVL260
	.4byte	0x75c8
	.uleb128 0x24
	.8byte	.LVL264
	.4byte	0x75c8
	.byte	0
	.uleb128 0x56
	.4byte	0x1dcf
	.8byte	.LFB269
	.8byte	.LFE269-.LFB269
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x679c
	.uleb128 0x57
	.4byte	0x1dea
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x57
	.4byte	0x1df5
	.uleb128 0x1
	.byte	0x52
	.uleb128 0x48
	.4byte	0x1ddf
	.uleb128 0x58
	.4byte	0x1e00
	.uleb128 0x58
	.4byte	0x1e0b
	.uleb128 0x59
	.4byte	0x1e16
	.4byte	.LLST0
	.uleb128 0x5a
	.4byte	0x1e21
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x5a
	.4byte	0x1e2c
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x58
	.4byte	0x1e37
	.uleb128 0x59
	.4byte	0x1e42
	.4byte	.LLST1
	.byte	0
	.uleb128 0x56
	.4byte	0x3a2f
	.8byte	.LFB276
	.8byte	.LFE276-.LFB276
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x67c6
	.uleb128 0x5b
	.4byte	0x3a3c
	.4byte	.LLST2
	.uleb128 0x48
	.4byte	0x3a48
	.byte	0
	.uleb128 0x56
	.4byte	0x625f
	.8byte	.LFB142
	.8byte	.LFE142-.LFB142
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x67e9
	.uleb128 0x57
	.4byte	0x626b
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x56
	.4byte	0x5f01
	.8byte	.LFB147
	.8byte	.LFE147-.LFB147
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6829
	.uleb128 0x5b
	.4byte	0x5f12
	.4byte	.LLST6
	.uleb128 0x58
	.4byte	0x5f1e
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x48
	.4byte	0x5f12
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x58
	.4byte	0x680d
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5d
	.4byte	0x525d
	.8byte	.LFB163
	.8byte	.LFE163-.LFB163
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x5d
	.4byte	0x4b91
	.8byte	.LFB177
	.8byte	.LFE177-.LFB177
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x5d
	.4byte	0x4b85
	.8byte	.LFB178
	.8byte	.LFE178-.LFB178
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x56
	.4byte	0x4929
	.8byte	.LFB185
	.8byte	.LFE185-.LFB185
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x690a
	.uleb128 0x5b
	.4byte	0x4939
	.4byte	.LLST20
	.uleb128 0x58
	.4byte	0x4944
	.uleb128 0x58
	.4byte	0x494f
	.uleb128 0x58
	.4byte	0x495a
	.uleb128 0x58
	.4byte	0x4965
	.uleb128 0x58
	.4byte	0x4970
	.uleb128 0x58
	.4byte	0x497b
	.uleb128 0x58
	.4byte	0x4986
	.uleb128 0x58
	.4byte	0x4991
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0x60
	.uleb128 0x48
	.4byte	0x4939
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0x60
	.uleb128 0x5a
	.4byte	0x6892
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x5a
	.4byte	0x6897
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x59
	.4byte	0x689c
	.4byte	.LLST21
	.uleb128 0x59
	.4byte	0x68a1
	.4byte	.LLST22
	.uleb128 0x5a
	.4byte	0x68a6
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x59
	.4byte	0x68ab
	.4byte	.LLST23
	.uleb128 0x58
	.4byte	0x68b0
	.uleb128 0x59
	.4byte	0x68b5
	.4byte	.LLST24
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x499e
	.8byte	.LFB184
	.8byte	.LFE184-.LFB184
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6980
	.uleb128 0x5b
	.4byte	0x49aa
	.4byte	.LLST25
	.uleb128 0x58
	.4byte	0x49b5
	.uleb128 0x4c
	.8byte	.LBB188
	.8byte	.LBE188-.LBB188
	.4byte	0x6972
	.uleb128 0x48
	.4byte	0x49aa
	.uleb128 0x5e
	.8byte	.LBB189
	.8byte	.LBE189-.LBB189
	.uleb128 0x58
	.4byte	0x692e
	.uleb128 0x24
	.8byte	.LVL87
	.4byte	0x75a7
	.byte	0
	.byte	0
	.uleb128 0x24
	.8byte	.LVL86
	.4byte	0x4929
	.byte	0
	.uleb128 0x56
	.4byte	0x49c3
	.8byte	.LFB183
	.8byte	.LFE183-.LFB183
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x69f6
	.uleb128 0x5b
	.4byte	0x49cf
	.4byte	.LLST29
	.uleb128 0x58
	.4byte	0x49da
	.uleb128 0x4c
	.8byte	.LBB192
	.8byte	.LBE192-.LBB192
	.4byte	0x69e8
	.uleb128 0x48
	.4byte	0x49cf
	.uleb128 0x5e
	.8byte	.LBB193
	.8byte	.LBE193-.LBB193
	.uleb128 0x58
	.4byte	0x69a4
	.uleb128 0x24
	.8byte	.LVL98
	.4byte	0x75a7
	.byte	0
	.byte	0
	.uleb128 0x24
	.8byte	.LVL97
	.4byte	0x48b0
	.byte	0
	.uleb128 0x56
	.4byte	0x4698
	.8byte	.LFB191
	.8byte	.LFE191-.LFB191
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6adc
	.uleb128 0x5b
	.4byte	0x46a9
	.4byte	.LLST40
	.uleb128 0x58
	.4byte	0x46b5
	.uleb128 0x58
	.4byte	0x46c1
	.uleb128 0x58
	.4byte	0x46cd
	.uleb128 0x58
	.4byte	0x46d9
	.uleb128 0x58
	.4byte	0x46e5
	.uleb128 0x58
	.4byte	0x46f1
	.uleb128 0x58
	.4byte	0x46fd
	.uleb128 0x58
	.4byte	0x4709
	.uleb128 0x5e
	.8byte	.LBB196
	.8byte	.LBE196-.LBB196
	.uleb128 0x48
	.4byte	0x46a9
	.uleb128 0x5e
	.8byte	.LBB197
	.8byte	.LBE197-.LBB197
	.uleb128 0x58
	.4byte	0x6a1a
	.uleb128 0x59
	.4byte	0x6a1f
	.4byte	.LLST41
	.uleb128 0x59
	.4byte	0x6a24
	.4byte	.LLST42
	.uleb128 0x59
	.4byte	0x6a29
	.4byte	.LLST43
	.uleb128 0x59
	.4byte	0x6a2e
	.4byte	.LLST44
	.uleb128 0x59
	.4byte	0x6a33
	.4byte	.LLST45
	.uleb128 0x58
	.4byte	0x6a38
	.uleb128 0x58
	.4byte	0x6a3d
	.uleb128 0x24
	.8byte	.LVL134
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL139
	.4byte	0x481c
	.uleb128 0x24
	.8byte	.LVL140
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL141
	.4byte	0x499e
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x3d7b
	.8byte	.LFB205
	.8byte	.LFE205-.LFB205
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6b1c
	.uleb128 0x5b
	.4byte	0x3d88
	.4byte	.LLST47
	.uleb128 0x58
	.4byte	0x3d94
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0xb0
	.uleb128 0x48
	.4byte	0x3d88
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0xb0
	.uleb128 0x58
	.4byte	0x6b00
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x3a2f
	.8byte	.LFB211
	.8byte	.LFE211-.LFB211
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6b57
	.uleb128 0x5b
	.4byte	0x3a3c
	.4byte	.LLST48
	.uleb128 0x5b
	.4byte	0x3a48
	.4byte	.LLST49
	.uleb128 0x24
	.8byte	.LVL153
	.4byte	0x679c
	.byte	0
	.uleb128 0x56
	.4byte	0x31a1
	.8byte	.LFB221
	.8byte	.LFE221-.LFB221
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6baf
	.uleb128 0x57
	.4byte	0x31ae
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x58
	.4byte	0x31ba
	.uleb128 0x5e
	.8byte	.LBB206
	.8byte	.LBE206-.LBB206
	.uleb128 0x48
	.4byte	0x31ae
	.uleb128 0x5e
	.8byte	.LBB207
	.8byte	.LBE207-.LBB207
	.uleb128 0x5a
	.4byte	0x6b79
	.uleb128 0x1
	.byte	0x53
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x2a79
	.8byte	.LFB236
	.8byte	.LFE236-.LFB236
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6bec
	.uleb128 0x5b
	.4byte	0x2a8a
	.4byte	.LLST69
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0xe0
	.uleb128 0x48
	.4byte	0x2a8a
	.uleb128 0x24
	.8byte	.LVL303
	.4byte	0x49c3
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x2010
	.8byte	.LFB255
	.8byte	.LFE255-.LFB255
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6c22
	.uleb128 0x24
	.8byte	.LVL356
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL357
	.4byte	0x208d
	.byte	0
	.uleb128 0x56
	.4byte	0x4b4a
	.8byte	.LFB179
	.8byte	.LFE179-.LFB179
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6ccf
	.uleb128 0x5b
	.4byte	0x4b56
	.4byte	.LLST103
	.uleb128 0x5b
	.4byte	0x4b61
	.4byte	.LLST104
	.uleb128 0x5f
	.4byte	0x4b85
	.8byte	.LBB230
	.8byte	.LBE230-.LBB230
	.byte	0x2
	.byte	0x65
	.uleb128 0x5e
	.8byte	.LBB232
	.8byte	.LBE232-.LBB232
	.uleb128 0x48
	.4byte	0x4b61
	.uleb128 0x48
	.4byte	0x4b56
	.uleb128 0x5e
	.8byte	.LBB233
	.8byte	.LBE233-.LBB233
	.uleb128 0x58
	.4byte	0x4b6d
	.uleb128 0x5e
	.8byte	.LBB234
	.8byte	.LBE234-.LBB234
	.uleb128 0x59
	.4byte	0x4b77
	.4byte	.LLST105
	.uleb128 0x24
	.8byte	.LVL427
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL429
	.4byte	0x1b39
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x44a6
	.8byte	.LFB194
	.8byte	.LFE194-.LFB194
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6d6a
	.uleb128 0x5b
	.4byte	0x44b7
	.4byte	.LLST115
	.uleb128 0x59
	.4byte	0x44c3
	.4byte	.LLST116
	.uleb128 0x58
	.4byte	0x44cf
	.uleb128 0x58
	.4byte	0x44db
	.uleb128 0x4c
	.8byte	.LBB245
	.8byte	.LBE245-.LBB245
	.4byte	0x6d4f
	.uleb128 0x48
	.4byte	0x44b7
	.uleb128 0x5e
	.8byte	.LBB246
	.8byte	.LBE246-.LBB246
	.uleb128 0x58
	.4byte	0x6cf3
	.uleb128 0x58
	.4byte	0x6cfc
	.uleb128 0x58
	.4byte	0x6d01
	.uleb128 0x24
	.8byte	.LVL491
	.4byte	0x75a7
	.byte	0
	.byte	0
	.uleb128 0x24
	.8byte	.LVL489
	.4byte	0x49e8
	.uleb128 0x24
	.8byte	.LVL490
	.4byte	0x75a7
	.byte	0
	.uleb128 0x56
	.4byte	0x1f66
	.8byte	.LFB257
	.8byte	.LFE257-.LFB257
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6e2f
	.uleb128 0x5a
	.4byte	0x1f77
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x58
	.4byte	0x1f83
	.uleb128 0x58
	.4byte	0x1f8d
	.uleb128 0x58
	.4byte	0x1f99
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1e0
	.4byte	0x6e13
	.uleb128 0x58
	.4byte	0x6d85
	.uleb128 0x58
	.4byte	0x6d8c
	.uleb128 0x59
	.4byte	0x6d91
	.4byte	.LLST165
	.uleb128 0x5a
	.4byte	0x6d96
	.uleb128 0x2
	.byte	0x91
	.sleb128 -64
	.uleb128 0x4c
	.8byte	.LBB256
	.8byte	.LBE256-.LBB256
	.4byte	0x6e05
	.uleb128 0x5a
	.4byte	0x1faa
	.uleb128 0x3
	.byte	0x91
	.sleb128 -96
	.uleb128 0x24
	.8byte	.LVL712
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL713
	.4byte	0x4cdc
	.uleb128 0x24
	.8byte	.LVL714
	.4byte	0x208d
	.byte	0
	.uleb128 0x24
	.8byte	.LVL711
	.4byte	0x3fd5
	.byte	0
	.uleb128 0x5e
	.8byte	.LBB263
	.8byte	.LBE263-.LBB263
	.uleb128 0x59
	.4byte	0x1fb8
	.4byte	.LLST166
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x1ae5
	.8byte	.LFB264
	.8byte	.LFE264-.LFB264
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6ee8
	.uleb128 0x5b
	.4byte	0x1af5
	.4byte	.LLST199
	.uleb128 0x58
	.4byte	0x1b00
	.uleb128 0x58
	.4byte	0x1b0b
	.uleb128 0x58
	.4byte	0x1b16
	.uleb128 0x5e
	.8byte	.LBB268
	.8byte	.LBE268-.LBB268
	.uleb128 0x48
	.4byte	0x1af5
	.uleb128 0x5e
	.8byte	.LBB269
	.8byte	.LBE269-.LBB269
	.uleb128 0x58
	.4byte	0x6e53
	.uleb128 0x5a
	.4byte	0x6e58
	.uleb128 0x2
	.byte	0x91
	.sleb128 -64
	.uleb128 0x5a
	.4byte	0x6e5d
	.uleb128 0x3
	.byte	0x91
	.sleb128 -96
	.uleb128 0x60
	.4byte	0x1b21
	.uleb128 0x24
	.8byte	.LVL893
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL895
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL896
	.4byte	0x1b39
	.uleb128 0x24
	.8byte	.LVL897
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL898
	.4byte	0x1b39
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x1fc6
	.8byte	.LFB256
	.8byte	.LFE256-.LFB256
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6f97
	.uleb128 0x5b
	.4byte	0x1fd7
	.4byte	.LLST215
	.uleb128 0x58
	.4byte	0x1fe3
	.uleb128 0x58
	.4byte	0x1fef
	.uleb128 0x58
	.4byte	0x1ff9
	.uleb128 0x58
	.4byte	0x2003
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0x260
	.uleb128 0x48
	.4byte	0x1fd7
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0x260
	.uleb128 0x59
	.4byte	0x6f0c
	.4byte	.LLST216
	.uleb128 0x59
	.4byte	0x6f11
	.4byte	.LLST217
	.uleb128 0x59
	.4byte	0x6f16
	.4byte	.LLST218
	.uleb128 0x59
	.4byte	0x6f1b
	.4byte	.LLST219
	.uleb128 0x24
	.8byte	.LVL977
	.4byte	0x2010
	.uleb128 0x24
	.8byte	.LVL982
	.4byte	0x4d54
	.uleb128 0x24
	.8byte	.LVL985
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL986
	.4byte	0x52c4
	.uleb128 0x24
	.8byte	.LVL987
	.4byte	0x510a
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x2707
	.8byte	.LFB240
	.8byte	.LFE240-.LFB240
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x7041
	.uleb128 0x5b
	.4byte	0x2718
	.4byte	.LLST220
	.uleb128 0x58
	.4byte	0x2724
	.uleb128 0x4c
	.8byte	.LBB282
	.8byte	.LBE282-.LBB282
	.4byte	0x7033
	.uleb128 0x48
	.4byte	0x2718
	.uleb128 0x5e
	.8byte	.LBB283
	.8byte	.LBE283-.LBB283
	.uleb128 0x58
	.4byte	0x6fbb
	.uleb128 0x24
	.8byte	.LVL995
	.4byte	0x481c
	.uleb128 0x24
	.8byte	.LVL996
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL997
	.4byte	0x2a79
	.uleb128 0x24
	.8byte	.LVL998
	.4byte	0x1fc6
	.uleb128 0x24
	.8byte	.LVL1000
	.4byte	0x75a7
	.byte	0
	.byte	0
	.uleb128 0x24
	.8byte	.LVL1004
	.4byte	0x4698
	.byte	0
	.uleb128 0x56
	.4byte	0x3ca9
	.8byte	.LFB207
	.8byte	.LFE207-.LFB207
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x70d9
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0x2a0
	.uleb128 0x59
	.4byte	0x3cb7
	.4byte	.LLST222
	.uleb128 0x59
	.4byte	0x3cc3
	.4byte	.LLST223
	.uleb128 0x59
	.4byte	0x3ccf
	.4byte	.LLST224
	.uleb128 0x5a
	.4byte	0x3cdb
	.uleb128 0x2
	.byte	0x91
	.sleb128 -32
	.uleb128 0x58
	.4byte	0x3ce7
	.uleb128 0x24
	.8byte	.LVL1014
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1016
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1021
	.4byte	0x1a1d
	.uleb128 0x24
	.8byte	.LVL1023
	.4byte	0x2747
	.uleb128 0x24
	.8byte	.LVL1024
	.4byte	0x1bdb
	.uleb128 0x24
	.8byte	.LVL1025
	.4byte	0x266c
	.byte	0
	.byte	0
	.uleb128 0x56
	.4byte	0x2fb7
	.8byte	.LFB225
	.8byte	.LFE225-.LFB225
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x7102
	.uleb128 0x24
	.8byte	.LVL1224
	.4byte	0x3ee5
	.byte	0
	.uleb128 0x56
	.4byte	0x3a55
	.8byte	.LFB210
	.8byte	.LFE210-.LFB210
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x7145
	.uleb128 0x24
	.8byte	.LVL1257
	.4byte	0x3dfc
	.uleb128 0x24
	.8byte	.LVL1258
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1259
	.4byte	0x1a1d
	.byte	0
	.uleb128 0x56
	.4byte	0x21e7
	.8byte	.LFB250
	.8byte	.LFE250-.LFB250
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x7188
	.uleb128 0x24
	.8byte	.LVL1391
	.4byte	0x21f0
	.uleb128 0x24
	.8byte	.LVL1392
	.4byte	0x3cf5
	.uleb128 0x24
	.8byte	.LVL1393
	.4byte	0x2378
	.byte	0
	.uleb128 0x56
	.4byte	0x1e7e
	.8byte	.LFB258
	.8byte	.LFE258-.LFB258
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x740e
	.uleb128 0x5b
	.4byte	0x1e8f
	.4byte	.LLST288
	.uleb128 0x5b
	.4byte	0x1e9b
	.4byte	.LLST289
	.uleb128 0x58
	.4byte	0x1ea7
	.uleb128 0x58
	.4byte	0x1eb3
	.uleb128 0x58
	.4byte	0x1ebf
	.uleb128 0x58
	.4byte	0x1ecb
	.uleb128 0x58
	.4byte	0x1ed7
	.uleb128 0x58
	.4byte	0x1ee3
	.uleb128 0x58
	.4byte	0x1eef
	.uleb128 0x58
	.4byte	0x1efb
	.uleb128 0x58
	.4byte	0x1f07
	.uleb128 0x58
	.4byte	0x1f13
	.uleb128 0x58
	.4byte	0x1f2f
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x310
	.4byte	0x7400
	.uleb128 0x48
	.4byte	0x1e9b
	.uleb128 0x48
	.4byte	0x1e8f
	.uleb128 0x5c
	.4byte	.Ldebug_ranges0+0x310
	.uleb128 0x59
	.4byte	0x71b5
	.4byte	.LLST290
	.uleb128 0x59
	.4byte	0x71ba
	.4byte	.LLST291
	.uleb128 0x59
	.4byte	0x71bf
	.4byte	.LLST292
	.uleb128 0x58
	.4byte	0x71c4
	.uleb128 0x58
	.4byte	0x71c9
	.uleb128 0x59
	.4byte	0x71ce
	.4byte	.LLST293
	.uleb128 0x59
	.4byte	0x71d3
	.4byte	.LLST294
	.uleb128 0x59
	.4byte	0x71d8
	.4byte	.LLST295
	.uleb128 0x59
	.4byte	0x71dd
	.4byte	.LLST296
	.uleb128 0x59
	.4byte	0x71e2
	.4byte	.LLST297
	.uleb128 0x60
	.4byte	0x1f1f
	.uleb128 0x60
	.4byte	0x1f27
	.uleb128 0x58
	.4byte	0x71e7
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x350
	.4byte	0x72b9
	.uleb128 0x59
	.4byte	0x1f3d
	.4byte	.LLST298
	.uleb128 0x24
	.8byte	.LVL1425
	.4byte	0x2a97
	.uleb128 0x24
	.8byte	.LVL1426
	.4byte	0x4717
	.uleb128 0x24
	.8byte	.LVL1429
	.4byte	0x4717
	.uleb128 0x24
	.8byte	.LVL1430
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1433
	.4byte	0x2010
	.byte	0
	.uleb128 0x24
	.8byte	.LVL1448
	.4byte	0x2563
	.uleb128 0x24
	.8byte	.LVL1450
	.4byte	0x30ad
	.uleb128 0x24
	.8byte	.LVL1454
	.4byte	0x4717
	.uleb128 0x24
	.8byte	.LVL1458
	.4byte	0x1f66
	.uleb128 0x24
	.8byte	.LVL1460
	.4byte	0x4717
	.uleb128 0x24
	.8byte	.LVL1461
	.4byte	0x1f66
	.uleb128 0x24
	.8byte	.LVL1463
	.4byte	0x266c
	.uleb128 0x24
	.8byte	.LVL1467
	.4byte	0x2563
	.uleb128 0x24
	.8byte	.LVL1468
	.4byte	0x266c
	.uleb128 0x24
	.8byte	.LVL1469
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1470
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1471
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1472
	.4byte	0x30ad
	.uleb128 0x24
	.8byte	.LVL1473
	.4byte	0x1f66
	.uleb128 0x24
	.8byte	.LVL1488
	.4byte	0x1d0d
	.uleb128 0x24
	.8byte	.LVL1492
	.4byte	0x75a7
	.uleb128 0x24
	.8byte	.LVL1493
	.4byte	0x3fd5
	.uleb128 0x24
	.8byte	.LVL1496
	.4byte	0x2169
	.uleb128 0x24
	.8byte	.LVL1499
	.4byte	0x259d
	.uleb128 0x24
	.8byte	.LVL1500
	.4byte	0x20cd
	.uleb128 0x24
	.8byte	.LVL1503
	.4byte	0x20cd
	.uleb128 0x24
	.8byte	.LVL1504
	.4byte	0x2707
	.uleb128 0x24
	.8byte	.LVL1505
	.4byte	0x57a6
	.uleb128 0x24
	.8byte	.LVL1506
	.4byte	0x40c4
	.uleb128 0x24
	.8byte	.LVL1507
	.4byte	0x3baa
	.byte	0
	.byte	0
	.uleb128 0x24
	.8byte	.LVL1431
	.4byte	0x2378
	.byte	0
	.uleb128 0x56
	.4byte	0x2c58
	.8byte	.LFB231
	.8byte	.LFE231-.LFB231
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x7588
	.uleb128 0x58
	.4byte	0x2c69
	.uleb128 0x4c
	.8byte	.LBB331
	.8byte	.LBE331-.LBB331
	.4byte	0x74f8
	.uleb128 0x59
	.4byte	0x7429
	.4byte	.LLST331
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x3c0
	.4byte	0x745b
	.uleb128 0x58
	.4byte	0x2c74
	.byte	0
	.uleb128 0x24
	.8byte	.LVL1676
	.4byte	0x2ef5
	.uleb128 0x24
	.8byte	.LVL1677
	.4byte	0x2ef5
	.uleb128 0x24
	.8byte	.LVL1678
	.4byte	0x3cf5
	.uleb128 0x24
	.8byte	.LVL1679
	.4byte	0x3cf5
	.uleb128 0x24
	.8byte	.LVL1680
	.4byte	0x40c4
	.uleb128 0x24
	.8byte	.LVL1681
	.4byte	0x3baa
	.uleb128 0x24
	.8byte	.LVL1682
	.4byte	0x3baa
	.uleb128 0x24
	.8byte	.LVL1685
	.4byte	0x208d
	.uleb128 0x24
	.8byte	.LVL1686
	.4byte	0x208d
	.uleb128 0x24
	.8byte	.LVL1687
	.4byte	0x27df
	.uleb128 0x24
	.8byte	.LVL1688
	.4byte	0x27df
	.uleb128 0x24
	.8byte	.LVL1689
	.4byte	0x2e0d
	.byte	0
	.uleb128 0x24
	.8byte	.LVL1663
	.4byte	0x4b9d
	.uleb128 0x24
	.8byte	.LVL1664
	.4byte	0x3a62
	.uleb128 0x24
	.8byte	.LVL1665
	.4byte	0x390d
	.uleb128 0x24
	.8byte	.LVL1666
	.4byte	0x370b
	.uleb128 0x24
	.8byte	.LVL1667
	.4byte	0x3791
	.uleb128 0x24
	.8byte	.LVL1668
	.4byte	0x2f7e
	.uleb128 0x24
	.8byte	.LVL1669
	.4byte	0x3a55
	.uleb128 0x24
	.8byte	.LVL1670
	.4byte	0x4afc
	.uleb128 0x24
	.8byte	.LVL1671
	.4byte	0x2fc0
	.uleb128 0x24
	.8byte	.LVL1672
	.4byte	0x3130
	.uleb128 0x24
	.8byte	.LVL1673
	.4byte	0x3d7b
	.byte	0
	.uleb128 0x61
	.4byte	.LASF653
	.4byte	.LASF653
	.byte	0x10
	.byte	0x35
	.uleb128 0x62
	.4byte	.LASF665
	.4byte	.LASF665
	.uleb128 0x61
	.4byte	.LASF654
	.4byte	.LASF654
	.byte	0x10
	.byte	0x31
	.uleb128 0x61
	.4byte	.LASF655
	.4byte	.LASF655
	.byte	0xa
	.byte	0x8e
	.uleb128 0x61
	.4byte	.LASF656
	.4byte	.LASF656
	.byte	0x11
	.byte	0x27
	.uleb128 0x61
	.4byte	.LASF657
	.4byte	.LASF657
	.byte	0xc
	.byte	0x18
	.uleb128 0x63
	.4byte	.LASF658
	.4byte	.LASF658
	.byte	0xb
	.2byte	0x19b
	.uleb128 0x64
	.4byte	.LASF666
	.4byte	.LASF667
	.byte	0x12
	.byte	0
	.4byte	.LASF666
	.byte	0
	.section	.debug_abbrev,"",@progbits
.Ldebug_abbrev0:
	.uleb128 0x1
	.uleb128 0x11
	.byte	0x1
	.uleb128 0x25
	.uleb128 0xe
	.uleb128 0x13
	.uleb128 0xb
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x1b
	.uleb128 0xe
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x10
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x2
	.uleb128 0xf
	.byte	0
	.uleb128 0xb
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x3
	.uleb128 0x24
	.byte	0
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x3e
	.uleb128 0xb
	.uleb128 0x3
	.uleb128 0x8
	.byte	0
	.byte	0
	.uleb128 0x4
	.uleb128 0x24
	.byte	0
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x3e
	.uleb128 0xb
	.uleb128 0x3
	.uleb128 0xe
	.byte	0
	.byte	0
	.uleb128 0x5
	.uleb128 0x26
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x6
	.uleb128 0x16
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x7
	.uleb128 0xf
	.byte	0
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x8
	.uleb128 0x16
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x9
	.uleb128 0x15
	.byte	0
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0xa
	.uleb128 0x15
	.byte	0
	.uleb128 0x27
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0xb
	.uleb128 0x1
	.byte	0x1
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0xc
	.uleb128 0x21
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0xd
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3c
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0xe
	.uleb128 0x13
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3c
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0xf
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3c
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0x10
	.uleb128 0x15
	.byte	0x1
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x11
	.uleb128 0x5
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x12
	.uleb128 0x4
	.byte	0x1
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x13
	.uleb128 0x28
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x1c
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x14
	.uleb128 0x13
	.byte	0x1
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x15
	.uleb128 0xd
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x38
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x16
	.uleb128 0xd
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x38
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x17
	.uleb128 0x21
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2f
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x18
	.uleb128 0x13
	.byte	0x1
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0xb
	.uleb128 0x5
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x19
	.uleb128 0x21
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2f
	.uleb128 0x5
	.byte	0
	.byte	0
	.uleb128 0x1a
	.uleb128 0xd
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x38
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x1b
	.uleb128 0x13
	.byte	0x1
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x1c
	.uleb128 0xd
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x38
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x1d
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x1e
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x1f
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x20
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x21
	.uleb128 0x34
	.byte	0
	.uleb128 0x47
	.uleb128 0x13
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x22
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x23
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x24
	.uleb128 0x4109
	.byte	0
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x25
	.uleb128 0x26
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x26
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x27
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2116
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x28
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x29
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x2a
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x20
	.uleb128 0xb
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x2b
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x2c
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x2d
	.uleb128 0xa
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x2e
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x34
	.uleb128 0x19
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x2f
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x55
	.uleb128 0x17
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x30
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x20
	.uleb128 0xb
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x31
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x32
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x33
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x20
	.uleb128 0xb
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x34
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x35
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x36
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x37
	.uleb128 0xa
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.byte	0
	.byte	0
	.uleb128 0x38
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x34
	.uleb128 0x19
	.uleb128 0x1c
	.uleb128 0xe
	.byte	0
	.byte	0
	.uleb128 0x39
	.uleb128 0xb
	.byte	0x1
	.byte	0
	.byte	0
	.uleb128 0x3a
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x3b
	.uleb128 0x2e
	.byte	0
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x20
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x3c
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x3d
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x3e
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x3f
	.uleb128 0x2e
	.byte	0
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x20
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x40
	.uleb128 0xa
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x41
	.uleb128 0xa
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x11
	.uleb128 0x1
	.byte	0
	.byte	0
	.uleb128 0x42
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x43
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x44
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x45
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x46
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x47
	.uleb128 0x1d
	.byte	0x1
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x58
	.uleb128 0xb
	.uleb128 0x59
	.uleb128 0x5
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x48
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x49
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x4a
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x4b
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x20
	.uleb128 0xb
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x4c
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x4d
	.uleb128 0x5
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x4e
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x20
	.uleb128 0xb
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x4f
	.uleb128 0xa
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x11
	.uleb128 0x1
	.byte	0
	.byte	0
	.uleb128 0x50
	.uleb128 0x1d
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x52
	.uleb128 0x1
	.uleb128 0x55
	.uleb128 0x17
	.uleb128 0x58
	.uleb128 0xb
	.uleb128 0x59
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x51
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x52
	.uleb128 0x2e
	.byte	0
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x20
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x53
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x34
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0x54
	.uleb128 0x2e
	.byte	0
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x20
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x55
	.uleb128 0x2e
	.byte	0
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0x56
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x57
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x58
	.uleb128 0x34
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x59
	.uleb128 0x34
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x5a
	.uleb128 0x34
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x5b
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x5c
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x55
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x5d
	.uleb128 0x2e
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0x5e
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.byte	0
	.byte	0
	.uleb128 0x5f
	.uleb128 0x1d
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x7
	.uleb128 0x58
	.uleb128 0xb
	.uleb128 0x59
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x60
	.uleb128 0xa
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x61
	.uleb128 0x2e
	.byte	0
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3c
	.uleb128 0x19
	.uleb128 0x6e
	.uleb128 0xe
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x62
	.uleb128 0x2e
	.byte	0
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3c
	.uleb128 0x19
	.uleb128 0x6e
	.uleb128 0xe
	.uleb128 0x3
	.uleb128 0xe
	.byte	0
	.byte	0
	.uleb128 0x63
	.uleb128 0x2e
	.byte	0
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3c
	.uleb128 0x19
	.uleb128 0x6e
	.uleb128 0xe
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.byte	0
	.byte	0
	.uleb128 0x64
	.uleb128 0x2e
	.byte	0
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3c
	.uleb128 0x19
	.uleb128 0x6e
	.uleb128 0xe
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x6e
	.uleb128 0xe
	.byte	0
	.byte	0
	.byte	0
	.section	.debug_loc,"",@progbits
.Ldebug_loc0:
.LLST335:
	.8byte	.LVL1731-.Ltext0
	.8byte	.LVL1733-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST336:
	.8byte	.LVL1731-.Ltext0
	.8byte	.LVL1733-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST337:
	.8byte	.LVL1731-.Ltext0
	.8byte	.LVL1732-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST203:
	.8byte	.LVL912-.Ltext0
	.8byte	.LVL914-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST204:
	.8byte	.LVL912-.Ltext0
	.8byte	.LVL914-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST205:
	.8byte	.LVL912-.Ltext0
	.8byte	.LVL913-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST117:
	.8byte	.LVL496-.Ltext0
	.8byte	.LVL498-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST118:
	.8byte	.LVL496-.Ltext0
	.8byte	.LVL498-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST119:
	.8byte	.LVL496-.Ltext0
	.8byte	.LVL497-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST200:
	.8byte	.LVL901-.Ltext0
	.8byte	.LVL903-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL903-1-.Ltext0
	.8byte	.LVL909-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL911-.Ltext0
	.8byte	.LFE265-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST201:
	.8byte	.LVL901-.Ltext0
	.8byte	.LVL902-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL902-.Ltext0
	.8byte	.LVL909-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL911-.Ltext0
	.8byte	.LFE265-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST202:
	.8byte	.LVL905-.Ltext0
	.8byte	.LVL906-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL908-.Ltext0
	.8byte	.LVL910-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL911-.Ltext0
	.8byte	.LFE265-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST99:
	.8byte	.LVL413-.Ltext0
	.8byte	.LVL418-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL420-.Ltext0
	.8byte	.LVL421-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST100:
	.8byte	.LVL413-.Ltext0
	.8byte	.LVL414-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST101:
	.8byte	.LVL413-.Ltext0
	.8byte	.LVL415-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST102:
	.8byte	.LVL416-.Ltext0
	.8byte	.LVL419-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL420-.Ltext0
	.8byte	.LFE263-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST94:
	.8byte	.LVL396-.Ltext0
	.8byte	.LVL400-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST95:
	.8byte	.LVL396-.Ltext0
	.8byte	.LVL397-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST96:
	.8byte	.LVL396-.Ltext0
	.8byte	.LVL398-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL398-.Ltext0
	.8byte	.LVL402-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL404-.Ltext0
	.8byte	.LFE262-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST97:
	.8byte	.LVL396-.Ltext0
	.8byte	.LVL404-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL404-.Ltext0
	.8byte	.LFE262-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	0
	.8byte	0
.LLST98:
	.8byte	.LVL399-.Ltext0
	.8byte	.LVL403-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL404-.Ltext0
	.8byte	.LFE262-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	0
	.8byte	0
.LLST83:
	.8byte	.LVL358-.Ltext0
	.8byte	.LVL362-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST84:
	.8byte	.LVL358-.Ltext0
	.8byte	.LVL359-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST85:
	.8byte	.LVL358-.Ltext0
	.8byte	.LVL360-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST86:
	.8byte	.LVL361-.Ltext0
	.8byte	.LVL364-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	.LVL365-.Ltext0
	.8byte	.LFE261-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	0
	.8byte	0
.LLST82:
	.8byte	.LVL350-.Ltext0
	.8byte	.LVL351-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL351-.Ltext0
	.8byte	.LVL355-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST81:
	.8byte	.LVL345-.Ltext0
	.8byte	.LVL346-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL346-.Ltext0
	.8byte	.LVL348-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL349-.Ltext0
	.8byte	.LFE253-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST286:
	.8byte	.LVL1401-.Ltext0
	.8byte	.LVL1402-1-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.8byte	.LVL1404-.Ltext0
	.8byte	.LVL1405-1-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.8byte	.LVL1408-.Ltext0
	.8byte	.LVL1412-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.8byte	.LVL1413-.Ltext0
	.8byte	.LVL1414-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.8byte	0
	.8byte	0
.LLST287:
	.8byte	.LVL1409-.Ltext0
	.8byte	.LVL1410-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST190:
	.8byte	.LVL855-.Ltext0
	.8byte	.LVL856-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL856-.Ltext0
	.8byte	.LVL888-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST191:
	.8byte	.LVL855-.Ltext0
	.8byte	.LVL859-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL859-.Ltext0
	.8byte	.LVL890-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	0
	.8byte	0
.LLST192:
	.8byte	.LVL862-.Ltext0
	.8byte	.LVL863-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL869-.Ltext0
	.8byte	.LVL870-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL873-.Ltext0
	.8byte	.LVL874-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST193:
	.8byte	.LVL861-.Ltext0
	.8byte	.LVL864-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL869-.Ltext0
	.8byte	.LVL871-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL872-.Ltext0
	.8byte	.LVL874-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST194:
	.8byte	.LVL868-.Ltext0
	.8byte	.LVL869-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL877-.Ltext0
	.8byte	.LVL880-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL881-.Ltext0
	.8byte	.LVL882-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL884-.Ltext0
	.8byte	.LVL885-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST195:
	.8byte	.LVL857-.Ltext0
	.8byte	.LVL866-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL867-.Ltext0
	.8byte	.LVL887-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST196:
	.8byte	.LVL875-.Ltext0
	.8byte	.LVL880-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL881-.Ltext0
	.8byte	.LVL883-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL884-.Ltext0
	.8byte	.LVL885-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	0
	.8byte	0
.LLST197:
	.8byte	.LVL860-.Ltext0
	.8byte	.LVL889-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST198:
	.8byte	.LVL878-.Ltext0
	.8byte	.LVL879-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL881-.Ltext0
	.8byte	.LVL882-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST281:
	.8byte	.LVL1355-.Ltext0
	.8byte	.LVL1358-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1362-.Ltext0
	.8byte	.LVL1363-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1364-.Ltext0
	.8byte	.LVL1365-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST282:
	.8byte	.LVL1375-.Ltext0
	.8byte	.LVL1379-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL1382-.Ltext0
	.8byte	.LVL1388-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL1389-.Ltext0
	.8byte	.LFE247-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST283:
	.8byte	.LVL1357-.Ltext0
	.8byte	.LVL1362-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 60
	.8byte	.LVL1367-.Ltext0
	.8byte	.LVL1371-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 60
	.8byte	.LVL1372-.Ltext0
	.8byte	.LFE247-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 60
	.8byte	0
	.8byte	0
.LLST284:
	.8byte	.LVL1356-.Ltext0
	.8byte	.LVL1359-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1362-.Ltext0
	.8byte	.LVL1366-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1371-.Ltext0
	.8byte	.LVL1372-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST285:
	.8byte	.LVL1360-.Ltext0
	.8byte	.LVL1361-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1376-.Ltext0
	.8byte	.LFE247-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST77:
	.8byte	.LVL338-.Ltext0
	.8byte	.LVL339-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL339-.Ltext0
	.8byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	0
	.8byte	0
.LLST78:
	.8byte	.LVL338-.Ltext0
	.8byte	.LVL340-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL340-1-.Ltext0
	.8byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x58
	.8byte	0
	.8byte	0
.LLST79:
	.8byte	.LVL338-.Ltext0
	.8byte	.LVL340-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL340-1-.Ltext0
	.8byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x57
	.8byte	0
	.8byte	0
.LLST80:
	.8byte	.LVL342-.Ltext0
	.8byte	.LVL343-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL344-.Ltext0
	.8byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST76:
	.8byte	.LVL334-.Ltext0
	.8byte	.LVL335-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL336-.Ltext0
	.8byte	.LVL337-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST74:
	.8byte	.LVL327-.Ltext0
	.8byte	.LVL328-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL329-.Ltext0
	.8byte	.LVL330-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL331-.Ltext0
	.8byte	.LFE244-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST75:
	.8byte	.LVL326-.Ltext0
	.8byte	.LVL332-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL333-.Ltext0
	.8byte	.LFE244-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST72:
	.8byte	.LVL320-.Ltext0
	.8byte	.LVL321-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL322-.Ltext0
	.8byte	.LFE243-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST73:
	.8byte	.LVL319-.Ltext0
	.8byte	.LVL323-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL324-.Ltext0
	.8byte	.LFE243-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST221:
	.8byte	.LVL1005-.Ltext0
	.8byte	.LVL1006-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1006-.Ltext0
	.8byte	.LVL1008-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1009-.Ltext0
	.8byte	.LFE241-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST70:
	.8byte	.LVL306-.Ltext0
	.8byte	.LVL307-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL307-.Ltext0
	.8byte	.LVL314-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL315-.Ltext0
	.8byte	.LFE239-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST71:
	.8byte	.LVL311-.Ltext0
	.8byte	.LVL312-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL315-.Ltext0
	.8byte	.LVL316-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL317-.Ltext0
	.8byte	.LFE239-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST267:
	.8byte	.LVL1295-.Ltext0
	.8byte	.LVL1297-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1297-.Ltext0
	.8byte	.LVL1305-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1306-.Ltext0
	.8byte	.LFE238-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST268:
	.8byte	.LVL1296-.Ltext0
	.8byte	.LVL1305-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1306-.Ltext0
	.8byte	.LFE238-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST210:
	.8byte	.LVL934-.Ltext0
	.8byte	.LVL971-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST211:
	.8byte	.LVL935-.Ltext0
	.8byte	.LVL950-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL950-.Ltext0
	.8byte	.LVL951-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL951-.Ltext0
	.8byte	.LVL953-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL953-.Ltext0
	.8byte	.LVL954-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL954-.Ltext0
	.8byte	.LVL963-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL963-.Ltext0
	.8byte	.LVL965-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL965-.Ltext0
	.8byte	.LVL972-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST212:
	.8byte	.LVL935-.Ltext0
	.8byte	.LVL961-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	.LVL962-.Ltext0
	.8byte	.LVL973-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	0
	.8byte	0
.LLST213:
	.8byte	.LVL956-.Ltext0
	.8byte	.LVL957-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL958-.Ltext0
	.8byte	.LVL959-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL966-.Ltext0
	.8byte	.LVL967-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL968-.Ltext0
	.8byte	.LVL969-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST214:
	.8byte	.LVL937-.Ltext0
	.8byte	.LVL938-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL939-.Ltext0
	.8byte	.LVL940-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL945-.Ltext0
	.8byte	.LVL946-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST63:
	.8byte	.LVL277-.Ltext0
	.8byte	.LVL279-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL286-.Ltext0
	.8byte	.LFE235-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST64:
	.8byte	.LVL277-.Ltext0
	.8byte	.LVL280-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL284-.Ltext0
	.8byte	.LVL295-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL297-.Ltext0
	.8byte	.LFE235-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	0
	.8byte	0
.LLST65:
	.8byte	.LVL277-.Ltext0
	.8byte	.LVL278-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL283-.Ltext0
	.8byte	.LVL287-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL297-.Ltext0
	.8byte	.LVL300-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL301-.Ltext0
	.8byte	.LFE235-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST66:
	.8byte	.LVL277-.Ltext0
	.8byte	.LVL280-.Ltext0
	.2byte	0x1
	.byte	0x56
	.8byte	.LVL282-.Ltext0
	.8byte	.LVL296-1-.Ltext0
	.2byte	0x1
	.byte	0x56
	.8byte	.LVL297-.Ltext0
	.8byte	.LFE235-.Ltext0
	.2byte	0x1
	.byte	0x56
	.8byte	0
	.8byte	0
.LLST67:
	.8byte	.LVL277-.Ltext0
	.8byte	.LVL280-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL288-.Ltext0
	.8byte	.LVL291-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL292-.Ltext0
	.8byte	.LVL294-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST68:
	.8byte	.LVL277-.Ltext0
	.8byte	.LVL280-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL285-.Ltext0
	.8byte	.LVL289-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL290-.Ltext0
	.8byte	.LVL293-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL297-.Ltext0
	.8byte	.LVL298-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL299-.Ltext0
	.8byte	.LFE235-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST56:
	.8byte	.LVL182-.Ltext0
	.8byte	.LVL184-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL187-.Ltext0
	.8byte	.LVL191-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST57:
	.8byte	.LVL185-.Ltext0
	.8byte	.LVL186-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL191-.Ltext0
	.8byte	.LVL192-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST58:
	.8byte	.LVL183-.Ltext0
	.8byte	.LVL188-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL189-.Ltext0
	.8byte	.LVL190-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST55:
	.8byte	.LVL178-.Ltext0
	.8byte	.LVL179-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL180-.Ltext0
	.8byte	.LVL181-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST54:
	.8byte	.LVL171-.Ltext0
	.8byte	.LVL172-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL172-.Ltext0
	.8byte	.LVL173-.Ltext0
	.2byte	0x1
	.byte	0x58
	.8byte	.LVL174-.Ltext0
	.8byte	.LVL177-.Ltext0
	.2byte	0x1
	.byte	0x58
	.8byte	0
	.8byte	0
.LLST164:
	.8byte	.LVL702-.Ltext0
	.8byte	.LVL704-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL705-.Ltext0
	.8byte	.LFE230-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST187:
	.8byte	.LVL836-.Ltext0
	.8byte	.LVL839-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL845-.Ltext0
	.8byte	.LVL848-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST188:
	.8byte	.LVL840-.Ltext0
	.8byte	.LVL843-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL848-.Ltext0
	.8byte	.LFE229-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST189:
	.8byte	.LVL841-.Ltext0
	.8byte	.LVL844-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL850-.Ltext0
	.8byte	.LFE229-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST265:
	.8byte	.LVL1281-.Ltext0
	.8byte	.LVL1282-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1287-.Ltext0
	.8byte	.LVL1293-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1294-.Ltext0
	.8byte	.LFE228-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST266:
	.8byte	.LVL1279-.Ltext0
	.8byte	.LVL1283-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1284-.Ltext0
	.8byte	.LVL1288-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST243:
	.8byte	.LVL1142-.Ltext0
	.8byte	.LVL1143-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1143-.Ltext0
	.8byte	.LVL1144-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1145-.Ltext0
	.8byte	.LFE227-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST184:
	.8byte	.LVL818-.Ltext0
	.8byte	.LVL827-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL828-.Ltext0
	.8byte	.LVL834-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST185:
	.8byte	.LVL820-.Ltext0
	.8byte	.LVL829-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL830-.Ltext0
	.8byte	.LVL832-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST186:
	.8byte	.LVL819-.Ltext0
	.8byte	.LVL824-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL825-.Ltext0
	.8byte	.LVL835-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST53:
	.8byte	.LVL164-.Ltext0
	.8byte	.LVL165-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL165-.Ltext0
	.8byte	.LVL167-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL168-.Ltext0
	.8byte	.LFE223-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST226:
	.8byte	.LVL1034-.Ltext0
	.8byte	.LVL1035-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1035-.Ltext0
	.8byte	.LVL1041-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1047-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST227:
	.8byte	.LVL1038-.Ltext0
	.8byte	.LVL1046-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	.LVL1047-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	0
	.8byte	0
.LLST228:
	.8byte	.LVL1036-.Ltext0
	.8byte	.LVL1037-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1039-.Ltext0
	.8byte	.LVL1042-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1056-.Ltext0
	.8byte	.LVL1058-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1064-.Ltext0
	.8byte	.LVL1065-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1066-.Ltext0
	.8byte	.LVL1067-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1075-.Ltext0
	.8byte	.LVL1076-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1083-.Ltext0
	.8byte	.LVL1085-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1087-.Ltext0
	.8byte	.LVL1089-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1090-.Ltext0
	.8byte	.LVL1091-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1093-.Ltext0
	.8byte	.LVL1095-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1096-.Ltext0
	.8byte	.LVL1097-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1137-.Ltext0
	.8byte	.LVL1138-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1139-.Ltext0
	.8byte	.LVL1140-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1141-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST229:
	.8byte	.LVL1057-.Ltext0
	.8byte	.LVL1058-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1087-.Ltext0
	.8byte	.LVL1088-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1092-.Ltext0
	.8byte	.LVL1094-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1096-.Ltext0
	.8byte	.LVL1097-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1141-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST230:
	.8byte	.LVL1040-.Ltext0
	.8byte	.LVL1046-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1055-.Ltext0
	.8byte	.LVL1060-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1075-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	0
	.8byte	0
.LLST231:
	.8byte	.LVL1040-.Ltext0
	.8byte	.LVL1044-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1053-.Ltext0
	.8byte	.LVL1060-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1071-.Ltext0
	.8byte	.LVL1072-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1075-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST232:
	.8byte	.LVL1039-.Ltext0
	.8byte	.LVL1044-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL1049-.Ltext0
	.8byte	.LVL1060-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL1061-.Ltext0
	.8byte	.LVL1062-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1062-.Ltext0
	.8byte	.LVL1077-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL1078-.Ltext0
	.8byte	.LVL1079-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL1081-.Ltext0
	.8byte	.LVL1084-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST233:
	.8byte	.LVL1052-.Ltext0
	.8byte	.LVL1054-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1068-.Ltext0
	.8byte	.LVL1070-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1072-.Ltext0
	.8byte	.LVL1073-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1074-.Ltext0
	.8byte	.LVL1075-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST234:
	.8byte	.LVL1040-.Ltext0
	.8byte	.LVL1045-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1057-.Ltext0
	.8byte	.LVL1060-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1083-.Ltext0
	.8byte	.LVL1101-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1102-.Ltext0
	.8byte	.LVL1110-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1115-.Ltext0
	.8byte	.LVL1135-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1137-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST235:
	.8byte	.LVL1040-.Ltext0
	.8byte	.LVL1047-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 132
	.8byte	.LVL1057-.Ltext0
	.8byte	.LVL1060-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 132
	.8byte	.LVL1083-.Ltext0
	.8byte	.LVL1126-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 132
	.8byte	.LVL1126-.Ltext0
	.8byte	.LVL1127-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1127-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 132
	.8byte	0
	.8byte	0
.LLST236:
	.8byte	.LVL1040-.Ltext0
	.8byte	.LVL1047-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 136
	.8byte	.LVL1057-.Ltext0
	.8byte	.LVL1060-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 136
	.8byte	.LVL1083-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 136
	.8byte	0
	.8byte	0
.LLST237:
	.8byte	.LVL1040-.Ltext0
	.8byte	.LVL1047-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 140
	.8byte	.LVL1057-.Ltext0
	.8byte	.LVL1060-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 140
	.8byte	.LVL1083-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 140
	.8byte	0
	.8byte	0
.LLST238:
	.8byte	.LVL1040-.Ltext0
	.8byte	.LVL1045-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1057-.Ltext0
	.8byte	.LVL1060-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1083-.Ltext0
	.8byte	.LVL1101-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1111-.Ltext0
	.8byte	.LVL1112-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1114-.Ltext0
	.8byte	.LVL1115-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1135-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST239:
	.8byte	.LVL1087-.Ltext0
	.8byte	.LVL1088-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1096-.Ltext0
	.8byte	.LVL1097-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1107-.Ltext0
	.8byte	.LVL1108-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1130-.Ltext0
	.8byte	.LVL1131-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST240:
	.8byte	.LVL1040-.Ltext0
	.8byte	.LVL1043-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1051-.Ltext0
	.8byte	.LVL1060-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1068-.Ltext0
	.8byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST241:
	.8byte	.LVL1117-.Ltext0
	.8byte	.LVL1118-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST242:
	.8byte	.LVL1106-.Ltext0
	.8byte	.LVL1107-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1120-.Ltext0
	.8byte	.LVL1121-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1123-.Ltext0
	.8byte	.LVL1126-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST158:
	.8byte	.LVL677-.Ltext0
	.8byte	.LVL678-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL678-.Ltext0
	.8byte	.LVL694-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL695-.Ltext0
	.8byte	.LFE219-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST159:
	.8byte	.LVL677-.Ltext0
	.8byte	.LVL679-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL679-1-.Ltext0
	.8byte	.LFE219-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 44
	.8byte	0
	.8byte	0
.LLST160:
	.8byte	.LVL680-.Ltext0
	.8byte	.LVL685-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL689-.Ltext0
	.8byte	.LVL691-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL695-.Ltext0
	.8byte	.LFE219-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST161:
	.8byte	.LVL681-.Ltext0
	.8byte	.LVL684-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL689-.Ltext0
	.8byte	.LVL690-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL695-.Ltext0
	.8byte	.LVL698-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL699-.Ltext0
	.8byte	.LVL701-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST162:
	.8byte	.LVL683-.Ltext0
	.8byte	.LVL693-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL695-.Ltext0
	.8byte	.LVL696-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL697-.Ltext0
	.8byte	.LVL700-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST163:
	.8byte	.LVL682-.Ltext0
	.8byte	.LVL686-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL689-.Ltext0
	.8byte	.LVL692-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL695-.Ltext0
	.8byte	.LVL700-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST51:
	.8byte	.LVL159-.Ltext0
	.8byte	.LVL160-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL160-.Ltext0
	.8byte	.LFE218-.Ltext0
	.2byte	0x1
	.byte	0x56
	.8byte	0
	.8byte	0
.LLST52:
	.8byte	.LVL159-.Ltext0
	.8byte	.LVL161-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST183:
	.8byte	.LVL804-.Ltext0
	.8byte	.LVL805-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL806-.Ltext0
	.8byte	.LVL807-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL808-.Ltext0
	.8byte	.LFE216-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST174:
	.8byte	.LVL754-.Ltext0
	.8byte	.LVL757-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL757-.Ltext0
	.8byte	.LVL779-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL783-.Ltext0
	.8byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST175:
	.8byte	.LVL755-.Ltext0
	.8byte	.LVL767-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	.LVL767-.Ltext0
	.8byte	.LVL782-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL783-.Ltext0
	.8byte	.LVL789-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL789-.Ltext0
	.8byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	0
	.8byte	0
.LLST176:
	.8byte	.LVL770-.Ltext0
	.8byte	.LVL775-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL783-.Ltext0
	.8byte	.LVL785-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST177:
	.8byte	.LVL796-.Ltext0
	.8byte	.LVL797-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL798-.Ltext0
	.8byte	.LVL799-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL800-.Ltext0
	.8byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST178:
	.8byte	.LVL757-.Ltext0
	.8byte	.LVL758-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL764-.Ltext0
	.8byte	.LVL769-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL772-.Ltext0
	.8byte	.LVL775-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL783-.Ltext0
	.8byte	.LVL784-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL787-.Ltext0
	.8byte	.LVL788-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL788-.Ltext0
	.8byte	.LVL790-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL791-.Ltext0
	.8byte	.LVL792-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL794-.Ltext0
	.8byte	.LVL795-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL796-.Ltext0
	.8byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST179:
	.8byte	.LVL760-.Ltext0
	.8byte	.LVL768-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL773-.Ltext0
	.8byte	.LVL780-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL788-.Ltext0
	.8byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST180:
	.8byte	.LVL760-.Ltext0
	.8byte	.LVL771-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL773-.Ltext0
	.8byte	.LVL781-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL788-.Ltext0
	.8byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST181:
	.8byte	.LVL756-.Ltext0
	.8byte	.LVL780-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL783-.Ltext0
	.8byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST182:
	.8byte	.LVL758-.Ltext0
	.8byte	.LVL759-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL762-.Ltext0
	.8byte	.LVL763-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL765-.Ltext0
	.8byte	.LVL766-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL773-.Ltext0
	.8byte	.LVL774-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST330:
	.8byte	.LVL1646-.Ltext0
	.8byte	.LVL1647-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1649-.Ltext0
	.8byte	.LVL1661-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1662-.Ltext0
	.8byte	.LFE213-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST50:
	.8byte	.LVL157-.Ltext0
	.8byte	.LVL158-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL158-.Ltext0
	.8byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST171:
	.8byte	.LVL734-.Ltext0
	.8byte	.LVL739-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL740-.Ltext0
	.8byte	.LVL751-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL753-.Ltext0
	.8byte	.LFE209-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST172:
	.8byte	.LVL734-.Ltext0
	.8byte	.LVL737-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL738-.Ltext0
	.8byte	.LVL741-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL748-.Ltext0
	.8byte	.LVL750-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL753-.Ltext0
	.8byte	.LFE209-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST173:
	.8byte	.LVL734-.Ltext0
	.8byte	.LVL752-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	.LVL753-.Ltext0
	.8byte	.LFE209-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	0
	.8byte	0
.LLST263:
	.8byte	.LVL1263-.Ltext0
	.8byte	.LVL1277-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST264:
	.8byte	.LVL1262-.Ltext0
	.8byte	.LVL1273-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1274-.Ltext0
	.8byte	.LVL1277-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST167:
	.8byte	.LVL718-.Ltext0
	.8byte	.LVL719-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL719-.Ltext0
	.8byte	.LVL725-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL726-.Ltext0
	.8byte	.LVL728-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL728-.Ltext0
	.8byte	.LFE206-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST168:
	.8byte	.LVL722-.Ltext0
	.8byte	.LVL723-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL726-.Ltext0
	.8byte	.LVL727-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	0
	.8byte	0
.LLST169:
	.8byte	.LVL720-.Ltext0
	.8byte	.LVL725-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL726-.Ltext0
	.8byte	.LVL728-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST170:
	.8byte	.LVL721-.Ltext0
	.8byte	.LVL723-1-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	.LVL726-.Ltext0
	.8byte	.LVL727-1-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	0
	.8byte	0
.LLST255:
	.8byte	.LVL1225-.Ltext0
	.8byte	.LVL1227-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1229-.Ltext0
	.8byte	.LVL1230-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1234-.Ltext0
	.8byte	.LVL1236-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST256:
	.8byte	.LVL1226-.Ltext0
	.8byte	.LVL1228-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL1229-.Ltext0
	.8byte	.LVL1231-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL1235-.Ltext0
	.8byte	.LVL1236-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST257:
	.8byte	.LVL1237-.Ltext0
	.8byte	.LVL1239-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1239-.Ltext0
	.8byte	.LVL1243-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1244-.Ltext0
	.8byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	0
	.8byte	0
.LLST258:
	.8byte	.LVL1237-.Ltext0
	.8byte	.LVL1238-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1238-.Ltext0
	.8byte	.LVL1243-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	.LVL1244-.Ltext0
	.8byte	.LVL1246-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	.LVL1246-.Ltext0
	.8byte	.LVL1254-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1254-.Ltext0
	.8byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	0
	.8byte	0
.LLST259:
	.8byte	.LVL1237-.Ltext0
	.8byte	.LVL1244-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL1244-.Ltext0
	.8byte	.LVL1256-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1256-.Ltext0
	.8byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST260:
	.8byte	.LVL1240-.Ltext0
	.8byte	.LVL1241-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1244-.Ltext0
	.8byte	.LVL1245-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1247-.Ltext0
	.8byte	.LVL1256-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST261:
	.8byte	.LVL1248-.Ltext0
	.8byte	.LVL1249-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1252-.Ltext0
	.8byte	.LVL1253-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST262:
	.8byte	.LVL1240-.Ltext0
	.8byte	.LVL1242-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL1244-.Ltext0
	.8byte	.LVL1256-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST249:
	.8byte	.LVL1203-.Ltext0
	.8byte	.LVL1205-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1205-.Ltext0
	.8byte	.LVL1211-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1212-.Ltext0
	.8byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	0
	.8byte	0
.LLST250:
	.8byte	.LVL1203-.Ltext0
	.8byte	.LVL1206-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1206-.Ltext0
	.8byte	.LVL1209-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1212-.Ltext0
	.8byte	.LVL1213-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1213-.Ltext0
	.8byte	.LVL1221-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1221-.Ltext0
	.8byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST251:
	.8byte	.LVL1203-.Ltext0
	.8byte	.LVL1204-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL1204-.Ltext0
	.8byte	.LVL1210-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1212-.Ltext0
	.8byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST252:
	.8byte	.LVL1207-.Ltext0
	.8byte	.LVL1208-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1212-.Ltext0
	.8byte	.LVL1214-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1215-.Ltext0
	.8byte	.LVL1223-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST253:
	.8byte	.LVL1216-.Ltext0
	.8byte	.LVL1217-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1220-.Ltext0
	.8byte	.LVL1222-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST254:
	.8byte	.LVL1207-.Ltext0
	.8byte	.LVL1209-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1212-.Ltext0
	.8byte	.LVL1223-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST152:
	.8byte	.LVL656-.Ltext0
	.8byte	.LVL660-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL661-.Ltext0
	.8byte	.LVL662-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST153:
	.8byte	.LVL656-.Ltext0
	.8byte	.LVL658-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL658-.Ltext0
	.8byte	.LVL673-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL675-.Ltext0
	.8byte	.LFE201-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST154:
	.8byte	.LVL656-.Ltext0
	.8byte	.LVL657-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL657-.Ltext0
	.8byte	.LVL674-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL675-.Ltext0
	.8byte	.LFE201-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST155:
	.8byte	.LVL663-.Ltext0
	.8byte	.LVL666-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL667-.Ltext0
	.8byte	.LVL671-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL675-.Ltext0
	.8byte	.LFE201-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST156:
	.8byte	.LVL659-.Ltext0
	.8byte	.LVL673-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL675-.Ltext0
	.8byte	.LFE201-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST157:
	.8byte	.LVL670-.Ltext0
	.8byte	.LVL672-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL676-.Ltext0
	.8byte	.LFE201-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST225:
	.8byte	.LVL1027-.Ltext0
	.8byte	.LVL1029-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1030-.Ltext0
	.8byte	.LVL1032-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1033-.Ltext0
	.8byte	.LFE200-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST46:
	.8byte	.LVL146-.Ltext0
	.8byte	.LVL148-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL149-.Ltext0
	.8byte	.LFE199-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST151:
	.8byte	.LVL653-.Ltext0
	.8byte	.LVL654-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST141:
	.8byte	.LVL617-.Ltext0
	.8byte	.LVL630-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST142:
	.8byte	.LVL617-.Ltext0
	.8byte	.LVL632-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST143:
	.8byte	.LVL617-.Ltext0
	.8byte	.LVL619-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL619-.Ltext0
	.8byte	.LVL633-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	0
	.8byte	0
.LLST144:
	.8byte	.LVL623-.Ltext0
	.8byte	.LVL624-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST145:
	.8byte	.LVL618-.Ltext0
	.8byte	.LVL626-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL627-.Ltext0
	.8byte	.LVL631-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST136:
	.8byte	.LVL594-.Ltext0
	.8byte	.LVL596-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL596-.Ltext0
	.8byte	.LVL601-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL602-.Ltext0
	.8byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST137:
	.8byte	.LVL607-.Ltext0
	.8byte	.LVL609-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL612-.Ltext0
	.8byte	.LVL615-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL616-.Ltext0
	.8byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST138:
	.8byte	.LVL606-.Ltext0
	.8byte	.LVL608-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL612-.Ltext0
	.8byte	.LVL613-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL614-.Ltext0
	.8byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST139:
	.8byte	.LVL595-.Ltext0
	.8byte	.LVL601-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL602-.Ltext0
	.8byte	.LVL604-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST140:
	.8byte	.LVL599-.Ltext0
	.8byte	.LVL600-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL603-.Ltext0
	.8byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST146:
	.8byte	.LVL634-.Ltext0
	.8byte	.LVL637-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL637-1-.Ltext0
	.8byte	.LVL644-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL647-.Ltext0
	.8byte	.LFE195-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST147:
	.8byte	.LVL638-.Ltext0
	.8byte	.LVL645-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL647-.Ltext0
	.8byte	.LFE195-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST148:
	.8byte	.LVL640-.Ltext0
	.8byte	.LVL644-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL647-.Ltext0
	.8byte	.LVL650-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL651-.Ltext0
	.8byte	.LFE195-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST149:
	.8byte	.LVL635-.Ltext0
	.8byte	.LVL640-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL642-.Ltext0
	.8byte	.LVL644-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST150:
	.8byte	.LVL636-.Ltext0
	.8byte	.LVL646-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	.LVL647-.Ltext0
	.8byte	.LFE195-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	0
	.8byte	0
.LLST128:
	.8byte	.LVL572-.Ltext0
	.8byte	.LVL575-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL575-.Ltext0
	.8byte	.LVL582-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL586-.Ltext0
	.8byte	.LFE193-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST129:
	.8byte	.LVL574-.Ltext0
	.8byte	.LVL584-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL586-.Ltext0
	.8byte	.LFE193-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST130:
	.8byte	.LVL573-.Ltext0
	.8byte	.LVL580-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL586-.Ltext0
	.8byte	.LVL591-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST131:
	.8byte	.LVL573-.Ltext0
	.8byte	.LVL583-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL586-.Ltext0
	.8byte	.LFE193-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST132:
	.8byte	.LVL577-.Ltext0
	.8byte	.LVL587-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL588-.Ltext0
	.8byte	.LVL592-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL593-.Ltext0
	.8byte	.LFE193-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST133:
	.8byte	.LVL577-.Ltext0
	.8byte	.LVL581-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL586-.Ltext0
	.8byte	.LVL589-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL590-.Ltext0
	.8byte	.LVL591-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST134:
	.8byte	.LVL578-.Ltext0
	.8byte	.LVL585-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL591-.Ltext0
	.8byte	.LFE193-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST135:
	.8byte	.LVL579-.Ltext0
	.8byte	.LVL585-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	.LVL591-.Ltext0
	.8byte	.LFE193-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	0
	.8byte	0
.LLST124:
	.8byte	.LVL551-.Ltext0
	.8byte	.LVL552-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL552-.Ltext0
	.8byte	.LVL562-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL563-.Ltext0
	.8byte	.LFE192-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST125:
	.8byte	.LVL551-.Ltext0
	.8byte	.LVL553-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL554-.Ltext0
	.8byte	.LVL555-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL557-.Ltext0
	.8byte	.LVL558-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL564-.Ltext0
	.8byte	.LVL565-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST126:
	.8byte	.LVL560-.Ltext0
	.8byte	.LVL561-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	.LVL567-.Ltext0
	.8byte	.LFE192-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	0
	.8byte	0
.LLST127:
	.8byte	.LVL556-.Ltext0
	.8byte	.LVL562-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL563-.Ltext0
	.8byte	.LFE192-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST38:
	.8byte	.LVL120-.Ltext0
	.8byte	.LVL121-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL121-.Ltext0
	.8byte	.LVL124-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL126-.Ltext0
	.8byte	.LFE190-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST39:
	.8byte	.LVL122-.Ltext0
	.8byte	.LVL123-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL125-.Ltext0
	.8byte	.LVL127-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST37:
	.8byte	.LVL118-.Ltext0
	.8byte	.LVL119-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST33:
	.8byte	.LVL107-.Ltext0
	.8byte	.LVL112-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL114-.Ltext0
	.8byte	.LVL117-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST34:
	.8byte	.LVL107-.Ltext0
	.8byte	.LVL111-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL114-.Ltext0
	.8byte	.LVL115-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL116-.Ltext0
	.8byte	.LFE188-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST35:
	.8byte	.LVL110-.Ltext0
	.8byte	.LVL113-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST36:
	.8byte	.LVL108-.Ltext0
	.8byte	.LVL109-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL114-.Ltext0
	.8byte	.LFE188-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST30:
	.8byte	.LVL99-.Ltext0
	.8byte	.LVL100-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL100-.Ltext0
	.8byte	.LVL105-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL106-.Ltext0
	.8byte	.LFE187-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST31:
	.8byte	.LVL99-.Ltext0
	.8byte	.LVL102-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST32:
	.8byte	.LVL101-.Ltext0
	.8byte	.LVL104-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL106-.Ltext0
	.8byte	.LFE187-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST26:
	.8byte	.LVL88-.Ltext0
	.8byte	.LVL92-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL93-.Ltext0
	.8byte	.LFE186-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST27:
	.8byte	.LVL89-.Ltext0
	.8byte	.LVL95-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	0
	.8byte	0
.LLST28:
	.8byte	.LVL91-.Ltext0
	.8byte	.LVL93-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.8byte	.LVL94-.Ltext0
	.8byte	.LFE186-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.8byte	0
	.8byte	0
.LLST113:
	.8byte	.LVL480-.Ltext0
	.8byte	.LVL485-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST114:
	.8byte	.LVL480-.Ltext0
	.8byte	.LVL485-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST19:
	.8byte	.LVL57-.Ltext0
	.8byte	.LVL59-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL60-.Ltext0
	.8byte	.LVL61-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST18:
	.8byte	.LVL53-.Ltext0
	.8byte	.LVL55-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL56-.Ltext0
	.8byte	.LFE180-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	0
	.8byte	0
.LLST123:
	.8byte	.LVL548-.Ltext0
	.8byte	.LVL549-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST90:
	.8byte	.LVL382-.Ltext0
	.8byte	.LVL385-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL391-.Ltext0
	.8byte	.LVL392-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST91:
	.8byte	.LVL382-.Ltext0
	.8byte	.LVL383-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL383-.Ltext0
	.8byte	.LVL390-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL391-.Ltext0
	.8byte	.LFE175-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST92:
	.8byte	.LVL387-.Ltext0
	.8byte	.LVL389-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL392-.Ltext0
	.8byte	.LFE175-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST93:
	.8byte	.LVL384-.Ltext0
	.8byte	.LVL388-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL392-.Ltext0
	.8byte	.LVL394-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL395-.Ltext0
	.8byte	.LFE175-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST16:
	.8byte	.LVL47-.Ltext0
	.8byte	.LVL49-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL50-.Ltext0
	.8byte	.LVL52-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST17:
	.8byte	.LVL47-.Ltext0
	.8byte	.LVL48-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL50-.Ltext0
	.8byte	.LVL51-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST15:
	.8byte	.LVL45-.Ltext0
	.8byte	.LVL46-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL46-.Ltext0
	.8byte	.LFE173-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST14:
	.8byte	.LVL43-.Ltext0
	.8byte	.LVL44-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL44-.Ltext0
	.8byte	.LFE172-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST13:
	.8byte	.LVL41-.Ltext0
	.8byte	.LVL42-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST247:
	.8byte	.LVL1173-.Ltext0
	.8byte	.LVL1202-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST248:
	.8byte	.LVL1176-.Ltext0
	.8byte	.LVL1180-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1181-.Ltext0
	.8byte	.LVL1190-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1191-.Ltext0
	.8byte	.LVL1202-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST87:
	.8byte	.LVL370-.Ltext0
	.8byte	.LVL373-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL374-.Ltext0
	.8byte	.LFE169-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST88:
	.8byte	.LVL371-.Ltext0
	.8byte	.LVL372-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL374-.Ltext0
	.8byte	.LVL375-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL376-.Ltext0
	.8byte	.LVL377-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL378-.Ltext0
	.8byte	.LFE169-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST89:
	.8byte	.LVL379-.Ltext0
	.8byte	.LVL380-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST244:
	.8byte	.LVL1160-.Ltext0
	.8byte	.LVL1162-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL1170-.Ltext0
	.8byte	.LVL1172-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST245:
	.8byte	.LVL1150-.Ltext0
	.8byte	.LVL1154-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL1161-.Ltext0
	.8byte	.LVL1162-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL1163-.Ltext0
	.8byte	.LVL1164-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL1165-.Ltext0
	.8byte	.LVL1166-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL1172-.Ltext0
	.8byte	.LFE168-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST246:
	.8byte	.LVL1156-.Ltext0
	.8byte	.LVL1159-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1166-.Ltext0
	.8byte	.LVL1168-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1169-.Ltext0
	.8byte	.LVL1170-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST11:
	.8byte	.LVL34-.Ltext0
	.8byte	.LVL39-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL40-.Ltext0
	.8byte	.LFE167-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	0
	.8byte	0
.LLST12:
	.8byte	.LVL35-.Ltext0
	.8byte	.LVL37-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	.LVL38-.Ltext0
	.8byte	.LFE167-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	0
	.8byte	0
.LLST206:
	.8byte	.LVL915-.Ltext0
	.8byte	.LVL919-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL929-.Ltext0
	.8byte	.LVL931-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST207:
	.8byte	.LVL920-.Ltext0
	.8byte	.LVL929-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL931-.Ltext0
	.8byte	.LVL933-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST208:
	.8byte	.LVL917-.Ltext0
	.8byte	.LVL929-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL931-.Ltext0
	.8byte	.LVL933-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST209:
	.8byte	.LVL919-.Ltext0
	.8byte	.LVL925-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL926-.Ltext0
	.8byte	.LVL929-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL931-.Ltext0
	.8byte	.LVL932-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST121:
	.8byte	.LVL541-.Ltext0
	.8byte	.LVL542-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL542-.Ltext0
	.8byte	.LVL546-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST122:
	.8byte	.LVL541-.Ltext0
	.8byte	.LVL543-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL543-.Ltext0
	.8byte	.LVL546-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST9:
	.8byte	.LVL30-.Ltext0
	.8byte	.LVL31-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST10:
	.8byte	.LVL32-.Ltext0
	.8byte	.LVL33-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST7:
	.8byte	.LVL26-.Ltext0
	.8byte	.LVL27-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL27-.Ltext0
	.8byte	.LVL29-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST8:
	.8byte	.LVL28-.Ltext0
	.8byte	.LVL29-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST327:
	.8byte	.LVL1640-.Ltext0
	.8byte	.LVL1643-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST328:
	.8byte	.LVL1640-.Ltext0
	.8byte	.LVL1642-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST329:
	.8byte	.LVL1640-.Ltext0
	.8byte	.LVL1641-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST313:
	.8byte	.LVL1561-.Ltext0
	.8byte	.LVL1564-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST314:
	.8byte	.LVL1561-.Ltext0
	.8byte	.LVL1563-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST315:
	.8byte	.LVL1561-.Ltext0
	.8byte	.LVL1562-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST275:
	.8byte	.LVL1333-.Ltext0
	.8byte	.LVL1335-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1335-.Ltext0
	.8byte	.LVL1338-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1339-.Ltext0
	.8byte	.LVL1342-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1354-.Ltext0
	.8byte	.LFE157-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST276:
	.8byte	.LVL1333-.Ltext0
	.8byte	.LVL1334-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1334-.Ltext0
	.8byte	.LVL1337-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1339-.Ltext0
	.8byte	.LFE157-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST277:
	.8byte	.LVL1343-.Ltext0
	.8byte	.LVL1344-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST278:
	.8byte	.LVL1336-.Ltext0
	.8byte	.LVL1338-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1341-.Ltext0
	.8byte	.LVL1354-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST279:
	.8byte	.LVL1336-.Ltext0
	.8byte	.LVL1339-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 24
	.8byte	.LVL1345-.Ltext0
	.8byte	.LVL1350-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 24
	.8byte	.LVL1350-.Ltext0
	.8byte	.LVL1351-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1351-.Ltext0
	.8byte	.LVL1354-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 24
	.8byte	0
	.8byte	0
.LLST280:
	.8byte	.LVL1336-.Ltext0
	.8byte	.LVL1339-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 28
	.8byte	.LVL1345-.Ltext0
	.8byte	.LVL1354-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 28
	.8byte	0
	.8byte	0
.LLST316:
	.8byte	.LVL1566-.Ltext0
	.8byte	.LVL1567-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1574-.Ltext0
	.8byte	.LVL1575-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST317:
	.8byte	.LVL1566-.Ltext0
	.8byte	.LVL1568-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1568-.Ltext0
	.8byte	.LVL1571-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1571-.Ltext0
	.8byte	.LVL1574-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1574-.Ltext0
	.8byte	.LVL1639-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1639-.Ltext0
	.8byte	.LFE156-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST318:
	.8byte	.LVL1566-.Ltext0
	.8byte	.LVL1572-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL1574-.Ltext0
	.8byte	.LFE156-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST319:
	.8byte	.LVL1566-.Ltext0
	.8byte	.LVL1568-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1568-1-.Ltext0
	.8byte	.LVL1573-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	.LVL1574-.Ltext0
	.8byte	.LFE156-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	0
	.8byte	0
.LLST320:
	.8byte	.LVL1568-.Ltext0
	.8byte	.LVL1570-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1582-.Ltext0
	.8byte	.LVL1597-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1605-.Ltext0
	.8byte	.LVL1606-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1609-.Ltext0
	.8byte	.LVL1639-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST321:
	.8byte	.LVL1568-.Ltext0
	.8byte	.LVL1570-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1576-.Ltext0
	.8byte	.LVL1639-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST322:
	.8byte	.LVL1577-.Ltext0
	.8byte	.LVL1578-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1594-.Ltext0
	.8byte	.LVL1595-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1617-.Ltext0
	.8byte	.LVL1618-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1619-.Ltext0
	.8byte	.LVL1620-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1624-.Ltext0
	.8byte	.LVL1625-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1628-.Ltext0
	.8byte	.LVL1629-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1634-.Ltext0
	.8byte	.LVL1635-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST323:
	.8byte	.LVL1568-.Ltext0
	.8byte	.LVL1574-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 124
	.8byte	.LVL1583-.Ltext0
	.8byte	.LVL1603-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 124
	.8byte	.LVL1605-.Ltext0
	.8byte	.LVL1606-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 124
	.8byte	.LVL1609-.Ltext0
	.8byte	.LVL1622-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 124
	.8byte	.LVL1622-.Ltext0
	.8byte	.LVL1623-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1623-.Ltext0
	.8byte	.LVL1639-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 124
	.8byte	0
	.8byte	0
.LLST324:
	.8byte	.LVL1568-.Ltext0
	.8byte	.LVL1573-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1583-.Ltext0
	.8byte	.LVL1592-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1593-.Ltext0
	.8byte	.LVL1603-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1605-.Ltext0
	.8byte	.LVL1606-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1609-.Ltext0
	.8byte	.LVL1611-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1614-.Ltext0
	.8byte	.LVL1615-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1636-.Ltext0
	.8byte	.LVL1639-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	0
	.8byte	0
.LLST325:
	.8byte	.LVL1568-.Ltext0
	.8byte	.LVL1572-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	.LVL1579-.Ltext0
	.8byte	.LVL1639-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	0
	.8byte	0
.LLST326:
	.8byte	.LVL1568-.Ltext0
	.8byte	.LVL1569-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1581-.Ltext0
	.8byte	.LVL1587-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1589-.Ltext0
	.8byte	.LVL1598-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1605-.Ltext0
	.8byte	.LVL1606-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1608-.Ltext0
	.8byte	.LVL1639-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST269:
	.8byte	.LVL1308-.Ltext0
	.8byte	.LVL1311-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST270:
	.8byte	.LVL1308-.Ltext0
	.8byte	.LVL1311-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST271:
	.8byte	.LVL1308-.Ltext0
	.8byte	.LVL1309-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST272:
	.8byte	.LVL1308-.Ltext0
	.8byte	.LVL1310-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1310-.Ltext0
	.8byte	.LVL1326-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST273:
	.8byte	.LVL1311-.Ltext0
	.8byte	.LVL1312-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.8byte	.LVL1312-.Ltext0
	.8byte	.LVL1319-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 60
	.8byte	.LVL1319-.Ltext0
	.8byte	.LFE154-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.8byte	0
	.8byte	0
.LLST274:
	.8byte	.LVL1311-.Ltext0
	.8byte	.LVL1314-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL1314-.Ltext0
	.8byte	.LVL1315-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL1317-.Ltext0
	.8byte	.LVL1318-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL1318-.Ltext0
	.8byte	.LVL1320-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL1321-.Ltext0
	.8byte	.LVL1323-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL1323-.Ltext0
	.8byte	.LVL1324-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL1324-.Ltext0
	.8byte	.LVL1325-1-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	.LVL1325-.Ltext0
	.8byte	.LFE154-.Ltext0
	.2byte	0x1
	.byte	0x54
	.8byte	0
	.8byte	0
.LLST299:
	.8byte	.LVL1513-.Ltext0
	.8byte	.LVL1514-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1522-.Ltext0
	.8byte	.LVL1523-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST300:
	.8byte	.LVL1513-.Ltext0
	.8byte	.LVL1517-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1522-.Ltext0
	.8byte	.LFE153-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST301:
	.8byte	.LVL1513-.Ltext0
	.8byte	.LVL1521-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL1522-.Ltext0
	.8byte	.LFE153-.Ltext0
	.2byte	0x1
	.byte	0x6b
	.8byte	0
	.8byte	0
.LLST302:
	.8byte	.LVL1513-.Ltext0
	.8byte	.LVL1515-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1515-1-.Ltext0
	.8byte	.LVL1519-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1519-.Ltext0
	.8byte	.LVL1522-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1522-.Ltext0
	.8byte	.LFE153-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST303:
	.8byte	.LVL1516-.Ltext0
	.8byte	.LVL1520-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1526-.Ltext0
	.8byte	.LVL1560-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST304:
	.8byte	.LVL1516-.Ltext0
	.8byte	.LVL1519-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL1527-.Ltext0
	.8byte	.LVL1560-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST305:
	.8byte	.LVL1534-.Ltext0
	.8byte	.LVL1535-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1548-.Ltext0
	.8byte	.LVL1549-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1551-.Ltext0
	.8byte	.LVL1552-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST306:
	.8byte	.LVL1516-.Ltext0
	.8byte	.LVL1517-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL1528-.Ltext0
	.8byte	.LVL1560-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST307:
	.8byte	.LVL1516-.Ltext0
	.8byte	.LVL1522-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 124
	.8byte	.LVL1530-.Ltext0
	.8byte	.LVL1560-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 124
	.8byte	0
	.8byte	0
.LLST308:
	.8byte	.LVL1516-.Ltext0
	.8byte	.LVL1521-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1525-.Ltext0
	.8byte	.LVL1542-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1546-.Ltext0
	.8byte	.LVL1559-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	0
	.8byte	0
.LLST309:
	.8byte	.LVL1516-.Ltext0
	.8byte	.LVL1518-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1529-.Ltext0
	.8byte	.LVL1560-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST310:
	.8byte	.LVL1540-.Ltext0
	.8byte	.LVL1541-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1553-.Ltext0
	.8byte	.LVL1554-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST311:
	.8byte	.LVL1516-.Ltext0
	.8byte	.LVL1522-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1530-.Ltext0
	.8byte	.LVL1531-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1532-.Ltext0
	.8byte	.LVL1533-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1536-.Ltext0
	.8byte	.LVL1537-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1538-.Ltext0
	.8byte	.LVL1539-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1555-.Ltext0
	.8byte	.LVL1556-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL1557-.Ltext0
	.8byte	.LVL1558-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST312:
	.8byte	.LVL1516-.Ltext0
	.8byte	.LVL1522-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	.LVL1530-.Ltext0
	.8byte	.LVL1531-1-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	.LVL1532-.Ltext0
	.8byte	.LVL1533-1-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	.LVL1534-.Ltext0
	.8byte	.LVL1537-1-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	.LVL1547-.Ltext0
	.8byte	.LVL1550-1-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	.LVL1551-.Ltext0
	.8byte	.LVL1552-.Ltext0
	.2byte	0x1
	.byte	0x55
	.8byte	0
	.8byte	0
.LLST332:
	.8byte	.LVL1697-.Ltext0
	.8byte	.LVL1698-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1714-.Ltext0
	.8byte	.LVL1716-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1717-.Ltext0
	.8byte	.LVL1718-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST333:
	.8byte	.LVL1694-.Ltext0
	.8byte	.LVL1696-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1706-.Ltext0
	.8byte	.LVL1708-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1709-.Ltext0
	.8byte	.LVL1710-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST334:
	.8byte	.LVL1693-.Ltext0
	.8byte	.LVL1695-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1703-.Ltext0
	.8byte	.LVL1704-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1705-.Ltext0
	.8byte	.LVL1706-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST106:
	.8byte	.LVL434-.Ltext0
	.8byte	.LVL436-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL436-.Ltext0
	.8byte	.LVL465-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL467-.Ltext0
	.8byte	.LFE148-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST107:
	.8byte	.LVL434-.Ltext0
	.8byte	.LVL435-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL435-.Ltext0
	.8byte	.LVL465-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL467-.Ltext0
	.8byte	.LFE148-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST108:
	.8byte	.LVL437-.Ltext0
	.8byte	.LVL439-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL440-.Ltext0
	.8byte	.LVL450-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL451-.Ltext0
	.8byte	.LVL463-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL467-.Ltext0
	.8byte	.LVL471-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL472-.Ltext0
	.8byte	.LFE148-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST109:
	.8byte	.LVL443-.Ltext0
	.8byte	.LVL456-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL461-.Ltext0
	.8byte	.LVL466-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL475-.Ltext0
	.8byte	.LFE148-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST110:
	.8byte	.LVL445-.Ltext0
	.8byte	.LVL454-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL455-.Ltext0
	.8byte	.LVL457-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL461-.Ltext0
	.8byte	.LVL464-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL476-.Ltext0
	.8byte	.LFE148-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST111:
	.8byte	.LVL444-.Ltext0
	.8byte	.LVL446-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL447-.Ltext0
	.8byte	.LVL448-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL453-.Ltext0
	.8byte	.LVL458-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL459-.Ltext0
	.8byte	.LVL460-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL461-.Ltext0
	.8byte	.LVL462-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL473-.Ltext0
	.8byte	.LVL476-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST112:
	.8byte	.LVL442-.Ltext0
	.8byte	.LVL466-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	.LVL474-.Ltext0
	.8byte	.LFE148-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	0
	.8byte	0
.LLST120:
	.8byte	.LVL534-.Ltext0
	.8byte	.LVL535-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL537-.Ltext0
	.8byte	.LVL538-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL539-.Ltext0
	.8byte	.LVL540-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST5:
	.8byte	.LVL17-.Ltext0
	.8byte	.LVL18-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL18-.Ltext0
	.8byte	.LFE144-.Ltext0
	.2byte	0x1
	.byte	0x57
	.8byte	0
	.8byte	0
.LLST4:
	.8byte	.LVL13-.Ltext0
	.8byte	.LVL14-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL15-.Ltext0
	.8byte	.LVL16-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST3:
	.8byte	.LVL8-.Ltext0
	.8byte	.LVL10-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL11-.Ltext0
	.8byte	.LFE141-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST62:
	.8byte	.LVL273-.Ltext0
	.8byte	.LVL274-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL274-1-.Ltext0
	.8byte	.LVL276-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST59:
	.8byte	.LVL193-.Ltext0
	.8byte	.LVL194-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL194-1-.Ltext0
	.8byte	.LVL262-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL263-.Ltext0
	.8byte	.LFE139-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST60:
	.8byte	.LVL266-.Ltext0
	.8byte	.LVL267-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL268-.Ltext0
	.8byte	.LVL269-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST61:
	.8byte	.LVL261-.Ltext0
	.8byte	.LVL263-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL270-.Ltext0
	.8byte	.LVL271-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL272-.Ltext0
	.8byte	.LFE139-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST0:
	.8byte	.LVL1-.Ltext0
	.8byte	.LVL4-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST1:
	.8byte	.LVL4-.Ltext0
	.8byte	.LVL5-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST2:
	.8byte	.LVL6-.Ltext0
	.8byte	.LVL7-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST6:
	.8byte	.LVL22-.Ltext0
	.8byte	.LVL23-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL24-.Ltext0
	.8byte	.LVL25-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST20:
	.8byte	.LVL62-.Ltext0
	.8byte	.LVL65-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL66-.Ltext0
	.8byte	.LVL82-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL83-.Ltext0
	.8byte	.LFE185-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST21:
	.8byte	.LVL67-.Ltext0
	.8byte	.LVL69-.Ltext0
	.2byte	0x1
	.byte	0x56
	.8byte	0
	.8byte	0
.LLST22:
	.8byte	.LVL73-.Ltext0
	.8byte	.LVL74-.Ltext0
	.2byte	0x1
	.byte	0x57
	.8byte	0
	.8byte	0
.LLST23:
	.8byte	.LVL75-.Ltext0
	.8byte	.LVL76-.Ltext0
	.2byte	0x1
	.byte	0x57
	.8byte	.LVL77-.Ltext0
	.8byte	.LVL80-.Ltext0
	.2byte	0x1
	.byte	0x57
	.8byte	.LVL81-.Ltext0
	.8byte	.LVL83-.Ltext0
	.2byte	0x1
	.byte	0x57
	.8byte	.LVL84-.Ltext0
	.8byte	.LFE185-.Ltext0
	.2byte	0x1
	.byte	0x57
	.8byte	0
	.8byte	0
.LLST24:
	.8byte	.LVL68-.Ltext0
	.8byte	.LVL71-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL72-.Ltext0
	.8byte	.LVL78-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	.LVL79-.Ltext0
	.8byte	.LVL84-.Ltext0
	.2byte	0x1
	.byte	0x53
	.8byte	0
	.8byte	0
.LLST25:
	.8byte	.LVL85-.Ltext0
	.8byte	.LVL86-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST29:
	.8byte	.LVL96-.Ltext0
	.8byte	.LVL97-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST40:
	.8byte	.LVL128-.Ltext0
	.8byte	.LVL129-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL129-.Ltext0
	.8byte	.LVL143-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST41:
	.8byte	.LVL131-.Ltext0
	.8byte	.LVL132-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST42:
	.8byte	.LVL133-.Ltext0
	.8byte	.LVL144-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST43:
	.8byte	.LVL135-.Ltext0
	.8byte	.LVL137-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL141-.Ltext0
	.8byte	.LFE191-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST44:
	.8byte	.LVL136-.Ltext0
	.8byte	.LVL138-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL141-.Ltext0
	.8byte	.LVL142-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST45:
	.8byte	.LVL130-.Ltext0
	.8byte	.LVL145-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST47:
	.8byte	.LVL150-.Ltext0
	.8byte	.LVL151-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST48:
	.8byte	.LVL152-.Ltext0
	.8byte	.LVL153-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL154-.Ltext0
	.8byte	.LVL156-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST49:
	.8byte	.LVL152-.Ltext0
	.8byte	.LVL153-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL154-.Ltext0
	.8byte	.LVL155-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST69:
	.8byte	.LVL302-.Ltext0
	.8byte	.LVL303-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL304-.Ltext0
	.8byte	.LVL305-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST103:
	.8byte	.LVL425-.Ltext0
	.8byte	.LVL426-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL426-.Ltext0
	.8byte	.LVL433-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST104:
	.8byte	.LVL425-.Ltext0
	.8byte	.LVL427-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL430-.Ltext0
	.8byte	.LVL431-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL432-.Ltext0
	.8byte	.LFE179-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST105:
	.8byte	.LVL428-.Ltext0
	.8byte	.LVL433-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST115:
	.8byte	.LVL486-.Ltext0
	.8byte	.LVL488-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL488-.Ltext0
	.8byte	.LVL492-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL493-.Ltext0
	.8byte	.LFE194-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST116:
	.8byte	.LVL487-.Ltext0
	.8byte	.LVL492-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL493-.Ltext0
	.8byte	.LVL494-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL495-.Ltext0
	.8byte	.LFE194-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST165:
	.8byte	.LVL710-.Ltext0
	.8byte	.LVL715-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -68
	.8byte	0
	.8byte	0
.LLST166:
	.8byte	.LVL716-.Ltext0
	.8byte	.LVL717-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST199:
	.8byte	.LVL891-.Ltext0
	.8byte	.LVL892-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL892-.Ltext0
	.8byte	.LVL894-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL899-.Ltext0
	.8byte	.LVL900-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST215:
	.8byte	.LVL974-.Ltext0
	.8byte	.LVL975-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL975-.Ltext0
	.8byte	.LVL979-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL981-.Ltext0
	.8byte	.LFE256-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST216:
	.8byte	.LVL976-.Ltext0
	.8byte	.LVL979-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL981-.Ltext0
	.8byte	.LVL983-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL984-.Ltext0
	.8byte	.LFE256-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST217:
	.8byte	.LVL976-.Ltext0
	.8byte	.LVL978-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL981-.Ltext0
	.8byte	.LVL991-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST218:
	.8byte	.LVL988-.Ltext0
	.8byte	.LVL989-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL990-.Ltext0
	.8byte	.LVL991-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	.LVL992-.Ltext0
	.8byte	.LFE256-.Ltext0
	.2byte	0x1
	.byte	0x64
	.8byte	0
	.8byte	0
.LLST219:
	.8byte	.LVL976-.Ltext0
	.8byte	.LVL980-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	.LVL981-.Ltext0
	.8byte	.LFE256-.Ltext0
	.2byte	0x1
	.byte	0x69
	.8byte	0
	.8byte	0
.LLST220:
	.8byte	.LVL993-.Ltext0
	.8byte	.LVL994-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL994-.Ltext0
	.8byte	.LVL999-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1001-.Ltext0
	.8byte	.LVL1002-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	.LVL1003-.Ltext0
	.8byte	.LFE240-.Ltext0
	.2byte	0x1
	.byte	0x63
	.8byte	0
	.8byte	0
.LLST222:
	.8byte	.LVL1013-.Ltext0
	.8byte	.LVL1018-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	.LVL1020-.Ltext0
	.8byte	.LVL1026-.Ltext0
	.2byte	0x1
	.byte	0x66
	.8byte	0
	.8byte	0
.LLST223:
	.8byte	.LVL1015-.Ltext0
	.8byte	.LVL1026-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 28
	.8byte	0
	.8byte	0
.LLST224:
	.8byte	.LVL1017-.Ltext0
	.8byte	.LVL1019-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	.LVL1022-.Ltext0
	.8byte	.LVL1026-.Ltext0
	.2byte	0x1
	.byte	0x68
	.8byte	0
	.8byte	0
.LLST288:
	.8byte	.LVL1422-.Ltext0
	.8byte	.LVL1424-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1424-.Ltext0
	.8byte	.LVL1437-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1440-.Ltext0
	.8byte	.LVL1449-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1451-.Ltext0
	.8byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1455-.Ltext0
	.8byte	.LVL1466-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1510-.Ltext0
	.8byte	.LVL1511-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST289:
	.8byte	.LVL1422-.Ltext0
	.8byte	.LVL1423-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST290:
	.8byte	.LVL1434-.Ltext0
	.8byte	.LVL1436-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1441-.Ltext0
	.8byte	.LVL1442-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1443-.Ltext0
	.8byte	.LVL1444-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1445-.Ltext0
	.8byte	.LVL1446-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1447-.Ltext0
	.8byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1456-.Ltext0
	.8byte	.LVL1457-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1464-.Ltext0
	.8byte	.LVL1474-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1475-.Ltext0
	.8byte	.LVL1502-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	.LVL1508-.Ltext0
	.8byte	.LVL1509-.Ltext0
	.2byte	0x1
	.byte	0x65
	.8byte	0
	.8byte	0
.LLST291:
	.8byte	.LVL1435-.Ltext0
	.8byte	.LVL1439-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1452-.Ltext0
	.8byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1458-.Ltext0
	.8byte	.LVL1459-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1461-.Ltext0
	.8byte	.LVL1462-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1465-.Ltext0
	.8byte	.LVL1495-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	.LVL1501-.Ltext0
	.8byte	.LVL1510-.Ltext0
	.2byte	0x1
	.byte	0x6c
	.8byte	0
	.8byte	0
.LLST292:
	.8byte	.LVL1435-.Ltext0
	.8byte	.LVL1440-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 92
	.8byte	.LVL1452-.Ltext0
	.8byte	.LVL1455-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 92
	.8byte	.LVL1458-.Ltext0
	.8byte	.LVL1459-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 92
	.8byte	.LVL1461-.Ltext0
	.8byte	.LVL1462-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 92
	.8byte	.LVL1465-.Ltext0
	.8byte	.LVL1494-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 92
	.8byte	.LVL1494-.Ltext0
	.8byte	.LVL1497-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1497-.Ltext0
	.8byte	.LVL1498-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1498-.Ltext0
	.8byte	.LVL1510-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 92
	.8byte	0
	.8byte	0
.LLST293:
	.8byte	.LVL1478-.Ltext0
	.8byte	.LVL1484-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	.LVL1485-.Ltext0
	.8byte	.LVL1486-.Ltext0
	.2byte	0x1
	.byte	0x51
	.8byte	0
	.8byte	0
.LLST294:
	.8byte	.LVL1435-.Ltext0
	.8byte	.LVL1437-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1452-.Ltext0
	.8byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1458-.Ltext0
	.8byte	.LVL1459-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1461-.Ltext0
	.8byte	.LVL1462-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1476-.Ltext0
	.8byte	.LVL1480-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1482-.Ltext0
	.8byte	.LVL1490-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1501-.Ltext0
	.8byte	.LVL1510-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST295:
	.8byte	.LVL1479-.Ltext0
	.8byte	.LVL1481-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	.LVL1483-.Ltext0
	.8byte	.LVL1487-.Ltext0
	.2byte	0x1
	.byte	0x52
	.8byte	0
	.8byte	0
.LLST296:
	.8byte	.LVL1435-.Ltext0
	.8byte	.LVL1438-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1452-.Ltext0
	.8byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1458-.Ltext0
	.8byte	.LVL1459-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1461-.Ltext0
	.8byte	.LVL1462-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	.LVL1465-.Ltext0
	.8byte	.LVL1510-.Ltext0
	.2byte	0x1
	.byte	0x6a
	.8byte	0
	.8byte	0
.LLST297:
	.8byte	.LVL1435-.Ltext0
	.8byte	.LVL1437-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1452-.Ltext0
	.8byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1458-.Ltext0
	.8byte	.LVL1459-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1461-.Ltext0
	.8byte	.LVL1462-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1476-.Ltext0
	.8byte	.LVL1477-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	.LVL1489-.Ltext0
	.8byte	.LVL1510-.Ltext0
	.2byte	0x1
	.byte	0x67
	.8byte	0
	.8byte	0
.LLST298:
	.8byte	.LVL1427-.Ltext0
	.8byte	.LVL1428-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1432-.Ltext0
	.8byte	.LVL1433-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
.LLST331:
	.8byte	.LVL1674-.Ltext0
	.8byte	.LVL1675-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	.LVL1683-.Ltext0
	.8byte	.LVL1684-.Ltext0
	.2byte	0x1
	.byte	0x50
	.8byte	0
	.8byte	0
	.section	.debug_aranges,"",@progbits
	.4byte	0x2c
	.2byte	0x2
	.4byte	.Ldebug_info0
	.byte	0x8
	.byte	0
	.2byte	0
	.2byte	0
	.8byte	.Ltext0
	.8byte	.Letext0-.Ltext0
	.8byte	0
	.8byte	0
	.section	.debug_ranges,"",@progbits
.Ldebug_ranges0:
	.8byte	.LBB168-.Ltext0
	.8byte	.LBE168-.Ltext0
	.8byte	.LBB171-.Ltext0
	.8byte	.LBE171-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB172-.Ltext0
	.8byte	.LBE172-.Ltext0
	.8byte	.LBB175-.Ltext0
	.8byte	.LBE175-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB178-.Ltext0
	.8byte	.LBE178-.Ltext0
	.8byte	.LBB183-.Ltext0
	.8byte	.LBE183-.Ltext0
	.8byte	.LBB184-.Ltext0
	.8byte	.LBE184-.Ltext0
	.8byte	.LBB185-.Ltext0
	.8byte	.LBE185-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB200-.Ltext0
	.8byte	.LBE200-.Ltext0
	.8byte	.LBB203-.Ltext0
	.8byte	.LBE203-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB212-.Ltext0
	.8byte	.LBE212-.Ltext0
	.8byte	.LBB215-.Ltext0
	.8byte	.LBE215-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB220-.Ltext0
	.8byte	.LBE220-.Ltext0
	.8byte	.LBB221-.Ltext0
	.8byte	.LBE221-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB235-.Ltext0
	.8byte	.LBE235-.Ltext0
	.8byte	.LBB239-.Ltext0
	.8byte	.LBE239-.Ltext0
	.8byte	.LBB240-.Ltext0
	.8byte	.LBE240-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB241-.Ltext0
	.8byte	.LBE241-.Ltext0
	.8byte	.LBB242-.Ltext0
	.8byte	.LBE242-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB247-.Ltext0
	.8byte	.LBE247-.Ltext0
	.8byte	.LBB248-.Ltext0
	.8byte	.LBE248-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB255-.Ltext0
	.8byte	.LBE255-.Ltext0
	.8byte	.LBB257-.Ltext0
	.8byte	.LBE257-.Ltext0
	.8byte	.LBB258-.Ltext0
	.8byte	.LBE258-.Ltext0
	.8byte	.LBB259-.Ltext0
	.8byte	.LBE259-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB264-.Ltext0
	.8byte	.LBE264-.Ltext0
	.8byte	.LBB265-.Ltext0
	.8byte	.LBE265-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB274-.Ltext0
	.8byte	.LBE274-.Ltext0
	.8byte	.LBB278-.Ltext0
	.8byte	.LBE278-.Ltext0
	.8byte	.LBB279-.Ltext0
	.8byte	.LBE279-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB288-.Ltext0
	.8byte	.LBE288-.Ltext0
	.8byte	.LBB289-.Ltext0
	.8byte	.LBE289-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB292-.Ltext0
	.8byte	.LBE292-.Ltext0
	.8byte	.LBB293-.Ltext0
	.8byte	.LBE293-.Ltext0
	.8byte	.LBB294-.Ltext0
	.8byte	.LBE294-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB315-.Ltext0
	.8byte	.LBE315-.Ltext0
	.8byte	.LBB322-.Ltext0
	.8byte	.LBE322-.Ltext0
	.8byte	.LBB323-.Ltext0
	.8byte	.LBE323-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB317-.Ltext0
	.8byte	.LBE317-.Ltext0
	.8byte	.LBB318-.Ltext0
	.8byte	.LBE318-.Ltext0
	.8byte	.LBB319-.Ltext0
	.8byte	.LBE319-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB325-.Ltext0
	.8byte	.LBE325-.Ltext0
	.8byte	.LBB326-.Ltext0
	.8byte	.LBE326-.Ltext0
	.8byte	0
	.8byte	0
	.8byte	.LBB332-.Ltext0
	.8byte	.LBE332-.Ltext0
	.8byte	.LBB333-.Ltext0
	.8byte	.LBE333-.Ltext0
	.8byte	0
	.8byte	0
	.section	.debug_line,"",@progbits
.Ldebug_line0:
	.section	.debug_str,"MS",@progbits,1
.LASF467:
	.string	"recovery_flash_mode"
.LASF43:
	.string	"root_mountflags"
.LASF397:
	.string	"error_phy_page"
.LASF335:
	.string	"gc_ink_free_return_value"
.LASF168:
	.string	"available_pages"
.LASF463:
	.string	"mlc_detected_active_page"
.LASF385:
	.string	"spperBlk"
.LASF359:
	.string	"FlashReadPages"
.LASF186:
	.string	"c_ftl_nand_planes_num"
.LASF1:
	.string	"sizetype"
.LASF589:
	.string	"re_try_next_blk"
.LASF19:
	.string	"saved_command_line"
.LASF584:
	.string	"P2V_block_in_plane"
.LASF340:
	.string	"ftl_memcmp"
.LASF213:
	.string	"g_GlobalSysVersion"
.LASF261:
	.string	"g_totle_vendor_block"
.LASF93:
	.string	"BlkNum"
.LASF594:
	.string	"totle_count"
.LASF485:
	.string	"lastWritePage"
.LASF442:
	.string	"ftl_check_vpc"
.LASF350:
	.string	"request"
.LASF323:
	.string	"DeviceCapacity"
.LASF555:
	.string	"List_remove_node"
.LASF132:
	.string	"PowerOnTimes"
.LASF495:
	.string	"FtlLoadEctTbl"
.LASF382:
	.string	"req_read_temp"
.LASF66:
	.string	"plane_per_die"
.LASF55:
	.string	"uint32"
.LASF446:
	.string	"FtlSuperblockPowerLostFix"
.LASF222:
	.string	"gL2pMapInfo"
.LASF498:
	.string	"pMapHeader"
.LASF357:
	.string	"__func__"
.LASF474:
	.string	"prev_ppa_ver"
.LASF83:
	.string	"prev_id"
.LASF100:
	.string	"dump_write_en"
.LASF145:
	.string	"max_erase_count"
.LASF225:
	.string	"req_read"
.LASF392:
	.string	"FtlGcScanTempBlk"
.LASF401:
	.string	"BOPS_EN"
.LASF418:
	.string	"get_new_active_ppa"
.LASF320:
	.string	"g_nand_phy_info"
.LASF400:
	.string	"FtlGcFreeTempBlock"
.LASF248:
	.string	"g_totle_swl_count"
.LASF560:
	.string	"tmp_id"
.LASF91:
	.string	"sign"
.LASF229:
	.string	"req_gc_dst"
.LASF439:
	.string	"FtlSysBlkInit"
.LASF464:
	.string	"lookup_superblock_id"
.LASF138:
	.string	"gc_page_count"
.LASF563:
	.string	"tmp_data_count"
.LASF187:
	.string	"c_ftl_nand_blk_pre_plane"
.LASF72:
	.string	"sec_per_blk"
.LASF366:
	.string	"sftl_gc"
.LASF379:
	.string	"get_new_gc_superblock"
.LASF559:
	.string	"tmp_erase_count"
.LASF421:
	.string	"allocate_new_data_superblock"
.LASF378:
	.string	"gc_next_superblock"
.LASF18:
	.string	"boot_command_line"
.LASF530:
	.string	"update_map_block"
.LASF603:
	.string	"FtlBbmMapBadBlock"
.LASF601:
	.string	"FtlBbtInfoPrint"
.LASF52:
	.string	"hex_asc_upper"
.LASF458:
	.string	"saved_active_page"
.LASF502:
	.string	"re_save_vpndata"
.LASF532:
	.string	"region_num"
.LASF598:
	.string	"re_save_bbmdata"
.LASF618:
	.string	"first_lpa_sctidx"
.LASF16:
	.string	"__security_initcall_start"
.LASF304:
	.string	"g_totle_slc_erase_count"
.LASF592:
	.string	"FtlLoadBbt"
.LASF172:
	.string	"check_en"
.LASF218:
	.string	"g_MaxLbn"
.LASF59:
	.string	"page_addr"
.LASF394:
	.string	"totle_num"
.LASF321:
	.string	"g_nand_ops"
.LASF583:
	.string	"ver2"
.LASF211:
	.string	"c_ftl_nand_max_data_blks"
.LASF210:
	.string	"c_ftl_nand_data_op_blks_per_plane"
.LASF567:
	.string	"FtlFreeSysBlkQueueOut"
.LASF113:
	.string	"ftl_data_blk_header"
.LASF227:
	.string	"req_erase"
.LASF610:
	.string	"num_lpa"
.LASF196:
	.string	"c_ftl_nand_byte_pre_oob"
.LASF507:
	.string	"FtlEctTblFlush"
.LASF117:
	.string	"ftl_sys_blk_header"
.LASF341:
	.string	"ftl_memcpy"
.LASF297:
	.string	"g_totle_read_page_count"
.LASF233:
	.string	"p_sys_data_buf_1"
.LASF41:
	.string	"sysctl_panic_on_stackoverflow"
.LASF199:
	.string	"c_ftl_nand_map_region_num"
.LASF49:
	.string	"SYSTEM_RESTART"
.LASF122:
	.string	"activeFlashMode"
.LASF358:
	.string	"read_req"
.LASF257:
	.string	"p_vendor_block_table"
.LASF144:
	.string	"discard_page_count"
.LASF540:
	.string	"vpcTbl"
.LASF272:
	.string	"g_buffer_superblock"
.LASF10:
	.string	"long int"
.LASF631:
	.string	"create_first_buffer_superblock"
.LASF60:
	.string	"p_data"
.LASF242:
	.string	"c_gc_page_buf_num"
.LASF511:
	.string	"Index"
.LASF221:
	.string	"gSysFreeQueue"
.LASF141:
	.string	"l2p_write_count"
.LASF617:
	.string	"absolute_sector"
.LASF553:
	.string	"pHead"
.LASF63:
	.string	"nand_phy_info"
.LASF181:
	.string	"c_ftl_nand_type"
.LASF331:
	.string	"FtlUpdateVaildLpnCount"
.LASF367:
	.string	"FlashTestBlk"
.LASF667:
	.string	"__builtin_strcpy"
.LASF287:
	.string	"g_gc_bad_block_gc_index"
.LASF266:
	.string	"p_free_data_block_list_head"
.LASF97:
	.string	"pBlkVpcTbl"
.LASF578:
	.string	"high"
.LASF633:
	.string	"is_sys_blk"
.LASF260:
	.string	"p_vendor_region_ppn_table"
.LASF542:
	.string	"load_l2p_region"
.LASF528:
	.string	"flush_l2p_region"
.LASF34:
	.string	"panic_blink"
.LASF454:
	.string	"num_free_node"
.LASF354:
	.string	"block_size"
.LASF343:
	.string	"pvForm"
.LASF205:
	.string	"c_ftl_vendor_part_size"
.LASF556:
	.string	"pTmp"
.LASF386:
	.string	"FtlGcMarkBadPhyBlk"
.LASF110:
	.string	"ftl_bbt_blk_header"
.LASF440:
	.string	"ftl_scan_all_data"
.LASF348:
	.string	"spare"
.LASF566:
	.string	"INSERT_FREE_LIST"
.LASF51:
	.string	"hex_asc"
.LASF4:
	.string	"signed char"
.LASF295:
	.string	"g_totle_read_sector"
.LASF62:
	.string	"nand_req"
.LASF259:
	.string	"p_vendor_block_ver_table"
.LASF319:
	.string	"gNandPhyInfo"
.LASF480:
	.string	"FtlL2PDataInit"
.LASF519:
	.string	"action"
.LASF402:
	.string	"superblock_id"
.LASF310:
	.string	"g_gc_head_data_block"
.LASF50:
	.string	"system_state"
.LASF616:
	.string	"FtlRead"
.LASF269:
	.string	"g_num_free_superblocks"
.LASF588:
	.string	"pBbtHeader"
.LASF5:
	.string	"unsigned char"
.LASF342:
	.string	"pvTo"
.LASF44:
	.string	"early_boot_irqs_disabled"
.LASF201:
	.string	"c_ftl_nand_vendor_region_num"
.LASF460:
	.string	"next_free_active_plane"
.LASF517:
	.string	"FtlVendorPartWrite"
.LASF45:
	.string	"SYSTEM_BOOTING"
.LASF116:
	.string	"regionId"
.LASF608:
	.string	"start_lpa"
.LASF307:
	.string	"g_in_gc_progress"
.LASF14:
	.string	"__con_initcall_start"
.LASF80:
	.string	"read_page"
.LASF280:
	.string	"g_gc_blk_num"
.LASF428:
	.string	"GetSwlReplaceBlock"
.LASF87:
	.string	"ftl_sys_info"
.LASF155:
	.string	"refresh_start_lpa"
.LASF456:
	.string	"FtlPowerLostRecovery"
.LASF557:
	.string	"insert_free_list"
.LASF12:
	.string	"_Bool"
.LASF457:
	.string	"FtlRecoverySuperblock"
.LASF25:
	.string	"linux_proc_banner"
.LASF426:
	.string	"get_new_id"
.LASF313:
	.string	"g_cur_erase_blk"
.LASF347:
	.string	"table"
.LASF162:
	.string	"value"
.LASF220:
	.string	"gSysInfo"
.LASF371:
	.string	"gc_page_num"
.LASF346:
	.string	"FlashGetBadBlockList"
.LASF86:
	.string	"BbtMap"
.LASF2:
	.string	"char"
.LASF645:
	.string	"FtlSysBlkNumInit"
.LASF654:
	.string	"memset"
.LASF325:
	.string	"TotleReadBufferCount1"
.LASF157:
	.string	"totle_read_sector"
.LASF327:
	.string	"TotleReadBufferCount3"
.LASF623:
	.string	"FtlSysFlush"
.LASF254:
	.string	"p_map_block_ver_table"
.LASF128:
	.string	"bufferPageOffset"
.LASF521:
	.string	"l2p_region_id"
.LASF407:
	.string	"FtlGcUpdatePage"
.LASF433:
	.string	"maxFreeBlockEraseCount"
.LASF638:
	.string	"re_test_next_page"
.LASF39:
	.string	"panic_on_io_nmi"
.LASF422:
	.string	"allocate_data_superblock"
.LASF572:
	.string	"FtlFreeSysBlkQueueIn"
.LASF488:
	.string	"pMapBlkHeader"
.LASF68:
	.string	"blk_per_die"
.LASF326:
	.string	"TotleReadBufferCount2"
.LASF374:
	.string	"num_io"
.LASF580:
	.string	"pagePreBlk"
.LASF126:
	.string	"activePageOffset"
.LASF74:
	.string	"reserved_blk"
.LASF479:
	.string	"FtlLoadMapInfo"
.LASF362:
	.string	"block_index"
.LASF647:
	.string	"FtlPrintInfo"
.LASF477:
	.string	"FtlReUsePrevPpa"
.LASF247:
	.string	"p_swl_mul_table"
.LASF561:
	.string	"insert_data_list"
.LASF641:
	.string	"FtlVariablesInit"
.LASF265:
	.string	"p_data_block_list_table"
.LASF509:
	.string	"ec_mod_count"
.LASF432:
	.string	"pNode"
.LASF301:
	.string	"g_totle_mlc_erase_count"
.LASF581:
	.string	"ftl_cmp_data_ver"
.LASF273:
	.string	"g_gc_temp_superblock"
.LASF258:
	.string	"p_vendor_block_valid_page_count"
.LASF496:
	.string	"FtlScanSysBlk"
.LASF663:
	.string	"l2p_addr_tran"
.LASF306:
	.string	"g_max_erase_count"
.LASF207:
	.string	"c_ftl_nand_init_sys_blks_per_plane"
.LASF179:
	.string	"ftl_gc_page_buffer"
.LASF230:
	.string	"g_req_cache"
.LASF644:
	.string	"FtlConstantsInit"
.LASF448:
	.string	"pages"
.LASF417:
	.string	"update_vpc_list"
.LASF656:
	.string	"rknand_print_hex"
.LASF405:
	.string	"p_gc_page"
.LASF650:
	.string	"limit"
.LASF391:
	.string	"Ftl_get_new_temp_ppa"
.LASF133:
	.string	"PowerLostTimes"
.LASF569:
	.string	"blk_Id"
.LASF290:
	.string	"g_gc_merge_free_blk_threshold"
.LASF35:
	.string	"oops_in_progress"
.LASF429:
	.string	"node_id"
.LASF131:
	.string	"cache_write_count"
.LASF256:
	.string	"g_totle_map_block"
.LASF655:
	.string	"printk"
.LASF543:
	.string	"phyAddr"
.LASF183:
	.string	"c_ftl_nand_planes_per_die"
.LASF486:
	.string	"ppnTbl"
.LASF372:
	.string	"gc_blk"
.LASF481:
	.string	"FtlLoadVonderInfo"
.LASF551:
	.string	"List_get_gc_head_node"
.LASF500:
	.string	"pSysHeader"
.LASF177:
	.string	"scr_ppa"
.LASF206:
	.string	"c_ftl_nand_sys_blks_per_plane"
.LASF571:
	.string	"FtlFreeSysBLkSort"
.LASF263:
	.string	"p_l2p_map_buf"
.LASF639:
	.string	"IsBlkInVendorPart"
.LASF595:
	.string	"FtlBbtMemInit"
.LASF241:
	.string	"gp_gc_page_buf_info"
.LASF164:
	.string	"front"
.LASF232:
	.string	"p_sys_data_buf"
.LASF90:
	.string	"index"
.LASF535:
	.string	"lastMapBlk"
.LASF620:
	.string	"last_lpa_nscts"
.LASF298:
	.string	"g_tmp_data_superblock_id"
.LASF244:
	.string	"gp_ect_tbl_info"
.LASF651:
	.string	"rknand_proc_ftlread"
.LASF539:
	.string	"ftl_free_no_use_map_blk"
.LASF661:
	.string	"/home/ldq/rk-linux/kernel-debug"
.LASF32:
	.string	"atomic_notifier_head"
.LASF381:
	.string	"FtlReadRefresh"
.LASF184:
	.string	"c_ftl_nand_blks_per_die"
.LASF562:
	.string	"node_data_count"
.LASF597:
	.string	"write_count"
.LASF238:
	.string	"p_io_spare_buf"
.LASF476:
	.string	"current_ppa_ver"
.LASF0:
	.string	"long unsigned int"
.LASF170:
	.string	"num_planes"
.LASF118:
	.string	"res32_0"
.LASF119:
	.string	"res32_1"
.LASF430:
	.string	"min_ec_id"
.LASF356:
	.string	"check"
.LASF26:
	.string	"console_printk"
.LASF666:
	.string	"strcpy"
.LASF58:
	.string	"status"
.LASF65:
	.string	"die_num"
.LASF624:
	.string	"sftl_init"
.LASF289:
	.string	"g_gc_free_blk_threshold"
.LASF29:
	.string	"kptr_restrict"
.LASF590:
	.string	"FtlLoadFactoryBbt"
.LASF404:
	.string	"scan_page_num"
.LASF146:
	.string	"min_erase_count"
.LASF182:
	.string	"c_ftl_nand_die_num"
.LASF303:
	.string	"g_min_erase_count"
.LASF478:
	.string	"ftl_sb_update_avl_pages"
.LASF102:
	.string	"region_id"
.LASF627:
	.string	"ftl_low_format"
.LASF339:
	.string	"str2"
.LASF92:
	.string	"maxRegion"
.LASF322:
	.string	"ftl_temp_buf"
.LASF158:
	.string	"progErrorCount"
.LASF520:
	.string	"ram_region_id"
.LASF332:
	.string	"check_vpc_table"
.LASF450:
	.string	"FtlGcPageRecovery"
.LASF27:
	.string	"printk_delay_msec"
.LASF490:
	.string	"ftl_get_blk_mode"
.LASF24:
	.string	"linux_banner"
.LASF658:
	.string	"sprintf"
.LASF105:
	.string	"ftl_ect_tbl_info"
.LASF537:
	.string	"ftl_map_blk_alloc_new_blk"
.LASF510:
	.string	"FtlVendorPartRead"
.LASF607:
	.string	"FtlWrite"
.LASF71:
	.string	"sec_per_page"
.LASF67:
	.string	"blk_per_plane"
.LASF398:
	.string	"ReInit"
.LASF515:
	.string	"result"
.LASF96:
	.string	"pBlkVerTbl"
.LASF447:
	.string	"req_temp"
.LASF114:
	.string	"lastPpa"
.LASF318:
	.string	"g_power_lost_recovery_flag"
.LASF599:
	.string	"FtlBbt2Bitmap"
.LASF69:
	.string	"page_per_blk"
.LASF469:
	.string	"recovery_super_page"
.LASF309:
	.string	"g_ftl_nand_free_count"
.LASF364:
	.string	"die_index"
.LASF412:
	.string	"numREQ"
.LASF75:
	.string	"ecc_bits"
.LASF629:
	.string	"sys_bad_block_cnt"
.LASF192:
	.string	"c_ftl_nand_page_pre_super_blk"
.LASF174:
	.string	"reversed"
.LASF399:
	.string	"read_super_page"
.LASF36:
	.string	"panic_timeout"
.LASF124:
	.string	"gcTempFlashMode"
.LASF292:
	.string	"g_totle_gc_page_count"
.LASF234:
	.string	"p_vendor_data_buf"
.LASF596:
	.string	"FtlBbmTblFlush"
.LASF564:
	.string	"tmp_multiplier_value"
.LASF403:
	.string	"lookup_ppa"
.LASF8:
	.string	"long long int"
.LASF302:
	.string	"g_totle_avg_erase_count"
.LASF212:
	.string	"ftl_gc_temp_power_lost_recovery_flag"
.LASF548:
	.string	"prev_valid_page_count"
.LASF384:
	.string	"FtlGcFreeBadSuperBlk"
.LASF219:
	.string	"gBbtInfo"
.LASF223:
	.string	"gVendorBlkInfo"
.LASF419:
	.string	"p_superblock"
.LASF267:
	.string	"p_data_block_list_head"
.LASF472:
	.string	"last_page_version"
.LASF228:
	.string	"req_gc"
.LASF606:
	.string	"FtlDiscard"
.LASF33:
	.string	"panic_notifier_list"
.LASF284:
	.string	"g_gc_next_blk"
.LASF383:
	.string	"add_count"
.LASF151:
	.string	"inkDie_write_and_check_en"
.LASF577:
	.string	"FtlGetLastWrittenPage"
.LASF462:
	.string	"detected_active_page"
.LASF506:
	.string	"FtlUpdateVaildLpn"
.LASF20:
	.string	"reset_devices"
.LASF154:
	.string	"refresh_enable_mode"
.LASF602:
	.string	"FtlBbmIsBadBlock"
.LASF23:
	.string	"__icache_flags"
.LASF388:
	.string	"FtlGcRefreshBlock"
.LASF85:
	.string	"fBbtBlk"
.LASF533:
	.string	"blkTbl"
.LASF377:
	.string	"p_dataHeader"
.LASF459:
	.string	"saved_active_plane"
.LASF123:
	.string	"bufferFlashMode"
.LASF111:
	.string	"count"
.LASF130:
	.string	"gcTempPageOffset"
.LASF353:
	.string	"addr"
.LASF136:
	.string	"GlobalSysVersion"
.LASF408:
	.string	"src_ppa"
.LASF554:
	.string	"List_pop_index_node"
.LASF270:
	.string	"g_num_data_superblocks"
.LASF444:
	.string	"error_flag"
.LASF660:
	.string	"drivers/rkflash/rksftl/rk_sftl.c"
.LASF308:
	.string	"g_in_swl_replace"
.LASF166:
	.string	"ftl_superblock_info"
.LASF3:
	.string	"unsigned int"
.LASF95:
	.string	"pBlkTbl"
.LASF165:
	.string	"rear"
.LASF189:
	.string	"c_ftl_nand_bbm_buf_size"
.LASF216:
	.string	"g_MaxLpn"
.LASF538:
	.string	"blk_index"
.LASF453:
	.string	"num_data_node"
.LASF47:
	.string	"SYSTEM_HALT"
.LASF288:
	.string	"g_gc_bad_block_temp_tbl"
.LASF30:
	.string	"kmsg_fops"
.LASF565:
	.string	"INSERT_DATA_LIST"
.LASF611:
	.string	"sctidx"
.LASF387:
	.string	"superBlk"
.LASF180:
	.string	"flag"
.LASF281:
	.string	"g_gc_page_offset"
.LASF637:
	.string	"test_mode"
.LASF296:
	.string	"g_totle_discard_page_count"
.LASF214:
	.string	"g_GlobalDataVersion"
.LASF648:
	.string	"debug_flag"
.LASF324:
	.string	"gFtlInitStatus"
.LASF527:
	.string	"hit_count"
.LASF79:
	.string	"prog_page"
.LASF127:
	.string	"bufferSuperblockId"
.LASF489:
	.string	"FtlLoadSysInfo"
.LASF152:
	.string	"readErrorCount"
.LASF169:
	.string	"current_plane"
.LASF396:
	.string	"page_count"
.LASF274:
	.string	"g_gc_superblock"
.LASF271:
	.string	"g_active_superblock"
.LASF514:
	.string	"offset"
.LASF249:
	.string	"p_valid_page_count_table"
.LASF194:
	.string	"c_ftl_nand_sec_pre_page_shift"
.LASF22:
	.string	"initcall_debug"
.LASF389:
	.string	"block"
.LASF349:
	.string	"FlashEraseBlocks"
.LASF82:
	.string	"page"
.LASF410:
	.string	"IsBlkInGcList"
.LASF338:
	.string	"str1"
.LASF64:
	.string	"nand_type"
.LASF612:
	.string	"nscts"
.LASF101:
	.string	"ftl_l2p_ram_map_info"
.LASF294:
	.string	"g_totle_write_sector"
.LASF443:
	.string	"prev_ppa"
.LASF77:
	.string	"get_bad_blk_list"
.LASF293:
	.string	"g_totle_write_page_count"
.LASF642:
	.string	"FtlMemInit"
.LASF300:
	.string	"g_totle_l2p_write_count"
.LASF585:
	.string	"P2V_plane"
.LASF11:
	.string	"bool"
.LASF253:
	.string	"p_map_block_valid_page_count"
.LASF336:
	.string	"check_buf"
.LASF558:
	.string	"node_erase_count"
.LASF552:
	.string	"List_pop_head_node"
.LASF142:
	.string	"slc_erase_count"
.LASF541:
	.string	"minValidPageCount"
.LASF640:
	.string	"ven_blk"
.LASF190:
	.string	"c_ftl_nand_page_pre_blk"
.LASF104:
	.string	"L2PMap"
.LASF344:
	.string	"size"
.LASF544:
	.string	"List_update_data_list"
.LASF305:
	.string	"g_totle_sys_slc_erase_count"
.LASF121:
	.string	"maxDieNum"
.LASF243:
	.string	"g_gc_num_req"
.LASF88:
	.string	"maxLogicBlk"
.LASF147:
	.string	"totle_power_on_run_times"
.LASF518:
	.string	"log2phys"
.LASF466:
	.string	"detected_error_page"
.LASF579:
	.string	"spareBuf"
.LASF185:
	.string	"c_ftl_nand_blks_per_die_shift"
.LASF137:
	.string	"ftl_sys_ext_info"
.LASF262:
	.string	"p_l2p_ram_map"
.LASF94:
	.string	"maxBlkNum"
.LASF657:
	.string	"ftl_malloc"
.LASF441:
	.string	"pDataHeader"
.LASF167:
	.string	"current_page"
.LASF78:
	.string	"erase_blk"
.LASF452:
	.string	"SupperBlkListInit"
.LASF630:
	.string	"create_first_active_superblock"
.LASF163:
	.string	"sys_blk_queue"
.LASF31:
	.string	"file_operations"
.LASF355:
	.string	"FlashProgPages"
.LASF516:
	.string	"ppn_index"
.LASF9:
	.string	"long long unsigned int"
.LASF217:
	.string	"g_VaildLpn"
.LASF246:
	.string	"p_erase_count_table"
.LASF525:
	.string	"select_l2p_ram_region"
.LASF435:
	.string	"GetFreeBlockMaxEraseCount"
.LASF549:
	.string	"prev_multiplier_value"
.LASF17:
	.string	"__security_initcall_end"
.LASF37:
	.string	"panic_on_oops"
.LASF264:
	.string	"g_l2p_last_update_region_id"
.LASF198:
	.string	"c_ftl_nand_totle_phy_blks"
.LASF632:
	.string	"FtlLowFormatEraseBlock"
.LASF73:
	.string	"byte_per_sec"
.LASF178:
	.string	"des_ppa"
.LASF252:
	.string	"p_map_block_table"
.LASF545:
	.string	"prev_node_id"
.LASF240:
	.string	"p_gc_data_buf"
.LASF529:
	.string	"FtlMapWritePage"
.LASF251:
	.string	"p_blk_mode_table"
.LASF282:
	.string	"g_gc_cur_blk_valid_pages"
.LASF575:
	.string	"FtlFreeSysBlkQueueEmpty"
.LASF191:
	.string	"c_ftl_nand_page_pre_slc_blk"
.LASF512:
	.string	"nSec"
.LASF593:
	.string	"FtlBbtCalcTotleCnt"
.LASF193:
	.string	"c_ftl_nand_sec_pre_page"
.LASF492:
	.string	"FtlSlcSuperblockCheck"
.LASF582:
	.string	"ver1"
.LASF150:
	.string	"SlcPartLbaEndSector"
.LASF203:
	.string	"c_ftl_nand_max_map_blks"
.LASF494:
	.string	"FtlGcReFreshBadBlk"
.LASF473:
	.string	"last_mlc_page_version"
.LASF505:
	.string	"FtlMapBlkWriteDump_data"
.LASF245:
	.string	"g_ect_tbl_info_size"
.LASF107:
	.string	"totleEc"
.LASF437:
	.string	"GetFreeBlockMinEraseCount"
.LASF434:
	.string	"minDataBlockEraseCount"
.LASF48:
	.string	"SYSTEM_POWER_OFF"
.LASF215:
	.string	"g_MaxLbaSector"
.LASF84:
	.string	"version"
.LASF28:
	.string	"dmesg_restrict"
.LASF461:
	.string	"next_free_active_page"
.LASF129:
	.string	"gcTempSuperblockId"
.LASF619:
	.string	"first_lpa_nscts"
.LASF299:
	.string	"g_totle_cache_write_count"
.LASF493:
	.string	"ftl_set_blk_mode"
.LASF449:
	.string	"Ftl_load_ext_data"
.LASF503:
	.string	"FtlWriteDump_data"
.LASF634:
	.string	"max_test_page_num"
.LASF390:
	.string	"Ftl_gc_temp_data_write_back"
.LASF365:
	.string	"block_in_die"
.LASF415:
	.string	"FtlGcBufInit"
.LASF231:
	.string	"p_plane_order_table"
.LASF98:
	.string	"pMapPpnTbl"
.LASF427:
	.string	"free_data_superblock"
.LASF143:
	.string	"sys_slc_erase_count"
.LASF204:
	.string	"c_ftl_nand_max_vendor_blks"
.LASF375:
	.string	"physical_block"
.LASF445:
	.string	"FtlVpcCheckAndModify"
.LASF276:
	.string	"g_sys_save_data"
.LASF81:
	.string	"ftl_bbt_info"
.LASF425:
	.string	"free_blk_index"
.LASF250:
	.string	"p_valid_page_count_check_table"
.LASF209:
	.string	"c_ftl_nand_data_blks_per_plane"
.LASF368:
	.string	"rk_ftl_garbage_collect"
.LASF653:
	.string	"memcmp"
.LASF370:
	.string	"numPages"
.LASF311:
	.string	"g_gc_head_data_block_count"
.LASF255:
	.string	"p_map_region_ppn_table"
.LASF524:
	.string	"l2p_flush"
.LASF455:
	.string	"make_superblock"
.LASF15:
	.string	"__con_initcall_end"
.LASF628:
	.string	"bad_block_cnt"
.LASF547:
	.string	"node_multiplier_value"
.LASF330:
	.string	"g_ect_tbl_power_up_flush"
.LASF316:
	.string	"g_recovery_ppa_tbl"
.LASF523:
	.string	"found_lpa"
.LASF277:
	.string	"g_sys_ext_data"
.LASF351:
	.string	"flash_type"
.LASF413:
	.string	"req_num"
.LASF568:
	.string	"remalloc"
.LASF508:
	.string	"forceFlush"
.LASF53:
	.string	"uint8"
.LASF329:
	.string	"power_up_flag"
.LASF279:
	.string	"p_gc_blk_tbl"
.LASF411:
	.string	"FtlGcBufAlloc"
.LASF188:
	.string	"c_ftl_nand_ext_blk_pre_plane"
.LASF376:
	.string	"max_gc_page_num"
.LASF195:
	.string	"c_ftl_nand_byte_pre_page"
.LASF438:
	.string	"update_multiplier_value"
.LASF615:
	.string	"flashType"
.LASF6:
	.string	"short int"
.LASF239:
	.string	"p_gc_spare_buf"
.LASF491:
	.string	"sblk"
.LASF369:
	.string	"mode"
.LASF423:
	.string	"new_id"
.LASF625:
	.string	"pNand"
.LASF46:
	.string	"SYSTEM_RUNNING"
.LASF513:
	.string	"pBuf"
.LASF139:
	.string	"write_page_count"
.LASF600:
	.string	"bitmap"
.LASF197:
	.string	"c_ftl_nand_reserved_blks"
.LASF664:
	.string	"sftl_get_density"
.LASF636:
	.string	"test_page_step"
.LASF591:
	.string	"blkAddr"
.LASF283:
	.string	"g_gc_cur_blk_max_valid_pages"
.LASF285:
	.string	"g_gc_next_blk_1"
.LASF526:
	.string	"target_region"
.LASF609:
	.string	"end_lpa"
.LASF470:
	.string	"scan_completed"
.LASF652:
	.string	"FtlPrintInfo2buf"
.LASF361:
	.string	"p_die"
.LASF236:
	.string	"p_io_data_buf_0"
.LASF237:
	.string	"p_io_data_buf_1"
.LASF134:
	.string	"mlc_erase_count"
.LASF175:
	.string	"phyBlk"
.LASF622:
	.string	"sftl_deinit"
.LASF483:
	.string	"pMapBlockInfo"
.LASF148:
	.string	"last_refresh_data_times"
.LASF662:
	.string	"system_states"
.LASF156:
	.string	"totle_write_sector"
.LASF226:
	.string	"req_prgm"
.LASF587:
	.string	"FtlMakeBbt"
.LASF99:
	.string	"in_gc_mode"
.LASF431:
	.string	"min_ec"
.LASF57:
	.string	"int16"
.LASF278:
	.string	"p_gc_page_info"
.LASF208:
	.string	"c_ftl_nand_max_sys_blks"
.LASF436:
	.string	"max_ec"
.LASF235:
	.string	"p_sys_spare_buf"
.LASF665:
	.string	"memcpy"
.LASF501:
	.string	"prog_error_count"
.LASF333:
	.string	"gc_discard_updated"
.LASF409:
	.string	"prev_superblock_id"
.LASF635:
	.string	"test_page_num"
.LASF153:
	.string	"last_refresh_read_count"
.LASF475:
	.string	"lookup_ppa_ver"
.LASF487:
	.string	"totleBlkNum"
.LASF605:
	.string	"sftl_read"
.LASF574:
	.string	"FtlFreeSysBlkQueueFull"
.LASF406:
	.string	"FtlGcPageVarInit"
.LASF109:
	.string	"padding"
.LASF171:
	.string	"flash_mode"
.LASF465:
	.string	"recovery_cur_page_ver"
.LASF534:
	.string	"ftl_map_blk_gc"
.LASF604:
	.string	"sftl_write"
.LASF115:
	.string	"ftl_map_blk_header"
.LASF576:
	.string	"FtlFreeSysBlkQueueInit"
.LASF21:
	.string	"late_time_init"
.LASF643:
	.string	"mem_size"
.LASF482:
	.string	"FtlMapTblRecovery"
.LASF420:
	.string	"new_ppa"
.LASF424:
	.string	"num_bad_block"
.LASF522:
	.string	"lpn_index"
.LASF200:
	.string	"c_ftl_nand_l2pmap_ram_region_num"
.LASF659:
	.ascii	"GNU C89 6.3.1 20170404 -mlittle-endian -mgeneral-regs-only -"
	.ascii	"mpc-relative-literal-loads -march=armv8-a -mabi=lp64 -g -Os "
	.ascii	"-std=gnu90 -fno-strict-aliasing -fno-common -fno-pic -fno-as"
	.ascii	"ynchronous-unwind-tables -fno-dele"
	.string	"te-null-pointer-checks -fno-stack-protector -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-var-tracking-assignments -fno-strict-overflow -fno-merge-all-constants -fmerge-constants -fstack-check=no -fconserve-stack --param allow-store-data-races=0"
.LASF621:
	.string	"sector"
.LASF504:
	.string	"num_page"
.LASF614:
	.string	"FtlProgPages"
.LASF414:
	.string	"FtlGcBufFree"
.LASF268:
	.string	"p_data_block_list_tail"
.LASF395:
	.string	"current_ppa"
.LASF61:
	.string	"p_spare"
.LASF380:
	.string	"tmp_blk"
.LASF291:
	.string	"g_gc_blk_index"
.LASF497:
	.string	"block_in_plane"
.LASF108:
	.string	"lastEc"
.LASF360:
	.string	"PhyBlk"
.LASF103:
	.string	"reserved"
.LASF314:
	.string	"g_recovery_page_num"
.LASF546:
	.string	"node_valid_page_count"
.LASF176:
	.string	"ftl_gc_page_item"
.LASF140:
	.string	"read_page_count"
.LASF416:
	.string	"decrement_vpc_count"
.LASF7:
	.string	"short unsigned int"
.LASF536:
	.string	"minValidPageIndex"
.LASF328:
	.string	"TotleReadBufferMatchCount"
.LASF337:
	.string	"check_spare_buf"
.LASF312:
	.string	"g_gc_skip_write_count"
.LASF56:
	.string	"int32"
.LASF70:
	.string	"page_per_slc_blk"
.LASF315:
	.string	"g_recovery_page_min_ver"
.LASF224:
	.string	"req_sys"
.LASF159:
	.string	"List_Node"
.LASF649:
	.string	"Ftl_log2"
.LASF149:
	.string	"all_blk_used_slc_mode"
.LASF393:
	.string	"pSuperblock"
.LASF471:
	.string	"function_exit"
.LASF202:
	.string	"c_ftl_nand_map_blks_per_plane"
.LASF13:
	.string	"initcall_t"
.LASF586:
	.string	"V2P_block"
.LASF112:
	.string	"sysBlksPerPlane"
.LASF451:
	.string	"Ftl_save_ext_data"
.LASF613:
	.string	"FtlCacheWriteBack"
.LASF76:
	.string	"nand_ops"
.LASF626:
	.string	"load_bbt"
.LASF646:
	.string	"blkNum"
.LASF363:
	.string	"page_index"
.LASF499:
	.string	"FtlVpcTblFlush"
.LASF106:
	.string	"hash"
.LASF570:
	.string	"IsInFreeQueue"
.LASF468:
	.string	"read_super_page_end"
.LASF352:
	.string	"num_req"
.LASF317:
	.string	"c_mlc_erase_count_value"
.LASF550:
	.string	"pPrev"
.LASF135:
	.string	"GlobalDataVersion"
.LASF38:
	.string	"panic_on_unrecovered_nmi"
.LASF160:
	.string	"next"
.LASF125:
	.string	"activeSuperblockId"
.LASF373:
	.string	"plane"
.LASF40:
	.string	"panic_on_warn"
.LASF89:
	.string	"ftl_l2p_map_info"
.LASF161:
	.string	"prev"
.LASF54:
	.string	"uint16"
.LASF345:
	.string	"ftl_memset"
.LASF531:
	.string	"Ftl_write_map_blk_to_last_page"
.LASF42:
	.string	"crash_kexec_post_notifiers"
.LASF286:
	.string	"g_gc_bad_block_temp_num"
.LASF120:
	.string	"ftl_sys_save_info"
.LASF275:
	.string	"gp_last_act_superblock"
.LASF573:
	.string	"erase_flag"
.LASF484:
	.string	"blk_id"
.LASF173:
	.string	"dump_writed"
.LASF334:
	.string	"ftl_gc_temp_block_bops_scan_page_addr"
